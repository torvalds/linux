//===-- UserExpression.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <cstdlib>
#include <map>
#include <string>

#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/ExpressionSourceCode.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Expression/IRInterpreter.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanCallUserExpression.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;

UserExpression::UserExpression(ExecutionContextScope &exe_scope,
                               llvm::StringRef expr, llvm::StringRef prefix,
                               lldb::LanguageType language,
                               ResultType desired_type,
                               const EvaluateExpressionOptions &options)
    : Expression(exe_scope), m_expr_text(expr), m_expr_prefix(prefix),
      m_language(language), m_desired_type(desired_type), m_options(options) {}

UserExpression::~UserExpression() {}

void UserExpression::InstallContext(ExecutionContext &exe_ctx) {
  m_jit_process_wp = exe_ctx.GetProcessSP();

  lldb::StackFrameSP frame_sp = exe_ctx.GetFrameSP();

  if (frame_sp)
    m_address = frame_sp->GetFrameCodeAddress();
}

bool UserExpression::LockAndCheckContext(ExecutionContext &exe_ctx,
                                         lldb::TargetSP &target_sp,
                                         lldb::ProcessSP &process_sp,
                                         lldb::StackFrameSP &frame_sp) {
  lldb::ProcessSP expected_process_sp = m_jit_process_wp.lock();
  process_sp = exe_ctx.GetProcessSP();

  if (process_sp != expected_process_sp)
    return false;

  process_sp = exe_ctx.GetProcessSP();
  target_sp = exe_ctx.GetTargetSP();
  frame_sp = exe_ctx.GetFrameSP();

  if (m_address.IsValid()) {
    if (!frame_sp)
      return false;
    else
      return (0 == Address::CompareLoadAddress(m_address,
                                               frame_sp->GetFrameCodeAddress(),
                                               target_sp.get()));
  }

  return true;
}

bool UserExpression::MatchesContext(ExecutionContext &exe_ctx) {
  lldb::TargetSP target_sp;
  lldb::ProcessSP process_sp;
  lldb::StackFrameSP frame_sp;

  return LockAndCheckContext(exe_ctx, target_sp, process_sp, frame_sp);
}

lldb::addr_t UserExpression::GetObjectPointer(lldb::StackFrameSP frame_sp,
                                              ConstString &object_name,
                                              Status &err) {
  err.Clear();

  if (!frame_sp) {
    err.SetErrorStringWithFormat(
        "Couldn't load '%s' because the context is incomplete",
        object_name.AsCString());
    return LLDB_INVALID_ADDRESS;
  }

  lldb::VariableSP var_sp;
  lldb::ValueObjectSP valobj_sp;

  valobj_sp = frame_sp->GetValueForVariableExpressionPath(
      object_name.AsCString(), lldb::eNoDynamicValues,
      StackFrame::eExpressionPathOptionCheckPtrVsMember |
          StackFrame::eExpressionPathOptionsNoFragileObjcIvar |
          StackFrame::eExpressionPathOptionsNoSyntheticChildren |
          StackFrame::eExpressionPathOptionsNoSyntheticArrayRange,
      var_sp, err);

  if (!err.Success() || !valobj_sp.get())
    return LLDB_INVALID_ADDRESS;

  lldb::addr_t ret = valobj_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);

  if (ret == LLDB_INVALID_ADDRESS) {
    err.SetErrorStringWithFormat(
        "Couldn't load '%s' because its value couldn't be evaluated",
        object_name.AsCString());
    return LLDB_INVALID_ADDRESS;
  }

  return ret;
}

lldb::ExpressionResults UserExpression::Evaluate(
    ExecutionContext &exe_ctx, const EvaluateExpressionOptions &options,
    llvm::StringRef expr, llvm::StringRef prefix,
    lldb::ValueObjectSP &result_valobj_sp, Status &error, uint32_t line_offset,
    std::string *fixed_expression, lldb::ModuleSP *jit_module_sp_ptr) {
  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_EXPRESSIONS |
                                                  LIBLLDB_LOG_STEP));

  lldb_private::ExecutionPolicy execution_policy = options.GetExecutionPolicy();
  lldb::LanguageType language = options.GetLanguage();
  const ResultType desired_type = options.DoesCoerceToId()
                                      ? UserExpression::eResultTypeId
                                      : UserExpression::eResultTypeAny;
  lldb::ExpressionResults execution_results = lldb::eExpressionSetupError;

  Target *target = exe_ctx.GetTargetPtr();
  if (!target) {
    if (log)
      log->Printf("== [UserExpression::Evaluate] Passed a NULL target, can't "
                  "run expressions.");
    error.SetErrorString("expression passed a null target");
    return lldb::eExpressionSetupError;
  }

  Process *process = exe_ctx.GetProcessPtr();

  if (process == NULL || process->GetState() != lldb::eStateStopped) {
    if (execution_policy == eExecutionPolicyAlways) {
      if (log)
        log->Printf("== [UserExpression::Evaluate] Expression may not run, but "
                    "is not constant ==");

      error.SetErrorString("expression needed to run but couldn't");

      return execution_results;
    }
  }

  if (process == NULL || !process->CanJIT())
    execution_policy = eExecutionPolicyNever;

  // We need to set the expression execution thread here, turns out parse can
  // call functions in the process of looking up symbols, which will escape the
  // context set by exe_ctx passed to Execute.
  lldb::ThreadSP thread_sp = exe_ctx.GetThreadSP();
  ThreadList::ExpressionExecutionThreadPusher execution_thread_pusher(
      thread_sp);

  llvm::StringRef full_prefix;
  llvm::StringRef option_prefix(options.GetPrefix());
  std::string full_prefix_storage;
  if (!prefix.empty() && !option_prefix.empty()) {
    full_prefix_storage = prefix;
    full_prefix_storage.append(option_prefix);
    full_prefix = full_prefix_storage;
  } else if (!prefix.empty())
    full_prefix = prefix;
  else
    full_prefix = option_prefix;

  // If the language was not specified in the expression command, set it to the
  // language in the target's properties if specified, else default to the
  // langage for the frame.
  if (language == lldb::eLanguageTypeUnknown) {
    if (target->GetLanguage() != lldb::eLanguageTypeUnknown)
      language = target->GetLanguage();
    else if (StackFrame *frame = exe_ctx.GetFramePtr())
      language = frame->GetLanguage();
  }

  lldb::UserExpressionSP user_expression_sp(
      target->GetUserExpressionForLanguage(expr, full_prefix, language,
                                           desired_type, options, error));
  if (error.Fail()) {
    if (log)
      log->Printf("== [UserExpression::Evaluate] Getting expression: %s ==",
                  error.AsCString());
    return lldb::eExpressionSetupError;
  }

  if (log)
    log->Printf("== [UserExpression::Evaluate] Parsing expression %s ==",
                expr.str().c_str());

  const bool keep_expression_in_memory = true;
  const bool generate_debug_info = options.GetGenerateDebugInfo();

  if (options.InvokeCancelCallback(lldb::eExpressionEvaluationParse)) {
    error.SetErrorString("expression interrupted by callback before parse");
    result_valobj_sp = ValueObjectConstResult::Create(
        exe_ctx.GetBestExecutionContextScope(), error);
    return lldb::eExpressionInterrupted;
  }

  DiagnosticManager diagnostic_manager;

  bool parse_success =
      user_expression_sp->Parse(diagnostic_manager, exe_ctx, execution_policy,
                                keep_expression_in_memory, generate_debug_info);

  // Calculate the fixed expression always, since we need it for errors.
  std::string tmp_fixed_expression;
  if (fixed_expression == nullptr)
    fixed_expression = &tmp_fixed_expression;

  const char *fixed_text = user_expression_sp->GetFixedText();
  if (fixed_text != nullptr)
    fixed_expression->append(fixed_text);

  // If there is a fixed expression, try to parse it:
  if (!parse_success) {
    execution_results = lldb::eExpressionParseError;
    if (fixed_expression && !fixed_expression->empty() &&
        options.GetAutoApplyFixIts()) {
      lldb::UserExpressionSP fixed_expression_sp(
          target->GetUserExpressionForLanguage(fixed_expression->c_str(),
                                               full_prefix, language,
                                               desired_type, options, error));
      DiagnosticManager fixed_diagnostic_manager;
      parse_success = fixed_expression_sp->Parse(
          fixed_diagnostic_manager, exe_ctx, execution_policy,
          keep_expression_in_memory, generate_debug_info);
      if (parse_success) {
        diagnostic_manager.Clear();
        user_expression_sp = fixed_expression_sp;
      } else {
        // If the fixed expression failed to parse, don't tell the user about,
        // that won't help.
        fixed_expression->clear();
      }
    }

    if (!parse_success) {
      if (!fixed_expression->empty() && target->GetEnableNotifyAboutFixIts()) {
        error.SetExpressionErrorWithFormat(
            execution_results,
            "expression failed to parse, fixed expression suggested:\n  %s",
            fixed_expression->c_str());
      } else {
        if (!diagnostic_manager.Diagnostics().size())
          error.SetExpressionError(execution_results,
                                   "expression failed to parse, unknown error");
        else
          error.SetExpressionError(execution_results,
                                   diagnostic_manager.GetString().c_str());
      }
    }
  }

  if (parse_success) {
    // If a pointer to a lldb::ModuleSP was passed in, return the JIT'ed module
    // if one was created
    if (jit_module_sp_ptr)
      *jit_module_sp_ptr = user_expression_sp->GetJITModule();

    lldb::ExpressionVariableSP expr_result;

    if (execution_policy == eExecutionPolicyNever &&
        !user_expression_sp->CanInterpret()) {
      if (log)
        log->Printf("== [UserExpression::Evaluate] Expression may not run, but "
                    "is not constant ==");

      if (!diagnostic_manager.Diagnostics().size())
        error.SetExpressionError(lldb::eExpressionSetupError,
                                 "expression needed to run but couldn't");
    } else if (execution_policy == eExecutionPolicyTopLevel) {
      error.SetError(UserExpression::kNoResult, lldb::eErrorTypeGeneric);
      return lldb::eExpressionCompleted;
    } else {
      if (options.InvokeCancelCallback(lldb::eExpressionEvaluationExecution)) {
        error.SetExpressionError(
            lldb::eExpressionInterrupted,
            "expression interrupted by callback before execution");
        result_valobj_sp = ValueObjectConstResult::Create(
            exe_ctx.GetBestExecutionContextScope(), error);
        return lldb::eExpressionInterrupted;
      }

      diagnostic_manager.Clear();

      if (log)
        log->Printf("== [UserExpression::Evaluate] Executing expression ==");

      execution_results =
          user_expression_sp->Execute(diagnostic_manager, exe_ctx, options,
                                      user_expression_sp, expr_result);

      if (execution_results != lldb::eExpressionCompleted) {
        if (log)
          log->Printf("== [UserExpression::Evaluate] Execution completed "
                      "abnormally ==");

        if (!diagnostic_manager.Diagnostics().size())
          error.SetExpressionError(
              execution_results, "expression failed to execute, unknown error");
        else
          error.SetExpressionError(execution_results,
                                   diagnostic_manager.GetString().c_str());
      } else {
        if (expr_result) {
          result_valobj_sp = expr_result->GetValueObject();

          if (log)
            log->Printf("== [UserExpression::Evaluate] Execution completed "
                        "normally with result %s ==",
                        result_valobj_sp->GetValueAsCString());
        } else {
          if (log)
            log->Printf("== [UserExpression::Evaluate] Execution completed "
                        "normally with no result ==");

          error.SetError(UserExpression::kNoResult, lldb::eErrorTypeGeneric);
        }
      }
    }
  }

  if (options.InvokeCancelCallback(lldb::eExpressionEvaluationComplete)) {
    error.SetExpressionError(
        lldb::eExpressionInterrupted,
        "expression interrupted by callback after complete");
    return lldb::eExpressionInterrupted;
  }

  if (result_valobj_sp.get() == NULL) {
    result_valobj_sp = ValueObjectConstResult::Create(
        exe_ctx.GetBestExecutionContextScope(), error);
  }

  return execution_results;
}

lldb::ExpressionResults
UserExpression::Execute(DiagnosticManager &diagnostic_manager,
                        ExecutionContext &exe_ctx,
                        const EvaluateExpressionOptions &options,
                        lldb::UserExpressionSP &shared_ptr_to_me,
                        lldb::ExpressionVariableSP &result_var) {
  lldb::ExpressionResults expr_result = DoExecute(
      diagnostic_manager, exe_ctx, options, shared_ptr_to_me, result_var);
  Target *target = exe_ctx.GetTargetPtr();
  if (options.GetResultIsInternal() && result_var && target) {
    target->GetPersistentExpressionStateForLanguage(m_language)
        ->RemovePersistentVariable(result_var);
  }
  return expr_result;
}
