//===- Comdat.cpp - Implement Metadata classes ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Comdat class (including the C bindings).
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Comdat.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/Module.h"

using namespace llvm;

Comdat::Comdat(Comdat &&C) : Name(C.Name), SK(C.SK) {}

Comdat::Comdat() = default;

StringRef Comdat::getName() const { return Name->first(); }

LLVMComdatRef LLVMGetOrInsertComdat(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getOrInsertComdat(Name));
}

LLVMComdatRef LLVMGetComdat(LLVMValueRef V) {
  GlobalObject *G = unwrap<GlobalObject>(V);
  return wrap(G->getComdat());
}

void LLVMSetComdat(LLVMValueRef V, LLVMComdatRef C) {
  GlobalObject *G = unwrap<GlobalObject>(V);
  G->setComdat(unwrap(C));
}

LLVMComdatSelectionKind LLVMGetComdatSelectionKind(LLVMComdatRef C) {
  switch (unwrap(C)->getSelectionKind()) {
  case Comdat::Any:
    return LLVMAnyComdatSelectionKind;
  case Comdat::ExactMatch:
    return LLVMExactMatchComdatSelectionKind;
  case Comdat::Largest:
    return LLVMLargestComdatSelectionKind;
  case Comdat::NoDuplicates:
    return LLVMNoDuplicatesComdatSelectionKind;
  case Comdat::SameSize:
    return LLVMSameSizeComdatSelectionKind;
  }
  llvm_unreachable("Invalid Comdat SelectionKind!");
}

void LLVMSetComdatSelectionKind(LLVMComdatRef C, LLVMComdatSelectionKind kind) {
  Comdat *Cd = unwrap(C);
  switch (kind) {
  case LLVMAnyComdatSelectionKind:
    Cd->setSelectionKind(Comdat::Any);
    break;
  case LLVMExactMatchComdatSelectionKind:
    Cd->setSelectionKind(Comdat::ExactMatch);
    break;
  case LLVMLargestComdatSelectionKind:
    Cd->setSelectionKind(Comdat::Largest);
    break;
  case LLVMNoDuplicatesComdatSelectionKind:
    Cd->setSelectionKind(Comdat::NoDuplicates);
    break;
  case LLVMSameSizeComdatSelectionKind:
    Cd->setSelectionKind(Comdat::SameSize);
    break;
  }
}
