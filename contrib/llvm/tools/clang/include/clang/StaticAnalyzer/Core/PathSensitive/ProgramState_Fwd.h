//== ProgramState_Fwd.h - Incomplete declarations of ProgramState -*- C++ -*--=/
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_PROGRAMSTATE_FWD_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_PROGRAMSTATE_FWD_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

namespace clang {
namespace ento {
  class ProgramState;
  class ProgramStateManager;
  void ProgramStateRetain(const ProgramState *state);
  void ProgramStateRelease(const ProgramState *state);
}
}

namespace llvm {
  template <> struct IntrusiveRefCntPtrInfo<const clang::ento::ProgramState> {
    static void retain(const clang::ento::ProgramState *state) {
      clang::ento::ProgramStateRetain(state);
    }
    static void release(const clang::ento::ProgramState *state) {
      clang::ento::ProgramStateRelease(state);
    }
  };
}

namespace clang {
namespace ento {
  typedef IntrusiveRefCntPtr<const ProgramState> ProgramStateRef;
}
}

#endif

