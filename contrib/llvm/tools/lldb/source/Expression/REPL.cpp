//===-- REPL.cpp ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/REPL.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/AnsiTerminal.h"

using namespace lldb_private;

REPL::REPL(LLVMCastKind kind, Target &target) : m_target(target), m_kind(kind) {
  // Make sure all option values have sane defaults
  Debugger &debugger = m_target.GetDebugger();
  auto exe_ctx = debugger.GetCommandInterpreter().GetExecutionContext();
  m_format_options.OptionParsingStarting(&exe_ctx);
  m_varobj_options.OptionParsingStarting(&exe_ctx);
  m_command_options.OptionParsingStarting(&exe_ctx);

  // Default certain settings for REPL regardless of the global settings.
  m_command_options.unwind_on_error = false;
  m_command_options.ignore_breakpoints = false;
  m_command_options.debug = false;
}

REPL::~REPL() = default;

lldb::REPLSP REPL::Create(Status &err, lldb::LanguageType language,
                          Debugger *debugger, Target *target,
                          const char *repl_options) {
  uint32_t idx = 0;
  lldb::REPLSP ret;

  while (REPLCreateInstance create_instance =
             PluginManager::GetREPLCreateCallbackAtIndex(idx++)) {
    ret = (*create_instance)(err, language, debugger, target, repl_options);
    if (ret) {
      break;
    }
  }

  return ret;
}

std::string REPL::GetSourcePath() {
  ConstString file_basename = GetSourceFileBasename();
  FileSpec tmpdir_file_spec = HostInfo::GetProcessTempDir();
  if (tmpdir_file_spec) {
    tmpdir_file_spec.GetFilename().SetCString(file_basename.AsCString());
    m_repl_source_path = tmpdir_file_spec.GetPath();
  } else {
    tmpdir_file_spec = FileSpec("/tmp");
    tmpdir_file_spec.AppendPathComponent(file_basename.AsCString());
  }

  return tmpdir_file_spec.GetPath();
}

lldb::IOHandlerSP REPL::GetIOHandler() {
  if (!m_io_handler_sp) {
    Debugger &debugger = m_target.GetDebugger();
    m_io_handler_sp.reset(
        new IOHandlerEditline(debugger, IOHandler::Type::REPL,
                              "lldb-repl", // Name of input reader for history
                              llvm::StringRef("> "), // prompt
                              llvm::StringRef(". "), // Continuation prompt
                              true,                  // Multi-line
                              true, // The REPL prompt is always colored
                              1,    // Line number
                              *this));

    // Don't exit if CTRL+C is pressed
    static_cast<IOHandlerEditline *>(m_io_handler_sp.get())
        ->SetInterruptExits(false);

    if (m_io_handler_sp->GetIsInteractive() &&
        m_io_handler_sp->GetIsRealTerminal()) {
      m_indent_str.assign(debugger.GetTabSize(), ' ');
      m_enable_auto_indent = debugger.GetAutoIndent();
    } else {
      m_indent_str.clear();
      m_enable_auto_indent = false;
    }
  }
  return m_io_handler_sp;
}

void REPL::IOHandlerActivated(IOHandler &io_handler) {
  lldb::ProcessSP process_sp = m_target.GetProcessSP();
  if (process_sp && process_sp->IsAlive())
    return;
  lldb::StreamFileSP error_sp(io_handler.GetErrorStreamFile());
  error_sp->Printf("REPL requires a running target process.\n");
  io_handler.SetIsDone(true);
}

bool REPL::IOHandlerInterrupt(IOHandler &io_handler) { return false; }

void REPL::IOHandlerInputInterrupted(IOHandler &io_handler, std::string &line) {
}

const char *REPL::IOHandlerGetFixIndentationCharacters() {
  return (m_enable_auto_indent ? GetAutoIndentCharacters() : nullptr);
}

ConstString REPL::IOHandlerGetControlSequence(char ch) {
  if (ch == 'd')
    return ConstString(":quit\n");
  return ConstString();
}

const char *REPL::IOHandlerGetCommandPrefix() { return ":"; }

const char *REPL::IOHandlerGetHelpPrologue() {
  return "\nThe REPL (Read-Eval-Print-Loop) acts like an interpreter.  "
         "Valid statements, expressions, and declarations are immediately "
         "compiled and executed.\n\n"
         "The complete set of LLDB debugging commands are also available as "
         "described below.  Commands "
         "must be prefixed with a colon at the REPL prompt (:quit for "
         "example.)  Typing just a colon "
         "followed by return will switch to the LLDB prompt.\n\n";
}

bool REPL::IOHandlerIsInputComplete(IOHandler &io_handler, StringList &lines) {
  // Check for meta command
  const size_t num_lines = lines.GetSize();
  if (num_lines == 1) {
    const char *first_line = lines.GetStringAtIndex(0);
    if (first_line[0] == ':')
      return true; // Meta command is a single line where that starts with ':'
  }

  // Check if REPL input is done
  std::string source_string(lines.CopyList());
  return SourceIsComplete(source_string);
}

int REPL::CalculateActualIndentation(const StringList &lines) {
  std::string last_line = lines[lines.GetSize() - 1];

  int actual_indent = 0;
  for (char &ch : last_line) {
    if (ch != ' ')
      break;
    ++actual_indent;
  }

  return actual_indent;
}

int REPL::IOHandlerFixIndentation(IOHandler &io_handler,
                                  const StringList &lines,
                                  int cursor_position) {
  if (!m_enable_auto_indent)
    return 0;

  if (!lines.GetSize()) {
    return 0;
  }

  int tab_size = io_handler.GetDebugger().GetTabSize();

  lldb::offset_t desired_indent =
      GetDesiredIndentation(lines, cursor_position, tab_size);

  int actual_indent = REPL::CalculateActualIndentation(lines);

  if (desired_indent == LLDB_INVALID_OFFSET)
    return 0;

  return (int)desired_indent - actual_indent;
}

void REPL::IOHandlerInputComplete(IOHandler &io_handler, std::string &code) {
  lldb::StreamFileSP output_sp(io_handler.GetOutputStreamFile());
  lldb::StreamFileSP error_sp(io_handler.GetErrorStreamFile());
  bool extra_line = false;
  bool did_quit = false;

  if (code.empty()) {
    m_code.AppendString("");
    static_cast<IOHandlerEditline &>(io_handler)
        .SetBaseLineNumber(m_code.GetSize() + 1);
  } else {
    Debugger &debugger = m_target.GetDebugger();
    CommandInterpreter &ci = debugger.GetCommandInterpreter();
    extra_line = ci.GetSpaceReplPrompts();

    ExecutionContext exe_ctx(m_target.GetProcessSP()
                                 ->GetThreadList()
                                 .GetSelectedThread()
                                 ->GetSelectedFrame()
                                 .get());

    lldb::ProcessSP process_sp(exe_ctx.GetProcessSP());

    if (code[0] == ':') {
      // Meta command
      // Strip the ':'
      code.erase(0, 1);
      if (Args::StripSpaces(code)) {
        // "lldb" was followed by arguments, so just execute the command dump
        // the results

        // Turn off prompt on quit in case the user types ":quit"
        const bool saved_prompt_on_quit = ci.GetPromptOnQuit();
        if (saved_prompt_on_quit)
          ci.SetPromptOnQuit(false);

        // Execute the command
        CommandReturnObject result;
        result.SetImmediateOutputStream(output_sp);
        result.SetImmediateErrorStream(error_sp);
        ci.HandleCommand(code.c_str(), eLazyBoolNo, result);

        if (saved_prompt_on_quit)
          ci.SetPromptOnQuit(true);

        if (result.GetStatus() == lldb::eReturnStatusQuit) {
          did_quit = true;
          io_handler.SetIsDone(true);
          if (debugger.CheckTopIOHandlerTypes(
                  IOHandler::Type::REPL, IOHandler::Type::CommandInterpreter)) {
            // We typed "quit" or an alias to quit so we need to check if the
            // command interpreter is above us and tell it that it is done as
            // well so we don't drop back into the command interpreter if we
            // have already quit
            lldb::IOHandlerSP io_handler_sp(ci.GetIOHandler());
            if (io_handler_sp)
              io_handler_sp->SetIsDone(true);
          }
        }
      } else {
        // ":" was followed by no arguments, so push the LLDB command prompt
        if (debugger.CheckTopIOHandlerTypes(
                IOHandler::Type::REPL, IOHandler::Type::CommandInterpreter)) {
          // If the user wants to get back to the command interpreter and the
          // command interpreter is what launched the REPL, then just let the
          // REPL exit and fall back to the command interpreter.
          io_handler.SetIsDone(true);
        } else {
          // The REPL wasn't launched the by the command interpreter, it is the
          // base IOHandler, so we need to get the command interpreter and
          lldb::IOHandlerSP io_handler_sp(ci.GetIOHandler());
          if (io_handler_sp) {
            io_handler_sp->SetIsDone(false);
            debugger.PushIOHandler(ci.GetIOHandler());
          }
        }
      }
    } else {
      // Unwind any expression we might have been running in case our REPL
      // expression crashed and the user was looking around
      if (m_dedicated_repl_mode) {
        Thread *thread = exe_ctx.GetThreadPtr();
        if (thread && thread->UnwindInnermostExpression().Success()) {
          thread->SetSelectedFrameByIndex(0, false);
          exe_ctx.SetFrameSP(thread->GetSelectedFrame());
        }
      }

      const bool colorize_err = error_sp->GetFile().GetIsTerminalWithColors();

      EvaluateExpressionOptions expr_options;
      expr_options.SetCoerceToId(m_varobj_options.use_objc);
      expr_options.SetUnwindOnError(m_command_options.unwind_on_error);
      expr_options.SetIgnoreBreakpoints(m_command_options.ignore_breakpoints);
      expr_options.SetKeepInMemory(true);
      expr_options.SetUseDynamic(m_varobj_options.use_dynamic);
      expr_options.SetTryAllThreads(m_command_options.try_all_threads);
      expr_options.SetGenerateDebugInfo(true);
      expr_options.SetREPLEnabled(true);
      expr_options.SetColorizeErrors(colorize_err);
      expr_options.SetPoundLine(m_repl_source_path.c_str(),
                                m_code.GetSize() + 1);
      if (m_command_options.timeout > 0)
        expr_options.SetTimeout(std::chrono::microseconds(m_command_options.timeout));
      else
        expr_options.SetTimeout(llvm::None);

      expr_options.SetLanguage(GetLanguage());

      PersistentExpressionState *persistent_state =
          m_target.GetPersistentExpressionStateForLanguage(GetLanguage());

      const size_t var_count_before = persistent_state->GetSize();

      const char *expr_prefix = nullptr;
      lldb::ValueObjectSP result_valobj_sp;
      Status error;
      lldb::ModuleSP jit_module_sp;
      lldb::ExpressionResults execution_results =
          UserExpression::Evaluate(exe_ctx, expr_options, code.c_str(),
                                   expr_prefix, result_valobj_sp, error,
                                   0,       // Line offset
                                   nullptr, // Fixed Expression
                                   &jit_module_sp);

      // CommandInterpreter &ci = debugger.GetCommandInterpreter();

      if (process_sp && process_sp->IsAlive()) {
        bool add_to_code = true;
        bool handled = false;
        if (result_valobj_sp) {
          lldb::Format format = m_format_options.GetFormat();

          if (result_valobj_sp->GetError().Success()) {
            handled |= PrintOneVariable(debugger, output_sp, result_valobj_sp);
          } else if (result_valobj_sp->GetError().GetError() ==
                     UserExpression::kNoResult) {
            if (format != lldb::eFormatVoid && debugger.GetNotifyVoid()) {
              error_sp->PutCString("(void)\n");
              handled = true;
            }
          }
        }

        if (debugger.GetPrintDecls()) {
          for (size_t vi = var_count_before, ve = persistent_state->GetSize();
               vi != ve; ++vi) {
            lldb::ExpressionVariableSP persistent_var_sp =
                persistent_state->GetVariableAtIndex(vi);
            lldb::ValueObjectSP valobj_sp = persistent_var_sp->GetValueObject();

            PrintOneVariable(debugger, output_sp, valobj_sp,
                             persistent_var_sp.get());
          }
        }

        if (!handled) {
          bool useColors = error_sp->GetFile().GetIsTerminalWithColors();
          switch (execution_results) {
          case lldb::eExpressionSetupError:
          case lldb::eExpressionParseError:
            add_to_code = false;
            LLVM_FALLTHROUGH;
          case lldb::eExpressionDiscarded:
            error_sp->Printf("%s\n", error.AsCString());
            break;

          case lldb::eExpressionCompleted:
            break;
          case lldb::eExpressionInterrupted:
            if (useColors) {
              error_sp->Printf(ANSI_ESCAPE1(ANSI_FG_COLOR_RED));
              error_sp->Printf(ANSI_ESCAPE1(ANSI_CTRL_BOLD));
            }
            error_sp->Printf("Execution interrupted. ");
            if (useColors)
              error_sp->Printf(ANSI_ESCAPE1(ANSI_CTRL_NORMAL));
            error_sp->Printf("Enter code to recover and continue.\nEnter LLDB "
                             "commands to investigate (type :help for "
                             "assistance.)\n");
            break;

          case lldb::eExpressionHitBreakpoint:
            // Breakpoint was hit, drop into LLDB command interpreter
            if (useColors) {
              error_sp->Printf(ANSI_ESCAPE1(ANSI_FG_COLOR_RED));
              error_sp->Printf(ANSI_ESCAPE1(ANSI_CTRL_BOLD));
            }
            output_sp->Printf("Execution stopped at breakpoint.  ");
            if (useColors)
              error_sp->Printf(ANSI_ESCAPE1(ANSI_CTRL_NORMAL));
            output_sp->Printf("Enter LLDB commands to investigate (type help "
                              "for assistance.)\n");
            {
              lldb::IOHandlerSP io_handler_sp(ci.GetIOHandler());
              if (io_handler_sp) {
                io_handler_sp->SetIsDone(false);
                debugger.PushIOHandler(ci.GetIOHandler());
              }
            }
            break;

          case lldb::eExpressionTimedOut:
            error_sp->Printf("error: timeout\n");
            if (error.AsCString())
              error_sp->Printf("error: %s\n", error.AsCString());
            break;
          case lldb::eExpressionResultUnavailable:
            // Shoulnd't happen???
            error_sp->Printf("error: could not fetch result -- %s\n",
                             error.AsCString());
            break;
          case lldb::eExpressionStoppedForDebug:
            // Shoulnd't happen???
            error_sp->Printf("error: stopped for debug -- %s\n",
                             error.AsCString());
            break;
          }
        }

        if (add_to_code) {
          const uint32_t new_default_line = m_code.GetSize() + 1;

          m_code.SplitIntoLines(code);

          // Update our code on disk
          if (!m_repl_source_path.empty()) {
            lldb_private::File file;
            FileSystem::Instance().Open(file, FileSpec(m_repl_source_path),
                                        File::eOpenOptionWrite |
                                            File::eOpenOptionTruncate |
                                            File::eOpenOptionCanCreate,
                                        lldb::eFilePermissionsFileDefault);
            std::string code(m_code.CopyList());
            code.append(1, '\n');
            size_t bytes_written = code.size();
            file.Write(code.c_str(), bytes_written);
            file.Close();

            // Now set the default file and line to the REPL source file
            m_target.GetSourceManager().SetDefaultFileAndLine(
                FileSpec(m_repl_source_path), new_default_line);
          }
          static_cast<IOHandlerEditline &>(io_handler)
              .SetBaseLineNumber(m_code.GetSize() + 1);
        }
        if (extra_line) {
          fprintf(output_sp->GetFile().GetStream(), "\n");
        }
      }
    }

    // Don't complain about the REPL process going away if we are in the
    // process of quitting.
    if (!did_quit && (!process_sp || !process_sp->IsAlive())) {
      error_sp->Printf(
          "error: REPL process is no longer alive, exiting REPL\n");
      io_handler.SetIsDone(true);
    }
  }
}

int REPL::IOHandlerComplete(IOHandler &io_handler, const char *current_line,
                            const char *cursor, const char *last_char,
                            int skip_first_n_matches, int max_matches,
                            StringList &matches, StringList &descriptions) {
  matches.Clear();

  llvm::StringRef line(current_line, cursor - current_line);

  // Complete an LLDB command if the first character is a colon...
  if (!line.empty() && line[0] == ':') {
    Debugger &debugger = m_target.GetDebugger();

    // auto complete LLDB commands
    const char *lldb_current_line = line.substr(1).data();
    return debugger.GetCommandInterpreter().HandleCompletion(
        lldb_current_line, cursor, last_char, skip_first_n_matches, max_matches,
        matches, descriptions);
  }

  // Strip spaces from the line and see if we had only spaces
  line = line.ltrim();
  if (line.empty()) {
    // Only spaces on this line, so just indent
    matches.AppendString(m_indent_str);
    return 1;
  }

  std::string current_code;
  current_code.append(m_code.CopyList());

  IOHandlerEditline &editline = static_cast<IOHandlerEditline &>(io_handler);
  const StringList *current_lines = editline.GetCurrentLines();
  if (current_lines) {
    const uint32_t current_line_idx = editline.GetCurrentLineIndex();

    if (current_line_idx < current_lines->GetSize()) {
      for (uint32_t i = 0; i < current_line_idx; ++i) {
        const char *line_cstr = current_lines->GetStringAtIndex(i);
        if (line_cstr) {
          current_code.append("\n");
          current_code.append(line_cstr);
        }
      }
    }
  }

  if (cursor > current_line) {
    current_code.append("\n");
    current_code.append(current_line, cursor - current_line);
  }

  return CompleteCode(current_code, matches);
}

bool QuitCommandOverrideCallback(void *baton, const char **argv) {
  Target *target = (Target *)baton;
  lldb::ProcessSP process_sp(target->GetProcessSP());
  if (process_sp) {
    process_sp->Destroy(false);
    process_sp->GetTarget().GetDebugger().ClearIOHandlers();
  }
  return false;
}

Status REPL::RunLoop() {
  Status error;

  error = DoInitialization();
  m_repl_source_path = GetSourcePath();

  if (!error.Success())
    return error;

  Debugger &debugger = m_target.GetDebugger();

  lldb::IOHandlerSP io_handler_sp(GetIOHandler());

  FileSpec save_default_file;
  uint32_t save_default_line = 0;

  if (!m_repl_source_path.empty()) {
    // Save the current default file and line
    m_target.GetSourceManager().GetDefaultFileAndLine(save_default_file,
                                                      save_default_line);
  }

  debugger.PushIOHandler(io_handler_sp);

  // Check if we are in dedicated REPL mode where LLDB was start with the "--
  // repl" option from the command line. Currently we know this by checking if
  // the debugger already has a IOHandler thread.
  if (!debugger.HasIOHandlerThread()) {
    // The debugger doesn't have an existing IOHandler thread, so this must be
    // dedicated REPL mode...
    m_dedicated_repl_mode = true;
    debugger.StartIOHandlerThread();
    llvm::StringRef command_name_str("quit");
    CommandObject *cmd_obj =
        debugger.GetCommandInterpreter().GetCommandObjectForCommand(
            command_name_str);
    if (cmd_obj) {
      assert(command_name_str.empty());
      cmd_obj->SetOverrideCallback(QuitCommandOverrideCallback, &m_target);
    }
  }

  // Wait for the REPL command interpreter to get popped
  io_handler_sp->WaitForPop();

  if (m_dedicated_repl_mode) {
    // If we were in dedicated REPL mode we would have started the IOHandler
    // thread, and we should kill our process
    lldb::ProcessSP process_sp = m_target.GetProcessSP();
    if (process_sp && process_sp->IsAlive())
      process_sp->Destroy(false);

    // Wait for the IO handler thread to exit (TODO: don't do this if the IO
    // handler thread already exists...)
    debugger.JoinIOHandlerThread();
  }

  // Restore the default file and line
  if (save_default_file && save_default_line != 0)
    m_target.GetSourceManager().SetDefaultFileAndLine(save_default_file,
                                                      save_default_line);
  return error;
}
