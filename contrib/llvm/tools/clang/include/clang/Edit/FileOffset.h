//===- FileOffset.h - Offset in a file --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EDIT_FILEOFFSET_H
#define LLVM_CLANG_EDIT_FILEOFFSET_H

#include "clang/Basic/SourceLocation.h"
#include <tuple>

namespace clang {
namespace edit {

class FileOffset {
  FileID FID;
  unsigned Offs = 0;

public:
  FileOffset() = default;
  FileOffset(FileID fid, unsigned offs) : FID(fid), Offs(offs) {}

  bool isInvalid() const { return FID.isInvalid(); }

  FileID getFID() const { return FID; }
  unsigned getOffset() const { return Offs; }

  FileOffset getWithOffset(unsigned offset) const {
    FileOffset NewOffs = *this;
    NewOffs.Offs += offset;
    return NewOffs;
  }

  friend bool operator==(FileOffset LHS, FileOffset RHS) {
    return LHS.FID == RHS.FID && LHS.Offs == RHS.Offs;
  }

  friend bool operator!=(FileOffset LHS, FileOffset RHS) {
    return !(LHS == RHS);
  }

  friend bool operator<(FileOffset LHS, FileOffset RHS) {
    return std::tie(LHS.FID, LHS.Offs) < std::tie(RHS.FID, RHS.Offs);
  }

  friend bool operator>(FileOffset LHS, FileOffset RHS) {
    return RHS < LHS;
  }

  friend bool operator>=(FileOffset LHS, FileOffset RHS) {
    return !(LHS < RHS);
  }

  friend bool operator<=(FileOffset LHS, FileOffset RHS) {
    return !(RHS < LHS);
  }
};

} // namespace edit
} // namespace clang

#endif // LLVM_CLANG_EDIT_FILEOFFSET_H
