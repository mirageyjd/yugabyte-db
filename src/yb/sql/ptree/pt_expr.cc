//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for expressions.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context) {
  switch (op_) {
    case ExprOperator::kNoOp:
      break;
    case ExprOperator::kExists:
      break;
    case ExprOperator::kNotExists:
      break;

    default:
      LOG(FATAL) << "Invalid operator";
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1) {
  switch (op_) {
    case ExprOperator::kUMinus:
      // "op1" must have been analyzed before we get here.
      // Check to make sure that it is allowed in this context.
      if (op1->op_ != ExprOperator::kConst) {
        return sem_context->Error(loc(), "Only numeric constant is allowed in this context",
                                  ErrorCode::FEATURE_NOT_SUPPORTED);
      }
      if (!YBColumnSchema::IsNumeric(op1->sql_type())) {
        return sem_context->Error(loc(), "Only numeric data type is allowed in this context",
                                  ErrorCode::INVALID_DATATYPE);
      }

      // Type resolution: (-x) should have the same datatype as (x).
      sql_type_ = op1->sql_type();
      type_id_ = op1->type_id();
      break;
    case ExprOperator::kNot:
      if (op1->sql_type_ != BOOL) {
        return sem_context->Error(loc(), "Only boolean data type is allowed in this context",
            ErrorCode::INVALID_DATATYPE);
      }
      sql_type_ = yb::DataType::BOOL;
      type_id_ = yb::InternalType::kBoolValue;
      break;
    case ExprOperator::kIsNull: FALLTHROUGH_INTENDED;
    case ExprOperator::kIsNotNull: FALLTHROUGH_INTENDED;
    case ExprOperator::kIsTrue: FALLTHROUGH_INTENDED;
    case ExprOperator::kIsFalse:
      return sem_context->Error(loc(), "Operator not supported yet",
          ErrorCode::CQL_STATEMENT_INVALID);
    default:
      LOG(FATAL) << "Invalid operator" << int(op_);
  }

  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2) {
  // "op1" and "op2" must have been analyzed before getting here
  switch (op_) {
    case ExprOperator::kEQ: FALLTHROUGH_INTENDED;
    case ExprOperator::kLT: FALLTHROUGH_INTENDED;
    case ExprOperator::kGT: FALLTHROUGH_INTENDED;
    case ExprOperator::kLE: FALLTHROUGH_INTENDED;
    case ExprOperator::kGE: FALLTHROUGH_INTENDED;
    case ExprOperator::kNE:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      if (!sem_context->IsComparable(op1->sql_type(), op2->sql_type())) {
        return sem_context->Error(loc(), "Cannot compare values of these datatypes",
            ErrorCode::INCOMPARABLE_DATATYPES);
      }
      sql_type_ = yb::DataType::BOOL;
      type_id_ = yb::InternalType::kBoolValue;
      break;
    case ExprOperator::kAND: FALLTHROUGH_INTENDED;
    case ExprOperator::kOR:
      if (op1->sql_type_ != BOOL || op2->sql_type_ != BOOL) {
        return sem_context->Error(loc(), "Only boolean data type is allowed in this context",
            ErrorCode::INVALID_DATATYPE);
      }
      sql_type_ = yb::DataType::BOOL;
      type_id_ = yb::InternalType::kBoolValue;
      break;
    case ExprOperator::kLike: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotLike: FALLTHROUGH_INTENDED;
    case ExprOperator::kIn: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotIn:
      return sem_context->Error(loc(), "Operator not supported yet",
          ErrorCode::CQL_STATEMENT_INVALID);
    default:
      LOG(FATAL) << "Invalid operator";
  }

  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2,
                                       PTExpr::SharedPtr op3) {
  // "op1", "op2", and "op3" must have been analyzed before getting here
  switch (op_) {
    case ExprOperator::kBetween: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotBetween:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(op3->CheckRhsExpr(sem_context));
      if (!sem_context->IsComparable(op1->sql_type(), op2->sql_type()) ||
          !sem_context->IsComparable(op1->sql_type(), op3->sql_type())) {
        return sem_context->Error(loc(), "Cannot compare values of these datatypes",
            ErrorCode::INCOMPARABLE_DATATYPES);
      }
      sql_type_ = yb::DataType::BOOL;
      type_id_ = yb::InternalType::kBoolValue;
      break;
    default:
      LOG(FATAL) << "Invalid operator";
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTExpr::CheckLhsExpr(SemContext *sem_context) {
  if (op_ != ExprOperator::kRef) {
    return sem_context->Error(loc(), "Only column reference is allowed for left hand value",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::CheckRhsExpr(SemContext *sem_context) {
  // Check for limitation in YQL (Not all expressions are acceptable).
  switch (op_) {
    case ExprOperator::kConst: FALLTHROUGH_INTENDED;
    case ExprOperator::kUMinus:
      break;
    default:
      return sem_context->Error(loc(), "Only literal value is allowed for right hand value",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTRef::PTRef(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTQualifiedName::SharedPtr& name)
    : PTOperator0(memctx, loc, ExprOperator::kRef),
      name_(name),
      desc_(nullptr) {
}

PTRef::~PTRef() {
}

CHECKED_STATUS PTRef::AnalyzeOperator(SemContext *sem_context) {

  // Check if this refers to the whole table (SELECT *).
  if (name_ == nullptr) {
    return sem_context->Error(loc(), "Cannot do type resolution for wildcard reference (SELECT *)");
  }

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  desc_ = sem_context->GetColumnDesc(name_->last_name());
  if (desc_ == nullptr) {
    return sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  // Type resolution: Ref(x) should have the same datatype as (x).
  type_id_ = desc_->type_id();
  sql_type_ = desc_->sql_type();
  return Status::OK();
}

void PTRef::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTExprAlias::PTExprAlias(MemoryContext *memctx,
                         YBLocation::SharedPtr loc,
                         const PTExpr::SharedPtr& expr,
                         const MCString::SharedPtr& alias)
    : PTOperator1(memctx, loc, ExprOperator::kAlias, expr),
      alias_(alias) {
}

PTExprAlias::~PTExprAlias() {
}

CHECKED_STATUS PTExprAlias::AnalyzeOperator(SemContext *sem_context, PTExpr::SharedPtr op1) {
  // Type resolution: Alias of (x) should have the same datatype as (x).
  sql_type_ = op1->sql_type();
  type_id_ = op1->type_id();

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
