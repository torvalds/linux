//===-- OptionGroupFormat.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupFormat_h_
#define liblldb_OptionGroupFormat_h_

#include "lldb/Interpreter/OptionValueFormat.h"
#include "lldb/Interpreter/OptionValueSInt64.h"
#include "lldb/Interpreter/OptionValueUInt64.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// OptionGroupFormat
//-------------------------------------------------------------------------

class OptionGroupFormat : public OptionGroup {
public:
  static const uint32_t OPTION_GROUP_FORMAT = LLDB_OPT_SET_1;
  static const uint32_t OPTION_GROUP_GDB_FMT = LLDB_OPT_SET_2;
  static const uint32_t OPTION_GROUP_SIZE = LLDB_OPT_SET_3;
  static const uint32_t OPTION_GROUP_COUNT = LLDB_OPT_SET_4;

  OptionGroupFormat(
      lldb::Format default_format,
      uint64_t default_byte_size =
          UINT64_MAX, // Pass UINT64_MAX to disable the "--size" option
      uint64_t default_count =
          UINT64_MAX); // Pass UINT64_MAX to disable the "--count" option

  ~OptionGroupFormat() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  lldb::Format GetFormat() const { return m_format.GetCurrentValue(); }

  OptionValueFormat &GetFormatValue() { return m_format; }

  const OptionValueFormat &GetFormatValue() const { return m_format; }

  OptionValueUInt64 &GetByteSizeValue() { return m_byte_size; }

  const OptionValueUInt64 &GetByteSizeValue() const { return m_byte_size; }

  OptionValueUInt64 &GetCountValue() { return m_count; }

  const OptionValueUInt64 &GetCountValue() const { return m_count; }

  bool HasGDBFormat() const { return m_has_gdb_format; }

  bool AnyOptionWasSet() const {
    return m_format.OptionWasSet() || m_byte_size.OptionWasSet() ||
           m_count.OptionWasSet();
  }

protected:
  bool ParserGDBFormatLetter(ExecutionContext *execution_context,
                             char format_letter, lldb::Format &format,
                             uint32_t &byte_size);

  OptionValueFormat m_format;
  OptionValueUInt64 m_byte_size;
  OptionValueUInt64 m_count;
  char m_prev_gdb_format;
  char m_prev_gdb_size;
  bool m_has_gdb_format;
};

} // namespace lldb_private

#endif // liblldb_OptionGroupFormat_h_
