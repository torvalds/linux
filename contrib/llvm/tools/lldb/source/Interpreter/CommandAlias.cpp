//===-- CommandAlias.cpp -----------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandAlias.h"

#include "llvm/Support/ErrorHandling.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

static bool ProcessAliasOptionsArgs(lldb::CommandObjectSP &cmd_obj_sp,
                                    llvm::StringRef options_args,
                                    OptionArgVectorSP &option_arg_vector_sp) {
  bool success = true;
  OptionArgVector *option_arg_vector = option_arg_vector_sp.get();

  if (options_args.size() < 1)
    return true;

  Args args(options_args);
  std::string options_string(options_args);
  CommandReturnObject result;
  // Check to see if the command being aliased can take any command options.
  Options *options = cmd_obj_sp->GetOptions();
  if (options) {
    // See if any options were specified as part of the alias;  if so, handle
    // them appropriately.
    ExecutionContext exe_ctx =
        cmd_obj_sp->GetCommandInterpreter().GetExecutionContext();
    options->NotifyOptionParsingStarting(&exe_ctx);

    llvm::Expected<Args> args_or =
        options->ParseAlias(args, option_arg_vector, options_string);
    if (!args_or) {
      result.AppendError(toString(args_or.takeError()));
      result.AppendError("Unable to create requested alias.\n");
      result.SetStatus(eReturnStatusFailed);
      return false;
    }
    args = std::move(*args_or);
    options->VerifyPartialOptions(result);
    if (!result.Succeeded() &&
        result.GetStatus() != lldb::eReturnStatusStarted) {
      result.AppendError("Unable to create requested alias.\n");
      return false;
    }
  }

  if (!options_string.empty()) {
    if (cmd_obj_sp->WantsRawCommandString())
      option_arg_vector->emplace_back("<argument>", -1, options_string);
    else {
      for (auto &entry : args.entries()) {
        if (!entry.ref.empty())
          option_arg_vector->emplace_back("<argument>", -1, entry.ref);
      }
    }
  }

  return success;
}

CommandAlias::CommandAlias(CommandInterpreter &interpreter,
                           lldb::CommandObjectSP cmd_sp,
                           llvm::StringRef options_args, llvm::StringRef name,
                           llvm::StringRef help, llvm::StringRef syntax,
                           uint32_t flags)
    : CommandObject(interpreter, name, help, syntax, flags),
      m_underlying_command_sp(), m_option_string(options_args),
      m_option_args_sp(new OptionArgVector),
      m_is_dashdash_alias(eLazyBoolCalculate), m_did_set_help(false),
      m_did_set_help_long(false) {
  if (ProcessAliasOptionsArgs(cmd_sp, options_args, m_option_args_sp)) {
    m_underlying_command_sp = cmd_sp;
    for (int i = 0;
         auto cmd_entry = m_underlying_command_sp->GetArgumentEntryAtIndex(i);
         i++) {
      m_arguments.push_back(*cmd_entry);
    }
    if (!help.empty()) {
      StreamString sstr;
      StreamString translation_and_help;
      GetAliasExpansion(sstr);

      translation_and_help.Printf(
          "(%s)  %s", sstr.GetData(),
          GetUnderlyingCommand()->GetHelp().str().c_str());
      SetHelp(translation_and_help.GetString());
    }
  }
}

bool CommandAlias::WantsRawCommandString() {
  if (IsValid())
    return m_underlying_command_sp->WantsRawCommandString();
  return false;
}

bool CommandAlias::WantsCompletion() {
  if (IsValid())
    return m_underlying_command_sp->WantsCompletion();
  return false;
}

int CommandAlias::HandleCompletion(CompletionRequest &request) {
  if (IsValid())
    return m_underlying_command_sp->HandleCompletion(request);
  return -1;
}

int CommandAlias::HandleArgumentCompletion(
    CompletionRequest &request, OptionElementVector &opt_element_vector) {
  if (IsValid())
    return m_underlying_command_sp->HandleArgumentCompletion(
        request, opt_element_vector);
  return -1;
}

Options *CommandAlias::GetOptions() {
  if (IsValid())
    return m_underlying_command_sp->GetOptions();
  return nullptr;
}

bool CommandAlias::Execute(const char *args_string,
                           CommandReturnObject &result) {
  llvm_unreachable("CommandAlias::Execute is not to be called");
}

void CommandAlias::GetAliasExpansion(StreamString &help_string) const {
  llvm::StringRef command_name = m_underlying_command_sp->GetCommandName();
  help_string.Printf("'%*s", (int)command_name.size(), command_name.data());

  if (!m_option_args_sp) {
    help_string.Printf("'");
    return;
  }

  OptionArgVector *options = m_option_args_sp.get();
  std::string opt;
  std::string value;

  for (const auto &opt_entry : *options) {
    std::tie(opt, std::ignore, value) = opt_entry;
    if (opt == "<argument>") {
      help_string.Printf(" %s", value.c_str());
    } else {
      help_string.Printf(" %s", opt.c_str());
      if ((value != "<no-argument>") && (value != "<need-argument")) {
        help_string.Printf(" %s", value.c_str());
      }
    }
  }

  help_string.Printf("'");
}

bool CommandAlias::IsDashDashCommand() {
  if (m_is_dashdash_alias != eLazyBoolCalculate)
    return (m_is_dashdash_alias == eLazyBoolYes);
  m_is_dashdash_alias = eLazyBoolNo;
  if (!IsValid())
    return false;

  std::string opt;
  std::string value;

  for (const auto &opt_entry : *GetOptionArguments()) {
    std::tie(opt, std::ignore, value) = opt_entry;
    if (opt == "<argument>" && !value.empty() &&
        llvm::StringRef(value).endswith("--")) {
      m_is_dashdash_alias = eLazyBoolYes;
      break;
    }
  }

  // if this is a nested alias, it may be adding arguments on top of an already
  // dash-dash alias
  if ((m_is_dashdash_alias == eLazyBoolNo) && IsNestedAlias())
    m_is_dashdash_alias =
        (GetUnderlyingCommand()->IsDashDashCommand() ? eLazyBoolYes
                                                     : eLazyBoolNo);
  return (m_is_dashdash_alias == eLazyBoolYes);
}

bool CommandAlias::IsNestedAlias() {
  if (GetUnderlyingCommand())
    return GetUnderlyingCommand()->IsAlias();
  return false;
}

std::pair<lldb::CommandObjectSP, OptionArgVectorSP> CommandAlias::Desugar() {
  auto underlying = GetUnderlyingCommand();
  if (!underlying)
    return {nullptr, nullptr};

  if (underlying->IsAlias()) {
    auto desugared = ((CommandAlias *)underlying.get())->Desugar();
    auto options = GetOptionArguments();
    options->insert(options->begin(), desugared.second->begin(),
                    desugared.second->end());
    return {desugared.first, options};
  }

  return {underlying, GetOptionArguments()};
}

// allow CommandAlias objects to provide their own help, but fallback to the
// info for the underlying command if no customization has been provided
void CommandAlias::SetHelp(llvm::StringRef str) {
  this->CommandObject::SetHelp(str);
  m_did_set_help = true;
}

void CommandAlias::SetHelpLong(llvm::StringRef str) {
  this->CommandObject::SetHelpLong(str);
  m_did_set_help_long = true;
}

llvm::StringRef CommandAlias::GetHelp() {
  if (!m_cmd_help_short.empty() || m_did_set_help)
    return m_cmd_help_short;
  if (IsValid())
    return m_underlying_command_sp->GetHelp();
  return llvm::StringRef();
}

llvm::StringRef CommandAlias::GetHelpLong() {
  if (!m_cmd_help_long.empty() || m_did_set_help_long)
    return m_cmd_help_long;
  if (IsValid())
    return m_underlying_command_sp->GetHelpLong();
  return llvm::StringRef();
}
