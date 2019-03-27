//===- OutputStyle.h ------------------------------------------ *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_OUTPUTSTYLE_H
#define LLVM_TOOLS_LLVMPDBDUMP_OUTPUTSTYLE_H

#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
class PDBFile;

class OutputStyle {
public:
  virtual ~OutputStyle() {}

  virtual Error dump() = 0;
};
}
}

#endif
