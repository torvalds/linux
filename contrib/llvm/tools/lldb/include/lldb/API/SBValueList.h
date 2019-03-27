//===-- SBValueList.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBValueList_h_
#define LLDB_SBValueList_h_

#include "lldb/API/SBDefines.h"

class ValueListImpl;

namespace lldb {

class LLDB_API SBValueList {
public:
  SBValueList();

  SBValueList(const lldb::SBValueList &rhs);

  ~SBValueList();

  bool IsValid() const;

  void Clear();

  void Append(const lldb::SBValue &val_obj);

  void Append(const lldb::SBValueList &value_list);

  uint32_t GetSize() const;

  lldb::SBValue GetValueAtIndex(uint32_t idx) const;

  lldb::SBValue GetFirstValueByName(const char *name) const;

  lldb::SBValue FindValueObjectByUID(lldb::user_id_t uid);

  const lldb::SBValueList &operator=(const lldb::SBValueList &rhs);

protected:
  // only useful for visualizing the pointer or comparing two SBValueLists to
  // see if they are backed by the same underlying Impl.
  void *opaque_ptr();

private:
  friend class SBFrame;

  SBValueList(const ValueListImpl *lldb_object_ptr);

  void Append(lldb::ValueObjectSP &val_obj_sp);

  void CreateIfNeeded();

  ValueListImpl *operator->();

  ValueListImpl &operator*();

  const ValueListImpl *operator->() const;

  const ValueListImpl &operator*() const;

  ValueListImpl &ref();

  std::unique_ptr<ValueListImpl> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBValueList_h_
