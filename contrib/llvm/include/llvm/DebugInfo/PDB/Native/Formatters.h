//===- Formatters.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_FORMATTERS_H
#define LLVM_DEBUGINFO_PDB_NATIVE_FORMATTERS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/FormatProviders.h"

#define FORMAT_CASE(Value, Name)                                               \
  case Value:                                                                  \
    Stream << Name;                                                            \
    break;

namespace llvm {
template <> struct format_provider<pdb::PdbRaw_ImplVer> {
  static void format(const pdb::PdbRaw_ImplVer &V, llvm::raw_ostream &Stream,
                     StringRef Style) {
    switch (V) {
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC110, "VC110")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC140, "VC140")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC2, "VC2")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC4, "VC4")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC41, "VC41")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC50, "VC50")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC70, "VC70")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC70Dep, "VC70Dep")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC80, "VC80")
      FORMAT_CASE(pdb::PdbRaw_ImplVer::PdbImplVC98, "VC98")
    }
  }
};
}

#endif
