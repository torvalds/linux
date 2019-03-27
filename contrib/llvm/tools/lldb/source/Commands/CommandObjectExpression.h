//===-- CommandObjectExpression.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectExpression_h_
#define liblldb_CommandObjectExpression_h_

#include "lldb/Core/IOHandler.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/OptionGroupBoolean.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/lldb-private-enumerations.h"
namespace lldb_private {

class CommandObjectExpression : public CommandObjectRaw,
                                public IOHandlerDelegate {
public:
  class CommandOptions : public OptionGroup {
  public:
    CommandOptions();

    ~CommandOptions() override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    bool top_level;
    bool unwind_on_error;
    bool ignore_breakpoints;
    bool allow_jit;
    bool show_types;
    bool show_summary;
    bool debug;
    uint32_t timeout;
    bool try_all_threads;
    lldb::LanguageType language;
    LanguageRuntimeDescriptionDisplayVerbosity m_verbosity;
    LazyBool auto_apply_fixits;
  };

  CommandObjectExpression(CommandInterpreter &interpreter);

  ~CommandObjectExpression() override;

  Options *GetOptions() override;

  int HandleCompletion(CompletionRequest &request) override;

protected:
  //------------------------------------------------------------------
  // IOHandler::Delegate functions
  //------------------------------------------------------------------
  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override;

  bool IOHandlerIsInputComplete(IOHandler &io_handler,
                                StringList &lines) override;

  bool DoExecute(llvm::StringRef command, CommandReturnObject &result) override;

  bool EvaluateExpression(llvm::StringRef expr, Stream *output_stream,
                          Stream *error_stream,
                          CommandReturnObject *result = NULL);

  void GetMultilineExpression();

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options;
  OptionGroupValueObjectDisplay m_varobj_options;
  OptionGroupBoolean m_repl_option;
  CommandOptions m_command_options;
  uint32_t m_expr_line_count;
  std::string m_expr_lines;       // Multi-line expression support
  std::string m_fixed_expression; // Holds the current expression's fixed text.
};

} // namespace lldb_private

#endif // liblldb_CommandObjectExpression_h_
