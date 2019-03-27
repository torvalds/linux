//===-- MainThreadCheckerRuntime.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MainThreadCheckerRuntime.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/InstrumentationRuntimeStopInfo.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegularExpression.h"
#include "Plugins/Process/Utility/HistoryThread.h"

using namespace lldb;
using namespace lldb_private;

MainThreadCheckerRuntime::~MainThreadCheckerRuntime() {
  Deactivate();
}

lldb::InstrumentationRuntimeSP
MainThreadCheckerRuntime::CreateInstance(const lldb::ProcessSP &process_sp) {
  return InstrumentationRuntimeSP(new MainThreadCheckerRuntime(process_sp));
}

void MainThreadCheckerRuntime::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "MainThreadChecker instrumentation runtime plugin.",
      CreateInstance, GetTypeStatic);
}

void MainThreadCheckerRuntime::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString MainThreadCheckerRuntime::GetPluginNameStatic() {
  return ConstString("MainThreadChecker");
}

lldb::InstrumentationRuntimeType MainThreadCheckerRuntime::GetTypeStatic() {
  return eInstrumentationRuntimeTypeMainThreadChecker;
}

const RegularExpression &
MainThreadCheckerRuntime::GetPatternForRuntimeLibrary() {
  static RegularExpression regex(llvm::StringRef("libMainThreadChecker.dylib"));
  return regex;
}

bool MainThreadCheckerRuntime::CheckIfRuntimeIsValid(
    const lldb::ModuleSP module_sp) {
  static ConstString test_sym("__main_thread_checker_on_report");
  const Symbol *symbol =
      module_sp->FindFirstSymbolWithNameAndType(test_sym, lldb::eSymbolTypeAny);
  return symbol != nullptr;
}

StructuredData::ObjectSP
MainThreadCheckerRuntime::RetrieveReportData(ExecutionContextRef exe_ctx_ref) {
  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return StructuredData::ObjectSP();

  ThreadSP thread_sp = exe_ctx_ref.GetThreadSP();
  StackFrameSP frame_sp = thread_sp->GetSelectedFrame();
  ModuleSP runtime_module_sp = GetRuntimeModuleSP();
  Target &target = process_sp->GetTarget();

  if (!frame_sp)
    return StructuredData::ObjectSP();

  RegisterContextSP regctx_sp = frame_sp->GetRegisterContext();
  if (!regctx_sp)
    return StructuredData::ObjectSP();

  const RegisterInfo *reginfo = regctx_sp->GetRegisterInfoByName("arg1");
  if (!reginfo)
    return StructuredData::ObjectSP();

  uint64_t apiname_ptr = regctx_sp->ReadRegisterAsUnsigned(reginfo, 0);
  if (!apiname_ptr)
    return StructuredData::ObjectSP();

  std::string apiName = "";
  Status read_error;
  target.ReadCStringFromMemory(apiname_ptr, apiName, read_error);
  if (read_error.Fail())
    return StructuredData::ObjectSP();

  std::string className = "";
  std::string selector = "";
  if (apiName.substr(0, 2) == "-[") {
    size_t spacePos = apiName.find(" ");
    if (spacePos != std::string::npos) {
      className = apiName.substr(2, spacePos - 2);
      selector = apiName.substr(spacePos + 1, apiName.length() - spacePos - 2);
    }
  }

  // Gather the PCs of the user frames in the backtrace.
  StructuredData::Array *trace = new StructuredData::Array();
  auto trace_sp = StructuredData::ObjectSP(trace);
  StackFrameSP responsible_frame;
  for (unsigned I = 0; I < thread_sp->GetStackFrameCount(); ++I) {
    StackFrameSP frame = thread_sp->GetStackFrameAtIndex(I);
    Address addr = frame->GetFrameCodeAddress();
    if (addr.GetModule() == runtime_module_sp) // Skip PCs from the runtime.
      continue;

    // The first non-runtime frame is responsible for the bug.
    if (!responsible_frame)
      responsible_frame = frame;

    // First frame in stacktrace should point to a real PC, not return address.
    if (I != 0 && trace->GetSize() == 0) {
      addr.Slide(-1);
    }

    lldb::addr_t PC = addr.GetLoadAddress(&target);
    trace->AddItem(StructuredData::ObjectSP(new StructuredData::Integer(PC)));
  }

  auto *d = new StructuredData::Dictionary();
  auto dict_sp = StructuredData::ObjectSP(d);
  d->AddStringItem("instrumentation_class", "MainThreadChecker");
  d->AddStringItem("api_name", apiName);
  d->AddStringItem("class_name", className);
  d->AddStringItem("selector", selector);
  d->AddStringItem("description",
                   apiName + " must be used from main thread only");
  d->AddIntegerItem("tid", thread_sp->GetIndexID());
  d->AddItem("trace", trace_sp);
  return dict_sp;
}

bool MainThreadCheckerRuntime::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false; //< false => resume execution.

  MainThreadCheckerRuntime *const instance =
      static_cast<MainThreadCheckerRuntime *>(baton);

  ProcessSP process_sp = instance->GetProcessSP();
  ThreadSP thread_sp = context->exe_ctx_ref.GetThreadSP();
  if (!process_sp || !thread_sp ||
      process_sp != context->exe_ctx_ref.GetProcessSP())
    return false;

  if (process_sp->GetModIDRef().IsLastResumeForUserExpression())
    return false;

  StructuredData::ObjectSP report =
      instance->RetrieveReportData(context->exe_ctx_ref);

  if (report) {
    std::string description = report->GetAsDictionary()
                                ->GetValueForKey("description")
                                ->GetAsString()
                                ->GetValue();
    thread_sp->SetStopInfo(
        InstrumentationRuntimeStopInfo::CreateStopReasonWithInstrumentationData(
            *thread_sp, description, report));
    return true;
  }

  return false;
}

void MainThreadCheckerRuntime::Activate() {
  if (IsActive())
    return;

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  ModuleSP runtime_module_sp = GetRuntimeModuleSP();

  ConstString symbol_name("__main_thread_checker_on_report");
  const Symbol *symbol = runtime_module_sp->FindFirstSymbolWithNameAndType(
      symbol_name, eSymbolTypeCode);

  if (symbol == nullptr)
    return;

  if (!symbol->ValueIsAddress() || !symbol->GetAddressRef().IsValid())
    return;

  Target &target = process_sp->GetTarget();
  addr_t symbol_address = symbol->GetAddressRef().GetOpcodeLoadAddress(&target);

  if (symbol_address == LLDB_INVALID_ADDRESS)
    return;

  Breakpoint *breakpoint =
      process_sp->GetTarget()
          .CreateBreakpoint(symbol_address, /*internal=*/true,
                            /*hardware=*/false)
          .get();
  breakpoint->SetCallback(MainThreadCheckerRuntime::NotifyBreakpointHit, this,
                          true);
  breakpoint->SetBreakpointKind("main-thread-checker-report");
  SetBreakpointID(breakpoint->GetID());

  SetActive(true);
}

void MainThreadCheckerRuntime::Deactivate() {
  SetActive(false);

  auto BID = GetBreakpointID();
  if (BID == LLDB_INVALID_BREAK_ID)
    return;

  if (ProcessSP process_sp = GetProcessSP()) {
    process_sp->GetTarget().RemoveBreakpointByID(BID);
    SetBreakpointID(LLDB_INVALID_BREAK_ID);
  }
}

lldb::ThreadCollectionSP
MainThreadCheckerRuntime::GetBacktracesFromExtendedStopInfo(
    StructuredData::ObjectSP info) {
  ThreadCollectionSP threads;
  threads.reset(new ThreadCollection());
  
  ProcessSP process_sp = GetProcessSP();
  
  if (info->GetObjectForDotSeparatedPath("instrumentation_class")
      ->GetStringValue() != "MainThreadChecker")
    return threads;
  
  std::vector<lldb::addr_t> PCs;
  auto trace = info->GetObjectForDotSeparatedPath("trace")->GetAsArray();
  trace->ForEach([&PCs](StructuredData::Object *PC) -> bool {
    PCs.push_back(PC->GetAsInteger()->GetValue());
    return true;
  });
  
  if (PCs.empty())
    return threads;
  
  StructuredData::ObjectSP thread_id_obj =
      info->GetObjectForDotSeparatedPath("tid");
  tid_t tid = thread_id_obj ? thread_id_obj->GetIntegerValue() : 0;
  
  uint32_t stop_id = 0;
  bool stop_id_is_valid = false;
  HistoryThread *history_thread =
      new HistoryThread(*process_sp, tid, PCs, stop_id, stop_id_is_valid);
  ThreadSP new_thread_sp(history_thread);
  
  // Save this in the Process' ExtendedThreadList so a strong pointer retains
  // the object
  process_sp->GetExtendedThreadList().AddThread(new_thread_sp);
  threads->AddThread(new_thread_sp);

  return threads;
}
