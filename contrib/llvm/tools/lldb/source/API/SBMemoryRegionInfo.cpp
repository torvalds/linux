//===-- SBMemoryRegionInfo.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBStream.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBMemoryRegionInfo::SBMemoryRegionInfo()
    : m_opaque_ap(new MemoryRegionInfo()) {}

SBMemoryRegionInfo::SBMemoryRegionInfo(const MemoryRegionInfo *lldb_object_ptr)
    : m_opaque_ap(new MemoryRegionInfo()) {
  if (lldb_object_ptr)
    ref() = *lldb_object_ptr;
}

SBMemoryRegionInfo::SBMemoryRegionInfo(const SBMemoryRegionInfo &rhs)
    : m_opaque_ap(new MemoryRegionInfo()) {
  ref() = rhs.ref();
}

const SBMemoryRegionInfo &SBMemoryRegionInfo::
operator=(const SBMemoryRegionInfo &rhs) {
  if (this != &rhs) {
    ref() = rhs.ref();
  }
  return *this;
}

SBMemoryRegionInfo::~SBMemoryRegionInfo() {}

void SBMemoryRegionInfo::Clear() { m_opaque_ap->Clear(); }

bool SBMemoryRegionInfo::operator==(const SBMemoryRegionInfo &rhs) const {
  return ref() == rhs.ref();
}

bool SBMemoryRegionInfo::operator!=(const SBMemoryRegionInfo &rhs) const {
  return ref() != rhs.ref();
}

MemoryRegionInfo &SBMemoryRegionInfo::ref() { return *m_opaque_ap; }

const MemoryRegionInfo &SBMemoryRegionInfo::ref() const { return *m_opaque_ap; }

lldb::addr_t SBMemoryRegionInfo::GetRegionBase() {
  return m_opaque_ap->GetRange().GetRangeBase();
}

lldb::addr_t SBMemoryRegionInfo::GetRegionEnd() {
  return m_opaque_ap->GetRange().GetRangeEnd();
}

bool SBMemoryRegionInfo::IsReadable() {
  return m_opaque_ap->GetReadable() == MemoryRegionInfo::eYes;
}

bool SBMemoryRegionInfo::IsWritable() {
  return m_opaque_ap->GetWritable() == MemoryRegionInfo::eYes;
}

bool SBMemoryRegionInfo::IsExecutable() {
  return m_opaque_ap->GetExecutable() == MemoryRegionInfo::eYes;
}

bool SBMemoryRegionInfo::IsMapped() {
  return m_opaque_ap->GetMapped() == MemoryRegionInfo::eYes;
}

const char *SBMemoryRegionInfo::GetName() {
  return m_opaque_ap->GetName().AsCString();
}

bool SBMemoryRegionInfo::GetDescription(SBStream &description) {
  Stream &strm = description.ref();
  const addr_t load_addr = m_opaque_ap->GetRange().base;

  strm.Printf("[0x%16.16" PRIx64 "-0x%16.16" PRIx64 " ", load_addr,
              load_addr + m_opaque_ap->GetRange().size);
  strm.Printf(m_opaque_ap->GetReadable() ? "R" : "-");
  strm.Printf(m_opaque_ap->GetWritable() ? "W" : "-");
  strm.Printf(m_opaque_ap->GetExecutable() ? "X" : "-");
  strm.Printf("]");

  return true;
}
