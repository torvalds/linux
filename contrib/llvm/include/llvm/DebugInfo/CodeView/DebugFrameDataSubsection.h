//===- DebugFrameDataSubsection.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGFRAMEDATASUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGFRAMEDATASUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class DebugFrameDataSubsectionRef final : public DebugSubsectionRef {
public:
  DebugFrameDataSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::FrameData) {}
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::FrameData;
  }

  Error initialize(BinaryStreamReader Reader);
  Error initialize(BinaryStreamRef Stream);

  FixedStreamArray<FrameData>::Iterator begin() const { return Frames.begin(); }
  FixedStreamArray<FrameData>::Iterator end() const { return Frames.end(); }

  const support::ulittle32_t *getRelocPtr() const { return RelocPtr; }

private:
  const support::ulittle32_t *RelocPtr = nullptr;
  FixedStreamArray<FrameData> Frames;
};

class DebugFrameDataSubsection final : public DebugSubsection {
public:
  DebugFrameDataSubsection(bool IncludeRelocPtr)
      : DebugSubsection(DebugSubsectionKind::FrameData),
        IncludeRelocPtr(IncludeRelocPtr) {}
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::FrameData;
  }

  uint32_t calculateSerializedSize() const override;
  Error commit(BinaryStreamWriter &Writer) const override;

  void addFrameData(const FrameData &Frame);
  void setFrames(ArrayRef<FrameData> Frames);

private:
  bool IncludeRelocPtr = false;
  std::vector<FrameData> Frames;
};
}
}

#endif
