//===-- CommandObjectApropos.cpp ---------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectApropos.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Utility/Args.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// CommandObjectApropos
//-------------------------------------------------------------------------

CommandObjectApropos::CommandObjectApropos(CommandInterpreter &interpreter)
    : CommandObjectParsed(
          interpreter, "apropos",
          "List debugger commands related to a word or subject.", nullptr) {
  CommandArgumentEntry arg;
  CommandArgumentData search_word_arg;

  // Define the first (and only) variant of this arg.
  search_word_arg.arg_type = eArgTypeSearchWord;
  search_word_arg.arg_repetition = eArgRepeatPlain;

  // There is only one variant this argument could be; put it into the argument
  // entry.
  arg.push_back(search_word_arg);

  // Push the data for the first argument into the m_arguments vector.
  m_arguments.push_back(arg);
}

CommandObjectApropos::~CommandObjectApropos() = default;

bool CommandObjectApropos::DoExecute(Args &args, CommandReturnObject &result) {
  const size_t argc = args.GetArgumentCount();

  if (argc == 1) {
    auto search_word = args[0].ref;
    if (!search_word.empty()) {
      // The bulk of the work must be done inside the Command Interpreter,
      // since the command dictionary is private.
      StringList commands_found;
      StringList commands_help;

      m_interpreter.FindCommandsForApropos(search_word, commands_found,
                                           commands_help, true, true, true);

      if (commands_found.GetSize() == 0) {
        result.AppendMessageWithFormat("No commands found pertaining to '%s'. "
                                       "Try 'help' to see a complete list of "
                                       "debugger commands.\n",
                                       args[0].c_str());
      } else {
        if (commands_found.GetSize() > 0) {
          result.AppendMessageWithFormat(
              "The following commands may relate to '%s':\n", args[0].c_str());
          size_t max_len = 0;

          for (size_t i = 0; i < commands_found.GetSize(); ++i) {
            size_t len = strlen(commands_found.GetStringAtIndex(i));
            if (len > max_len)
              max_len = len;
          }

          for (size_t i = 0; i < commands_found.GetSize(); ++i)
            m_interpreter.OutputFormattedHelpText(
                result.GetOutputStream(), commands_found.GetStringAtIndex(i),
                "--", commands_help.GetStringAtIndex(i), max_len);
        }
      }

      std::vector<const Property *> properties;
      const size_t num_properties =
          m_interpreter.GetDebugger().Apropos(search_word, properties);
      if (num_properties) {
        const bool dump_qualified_name = true;
        result.AppendMessageWithFormatv(
            "\nThe following settings variables may relate to '{0}': \n\n",
            args[0].ref);
        for (size_t i = 0; i < num_properties; ++i)
          properties[i]->DumpDescription(
              m_interpreter, result.GetOutputStream(), 0, dump_qualified_name);
      }

      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError("'' is not a valid search word.\n");
      result.SetStatus(eReturnStatusFailed);
    }
  } else {
    result.AppendError("'apropos' must be called with exactly one argument.\n");
    result.SetStatus(eReturnStatusFailed);
  }

  return result.Succeeded();
}
