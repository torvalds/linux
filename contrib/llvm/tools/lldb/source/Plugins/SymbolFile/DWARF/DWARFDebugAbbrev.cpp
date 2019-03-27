//===-- DWARFDebugAbbrev.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugAbbrev.h"
#include "DWARFDataExtractor.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace std;

//----------------------------------------------------------------------
// DWARFAbbreviationDeclarationSet::Clear()
//----------------------------------------------------------------------
void DWARFAbbreviationDeclarationSet::Clear() {
  m_idx_offset = 0;
  m_decls.clear();
}

//----------------------------------------------------------------------
// DWARFAbbreviationDeclarationSet::Extract()
//----------------------------------------------------------------------
bool DWARFAbbreviationDeclarationSet::Extract(const DWARFDataExtractor &data,
                                              lldb::offset_t *offset_ptr) {
  const lldb::offset_t begin_offset = *offset_ptr;
  m_offset = begin_offset;
  Clear();
  DWARFAbbreviationDeclaration abbrevDeclaration;
  dw_uleb128_t prev_abbr_code = 0;
  while (abbrevDeclaration.Extract(data, offset_ptr)) {
    m_decls.push_back(abbrevDeclaration);
    if (m_idx_offset == 0)
      m_idx_offset = abbrevDeclaration.Code();
    else {
      if (prev_abbr_code + 1 != abbrevDeclaration.Code())
        m_idx_offset =
            UINT32_MAX; // Out of order indexes, we can't do O(1) lookups...
    }
    prev_abbr_code = abbrevDeclaration.Code();
  }
  return begin_offset != *offset_ptr;
}

//----------------------------------------------------------------------
// DWARFAbbreviationDeclarationSet::Dump()
//----------------------------------------------------------------------
void DWARFAbbreviationDeclarationSet::Dump(Stream *s) const {
  std::for_each(
      m_decls.begin(), m_decls.end(),
      bind2nd(std::mem_fun_ref(&DWARFAbbreviationDeclaration::Dump), s));
}

//----------------------------------------------------------------------
// DWARFAbbreviationDeclarationSet::GetAbbreviationDeclaration()
//----------------------------------------------------------------------
const DWARFAbbreviationDeclaration *
DWARFAbbreviationDeclarationSet::GetAbbreviationDeclaration(
    dw_uleb128_t abbrCode) const {
  if (m_idx_offset == UINT32_MAX) {
    DWARFAbbreviationDeclarationCollConstIter pos;
    DWARFAbbreviationDeclarationCollConstIter end = m_decls.end();
    for (pos = m_decls.begin(); pos != end; ++pos) {
      if (pos->Code() == abbrCode)
        return &(*pos);
    }
  } else {
    uint32_t idx = abbrCode - m_idx_offset;
    if (idx < m_decls.size())
      return &m_decls[idx];
  }
  return NULL;
}

//----------------------------------------------------------------------
// DWARFAbbreviationDeclarationSet::AppendAbbrevDeclSequential()
//
// Append an abbreviation declaration with a sequential code for O(n) lookups.
// Handy when creating an DWARFAbbreviationDeclarationSet.
//----------------------------------------------------------------------
dw_uleb128_t DWARFAbbreviationDeclarationSet::AppendAbbrevDeclSequential(
    const DWARFAbbreviationDeclaration &abbrevDecl) {
  // Get the next abbreviation code based on our current array size
  dw_uleb128_t code = m_decls.size() + 1;

  // Push the new declaration on the back
  m_decls.push_back(abbrevDecl);

  // Update the code for this new declaration
  m_decls.back().SetCode(code);

  return code; // return the new abbreviation code!
}

//----------------------------------------------------------------------
// DWARFAbbreviationDeclarationSet::GetUnsupportedForms()
//----------------------------------------------------------------------
void DWARFAbbreviationDeclarationSet::GetUnsupportedForms(
    std::set<dw_form_t> &invalid_forms) const {
  for (const auto &abbr_decl : m_decls) {
    const size_t num_attrs = abbr_decl.NumAttributes();
    for (size_t i=0; i<num_attrs; ++i) {
      dw_form_t form = abbr_decl.GetFormByIndex(i);
      if (!DWARFFormValue::FormIsSupported(form))
        invalid_forms.insert(form);
    }
  }
}

//----------------------------------------------------------------------
// Encode
//
// Encode the abbreviation table onto the end of the buffer provided into a
// byte representation as would be found in a ".debug_abbrev" debug information
// section.
//----------------------------------------------------------------------
// void
// DWARFAbbreviationDeclarationSet::Encode(BinaryStreamBuf& debug_abbrev_buf)
// const
//{
//  DWARFAbbreviationDeclarationCollConstIter pos;
//  DWARFAbbreviationDeclarationCollConstIter end = m_decls.end();
//  for (pos = m_decls.begin(); pos != end; ++pos)
//      pos->Append(debug_abbrev_buf);
//  debug_abbrev_buf.Append8(0);
//}

//----------------------------------------------------------------------
// DWARFDebugAbbrev constructor
//----------------------------------------------------------------------
DWARFDebugAbbrev::DWARFDebugAbbrev()
    : m_abbrevCollMap(), m_prev_abbr_offset_pos(m_abbrevCollMap.end()) {}

//----------------------------------------------------------------------
// DWARFDebugAbbrev::Parse()
//----------------------------------------------------------------------
void DWARFDebugAbbrev::Parse(const DWARFDataExtractor &data) {
  lldb::offset_t offset = 0;

  while (data.ValidOffset(offset)) {
    uint32_t initial_cu_offset = offset;
    DWARFAbbreviationDeclarationSet abbrevDeclSet;

    if (abbrevDeclSet.Extract(data, &offset))
      m_abbrevCollMap[initial_cu_offset] = abbrevDeclSet;
    else
      break;
  }
  m_prev_abbr_offset_pos = m_abbrevCollMap.end();
}

//----------------------------------------------------------------------
// DWARFDebugAbbrev::Dump()
//----------------------------------------------------------------------
void DWARFDebugAbbrev::Dump(Stream *s) const {
  if (m_abbrevCollMap.empty()) {
    s->PutCString("< EMPTY >\n");
    return;
  }

  DWARFAbbreviationDeclarationCollMapConstIter pos;
  for (pos = m_abbrevCollMap.begin(); pos != m_abbrevCollMap.end(); ++pos) {
    s->Printf("Abbrev table for offset: 0x%8.8x\n", pos->first);
    pos->second.Dump(s);
  }
}

//----------------------------------------------------------------------
// DWARFDebugAbbrev::GetAbbreviationDeclarationSet()
//----------------------------------------------------------------------
const DWARFAbbreviationDeclarationSet *
DWARFDebugAbbrev::GetAbbreviationDeclarationSet(
    dw_offset_t cu_abbr_offset) const {
  DWARFAbbreviationDeclarationCollMapConstIter end = m_abbrevCollMap.end();
  DWARFAbbreviationDeclarationCollMapConstIter pos;
  if (m_prev_abbr_offset_pos != end &&
      m_prev_abbr_offset_pos->first == cu_abbr_offset)
    return &(m_prev_abbr_offset_pos->second);
  else {
    pos = m_abbrevCollMap.find(cu_abbr_offset);
    m_prev_abbr_offset_pos = pos;
  }

  if (pos != m_abbrevCollMap.end())
    return &(pos->second);
  return NULL;
}

//----------------------------------------------------------------------
// DWARFDebugAbbrev::GetUnsupportedForms()
//----------------------------------------------------------------------
void DWARFDebugAbbrev::GetUnsupportedForms(
    std::set<dw_form_t> &invalid_forms) const {
  for (const auto &pair : m_abbrevCollMap)
    pair.second.GetUnsupportedForms(invalid_forms);
}
