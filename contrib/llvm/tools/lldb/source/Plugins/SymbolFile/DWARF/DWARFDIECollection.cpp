//===-- DWARFDIECollection.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDIECollection.h"

#include <algorithm>

#include "lldb/Utility/Stream.h"

using namespace lldb_private;
using namespace std;

void DWARFDIECollection::Append(const DWARFDIE &die) { m_dies.push_back(die); }

DWARFDIE
DWARFDIECollection::GetDIEAtIndex(uint32_t idx) const {
  if (idx < m_dies.size())
    return m_dies[idx];
  return DWARFDIE();
}

size_t DWARFDIECollection::Size() const { return m_dies.size(); }

void DWARFDIECollection::Dump(Stream *s, const char *title) const {
  if (title && title[0] != '\0')
    s->Printf("%s\n", title);
  for (const auto &die : m_dies)
    s->Printf("0x%8.8x\n", die.GetOffset());
}
