//===-- SBSymbolContextList.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBSymbolContextList_h_
#define LLDB_SBSymbolContextList_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBSymbolContext.h"

namespace lldb {

class LLDB_API SBSymbolContextList {
public:
  SBSymbolContextList();

  SBSymbolContextList(const lldb::SBSymbolContextList &rhs);

  ~SBSymbolContextList();

  const lldb::SBSymbolContextList &
  operator=(const lldb::SBSymbolContextList &rhs);

  bool IsValid() const;

  uint32_t GetSize() const;

  lldb::SBSymbolContext GetContextAtIndex(uint32_t idx);

  bool GetDescription(lldb::SBStream &description);

  void Append(lldb::SBSymbolContext &sc);

  void Append(lldb::SBSymbolContextList &sc_list);

  void Clear();

protected:
  friend class SBModule;
  friend class SBTarget;

  lldb_private::SymbolContextList *operator->() const;

  lldb_private::SymbolContextList &operator*() const;

private:
  std::unique_ptr<lldb_private::SymbolContextList> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBSymbolContextList_h_
