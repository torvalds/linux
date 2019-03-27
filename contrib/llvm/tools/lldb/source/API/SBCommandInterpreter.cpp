//===-- SBCommandInterpreter.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-types.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Listener.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBExecutionContext.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBTarget.h"

using namespace lldb;
using namespace lldb_private;

SBCommandInterpreterRunOptions::SBCommandInterpreterRunOptions() {
  m_opaque_up.reset(new CommandInterpreterRunOptions());
}

SBCommandInterpreterRunOptions::~SBCommandInterpreterRunOptions() = default;

bool SBCommandInterpreterRunOptions::GetStopOnContinue() const {
  return m_opaque_up->GetStopOnContinue();
}

void SBCommandInterpreterRunOptions::SetStopOnContinue(bool stop_on_continue) {
  m_opaque_up->SetStopOnContinue(stop_on_continue);
}

bool SBCommandInterpreterRunOptions::GetStopOnError() const {
  return m_opaque_up->GetStopOnError();
}

void SBCommandInterpreterRunOptions::SetStopOnError(bool stop_on_error) {
  m_opaque_up->SetStopOnError(stop_on_error);
}

bool SBCommandInterpreterRunOptions::GetStopOnCrash() const {
  return m_opaque_up->GetStopOnCrash();
}

void SBCommandInterpreterRunOptions::SetStopOnCrash(bool stop_on_crash) {
  m_opaque_up->SetStopOnCrash(stop_on_crash);
}

bool SBCommandInterpreterRunOptions::GetEchoCommands() const {
  return m_opaque_up->GetEchoCommands();
}

void SBCommandInterpreterRunOptions::SetEchoCommands(bool echo_commands) {
  m_opaque_up->SetEchoCommands(echo_commands);
}

bool SBCommandInterpreterRunOptions::GetEchoCommentCommands() const {
  return m_opaque_up->GetEchoCommentCommands();
}

void SBCommandInterpreterRunOptions::SetEchoCommentCommands(bool echo) {
  m_opaque_up->SetEchoCommentCommands(echo);
}

bool SBCommandInterpreterRunOptions::GetPrintResults() const {
  return m_opaque_up->GetPrintResults();
}

void SBCommandInterpreterRunOptions::SetPrintResults(bool print_results) {
  m_opaque_up->SetPrintResults(print_results);
}

bool SBCommandInterpreterRunOptions::GetAddToHistory() const {
  return m_opaque_up->GetAddToHistory();
}

void SBCommandInterpreterRunOptions::SetAddToHistory(bool add_to_history) {
  m_opaque_up->SetAddToHistory(add_to_history);
}

lldb_private::CommandInterpreterRunOptions *
SBCommandInterpreterRunOptions::get() const {
  return m_opaque_up.get();
}

lldb_private::CommandInterpreterRunOptions &
SBCommandInterpreterRunOptions::ref() const {
  return *m_opaque_up;
}

class CommandPluginInterfaceImplementation : public CommandObjectParsed {
public:
  CommandPluginInterfaceImplementation(CommandInterpreter &interpreter,
                                       const char *name,
                                       lldb::SBCommandPluginInterface *backend,
                                       const char *help = nullptr,
                                       const char *syntax = nullptr,
                                       uint32_t flags = 0)
      : CommandObjectParsed(interpreter, name, help, syntax, flags),
        m_backend(backend) {}

  bool IsRemovable() const override { return true; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override {
    SBCommandReturnObject sb_return(&result);
    SBCommandInterpreter sb_interpreter(&m_interpreter);
    SBDebugger debugger_sb(m_interpreter.GetDebugger().shared_from_this());
    bool ret = m_backend->DoExecute(
        debugger_sb, (char **)command.GetArgumentVector(), sb_return);
    sb_return.Release();
    return ret;
  }
  std::shared_ptr<lldb::SBCommandPluginInterface> m_backend;
};

SBCommandInterpreter::SBCommandInterpreter(CommandInterpreter *interpreter)
    : m_opaque_ptr(interpreter) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBCommandInterpreter::SBCommandInterpreter (interpreter=%p)"
                " => SBCommandInterpreter(%p)",
                static_cast<void *>(interpreter),
                static_cast<void *>(m_opaque_ptr));
}

SBCommandInterpreter::SBCommandInterpreter(const SBCommandInterpreter &rhs)
    : m_opaque_ptr(rhs.m_opaque_ptr) {}

SBCommandInterpreter::~SBCommandInterpreter() = default;

const SBCommandInterpreter &SBCommandInterpreter::
operator=(const SBCommandInterpreter &rhs) {
  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

bool SBCommandInterpreter::IsValid() const { return m_opaque_ptr != nullptr; }

bool SBCommandInterpreter::CommandExists(const char *cmd) {
  return (((cmd != nullptr) && IsValid()) ? m_opaque_ptr->CommandExists(cmd)
                                          : false);
}

bool SBCommandInterpreter::AliasExists(const char *cmd) {
  return (((cmd != nullptr) && IsValid()) ? m_opaque_ptr->AliasExists(cmd)
                                          : false);
}

bool SBCommandInterpreter::IsActive() {
  return (IsValid() ? m_opaque_ptr->IsActive() : false);
}

bool SBCommandInterpreter::WasInterrupted() const {
  return (IsValid() ? m_opaque_ptr->WasInterrupted() : false);
}

const char *SBCommandInterpreter::GetIOHandlerControlSequence(char ch) {
  return (IsValid()
              ? m_opaque_ptr->GetDebugger()
                    .GetTopIOHandlerControlSequence(ch)
                    .GetCString()
              : nullptr);
}

lldb::ReturnStatus
SBCommandInterpreter::HandleCommand(const char *command_line,
                                    SBCommandReturnObject &result,
                                    bool add_to_history) {
  SBExecutionContext sb_exe_ctx;
  return HandleCommand(command_line, sb_exe_ctx, result, add_to_history);
}

lldb::ReturnStatus SBCommandInterpreter::HandleCommand(
    const char *command_line, SBExecutionContext &override_context,
    SBCommandReturnObject &result, bool add_to_history) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBCommandInterpreter(%p)::HandleCommand (command=\"%s\", "
                "SBCommandReturnObject(%p), add_to_history=%i)",
                static_cast<void *>(m_opaque_ptr), command_line,
                static_cast<void *>(result.get()), add_to_history);

  ExecutionContext ctx, *ctx_ptr;
  if (override_context.get()) {
    ctx = override_context.get()->Lock(true);
    ctx_ptr = &ctx;
  } else
    ctx_ptr = nullptr;

  result.Clear();
  if (command_line && IsValid()) {
    result.ref().SetInteractive(false);
    m_opaque_ptr->HandleCommand(command_line,
                                add_to_history ? eLazyBoolYes : eLazyBoolNo,
                                result.ref(), ctx_ptr);
  } else {
    result->AppendError(
        "SBCommandInterpreter or the command line is not valid");
    result->SetStatus(eReturnStatusFailed);
  }

  // We need to get the value again, in case the command disabled the log!
  log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API);
  if (log) {
    SBStream sstr;
    result.GetDescription(sstr);
    log->Printf("SBCommandInterpreter(%p)::HandleCommand (command=\"%s\", "
                "SBCommandReturnObject(%p): %s, add_to_history=%i) => %i",
                static_cast<void *>(m_opaque_ptr), command_line,
                static_cast<void *>(result.get()), sstr.GetData(),
                add_to_history, result.GetStatus());
  }

  return result.GetStatus();
}

void SBCommandInterpreter::HandleCommandsFromFile(
    lldb::SBFileSpec &file, lldb::SBExecutionContext &override_context,
    lldb::SBCommandInterpreterRunOptions &options,
    lldb::SBCommandReturnObject result) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log) {
    SBStream s;
    file.GetDescription(s);
    log->Printf("SBCommandInterpreter(%p)::HandleCommandsFromFile "
                "(file=\"%s\", SBCommandReturnObject(%p))",
                static_cast<void *>(m_opaque_ptr), s.GetData(),
                static_cast<void *>(result.get()));
  }

  if (!IsValid()) {
    result->AppendError("SBCommandInterpreter is not valid.");
    result->SetStatus(eReturnStatusFailed);
    return;
  }

  if (!file.IsValid()) {
    SBStream s;
    file.GetDescription(s);
    result->AppendErrorWithFormat("File is not valid: %s.", s.GetData());
    result->SetStatus(eReturnStatusFailed);
  }

  FileSpec tmp_spec = file.ref();
  ExecutionContext ctx, *ctx_ptr;
  if (override_context.get()) {
    ctx = override_context.get()->Lock(true);
    ctx_ptr = &ctx;
  } else
    ctx_ptr = nullptr;

  m_opaque_ptr->HandleCommandsFromFile(tmp_spec, ctx_ptr, options.ref(),
                                       result.ref());
}

int SBCommandInterpreter::HandleCompletion(
    const char *current_line, const char *cursor, const char *last_char,
    int match_start_point, int max_return_elements, SBStringList &matches) {
  SBStringList dummy_descriptions;
  return HandleCompletionWithDescriptions(
      current_line, cursor, last_char, match_start_point, max_return_elements,
      matches, dummy_descriptions);
}

int SBCommandInterpreter::HandleCompletionWithDescriptions(
    const char *current_line, const char *cursor, const char *last_char,
    int match_start_point, int max_return_elements, SBStringList &matches,
    SBStringList &descriptions) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  int num_completions = 0;

  // Sanity check the arguments that are passed in: cursor & last_char have to
  // be within the current_line.
  if (current_line == nullptr || cursor == nullptr || last_char == nullptr)
    return 0;

  if (cursor < current_line || last_char < current_line)
    return 0;

  size_t current_line_size = strlen(current_line);
  if (cursor - current_line > static_cast<ptrdiff_t>(current_line_size) ||
      last_char - current_line > static_cast<ptrdiff_t>(current_line_size))
    return 0;

  if (log)
    log->Printf("SBCommandInterpreter(%p)::HandleCompletion "
                "(current_line=\"%s\", cursor at: %" PRId64
                ", last char at: %" PRId64
                ", match_start_point: %d, max_return_elements: %d)",
                static_cast<void *>(m_opaque_ptr), current_line,
                static_cast<uint64_t>(cursor - current_line),
                static_cast<uint64_t>(last_char - current_line),
                match_start_point, max_return_elements);

  if (IsValid()) {
    lldb_private::StringList lldb_matches, lldb_descriptions;
    num_completions = m_opaque_ptr->HandleCompletion(
        current_line, cursor, last_char, match_start_point, max_return_elements,
        lldb_matches, lldb_descriptions);

    SBStringList temp_matches_list(&lldb_matches);
    matches.AppendList(temp_matches_list);
    SBStringList temp_descriptions_list(&lldb_descriptions);
    descriptions.AppendList(temp_descriptions_list);
  }
  if (log)
    log->Printf(
        "SBCommandInterpreter(%p)::HandleCompletion - Found %d completions.",
        static_cast<void *>(m_opaque_ptr), num_completions);

  return num_completions;
}

int SBCommandInterpreter::HandleCompletionWithDescriptions(
    const char *current_line, uint32_t cursor_pos, int match_start_point,
    int max_return_elements, SBStringList &matches,
    SBStringList &descriptions) {
  const char *cursor = current_line + cursor_pos;
  const char *last_char = current_line + strlen(current_line);
  return HandleCompletionWithDescriptions(
      current_line, cursor, last_char, match_start_point, max_return_elements,
      matches, descriptions);
}

int SBCommandInterpreter::HandleCompletion(const char *current_line,
                                           uint32_t cursor_pos,
                                           int match_start_point,
                                           int max_return_elements,
                                           lldb::SBStringList &matches) {
  const char *cursor = current_line + cursor_pos;
  const char *last_char = current_line + strlen(current_line);
  return HandleCompletion(current_line, cursor, last_char, match_start_point,
                          max_return_elements, matches);
}

bool SBCommandInterpreter::HasCommands() {
  return (IsValid() ? m_opaque_ptr->HasCommands() : false);
}

bool SBCommandInterpreter::HasAliases() {
  return (IsValid() ? m_opaque_ptr->HasAliases() : false);
}

bool SBCommandInterpreter::HasAliasOptions() {
  return (IsValid() ? m_opaque_ptr->HasAliasOptions() : false);
}

SBProcess SBCommandInterpreter::GetProcess() {
  SBProcess sb_process;
  ProcessSP process_sp;
  if (IsValid()) {
    TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
    if (target_sp) {
      std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
      process_sp = target_sp->GetProcessSP();
      sb_process.SetSP(process_sp);
    }
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBCommandInterpreter(%p)::GetProcess () => SBProcess(%p)",
                static_cast<void *>(m_opaque_ptr),
                static_cast<void *>(process_sp.get()));

  return sb_process;
}

SBDebugger SBCommandInterpreter::GetDebugger() {
  SBDebugger sb_debugger;
  if (IsValid())
    sb_debugger.reset(m_opaque_ptr->GetDebugger().shared_from_this());
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBCommandInterpreter(%p)::GetDebugger () => SBDebugger(%p)",
                static_cast<void *>(m_opaque_ptr),
                static_cast<void *>(sb_debugger.get()));

  return sb_debugger;
}

bool SBCommandInterpreter::GetPromptOnQuit() {
  return (IsValid() ? m_opaque_ptr->GetPromptOnQuit() : false);
}

void SBCommandInterpreter::SetPromptOnQuit(bool b) {
  if (IsValid())
    m_opaque_ptr->SetPromptOnQuit(b);
}

void SBCommandInterpreter::AllowExitCodeOnQuit(bool allow) {
  if (m_opaque_ptr)
    m_opaque_ptr->AllowExitCodeOnQuit(allow);
}

bool SBCommandInterpreter::HasCustomQuitExitCode() {
  bool exited = false;
  if (m_opaque_ptr)
    m_opaque_ptr->GetQuitExitCode(exited);
  return exited;
}

int SBCommandInterpreter::GetQuitStatus() {
  bool exited = false;
  return (m_opaque_ptr ? m_opaque_ptr->GetQuitExitCode(exited) : 0);
}

void SBCommandInterpreter::ResolveCommand(const char *command_line,
                                          SBCommandReturnObject &result) {
  result.Clear();
  if (command_line && IsValid()) {
    m_opaque_ptr->ResolveCommand(command_line, result.ref());
  } else {
    result->AppendError(
        "SBCommandInterpreter or the command line is not valid");
    result->SetStatus(eReturnStatusFailed);
  }
}

CommandInterpreter *SBCommandInterpreter::get() { return m_opaque_ptr; }

CommandInterpreter &SBCommandInterpreter::ref() {
  assert(m_opaque_ptr);
  return *m_opaque_ptr;
}

void SBCommandInterpreter::reset(
    lldb_private::CommandInterpreter *interpreter) {
  m_opaque_ptr = interpreter;
}

void SBCommandInterpreter::SourceInitFileInHomeDirectory(
    SBCommandReturnObject &result) {
  result.Clear();
  if (IsValid()) {
    TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp)
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
    m_opaque_ptr->SourceInitFile(false, result.ref());
  } else {
    result->AppendError("SBCommandInterpreter is not valid");
    result->SetStatus(eReturnStatusFailed);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBCommandInterpreter(%p)::SourceInitFileInHomeDirectory "
                "(&SBCommandReturnObject(%p))",
                static_cast<void *>(m_opaque_ptr),
                static_cast<void *>(result.get()));
}

void SBCommandInterpreter::SourceInitFileInCurrentWorkingDirectory(
    SBCommandReturnObject &result) {
  result.Clear();
  if (IsValid()) {
    TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp)
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
    m_opaque_ptr->SourceInitFile(true, result.ref());
  } else {
    result->AppendError("SBCommandInterpreter is not valid");
    result->SetStatus(eReturnStatusFailed);
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf(
        "SBCommandInterpreter(%p)::SourceInitFileInCurrentWorkingDirectory "
        "(&SBCommandReturnObject(%p))",
        static_cast<void *>(m_opaque_ptr), static_cast<void *>(result.get()));
}

SBBroadcaster SBCommandInterpreter::GetBroadcaster() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  SBBroadcaster broadcaster(m_opaque_ptr, false);

  if (log)
    log->Printf(
        "SBCommandInterpreter(%p)::GetBroadcaster() => SBBroadcaster(%p)",
        static_cast<void *>(m_opaque_ptr),
        static_cast<void *>(broadcaster.get()));

  return broadcaster;
}

const char *SBCommandInterpreter::GetBroadcasterClass() {
  return CommandInterpreter::GetStaticBroadcasterClass().AsCString();
}

const char *SBCommandInterpreter::GetArgumentTypeAsCString(
    const lldb::CommandArgumentType arg_type) {
  return CommandObject::GetArgumentTypeAsCString(arg_type);
}

const char *SBCommandInterpreter::GetArgumentDescriptionAsCString(
    const lldb::CommandArgumentType arg_type) {
  return CommandObject::GetArgumentDescriptionAsCString(arg_type);
}

bool SBCommandInterpreter::EventIsCommandInterpreterEvent(
    const lldb::SBEvent &event) {
  return event.GetBroadcasterClass() ==
         SBCommandInterpreter::GetBroadcasterClass();
}

bool SBCommandInterpreter::SetCommandOverrideCallback(
    const char *command_name, lldb::CommandOverrideCallback callback,
    void *baton) {
  if (command_name && command_name[0] && IsValid()) {
    llvm::StringRef command_name_str = command_name;
    CommandObject *cmd_obj =
        m_opaque_ptr->GetCommandObjectForCommand(command_name_str);
    if (cmd_obj) {
      assert(command_name_str.empty());
      cmd_obj->SetOverrideCallback(callback, baton);
      return true;
    }
  }
  return false;
}

lldb::SBCommand SBCommandInterpreter::AddMultiwordCommand(const char *name,
                                                          const char *help) {
  CommandObjectMultiword *new_command =
      new CommandObjectMultiword(*m_opaque_ptr, name, help);
  new_command->SetRemovable(true);
  lldb::CommandObjectSP new_command_sp(new_command);
  if (new_command_sp &&
      m_opaque_ptr->AddUserCommand(name, new_command_sp, true))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

lldb::SBCommand SBCommandInterpreter::AddCommand(
    const char *name, lldb::SBCommandPluginInterface *impl, const char *help) {
  lldb::CommandObjectSP new_command_sp;
  new_command_sp.reset(new CommandPluginInterfaceImplementation(
      *m_opaque_ptr, name, impl, help));

  if (new_command_sp &&
      m_opaque_ptr->AddUserCommand(name, new_command_sp, true))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

lldb::SBCommand
SBCommandInterpreter::AddCommand(const char *name,
                                 lldb::SBCommandPluginInterface *impl,
                                 const char *help, const char *syntax) {
  lldb::CommandObjectSP new_command_sp;
  new_command_sp.reset(new CommandPluginInterfaceImplementation(
      *m_opaque_ptr, name, impl, help, syntax));

  if (new_command_sp &&
      m_opaque_ptr->AddUserCommand(name, new_command_sp, true))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

SBCommand::SBCommand() = default;

SBCommand::SBCommand(lldb::CommandObjectSP cmd_sp) : m_opaque_sp(cmd_sp) {}

bool SBCommand::IsValid() { return m_opaque_sp.get() != nullptr; }

const char *SBCommand::GetName() {
  return (IsValid() ? ConstString(m_opaque_sp->GetCommandName()).AsCString() : nullptr);
}

const char *SBCommand::GetHelp() {
  return (IsValid() ? ConstString(m_opaque_sp->GetHelp()).AsCString()
                    : nullptr);
}

const char *SBCommand::GetHelpLong() {
  return (IsValid() ? ConstString(m_opaque_sp->GetHelpLong()).AsCString()
                    : nullptr);
}

void SBCommand::SetHelp(const char *help) {
  if (IsValid())
    m_opaque_sp->SetHelp(help);
}

void SBCommand::SetHelpLong(const char *help) {
  if (IsValid())
    m_opaque_sp->SetHelpLong(help);
}

lldb::SBCommand SBCommand::AddMultiwordCommand(const char *name,
                                               const char *help) {
  if (!IsValid())
    return lldb::SBCommand();
  if (!m_opaque_sp->IsMultiwordObject())
    return lldb::SBCommand();
  CommandObjectMultiword *new_command = new CommandObjectMultiword(
      m_opaque_sp->GetCommandInterpreter(), name, help);
  new_command->SetRemovable(true);
  lldb::CommandObjectSP new_command_sp(new_command);
  if (new_command_sp && m_opaque_sp->LoadSubCommand(name, new_command_sp))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

lldb::SBCommand SBCommand::AddCommand(const char *name,
                                      lldb::SBCommandPluginInterface *impl,
                                      const char *help) {
  if (!IsValid())
    return lldb::SBCommand();
  if (!m_opaque_sp->IsMultiwordObject())
    return lldb::SBCommand();
  lldb::CommandObjectSP new_command_sp;
  new_command_sp.reset(new CommandPluginInterfaceImplementation(
      m_opaque_sp->GetCommandInterpreter(), name, impl, help));
  if (new_command_sp && m_opaque_sp->LoadSubCommand(name, new_command_sp))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

lldb::SBCommand SBCommand::AddCommand(const char *name,
                                      lldb::SBCommandPluginInterface *impl,
                                      const char *help, const char *syntax) {
  if (!IsValid())
    return lldb::SBCommand();
  if (!m_opaque_sp->IsMultiwordObject())
    return lldb::SBCommand();
  lldb::CommandObjectSP new_command_sp;
  new_command_sp.reset(new CommandPluginInterfaceImplementation(
      m_opaque_sp->GetCommandInterpreter(), name, impl, help, syntax));
  if (new_command_sp && m_opaque_sp->LoadSubCommand(name, new_command_sp))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

uint32_t SBCommand::GetFlags() {
  return (IsValid() ? m_opaque_sp->GetFlags().Get() : 0);
}

void SBCommand::SetFlags(uint32_t flags) {
  if (IsValid())
    m_opaque_sp->GetFlags().Set(flags);
}
