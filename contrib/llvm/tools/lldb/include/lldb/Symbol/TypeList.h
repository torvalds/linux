//===-- TypeList.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_TypeList_h_
#define liblldb_TypeList_h_

#include "lldb/Symbol/Type.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/lldb-private.h"
#include <functional>
#include <vector>

namespace lldb_private {

class TypeList {
public:
  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  TypeList();

  virtual ~TypeList();

  void Clear();

  void Dump(Stream *s, bool show_context);

  //    lldb::TypeSP
  //    FindType(lldb::user_id_t uid);

  TypeList FindTypes(const ConstString &name);

  void Insert(const lldb::TypeSP &type);

  uint32_t GetSize() const;

  lldb::TypeSP GetTypeAtIndex(uint32_t idx);

  typedef std::vector<lldb::TypeSP> collection;
  typedef AdaptedIterable<collection, lldb::TypeSP, vector_adapter>
      TypeIterable;

  TypeIterable Types() { return TypeIterable(m_types); }

  void ForEach(
      std::function<bool(const lldb::TypeSP &type_sp)> const &callback) const;

  void ForEach(std::function<bool(lldb::TypeSP &type_sp)> const &callback);

  void RemoveMismatchedTypes(const char *qualified_typename, bool exact_match);

  void RemoveMismatchedTypes(const std::string &type_scope,
                             const std::string &type_basename,
                             lldb::TypeClass type_class, bool exact_match);

  void RemoveMismatchedTypes(lldb::TypeClass type_class);

private:
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  collection m_types;

  DISALLOW_COPY_AND_ASSIGN(TypeList);
};

} // namespace lldb_private

#endif // liblldb_TypeList_h_
