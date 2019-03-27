//===-- ValueObjectConstResultImpl.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObjectConstResultImpl_h_
#define liblldb_ValueObjectConstResultImpl_h_

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>
namespace lldb_private {
class CompilerType;
}
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
// A class wrapping common implementation details for operations in
// ValueObjectConstResult ( & Child ) that may need to jump from the host
// memory space into the target's memory space
//----------------------------------------------------------------------
class ValueObjectConstResultImpl {
public:
  ValueObjectConstResultImpl(ValueObject *valobj,
                             lldb::addr_t live_address = LLDB_INVALID_ADDRESS);

  virtual ~ValueObjectConstResultImpl() = default;

  lldb::ValueObjectSP Dereference(Status &error);

  ValueObject *CreateChildAtIndex(size_t idx, bool synthetic_array_member,
                                  int32_t synthetic_index);

  lldb::ValueObjectSP
  GetSyntheticChildAtOffset(uint32_t offset, const CompilerType &type,
                            bool can_create,
                            ConstString name_const_str = ConstString());

  lldb::ValueObjectSP AddressOf(Status &error);

  lldb::addr_t GetLiveAddress() { return m_live_address; }

  lldb::ValueObjectSP Cast(const CompilerType &compiler_type);

  void SetLiveAddress(lldb::addr_t addr = LLDB_INVALID_ADDRESS,
                      AddressType address_type = eAddressTypeLoad) {
    m_live_address = addr;
    m_live_address_type = address_type;
  }

  virtual lldb::addr_t GetAddressOf(bool scalar_is_load_address = true,
                                    AddressType *address_type = nullptr);

  virtual size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                                uint32_t item_count = 1);

private:
  ValueObject *m_impl_backend;
  lldb::addr_t m_live_address;
  AddressType m_live_address_type;
  lldb::ValueObjectSP m_load_addr_backend;
  lldb::ValueObjectSP m_address_of_backend;

  DISALLOW_COPY_AND_ASSIGN(ValueObjectConstResultImpl);
};

} // namespace lldb_private

#endif // liblldb_ValueObjectConstResultImpl_h_
