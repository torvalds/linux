//===-- SymbolFileDWARFDwoDwp.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARFDwoDwp_SymbolFileDWARFDwoDwp_h_
#define SymbolFileDWARFDwoDwp_SymbolFileDWARFDwoDwp_h_

#include "SymbolFileDWARFDwo.h"
#include "SymbolFileDWARFDwp.h"

class SymbolFileDWARFDwoDwp : public SymbolFileDWARFDwo {
public:
  SymbolFileDWARFDwoDwp(SymbolFileDWARFDwp *dwp_symfile,
                        lldb::ObjectFileSP objfile, DWARFUnit *dwarf_cu,
                        uint64_t dwo_id);

protected:
  void LoadSectionData(lldb::SectionType sect_type,
                       lldb_private::DWARFDataExtractor &data) override;

  SymbolFileDWARFDwp *m_dwp_symfile;
  uint64_t m_dwo_id;
};

#endif // SymbolFileDWARFDwoDwp_SymbolFileDWARFDwoDwp_h_
