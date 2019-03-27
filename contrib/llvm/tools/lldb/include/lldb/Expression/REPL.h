//===-- REPL.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_REPL_h
#define lldb_REPL_h

#include <string>

#include "lldb/../../source/Commands/CommandObjectExpression.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"

namespace lldb_private {

class REPL : public IOHandlerDelegate {
public:
  //----------------------------------------------------------------------
  // See TypeSystem.h for how to add subclasses to this.
  //----------------------------------------------------------------------
  enum LLVMCastKind { eKindClang, eKindSwift, eKindGo, kNumKinds };

  LLVMCastKind getKind() const { return m_kind; }

  REPL(LLVMCastKind kind, Target &target);

  ~REPL() override;

  //------------------------------------------------------------------
  /// Get a REPL with an existing target (or, failing that, a debugger to use),
  /// and (optional) extra arguments for the compiler.
  ///
  /// @param[out] error
  ///     If this language is supported but the REPL couldn't be created, this
  ///     error is populated with the reason.
  ///
  /// @param[in] language
  ///     The language to create a REPL for.
  ///
  /// @param[in] debugger
  ///     If provided, and target is nullptr, the debugger to use when setting
  ///     up a top-level REPL.
  ///
  /// @param[in] target
  ///     If provided, the target to put the REPL inside.
  ///
  /// @param[in] repl_options
  ///     If provided, additional options for the compiler when parsing REPL
  ///     expressions.
  ///
  /// @return
  ///     The range of the containing object in the target process.
  //------------------------------------------------------------------
  static lldb::REPLSP Create(Status &Status, lldb::LanguageType language,
                             Debugger *debugger, Target *target,
                             const char *repl_options);

  void SetFormatOptions(const OptionGroupFormat &options) {
    m_format_options = options;
  }

  void
  SetValueObjectDisplayOptions(const OptionGroupValueObjectDisplay &options) {
    m_varobj_options = options;
  }

  void
  SetCommandOptions(const CommandObjectExpression::CommandOptions &options) {
    m_command_options = options;
  }

  void SetCompilerOptions(const char *options) {
    if (options)
      m_compiler_options = options;
  }

  lldb::IOHandlerSP GetIOHandler();

  Status RunLoop();

  //------------------------------------------------------------------
  // IOHandler::Delegate functions
  //------------------------------------------------------------------
  void IOHandlerActivated(IOHandler &io_handler) override;

  bool IOHandlerInterrupt(IOHandler &io_handler) override;

  void IOHandlerInputInterrupted(IOHandler &io_handler,
                                 std::string &line) override;

  const char *IOHandlerGetFixIndentationCharacters() override;

  ConstString IOHandlerGetControlSequence(char ch) override;

  const char *IOHandlerGetCommandPrefix() override;

  const char *IOHandlerGetHelpPrologue() override;

  bool IOHandlerIsInputComplete(IOHandler &io_handler,
                                StringList &lines) override;

  int IOHandlerFixIndentation(IOHandler &io_handler, const StringList &lines,
                              int cursor_position) override;

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &line) override;

  int IOHandlerComplete(IOHandler &io_handler, const char *current_line,
                        const char *cursor, const char *last_char,
                        int skip_first_n_matches, int max_matches,
                        StringList &matches, StringList &descriptions) override;

protected:
  static int CalculateActualIndentation(const StringList &lines);

  //----------------------------------------------------------------------
  // Subclasses should override these functions to implement a functional REPL.
  //----------------------------------------------------------------------

  virtual Status DoInitialization() = 0;

  virtual ConstString GetSourceFileBasename() = 0;

  virtual const char *GetAutoIndentCharacters() = 0;

  virtual bool SourceIsComplete(const std::string &source) = 0;

  virtual lldb::offset_t GetDesiredIndentation(
      const StringList &lines, int cursor_position,
      int tab_size) = 0; // LLDB_INVALID_OFFSET means no change

  virtual lldb::LanguageType GetLanguage() = 0;

  virtual bool PrintOneVariable(Debugger &debugger,
                                lldb::StreamFileSP &output_sp,
                                lldb::ValueObjectSP &valobj_sp,
                                ExpressionVariable *var = nullptr) = 0;

  virtual int CompleteCode(const std::string &current_code,
                           StringList &matches) = 0;

  OptionGroupFormat m_format_options = OptionGroupFormat(lldb::eFormatDefault);
  OptionGroupValueObjectDisplay m_varobj_options;
  CommandObjectExpression::CommandOptions m_command_options;
  std::string m_compiler_options;

  bool m_enable_auto_indent = true;
  std::string m_indent_str; // Use this string for each level of indentation
  std::string m_current_indent_str;
  uint32_t m_current_indent_level = 0;

  std::string m_repl_source_path;
  bool m_dedicated_repl_mode = false;

  StringList m_code; // All accumulated REPL statements are saved here

  Target &m_target;
  lldb::IOHandlerSP m_io_handler_sp;
  LLVMCastKind m_kind;

private:
  std::string GetSourcePath();
};

} // namespace lldb_private

#endif // lldb_REPL_h
