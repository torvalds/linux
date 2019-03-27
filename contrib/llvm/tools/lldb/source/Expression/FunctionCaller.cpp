//===-- FunctionCaller.cpp ---------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

using namespace lldb_private;

//----------------------------------------------------------------------
// FunctionCaller constructor
//----------------------------------------------------------------------
FunctionCaller::FunctionCaller(ExecutionContextScope &exe_scope,
                               const CompilerType &return_type,
                               const Address &functionAddress,
                               const ValueList &arg_value_list,
                               const char *name)
    : Expression(exe_scope), m_execution_unit_sp(), m_parser(),
      m_jit_module_wp(), m_name(name ? name : "<unknown>"),
      m_function_ptr(NULL), m_function_addr(functionAddress),
      m_function_return_type(return_type),
      m_wrapper_function_name("__lldb_caller_function"),
      m_wrapper_struct_name("__lldb_caller_struct"), m_wrapper_args_addrs(),
      m_struct_valid(false), m_arg_values(arg_value_list), m_compiled(false),
      m_JITted(false) {
  m_jit_process_wp = lldb::ProcessWP(exe_scope.CalculateProcess());
  // Can't make a FunctionCaller without a process.
  assert(m_jit_process_wp.lock());
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
FunctionCaller::~FunctionCaller() {
  lldb::ProcessSP process_sp(m_jit_process_wp.lock());
  if (process_sp) {
    lldb::ModuleSP jit_module_sp(m_jit_module_wp.lock());
    if (jit_module_sp)
      process_sp->GetTarget().GetImages().Remove(jit_module_sp);
  }
}

bool FunctionCaller::WriteFunctionWrapper(
    ExecutionContext &exe_ctx, DiagnosticManager &diagnostic_manager) {
  Process *process = exe_ctx.GetProcessPtr();

  if (!process)
    return false;

  lldb::ProcessSP jit_process_sp(m_jit_process_wp.lock());

  if (process != jit_process_sp.get())
    return false;

  if (!m_compiled)
    return false;

  if (m_JITted)
    return true;

  bool can_interpret = false; // should stay that way

  Status jit_error(m_parser->PrepareForExecution(
      m_jit_start_addr, m_jit_end_addr, m_execution_unit_sp, exe_ctx,
      can_interpret, eExecutionPolicyAlways));

  if (!jit_error.Success()) {
    diagnostic_manager.Printf(eDiagnosticSeverityError,
                              "Error in PrepareForExecution: %s.",
                              jit_error.AsCString());
    return false;
  }

  if (m_parser->GetGenerateDebugInfo()) {
    lldb::ModuleSP jit_module_sp(m_execution_unit_sp->GetJITModule());

    if (jit_module_sp) {
      ConstString const_func_name(FunctionName());
      FileSpec jit_file;
      jit_file.GetFilename() = const_func_name;
      jit_module_sp->SetFileSpecAndObjectName(jit_file, ConstString());
      m_jit_module_wp = jit_module_sp;
      process->GetTarget().GetImages().Append(jit_module_sp);
    }
  }
  if (process && m_jit_start_addr)
    m_jit_process_wp = process->shared_from_this();

  m_JITted = true;

  return true;
}

bool FunctionCaller::WriteFunctionArguments(
    ExecutionContext &exe_ctx, lldb::addr_t &args_addr_ref,
    DiagnosticManager &diagnostic_manager) {
  return WriteFunctionArguments(exe_ctx, args_addr_ref, m_arg_values,
                                diagnostic_manager);
}

// FIXME: Assure that the ValueList we were passed in is consistent with the one
// that defined this function.

bool FunctionCaller::WriteFunctionArguments(
    ExecutionContext &exe_ctx, lldb::addr_t &args_addr_ref,
    ValueList &arg_values, DiagnosticManager &diagnostic_manager) {
  // All the information to reconstruct the struct is provided by the
  // StructExtractor.
  if (!m_struct_valid) {
    diagnostic_manager.PutString(eDiagnosticSeverityError,
                                 "Argument information was not correctly "
                                 "parsed, so the function cannot be called.");
    return false;
  }

  Status error;
  lldb::ExpressionResults return_value = lldb::eExpressionSetupError;

  Process *process = exe_ctx.GetProcessPtr();

  if (process == NULL)
    return return_value;

  lldb::ProcessSP jit_process_sp(m_jit_process_wp.lock());

  if (process != jit_process_sp.get())
    return false;

  if (args_addr_ref == LLDB_INVALID_ADDRESS) {
    args_addr_ref = process->AllocateMemory(
        m_struct_size, lldb::ePermissionsReadable | lldb::ePermissionsWritable,
        error);
    if (args_addr_ref == LLDB_INVALID_ADDRESS)
      return false;
    m_wrapper_args_addrs.push_back(args_addr_ref);
  } else {
    // Make sure this is an address that we've already handed out.
    if (find(m_wrapper_args_addrs.begin(), m_wrapper_args_addrs.end(),
             args_addr_ref) == m_wrapper_args_addrs.end()) {
      return false;
    }
  }

  // TODO: verify fun_addr needs to be a callable address
  Scalar fun_addr(
      m_function_addr.GetCallableLoadAddress(exe_ctx.GetTargetPtr()));
  uint64_t first_offset = m_member_offsets[0];
  process->WriteScalarToMemory(args_addr_ref + first_offset, fun_addr,
                               process->GetAddressByteSize(), error);

  // FIXME: We will need to extend this for Variadic functions.

  Status value_error;

  size_t num_args = arg_values.GetSize();
  if (num_args != m_arg_values.GetSize()) {
    diagnostic_manager.Printf(
        eDiagnosticSeverityError,
        "Wrong number of arguments - was: %" PRIu64 " should be: %" PRIu64 "",
        (uint64_t)num_args, (uint64_t)m_arg_values.GetSize());
    return false;
  }

  for (size_t i = 0; i < num_args; i++) {
    // FIXME: We should sanity check sizes.

    uint64_t offset = m_member_offsets[i + 1]; // Clang sizes are in bytes.
    Value *arg_value = arg_values.GetValueAtIndex(i);

    // FIXME: For now just do scalars:

    // Special case: if it's a pointer, don't do anything (the ABI supports
    // passing cstrings)

    if (arg_value->GetValueType() == Value::eValueTypeHostAddress &&
        arg_value->GetContextType() == Value::eContextTypeInvalid &&
        arg_value->GetCompilerType().IsPointerType())
      continue;

    const Scalar &arg_scalar = arg_value->ResolveValue(&exe_ctx);

    if (!process->WriteScalarToMemory(args_addr_ref + offset, arg_scalar,
                                      arg_scalar.GetByteSize(), error))
      return false;
  }

  return true;
}

bool FunctionCaller::InsertFunction(ExecutionContext &exe_ctx,
                                    lldb::addr_t &args_addr_ref,
                                    DiagnosticManager &diagnostic_manager) {
  if (CompileFunction(exe_ctx.GetThreadSP(), diagnostic_manager) != 0)
    return false;
  if (!WriteFunctionWrapper(exe_ctx, diagnostic_manager))
    return false;
  if (!WriteFunctionArguments(exe_ctx, args_addr_ref, diagnostic_manager))
    return false;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  if (log)
    log->Printf("Call Address: 0x%" PRIx64 " Struct Address: 0x%" PRIx64 ".\n",
                m_jit_start_addr, args_addr_ref);

  return true;
}

lldb::ThreadPlanSP FunctionCaller::GetThreadPlanToCallFunction(
    ExecutionContext &exe_ctx, lldb::addr_t args_addr,
    const EvaluateExpressionOptions &options,
    DiagnosticManager &diagnostic_manager) {
  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_EXPRESSIONS |
                                                  LIBLLDB_LOG_STEP));

  if (log)
    log->Printf("-- [FunctionCaller::GetThreadPlanToCallFunction] Creating "
                "thread plan to call function \"%s\" --",
                m_name.c_str());

  // FIXME: Use the errors Stream for better error reporting.
  Thread *thread = exe_ctx.GetThreadPtr();
  if (thread == NULL) {
    diagnostic_manager.PutString(
        eDiagnosticSeverityError,
        "Can't call a function without a valid thread.");
    return NULL;
  }

  // Okay, now run the function:

  Address wrapper_address(m_jit_start_addr);

  lldb::addr_t args = {args_addr};

  lldb::ThreadPlanSP new_plan_sp(new ThreadPlanCallFunction(
      *thread, wrapper_address, CompilerType(), args, options));
  new_plan_sp->SetIsMasterPlan(true);
  new_plan_sp->SetOkayToDiscard(false);
  return new_plan_sp;
}

bool FunctionCaller::FetchFunctionResults(ExecutionContext &exe_ctx,
                                          lldb::addr_t args_addr,
                                          Value &ret_value) {
  // Read the return value - it is the last field in the struct:
  // FIXME: How does clang tell us there's no return value?  We need to handle
  // that case.
  // FIXME: Create our ThreadPlanCallFunction with the return CompilerType, and
  // then use GetReturnValueObject
  // to fetch the value.  That way we can fetch any values we need.

  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_EXPRESSIONS |
                                                  LIBLLDB_LOG_STEP));

  if (log)
    log->Printf("-- [FunctionCaller::FetchFunctionResults] Fetching function "
                "results for \"%s\"--",
                m_name.c_str());

  Process *process = exe_ctx.GetProcessPtr();

  if (process == NULL)
    return false;

  lldb::ProcessSP jit_process_sp(m_jit_process_wp.lock());

  if (process != jit_process_sp.get())
    return false;

  Status error;
  ret_value.GetScalar() = process->ReadUnsignedIntegerFromMemory(
      args_addr + m_return_offset, m_return_size, 0, error);

  if (error.Fail())
    return false;

  ret_value.SetCompilerType(m_function_return_type);
  ret_value.SetValueType(Value::eValueTypeScalar);
  return true;
}

void FunctionCaller::DeallocateFunctionResults(ExecutionContext &exe_ctx,
                                               lldb::addr_t args_addr) {
  std::list<lldb::addr_t>::iterator pos;
  pos = std::find(m_wrapper_args_addrs.begin(), m_wrapper_args_addrs.end(),
                  args_addr);
  if (pos != m_wrapper_args_addrs.end())
    m_wrapper_args_addrs.erase(pos);

  exe_ctx.GetProcessRef().DeallocateMemory(args_addr);
}

lldb::ExpressionResults FunctionCaller::ExecuteFunction(
    ExecutionContext &exe_ctx, lldb::addr_t *args_addr_ptr,
    const EvaluateExpressionOptions &options,
    DiagnosticManager &diagnostic_manager, Value &results) {
  lldb::ExpressionResults return_value = lldb::eExpressionSetupError;

  // FunctionCaller::ExecuteFunction execution is always just to get the
  // result. Do make sure we ignore breakpoints, unwind on error, and don't try
  // to debug it.
  EvaluateExpressionOptions real_options = options;
  real_options.SetDebug(false);
  real_options.SetUnwindOnError(true);
  real_options.SetIgnoreBreakpoints(true);

  lldb::addr_t args_addr;

  if (args_addr_ptr != NULL)
    args_addr = *args_addr_ptr;
  else
    args_addr = LLDB_INVALID_ADDRESS;

  if (CompileFunction(exe_ctx.GetThreadSP(), diagnostic_manager) != 0)
    return lldb::eExpressionSetupError;

  if (args_addr == LLDB_INVALID_ADDRESS) {
    if (!InsertFunction(exe_ctx, args_addr, diagnostic_manager))
      return lldb::eExpressionSetupError;
  }

  Log *log(lldb_private::GetLogIfAnyCategoriesSet(LIBLLDB_LOG_EXPRESSIONS |
                                                  LIBLLDB_LOG_STEP));

  if (log)
    log->Printf(
        "== [FunctionCaller::ExecuteFunction] Executing function \"%s\" ==",
        m_name.c_str());

  lldb::ThreadPlanSP call_plan_sp = GetThreadPlanToCallFunction(
      exe_ctx, args_addr, real_options, diagnostic_manager);
  if (!call_plan_sp)
    return lldb::eExpressionSetupError;

  // We need to make sure we record the fact that we are running an expression
  // here otherwise this fact will fail to be recorded when fetching an
  // Objective-C object description
  if (exe_ctx.GetProcessPtr())
    exe_ctx.GetProcessPtr()->SetRunningUserExpression(true);

  return_value = exe_ctx.GetProcessRef().RunThreadPlan(
      exe_ctx, call_plan_sp, real_options, diagnostic_manager);

  if (log) {
    if (return_value != lldb::eExpressionCompleted) {
      log->Printf("== [FunctionCaller::ExecuteFunction] Execution of \"%s\" "
                  "completed abnormally ==",
                  m_name.c_str());
    } else {
      log->Printf("== [FunctionCaller::ExecuteFunction] Execution of \"%s\" "
                  "completed normally ==",
                  m_name.c_str());
    }
  }

  if (exe_ctx.GetProcessPtr())
    exe_ctx.GetProcessPtr()->SetRunningUserExpression(false);

  if (args_addr_ptr != NULL)
    *args_addr_ptr = args_addr;

  if (return_value != lldb::eExpressionCompleted)
    return return_value;

  FetchFunctionResults(exe_ctx, args_addr, results);

  if (args_addr_ptr == NULL)
    DeallocateFunctionResults(exe_ctx, args_addr);

  return lldb::eExpressionCompleted;
}
