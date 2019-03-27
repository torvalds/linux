//===- TypeCollection.h - A collection of CodeView type records -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPECOLLECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPECOLLECTION_H

#include "llvm/ADT/StringRef.h"

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"

namespace llvm {
namespace codeview {
class TypeCollection {
public:
  virtual ~TypeCollection() = default;

  bool empty() { return size() == 0; }

  virtual Optional<TypeIndex> getFirst() = 0;
  virtual Optional<TypeIndex> getNext(TypeIndex Prev) = 0;

  virtual CVType getType(TypeIndex Index) = 0;
  virtual StringRef getTypeName(TypeIndex Index) = 0;
  virtual bool contains(TypeIndex Index) = 0;
  virtual uint32_t size() = 0;
  virtual uint32_t capacity() = 0;

  template <typename TFunc> void ForEachRecord(TFunc Func) {
    Optional<TypeIndex> Next = getFirst();

    while (Next.hasValue()) {
      TypeIndex N = *Next;
      Func(N, getType(N));
      Next = getNext(N);
    }
  }
};
}
}

#endif
