//===-- ThreadPlanCallFunctionUsingABI.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlanCallFunctionUsingABI.h"
#include "lldb/Core/Address.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

//--------------------------------------------------------------------------------------------
// ThreadPlanCallFunctionUsingABI: Plan to call a single function using the ABI
// instead of JIT
//-------------------------------------------------------------------------------------------
ThreadPlanCallFunctionUsingABI::ThreadPlanCallFunctionUsingABI(
    Thread &thread, const Address &function, llvm::Type &prototype,
    llvm::Type &return_type, llvm::ArrayRef<ABI::CallArgument> args,
    const EvaluateExpressionOptions &options)
    : ThreadPlanCallFunction(thread, function, options),
      m_return_type(return_type) {
  lldb::addr_t start_load_addr = LLDB_INVALID_ADDRESS;
  lldb::addr_t function_load_addr = LLDB_INVALID_ADDRESS;
  ABI *abi = nullptr;

  if (!ConstructorSetup(thread, abi, start_load_addr, function_load_addr))
    return;

  if (!abi->PrepareTrivialCall(thread, m_function_sp, function_load_addr,
                               start_load_addr, prototype, args))
    return;

  ReportRegisterState("ABI Function call was set up.  Register state was:");

  m_valid = true;
}

ThreadPlanCallFunctionUsingABI::~ThreadPlanCallFunctionUsingABI() = default;

void ThreadPlanCallFunctionUsingABI::GetDescription(Stream *s,
                                                    DescriptionLevel level) {
  if (level == eDescriptionLevelBrief) {
    s->Printf("Function call thread plan using ABI instead of JIT");
  } else {
    TargetSP target_sp(m_thread.CalculateTarget());
    s->Printf("Thread plan to call 0x%" PRIx64 " using ABI instead of JIT",
              m_function_addr.GetLoadAddress(target_sp.get()));
  }
}

void ThreadPlanCallFunctionUsingABI::SetReturnValue() {
  ProcessSP process_sp(m_thread.GetProcess());
  const ABI *abi = process_sp ? process_sp->GetABI().get() : nullptr;

  // Ask the abi for the return value
  if (abi) {
    const bool persistent = false;
    m_return_valobj_sp =
        abi->GetReturnValueObject(m_thread, m_return_type, persistent);
  }
}
