//===-- SBThreadCollection.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBThreadCollection.h"
#include "lldb/API/SBThread.h"
#include "lldb/Target/ThreadList.h"

using namespace lldb;
using namespace lldb_private;

SBThreadCollection::SBThreadCollection() : m_opaque_sp() {}

SBThreadCollection::SBThreadCollection(const SBThreadCollection &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {}

const SBThreadCollection &SBThreadCollection::
operator=(const SBThreadCollection &rhs) {
  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBThreadCollection::SBThreadCollection(const ThreadCollectionSP &threads)
    : m_opaque_sp(threads) {}

SBThreadCollection::~SBThreadCollection() {}

void SBThreadCollection::SetOpaque(const lldb::ThreadCollectionSP &threads) {
  m_opaque_sp = threads;
}

lldb_private::ThreadCollection *SBThreadCollection::get() const {
  return m_opaque_sp.get();
}

lldb_private::ThreadCollection *SBThreadCollection::operator->() const {
  return m_opaque_sp.operator->();
}

lldb::ThreadCollectionSP &SBThreadCollection::operator*() {
  return m_opaque_sp;
}

const lldb::ThreadCollectionSP &SBThreadCollection::operator*() const {
  return m_opaque_sp;
}

bool SBThreadCollection::IsValid() const { return m_opaque_sp.get() != NULL; }

size_t SBThreadCollection::GetSize() {
  if (m_opaque_sp)
    return m_opaque_sp->GetSize();
  return 0;
}

SBThread SBThreadCollection::GetThreadAtIndex(size_t idx) {
  SBThread thread;
  if (m_opaque_sp && idx < m_opaque_sp->GetSize())
    thread = m_opaque_sp->GetThreadAtIndex(idx);
  return thread;
}
