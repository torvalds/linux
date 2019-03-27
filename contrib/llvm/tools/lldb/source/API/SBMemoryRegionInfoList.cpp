//===-- SBMemoryRegionInfoList.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBMemoryRegionInfoList.h"
#include "lldb/API/SBMemoryRegionInfo.h"
#include "lldb/API/SBStream.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/Log.h"

#include <vector>

using namespace lldb;
using namespace lldb_private;

class MemoryRegionInfoListImpl {
public:
  MemoryRegionInfoListImpl() : m_regions() {}

  MemoryRegionInfoListImpl(const MemoryRegionInfoListImpl &rhs)
      : m_regions(rhs.m_regions) {}

  MemoryRegionInfoListImpl &operator=(const MemoryRegionInfoListImpl &rhs) {
    if (this == &rhs)
      return *this;
    m_regions = rhs.m_regions;
    return *this;
  }

  size_t GetSize() const { return m_regions.size(); }

  void Reserve(size_t capacity) { return m_regions.reserve(capacity); }

  void Append(const MemoryRegionInfo &sb_region) {
    m_regions.push_back(sb_region);
  }

  void Append(const MemoryRegionInfoListImpl &list) {
    Reserve(GetSize() + list.GetSize());

    for (const auto &val : list.m_regions)
      Append(val);
  }

  void Clear() { m_regions.clear(); }

  bool GetMemoryRegionInfoAtIndex(size_t index,
                                  MemoryRegionInfo &region_info) {
    if (index >= GetSize())
      return false;
    region_info = m_regions[index];
    return true;
  }

  MemoryRegionInfos &Ref() { return m_regions; }

  const MemoryRegionInfos &Ref() const { return m_regions; }

private:
  MemoryRegionInfos m_regions;
};

MemoryRegionInfos &SBMemoryRegionInfoList::ref() {
  return m_opaque_ap->Ref();
}

const MemoryRegionInfos &SBMemoryRegionInfoList::ref() const {
  return m_opaque_ap->Ref();
}

SBMemoryRegionInfoList::SBMemoryRegionInfoList()
    : m_opaque_ap(new MemoryRegionInfoListImpl()) {}

SBMemoryRegionInfoList::SBMemoryRegionInfoList(
    const SBMemoryRegionInfoList &rhs)
    : m_opaque_ap(new MemoryRegionInfoListImpl(*rhs.m_opaque_ap)) {}

SBMemoryRegionInfoList::~SBMemoryRegionInfoList() {}

const SBMemoryRegionInfoList &SBMemoryRegionInfoList::
operator=(const SBMemoryRegionInfoList &rhs) {
  if (this != &rhs) {
    *m_opaque_ap = *rhs.m_opaque_ap;
  }
  return *this;
}

uint32_t SBMemoryRegionInfoList::GetSize() const {
  return m_opaque_ap->GetSize();
}

bool SBMemoryRegionInfoList::GetMemoryRegionAtIndex(
    uint32_t idx, SBMemoryRegionInfo &region_info) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  bool result = m_opaque_ap->GetMemoryRegionInfoAtIndex(idx, region_info.ref());

  if (log) {
    SBStream sstr;
    region_info.GetDescription(sstr);
    log->Printf("SBMemoryRegionInfoList::GetMemoryRegionAtIndex (this.ap=%p, "
                "idx=%d) => SBMemoryRegionInfo (this.ap=%p, '%s')",
                static_cast<void *>(m_opaque_ap.get()), idx,
                static_cast<void *>(region_info.m_opaque_ap.get()),
                sstr.GetData());
  }

  return result;
}

void SBMemoryRegionInfoList::Clear() { m_opaque_ap->Clear(); }

void SBMemoryRegionInfoList::Append(SBMemoryRegionInfo &sb_region) {
  m_opaque_ap->Append(sb_region.ref());
}

void SBMemoryRegionInfoList::Append(SBMemoryRegionInfoList &sb_region_list) {
  m_opaque_ap->Append(*sb_region_list);
}

const MemoryRegionInfoListImpl *SBMemoryRegionInfoList::operator->() const {
  return m_opaque_ap.get();
}

const MemoryRegionInfoListImpl &SBMemoryRegionInfoList::operator*() const {
  assert(m_opaque_ap.get());
  return *m_opaque_ap;
}
