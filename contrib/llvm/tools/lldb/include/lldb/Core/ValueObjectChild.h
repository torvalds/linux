//===-- ValueObjectChild.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObjectChild_h_
#define liblldb_ValueObjectChild_h_

#include "lldb/Core/ValueObject.h"

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/Optional.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {

//----------------------------------------------------------------------
// A child of another ValueObject.
//----------------------------------------------------------------------
class ValueObjectChild : public ValueObject {
public:
  ~ValueObjectChild() override;

  uint64_t GetByteSize() override { return m_byte_size; }

  lldb::offset_t GetByteOffset() override { return m_byte_offset; }

  uint32_t GetBitfieldBitSize() override { return m_bitfield_bit_size; }

  uint32_t GetBitfieldBitOffset() override { return m_bitfield_bit_offset; }

  lldb::ValueType GetValueType() const override;

  size_t CalculateNumChildren(uint32_t max) override;

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  ConstString GetDisplayTypeName() override;

  bool IsInScope() override;

  bool IsBaseClass() override { return m_is_base_class; }

  bool IsDereferenceOfParent() override { return m_is_deref_of_parent; }

protected:
  bool UpdateValue() override;

  LazyBool CanUpdateWithInvalidExecutionContext() override;

  CompilerType GetCompilerTypeImpl() override { return m_compiler_type; }

  CompilerType m_compiler_type;
  ConstString m_type_name;
  uint64_t m_byte_size;
  int32_t m_byte_offset;
  uint8_t m_bitfield_bit_size;
  uint8_t m_bitfield_bit_offset;
  bool m_is_base_class;
  bool m_is_deref_of_parent;
  llvm::Optional<LazyBool> m_can_update_with_invalid_exe_ctx;

  //
  //  void
  //  ReadValueFromMemory (ValueObject* parent, lldb::addr_t address);

protected:
  friend class ValueObject;
  friend class ValueObjectConstResult;
  friend class ValueObjectConstResultImpl;

  ValueObjectChild(ValueObject &parent, const CompilerType &compiler_type,
                   const ConstString &name, uint64_t byte_size,
                   int32_t byte_offset, uint32_t bitfield_bit_size,
                   uint32_t bitfield_bit_offset, bool is_base_class,
                   bool is_deref_of_parent,
                   AddressType child_ptr_or_ref_addr_type,
                   uint64_t language_flags);

  DISALLOW_COPY_AND_ASSIGN(ValueObjectChild);
};

} // namespace lldb_private

#endif // liblldb_ValueObjectChild_h_
