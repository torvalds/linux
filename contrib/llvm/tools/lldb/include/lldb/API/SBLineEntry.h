//===-- SBLineEntry.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBLineEntry_h_
#define LLDB_SBLineEntry_h_

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBFileSpec.h"

namespace lldb {

class LLDB_API SBLineEntry {
public:
  SBLineEntry();

  SBLineEntry(const lldb::SBLineEntry &rhs);

  ~SBLineEntry();

  const lldb::SBLineEntry &operator=(const lldb::SBLineEntry &rhs);

  lldb::SBAddress GetStartAddress() const;

  lldb::SBAddress GetEndAddress() const;

  bool IsValid() const;

  lldb::SBFileSpec GetFileSpec() const;

  uint32_t GetLine() const;

  uint32_t GetColumn() const;

  void SetFileSpec(lldb::SBFileSpec filespec);

  void SetLine(uint32_t line);

  void SetColumn(uint32_t column);

  bool operator==(const lldb::SBLineEntry &rhs) const;

  bool operator!=(const lldb::SBLineEntry &rhs) const;

  bool GetDescription(lldb::SBStream &description);

protected:
  lldb_private::LineEntry *get();

private:
  friend class SBAddress;
  friend class SBCompileUnit;
  friend class SBFrame;
  friend class SBSymbolContext;

  const lldb_private::LineEntry *operator->() const;

  lldb_private::LineEntry &ref();

  const lldb_private::LineEntry &ref() const;

  SBLineEntry(const lldb_private::LineEntry *lldb_object_ptr);

  void SetLineEntry(const lldb_private::LineEntry &lldb_object_ref);

  std::unique_ptr<lldb_private::LineEntry> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBLineEntry_h_
