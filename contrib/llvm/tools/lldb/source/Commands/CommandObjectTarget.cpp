//===-- CommandObjectTarget.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectTarget.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/DataFormatters/ValueObjectPrinter.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Host/Symbols.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupArchitecture.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Interpreter/OptionGroupFile.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupPlatform.h"
#include "lldb/Interpreter/OptionGroupString.h"
#include "lldb/Interpreter/OptionGroupUInt64.h"
#include "lldb/Interpreter/OptionGroupUUID.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Interpreter/OptionGroupVariable.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/FuncUnwinders.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadSpec.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Timer.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatAdapters.h"

#include <cerrno>

using namespace lldb;
using namespace lldb_private;

static void DumpTargetInfo(uint32_t target_idx, Target *target,
                           const char *prefix_cstr,
                           bool show_stopped_process_status, Stream &strm) {
  const ArchSpec &target_arch = target->GetArchitecture();

  Module *exe_module = target->GetExecutableModulePointer();
  char exe_path[PATH_MAX];
  bool exe_valid = false;
  if (exe_module)
    exe_valid = exe_module->GetFileSpec().GetPath(exe_path, sizeof(exe_path));

  if (!exe_valid)
    ::strcpy(exe_path, "<none>");

  strm.Printf("%starget #%u: %s", prefix_cstr ? prefix_cstr : "", target_idx,
              exe_path);

  uint32_t properties = 0;
  if (target_arch.IsValid()) {
    strm.Printf("%sarch=", properties++ > 0 ? ", " : " ( ");
    target_arch.DumpTriple(strm);
    properties++;
  }
  PlatformSP platform_sp(target->GetPlatform());
  if (platform_sp)
    strm.Printf("%splatform=%s", properties++ > 0 ? ", " : " ( ",
                platform_sp->GetName().GetCString());

  ProcessSP process_sp(target->GetProcessSP());
  bool show_process_status = false;
  if (process_sp) {
    lldb::pid_t pid = process_sp->GetID();
    StateType state = process_sp->GetState();
    if (show_stopped_process_status)
      show_process_status = StateIsStoppedState(state, true);
    const char *state_cstr = StateAsCString(state);
    if (pid != LLDB_INVALID_PROCESS_ID)
      strm.Printf("%spid=%" PRIu64, properties++ > 0 ? ", " : " ( ", pid);
    strm.Printf("%sstate=%s", properties++ > 0 ? ", " : " ( ", state_cstr);
  }
  if (properties > 0)
    strm.PutCString(" )\n");
  else
    strm.EOL();
  if (show_process_status) {
    const bool only_threads_with_stop_reason = true;
    const uint32_t start_frame = 0;
    const uint32_t num_frames = 1;
    const uint32_t num_frames_with_source = 1;
    const bool     stop_format = false;
    process_sp->GetStatus(strm);
    process_sp->GetThreadStatus(strm, only_threads_with_stop_reason,
                                start_frame, num_frames,
                                num_frames_with_source, stop_format);
  }
}

static uint32_t DumpTargetList(TargetList &target_list,
                               bool show_stopped_process_status, Stream &strm) {
  const uint32_t num_targets = target_list.GetNumTargets();
  if (num_targets) {
    TargetSP selected_target_sp(target_list.GetSelectedTarget());
    strm.PutCString("Current targets:\n");
    for (uint32_t i = 0; i < num_targets; ++i) {
      TargetSP target_sp(target_list.GetTargetAtIndex(i));
      if (target_sp) {
        bool is_selected = target_sp.get() == selected_target_sp.get();
        DumpTargetInfo(i, target_sp.get(), is_selected ? "* " : "  ",
                       show_stopped_process_status, strm);
      }
    }
  }
  return num_targets;
}

// Note that the negation in the argument name causes a slightly confusing
// mapping of the enum values,
static constexpr OptionEnumValueElement g_dependents_enumaration[] = {
    {eLoadDependentsDefault, "default",
     "Only load dependents when the target is an executable."},
    {eLoadDependentsNo, "true",
     "Don't load dependents, even if the target is an executable."},
    {eLoadDependentsYes, "false",
     "Load dependents, even if the target is not an executable."}};

static constexpr OptionDefinition g_dependents_options[] = {
    {LLDB_OPT_SET_1, false, "no-dependents", 'd',
     OptionParser::eOptionalArgument, nullptr,
     OptionEnumValues(g_dependents_enumaration), 0, eArgTypeValue,
     "Whether or not to load dependents when creating a target. If the option "
     "is not specified, the value is implicitly 'default'. If the option is "
     "specified but without a value, the value is implicitly 'true'."}};

class OptionGroupDependents : public OptionGroup {
public:
  OptionGroupDependents() {}

  ~OptionGroupDependents() override {}

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::makeArrayRef(g_dependents_options);
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override {
    Status error;

    // For compatibility no value means don't load dependents.
    if (option_value.empty()) {
      m_load_dependent_files = eLoadDependentsNo;
      return error;
    }

    const char short_option = g_dependents_options[option_idx].short_option;
    if (short_option == 'd') {
      LoadDependentFiles tmp_load_dependents;
      tmp_load_dependents = (LoadDependentFiles)OptionArgParser::ToOptionEnum(
          option_value, g_dependents_options[option_idx].enum_values, 0, error);
      if (error.Success())
        m_load_dependent_files = tmp_load_dependents;
    } else {
      error.SetErrorStringWithFormat("unrecognized short option '%c'",
                                     short_option);
    }

    return error;
  }

  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override {
    m_load_dependent_files = eLoadDependentsDefault;
  }

  LoadDependentFiles m_load_dependent_files;

private:
  DISALLOW_COPY_AND_ASSIGN(OptionGroupDependents);
};

#pragma mark CommandObjectTargetCreate

//-------------------------------------------------------------------------
// "target create"
//-------------------------------------------------------------------------

class CommandObjectTargetCreate : public CommandObjectParsed {
public:
  CommandObjectTargetCreate(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target create",
            "Create a target using the argument as the main executable.",
            nullptr),
        m_option_group(), m_arch_option(),
        m_core_file(LLDB_OPT_SET_1, false, "core", 'c', 0, eArgTypeFilename,
                    "Fullpath to a core file to use for this target."),
        m_platform_path(LLDB_OPT_SET_1, false, "platform-path", 'P', 0,
                        eArgTypePath,
                        "Path to the remote file to use for this target."),
        m_symbol_file(LLDB_OPT_SET_1, false, "symfile", 's', 0,
                      eArgTypeFilename,
                      "Fullpath to a stand alone debug "
                      "symbols file for when debug symbols "
                      "are not in the executable."),
        m_remote_file(
            LLDB_OPT_SET_1, false, "remote-file", 'r', 0, eArgTypeFilename,
            "Fullpath to the file on the remote host if debugging remotely."),
        m_add_dependents() {
    CommandArgumentEntry arg;
    CommandArgumentData file_arg;

    // Define the first (and only) variant of this arg.
    file_arg.arg_type = eArgTypeFilename;
    file_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(file_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);

    m_option_group.Append(&m_arch_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_core_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_platform_path, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_symbol_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_remote_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_add_dependents, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetCreate() override = default;

  Options *GetOptions() override { return &m_option_group; }

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eDiskFileCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    FileSpec core_file(m_core_file.GetOptionValue().GetCurrentValue());
    FileSpec remote_file(m_remote_file.GetOptionValue().GetCurrentValue());

    if (core_file) {
      if (!FileSystem::Instance().Exists(core_file)) {
        result.AppendErrorWithFormat("core file '%s' doesn't exist",
                                     core_file.GetPath().c_str());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
      if (!FileSystem::Instance().Readable(core_file)) {
        result.AppendErrorWithFormat("core file '%s' is not readable",
                                     core_file.GetPath().c_str());
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }

    if (argc == 1 || core_file || remote_file) {
      FileSpec symfile(m_symbol_file.GetOptionValue().GetCurrentValue());
      if (symfile) {
        if (FileSystem::Instance().Exists(symfile)) {
          if (!FileSystem::Instance().Readable(symfile)) {
            result.AppendErrorWithFormat("symbol file '%s' is not readable",
                                         symfile.GetPath().c_str());
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        } else {
          char symfile_path[PATH_MAX];
          symfile.GetPath(symfile_path, sizeof(symfile_path));
          result.AppendErrorWithFormat("invalid symbol file path '%s'",
                                       symfile_path);
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      }

      const char *file_path = command.GetArgumentAtIndex(0);
      static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
      Timer scoped_timer(func_cat, "(lldb) target create '%s'", file_path);
      FileSpec file_spec;

      if (file_path) {
        file_spec.SetFile(file_path, FileSpec::Style::native);
        FileSystem::Instance().Resolve(file_spec);
      }

      bool must_set_platform_path = false;

      Debugger &debugger = m_interpreter.GetDebugger();

      TargetSP target_sp;
      llvm::StringRef arch_cstr = m_arch_option.GetArchitectureName();
      Status error(debugger.GetTargetList().CreateTarget(
          debugger, file_path, arch_cstr,
          m_add_dependents.m_load_dependent_files, nullptr, target_sp));

      if (target_sp) {
        // Only get the platform after we create the target because we might
        // have switched platforms depending on what the arguments were to
        // CreateTarget() we can't rely on the selected platform.

        PlatformSP platform_sp = target_sp->GetPlatform();

        if (remote_file) {
          if (platform_sp) {
            // I have a remote file.. two possible cases
            if (file_spec && FileSystem::Instance().Exists(file_spec)) {
              // if the remote file does not exist, push it there
              if (!platform_sp->GetFileExists(remote_file)) {
                Status err = platform_sp->PutFile(file_spec, remote_file);
                if (err.Fail()) {
                  result.AppendError(err.AsCString());
                  result.SetStatus(eReturnStatusFailed);
                  return false;
                }
              }
            } else {
              // there is no local file and we need one
              // in order to make the remote ---> local transfer we need a
              // platform
              // TODO: if the user has passed in a --platform argument, use it
              // to fetch the right platform
              if (!platform_sp) {
                result.AppendError(
                    "unable to perform remote debugging without a platform");
                result.SetStatus(eReturnStatusFailed);
                return false;
              }
              if (file_path) {
                // copy the remote file to the local file
                Status err = platform_sp->GetFile(remote_file, file_spec);
                if (err.Fail()) {
                  result.AppendError(err.AsCString());
                  result.SetStatus(eReturnStatusFailed);
                  return false;
                }
              } else {
                // make up a local file
                result.AppendError("remote --> local transfer without local "
                                   "path is not implemented yet");
                result.SetStatus(eReturnStatusFailed);
                return false;
              }
            }
          } else {
            result.AppendError("no platform found for target");
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        }

        if (symfile || remote_file) {
          ModuleSP module_sp(target_sp->GetExecutableModule());
          if (module_sp) {
            if (symfile)
              module_sp->SetSymbolFileFileSpec(symfile);
            if (remote_file) {
              std::string remote_path = remote_file.GetPath();
              target_sp->SetArg0(remote_path.c_str());
              module_sp->SetPlatformFileSpec(remote_file);
            }
          }
        }

        debugger.GetTargetList().SetSelectedTarget(target_sp.get());
        if (must_set_platform_path) {
          ModuleSpec main_module_spec(file_spec);
          ModuleSP module_sp = target_sp->GetSharedModule(main_module_spec);
          if (module_sp)
            module_sp->SetPlatformFileSpec(remote_file);
        }
        if (core_file) {
          char core_path[PATH_MAX];
          core_file.GetPath(core_path, sizeof(core_path));
          if (FileSystem::Instance().Exists(core_file)) {
            if (!FileSystem::Instance().Readable(core_file)) {
              result.AppendMessageWithFormat(
                  "Core file '%s' is not readable.\n", core_path);
              result.SetStatus(eReturnStatusFailed);
              return false;
            }
            FileSpec core_file_dir;
            core_file_dir.GetDirectory() = core_file.GetDirectory();
            target_sp->GetExecutableSearchPaths().Append(core_file_dir);

            ProcessSP process_sp(target_sp->CreateProcess(
                m_interpreter.GetDebugger().GetListener(), llvm::StringRef(),
                &core_file));

            if (process_sp) {
              // Seems weird that we Launch a core file, but that is what we
              // do!
              error = process_sp->LoadCore();

              if (error.Fail()) {
                result.AppendError(
                    error.AsCString("can't find plug-in for core file"));
                result.SetStatus(eReturnStatusFailed);
                return false;
              } else {
                result.AppendMessageWithFormat(
                    "Core file '%s' (%s) was loaded.\n", core_path,
                    target_sp->GetArchitecture().GetArchitectureName());
                result.SetStatus(eReturnStatusSuccessFinishNoResult);
              }
            } else {
              result.AppendErrorWithFormat(
                  "Unable to find process plug-in for core file '%s'\n",
                  core_path);
              result.SetStatus(eReturnStatusFailed);
            }
          } else {
            result.AppendErrorWithFormat("Core file '%s' does not exist\n",
                                         core_path);
            result.SetStatus(eReturnStatusFailed);
          }
        } else {
          result.AppendMessageWithFormat(
              "Current executable set to '%s' (%s).\n", file_path,
              target_sp->GetArchitecture().GetArchitectureName());
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        }
      } else {
        result.AppendError(error.AsCString());
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendErrorWithFormat("'%s' takes exactly one executable path "
                                   "argument, or use the --core option.\n",
                                   m_cmd_name.c_str());
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }

private:
  OptionGroupOptions m_option_group;
  OptionGroupArchitecture m_arch_option;
  OptionGroupFile m_core_file;
  OptionGroupFile m_platform_path;
  OptionGroupFile m_symbol_file;
  OptionGroupFile m_remote_file;
  OptionGroupDependents m_add_dependents;
};

#pragma mark CommandObjectTargetList

//----------------------------------------------------------------------
// "target list"
//----------------------------------------------------------------------

class CommandObjectTargetList : public CommandObjectParsed {
public:
  CommandObjectTargetList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target list",
            "List all current targets in the current debug session.", nullptr) {
  }

  ~CommandObjectTargetList() override = default;

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.GetArgumentCount() == 0) {
      Stream &strm = result.GetOutputStream();

      bool show_stopped_process_status = false;
      if (DumpTargetList(m_interpreter.GetDebugger().GetTargetList(),
                         show_stopped_process_status, strm) == 0) {
        strm.PutCString("No targets.\n");
      }
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("the 'target list' command takes no arguments\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetSelect

//----------------------------------------------------------------------
// "target select"
//----------------------------------------------------------------------

class CommandObjectTargetSelect : public CommandObjectParsed {
public:
  CommandObjectTargetSelect(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target select",
            "Select a target as the current target by target index.", nullptr) {
  }

  ~CommandObjectTargetSelect() override = default;

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    if (args.GetArgumentCount() == 1) {
      bool success = false;
      const char *target_idx_arg = args.GetArgumentAtIndex(0);
      uint32_t target_idx =
          StringConvert::ToUInt32(target_idx_arg, UINT32_MAX, 0, &success);
      if (success) {
        TargetList &target_list = m_interpreter.GetDebugger().GetTargetList();
        const uint32_t num_targets = target_list.GetNumTargets();
        if (target_idx < num_targets) {
          TargetSP target_sp(target_list.GetTargetAtIndex(target_idx));
          if (target_sp) {
            Stream &strm = result.GetOutputStream();
            target_list.SetSelectedTarget(target_sp.get());
            bool show_stopped_process_status = false;
            DumpTargetList(target_list, show_stopped_process_status, strm);
            result.SetStatus(eReturnStatusSuccessFinishResult);
          } else {
            result.AppendErrorWithFormat("target #%u is NULL in target list\n",
                                         target_idx);
            result.SetStatus(eReturnStatusFailed);
          }
        } else {
          if (num_targets > 0) {
            result.AppendErrorWithFormat(
                "index %u is out of range, valid target indexes are 0 - %u\n",
                target_idx, num_targets - 1);
          } else {
            result.AppendErrorWithFormat(
                "index %u is out of range since there are no active targets\n",
                target_idx);
          }
          result.SetStatus(eReturnStatusFailed);
        }
      } else {
        result.AppendErrorWithFormat("invalid index string value '%s'\n",
                                     target_idx_arg);
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      result.AppendError(
          "'target select' takes a single argument: a target index\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetSelect

//----------------------------------------------------------------------
// "target delete"
//----------------------------------------------------------------------

class CommandObjectTargetDelete : public CommandObjectParsed {
public:
  CommandObjectTargetDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target delete",
                            "Delete one or more targets by target index.",
                            nullptr),
        m_option_group(), m_all_option(LLDB_OPT_SET_1, false, "all", 'a',
                                       "Delete all targets.", false, true),
        m_cleanup_option(
            LLDB_OPT_SET_1, false, "clean", 'c',
            "Perform extra cleanup to minimize memory consumption after "
            "deleting the target.  "
            "By default, LLDB will keep in memory any modules previously "
            "loaded by the target as well "
            "as all of its debug info.  Specifying --clean will unload all of "
            "these shared modules and "
            "cause them to be reparsed again the next time the target is run",
            false, true) {
    m_option_group.Append(&m_all_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_cleanup_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetDelete() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    const size_t argc = args.GetArgumentCount();
    std::vector<TargetSP> delete_target_list;
    TargetList &target_list = m_interpreter.GetDebugger().GetTargetList();
    TargetSP target_sp;

    if (m_all_option.GetOptionValue()) {
      for (int i = 0; i < target_list.GetNumTargets(); ++i)
        delete_target_list.push_back(target_list.GetTargetAtIndex(i));
    } else if (argc > 0) {
      const uint32_t num_targets = target_list.GetNumTargets();
      // Bail out if don't have any targets.
      if (num_targets == 0) {
        result.AppendError("no targets to delete");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }

      for (auto &entry : args.entries()) {
        uint32_t target_idx;
        if (entry.ref.getAsInteger(0, target_idx)) {
          result.AppendErrorWithFormat("invalid target index '%s'\n",
                                       entry.c_str());
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
        if (target_idx < num_targets) {
          target_sp = target_list.GetTargetAtIndex(target_idx);
          if (target_sp) {
            delete_target_list.push_back(target_sp);
            continue;
          }
        }
        if (num_targets > 1)
          result.AppendErrorWithFormat("target index %u is out of range, valid "
                                       "target indexes are 0 - %u\n",
                                       target_idx, num_targets - 1);
        else
          result.AppendErrorWithFormat(
              "target index %u is out of range, the only valid index is 0\n",
              target_idx);

        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    } else {
      target_sp = target_list.GetSelectedTarget();
      if (!target_sp) {
        result.AppendErrorWithFormat("no target is currently selected\n");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
      delete_target_list.push_back(target_sp);
    }

    const size_t num_targets_to_delete = delete_target_list.size();
    for (size_t idx = 0; idx < num_targets_to_delete; ++idx) {
      target_sp = delete_target_list[idx];
      target_list.DeleteTarget(target_sp);
      target_sp->Destroy();
    }
    // If "--clean" was specified, prune any orphaned shared modules from the
    // global shared module list
    if (m_cleanup_option.GetOptionValue()) {
      const bool mandatory = true;
      ModuleList::RemoveOrphanSharedModules(mandatory);
    }
    result.GetOutputStream().Printf("%u targets deleted.\n",
                                    (uint32_t)num_targets_to_delete);
    result.SetStatus(eReturnStatusSuccessFinishResult);

    return true;
  }

  OptionGroupOptions m_option_group;
  OptionGroupBoolean m_all_option;
  OptionGroupBoolean m_cleanup_option;
};

#pragma mark CommandObjectTargetVariable

//----------------------------------------------------------------------
// "target variable"
//----------------------------------------------------------------------

class CommandObjectTargetVariable : public CommandObjectParsed {
  static const uint32_t SHORT_OPTION_FILE = 0x66696c65; // 'file'
  static const uint32_t SHORT_OPTION_SHLB = 0x73686c62; // 'shlb'

public:
  CommandObjectTargetVariable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target variable",
                            "Read global variables for the current target, "
                            "before or while running a process.",
                            nullptr, eCommandRequiresTarget),
        m_option_group(),
        m_option_variable(false), // Don't include frame options
        m_option_format(eFormatDefault),
        m_option_compile_units(LLDB_OPT_SET_1, false, "file", SHORT_OPTION_FILE,
                               0, eArgTypeFilename,
                               "A basename or fullpath to a file that contains "
                               "global variables. This option can be "
                               "specified multiple times."),
        m_option_shared_libraries(
            LLDB_OPT_SET_1, false, "shlib", SHORT_OPTION_SHLB, 0,
            eArgTypeFilename,
            "A basename or fullpath to a shared library to use in the search "
            "for global "
            "variables. This option can be specified multiple times."),
        m_varobj_options() {
    CommandArgumentEntry arg;
    CommandArgumentData var_name_arg;

    // Define the first (and only) variant of this arg.
    var_name_arg.arg_type = eArgTypeVarName;
    var_name_arg.arg_repetition = eArgRepeatPlus;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(var_name_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);

    m_option_group.Append(&m_varobj_options, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_variable, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_format,
                          OptionGroupFormat::OPTION_GROUP_FORMAT |
                              OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_compile_units, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_option_shared_libraries, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetVariable() override = default;

  void DumpValueObject(Stream &s, VariableSP &var_sp, ValueObjectSP &valobj_sp,
                       const char *root_name) {
    DumpValueObjectOptions options(m_varobj_options.GetAsDumpOptions());

    if (!valobj_sp->GetTargetSP()->GetDisplayRuntimeSupportValues() &&
        valobj_sp->IsRuntimeSupportValue())
      return;

    switch (var_sp->GetScope()) {
    case eValueTypeVariableGlobal:
      if (m_option_variable.show_scope)
        s.PutCString("GLOBAL: ");
      break;

    case eValueTypeVariableStatic:
      if (m_option_variable.show_scope)
        s.PutCString("STATIC: ");
      break;

    case eValueTypeVariableArgument:
      if (m_option_variable.show_scope)
        s.PutCString("   ARG: ");
      break;

    case eValueTypeVariableLocal:
      if (m_option_variable.show_scope)
        s.PutCString(" LOCAL: ");
      break;

    case eValueTypeVariableThreadLocal:
      if (m_option_variable.show_scope)
        s.PutCString("THREAD: ");
      break;

    default:
      break;
    }

    if (m_option_variable.show_decl) {
      bool show_fullpaths = false;
      bool show_module = true;
      if (var_sp->DumpDeclaration(&s, show_fullpaths, show_module))
        s.PutCString(": ");
    }

    const Format format = m_option_format.GetFormat();
    if (format != eFormatDefault)
      options.SetFormat(format);

    options.SetRootValueObjectName(root_name);

    valobj_sp->Dump(s, options);
  }

  static size_t GetVariableCallback(void *baton, const char *name,
                                    VariableList &variable_list) {
    Target *target = static_cast<Target *>(baton);
    if (target) {
      return target->GetImages().FindGlobalVariables(ConstString(name),
                                                     UINT32_MAX, variable_list);
    }
    return 0;
  }

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DumpGlobalVariableList(const ExecutionContext &exe_ctx,
                              const SymbolContext &sc,
                              const VariableList &variable_list, Stream &s) {
    size_t count = variable_list.GetSize();
    if (count > 0) {
      if (sc.module_sp) {
        if (sc.comp_unit) {
          s.Printf("Global variables for %s in %s:\n",
                   sc.comp_unit->GetPath().c_str(),
                   sc.module_sp->GetFileSpec().GetPath().c_str());
        } else {
          s.Printf("Global variables for %s\n",
                   sc.module_sp->GetFileSpec().GetPath().c_str());
        }
      } else if (sc.comp_unit) {
        s.Printf("Global variables for %s\n", sc.comp_unit->GetPath().c_str());
      }

      for (uint32_t i = 0; i < count; ++i) {
        VariableSP var_sp(variable_list.GetVariableAtIndex(i));
        if (var_sp) {
          ValueObjectSP valobj_sp(ValueObjectVariable::Create(
              exe_ctx.GetBestExecutionContextScope(), var_sp));

          if (valobj_sp)
            DumpValueObject(s, var_sp, valobj_sp,
                            var_sp->GetName().GetCString());
        }
      }
    }
  }

  bool DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    const size_t argc = args.GetArgumentCount();
    Stream &s = result.GetOutputStream();

    if (argc > 0) {

      // TODO: Convert to entry-based iteration.  Requires converting
      // DumpValueObject.
      for (size_t idx = 0; idx < argc; ++idx) {
        VariableList variable_list;
        ValueObjectList valobj_list;

        const char *arg = args.GetArgumentAtIndex(idx);
        size_t matches = 0;
        bool use_var_name = false;
        if (m_option_variable.use_regex) {
          RegularExpression regex(llvm::StringRef::withNullAsEmpty(arg));
          if (!regex.IsValid()) {
            result.GetErrorStream().Printf(
                "error: invalid regular expression: '%s'\n", arg);
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
          use_var_name = true;
          matches = target->GetImages().FindGlobalVariables(regex, UINT32_MAX,
                                                            variable_list);
        } else {
          Status error(Variable::GetValuesForVariableExpressionPath(
              arg, m_exe_ctx.GetBestExecutionContextScope(),
              GetVariableCallback, target, variable_list, valobj_list));
          matches = variable_list.GetSize();
        }

        if (matches == 0) {
          result.GetErrorStream().Printf(
              "error: can't find global variable '%s'\n", arg);
          result.SetStatus(eReturnStatusFailed);
          return false;
        } else {
          for (uint32_t global_idx = 0; global_idx < matches; ++global_idx) {
            VariableSP var_sp(variable_list.GetVariableAtIndex(global_idx));
            if (var_sp) {
              ValueObjectSP valobj_sp(
                  valobj_list.GetValueObjectAtIndex(global_idx));
              if (!valobj_sp)
                valobj_sp = ValueObjectVariable::Create(
                    m_exe_ctx.GetBestExecutionContextScope(), var_sp);

              if (valobj_sp)
                DumpValueObject(s, var_sp, valobj_sp,
                                use_var_name ? var_sp->GetName().GetCString()
                                             : arg);
            }
          }
        }
      }
    } else {
      const FileSpecList &compile_units =
          m_option_compile_units.GetOptionValue().GetCurrentValue();
      const FileSpecList &shlibs =
          m_option_shared_libraries.GetOptionValue().GetCurrentValue();
      SymbolContextList sc_list;
      const size_t num_compile_units = compile_units.GetSize();
      const size_t num_shlibs = shlibs.GetSize();
      if (num_compile_units == 0 && num_shlibs == 0) {
        bool success = false;
        StackFrame *frame = m_exe_ctx.GetFramePtr();
        CompileUnit *comp_unit = nullptr;
        if (frame) {
          SymbolContext sc = frame->GetSymbolContext(eSymbolContextCompUnit);
          if (sc.comp_unit) {
            const bool can_create = true;
            VariableListSP comp_unit_varlist_sp(
                sc.comp_unit->GetVariableList(can_create));
            if (comp_unit_varlist_sp) {
              size_t count = comp_unit_varlist_sp->GetSize();
              if (count > 0) {
                DumpGlobalVariableList(m_exe_ctx, sc, *comp_unit_varlist_sp, s);
                success = true;
              }
            }
          }
        }
        if (!success) {
          if (frame) {
            if (comp_unit)
              result.AppendErrorWithFormat(
                  "no global variables in current compile unit: %s\n",
                  comp_unit->GetPath().c_str());
            else
              result.AppendErrorWithFormat(
                  "no debug information for frame %u\n",
                  frame->GetFrameIndex());
          } else
            result.AppendError("'target variable' takes one or more global "
                               "variable names as arguments\n");
          result.SetStatus(eReturnStatusFailed);
        }
      } else {
        SymbolContextList sc_list;
        const bool append = true;
        // We have one or more compile unit or shlib
        if (num_shlibs > 0) {
          for (size_t shlib_idx = 0; shlib_idx < num_shlibs; ++shlib_idx) {
            const FileSpec module_file(shlibs.GetFileSpecAtIndex(shlib_idx));
            ModuleSpec module_spec(module_file);

            ModuleSP module_sp(
                target->GetImages().FindFirstModule(module_spec));
            if (module_sp) {
              if (num_compile_units > 0) {
                for (size_t cu_idx = 0; cu_idx < num_compile_units; ++cu_idx)
                  module_sp->FindCompileUnits(
                      compile_units.GetFileSpecAtIndex(cu_idx), append,
                      sc_list);
              } else {
                SymbolContext sc;
                sc.module_sp = module_sp;
                sc_list.Append(sc);
              }
            } else {
              // Didn't find matching shlib/module in target...
              result.AppendErrorWithFormat(
                  "target doesn't contain the specified shared library: %s\n",
                  module_file.GetPath().c_str());
            }
          }
        } else {
          // No shared libraries, we just want to find globals for the compile
          // units files that were specified
          for (size_t cu_idx = 0; cu_idx < num_compile_units; ++cu_idx)
            target->GetImages().FindCompileUnits(
                compile_units.GetFileSpecAtIndex(cu_idx), append, sc_list);
        }

        const uint32_t num_scs = sc_list.GetSize();
        if (num_scs > 0) {
          SymbolContext sc;
          for (uint32_t sc_idx = 0; sc_idx < num_scs; ++sc_idx) {
            if (sc_list.GetContextAtIndex(sc_idx, sc)) {
              if (sc.comp_unit) {
                const bool can_create = true;
                VariableListSP comp_unit_varlist_sp(
                    sc.comp_unit->GetVariableList(can_create));
                if (comp_unit_varlist_sp)
                  DumpGlobalVariableList(m_exe_ctx, sc, *comp_unit_varlist_sp,
                                         s);
              } else if (sc.module_sp) {
                // Get all global variables for this module
                lldb_private::RegularExpression all_globals_regex(
                    llvm::StringRef(
                        ".")); // Any global with at least one character
                VariableList variable_list;
                sc.module_sp->FindGlobalVariables(all_globals_regex, UINT32_MAX,
                                                  variable_list);
                DumpGlobalVariableList(m_exe_ctx, sc, variable_list, s);
              }
            }
          }
        }
      }
    }

    if (m_interpreter.TruncationWarningNecessary()) {
      result.GetOutputStream().Printf(m_interpreter.TruncationWarningText(),
                                      m_cmd_name.c_str());
      m_interpreter.TruncationWarningGiven();
    }

    return result.Succeeded();
  }

  OptionGroupOptions m_option_group;
  OptionGroupVariable m_option_variable;
  OptionGroupFormat m_option_format;
  OptionGroupFileList m_option_compile_units;
  OptionGroupFileList m_option_shared_libraries;
  OptionGroupValueObjectDisplay m_varobj_options;
};

#pragma mark CommandObjectTargetModulesSearchPathsAdd

class CommandObjectTargetModulesSearchPathsAdd : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths add",
                            "Add new image search paths substitution pairs to "
                            "the current target.",
                            nullptr) {
    CommandArgumentEntry arg;
    CommandArgumentData old_prefix_arg;
    CommandArgumentData new_prefix_arg;

    // Define the first variant of this arg pair.
    old_prefix_arg.arg_type = eArgTypeOldPathPrefix;
    old_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // Define the first variant of this arg pair.
    new_prefix_arg.arg_type = eArgTypeNewPathPrefix;
    new_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // There are two required arguments that must always occur together, i.e.
    // an argument "pair".  Because they must always occur together, they are
    // treated as two variants of one argument rather than two independent
    // arguments.  Push them both into the first argument position for
    // m_arguments...

    arg.push_back(old_prefix_arg);
    arg.push_back(new_prefix_arg);

    m_arguments.push_back(arg);
  }

  ~CommandObjectTargetModulesSearchPathsAdd() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target) {
      const size_t argc = command.GetArgumentCount();
      if (argc & 1) {
        result.AppendError("add requires an even number of arguments\n");
        result.SetStatus(eReturnStatusFailed);
      } else {
        for (size_t i = 0; i < argc; i += 2) {
          const char *from = command.GetArgumentAtIndex(i);
          const char *to = command.GetArgumentAtIndex(i + 1);

          if (from[0] && to[0]) {
            Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_HOST);
            if (log) {
              log->Printf("target modules search path adding ImageSearchPath "
                          "pair: '%s' -> '%s'",
                          from, to);
            }
            bool last_pair = ((argc - i) == 2);
            target->GetImageSearchPathList().Append(
                ConstString(from), ConstString(to),
                last_pair); // Notify if this is the last pair
            result.SetStatus(eReturnStatusSuccessFinishNoResult);
          } else {
            if (from[0])
              result.AppendError("<path-prefix> can't be empty\n");
            else
              result.AppendError("<new-path-prefix> can't be empty\n");
            result.SetStatus(eReturnStatusFailed);
          }
        }
      }
    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsClear

class CommandObjectTargetModulesSearchPathsClear : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths clear",
                            "Clear all current image search path substitution "
                            "pairs from the current target.",
                            "target modules search-paths clear") {}

  ~CommandObjectTargetModulesSearchPathsClear() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target) {
      bool notify = true;
      target->GetImageSearchPathList().Clear(notify);
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsInsert

class CommandObjectTargetModulesSearchPathsInsert : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsInsert(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths insert",
                            "Insert a new image search path substitution pair "
                            "into the current target at the specified index.",
                            nullptr) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData index_arg;
    CommandArgumentData old_prefix_arg;
    CommandArgumentData new_prefix_arg;

    // Define the first and only variant of this arg.
    index_arg.arg_type = eArgTypeIndex;
    index_arg.arg_repetition = eArgRepeatPlain;

    // Put the one and only variant into the first arg for m_arguments:
    arg1.push_back(index_arg);

    // Define the first variant of this arg pair.
    old_prefix_arg.arg_type = eArgTypeOldPathPrefix;
    old_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // Define the first variant of this arg pair.
    new_prefix_arg.arg_type = eArgTypeNewPathPrefix;
    new_prefix_arg.arg_repetition = eArgRepeatPairPlus;

    // There are two required arguments that must always occur together, i.e.
    // an argument "pair".  Because they must always occur together, they are
    // treated as two variants of one argument rather than two independent
    // arguments.  Push them both into the same argument position for
    // m_arguments...

    arg2.push_back(old_prefix_arg);
    arg2.push_back(new_prefix_arg);

    // Add arguments to m_arguments.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectTargetModulesSearchPathsInsert() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target) {
      size_t argc = command.GetArgumentCount();
      // check for at least 3 arguments and an odd number of parameters
      if (argc >= 3 && argc & 1) {
        bool success = false;

        uint32_t insert_idx = StringConvert::ToUInt32(
            command.GetArgumentAtIndex(0), UINT32_MAX, 0, &success);

        if (!success) {
          result.AppendErrorWithFormat(
              "<index> parameter is not an integer: '%s'.\n",
              command.GetArgumentAtIndex(0));
          result.SetStatus(eReturnStatusFailed);
          return result.Succeeded();
        }

        // shift off the index
        command.Shift();
        argc = command.GetArgumentCount();

        for (uint32_t i = 0; i < argc; i += 2, ++insert_idx) {
          const char *from = command.GetArgumentAtIndex(i);
          const char *to = command.GetArgumentAtIndex(i + 1);

          if (from[0] && to[0]) {
            bool last_pair = ((argc - i) == 2);
            target->GetImageSearchPathList().Insert(
                ConstString(from), ConstString(to), insert_idx, last_pair);
            result.SetStatus(eReturnStatusSuccessFinishNoResult);
          } else {
            if (from[0])
              result.AppendError("<path-prefix> can't be empty\n");
            else
              result.AppendError("<new-path-prefix> can't be empty\n");
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        }
      } else {
        result.AppendError("insert requires at least three arguments\n");
        result.SetStatus(eReturnStatusFailed);
        return result.Succeeded();
      }

    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsList

class CommandObjectTargetModulesSearchPathsList : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules search-paths list",
                            "List all current image search path substitution "
                            "pairs in the current target.",
                            "target modules search-paths list") {}

  ~CommandObjectTargetModulesSearchPathsList() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target) {
      if (command.GetArgumentCount() != 0) {
        result.AppendError("list takes no arguments\n");
        result.SetStatus(eReturnStatusFailed);
        return result.Succeeded();
      }

      target->GetImageSearchPathList().Dump(&result.GetOutputStream());
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesSearchPathsQuery

class CommandObjectTargetModulesSearchPathsQuery : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSearchPathsQuery(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target modules search-paths query",
            "Transform a path using the first applicable image search path.",
            nullptr) {
    CommandArgumentEntry arg;
    CommandArgumentData path_arg;

    // Define the first (and only) variant of this arg.
    path_arg.arg_type = eArgTypeDirectoryName;
    path_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(path_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectTargetModulesSearchPathsQuery() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target) {
      if (command.GetArgumentCount() != 1) {
        result.AppendError("query requires one argument\n");
        result.SetStatus(eReturnStatusFailed);
        return result.Succeeded();
      }

      ConstString orig(command.GetArgumentAtIndex(0));
      ConstString transformed;
      if (target->GetImageSearchPathList().RemapPath(orig, transformed))
        result.GetOutputStream().Printf("%s\n", transformed.GetCString());
      else
        result.GetOutputStream().Printf("%s\n", orig.GetCString());

      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

//----------------------------------------------------------------------
// Static Helper functions
//----------------------------------------------------------------------
static void DumpModuleArchitecture(Stream &strm, Module *module,
                                   bool full_triple, uint32_t width) {
  if (module) {
    StreamString arch_strm;

    if (full_triple)
      module->GetArchitecture().DumpTriple(arch_strm);
    else
      arch_strm.PutCString(module->GetArchitecture().GetArchitectureName());
    std::string arch_str = arch_strm.GetString();

    if (width)
      strm.Printf("%-*s", width, arch_str.c_str());
    else
      strm.PutCString(arch_str);
  }
}

static void DumpModuleUUID(Stream &strm, Module *module) {
  if (module && module->GetUUID().IsValid())
    module->GetUUID().Dump(&strm);
  else
    strm.PutCString("                                    ");
}

static uint32_t DumpCompileUnitLineTable(CommandInterpreter &interpreter,
                                         Stream &strm, Module *module,
                                         const FileSpec &file_spec,
                                         bool load_addresses) {
  uint32_t num_matches = 0;
  if (module) {
    SymbolContextList sc_list;
    num_matches = module->ResolveSymbolContextsForFileSpec(
        file_spec, 0, false, eSymbolContextCompUnit, sc_list);

    for (uint32_t i = 0; i < num_matches; ++i) {
      SymbolContext sc;
      if (sc_list.GetContextAtIndex(i, sc)) {
        if (i > 0)
          strm << "\n\n";

        strm << "Line table for " << *static_cast<FileSpec *>(sc.comp_unit)
             << " in `" << module->GetFileSpec().GetFilename() << "\n";
        LineTable *line_table = sc.comp_unit->GetLineTable();
        if (line_table)
          line_table->GetDescription(
              &strm, interpreter.GetExecutionContext().GetTargetPtr(),
              lldb::eDescriptionLevelBrief);
        else
          strm << "No line table";
      }
    }
  }
  return num_matches;
}

static void DumpFullpath(Stream &strm, const FileSpec *file_spec_ptr,
                         uint32_t width) {
  if (file_spec_ptr) {
    if (width > 0) {
      std::string fullpath = file_spec_ptr->GetPath();
      strm.Printf("%-*s", width, fullpath.c_str());
      return;
    } else {
      file_spec_ptr->Dump(&strm);
      return;
    }
  }
  // Keep the width spacing correct if things go wrong...
  if (width > 0)
    strm.Printf("%-*s", width, "");
}

static void DumpDirectory(Stream &strm, const FileSpec *file_spec_ptr,
                          uint32_t width) {
  if (file_spec_ptr) {
    if (width > 0)
      strm.Printf("%-*s", width, file_spec_ptr->GetDirectory().AsCString(""));
    else
      file_spec_ptr->GetDirectory().Dump(&strm);
    return;
  }
  // Keep the width spacing correct if things go wrong...
  if (width > 0)
    strm.Printf("%-*s", width, "");
}

static void DumpBasename(Stream &strm, const FileSpec *file_spec_ptr,
                         uint32_t width) {
  if (file_spec_ptr) {
    if (width > 0)
      strm.Printf("%-*s", width, file_spec_ptr->GetFilename().AsCString(""));
    else
      file_spec_ptr->GetFilename().Dump(&strm);
    return;
  }
  // Keep the width spacing correct if things go wrong...
  if (width > 0)
    strm.Printf("%-*s", width, "");
}

static size_t DumpModuleObjfileHeaders(Stream &strm, ModuleList &module_list) {
  size_t num_dumped = 0;
  std::lock_guard<std::recursive_mutex> guard(module_list.GetMutex());
  const size_t num_modules = module_list.GetSize();
  if (num_modules > 0) {
    strm.Printf("Dumping headers for %" PRIu64 " module(s).\n",
                static_cast<uint64_t>(num_modules));
    strm.IndentMore();
    for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
      Module *module = module_list.GetModulePointerAtIndexUnlocked(image_idx);
      if (module) {
        if (num_dumped++ > 0) {
          strm.EOL();
          strm.EOL();
        }
        ObjectFile *objfile = module->GetObjectFile();
        if (objfile)
          objfile->Dump(&strm);
        else {
          strm.Format("No object file for module: {0:F}\n",
                      module->GetFileSpec());
        }
      }
    }
    strm.IndentLess();
  }
  return num_dumped;
}

static void DumpModuleSymtab(CommandInterpreter &interpreter, Stream &strm,
                             Module *module, SortOrder sort_order) {
  if (module) {
    SymbolVendor *sym_vendor = module->GetSymbolVendor();
    if (sym_vendor) {
      Symtab *symtab = sym_vendor->GetSymtab();
      if (symtab)
        symtab->Dump(&strm, interpreter.GetExecutionContext().GetTargetPtr(),
                     sort_order);
    }
  }
}

static void DumpModuleSections(CommandInterpreter &interpreter, Stream &strm,
                               Module *module) {
  if (module) {
    SectionList *section_list = module->GetSectionList();
    if (section_list) {
      strm.Printf("Sections for '%s' (%s):\n",
                  module->GetSpecificationDescription().c_str(),
                  module->GetArchitecture().GetArchitectureName());
      strm.IndentMore();
      section_list->Dump(&strm,
                         interpreter.GetExecutionContext().GetTargetPtr(), true,
                         UINT32_MAX);
      strm.IndentLess();
    }
  }
}

static bool DumpModuleSymbolVendor(Stream &strm, Module *module) {
  if (module) {
    SymbolVendor *symbol_vendor = module->GetSymbolVendor(true);
    if (symbol_vendor) {
      symbol_vendor->Dump(&strm);
      return true;
    }
  }
  return false;
}

static void DumpAddress(ExecutionContextScope *exe_scope,
                        const Address &so_addr, bool verbose, Stream &strm) {
  strm.IndentMore();
  strm.Indent("    Address: ");
  so_addr.Dump(&strm, exe_scope, Address::DumpStyleModuleWithFileAddress);
  strm.PutCString(" (");
  so_addr.Dump(&strm, exe_scope, Address::DumpStyleSectionNameOffset);
  strm.PutCString(")\n");
  strm.Indent("    Summary: ");
  const uint32_t save_indent = strm.GetIndentLevel();
  strm.SetIndentLevel(save_indent + 13);
  so_addr.Dump(&strm, exe_scope, Address::DumpStyleResolvedDescription);
  strm.SetIndentLevel(save_indent);
  // Print out detailed address information when verbose is enabled
  if (verbose) {
    strm.EOL();
    so_addr.Dump(&strm, exe_scope, Address::DumpStyleDetailedSymbolContext);
  }
  strm.IndentLess();
}

static bool LookupAddressInModule(CommandInterpreter &interpreter, Stream &strm,
                                  Module *module, uint32_t resolve_mask,
                                  lldb::addr_t raw_addr, lldb::addr_t offset,
                                  bool verbose) {
  if (module) {
    lldb::addr_t addr = raw_addr - offset;
    Address so_addr;
    SymbolContext sc;
    Target *target = interpreter.GetExecutionContext().GetTargetPtr();
    if (target && !target->GetSectionLoadList().IsEmpty()) {
      if (!target->GetSectionLoadList().ResolveLoadAddress(addr, so_addr))
        return false;
      else if (so_addr.GetModule().get() != module)
        return false;
    } else {
      if (!module->ResolveFileAddress(addr, so_addr))
        return false;
    }

    ExecutionContextScope *exe_scope =
        interpreter.GetExecutionContext().GetBestExecutionContextScope();
    DumpAddress(exe_scope, so_addr, verbose, strm);
    //        strm.IndentMore();
    //        strm.Indent ("    Address: ");
    //        so_addr.Dump (&strm, exe_scope,
    //        Address::DumpStyleModuleWithFileAddress);
    //        strm.PutCString (" (");
    //        so_addr.Dump (&strm, exe_scope,
    //        Address::DumpStyleSectionNameOffset);
    //        strm.PutCString (")\n");
    //        strm.Indent ("    Summary: ");
    //        const uint32_t save_indent = strm.GetIndentLevel ();
    //        strm.SetIndentLevel (save_indent + 13);
    //        so_addr.Dump (&strm, exe_scope,
    //        Address::DumpStyleResolvedDescription);
    //        strm.SetIndentLevel (save_indent);
    //        // Print out detailed address information when verbose is enabled
    //        if (verbose)
    //        {
    //            strm.EOL();
    //            so_addr.Dump (&strm, exe_scope,
    //            Address::DumpStyleDetailedSymbolContext);
    //        }
    //        strm.IndentLess();
    return true;
  }

  return false;
}

static uint32_t LookupSymbolInModule(CommandInterpreter &interpreter,
                                     Stream &strm, Module *module,
                                     const char *name, bool name_is_regex,
                                     bool verbose) {
  if (module) {
    SymbolContext sc;

    SymbolVendor *sym_vendor = module->GetSymbolVendor();
    if (sym_vendor) {
      Symtab *symtab = sym_vendor->GetSymtab();
      if (symtab) {
        std::vector<uint32_t> match_indexes;
        ConstString symbol_name(name);
        uint32_t num_matches = 0;
        if (name_is_regex) {
          RegularExpression name_regexp(symbol_name.GetStringRef());
          num_matches = symtab->AppendSymbolIndexesMatchingRegExAndType(
              name_regexp, eSymbolTypeAny, match_indexes);
        } else {
          num_matches =
              symtab->AppendSymbolIndexesWithName(symbol_name, match_indexes);
        }

        if (num_matches > 0) {
          strm.Indent();
          strm.Printf("%u symbols match %s'%s' in ", num_matches,
                      name_is_regex ? "the regular expression " : "", name);
          DumpFullpath(strm, &module->GetFileSpec(), 0);
          strm.PutCString(":\n");
          strm.IndentMore();
          for (uint32_t i = 0; i < num_matches; ++i) {
            Symbol *symbol = symtab->SymbolAtIndex(match_indexes[i]);
            if (symbol && symbol->ValueIsAddress()) {
              DumpAddress(interpreter.GetExecutionContext()
                              .GetBestExecutionContextScope(),
                          symbol->GetAddressRef(), verbose, strm);
            }
          }
          strm.IndentLess();
          return num_matches;
        }
      }
    }
  }
  return 0;
}

static void DumpSymbolContextList(ExecutionContextScope *exe_scope,
                                  Stream &strm, SymbolContextList &sc_list,
                                  bool verbose) {
  strm.IndentMore();

  const uint32_t num_matches = sc_list.GetSize();

  for (uint32_t i = 0; i < num_matches; ++i) {
    SymbolContext sc;
    if (sc_list.GetContextAtIndex(i, sc)) {
      AddressRange range;

      sc.GetAddressRange(eSymbolContextEverything, 0, true, range);

      DumpAddress(exe_scope, range.GetBaseAddress(), verbose, strm);
    }
  }
  strm.IndentLess();
}

static size_t LookupFunctionInModule(CommandInterpreter &interpreter,
                                     Stream &strm, Module *module,
                                     const char *name, bool name_is_regex,
                                     bool include_inlines, bool include_symbols,
                                     bool verbose) {
  if (module && name && name[0]) {
    SymbolContextList sc_list;
    const bool append = true;
    size_t num_matches = 0;
    if (name_is_regex) {
      RegularExpression function_name_regex((llvm::StringRef(name)));
      num_matches = module->FindFunctions(function_name_regex, include_symbols,
                                          include_inlines, append, sc_list);
    } else {
      ConstString function_name(name);
      num_matches = module->FindFunctions(
          function_name, nullptr, eFunctionNameTypeAuto, include_symbols,
          include_inlines, append, sc_list);
    }

    if (num_matches) {
      strm.Indent();
      strm.Printf("%" PRIu64 " match%s found in ", (uint64_t)num_matches,
                  num_matches > 1 ? "es" : "");
      DumpFullpath(strm, &module->GetFileSpec(), 0);
      strm.PutCString(":\n");
      DumpSymbolContextList(
          interpreter.GetExecutionContext().GetBestExecutionContextScope(),
          strm, sc_list, verbose);
    }
    return num_matches;
  }
  return 0;
}

static size_t LookupTypeInModule(CommandInterpreter &interpreter, Stream &strm,
                                 Module *module, const char *name_cstr,
                                 bool name_is_regex) {
  if (module && name_cstr && name_cstr[0]) {
    TypeList type_list;
    const uint32_t max_num_matches = UINT32_MAX;
    size_t num_matches = 0;
    bool name_is_fully_qualified = false;

    ConstString name(name_cstr);
    llvm::DenseSet<lldb_private::SymbolFile *> searched_symbol_files;
    num_matches =
        module->FindTypes(name, name_is_fully_qualified, max_num_matches,
                          searched_symbol_files, type_list);

    if (num_matches) {
      strm.Indent();
      strm.Printf("%" PRIu64 " match%s found in ", (uint64_t)num_matches,
                  num_matches > 1 ? "es" : "");
      DumpFullpath(strm, &module->GetFileSpec(), 0);
      strm.PutCString(":\n");
      for (TypeSP type_sp : type_list.Types()) {
        if (type_sp) {
          // Resolve the clang type so that any forward references to types
          // that haven't yet been parsed will get parsed.
          type_sp->GetFullCompilerType();
          type_sp->GetDescription(&strm, eDescriptionLevelFull, true);
          // Print all typedef chains
          TypeSP typedef_type_sp(type_sp);
          TypeSP typedefed_type_sp(typedef_type_sp->GetTypedefType());
          while (typedefed_type_sp) {
            strm.EOL();
            strm.Printf("     typedef '%s': ",
                        typedef_type_sp->GetName().GetCString());
            typedefed_type_sp->GetFullCompilerType();
            typedefed_type_sp->GetDescription(&strm, eDescriptionLevelFull,
                                              true);
            typedef_type_sp = typedefed_type_sp;
            typedefed_type_sp = typedef_type_sp->GetTypedefType();
          }
        }
        strm.EOL();
      }
    }
    return num_matches;
  }
  return 0;
}

static size_t LookupTypeHere(CommandInterpreter &interpreter, Stream &strm,
                             Module &module, const char *name_cstr,
                             bool name_is_regex) {
  TypeList type_list;
  const uint32_t max_num_matches = UINT32_MAX;
  size_t num_matches = 1;
  bool name_is_fully_qualified = false;

  ConstString name(name_cstr);
  llvm::DenseSet<SymbolFile *> searched_symbol_files;
  num_matches = module.FindTypes(name, name_is_fully_qualified, max_num_matches,
                                 searched_symbol_files, type_list);

  if (num_matches) {
    strm.Indent();
    strm.PutCString("Best match found in ");
    DumpFullpath(strm, &module.GetFileSpec(), 0);
    strm.PutCString(":\n");

    TypeSP type_sp(type_list.GetTypeAtIndex(0));
    if (type_sp) {
      // Resolve the clang type so that any forward references to types that
      // haven't yet been parsed will get parsed.
      type_sp->GetFullCompilerType();
      type_sp->GetDescription(&strm, eDescriptionLevelFull, true);
      // Print all typedef chains
      TypeSP typedef_type_sp(type_sp);
      TypeSP typedefed_type_sp(typedef_type_sp->GetTypedefType());
      while (typedefed_type_sp) {
        strm.EOL();
        strm.Printf("     typedef '%s': ",
                    typedef_type_sp->GetName().GetCString());
        typedefed_type_sp->GetFullCompilerType();
        typedefed_type_sp->GetDescription(&strm, eDescriptionLevelFull, true);
        typedef_type_sp = typedefed_type_sp;
        typedefed_type_sp = typedef_type_sp->GetTypedefType();
      }
    }
    strm.EOL();
  }
  return num_matches;
}

static uint32_t LookupFileAndLineInModule(CommandInterpreter &interpreter,
                                          Stream &strm, Module *module,
                                          const FileSpec &file_spec,
                                          uint32_t line, bool check_inlines,
                                          bool verbose) {
  if (module && file_spec) {
    SymbolContextList sc_list;
    const uint32_t num_matches = module->ResolveSymbolContextsForFileSpec(
        file_spec, line, check_inlines, eSymbolContextEverything, sc_list);
    if (num_matches > 0) {
      strm.Indent();
      strm.Printf("%u match%s found in ", num_matches,
                  num_matches > 1 ? "es" : "");
      strm << file_spec;
      if (line > 0)
        strm.Printf(":%u", line);
      strm << " in ";
      DumpFullpath(strm, &module->GetFileSpec(), 0);
      strm.PutCString(":\n");
      DumpSymbolContextList(
          interpreter.GetExecutionContext().GetBestExecutionContextScope(),
          strm, sc_list, verbose);
      return num_matches;
    }
  }
  return 0;
}

static size_t FindModulesByName(Target *target, const char *module_name,
                                ModuleList &module_list,
                                bool check_global_list) {
  FileSpec module_file_spec(module_name);
  ModuleSpec module_spec(module_file_spec);

  const size_t initial_size = module_list.GetSize();

  if (check_global_list) {
    // Check the global list
    std::lock_guard<std::recursive_mutex> guard(
        Module::GetAllocationModuleCollectionMutex());
    const size_t num_modules = Module::GetNumberAllocatedModules();
    ModuleSP module_sp;
    for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
      Module *module = Module::GetAllocatedModuleAtIndex(image_idx);

      if (module) {
        if (module->MatchesModuleSpec(module_spec)) {
          module_sp = module->shared_from_this();
          module_list.AppendIfNeeded(module_sp);
        }
      }
    }
  } else {
    if (target) {
      const size_t num_matches =
          target->GetImages().FindModules(module_spec, module_list);

      // Not found in our module list for our target, check the main shared
      // module list in case it is a extra file used somewhere else
      if (num_matches == 0) {
        module_spec.GetArchitecture() = target->GetArchitecture();
        ModuleList::FindSharedModules(module_spec, module_list);
      }
    } else {
      ModuleList::FindSharedModules(module_spec, module_list);
    }
  }

  return module_list.GetSize() - initial_size;
}

#pragma mark CommandObjectTargetModulesModuleAutoComplete

//----------------------------------------------------------------------
// A base command object class that can auto complete with module file
// paths
//----------------------------------------------------------------------

class CommandObjectTargetModulesModuleAutoComplete
    : public CommandObjectParsed {
public:
  CommandObjectTargetModulesModuleAutoComplete(CommandInterpreter &interpreter,
                                               const char *name,
                                               const char *help,
                                               const char *syntax)
      : CommandObjectParsed(interpreter, name, help, syntax) {
    CommandArgumentEntry arg;
    CommandArgumentData file_arg;

    // Define the first (and only) variant of this arg.
    file_arg.arg_type = eArgTypeFilename;
    file_arg.arg_repetition = eArgRepeatStar;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(file_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectTargetModulesModuleAutoComplete() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eModuleCompletion, request,
        nullptr);
    return request.GetNumberOfMatches();
  }
};

#pragma mark CommandObjectTargetModulesSourceFileAutoComplete

//----------------------------------------------------------------------
// A base command object class that can auto complete with module source
// file paths
//----------------------------------------------------------------------

class CommandObjectTargetModulesSourceFileAutoComplete
    : public CommandObjectParsed {
public:
  CommandObjectTargetModulesSourceFileAutoComplete(
      CommandInterpreter &interpreter, const char *name, const char *help,
      const char *syntax, uint32_t flags)
      : CommandObjectParsed(interpreter, name, help, syntax, flags) {
    CommandArgumentEntry arg;
    CommandArgumentData source_file_arg;

    // Define the first (and only) variant of this arg.
    source_file_arg.arg_type = eArgTypeSourceFile;
    source_file_arg.arg_repetition = eArgRepeatPlus;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(source_file_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectTargetModulesSourceFileAutoComplete() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eSourceFileCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }
};

#pragma mark CommandObjectTargetModulesDumpObjfile

class CommandObjectTargetModulesDumpObjfile
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpObjfile(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump objfile",
            "Dump the object file headers from one or more target modules.",
            nullptr) {}

  ~CommandObjectTargetModulesDumpObjfile() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    size_t num_dumped = 0;
    if (command.GetArgumentCount() == 0) {
      // Dump all headers for all modules images
      num_dumped = DumpModuleObjfileHeaders(result.GetOutputStream(),
                                            target->GetImages());
      if (num_dumped == 0) {
        result.AppendError("the target has no associated executable images");
        result.SetStatus(eReturnStatusFailed);
      }
    } else {
      // Find the modules that match the basename or full path.
      ModuleList module_list;
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        size_t num_matched =
            FindModulesByName(target, arg_cstr, module_list, true);
        if (num_matched == 0) {
          result.AppendWarningWithFormat(
              "Unable to find an image that matches '%s'.\n", arg_cstr);
        }
      }
      // Dump all the modules we found.
      num_dumped =
          DumpModuleObjfileHeaders(result.GetOutputStream(), module_list);
    }

    if (num_dumped > 0) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("no matching executable images found");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesDumpSymtab

static constexpr OptionEnumValueElement g_sort_option_enumeration[] = {
    {eSortOrderNone, "none",
     "No sorting, use the original symbol table order."},
    {eSortOrderByAddress, "address", "Sort output by symbol address."},
    {eSortOrderByName, "name", "Sort output by symbol name."} };

static constexpr OptionDefinition g_target_modules_dump_symtab_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "sort", 's', OptionParser::eRequiredArgument, nullptr, OptionEnumValues(g_sort_option_enumeration), 0, eArgTypeSortOrder, "Supply a sort order when dumping the symbol table." }
    // clang-format on
};

class CommandObjectTargetModulesDumpSymtab
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpSymtab(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump symtab",
            "Dump the symbol table from one or more target modules.", nullptr),
        m_options() {}

  ~CommandObjectTargetModulesDumpSymtab() override = default;

  Options *GetOptions() override { return &m_options; }

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options(), m_sort_order(eSortOrderNone) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 's':
        m_sort_order = (SortOrder)OptionArgParser::ToOptionEnum(
            option_arg, GetDefinitions()[option_idx].enum_values,
            eSortOrderNone, error);
        break;

      default:
        error.SetErrorStringWithFormat("invalid short option character '%c'",
                                       short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_sort_order = eSortOrderNone;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_target_modules_dump_symtab_options);
    }

    SortOrder m_sort_order;
  };

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      uint32_t num_dumped = 0;

      uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
      result.GetOutputStream().SetAddressByteSize(addr_byte_size);
      result.GetErrorStream().SetAddressByteSize(addr_byte_size);

      if (command.GetArgumentCount() == 0) {
        // Dump all sections for all modules images
        std::lock_guard<std::recursive_mutex> guard(
            target->GetImages().GetMutex());
        const size_t num_modules = target->GetImages().GetSize();
        if (num_modules > 0) {
          result.GetOutputStream().Printf("Dumping symbol table for %" PRIu64
                                          " modules.\n",
                                          (uint64_t)num_modules);
          for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
            if (num_dumped > 0) {
              result.GetOutputStream().EOL();
              result.GetOutputStream().EOL();
            }
            if (m_interpreter.WasInterrupted())
              break;
            num_dumped++;
            DumpModuleSymtab(
                m_interpreter, result.GetOutputStream(),
                target->GetImages().GetModulePointerAtIndexUnlocked(image_idx),
                m_options.m_sort_order);
          }
        } else {
          result.AppendError("the target has no associated executable images");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      } else {
        // Dump specified images (by basename or fullpath)
        const char *arg_cstr;
        for (int arg_idx = 0;
             (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
             ++arg_idx) {
          ModuleList module_list;
          const size_t num_matches =
              FindModulesByName(target, arg_cstr, module_list, true);
          if (num_matches > 0) {
            for (size_t i = 0; i < num_matches; ++i) {
              Module *module = module_list.GetModulePointerAtIndex(i);
              if (module) {
                if (num_dumped > 0) {
                  result.GetOutputStream().EOL();
                  result.GetOutputStream().EOL();
                }
                if (m_interpreter.WasInterrupted())
                  break;
                num_dumped++;
                DumpModuleSymtab(m_interpreter, result.GetOutputStream(),
                                 module, m_options.m_sort_order);
              }
            }
          } else
            result.AppendWarningWithFormat(
                "Unable to find an image that matches '%s'.\n", arg_cstr);
        }
      }

      if (num_dumped > 0)
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else {
        result.AppendError("no matching executable images found");
        result.SetStatus(eReturnStatusFailed);
      }
    }
    return result.Succeeded();
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectTargetModulesDumpSections

//----------------------------------------------------------------------
// Image section dumping command
//----------------------------------------------------------------------

class CommandObjectTargetModulesDumpSections
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpSections(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump sections",
            "Dump the sections from one or more target modules.",
            //"target modules dump sections [<file1> ...]")
            nullptr) {}

  ~CommandObjectTargetModulesDumpSections() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      uint32_t num_dumped = 0;

      uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
      result.GetOutputStream().SetAddressByteSize(addr_byte_size);
      result.GetErrorStream().SetAddressByteSize(addr_byte_size);

      if (command.GetArgumentCount() == 0) {
        // Dump all sections for all modules images
        const size_t num_modules = target->GetImages().GetSize();
        if (num_modules > 0) {
          result.GetOutputStream().Printf("Dumping sections for %" PRIu64
                                          " modules.\n",
                                          (uint64_t)num_modules);
          for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
            if (m_interpreter.WasInterrupted())
              break;
            num_dumped++;
            DumpModuleSections(
                m_interpreter, result.GetOutputStream(),
                target->GetImages().GetModulePointerAtIndex(image_idx));
          }
        } else {
          result.AppendError("the target has no associated executable images");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      } else {
        // Dump specified images (by basename or fullpath)
        const char *arg_cstr;
        for (int arg_idx = 0;
             (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
             ++arg_idx) {
          ModuleList module_list;
          const size_t num_matches =
              FindModulesByName(target, arg_cstr, module_list, true);
          if (num_matches > 0) {
            for (size_t i = 0; i < num_matches; ++i) {
              if (m_interpreter.WasInterrupted())
                break;
              Module *module = module_list.GetModulePointerAtIndex(i);
              if (module) {
                num_dumped++;
                DumpModuleSections(m_interpreter, result.GetOutputStream(),
                                   module);
              }
            }
          } else {
            // Check the global list
            std::lock_guard<std::recursive_mutex> guard(
                Module::GetAllocationModuleCollectionMutex());

            result.AppendWarningWithFormat(
                "Unable to find an image that matches '%s'.\n", arg_cstr);
          }
        }
      }

      if (num_dumped > 0)
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else {
        result.AppendError("no matching executable images found");
        result.SetStatus(eReturnStatusFailed);
      }
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesDumpSections

//----------------------------------------------------------------------
// Clang AST dumping command
//----------------------------------------------------------------------

class CommandObjectTargetModulesDumpClangAST
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpClangAST(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump ast",
            "Dump the clang ast for a given module's symbol file.",
            //"target modules dump ast [<file1> ...]")
            nullptr) {}

  ~CommandObjectTargetModulesDumpClangAST() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    const size_t num_modules = target->GetImages().GetSize();
    if (num_modules == 0) {
      result.AppendError("the target has no associated executable images");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    if (command.GetArgumentCount() == 0) {
      // Dump all ASTs for all modules images
      result.GetOutputStream().Printf("Dumping clang ast for %" PRIu64
                                      " modules.\n",
                                      (uint64_t)num_modules);
      for (size_t image_idx = 0; image_idx < num_modules; ++image_idx) {
        if (m_interpreter.WasInterrupted())
          break;
        Module *m = target->GetImages().GetModulePointerAtIndex(image_idx);
        SymbolFile *sf = m->GetSymbolVendor()->GetSymbolFile();
        sf->DumpClangAST(result.GetOutputStream());
      }
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return true;
    }

    // Dump specified ASTs (by basename or fullpath)
    for (const Args::ArgEntry &arg : command.entries()) {
      ModuleList module_list;
      const size_t num_matches =
          FindModulesByName(target, arg.c_str(), module_list, true);
      if (num_matches == 0) {
        // Check the global list
        std::lock_guard<std::recursive_mutex> guard(
            Module::GetAllocationModuleCollectionMutex());

        result.AppendWarningWithFormat(
            "Unable to find an image that matches '%s'.\n", arg.c_str());
        continue;
      }

      for (size_t i = 0; i < num_matches; ++i) {
        if (m_interpreter.WasInterrupted())
          break;
        Module *m = module_list.GetModulePointerAtIndex(i);
        SymbolFile *sf = m->GetSymbolVendor()->GetSymbolFile();
        sf->DumpClangAST(result.GetOutputStream());
      }
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return true;
  }
};

#pragma mark CommandObjectTargetModulesDumpSymfile

//----------------------------------------------------------------------
// Image debug symbol dumping command
//----------------------------------------------------------------------

class CommandObjectTargetModulesDumpSymfile
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesDumpSymfile(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules dump symfile",
            "Dump the debug symbol file for one or more target modules.",
            //"target modules dump symfile [<file1> ...]")
            nullptr) {}

  ~CommandObjectTargetModulesDumpSymfile() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      uint32_t num_dumped = 0;

      uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
      result.GetOutputStream().SetAddressByteSize(addr_byte_size);
      result.GetErrorStream().SetAddressByteSize(addr_byte_size);

      if (command.GetArgumentCount() == 0) {
        // Dump all sections for all modules images
        const ModuleList &target_modules = target->GetImages();
        std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());
        const size_t num_modules = target_modules.GetSize();
        if (num_modules > 0) {
          result.GetOutputStream().Printf("Dumping debug symbols for %" PRIu64
                                          " modules.\n",
                                          (uint64_t)num_modules);
          for (uint32_t image_idx = 0; image_idx < num_modules; ++image_idx) {
            if (m_interpreter.WasInterrupted())
              break;
            if (DumpModuleSymbolVendor(
                    result.GetOutputStream(),
                    target_modules.GetModulePointerAtIndexUnlocked(image_idx)))
              num_dumped++;
          }
        } else {
          result.AppendError("the target has no associated executable images");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      } else {
        // Dump specified images (by basename or fullpath)
        const char *arg_cstr;
        for (int arg_idx = 0;
             (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
             ++arg_idx) {
          ModuleList module_list;
          const size_t num_matches =
              FindModulesByName(target, arg_cstr, module_list, true);
          if (num_matches > 0) {
            for (size_t i = 0; i < num_matches; ++i) {
              if (m_interpreter.WasInterrupted())
                break;
              Module *module = module_list.GetModulePointerAtIndex(i);
              if (module) {
                if (DumpModuleSymbolVendor(result.GetOutputStream(), module))
                  num_dumped++;
              }
            }
          } else
            result.AppendWarningWithFormat(
                "Unable to find an image that matches '%s'.\n", arg_cstr);
        }
      }

      if (num_dumped > 0)
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else {
        result.AppendError("no matching executable images found");
        result.SetStatus(eReturnStatusFailed);
      }
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesDumpLineTable

//----------------------------------------------------------------------
// Image debug line table dumping command
//----------------------------------------------------------------------

class CommandObjectTargetModulesDumpLineTable
    : public CommandObjectTargetModulesSourceFileAutoComplete {
public:
  CommandObjectTargetModulesDumpLineTable(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesSourceFileAutoComplete(
            interpreter, "target modules dump line-table",
            "Dump the line table for one or more compilation units.", nullptr,
            eCommandRequiresTarget) {}

  ~CommandObjectTargetModulesDumpLineTable() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    uint32_t total_num_dumped = 0;

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    if (command.GetArgumentCount() == 0) {
      result.AppendError("file option must be specified.");
      result.SetStatus(eReturnStatusFailed);
      return result.Succeeded();
    } else {
      // Dump specified images (by basename or fullpath)
      const char *arg_cstr;
      for (int arg_idx = 0;
           (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != nullptr;
           ++arg_idx) {
        FileSpec file_spec(arg_cstr);

        const ModuleList &target_modules = target->GetImages();
        std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());
        const size_t num_modules = target_modules.GetSize();
        if (num_modules > 0) {
          uint32_t num_dumped = 0;
          for (uint32_t i = 0; i < num_modules; ++i) {
            if (m_interpreter.WasInterrupted())
              break;
            if (DumpCompileUnitLineTable(
                    m_interpreter, result.GetOutputStream(),
                    target_modules.GetModulePointerAtIndexUnlocked(i),
                    file_spec, m_exe_ctx.GetProcessPtr() &&
                                   m_exe_ctx.GetProcessRef().IsAlive()))
              num_dumped++;
          }
          if (num_dumped == 0)
            result.AppendWarningWithFormat(
                "No source filenames matched '%s'.\n", arg_cstr);
          else
            total_num_dumped += num_dumped;
        }
      }
    }

    if (total_num_dumped > 0)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.AppendError("no source filenames matched any command arguments");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetModulesDump

//----------------------------------------------------------------------
// Dump multi-word command for target modules
//----------------------------------------------------------------------

class CommandObjectTargetModulesDump : public CommandObjectMultiword {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  CommandObjectTargetModulesDump(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target modules dump",
            "Commands for dumping information about one or "
            "more target modules.",
            "target modules dump "
            "[headers|symtab|sections|ast|symfile|line-table] "
            "[<file1> <file2> ...]") {
    LoadSubCommand("objfile",
                   CommandObjectSP(
                       new CommandObjectTargetModulesDumpObjfile(interpreter)));
    LoadSubCommand(
        "symtab",
        CommandObjectSP(new CommandObjectTargetModulesDumpSymtab(interpreter)));
    LoadSubCommand("sections",
                   CommandObjectSP(new CommandObjectTargetModulesDumpSections(
                       interpreter)));
    LoadSubCommand("symfile",
                   CommandObjectSP(
                       new CommandObjectTargetModulesDumpSymfile(interpreter)));
    LoadSubCommand(
        "ast", CommandObjectSP(
                   new CommandObjectTargetModulesDumpClangAST(interpreter)));
    LoadSubCommand("line-table",
                   CommandObjectSP(new CommandObjectTargetModulesDumpLineTable(
                       interpreter)));
  }

  ~CommandObjectTargetModulesDump() override = default;
};

class CommandObjectTargetModulesAdd : public CommandObjectParsed {
public:
  CommandObjectTargetModulesAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules add",
                            "Add a new module to the current target's modules.",
                            "target modules add [<module>]"),
        m_option_group(),
        m_symbol_file(LLDB_OPT_SET_1, false, "symfile", 's', 0,
                      eArgTypeFilename, "Fullpath to a stand alone debug "
                                        "symbols file for when debug symbols "
                                        "are not in the executable.") {
    m_option_group.Append(&m_uuid_option_group, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_symbol_file, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetModulesAdd() override = default;

  Options *GetOptions() override { return &m_option_group; }

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eDiskFileCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }

protected:
  OptionGroupOptions m_option_group;
  OptionGroupUUID m_uuid_option_group;
  OptionGroupFile m_symbol_file;

  bool DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      bool flush = false;

      const size_t argc = args.GetArgumentCount();
      if (argc == 0) {
        if (m_uuid_option_group.GetOptionValue().OptionWasSet()) {
          // We are given a UUID only, go locate the file
          ModuleSpec module_spec;
          module_spec.GetUUID() =
              m_uuid_option_group.GetOptionValue().GetCurrentValue();
          if (m_symbol_file.GetOptionValue().OptionWasSet())
            module_spec.GetSymbolFileSpec() =
                m_symbol_file.GetOptionValue().GetCurrentValue();
          if (Symbols::DownloadObjectAndSymbolFile(module_spec)) {
            ModuleSP module_sp(target->GetSharedModule(module_spec));
            if (module_sp) {
              result.SetStatus(eReturnStatusSuccessFinishResult);
              return true;
            } else {
              StreamString strm;
              module_spec.GetUUID().Dump(&strm);
              if (module_spec.GetFileSpec()) {
                if (module_spec.GetSymbolFileSpec()) {
                  result.AppendErrorWithFormat(
                      "Unable to create the executable or symbol file with "
                      "UUID %s with path %s and symbol file %s",
                      strm.GetData(),
                      module_spec.GetFileSpec().GetPath().c_str(),
                      module_spec.GetSymbolFileSpec().GetPath().c_str());
                } else {
                  result.AppendErrorWithFormat(
                      "Unable to create the executable or symbol file with "
                      "UUID %s with path %s",
                      strm.GetData(),
                      module_spec.GetFileSpec().GetPath().c_str());
                }
              } else {
                result.AppendErrorWithFormat("Unable to create the executable "
                                             "or symbol file with UUID %s",
                                             strm.GetData());
              }
              result.SetStatus(eReturnStatusFailed);
              return false;
            }
          } else {
            StreamString strm;
            module_spec.GetUUID().Dump(&strm);
            result.AppendErrorWithFormat(
                "Unable to locate the executable or symbol file with UUID %s",
                strm.GetData());
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        } else {
          result.AppendError(
              "one or more executable image paths must be specified");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      } else {
        for (auto &entry : args.entries()) {
          if (entry.ref.empty())
            continue;

          FileSpec file_spec(entry.ref);
          if (FileSystem::Instance().Exists(file_spec)) {
            ModuleSpec module_spec(file_spec);
            if (m_uuid_option_group.GetOptionValue().OptionWasSet())
              module_spec.GetUUID() =
                  m_uuid_option_group.GetOptionValue().GetCurrentValue();
            if (m_symbol_file.GetOptionValue().OptionWasSet())
              module_spec.GetSymbolFileSpec() =
                  m_symbol_file.GetOptionValue().GetCurrentValue();
            if (!module_spec.GetArchitecture().IsValid())
              module_spec.GetArchitecture() = target->GetArchitecture();
            Status error;
            ModuleSP module_sp(target->GetSharedModule(module_spec, &error));
            if (!module_sp) {
              const char *error_cstr = error.AsCString();
              if (error_cstr)
                result.AppendError(error_cstr);
              else
                result.AppendErrorWithFormat("unsupported module: %s",
                                             entry.c_str());
              result.SetStatus(eReturnStatusFailed);
              return false;
            } else {
              flush = true;
            }
            result.SetStatus(eReturnStatusSuccessFinishResult);
          } else {
            std::string resolved_path = file_spec.GetPath();
            result.SetStatus(eReturnStatusFailed);
            if (resolved_path != entry.ref) {
              result.AppendErrorWithFormat(
                  "invalid module path '%s' with resolved path '%s'\n",
                  entry.ref.str().c_str(), resolved_path.c_str());
              break;
            }
            result.AppendErrorWithFormat("invalid module path '%s'\n",
                                         entry.c_str());
            break;
          }
        }
      }

      if (flush) {
        ProcessSP process = target->GetProcessSP();
        if (process)
          process->Flush();
      }
    }

    return result.Succeeded();
  }
};

class CommandObjectTargetModulesLoad
    : public CommandObjectTargetModulesModuleAutoComplete {
public:
  CommandObjectTargetModulesLoad(CommandInterpreter &interpreter)
      : CommandObjectTargetModulesModuleAutoComplete(
            interpreter, "target modules load", "Set the load addresses for "
                                                "one or more sections in a "
                                                "target module.",
            "target modules load [--file <module> --uuid <uuid>] <sect-name> "
            "<address> [<sect-name> <address> ....]"),
        m_option_group(),
        m_file_option(LLDB_OPT_SET_1, false, "file", 'f', 0, eArgTypeName,
                      "Fullpath or basename for module to load.", ""),
        m_load_option(LLDB_OPT_SET_1, false, "load", 'l',
                      "Write file contents to the memory.", false, true),
        m_pc_option(LLDB_OPT_SET_1, false, "set-pc-to-entry", 'p',
                    "Set PC to the entry point."
                    " Only applicable with '--load' option.",
                    false, true),
        m_slide_option(LLDB_OPT_SET_1, false, "slide", 's', 0, eArgTypeOffset,
                       "Set the load address for all sections to be the "
                       "virtual address in the file plus the offset.",
                       0) {
    m_option_group.Append(&m_uuid_option_group, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_file_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_load_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_pc_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_slide_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetModulesLoad() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  bool DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    const bool load = m_load_option.GetOptionValue().GetCurrentValue();
    const bool set_pc = m_pc_option.GetOptionValue().GetCurrentValue();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      const size_t argc = args.GetArgumentCount();
      ModuleSpec module_spec;
      bool search_using_module_spec = false;

      // Allow "load" option to work without --file or --uuid option.
      if (load) {
        if (!m_file_option.GetOptionValue().OptionWasSet() &&
            !m_uuid_option_group.GetOptionValue().OptionWasSet()) {
          ModuleList &module_list = target->GetImages();
          if (module_list.GetSize() == 1) {
            search_using_module_spec = true;
            module_spec.GetFileSpec() =
                module_list.GetModuleAtIndex(0)->GetFileSpec();
          }
        }
      }

      if (m_file_option.GetOptionValue().OptionWasSet()) {
        search_using_module_spec = true;
        const char *arg_cstr = m_file_option.GetOptionValue().GetCurrentValue();
        const bool use_global_module_list = true;
        ModuleList module_list;
        const size_t num_matches = FindModulesByName(
            target, arg_cstr, module_list, use_global_module_list);
        if (num_matches == 1) {
          module_spec.GetFileSpec() =
              module_list.GetModuleAtIndex(0)->GetFileSpec();
        } else if (num_matches > 1) {
          search_using_module_spec = false;
          result.AppendErrorWithFormat(
              "more than 1 module matched by name '%s'\n", arg_cstr);
          result.SetStatus(eReturnStatusFailed);
        } else {
          search_using_module_spec = false;
          result.AppendErrorWithFormat("no object file for module '%s'\n",
                                       arg_cstr);
          result.SetStatus(eReturnStatusFailed);
        }
      }

      if (m_uuid_option_group.GetOptionValue().OptionWasSet()) {
        search_using_module_spec = true;
        module_spec.GetUUID() =
            m_uuid_option_group.GetOptionValue().GetCurrentValue();
      }

      if (search_using_module_spec) {
        ModuleList matching_modules;
        const size_t num_matches =
            target->GetImages().FindModules(module_spec, matching_modules);

        char path[PATH_MAX];
        if (num_matches == 1) {
          Module *module = matching_modules.GetModulePointerAtIndex(0);
          if (module) {
            ObjectFile *objfile = module->GetObjectFile();
            if (objfile) {
              SectionList *section_list = module->GetSectionList();
              if (section_list) {
                bool changed = false;
                if (argc == 0) {
                  if (m_slide_option.GetOptionValue().OptionWasSet()) {
                    const addr_t slide =
                        m_slide_option.GetOptionValue().GetCurrentValue();
                    const bool slide_is_offset = true;
                    module->SetLoadAddress(*target, slide, slide_is_offset,
                                           changed);
                  } else {
                    result.AppendError("one or more section name + load "
                                       "address pair must be specified");
                    result.SetStatus(eReturnStatusFailed);
                    return false;
                  }
                } else {
                  if (m_slide_option.GetOptionValue().OptionWasSet()) {
                    result.AppendError("The \"--slide <offset>\" option can't "
                                       "be used in conjunction with setting "
                                       "section load addresses.\n");
                    result.SetStatus(eReturnStatusFailed);
                    return false;
                  }

                  for (size_t i = 0; i < argc; i += 2) {
                    const char *sect_name = args.GetArgumentAtIndex(i);
                    const char *load_addr_cstr = args.GetArgumentAtIndex(i + 1);
                    if (sect_name && load_addr_cstr) {
                      ConstString const_sect_name(sect_name);
                      bool success = false;
                      addr_t load_addr = StringConvert::ToUInt64(
                          load_addr_cstr, LLDB_INVALID_ADDRESS, 0, &success);
                      if (success) {
                        SectionSP section_sp(
                            section_list->FindSectionByName(const_sect_name));
                        if (section_sp) {
                          if (section_sp->IsThreadSpecific()) {
                            result.AppendErrorWithFormat(
                                "thread specific sections are not yet "
                                "supported (section '%s')\n",
                                sect_name);
                            result.SetStatus(eReturnStatusFailed);
                            break;
                          } else {
                            if (target->GetSectionLoadList()
                                    .SetSectionLoadAddress(section_sp,
                                                           load_addr))
                              changed = true;
                            result.AppendMessageWithFormat(
                                "section '%s' loaded at 0x%" PRIx64 "\n",
                                sect_name, load_addr);
                          }
                        } else {
                          result.AppendErrorWithFormat("no section found that "
                                                       "matches the section "
                                                       "name '%s'\n",
                                                       sect_name);
                          result.SetStatus(eReturnStatusFailed);
                          break;
                        }
                      } else {
                        result.AppendErrorWithFormat(
                            "invalid load address string '%s'\n",
                            load_addr_cstr);
                        result.SetStatus(eReturnStatusFailed);
                        break;
                      }
                    } else {
                      if (sect_name)
                        result.AppendError("section names must be followed by "
                                           "a load address.\n");
                      else
                        result.AppendError("one or more section name + load "
                                           "address pair must be specified.\n");
                      result.SetStatus(eReturnStatusFailed);
                      break;
                    }
                  }
                }

                if (changed) {
                  target->ModulesDidLoad(matching_modules);
                  Process *process = m_exe_ctx.GetProcessPtr();
                  if (process)
                    process->Flush();
                }
                if (load) {
                  ProcessSP process = target->CalculateProcess();
                  Address file_entry = objfile->GetEntryPointAddress();
                  if (!process) {
                    result.AppendError("No process");
                    return false;
                  }
                  if (set_pc && !file_entry.IsValid()) {
                    result.AppendError("No entry address in object file");
                    return false;
                  }
                  std::vector<ObjectFile::LoadableData> loadables(
                      objfile->GetLoadableData(*target));
                  if (loadables.size() == 0) {
                    result.AppendError("No loadable sections");
                    return false;
                  }
                  Status error = process->WriteObjectFile(std::move(loadables));
                  if (error.Fail()) {
                    result.AppendError(error.AsCString());
                    return false;
                  }
                  if (set_pc) {
                    ThreadList &thread_list = process->GetThreadList();
                    RegisterContextSP reg_context(
                        thread_list.GetSelectedThread()->GetRegisterContext());
                    addr_t file_entry_addr = file_entry.GetLoadAddress(target);
                    if (!reg_context->SetPC(file_entry_addr)) {
                      result.AppendErrorWithFormat("failed to set PC value to "
                                                   "0x%" PRIx64 "\n",
                                                   file_entry_addr);
                      result.SetStatus(eReturnStatusFailed);
                    }
                  }
                }
              } else {
                module->GetFileSpec().GetPath(path, sizeof(path));
                result.AppendErrorWithFormat(
                    "no sections in object file '%s'\n", path);
                result.SetStatus(eReturnStatusFailed);
              }
            } else {
              module->GetFileSpec().GetPath(path, sizeof(path));
              result.AppendErrorWithFormat("no object file for module '%s'\n",
                                           path);
              result.SetStatus(eReturnStatusFailed);
            }
          } else {
            FileSpec *module_spec_file = module_spec.GetFileSpecPtr();
            if (module_spec_file) {
              module_spec_file->GetPath(path, sizeof(path));
              result.AppendErrorWithFormat("invalid module '%s'.\n", path);
            } else
              result.AppendError("no module spec");
            result.SetStatus(eReturnStatusFailed);
          }
        } else {
          std::string uuid_str;

          if (module_spec.GetFileSpec())
            module_spec.GetFileSpec().GetPath(path, sizeof(path));
          else
            path[0] = '\0';

          if (module_spec.GetUUIDPtr())
            uuid_str = module_spec.GetUUID().GetAsString();
          if (num_matches > 1) {
            result.AppendErrorWithFormat(
                "multiple modules match%s%s%s%s:\n", path[0] ? " file=" : "",
                path, !uuid_str.empty() ? " uuid=" : "", uuid_str.c_str());
            for (size_t i = 0; i < num_matches; ++i) {
              if (matching_modules.GetModulePointerAtIndex(i)
                      ->GetFileSpec()
                      .GetPath(path, sizeof(path)))
                result.AppendMessageWithFormat("%s\n", path);
            }
          } else {
            result.AppendErrorWithFormat(
                "no modules were found  that match%s%s%s%s.\n",
                path[0] ? " file=" : "", path,
                !uuid_str.empty() ? " uuid=" : "", uuid_str.c_str());
          }
          result.SetStatus(eReturnStatusFailed);
        }
      } else {
        result.AppendError("either the \"--file <module>\" or the \"--uuid "
                           "<uuid>\" option must be specified.\n");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }
    return result.Succeeded();
  }

  OptionGroupOptions m_option_group;
  OptionGroupUUID m_uuid_option_group;
  OptionGroupString m_file_option;
  OptionGroupBoolean m_load_option;
  OptionGroupBoolean m_pc_option;
  OptionGroupUInt64 m_slide_option;
};

//----------------------------------------------------------------------
// List images with associated information
//----------------------------------------------------------------------

static constexpr OptionDefinition g_target_modules_list_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "address",        'a', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeAddressOrExpression, "Display the image at this address." },
  { LLDB_OPT_SET_1, false, "arch",           'A', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the architecture when listing images." },
  { LLDB_OPT_SET_1, false, "triple",         't', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the triple when listing images." },
  { LLDB_OPT_SET_1, false, "header",         'h', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,                "Display the image base address as a load address if debugging, a file address otherwise." },
  { LLDB_OPT_SET_1, false, "offset",         'o', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,                "Display the image load address offset from the base file address (the slide amount)." },
  { LLDB_OPT_SET_1, false, "uuid",           'u', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,                "Display the UUID when listing images." },
  { LLDB_OPT_SET_1, false, "fullpath",       'f', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the fullpath to the image object file." },
  { LLDB_OPT_SET_1, false, "directory",      'd', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the directory with optional width for the image object file." },
  { LLDB_OPT_SET_1, false, "basename",       'b', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the basename with optional width for the image object file." },
  { LLDB_OPT_SET_1, false, "symfile",        's', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the fullpath to the image symbol file with optional width." },
  { LLDB_OPT_SET_1, false, "symfile-unique", 'S', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the symbol file with optional width only if it is different from the executable object file." },
  { LLDB_OPT_SET_1, false, "mod-time",       'm', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the modification time with optional width of the module." },
  { LLDB_OPT_SET_1, false, "ref-count",      'r', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeWidth,               "Display the reference count if the module is still in the shared module cache." },
  { LLDB_OPT_SET_1, false, "pointer",        'p', OptionParser::eOptionalArgument, nullptr, {}, 0, eArgTypeNone,                "Display the module pointer." },
  { LLDB_OPT_SET_1, false, "global",         'g', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,                "Display the modules from the global module list, not just the current target." }
    // clang-format on
};

class CommandObjectTargetModulesList : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions()
        : Options(), m_format_array(), m_use_global_module_list(false),
          m_module_addr(LLDB_INVALID_ADDRESS) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = m_getopt_table[option_idx].val;
      if (short_option == 'g') {
        m_use_global_module_list = true;
      } else if (short_option == 'a') {
        m_module_addr = OptionArgParser::ToAddress(
            execution_context, option_arg, LLDB_INVALID_ADDRESS, &error);
      } else {
        unsigned long width = 0;
        option_arg.getAsInteger(0, width);
        m_format_array.push_back(std::make_pair(short_option, width));
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_format_array.clear();
      m_use_global_module_list = false;
      m_module_addr = LLDB_INVALID_ADDRESS;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_target_modules_list_options);
    }

    // Instance variables to hold the values for command options.
    typedef std::vector<std::pair<char, uint32_t>> FormatWidthCollection;
    FormatWidthCollection m_format_array;
    bool m_use_global_module_list;
    lldb::addr_t m_module_addr;
  };

  CommandObjectTargetModulesList(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target modules list",
            "List current executable and dependent shared library images.",
            "target modules list [<cmd-options>]"),
        m_options() {}

  ~CommandObjectTargetModulesList() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    const bool use_global_module_list = m_options.m_use_global_module_list;
    // Define a local module list here to ensure it lives longer than any
    // "locker" object which might lock its contents below (through the
    // "module_list_ptr" variable).
    ModuleList module_list;
    if (target == nullptr && !use_global_module_list) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      if (target) {
        uint32_t addr_byte_size =
            target->GetArchitecture().GetAddressByteSize();
        result.GetOutputStream().SetAddressByteSize(addr_byte_size);
        result.GetErrorStream().SetAddressByteSize(addr_byte_size);
      }
      // Dump all sections for all modules images
      Stream &strm = result.GetOutputStream();

      if (m_options.m_module_addr != LLDB_INVALID_ADDRESS) {
        if (target) {
          Address module_address;
          if (module_address.SetLoadAddress(m_options.m_module_addr, target)) {
            ModuleSP module_sp(module_address.GetModule());
            if (module_sp) {
              PrintModule(target, module_sp.get(), 0, strm);
              result.SetStatus(eReturnStatusSuccessFinishResult);
            } else {
              result.AppendErrorWithFormat(
                  "Couldn't find module matching address: 0x%" PRIx64 ".",
                  m_options.m_module_addr);
              result.SetStatus(eReturnStatusFailed);
            }
          } else {
            result.AppendErrorWithFormat(
                "Couldn't find module containing address: 0x%" PRIx64 ".",
                m_options.m_module_addr);
            result.SetStatus(eReturnStatusFailed);
          }
        } else {
          result.AppendError(
              "Can only look up modules by address with a valid target.");
          result.SetStatus(eReturnStatusFailed);
        }
        return result.Succeeded();
      }

      size_t num_modules = 0;

      // This locker will be locked on the mutex in module_list_ptr if it is
      // non-nullptr. Otherwise it will lock the
      // AllocationModuleCollectionMutex when accessing the global module list
      // directly.
      std::unique_lock<std::recursive_mutex> guard(
          Module::GetAllocationModuleCollectionMutex(), std::defer_lock);

      const ModuleList *module_list_ptr = nullptr;
      const size_t argc = command.GetArgumentCount();
      if (argc == 0) {
        if (use_global_module_list) {
          guard.lock();
          num_modules = Module::GetNumberAllocatedModules();
        } else {
          module_list_ptr = &target->GetImages();
        }
      } else {
        // TODO: Convert to entry based iteration.  Requires converting
        // FindModulesByName.
        for (size_t i = 0; i < argc; ++i) {
          // Dump specified images (by basename or fullpath)
          const char *arg_cstr = command.GetArgumentAtIndex(i);
          const size_t num_matches = FindModulesByName(
              target, arg_cstr, module_list, use_global_module_list);
          if (num_matches == 0) {
            if (argc == 1) {
              result.AppendErrorWithFormat("no modules found that match '%s'",
                                           arg_cstr);
              result.SetStatus(eReturnStatusFailed);
              return false;
            }
          }
        }

        module_list_ptr = &module_list;
      }

      std::unique_lock<std::recursive_mutex> lock;
      if (module_list_ptr != nullptr) {
        lock =
            std::unique_lock<std::recursive_mutex>(module_list_ptr->GetMutex());

        num_modules = module_list_ptr->GetSize();
      }

      if (num_modules > 0) {
        for (uint32_t image_idx = 0; image_idx < num_modules; ++image_idx) {
          ModuleSP module_sp;
          Module *module;
          if (module_list_ptr) {
            module_sp = module_list_ptr->GetModuleAtIndexUnlocked(image_idx);
            module = module_sp.get();
          } else {
            module = Module::GetAllocatedModuleAtIndex(image_idx);
            module_sp = module->shared_from_this();
          }

          const size_t indent = strm.Printf("[%3u] ", image_idx);
          PrintModule(target, module, indent, strm);
        }
        result.SetStatus(eReturnStatusSuccessFinishResult);
      } else {
        if (argc) {
          if (use_global_module_list)
            result.AppendError(
                "the global module list has no matching modules");
          else
            result.AppendError("the target has no matching modules");
        } else {
          if (use_global_module_list)
            result.AppendError("the global module list is empty");
          else
            result.AppendError(
                "the target has no associated executable images");
        }
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    }
    return result.Succeeded();
  }

  void PrintModule(Target *target, Module *module, int indent, Stream &strm) {
    if (module == nullptr) {
      strm.PutCString("Null module");
      return;
    }

    bool dump_object_name = false;
    if (m_options.m_format_array.empty()) {
      m_options.m_format_array.push_back(std::make_pair('u', 0));
      m_options.m_format_array.push_back(std::make_pair('h', 0));
      m_options.m_format_array.push_back(std::make_pair('f', 0));
      m_options.m_format_array.push_back(std::make_pair('S', 0));
    }
    const size_t num_entries = m_options.m_format_array.size();
    bool print_space = false;
    for (size_t i = 0; i < num_entries; ++i) {
      if (print_space)
        strm.PutChar(' ');
      print_space = true;
      const char format_char = m_options.m_format_array[i].first;
      uint32_t width = m_options.m_format_array[i].second;
      switch (format_char) {
      case 'A':
        DumpModuleArchitecture(strm, module, false, width);
        break;

      case 't':
        DumpModuleArchitecture(strm, module, true, width);
        break;

      case 'f':
        DumpFullpath(strm, &module->GetFileSpec(), width);
        dump_object_name = true;
        break;

      case 'd':
        DumpDirectory(strm, &module->GetFileSpec(), width);
        break;

      case 'b':
        DumpBasename(strm, &module->GetFileSpec(), width);
        dump_object_name = true;
        break;

      case 'h':
      case 'o':
        // Image header address
        {
          uint32_t addr_nibble_width =
              target ? (target->GetArchitecture().GetAddressByteSize() * 2)
                     : 16;

          ObjectFile *objfile = module->GetObjectFile();
          if (objfile) {
            Address base_addr(objfile->GetBaseAddress());
            if (base_addr.IsValid()) {
              if (target && !target->GetSectionLoadList().IsEmpty()) {
                lldb::addr_t load_addr =
                    base_addr.GetLoadAddress(target);
                if (load_addr == LLDB_INVALID_ADDRESS) {
                  base_addr.Dump(&strm, target,
                                   Address::DumpStyleModuleWithFileAddress,
                                   Address::DumpStyleFileAddress);
                } else {
                  if (format_char == 'o') {
                    // Show the offset of slide for the image
                    strm.Printf(
                        "0x%*.*" PRIx64, addr_nibble_width, addr_nibble_width,
                        load_addr - base_addr.GetFileAddress());
                  } else {
                    // Show the load address of the image
                    strm.Printf("0x%*.*" PRIx64, addr_nibble_width,
                                addr_nibble_width, load_addr);
                  }
                }
                break;
              }
              // The address was valid, but the image isn't loaded, output the
              // address in an appropriate format
              base_addr.Dump(&strm, target, Address::DumpStyleFileAddress);
              break;
            }
          }
          strm.Printf("%*s", addr_nibble_width + 2, "");
        }
        break;

      case 'r': {
        size_t ref_count = 0;
        ModuleSP module_sp(module->shared_from_this());
        if (module_sp) {
          // Take one away to make sure we don't count our local "module_sp"
          ref_count = module_sp.use_count() - 1;
        }
        if (width)
          strm.Printf("{%*" PRIu64 "}", width, (uint64_t)ref_count);
        else
          strm.Printf("{%" PRIu64 "}", (uint64_t)ref_count);
      } break;

      case 's':
      case 'S': {
        const SymbolVendor *symbol_vendor = module->GetSymbolVendor();
        if (symbol_vendor) {
          const FileSpec symfile_spec = symbol_vendor->GetMainFileSpec();
          if (format_char == 'S') {
            // Dump symbol file only if different from module file
            if (!symfile_spec || symfile_spec == module->GetFileSpec()) {
              print_space = false;
              break;
            }
            // Add a newline and indent past the index
            strm.Printf("\n%*s", indent, "");
          }
          DumpFullpath(strm, &symfile_spec, width);
          dump_object_name = true;
          break;
        }
        strm.Printf("%.*s", width, "<NONE>");
      } break;

      case 'm':
        strm.Format("{0:%c}", llvm::fmt_align(module->GetModificationTime(),
                                              llvm::AlignStyle::Left, width));
        break;

      case 'p':
        strm.Printf("%p", static_cast<void *>(module));
        break;

      case 'u':
        DumpModuleUUID(strm, module);
        break;

      default:
        break;
      }
    }
    if (dump_object_name) {
      const char *object_name = module->GetObjectName().GetCString();
      if (object_name)
        strm.Printf("(%s)", object_name);
    }
    strm.EOL();
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectTargetModulesShowUnwind

//----------------------------------------------------------------------
// Lookup unwind information in images
//----------------------------------------------------------------------

static constexpr OptionDefinition g_target_modules_show_unwind_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1, false, "name",    'n', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeFunctionName,        "Show unwind instructions for a function or symbol name." },
  { LLDB_OPT_SET_2, false, "address", 'a', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeAddressOrExpression, "Show unwind instructions for a function or symbol containing an address" }
    // clang-format on
};

class CommandObjectTargetModulesShowUnwind : public CommandObjectParsed {
public:
  enum {
    eLookupTypeInvalid = -1,
    eLookupTypeAddress = 0,
    eLookupTypeSymbol,
    eLookupTypeFunction,
    eLookupTypeFunctionOrSymbol,
    kNumLookupTypes
  };

  class CommandOptions : public Options {
  public:
    CommandOptions()
        : Options(), m_type(eLookupTypeInvalid), m_str(),
          m_addr(LLDB_INVALID_ADDRESS) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a': {
        m_str = option_arg;
        m_type = eLookupTypeAddress;
        m_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                            LLDB_INVALID_ADDRESS, &error);
        if (m_addr == LLDB_INVALID_ADDRESS)
          error.SetErrorStringWithFormat("invalid address string '%s'",
                                         option_arg.str().c_str());
        break;
      }

      case 'n':
        m_str = option_arg;
        m_type = eLookupTypeFunctionOrSymbol;
        break;

      default:
        error.SetErrorStringWithFormat("unrecognized option %c.", short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_type = eLookupTypeInvalid;
      m_str.clear();
      m_addr = LLDB_INVALID_ADDRESS;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_target_modules_show_unwind_options);
    }

    // Instance variables to hold the values for command options.

    int m_type;        // Should be a eLookupTypeXXX enum after parsing options
    std::string m_str; // Holds name lookup
    lldb::addr_t m_addr; // Holds the address to lookup
  };

  CommandObjectTargetModulesShowUnwind(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target modules show-unwind",
            "Show synthesized unwind instructions for a function.", nullptr,
            eCommandRequiresTarget | eCommandRequiresProcess |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused),
        m_options() {}

  ~CommandObjectTargetModulesShowUnwind() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    Process *process = m_exe_ctx.GetProcessPtr();
    ABI *abi = nullptr;
    if (process)
      abi = process->GetABI().get();

    if (process == nullptr) {
      result.AppendError(
          "You must have a process running to use this command.");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    ThreadList threads(process->GetThreadList());
    if (threads.GetSize() == 0) {
      result.AppendError("The process must be paused to use this command.");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    ThreadSP thread(threads.GetThreadAtIndex(0));
    if (!thread) {
      result.AppendError("The process must be paused to use this command.");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    SymbolContextList sc_list;

    if (m_options.m_type == eLookupTypeFunctionOrSymbol) {
      ConstString function_name(m_options.m_str.c_str());
      target->GetImages().FindFunctions(function_name, eFunctionNameTypeAuto,
                                        true, false, true, sc_list);
    } else if (m_options.m_type == eLookupTypeAddress && target) {
      Address addr;
      if (target->GetSectionLoadList().ResolveLoadAddress(m_options.m_addr,
                                                          addr)) {
        SymbolContext sc;
        ModuleSP module_sp(addr.GetModule());
        module_sp->ResolveSymbolContextForAddress(addr,
                                                  eSymbolContextEverything, sc);
        if (sc.function || sc.symbol) {
          sc_list.Append(sc);
        }
      }
    } else {
      result.AppendError(
          "address-expression or function name option must be specified.");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    size_t num_matches = sc_list.GetSize();
    if (num_matches == 0) {
      result.AppendErrorWithFormat("no unwind data found that matches '%s'.",
                                   m_options.m_str.c_str());
      result.SetStatus(eReturnStatusFailed);
      return false;
    }

    for (uint32_t idx = 0; idx < num_matches; idx++) {
      SymbolContext sc;
      sc_list.GetContextAtIndex(idx, sc);
      if (sc.symbol == nullptr && sc.function == nullptr)
        continue;
      if (!sc.module_sp || sc.module_sp->GetObjectFile() == nullptr)
        continue;
      AddressRange range;
      if (!sc.GetAddressRange(eSymbolContextFunction | eSymbolContextSymbol, 0,
                              false, range))
        continue;
      if (!range.GetBaseAddress().IsValid())
        continue;
      ConstString funcname(sc.GetFunctionName());
      if (funcname.IsEmpty())
        continue;
      addr_t start_addr = range.GetBaseAddress().GetLoadAddress(target);
      if (abi)
        start_addr = abi->FixCodeAddress(start_addr);

      FuncUnwindersSP func_unwinders_sp(
          sc.module_sp->GetObjectFile()
              ->GetUnwindTable()
              .GetUncachedFuncUnwindersContainingAddress(start_addr, sc));
      if (!func_unwinders_sp)
        continue;

      result.GetOutputStream().Printf(
          "UNWIND PLANS for %s`%s (start addr 0x%" PRIx64 ")\n\n",
          sc.module_sp->GetPlatformFileSpec().GetFilename().AsCString(),
          funcname.AsCString(), start_addr);

      UnwindPlanSP non_callsite_unwind_plan =
          func_unwinders_sp->GetUnwindPlanAtNonCallSite(*target, *thread, -1);
      if (non_callsite_unwind_plan) {
        result.GetOutputStream().Printf(
            "Asynchronous (not restricted to call-sites) UnwindPlan is '%s'\n",
            non_callsite_unwind_plan->GetSourceName().AsCString());
      }
      UnwindPlanSP callsite_unwind_plan =
          func_unwinders_sp->GetUnwindPlanAtCallSite(*target, -1);
      if (callsite_unwind_plan) {
        result.GetOutputStream().Printf(
            "Synchronous (restricted to call-sites) UnwindPlan is '%s'\n",
            callsite_unwind_plan->GetSourceName().AsCString());
      }
      UnwindPlanSP fast_unwind_plan =
          func_unwinders_sp->GetUnwindPlanFastUnwind(*target, *thread);
      if (fast_unwind_plan) {
        result.GetOutputStream().Printf(
            "Fast UnwindPlan is '%s'\n",
            fast_unwind_plan->GetSourceName().AsCString());
      }

      result.GetOutputStream().Printf("\n");

      UnwindPlanSP assembly_sp =
          func_unwinders_sp->GetAssemblyUnwindPlan(*target, *thread, 0);
      if (assembly_sp) {
        result.GetOutputStream().Printf(
            "Assembly language inspection UnwindPlan:\n");
        assembly_sp->Dump(result.GetOutputStream(), thread.get(),
                          LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP ehframe_sp =
          func_unwinders_sp->GetEHFrameUnwindPlan(*target, 0);
      if (ehframe_sp) {
        result.GetOutputStream().Printf("eh_frame UnwindPlan:\n");
        ehframe_sp->Dump(result.GetOutputStream(), thread.get(),
                         LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP ehframe_augmented_sp =
          func_unwinders_sp->GetEHFrameAugmentedUnwindPlan(*target, *thread, 0);
      if (ehframe_augmented_sp) {
        result.GetOutputStream().Printf("eh_frame augmented UnwindPlan:\n");
        ehframe_augmented_sp->Dump(result.GetOutputStream(), thread.get(),
                                   LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      if (UnwindPlanSP plan_sp =
              func_unwinders_sp->GetDebugFrameUnwindPlan(*target, 0)) {
        result.GetOutputStream().Printf("debug_frame UnwindPlan:\n");
        plan_sp->Dump(result.GetOutputStream(), thread.get(),
                      LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      if (UnwindPlanSP plan_sp =
              func_unwinders_sp->GetDebugFrameAugmentedUnwindPlan(*target,
                                                                  *thread, 0)) {
        result.GetOutputStream().Printf("debug_frame augmented UnwindPlan:\n");
        plan_sp->Dump(result.GetOutputStream(), thread.get(),
                      LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP arm_unwind_sp =
          func_unwinders_sp->GetArmUnwindUnwindPlan(*target, 0);
      if (arm_unwind_sp) {
        result.GetOutputStream().Printf("ARM.exidx unwind UnwindPlan:\n");
        arm_unwind_sp->Dump(result.GetOutputStream(), thread.get(),
                            LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      UnwindPlanSP compact_unwind_sp =
          func_unwinders_sp->GetCompactUnwindUnwindPlan(*target, 0);
      if (compact_unwind_sp) {
        result.GetOutputStream().Printf("Compact unwind UnwindPlan:\n");
        compact_unwind_sp->Dump(result.GetOutputStream(), thread.get(),
                                LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      if (fast_unwind_plan) {
        result.GetOutputStream().Printf("Fast UnwindPlan:\n");
        fast_unwind_plan->Dump(result.GetOutputStream(), thread.get(),
                               LLDB_INVALID_ADDRESS);
        result.GetOutputStream().Printf("\n");
      }

      ABISP abi_sp = process->GetABI();
      if (abi_sp) {
        UnwindPlan arch_default(lldb::eRegisterKindGeneric);
        if (abi_sp->CreateDefaultUnwindPlan(arch_default)) {
          result.GetOutputStream().Printf("Arch default UnwindPlan:\n");
          arch_default.Dump(result.GetOutputStream(), thread.get(),
                            LLDB_INVALID_ADDRESS);
          result.GetOutputStream().Printf("\n");
        }

        UnwindPlan arch_entry(lldb::eRegisterKindGeneric);
        if (abi_sp->CreateFunctionEntryUnwindPlan(arch_entry)) {
          result.GetOutputStream().Printf(
              "Arch default at entry point UnwindPlan:\n");
          arch_entry.Dump(result.GetOutputStream(), thread.get(),
                          LLDB_INVALID_ADDRESS);
          result.GetOutputStream().Printf("\n");
        }
      }

      result.GetOutputStream().Printf("\n");
    }
    return result.Succeeded();
  }

  CommandOptions m_options;
};

//----------------------------------------------------------------------
// Lookup information in images
//----------------------------------------------------------------------

static constexpr OptionDefinition g_target_modules_lookup_options[] = {
    // clang-format off
  { LLDB_OPT_SET_1,                                  true,  "address",    'a', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeAddressOrExpression, "Lookup an address in one or more target modules." },
  { LLDB_OPT_SET_1,                                  false, "offset",     'o', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeOffset,              "When looking up an address subtract <offset> from any addresses before doing the lookup." },
  /* FIXME: re-enable regex for types when the LookupTypeInModule actually uses the regex option: | LLDB_OPT_SET_6 */
  { LLDB_OPT_SET_2 | LLDB_OPT_SET_4 | LLDB_OPT_SET_5, false, "regex",      'r', OptionParser::eNoArgument,      nullptr, {}, 0, eArgTypeNone,                "The <name> argument for name lookups are regular expressions." },
  { LLDB_OPT_SET_2,                                  true,  "symbol",     's', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeSymbol,              "Lookup a symbol by name in the symbol tables in one or more target modules." },
  { LLDB_OPT_SET_3,                                  true,  "file",       'f', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeFilename,            "Lookup a file by fullpath or basename in one or more target modules." },
  { LLDB_OPT_SET_3,                                  false, "line",       'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLineNum,             "Lookup a line number in a file (must be used in conjunction with --file)." },
  { LLDB_OPT_SET_FROM_TO(3,5),                       false, "no-inlines", 'i', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,                "Ignore inline entries (must be used in conjunction with --file or --function)." },
  { LLDB_OPT_SET_4,                                  true,  "function",   'F', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeFunctionName,        "Lookup a function by name in the debug symbols in one or more target modules." },
  { LLDB_OPT_SET_5,                                  true,  "name",       'n', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeFunctionOrSymbol,    "Lookup a function or symbol by name in one or more target modules." },
  { LLDB_OPT_SET_6,                                  true,  "type",       't', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeName,                "Lookup a type by name in the debug symbols in one or more target modules." },
  { LLDB_OPT_SET_ALL,                                false, "verbose",    'v', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,                "Enable verbose lookup information." },
  { LLDB_OPT_SET_ALL,                                false, "all",        'A', OptionParser::eNoArgument,       nullptr, {}, 0, eArgTypeNone,                "Print all matches, not just the best match, if a best match is available." },
    // clang-format on
};

class CommandObjectTargetModulesLookup : public CommandObjectParsed {
public:
  enum {
    eLookupTypeInvalid = -1,
    eLookupTypeAddress = 0,
    eLookupTypeSymbol,
    eLookupTypeFileLine, // Line is optional
    eLookupTypeFunction,
    eLookupTypeFunctionOrSymbol,
    eLookupTypeType,
    kNumLookupTypes
  };

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() { OptionParsingStarting(nullptr); }

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a': {
        m_type = eLookupTypeAddress;
        m_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                            LLDB_INVALID_ADDRESS, &error);
      } break;

      case 'o':
        if (option_arg.getAsInteger(0, m_offset))
          error.SetErrorStringWithFormat("invalid offset string '%s'",
                                         option_arg.str().c_str());
        break;

      case 's':
        m_str = option_arg;
        m_type = eLookupTypeSymbol;
        break;

      case 'f':
        m_file.SetFile(option_arg, FileSpec::Style::native);
        m_type = eLookupTypeFileLine;
        break;

      case 'i':
        m_include_inlines = false;
        break;

      case 'l':
        if (option_arg.getAsInteger(0, m_line_number))
          error.SetErrorStringWithFormat("invalid line number string '%s'",
                                         option_arg.str().c_str());
        else if (m_line_number == 0)
          error.SetErrorString("zero is an invalid line number");
        m_type = eLookupTypeFileLine;
        break;

      case 'F':
        m_str = option_arg;
        m_type = eLookupTypeFunction;
        break;

      case 'n':
        m_str = option_arg;
        m_type = eLookupTypeFunctionOrSymbol;
        break;

      case 't':
        m_str = option_arg;
        m_type = eLookupTypeType;
        break;

      case 'v':
        m_verbose = 1;
        break;

      case 'A':
        m_print_all = true;
        break;

      case 'r':
        m_use_regex = true;
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_type = eLookupTypeInvalid;
      m_str.clear();
      m_file.Clear();
      m_addr = LLDB_INVALID_ADDRESS;
      m_offset = 0;
      m_line_number = 0;
      m_use_regex = false;
      m_include_inlines = true;
      m_verbose = false;
      m_print_all = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_target_modules_lookup_options);
    }

    int m_type;        // Should be a eLookupTypeXXX enum after parsing options
    std::string m_str; // Holds name lookup
    FileSpec m_file;   // Files for file lookups
    lldb::addr_t m_addr; // Holds the address to lookup
    lldb::addr_t
        m_offset; // Subtract this offset from m_addr before doing lookups.
    uint32_t m_line_number; // Line number for file+line lookups
    bool m_use_regex;       // Name lookups in m_str are regular expressions.
    bool m_include_inlines; // Check for inline entries when looking up by
                            // file/line.
    bool m_verbose;         // Enable verbose lookup info
    bool m_print_all; // Print all matches, even in cases where there's a best
                      // match.
  };

  CommandObjectTargetModulesLookup(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target modules lookup",
                            "Look up information within executable and "
                            "dependent shared library images.",
                            nullptr, eCommandRequiresTarget),
        m_options() {
    CommandArgumentEntry arg;
    CommandArgumentData file_arg;

    // Define the first (and only) variant of this arg.
    file_arg.arg_type = eArgTypeFilename;
    file_arg.arg_repetition = eArgRepeatStar;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg.push_back(file_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg);
  }

  ~CommandObjectTargetModulesLookup() override = default;

  Options *GetOptions() override { return &m_options; }

  bool LookupHere(CommandInterpreter &interpreter, CommandReturnObject &result,
                  bool &syntax_error) {
    switch (m_options.m_type) {
    case eLookupTypeAddress:
    case eLookupTypeFileLine:
    case eLookupTypeFunction:
    case eLookupTypeFunctionOrSymbol:
    case eLookupTypeSymbol:
    default:
      return false;
    case eLookupTypeType:
      break;
    }

    StackFrameSP frame = m_exe_ctx.GetFrameSP();

    if (!frame)
      return false;

    const SymbolContext &sym_ctx(frame->GetSymbolContext(eSymbolContextModule));

    if (!sym_ctx.module_sp)
      return false;

    switch (m_options.m_type) {
    default:
      return false;
    case eLookupTypeType:
      if (!m_options.m_str.empty()) {
        if (LookupTypeHere(m_interpreter, result.GetOutputStream(),
                           *sym_ctx.module_sp, m_options.m_str.c_str(),
                           m_options.m_use_regex)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;
    }

    return true;
  }

  bool LookupInModule(CommandInterpreter &interpreter, Module *module,
                      CommandReturnObject &result, bool &syntax_error) {
    switch (m_options.m_type) {
    case eLookupTypeAddress:
      if (m_options.m_addr != LLDB_INVALID_ADDRESS) {
        if (LookupAddressInModule(
                m_interpreter, result.GetOutputStream(), module,
                eSymbolContextEverything |
                    (m_options.m_verbose
                         ? static_cast<int>(eSymbolContextVariable)
                         : 0),
                m_options.m_addr, m_options.m_offset, m_options.m_verbose)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeSymbol:
      if (!m_options.m_str.empty()) {
        if (LookupSymbolInModule(m_interpreter, result.GetOutputStream(),
                                 module, m_options.m_str.c_str(),
                                 m_options.m_use_regex, m_options.m_verbose)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeFileLine:
      if (m_options.m_file) {
        if (LookupFileAndLineInModule(
                m_interpreter, result.GetOutputStream(), module,
                m_options.m_file, m_options.m_line_number,
                m_options.m_include_inlines, m_options.m_verbose)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeFunctionOrSymbol:
    case eLookupTypeFunction:
      if (!m_options.m_str.empty()) {
        if (LookupFunctionInModule(
                m_interpreter, result.GetOutputStream(), module,
                m_options.m_str.c_str(), m_options.m_use_regex,
                m_options.m_include_inlines,
                m_options.m_type ==
                    eLookupTypeFunctionOrSymbol, // include symbols
                m_options.m_verbose)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    case eLookupTypeType:
      if (!m_options.m_str.empty()) {
        if (LookupTypeInModule(m_interpreter, result.GetOutputStream(), module,
                               m_options.m_str.c_str(),
                               m_options.m_use_regex)) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
          return true;
        }
      }
      break;

    default:
      m_options.GenerateOptionUsage(
          result.GetErrorStream(), this,
          GetCommandInterpreter().GetDebugger().GetTerminalWidth());
      syntax_error = true;
      break;
    }

    result.SetStatus(eReturnStatusFailed);
    return false;
  }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    if (target == nullptr) {
      result.AppendError("invalid target, create a debug target using the "
                         "'target create' command");
      result.SetStatus(eReturnStatusFailed);
      return false;
    } else {
      bool syntax_error = false;
      uint32_t i;
      uint32_t num_successful_lookups = 0;
      uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
      result.GetOutputStream().SetAddressByteSize(addr_byte_size);
      result.GetErrorStream().SetAddressByteSize(addr_byte_size);
      // Dump all sections for all modules images

      if (command.GetArgumentCount() == 0) {
        ModuleSP current_module;

        // Where it is possible to look in the current symbol context first,
        // try that.  If this search was successful and --all was not passed,
        // don't print anything else.
        if (LookupHere(m_interpreter, result, syntax_error)) {
          result.GetOutputStream().EOL();
          num_successful_lookups++;
          if (!m_options.m_print_all) {
            result.SetStatus(eReturnStatusSuccessFinishResult);
            return result.Succeeded();
          }
        }

        // Dump all sections for all other modules

        const ModuleList &target_modules = target->GetImages();
        std::lock_guard<std::recursive_mutex> guard(target_modules.GetMutex());
        const size_t num_modules = target_modules.GetSize();
        if (num_modules > 0) {
          for (i = 0; i < num_modules && !syntax_error; ++i) {
            Module *module_pointer =
                target_modules.GetModulePointerAtIndexUnlocked(i);

            if (module_pointer != current_module.get() &&
                LookupInModule(
                    m_interpreter,
                    target_modules.GetModulePointerAtIndexUnlocked(i), result,
                    syntax_error)) {
              result.GetOutputStream().EOL();
              num_successful_lookups++;
            }
          }
        } else {
          result.AppendError("the target has no associated executable images");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      } else {
        // Dump specified images (by basename or fullpath)
        const char *arg_cstr;
        for (i = 0; (arg_cstr = command.GetArgumentAtIndex(i)) != nullptr &&
                    !syntax_error;
             ++i) {
          ModuleList module_list;
          const size_t num_matches =
              FindModulesByName(target, arg_cstr, module_list, false);
          if (num_matches > 0) {
            for (size_t j = 0; j < num_matches; ++j) {
              Module *module = module_list.GetModulePointerAtIndex(j);
              if (module) {
                if (LookupInModule(m_interpreter, module, result,
                                   syntax_error)) {
                  result.GetOutputStream().EOL();
                  num_successful_lookups++;
                }
              }
            }
          } else
            result.AppendWarningWithFormat(
                "Unable to find an image that matches '%s'.\n", arg_cstr);
        }
      }

      if (num_successful_lookups > 0)
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else
        result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectMultiwordImageSearchPaths

//-------------------------------------------------------------------------
// CommandObjectMultiwordImageSearchPaths
//-------------------------------------------------------------------------

class CommandObjectTargetModulesImageSearchPaths
    : public CommandObjectMultiword {
public:
  CommandObjectTargetModulesImageSearchPaths(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target modules search-paths",
            "Commands for managing module search paths for a target.",
            "target modules search-paths <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "add", CommandObjectSP(
                   new CommandObjectTargetModulesSearchPathsAdd(interpreter)));
    LoadSubCommand(
        "clear", CommandObjectSP(new CommandObjectTargetModulesSearchPathsClear(
                     interpreter)));
    LoadSubCommand(
        "insert",
        CommandObjectSP(
            new CommandObjectTargetModulesSearchPathsInsert(interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTargetModulesSearchPathsList(
                    interpreter)));
    LoadSubCommand(
        "query", CommandObjectSP(new CommandObjectTargetModulesSearchPathsQuery(
                     interpreter)));
  }

  ~CommandObjectTargetModulesImageSearchPaths() override = default;
};

#pragma mark CommandObjectTargetModules

//-------------------------------------------------------------------------
// CommandObjectTargetModules
//-------------------------------------------------------------------------

class CommandObjectTargetModules : public CommandObjectMultiword {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  CommandObjectTargetModules(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "target modules",
                               "Commands for accessing information for one or "
                               "more target modules.",
                               "target modules <sub-command> ...") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTargetModulesAdd(interpreter)));
    LoadSubCommand("load", CommandObjectSP(new CommandObjectTargetModulesLoad(
                               interpreter)));
    LoadSubCommand("dump", CommandObjectSP(new CommandObjectTargetModulesDump(
                               interpreter)));
    LoadSubCommand("list", CommandObjectSP(new CommandObjectTargetModulesList(
                               interpreter)));
    LoadSubCommand(
        "lookup",
        CommandObjectSP(new CommandObjectTargetModulesLookup(interpreter)));
    LoadSubCommand(
        "search-paths",
        CommandObjectSP(
            new CommandObjectTargetModulesImageSearchPaths(interpreter)));
    LoadSubCommand(
        "show-unwind",
        CommandObjectSP(new CommandObjectTargetModulesShowUnwind(interpreter)));
  }

  ~CommandObjectTargetModules() override = default;

private:
  //------------------------------------------------------------------
  // For CommandObjectTargetModules only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(CommandObjectTargetModules);
};

class CommandObjectTargetSymbolsAdd : public CommandObjectParsed {
public:
  CommandObjectTargetSymbolsAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "target symbols add",
            "Add a debug symbol file to one of the target's current modules by "
            "specifying a path to a debug symbols file, or using the options "
            "to specify a module to download symbols for.",
            "target symbols add <cmd-options> [<symfile>]",
            eCommandRequiresTarget),
        m_option_group(),
        m_file_option(
            LLDB_OPT_SET_1, false, "shlib", 's',
            CommandCompletions::eModuleCompletion, eArgTypeShlibName,
            "Fullpath or basename for module to find debug symbols for."),
        m_current_frame_option(
            LLDB_OPT_SET_2, false, "frame", 'F',
            "Locate the debug symbols the currently selected frame.", false,
            true)

  {
    m_option_group.Append(&m_uuid_option_group, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_file_option, LLDB_OPT_SET_ALL, LLDB_OPT_SET_1);
    m_option_group.Append(&m_current_frame_option, LLDB_OPT_SET_2,
                          LLDB_OPT_SET_2);
    m_option_group.Finalize();
  }

  ~CommandObjectTargetSymbolsAdd() override = default;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override {
    CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), CommandCompletions::eDiskFileCompletion,
        request, nullptr);
    return request.GetNumberOfMatches();
  }

  Options *GetOptions() override { return &m_option_group; }

protected:
  bool AddModuleSymbols(Target *target, ModuleSpec &module_spec, bool &flush,
                        CommandReturnObject &result) {
    const FileSpec &symbol_fspec = module_spec.GetSymbolFileSpec();
    if (symbol_fspec) {
      char symfile_path[PATH_MAX];
      symbol_fspec.GetPath(symfile_path, sizeof(symfile_path));

      if (!module_spec.GetUUID().IsValid()) {
        if (!module_spec.GetFileSpec() && !module_spec.GetPlatformFileSpec())
          module_spec.GetFileSpec().GetFilename() = symbol_fspec.GetFilename();
      }
      // We now have a module that represents a symbol file that can be used
      // for a module that might exist in the current target, so we need to
      // find that module in the target
      ModuleList matching_module_list;

      size_t num_matches = 0;
      // First extract all module specs from the symbol file
      lldb_private::ModuleSpecList symfile_module_specs;
      if (ObjectFile::GetModuleSpecifications(module_spec.GetSymbolFileSpec(),
                                              0, 0, symfile_module_specs)) {
        // Now extract the module spec that matches the target architecture
        ModuleSpec target_arch_module_spec;
        ModuleSpec symfile_module_spec;
        target_arch_module_spec.GetArchitecture() = target->GetArchitecture();
        if (symfile_module_specs.FindMatchingModuleSpec(target_arch_module_spec,
                                                        symfile_module_spec)) {
          // See if it has a UUID?
          if (symfile_module_spec.GetUUID().IsValid()) {
            // It has a UUID, look for this UUID in the target modules
            ModuleSpec symfile_uuid_module_spec;
            symfile_uuid_module_spec.GetUUID() = symfile_module_spec.GetUUID();
            num_matches = target->GetImages().FindModules(
                symfile_uuid_module_spec, matching_module_list);
          }
        }

        if (num_matches == 0) {
          // No matches yet, iterate through the module specs to find a UUID
          // value that we can match up to an image in our target
          const size_t num_symfile_module_specs =
              symfile_module_specs.GetSize();
          for (size_t i = 0; i < num_symfile_module_specs && num_matches == 0;
               ++i) {
            if (symfile_module_specs.GetModuleSpecAtIndex(
                    i, symfile_module_spec)) {
              if (symfile_module_spec.GetUUID().IsValid()) {
                // It has a UUID, look for this UUID in the target modules
                ModuleSpec symfile_uuid_module_spec;
                symfile_uuid_module_spec.GetUUID() =
                    symfile_module_spec.GetUUID();
                num_matches = target->GetImages().FindModules(
                    symfile_uuid_module_spec, matching_module_list);
              }
            }
          }
        }
      }

      // Just try to match up the file by basename if we have no matches at
      // this point
      if (num_matches == 0)
        num_matches =
            target->GetImages().FindModules(module_spec, matching_module_list);

      while (num_matches == 0) {
        ConstString filename_no_extension(
            module_spec.GetFileSpec().GetFileNameStrippingExtension());
        // Empty string returned, lets bail
        if (!filename_no_extension)
          break;

        // Check if there was no extension to strip and the basename is the
        // same
        if (filename_no_extension == module_spec.GetFileSpec().GetFilename())
          break;

        // Replace basename with one less extension
        module_spec.GetFileSpec().GetFilename() = filename_no_extension;

        num_matches =
            target->GetImages().FindModules(module_spec, matching_module_list);
      }

      if (num_matches > 1) {
        result.AppendErrorWithFormat("multiple modules match symbol file '%s', "
                                     "use the --uuid option to resolve the "
                                     "ambiguity.\n",
                                     symfile_path);
      } else if (num_matches == 1) {
        ModuleSP module_sp(matching_module_list.GetModuleAtIndex(0));

        // The module has not yet created its symbol vendor, we can just give
        // the existing target module the symfile path to use for when it
        // decides to create it!
        module_sp->SetSymbolFileFileSpec(symbol_fspec);

        SymbolVendor *symbol_vendor =
            module_sp->GetSymbolVendor(true, &result.GetErrorStream());
        if (symbol_vendor) {
          SymbolFile *symbol_file = symbol_vendor->GetSymbolFile();

          if (symbol_file) {
            ObjectFile *object_file = symbol_file->GetObjectFile();

            if (object_file && object_file->GetFileSpec() == symbol_fspec) {
              // Provide feedback that the symfile has been successfully added.
              const FileSpec &module_fs = module_sp->GetFileSpec();
              result.AppendMessageWithFormat(
                  "symbol file '%s' has been added to '%s'\n", symfile_path,
                  module_fs.GetPath().c_str());

              // Let clients know something changed in the module if it is
              // currently loaded
              ModuleList module_list;
              module_list.Append(module_sp);
              target->SymbolsDidLoad(module_list);

              // Make sure we load any scripting resources that may be embedded
              // in the debug info files in case the platform supports that.
              Status error;
              StreamString feedback_stream;
              module_sp->LoadScriptingResourceInTarget(target, error,
                                                       &feedback_stream);
              if (error.Fail() && error.AsCString())
                result.AppendWarningWithFormat(
                    "unable to load scripting data for module %s - error "
                    "reported was %s",
                    module_sp->GetFileSpec()
                        .GetFileNameStrippingExtension()
                        .GetCString(),
                    error.AsCString());
              else if (feedback_stream.GetSize())
                result.AppendWarningWithFormat("%s", feedback_stream.GetData());

              flush = true;
              result.SetStatus(eReturnStatusSuccessFinishResult);
              return true;
            }
          }
        }
        // Clear the symbol file spec if anything went wrong
        module_sp->SetSymbolFileFileSpec(FileSpec());
      }

      namespace fs = llvm::sys::fs;
      if (module_spec.GetUUID().IsValid()) {
        StreamString ss_symfile_uuid;
        module_spec.GetUUID().Dump(&ss_symfile_uuid);
        result.AppendErrorWithFormat(
            "symbol file '%s' (%s) does not match any existing module%s\n",
            symfile_path, ss_symfile_uuid.GetData(),
            !fs::is_regular_file(symbol_fspec.GetPath())
                ? "\n       please specify the full path to the symbol file"
                : "");
      } else {
        result.AppendErrorWithFormat(
            "symbol file '%s' does not match any existing module%s\n",
            symfile_path,
            !fs::is_regular_file(symbol_fspec.GetPath())
                ? "\n       please specify the full path to the symbol file"
                : "");
      }
    } else {
      result.AppendError(
          "one or more executable image paths must be specified");
    }
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  bool DoExecute(Args &args, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    result.SetStatus(eReturnStatusFailed);
    bool flush = false;
    ModuleSpec module_spec;
    const bool uuid_option_set =
        m_uuid_option_group.GetOptionValue().OptionWasSet();
    const bool file_option_set = m_file_option.GetOptionValue().OptionWasSet();
    const bool frame_option_set =
        m_current_frame_option.GetOptionValue().OptionWasSet();
    const size_t argc = args.GetArgumentCount();

    if (argc == 0) {
      if (uuid_option_set || file_option_set || frame_option_set) {
        bool success = false;
        bool error_set = false;
        if (frame_option_set) {
          Process *process = m_exe_ctx.GetProcessPtr();
          if (process) {
            const StateType process_state = process->GetState();
            if (StateIsStoppedState(process_state, true)) {
              StackFrame *frame = m_exe_ctx.GetFramePtr();
              if (frame) {
                ModuleSP frame_module_sp(
                    frame->GetSymbolContext(eSymbolContextModule).module_sp);
                if (frame_module_sp) {
                  if (FileSystem::Instance().Exists(
                          frame_module_sp->GetPlatformFileSpec())) {
                    module_spec.GetArchitecture() =
                        frame_module_sp->GetArchitecture();
                    module_spec.GetFileSpec() =
                        frame_module_sp->GetPlatformFileSpec();
                  }
                  module_spec.GetUUID() = frame_module_sp->GetUUID();
                  success = module_spec.GetUUID().IsValid() ||
                            module_spec.GetFileSpec();
                } else {
                  result.AppendError("frame has no module");
                  error_set = true;
                }
              } else {
                result.AppendError("invalid current frame");
                error_set = true;
              }
            } else {
              result.AppendErrorWithFormat("process is not stopped: %s",
                                           StateAsCString(process_state));
              error_set = true;
            }
          } else {
            result.AppendError(
                "a process must exist in order to use the --frame option");
            error_set = true;
          }
        } else {
          if (uuid_option_set) {
            module_spec.GetUUID() =
                m_uuid_option_group.GetOptionValue().GetCurrentValue();
            success |= module_spec.GetUUID().IsValid();
          } else if (file_option_set) {
            module_spec.GetFileSpec() =
                m_file_option.GetOptionValue().GetCurrentValue();
            ModuleSP module_sp(
                target->GetImages().FindFirstModule(module_spec));
            if (module_sp) {
              module_spec.GetFileSpec() = module_sp->GetFileSpec();
              module_spec.GetPlatformFileSpec() =
                  module_sp->GetPlatformFileSpec();
              module_spec.GetUUID() = module_sp->GetUUID();
              module_spec.GetArchitecture() = module_sp->GetArchitecture();
            } else {
              module_spec.GetArchitecture() = target->GetArchitecture();
            }
            success |= module_spec.GetUUID().IsValid() ||
                       FileSystem::Instance().Exists(module_spec.GetFileSpec());
          }
        }

        if (success) {
          if (Symbols::DownloadObjectAndSymbolFile(module_spec)) {
            if (module_spec.GetSymbolFileSpec())
              success = AddModuleSymbols(target, module_spec, flush, result);
          }
        }

        if (!success && !error_set) {
          StreamString error_strm;
          if (uuid_option_set) {
            error_strm.PutCString("unable to find debug symbols for UUID ");
            module_spec.GetUUID().Dump(&error_strm);
          } else if (file_option_set) {
            error_strm.PutCString(
                "unable to find debug symbols for the executable file ");
            error_strm << module_spec.GetFileSpec();
          } else if (frame_option_set) {
            error_strm.PutCString(
                "unable to find debug symbols for the current frame");
          }
          result.AppendError(error_strm.GetString());
        }
      } else {
        result.AppendError("one or more symbol file paths must be specified, "
                           "or options must be specified");
      }
    } else {
      if (uuid_option_set) {
        result.AppendError("specify either one or more paths to symbol files "
                           "or use the --uuid option without arguments");
      } else if (frame_option_set) {
        result.AppendError("specify either one or more paths to symbol files "
                           "or use the --frame option without arguments");
      } else if (file_option_set && argc > 1) {
        result.AppendError("specify at most one symbol file path when "
                           "--shlib option is set");
      } else {
        PlatformSP platform_sp(target->GetPlatform());

        for (auto &entry : args.entries()) {
          if (!entry.ref.empty()) {
            auto &symbol_file_spec = module_spec.GetSymbolFileSpec();
            symbol_file_spec.SetFile(entry.ref, FileSpec::Style::native);
            FileSystem::Instance().Resolve(symbol_file_spec);
            if (file_option_set) {
              module_spec.GetFileSpec() =
                  m_file_option.GetOptionValue().GetCurrentValue();
            }
            if (platform_sp) {
              FileSpec symfile_spec;
              if (platform_sp
                      ->ResolveSymbolFile(*target, module_spec, symfile_spec)
                      .Success())
                module_spec.GetSymbolFileSpec() = symfile_spec;
            }

            ArchSpec arch;
            bool symfile_exists =
                FileSystem::Instance().Exists(module_spec.GetSymbolFileSpec());

            if (symfile_exists) {
              if (!AddModuleSymbols(target, module_spec, flush, result))
                break;
            } else {
              std::string resolved_symfile_path =
                  module_spec.GetSymbolFileSpec().GetPath();
              if (resolved_symfile_path != entry.ref) {
                result.AppendErrorWithFormat(
                    "invalid module path '%s' with resolved path '%s'\n",
                    entry.c_str(), resolved_symfile_path.c_str());
                break;
              }
              result.AppendErrorWithFormat("invalid module path '%s'\n",
                                           entry.c_str());
              break;
            }
          }
        }
      }
    }

    if (flush) {
      Process *process = m_exe_ctx.GetProcessPtr();
      if (process)
        process->Flush();
    }
    return result.Succeeded();
  }

  OptionGroupOptions m_option_group;
  OptionGroupUUID m_uuid_option_group;
  OptionGroupFile m_file_option;
  OptionGroupBoolean m_current_frame_option;
};

#pragma mark CommandObjectTargetSymbols

//-------------------------------------------------------------------------
// CommandObjectTargetSymbols
//-------------------------------------------------------------------------

class CommandObjectTargetSymbols : public CommandObjectMultiword {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  CommandObjectTargetSymbols(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target symbols",
            "Commands for adding and managing debug symbol files.",
            "target symbols <sub-command> ...") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTargetSymbolsAdd(interpreter)));
  }

  ~CommandObjectTargetSymbols() override = default;

private:
  //------------------------------------------------------------------
  // For CommandObjectTargetModules only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(CommandObjectTargetSymbols);
};

#pragma mark CommandObjectTargetStopHookAdd

//-------------------------------------------------------------------------
// CommandObjectTargetStopHookAdd
//-------------------------------------------------------------------------

static constexpr OptionDefinition g_target_stop_hook_add_options[] = {
    // clang-format off
  { LLDB_OPT_SET_ALL, false, "one-liner",    'o', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeOneLiner,                                         "Specify a one-line breakpoint command inline. Be sure to surround it with quotes." },
  { LLDB_OPT_SET_ALL, false, "shlib",        's', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eModuleCompletion, eArgTypeShlibName,    "Set the module within which the stop-hook is to be run." },
  { LLDB_OPT_SET_ALL, false, "thread-index", 'x', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeThreadIndex,                                      "The stop hook is run only for the thread whose index matches this argument." },
  { LLDB_OPT_SET_ALL, false, "thread-id",    't', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeThreadID,                                         "The stop hook is run only for the thread whose TID matches this argument." },
  { LLDB_OPT_SET_ALL, false, "thread-name",  'T', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeThreadName,                                       "The stop hook is run only for the thread whose thread name matches this argument." },
  { LLDB_OPT_SET_ALL, false, "queue-name",   'q', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeQueueName,                                        "The stop hook is run only for threads in the queue whose name is given by this argument." },
  { LLDB_OPT_SET_1,   false, "file",         'f', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eSourceFileCompletion, eArgTypeFilename, "Specify the source file within which the stop-hook is to be run." },
  { LLDB_OPT_SET_1,   false, "start-line",   'l', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLineNum,                                          "Set the start of the line range for which the stop-hook is to be run." },
  { LLDB_OPT_SET_1,   false, "end-line",     'e', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeLineNum,                                          "Set the end of the line range for which the stop-hook is to be run." },
  { LLDB_OPT_SET_2,   false, "classname",    'c', OptionParser::eRequiredArgument, nullptr, {}, 0, eArgTypeClassName,                                        "Specify the class within which the stop-hook is to be run." },
  { LLDB_OPT_SET_3,   false, "name",         'n', OptionParser::eRequiredArgument, nullptr, {}, CommandCompletions::eSymbolCompletion, eArgTypeFunctionName, "Set the function name within which the stop hook will be run." },
    // clang-format on
};

class CommandObjectTargetStopHookAdd : public CommandObjectParsed,
                                       public IOHandlerDelegateMultiline {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions()
        : Options(), m_line_start(0), m_line_end(UINT_MAX),
          m_func_name_type_mask(eFunctionNameTypeAuto),
          m_sym_ctx_specified(false), m_thread_specified(false),
          m_use_one_liner(false), m_one_liner() {}

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::makeArrayRef(g_target_stop_hook_add_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'c':
        m_class_name = option_arg;
        m_sym_ctx_specified = true;
        break;

      case 'e':
        if (option_arg.getAsInteger(0, m_line_end)) {
          error.SetErrorStringWithFormat("invalid end line number: \"%s\"",
                                         option_arg.str().c_str());
          break;
        }
        m_sym_ctx_specified = true;
        break;

      case 'l':
        if (option_arg.getAsInteger(0, m_line_start)) {
          error.SetErrorStringWithFormat("invalid start line number: \"%s\"",
                                         option_arg.str().c_str());
          break;
        }
        m_sym_ctx_specified = true;
        break;

      case 'i':
        m_no_inlines = true;
        break;

      case 'n':
        m_function_name = option_arg;
        m_func_name_type_mask |= eFunctionNameTypeAuto;
        m_sym_ctx_specified = true;
        break;

      case 'f':
        m_file_name = option_arg;
        m_sym_ctx_specified = true;
        break;

      case 's':
        m_module_name = option_arg;
        m_sym_ctx_specified = true;
        break;

      case 't':
        if (option_arg.getAsInteger(0, m_thread_id))
          error.SetErrorStringWithFormat("invalid thread id string '%s'",
                                         option_arg.str().c_str());
        m_thread_specified = true;
        break;

      case 'T':
        m_thread_name = option_arg;
        m_thread_specified = true;
        break;

      case 'q':
        m_queue_name = option_arg;
        m_thread_specified = true;
        break;

      case 'x':
        if (option_arg.getAsInteger(0, m_thread_index))
          error.SetErrorStringWithFormat("invalid thread index string '%s'",
                                         option_arg.str().c_str());
        m_thread_specified = true;
        break;

      case 'o':
        m_use_one_liner = true;
        m_one_liner = option_arg;
        break;

      default:
        error.SetErrorStringWithFormat("unrecognized option %c.", short_option);
        break;
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_class_name.clear();
      m_function_name.clear();
      m_line_start = 0;
      m_line_end = UINT_MAX;
      m_file_name.clear();
      m_module_name.clear();
      m_func_name_type_mask = eFunctionNameTypeAuto;
      m_thread_id = LLDB_INVALID_THREAD_ID;
      m_thread_index = UINT32_MAX;
      m_thread_name.clear();
      m_queue_name.clear();

      m_no_inlines = false;
      m_sym_ctx_specified = false;
      m_thread_specified = false;

      m_use_one_liner = false;
      m_one_liner.clear();
    }

    std::string m_class_name;
    std::string m_function_name;
    uint32_t m_line_start;
    uint32_t m_line_end;
    std::string m_file_name;
    std::string m_module_name;
    uint32_t m_func_name_type_mask; // A pick from lldb::FunctionNameType.
    lldb::tid_t m_thread_id;
    uint32_t m_thread_index;
    std::string m_thread_name;
    std::string m_queue_name;
    bool m_sym_ctx_specified;
    bool m_no_inlines;
    bool m_thread_specified;
    // Instance variables to hold the values for one_liner options.
    bool m_use_one_liner;
    std::string m_one_liner;
  };

  CommandObjectTargetStopHookAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target stop-hook add",
                            "Add a hook to be executed when the target stops.",
                            "target stop-hook add"),
        IOHandlerDelegateMultiline("DONE",
                                   IOHandlerDelegate::Completion::LLDBCommand),
        m_options() {}

  ~CommandObjectTargetStopHookAdd() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  void IOHandlerActivated(IOHandler &io_handler) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFile());
    if (output_sp) {
      output_sp->PutCString(
          "Enter your stop hook command(s).  Type 'DONE' to end.\n");
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override {
    if (m_stop_hook_sp) {
      if (line.empty()) {
        StreamFileSP error_sp(io_handler.GetErrorStreamFile());
        if (error_sp) {
          error_sp->Printf("error: stop hook #%" PRIu64
                           " aborted, no commands.\n",
                           m_stop_hook_sp->GetID());
          error_sp->Flush();
        }
        Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
        if (target)
          target->RemoveStopHookByID(m_stop_hook_sp->GetID());
      } else {
        m_stop_hook_sp->GetCommandPointer()->SplitIntoLines(line);
        StreamFileSP output_sp(io_handler.GetOutputStreamFile());
        if (output_sp) {
          output_sp->Printf("Stop hook #%" PRIu64 " added.\n",
                            m_stop_hook_sp->GetID());
          output_sp->Flush();
        }
      }
      m_stop_hook_sp.reset();
    }
    io_handler.SetIsDone(true);
  }

  bool DoExecute(Args &command, CommandReturnObject &result) override {
    m_stop_hook_sp.reset();

    Target *target = GetSelectedOrDummyTarget();
    if (target) {
      Target::StopHookSP new_hook_sp = target->CreateStopHook();

      //  First step, make the specifier.
      std::unique_ptr<SymbolContextSpecifier> specifier_ap;
      if (m_options.m_sym_ctx_specified) {
        specifier_ap.reset(new SymbolContextSpecifier(
            m_interpreter.GetDebugger().GetSelectedTarget()));

        if (!m_options.m_module_name.empty()) {
          specifier_ap->AddSpecification(
              m_options.m_module_name.c_str(),
              SymbolContextSpecifier::eModuleSpecified);
        }

        if (!m_options.m_class_name.empty()) {
          specifier_ap->AddSpecification(
              m_options.m_class_name.c_str(),
              SymbolContextSpecifier::eClassOrNamespaceSpecified);
        }

        if (!m_options.m_file_name.empty()) {
          specifier_ap->AddSpecification(
              m_options.m_file_name.c_str(),
              SymbolContextSpecifier::eFileSpecified);
        }

        if (m_options.m_line_start != 0) {
          specifier_ap->AddLineSpecification(
              m_options.m_line_start,
              SymbolContextSpecifier::eLineStartSpecified);
        }

        if (m_options.m_line_end != UINT_MAX) {
          specifier_ap->AddLineSpecification(
              m_options.m_line_end, SymbolContextSpecifier::eLineEndSpecified);
        }

        if (!m_options.m_function_name.empty()) {
          specifier_ap->AddSpecification(
              m_options.m_function_name.c_str(),
              SymbolContextSpecifier::eFunctionSpecified);
        }
      }

      if (specifier_ap)
        new_hook_sp->SetSpecifier(specifier_ap.release());

      // Next see if any of the thread options have been entered:

      if (m_options.m_thread_specified) {
        ThreadSpec *thread_spec = new ThreadSpec();

        if (m_options.m_thread_id != LLDB_INVALID_THREAD_ID) {
          thread_spec->SetTID(m_options.m_thread_id);
        }

        if (m_options.m_thread_index != UINT32_MAX)
          thread_spec->SetIndex(m_options.m_thread_index);

        if (!m_options.m_thread_name.empty())
          thread_spec->SetName(m_options.m_thread_name.c_str());

        if (!m_options.m_queue_name.empty())
          thread_spec->SetQueueName(m_options.m_queue_name.c_str());

        new_hook_sp->SetThreadSpecifier(thread_spec);
      }
      if (m_options.m_use_one_liner) {
        // Use one-liner.
        new_hook_sp->GetCommandPointer()->AppendString(
            m_options.m_one_liner.c_str());
        result.AppendMessageWithFormat("Stop hook #%" PRIu64 " added.\n",
                                       new_hook_sp->GetID());
      } else {
        m_stop_hook_sp = new_hook_sp;
        m_interpreter.GetLLDBCommandsFromIOHandler(
            "> ",     // Prompt
            *this,    // IOHandlerDelegate
            true,     // Run IOHandler in async mode
            nullptr); // Baton for the "io_handler" that will be passed back
                      // into our IOHandlerDelegate functions
      }
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }

    return result.Succeeded();
  }

private:
  CommandOptions m_options;
  Target::StopHookSP m_stop_hook_sp;
};

#pragma mark CommandObjectTargetStopHookDelete

//-------------------------------------------------------------------------
// CommandObjectTargetStopHookDelete
//-------------------------------------------------------------------------

class CommandObjectTargetStopHookDelete : public CommandObjectParsed {
public:
  CommandObjectTargetStopHookDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target stop-hook delete",
                            "Delete a stop-hook.",
                            "target stop-hook delete [<idx>]") {}

  ~CommandObjectTargetStopHookDelete() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = GetSelectedOrDummyTarget();
    if (target) {
      // FIXME: see if we can use the breakpoint id style parser?
      size_t num_args = command.GetArgumentCount();
      if (num_args == 0) {
        if (!m_interpreter.Confirm("Delete all stop hooks?", true)) {
          result.SetStatus(eReturnStatusFailed);
          return false;
        } else {
          target->RemoveAllStopHooks();
        }
      } else {
        bool success;
        for (size_t i = 0; i < num_args; i++) {
          lldb::user_id_t user_id = StringConvert::ToUInt32(
              command.GetArgumentAtIndex(i), 0, 0, &success);
          if (!success) {
            result.AppendErrorWithFormat("invalid stop hook id: \"%s\".\n",
                                         command.GetArgumentAtIndex(i));
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
          success = target->RemoveStopHookByID(user_id);
          if (!success) {
            result.AppendErrorWithFormat("unknown stop hook id: \"%s\".\n",
                                         command.GetArgumentAtIndex(i));
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        }
      }
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }

    return result.Succeeded();
  }
};

#pragma mark CommandObjectTargetStopHookEnableDisable

//-------------------------------------------------------------------------
// CommandObjectTargetStopHookEnableDisable
//-------------------------------------------------------------------------

class CommandObjectTargetStopHookEnableDisable : public CommandObjectParsed {
public:
  CommandObjectTargetStopHookEnableDisable(CommandInterpreter &interpreter,
                                           bool enable, const char *name,
                                           const char *help, const char *syntax)
      : CommandObjectParsed(interpreter, name, help, syntax), m_enable(enable) {
  }

  ~CommandObjectTargetStopHookEnableDisable() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = GetSelectedOrDummyTarget();
    if (target) {
      // FIXME: see if we can use the breakpoint id style parser?
      size_t num_args = command.GetArgumentCount();
      bool success;

      if (num_args == 0) {
        target->SetAllStopHooksActiveState(m_enable);
      } else {
        for (size_t i = 0; i < num_args; i++) {
          lldb::user_id_t user_id = StringConvert::ToUInt32(
              command.GetArgumentAtIndex(i), 0, 0, &success);
          if (!success) {
            result.AppendErrorWithFormat("invalid stop hook id: \"%s\".\n",
                                         command.GetArgumentAtIndex(i));
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
          success = target->SetStopHookActiveStateByID(user_id, m_enable);
          if (!success) {
            result.AppendErrorWithFormat("unknown stop hook id: \"%s\".\n",
                                         command.GetArgumentAtIndex(i));
            result.SetStatus(eReturnStatusFailed);
            return false;
          }
        }
      }
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
    }
    return result.Succeeded();
  }

private:
  bool m_enable;
};

#pragma mark CommandObjectTargetStopHookList

//-------------------------------------------------------------------------
// CommandObjectTargetStopHookList
//-------------------------------------------------------------------------

class CommandObjectTargetStopHookList : public CommandObjectParsed {
public:
  CommandObjectTargetStopHookList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "target stop-hook list",
                            "List all stop-hooks.",
                            "target stop-hook list [<type>]") {}

  ~CommandObjectTargetStopHookList() override = default;

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = GetSelectedOrDummyTarget();
    if (!target) {
      result.AppendError("invalid target\n");
      result.SetStatus(eReturnStatusFailed);
      return result.Succeeded();
    }

    size_t num_hooks = target->GetNumStopHooks();
    if (num_hooks == 0) {
      result.GetOutputStream().PutCString("No stop hooks.\n");
    } else {
      for (size_t i = 0; i < num_hooks; i++) {
        Target::StopHookSP this_hook = target->GetStopHookAtIndex(i);
        if (i > 0)
          result.GetOutputStream().PutCString("\n");
        this_hook->GetDescription(&(result.GetOutputStream()),
                                  eDescriptionLevelFull);
      }
    }
    result.SetStatus(eReturnStatusSuccessFinishResult);
    return result.Succeeded();
  }
};

#pragma mark CommandObjectMultiwordTargetStopHooks

//-------------------------------------------------------------------------
// CommandObjectMultiwordTargetStopHooks
//-------------------------------------------------------------------------

class CommandObjectMultiwordTargetStopHooks : public CommandObjectMultiword {
public:
  CommandObjectMultiwordTargetStopHooks(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "target stop-hook",
            "Commands for operating on debugger target stop-hooks.",
            "target stop-hook <subcommand> [<subcommand-options>]") {
    LoadSubCommand("add", CommandObjectSP(
                              new CommandObjectTargetStopHookAdd(interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectTargetStopHookDelete(interpreter)));
    LoadSubCommand("disable",
                   CommandObjectSP(new CommandObjectTargetStopHookEnableDisable(
                       interpreter, false, "target stop-hook disable [<id>]",
                       "Disable a stop-hook.", "target stop-hook disable")));
    LoadSubCommand("enable",
                   CommandObjectSP(new CommandObjectTargetStopHookEnableDisable(
                       interpreter, true, "target stop-hook enable [<id>]",
                       "Enable a stop-hook.", "target stop-hook enable")));
    LoadSubCommand("list", CommandObjectSP(new CommandObjectTargetStopHookList(
                               interpreter)));
  }

  ~CommandObjectMultiwordTargetStopHooks() override = default;
};

#pragma mark CommandObjectMultiwordTarget

//-------------------------------------------------------------------------
// CommandObjectMultiwordTarget
//-------------------------------------------------------------------------

CommandObjectMultiwordTarget::CommandObjectMultiwordTarget(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "target",
                             "Commands for operating on debugger targets.",
                             "target <subcommand> [<subcommand-options>]") {
  LoadSubCommand("create",
                 CommandObjectSP(new CommandObjectTargetCreate(interpreter)));
  LoadSubCommand("delete",
                 CommandObjectSP(new CommandObjectTargetDelete(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectTargetList(interpreter)));
  LoadSubCommand("select",
                 CommandObjectSP(new CommandObjectTargetSelect(interpreter)));
  LoadSubCommand(
      "stop-hook",
      CommandObjectSP(new CommandObjectMultiwordTargetStopHooks(interpreter)));
  LoadSubCommand("modules",
                 CommandObjectSP(new CommandObjectTargetModules(interpreter)));
  LoadSubCommand("symbols",
                 CommandObjectSP(new CommandObjectTargetSymbols(interpreter)));
  LoadSubCommand("variable",
                 CommandObjectSP(new CommandObjectTargetVariable(interpreter)));
}

CommandObjectMultiwordTarget::~CommandObjectMultiwordTarget() = default;
