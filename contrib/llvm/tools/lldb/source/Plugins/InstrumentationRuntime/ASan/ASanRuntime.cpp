//===-- ASanRuntime.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ASanRuntime.h"

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
#include "lldb/Target/InstrumentationRuntimeStopInfo.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/StringSwitch.h"

using namespace lldb;
using namespace lldb_private;

lldb::InstrumentationRuntimeSP
AddressSanitizerRuntime::CreateInstance(const lldb::ProcessSP &process_sp) {
  return InstrumentationRuntimeSP(new AddressSanitizerRuntime(process_sp));
}

void AddressSanitizerRuntime::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "AddressSanitizer instrumentation runtime plugin.",
      CreateInstance, GetTypeStatic);
}

void AddressSanitizerRuntime::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString AddressSanitizerRuntime::GetPluginNameStatic() {
  return ConstString("AddressSanitizer");
}

lldb::InstrumentationRuntimeType AddressSanitizerRuntime::GetTypeStatic() {
  return eInstrumentationRuntimeTypeAddressSanitizer;
}

AddressSanitizerRuntime::~AddressSanitizerRuntime() { Deactivate(); }

const RegularExpression &
AddressSanitizerRuntime::GetPatternForRuntimeLibrary() {
  // FIXME: This shouldn't include the "dylib" suffix.
  static RegularExpression regex(
      llvm::StringRef("libclang_rt.asan_(.*)_dynamic\\.dylib"));
  return regex;
}

bool AddressSanitizerRuntime::CheckIfRuntimeIsValid(
    const lldb::ModuleSP module_sp) {
  const Symbol *symbol = module_sp->FindFirstSymbolWithNameAndType(
      ConstString("__asan_get_alloc_stack"), lldb::eSymbolTypeAny);

  return symbol != nullptr;
}

static constexpr std::chrono::seconds g_retrieve_report_data_function_timeout(2);
const char *address_sanitizer_retrieve_report_data_prefix = R"(
extern "C"
{
int __asan_report_present();
void *__asan_get_report_pc();
void *__asan_get_report_bp();
void *__asan_get_report_sp();
void *__asan_get_report_address();
const char *__asan_get_report_description();
int __asan_get_report_access_type();
size_t __asan_get_report_access_size();
}
)";

const char *address_sanitizer_retrieve_report_data_command = R"(
struct {
    int present;
    int access_type;
    void *pc;
    void *bp;
    void *sp;
    void *address;
    size_t access_size;
    const char *description;
} t;

t.present = __asan_report_present();
t.access_type = __asan_get_report_access_type();
t.pc = __asan_get_report_pc();
t.bp = __asan_get_report_bp();
t.sp = __asan_get_report_sp();
t.address = __asan_get_report_address();
t.access_size = __asan_get_report_access_size();
t.description = __asan_get_report_description();
t
)";

StructuredData::ObjectSP AddressSanitizerRuntime::RetrieveReportData() {
  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return StructuredData::ObjectSP();

  ThreadSP thread_sp =
      process_sp->GetThreadList().GetExpressionExecutionThread();
  StackFrameSP frame_sp = thread_sp->GetSelectedFrame();

  if (!frame_sp)
    return StructuredData::ObjectSP();

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetTryAllThreads(true);
  options.SetStopOthers(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTimeout(g_retrieve_report_data_function_timeout);
  options.SetPrefix(address_sanitizer_retrieve_report_data_prefix);
  options.SetAutoApplyFixIts(false);
  options.SetLanguage(eLanguageTypeObjC_plus_plus);

  ValueObjectSP return_value_sp;
  ExecutionContext exe_ctx;
  Status eval_error;
  frame_sp->CalculateExecutionContext(exe_ctx);
  ExpressionResults result = UserExpression::Evaluate(
      exe_ctx, options, address_sanitizer_retrieve_report_data_command, "",
      return_value_sp, eval_error);
  if (result != eExpressionCompleted) {
    process_sp->GetTarget().GetDebugger().GetAsyncOutputStream()->Printf(
        "Warning: Cannot evaluate AddressSanitizer expression:\n%s\n",
        eval_error.AsCString());
    return StructuredData::ObjectSP();
  }

  int present = return_value_sp->GetValueForExpressionPath(".present")
                    ->GetValueAsUnsigned(0);
  if (present != 1)
    return StructuredData::ObjectSP();

  addr_t pc =
      return_value_sp->GetValueForExpressionPath(".pc")->GetValueAsUnsigned(0);
  /* commented out because rdar://problem/18533301
  addr_t bp =
  return_value_sp->GetValueForExpressionPath(".bp")->GetValueAsUnsigned(0);
  addr_t sp =
  return_value_sp->GetValueForExpressionPath(".sp")->GetValueAsUnsigned(0);
  */
  addr_t address = return_value_sp->GetValueForExpressionPath(".address")
                       ->GetValueAsUnsigned(0);
  addr_t access_type =
      return_value_sp->GetValueForExpressionPath(".access_type")
          ->GetValueAsUnsigned(0);
  addr_t access_size =
      return_value_sp->GetValueForExpressionPath(".access_size")
          ->GetValueAsUnsigned(0);
  addr_t description_ptr =
      return_value_sp->GetValueForExpressionPath(".description")
          ->GetValueAsUnsigned(0);
  std::string description;
  Status error;
  process_sp->ReadCStringFromMemory(description_ptr, description, error);

  StructuredData::Dictionary *dict = new StructuredData::Dictionary();
  dict->AddStringItem("instrumentation_class", "AddressSanitizer");
  dict->AddStringItem("stop_type", "fatal_error");
  dict->AddIntegerItem("pc", pc);
  /* commented out because rdar://problem/18533301
  dict->AddIntegerItem("bp", bp);
  dict->AddIntegerItem("sp", sp);
  */
  dict->AddIntegerItem("address", address);
  dict->AddIntegerItem("access_type", access_type);
  dict->AddIntegerItem("access_size", access_size);
  dict->AddStringItem("description", description);

  return StructuredData::ObjectSP(dict);
}

std::string
AddressSanitizerRuntime::FormatDescription(StructuredData::ObjectSP report) {
  std::string description = report->GetAsDictionary()
                                ->GetValueForKey("description")
                                ->GetAsString()
                                ->GetValue();
  return llvm::StringSwitch<std::string>(description)
      .Case("heap-use-after-free", "Use of deallocated memory")
      .Case("heap-buffer-overflow", "Heap buffer overflow")
      .Case("stack-buffer-underflow", "Stack buffer underflow")
      .Case("initialization-order-fiasco", "Initialization order problem")
      .Case("stack-buffer-overflow", "Stack buffer overflow")
      .Case("stack-use-after-return", "Use of stack memory after return")
      .Case("use-after-poison", "Use of poisoned memory")
      .Case("container-overflow", "Container overflow")
      .Case("stack-use-after-scope", "Use of out-of-scope stack memory")
      .Case("global-buffer-overflow", "Global buffer overflow")
      .Case("unknown-crash", "Invalid memory access")
      .Case("stack-overflow", "Stack space exhausted")
      .Case("null-deref", "Dereference of null pointer")
      .Case("wild-jump", "Jump to non-executable address")
      .Case("wild-addr-write", "Write through wild pointer")
      .Case("wild-addr-read", "Read from wild pointer")
      .Case("wild-addr", "Access through wild pointer")
      .Case("signal", "Deadly signal")
      .Case("double-free", "Deallocation of freed memory")
      .Case("new-delete-type-mismatch",
            "Deallocation size different from allocation size")
      .Case("bad-free", "Deallocation of non-allocated memory")
      .Case("alloc-dealloc-mismatch",
            "Mismatch between allocation and deallocation APIs")
      .Case("bad-malloc_usable_size", "Invalid argument to malloc_usable_size")
      .Case("bad-__sanitizer_get_allocated_size",
            "Invalid argument to __sanitizer_get_allocated_size")
      .Case("param-overlap",
            "Call to function disallowing overlapping memory ranges")
      .Case("negative-size-param", "Negative size used when accessing memory")
      .Case("bad-__sanitizer_annotate_contiguous_container",
            "Invalid argument to __sanitizer_annotate_contiguous_container")
      .Case("odr-violation", "Symbol defined in multiple translation units")
      .Case(
          "invalid-pointer-pair",
          "Comparison or arithmetic on pointers from different memory regions")
      // for unknown report codes just show the code
      .Default("AddressSanitizer detected: " + description);
}

bool AddressSanitizerRuntime::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false;

  AddressSanitizerRuntime *const instance =
      static_cast<AddressSanitizerRuntime *>(baton);

  ProcessSP process_sp = instance->GetProcessSP();

  if (process_sp->GetModIDRef().IsLastResumeForUserExpression())
    return false;

  StructuredData::ObjectSP report = instance->RetrieveReportData();
  std::string description;
  if (report) {
    description = instance->FormatDescription(report);
  }
  // Make sure this is the right process
  if (process_sp && process_sp == context->exe_ctx_ref.GetProcessSP()) {
    ThreadSP thread_sp = context->exe_ctx_ref.GetThreadSP();
    if (thread_sp)
      thread_sp->SetStopInfo(InstrumentationRuntimeStopInfo::
                                 CreateStopReasonWithInstrumentationData(
                                     *thread_sp, description, report));

    StreamFileSP stream_sp(
        process_sp->GetTarget().GetDebugger().GetOutputFile());
    if (stream_sp) {
      stream_sp->Printf("AddressSanitizer report breakpoint hit. Use 'thread "
                        "info -s' to get extended information about the "
                        "report.\n");
    }
    return true; // Return true to stop the target
  } else
    return false; // Let target run
}

void AddressSanitizerRuntime::Activate() {
  if (IsActive())
    return;

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  ConstString symbol_name("__asan::AsanDie()");
  const Symbol *symbol = GetRuntimeModuleSP()->FindFirstSymbolWithNameAndType(
      symbol_name, eSymbolTypeCode);

  if (symbol == NULL)
    return;

  if (!symbol->ValueIsAddress() || !symbol->GetAddressRef().IsValid())
    return;

  Target &target = process_sp->GetTarget();
  addr_t symbol_address = symbol->GetAddressRef().GetOpcodeLoadAddress(&target);

  if (symbol_address == LLDB_INVALID_ADDRESS)
    return;

  bool internal = true;
  bool hardware = false;
  Breakpoint *breakpoint =
      process_sp->GetTarget()
          .CreateBreakpoint(symbol_address, internal, hardware)
          .get();
  breakpoint->SetCallback(AddressSanitizerRuntime::NotifyBreakpointHit, this,
                          true);
  breakpoint->SetBreakpointKind("address-sanitizer-report");
  SetBreakpointID(breakpoint->GetID());

  SetActive(true);
}

void AddressSanitizerRuntime::Deactivate() {
  if (GetBreakpointID() != LLDB_INVALID_BREAK_ID) {
    ProcessSP process_sp = GetProcessSP();
    if (process_sp) {
      process_sp->GetTarget().RemoveBreakpointByID(GetBreakpointID());
      SetBreakpointID(LLDB_INVALID_BREAK_ID);
    }
  }
  SetActive(false);
}
