//===- lld/Common/Driver.h - Linker Driver Emulator -----------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_DRIVER_H
#define LLD_COMMON_DRIVER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

namespace lld {
namespace coff {
bool link(llvm::ArrayRef<const char *> Args, bool CanExitEarly,
          llvm::raw_ostream &Diag = llvm::errs());
}

namespace mingw {
bool link(llvm::ArrayRef<const char *> Args,
          llvm::raw_ostream &Diag = llvm::errs());
}

namespace elf {
bool link(llvm::ArrayRef<const char *> Args, bool CanExitEarly,
          llvm::raw_ostream &Diag = llvm::errs());
}

namespace mach_o {
bool link(llvm::ArrayRef<const char *> Args, bool CanExitEarly,
          llvm::raw_ostream &Diag = llvm::errs());
}

namespace wasm {
bool link(llvm::ArrayRef<const char *> Args, bool CanExitEarly,
          llvm::raw_ostream &Diag = llvm::errs());
}
}

#endif
