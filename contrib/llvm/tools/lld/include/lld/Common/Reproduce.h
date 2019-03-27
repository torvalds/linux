//===- Reproduce.h - Utilities for creating reproducers ---------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_REPRODUCE_H
#define LLD_COMMON_REPRODUCE_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace opt { class Arg; }
}

namespace lld {

// Makes a given pathname an absolute path first, and then remove
// beginning /. For example, "../foo.o" is converted to "home/john/foo.o",
// assuming that the current directory is "/home/john/bar".
std::string relativeToRoot(StringRef Path);

// Quote a given string if it contains a space character.
std::string quote(StringRef S);

// Rewrite the given path if a file exists with that pathname, otherwise
// returns the original path.
std::string rewritePath(StringRef S);

// Returns the string form of the given argument.
std::string toString(const llvm::opt::Arg &Arg);
}

#endif
