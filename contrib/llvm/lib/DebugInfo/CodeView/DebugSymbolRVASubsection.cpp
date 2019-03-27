//===- DebugSymbolRVASubsection.cpp ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugSymbolRVASubsection.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include <cstdint>

using namespace llvm;
using namespace llvm::codeview;

DebugSymbolRVASubsectionRef::DebugSymbolRVASubsectionRef()
    : DebugSubsectionRef(DebugSubsectionKind::CoffSymbolRVA) {}

Error DebugSymbolRVASubsectionRef::initialize(BinaryStreamReader &Reader) {
  return Reader.readArray(RVAs, Reader.bytesRemaining() / sizeof(uint32_t));
}

DebugSymbolRVASubsection::DebugSymbolRVASubsection()
    : DebugSubsection(DebugSubsectionKind::CoffSymbolRVA) {}

Error DebugSymbolRVASubsection::commit(BinaryStreamWriter &Writer) const {
  return Writer.writeArray(makeArrayRef(RVAs));
}

uint32_t DebugSymbolRVASubsection::calculateSerializedSize() const {
  return RVAs.size() * sizeof(uint32_t);
}
