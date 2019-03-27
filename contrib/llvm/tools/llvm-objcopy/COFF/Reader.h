//===- Reader.h -------------------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OBJCOPY_COFF_READER_H
#define LLVM_TOOLS_OBJCOPY_COFF_READER_H

#include "Buffer.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace objcopy {
namespace coff {

struct Object;

using object::COFFObjectFile;

class COFFReader {
  const COFFObjectFile &COFFObj;

  Error readExecutableHeaders(Object &Obj) const;
  Error readSections(Object &Obj) const;
  Error readSymbols(Object &Obj, bool IsBigObj) const;
  Error setRelocTargets(Object &Obj) const;

public:
  explicit COFFReader(const COFFObjectFile &O) : COFFObj(O) {}
  Expected<std::unique_ptr<Object>> create() const;
};

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_TOOLS_OBJCOPY_COFF_READER_H
