//===- LTO.h ----------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides a way to combine bitcode files into one ELF
// file by compiling them using LLVM.
//
// If LTO is in use, your input files are not in regular ELF files
// but instead LLVM bitcode files. In that case, the linker has to
// convert bitcode files into the native format so that we can create
// an ELF file that contains native code. This file provides that
// functionality.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_LTO_H
#define LLD_ELF_LTO_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <vector>

namespace llvm {
namespace lto {
class LTO;
}
} // namespace llvm

namespace lld {
namespace elf {

class BitcodeFile;
class InputFile;
class LazyObjFile;

class BitcodeCompiler {
public:
  BitcodeCompiler();
  ~BitcodeCompiler();

  void add(BitcodeFile &F);
  std::vector<InputFile *> compile();

private:
  std::unique_ptr<llvm::lto::LTO> LTOObj;
  std::vector<SmallString<0>> Buf;
  std::vector<std::unique_ptr<MemoryBuffer>> Files;
  llvm::DenseSet<StringRef> UsedStartStop;
  std::unique_ptr<llvm::raw_fd_ostream> IndexFile;
  llvm::DenseSet<StringRef> ThinIndices;
};
} // namespace elf
} // namespace lld

#endif
