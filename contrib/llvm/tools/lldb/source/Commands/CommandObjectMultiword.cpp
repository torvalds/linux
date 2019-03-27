//===-- CommandObjectMultiword.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/Options.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// CommandObjectMultiword
//-------------------------------------------------------------------------

CommandObjectMultiword::CommandObjectMultiword(CommandInterpreter &interpreter,
                                               const char *name,
                                               const char *help,
                                               const char *syntax,
                                               uint32_t flags)
    : CommandObject(interpreter, name, help, syntax, flags),
      m_can_be_removed(false) {}

CommandObjectMultiword::~CommandObjectMultiword() = default;

CommandObjectSP CommandObjectMultiword::GetSubcommandSP(llvm::StringRef sub_cmd,
                                                        StringList *matches) {
  CommandObjectSP return_cmd_sp;
  CommandObject::CommandMap::iterator pos;

  if (!m_subcommand_dict.empty()) {
    pos = m_subcommand_dict.find(sub_cmd);
    if (pos != m_subcommand_dict.end()) {
      // An exact match; append the sub_cmd to the 'matches' string list.
      if (matches)
        matches->AppendString(sub_cmd);
      return_cmd_sp = pos->second;
    } else {
      StringList local_matches;
      if (matches == nullptr)
        matches = &local_matches;
      int num_matches =
          AddNamesMatchingPartialString(m_subcommand_dict, sub_cmd, *matches);

      if (num_matches == 1) {
        // Cleaner, but slightly less efficient would be to call back into this
        // function, since I now know I have an exact match...

        sub_cmd = matches->GetStringAtIndex(0);
        pos = m_subcommand_dict.find(sub_cmd);
        if (pos != m_subcommand_dict.end())
          return_cmd_sp = pos->second;
      }
    }
  }
  return return_cmd_sp;
}

CommandObject *
CommandObjectMultiword::GetSubcommandObject(llvm::StringRef sub_cmd,
                                            StringList *matches) {
  return GetSubcommandSP(sub_cmd, matches).get();
}

bool CommandObjectMultiword::LoadSubCommand(llvm::StringRef name,
                                            const CommandObjectSP &cmd_obj) {
  if (cmd_obj)
    assert((&GetCommandInterpreter() == &cmd_obj->GetCommandInterpreter()) &&
           "tried to add a CommandObject from a different interpreter");

  CommandMap::iterator pos;
  bool success = true;

  pos = m_subcommand_dict.find(name);
  if (pos == m_subcommand_dict.end()) {
    m_subcommand_dict[name] = cmd_obj;
  } else
    success = false;

  return success;
}

bool CommandObjectMultiword::Execute(const char *args_string,
                                     CommandReturnObject &result) {
  Args args(args_string);
  const size_t argc = args.GetArgumentCount();
  if (argc == 0) {
    this->CommandObject::GenerateHelpText(result);
    return result.Succeeded();
  }

  auto sub_command = args[0].ref;
  if (sub_command.empty())
    return result.Succeeded();

  if (sub_command.equals_lower("help")) {
    this->CommandObject::GenerateHelpText(result);
    return result.Succeeded();
  }

  if (m_subcommand_dict.empty()) {
    result.AppendErrorWithFormat("'%s' does not have any subcommands.\n",
                                 GetCommandName().str().c_str());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }

  StringList matches;
  CommandObject *sub_cmd_obj = GetSubcommandObject(sub_command, &matches);
  if (sub_cmd_obj != nullptr) {
    // Now call CommandObject::Execute to process options in `rest_of_line`.
    // From there the command-specific version of Execute will be called, with
    // the processed arguments.

    args.Shift();
    sub_cmd_obj->Execute(args_string, result);
    return result.Succeeded();
  }

  std::string error_msg;
  const size_t num_subcmd_matches = matches.GetSize();
  if (num_subcmd_matches > 0)
    error_msg.assign("ambiguous command ");
  else
    error_msg.assign("invalid command ");

  error_msg.append("'");
  error_msg.append(GetCommandName());
  error_msg.append(" ");
  error_msg.append(sub_command);
  error_msg.append("'.");

  if (num_subcmd_matches > 0) {
    error_msg.append(" Possible completions:");
    for (size_t i = 0; i < matches.GetSize(); i++) {
      error_msg.append("\n\t");
      error_msg.append(matches.GetStringAtIndex(i));
    }
  }
  error_msg.append("\n");
  result.AppendRawError(error_msg.c_str());
  result.SetStatus(eReturnStatusFailed);
  return false;
}

void CommandObjectMultiword::GenerateHelpText(Stream &output_stream) {
  // First time through here, generate the help text for the object and push it
  // to the return result object as well

  CommandObject::GenerateHelpText(output_stream);
  output_stream.PutCString("\nThe following subcommands are supported:\n\n");

  CommandMap::iterator pos;
  uint32_t max_len = FindLongestCommandWord(m_subcommand_dict);

  if (max_len)
    max_len += 4; // Indent the output by 4 spaces.

  for (pos = m_subcommand_dict.begin(); pos != m_subcommand_dict.end(); ++pos) {
    std::string indented_command("    ");
    indented_command.append(pos->first);
    if (pos->second->WantsRawCommandString()) {
      std::string help_text(pos->second->GetHelp());
      help_text.append("  Expects 'raw' input (see 'help raw-input'.)");
      m_interpreter.OutputFormattedHelpText(output_stream,
                                            indented_command.c_str(), "--",
                                            help_text.c_str(), max_len);
    } else
      m_interpreter.OutputFormattedHelpText(output_stream,
                                            indented_command.c_str(), "--",
                                            pos->second->GetHelp(), max_len);
  }

  output_stream.PutCString("\nFor more help on any particular subcommand, type "
                           "'help <command> <subcommand>'.\n");
}

int CommandObjectMultiword::HandleCompletion(CompletionRequest &request) {
  // Any of the command matches will provide a complete word, otherwise the
  // individual completers will override this.
  request.SetWordComplete(true);

  auto arg0 = request.GetParsedLine()[0].ref;
  if (request.GetCursorIndex() == 0) {
    StringList new_matches, descriptions;
    AddNamesMatchingPartialString(m_subcommand_dict, arg0, new_matches,
                                  &descriptions);
    request.AddCompletions(new_matches, descriptions);

    if (new_matches.GetSize() == 1 &&
        new_matches.GetStringAtIndex(0) != nullptr &&
        (arg0 == new_matches.GetStringAtIndex(0))) {
      StringList temp_matches;
      CommandObject *cmd_obj = GetSubcommandObject(arg0, &temp_matches);
      if (cmd_obj != nullptr) {
        if (request.GetParsedLine().GetArgumentCount() == 1) {
          request.SetWordComplete(true);
        } else {
          request.GetParsedLine().Shift();
          request.SetCursorCharPosition(0);
          request.GetParsedLine().AppendArgument(llvm::StringRef());
          return cmd_obj->HandleCompletion(request);
        }
      }
    }
    return new_matches.GetSize();
  } else {
    StringList new_matches;
    CommandObject *sub_command_object = GetSubcommandObject(arg0, &new_matches);
    if (sub_command_object == nullptr) {
      request.AddCompletions(new_matches);
      return request.GetNumberOfMatches();
    } else {
      // Remove the one match that we got from calling GetSubcommandObject.
      new_matches.DeleteStringAtIndex(0);
      request.AddCompletions(new_matches);
      request.GetParsedLine().Shift();
      request.SetCursorIndex(request.GetCursorIndex() - 1);
      return sub_command_object->HandleCompletion(request);
    }
  }
}

const char *CommandObjectMultiword::GetRepeatCommand(Args &current_command_args,
                                                     uint32_t index) {
  index++;
  if (current_command_args.GetArgumentCount() <= index)
    return nullptr;
  CommandObject *sub_command_object =
      GetSubcommandObject(current_command_args[index].ref);
  if (sub_command_object == nullptr)
    return nullptr;
  return sub_command_object->GetRepeatCommand(current_command_args, index);
}

void CommandObjectMultiword::AproposAllSubCommands(llvm::StringRef prefix,
                                                   llvm::StringRef search_word,
                                                   StringList &commands_found,
                                                   StringList &commands_help) {
  CommandObject::CommandMap::const_iterator pos;

  for (pos = m_subcommand_dict.begin(); pos != m_subcommand_dict.end(); ++pos) {
    const char *command_name = pos->first.c_str();
    CommandObject *sub_cmd_obj = pos->second.get();
    StreamString complete_command_name;

    complete_command_name << prefix << " " << command_name;

    if (sub_cmd_obj->HelpTextContainsWord(search_word)) {
      commands_found.AppendString(complete_command_name.GetString());
      commands_help.AppendString(sub_cmd_obj->GetHelp());
    }

    if (sub_cmd_obj->IsMultiwordObject())
      sub_cmd_obj->AproposAllSubCommands(complete_command_name.GetString(),
                                         search_word, commands_found,
                                         commands_help);
  }
}

CommandObjectProxy::CommandObjectProxy(CommandInterpreter &interpreter,
                                       const char *name, const char *help,
                                       const char *syntax, uint32_t flags)
    : CommandObject(interpreter, name, help, syntax, flags) {}

CommandObjectProxy::~CommandObjectProxy() = default;

llvm::StringRef CommandObjectProxy::GetHelpLong() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetHelpLong();
  return llvm::StringRef();
}

bool CommandObjectProxy::IsRemovable() const {
  const CommandObject *proxy_command =
      const_cast<CommandObjectProxy *>(this)->GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->IsRemovable();
  return false;
}

bool CommandObjectProxy::IsMultiwordObject() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->IsMultiwordObject();
  return false;
}

CommandObjectMultiword *CommandObjectProxy::GetAsMultiwordCommand() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetAsMultiwordCommand();
  return nullptr;
}

void CommandObjectProxy::GenerateHelpText(Stream &result) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GenerateHelpText(result);
}

lldb::CommandObjectSP
CommandObjectProxy::GetSubcommandSP(llvm::StringRef sub_cmd,
                                    StringList *matches) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetSubcommandSP(sub_cmd, matches);
  return lldb::CommandObjectSP();
}

CommandObject *CommandObjectProxy::GetSubcommandObject(llvm::StringRef sub_cmd,
                                                       StringList *matches) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetSubcommandObject(sub_cmd, matches);
  return nullptr;
}

void CommandObjectProxy::AproposAllSubCommands(llvm::StringRef prefix,
                                               llvm::StringRef search_word,
                                               StringList &commands_found,
                                               StringList &commands_help) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->AproposAllSubCommands(prefix, search_word,
                                                commands_found, commands_help);
}

bool CommandObjectProxy::LoadSubCommand(
    llvm::StringRef cmd_name, const lldb::CommandObjectSP &command_sp) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->LoadSubCommand(cmd_name, command_sp);
  return false;
}

bool CommandObjectProxy::WantsRawCommandString() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->WantsRawCommandString();
  return false;
}

bool CommandObjectProxy::WantsCompletion() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->WantsCompletion();
  return false;
}

Options *CommandObjectProxy::GetOptions() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetOptions();
  return nullptr;
}

int CommandObjectProxy::HandleCompletion(CompletionRequest &request) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->HandleCompletion(request);
  return 0;
}

int CommandObjectProxy::HandleArgumentCompletion(
    CompletionRequest &request, OptionElementVector &opt_element_vector) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->HandleArgumentCompletion(request, opt_element_vector);
  return 0;
}

const char *CommandObjectProxy::GetRepeatCommand(Args &current_command_args,
                                                 uint32_t index) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetRepeatCommand(current_command_args, index);
  return nullptr;
}

bool CommandObjectProxy::Execute(const char *args_string,
                                 CommandReturnObject &result) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->Execute(args_string, result);
  result.AppendError("command is not implemented");
  result.SetStatus(eReturnStatusFailed);
  return false;
}
