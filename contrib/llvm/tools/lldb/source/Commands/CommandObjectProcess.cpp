//===-- CommandObjectProcess.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectProcess.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/BreakpointSite.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

class CommandObjectProcessLaunchOrAttach : public CommandObjectParsed {
public:
  CommandObjectProcessLaunchOrAttach(CommandInterpreter &interpreter,
                                     const char *name, const char *help,
                                     const char *syntax, uint32_t flags,
                                     const char *new_process_action)
      : CommandObjectParsed(interpreter, name, help, syntax, flags),
        m_new_process_action(new_process_action) {}

  ~CommandObjectProcessLaunchOrAttach() override = default;

protected:
  bool StopProcessIfNecessary(Process *process, StateType &state,
                              CommandReturnObject &result) {
    state = eStateInvalid;
    if (process) {
      state = process->GetState();

      if (process->IsAlive() && state != eStateConnected) {
        char message[1024];
        if (process->GetState() == eStateAttaching)
          ::snprintf(message, sizeof(message),
                     "There is a pending attach, abort it and %s?",
                     m_new_process_action.c_str());
        else if (process->GetShouldDetach())
          ::snprintf(message, sizeof(message),
                     "There is a running process, detach from it and %s?",
                     m_new_process_action.c_str());
        else
          ::snprintf(message, sizeof(message),
                     "There is a running process, kill it and %s?",
                     m_new_process_action.c_str());

        if (!m_interpreter.Confirm(message, true)) {
          result.SetStatus(eReturnStatusFailed);
          return false;
        } else {
          if (process->GetShouldDetach()) {
            bool keep_stopped = false;
            Status detach_error(process->Detach(keep_stopped));
            if (detach_error.Success()) {
              result.SetStatus(eReturnStatusSuccessFinishResult);
              process = nullptr;
            } else {
              result.AppendErrorWithFormat(
                  "Failed to detach from process: %s\n",
                  detach_error.AsCString());
              result.SetStatus(eReturnStatusFailed);
            }
          } else {
            Status destroy_error(process->Destroy(false));
            if (destroy_error.Success()) {
              result.SetStatus(eReturnStatusSuccessFinishResult);
              process = nullptr;
            } else {
              result.AppendErrorWithFormat("Failed to kill process: %s\n",
                                           destroy_error.AsCString());
              result.SetStatus(eReturnStatusFailed);
            }
          }
        }
      }
    }
    return result.Succeeded();
  }

  std::string m_new_process_action;
};

//-------------------------------------------------------------------------
// CommandObjectProcessLaunch
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessLaunch
class CommandObjectProcessLaunch : public CommandObjectProcessLaunchOrAttach {
public:
  CommandObjectProcessLaunch(CommandInterpreter &interpreter)
      : CommandObjectProcessLaunchOrAttach(
            interpreter, "process launch",
            "Launch the executable in the debugger.", nullptr,
            eCommandRequiresTarget, "restart"),
        m_options() {
    CommandArgumentEntry arg;
    CommandArgumentData run_args_arg;

    // Define the first (and only) variant of this arg.
    run_args_arg.arg_type = eArgTypeRunArgs;
    run_args_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(run_args_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectProcessLaunch() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {

    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eDiskFileCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }

  Options *GetOptions() override { return &m_options; }

  const char *GetRepeatCommand(Args &current_command_args,
                               uint32_t index) override {
    // No repeat for "process launch"...
    return "";
  }

protected:
  bool DoExecute(Args &launch_args, CommandReturnObject &result) override {
    Debugger &debugger = m_interpreter.GetDebugger();
    Target *target = debugger.GetSelectedTarget().get();
    // If our listener is nullptr, users aren't allows to launch
    ModuleSP exe_module_sp = target->GetExecutableModule();

    if (exe_module_sp == nullptr) {
      result.AppendError("no file in target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    StateType state = eStateInvalid;

    if (!StopProcessIfNecessary(m_exe_ctx.GetProcessPtr(), state, result))
      return false;

    llvm::StringRef target_settings_argv0 = target->GetArg0();

    // Determine whether we will disable ASLR or leave it in the default state
    // (i.e. enabled if the platform supports it). First check if the process
    // launch options explicitly turn on/off
    // disabling ASLR.  If so, use that setting;
    // otherwise, use the 'settings target.disable-aslr' setting.
    bool disable_aslr = false;
    if (m_options.disable_aslr != eLazyBoolCalculate) {
      // The user specified an explicit setting on the process launch line.
      // Use it.
      disable_aslr = (m_options.disable_aslr == eLazyBoolYes);
    } else {
      // The user did not explicitly specify whether to disable ASLR.  Fall
      // back to the target.disable-aslr setting.
      disable_aslr = target->GetDisableASLR();
    }

    if (disable_aslr)
      m_options.launch_info.GetFlags().Set(eLaunchFlagDisableASLR);
    else
      m_options.launch_info.GetFlags().Clear(eLaunchFlagDisableASLR);

    if (target->GetDetachOnError())
      m_options.launch_info.GetFlags().Set(eLaunchFlagDetachOnError);

    if (target->GetDisableSTDIO())
      m_options.launch_info.GetFlags().Set(eLaunchFlagDisableSTDIO);

    m_options.launch_info.GetEnvironment() = target->GetEnvironment();

    if (!target_settings_argv0.empty()) {
      m_options.launch_info.GetArguments().AppendArgument(
          target_settings_argv0);
      m_options.launch_info.SetExecutableFile(
          exe_module_sp->GetPlatformFileSpec(), false);
    } else {
      m_options.launch_info.SetExecutableFile(
          exe_module_sp->GetPlatformFileSpec(), true);
    }

    if (launch_args.GetArgumentCount() == 0) {
      m_options.launch_info.GetArguments().AppendArguments(
          target->GetProcessLaunchInfo().GetArguments());
    } else {
      m_options.launch_info.GetArguments().AppendArguments(launch_args);
      // Save the arguments for subsequent runs in the current target.
      target->SetRunArguments(launch_args);
    }

    StreamString stream;
    Status error = target->Launch(m_options.launch_info, &stream);

    if (error.Success()) {
      ProcessSP process_sp(target->GetProcessSP());
      if (process_sp) {
        // There is a race condition where this thread will return up the call
        // stack to the main command handler and show an (lldb) prompt before
        // HandlePrivateEvent (from PrivateStateThread) has a chance to call
        // PushProcessIOHandler().
        process_sp->SyncIOHandler(0, std::chrono::seconds(2));

        llvm::StringRef data = stream.GetString();
        if (!data.empty())
          result.AppendMessage(data);
        const char *archname =
            exe_module_sp->GetArchitecture().GetArchitectureName();
        result.AppendMessageWithFormat(
            "Process %" PRIu64 " launched: '%s' (%s)\n", process_sp->GetID(),
            exe_module_sp->GetFileSpec().GetPath().c_str(), archname);
        result.SetStatus(eReturnStatusSuccessFinishResult);
        result.SetDidChangeProcessState(true);
      } else {
        result.AppendError(
            "no error returned from Target::Launch, and target has no process");
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendError(error.AsCString());
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }

protected:
  ProcessLaunchCommandOptions m_options;
};

//#define SET1 LLDB_OPT_SET_1
//#define SET2 LLDB_OPT_SET_2
//#define SET3 LLDB_OPT_SET_3
//
// OptionDefinition
// CommandObjectProcessLaunch::CommandOptions::g_option_table[] =
//{
//  // clang-format off
//  {SET1 | SET2 | SET3, false, "stop-at-entry", 's', OptionParser::eNoArgument,
//  nullptr, 0, eArgTypeNone,          "Stop at the entry point of the program
//  when launching a process."},
//  {SET1,               false, "stdin",         'i',
//  OptionParser::eRequiredArgument, nullptr, 0, eArgTypeDirectoryName,
//  "Redirect stdin for the process to <path>."},
//  {SET1,               false, "stdout",        'o',
//  OptionParser::eRequiredArgument, nullptr, 0, eArgTypeDirectoryName,
//  "Redirect stdout for the process to <path>."},
//  {SET1,               false, "stderr",        'e',
//  OptionParser::eRequiredArgument, nullptr, 0, eArgTypeDirectoryName,
//  "Redirect stderr for the process to <path>."},
//  {SET1 | SET2 | SET3, false, "plugin",        'p',
//  OptionParser::eRequiredArgument, nullptr, 0, eArgTypePlugin,        "Name of
//  the process plugin you want to use."},
//  {       SET2,        false, "tty",           't',
//  OptionParser::eOptionalArgument, nullptr, 0, eArgTypeDirectoryName, "Start
//  the process in a terminal. If <path> is specified, look for a terminal whose
//  name contains <path>, else start the process in a new terminal."},
//  {              SET3, false, "no-stdio",      'n', OptionParser::eNoArgument,
//  nullptr, 0, eArgTypeNone,          "Do not set up for terminal I/O to go to
//  running process."},
//  {SET1 | SET2 | SET3, false, "working-dir",   'w',
//  OptionParser::eRequiredArgument, nullptr, 0, eArgTypeDirectoryName, "Set the
//  current working directory to <path> when running the inferior."},
//  {0, false, nullptr, 0, 0, nullptr, 0, eArgTypeNone, nullptr}
//  // clang-format on
//};
//
//#undef SET1
//#undef SET2
//#undef SET3

//-------------------------------------------------------------------------
// CommandObjectProcessAttach
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_process_attach_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "continue",         'c', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,         "Immediately continue the process once attached." },
  { LLDB_OPT_SET_ALL, false, "plugin",           'P', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypePlugin,       "Name of the process plugin you want to use." },
  { LLDB_OPT_SET_1,   false, "pid",              'p', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypePid,          "The process ID of an existing process to attach to." },
  { LLDB_OPT_SET_2,   false, "name",             'n', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeProcessName,  "The name of the process to attach to." },
  { LLDB_OPT_SET_2,   false, "include-existing", 'i', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,         "Include existing processes when doing attach -w." },
  { LLDB_OPT_SET_2,   false, "waitfor",          'w', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,         "Wait for the process with <process-name> to launch." },
    // clang-format on
};

#pragma mark CommandObjectProcessAttach
class CommandObjectProcessAttach : public CommandObjectProcessLaunchOrAttach {
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
      case 'c':
        attach_info.SetContinueOnceAttached(true);
        break;

      case 'p': {
        lldb::pid_t pid;
        if (option_arg.getAsInteger(0, pid)) {
          error.SetErrorStringWithFormat("invalid process ID '%s'",
                                         option_arg.str().c_str());
        } else {
          attach_info.SetProcessID(pid);
        }
      } break;

      case 'P':
        attach_info.SetProcessPluginName(option_arg);
        break;

      case 'n':
        attach_info.GetExecutableFile().SetFile(option_arg,
                                                FileSpec::Style::native);
        break;

      case 'w':
        attach_info.SetWaitForLaunch(true);
        break;

      case 'i':
        attach_info.SetIgnoreExisting(false);
        break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      attach_info.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_process_attach_options);
    }

    bool HandleOptionArgumentCompletion(
        CompletionRequest &request, OptionElementVector &opt_element_vector,
        int opt_element_index, CommandInterpreter &interpreter) override {
      int opt_arg_pos = opt_element_vector[opt_element_index].opt_arg_pos;
      int opt_defs_index = opt_element_vector[opt_element_index].opt_defs_index;

      // We are only completing the name option for now...

      if (GetDefinitions()[opt_defs_index].short_option == 'n') {
        // Are we in the name?

        // Look to see if there is a -P argument provided, and if so use that
        // plugin, otherwise use the default plugin.

        const char *partial_name = nullptr;
        partial_name = request.GetParsedLine().GetArgumentAtIndex(opt_arg_pos);

        PlatformSP platform_sp(interpreter.GetPlatform(true));
        if (platform_sp) {
          ProcessInstanceInfoList process_infos;
          ProcessInstanceInfoMatch match_info;
          if (partial_name) {
            match_info.GetProcessInfo().GetExecutableFile().SetFile(
                partial_name, FileSpec::Style::native);
            match_info.SetNameMatchType(NameMatch::StartsWith);
          }
          platform_sp->FindProcesses(match_info, process_infos);
          const size_t num_matches = process_infos.GetSize();
          if (num_matches > 0) {
            for (size_t i = 0; i < num_matches; ++i) {
              request.AddCompletion(llvm::StringRef(
                  process_infos.GetProcessNameAtIndex(i),
                  process_infos.GetProcessNameLengthAtIndex(i)));
            }
          }
        }
      }

      return false;
    }

    // Instance variables to hold the values for command options.

    ProcessAttachInfo attach_info;
  };

  CommandObjectProcessAttach(CommandInterpreter &interpreter)
      : CommandObjectProcessLaunchOrAttach(
            interpreter, "process attach", "Attach to a process.",
            "process attach <cmd-options>", 0, "attach"),
        m_options() {}

  ~CommandObjectProcessAttach() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    PlatformSP platform_sp(
        m_interpreter.GetDebugger().GetPlatformList().GetSelectedPlatform());

    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    // N.B. The attach should be synchronous.  It doesn't help much to get the
    // prompt back between initiating the attach and the target actually
    // stopping.  So even if the interpreter is set to be asynchronous, we wait
    // for the stop ourselves here.

    StateType state = eStateInvalid;
    Process *process = m_exe_ctx.GetProcessPtr();

    if (!StopProcessIfNecessary(process, state, result))
      return false;

    if (target == nullptr) {
      // If there isn't a current target create one.
      TargetSP new_target_sp;
      Status error;

      error = m_interpreter.GetDebugger().GetTargetList().CreateTarget(
          m_interpreter.GetDebugger(), "", "", eLoadDependentsNo,
          nullptr, // No platform options
          new_target_sp);
      target = new_target_sp.get();
      if (target == nullptr || error.Fail()) {
        result.AppendError(error.AsCString("Error creating target"));
        return false;
      }
      m_interpreter.GetDebugger().GetTargetList().SetSelectedTarget(target);
    }

    // Record the old executable module, we want to issue a warning if the
    // process of attaching changed the current executable (like somebody said
    // "file foo" then attached to a PID whose executable was bar.)

    ModuleSP old_exec_module_sp = target->GetExecutableModule();
    ArchSpec old_arch_spec = target->GetArchitecture();

    if (command.GetArgumentCount()) {
      result.AppendErrorWithFormat("Invalid arguments for '%s'.\nUsage: %s\n",
                                   m_cmd_name.c_str(), m_cmd_syntax.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    m_interpreter.UpdateExecutionContext(nullptr);
    StreamString stream;
    const auto error = target->Attach(m_options.attach_info, &stream);
    if (error.Success()) {
      ProcessSP process_sp(target->GetProcessSP());
      if (process_sp) {
        result.AppendMessage(stream.GetString());
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
        result.SetDidChangeProcessState(true);
        result.SetAbnormalStopWasExpected(true);
      } else {
        result.AppendError(
            "no error returned from Target::Attach, and target has no process");
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendErrorWithFormat("attach failed: %s\n", error.AsCString());
      result.SetStatus(eReturnStatusFailed);
    }

    if (!result.Succeeded())
      return false;

    // Okay, we're done.  Last step is to warn if the executable module has
    // changed:
    char new_path[PATH_MAX];
    ModuleSP new_exec_module_sp(target->GetExecutableModule());
    if (!old_exec_module_sp) {
      // We might not have a module if we attached to a raw pid...
      if (new_exec_module_sp) {
        new_exec_module_sp->GetFileSpec().GetPath(new_path, PATH_MAX);
        result.AppendMessageWithFormat("Executable module set to \"%s\".\n",
                                       new_path);
      }
    } else if (old_exec_module_sp->GetFileSpec() !=
               new_exec_module_sp->GetFileSpec()) {
      char old_path[PATH_MAX];

      old_exec_module_sp->GetFileSpec().GetPath(old_path, PATH_MAX);
      new_exec_module_sp->GetFileSpec().GetPath(new_path, PATH_MAX);

      result.AppendWarningWithFormat(
          "Executable module changed from \"%s\" to \"%s\".\n", old_path,
          new_path);
    }

    if (!old_arch_spec.IsValid()) {
      result.AppendMessageWithFormat(
          "Architecture set to: %s.\n",
          target->GetArchitecture().GetTriple().getTriple().c_str());
    } else if (!old_arch_spec.IsExactMatch(target->GetArchitecture())) {
      result.AppendWarningWithFormat(
          "Architecture changed from %s to %s.\n",
          old_arch_spec.GetTriple().getTriple().c_str(),
          target->GetArchitecture().GetTriple().getTriple().c_str());
    }

    // This supports the use-case scenario of immediately continuing the
    // process once attached.
    if (m_options.attach_info.GetContinueOnceAttached())
      m_interpreter.HandleCommand("process continue", eLazyBoolNo, result);

    return result.Succeeded();
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectProcessContinue
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_process_continue_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "ignore-count",'i', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeUnsignedInteger, "Ignore <N> crossings of the breakpoint (if it exists) for the currently selected thread." }
    // clang-format on
};

#pragma mark CommandObjectProcessContinue

class CommandObjectProcessContinue : public CommandObjectParsed {
public:
  CommandObjectProcessContinue(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process continue",
            "Continue execution of all threads in the current process.",
            "process continue",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused),
        m_options() {}

  ~CommandObjectProcessContinue() override = default;

protected:
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
        if (option_arg.getAsInteger(0, m_ignore))
          error.SetErrorStringWithFormat(
              "invalid value for ignore option: \"%s\", should be a number.",
              option_arg.str().c_str());
        break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_ignore = 0;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_process_continue_options);
    }

    uint32_t m_ignore;
  };

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    bool synchronous_execution = m_interpreter.GetSynchronous();
    StateType state = process->GetState();
    if (state == eStateStopped) {
      if (command.GetArgumentCount() != 0) {
        result.AppendErrorWithFormat(
            "The '%s' command does not take any arguments.\n",
            m_cmd_name.c_str());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      if (m_options.m_ignore > 0) {
        ThreadSP sel_thread_sp(GetDefaultThread()->shared_from_this());
        if (sel_thread_sp) {
          StopInfoSP stop_info_sp = sel_thread_sp->GetStopInfo();
          if (stop_info_sp &&
              stop_info_sp->GetStopReason() == eStopReasonBreakpoint) {
            lldb::break_id_t bp_site_id =
                (lldb::break_id_t)stop_info_sp->GetValue();
            BreakpointSiteSP bp_site_sp(
                process->GetBreakpointSiteList().FindByID(bp_site_id));
            if (bp_site_sp) {
              const size_t num_owners = bp_site_sp->GetNumberOfOwners();
              for (size_t i = 0; i < num_owners; i++) {
                Breakpoint &bp_ref =
                    bp_site_sp->GetOwnerAtIndex(i)->GetBreakpoint();
                if (!bp_ref.IsInternal()) {
                  bp_ref.SetIgnoreCount(m_options.m_ignore);
                }
              }
            }
          }
        }
      }

      { // Scope for thread list mutex:
        std::lock_guard<std::recursive_mutex> guard(
            process->GetThreadList().GetMutex());
        const uint32_t num_threads = process->GetThreadList().GetSize();

        // Set the actions that the threads should each take when resuming
        for (uint32_t idx = 0; idx < num_threads; ++idx) {
          const bool override_suspend = false;
          process->GetThreadList().GetThreadAtIndex(idx)->SetResumeState(
              eStateRunning, override_suspend);
        }
      }

      const uint32_t iohandler_id = process->GetIOHandlerID();

      StreamString stream;
      Status error;
      if (synchronous_execution)
        error = process->ResumeSynchronous(&stream);
      else
        error = process->Resume();

      if (error.Success()) {
        // There is a race condition where this thread will return up the call
        // stack to the main command handler and show an (lldb) prompt before
        // HandlePrivateEvent (from PrivateStateThread) has a chance to call
        // PushProcessIOHandler().
        process->SyncIOHandler(iohandler_id, std::chrono::seconds(2));

        result.AppendMessageWithFormat("Process %" PRIu64 " resuming\n",
                                       process->GetID());
        if (synchronous_execution) {
          // If any state changed events had anything to say, add that to the
          // result
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
    } else {
      result.AppendErrorWithFormat(
          "Process cannot be continued from its current state (%s).\n",
          StateAsCString(state));
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }

  Options *GetOptions() override { return &m_options; }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectProcessDetach
//-------------------------------------------------------------------------
static constexpr OptionDefinition g_process_detach_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "keep-stopped", 's', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean, "Whether or not the process should be kept stopped on detach (if possible)." },
    // clang-format on
};

#pragma mark CommandObjectProcessDetach

class CommandObjectProcessDetach : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 's':
        bool tmp_result;
        bool success;
        tmp_result = OptionArgParser::ToBoolean(option_arg, false, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid boolean option: \"%s\"",
                                         option_arg.str().c_str());
        else {
          if (tmp_result)
            m_keep_stopped = eLazyBoolYes;
          else
            m_keep_stopped = eLazyBoolNo;
        }
        break;
      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_keep_stopped = eLazyBoolCalculate;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_process_detach_options);
    }

    // Instance variables to hold the values for command options.
    LazyBool m_keep_stopped;
  };

  CommandObjectProcessDetach(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process detach",
                            "Detach from the current target process.",
                            "process detach",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched),
        m_options() {}

  ~CommandObjectProcessDetach() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    // FIXME: This will be a Command Option:
    bool keep_stopped;
    if (m_options.m_keep_stopped == eLazyBoolCalculate) {
      // Check the process default:
      keep_stopped = process->GetDetachKeepsStopped();
    } else if (m_options.m_keep_stopped == eLazyBoolYes)
      keep_stopped = true;
    else
      keep_stopped = false;

    Status error(process->Detach(keep_stopped));
    if (error.Success()) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendErrorWithFormat("Detach failed: %s\n", error.AsCString());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    return result.Succeeded();
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectProcessConnect
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_process_connect_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "plugin", 'p', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypePlugin, "Name of the process plugin you want to use." },
    // clang-format on
};

#pragma mark CommandObjectProcessConnect

class CommandObjectProcessConnect : public CommandObjectParsed {
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
      case 'p':
        plugin_name.assign(option_arg);
        break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      plugin_name.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_process_connect_options);
    }

    // Instance variables to hold the values for command options.

    std::string plugin_name;
  };

  CommandObjectProcessConnect(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process connect",
                            "Connect to a remote debug service.",
                            "process connect <remote-url>", 0),
        m_options() {}

  ~CommandObjectProcessConnect() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one argument:\nUsage: %s\n", m_cmd_name.c_str(),
          m_cmd_syntax.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    if (process && process->IsAlive()) {
      result.AppendErrorWithFormat(
          "Process %" PRIu64
          " is currently being debugged, kill the process before connecting.\n",
          process->GetID());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const char *plugin_name = nullptr;
    if (!m_options.plugin_name.empty())
      plugin_name = m_options.plugin_name.c_str();

    Status error;
    Debugger &debugger = m_interpreter.GetDebugger();
    PlatformSP platform_sp = m_interpreter.GetPlatform(true);
    ProcessSP process_sp = platform_sp->ConnectProcess(
        command.GetArgumentAtIndex(0), plugin_name, debugger,
        debugger.GetSelectedTarget().get(), error);
    if (error.Fail() || process_sp == nullptr) {
      result.AppendError(error.AsCString("Error connecting to the process"));
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    return true;
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectProcessPlugin
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessPlugin

class CommandObjectProcessPlugin : public CommandObjectProxy {
public:
  CommandObjectProcessPlugin(CommandInterpreter &interpreter)
      : CommandObjectProxy(
            interpreter, "process plugin",
            "Send a custom command to the current target process plug-in.",
            "process plugin <args>", 0) {}

  ~CommandObjectProcessPlugin() override = default;

  CommandObject *GetProxyCommandObject() override {
    Process *process = m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process)
      return process->GetPluginCommandObject();
    return nullptr;
  }
};

//-------------------------------------------------------------------------
// CommandObjectProcessLoad
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_process_load_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "install", 'i', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypePath, "Install the shared library to the target. If specified without an argument then the library will installed in the current working directory." },
    // clang-format on
};

#pragma mark CommandObjectProcessLoad

class CommandObjectProcessLoad : public CommandObjectParsed {
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
        do_install = true;
        if (!option_arg.empty())
          install_path.SetFile(option_arg, FileSpec::Style::native);
        break;
      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      do_install = false;
      install_path.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_process_load_options);
    }

    // Instance variables to hold the values for command options.
    bool do_install;
    FileSpec install_path;
  };

  CommandObjectProcessLoad(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process load",
                            "Load a shared library into the current process.",
                            "process load <filename> [<filename> ...]",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused),
        m_options() {}

  ~CommandObjectProcessLoad() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();

    for (auto &entry : command.entries()) {
      Status error;
      PlatformSP platform = process->GetTarget().GetPlatform();
      llvm::StringRef image_path = entry.ref;
      uint32_t image_token = LLDB_INVALID_IMAGE_TOKEN;

      if (!m_options.do_install) {
        FileSpec image_spec(image_path);
        platform->ResolveRemotePath(image_spec, image_spec);
        image_token =
            platform->LoadImage(process, FileSpec(), image_spec, error);
      } else if (m_options.install_path) {
        FileSpec image_spec(image_path);
        FileSystem::Instance().Resolve(image_spec);
        platform->ResolveRemotePath(m_options.install_path,
                                    m_options.install_path);
        image_token = platform->LoadImage(process, image_spec,
                                          m_options.install_path, error);
      } else {
        FileSpec image_spec(image_path);
        FileSystem::Instance().Resolve(image_spec);
        image_token =
            platform->LoadImage(process, image_spec, FileSpec(), error);
      }

      if (image_token != LLDB_INVALID_IMAGE_TOKEN) {
        result.AppendMessageWithFormat(
            "Loading \"%s\"...ok\nImage %u loaded.\n", image_path.str().c_str(),
            image_token);
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendErrorWithFormat("failed to load '%s': %s",
                                     image_path.str().c_str(),
                                     error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      }
    }
    return result.Succeeded();
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectProcessUnload
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessUnload

class CommandObjectProcessUnload : public CommandObjectParsed {
public:
  CommandObjectProcessUnload(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process unload",
            "Unload a shared library from the current process using the index "
            "returned by a previous call to \"process load\".",
            "process unload <index>",
            eCommandRequiresProcess | eCommandTryTargetAPILock |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused) {}

  ~CommandObjectProcessUnload() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();

    for (auto &entry : command.entries()) {
      uint32_t image_token;
      if (entry.ref.getAsInteger(0, image_token)) {
        result.AppendErrorWithFormat("invalid image index argument '%s'",
                                     entry.ref.str().c_str());
        result.SetStatus(eReturnStatusFailed);
        break;
      } else {
        Status error(process->GetTarget().GetPlatform()->UnloadImage(
            process, image_token));
        if (error.Success()) {
          result.AppendMessageWithFormat(
              "Unloading shared library with index %u...ok\n", image_token);
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("failed to unload image: %s",
                                       error.AsCString());
          result.SetStatus(eReturnStatusFailed);
          break;
        }
      }
    }
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectProcessSignal
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessSignal

class CommandObjectProcessSignal : public CommandObjectParsed {
public:
  CommandObjectProcessSignal(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process signal",
                            "Send a UNIX signal to the current target process.",
                            nullptr, eCommandRequiresProcess |
                                         eCommandTryTargetAPILock) {
    CommandArgumentEntry arg;
    CommandArgumentData signal_arg;

    // Define the first (and only) variant of this arg.
    signal_arg.arg_type = eArgTypeUnixSignal;
    signal_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(signal_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectProcessSignal() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();

    if (command.GetArgumentCount() == 1) {
      int signo = LLDB_INVALID_SIGNAL_NUMBER;

      const char *signal_name = command.GetArgumentAtIndex(0);
      if (::isxdigit(signal_name[0]))
        signo =
            StringConvert::ToSInt32(signal_name, LLDB_INVALID_SIGNAL_NUMBER, 0);
      else
        signo = process->GetUnixSignals()->GetSignalNumberFromName(signal_name);

      if (signo == LLDB_INVALID_SIGNAL_NUMBER) {
        result.AppendErrorWithFormat("Invalid signal argument '%s'.\n",
                                     command.GetArgumentAtIndex(0));
        result.SetStatus(eReturnStatusFailed);
      } else {
        Status error(process->Signal(signo));
        if (error.Success()) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("Failed to send signal %i: %s\n", signo,
                                       error.AsCString());
          result.SetStatus(eReturnStatusFailed);
        }
      }
    } else {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one signal number argument:\nUsage: %s\n",
          m_cmd_name.c_str(), m_cmd_syntax.c_str());
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectProcessInterrupt
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessInterrupt

class CommandObjectProcessInterrupt : public CommandObjectParsed {
public:
  CommandObjectProcessInterrupt(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process interrupt",
                            "Interrupt the current target process.",
                            "process interrupt",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectProcessInterrupt() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process to halt");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (command.GetArgumentCount() == 0) {
      bool clear_thread_plans = true;
      Status error(process->Halt(clear_thread_plans));
      if (error.Success()) {
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendErrorWithFormat("Failed to halt process: %s\n",
                                     error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendErrorWithFormat("'%s' takes no arguments:\nUsage: %s\n",
                                   m_cmd_name.c_str(), m_cmd_syntax.c_str());
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectProcessKill
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessKill

class CommandObjectProcessKill : public CommandObjectParsed {
public:
  CommandObjectProcessKill(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process kill",
                            "Terminate the current target process.",
                            "process kill",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectProcessKill() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Process *process = m_exe_ctx.GetProcessPtr();
    if (process == nullptr) {
      result.AppendError("no process to kill");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (command.GetArgumentCount() == 0) {
      Status error(process->Destroy(true));
      if (error.Success()) {
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        result.AppendErrorWithFormat("Failed to kill process: %s\n",
                                     error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendErrorWithFormat("'%s' takes no arguments:\nUsage: %s\n",
                                   m_cmd_name.c_str(), m_cmd_syntax.c_str());
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectProcessSaveCore
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessSaveCore

class CommandObjectProcessSaveCore : public CommandObjectParsed {
public:
  CommandObjectProcessSaveCore(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process save-core",
                            "Save the current process as a core file using an "
                            "appropriate file type.",
                            "process save-core FILE",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched) {}

  ~CommandObjectProcessSaveCore() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    ProcessSP process_sp = m_exe_ctx.GetProcessSP();
    if (process_sp) {
      if (command.GetArgumentCount() == 1) {
        FileSpec output_file(command.GetArgumentAtIndex(0));
        Status error = PluginManager::SaveCore(process_sp, output_file);
        if (error.Success()) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat(
              "Failed to save core file for process: %s\n", error.AsCString());
          result.SetStatus(eReturnStatusFailed);
        }
      } else {
        result.AppendErrorWithFormat("'%s' takes one arguments:\nUsage: %s\n",
                                     m_cmd_name.c_str(), m_cmd_syntax.c_str());
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendError("invalid process");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectProcessStatus
//-------------------------------------------------------------------------
#pragma mark CommandObjectProcessStatus

class CommandObjectProcessStatus : public CommandObjectParsed {
public:
  CommandObjectProcessStatus(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "process status",
            "Show status and stop location for the current target process.",
            "process status",
            eCommandRequiresProcess | eCommandTryTargetAPILock) {}

  ~CommandObjectProcessStatus() override = default;

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Stream &strm = result.GetOutputStream();
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    // No need to check "process" for validity as eCommandRequiresProcess
    // ensures it is valid
    Process *process = m_exe_ctx.GetProcessPtr();
    const bool only_threads_with_stop_reason = true;
    const uint32_t start_frame = 0;
    const uint32_t num_frames = 1;
    const uint32_t num_frames_with_source = 1;
    const bool     stop_format = true;
    process->GetStatus(strm);
    process->GetThreadStatus(strm, only_threads_with_stop_reason, start_frame,
                             num_frames, num_frames_with_source, stop_format);
    return result.Succeeded();
  }
};

//-------------------------------------------------------------------------
// CommandObjectProcessHandle
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_process_handle_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "stop",   's', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean, "Whether or not the process should be stopped if the signal is received." },
  { LLDB_OPT_SET_1, false, "notify", 'n', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean, "Whether or not the debugger should notify the user if the signal is received." },
  { LLDB_OPT_SET_1, false, "pass",   'p', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeBoolean, "Whether or not the signal should be passed to the process." }
    // clang-format on
};

#pragma mark CommandObjectProcessHandle

class CommandObjectProcessHandle : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 's':
        stop = option_arg;
        break;
      case 'n':
        notify = option_arg;
        break;
      case 'p':
        pass = option_arg;
        break;
      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      stop.clear();
      notify.clear();
      pass.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_process_handle_options);
    }

    // Instance variables to hold the values for command options.

    std::string stop;
    std::string notify;
    std::string pass;
  };

  CommandObjectProcessHandle(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "process handle",
                            "Manage LLDB handling of OS signals for the "
                            "current target process.  Defaults to showing "
                            "current policy.",
                            nullptr),
        m_options() {
    SetHelpLong("\nIf no signals are specified, update them all.  If no update "
                "option is specified, list the current values.");
    CommandArgumentEntry arg;
    CommandArgumentData signal_arg;

    signal_arg.arg_type = eArgTypeUnixSignal;
    signal_arg.arg_repetition = eArgRepeatStar;

    arg.push_back(signal_arg);

    m_arguments.push_back(arg);
  }

  ~CommandObjectProcessHandle() override = default;

  Options *GetOptions() override { return &m_options; }

  bool VerifyCommandOptionValue(const std::string &option, int &real_value) {
    bool okay = true;
    bool success = false;
    bool tmp_value = OptionArgParser::ToBoolean(option, false, &success);

    if (success && tmp_value)
      real_value = 1;
    else if (success && !tmp_value)
      real_value = 0;
    else {
      // If the value isn't 'true' or 'false', it had better be 0 or 1.
      real_value = StringConvert::ToUInt32(option.c_str(), 3);
      if (real_value != 0 && real_value != 1)
        okay = false;
    }

    return okay;
  }

  void PrintSignalHeader(Stream &str) {
    str.Printf("NAME         PASS   STOP   NOTIFY\n");
    str.Printf("===========  =====  =====  ======\n");
  }

  void PrintSignal(Stream &str, int32_t signo, const char *sig_name,
                   const UnixSignalsSP &signals_sp) {
    bool stop;
    bool suppress;
    bool notify;

    str.Printf("%-11s  ", sig_name);
    if (signals_sp->GetSignalInfo(signo, suppress, stop, notify)) {
      bool pass = !suppress;
      str.Printf("%s  %s  %s", (pass ? "true " : "false"),
                 (stop ? "true " : "false"), (notify ? "true " : "false"));
    }
    str.Printf("\n");
  }

  void PrintSignalInformation(Stream &str, Args &signal_args,
                              int num_valid_signals,
                              const UnixSignalsSP &signals_sp) {
    PrintSignalHeader(str);

    if (num_valid_signals > 0) {
      size_t num_args = signal_args.GetArgumentCount();
      for (size_t i = 0; i < num_args; ++i) {
        int32_t signo = signals_sp->GetSignalNumberFromName(
            signal_args.GetArgumentAtIndex(i));
        if (signo != LLDB_INVALID_SIGNAL_NUMBER)
          PrintSignal(str, signo, signal_args.GetArgumentAtIndex(i),
                      signals_sp);
      }
    } else // Print info for ALL signals
    {
      int32_t signo = signals_sp->GetFirstSignalNumber();
      while (signo != LLDB_INVALID_SIGNAL_NUMBER) {
        PrintSignal(str, signo, signals_sp->GetSignalAsCString(signo),
                    signals_sp);
        signo = signals_sp->GetNextSignalNumber(signo);
      }
    }
  }

protected:
  bool DoExecute(Args &signal_args, CommandReturnObject &result) override {
    TargetSP target_sp = m_interpreter.GetDebugger().GetSelectedTarget();

    if (!target_sp) {
      result.AppendError("No current target;"
                         " cannot handle signals until you have a valid target "
                         "and process.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    ProcessSP process_sp = target_sp->GetProcessSP();

    if (!process_sp) {
      result.AppendError("No current process; cannot handle signals until you "
                         "have a valid process.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    int stop_action = -1;   // -1 means leave the current setting alone
    int pass_action = -1;   // -1 means leave the current setting alone
    int notify_action = -1; // -1 means leave the current setting alone

    if (!m_options.stop.empty() &&
        !VerifyCommandOptionValue(m_options.stop, stop_action)) {
      result.AppendError("Invalid argument for command option --stop; must be "
                         "true or false.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (!m_options.notify.empty() &&
        !VerifyCommandOptionValue(m_options.notify, notify_action)) {
      result.AppendError("Invalid argument for command option --notify; must "
                         "be true or false.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (!m_options.pass.empty() &&
        !VerifyCommandOptionValue(m_options.pass, pass_action)) {
      result.AppendError("Invalid argument for command option --pass; must be "
                         "true or false.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    size_t num_args = signal_args.GetArgumentCount();
    UnixSignalsSP signals_sp = process_sp->GetUnixSignals();
    int num_signals_set = 0;

    if (num_args > 0) {
      for (const auto &arg : signal_args) {
        int32_t signo = signals_sp->GetSignalNumberFromName(arg.c_str());
        if (signo != LLDB_INVALID_SIGNAL_NUMBER) {
          // Casting the actions as bools here should be okay, because
          // VerifyCommandOptionValue guarantees the value is either 0 or 1.
          if (stop_action != -1)
            signals_sp->SetShouldStop(signo, stop_action);
          if (pass_action != -1) {
            bool suppress = !pass_action;
            signals_sp->SetShouldSuppress(signo, suppress);
          }
          if (notify_action != -1)
            signals_sp->SetShouldNotify(signo, notify_action);
          ++num_signals_set;
        } else {
          result.AppendErrorWithFormat("Invalid signal name '%s'\n",
                                       arg.c_str());
        }
      }
    } else {
      // No signal specified, if any command options were specified, update ALL
      // signals.
      if ((notify_action != -1) || (stop_action != -1) || (pass_action != -1)) {
        if (m_interpreter.Confirm(
                "Do you really want to update all the signals?", false)) {
          int32_t signo = signals_sp->GetFirstSignalNumber();
          while (signo != LLDB_INVALID_SIGNAL_NUMBER) {
            if (notify_action != -1)
              signals_sp->SetShouldNotify(signo, notify_action);
            if (stop_action != -1)
              signals_sp->SetShouldStop(signo, stop_action);
            if (pass_action != -1) {
              bool suppress = !pass_action;
              signals_sp->SetShouldSuppress(signo, suppress);
            }
            signo = signals_sp->GetNextSignalNumber(signo);
          }
        }
      }
    }

    PrintSignalInformation(result.GetOutputStream(), signal_args,
                           num_signals_set, signals_sp);

    if (num_signals_set > 0)
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    else
      result.SetStatus(eReturnStatusFailed);

    return result.Succeeded();
  }

  CommandOptions m_options;
};

//-------------------------------------------------------------------------
// CommandObjectMultiwordProcess
//-------------------------------------------------------------------------

CommandObjectMultiwordProcess::CommandObjectMultiwordProcess(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "process",
          "Commands for interacting with processes on the current platform.",
          "process <subcommand> [<subcommand-options>]") {
  LoadSubCommand("attach",
                 CommandObjectSP(new CommandObjectProcessAttach(interpreter)));
  LoadSubCommand("launch",
                 CommandObjectSP(new CommandObjectProcessLaunch(interpreter)));
  LoadSubCommand("continue", CommandObjectSP(new CommandObjectProcessContinue(
                                 interpreter)));
  LoadSubCommand("connect",
                 CommandObjectSP(new CommandObjectProcessConnect(interpreter)));
  LoadSubCommand("detach",
                 CommandObjectSP(new CommandObjectProcessDetach(interpreter)));
  LoadSubCommand("load",
                 CommandObjectSP(new CommandObjectProcessLoad(interpreter)));
  LoadSubCommand("unload",
                 CommandObjectSP(new CommandObjectProcessUnload(interpreter)));
  LoadSubCommand("signal",
                 CommandObjectSP(new CommandObjectProcessSignal(interpreter)));
  LoadSubCommand("handle",
                 CommandObjectSP(new CommandObjectProcessHandle(interpreter)));
  LoadSubCommand("status",
                 CommandObjectSP(new CommandObjectProcessStatus(interpreter)));
  LoadSubCommand("interrupt", CommandObjectSP(new CommandObjectProcessInterrupt(
                                  interpreter)));
  LoadSubCommand("kill",
                 CommandObjectSP(new CommandObjectProcessKill(interpreter)));
  LoadSubCommand("plugin",
                 CommandObjectSP(new CommandObjectProcessPlugin(interpreter)));
  LoadSubCommand("save-core", CommandObjectSP(new CommandObjectProcessSaveCore(
                                  interpreter)));
}

CommandObjectMultiwordProcess::~CommandObjectMultiwordProcess() = default;
