//===-- ValueObjectList.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObjectList_h_
#define liblldb_ValueObjectList_h_

#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <vector>

#include <stddef.h>

namespace lldb_private {
class ValueObject;
}

namespace lldb_private {

//----------------------------------------------------------------------
// A collection of ValueObject values that
//----------------------------------------------------------------------
class ValueObjectList {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  ValueObjectList();

  ValueObjectList(const ValueObjectList &rhs);

  ~ValueObjectList();

  const ValueObjectList &operator=(const ValueObjectList &rhs);

  void Append(const lldb::ValueObjectSP &val_obj_sp);

  void Append(const ValueObjectList &valobj_list);

  lldb::ValueObjectSP FindValueObjectByPointer(ValueObject *valobj);

  size_t GetSize() const;

  void Resize(size_t size);

  lldb::ValueObjectSP GetValueObjectAtIndex(size_t idx);

  lldb::ValueObjectSP RemoveValueObjectAtIndex(size_t idx);

  void SetValueObjectAtIndex(size_t idx, const lldb::ValueObjectSP &valobj_sp);

  lldb::ValueObjectSP FindValueObjectByValueName(const char *name);

  lldb::ValueObjectSP FindValueObjectByUID(lldb::user_id_t uid);

  void Swap(ValueObjectList &value_object_list);

  void Clear() { m_value_objects.clear(); }

  const std::vector<lldb::ValueObjectSP> &GetObjects() const {
    return m_value_objects;
  }
protected:
  typedef std::vector<lldb::ValueObjectSP> collection;
  //------------------------------------------------------------------
  // Classes that inherit from ValueObjectList can see and modify these
  //------------------------------------------------------------------
  collection m_value_objects;
};

} // namespace lldb_private

#endif // liblldb_ValueObjectList_h_
