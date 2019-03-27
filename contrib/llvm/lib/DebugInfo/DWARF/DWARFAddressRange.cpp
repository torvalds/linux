//===- DWARFDebugAranges.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"

#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void DWARFAddressRange::dump(raw_ostream &OS, uint32_t AddressSize,
                             DIDumpOptions DumpOpts) const {

  OS << (DumpOpts.DisplayRawContents ? " " : "[");
  OS << format("0x%*.*" PRIx64 ", ", AddressSize * 2, AddressSize * 2, LowPC)
     << format("0x%*.*" PRIx64, AddressSize * 2, AddressSize * 2, HighPC);
  OS << (DumpOpts.DisplayRawContents ? "" : ")");
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const DWARFAddressRange &R) {
  R.dump(OS, /* AddressSize */ 8);
  return OS;
}
