//===-- BuiltinGCs.h - Garbage collector linkage hacks --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains hack functions to force linking in the builtin GC
// components.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GCS_H
#define LLVM_CODEGEN_GCS_H

namespace llvm {

/// FIXME: Collector instances are not useful on their own. These no longer
///        serve any purpose except to link in the plugins.

/// Ensure the definition of the builtin GCs gets linked in
void linkAllBuiltinGCs();

/// Creates an ocaml-compatible metadata printer.
void linkOcamlGCPrinter();

/// Creates an erlang-compatible metadata printer.
void linkErlangGCPrinter();
}

#endif
