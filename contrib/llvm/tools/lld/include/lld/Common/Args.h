//===- Args.h ---------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ARGS_H
#define LLD_ARGS_H

#include "lld/Common/LLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include <vector>

namespace llvm {
namespace opt {
class InputArgList;
}
} // namespace llvm

namespace lld {
namespace args {
int getInteger(llvm::opt::InputArgList &Args, unsigned Key, int Default);
std::vector<StringRef> getStrings(llvm::opt::InputArgList &Args, int Id);

uint64_t getZOptionValue(llvm::opt::InputArgList &Args, int Id, StringRef Key,
                         uint64_t Default);

std::vector<StringRef> getLines(MemoryBufferRef MB);

StringRef getFilenameWithoutExe(StringRef Path);

} // namespace args
} // namespace lld

#endif
