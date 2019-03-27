//===-- SBThreadCollection.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBThreadCollection_h_
#define LLDB_SBThreadCollection_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBThreadCollection {
public:
  SBThreadCollection();

  SBThreadCollection(const SBThreadCollection &rhs);

  const SBThreadCollection &operator=(const SBThreadCollection &rhs);

  ~SBThreadCollection();

  bool IsValid() const;

  size_t GetSize();

  lldb::SBThread GetThreadAtIndex(size_t idx);

protected:
  // Mimic shared pointer...
  lldb_private::ThreadCollection *get() const;

  lldb_private::ThreadCollection *operator->() const;

  lldb::ThreadCollectionSP &operator*();

  const lldb::ThreadCollectionSP &operator*() const;

  SBThreadCollection(const lldb::ThreadCollectionSP &threads);

  void SetOpaque(const lldb::ThreadCollectionSP &threads);

private:
  friend class SBProcess;
  friend class SBThread;

  lldb::ThreadCollectionSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBThreadCollection_h_
