//===-- DWARFDebugInfo.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDebugInfo_h_
#define SymbolFileDWARF_DWARFDebugInfo_h_

#include <map>
#include <vector>

#include "DWARFUnit.h"
#include "DWARFDIE.h"
#include "SymbolFileDWARF.h"
#include "lldb/Core/STLUtils.h"
#include "lldb/lldb-private.h"

typedef std::multimap<const char *, dw_offset_t, CStringCompareFunctionObject>
    CStringToDIEMap;
typedef CStringToDIEMap::iterator CStringToDIEMapIter;
typedef CStringToDIEMap::const_iterator CStringToDIEMapConstIter;

class DWARFDebugInfo {
public:
  typedef dw_offset_t (*Callback)(SymbolFileDWARF *dwarf2Data,
                                  DWARFUnit *cu,
                                  DWARFDebugInfoEntry *die,
                                  const dw_offset_t next_offset,
                                  const uint32_t depth, void *userData);

  DWARFDebugInfo();
  void SetDwarfData(SymbolFileDWARF *dwarf2Data);

  size_t GetNumCompileUnits();
  bool ContainsCompileUnit(const DWARFUnit *cu) const;
  DWARFUnit *GetCompileUnitAtIndex(uint32_t idx);
  DWARFUnit *GetCompileUnit(dw_offset_t cu_offset, uint32_t *idx_ptr = NULL);
  DWARFUnit *GetCompileUnitContainingDIEOffset(dw_offset_t die_offset);
  DWARFUnit *GetCompileUnit(const DIERef &die_ref);
  DWARFDIE GetDIEForDIEOffset(dw_offset_t die_offset);
  DWARFDIE GetDIE(const DIERef &die_ref);

  enum {
    eDumpFlag_Verbose = (1 << 0),  // Verbose dumping
    eDumpFlag_ShowForm = (1 << 1), // Show the DW_form type
    eDumpFlag_ShowAncestors =
        (1 << 2) // Show all parent DIEs when dumping single DIEs
  };

  DWARFDebugAranges &GetCompileUnitAranges();

protected:
  static bool OffsetLessThanCompileUnitOffset(dw_offset_t offset,
                                              const DWARFUnitSP &cu_sp);

  typedef std::vector<DWARFUnitSP> CompileUnitColl;

  //----------------------------------------------------------------------
  // Member variables
  //----------------------------------------------------------------------
  SymbolFileDWARF *m_dwarf2Data;
  CompileUnitColl m_compile_units;
  std::unique_ptr<DWARFDebugAranges>
      m_cu_aranges_ap; // A quick address to compile unit table

private:
  // All parsing needs to be done partially any managed by this class as
  // accessors are called.
  void ParseCompileUnitHeadersIfNeeded();

  DISALLOW_COPY_AND_ASSIGN(DWARFDebugInfo);
};

#endif // SymbolFileDWARF_DWARFDebugInfo_h_
