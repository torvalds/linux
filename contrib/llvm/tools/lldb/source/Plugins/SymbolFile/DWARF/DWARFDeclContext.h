//===-- DWARFDeclContext.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDeclContext_h_
#define SymbolFileDWARF_DWARFDeclContext_h_

#include <string>
#include <vector>
#include "lldb/Utility/ConstString.h"
#include "DWARFDefines.h"

//----------------------------------------------------------------------
// DWARFDeclContext
//
// A class that represents a declaration context all the way down to a
// DIE. This is useful when trying to find a DIE in one DWARF to a DIE
// in another DWARF file.
//----------------------------------------------------------------------

class DWARFDeclContext {
public:
  struct Entry {
    Entry() : tag(0), name(NULL) {}
    Entry(dw_tag_t t, const char *n) : tag(t), name(n) {}

    bool NameMatches(const Entry &rhs) const {
      if (name == rhs.name)
        return true;
      else if (name && rhs.name)
        return strcmp(name, rhs.name) == 0;
      return false;
    }

    // Test operator
    explicit operator bool() const { return tag != 0; }

    dw_tag_t tag;
    const char *name;
  };

  DWARFDeclContext() : m_entries(), m_language(lldb::eLanguageTypeUnknown) {}

  void AppendDeclContext(dw_tag_t tag, const char *name) {
    m_entries.push_back(Entry(tag, name));
  }

  bool operator==(const DWARFDeclContext &rhs) const;

  uint32_t GetSize() const { return m_entries.size(); }

  Entry &operator[](uint32_t idx) {
    // "idx" must be valid
    return m_entries[idx];
  }

  const Entry &operator[](uint32_t idx) const {
    // "idx" must be valid
    return m_entries[idx];
  }

  const char *GetQualifiedName() const;

  // Same as GetQaulifiedName, but the life time of the returned string will
  // be that of the LLDB session.
  lldb_private::ConstString GetQualifiedNameAsConstString() const {
    return lldb_private::ConstString(GetQualifiedName());
  }

  void Clear() {
    m_entries.clear();
    m_qualified_name.clear();
  }

  lldb::LanguageType GetLanguage() const { return m_language; }

  void SetLanguage(lldb::LanguageType language) { m_language = language; }

protected:
  typedef std::vector<Entry> collection;
  collection m_entries;
  mutable std::string m_qualified_name;
  lldb::LanguageType m_language;
};

#endif // SymbolFileDWARF_DWARFDeclContext_h_
