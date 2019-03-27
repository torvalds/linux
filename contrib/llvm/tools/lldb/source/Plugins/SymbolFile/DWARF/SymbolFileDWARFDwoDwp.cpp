//===-- SymbolFileDWARFDwoDwp.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARFDwoDwp.h"

#include "lldb/Core/Section.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/LLDBAssert.h"

#include "DWARFUnit.h"
#include "DWARFDebugInfo.h"

using namespace lldb;
using namespace lldb_private;

SymbolFileDWARFDwoDwp::SymbolFileDWARFDwoDwp(SymbolFileDWARFDwp *dwp_symfile,
                                             ObjectFileSP objfile,
                                             DWARFUnit *dwarf_cu,
                                             uint64_t dwo_id)
    : SymbolFileDWARFDwo(objfile, dwarf_cu), m_dwp_symfile(dwp_symfile),
      m_dwo_id(dwo_id) {}

void SymbolFileDWARFDwoDwp::LoadSectionData(lldb::SectionType sect_type,
                                            DWARFDataExtractor &data) {
  if (m_dwp_symfile->LoadSectionData(m_dwo_id, sect_type, data))
    return;

  SymbolFileDWARF::LoadSectionData(sect_type, data);
}
