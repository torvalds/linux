//===-- OptionGroupOutputFile.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupOutputFile_h_
#define liblldb_OptionGroupOutputFile_h_

#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/OptionValueFileSpec.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {
//-------------------------------------------------------------------------
// OptionGroupOutputFile
//-------------------------------------------------------------------------

class OptionGroupOutputFile : public OptionGroup {
public:
  OptionGroupOutputFile();

  ~OptionGroupOutputFile() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  const OptionValueFileSpec &GetFile() { return m_file; }

  const OptionValueBoolean &GetAppend() { return m_append; }

  bool AnyOptionWasSet() const {
    return m_file.OptionWasSet() || m_append.OptionWasSet();
  }

protected:
  OptionValueFileSpec m_file;
  OptionValueBoolean m_append;
};

} // namespace lldb_private

#endif // liblldb_OptionGroupOutputFile_h_
