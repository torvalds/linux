//===-- DWARFDIE.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDIE_h_
#define SymbolFileDWARF_DWARFDIE_h_

#include "DWARFBaseDIE.h"
#include "llvm/ADT/SmallSet.h"

class DWARFDIE : public DWARFBaseDIE {
public:
  class ElaboratingDIEIterator;

  using DWARFBaseDIE::DWARFBaseDIE;

  //----------------------------------------------------------------------
  // Tests
  //----------------------------------------------------------------------
  bool IsStructUnionOrClass() const;

  bool IsMethod() const;

  //----------------------------------------------------------------------
  // Accessors
  //----------------------------------------------------------------------
  lldb::ModuleSP GetContainingDWOModule() const;

  DWARFDIE
  GetContainingDWOModuleDIE() const;

  inline llvm::iterator_range<ElaboratingDIEIterator> elaborating_dies() const;

  //----------------------------------------------------------------------
  // Accessing information about a DIE
  //----------------------------------------------------------------------
  const char *GetMangledName() const;

  const char *GetPubname() const;

  const char *GetQualifiedName(std::string &storage) const;

  lldb_private::Type *ResolveType() const;

  //----------------------------------------------------------------------
  // Resolve a type by UID using this DIE's DWARF file
  //----------------------------------------------------------------------
  lldb_private::Type *ResolveTypeUID(const DIERef &die_ref) const;

  //----------------------------------------------------------------------
  // Functions for obtaining DIE relations and references
  //----------------------------------------------------------------------

  DWARFDIE
  GetParent() const;

  DWARFDIE
  GetFirstChild() const;

  DWARFDIE
  GetSibling() const;

  DWARFDIE
  GetReferencedDIE(const dw_attr_t attr) const;

  //----------------------------------------------------------------------
  // Get a another DIE from the same DWARF file as this DIE. This will
  // check the current DIE's compile unit first to see if "die_offset" is
  // in the same compile unit, and fall back to checking the DWARF file.
  //----------------------------------------------------------------------
  DWARFDIE
  GetDIE(dw_offset_t die_offset) const;
  using DWARFBaseDIE::GetDIE;

  DWARFDIE
  LookupDeepestBlock(lldb::addr_t file_addr) const;

  DWARFDIE
  GetParentDeclContextDIE() const;

  //----------------------------------------------------------------------
  // DeclContext related functions
  //----------------------------------------------------------------------
  void GetDeclContextDIEs(DWARFDIECollection &decl_context_dies) const;

  void GetDWARFDeclContext(DWARFDeclContext &dwarf_decl_ctx) const;

  /// Return this DIE's decl context as it is needed to look up types
  /// in Clang's -gmodules debug info format.
  void
  GetDeclContext(std::vector<lldb_private::CompilerContext> &context) const;

  //----------------------------------------------------------------------
  // Getting attribute values from the DIE.
  //
  // GetAttributeValueAsXXX() functions should only be used if you are
  // looking for one or two attributes on a DIE. If you are trying to
  // parse all attributes, use GetAttributes (...) instead
  //----------------------------------------------------------------------
  DWARFDIE
  GetAttributeValueAsReferenceDIE(const dw_attr_t attr) const;

  bool GetDIENamesAndRanges(const char *&name, const char *&mangled,
                            DWARFRangeList &ranges, int &decl_file,
                            int &decl_line, int &decl_column, int &call_file,
                            int &call_line, int &call_column,
                            lldb_private::DWARFExpression *frame_base) const;

  //----------------------------------------------------------------------
  // CompilerDecl related functions
  //----------------------------------------------------------------------

  lldb_private::CompilerDecl GetDecl() const;

  lldb_private::CompilerDeclContext GetDeclContext() const;

  lldb_private::CompilerDeclContext GetContainingDeclContext() const;
};

/// Iterate through all DIEs elaborating (i.e. reachable by a chain of
/// DW_AT_specification and DW_AT_abstract_origin attributes) a given DIE. For
/// convenience, the starting die is included in the sequence as the first
/// item.
class DWARFDIE::ElaboratingDIEIterator
    : public std::iterator<std::input_iterator_tag, DWARFDIE> {

  // The operating invariant is: top of m_worklist contains the "current" item
  // and the rest of the list are items yet to be visited. An empty worklist
  // means we've reached the end.
  // Infinite recursion is prevented by maintaining a list of seen DIEs.
  // Container sizes are optimized for the case of following DW_AT_specification
  // and DW_AT_abstract_origin just once.
  llvm::SmallVector<DWARFDIE, 2> m_worklist;
  llvm::SmallSet<lldb::user_id_t, 3> m_seen;

  void Next();

public:
  /// An iterator starting at die d.
  explicit ElaboratingDIEIterator(DWARFDIE d) : m_worklist(1, d) {}

  /// End marker
  ElaboratingDIEIterator() {}

  const DWARFDIE &operator*() const { return m_worklist.back(); }
  ElaboratingDIEIterator &operator++() {
    Next();
    return *this;
  }
  ElaboratingDIEIterator operator++(int) {
    ElaboratingDIEIterator I = *this;
    Next();
    return I;
  }

  friend bool operator==(const ElaboratingDIEIterator &a,
                         const ElaboratingDIEIterator &b) {
    if (a.m_worklist.empty() || b.m_worklist.empty())
      return a.m_worklist.empty() == b.m_worklist.empty();
    return a.m_worklist.back() == b.m_worklist.back();
  }
  friend bool operator!=(const ElaboratingDIEIterator &a,
                         const ElaboratingDIEIterator &b) {
    return !(a == b);
  }
};

llvm::iterator_range<DWARFDIE::ElaboratingDIEIterator>
DWARFDIE::elaborating_dies() const {
  return llvm::make_range(ElaboratingDIEIterator(*this),
                          ElaboratingDIEIterator());
}

#endif // SymbolFileDWARF_DWARFDIE_h_
