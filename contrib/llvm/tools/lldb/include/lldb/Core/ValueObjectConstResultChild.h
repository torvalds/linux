//===-- ValueObjectConstResultChild.h ----------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObjectConstResultChild_h_
#define liblldb_ValueObjectConstResultChild_h_

#include "lldb/Core/ValueObjectChild.h"
#include "lldb/Core/ValueObjectConstResultImpl.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>
namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class Status;
}
namespace lldb_private {
class ValueObject;
}

namespace lldb_private {

//----------------------------------------------------------------------
// A child of a ValueObjectConstResult.
//----------------------------------------------------------------------
class ValueObjectConstResultChild : public ValueObjectChild {
public:
  ValueObjectConstResultChild(ValueObject &parent,
                              const CompilerType &compiler_type,
                              const ConstString &name, uint32_t byte_size,
                              int32_t byte_offset, uint32_t bitfield_bit_size,
                              uint32_t bitfield_bit_offset, bool is_base_class,
                              bool is_deref_of_parent,
                              lldb::addr_t live_address,
                              uint64_t language_flags);

  ~ValueObjectConstResultChild() override;

  lldb::ValueObjectSP Dereference(Status &error) override;

  ValueObject *CreateChildAtIndex(size_t idx, bool synthetic_array_member,
                                  int32_t synthetic_index) override;

  virtual CompilerType GetCompilerType() {
    return ValueObjectChild::GetCompilerType();
  }

  lldb::ValueObjectSP GetSyntheticChildAtOffset(
      uint32_t offset, const CompilerType &type, bool can_create,
      ConstString name_const_str = ConstString()) override;

  lldb::ValueObjectSP AddressOf(Status &error) override;

  lldb::addr_t GetAddressOf(bool scalar_is_load_address = true,
                            AddressType *address_type = nullptr) override;

  size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                        uint32_t item_count = 1) override;

  lldb::ValueObjectSP Cast(const CompilerType &compiler_type) override;

protected:
  ValueObjectConstResultImpl m_impl;

private:
  friend class ValueObject;
  friend class ValueObjectConstResult;
  friend class ValueObjectConstResultImpl;

  DISALLOW_COPY_AND_ASSIGN(ValueObjectConstResultChild);
};

} // namespace lldb_private

#endif // liblldb_ValueObjectConstResultChild_h_
