//===- FunctionId.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_FUNCTIONID_H
#define LLVM_DEBUGINFO_CODEVIEW_FUNCTIONID_H

#include <cinttypes>

namespace llvm {
namespace codeview {

class FunctionId {
public:
  FunctionId() : Index(0) {}

  explicit FunctionId(uint32_t Index) : Index(Index) {}

  uint32_t getIndex() const { return Index; }

private:
  uint32_t Index;
};

inline bool operator==(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() == B.getIndex();
}

inline bool operator!=(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() != B.getIndex();
}

inline bool operator<(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() < B.getIndex();
}

inline bool operator<=(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() <= B.getIndex();
}

inline bool operator>(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() > B.getIndex();
}

inline bool operator>=(const FunctionId &A, const FunctionId &B) {
  return A.getIndex() >= B.getIndex();
}
}
}

#endif
