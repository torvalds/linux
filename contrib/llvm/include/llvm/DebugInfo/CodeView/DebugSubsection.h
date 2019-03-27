//===- DebugSubsection.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_MODULEDEBUGFRAGMENT_H
#define LLVM_DEBUGINFO_CODEVIEW_MODULEDEBUGFRAGMENT_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Casting.h"

namespace llvm {
namespace codeview {

class DebugSubsectionRef {
public:
  explicit DebugSubsectionRef(DebugSubsectionKind Kind) : Kind(Kind) {}
  virtual ~DebugSubsectionRef();

  static bool classof(const DebugSubsectionRef *S) { return true; }

  DebugSubsectionKind kind() const { return Kind; }

protected:
  DebugSubsectionKind Kind;
};

class DebugSubsection {
public:
  explicit DebugSubsection(DebugSubsectionKind Kind) : Kind(Kind) {}
  virtual ~DebugSubsection();

  static bool classof(const DebugSubsection *S) { return true; }

  DebugSubsectionKind kind() const { return Kind; }

  virtual Error commit(BinaryStreamWriter &Writer) const = 0;
  virtual uint32_t calculateSerializedSize() const = 0;

protected:
  DebugSubsectionKind Kind;
};

} // namespace codeview
} // namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_MODULEDEBUGFRAGMENT_H
