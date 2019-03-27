//===- MarkLive.h -----------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_MARKLIVE_H
#define LLD_COFF_MARKLIVE_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"

namespace lld {
namespace coff {

void markLive(ArrayRef<Chunk *> Chunks);

} // namespace coff
} // namespace lld

#endif // LLD_COFF_MARKLIVE_H
