//===-- NameToDIE.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NameToDIE.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

#include "DWARFDebugInfo.h"
#include "DWARFDebugInfoEntry.h"
#include "SymbolFileDWARF.h"

using namespace lldb;
using namespace lldb_private;

void NameToDIE::Finalize() {
  m_map.Sort();
  m_map.SizeToFit();
}

void NameToDIE::Insert(const ConstString &name, const DIERef &die_ref) {
  m_map.Append(name, die_ref);
}

size_t NameToDIE::Find(const ConstString &name, DIEArray &info_array) const {
  return m_map.GetValues(name, info_array);
}

size_t NameToDIE::Find(const RegularExpression &regex,
                       DIEArray &info_array) const {
  return m_map.GetValues(regex, info_array);
}

size_t NameToDIE::FindAllEntriesForCompileUnit(dw_offset_t cu_offset,
                                               DIEArray &info_array) const {
  const size_t initial_size = info_array.size();
  const uint32_t size = m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    const DIERef &die_ref = m_map.GetValueAtIndexUnchecked(i);
    if (cu_offset == die_ref.cu_offset)
      info_array.push_back(die_ref);
  }
  return info_array.size() - initial_size;
}

void NameToDIE::Dump(Stream *s) {
  const uint32_t size = m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    ConstString cstr = m_map.GetCStringAtIndex(i);
    const DIERef &die_ref = m_map.GetValueAtIndexUnchecked(i);
    s->Printf("%p: {0x%8.8x/0x%8.8x} \"%s\"\n", (const void *)cstr.GetCString(),
              die_ref.cu_offset, die_ref.die_offset, cstr.GetCString());
  }
}

void NameToDIE::ForEach(
    std::function<bool(ConstString name, const DIERef &die_ref)> const
        &callback) const {
  const uint32_t size = m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    if (!callback(m_map.GetCStringAtIndexUnchecked(i),
                  m_map.GetValueAtIndexUnchecked(i)))
      break;
  }
}

void NameToDIE::Append(const NameToDIE &other) {
  const uint32_t size = other.m_map.GetSize();
  for (uint32_t i = 0; i < size; ++i) {
    m_map.Append(other.m_map.GetCStringAtIndexUnchecked(i),
                 other.m_map.GetValueAtIndexUnchecked(i));
  }
}
