//===--- DebugInfoOptions.h - Debug Info Emission Types ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DEBUGINFOOPTIONS_H
#define LLVM_CLANG_BASIC_DEBUGINFOOPTIONS_H

namespace clang {
namespace codegenoptions {

enum DebugInfoFormat {
  DIF_DWARF,
  DIF_CodeView,
};

enum DebugInfoKind {
  NoDebugInfo,         /// Don't generate debug info.
  LocTrackingOnly,     /// Emit location information but do not generate
                       /// debug info in the output. This is useful in
                       /// cases where the backend wants to track source
                       /// locations for instructions without actually
                       /// emitting debug info for them (e.g., when -Rpass
                       /// is used).
  DebugDirectivesOnly, /// Emit only debug directives with the line numbers data
  DebugLineTablesOnly, /// Emit only debug info necessary for generating
                       /// line number tables (-gline-tables-only).
  LimitedDebugInfo,    /// Limit generated debug info to reduce size
                       /// (-fno-standalone-debug). This emits
                       /// forward decls for types that could be
                       /// replaced with forward decls in the source
                       /// code. For dynamic C++ classes type info
                       /// is only emitted into the module that
                       /// contains the classe's vtable.
  FullDebugInfo        /// Generate complete debug info.
};

} // end namespace codegenoptions
} // end namespace clang

#endif
