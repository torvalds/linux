//===- Error.cpp - system_error extensions for PDB --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/GenericError.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"

using namespace llvm;
using namespace llvm::pdb;

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class PDBErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.pdb"; }
  std::string message(int Condition) const override {
    switch (static_cast<pdb_error_code>(Condition)) {
    case pdb_error_code::unspecified:
      return "An unknown error has occurred.";
    case pdb_error_code::dia_sdk_not_present:
      return "LLVM was not compiled with support for DIA. This usually means "
             "that you are not using MSVC, or your Visual Studio "
             "installation is corrupt.";
    case pdb_error_code::dia_failed_loading:
      return "DIA is only supported when using MSVC.";
    case pdb_error_code::invalid_utf8_path:
      return "The PDB file path is an invalid UTF8 sequence.";
    case pdb_error_code::signature_out_of_date:
      return "The signature does not match; the file(s) might be out of date.";
    case pdb_error_code::external_cmdline_ref:
      return "The path to this file must be provided on the command-line.";
    }
    llvm_unreachable("Unrecognized generic_error_code");
  }
};

static llvm::ManagedStatic<PDBErrorCategory> PDBCategory;
const std::error_category &llvm::pdb::PDBErrCategory() { return *PDBCategory; }

char PDBError::ID;
