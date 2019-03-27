//===-- CommandObjectHelp.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectHelp_h_
#define liblldb_CommandObjectHelp_h_

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectHelp
//-------------------------------------------------------------------------

class CommandObjectHelp : public CommandObjectParsed {
public:
  CommandObjectHelp(CommandInterpreter &interpreter);

  ~CommandObjectHelp() override;

  int HandleCompletion(CompletionRequest &request) override;

  static void GenerateAdditionalHelpAvenuesMessage(
      Stream *s, llvm::StringRef command, llvm::StringRef prefix,
      llvm::StringRef subcommand, bool include_apropos = true,
      bool include_type_lookup = true);

  class CommandOptions : public Options {
  public:
    CommandOptions() : Options() {}

    ~CommandOptions() override {}

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a':
        m_show_aliases = false;
        break;
      case 'u':
        m_show_user_defined = false;
        break;
      case 'h':
        m_show_hidden = true;
        break;
      default:
        error.SetErrorStringWithFormat("unrecognized option '%c'",
                                       short_option);
        break;
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_show_aliases = true;
      m_show_user_defined = true;
      m_show_hidden = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

    // Instance variables to hold the values for command options.

    bool m_show_aliases;
    bool m_show_user_defined;
    bool m_show_hidden;
  };

  Options *GetOptions() override { return &m_options; }

protected:
  bool DoExecute(Args &command, CommandReturnObject &result) override;

private:
  CommandOptions m_options;
};

} // namespace lldb_private

#endif // liblldb_CommandObjectHelp_h_
