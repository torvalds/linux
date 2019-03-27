//===- llvm-objcopy.h -------------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OBJCOPY_OBJCOPY_H
#define LLVM_TOOLS_OBJCOPY_OBJCOPY_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

namespace llvm {
namespace objcopy {

LLVM_ATTRIBUTE_NORETURN extern void error(Twine Message);
LLVM_ATTRIBUTE_NORETURN extern void reportError(StringRef File, Error E);
LLVM_ATTRIBUTE_NORETURN extern void reportError(StringRef File,
                                                std::error_code EC);

// This is taken from llvm-readobj.
// [see here](llvm/tools/llvm-readobj/llvm-readobj.h:38)
template <class T> T unwrapOrError(Expected<T> EO) {
  if (EO)
    return *EO;
  std::string Buf;
  raw_string_ostream OS(Buf);
  logAllUnhandledErrors(EO.takeError(), OS);
  OS.flush();
  error(Buf);
}

} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_TOOLS_OBJCOPY_OBJCOPY_H
