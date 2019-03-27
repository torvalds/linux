//===-- OptionGroupUUID.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupUUID_h_
#define liblldb_OptionGroupUUID_h_

#include "lldb/Interpreter/OptionValueUUID.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// OptionGroupUUID
//-------------------------------------------------------------------------

class OptionGroupUUID : public OptionGroup {
public:
  OptionGroupUUID();

  ~OptionGroupUUID() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  const OptionValueUUID &GetOptionValue() const { return m_uuid; }

protected:
  OptionValueUUID m_uuid;
};

} // namespace lldb_private

#endif // liblldb_OptionGroupUUID_h_
