//===-- SBTypeFilter.cpp ------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeFilter.h"

#include "lldb/API/SBStream.h"

#include "lldb/DataFormatters/DataVisualization.h"

using namespace lldb;
using namespace lldb_private;

SBTypeFilter::SBTypeFilter() : m_opaque_sp() {}

SBTypeFilter::SBTypeFilter(uint32_t options)
    : m_opaque_sp(TypeFilterImplSP(new TypeFilterImpl(options))) {}

SBTypeFilter::SBTypeFilter(const lldb::SBTypeFilter &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {}

SBTypeFilter::~SBTypeFilter() {}

bool SBTypeFilter::IsValid() const { return m_opaque_sp.get() != NULL; }

uint32_t SBTypeFilter::GetOptions() {
  if (IsValid())
    return m_opaque_sp->GetOptions();
  return 0;
}

void SBTypeFilter::SetOptions(uint32_t value) {
  if (CopyOnWrite_Impl())
    m_opaque_sp->SetOptions(value);
}

bool SBTypeFilter::GetDescription(lldb::SBStream &description,
                                  lldb::DescriptionLevel description_level) {
  if (!IsValid())
    return false;
  else {
    description.Printf("%s\n", m_opaque_sp->GetDescription().c_str());
    return true;
  }
}

void SBTypeFilter::Clear() {
  if (CopyOnWrite_Impl())
    m_opaque_sp->Clear();
}

uint32_t SBTypeFilter::GetNumberOfExpressionPaths() {
  if (IsValid())
    return m_opaque_sp->GetCount();
  return 0;
}

const char *SBTypeFilter::GetExpressionPathAtIndex(uint32_t i) {
  if (IsValid()) {
    const char *item = m_opaque_sp->GetExpressionPathAtIndex(i);
    if (item && *item == '.')
      item++;
    return item;
  }
  return NULL;
}

bool SBTypeFilter::ReplaceExpressionPathAtIndex(uint32_t i, const char *item) {
  if (CopyOnWrite_Impl())
    return m_opaque_sp->SetExpressionPathAtIndex(i, item);
  else
    return false;
}

void SBTypeFilter::AppendExpressionPath(const char *item) {
  if (CopyOnWrite_Impl())
    m_opaque_sp->AddExpressionPath(item);
}

lldb::SBTypeFilter &SBTypeFilter::operator=(const lldb::SBTypeFilter &rhs) {
  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeFilter::operator==(lldb::SBTypeFilter &rhs) {
  if (!IsValid())
    return !rhs.IsValid();

  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeFilter::IsEqualTo(lldb::SBTypeFilter &rhs) {
  if (!IsValid())
    return !rhs.IsValid();

  if (GetNumberOfExpressionPaths() != rhs.GetNumberOfExpressionPaths())
    return false;

  for (uint32_t j = 0; j < GetNumberOfExpressionPaths(); j++)
    if (strcmp(GetExpressionPathAtIndex(j), rhs.GetExpressionPathAtIndex(j)) !=
        0)
      return false;

  return GetOptions() == rhs.GetOptions();
}

bool SBTypeFilter::operator!=(lldb::SBTypeFilter &rhs) {
  if (!IsValid())
    return !rhs.IsValid();

  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::TypeFilterImplSP SBTypeFilter::GetSP() { return m_opaque_sp; }

void SBTypeFilter::SetSP(const lldb::TypeFilterImplSP &typefilter_impl_sp) {
  m_opaque_sp = typefilter_impl_sp;
}

SBTypeFilter::SBTypeFilter(const lldb::TypeFilterImplSP &typefilter_impl_sp)
    : m_opaque_sp(typefilter_impl_sp) {}

bool SBTypeFilter::CopyOnWrite_Impl() {
  if (!IsValid())
    return false;
  if (m_opaque_sp.unique())
    return true;

  TypeFilterImplSP new_sp(new TypeFilterImpl(GetOptions()));

  for (uint32_t j = 0; j < GetNumberOfExpressionPaths(); j++)
    new_sp->AddExpressionPath(GetExpressionPathAtIndex(j));

  SetSP(new_sp);

  return true;
}
