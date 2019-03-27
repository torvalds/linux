//===-- ValueObjectRegister.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObjectRegister_h_
#define liblldb_ValueObjectRegister_h_

#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-types.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class Status;
}
namespace lldb_private {
class ExecutionContextScope;
}
namespace lldb_private {
class Scalar;
}
namespace lldb_private {
class Stream;
}

namespace lldb_private {

//----------------------------------------------------------------------
// A ValueObject that contains a root variable that may or may not
// have children.
//----------------------------------------------------------------------
class ValueObjectRegisterContext : public ValueObject {
public:
  ~ValueObjectRegisterContext() override;

  uint64_t GetByteSize() override;

  lldb::ValueType GetValueType() const override {
    return lldb::eValueTypeRegisterSet;
  }

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  ConstString GetDisplayTypeName() override;

  size_t CalculateNumChildren(uint32_t max) override;

  ValueObject *CreateChildAtIndex(size_t idx, bool synthetic_array_member,
                                  int32_t synthetic_index) override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  lldb::RegisterContextSP m_reg_ctx_sp;

private:
  ValueObjectRegisterContext(ValueObject &parent,
                             lldb::RegisterContextSP &reg_ctx_sp);
  //------------------------------------------------------------------
  // For ValueObject only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(ValueObjectRegisterContext);
};

class ValueObjectRegisterSet : public ValueObject {
public:
  ~ValueObjectRegisterSet() override;

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    lldb::RegisterContextSP &reg_ctx_sp,
                                    uint32_t set_idx);

  uint64_t GetByteSize() override;

  lldb::ValueType GetValueType() const override {
    return lldb::eValueTypeRegisterSet;
  }

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  size_t CalculateNumChildren(uint32_t max) override;

  ValueObject *CreateChildAtIndex(size_t idx, bool synthetic_array_member,
                                  int32_t synthetic_index) override;

  lldb::ValueObjectSP GetChildMemberWithName(const ConstString &name,
                                             bool can_create) override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  lldb::RegisterContextSP m_reg_ctx_sp;
  const RegisterSet *m_reg_set;
  uint32_t m_reg_set_idx;

private:
  friend class ValueObjectRegisterContext;

  ValueObjectRegisterSet(ExecutionContextScope *exe_scope,
                         lldb::RegisterContextSP &reg_ctx_sp, uint32_t set_idx);

  //------------------------------------------------------------------
  // For ValueObject only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(ValueObjectRegisterSet);
};

class ValueObjectRegister : public ValueObject {
public:
  ~ValueObjectRegister() override;

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    lldb::RegisterContextSP &reg_ctx_sp,
                                    uint32_t reg_num);

  uint64_t GetByteSize() override;

  lldb::ValueType GetValueType() const override {
    return lldb::eValueTypeRegister;
  }

  ConstString GetTypeName() override;

  size_t CalculateNumChildren(uint32_t max) override;

  bool SetValueFromCString(const char *value_str, Status &error) override;

  bool SetData(DataExtractor &data, Status &error) override;

  bool ResolveValue(Scalar &scalar) override;

  void
  GetExpressionPath(Stream &s, bool qualify_cxx_base_classes,
                    GetExpressionPathFormat epformat =
                        eGetExpressionPathFormatDereferencePointers) override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  lldb::RegisterContextSP m_reg_ctx_sp;
  RegisterInfo m_reg_info;
  RegisterValue m_reg_value;
  ConstString m_type_name;
  CompilerType m_compiler_type;

private:
  void ConstructObject(uint32_t reg_num);

  friend class ValueObjectRegisterSet;

  ValueObjectRegister(ValueObject &parent, lldb::RegisterContextSP &reg_ctx_sp,
                      uint32_t reg_num);
  ValueObjectRegister(ExecutionContextScope *exe_scope,
                      lldb::RegisterContextSP &reg_ctx_sp, uint32_t reg_num);

  //------------------------------------------------------------------
  // For ValueObject only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(ValueObjectRegister);
};

} // namespace lldb_private

#endif // liblldb_ValueObjectRegister_h_
