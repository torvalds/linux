//===- PDB.h - base header file for creating a PDB reader -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDB_H
#define LLVM_DEBUGINFO_PDB_PDB_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace llvm {
namespace pdb {

class IPDBSession;

Error loadDataForPDB(PDB_ReaderType Type, StringRef Path,
                     std::unique_ptr<IPDBSession> &Session);

Error loadDataForEXE(PDB_ReaderType Type, StringRef Path,
                     std::unique_ptr<IPDBSession> &Session);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDB_H
