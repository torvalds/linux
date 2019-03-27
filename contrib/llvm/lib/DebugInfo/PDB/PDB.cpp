//===- PDB.cpp - base header file for creating a PDB reader ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/PDB/GenericError.h"
#if LLVM_ENABLE_DIA_SDK
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"
#endif
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;
using namespace llvm::pdb;

Error llvm::pdb::loadDataForPDB(PDB_ReaderType Type, StringRef Path,
                                std::unique_ptr<IPDBSession> &Session) {
  // Create the correct concrete instance type based on the value of Type.
  if (Type == PDB_ReaderType::Native) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> ErrorOrBuffer =
        MemoryBuffer::getFileOrSTDIN(Path, /*FileSize=*/-1,
                                     /*RequiresNullTerminator=*/false);
    if (!ErrorOrBuffer)
      return errorCodeToError(ErrorOrBuffer.getError());

    return NativeSession::createFromPdb(std::move(*ErrorOrBuffer), Session);
  }

#if LLVM_ENABLE_DIA_SDK
  return DIASession::createFromPdb(Path, Session);
#else
  return make_error<PDBError>(pdb_error_code::dia_sdk_not_present);
#endif
}

Error llvm::pdb::loadDataForEXE(PDB_ReaderType Type, StringRef Path,
                                std::unique_ptr<IPDBSession> &Session) {
  // Create the correct concrete instance type based on the value of Type.
  if (Type == PDB_ReaderType::Native)
    return NativeSession::createFromExe(Path, Session);

#if LLVM_ENABLE_DIA_SDK
  return DIASession::createFromExe(Path, Session);
#else
  return make_error<PDBError>(pdb_error_code::dia_sdk_not_present);
#endif
}
