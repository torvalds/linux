//===-- AddressResolver.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/AddressResolver.h"

#include "lldb/Core/SearchFilter.h"

namespace lldb_private {
class ModuleList;
}

using namespace lldb_private;

//----------------------------------------------------------------------
// AddressResolver:
//----------------------------------------------------------------------
AddressResolver::AddressResolver() {}

AddressResolver::~AddressResolver() {}

void AddressResolver::ResolveAddressInModules(SearchFilter &filter,
                                              ModuleList &modules) {
  filter.SearchInModuleList(*this, modules);
}

void AddressResolver::ResolveAddress(SearchFilter &filter) {
  filter.Search(*this);
}

std::vector<AddressRange> &AddressResolver::GetAddressRanges() {
  return m_address_ranges;
}

size_t AddressResolver::GetNumberOfAddresses() {
  return m_address_ranges.size();
}

AddressRange &AddressResolver::GetAddressRangeAtIndex(size_t idx) {
  return m_address_ranges[idx];
}
