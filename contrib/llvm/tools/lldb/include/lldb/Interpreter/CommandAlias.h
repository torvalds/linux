//===-- CommandAlias.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandAlias_h_
#define liblldb_CommandAlias_h_

#include <memory>

#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/lldb-forward.h"

namespace lldb_private {
class CommandAlias : public CommandObject {
public:
  typedef std::unique_ptr<CommandAlias> UniquePointer;

  CommandAlias(CommandInterpreter &interpreter, lldb::CommandObjectSP cmd_sp,
               llvm::StringRef options_args, llvm::StringRef name,
               llvm::StringRef help = llvm::StringRef(),
               llvm::StringRef syntax = llvm::StringRef(), uint32_t flags = 0);

  void GetAliasExpansion(StreamString &help_string) const;

  bool IsValid() const { return m_underlying_command_sp && m_option_args_sp; }

  explicit operator bool() const { return IsValid(); }

  bool WantsRawCommandString() override;

  bool WantsCompletion() override;

  int HandleCompletion(CompletionRequest &request) override;

  int HandleArgumentCompletion(
      CompletionRequest &request,
      OptionElementVector &opt_element_vector) override;

  Options *GetOptions() override;

  bool IsAlias() override { return true; }

  bool IsDashDashCommand() override;

  llvm::StringRef GetHelp() override;

  llvm::StringRef GetHelpLong() override;

  void SetHelp(llvm::StringRef str) override;

  void SetHelpLong(llvm::StringRef str) override;

  bool Execute(const char *args_string, CommandReturnObject &result) override;

  lldb::CommandObjectSP GetUnderlyingCommand() {
    return m_underlying_command_sp;
  }
  OptionArgVectorSP GetOptionArguments() const { return m_option_args_sp; }
  const char *GetOptionString() { return m_option_string.c_str(); }

  // this takes an alias - potentially nested (i.e. an alias to an alias) and
  // expands it all the way to a non-alias command
  std::pair<lldb::CommandObjectSP, OptionArgVectorSP> Desugar();

protected:
  bool IsNestedAlias();

private:
  lldb::CommandObjectSP m_underlying_command_sp;
  std::string m_option_string;
  OptionArgVectorSP m_option_args_sp;
  LazyBool m_is_dashdash_alias;
  bool m_did_set_help : 1;
  bool m_did_set_help_long : 1;
};
} // namespace lldb_private

#endif // liblldb_CommandAlias_h_
