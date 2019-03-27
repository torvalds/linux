//===-- NameToDIE.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_NameToDIE_h_
#define SymbolFileDWARF_NameToDIE_h_

#include <functional>

#include "DIERef.h"
#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Core/dwarf.h"
#include "lldb/lldb-defines.h"

class SymbolFileDWARF;

class NameToDIE {
public:
  NameToDIE() : m_map() {}

  ~NameToDIE() {}

  void Dump(lldb_private::Stream *s);

  void Insert(const lldb_private::ConstString &name, const DIERef &die_ref);

  void Append(const NameToDIE &other);

  void Finalize();

  size_t Find(const lldb_private::ConstString &name,
              DIEArray &info_array) const;

  size_t Find(const lldb_private::RegularExpression &regex,
              DIEArray &info_array) const;

  size_t FindAllEntriesForCompileUnit(dw_offset_t cu_offset,
                                      DIEArray &info_array) const;

  void
  ForEach(std::function<bool(lldb_private::ConstString name,
                             const DIERef &die_ref)> const
              &callback) const;

protected:
  lldb_private::UniqueCStringMap<DIERef> m_map;
};

#endif // SymbolFileDWARF_NameToDIE_h_
