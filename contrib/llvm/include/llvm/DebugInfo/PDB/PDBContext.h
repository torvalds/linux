//===-- PDBContext.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===/

#ifndef LLVM_DEBUGINFO_PDB_PDBCONTEXT_H
#define LLVM_DEBUGINFO_PDB_PDBCONTEXT_H

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include <cstdint>
#include <memory>
#include <string>

namespace llvm {

namespace object {
class COFFObjectFile;
} // end namespace object

namespace pdb {

  /// PDBContext
  /// This data structure is the top level entity that deals with PDB debug
  /// information parsing.  This data structure exists only when there is a
  /// need for a transparent interface to different debug information formats
  /// (e.g. PDB and DWARF).  More control and power over the debug information
  /// access can be had by using the PDB interfaces directly.
  class PDBContext : public DIContext {
  public:
    PDBContext(const object::COFFObjectFile &Object,
               std::unique_ptr<IPDBSession> PDBSession);
    PDBContext(PDBContext &) = delete;
    PDBContext &operator=(PDBContext &) = delete;

    static bool classof(const DIContext *DICtx) {
      return DICtx->getKind() == CK_PDB;
    }

    void dump(raw_ostream &OS, DIDumpOptions DIDumpOpts) override;

    DILineInfo getLineInfoForAddress(
        uint64_t Address,
        DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
    DILineInfoTable getLineInfoForAddressRange(
        uint64_t Address, uint64_t Size,
        DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
    DIInliningInfo getInliningInfoForAddress(
        uint64_t Address,
        DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  private:
    std::string getFunctionName(uint64_t Address, DINameKind NameKind) const;
    std::unique_ptr<IPDBSession> Session;
  };

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBCONTEXT_H
