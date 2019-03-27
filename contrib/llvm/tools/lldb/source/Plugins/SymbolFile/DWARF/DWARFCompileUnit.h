//===-- DWARFCompileUnit.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFCompileUnit_h_
#define SymbolFileDWARF_DWARFCompileUnit_h_

#include "DWARFUnit.h"

class DWARFCompileUnit : public DWARFUnit {
public:
  static DWARFUnitSP Extract(SymbolFileDWARF *dwarf2Data,
                             const lldb_private::DWARFDataExtractor &debug_info,
                             lldb::offset_t *offset_ptr);
  void Dump(lldb_private::Stream *s) const override;

  //------------------------------------------------------------------
  /// Get the data that contains the DIE information for this unit.
  ///
  /// @return
  ///   The correct data (.debug_types for DWARF 4 and earlier, and
  ///   .debug_info for DWARF 5 and later) for the DIE information in
  ///   this unit.
  //------------------------------------------------------------------
  const lldb_private::DWARFDataExtractor &GetData() const override;

  //------------------------------------------------------------------
  /// Get the size in bytes of the header.
  ///
  /// @return
  ///     Byte size of the compile unit header
  //------------------------------------------------------------------
  uint32_t GetHeaderByteSize() const override;

private:
  DWARFCompileUnit(SymbolFileDWARF *dwarf2Data);
  DISALLOW_COPY_AND_ASSIGN(DWARFCompileUnit);
};

#endif // SymbolFileDWARF_DWARFCompileUnit_h_
