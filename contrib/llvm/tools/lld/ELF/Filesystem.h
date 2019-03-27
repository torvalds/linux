//===- Filesystem.h ---------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_FILESYSTEM_H
#define LLD_ELF_FILESYSTEM_H

#include "lld/Common/LLVM.h"
#include <system_error>

namespace lld {
namespace elf {
void unlinkAsync(StringRef Path);
std::error_code tryCreateFile(StringRef Path);
} // namespace elf
} // namespace lld

#endif
