//===-- TSanRuntime.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TSanRuntime.h"

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

using namespace lldb;
using namespace lldb_private;

lldb::InstrumentationRuntimeSP
ThreadSanitizerRuntime::CreateInstance(const lldb::ProcessSP &process_sp) {
  return InstrumentationRuntimeSP(new ThreadSanitizerRuntime(process_sp));
}

void ThreadSanitizerRuntime::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "ThreadSanitizer instrumentation runtime plugin.",
      CreateInstance, GetTypeStatic);
}

void ThreadSanitizerRuntime::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString ThreadSanitizerRuntime::GetPluginNameStatic() {
  return ConstString("ThreadSanitizer");
}

lldb::InstrumentationRuntimeType ThreadSanitizerRuntime::GetTypeStatic() {
  return eInstrumentationRuntimeTypeThreadSanitizer;
}

ThreadSanitizerRuntime::~ThreadSanitizerRuntime() { Deactivate(); }

static constexpr std::chrono::seconds g_retrieve_data_function_timeout(2);

const char *thread_sanitizer_retrieve_report_data_prefix = R"(
extern "C"
{
    void *__tsan_get_current_report();
    int __tsan_get_report_data(void *report, const char **description, int *count,
                               int *stack_count, int *mop_count, int *loc_count,
                               int *mutex_count, int *thread_count,
                               int *unique_tid_count, void **sleep_trace,
                               unsigned long trace_size);
    int __tsan_get_report_stack(void *report, unsigned long idx, void **trace,
                                unsigned long trace_size);
    int __tsan_get_report_mop(void *report, unsigned long idx, int *tid, void **addr,
                              int *size, int *write, int *atomic, void **trace,
                              unsigned long trace_size);
    int __tsan_get_report_loc(void *report, unsigned long idx, const char **type,
                              void **addr, unsigned long *start, unsigned long *size, int *tid,
                              int *fd, int *suppressable, void **trace,
                              unsigned long trace_size);
    int __tsan_get_report_mutex(void *report, unsigned long idx, unsigned long *mutex_id, void **addr,
                                int *destroyed, void **trace, unsigned long trace_size);
    int __tsan_get_report_thread(void *report, unsigned long idx, int *tid, unsigned long *os_id,
                                 int *running, const char **name, int *parent_tid,
                                 void **trace, unsigned long trace_size);
    int __tsan_get_report_unique_tid(void *report, unsigned long idx, int *tid);
  
    // TODO: dlsym won't work on Windows.
    void *dlsym(void* handle, const char* symbol);
    int (*ptr__tsan_get_report_loc_object_type)(void *report, unsigned long idx, const char **object_type);
}

const int REPORT_TRACE_SIZE = 128;
const int REPORT_ARRAY_SIZE = 4;

struct data {
    void *report;
    const char *description;
    int report_count;
    
    void *sleep_trace[REPORT_TRACE_SIZE];
    
    int stack_count;
    struct {
        int idx;
        void *trace[REPORT_TRACE_SIZE];
    } stacks[REPORT_ARRAY_SIZE];
    
    int mop_count;
    struct {
        int idx;
        int tid;
        int size;
        int write;
        int atomic;
        void *addr;
        void *trace[REPORT_TRACE_SIZE];
    } mops[REPORT_ARRAY_SIZE];
    
    int loc_count;
    struct {
        int idx;
        const char *type;
        void *addr;
        unsigned long start;
        unsigned long size;
        int tid;
        int fd;
        int suppressable;
        void *trace[REPORT_TRACE_SIZE];
        const char *object_type;
    } locs[REPORT_ARRAY_SIZE];
    
    int mutex_count;
    struct {
        int idx;
        unsigned long mutex_id;
        void *addr;
        int destroyed;
        void *trace[REPORT_TRACE_SIZE];
    } mutexes[REPORT_ARRAY_SIZE];
    
    int thread_count;
    struct {
        int idx;
        int tid;
        unsigned long os_id;
        int running;
        const char *name;
        int parent_tid;
        void *trace[REPORT_TRACE_SIZE];
    } threads[REPORT_ARRAY_SIZE];
    
    int unique_tid_count;
    struct {
        int idx;
        int tid;
    } unique_tids[REPORT_ARRAY_SIZE];
};
)";

const char *thread_sanitizer_retrieve_report_data_command = R"(
data t = {0};

ptr__tsan_get_report_loc_object_type = (typeof(ptr__tsan_get_report_loc_object_type))(void *)dlsym((void*)-2 /*RTLD_DEFAULT*/, "__tsan_get_report_loc_object_type");

t.report = __tsan_get_current_report();
__tsan_get_report_data(t.report, &t.description, &t.report_count, &t.stack_count, &t.mop_count, &t.loc_count, &t.mutex_count, &t.thread_count, &t.unique_tid_count, t.sleep_trace, REPORT_TRACE_SIZE);

if (t.stack_count > REPORT_ARRAY_SIZE) t.stack_count = REPORT_ARRAY_SIZE;
for (int i = 0; i < t.stack_count; i++) {
    t.stacks[i].idx = i;
    __tsan_get_report_stack(t.report, i, t.stacks[i].trace, REPORT_TRACE_SIZE);
}

if (t.mop_count > REPORT_ARRAY_SIZE) t.mop_count = REPORT_ARRAY_SIZE;
for (int i = 0; i < t.mop_count; i++) {
    t.mops[i].idx = i;
    __tsan_get_report_mop(t.report, i, &t.mops[i].tid, &t.mops[i].addr, &t.mops[i].size, &t.mops[i].write, &t.mops[i].atomic, t.mops[i].trace, REPORT_TRACE_SIZE);
}

if (t.loc_count > REPORT_ARRAY_SIZE) t.loc_count = REPORT_ARRAY_SIZE;
for (int i = 0; i < t.loc_count; i++) {
    t.locs[i].idx = i;
    __tsan_get_report_loc(t.report, i, &t.locs[i].type, &t.locs[i].addr, &t.locs[i].start, &t.locs[i].size, &t.locs[i].tid, &t.locs[i].fd, &t.locs[i].suppressable, t.locs[i].trace, REPORT_TRACE_SIZE);
    if (ptr__tsan_get_report_loc_object_type)
        ptr__tsan_get_report_loc_object_type(t.report, i, &t.locs[i].object_type);
}

if (t.mutex_count > REPORT_ARRAY_SIZE) t.mutex_count = REPORT_ARRAY_SIZE;
for (int i = 0; i < t.mutex_count; i++) {
    t.mutexes[i].idx = i;
    __tsan_get_report_mutex(t.report, i, &t.mutexes[i].mutex_id, &t.mutexes[i].addr, &t.mutexes[i].destroyed, t.mutexes[i].trace, REPORT_TRACE_SIZE);
}

if (t.thread_count > REPORT_ARRAY_SIZE) t.thread_count = REPORT_ARRAY_SIZE;
for (int i = 0; i < t.thread_count; i++) {
    t.threads[i].idx = i;
    __tsan_get_report_thread(t.report, i, &t.threads[i].tid, &t.threads[i].os_id, &t.threads[i].running, &t.threads[i].name, &t.threads[i].parent_tid, t.threads[i].trace, REPORT_TRACE_SIZE);
}

if (t.unique_tid_count > REPORT_ARRAY_SIZE) t.unique_tid_count = REPORT_ARRAY_SIZE;
for (int i = 0; i < t.unique_tid_count; i++) {
    t.unique_tids[i].idx = i;
    __tsan_get_report_unique_tid(t.report, i, &t.unique_tids[i].tid);
}

t;
)";

static StructuredData::Array *
CreateStackTrace(ValueObjectSP o,
                 const std::string &trace_item_name = ".trace") {
  StructuredData::Array *trace = new StructuredData::Array();
  ValueObjectSP trace_value_object =
      o->GetValueForExpressionPath(trace_item_name.c_str());
  size_t count = trace_value_object->GetNumChildren();
  for (size_t j = 0; j < count; j++) {
    addr_t trace_addr =
        trace_value_object->GetChildAtIndex(j, true)->GetValueAsUnsigned(0);
    if (trace_addr == 0)
      break;
    trace->AddItem(
        StructuredData::ObjectSP(new StructuredData::Integer(trace_addr)));
  }
  return trace;
}

static StructuredData::Array *ConvertToStructuredArray(
    ValueObjectSP return_value_sp, const std::string &items_name,
    const std::string &count_name,
    std::function<void(ValueObjectSP o, StructuredData::Dictionary *dict)> const
        &callback) {
  StructuredData::Array *array = new StructuredData::Array();
  unsigned int count =
      return_value_sp->GetValueForExpressionPath(count_name.c_str())
          ->GetValueAsUnsigned(0);
  ValueObjectSP objects =
      return_value_sp->GetValueForExpressionPath(items_name.c_str());
  for (unsigned int i = 0; i < count; i++) {
    ValueObjectSP o = objects->GetChildAtIndex(i, true);
    StructuredData::Dictionary *dict = new StructuredData::Dictionary();

    callback(o, dict);

    array->AddItem(StructuredData::ObjectSP(dict));
  }
  return array;
}

static std::string RetrieveString(ValueObjectSP return_value_sp,
                                  ProcessSP process_sp,
                                  const std::string &expression_path) {
  addr_t ptr =
      return_value_sp->GetValueForExpressionPath(expression_path.c_str())
          ->GetValueAsUnsigned(0);
  std::string str;
  Status error;
  process_sp->ReadCStringFromMemory(ptr, str, error);
  return str;
}

static void
GetRenumberedThreadIds(ProcessSP process_sp, ValueObjectSP data,
                       std::map<uint64_t, user_id_t> &thread_id_map) {
  ConvertToStructuredArray(
      data, ".threads", ".thread_count",
      [process_sp, &thread_id_map](ValueObjectSP o,
                                   StructuredData::Dictionary *dict) {
        uint64_t thread_id =
            o->GetValueForExpressionPath(".tid")->GetValueAsUnsigned(0);
        uint64_t thread_os_id =
            o->GetValueForExpressionPath(".os_id")->GetValueAsUnsigned(0);
        user_id_t lldb_user_id = 0;

        bool can_update = true;
        ThreadSP lldb_thread = process_sp->GetThreadList().FindThreadByID(
            thread_os_id, can_update);
        if (lldb_thread) {
          lldb_user_id = lldb_thread->GetIndexID();
        } else {
          // This isn't a live thread anymore.  Ask process to assign a new
          // Index ID (or return an old one if we've already seen this
          // thread_os_id). It will also make sure that no new threads are
          // assigned this Index ID.
          lldb_user_id = process_sp->AssignIndexIDToThread(thread_os_id);
        }

        thread_id_map[thread_id] = lldb_user_id;
      });
}

static user_id_t Renumber(uint64_t id,
                          std::map<uint64_t, user_id_t> &thread_id_map) {
  auto IT = thread_id_map.find(id);
  if (IT == thread_id_map.end())
    return 0;

  return IT->second;
}

StructuredData::ObjectSP
ThreadSanitizerRuntime::RetrieveReportData(ExecutionContextRef exe_ctx_ref) {
  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return StructuredData::ObjectSP();

  ThreadSP thread_sp = exe_ctx_ref.GetThreadSP();
  StackFrameSP frame_sp = thread_sp->GetSelectedFrame();

  if (!frame_sp)
    return StructuredData::ObjectSP();

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetTryAllThreads(true);
  options.SetStopOthers(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTimeout(g_retrieve_data_function_timeout);
  options.SetPrefix(thread_sanitizer_retrieve_report_data_prefix);
  options.SetAutoApplyFixIts(false);
  options.SetLanguage(eLanguageTypeObjC_plus_plus);

  ValueObjectSP main_value;
  ExecutionContext exe_ctx;
  Status eval_error;
  frame_sp->CalculateExecutionContext(exe_ctx);
  ExpressionResults result = UserExpression::Evaluate(
      exe_ctx, options, thread_sanitizer_retrieve_report_data_command, "",
      main_value, eval_error);
  if (result != eExpressionCompleted) {
    process_sp->GetTarget().GetDebugger().GetAsyncOutputStream()->Printf(
        "Warning: Cannot evaluate ThreadSanitizer expression:\n%s\n",
        eval_error.AsCString());
    return StructuredData::ObjectSP();
  }

  std::map<uint64_t, user_id_t> thread_id_map;
  GetRenumberedThreadIds(process_sp, main_value, thread_id_map);

  StructuredData::Dictionary *dict = new StructuredData::Dictionary();
  dict->AddStringItem("instrumentation_class", "ThreadSanitizer");
  dict->AddStringItem("issue_type",
                      RetrieveString(main_value, process_sp, ".description"));
  dict->AddIntegerItem("report_count",
                       main_value->GetValueForExpressionPath(".report_count")
                           ->GetValueAsUnsigned(0));
  dict->AddItem("sleep_trace", StructuredData::ObjectSP(CreateStackTrace(
                                   main_value, ".sleep_trace")));

  StructuredData::Array *stacks = ConvertToStructuredArray(
      main_value, ".stacks", ".stack_count",
      [thread_sp](ValueObjectSP o, StructuredData::Dictionary *dict) {
        dict->AddIntegerItem(
            "index",
            o->GetValueForExpressionPath(".idx")->GetValueAsUnsigned(0));
        dict->AddItem("trace", StructuredData::ObjectSP(CreateStackTrace(o)));
        // "stacks" happen on the current thread
        dict->AddIntegerItem("thread_id", thread_sp->GetIndexID());
      });
  dict->AddItem("stacks", StructuredData::ObjectSP(stacks));

  StructuredData::Array *mops = ConvertToStructuredArray(
      main_value, ".mops", ".mop_count",
      [&thread_id_map](ValueObjectSP o, StructuredData::Dictionary *dict) {
        dict->AddIntegerItem(
            "index",
            o->GetValueForExpressionPath(".idx")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "thread_id",
            Renumber(
                o->GetValueForExpressionPath(".tid")->GetValueAsUnsigned(0),
                thread_id_map));
        dict->AddIntegerItem(
            "size",
            o->GetValueForExpressionPath(".size")->GetValueAsUnsigned(0));
        dict->AddBooleanItem(
            "is_write",
            o->GetValueForExpressionPath(".write")->GetValueAsUnsigned(0));
        dict->AddBooleanItem(
            "is_atomic",
            o->GetValueForExpressionPath(".atomic")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "address",
            o->GetValueForExpressionPath(".addr")->GetValueAsUnsigned(0));
        dict->AddItem("trace", StructuredData::ObjectSP(CreateStackTrace(o)));
      });
  dict->AddItem("mops", StructuredData::ObjectSP(mops));

  StructuredData::Array *locs = ConvertToStructuredArray(
      main_value, ".locs", ".loc_count",
      [process_sp, &thread_id_map](ValueObjectSP o,
                                   StructuredData::Dictionary *dict) {
        dict->AddIntegerItem(
            "index",
            o->GetValueForExpressionPath(".idx")->GetValueAsUnsigned(0));
        dict->AddStringItem("type", RetrieveString(o, process_sp, ".type"));
        dict->AddIntegerItem(
            "address",
            o->GetValueForExpressionPath(".addr")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "start",
            o->GetValueForExpressionPath(".start")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "size",
            o->GetValueForExpressionPath(".size")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "thread_id",
            Renumber(
                o->GetValueForExpressionPath(".tid")->GetValueAsUnsigned(0),
                thread_id_map));
        dict->AddIntegerItem(
            "file_descriptor",
            o->GetValueForExpressionPath(".fd")->GetValueAsUnsigned(0));
        dict->AddIntegerItem("suppressable",
                             o->GetValueForExpressionPath(".suppressable")
                                 ->GetValueAsUnsigned(0));
        dict->AddItem("trace", StructuredData::ObjectSP(CreateStackTrace(o)));
        dict->AddStringItem("object_type",
                            RetrieveString(o, process_sp, ".object_type"));
      });
  dict->AddItem("locs", StructuredData::ObjectSP(locs));

  StructuredData::Array *mutexes = ConvertToStructuredArray(
      main_value, ".mutexes", ".mutex_count",
      [](ValueObjectSP o, StructuredData::Dictionary *dict) {
        dict->AddIntegerItem(
            "index",
            o->GetValueForExpressionPath(".idx")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "mutex_id",
            o->GetValueForExpressionPath(".mutex_id")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "address",
            o->GetValueForExpressionPath(".addr")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "destroyed",
            o->GetValueForExpressionPath(".destroyed")->GetValueAsUnsigned(0));
        dict->AddItem("trace", StructuredData::ObjectSP(CreateStackTrace(o)));
      });
  dict->AddItem("mutexes", StructuredData::ObjectSP(mutexes));

  StructuredData::Array *threads = ConvertToStructuredArray(
      main_value, ".threads", ".thread_count",
      [process_sp, &thread_id_map](ValueObjectSP o,
                                   StructuredData::Dictionary *dict) {
        dict->AddIntegerItem(
            "index",
            o->GetValueForExpressionPath(".idx")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "thread_id",
            Renumber(
                o->GetValueForExpressionPath(".tid")->GetValueAsUnsigned(0),
                thread_id_map));
        dict->AddIntegerItem(
            "thread_os_id",
            o->GetValueForExpressionPath(".os_id")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "running",
            o->GetValueForExpressionPath(".running")->GetValueAsUnsigned(0));
        dict->AddStringItem("name", RetrieveString(o, process_sp, ".name"));
        dict->AddIntegerItem(
            "parent_thread_id",
            Renumber(o->GetValueForExpressionPath(".parent_tid")
                         ->GetValueAsUnsigned(0),
                     thread_id_map));
        dict->AddItem("trace", StructuredData::ObjectSP(CreateStackTrace(o)));
      });
  dict->AddItem("threads", StructuredData::ObjectSP(threads));

  StructuredData::Array *unique_tids = ConvertToStructuredArray(
      main_value, ".unique_tids", ".unique_tid_count",
      [&thread_id_map](ValueObjectSP o, StructuredData::Dictionary *dict) {
        dict->AddIntegerItem(
            "index",
            o->GetValueForExpressionPath(".idx")->GetValueAsUnsigned(0));
        dict->AddIntegerItem(
            "tid",
            Renumber(
                o->GetValueForExpressionPath(".tid")->GetValueAsUnsigned(0),
                thread_id_map));
      });
  dict->AddItem("unique_tids", StructuredData::ObjectSP(unique_tids));

  return StructuredData::ObjectSP(dict);
}

std::string
ThreadSanitizerRuntime::FormatDescription(StructuredData::ObjectSP report) {
  std::string description = report->GetAsDictionary()
                                ->GetValueForKey("issue_type")
                                ->GetAsString()
                                ->GetValue();

  if (description == "data-race") {
    return "Data race";
  } else if (description == "data-race-vptr") {
    return "Data race on C++ virtual pointer";
  } else if (description == "heap-use-after-free") {
    return "Use of deallocated memory";
  } else if (description == "heap-use-after-free-vptr") {
    return "Use of deallocated C++ virtual pointer";
  } else if (description == "thread-leak") {
    return "Thread leak";
  } else if (description == "locked-mutex-destroy") {
    return "Destruction of a locked mutex";
  } else if (description == "mutex-double-lock") {
    return "Double lock of a mutex";
  } else if (description == "mutex-invalid-access") {
    return "Use of an uninitialized or destroyed mutex";
  } else if (description == "mutex-bad-unlock") {
    return "Unlock of an unlocked mutex (or by a wrong thread)";
  } else if (description == "mutex-bad-read-lock") {
    return "Read lock of a write locked mutex";
  } else if (description == "mutex-bad-read-unlock") {
    return "Read unlock of a write locked mutex";
  } else if (description == "signal-unsafe-call") {
    return "Signal-unsafe call inside a signal handler";
  } else if (description == "errno-in-signal-handler") {
    return "Overwrite of errno in a signal handler";
  } else if (description == "lock-order-inversion") {
    return "Lock order inversion (potential deadlock)";
  } else if (description == "external-race") {
    return "Race on a library object";
  } else if (description == "swift-access-race") {
    return "Swift access race";
  }

  // for unknown report codes just show the code
  return description;
}

static std::string Sprintf(const char *format, ...) {
  StreamString s;
  va_list args;
  va_start(args, format);
  s.PrintfVarArg(format, args);
  va_end(args);
  return s.GetString();
}

static std::string GetSymbolNameFromAddress(ProcessSP process_sp, addr_t addr) {
  lldb_private::Address so_addr;
  if (!process_sp->GetTarget().GetSectionLoadList().ResolveLoadAddress(addr,
                                                                       so_addr))
    return "";

  lldb_private::Symbol *symbol = so_addr.CalculateSymbolContextSymbol();
  if (!symbol)
    return "";

  std::string sym_name = symbol->GetName().GetCString();
  return sym_name;
}

static void GetSymbolDeclarationFromAddress(ProcessSP process_sp, addr_t addr,
                                            Declaration &decl) {
  lldb_private::Address so_addr;
  if (!process_sp->GetTarget().GetSectionLoadList().ResolveLoadAddress(addr,
                                                                       so_addr))
    return;

  lldb_private::Symbol *symbol = so_addr.CalculateSymbolContextSymbol();
  if (!symbol)
    return;

  ConstString sym_name = symbol->GetMangled().GetName(
      lldb::eLanguageTypeUnknown, Mangled::ePreferMangled);

  ModuleSP module = symbol->CalculateSymbolContextModule();
  if (!module)
    return;

  VariableList var_list;
  module->FindGlobalVariables(sym_name, nullptr, 1U, var_list);
  if (var_list.GetSize() < 1)
    return;

  VariableSP var = var_list.GetVariableAtIndex(0);
  decl = var->GetDeclaration();
}

addr_t ThreadSanitizerRuntime::GetFirstNonInternalFramePc(
    StructuredData::ObjectSP trace, bool skip_one_frame) {
  ProcessSP process_sp = GetProcessSP();
  ModuleSP runtime_module_sp = GetRuntimeModuleSP();

  StructuredData::Array *trace_array = trace->GetAsArray();
  for (size_t i = 0; i < trace_array->GetSize(); i++) {
    if (skip_one_frame && i == 0)
      continue;

    addr_t addr;
    if (!trace_array->GetItemAtIndexAsInteger(i, addr))
      continue;

    lldb_private::Address so_addr;
    if (!process_sp->GetTarget().GetSectionLoadList().ResolveLoadAddress(
            addr, so_addr))
      continue;

    if (so_addr.GetModule() == runtime_module_sp)
      continue;

    return addr;
  }

  return 0;
}

std::string
ThreadSanitizerRuntime::GenerateSummary(StructuredData::ObjectSP report) {
  ProcessSP process_sp = GetProcessSP();

  std::string summary = report->GetAsDictionary()
                            ->GetValueForKey("description")
                            ->GetAsString()
                            ->GetValue();
  bool skip_one_frame =
      report->GetObjectForDotSeparatedPath("issue_type")->GetStringValue() ==
      "external-race";

  addr_t pc = 0;
  if (report->GetAsDictionary()
          ->GetValueForKey("mops")
          ->GetAsArray()
          ->GetSize() > 0)
    pc = GetFirstNonInternalFramePc(report->GetAsDictionary()
                                        ->GetValueForKey("mops")
                                        ->GetAsArray()
                                        ->GetItemAtIndex(0)
                                        ->GetAsDictionary()
                                        ->GetValueForKey("trace"),
                                    skip_one_frame);

  if (report->GetAsDictionary()
          ->GetValueForKey("stacks")
          ->GetAsArray()
          ->GetSize() > 0)
    pc = GetFirstNonInternalFramePc(report->GetAsDictionary()
                                        ->GetValueForKey("stacks")
                                        ->GetAsArray()
                                        ->GetItemAtIndex(0)
                                        ->GetAsDictionary()
                                        ->GetValueForKey("trace"),
                                    skip_one_frame);

  if (pc != 0) {
    summary = summary + " in " + GetSymbolNameFromAddress(process_sp, pc);
  }

  if (report->GetAsDictionary()
          ->GetValueForKey("locs")
          ->GetAsArray()
          ->GetSize() > 0) {
    StructuredData::ObjectSP loc = report->GetAsDictionary()
                                       ->GetValueForKey("locs")
                                       ->GetAsArray()
                                       ->GetItemAtIndex(0);
    std::string object_type = loc->GetAsDictionary()
                                  ->GetValueForKey("object_type")
                                  ->GetAsString()
                                  ->GetValue();
    if (!object_type.empty()) {
      summary = "Race on " + object_type + " object";
    }
    addr_t addr = loc->GetAsDictionary()
                      ->GetValueForKey("address")
                      ->GetAsInteger()
                      ->GetValue();
    if (addr == 0)
      addr = loc->GetAsDictionary()
                 ->GetValueForKey("start")
                 ->GetAsInteger()
                 ->GetValue();

    if (addr != 0) {
      std::string global_name = GetSymbolNameFromAddress(process_sp, addr);
      if (!global_name.empty()) {
        summary = summary + " at " + global_name;
      } else {
        summary = summary + " at " + Sprintf("0x%llx", addr);
      }
    } else {
      int fd = loc->GetAsDictionary()
                   ->GetValueForKey("file_descriptor")
                   ->GetAsInteger()
                   ->GetValue();
      if (fd != 0) {
        summary = summary + " on file descriptor " + Sprintf("%d", fd);
      }
    }
  }

  return summary;
}

addr_t
ThreadSanitizerRuntime::GetMainRacyAddress(StructuredData::ObjectSP report) {
  addr_t result = (addr_t)-1;

  report->GetObjectForDotSeparatedPath("mops")->GetAsArray()->ForEach(
      [&result](StructuredData::Object *o) -> bool {
        addr_t addr =
            o->GetObjectForDotSeparatedPath("address")->GetIntegerValue();
        if (addr < result)
          result = addr;
        return true;
      });

  return (result == (addr_t)-1) ? 0 : result;
}

std::string ThreadSanitizerRuntime::GetLocationDescription(
    StructuredData::ObjectSP report, addr_t &global_addr,
    std::string &global_name, std::string &filename, uint32_t &line) {
  std::string result = "";

  ProcessSP process_sp = GetProcessSP();

  if (report->GetAsDictionary()
          ->GetValueForKey("locs")
          ->GetAsArray()
          ->GetSize() > 0) {
    StructuredData::ObjectSP loc = report->GetAsDictionary()
                                       ->GetValueForKey("locs")
                                       ->GetAsArray()
                                       ->GetItemAtIndex(0);
    std::string type =
        loc->GetAsDictionary()->GetValueForKey("type")->GetStringValue();
    if (type == "global") {
      global_addr = loc->GetAsDictionary()
                        ->GetValueForKey("address")
                        ->GetAsInteger()
                        ->GetValue();
      global_name = GetSymbolNameFromAddress(process_sp, global_addr);
      if (!global_name.empty()) {
        result = Sprintf("'%s' is a global variable (0x%llx)",
                         global_name.c_str(), global_addr);
      } else {
        result = Sprintf("0x%llx is a global variable", global_addr);
      }

      Declaration decl;
      GetSymbolDeclarationFromAddress(process_sp, global_addr, decl);
      if (decl.GetFile()) {
        filename = decl.GetFile().GetPath();
        line = decl.GetLine();
      }
    } else if (type == "heap") {
      addr_t addr = loc->GetAsDictionary()
                        ->GetValueForKey("start")
                        ->GetAsInteger()
                        ->GetValue();
      long size = loc->GetAsDictionary()
                      ->GetValueForKey("size")
                      ->GetAsInteger()
                      ->GetValue();
      std::string object_type = loc->GetAsDictionary()
                                    ->GetValueForKey("object_type")
                                    ->GetAsString()
                                    ->GetValue();
      if (!object_type.empty()) {
        result = Sprintf("Location is a %ld-byte %s object at 0x%llx", size,
                         object_type.c_str(), addr);
      } else {
        result =
            Sprintf("Location is a %ld-byte heap object at 0x%llx", size, addr);
      }
    } else if (type == "stack") {
      int tid = loc->GetAsDictionary()
                    ->GetValueForKey("thread_id")
                    ->GetAsInteger()
                    ->GetValue();
      result = Sprintf("Location is stack of thread %d", tid);
    } else if (type == "tls") {
      int tid = loc->GetAsDictionary()
                    ->GetValueForKey("thread_id")
                    ->GetAsInteger()
                    ->GetValue();
      result = Sprintf("Location is TLS of thread %d", tid);
    } else if (type == "fd") {
      int fd = loc->GetAsDictionary()
                   ->GetValueForKey("file_descriptor")
                   ->GetAsInteger()
                   ->GetValue();
      result = Sprintf("Location is file descriptor %d", fd);
    }
  }

  return result;
}

bool ThreadSanitizerRuntime::NotifyBreakpointHit(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(baton && "null baton");
  if (!baton)
    return false;

  ThreadSanitizerRuntime *const instance =
      static_cast<ThreadSanitizerRuntime *>(baton);

  ProcessSP process_sp = instance->GetProcessSP();

  if (process_sp->GetModIDRef().IsLastResumeForUserExpression())
    return false;

  StructuredData::ObjectSP report =
      instance->RetrieveReportData(context->exe_ctx_ref);
  std::string stop_reason_description;
  if (report) {
    std::string issue_description = instance->FormatDescription(report);
    report->GetAsDictionary()->AddStringItem("description", issue_description);
    stop_reason_description = issue_description + " detected";
    report->GetAsDictionary()->AddStringItem("stop_description",
                                             stop_reason_description);
    std::string summary = instance->GenerateSummary(report);
    report->GetAsDictionary()->AddStringItem("summary", summary);
    addr_t main_address = instance->GetMainRacyAddress(report);
    report->GetAsDictionary()->AddIntegerItem("memory_address", main_address);

    addr_t global_addr = 0;
    std::string global_name = "";
    std::string location_filename = "";
    uint32_t location_line = 0;
    std::string location_description = instance->GetLocationDescription(
        report, global_addr, global_name, location_filename, location_line);
    report->GetAsDictionary()->AddStringItem("location_description",
                                             location_description);
    if (global_addr != 0) {
      report->GetAsDictionary()->AddIntegerItem("global_address", global_addr);
    }
    if (!global_name.empty()) {
      report->GetAsDictionary()->AddStringItem("global_name", global_name);
    }
    if (location_filename != "") {
      report->GetAsDictionary()->AddStringItem("location_filename",
                                               location_filename);
      report->GetAsDictionary()->AddIntegerItem("location_line", location_line);
    }

    bool all_addresses_are_same = true;
    report->GetObjectForDotSeparatedPath("mops")->GetAsArray()->ForEach(
        [&all_addresses_are_same,
         main_address](StructuredData::Object *o) -> bool {
          addr_t addr =
              o->GetObjectForDotSeparatedPath("address")->GetIntegerValue();
          if (main_address != addr)
            all_addresses_are_same = false;
          return true;
        });
    report->GetAsDictionary()->AddBooleanItem("all_addresses_are_same",
                                              all_addresses_are_same);
  }

  // Make sure this is the right process
  if (process_sp && process_sp == context->exe_ctx_ref.GetProcessSP()) {
    ThreadSP thread_sp = context->exe_ctx_ref.GetThreadSP();
    if (thread_sp)
      thread_sp->SetStopInfo(
          InstrumentationRuntimeStopInfo::
              CreateStopReasonWithInstrumentationData(
                  *thread_sp, stop_reason_description, report));

    StreamFileSP stream_sp(
        process_sp->GetTarget().GetDebugger().GetOutputFile());
    if (stream_sp) {
      stream_sp->Printf("ThreadSanitizer report breakpoint hit. Use 'thread "
                        "info -s' to get extended information about the "
                        "report.\n");
    }
    return true; // Return true to stop the target
  } else
    return false; // Let target run
}

const RegularExpression &ThreadSanitizerRuntime::GetPatternForRuntimeLibrary() {
  static RegularExpression regex(llvm::StringRef("libclang_rt.tsan_"));
  return regex;
}

bool ThreadSanitizerRuntime::CheckIfRuntimeIsValid(
    const lldb::ModuleSP module_sp) {
  static ConstString g_tsan_get_current_report("__tsan_get_current_report");
  const Symbol *symbol = module_sp->FindFirstSymbolWithNameAndType(
      g_tsan_get_current_report, lldb::eSymbolTypeAny);
  return symbol != nullptr;
}

void ThreadSanitizerRuntime::Activate() {
  if (IsActive())
    return;

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  ConstString symbol_name("__tsan_on_report");
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
  breakpoint->SetCallback(ThreadSanitizerRuntime::NotifyBreakpointHit, this,
                          true);
  breakpoint->SetBreakpointKind("thread-sanitizer-report");
  SetBreakpointID(breakpoint->GetID());

  SetActive(true);
}

void ThreadSanitizerRuntime::Deactivate() {
  if (GetBreakpointID() != LLDB_INVALID_BREAK_ID) {
    ProcessSP process_sp = GetProcessSP();
    if (process_sp) {
      process_sp->GetTarget().RemoveBreakpointByID(GetBreakpointID());
      SetBreakpointID(LLDB_INVALID_BREAK_ID);
    }
  }
  SetActive(false);
}
static std::string GenerateThreadName(const std::string &path,
                                      StructuredData::Object *o,
                                      StructuredData::ObjectSP main_info) {
  std::string result = "additional information";

  if (path == "mops") {
    int size = o->GetObjectForDotSeparatedPath("size")->GetIntegerValue();
    int thread_id =
        o->GetObjectForDotSeparatedPath("thread_id")->GetIntegerValue();
    bool is_write =
        o->GetObjectForDotSeparatedPath("is_write")->GetBooleanValue();
    bool is_atomic =
        o->GetObjectForDotSeparatedPath("is_atomic")->GetBooleanValue();
    addr_t addr = o->GetObjectForDotSeparatedPath("address")->GetIntegerValue();

    std::string addr_string = Sprintf(" at 0x%llx", addr);

    if (main_info->GetObjectForDotSeparatedPath("all_addresses_are_same")
            ->GetBooleanValue()) {
      addr_string = "";
    }

    if (main_info->GetObjectForDotSeparatedPath("issue_type")
            ->GetStringValue() == "external-race") {
      result = Sprintf("%s access by thread %d",
                       is_write ? "mutating" : "read-only", thread_id);
    } else if (main_info->GetObjectForDotSeparatedPath("issue_type")
                   ->GetStringValue() == "swift-access-race") {
      result = Sprintf("modifying access by thread %d", thread_id);
    } else {
      result = Sprintf("%s%s of size %d%s by thread %d",
                       is_atomic ? "atomic " : "", is_write ? "write" : "read",
                       size, addr_string.c_str(), thread_id);
    }
  }

  if (path == "threads") {
    int thread_id =
        o->GetObjectForDotSeparatedPath("thread_id")->GetIntegerValue();
    result = Sprintf("Thread %d created", thread_id);
  }

  if (path == "locs") {
    std::string type =
        o->GetAsDictionary()->GetValueForKey("type")->GetStringValue();
    int thread_id =
        o->GetObjectForDotSeparatedPath("thread_id")->GetIntegerValue();
    int fd =
        o->GetObjectForDotSeparatedPath("file_descriptor")->GetIntegerValue();
    if (type == "heap") {
      result = Sprintf("Heap block allocated by thread %d", thread_id);
    } else if (type == "fd") {
      result =
          Sprintf("File descriptor %d created by thread %t", fd, thread_id);
    }
  }

  if (path == "mutexes") {
    int mutex_id =
        o->GetObjectForDotSeparatedPath("mutex_id")->GetIntegerValue();

    result = Sprintf("Mutex M%d created", mutex_id);
  }

  if (path == "stacks") {
    int thread_id =
        o->GetObjectForDotSeparatedPath("thread_id")->GetIntegerValue();
    result = Sprintf("Thread %d", thread_id);
  }

  result[0] = toupper(result[0]);

  return result;
}

static void AddThreadsForPath(const std::string &path,
                              ThreadCollectionSP threads, ProcessSP process_sp,
                              StructuredData::ObjectSP info) {
  info->GetObjectForDotSeparatedPath(path)->GetAsArray()->ForEach(
      [process_sp, threads, path, info](StructuredData::Object *o) -> bool {
        std::vector<lldb::addr_t> pcs;
        o->GetObjectForDotSeparatedPath("trace")->GetAsArray()->ForEach(
            [&pcs](StructuredData::Object *pc) -> bool {
              pcs.push_back(pc->GetAsInteger()->GetValue());
              return true;
            });

        if (pcs.size() == 0)
          return true;

        StructuredData::ObjectSP thread_id_obj =
            o->GetObjectForDotSeparatedPath("thread_os_id");
        tid_t tid = thread_id_obj ? thread_id_obj->GetIntegerValue() : 0;

        uint32_t stop_id = 0;
        bool stop_id_is_valid = false;
        HistoryThread *history_thread =
            new HistoryThread(*process_sp, tid, pcs, stop_id, stop_id_is_valid);
        ThreadSP new_thread_sp(history_thread);
        new_thread_sp->SetName(GenerateThreadName(path, o, info).c_str());

        // Save this in the Process' ExtendedThreadList so a strong pointer
        // retains the object
        process_sp->GetExtendedThreadList().AddThread(new_thread_sp);
        threads->AddThread(new_thread_sp);

        return true;
      });
}

lldb::ThreadCollectionSP
ThreadSanitizerRuntime::GetBacktracesFromExtendedStopInfo(
    StructuredData::ObjectSP info) {
  ThreadCollectionSP threads;
  threads.reset(new ThreadCollection());

  if (info->GetObjectForDotSeparatedPath("instrumentation_class")
          ->GetStringValue() != "ThreadSanitizer")
    return threads;

  ProcessSP process_sp = GetProcessSP();

  AddThreadsForPath("stacks", threads, process_sp, info);
  AddThreadsForPath("mops", threads, process_sp, info);
  AddThreadsForPath("locs", threads, process_sp, info);
  AddThreadsForPath("mutexes", threads, process_sp, info);
  AddThreadsForPath("threads", threads, process_sp, info);

  return threads;
}
