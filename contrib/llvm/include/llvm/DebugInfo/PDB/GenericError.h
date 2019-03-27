//===- Error.h - system_error extensions for PDB ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_ERROR_H
#define LLVM_DEBUGINFO_PDB_ERROR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {

enum class pdb_error_code {
  invalid_utf8_path = 1,
  dia_sdk_not_present,
  dia_failed_loading,
  signature_out_of_date,
  external_cmdline_ref,
  unspecified,
};
} // namespace pdb
} // namespace llvm

namespace std {
template <>
struct is_error_code_enum<llvm::pdb::pdb_error_code> : std::true_type {};
} // namespace std

namespace llvm {
namespace pdb {
const std::error_category &PDBErrCategory();

inline std::error_code make_error_code(pdb_error_code E) {
  return std::error_code(static_cast<int>(E), PDBErrCategory());
}

/// Base class for errors originating when parsing raw PDB files
class PDBError : public ErrorInfo<PDBError, StringError> {
public:
  using ErrorInfo<PDBError, StringError>::ErrorInfo; // inherit constructors
  PDBError(const Twine &S) : ErrorInfo(S, pdb_error_code::unspecified) {}
  static char ID;
};
} // namespace pdb
} // namespace llvm
#endif
