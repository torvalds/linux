//===-- CommandObjectThread.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectThread.h"

#include "lldb/Core/SourceManager.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SystemRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Target/ThreadPlanStepOut.h"
#include "lldb/Target/ThreadPlanStepRange.h"
#include "lldb/Utility/State.h"
#include "lldb/lldb-private.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// CommandObjectIterateOverThreads
//-------------------------------------------------------------------------

class CommandObjectIterateOverThreads : public CommandObjectParsed {

  class UniqueStack {

  public:
    UniqueStack(std::stack<lldb::addr_t> stack_frames, uint32_t thread_index_id)
        : m_stack_frames(stack_frames) {
      m_thread_index_ids.push_back(thread_index_id);
    }

    void AddThread(uint32_t thread_index_id) const {
      m_thread_index_ids.push_back(thread_index_id);
    }

    const std::vector<uint32_t> &GetUniqueThreadIndexIDs() const {
      return m_thread_index_ids;
    }

    lldb::tid_t GetRepresentativeThread() const {
      return m_thread_index_ids.front();
    }

    friend bool inline operator<(const UniqueStack &lhs,
                                 const UniqueStack &rhs) {
      return lhs.m_stack_frames < rhs.m_stack_frames;
    }

  protected:
    // Mark the thread index as mutable, as we don't care about it from a const
    // perspective, we only care about m_stack_frames so we keep our std::set
    // sorted.
    mutable std::vector<uint32_t> m_thread_index_ids;
    std::stack<lldb::addr_t> m_stack_frames;
  };

public:
  CommandObjectIterateOverThreads(CommandInterpreter &interpreter,
                                  const char *name, const char *help,
                                  const char *syntax, uint32_t flags)
      : CommandObjectParsed(interpreter, name, help, syntax, flags) {}

  ~CommandObjectIterateOverThreads() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    result.SetStatus(m_success_return);

    bool all_threads = false;
    if (command.GetArgumentCount() == 0) {
      Thread *thread = m_exe_ctx.GetThreadPtr();
      if (!thread || !HandleOneThread(thread->GetID(), result))
        return false;
      return result.Succeeded();
    } else if (command.GetArgumentCount() == 1) {
      all_threads = ::strcmp(command.GetArgumentAtIndex(0), "all") == 0;
      m_unique_stacks = ::strcmp(command.GetArgumentAtIndex(0), "unique") == 0;
    }

    // Use tids instead of ThreadSPs to prevent deadlocking problems which
    // result from JIT-ing code while iterating over the (locked) ThreadSP
    // list.
    std::vector<lldb::tid_t> tids;

    if (all_threads || m_unique_stacks) {
      Process *process = m_exe_ctx.GetProcessPtr();

      for (ThreadSP thread_sp : process->Threads())
        tids.push_back(thread_sp->GetID());
    } else {
      const size_t num_args = command.GetArgumentCount();
      Process *process = m_exe_ctx.GetProcessPtr();

      std::lock_guard<std::recursive_mutex> guard(
          process->GetThreadList().GetMutex());

      for (size_t i = 0; i < num_args; i++) {
        bool success;

        uint32_t thread_idx = StringConvert::ToUInt32(
            command.GetArgumentAtIndex(i), 0, 0, &success);
        if (!success) {
          result.AppendErrorWithFormat("invalid thread specification: \"%s\"\n",
                                       command.GetArgumentAtIndex(i));
          result.SetStatus(eReturnStatusFailed);
          return false;
        }

        ThreadSP thread =
            process->GetThreadList().FindThreadByIndexID(thread_idx);

        if (!thread) {
          result.AppendErrorWithFormat("no thread with index: \"%s\"\n",
                                       command.GetArgumentAtIndex(i));
          result.SetStatus(eReturnStatusFailed);
          return false;
        }

        tids.push_back(thread->GetID());
      }
    }

    if (m_unique_stacks) {
      // Iterate over threads, finding unique stack buckets.
      std::set<UniqueStack> unique_stacks;
      for (const lldb::tid_t &tid : tids) {
        if (!BucketThread(tid, unique_stacks, result)) {
          return false;
        }
      }

      // Write the thread id's and unique call stacks to the output stream
      Stream &strm = result.GetOutputStream();
      Process *process = m_exe_ctx.GetProcessPtr();
      for (const UniqueStack &stack : unique_stacks) {
        // List the common thread ID's
        const std::vector<uint32_t> &thread_index_ids =
            stack.GetUniqueThreadIndexIDs();
        strm.Format("{0} thread(s) ", thread_index_ids.size());
        for (const uint32_t &thread_index_id : thread_index_ids) {
          strm.Format("#{0} ", thread_index_id);
        }
        strm.EOL();

        // List the shared call stack for this set of threads
        uint32_t representative_thread_id = stack.GetRepresentativeThread();
        ThreadSP thread = process->GetThreadList().FindThreadByIndexID(
            representative_thread_id);
        if (!HandleOneThread(thread->GetID(), result)) {
          return false;
        }
      }
    } else {
      uint32_t idx = 0;
      for (const lldb::tid_t &tid : tids) {
        if (idx != 0 && m_add_return)
          result.AppendMessage("");

        if (!HandleOneThread(tid, result))
          return false;

        ++idx;
      }
    }
    return result.Succeeded();
  }

protected:
  // Override this to do whatever you need to do for one thread.
  //
  // If you return false, the iteration will stop, otherwise it will proceed.
  // The result is set to m_success_return (defaults to
  // eReturnStatusSuccessFinishResult) before the iteration, so you only need
  // to set the return status in HandleOneThread if you want to indicate an
  // error. If m_add_return is true, a blank line will be inserted between each
  // of the listings (except the last one.)

  virtual bool HandleOneThread(lldb::tid_t, CommandReturnObject &result) = 0;

  bool BucketThread(lldb::tid_t tid, std::set<UniqueStack> &unique_stacks,
                    CommandReturnObject &result) {
    // Grab the corresponding thread for the given thread id.
    Process *process = m_exe_ctx.GetProcessPtr();
    Thread *thread = process->GetThreadList().FindThreadByID(tid).get();
    if (thread == nullptr) {
      result.AppendErrorWithFormatv("Failed to process thread #{0}.\n", tid);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // Collect the each frame's address for this call-stack
    std::stack<lldb::addr_t> stack_frames;
    const uint32_t frame_count = thread->GetStackFrameCount();
    for (uint32_t frame_index = 0; frame_index < frame_count; frame_index++) {
      const lldb::StackFrameSP frame_sp =
          thread->GetStackFrameAtIndex(frame_index);
      const lldb::addr_t pc = frame_sp->GetStackID().GetPC();
      stack_frames.push(pc);
    }

    uint32_t thread_index_id = thread->GetIndexID();
    UniqueStack new_unique_stack(stack_frames, thread_index_id);

    // Try to match the threads stack to and existing entry.
    std::set<UniqueStack>::iterator matching_stack =
        unique_stacks.find(new_unique_stack);
    if (matching_stack != unique_stacks.end()) {
      matching_stack->AddThread(thread_index_id);
    } else {
      unique_stacks.insert(new_unique_stack);
    }
    return true;
  }

  ReturnStatus m_success_return = eReturnStatusSuccessFinishResult;
  bool m_unique_stacks = false;
  bool m_add_return = true;
};

//-------------------------------------------------------------------------
// CommandObjectThreadBacktrace
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_thread_backtrace_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "count",    'c', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeCount,      "How many frames to display (-1 for all)" },
  { LLDB_OPT_SET_1, false, "start",    's', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeFrameIndex, "Frame in which to start the backtrace" },
  { LLDB_OPT_SET_1, false, "extended", 'e', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean,    "Show the extended backtrace, if available" }
    // clang-format on
};

class CommandObjectThreadBacktrace : public CommandObjectIterateOverThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'c': {
        int32_t input_count = 0;
        if (option_arg.getAsInteger(0, m_count)) {
          m_count = UINT32_MAX;
          error.SetErrorStringWithFormat(
              "invalid integer value for option '%c'", short_option);
        } else if (input_count < 0)
          m_count = UINT32_MAX;
      } break;
      case 's':
        if (option_arg.getAsInteger(0, m_start))
          error.SetErrorStringWithFormat(
              "invalid integer value for option '%c'", short_option);
        break;
      case 'e': {
        bool success;
        m_extended_backtrace =
            OptionArgParser::ToBoolean(option_arg, false, &success);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid boolean value for option '%c'", short_option);
      } break;
      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_count = UINT32_MAX;
      m_start = 0;
      m_extended_backtrace = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_thread_backtrace_options);
    }

    // Instance variables to hold the values for command options.
    uint32_t m_count;
    uint32_t m_start;
    bool m_extended_backtrace;
  };

  CommandObjectThreadBacktrace(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread backtrace",
            "Show thread call stacks.  Defaults to the current thread, thread "
            "indexes can be specified as arguments.\n"
            "Use the thread-index \"all\" to see all threads.\n"
            "Use the thread-index \"unique\" to see threads grouped by unique "
            "call stacks.",
            nullptr,
            eCommandRequiresProcess | eCommandRequiresThread |
                eCommandTryTargetAPILock | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused),
        m_options() {}

  ~CommandObjectThreadBacktrace() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExtendedBacktrace(Thread *thread, CommandReturnObject &result) {
    SystemRuntime *runtime = thread->GetProcess()->GetSystemRuntime();
    if (runtime) {
      Stream &strm = result.GetOutputStream();
      const std::vector<ConstString> &types =
          runtime->GetExtendedBacktraceTypes();
      for (auto type : types) {
        ThreadSP ext_thread_sp = runtime->GetExtendedBacktraceThread(
            thread->shared_from_this(), type);
        if (ext_thread_sp && ext_thread_sp->IsValid()) {
          const uint32_t num_frames_with_source = 0;
          const bool stop_format = false;
          if (ext_thread_sp->GetStatus(strm, m_options.m_start,
                                       m_options.m_count,
                                       num_frames_with_source,
                                       stop_format)) {
            DoExtendedBacktrace(ext_thread_sp.get(), result);
          }
        }
      }
    }
  }

  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat(
          "thread disappeared while computing backtraces: 0x%" PRIx64 "\n",
          tid);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Thread *thread = thread_sp.get();

    Stream &strm = result.GetOutputStream();

    // Only dump stack info if we processing unique stacks.
    const bool only_stacks = m_unique_stacks;

    // Don't show source context when doing backtraces.
    const uint32_t num_frames_with_source = 0;
    const bool stop_format = true;
    if (!thread->GetStatus(strm, m_options.m_start, m_options.m_count,
                           num_frames_with_source, stop_format, only_stacks)) {
      result.AppendErrorWithFormat(
          "error displaying backtrace for thread: \"0x%4.4x\"\n",
          thread->GetIndexID());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    if (m_options.m_extended_backtrace) {
      DoExtendedBacktrace(thread, result);
    }

    return true;
  }

  CommandOptions m_options;
};

enum StepScope { eStepScopeSource, eStepScopeInstruction };

static constexpr OptionEnumValueElement g_tri_running_mode[] = {
    {eOnlyThisThread, "this-thread", "Run only this thread"},
    {eAllThreads, "all-threads", "Run all threads"},
    {eOnlyDuringStepping, "while-stepping",
     "Run only this thread while stepping"} };

static constexpr OptionEnumValues TriRunningModes() {
  return OptionEnumValues(g_tri_running_mode);
}

static constexpr OptionDefinition g_thread_step_scope_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "step-in-avoids-no-debug",   'a', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypeBoolean,           "A boolean value that sets whether stepping into functions will step over functions with no debug information." },
  { LLDB_OPT_SET_1, false, "step-out-avoids-no-debug",  'A', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypeBoolean,           "A boolean value, if true stepping out of functions will continue to step out till it hits a function with debug information." },
  { LLDB_OPT_SET_1, false, "count",                     'c', OptionParser::eRequiredArgument, nullptr, {},                1, eArgTypeCount,             "How many times to perform the stepping operation - currently only supported for step-inst and next-inst." },
  { LLDB_OPT_SET_1, false, "end-linenumber",            'e', OptionParser::eRequiredArgument, nullptr, {},                1, eArgTypeLineNum,           "The line at which to stop stepping - defaults to the next line and only supported for step-in and step-over.  You can also pass the string 'block' to step to the end of the current block.  This is particularly useful in conjunction with --step-target to step through a complex calling sequence." },
  { LLDB_OPT_SET_1, false, "run-mode",                  'm', OptionParser::eRequiredArgument, nullptr, TriRunningModes(), 0, eArgTypeRunMode,           "Determine how to run other threads while stepping the current thread." },
  { LLDB_OPT_SET_1, false, "step-over-regexp",          'r', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypeRegularExpression, "A regular expression that defines function names to not to stop at when stepping in." },
  { LLDB_OPT_SET_1, false, "step-in-target",            't', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypeFunctionName,      "The name of the directly called function step in should stop at when stepping into." },
  { LLDB_OPT_SET_2, false, "python-class",              'C', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypePythonClass,       "The name of the class that will manage this step - only supported for Scripted Step." }
    // clang-format on
};

class CommandObjectThreadStepWithTypeAndScope : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a': {
        bool success;
        bool avoid_no_debug =
            OptionArgParser::ToBoolean(option_arg, true, &success);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid boolean value for option '%c'", short_option);
        else {
          m_step_in_avoid_no_debug =
              avoid_no_debug ? eLazyBoolYes : eLazyBoolNo;
        }
      } break;

      case 'A': {
        bool success;
        bool avoid_no_debug =
            OptionArgParser::ToBoolean(option_arg, true, &success);
        if (!success)
          error.SetErrorStringWithFormat(
              "invalid boolean value for option '%c'", short_option);
        else {
          m_step_out_avoid_no_debug =
              avoid_no_debug ? eLazyBoolYes : eLazyBoolNo;
        }
      } break;

      case 'c':
        if (option_arg.getAsInteger(0, m_step_count))
          error.SetErrorStringWithFormat("invalid step count '%s'",
                                         option_arg.str().c_str());
        break;

      case 'C':
        m_class_name.clear();
        m_class_name.assign(option_arg);
        break;

      case 'm': {
        auto enum_values = GetDefinitions()[option_idx].enum_values;
        m_run_mode = (lldb::RunMode)OptionArgParser::ToOptionEnum(
            option_arg, enum_values, eOnlyDuringStepping, error);
      } break;

      case 'e':
        if (option_arg == "block") {
          m_end_line_is_block_end = 1;
          break;
        }
        if (option_arg.getAsInteger(0, m_end_line))
          error.SetErrorStringWithFormat("invalid end line number '%s'",
                                         option_arg.str().c_str());
        break;

      case 'r':
        m_avoid_regexp.clear();
        m_avoid_regexp.assign(option_arg);
        break;

      case 't':
        m_step_in_target.clear();
        m_step_in_target.assign(option_arg);
        break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_step_in_avoid_no_debug = eLazyBoolCalculate;
      m_step_out_avoid_no_debug = eLazyBoolCalculate;
      m_run_mode = eOnlyDuringStepping;

      // Check if we are in Non-Stop mode
      TargetSP target_sp =
          execution_context ? execution_context->GetTargetSP() : TargetSP();
      if (target_sp && target_sp->GetNonStopModeEnabled())
        m_run_mode = eOnlyThisThread;

      m_avoid_regexp.clear();
      m_step_in_target.clear();
      m_class_name.clear();
      m_step_count = 1;
      m_end_line = LLDB_INVALID_LINE_NUMBER;
      m_end_line_is_block_end = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_thread_step_scope_options);
    }

    // Instance variables to hold the values for command options.
    LazyBool m_step_in_avoid_no_debug;
    LazyBool m_step_out_avoid_no_debug;
    RunMode m_run_mode;
    std::string m_avoid_regexp;
    std::string m_step_in_target;
    std::string m_class_name;
    uint32_t m_step_count;
    uint32_t m_end_line;
    bool m_end_line_is_block_end;
  };

  CommandObjectThreadStepWithTypeAndScope(CommandInterpreter &interpreter,
                                          const char *name, const char *help,
                                          const char *syntax,
                                          StepType step_type,
                                          StepScope step_scope)
      : CommandObjectParsed(interpreter, name, help, syntax,
                            eCommandRequiresProcess | eCommandRequiresThread |
                                eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused),
        m_step_type(step_type), m_step_scope(step_scope), m_options() {
    CommandArgumentEntry arg;
    CommandArgumentData thread_id_arg;

    // Define the first (and only) variant of this arg.
    thread_id_arg.arg_type = eArgTypeThreadID;
    thread_id_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(thread_id_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectThreadStepWithTypeAndScope() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    bool synchronous_execution = m_interpreter.GetSynchronous();

    const uint32_t num_threads = process->GetThreadList().GetSize();
    Thread *thread = nullptr;

    if (command.GetArgumentCount() == 0) {
      thread = GetDefaultThread();

      if (thread == nullptr) {
        result.AppendError("no selected thread in process");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    } else {
      const char *thread_idx_cstr = command.GetArgumentAtIndex(0);
      uint32_t step_thread_idx =
          StringConvert::ToUInt32(thread_idx_cstr, LLDB_INVALID_INDEX32);
      if (step_thread_idx == LLDB_INVALID_INDEX32) {
        result.AppendErrorWithFormat("invalid thread index '%s'.\n",
                                     thread_idx_cstr);
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
      thread =
          process->GetThreadList().FindThreadByIndexID(step_thread_idx).get();
      if (thread == nullptr) {
        result.AppendErrorWithFormat(
            "Thread index %u is out of range (valid values are 0 - %u).\n",
            step_thread_idx, num_threads);
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }

    if (m_step_type == eStepTypeScripted) {
      if (m_options.m_class_name.empty()) {
        result.AppendErrorWithFormat("empty class name for scripted step.");
        result.SetStatus(eReturnStatusFailed);
        return false;
      } else if (!m_interpreter.GetScriptInterpreter()->CheckObjectExists(
                     m_options.m_class_name.c_str())) {
        result.AppendErrorWithFormat(
            "class for scripted step: \"%s\" does not exist.",
            m_options.m_class_name.c_str());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }

    if (m_options.m_end_line != LLDB_INVALID_LINE_NUMBER &&
        m_step_type != eStepTypeInto) {
      result.AppendErrorWithFormat(
          "end line option is only valid for step into");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const bool abort_other_plans = false;
    const lldb::RunMode stop_other_threads = m_options.m_run_mode;

    // This is a bit unfortunate, but not all the commands in this command
    // object support only while stepping, so I use the bool for them.
    bool bool_stop_other_threads;
    if (m_options.m_run_mode == eAllThreads)
      bool_stop_other_threads = false;
    else if (m_options.m_run_mode == eOnlyDuringStepping)
      bool_stop_other_threads =
          (m_step_type != eStepTypeOut && m_step_type != eStepTypeScripted);
    else
      bool_stop_other_threads = true;

    ThreadPlanSP new_plan_sp;
    Status new_plan_status;

    if (m_step_type == eStepTypeInto) {
      StackFrame *frame = thread->GetStackFrameAtIndex(0).get();
      assert(frame != nullptr);

      if (frame->HasDebugInformation()) {
        AddressRange range;
        SymbolContext sc = frame->GetSymbolContext(eSymbolContextEverything);
        if (m_options.m_end_line != LLDB_INVALID_LINE_NUMBER) {
          Status error;
          if (!sc.GetAddressRangeFromHereToEndLine(m_options.m_end_line, range,
                                                   error)) {
            result.AppendErrorWithFormat("invalid end-line option: %s.",
                                         error.AsCString());
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        } else if (m_options.m_end_line_is_block_end) {
          Status error;
          Block *block = frame->GetSymbolContext(eSymbolContextBlock).block;
          if (!block) {
            result.AppendErrorWithFormat("Could not find the current block.");
            result.SetStatus(eReturnStatusFailed);
            return false;
          }

          AddressRange block_range;
          Address pc_address = frame->GetFrameCodeAddress();
          block->GetRangeContainingAddress(pc_address, block_range);
          if (!block_range.GetBaseAddress().IsValid()) {
            result.AppendErrorWithFormat(
                "Could not find the current block address.");
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
          lldb::addr_t pc_offset_in_block =
              pc_address.GetFileAddress() -
              block_range.GetBaseAddress().GetFileAddress();
          lldb::addr_t range_length =
              block_range.GetByteSize() - pc_offset_in_block;
          range = AddressRange(pc_address, range_length);
        } else {
          range = sc.line_entry.range;
        }

        new_plan_sp = thread->QueueThreadPlanForStepInRange(
            abort_other_plans, range,
            frame->GetSymbolContext(eSymbolContextEverything),
            m_options.m_step_in_target.c_str(), stop_other_threads,
            new_plan_status, m_options.m_step_in_avoid_no_debug,
            m_options.m_step_out_avoid_no_debug);

        if (new_plan_sp && !m_options.m_avoid_regexp.empty()) {
          ThreadPlanStepInRange *step_in_range_plan =
              static_cast<ThreadPlanStepInRange *>(new_plan_sp.get());
          step_in_range_plan->SetAvoidRegexp(m_options.m_avoid_regexp.c_str());
        }
      } else
        new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
            false, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeOver) {
      StackFrame *frame = thread->GetStackFrameAtIndex(0).get();

      if (frame->HasDebugInformation())
        new_plan_sp = thread->QueueThreadPlanForStepOverRange(
            abort_other_plans,
            frame->GetSymbolContext(eSymbolContextEverything).line_entry,
            frame->GetSymbolContext(eSymbolContextEverything),
            stop_other_threads, new_plan_status,
            m_options.m_step_out_avoid_no_debug);
      else
        new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
            true, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeTrace) {
      new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
          false, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeTraceOver) {
      new_plan_sp = thread->QueueThreadPlanForStepSingleInstruction(
          true, abort_other_plans, bool_stop_other_threads, new_plan_status);
    } else if (m_step_type == eStepTypeOut) {
      new_plan_sp = thread->QueueThreadPlanForStepOut(
          abort_other_plans, nullptr, false, bool_stop_other_threads, eVoteYes,
          eVoteNoOpinion, thread->GetSelectedFrameIndex(), new_plan_status,
          m_options.m_step_out_avoid_no_debug);
    } else if (m_step_type == eStepTypeScripted) {
      new_plan_sp = thread->QueueThreadPlanForStepScripted(
          abort_other_plans, m_options.m_class_name.c_str(),
          bool_stop_other_threads, new_plan_status);
    } else {
      result.AppendError("step type is not supported");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    // If we got a new plan, then set it to be a master plan (User level Plans
    // should be master plans so that they can be interruptible).  Then resume
    // the process.

    if (new_plan_sp) {
      new_plan_sp->SetIsMasterPlan(true);
      new_plan_sp->SetOkayToDiscard(false);

      if (m_options.m_step_count > 1) {
        if (!new_plan_sp->SetIterationCount(m_options.m_step_count)) {
          result.AppendWarning(
              "step operation does not support iteration count.");
        }
      }

      process->GetThreadList().SetSelectedThreadByID(thread->GetID());

      const uint32_t iohandler_id = process->GetIOHandlerID();

      StreamString stream;
      Status error;
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      if (!error.Success()) {
        result.AppendMessage(error.AsCString());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      // There is a race condition where this thread will return up the call
      // stack to the main command handler and show an (lldb) prompt before
      // HandlePrivateEvent (from PrivateStateThread) has a chance to call
      // PushProcessIOHandler().
      process->SyncIOHandler(iohandler_id, std::chrono::seconds(2));

      if (synchronous_execution) {
        // If any state changed events had anything to say, add that to the
        // result
        if (stream.GetSize() > 0)
          result.AppendMessage(stream.GetString());

        process->GetThreadList().SetSelectedThreadByID(thread->GetID());
        result.SetDidChangeProcessState(true);
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      } else {
        result.SetStatus(eReturnStatusSuccessContinuingNoResult);
      }
    } else {
      result.SetError(new_plan_status);
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }

protected:
  StepType m_step_type;
  StepScope m_step_scope;
  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectThreadContinue
//-------------------------------------------------------------------------

class CommandObjectThreadContinue : public CommandObjectParsed {
public:
  CommandObjectThreadContinue(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread continue",
            "Continue execution of the current target process.  One "
            "or more threads may be specified, by default all "
            "threads continue.",
            nullptr,
            eCommandRequiresThread | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {
    CommandArgumentEntry arg;
    CommandArgumentData thread_idx_arg;

    // Define the first (and only) variant of this arg.
    thread_idx_arg.arg_type = eArgTypeThreadIndex;
    thread_idx_arg.arg_repetition = eArgRepeatPlus;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(thread_idx_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectThreadContinue() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    bool synchronous_execution = m_interpreter.GetSynchronous();

    if (!m_interpreter.GetDebugger().GetSelectedTarget()) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process exists. Cannot continue");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    StateType state = process->GetState();
    if ((state == eStateCrashed) || (state == eStateStopped) ||
        (state == eStateSuspended)) {
      const size_t argc = command.GetArgumentCount();
      if (argc > 0) {
        // These two lines appear at the beginning of both blocks in this
        // if..else, but that is because we need to release the lock before
        // calling process->Resume below.
        std::lock_guard<std::recursive_mutex> guard(
            process->GetThreadList().GetMutex());
        const uint32_t num_threads = process->GetThreadList().GetSize();
        std::vector<Thread *> resume_threads;
        for (auto &entry : command.entries()) {
          uint32_t thread_idx;
          if (entry.ref.getAsInteger(0, thread_idx)) {
            result.AppendErrorWithFormat(
                "invalid thread index argument: \"%s\".\n", entry.c_str());
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
          Thread *thread =
              process->GetThreadList().FindThreadByIndexID(thread_idx).get();

          if (thread) {
            resume_threads.push_back(thread);
          } else {
            result.AppendErrorWithFormat("invalid thread index %u.\n",
                                         thread_idx);
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        }

        if (resume_threads.empty()) {
          result.AppendError("no valid thread indexes were specified");
          result.SetStatus(eReturnStatusFailed);
          return false;
        } else {
          if (resume_threads.size() == 1)
            result.AppendMessageWithFormat("Resuming thread: ");
          else
            result.AppendMessageWithFormat("Resuming threads: ");

          for (uint32_t idx = 0; idx < num_threads; ++idx) {
            Thread *thread =
                process->GetThreadList().GetThreadAtIndex(idx).get();
            std::vector<Thread *>::iterator this_thread_pos =
                find(resume_threads.begin(), resume_threads.end(), thread);

            if (this_thread_pos != resume_threads.end()) {
              resume_threads.erase(this_thread_pos);
              if (!resume_threads.empty())
                result.AppendMessageWithFormat("%u, ", thread->GetIndexID());
              else
                result.AppendMessageWithFormat("%u ", thread->GetIndexID());

              const bool override_suspend = true;
              thread->SetResumeState(eStateRunning, override_suspend);
            } else {
              thread->SetResumeState(eStateSuspended);
            }
          }
          result.AppendMessageWithFormat("in process %" PRIu64 "\n",
                                         process->GetID());
        }
      } else {
        // These two lines appear at the beginning of both blocks in this
        // if..else, but that is because we need to release the lock before
        // calling process->Resume below.
        std::lock_guard<std::recursive_mutex> guard(
            process->GetThreadList().GetMutex());
        const uint32_t num_threads = process->GetThreadList().GetSize();
        Thread *current_thread = GetDefaultThread();
        if (current_thread == nullptr) {
          result.AppendError("the process doesn't have a current thread");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
        // Set the actions that the threads should each take when resuming
        for (uint32_t idx = 0; idx < num_threads; ++idx) {
          Thread *thread = process->GetThreadList().GetThreadAtIndex(idx).get();
          if (thread == current_thread) {
            result.AppendMessageWithFormat("Resuming thread 0x%4.4" PRIx64
                                           " in process %" PRIu64 "\n",
                                           thread->GetID(), process->GetID());
            const bool override_suspend = true;
            thread->SetResumeState(eStateRunning, override_suspend);
          } else {
            thread->SetResumeState(eStateSuspended);
          }
        }
      }

      StreamString stream;
      Status error;
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      // We should not be holding the thread list lock when we do this.
      if (error.Success()) {
        result.AppendMessageWithFormat("Process %" PRIu64 " resuming\n",
                                       process->GetID());
        if (synchronous_execution) {
          // If any state changed events had anything to say, add that to the
          // result
          if (stream.GetSize() > 0)
            result.AppendMessage(stream.GetString());

          result.SetDidChangeProcessState(true);
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        } else {
          result.SetStatus(eReturnStatusSuccessContinuingNoResult);
        }
      } else {
        result.AppendErrorWithFormat("Failed to resume process: %s\n",
                                     error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendErrorWithFormat(
          "Process cannot be continued from its current state (%s).\n",
          StateAsCString(state));
      result.SetStatus(eReturnStatusFailed);
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectThreadUntil
//-------------------------------------------------------------------------

static constexpr OptionEnumValueElement g_duo_running_mode[] = {
    {eOnlyThisThread, "this-thread", "Run only this thread"},
    {eAllThreads, "all-threads", "Run all threads"} };

static constexpr OptionEnumValues DuoRunningModes() {
  return OptionEnumValues(g_duo_running_mode);
}

static constexpr OptionDefinition g_thread_until_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "frame",   'f', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypeFrameIndex,          "Frame index for until operation - defaults to 0" },
  { LLDB_OPT_SET_1, false, "thread",  't', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypeThreadIndex,         "Thread index for the thread for until operation" },
  { LLDB_OPT_SET_1, false, "run-mode",'m', OptionParser::eRequiredArgument, nullptr, DuoRunningModes(), 0, eArgTypeRunMode,             "Determine how to run other threads while stepping this one" },
  { LLDB_OPT_SET_1, false, "address", 'a', OptionParser::eRequiredArgument, nullptr, {},                0, eArgTypeAddressOrExpression, "Run until we reach the specified address, or leave the function - can be specified multiple times." }
    // clang-format on
};

class CommandObjectThreadUntil : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    uint32_t m_thread_idx;
    uint32_t m_frame_idx;

    CommandOptions()
        : Options(), m_thread_idx(LLDB_INVALID_THREAD_ID),
          m_frame_idx(LLDB_INVALID_FRAME_ID) {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a': {
        lldb::addr_t tmp_addr = OptionArgParser::ToAddress(
            execution_context, option_arg, LLDB_INVALID_ADDRESS, &error);
        if (error.Success())
          m_until_addrs.push_back(tmp_addr);
      } break;
      case 't':
        if (option_arg.getAsInteger(0, m_thread_idx)) {
          m_thread_idx = LLDB_INVALID_INDEX32;
          error.SetErrorStringWithFormat("invalid thread index '%s'",
                                         option_arg.str().c_str());
        }
        break;
      case 'f':
        if (option_arg.getAsInteger(0, m_frame_idx)) {
          m_frame_idx = LLDB_INVALID_FRAME_ID;
          error.SetErrorStringWithFormat("invalid frame index '%s'",
                                         option_arg.str().c_str());
        }
        break;
      case 'm': {
        auto enum_values = GetDefinitions()[option_idx].enum_values;
        lldb::RunMode run_mode = (lldb::RunMode)OptionArgParser::ToOptionEnum(
            option_arg, enum_values, eOnlyDuringStepping, error);

        if (error.Success()) {
          if (run_mode == eAllThreads)
            m_stop_others = false;
          else
            m_stop_others = true;
        }
      } break;
      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_thread_idx = LLDB_INVALID_THREAD_ID;
      m_frame_idx = 0;
      m_stop_others = false;
      m_until_addrs.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_thread_until_options);
    }

    uint32_t m_step_thread_idx;
    bool m_stop_others;
    std::vector<lldb::addr_t> m_until_addrs;

    // Instance variables to hold the values for command options.
  };

  CommandObjectThreadUntil(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread until",
            "Continue until a line number or address is reached by the "
            "current or specified thread.  Stops when returning from "
            "the current function as a safety measure.  "
            "The target line number(s) are given as arguments, and if more than one"
            " is provided, stepping will stop when the first one is hit.",
            nullptr,
            eCommandRequiresThread | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused),
        m_options() {
    CommandArgumentEntry arg;
    CommandArgumentData line_num_arg;

    // Define the first (and only) variant of this arg.
    line_num_arg.arg_type = eArgTypeLineNum;
    line_num_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(line_num_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectThreadUntil() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    bool synchronous_execution = m_interpreter.GetSynchronous();

    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("need a valid process to step");
      result.SetStatus(eReturnStatusFailed);
    } else {
      Thread *thread = nullptr;
      std::vector<uint32_t> line_numbers;

      if (command.GetArgumentCount() >= 1) {
        size_t num_args = command.GetArgumentCount();
        for (size_t i = 0; i < num_args; i++) {
          uint32_t line_number;
          line_number = StringConvert::ToUInt32(command.GetArgumentAtIndex(i),
                                                UINT32_MAX);
          if (line_number == UINT32_MAX) {
            result.AppendErrorWithFormat("invalid line number: '%s'.\n",
                                         command.GetArgumentAtIndex(i));
            result.SetStatus(eReturnStatusFailed);
            return false;
          } else
            line_numbers.push_back(line_number);
        }
      } else if (m_options.m_until_addrs.empty()) {
        result.AppendErrorWithFormat("No line number or address provided:\n%s",
                                     GetSyntax().str().c_str());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      if (m_options.m_thread_idx == LLDB_INVALID_THREAD_ID) {
        thread = GetDefaultThread();
      } else {
        thread = process->GetThreadList()
                     .FindThreadByIndexID(m_options.m_thread_idx)
                     .get();
      }

      if (thread == nullptr) {
        const uint32_t num_threads = process->GetThreadList().GetSize();
        result.AppendErrorWithFormat(
            "Thread index %u is out of range (valid values are 0 - %u).\n",
            m_options.m_thread_idx, num_threads);
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      const bool abort_other_plans = false;

      StackFrame *frame =
          thread->GetStackFrameAtIndex(m_options.m_frame_idx).get();
      if (frame == nullptr) {
        result.AppendErrorWithFormat(
            "Frame index %u is out of range for thread %u.\n",
            m_options.m_frame_idx, m_options.m_thread_idx);
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      ThreadPlanSP new_plan_sp;
      Status new_plan_status;

      if (frame->HasDebugInformation()) {
        // Finally we got here...  Translate the given line number to a bunch
        // of addresses:
        SymbolContext sc(frame->GetSymbolContext(eSymbolContextCompUnit));
        LineTable *line_table = nullptr;
        if (sc.comp_unit)
          line_table = sc.comp_unit->GetLineTable();

        if (line_table == nullptr) {
          result.AppendErrorWithFormat("Failed to resolve the line table for "
                                       "frame %u of thread index %u.\n",
                                       m_options.m_frame_idx,
                                       m_options.m_thread_idx);
          result.SetStatus(eReturnStatusFailed);
          return false;
        }

        LineEntry function_start;
        uint32_t index_ptr = 0, end_ptr;
        std::vector<addr_t> address_list;

        // Find the beginning & end index of the
        AddressRange fun_addr_range = sc.function->GetAddressRange();
        Address fun_start_addr = fun_addr_range.GetBaseAddress();
        line_table->FindLineEntryByAddress(fun_start_addr, function_start,
                                           &index_ptr);

        Address fun_end_addr(fun_start_addr.GetSection(),
                             fun_start_addr.GetOffset() +
                                 fun_addr_range.GetByteSize());

        bool all_in_function = true;

        line_table->FindLineEntryByAddress(fun_end_addr, function_start,
                                           &end_ptr);

        for (uint32_t line_number : line_numbers) {
          uint32_t start_idx_ptr = index_ptr;
          while (start_idx_ptr <= end_ptr) {
            LineEntry line_entry;
            const bool exact = false;
            start_idx_ptr = sc.comp_unit->FindLineEntry(
                start_idx_ptr, line_number, sc.comp_unit, exact, &line_entry);
            if (start_idx_ptr == UINT32_MAX)
              break;

            addr_t address =
                line_entry.range.GetBaseAddress().GetLoadAddress(target);
            if (address != LLDB_INVALID_ADDRESS) {
              if (fun_addr_range.ContainsLoadAddress(address, target))
                address_list.push_back(address);
              else
                all_in_function = false;
            }
            start_idx_ptr++;
          }
        }

        for (lldb::addr_t address : m_options.m_until_addrs) {
          if (fun_addr_range.ContainsLoadAddress(address, target))
            address_list.push_back(address);
          else
            all_in_function = false;
        }

        if (address_list.empty()) {
          if (all_in_function)
            result.AppendErrorWithFormat(
                "No line entries matching until target.\n");
          else
            result.AppendErrorWithFormat(
                "Until target outside of the current function.\n");

          result.SetStatus(eReturnStatusFailed);
          return false;
        }

        new_plan_sp = thread->QueueThreadPlanForStepUntil(
            abort_other_plans, &address_list.front(), address_list.size(),
            m_options.m_stop_others, m_options.m_frame_idx, new_plan_status);
        if (new_plan_sp) {
          // User level plans should be master plans so they can be interrupted
          // (e.g. by hitting a breakpoint) and other plans executed by the
          // user (stepping around the breakpoint) and then a "continue" will
          // resume the original plan.
          new_plan_sp->SetIsMasterPlan(true);
          new_plan_sp->SetOkayToDiscard(false);
        } else {
          result.SetError(new_plan_status);
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      } else {
        result.AppendErrorWithFormat(
            "Frame index %u of thread %u has no debug information.\n",
            m_options.m_frame_idx, m_options.m_thread_idx);
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      process->GetThreadList().SetSelectedThreadByID(m_options.m_thread_idx);

      StreamString stream;
      Status error;
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      if (error.Success()) {
        result.AppendMessageWithFormat("Process %" PRIu64 " resuming\n",
                                       process->GetID());
        if (synchronous_execution) {
          // If any state changed events had anything to say, add that to the
          // result
          if (stream.GetSize() > 0)
            result.AppendMessage(stream.GetString());

          result.SetDidChangeProcessState(true);
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        } else {
          result.SetStatus(eReturnStatusSuccessContinuingNoResult);
        }
      } else {
        result.AppendErrorWithFormat("Failed to resume process: %s.\n",
                                     error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      }
    }
    return result.Succeeded();
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectThreadSelect
//-------------------------------------------------------------------------

class CommandObjectThreadSelect : public CommandObjectParsed {
public:
  CommandObjectThreadSelect(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "thread select",
                            "Change the currently selected thread.", nullptr,
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    CommandArgumentEntry arg;
    CommandArgumentData thread_idx_arg;

    // Define the first (and only) variant of this arg.
    thread_idx_arg.arg_type = eArgTypeThreadIndex;
    thread_idx_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(thread_idx_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectThreadSelect() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one thread index argument:\nUsage: %s\n",
          m_cmd_name.c_str(), m_cmd_syntax.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    uint32_t index_id =
        StringConvert::ToUInt32(command.GetArgumentAtIndex(0), 0, 0);

    Thread *new_thread =
        process->GetThreadList().FindThreadByIndexID(index_id).get();
    if (new_thread == nullptr) {
      result.AppendErrorWithFormat("invalid thread #%s.\n",
                                   command.GetArgumentAtIndex(0));
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    process->GetThreadList().SetSelectedThreadByID(new_thread->GetID(), true);
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectThreadList
//-------------------------------------------------------------------------

class CommandObjectThreadList : public CommandObjectParsed {
public:
  CommandObjectThreadList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread list",
            "Show a summary of each thread in the current target process.",
            "thread list",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectThreadList() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Stream &strm = result.GetOutputStream();
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    Process *process = m_exe_ctx.GetProcessPtr();
    const bool only_threads_with_stop_reason = false;
    const uint32_t start_frame = 0;
    const uint32_t num_frames = 0;
    const uint32_t num_frames_with_source = 0;
    process->GetStatus(strm);
    process->GetThreadStatus(strm, only_threads_with_stop_reason, start_frame,
                             num_frames, num_frames_with_source, false);
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectThreadInfo
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_thread_info_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "json",      'j', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Display the thread info in JSON format." },
  { LLDB_OPT_SET_ALL, false, "stop-info", 's', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Display the extended stop info in JSON format." }
    // clang-format on
};

class CommandObjectThreadInfo : public CommandObjectIterateOverThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_json_thread = false;
      m_json_stopinfo = false;
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;
      Status error;

      switch (short_option) {
      case 'j':
        m_json_thread = true;
        break;

      case 's':
        m_json_stopinfo = true;
        break;

      default:
        return Status("invalid short option character '%c'", short_option);
      }
      return error;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_thread_info_options);
    }

    bool m_json_thread;
    bool m_json_stopinfo;
  };

  CommandObjectThreadInfo(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread info", "Show an extended summary of one or "
                                        "more threads.  Defaults to the "
                                        "current thread.",
            "thread info",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused),
        m_options() {
    m_add_return = false;
  }

  ~CommandObjectThreadInfo() override = default;

  Options *GetOptions() override { return &m_options; }

  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat("thread no longer exists: 0x%" PRIx64 "\n",
                                   tid);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Thread *thread = thread_sp.get();

    Stream &strm = result.GetOutputStream();
    if (!thread->GetDescription(strm, eDescriptionLevelFull,
                                m_options.m_json_thread,
                                m_options.m_json_stopinfo)) {
      result.AppendErrorWithFormat("error displaying info for thread: \"%d\"\n",
                                   thread->GetIndexID());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    return true;
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectThreadException
//-------------------------------------------------------------------------

class CommandObjectThreadException : public CommandObjectIterateOverThreads {
 public:
  CommandObjectThreadException(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread exception",
            "Display the current exception object for a thread. Defaults to "
            "the current thread.",
            "thread exception",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectThreadException() override = default;

  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat("thread no longer exists: 0x%" PRIx64 "\n",
                                   tid);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Stream &strm = result.GetOutputStream();
    ValueObjectSP exception_object_sp = thread_sp->GetCurrentException();
    if (exception_object_sp) {
      exception_object_sp->Dump(strm);
    }

    ThreadSP exception_thread_sp = thread_sp->GetCurrentExceptionBacktrace();
    if (exception_thread_sp && exception_thread_sp->IsValid()) {
      const uint32_t num_frames_with_source = 0;
      const bool stop_format = false;
      exception_thread_sp->GetStatus(strm, 0, UINT32_MAX,
                                     num_frames_with_source, stop_format);
    }

    return true;
  }
};

//-------------------------------------------------------------------------
// CommandObjectThreadReturn
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_thread_return_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "from-expression", 'x', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Return from the innermost expression evaluation." }
    // clang-format on
};

class CommandObjectThreadReturn : public CommandObjectRaw {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options(), m_from_expression(false) {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'x': {
        bool success;
        bool tmp_value =
            OptionArgParser::ToBoolean(option_arg, false, &success);
        if (success)
          m_from_expression = tmp_value;
        else {
          error.SetErrorStringWithFormat(
              "invalid boolean value '%s' for 'x' option",
              option_arg.str().c_str());
        }
      } break;
      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_from_expression = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_thread_return_options);
    }

    bool m_from_expression;

    // Instance variables to hold the values for command options.
  };

  CommandObjectThreadReturn(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "thread return",
                         "Prematurely return from a stack frame, "
                         "short-circuiting execution of newer frames "
                         "and optionally yielding a specified value.  Defaults "
                         "to the exiting the current stack "
                         "frame.",
                         "thread return",
                         eCommandRequiresFrame | eCommandTryTargetAPILock |
                             eCommandProcessMustBeLaunched |
                             eCommandProcessMustBePaused),
        m_options() {
    CommandArgumentEntry arg;
    CommandArgumentData expression_arg;

    // Define the first (and only) variant of this arg.
    expression_arg.arg_type = eArgTypeExpression;
    expression_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(expression_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectThreadReturn() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    // I am going to handle this by hand, because I don't want you to have to
    // say:
    // "thread return -- -5".
    if (command.startswith("-x")) {
      if (command.size() != 2U)
        result.AppendWarning("Return values ignored when returning from user "
                             "called expressions");

      Thread *thread = m_exe_ctx.GetThreadPtr();
      Status error;
      error = thread->UnwindInnermostExpression();
      if (!error.Success()) {
        result.AppendErrorWithFormat("Unwinding expression failed - %s.",
                                     error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      } else {
        bool success =
            thread->SetSelectedFrameByIndexNoisily(0, result.GetOutputStream());
        if (success) {
          m_exe_ctx.SetFrameSP(thread->GetSelectedFrame());
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat(
              "Could not select 0th frame after unwinding expression.");
          result.SetStatus(eReturnStatusFailed);
        }
      }
      return result.Succeeded();
    }

    ValueObjectSP return_valobj_sp;

    StackFrameSP frame_sp = m_exe_ctx.GetFrameSP();
    uint32_t frame_idx = frame_sp->GetFrameIndex();

    if (frame_sp->IsInlined()) {
      result.AppendError("Don't know how to return from inlined frames.");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (!command.empty()) {
      Target *target = m_exe_ctx.GetTargetPtr();
      EvaluateExpressionOptions options;

      options.SetUnwindOnError(true);
      options.SetUseDynamic(eNoDynamicValues);

      ExpressionResults exe_results = eExpressionSetupError;
      exe_results = target->EvaluateExpression(command, frame_sp.get(),
                                               return_valobj_sp, options);
      if (exe_results != eExpressionCompleted) {
        if (return_valobj_sp)
          result.AppendErrorWithFormat(
              "Error evaluating result expression: %s",
              return_valobj_sp->GetError().AsCString());
        else
          result.AppendErrorWithFormat(
              "Unknown error evaluating result expression.");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }

    Status error;
    ThreadSP thread_sp = m_exe_ctx.GetThreadSP();
    const bool broadcast = true;
    error = thread_sp->ReturnFromFrame(frame_sp, return_valobj_sp, broadcast);
    if (!error.Success()) {
      result.AppendErrorWithFormat(
          "Error returning from frame %d of thread %d: %s.", frame_idx,
          thread_sp->GetIndexID(), error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectThreadJump
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_thread_jump_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1,                                   false, "file",    'f', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eSourceFileCompletion, eArgTypeFilename,            "Specifies the source file to jump to." },
  { LLDB_OPT_SET_1,                                   true,  "line",    'l', OptionParser::eRequiredArgument, nullptr, {}, 0,                                         eArgTypeLineNum,             "Specifies the line number to jump to." },
  { LLDB_OPT_SET_2,                                   true,  "by",      'b', OptionParser::eRequiredArgument, nullptr, {}, 0,                                         eArgTypeOffset,              "Jumps by a relative line offset from the current line." },
  { LLDB_OPT_SET_3,                                   true,  "address", 'a', OptionParser::eRequiredArgument, nullptr, {}, 0,                                         eArgTypeAddressOrExpression, "Jumps to a specific address." },
  { LLDB_OPT_SET_1 | LLDB_OPT_SET_2 | LLDB_OPT_SET_3, false, "force",   'r', OptionParser::eNoArgument,       nullptr, {}, 0,                                         eArgTypeNone,                "Allows the PC to leave the current function." }
    // clang-format on
};

class CommandObjectThreadJump : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_filenames.Clear();
      m_line_num = 0;
      m_line_offset = 0;
      m_load_addr = LLDB_INVALID_ADDRESS;
      m_force = false;
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      const int short_option = m_getopt_table[option_idx].val;
      Status error;

      switch (short_option) {
      case 'f':
        m_filenames.AppendIfUnique(FileSpec(option_arg));
        if (m_filenames.GetSize() > 1)
          return Status("only one source file expected.");
        break;
      case 'l':
        if (option_arg.getAsInteger(0, m_line_num))
          return Status("invalid line number: '%s'.", option_arg.str().c_str());
        break;
      case 'b':
        if (option_arg.getAsInteger(0, m_line_offset))
          return Status("invalid line offset: '%s'.", option_arg.str().c_str());
        break;
      case 'a':
        m_load_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                                 LLDB_INVALID_ADDRESS, &error);
        break;
      case 'r':
        m_force = true;
        break;
      default:
        return Status("invalid short option character '%c'", short_option);
      }
      return error;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_thread_jump_options);
    }

    FileSpecList m_filenames;
    uint32_t m_line_num;
    int32_t m_line_offset;
    lldb::addr_t m_load_addr;
    bool m_force;
  };

  CommandObjectThreadJump(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "thread jump",
            "Sets the program counter to a new address.", "thread jump",
            eCommandRequiresFrame | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused),
        m_options() {}

  ~CommandObjectThreadJump() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    RegisterContext *reg_ctx = m_exe_ctx.GetRegisterContext();
    StackFrame *frame = m_exe_ctx.GetFramePtr();
    Thread *thread = m_exe_ctx.GetThreadPtr();
    Target *target = m_exe_ctx.GetTargetPtr();
    const SymbolContext &sym_ctx =
        frame->GetSymbolContext(eSymbolContextLineEntry);

    if (m_options.m_load_addr != LLDB_INVALID_ADDRESS) {
      // Use this address directly.
      Address dest = Address(m_options.m_load_addr);

      lldb::addr_t callAddr = dest.GetCallableLoadAddress(target);
      if (callAddr == LLDB_INVALID_ADDRESS) {
        result.AppendErrorWithFormat("Invalid destination address.");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      if (!reg_ctx->SetPC(callAddr)) {
        result.AppendErrorWithFormat("Error changing PC value for thread %d.",
                                     thread->GetIndexID());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    } else {
      // Pick either the absolute line, or work out a relative one.
      int32_t line = (int32_t)m_options.m_line_num;
      if (line == 0)
        line = sym_ctx.line_entry.line + m_options.m_line_offset;

      // Try the current file, but override if asked.
      FileSpec file = sym_ctx.line_entry.file;
      if (m_options.m_filenames.GetSize() == 1)
        file = m_options.m_filenames.GetFileSpecAtIndex(0);

      if (!file) {
        result.AppendErrorWithFormat(
            "No source file available for the current location.");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      std::string warnings;
      Status err = thread->JumpToLine(file, line, m_options.m_force, &warnings);

      if (err.Fail()) {
        result.SetError(err);
        return false;
      }

      if (!warnings.empty())
        result.AppendWarning(warnings.c_str());
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// Next are the subcommands of CommandObjectMultiwordThreadPlan
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// CommandObjectThreadPlanList
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_thread_plan_list_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "verbose",  'v', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Display more information about the thread plans" },
  { LLDB_OPT_SET_1, false, "internal", 'i', OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone, "Display internal as well as user thread plans" }
    // clang-format on
};

class CommandObjectThreadPlanList : public CommandObjectIterateOverThreads {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {
      // Keep default values of all options in one place: OptionParsingStarting
      // ()
      OptionParsingStarting(nullptr);
    }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'i':
        m_internal = true;
        break;
      case 'v':
        m_verbose = true;
        break;
      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_verbose = false;
      m_internal = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_thread_plan_list_options);
    }

    // Instance variables to hold the values for command options.
    bool m_verbose;
    bool m_internal;
  };

  CommandObjectThreadPlanList(CommandInterpreter &interpreter)
      : CommandObjectIterateOverThreads(
            interpreter, "thread plan list",
            "Show thread plans for one or more threads.  If no threads are "
            "specified, show the "
            "current thread.  Use the thread-index \"all\" to see all threads.",
            nullptr,
            eCommandRequiresProcess | eCommandRequiresThread |
                eCommandTryTargetAPILock | eCommandProcessMustBeLaunched |
                eCommandProcessMustBePaused),
        m_options() {}

  ~CommandObjectThreadPlanList() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool HandleOneThread(lldb::tid_t tid, CommandReturnObject &result) override {
    ThreadSP thread_sp =
        m_exe_ctx.GetProcessPtr()->GetThreadList().FindThreadByID(tid);
    if (!thread_sp) {
      result.AppendErrorWithFormat("thread no longer exists: 0x%" PRIx64 "\n",
                                   tid);
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Thread *thread = thread_sp.get();

    Stream &strm = result.GetOutputStream();
    DescriptionLevel desc_level = eDescriptionLevelFull;
    if (m_options.m_verbose)
      desc_level = eDescriptionLevelVerbose;

    thread->DumpThreadPlans(&strm, desc_level, m_options.m_internal, true);
    return true;
  }

  CommandOptions m_options;
};

class CommandObjectThreadPlanDiscard : public CommandObjectParsed {
public:
  CommandObjectThreadPlanDiscard(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "thread plan discard",
                            "Discards thread plans up to and including the "
                            "specified index (see 'thread plan list'.)  "
                            "Only user visible plans can be discarded.",
                            nullptr,
                            eCommandRequiresProcess | eCommandRequiresThread |
                                eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    CommandArgumentEntry arg;
    CommandArgumentData plan_index_arg;

    // Define the first (and only) variant of this arg.
    plan_index_arg.arg_type = eArgTypeUnsignedInteger;
    plan_index_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(plan_index_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectThreadPlanDiscard() override = default;

  bool DoExecute(Args &args, CommandReturnObject &result) override {
    Thread *thread = m_exe_ctx.GetThreadPtr();
    if (args.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat("Too many arguments, expected one - the "
                                   "thread plan index - but got %zu.",
                                   args.GetArgumentCount());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    bool success;
    uint32_t thread_plan_idx =
        StringConvert::ToUInt32(args.GetArgumentAtIndex(0), 0, 0, &success);
    if (!success) {
      result.AppendErrorWithFormat(
          "Invalid thread index: \"%s\" - should be unsigned int.",
          args.GetArgumentAtIndex(0));
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (thread_plan_idx == 0) {
      result.AppendErrorWithFormat(
          "You wouldn't really want me to discard the base thread plan.");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (thread->DiscardUserThreadPlansUpToIndex(thread_plan_idx)) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return true;
    } else {
      result.AppendErrorWithFormat(
          "Could not find User thread plan with index %s.",
          args.GetArgumentAtIndex(0));
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
  }
};

//-------------------------------------------------------------------------
// CommandObjectMultiwordThreadPlan
//-------------------------------------------------------------------------

class CommandObjectMultiwordThreadPlan : public CommandObjectMultiword {
public:
  CommandObjectMultiwordThreadPlan(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "plan",
            "Commands for managing thread plans that control execution.",
            "thread plan <subcommand> [<subcommand objects]") {
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectThreadPlanList(interpreter)));
    LoadSubCommand(
        "discard",
        CommandObjectSP(new CommandObjectThreadPlanDiscard(interpreter)));
  }

  ~CommandObjectMultiwordThreadPlan() override = default;
};

//-------------------------------------------------------------------------
// CommandObjectMultiwordThread
//-------------------------------------------------------------------------

CommandObjectMultiwordThread::CommandObjectMultiwordThread(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "thread", "Commands for operating on "
                                                    "one or more threads in "
                                                    "the current process.",
                             "thread <subcommand> [<subcommand-options>]") {
  LoadSubCommand("backtrace", CommandObjectSP(new CommandObjectThreadBacktrace(
                                  interpreter)));
  LoadSubCommand("continue",
                 CommandObjectSP(new CommandObjectThreadContinue(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectThreadList(interpreter)));
  LoadSubCommand("return",
                 CommandObjectSP(new CommandObjectThreadReturn(interpreter)));
  LoadSubCommand("jump",
                 CommandObjectSP(new CommandObjectThreadJump(interpreter)));
  LoadSubCommand("select",
                 CommandObjectSP(new CommandObjectThreadSelect(interpreter)));
  LoadSubCommand("until",
                 CommandObjectSP(new CommandObjectThreadUntil(interpreter)));
  LoadSubCommand("info",
                 CommandObjectSP(new CommandObjectThreadInfo(interpreter)));
  LoadSubCommand(
      "exception",
      CommandObjectSP(new CommandObjectThreadException(interpreter)));
  LoadSubCommand("step-in",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-in",
                     "Source level single step, stepping into calls.  Defaults "
                     "to current thread unless specified.",
                     nullptr, eStepTypeInto, eStepScopeSource)));

  LoadSubCommand("step-out",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-out",
                     "Finish executing the current stack frame and stop after "
                     "returning.  Defaults to current thread unless specified.",
                     nullptr, eStepTypeOut, eStepScopeSource)));

  LoadSubCommand("step-over",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-over",
                     "Source level single step, stepping over calls.  Defaults "
                     "to current thread unless specified.",
                     nullptr, eStepTypeOver, eStepScopeSource)));

  LoadSubCommand("step-inst",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-inst",
                     "Instruction level single step, stepping into calls.  "
                     "Defaults to current thread unless specified.",
                     nullptr, eStepTypeTrace, eStepScopeInstruction)));

  LoadSubCommand("step-inst-over",
                 CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
                     interpreter, "thread step-inst-over",
                     "Instruction level single step, stepping over calls.  "
                     "Defaults to current thread unless specified.",
                     nullptr, eStepTypeTraceOver, eStepScopeInstruction)));

  LoadSubCommand(
      "step-scripted",
      CommandObjectSP(new CommandObjectThreadStepWithTypeAndScope(
          interpreter, "thread step-scripted",
          "Step as instructed by the script class passed in the -C option.",
          nullptr, eStepTypeScripted, eStepScopeSource)));

  LoadSubCommand("plan", CommandObjectSP(new CommandObjectMultiwordThreadPlan(
                             interpreter)));
}

CommandObjectMultiwordThread::~CommandObjectMultiwordThread() = default;
