//===-- DWARFDebugAbbrev.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDebugAbbrev_h_
#define SymbolFileDWARF_DWARFDebugAbbrev_h_

#include <list>
#include <map>

#include "lldb/lldb-private.h"

#include "DWARFAbbreviationDeclaration.h"
#include "DWARFDefines.h"

typedef std::vector<DWARFAbbreviationDeclaration>
    DWARFAbbreviationDeclarationColl;
typedef DWARFAbbreviationDeclarationColl::iterator
    DWARFAbbreviationDeclarationCollIter;
typedef DWARFAbbreviationDeclarationColl::const_iterator
    DWARFAbbreviationDeclarationCollConstIter;

class DWARFAbbreviationDeclarationSet {
public:
  DWARFAbbreviationDeclarationSet()
      : m_offset(DW_INVALID_OFFSET), m_idx_offset(0), m_decls() {}

  DWARFAbbreviationDeclarationSet(dw_offset_t offset, uint32_t idx_offset)
      : m_offset(offset), m_idx_offset(idx_offset), m_decls() {}

  void Clear();
  dw_offset_t GetOffset() const { return m_offset; }
  void Dump(lldb_private::Stream *s) const;
  bool Extract(const lldb_private::DWARFDataExtractor &data,
               lldb::offset_t *offset_ptr);
  // void Encode(BinaryStreamBuf& debug_abbrev_buf) const;
  dw_uleb128_t
  AppendAbbrevDeclSequential(const DWARFAbbreviationDeclaration &abbrevDecl);
  void GetUnsupportedForms(std::set<dw_form_t> &invalid_forms) const;

  const DWARFAbbreviationDeclaration *
  GetAbbreviationDeclaration(dw_uleb128_t abbrCode) const;

private:
  dw_offset_t m_offset;
  uint32_t m_idx_offset;
  std::vector<DWARFAbbreviationDeclaration> m_decls;
};

typedef std::map<dw_offset_t, DWARFAbbreviationDeclarationSet>
    DWARFAbbreviationDeclarationCollMap;
typedef DWARFAbbreviationDeclarationCollMap::iterator
    DWARFAbbreviationDeclarationCollMapIter;
typedef DWARFAbbreviationDeclarationCollMap::const_iterator
    DWARFAbbreviationDeclarationCollMapConstIter;

class DWARFDebugAbbrev {
public:
  DWARFDebugAbbrev();
  const DWARFAbbreviationDeclarationSet *
  GetAbbreviationDeclarationSet(dw_offset_t cu_abbr_offset) const;
  void Dump(lldb_private::Stream *s) const;
  void Parse(const lldb_private::DWARFDataExtractor &data);
  void GetUnsupportedForms(std::set<dw_form_t> &invalid_forms) const;

protected:
  DWARFAbbreviationDeclarationCollMap m_abbrevCollMap;
  mutable DWARFAbbreviationDeclarationCollMapConstIter m_prev_abbr_offset_pos;
};

#endif // SymbolFileDWARF_DWARFDebugAbbrev_h_
