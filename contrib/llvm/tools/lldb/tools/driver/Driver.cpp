//===-- Driver.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Driver.h"

#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBHostOS.h"
#include "lldb/API/SBLanguageRuntime.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <csignal>
#include <string>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Includes for pipe()
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#if !defined(__APPLE__)
#include "llvm/Support/DataTypes.h"
#endif

using namespace lldb;
using namespace llvm;

namespace {
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  OPT_##ID,
#include "Options.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE) const char *const NAME[] = VALUE;
#include "Options.inc"
#undef PREFIX

const opt::OptTable::Info InfoTable[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  {                                                                            \
      PREFIX,      NAME,      HELPTEXT,                                        \
      METAVAR,     OPT_##ID,  opt::Option::KIND##Class,                        \
      PARAM,       FLAGS,     OPT_##GROUP,                                     \
      OPT_##ALIAS, ALIASARGS, VALUES},
#include "Options.inc"
#undef OPTION
};

class LLDBOptTable : public opt::OptTable {
public:
  LLDBOptTable() : OptTable(InfoTable) {}
};
} // namespace

static void reset_stdin_termios();
static bool g_old_stdin_termios_is_valid = false;
static struct termios g_old_stdin_termios;

static Driver *g_driver = nullptr;

// In the Driver::MainLoop, we change the terminal settings.  This function is
// added as an atexit handler to make sure we clean them up.
static void reset_stdin_termios() {
  if (g_old_stdin_termios_is_valid) {
    g_old_stdin_termios_is_valid = false;
    ::tcsetattr(STDIN_FILENO, TCSANOW, &g_old_stdin_termios);
  }
}

Driver::Driver()
    : SBBroadcaster("Driver"), m_debugger(SBDebugger::Create(false)) {
  // We want to be able to handle CTRL+D in the terminal to have it terminate
  // certain input
  m_debugger.SetCloseInputOnEOF(false);
  g_driver = this;
}

Driver::~Driver() { g_driver = nullptr; }

void Driver::OptionData::AddLocalLLDBInit() {
  // If there is a local .lldbinit, add that to the list of things to be
  // sourced, if the settings permit it.
  SBFileSpec local_lldbinit(".lldbinit", true);
  SBFileSpec homedir_dot_lldb = SBHostOS::GetUserHomeDirectory();
  homedir_dot_lldb.AppendPathComponent(".lldbinit");

  // Only read .lldbinit in the current working directory if it's not the same
  // as the .lldbinit in the home directory (which is already being read in).
  if (local_lldbinit.Exists() && strcmp(local_lldbinit.GetDirectory(),
                                        homedir_dot_lldb.GetDirectory()) != 0) {
    char path[PATH_MAX];
    local_lldbinit.GetPath(path, sizeof(path));
    InitialCmdEntry entry(path, true, true, true);
    m_after_file_commands.push_back(entry);
  }
}

void Driver::OptionData::AddInitialCommand(std::string command,
                                           CommandPlacement placement,
                                           bool is_file, SBError &error) {
  std::vector<InitialCmdEntry> *command_set;
  switch (placement) {
  case eCommandPlacementBeforeFile:
    command_set = &(m_initial_commands);
    break;
  case eCommandPlacementAfterFile:
    command_set = &(m_after_file_commands);
    break;
  case eCommandPlacementAfterCrash:
    command_set = &(m_after_crash_commands);
    break;
  }

  if (is_file) {
    SBFileSpec file(command.c_str());
    if (file.Exists())
      command_set->push_back(InitialCmdEntry(command, is_file, false));
    else if (file.ResolveExecutableLocation()) {
      char final_path[PATH_MAX];
      file.GetPath(final_path, sizeof(final_path));
      command_set->push_back(InitialCmdEntry(final_path, is_file, false));
    } else
      error.SetErrorStringWithFormat(
          "file specified in --source (-s) option doesn't exist: '%s'",
          command.c_str());
  } else
    command_set->push_back(InitialCmdEntry(command, is_file, false));
}

const char *Driver::GetFilename() const {
  if (m_option_data.m_args.empty())
    return nullptr;
  return m_option_data.m_args.front().c_str();
}

const char *Driver::GetCrashLogFilename() const {
  if (m_option_data.m_crash_log.empty())
    return nullptr;
  return m_option_data.m_crash_log.c_str();
}

lldb::ScriptLanguage Driver::GetScriptLanguage() const {
  return m_option_data.m_script_lang;
}

void Driver::WriteCommandsForSourcing(CommandPlacement placement,
                                      SBStream &strm) {
  std::vector<OptionData::InitialCmdEntry> *command_set;
  switch (placement) {
  case eCommandPlacementBeforeFile:
    command_set = &m_option_data.m_initial_commands;
    break;
  case eCommandPlacementAfterFile:
    command_set = &m_option_data.m_after_file_commands;
    break;
  case eCommandPlacementAfterCrash:
    command_set = &m_option_data.m_after_crash_commands;
    break;
  }

  for (const auto &command_entry : *command_set) {
    const char *command = command_entry.contents.c_str();
    if (command_entry.is_file) {
      // If this command_entry is a file to be sourced, and it's the ./.lldbinit
      // file (the .lldbinit
      // file in the current working directory), only read it if
      // target.load-cwd-lldbinit is 'true'.
      if (command_entry.is_cwd_lldbinit_file_read) {
        SBStringList strlist = lldb::SBDebugger::GetInternalVariableValue(
            "target.load-cwd-lldbinit", m_debugger.GetInstanceName());
        if (strlist.GetSize() == 1 &&
            strcmp(strlist.GetStringAtIndex(0), "warn") == 0) {
          FILE *output = m_debugger.GetOutputFileHandle();
          ::fprintf(
              output,
              "There is a .lldbinit file in the current directory which is not "
              "being read.\n"
              "To silence this warning without sourcing in the local "
              ".lldbinit,\n"
              "add the following to the lldbinit file in your home directory:\n"
              "    settings set target.load-cwd-lldbinit false\n"
              "To allow lldb to source .lldbinit files in the current working "
              "directory,\n"
              "set the value of this variable to true.  Only do so if you "
              "understand and\n"
              "accept the security risk.\n");
          return;
        }
        if (strlist.GetSize() == 1 &&
            strcmp(strlist.GetStringAtIndex(0), "false") == 0) {
          return;
        }
      }
      bool source_quietly =
          m_option_data.m_source_quietly || command_entry.source_quietly;
      strm.Printf("command source -s %i '%s'\n",
                  static_cast<int>(source_quietly), command);
    } else
      strm.Printf("%s\n", command);
  }
}

bool Driver::GetDebugMode() const { return m_option_data.m_debug_mode; }

// Check the arguments that were passed to this program to make sure they are
// valid and to get their argument values (if any).  Return a boolean value
// indicating whether or not to start up the full debugger (i.e. the Command
// Interpreter) or not.  Return FALSE if the arguments were invalid OR if the
// user only wanted help or version information.
SBError Driver::ProcessArgs(const opt::InputArgList &args, bool &exiting) {
  SBError error;
  m_option_data.AddLocalLLDBInit();

  // This is kind of a pain, but since we make the debugger in the Driver's
  // constructor, we can't know at that point whether we should read in init
  // files yet.  So we don't read them in in the Driver constructor, then set
  // the flags back to "read them in" here, and then if we see the "-n" flag,
  // we'll turn it off again.  Finally we have to read them in by hand later in
  // the main loop.
  m_debugger.SkipLLDBInitFiles(false);
  m_debugger.SkipAppInitFiles(false);

  if (args.hasArg(OPT_version)) {
    m_option_data.m_print_version = true;
  }

  if (args.hasArg(OPT_python_path)) {
    m_option_data.m_print_python_path = true;
  }

  if (args.hasArg(OPT_batch)) {
    m_option_data.m_batch = true;
  }

  if (auto *arg = args.getLastArg(OPT_core)) {
    auto arg_value = arg->getValue();
    SBFileSpec file(arg_value);
    if (!file.Exists()) {
      error.SetErrorStringWithFormat(
          "file specified in --core (-c) option doesn't exist: '%s'",
          arg_value);
      return error;
    }
    m_option_data.m_core_file = arg_value;
  }

  if (args.hasArg(OPT_editor)) {
    m_option_data.m_use_external_editor = true;
  }

  if (args.hasArg(OPT_no_lldbinit)) {
    m_debugger.SkipLLDBInitFiles(true);
    m_debugger.SkipAppInitFiles(true);
  }

  if (args.hasArg(OPT_no_use_colors)) {
    m_debugger.SetUseColor(false);
  }

  if (auto *arg = args.getLastArg(OPT_file)) {
    auto arg_value = arg->getValue();
    SBFileSpec file(arg_value);
    if (file.Exists()) {
      m_option_data.m_args.emplace_back(arg_value);
    } else if (file.ResolveExecutableLocation()) {
      char path[PATH_MAX];
      file.GetPath(path, sizeof(path));
      m_option_data.m_args.emplace_back(path);
    } else {
      error.SetErrorStringWithFormat(
          "file specified in --file (-f) option doesn't exist: '%s'",
          arg_value);
      return error;
    }
  }

  if (auto *arg = args.getLastArg(OPT_arch)) {
    auto arg_value = arg->getValue();
    if (!lldb::SBDebugger::SetDefaultArchitecture(arg_value)) {
      error.SetErrorStringWithFormat(
          "invalid architecture in the -a or --arch option: '%s'", arg_value);
      return error;
    }
  }

  if (auto *arg = args.getLastArg(OPT_script_language)) {
    auto arg_value = arg->getValue();
    m_option_data.m_script_lang = m_debugger.GetScriptingLanguage(arg_value);
  }

  if (args.hasArg(OPT_no_use_colors)) {
    m_option_data.m_debug_mode = true;
  }

  if (args.hasArg(OPT_no_use_colors)) {
    m_debugger.SetUseColor(false);
  }

  if (args.hasArg(OPT_source_quietly)) {
    m_option_data.m_source_quietly = true;
  }

  if (auto *arg = args.getLastArg(OPT_attach_name)) {
    auto arg_value = arg->getValue();
    m_option_data.m_process_name = arg_value;
  }

  if (args.hasArg(OPT_wait_for)) {
    m_option_data.m_wait_for = true;
  }

  if (auto *arg = args.getLastArg(OPT_attach_pid)) {
    auto arg_value = arg->getValue();
    char *remainder;
    m_option_data.m_process_pid = strtol(arg_value, &remainder, 0);
    if (remainder == arg_value || *remainder != '\0') {
      error.SetErrorStringWithFormat(
          "Could not convert process PID: \"%s\" into a pid.", arg_value);
      return error;
    }
  }

  if (auto *arg = args.getLastArg(OPT_repl_language)) {
    auto arg_value = arg->getValue();
    m_option_data.m_repl_lang =
        SBLanguageRuntime::GetLanguageTypeFromString(arg_value);
    if (m_option_data.m_repl_lang == eLanguageTypeUnknown) {
      error.SetErrorStringWithFormat("Unrecognized language name: \"%s\"",
                                     arg_value);
      return error;
    }
  }

  if (args.hasArg(OPT_repl)) {
    m_option_data.m_repl = true;
  }

  if (auto *arg = args.getLastArg(OPT_repl_)) {
    m_option_data.m_repl = true;
    if (auto arg_value = arg->getValue())
      m_option_data.m_repl_options = arg_value;
  }

  // We need to process the options below together as their relative order
  // matters.
  for (auto *arg : args.filtered(OPT_source_on_crash, OPT_one_line_on_crash,
                                 OPT_source, OPT_source_before_file,
                                 OPT_one_line, OPT_one_line_before_file)) {
    auto arg_value = arg->getValue();
    if (arg->getOption().matches(OPT_source_on_crash)) {
      m_option_data.AddInitialCommand(arg_value, eCommandPlacementAfterCrash,
                                      true, error);
      if (error.Fail())
        return error;
    }

    if (arg->getOption().matches(OPT_one_line_on_crash)) {
      m_option_data.AddInitialCommand(arg_value, eCommandPlacementAfterCrash,
                                      false, error);
      if (error.Fail())
        return error;
    }

    if (arg->getOption().matches(OPT_source)) {
      m_option_data.AddInitialCommand(arg_value, eCommandPlacementAfterFile,
                                      true, error);
      if (error.Fail())
        return error;
    }

    if (arg->getOption().matches(OPT_source_before_file)) {
      m_option_data.AddInitialCommand(arg_value, eCommandPlacementBeforeFile,
                                      true, error);
      if (error.Fail())
        return error;
    }

    if (arg->getOption().matches(OPT_one_line)) {
      m_option_data.AddInitialCommand(arg_value, eCommandPlacementAfterFile,
                                      false, error);
      if (error.Fail())
        return error;
    }

    if (arg->getOption().matches(OPT_one_line_before_file)) {
      m_option_data.AddInitialCommand(arg_value, eCommandPlacementBeforeFile,
                                      false, error);
      if (error.Fail())
        return error;
    }
  }

  if (m_option_data.m_process_name.empty() &&
      m_option_data.m_process_pid == LLDB_INVALID_PROCESS_ID) {

    // If the option data args array is empty that means the file was not
    // specified with -f and we need to get it from the input args.
    if (m_option_data.m_args.empty()) {
      if (auto *arg = args.getLastArgNoClaim(OPT_INPUT)) {
        m_option_data.m_args.push_back(arg->getAsString((args)));
      }
    }

    // Any argument following -- is an argument for the inferior.
    if (auto *arg = args.getLastArgNoClaim(OPT_REM)) {
      for (auto value : arg->getValues())
        m_option_data.m_args.emplace_back(value);
    }
  } else if (args.getLastArgNoClaim() != nullptr) {
    WithColor::warning() << "program arguments are ignored when attaching.\n";
  }

  if (m_option_data.m_print_version) {
    llvm::outs() << lldb::SBDebugger::GetVersionString() << '\n';
    exiting = true;
    return error;
  }

  if (m_option_data.m_print_python_path) {
    SBFileSpec python_file_spec = SBHostOS::GetLLDBPythonPath();
    if (python_file_spec.IsValid()) {
      char python_path[PATH_MAX];
      size_t num_chars = python_file_spec.GetPath(python_path, PATH_MAX);
      if (num_chars < PATH_MAX) {
        llvm::outs() << python_path << '\n';
      } else
        llvm::outs() << "<PATH TOO LONG>\n";
    } else
      llvm::outs() << "<COULD NOT FIND PATH>\n";
    exiting = true;
    return error;
  }

  return error;
}

static ::FILE *PrepareCommandsForSourcing(const char *commands_data,
                                          size_t commands_size, int fds[2]) {
  enum PIPES { READ, WRITE }; // Constants 0 and 1 for READ and WRITE

  ::FILE *commands_file = nullptr;
  fds[0] = -1;
  fds[1] = -1;
  int err = 0;
#ifdef _WIN32
  err = _pipe(fds, commands_size, O_BINARY);
#else
  err = pipe(fds);
#endif
  if (err == 0) {
    ssize_t nrwr = write(fds[WRITE], commands_data, commands_size);
    if (nrwr < 0) {
      WithColor::error()
          << format(
                 "write(%i, %p, %" PRIu64
                 ") failed (errno = %i) when trying to open LLDB commands pipe",
                 fds[WRITE], static_cast<const void *>(commands_data),
                 static_cast<uint64_t>(commands_size), errno)
          << '\n';
    } else if (static_cast<size_t>(nrwr) == commands_size) {
// Close the write end of the pipe so when we give the read end to
// the debugger/command interpreter it will exit when it consumes all
// of the data
#ifdef _WIN32
      _close(fds[WRITE]);
      fds[WRITE] = -1;
#else
      close(fds[WRITE]);
      fds[WRITE] = -1;
#endif
      // Now open the read file descriptor in a FILE * that we can give to
      // the debugger as an input handle
      commands_file = fdopen(fds[READ], "r");
      if (commands_file != nullptr) {
        fds[READ] = -1; // The FILE * 'commands_file' now owns the read
                        // descriptor Hand ownership if the FILE * over to the
                        // debugger for "commands_file".
      } else {
        WithColor::error() << format("fdopen(%i, \"r\") failed (errno = %i) "
                                     "when trying to open LLDB commands pipe",
                                     fds[READ], errno)
                           << '\n';
      }
    }
  } else {
    WithColor::error()
        << "can't create pipe file descriptors for LLDB commands\n";
  }

  return commands_file;
}

void CleanupAfterCommandSourcing(int fds[2]) {
  enum PIPES { READ, WRITE }; // Constants 0 and 1 for READ and WRITE

  // Close any pipes that we still have ownership of
  if (fds[WRITE] != -1) {
#ifdef _WIN32
    _close(fds[WRITE]);
    fds[WRITE] = -1;
#else
    close(fds[WRITE]);
    fds[WRITE] = -1;
#endif
  }

  if (fds[READ] != -1) {
#ifdef _WIN32
    _close(fds[READ]);
    fds[READ] = -1;
#else
    close(fds[READ]);
    fds[READ] = -1;
#endif
  }
}

std::string EscapeString(std::string arg) {
  std::string::size_type pos = 0;
  while ((pos = arg.find_first_of("\"\\", pos)) != std::string::npos) {
    arg.insert(pos, 1, '\\');
    pos += 2;
  }
  return '"' + arg + '"';
}

int Driver::MainLoop() {
  if (::tcgetattr(STDIN_FILENO, &g_old_stdin_termios) == 0) {
    g_old_stdin_termios_is_valid = true;
    atexit(reset_stdin_termios);
  }

#ifndef _MSC_VER
  // Disabling stdin buffering with MSVC's 2015 CRT exposes a bug in fgets
  // which causes it to miss newlines depending on whether there have been an
  // odd or even number of characters.  Bug has been reported to MS via Connect.
  ::setbuf(stdin, nullptr);
#endif
  ::setbuf(stdout, nullptr);

  m_debugger.SetErrorFileHandle(stderr, false);
  m_debugger.SetOutputFileHandle(stdout, false);
  m_debugger.SetInputFileHandle(stdin,
                                false); // Don't take ownership of STDIN yet...

  m_debugger.SetUseExternalEditor(m_option_data.m_use_external_editor);

  struct winsize window_size;
  if ((isatty(STDIN_FILENO) != 0) &&
      ::ioctl(STDIN_FILENO, TIOCGWINSZ, &window_size) == 0) {
    if (window_size.ws_col > 0)
      m_debugger.SetTerminalWidth(window_size.ws_col);
  }

  SBCommandInterpreter sb_interpreter = m_debugger.GetCommandInterpreter();

  // Before we handle any options from the command line, we parse the
  // .lldbinit file in the user's home directory.
  SBCommandReturnObject result;
  sb_interpreter.SourceInitFileInHomeDirectory(result);
  if (GetDebugMode()) {
    result.PutError(m_debugger.GetErrorFileHandle());
    result.PutOutput(m_debugger.GetOutputFileHandle());
  }

  // We allow the user to specify an exit code when calling quit which we will
  // return when exiting.
  m_debugger.GetCommandInterpreter().AllowExitCodeOnQuit(true);

  // Now we handle options we got from the command line
  SBStream commands_stream;

  // First source in the commands specified to be run before the file arguments
  // are processed.
  WriteCommandsForSourcing(eCommandPlacementBeforeFile, commands_stream);

  const size_t num_args = m_option_data.m_args.size();
  if (num_args > 0) {
    char arch_name[64];
    if (lldb::SBDebugger::GetDefaultArchitecture(arch_name, sizeof(arch_name)))
      commands_stream.Printf("target create --arch=%s %s", arch_name,
                             EscapeString(m_option_data.m_args[0]).c_str());
    else
      commands_stream.Printf("target create %s",
                             EscapeString(m_option_data.m_args[0]).c_str());

    if (!m_option_data.m_core_file.empty()) {
      commands_stream.Printf(" --core %s",
                             EscapeString(m_option_data.m_core_file).c_str());
    }
    commands_stream.Printf("\n");

    if (num_args > 1) {
      commands_stream.Printf("settings set -- target.run-args ");
      for (size_t arg_idx = 1; arg_idx < num_args; ++arg_idx)
        commands_stream.Printf(
            " %s", EscapeString(m_option_data.m_args[arg_idx]).c_str());
      commands_stream.Printf("\n");
    }
  } else if (!m_option_data.m_core_file.empty()) {
    commands_stream.Printf("target create --core %s\n",
                           EscapeString(m_option_data.m_core_file).c_str());
  } else if (!m_option_data.m_process_name.empty()) {
    commands_stream.Printf("process attach --name %s",
                           EscapeString(m_option_data.m_process_name).c_str());

    if (m_option_data.m_wait_for)
      commands_stream.Printf(" --waitfor");

    commands_stream.Printf("\n");

  } else if (LLDB_INVALID_PROCESS_ID != m_option_data.m_process_pid) {
    commands_stream.Printf("process attach --pid %" PRIu64 "\n",
                           m_option_data.m_process_pid);
  }

  WriteCommandsForSourcing(eCommandPlacementAfterFile, commands_stream);

  if (GetDebugMode()) {
    result.PutError(m_debugger.GetErrorFileHandle());
    result.PutOutput(m_debugger.GetOutputFileHandle());
  }

  bool handle_events = true;
  bool spawn_thread = false;

  if (m_option_data.m_repl) {
    const char *repl_options = nullptr;
    if (!m_option_data.m_repl_options.empty())
      repl_options = m_option_data.m_repl_options.c_str();
    SBError error(m_debugger.RunREPL(m_option_data.m_repl_lang, repl_options));
    if (error.Fail()) {
      const char *error_cstr = error.GetCString();
      if ((error_cstr != nullptr) && (error_cstr[0] != 0))
        WithColor::error() << error_cstr << '\n';
      else
        WithColor::error() << error.GetError() << '\n';
    }
  } else {
    // Check if we have any data in the commands stream, and if so, save it to a
    // temp file
    // so we can then run the command interpreter using the file contents.
    const char *commands_data = commands_stream.GetData();
    const size_t commands_size = commands_stream.GetSize();

    // The command file might have requested that we quit, this variable will
    // track that.
    bool quit_requested = false;
    bool stopped_for_crash = false;
    if ((commands_data != nullptr) && (commands_size != 0u)) {
      int initial_commands_fds[2];
      bool success = true;
      FILE *commands_file = PrepareCommandsForSourcing(
          commands_data, commands_size, initial_commands_fds);
      if (commands_file != nullptr) {
        m_debugger.SetInputFileHandle(commands_file, true);

        // Set the debugger into Sync mode when running the command file.
        // Otherwise command files
        // that run the target won't run in a sensible way.
        bool old_async = m_debugger.GetAsync();
        m_debugger.SetAsync(false);
        int num_errors;

        SBCommandInterpreterRunOptions options;
        options.SetStopOnError(true);
        if (m_option_data.m_batch)
          options.SetStopOnCrash(true);

        m_debugger.RunCommandInterpreter(handle_events, spawn_thread, options,
                                         num_errors, quit_requested,
                                         stopped_for_crash);

        if (m_option_data.m_batch && stopped_for_crash &&
            !m_option_data.m_after_crash_commands.empty()) {
          int crash_command_fds[2];
          SBStream crash_commands_stream;
          WriteCommandsForSourcing(eCommandPlacementAfterCrash,
                                   crash_commands_stream);
          const char *crash_commands_data = crash_commands_stream.GetData();
          const size_t crash_commands_size = crash_commands_stream.GetSize();
          commands_file = PrepareCommandsForSourcing(
              crash_commands_data, crash_commands_size, crash_command_fds);
          if (commands_file != nullptr) {
            bool local_quit_requested;
            bool local_stopped_for_crash;
            m_debugger.SetInputFileHandle(commands_file, true);

            m_debugger.RunCommandInterpreter(
                handle_events, spawn_thread, options, num_errors,
                local_quit_requested, local_stopped_for_crash);
            if (local_quit_requested)
              quit_requested = true;
          }
        }
        m_debugger.SetAsync(old_async);
      } else
        success = false;

      // Close any pipes that we still have ownership of
      CleanupAfterCommandSourcing(initial_commands_fds);

      // Something went wrong with command pipe
      if (!success) {
        exit(1);
      }
    }

    // Now set the input file handle to STDIN and run the command
    // interpreter again in interactive mode and let the debugger
    // take ownership of stdin

    bool go_interactive = true;
    if (quit_requested)
      go_interactive = false;
    else if (m_option_data.m_batch && !stopped_for_crash)
      go_interactive = false;

    if (go_interactive) {
      m_debugger.SetInputFileHandle(stdin, true);
      m_debugger.RunCommandInterpreter(handle_events, spawn_thread);
    }
  }

  reset_stdin_termios();
  fclose(stdin);

  int exit_code = sb_interpreter.GetQuitStatus();
  SBDebugger::Destroy(m_debugger);
  return exit_code;
}

void Driver::ResizeWindow(unsigned short col) {
  GetDebugger().SetTerminalWidth(col);
}

void sigwinch_handler(int signo) {
  struct winsize window_size;
  if ((isatty(STDIN_FILENO) != 0) &&
      ::ioctl(STDIN_FILENO, TIOCGWINSZ, &window_size) == 0) {
    if ((window_size.ws_col > 0) && g_driver != nullptr) {
      g_driver->ResizeWindow(window_size.ws_col);
    }
  }
}

void sigint_handler(int signo) {
  static std::atomic_flag g_interrupt_sent = ATOMIC_FLAG_INIT;
  if (g_driver != nullptr) {
    if (!g_interrupt_sent.test_and_set()) {
      g_driver->GetDebugger().DispatchInputInterrupt();
      g_interrupt_sent.clear();
      return;
    }
  }

  _exit(signo);
}

void sigtstp_handler(int signo) {
  if (g_driver != nullptr)
    g_driver->GetDebugger().SaveInputTerminalState();

  signal(signo, SIG_DFL);
  kill(getpid(), signo);
  signal(signo, sigtstp_handler);
}

void sigcont_handler(int signo) {
  if (g_driver != nullptr)
    g_driver->GetDebugger().RestoreInputTerminalState();

  signal(signo, SIG_DFL);
  kill(getpid(), signo);
  signal(signo, sigcont_handler);
}

static void printHelp(LLDBOptTable &table, llvm::StringRef tool_name) {
  std::string usage_str = tool_name.str() + "options";
  table.PrintHelp(llvm::outs(), usage_str.c_str(), "LLDB", false);

  std::string examples = R"___(
EXAMPLES:
  The debugger can be started in several modes.

  Passing an executable as a positional argument prepares lldb to debug the
  given executable. Arguments passed after -- are considered arguments to the
  debugged executable.

    lldb --arch x86_64 /path/to/program -- --arch arvm7

  Passing one of the attach options causes lldb to immediately attach to the
  given process.

    lldb -p <pid>
    lldb -n <process-name>

  Passing --repl starts lldb in REPL mode.

    lldb -r

  Passing --core causes lldb to debug the core file.

    lldb -c /path/to/core

  Command options can be combined with either mode and cause lldb to run the
  specified commands before or after events, like loading the file or crashing,
  in the order provided on the command line.

    lldb -O 'settings set stop-disassembly-count 20' -o 'run' -o 'bt'
    lldb -S /source/before/file -s /source/after/file
    lldb -K /source/before/crash -k /source/after/crash
  )___";
  llvm::outs() << examples;
}

int
#ifdef _MSC_VER
wmain(int argc, wchar_t const *wargv[])
#else
main(int argc, char const *argv[])
#endif
{
#ifdef _MSC_VER
  // Convert wide arguments to UTF-8
  std::vector<std::string> argvStrings(argc);
  std::vector<const char *> argvPointers(argc);
  for (int i = 0; i != argc; ++i) {
    llvm::convertWideToUTF8(wargv[i], argvStrings[i]);
    argvPointers[i] = argvStrings[i].c_str();
  }
  const char **argv = argvPointers.data();
#endif

  // Print stack trace on crash.
  llvm::StringRef ToolName = llvm::sys::path::filename(argv[0]);
  llvm::sys::PrintStackTraceOnErrorSignal(ToolName);
  llvm::PrettyStackTraceProgram X(argc, argv);

  // Parse arguments.
  LLDBOptTable T;
  unsigned MAI;
  unsigned MAC;
  ArrayRef<const char *> arg_arr = makeArrayRef(argv + 1, argc - 1);
  opt::InputArgList input_args = T.ParseArgs(arg_arr, MAI, MAC);

  if (input_args.hasArg(OPT_help)) {
    printHelp(T, ToolName);
    return 0;
  }

  for (auto *arg : input_args.filtered(OPT_UNKNOWN)) {
    WithColor::warning() << "ignoring unknown option: " << arg->getSpelling()
                         << '\n';
  }

  SBInitializerOptions options;

  if (auto *arg = input_args.getLastArg(OPT_capture)) {
    auto arg_value = arg->getValue();
    options.SetReproducerPath(arg_value);
    options.SetCaptureReproducer(true);
  }

  if (auto *arg = input_args.getLastArg(OPT_replay)) {
    auto arg_value = arg->getValue();
    options.SetReplayReproducer(true);
    options.SetReproducerPath(arg_value);
  }

  SBError error = SBDebugger::Initialize(options);
  if (error.Fail()) {
    WithColor::error() << "initialization failed: " << error.GetCString()
                       << '\n';
    return 1;
  }

  SBHostOS::ThreadCreated("<lldb.driver.main-thread>");

  signal(SIGINT, sigint_handler);
#if !defined(_MSC_VER)
  signal(SIGPIPE, SIG_IGN);
  signal(SIGWINCH, sigwinch_handler);
  signal(SIGTSTP, sigtstp_handler);
  signal(SIGCONT, sigcont_handler);
#endif

  int exit_code = 0;
  // Create a scope for driver so that the driver object will destroy itself
  // before SBDebugger::Terminate() is called.
  {
    Driver driver;

    bool exiting = false;
    SBError error(driver.ProcessArgs(input_args, exiting));
    if (error.Fail()) {
      exit_code = 1;
      if (const char *error_cstr = error.GetCString())
        WithColor::error() << error_cstr << '\n';
    } else if (!exiting) {
      exit_code = driver.MainLoop();
    }
  }

  SBDebugger::Terminate();
  return exit_code;
}
