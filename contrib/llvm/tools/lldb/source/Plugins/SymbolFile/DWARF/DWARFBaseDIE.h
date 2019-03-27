//===-- DWARFBaseDIE.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFBaseDIE_h_
#define SymbolFileDWARF_DWARFBaseDIE_h_

#include "lldb/Core/dwarf.h"
#include "lldb/lldb-types.h"

struct DIERef;
class DWARFASTParser;
class DWARFAttributes;
class DWARFUnit;
class DWARFDebugInfoEntry;
class DWARFDeclContext;
class DWARFDIECollection;
class SymbolFileDWARF;

class DWARFBaseDIE {
public:
  DWARFBaseDIE() : m_cu(nullptr), m_die(nullptr) {}

  DWARFBaseDIE(DWARFUnit *cu, DWARFDebugInfoEntry *die)
      : m_cu(cu), m_die(die) {}

  DWARFBaseDIE(const DWARFUnit *cu, DWARFDebugInfoEntry *die)
      : m_cu(const_cast<DWARFUnit *>(cu)), m_die(die) {}

  DWARFBaseDIE(DWARFUnit *cu, const DWARFDebugInfoEntry *die)
      : m_cu(cu), m_die(const_cast<DWARFDebugInfoEntry *>(die)) {}

  DWARFBaseDIE(const DWARFUnit *cu, const DWARFDebugInfoEntry *die)
      : m_cu(const_cast<DWARFUnit *>(cu)),
        m_die(const_cast<DWARFDebugInfoEntry *>(die)) {}

  //----------------------------------------------------------------------
  // Tests
  //----------------------------------------------------------------------
  explicit operator bool() const { return IsValid(); }

  bool IsValid() const { return m_cu && m_die; }

  bool HasChildren() const;

  bool Supports_DW_AT_APPLE_objc_complete_type() const;

  //----------------------------------------------------------------------
  // Accessors
  //----------------------------------------------------------------------
  SymbolFileDWARF *GetDWARF() const;

  DWARFUnit *GetCU() const { return m_cu; }

  DWARFDebugInfoEntry *GetDIE() const { return m_die; }

  DIERef GetDIERef() const;

  lldb_private::TypeSystem *GetTypeSystem() const;

  DWARFASTParser *GetDWARFParser() const;

  void Set(DWARFUnit *cu, DWARFDebugInfoEntry *die) {
    if (cu && die) {
      m_cu = cu;
      m_die = die;
    } else {
      Clear();
    }
  }

  void Clear() {
    m_cu = nullptr;
    m_die = nullptr;
  }

  //----------------------------------------------------------------------
  // Get the data that contains the attribute values for this DIE. Support
  // for .debug_types means that any DIE can have its data either in the
  // .debug_info or the .debug_types section; this method will return the
  // correct section data.
  //
  // Clients must validate that this object is valid before calling this.
  //----------------------------------------------------------------------
  const lldb_private::DWARFDataExtractor &GetData() const;

  //----------------------------------------------------------------------
  // Accessing information about a DIE
  //----------------------------------------------------------------------
  dw_tag_t Tag() const;

  const char *GetTagAsCString() const;

  dw_offset_t GetOffset() const;

  dw_offset_t GetCompileUnitRelativeOffset() const;

  //----------------------------------------------------------------------
  // Get the LLDB user ID for this DIE. This is often just the DIE offset,
  // but it might have a SymbolFileDWARF::GetID() in the high 32 bits if
  // we are doing Darwin DWARF in .o file, or DWARF stand alone debug
  // info.
  //----------------------------------------------------------------------
  lldb::user_id_t GetID() const;

  const char *GetName() const;

  lldb::LanguageType GetLanguage() const;

  lldb::ModuleSP GetModule() const;

  lldb_private::CompileUnit *GetLLDBCompileUnit() const;

  //----------------------------------------------------------------------
  // Getting attribute values from the DIE.
  //
  // GetAttributeValueAsXXX() functions should only be used if you are
  // looking for one or two attributes on a DIE. If you are trying to
  // parse all attributes, use GetAttributes (...) instead
  //----------------------------------------------------------------------
  const char *GetAttributeValueAsString(const dw_attr_t attr,
                                        const char *fail_value) const;

  uint64_t GetAttributeValueAsUnsigned(const dw_attr_t attr,
                                       uint64_t fail_value) const;

  int64_t GetAttributeValueAsSigned(const dw_attr_t attr,
                                    int64_t fail_value) const;

  uint64_t GetAttributeValueAsReference(const dw_attr_t attr,
                                        uint64_t fail_value) const;

  uint64_t GetAttributeValueAsAddress(const dw_attr_t attr,
                                      uint64_t fail_value) const;

  size_t GetAttributes(DWARFAttributes &attributes, uint32_t depth = 0) const;

  //----------------------------------------------------------------------
  // Pretty printing
  //----------------------------------------------------------------------

  void Dump(lldb_private::Stream *s, const uint32_t recurse_depth) const;

protected:
  DWARFUnit *m_cu;
  DWARFDebugInfoEntry *m_die;
};

bool operator==(const DWARFBaseDIE &lhs, const DWARFBaseDIE &rhs);
bool operator!=(const DWARFBaseDIE &lhs, const DWARFBaseDIE &rhs);

#endif // SymbolFileDWARF_DWARFBaseDIE_h_
