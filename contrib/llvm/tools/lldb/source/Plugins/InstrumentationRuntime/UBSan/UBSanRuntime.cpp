//===-- UBSanRuntime.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "UBSanRuntime.h"

#include "Plugins/Process/Utility/HistoryThread.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/InstrumentationRuntimeStopInfo.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include <ctype.h>

using namespace lldb;
using namespace lldb_private;

UndefinedBehaviorSanitizerRuntime::~UndefinedBehaviorSanitizerRuntime() {
  Deactivate();
}

lldb::InstrumentationRuntimeSP
UndefinedBehaviorSanitizerRuntime::CreateInstance(
    const lldb::ProcessSP &process_sp) {
  return InstrumentationRuntimeSP(
      new UndefinedBehaviorSanitizerRuntime(process_sp));
}

void UndefinedBehaviorSanitizerRuntime::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(),
      "UndefinedBehaviorSanitizer instrumentation runtime plugin.",
      CreateInstance, GetTypeStatic);
}

void UndefinedBehaviorSanitizerRuntime::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString
UndefinedBehaviorSanitizerRuntime::GetPluginNameStatic() {
  return ConstString("UndefinedBehaviorSanitizer");
}

lldb::InstrumentationRuntimeType
UndefinedBehaviorSanitizerRuntime::GetTypeStatic() {
  return eInstrumentationRuntimeTypeUndefinedBehaviorSanitizer;
}

static const char *ub_sanitizer_retrieve_report_data_prefix = R"(
extern "C" {
void
__ubsan_get_current_report_data(const char **OutIssueKind,
    const char **OutMessage, const char **OutFilename, unsigned *OutLine,
    unsigned *OutCol, char **OutMemoryAddr);
}

struct data {
  const char *issue_kind;
  const char *message;
  const char *filename;
  unsigned line;
  unsigned col;
  char *memory_addr;
};
)";

static const char *ub_sanitizer_retrieve_report_data_command = R"(
data t;
__ubsan_get_current_report_data(&t.issue_kind, &t.message, &t.filename, &t.line,
                                &t.col, &t.memory_addr);
t;
)";

static addr_t RetrieveUnsigned(ValueObjectSP return_value_sp,
                               ProcessSP process_sp,
                               const std::string &expression_path) {
  return return_value_sp->GetValueForExpressionPath(expression_path.c_str())
      ->GetValueAsUnsigned(0);
}

static std::string RetrieveString(ValueObjectSP return_value_sp,
                                  ProcessSP process_sp,
                                  const std::string &expression_path) {
  addr_t ptr = RetrieveUnsigned(return_value_sp, process_sp, expression_path);
  std::string str;
  Status error;
  process_sp->ReadCStringFromMemory(ptr, str, error);
  return str;
}

StructuredData::ObjectSP UndefinedBehaviorSanitizerRuntime::RetrieveReportData(
    ExecutionContextRef exe_ctx_ref) {
  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return StructuredData::ObjectSP();

  ThreadSP thread_sp = exe_ctx_ref.GetThreadSP();
  StackFrameSP frame_sp = thread_sp->GetSelectedFrame();
  ModuleSP runtime_module_sp = GetRuntimeModuleSP();
  Target &target = process_sp->GetTarget();

  if (!frame_sp)
    return StructuredData::ObjectSP();

  StreamFileSP Stream(target.GetDebugger().GetOutputFile());

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetTryAllThreads(true);
  options.SetStopOthers(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTimeout(std::chrono::seconds(2));
  options.SetPrefix(ub_sanitizer_retrieve_report_data_prefix);
  options.SetAutoApplyFixIts(false);
  options.SetLanguage(eLanguageTypeObjC_plus_plus);

  ValueObjectSP main_value;
  ExecutionContext exe_ctx;
  Status eval_error;
  frame_sp->CalculateExecutionContext(exe_ctx);
  ExpressionResults result = UserExpression::Evaluate(
      exe_ctx, options, ub_sanitizer_retrieve_report_data_command, "",
      main_value, eval_error);
  if (result != eExpressionCompleted) {
    target.GetDebugger().GetAsyncOutputStream()->Printf(
        "Warning: Cannot evaluate UndefinedBehaviorSanitizer expression:\n%s\n",
        eval_error.AsCString());
    return StructuredData::ObjectSP();
  }

  // Gather the PCs of the user frames in the backtrace.
  StructuredData::Array *trace = new StructuredData::Array();
  auto trace_sp = StructuredData::ObjectSP(trace);
  for (unsigned I = 0; I < thread_sp->GetStackFrameCount(); ++I) {
    const Address FCA =
        thread_sp->GetStackFrameAtIndex(I)->GetFrameCodeAddress();
    if (FCA.GetModule() == runtime_module_sp) // Skip PCs from the runtime.
      continue;

    lldb::addr_t PC = FCA.GetLoadAddress(&target);
    trace->AddItem(StructuredData::ObjectSP(new StructuredData::Integer(PC)));
  }

  std::string IssueKind = RetrieveString(main_value, process_sp, ".issue_kind");
  std::string ErrMessage = RetrieveString(main_value, process_sp, ".message");
  std::string Filename = RetrieveString(main_value, process_sp, ".filename");
  unsigned Line = RetrieveUnsigned(main_value, process_sp, ".line");
  unsigned Col = RetrieveUnsigned(main_value, process_sp, ".col");
  uintptr_t MemoryAddr =
      RetrieveUnsigned(main_value, process_sp, ".memory_addr");

  auto *d = new StructuredData::Dictionary();
  auto dict_sp = StructuredData::ObjectSP(d);
  d->AddStringItem("instrumentation_class", "UndefinedBehaviorSanitizer");
  d->AddStringItem("description", IssueKind);
  d->AddStringItem("summary", ErrMessage);
  d->AddStringItem("filename", Filename);
  d->AddIntegerItem("line", Line);
  d->AddIntegerItem("col", Col);
  d->AddIntegerItem("memory_address", MemoryAddr);
  d->AddIntegerItem("tid", thread_sp->GetID());
  d->AddItem("trace", trace_sp);
  return dict_sp;
}

static std::string GetStopReasonDescription(StructuredData::ObjectSP report) {
  llvm::StringRef stop_reason_description_ref;
  report->GetAsDictionary()->GetValueForKeyAsString("description",
                                                    stop_reason_description_ref);
  std::string stop_reason_description = stop_reason_description_ref;

  if (!stop_reason_description.size()) {
    stop_reason_description = "Undefined behavior detected";
  } else {
    stop_reason_description[0] = toupper(stop_reason_description[0]);
    for (unsigned I = 1; I < stop_reason_description.size(); ++I)
      if (stop_reason_description[I] == '-')
        stop_reason_description[I] = ' ';
  }
  return stop_reason_description;
}

bool UndefinedBehaviorSanitizerRuntime::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false; //< false => resume execution.

  UndefinedBehaviorSanitizerRuntime *const instance =
      static_cast<UndefinedBehaviorSanitizerRuntime *>(baton);

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
    thread_sp->SetStopInfo(
        InstrumentationRuntimeStopInfo::CreateStopReasonWithInstrumentationData(
            *thread_sp, GetStopReasonDescription(report), report));
    return true;
  }

  return false;
}

const RegularExpression &
UndefinedBehaviorSanitizerRuntime::GetPatternForRuntimeLibrary() {
  static RegularExpression regex(llvm::StringRef("libclang_rt\\.(a|t|ub)san_"));
  return regex;
}

bool UndefinedBehaviorSanitizerRuntime::CheckIfRuntimeIsValid(
    const lldb::ModuleSP module_sp) {
  static ConstString ubsan_test_sym("__ubsan_on_report");
  const Symbol *symbol = module_sp->FindFirstSymbolWithNameAndType(
      ubsan_test_sym, lldb::eSymbolTypeAny);
  return symbol != nullptr;
}

// FIXME: Factor out all the logic we have in common with the {a,t}san plugins.
void UndefinedBehaviorSanitizerRuntime::Activate() {
  if (IsActive())
    return;

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  ModuleSP runtime_module_sp = GetRuntimeModuleSP();

  ConstString symbol_name("__ubsan_on_report");
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
  breakpoint->SetCallback(
      UndefinedBehaviorSanitizerRuntime::NotifyBreakpointHit, this, true);
  breakpoint->SetBreakpointKind("undefined-behavior-sanitizer-report");
  SetBreakpointID(breakpoint->GetID());

  SetActive(true);
}

void UndefinedBehaviorSanitizerRuntime::Deactivate() {
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
UndefinedBehaviorSanitizerRuntime::GetBacktracesFromExtendedStopInfo(
    StructuredData::ObjectSP info) {
  ThreadCollectionSP threads;
  threads.reset(new ThreadCollection());

  ProcessSP process_sp = GetProcessSP();

  if (info->GetObjectForDotSeparatedPath("instrumentation_class")
          ->GetStringValue() != "UndefinedBehaviorSanitizer")
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
  std::string stop_reason_description = GetStopReasonDescription(info);
  new_thread_sp->SetName(stop_reason_description.c_str());

  // Save this in the Process' ExtendedThreadList so a strong pointer retains
  // the object
  process_sp->GetExtendedThreadList().AddThread(new_thread_sp);
  threads->AddThread(new_thread_sp);

  return threads;
}
