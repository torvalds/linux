//===- DebugFrameDataSubsection.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"

using namespace llvm;
using namespace llvm::codeview;

Error DebugFrameDataSubsectionRef::initialize(BinaryStreamReader Reader) {
  if (Reader.bytesRemaining() % sizeof(FrameData) != 0) {
    if (auto EC = Reader.readObject(RelocPtr))
      return EC;
  }

  if (Reader.bytesRemaining() % sizeof(FrameData) != 0)
    return make_error<CodeViewError>(cv_error_code::corrupt_record,
                                     "Invalid frame data record format!");

  uint32_t Count = Reader.bytesRemaining() / sizeof(FrameData);
  if (auto EC = Reader.readArray(Frames, Count))
    return EC;
  return Error::success();
}

Error DebugFrameDataSubsectionRef::initialize(BinaryStreamRef Section) {
  BinaryStreamReader Reader(Section);
  return initialize(Reader);
}

uint32_t DebugFrameDataSubsection::calculateSerializedSize() const {
  uint32_t Size = sizeof(FrameData) * Frames.size();
  if (IncludeRelocPtr)
    Size += sizeof(uint32_t);
  return Size;
}

Error DebugFrameDataSubsection::commit(BinaryStreamWriter &Writer) const {
  if (IncludeRelocPtr) {
    if (auto EC = Writer.writeInteger<uint32_t>(0))
      return EC;
  }

  std::vector<FrameData> SortedFrames(Frames.begin(), Frames.end());
  std::sort(SortedFrames.begin(), SortedFrames.end(),
            [](const FrameData &LHS, const FrameData &RHS) {
              return LHS.RvaStart < RHS.RvaStart;
            });
  if (auto EC = Writer.writeArray(makeArrayRef(SortedFrames)))
    return EC;
  return Error::success();
}

void DebugFrameDataSubsection::addFrameData(const FrameData &Frame) {
  Frames.push_back(Frame);
}
