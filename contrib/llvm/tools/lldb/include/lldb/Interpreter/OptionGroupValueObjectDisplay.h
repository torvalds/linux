//===-- OptionGroupValueObjectDisplay.h -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupValueObjectDisplay_h_
#define liblldb_OptionGroupValueObjectDisplay_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// OptionGroupValueObjectDisplay
//-------------------------------------------------------------------------

class OptionGroupValueObjectDisplay : public OptionGroup {
public:
  OptionGroupValueObjectDisplay();

  ~OptionGroupValueObjectDisplay() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  bool AnyOptionWasSet() const {
    return show_types || no_summary_depth != 0 || show_location ||
           flat_output || use_objc || max_depth != UINT32_MAX ||
           ptr_depth != 0 || !use_synth || be_raw || ignore_cap ||
           run_validator;
  }

  DumpValueObjectOptions GetAsDumpOptions(
      LanguageRuntimeDescriptionDisplayVerbosity lang_descr_verbosity =
          eLanguageRuntimeDescriptionDisplayVerbosityFull,
      lldb::Format format = lldb::eFormatDefault,
      lldb::TypeSummaryImplSP summary_sp = lldb::TypeSummaryImplSP());

  bool show_types : 1, show_location : 1, flat_output : 1, use_objc : 1,
      use_synth : 1, be_raw : 1, ignore_cap : 1, run_validator : 1;

  uint32_t no_summary_depth;
  uint32_t max_depth;
  uint32_t ptr_depth;
  uint32_t elem_count;
  lldb::DynamicValueType use_dynamic;
};

} // namespace lldb_private

#endif // liblldb_OptionGroupValueObjectDisplay_h_
