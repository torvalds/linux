//===-- SBTypeNameSpecifier.cpp ------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeNameSpecifier.h"

#include "lldb/API/SBStream.h"
#include "lldb/API/SBType.h"

#include "lldb/DataFormatters/DataVisualization.h"

using namespace lldb;
using namespace lldb_private;

SBTypeNameSpecifier::SBTypeNameSpecifier() : m_opaque_sp() {}

SBTypeNameSpecifier::SBTypeNameSpecifier(const char *name, bool is_regex)
    : m_opaque_sp(new TypeNameSpecifierImpl(name, is_regex)) {
  if (name == NULL || (*name) == 0)
    m_opaque_sp.reset();
}

SBTypeNameSpecifier::SBTypeNameSpecifier(SBType type) : m_opaque_sp() {
  if (type.IsValid())
    m_opaque_sp = TypeNameSpecifierImplSP(
        new TypeNameSpecifierImpl(type.m_opaque_sp->GetCompilerType(true)));
}

SBTypeNameSpecifier::SBTypeNameSpecifier(const lldb::SBTypeNameSpecifier &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {}

SBTypeNameSpecifier::~SBTypeNameSpecifier() {}

bool SBTypeNameSpecifier::IsValid() const { return m_opaque_sp.get() != NULL; }

const char *SBTypeNameSpecifier::GetName() {
  if (!IsValid())
    return NULL;

  return m_opaque_sp->GetName();
}

SBType SBTypeNameSpecifier::GetType() {
  if (!IsValid())
    return SBType();
  lldb_private::CompilerType c_type = m_opaque_sp->GetCompilerType();
  if (c_type.IsValid())
    return SBType(c_type);
  return SBType();
}

bool SBTypeNameSpecifier::IsRegex() {
  if (!IsValid())
    return false;

  return m_opaque_sp->IsRegex();
}

bool SBTypeNameSpecifier::GetDescription(
    lldb::SBStream &description, lldb::DescriptionLevel description_level) {
  if (!IsValid())
    return false;
  description.Printf("SBTypeNameSpecifier(%s,%s)", GetName(),
                     IsRegex() ? "regex" : "plain");
  return true;
}

lldb::SBTypeNameSpecifier &SBTypeNameSpecifier::
operator=(const lldb::SBTypeNameSpecifier &rhs) {
  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeNameSpecifier::operator==(lldb::SBTypeNameSpecifier &rhs) {
  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeNameSpecifier::IsEqualTo(lldb::SBTypeNameSpecifier &rhs) {
  if (!IsValid())
    return !rhs.IsValid();

  if (IsRegex() != rhs.IsRegex())
    return false;
  if (GetName() == NULL || rhs.GetName() == NULL)
    return false;

  return (strcmp(GetName(), rhs.GetName()) == 0);
}

bool SBTypeNameSpecifier::operator!=(lldb::SBTypeNameSpecifier &rhs) {
  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::TypeNameSpecifierImplSP SBTypeNameSpecifier::GetSP() {
  return m_opaque_sp;
}

void SBTypeNameSpecifier::SetSP(
    const lldb::TypeNameSpecifierImplSP &type_namespec_sp) {
  m_opaque_sp = type_namespec_sp;
}

SBTypeNameSpecifier::SBTypeNameSpecifier(
    const lldb::TypeNameSpecifierImplSP &type_namespec_sp)
    : m_opaque_sp(type_namespec_sp) {}
