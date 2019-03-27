//===-- SBType.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBType.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTypeEnumMember.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/APSInt.h"

using namespace lldb;
using namespace lldb_private;

SBType::SBType() : m_opaque_sp() {}

SBType::SBType(const CompilerType &type)
    : m_opaque_sp(new TypeImpl(
          CompilerType(type.GetTypeSystem(), type.GetOpaqueQualType()))) {}

SBType::SBType(const lldb::TypeSP &type_sp)
    : m_opaque_sp(new TypeImpl(type_sp)) {}

SBType::SBType(const lldb::TypeImplSP &type_impl_sp)
    : m_opaque_sp(type_impl_sp) {}

SBType::SBType(const SBType &rhs) : m_opaque_sp() {
  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
}

// SBType::SBType (TypeImpl* impl) :
//    m_opaque_ap(impl)
//{}
//
bool SBType::operator==(SBType &rhs) {
  if (!IsValid())
    return !rhs.IsValid();

  if (!rhs.IsValid())
    return false;

  return *m_opaque_sp.get() == *rhs.m_opaque_sp.get();
}

bool SBType::operator!=(SBType &rhs) {
  if (!IsValid())
    return rhs.IsValid();

  if (!rhs.IsValid())
    return true;

  return *m_opaque_sp.get() != *rhs.m_opaque_sp.get();
}

lldb::TypeImplSP SBType::GetSP() { return m_opaque_sp; }

void SBType::SetSP(const lldb::TypeImplSP &type_impl_sp) {
  m_opaque_sp = type_impl_sp;
}

SBType &SBType::operator=(const SBType &rhs) {
  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

SBType::~SBType() {}

TypeImpl &SBType::ref() {
  if (m_opaque_sp.get() == NULL)
    m_opaque_sp.reset(new TypeImpl());
  return *m_opaque_sp;
}

const TypeImpl &SBType::ref() const {
  // "const SBAddress &addr" should already have checked "addr.IsValid()" prior
  // to calling this function. In case you didn't we will assert and die to let
  // you know.
  assert(m_opaque_sp.get());
  return *m_opaque_sp;
}

bool SBType::IsValid() const {
  if (m_opaque_sp.get() == NULL)
    return false;

  return m_opaque_sp->IsValid();
}

uint64_t SBType::GetByteSize() {
  if (IsValid())
    if (llvm::Optional<uint64_t> size =
            m_opaque_sp->GetCompilerType(false).GetByteSize(nullptr))
      return *size;
  return 0;
}

bool SBType::IsPointerType() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsPointerType();
}

bool SBType::IsArrayType() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsArrayType(nullptr, nullptr,
                                                        nullptr);
}

bool SBType::IsVectorType() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsVectorType(nullptr, nullptr);
}

bool SBType::IsReferenceType() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsReferenceType();
}

SBType SBType::GetPointerType() {
  if (!IsValid())
    return SBType();

  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetPointerType())));
}

SBType SBType::GetPointeeType() {
  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetPointeeType())));
}

SBType SBType::GetReferenceType() {
  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetReferenceType())));
}

SBType SBType::GetTypedefedType() {
  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetTypedefedType())));
}

SBType SBType::GetDereferencedType() {
  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetDereferencedType())));
}

SBType SBType::GetArrayElementType() {
  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(
      new TypeImpl(m_opaque_sp->GetCompilerType(true).GetArrayElementType())));
}

SBType SBType::GetArrayType(uint64_t size) {
  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(
      new TypeImpl(m_opaque_sp->GetCompilerType(true).GetArrayType(size))));
}

SBType SBType::GetVectorElementType() {
  SBType type_sb;
  if (IsValid()) {
    CompilerType vector_element_type;
    if (m_opaque_sp->GetCompilerType(true).IsVectorType(&vector_element_type,
                                                        nullptr))
      type_sb.SetSP(TypeImplSP(new TypeImpl(vector_element_type)));
  }
  return type_sb;
}

bool SBType::IsFunctionType() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsFunctionType();
}

bool SBType::IsPolymorphicClass() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsPolymorphicClass();
}

bool SBType::IsTypedefType() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsTypedefType();
}

bool SBType::IsAnonymousType() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsAnonymousType();
}

lldb::SBType SBType::GetFunctionReturnType() {
  if (IsValid()) {
    CompilerType return_type(
        m_opaque_sp->GetCompilerType(true).GetFunctionReturnType());
    if (return_type.IsValid())
      return SBType(return_type);
  }
  return lldb::SBType();
}

lldb::SBTypeList SBType::GetFunctionArgumentTypes() {
  SBTypeList sb_type_list;
  if (IsValid()) {
    CompilerType func_type(m_opaque_sp->GetCompilerType(true));
    size_t count = func_type.GetNumberOfFunctionArguments();
    for (size_t i = 0; i < count; i++) {
      sb_type_list.Append(SBType(func_type.GetFunctionArgumentAtIndex(i)));
    }
  }
  return sb_type_list;
}

uint32_t SBType::GetNumberOfMemberFunctions() {
  if (IsValid()) {
    return m_opaque_sp->GetCompilerType(true).GetNumMemberFunctions();
  }
  return 0;
}

lldb::SBTypeMemberFunction SBType::GetMemberFunctionAtIndex(uint32_t idx) {
  SBTypeMemberFunction sb_func_type;
  if (IsValid())
    sb_func_type.reset(new TypeMemberFunctionImpl(
        m_opaque_sp->GetCompilerType(true).GetMemberFunctionAtIndex(idx)));
  return sb_func_type;
}

lldb::SBType SBType::GetUnqualifiedType() {
  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetUnqualifiedType())));
}

lldb::SBType SBType::GetCanonicalType() {
  if (IsValid())
    return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetCanonicalType())));
  return SBType();
}

lldb::BasicType SBType::GetBasicType() {
  if (IsValid())
    return m_opaque_sp->GetCompilerType(false).GetBasicTypeEnumeration();
  return eBasicTypeInvalid;
}

SBType SBType::GetBasicType(lldb::BasicType basic_type) {
  if (IsValid() && m_opaque_sp->IsValid())
    return SBType(
        m_opaque_sp->GetTypeSystem(false)->GetBasicTypeFromAST(basic_type));
  return SBType();
}

uint32_t SBType::GetNumberOfDirectBaseClasses() {
  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetNumDirectBaseClasses();
  return 0;
}

uint32_t SBType::GetNumberOfVirtualBaseClasses() {
  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetNumVirtualBaseClasses();
  return 0;
}

uint32_t SBType::GetNumberOfFields() {
  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetNumFields();
  return 0;
}

bool SBType::GetDescription(SBStream &description,
                            lldb::DescriptionLevel description_level) {
  Stream &strm = description.ref();

  if (m_opaque_sp) {
    m_opaque_sp->GetDescription(strm, description_level);
  } else
    strm.PutCString("No value");

  return true;
}

SBTypeMember SBType::GetDirectBaseClassAtIndex(uint32_t idx) {
  SBTypeMember sb_type_member;
  if (IsValid()) {
    uint32_t bit_offset = 0;
    CompilerType base_class_type =
        m_opaque_sp->GetCompilerType(true).GetDirectBaseClassAtIndex(
            idx, &bit_offset);
    if (base_class_type.IsValid())
      sb_type_member.reset(new TypeMemberImpl(
          TypeImplSP(new TypeImpl(base_class_type)), bit_offset));
  }
  return sb_type_member;
}

SBTypeMember SBType::GetVirtualBaseClassAtIndex(uint32_t idx) {
  SBTypeMember sb_type_member;
  if (IsValid()) {
    uint32_t bit_offset = 0;
    CompilerType base_class_type =
        m_opaque_sp->GetCompilerType(true).GetVirtualBaseClassAtIndex(
            idx, &bit_offset);
    if (base_class_type.IsValid())
      sb_type_member.reset(new TypeMemberImpl(
          TypeImplSP(new TypeImpl(base_class_type)), bit_offset));
  }
  return sb_type_member;
}

SBTypeEnumMemberList SBType::GetEnumMembers() {
  SBTypeEnumMemberList sb_enum_member_list;
  if (IsValid()) {
    CompilerType this_type(m_opaque_sp->GetCompilerType(true));
    if (this_type.IsValid()) {
      this_type.ForEachEnumerator([&sb_enum_member_list](
                                      const CompilerType &integer_type,
                                      const ConstString &name,
                                      const llvm::APSInt &value) -> bool {
        SBTypeEnumMember enum_member(
            lldb::TypeEnumMemberImplSP(new TypeEnumMemberImpl(
                lldb::TypeImplSP(new TypeImpl(integer_type)), name, value)));
        sb_enum_member_list.Append(enum_member);
        return true; // Keep iterating
      });
    }
  }
  return sb_enum_member_list;
}

SBTypeMember SBType::GetFieldAtIndex(uint32_t idx) {
  SBTypeMember sb_type_member;
  if (IsValid()) {
    CompilerType this_type(m_opaque_sp->GetCompilerType(false));
    if (this_type.IsValid()) {
      uint64_t bit_offset = 0;
      uint32_t bitfield_bit_size = 0;
      bool is_bitfield = false;
      std::string name_sstr;
      CompilerType field_type(this_type.GetFieldAtIndex(
          idx, name_sstr, &bit_offset, &bitfield_bit_size, &is_bitfield));
      if (field_type.IsValid()) {
        ConstString name;
        if (!name_sstr.empty())
          name.SetCString(name_sstr.c_str());
        sb_type_member.reset(
            new TypeMemberImpl(TypeImplSP(new TypeImpl(field_type)), bit_offset,
                               name, bitfield_bit_size, is_bitfield));
      }
    }
  }
  return sb_type_member;
}

bool SBType::IsTypeComplete() {
  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(false).IsCompleteType();
}

uint32_t SBType::GetTypeFlags() {
  if (!IsValid())
    return 0;
  return m_opaque_sp->GetCompilerType(true).GetTypeInfo();
}

const char *SBType::GetName() {
  if (!IsValid())
    return "";
  return m_opaque_sp->GetName().GetCString();
}

const char *SBType::GetDisplayTypeName() {
  if (!IsValid())
    return "";
  return m_opaque_sp->GetDisplayTypeName().GetCString();
}

lldb::TypeClass SBType::GetTypeClass() {
  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetTypeClass();
  return lldb::eTypeClassInvalid;
}

uint32_t SBType::GetNumberOfTemplateArguments() {
  if (IsValid())
    return m_opaque_sp->GetCompilerType(false).GetNumTemplateArguments();
  return 0;
}

lldb::SBType SBType::GetTemplateArgumentType(uint32_t idx) {
  if (!IsValid())
    return SBType();

  CompilerType type;
  switch(GetTemplateArgumentKind(idx)) {
    case eTemplateArgumentKindType:
      type = m_opaque_sp->GetCompilerType(false).GetTypeTemplateArgument(idx);
      break;
    case eTemplateArgumentKindIntegral:
      type = m_opaque_sp->GetCompilerType(false)
                 .GetIntegralTemplateArgument(idx)
                 ->type;
      break;
    default:
      break;
  }
  if (type.IsValid())
    return SBType(type);
  return SBType();
}

lldb::TemplateArgumentKind SBType::GetTemplateArgumentKind(uint32_t idx) {
  if (IsValid())
    return m_opaque_sp->GetCompilerType(false).GetTemplateArgumentKind(idx);
  return eTemplateArgumentKindNull;
}

SBTypeList::SBTypeList() : m_opaque_ap(new TypeListImpl()) {}

SBTypeList::SBTypeList(const SBTypeList &rhs)
    : m_opaque_ap(new TypeListImpl()) {
  for (uint32_t i = 0, rhs_size = const_cast<SBTypeList &>(rhs).GetSize();
       i < rhs_size; i++)
    Append(const_cast<SBTypeList &>(rhs).GetTypeAtIndex(i));
}

bool SBTypeList::IsValid() { return (m_opaque_ap != NULL); }

SBTypeList &SBTypeList::operator=(const SBTypeList &rhs) {
  if (this != &rhs) {
    m_opaque_ap.reset(new TypeListImpl());
    for (uint32_t i = 0, rhs_size = const_cast<SBTypeList &>(rhs).GetSize();
         i < rhs_size; i++)
      Append(const_cast<SBTypeList &>(rhs).GetTypeAtIndex(i));
  }
  return *this;
}

void SBTypeList::Append(SBType type) {
  if (type.IsValid())
    m_opaque_ap->Append(type.m_opaque_sp);
}

SBType SBTypeList::GetTypeAtIndex(uint32_t index) {
  if (m_opaque_ap)
    return SBType(m_opaque_ap->GetTypeAtIndex(index));
  return SBType();
}

uint32_t SBTypeList::GetSize() { return m_opaque_ap->GetSize(); }

SBTypeList::~SBTypeList() {}

SBTypeMember::SBTypeMember() : m_opaque_ap() {}

SBTypeMember::~SBTypeMember() {}

SBTypeMember::SBTypeMember(const SBTypeMember &rhs) : m_opaque_ap() {
  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_ap.reset(new TypeMemberImpl(rhs.ref()));
  }
}

lldb::SBTypeMember &SBTypeMember::operator=(const lldb::SBTypeMember &rhs) {
  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_ap.reset(new TypeMemberImpl(rhs.ref()));
  }
  return *this;
}

bool SBTypeMember::IsValid() const { return m_opaque_ap.get(); }

const char *SBTypeMember::GetName() {
  if (m_opaque_ap)
    return m_opaque_ap->GetName().GetCString();
  return NULL;
}

SBType SBTypeMember::GetType() {
  SBType sb_type;
  if (m_opaque_ap) {
    sb_type.SetSP(m_opaque_ap->GetTypeImpl());
  }
  return sb_type;
}

uint64_t SBTypeMember::GetOffsetInBytes() {
  if (m_opaque_ap)
    return m_opaque_ap->GetBitOffset() / 8u;
  return 0;
}

uint64_t SBTypeMember::GetOffsetInBits() {
  if (m_opaque_ap)
    return m_opaque_ap->GetBitOffset();
  return 0;
}

bool SBTypeMember::IsBitfield() {
  if (m_opaque_ap)
    return m_opaque_ap->GetIsBitfield();
  return false;
}

uint32_t SBTypeMember::GetBitfieldSizeInBits() {
  if (m_opaque_ap)
    return m_opaque_ap->GetBitfieldBitSize();
  return 0;
}

bool SBTypeMember::GetDescription(lldb::SBStream &description,
                                  lldb::DescriptionLevel description_level) {
  Stream &strm = description.ref();

  if (m_opaque_ap) {
    const uint32_t bit_offset = m_opaque_ap->GetBitOffset();
    const uint32_t byte_offset = bit_offset / 8u;
    const uint32_t byte_bit_offset = bit_offset % 8u;
    const char *name = m_opaque_ap->GetName().GetCString();
    if (byte_bit_offset)
      strm.Printf("+%u + %u bits: (", byte_offset, byte_bit_offset);
    else
      strm.Printf("+%u: (", byte_offset);

    TypeImplSP type_impl_sp(m_opaque_ap->GetTypeImpl());
    if (type_impl_sp)
      type_impl_sp->GetDescription(strm, description_level);

    strm.Printf(") %s", name);
    if (m_opaque_ap->GetIsBitfield()) {
      const uint32_t bitfield_bit_size = m_opaque_ap->GetBitfieldBitSize();
      strm.Printf(" : %u", bitfield_bit_size);
    }
  } else {
    strm.PutCString("No value");
  }
  return true;
}

void SBTypeMember::reset(TypeMemberImpl *type_member_impl) {
  m_opaque_ap.reset(type_member_impl);
}

TypeMemberImpl &SBTypeMember::ref() {
  if (m_opaque_ap == NULL)
    m_opaque_ap.reset(new TypeMemberImpl());
  return *m_opaque_ap;
}

const TypeMemberImpl &SBTypeMember::ref() const { return *m_opaque_ap; }

SBTypeMemberFunction::SBTypeMemberFunction() : m_opaque_sp() {}

SBTypeMemberFunction::~SBTypeMemberFunction() {}

SBTypeMemberFunction::SBTypeMemberFunction(const SBTypeMemberFunction &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {}

lldb::SBTypeMemberFunction &SBTypeMemberFunction::
operator=(const lldb::SBTypeMemberFunction &rhs) {
  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

bool SBTypeMemberFunction::IsValid() const { return m_opaque_sp.get(); }

const char *SBTypeMemberFunction::GetName() {
  if (m_opaque_sp)
    return m_opaque_sp->GetName().GetCString();
  return NULL;
}

const char *SBTypeMemberFunction::GetDemangledName() {
  if (m_opaque_sp) {
    ConstString mangled_str = m_opaque_sp->GetMangledName();
    if (mangled_str) {
      Mangled mangled(mangled_str, true);
      return mangled.GetDemangledName(mangled.GuessLanguage()).GetCString();
    }
  }
  return NULL;
}

const char *SBTypeMemberFunction::GetMangledName() {
  if (m_opaque_sp)
    return m_opaque_sp->GetMangledName().GetCString();
  return NULL;
}

SBType SBTypeMemberFunction::GetType() {
  SBType sb_type;
  if (m_opaque_sp) {
    sb_type.SetSP(lldb::TypeImplSP(new TypeImpl(m_opaque_sp->GetType())));
  }
  return sb_type;
}

lldb::SBType SBTypeMemberFunction::GetReturnType() {
  SBType sb_type;
  if (m_opaque_sp) {
    sb_type.SetSP(lldb::TypeImplSP(new TypeImpl(m_opaque_sp->GetReturnType())));
  }
  return sb_type;
}

uint32_t SBTypeMemberFunction::GetNumberOfArguments() {
  if (m_opaque_sp)
    return m_opaque_sp->GetNumArguments();
  return 0;
}

lldb::SBType SBTypeMemberFunction::GetArgumentTypeAtIndex(uint32_t i) {
  SBType sb_type;
  if (m_opaque_sp) {
    sb_type.SetSP(
        lldb::TypeImplSP(new TypeImpl(m_opaque_sp->GetArgumentAtIndex(i))));
  }
  return sb_type;
}

lldb::MemberFunctionKind SBTypeMemberFunction::GetKind() {
  if (m_opaque_sp)
    return m_opaque_sp->GetKind();
  return lldb::eMemberFunctionKindUnknown;
}

bool SBTypeMemberFunction::GetDescription(
    lldb::SBStream &description, lldb::DescriptionLevel description_level) {
  Stream &strm = description.ref();

  if (m_opaque_sp)
    return m_opaque_sp->GetDescription(strm);

  return false;
}

void SBTypeMemberFunction::reset(TypeMemberFunctionImpl *type_member_impl) {
  m_opaque_sp.reset(type_member_impl);
}

TypeMemberFunctionImpl &SBTypeMemberFunction::ref() {
  if (!m_opaque_sp)
    m_opaque_sp.reset(new TypeMemberFunctionImpl());
  return *m_opaque_sp.get();
}

const TypeMemberFunctionImpl &SBTypeMemberFunction::ref() const {
  return *m_opaque_sp.get();
}
