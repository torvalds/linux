//===-- ValueObjectConstResult.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObjectConstResult_h_
#define liblldb_ValueObjectConstResult_h_

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResultImpl.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class ExecutionContextScope;
}
namespace lldb_private {
class Module;
}
namespace lldb_private {

//----------------------------------------------------------------------
// A frozen ValueObject copied into host memory
//----------------------------------------------------------------------
class ValueObjectConstResult : public ValueObject {
public:
  ~ValueObjectConstResult() override;

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, lldb::ByteOrder byte_order,
         uint32_t addr_byte_size, lldb::addr_t address = LLDB_INVALID_ADDRESS);

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
         const ConstString &name, const DataExtractor &data,
         lldb::addr_t address = LLDB_INVALID_ADDRESS);

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
         const ConstString &name, const lldb::DataBufferSP &result_data_sp,
         lldb::ByteOrder byte_order, uint32_t addr_size,
         lldb::addr_t address = LLDB_INVALID_ADDRESS);

  static lldb::ValueObjectSP
  Create(ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
         const ConstString &name, lldb::addr_t address,
         AddressType address_type, uint32_t addr_byte_size);

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    Value &value, const ConstString &name,
                                    Module *module = nullptr);

  // When an expression fails to evaluate, we return an error
  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    const Status &error);

  uint64_t GetByteSize() override;

  lldb::ValueType GetValueType() const override;

  size_t CalculateNumChildren(uint32_t max) override;

  ConstString GetTypeName() override;

  ConstString GetDisplayTypeName() override;

  bool IsInScope() override;

  void SetByteSize(size_t size);

  lldb::ValueObjectSP Dereference(Status &error) override;

  ValueObject *CreateChildAtIndex(size_t idx, bool synthetic_array_member,
                                  int32_t synthetic_index) override;

  lldb::ValueObjectSP GetSyntheticChildAtOffset(
      uint32_t offset, const CompilerType &type, bool can_create,
      ConstString name_const_str = ConstString()) override;

  lldb::ValueObjectSP AddressOf(Status &error) override;

  lldb::addr_t GetAddressOf(bool scalar_is_load_address = true,
                            AddressType *address_type = nullptr) override;

  size_t GetPointeeData(DataExtractor &data, uint32_t item_idx = 0,
                        uint32_t item_count = 1) override;

  lldb::addr_t GetLiveAddress() override { return m_impl.GetLiveAddress(); }

  void SetLiveAddress(lldb::addr_t addr = LLDB_INVALID_ADDRESS,
                      AddressType address_type = eAddressTypeLoad) override {
    m_impl.SetLiveAddress(addr, address_type);
  }

  lldb::ValueObjectSP
  GetDynamicValue(lldb::DynamicValueType valueType) override;

  lldb::LanguageType GetPreferredDisplayLanguage() override;

  lldb::ValueObjectSP Cast(const CompilerType &compiler_type) override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  ConstString m_type_name;
  uint64_t m_byte_size;

  ValueObjectConstResultImpl m_impl;

private:
  friend class ValueObjectConstResultImpl;

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         lldb::ByteOrder byte_order, uint32_t addr_byte_size,
                         lldb::addr_t address);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         const CompilerType &compiler_type,
                         const ConstString &name, const DataExtractor &data,
                         lldb::addr_t address);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         const CompilerType &compiler_type,
                         const ConstString &name,
                         const lldb::DataBufferSP &result_data_sp,
                         lldb::ByteOrder byte_order, uint32_t addr_size,
                         lldb::addr_t address);

  ValueObjectConstResult(ExecutionContextScope *exe_scope,
                         const CompilerType &compiler_type,
                         const ConstString &name, lldb::addr_t address,
                         AddressType address_type, uint32_t addr_byte_size);

  ValueObjectConstResult(ExecutionContextScope *exe_scope, const Value &value,
                         const ConstString &name, Module *module = nullptr);

  ValueObjectConstResult(ExecutionContextScope *exe_scope, const Status &error);

  DISALLOW_COPY_AND_ASSIGN(ValueObjectConstResult);
};

} // namespace lldb_private

#endif // liblldb_ValueObjectConstResult_h_
