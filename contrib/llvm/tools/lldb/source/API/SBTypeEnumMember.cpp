//===-- SBTypeEnumMember.cpp ---------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeEnumMember.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBType.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

SBTypeEnumMember::SBTypeEnumMember() : m_opaque_sp() {}

SBTypeEnumMember::~SBTypeEnumMember() {}
SBTypeEnumMember::SBTypeEnumMember(
    const lldb::TypeEnumMemberImplSP &enum_member_sp)
    : m_opaque_sp(enum_member_sp) {}

SBTypeEnumMember::SBTypeEnumMember(const SBTypeEnumMember &rhs)
    : m_opaque_sp() {
  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_sp.reset(new TypeEnumMemberImpl(rhs.ref()));
  }
}

SBTypeEnumMember &SBTypeEnumMember::operator=(const SBTypeEnumMember &rhs) {
  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_sp.reset(new TypeEnumMemberImpl(rhs.ref()));
  }
  return *this;
}

bool SBTypeEnumMember::IsValid() const { return m_opaque_sp.get(); }

const char *SBTypeEnumMember::GetName() {
  if (m_opaque_sp.get())
    return m_opaque_sp->GetName().GetCString();
  return NULL;
}

int64_t SBTypeEnumMember::GetValueAsSigned() {
  if (m_opaque_sp.get())
    return m_opaque_sp->GetValueAsSigned();
  return 0;
}

uint64_t SBTypeEnumMember::GetValueAsUnsigned() {
  if (m_opaque_sp.get())
    return m_opaque_sp->GetValueAsUnsigned();
  return 0;
}

SBType SBTypeEnumMember::GetType() {
  SBType sb_type;
  if (m_opaque_sp.get()) {
    sb_type.SetSP(m_opaque_sp->GetIntegerType());
  }
  return sb_type;
}

void SBTypeEnumMember::reset(TypeEnumMemberImpl *type_member_impl) {
  m_opaque_sp.reset(type_member_impl);
}

TypeEnumMemberImpl &SBTypeEnumMember::ref() {
  if (m_opaque_sp.get() == NULL)
    m_opaque_sp.reset(new TypeEnumMemberImpl());
  return *m_opaque_sp.get();
}

const TypeEnumMemberImpl &SBTypeEnumMember::ref() const {
  return *m_opaque_sp.get();
}

SBTypeEnumMemberList::SBTypeEnumMemberList()
    : m_opaque_ap(new TypeEnumMemberListImpl()) {}

SBTypeEnumMemberList::SBTypeEnumMemberList(const SBTypeEnumMemberList &rhs)
    : m_opaque_ap(new TypeEnumMemberListImpl()) {
  for (uint32_t i = 0,
                rhs_size = const_cast<SBTypeEnumMemberList &>(rhs).GetSize();
       i < rhs_size; i++)
    Append(const_cast<SBTypeEnumMemberList &>(rhs).GetTypeEnumMemberAtIndex(i));
}

bool SBTypeEnumMemberList::IsValid() { return (m_opaque_ap != NULL); }

SBTypeEnumMemberList &SBTypeEnumMemberList::
operator=(const SBTypeEnumMemberList &rhs) {
  if (this != &rhs) {
    m_opaque_ap.reset(new TypeEnumMemberListImpl());
    for (uint32_t i = 0,
                  rhs_size = const_cast<SBTypeEnumMemberList &>(rhs).GetSize();
         i < rhs_size; i++)
      Append(
          const_cast<SBTypeEnumMemberList &>(rhs).GetTypeEnumMemberAtIndex(i));
  }
  return *this;
}

void SBTypeEnumMemberList::Append(SBTypeEnumMember enum_member) {
  if (enum_member.IsValid())
    m_opaque_ap->Append(enum_member.m_opaque_sp);
}

SBTypeEnumMember
SBTypeEnumMemberList::GetTypeEnumMemberAtIndex(uint32_t index) {
  if (m_opaque_ap)
    return SBTypeEnumMember(m_opaque_ap->GetTypeEnumMemberAtIndex(index));
  return SBTypeEnumMember();
}

uint32_t SBTypeEnumMemberList::GetSize() { return m_opaque_ap->GetSize(); }

SBTypeEnumMemberList::~SBTypeEnumMemberList() {}

bool SBTypeEnumMember::GetDescription(
    lldb::SBStream &description, lldb::DescriptionLevel description_level) {
  Stream &strm = description.ref();

  if (m_opaque_sp.get()) {
    if (m_opaque_sp->GetIntegerType()->GetDescription(strm,
                                                      description_level)) {
      strm.Printf(" %s", m_opaque_sp->GetName().GetCString());
    }
  } else {
    strm.PutCString("No value");
  }
  return true;
}
