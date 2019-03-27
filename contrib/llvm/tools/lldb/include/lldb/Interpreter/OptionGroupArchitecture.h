//===-- OptionGroupArchitecture.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupArchitecture_h_
#define liblldb_OptionGroupArchitecture_h_

#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// OptionGroupArchitecture
//-------------------------------------------------------------------------

class OptionGroupArchitecture : public OptionGroup {
public:
  OptionGroupArchitecture();

  ~OptionGroupArchitecture() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  bool GetArchitecture(Platform *platform, ArchSpec &arch);

  bool ArchitectureWasSpecified() const { return !m_arch_str.empty(); }

  llvm::StringRef GetArchitectureName() const { return m_arch_str; }

protected:
  std::string m_arch_str; // Save the arch triple in case a platform is
                          // specified after the architecture
};

} // namespace lldb_private

#endif // liblldb_OptionGroupArchitecture_h_
