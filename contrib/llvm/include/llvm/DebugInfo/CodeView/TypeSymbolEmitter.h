//===- TypeSymbolEmitter.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPESYMBOLEMITTER_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPESYMBOLEMITTER_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"

namespace llvm {
class StringRef;

namespace codeview {

class TypeSymbolEmitter {
private:
  TypeSymbolEmitter(const TypeSymbolEmitter &) = delete;
  TypeSymbolEmitter &operator=(const TypeSymbolEmitter &) = delete;

protected:
  TypeSymbolEmitter() {}

public:
  virtual ~TypeSymbolEmitter() {}

public:
  virtual void writeUserDefinedType(TypeIndex TI, StringRef Name) = 0;
};
}
}

#endif
