//===- llvm/Codegen/LinkAllAsmWriterComponents.h ----------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file pulls in all assembler writer related passes for tools like
// llc that need this functionality.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LINKALLASMWRITERCOMPONENTS_H
#define LLVM_CODEGEN_LINKALLASMWRITERCOMPONENTS_H

#include "llvm/CodeGen/BuiltinGCs.h"
#include <cstdlib>

namespace {
  struct ForceAsmWriterLinking {
    ForceAsmWriterLinking() {
      // We must reference the plug-ins in such a way that compilers will not
      // delete it all as dead code, even with whole program optimization,
      // yet is effectively a NO-OP. As the compiler isn't smart enough
      // to know that getenv() never returns -1, this will do the job.
      if (std::getenv("bar") != (char*) -1)
        return;

      llvm::linkOcamlGCPrinter();
      llvm::linkErlangGCPrinter();

    }
  } ForceAsmWriterLinking; // Force link by creating a global definition.
}

#endif // LLVM_CODEGEN_LINKALLASMWRITERCOMPONENTS_H
