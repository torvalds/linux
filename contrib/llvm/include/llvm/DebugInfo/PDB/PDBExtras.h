//===- PDBExtras.h - helper functions and classes for PDBs ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBEXTRAS_H
#define LLVM_DEBUGINFO_PDB_PDBEXTRAS_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_map>

namespace llvm {

class raw_ostream;

namespace pdb {

using TagStats = std::unordered_map<PDB_SymType, int>;

raw_ostream &operator<<(raw_ostream &OS, const PDB_VariantType &Value);
raw_ostream &operator<<(raw_ostream &OS, const PDB_CallingConv &Conv);
raw_ostream &operator<<(raw_ostream &OS, const PDB_BuiltinType &Type);
raw_ostream &operator<<(raw_ostream &OS, const PDB_DataKind &Data);
raw_ostream &operator<<(raw_ostream &OS, const codeview::RegisterId &Reg);
raw_ostream &operator<<(raw_ostream &OS, const PDB_LocType &Loc);
raw_ostream &operator<<(raw_ostream &OS, const codeview::ThunkOrdinal &Thunk);
raw_ostream &operator<<(raw_ostream &OS, const PDB_Checksum &Checksum);
raw_ostream &operator<<(raw_ostream &OS, const PDB_Lang &Lang);
raw_ostream &operator<<(raw_ostream &OS, const PDB_SymType &Tag);
raw_ostream &operator<<(raw_ostream &OS, const PDB_MemberAccess &Access);
raw_ostream &operator<<(raw_ostream &OS, const PDB_UdtType &Type);
raw_ostream &operator<<(raw_ostream &OS, const PDB_Machine &Machine);
raw_ostream &operator<<(raw_ostream &OS,
                        const PDB_SourceCompression &Compression);

raw_ostream &operator<<(raw_ostream &OS, const Variant &Value);
raw_ostream &operator<<(raw_ostream &OS, const VersionInfo &Version);
raw_ostream &operator<<(raw_ostream &OS, const TagStats &Stats);


template <typename T>
void dumpSymbolField(raw_ostream &OS, StringRef Name, T Value, int Indent) {
  OS << "\n";
  OS.indent(Indent);
  OS << Name << ": " << Value;
}


} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBEXTRAS_H
