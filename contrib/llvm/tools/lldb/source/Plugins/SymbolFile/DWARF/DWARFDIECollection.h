//===-- DWARFDIECollection.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDIECollection_h_
#define SymbolFileDWARF_DWARFDIECollection_h_

#include "DWARFDIE.h"
#include <vector>

class DWARFDIECollection {
public:
  DWARFDIECollection() : m_dies() {}
  ~DWARFDIECollection() {}

  void Append(const DWARFDIE &die);

  void Dump(lldb_private::Stream *s, const char *title) const;

  DWARFDIE
  GetDIEAtIndex(uint32_t idx) const;

  size_t Size() const;

protected:
  typedef std::vector<DWARFDIE> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  collection m_dies; // Ordered list of die offsets
};

#endif // SymbolFileDWARF_DWARFDIECollection_h_
