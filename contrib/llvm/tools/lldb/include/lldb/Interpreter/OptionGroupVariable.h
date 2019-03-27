//===-- OptionGroupVariable.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupVariable_h_
#define liblldb_OptionGroupVariable_h_

#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// OptionGroupVariable
//-------------------------------------------------------------------------

class OptionGroupVariable : public OptionGroup {
public:
  OptionGroupVariable(bool show_frame_options);

  ~OptionGroupVariable() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  bool include_frame_options : 1,
      show_args : 1,    // Frame option only (include_frame_options == true)
      show_recognized_args : 1,  // Frame option only (include_frame_options ==
                                 // true)
      show_locals : 1,  // Frame option only (include_frame_options == true)
      show_globals : 1, // Frame option only (include_frame_options == true)
      use_regex : 1, show_scope : 1, show_decl : 1;
  OptionValueString summary;        // the name of a named summary
  OptionValueString summary_string; // a summary string

private:
  DISALLOW_COPY_AND_ASSIGN(OptionGroupVariable);
};

} // namespace lldb_private

#endif // liblldb_OptionGroupVariable_h_
