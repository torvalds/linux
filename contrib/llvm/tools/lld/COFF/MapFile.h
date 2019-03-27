//===- MapFile.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_MAPFILE_H
#define LLD_COFF_MAPFILE_H

#include "llvm/ADT/ArrayRef.h"

namespace lld {
namespace coff {
class OutputSection;
void writeMapFile(llvm::ArrayRef<OutputSection *> OutputSections);
}
}

#endif
