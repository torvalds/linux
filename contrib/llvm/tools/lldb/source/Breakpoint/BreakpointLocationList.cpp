//===-- BreakpointLocationList.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointLocationList.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"

using namespace lldb;
using namespace lldb_private;

BreakpointLocationList::BreakpointLocationList(Breakpoint &owner)
    : m_owner(owner), m_locations(), m_address_to_location(), m_mutex(),
      m_next_id(0), m_new_location_recorder(nullptr) {}

BreakpointLocationList::~BreakpointLocationList() = default;

BreakpointLocationSP
BreakpointLocationList::Create(const Address &addr,
                               bool resolve_indirect_symbols) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  // The location ID is just the size of the location list + 1
  lldb::break_id_t bp_loc_id = ++m_next_id;
  BreakpointLocationSP bp_loc_sp(
      new BreakpointLocation(bp_loc_id, m_owner, addr, LLDB_INVALID_THREAD_ID,
                             m_owner.IsHardware(), resolve_indirect_symbols));
  m_locations.push_back(bp_loc_sp);
  m_address_to_location[addr] = bp_loc_sp;
  return bp_loc_sp;
}

bool BreakpointLocationList::ShouldStop(StoppointCallbackContext *context,
                                        lldb::break_id_t break_id) {
  BreakpointLocationSP bp = FindByID(break_id);
  if (bp) {
    // Let the BreakpointLocation decide if it should stop here (could not have
    // reached it's target hit count yet, or it could have a callback that
    // decided it shouldn't stop (shared library loads/unloads).
    return bp->ShouldStop(context);
  }
  // We should stop here since this BreakpointLocation isn't valid anymore or
  // it doesn't exist.
  return true;
}

lldb::break_id_t BreakpointLocationList::FindIDByAddress(const Address &addr) {
  BreakpointLocationSP bp_loc_sp = FindByAddress(addr);
  if (bp_loc_sp) {
    return bp_loc_sp->GetID();
  }
  return LLDB_INVALID_BREAK_ID;
}

static bool Compare(BreakpointLocationSP lhs, lldb::break_id_t val) {
  return lhs->GetID() < val;
}

BreakpointLocationSP
BreakpointLocationList::FindByID(lldb::break_id_t break_id) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  collection::const_iterator end = m_locations.end();
  collection::const_iterator pos =
      std::lower_bound(m_locations.begin(), end, break_id, Compare);
  if (pos != end && (*pos)->GetID() == break_id)
    return *(pos);
  else
    return BreakpointLocationSP();
}

size_t BreakpointLocationList::FindInModule(
    Module *module, BreakpointLocationCollection &bp_loc_list) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  const size_t orig_size = bp_loc_list.GetSize();
  collection::iterator pos, end = m_locations.end();

  for (pos = m_locations.begin(); pos != end; ++pos) {
    BreakpointLocationSP break_loc = (*pos);
    SectionSP section_sp(break_loc->GetAddress().GetSection());
    if (section_sp && section_sp->GetModule().get() == module) {
      bp_loc_list.Add(break_loc);
    }
  }
  return bp_loc_list.GetSize() - orig_size;
}

const BreakpointLocationSP
BreakpointLocationList::FindByAddress(const Address &addr) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  BreakpointLocationSP bp_loc_sp;
  if (!m_locations.empty()) {
    Address so_addr;

    if (addr.IsSectionOffset()) {
      so_addr = addr;
    } else {
      // Try and resolve as a load address if possible.
      m_owner.GetTarget().GetSectionLoadList().ResolveLoadAddress(
          addr.GetOffset(), so_addr);
      if (!so_addr.IsValid()) {
        // The address didn't resolve, so just set to passed in addr.
        so_addr = addr;
      }
    }

    addr_map::const_iterator pos = m_address_to_location.find(so_addr);
    if (pos != m_address_to_location.end())
      bp_loc_sp = pos->second;
  }

  return bp_loc_sp;
}

void BreakpointLocationList::Dump(Stream *s) const {
  s->Printf("%p: ", static_cast<const void *>(this));
  // s->Indent();
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  s->Printf("BreakpointLocationList with %" PRIu64 " BreakpointLocations:\n",
            (uint64_t)m_locations.size());
  s->IndentMore();
  collection::const_iterator pos, end = m_locations.end();
  for (pos = m_locations.begin(); pos != end; ++pos)
    (*pos).get()->Dump(s);
  s->IndentLess();
}

BreakpointLocationSP BreakpointLocationList::GetByIndex(size_t i) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  BreakpointLocationSP bp_loc_sp;
  if (i < m_locations.size())
    bp_loc_sp = m_locations[i];

  return bp_loc_sp;
}

const BreakpointLocationSP BreakpointLocationList::GetByIndex(size_t i) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  BreakpointLocationSP bp_loc_sp;
  if (i < m_locations.size())
    bp_loc_sp = m_locations[i];

  return bp_loc_sp;
}

void BreakpointLocationList::ClearAllBreakpointSites() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  collection::iterator pos, end = m_locations.end();
  for (pos = m_locations.begin(); pos != end; ++pos)
    (*pos)->ClearBreakpointSite();
}

void BreakpointLocationList::ResolveAllBreakpointSites() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  collection::iterator pos, end = m_locations.end();

  for (pos = m_locations.begin(); pos != end; ++pos) {
    if ((*pos)->IsEnabled())
      (*pos)->ResolveBreakpointSite();
  }
}

uint32_t BreakpointLocationList::GetHitCount() const {
  uint32_t hit_count = 0;
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  collection::const_iterator pos, end = m_locations.end();
  for (pos = m_locations.begin(); pos != end; ++pos)
    hit_count += (*pos)->GetHitCount();
  return hit_count;
}

size_t BreakpointLocationList::GetNumResolvedLocations() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  size_t resolve_count = 0;
  collection::const_iterator pos, end = m_locations.end();
  for (pos = m_locations.begin(); pos != end; ++pos) {
    if ((*pos)->IsResolved())
      ++resolve_count;
  }
  return resolve_count;
}

void BreakpointLocationList::GetDescription(Stream *s,
                                            lldb::DescriptionLevel level) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  collection::iterator pos, end = m_locations.end();

  for (pos = m_locations.begin(); pos != end; ++pos) {
    s->Printf(" ");
    (*pos)->GetDescription(s, level);
  }
}

BreakpointLocationSP BreakpointLocationList::AddLocation(
    const Address &addr, bool resolve_indirect_symbols, bool *new_location) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (new_location)
    *new_location = false;
  BreakpointLocationSP bp_loc_sp(FindByAddress(addr));
  if (!bp_loc_sp) {
    bp_loc_sp = Create(addr, resolve_indirect_symbols);
    if (bp_loc_sp) {
      bp_loc_sp->ResolveBreakpointSite();

      if (new_location)
        *new_location = true;
      if (m_new_location_recorder) {
        m_new_location_recorder->Add(bp_loc_sp);
      }
    }
  }
  return bp_loc_sp;
}

void BreakpointLocationList::SwapLocation(
    BreakpointLocationSP to_location_sp,
    BreakpointLocationSP from_location_sp) {
  if (!from_location_sp || !to_location_sp)
    return;

  m_address_to_location.erase(to_location_sp->GetAddress());
  to_location_sp->SwapLocation(from_location_sp);
  RemoveLocation(from_location_sp);
  m_address_to_location[to_location_sp->GetAddress()] = to_location_sp;
  to_location_sp->ResolveBreakpointSite();
}

bool BreakpointLocationList::RemoveLocation(
    const lldb::BreakpointLocationSP &bp_loc_sp) {
  if (bp_loc_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);

    m_address_to_location.erase(bp_loc_sp->GetAddress());

    size_t num_locations = m_locations.size();
    for (size_t idx = 0; idx < num_locations; idx++) {
      if (m_locations[idx].get() == bp_loc_sp.get()) {
        RemoveLocationByIndex(idx);
        return true;
      }
    }
  }
  return false;
}

void BreakpointLocationList::RemoveLocationByIndex(size_t idx) {
  assert (idx < m_locations.size());
  m_address_to_location.erase(m_locations[idx]->GetAddress());
  m_locations.erase(m_locations.begin() + idx);
}

void BreakpointLocationList::RemoveInvalidLocations(const ArchSpec &arch) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  size_t idx = 0;
  // Don't cache m_location.size() as it will change since we might remove
  // locations from our vector...
  while (idx < m_locations.size()) {
    BreakpointLocation *bp_loc = m_locations[idx].get();
    if (bp_loc->GetAddress().SectionWasDeleted()) {
      // Section was deleted which means this breakpoint comes from a module
      // that is no longer valid, so we should remove it.
      RemoveLocationByIndex(idx);
      continue;
    }
    if (arch.IsValid()) {
      ModuleSP module_sp(bp_loc->GetAddress().GetModule());
      if (module_sp) {
        if (!arch.IsCompatibleMatch(module_sp->GetArchitecture())) {
          // The breakpoint was in a module whose architecture is no longer
          // compatible with "arch", so we need to remove it
          RemoveLocationByIndex(idx);
          continue;
        }
      }
    }
    // Only increment the index if we didn't remove the locations at index
    // "idx"
    ++idx;
  }
}

void BreakpointLocationList::StartRecordingNewLocations(
    BreakpointLocationCollection &new_locations) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  assert(m_new_location_recorder == nullptr);
  m_new_location_recorder = &new_locations;
}

void BreakpointLocationList::StopRecordingNewLocations() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_new_location_recorder = nullptr;
}

void BreakpointLocationList::Compact() {
  lldb::break_id_t highest_id = 0;

  for (BreakpointLocationSP loc_sp : m_locations) {
    lldb::break_id_t cur_id = loc_sp->GetID();
    if (cur_id > highest_id)
      highest_id = cur_id;
  }
  m_next_id = highest_id;
}
