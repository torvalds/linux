//===-- CommandObject.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandObject.h"

#include <map>
#include <sstream>
#include <string>

#include <ctype.h>
#include <stdlib.h>

#include "lldb/Core/Address.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/ArchSpec.h"

// These are for the Sourcename completers.
// FIXME: Make a separate file for the completers.
#include "lldb/Core/FileSpecList.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"

#include "lldb/Target/Language.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// CommandObject
//-------------------------------------------------------------------------

CommandObject::CommandObject(CommandInterpreter &interpreter, llvm::StringRef name,
  llvm::StringRef help, llvm::StringRef syntax, uint32_t flags)
    : m_interpreter(interpreter), m_cmd_name(name),
      m_cmd_help_short(), m_cmd_help_long(), m_cmd_syntax(), m_flags(flags),
      m_arguments(), m_deprecated_command_override_callback(nullptr),
      m_command_override_callback(nullptr), m_command_override_baton(nullptr) {
  m_cmd_help_short = help;
  m_cmd_syntax = syntax;
}

CommandObject::~CommandObject() {}

llvm::StringRef CommandObject::GetHelp() { return m_cmd_help_short; }

llvm::StringRef CommandObject::GetHelpLong() { return m_cmd_help_long; }

llvm::StringRef CommandObject::GetSyntax() {
  if (!m_cmd_syntax.empty())
    return m_cmd_syntax;

  StreamString syntax_str;
  syntax_str.PutCString(GetCommandName());

  if (!IsDashDashCommand() && GetOptions() != nullptr)
    syntax_str.PutCString(" <cmd-options>");

  if (!m_arguments.empty()) {
    syntax_str.PutCString(" ");

    if (!IsDashDashCommand() && WantsRawCommandString() && GetOptions() &&
        GetOptions()->NumCommandOptions())
      syntax_str.PutCString("-- ");
    GetFormattedCommandArguments(syntax_str);
  }
  m_cmd_syntax = syntax_str.GetString();

  return m_cmd_syntax;
}

llvm::StringRef CommandObject::GetCommandName() const { return m_cmd_name; }

void CommandObject::SetCommandName(llvm::StringRef name) { m_cmd_name = name; }

void CommandObject::SetHelp(llvm::StringRef str) { m_cmd_help_short = str; }

void CommandObject::SetHelpLong(llvm::StringRef str) { m_cmd_help_long = str; }

void CommandObject::SetSyntax(llvm::StringRef str) { m_cmd_syntax = str; }

Options *CommandObject::GetOptions() {
  // By default commands don't have options unless this virtual function is
  // overridden by base classes.
  return nullptr;
}

bool CommandObject::ParseOptions(Args &args, CommandReturnObject &result) {
  // See if the subclass has options?
  Options *options = GetOptions();
  if (options != nullptr) {
    Status error;

    auto exe_ctx = GetCommandInterpreter().GetExecutionContext();
    options->NotifyOptionParsingStarting(&exe_ctx);

    const bool require_validation = true;
    llvm::Expected<Args> args_or = options->Parse(
        args, &exe_ctx, GetCommandInterpreter().GetPlatform(true),
        require_validation);

    if (args_or) {
      args = std::move(*args_or);
      error = options->NotifyOptionParsingFinished(&exe_ctx);
    } else
      error = args_or.takeError();

    if (error.Success()) {
      if (options->VerifyOptions(result))
        return true;
    } else {
      const char *error_cstr = error.AsCString();
      if (error_cstr) {
        // We got an error string, lets use that
        result.AppendError(error_cstr);
      } else {
        // No error string, output the usage information into result
        options->GenerateOptionUsage(
            result.GetErrorStream(), this,
            GetCommandInterpreter().GetDebugger().GetTerminalWidth());
      }
    }
    result.SetStatus(eReturnStatusFailed);
    return false;
  }
  return true;
}

bool CommandObject::CheckRequirements(CommandReturnObject &result) {
#ifdef LLDB_CONFIGURATION_DEBUG
  // Nothing should be stored in m_exe_ctx between running commands as
  // m_exe_ctx has shared pointers to the target, process, thread and frame and
  // we don't want any CommandObject instances to keep any of these objects
  // around longer than for a single command. Every command should call
  // CommandObject::Cleanup() after it has completed
  assert(m_exe_ctx.GetTargetPtr() == NULL);
  assert(m_exe_ctx.GetProcessPtr() == NULL);
  assert(m_exe_ctx.GetThreadPtr() == NULL);
  assert(m_exe_ctx.GetFramePtr() == NULL);
#endif

  // Lock down the interpreter's execution context prior to running the command
  // so we guarantee the selected target, process, thread and frame can't go
  // away during the execution
  m_exe_ctx = m_interpreter.GetExecutionContext();

  const uint32_t flags = GetFlags().Get();
  if (flags & (eCommandRequiresTarget | eCommandRequiresProcess |
               eCommandRequiresThread | eCommandRequiresFrame |
               eCommandTryTargetAPILock)) {

    if ((flags & eCommandRequiresTarget) && !m_exe_ctx.HasTargetScope()) {
      result.AppendError(GetInvalidTargetDescription());
      return false;
    }

    if ((flags & eCommandRequiresProcess) && !m_exe_ctx.HasProcessScope()) {
      if (!m_exe_ctx.HasTargetScope())
        result.AppendError(GetInvalidTargetDescription());
      else
        result.AppendError(GetInvalidProcessDescription());
      return false;
    }

    if ((flags & eCommandRequiresThread) && !m_exe_ctx.HasThreadScope()) {
      if (!m_exe_ctx.HasTargetScope())
        result.AppendError(GetInvalidTargetDescription());
      else if (!m_exe_ctx.HasProcessScope())
        result.AppendError(GetInvalidProcessDescription());
      else
        result.AppendError(GetInvalidThreadDescription());
      return false;
    }

    if ((flags & eCommandRequiresFrame) && !m_exe_ctx.HasFrameScope()) {
      if (!m_exe_ctx.HasTargetScope())
        result.AppendError(GetInvalidTargetDescription());
      else if (!m_exe_ctx.HasProcessScope())
        result.AppendError(GetInvalidProcessDescription());
      else if (!m_exe_ctx.HasThreadScope())
        result.AppendError(GetInvalidThreadDescription());
      else
        result.AppendError(GetInvalidFrameDescription());
      return false;
    }

    if ((flags & eCommandRequiresRegContext) &&
        (m_exe_ctx.GetRegisterContext() == nullptr)) {
      result.AppendError(GetInvalidRegContextDescription());
      return false;
    }

    if (flags & eCommandTryTargetAPILock) {
      Target *target = m_exe_ctx.GetTargetPtr();
      if (target)
        m_api_locker =
            std::unique_lock<std::recursive_mutex>(target->GetAPIMutex());
    }
  }

  if (GetFlags().AnySet(eCommandProcessMustBeLaunched |
                        eCommandProcessMustBePaused)) {
    Process *process = m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process == nullptr) {
      // A process that is not running is considered paused.
      if (GetFlags().Test(eCommandProcessMustBeLaunched)) {
        result.AppendError("Process must exist.");
        result.SetStatus(eReturnStatusFailed);
        return false;
      }
    } else {
      StateType state = process->GetState();
      switch (state) {
      case eStateInvalid:
      case eStateSuspended:
      case eStateCrashed:
      case eStateStopped:
        break;

      case eStateConnected:
      case eStateAttaching:
      case eStateLaunching:
      case eStateDetached:
      case eStateExited:
      case eStateUnloaded:
        if (GetFlags().Test(eCommandProcessMustBeLaunched)) {
          result.AppendError("Process must be launched.");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
        break;

      case eStateRunning:
      case eStateStepping:
        if (GetFlags().Test(eCommandProcessMustBePaused)) {
          result.AppendError("Process is running.  Use 'process interrupt' to "
                             "pause execution.");
          result.SetStatus(eReturnStatusFailed);
          return false;
        }
      }
    }
  }
  return true;
}

void CommandObject::Cleanup() {
  m_exe_ctx.Clear();
  if (m_api_locker.owns_lock())
    m_api_locker.unlock();
}

int CommandObject::HandleCompletion(CompletionRequest &request) {
  // Default implementation of WantsCompletion() is !WantsRawCommandString().
  // Subclasses who want raw command string but desire, for example, argument
  // completion should override WantsCompletion() to return true, instead.
  if (WantsRawCommandString() && !WantsCompletion()) {
    // FIXME: Abstract telling the completion to insert the completion
    // character.
    return -1;
  } else {
    // Can we do anything generic with the options?
    Options *cur_options = GetOptions();
    CommandReturnObject result;
    OptionElementVector opt_element_vector;

    if (cur_options != nullptr) {
      opt_element_vector = cur_options->ParseForCompletion(
          request.GetParsedLine(), request.GetCursorIndex());

      bool handled_by_options = cur_options->HandleOptionCompletion(
          request, opt_element_vector, GetCommandInterpreter());
      if (handled_by_options)
        return request.GetNumberOfMatches();
    }

    // If we got here, the last word is not an option or an option argument.
    return HandleArgumentCompletion(request, opt_element_vector);
  }
}

bool CommandObject::HelpTextContainsWord(llvm::StringRef search_word,
                                         bool search_short_help,
                                         bool search_long_help,
                                         bool search_syntax,
                                         bool search_options) {
  std::string options_usage_help;

  bool found_word = false;

  llvm::StringRef short_help = GetHelp();
  llvm::StringRef long_help = GetHelpLong();
  llvm::StringRef syntax_help = GetSyntax();

  if (search_short_help && short_help.contains_lower(search_word))
    found_word = true;
  else if (search_long_help && long_help.contains_lower(search_word))
    found_word = true;
  else if (search_syntax && syntax_help.contains_lower(search_word))
    found_word = true;

  if (!found_word && search_options && GetOptions() != nullptr) {
    StreamString usage_help;
    GetOptions()->GenerateOptionUsage(
        usage_help, this,
        GetCommandInterpreter().GetDebugger().GetTerminalWidth());
    if (!usage_help.Empty()) {
      llvm::StringRef usage_text = usage_help.GetString();
      if (usage_text.contains_lower(search_word))
        found_word = true;
    }
  }

  return found_word;
}

bool CommandObject::ParseOptionsAndNotify(Args &args,
                                          CommandReturnObject &result,
                                          OptionGroupOptions &group_options,
                                          ExecutionContext &exe_ctx) {
  if (!ParseOptions(args, result))
    return false;

  Status error(group_options.NotifyOptionParsingFinished(&exe_ctx));
  if (error.Fail()) {
    result.AppendError(error.AsCString());
    result.SetStatus(eReturnStatusFailed);
    return false;
  }
  return true;
}

int CommandObject::GetNumArgumentEntries() { return m_arguments.size(); }

CommandObject::CommandArgumentEntry *
CommandObject::GetArgumentEntryAtIndex(int idx) {
  if (static_cast<size_t>(idx) < m_arguments.size())
    return &(m_arguments[idx]);

  return nullptr;
}

const CommandObject::ArgumentTableEntry *
CommandObject::FindArgumentDataByType(CommandArgumentType arg_type) {
  const ArgumentTableEntry *table = CommandObject::GetArgumentTable();

  for (int i = 0; i < eArgTypeLastArg; ++i)
    if (table[i].arg_type == arg_type)
      return &(table[i]);

  return nullptr;
}

void CommandObject::GetArgumentHelp(Stream &str, CommandArgumentType arg_type,
                                    CommandInterpreter &interpreter) {
  const ArgumentTableEntry *table = CommandObject::GetArgumentTable();
  const ArgumentTableEntry *entry = &(table[arg_type]);

  // The table is *supposed* to be kept in arg_type order, but someone *could*
  // have messed it up...

  if (entry->arg_type != arg_type)
    entry = CommandObject::FindArgumentDataByType(arg_type);

  if (!entry)
    return;

  StreamString name_str;
  name_str.Printf("<%s>", entry->arg_name);

  if (entry->help_function) {
    llvm::StringRef help_text = entry->help_function();
    if (!entry->help_function.self_formatting) {
      interpreter.OutputFormattedHelpText(str, name_str.GetString(), "--",
                                          help_text, name_str.GetSize());
    } else {
      interpreter.OutputHelpText(str, name_str.GetString(), "--", help_text,
                                 name_str.GetSize());
    }
  } else
    interpreter.OutputFormattedHelpText(str, name_str.GetString(), "--",
                                        entry->help_text, name_str.GetSize());
}

const char *CommandObject::GetArgumentName(CommandArgumentType arg_type) {
  const ArgumentTableEntry *entry =
      &(CommandObject::GetArgumentTable()[arg_type]);

  // The table is *supposed* to be kept in arg_type order, but someone *could*
  // have messed it up...

  if (entry->arg_type != arg_type)
    entry = CommandObject::FindArgumentDataByType(arg_type);

  if (entry)
    return entry->arg_name;

  return nullptr;
}

bool CommandObject::IsPairType(ArgumentRepetitionType arg_repeat_type) {
  return (arg_repeat_type == eArgRepeatPairPlain) ||
         (arg_repeat_type == eArgRepeatPairOptional) ||
         (arg_repeat_type == eArgRepeatPairPlus) ||
         (arg_repeat_type == eArgRepeatPairStar) ||
         (arg_repeat_type == eArgRepeatPairRange) ||
         (arg_repeat_type == eArgRepeatPairRangeOptional);
}

static CommandObject::CommandArgumentEntry
OptSetFiltered(uint32_t opt_set_mask,
               CommandObject::CommandArgumentEntry &cmd_arg_entry) {
  CommandObject::CommandArgumentEntry ret_val;
  for (unsigned i = 0; i < cmd_arg_entry.size(); ++i)
    if (opt_set_mask & cmd_arg_entry[i].arg_opt_set_association)
      ret_val.push_back(cmd_arg_entry[i]);
  return ret_val;
}

// Default parameter value of opt_set_mask is LLDB_OPT_SET_ALL, which means
// take all the argument data into account.  On rare cases where some argument
// sticks with certain option sets, this function returns the option set
// filtered args.
void CommandObject::GetFormattedCommandArguments(Stream &str,
                                                 uint32_t opt_set_mask) {
  int num_args = m_arguments.size();
  for (int i = 0; i < num_args; ++i) {
    if (i > 0)
      str.Printf(" ");
    CommandArgumentEntry arg_entry =
        opt_set_mask == LLDB_OPT_SET_ALL
            ? m_arguments[i]
            : OptSetFiltered(opt_set_mask, m_arguments[i]);
    int num_alternatives = arg_entry.size();

    if ((num_alternatives == 2) && IsPairType(arg_entry[0].arg_repetition)) {
      const char *first_name = GetArgumentName(arg_entry[0].arg_type);
      const char *second_name = GetArgumentName(arg_entry[1].arg_type);
      switch (arg_entry[0].arg_repetition) {
      case eArgRepeatPairPlain:
        str.Printf("<%s> <%s>", first_name, second_name);
        break;
      case eArgRepeatPairOptional:
        str.Printf("[<%s> <%s>]", first_name, second_name);
        break;
      case eArgRepeatPairPlus:
        str.Printf("<%s> <%s> [<%s> <%s> [...]]", first_name, second_name,
                   first_name, second_name);
        break;
      case eArgRepeatPairStar:
        str.Printf("[<%s> <%s> [<%s> <%s> [...]]]", first_name, second_name,
                   first_name, second_name);
        break;
      case eArgRepeatPairRange:
        str.Printf("<%s_1> <%s_1> ... <%s_n> <%s_n>", first_name, second_name,
                   first_name, second_name);
        break;
      case eArgRepeatPairRangeOptional:
        str.Printf("[<%s_1> <%s_1> ... <%s_n> <%s_n>]", first_name, second_name,
                   first_name, second_name);
        break;
      // Explicitly test for all the rest of the cases, so if new types get
      // added we will notice the missing case statement(s).
      case eArgRepeatPlain:
      case eArgRepeatOptional:
      case eArgRepeatPlus:
      case eArgRepeatStar:
      case eArgRepeatRange:
        // These should not be reached, as they should fail the IsPairType test
        // above.
        break;
      }
    } else {
      StreamString names;
      for (int j = 0; j < num_alternatives; ++j) {
        if (j > 0)
          names.Printf(" | ");
        names.Printf("%s", GetArgumentName(arg_entry[j].arg_type));
      }

      std::string name_str = names.GetString();
      switch (arg_entry[0].arg_repetition) {
      case eArgRepeatPlain:
        str.Printf("<%s>", name_str.c_str());
        break;
      case eArgRepeatPlus:
        str.Printf("<%s> [<%s> [...]]", name_str.c_str(), name_str.c_str());
        break;
      case eArgRepeatStar:
        str.Printf("[<%s> [<%s> [...]]]", name_str.c_str(), name_str.c_str());
        break;
      case eArgRepeatOptional:
        str.Printf("[<%s>]", name_str.c_str());
        break;
      case eArgRepeatRange:
        str.Printf("<%s_1> .. <%s_n>", name_str.c_str(), name_str.c_str());
        break;
      // Explicitly test for all the rest of the cases, so if new types get
      // added we will notice the missing case statement(s).
      case eArgRepeatPairPlain:
      case eArgRepeatPairOptional:
      case eArgRepeatPairPlus:
      case eArgRepeatPairStar:
      case eArgRepeatPairRange:
      case eArgRepeatPairRangeOptional:
        // These should not be hit, as they should pass the IsPairType test
        // above, and control should have gone into the other branch of the if
        // statement.
        break;
      }
    }
  }
}

CommandArgumentType
CommandObject::LookupArgumentName(llvm::StringRef arg_name) {
  CommandArgumentType return_type = eArgTypeLastArg;

  arg_name = arg_name.ltrim('<').rtrim('>');

  const ArgumentTableEntry *table = GetArgumentTable();
  for (int i = 0; i < eArgTypeLastArg; ++i)
    if (arg_name == table[i].arg_name)
      return_type = g_arguments_data[i].arg_type;

  return return_type;
}

static llvm::StringRef RegisterNameHelpTextCallback() {
  return "Register names can be specified using the architecture specific "
         "names.  "
         "They can also be specified using generic names.  Not all generic "
         "entities have "
         "registers backing them on all architectures.  When they don't the "
         "generic name "
         "will return an error.\n"
         "The generic names defined in lldb are:\n"
         "\n"
         "pc       - program counter register\n"
         "ra       - return address register\n"
         "fp       - frame pointer register\n"
         "sp       - stack pointer register\n"
         "flags    - the flags register\n"
         "arg{1-6} - integer argument passing registers.\n";
}

static llvm::StringRef BreakpointIDHelpTextCallback() {
  return "Breakpoints are identified using major and minor numbers; the major "
         "number corresponds to the single entity that was created with a "
         "'breakpoint "
         "set' command; the minor numbers correspond to all the locations that "
         "were "
         "actually found/set based on the major breakpoint.  A full breakpoint "
         "ID might "
         "look like 3.14, meaning the 14th location set for the 3rd "
         "breakpoint.  You "
         "can specify all the locations of a breakpoint by just indicating the "
         "major "
         "breakpoint number. A valid breakpoint ID consists either of just the "
         "major "
         "number, or the major number followed by a dot and the location "
         "number (e.g. "
         "3 or 3.2 could both be valid breakpoint IDs.)";
}

static llvm::StringRef BreakpointIDRangeHelpTextCallback() {
  return "A 'breakpoint ID list' is a manner of specifying multiple "
         "breakpoints. "
         "This can be done through several mechanisms.  The easiest way is to "
         "just "
         "enter a space-separated list of breakpoint IDs.  To specify all the "
         "breakpoint locations under a major breakpoint, you can use the major "
         "breakpoint number followed by '.*', eg. '5.*' means all the "
         "locations under "
         "breakpoint 5.  You can also indicate a range of breakpoints by using "
         "<start-bp-id> - <end-bp-id>.  The start-bp-id and end-bp-id for a "
         "range can "
         "be any valid breakpoint IDs.  It is not legal, however, to specify a "
         "range "
         "using specific locations that cross major breakpoint numbers.  I.e. "
         "3.2 - 3.7"
         " is legal; 2 - 5 is legal; but 3.2 - 4.4 is not legal.";
}

static llvm::StringRef BreakpointNameHelpTextCallback() {
  return "A name that can be added to a breakpoint when it is created, or "
         "later "
         "on with the \"breakpoint name add\" command.  "
         "Breakpoint names can be used to specify breakpoints in all the "
         "places breakpoint IDs "
         "and breakpoint ID ranges can be used.  As such they provide a "
         "convenient way to group breakpoints, "
         "and to operate on breakpoints you create without having to track the "
         "breakpoint number.  "
         "Note, the attributes you set when using a breakpoint name in a "
         "breakpoint command don't "
         "adhere to the name, but instead are set individually on all the "
         "breakpoints currently tagged with that "
         "name.  Future breakpoints "
         "tagged with that name will not pick up the attributes previously "
         "given using that name.  "
         "In order to distinguish breakpoint names from breakpoint IDs and "
         "ranges, "
         "names must start with a letter from a-z or A-Z and cannot contain "
         "spaces, \".\" or \"-\".  "
         "Also, breakpoint names can only be applied to breakpoints, not to "
         "breakpoint locations.";
}

static llvm::StringRef GDBFormatHelpTextCallback() {
  return "A GDB format consists of a repeat count, a format letter and a size "
         "letter. "
         "The repeat count is optional and defaults to 1. The format letter is "
         "optional "
         "and defaults to the previous format that was used. The size letter "
         "is optional "
         "and defaults to the previous size that was used.\n"
         "\n"
         "Format letters include:\n"
         "o - octal\n"
         "x - hexadecimal\n"
         "d - decimal\n"
         "u - unsigned decimal\n"
         "t - binary\n"
         "f - float\n"
         "a - address\n"
         "i - instruction\n"
         "c - char\n"
         "s - string\n"
         "T - OSType\n"
         "A - float as hex\n"
         "\n"
         "Size letters include:\n"
         "b - 1 byte  (byte)\n"
         "h - 2 bytes (halfword)\n"
         "w - 4 bytes (word)\n"
         "g - 8 bytes (giant)\n"
         "\n"
         "Example formats:\n"
         "32xb - show 32 1 byte hexadecimal integer values\n"
         "16xh - show 16 2 byte hexadecimal integer values\n"
         "64   - show 64 2 byte hexadecimal integer values (format and size "
         "from the last format)\n"
         "dw   - show 1 4 byte decimal integer value\n";
}

static llvm::StringRef FormatHelpTextCallback() {
  static std::string help_text;

  if (!help_text.empty())
    return help_text;

  StreamString sstr;
  sstr << "One of the format names (or one-character names) that can be used "
          "to show a variable's value:\n";
  for (Format f = eFormatDefault; f < kNumFormats; f = Format(f + 1)) {
    if (f != eFormatDefault)
      sstr.PutChar('\n');

    char format_char = FormatManager::GetFormatAsFormatChar(f);
    if (format_char)
      sstr.Printf("'%c' or ", format_char);

    sstr.Printf("\"%s\"", FormatManager::GetFormatAsCString(f));
  }

  sstr.Flush();

  help_text = sstr.GetString();

  return help_text;
}

static llvm::StringRef LanguageTypeHelpTextCallback() {
  static std::string help_text;

  if (!help_text.empty())
    return help_text;

  StreamString sstr;
  sstr << "One of the following languages:\n";

  Language::PrintAllLanguages(sstr, "  ", "\n");

  sstr.Flush();

  help_text = sstr.GetString();

  return help_text;
}

static llvm::StringRef SummaryStringHelpTextCallback() {
  return "A summary string is a way to extract information from variables in "
         "order to present them using a summary.\n"
         "Summary strings contain static text, variables, scopes and control "
         "sequences:\n"
         "  - Static text can be any sequence of non-special characters, i.e. "
         "anything but '{', '}', '$', or '\\'.\n"
         "  - Variables are sequences of characters beginning with ${, ending "
         "with } and that contain symbols in the format described below.\n"
         "  - Scopes are any sequence of text between { and }. Anything "
         "included in a scope will only appear in the output summary if there "
         "were no errors.\n"
         "  - Control sequences are the usual C/C++ '\\a', '\\n', ..., plus "
         "'\\$', '\\{' and '\\}'.\n"
         "A summary string works by copying static text verbatim, turning "
         "control sequences into their character counterpart, expanding "
         "variables and trying to expand scopes.\n"
         "A variable is expanded by giving it a value other than its textual "
         "representation, and the way this is done depends on what comes after "
         "the ${ marker.\n"
         "The most common sequence if ${var followed by an expression path, "
         "which is the text one would type to access a member of an aggregate "
         "types, given a variable of that type"
         " (e.g. if type T has a member named x, which has a member named y, "
         "and if t is of type T, the expression path would be .x.y and the way "
         "to fit that into a summary string would be"
         " ${var.x.y}). You can also use ${*var followed by an expression path "
         "and in that case the object referred by the path will be "
         "dereferenced before being displayed."
         " If the object is not a pointer, doing so will cause an error. For "
         "additional details on expression paths, you can type 'help "
         "expr-path'. \n"
         "By default, summary strings attempt to display the summary for any "
         "variable they reference, and if that fails the value. If neither can "
         "be shown, nothing is displayed."
         "In a summary string, you can also use an array index [n], or a "
         "slice-like range [n-m]. This can have two different meanings "
         "depending on what kind of object the expression"
         " path refers to:\n"
         "  - if it is a scalar type (any basic type like int, float, ...) the "
         "expression is a bitfield, i.e. the bits indicated by the indexing "
         "operator are extracted out of the number"
         " and displayed as an individual variable\n"
         "  - if it is an array or pointer the array items indicated by the "
         "indexing operator are shown as the result of the variable. if the "
         "expression is an array, real array items are"
         " printed; if it is a pointer, the pointer-as-array syntax is used to "
         "obtain the values (this means, the latter case can have no range "
         "checking)\n"
         "If you are trying to display an array for which the size is known, "
         "you can also use [] instead of giving an exact range. This has the "
         "effect of showing items 0 thru size - 1.\n"
         "Additionally, a variable can contain an (optional) format code, as "
         "in ${var.x.y%code}, where code can be any of the valid formats "
         "described in 'help format', or one of the"
         " special symbols only allowed as part of a variable:\n"
         "    %V: show the value of the object by default\n"
         "    %S: show the summary of the object by default\n"
         "    %@: show the runtime-provided object description (for "
         "Objective-C, it calls NSPrintForDebugger; for C/C++ it does "
         "nothing)\n"
         "    %L: show the location of the object (memory address or a "
         "register name)\n"
         "    %#: show the number of children of the object\n"
         "    %T: show the type of the object\n"
         "Another variable that you can use in summary strings is ${svar . "
         "This sequence works exactly like ${var, including the fact that "
         "${*svar is an allowed sequence, but uses"
         " the object's synthetic children provider instead of the actual "
         "objects. For instance, if you are using STL synthetic children "
         "providers, the following summary string would"
         " count the number of actual elements stored in an std::list:\n"
         "type summary add -s \"${svar%#}\" -x \"std::list<\"";
}

static llvm::StringRef ExprPathHelpTextCallback() {
  return "An expression path is the sequence of symbols that is used in C/C++ "
         "to access a member variable of an aggregate object (class).\n"
         "For instance, given a class:\n"
         "  class foo {\n"
         "      int a;\n"
         "      int b; .\n"
         "      foo* next;\n"
         "  };\n"
         "the expression to read item b in the item pointed to by next for foo "
         "aFoo would be aFoo.next->b.\n"
         "Given that aFoo could just be any object of type foo, the string "
         "'.next->b' is the expression path, because it can be attached to any "
         "foo instance to achieve the effect.\n"
         "Expression paths in LLDB include dot (.) and arrow (->) operators, "
         "and most commands using expression paths have ways to also accept "
         "the star (*) operator.\n"
         "The meaning of these operators is the same as the usual one given to "
         "them by the C/C++ standards.\n"
         "LLDB also has support for indexing ([ ]) in expression paths, and "
         "extends the traditional meaning of the square brackets operator to "
         "allow bitfield extraction:\n"
         "for objects of native types (int, float, char, ...) saying '[n-m]' "
         "as an expression path (where n and m are any positive integers, e.g. "
         "[3-5]) causes LLDB to extract"
         " bits n thru m from the value of the variable. If n == m, [n] is "
         "also allowed as a shortcut syntax. For arrays and pointers, "
         "expression paths can only contain one index"
         " and the meaning of the operation is the same as the one defined by "
         "C/C++ (item extraction). Some commands extend bitfield-like syntax "
         "for arrays and pointers with the"
         " meaning of array slicing (taking elements n thru m inside the array "
         "or pointed-to memory).";
}

void CommandObject::FormatLongHelpText(Stream &output_strm,
                                       llvm::StringRef long_help) {
  CommandInterpreter &interpreter = GetCommandInterpreter();
  std::stringstream lineStream(long_help);
  std::string line;
  while (std::getline(lineStream, line)) {
    if (line.empty()) {
      output_strm << "\n";
      continue;
    }
    size_t result = line.find_first_not_of(" \t");
    if (result == std::string::npos) {
      result = 0;
    }
    std::string whitespace_prefix = line.substr(0, result);
    std::string remainder = line.substr(result);
    interpreter.OutputFormattedHelpText(output_strm, whitespace_prefix.c_str(),
                                        remainder.c_str());
  }
}

void CommandObject::GenerateHelpText(CommandReturnObject &result) {
  GenerateHelpText(result.GetOutputStream());

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
}

void CommandObject::GenerateHelpText(Stream &output_strm) {
  CommandInterpreter &interpreter = GetCommandInterpreter();
  if (WantsRawCommandString()) {
    std::string help_text(GetHelp());
    help_text.append("  Expects 'raw' input (see 'help raw-input'.)");
    interpreter.OutputFormattedHelpText(output_strm, "", "", help_text.c_str(),
                                        1);
  } else
    interpreter.OutputFormattedHelpText(output_strm, "", "", GetHelp(), 1);
  output_strm << "\nSyntax: " << GetSyntax() << "\n";
  Options *options = GetOptions();
  if (options != nullptr) {
    options->GenerateOptionUsage(
        output_strm, this,
        GetCommandInterpreter().GetDebugger().GetTerminalWidth());
  }
  llvm::StringRef long_help = GetHelpLong();
  if (!long_help.empty()) {
    FormatLongHelpText(output_strm, long_help);
  }
  if (!IsDashDashCommand() && options && options->NumCommandOptions() > 0) {
    if (WantsRawCommandString() && !WantsCompletion()) {
      // Emit the message about using ' -- ' between the end of the command
      // options and the raw input conditionally, i.e., only if the command
      // object does not want completion.
      interpreter.OutputFormattedHelpText(
          output_strm, "", "",
          "\nImportant Note: Because this command takes 'raw' input, if you "
          "use any command options"
          " you must use ' -- ' between the end of the command options and the "
          "beginning of the raw input.",
          1);
    } else if (GetNumArgumentEntries() > 0) {
      // Also emit a warning about using "--" in case you are using a command
      // that takes options and arguments.
      interpreter.OutputFormattedHelpText(
          output_strm, "", "",
          "\nThis command takes options and free-form arguments.  If your "
          "arguments resemble"
          " option specifiers (i.e., they start with a - or --), you must use "
          "' -- ' between"
          " the end of the command options and the beginning of the arguments.",
          1);
    }
  }
}

void CommandObject::AddIDsArgumentData(CommandArgumentEntry &arg,
                                       CommandArgumentType ID,
                                       CommandArgumentType IDRange) {
  CommandArgumentData id_arg;
  CommandArgumentData id_range_arg;

  // Create the first variant for the first (and only) argument for this
  // command.
  id_arg.arg_type = ID;
  id_arg.arg_repetition = eArgRepeatOptional;

  // Create the second variant for the first (and only) argument for this
  // command.
  id_range_arg.arg_type = IDRange;
  id_range_arg.arg_repetition = eArgRepeatOptional;

  // The first (and only) argument for this command could be either an id or an
  // id_range. Push both variants into the entry for the first argument for
  // this command.
  arg.push_back(id_arg);
  arg.push_back(id_range_arg);
}

const char *CommandObject::GetArgumentTypeAsCString(
    const lldb::CommandArgumentType arg_type) {
  assert(arg_type < eArgTypeLastArg &&
         "Invalid argument type passed to GetArgumentTypeAsCString");
  return g_arguments_data[arg_type].arg_name;
}

const char *CommandObject::GetArgumentDescriptionAsCString(
    const lldb::CommandArgumentType arg_type) {
  assert(arg_type < eArgTypeLastArg &&
         "Invalid argument type passed to GetArgumentDescriptionAsCString");
  return g_arguments_data[arg_type].help_text;
}

Target *CommandObject::GetDummyTarget() {
  return m_interpreter.GetDebugger().GetDummyTarget();
}

Target *CommandObject::GetSelectedOrDummyTarget(bool prefer_dummy) {
  return m_interpreter.GetDebugger().GetSelectedOrDummyTarget(prefer_dummy);
}

Thread *CommandObject::GetDefaultThread() {
  Thread *thread_to_use = m_exe_ctx.GetThreadPtr();
  if (thread_to_use)
    return thread_to_use;

  Process *process = m_exe_ctx.GetProcessPtr();
  if (!process) {
    Target *target = m_exe_ctx.GetTargetPtr();
    if (!target) {
      target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    }
    if (target)
      process = target->GetProcessSP().get();
  }

  if (process)
    return process->GetThreadList().GetSelectedThread().get();
  else
    return nullptr;
}

bool CommandObjectParsed::Execute(const char *args_string,
                                  CommandReturnObject &result) {
  bool handled = false;
  Args cmd_args(args_string);
  if (HasOverrideCallback()) {
    Args full_args(GetCommandName());
    full_args.AppendArguments(cmd_args);
    handled =
        InvokeOverrideCallback(full_args.GetConstArgumentVector(), result);
  }
  if (!handled) {
    for (auto entry : llvm::enumerate(cmd_args.entries())) {
      if (!entry.value().ref.empty() && entry.value().ref.front() == '`') {
        cmd_args.ReplaceArgumentAtIndex(
            entry.index(),
            m_interpreter.ProcessEmbeddedScriptCommands(entry.value().c_str()));
      }
    }

    if (CheckRequirements(result)) {
      if (ParseOptions(cmd_args, result)) {
        // Call the command-specific version of 'Execute', passing it the
        // already processed arguments.
        handled = DoExecute(cmd_args, result);
      }
    }

    Cleanup();
  }
  return handled;
}

bool CommandObjectRaw::Execute(const char *args_string,
                               CommandReturnObject &result) {
  bool handled = false;
  if (HasOverrideCallback()) {
    std::string full_command(GetCommandName());
    full_command += ' ';
    full_command += args_string;
    const char *argv[2] = {nullptr, nullptr};
    argv[0] = full_command.c_str();
    handled = InvokeOverrideCallback(argv, result);
  }
  if (!handled) {
    if (CheckRequirements(result))
      handled = DoExecute(args_string, result);

    Cleanup();
  }
  return handled;
}

static llvm::StringRef arch_helper() {
  static StreamString g_archs_help;
  if (g_archs_help.Empty()) {
    StringList archs;

    ArchSpec::ListSupportedArchNames(archs);
    g_archs_help.Printf("These are the supported architecture names:\n");
    archs.Join("\n", g_archs_help);
  }
  return g_archs_help.GetString();
}

CommandObject::ArgumentTableEntry CommandObject::g_arguments_data[] = {
    // clang-format off
    { eArgTypeAddress, "address", CommandCompletions::eNoCompletion, { nullptr, false }, "A valid address in the target program's execution space." },
    { eArgTypeAddressOrExpression, "address-expression", CommandCompletions::eNoCompletion, { nullptr, false }, "An expression that resolves to an address." },
    { eArgTypeAliasName, "alias-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of an abbreviation (alias) for a debugger command." },
    { eArgTypeAliasOptions, "options-for-aliased-command", CommandCompletions::eNoCompletion, { nullptr, false }, "Command options to be used as part of an alias (abbreviation) definition.  (See 'help commands alias' for more information.)" },
    { eArgTypeArchitecture, "arch", CommandCompletions::eArchitectureCompletion, { arch_helper, true }, "The architecture name, e.g. i386 or x86_64." },
    { eArgTypeBoolean, "boolean", CommandCompletions::eNoCompletion, { nullptr, false }, "A Boolean value: 'true' or 'false'" },
    { eArgTypeBreakpointID, "breakpt-id", CommandCompletions::eNoCompletion, { BreakpointIDHelpTextCallback, false }, nullptr },
    { eArgTypeBreakpointIDRange, "breakpt-id-list", CommandCompletions::eNoCompletion, { BreakpointIDRangeHelpTextCallback, false }, nullptr },
    { eArgTypeBreakpointName, "breakpoint-name", CommandCompletions::eNoCompletion, { BreakpointNameHelpTextCallback, false }, nullptr },
    { eArgTypeByteSize, "byte-size", CommandCompletions::eNoCompletion, { nullptr, false }, "Number of bytes to use." },
    { eArgTypeClassName, "class-name", CommandCompletions::eNoCompletion, { nullptr, false }, "Then name of a class from the debug information in the program." },
    { eArgTypeCommandName, "cmd-name", CommandCompletions::eNoCompletion, { nullptr, false }, "A debugger command (may be multiple words), without any options or arguments." },
    { eArgTypeCount, "count", CommandCompletions::eNoCompletion, { nullptr, false }, "An unsigned integer." },
    { eArgTypeDirectoryName, "directory", CommandCompletions::eDiskDirectoryCompletion, { nullptr, false }, "A directory name." },
    { eArgTypeDisassemblyFlavor, "disassembly-flavor", CommandCompletions::eNoCompletion, { nullptr, false }, "A disassembly flavor recognized by your disassembly plugin.  Currently the only valid options are \"att\" and \"intel\" for Intel targets" },
    { eArgTypeDescriptionVerbosity, "description-verbosity", CommandCompletions::eNoCompletion, { nullptr, false }, "How verbose the output of 'po' should be." },
    { eArgTypeEndAddress, "end-address", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeExpression, "expr", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeExpressionPath, "expr-path", CommandCompletions::eNoCompletion, { ExprPathHelpTextCallback, true }, nullptr },
    { eArgTypeExprFormat, "expression-format", CommandCompletions::eNoCompletion, { nullptr, false }, "[ [bool|b] | [bin] | [char|c] | [oct|o] | [dec|i|d|u] | [hex|x] | [float|f] | [cstr|s] ]" },
    { eArgTypeFilename, "filename", CommandCompletions::eDiskFileCompletion, { nullptr, false }, "The name of a file (can include path)." },
    { eArgTypeFormat, "format", CommandCompletions::eNoCompletion, { FormatHelpTextCallback, true }, nullptr },
    { eArgTypeFrameIndex, "frame-index", CommandCompletions::eNoCompletion, { nullptr, false }, "Index into a thread's list of frames." },
    { eArgTypeFullName, "fullname", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeFunctionName, "function-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a function." },
    { eArgTypeFunctionOrSymbol, "function-or-symbol", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a function or symbol." },
    { eArgTypeGDBFormat, "gdb-format", CommandCompletions::eNoCompletion, { GDBFormatHelpTextCallback, true }, nullptr },
    { eArgTypeHelpText, "help-text", CommandCompletions::eNoCompletion, { nullptr, false }, "Text to be used as help for some other entity in LLDB" },
    { eArgTypeIndex, "index", CommandCompletions::eNoCompletion, { nullptr, false }, "An index into a list." },
    { eArgTypeLanguage, "source-language", CommandCompletions::eNoCompletion, { LanguageTypeHelpTextCallback, true }, nullptr },
    { eArgTypeLineNum, "linenum", CommandCompletions::eNoCompletion, { nullptr, false }, "Line number in a source file." },
    { eArgTypeLogCategory, "log-category", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a category within a log channel, e.g. all (try \"log list\" to see a list of all channels and their categories." },
    { eArgTypeLogChannel, "log-channel", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a log channel, e.g. process.gdb-remote (try \"log list\" to see a list of all channels and their categories)." },
    { eArgTypeMethod, "method", CommandCompletions::eNoCompletion, { nullptr, false }, "A C++ method name." },
    { eArgTypeName, "name", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeNewPathPrefix, "new-path-prefix", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeNumLines, "num-lines", CommandCompletions::eNoCompletion, { nullptr, false }, "The number of lines to use." },
    { eArgTypeNumberPerLine, "number-per-line", CommandCompletions::eNoCompletion, { nullptr, false }, "The number of items per line to display." },
    { eArgTypeOffset, "offset", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeOldPathPrefix, "old-path-prefix", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeOneLiner, "one-line-command", CommandCompletions::eNoCompletion, { nullptr, false }, "A command that is entered as a single line of text." },
    { eArgTypePath, "path", CommandCompletions::eDiskFileCompletion, { nullptr, false }, "Path." },
    { eArgTypePermissionsNumber, "perms-numeric", CommandCompletions::eNoCompletion, { nullptr, false }, "Permissions given as an octal number (e.g. 755)." },
    { eArgTypePermissionsString, "perms=string", CommandCompletions::eNoCompletion, { nullptr, false }, "Permissions given as a string value (e.g. rw-r-xr--)." },
    { eArgTypePid, "pid", CommandCompletions::eNoCompletion, { nullptr, false }, "The process ID number." },
    { eArgTypePlugin, "plugin", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeProcessName, "process-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of the process." },
    { eArgTypePythonClass, "python-class", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a Python class." },
    { eArgTypePythonFunction, "python-function", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a Python function." },
    { eArgTypePythonScript, "python-script", CommandCompletions::eNoCompletion, { nullptr, false }, "Source code written in Python." },
    { eArgTypeQueueName, "queue-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of the thread queue." },
    { eArgTypeRegisterName, "register-name", CommandCompletions::eNoCompletion, { RegisterNameHelpTextCallback, true }, nullptr },
    { eArgTypeRegularExpression, "regular-expression", CommandCompletions::eNoCompletion, { nullptr, false }, "A regular expression." },
    { eArgTypeRunArgs, "run-args", CommandCompletions::eNoCompletion, { nullptr, false }, "Arguments to be passed to the target program when it starts executing." },
    { eArgTypeRunMode, "run-mode", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeScriptedCommandSynchronicity, "script-cmd-synchronicity", CommandCompletions::eNoCompletion, { nullptr, false }, "The synchronicity to use to run scripted commands with regard to LLDB event system." },
    { eArgTypeScriptLang, "script-language", CommandCompletions::eNoCompletion, { nullptr, false }, "The scripting language to be used for script-based commands.  Currently only Python is valid." },
    { eArgTypeSearchWord, "search-word", CommandCompletions::eNoCompletion, { nullptr, false }, "Any word of interest for search purposes." },
    { eArgTypeSelector, "selector", CommandCompletions::eNoCompletion, { nullptr, false }, "An Objective-C selector name." },
    { eArgTypeSettingIndex, "setting-index", CommandCompletions::eNoCompletion, { nullptr, false }, "An index into a settings variable that is an array (try 'settings list' to see all the possible settings variables and their types)." },
    { eArgTypeSettingKey, "setting-key", CommandCompletions::eNoCompletion, { nullptr, false }, "A key into a settings variables that is a dictionary (try 'settings list' to see all the possible settings variables and their types)." },
    { eArgTypeSettingPrefix, "setting-prefix", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a settable internal debugger variable up to a dot ('.'), e.g. 'target.process.'" },
    { eArgTypeSettingVariableName, "setting-variable-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a settable internal debugger variable.  Type 'settings list' to see a complete list of such variables." },
    { eArgTypeShlibName, "shlib-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a shared library." },
    { eArgTypeSourceFile, "source-file", CommandCompletions::eSourceFileCompletion, { nullptr, false }, "The name of a source file.." },
    { eArgTypeSortOrder, "sort-order", CommandCompletions::eNoCompletion, { nullptr, false }, "Specify a sort order when dumping lists." },
    { eArgTypeStartAddress, "start-address", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeSummaryString, "summary-string", CommandCompletions::eNoCompletion, { SummaryStringHelpTextCallback, true }, nullptr },
    { eArgTypeSymbol, "symbol", CommandCompletions::eSymbolCompletion, { nullptr, false }, "Any symbol name (function name, variable, argument, etc.)" },
    { eArgTypeThreadID, "thread-id", CommandCompletions::eNoCompletion, { nullptr, false }, "Thread ID number." },
    { eArgTypeThreadIndex, "thread-index", CommandCompletions::eNoCompletion, { nullptr, false }, "Index into the process' list of threads." },
    { eArgTypeThreadName, "thread-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The thread's name." },
    { eArgTypeTypeName, "type-name", CommandCompletions::eNoCompletion, { nullptr, false }, "A type name." },
    { eArgTypeUnsignedInteger, "unsigned-integer", CommandCompletions::eNoCompletion, { nullptr, false }, "An unsigned integer." },
    { eArgTypeUnixSignal, "unix-signal", CommandCompletions::eNoCompletion, { nullptr, false }, "A valid Unix signal name or number (e.g. SIGKILL, KILL or 9)." },
    { eArgTypeVarName, "variable-name", CommandCompletions::eNoCompletion, { nullptr, false }, "The name of a variable in your program." },
    { eArgTypeValue, "value", CommandCompletions::eNoCompletion, { nullptr, false }, "A value could be anything, depending on where and how it is used." },
    { eArgTypeWidth, "width", CommandCompletions::eNoCompletion, { nullptr, false }, "Help text goes here." },
    { eArgTypeNone, "none", CommandCompletions::eNoCompletion, { nullptr, false }, "No help available for this." },
    { eArgTypePlatform, "platform-name", CommandCompletions::ePlatformPluginCompletion, { nullptr, false }, "The name of an installed platform plug-in . Type 'platform list' to see a complete list of installed platforms." },
    { eArgTypeWatchpointID, "watchpt-id", CommandCompletions::eNoCompletion, { nullptr, false }, "Watchpoint IDs are positive integers." },
    { eArgTypeWatchpointIDRange, "watchpt-id-list", CommandCompletions::eNoCompletion, { nullptr, false }, "For example, '1-3' or '1 to 3'." },
    { eArgTypeWatchType, "watch-type", CommandCompletions::eNoCompletion, { nullptr, false }, "Specify the type for a watchpoint." },
    { eArgRawInput, "raw-input", CommandCompletions::eNoCompletion, { nullptr, false }, "Free-form text passed to a command without prior interpretation, allowing spaces without requiring quotes.  To pass arguments and free form text put two dashes ' -- ' between the last argument and any raw input." },
    { eArgTypeCommand, "command", CommandCompletions::eNoCompletion, { nullptr, false }, "An LLDB Command line command." }
    // clang-format on
};

const CommandObject::ArgumentTableEntry *CommandObject::GetArgumentTable() {
  // If this assertion fires, then the table above is out of date with the
  // CommandArgumentType enumeration
  assert((sizeof(CommandObject::g_arguments_data) /
          sizeof(CommandObject::ArgumentTableEntry)) == eArgTypeLastArg);
  return CommandObject::g_arguments_data;
}
