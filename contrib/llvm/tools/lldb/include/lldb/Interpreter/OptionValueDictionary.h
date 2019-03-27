//===-- OptionValueDictionary.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValueDictionary_h_
#define liblldb_OptionValueDictionary_h_

#include <map>

#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class OptionValueDictionary : public OptionValue {
public:
  OptionValueDictionary(uint32_t type_mask = UINT32_MAX,
                        bool raw_value_dump = true)
      : OptionValue(), m_type_mask(type_mask), m_values(),
        m_raw_value_dump(raw_value_dump) {}

  ~OptionValueDictionary() override {}

  //---------------------------------------------------------------------
  // Virtual subclass pure virtual overrides
  //---------------------------------------------------------------------

  OptionValue::Type GetType() const override { return eTypeDictionary; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

  Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign) override;

  bool Clear() override {
    m_values.clear();
    m_value_was_set = false;
    return true;
  }

  lldb::OptionValueSP DeepCopy() const override;

  bool IsAggregateValue() const override { return true; }

  bool IsHomogenous() const {
    return ConvertTypeMaskToType(m_type_mask) != eTypeInvalid;
  }

  //---------------------------------------------------------------------
  // Subclass specific functions
  //---------------------------------------------------------------------

  size_t GetNumValues() const { return m_values.size(); }

  lldb::OptionValueSP GetValueForKey(const ConstString &key) const;

  lldb::OptionValueSP GetSubValue(const ExecutionContext *exe_ctx,
                                  llvm::StringRef name, bool will_modify,
                                  Status &error) const override;

  Status SetSubValue(const ExecutionContext *exe_ctx, VarSetOperationType op,
                     llvm::StringRef name, llvm::StringRef value) override;

  bool SetValueForKey(const ConstString &key,
                      const lldb::OptionValueSP &value_sp,
                      bool can_replace = true);

  bool DeleteValueForKey(const ConstString &key);

  size_t GetArgs(Args &args) const;

  Status SetArgs(const Args &args, VarSetOperationType op);

protected:
  typedef std::map<ConstString, lldb::OptionValueSP> collection;
  uint32_t m_type_mask;
  collection m_values;
  bool m_raw_value_dump;
};

} // namespace lldb_private

#endif // liblldb_OptionValueDictionary_h_
