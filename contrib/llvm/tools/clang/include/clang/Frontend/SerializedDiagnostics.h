//===--- SerializedDiagnostics.h - Common data for serialized diagnostics -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_SERIALIZE_DIAGNOSTICS_H_
#define LLVM_CLANG_FRONTEND_SERIALIZE_DIAGNOSTICS_H_

#include "llvm/Bitcode/BitCodes.h"

namespace clang {
namespace serialized_diags {

enum BlockIDs {
  /// A top-level block which represents any meta data associated
  /// with the diagostics, including versioning of the format.
  BLOCK_META = llvm::bitc::FIRST_APPLICATION_BLOCKID,

  /// The this block acts as a container for all the information
  /// for a specific diagnostic.
  BLOCK_DIAG
};

enum RecordIDs {
  RECORD_VERSION = 1,
  RECORD_DIAG,
  RECORD_SOURCE_RANGE,
  RECORD_DIAG_FLAG,
  RECORD_CATEGORY,
  RECORD_FILENAME,
  RECORD_FIXIT,
  RECORD_FIRST = RECORD_VERSION,
  RECORD_LAST = RECORD_FIXIT
};

/// A stable version of DiagnosticIDs::Level.
///
/// Do not change the order of values in this enum, and please increment the
/// serialized diagnostics version number when you add to it.
enum Level {
  Ignored = 0,
  Note,
  Warning,
  Error,
  Fatal,
  Remark
};

/// The serialized diagnostics version number.
enum { VersionNumber = 2 };

} // end serialized_diags namespace
} // end clang namespace

#endif
