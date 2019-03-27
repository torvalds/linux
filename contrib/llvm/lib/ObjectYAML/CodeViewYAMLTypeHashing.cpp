//===- CodeViewYAMLTypeHashing.cpp - CodeView YAMLIO type hashing ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/CodeViewYAMLTypeHashing.h"

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::CodeViewYAML;
using namespace llvm::yaml;

namespace llvm {
namespace yaml {

void MappingTraits<DebugHSection>::mapping(IO &io, DebugHSection &DebugH) {
  io.mapRequired("Version", DebugH.Version);
  io.mapRequired("HashAlgorithm", DebugH.HashAlgorithm);
  io.mapOptional("HashValues", DebugH.Hashes);
}

void ScalarTraits<GlobalHash>::output(const GlobalHash &GH, void *Ctx,
                                      raw_ostream &OS) {
  ScalarTraits<BinaryRef>::output(GH.Hash, Ctx, OS);
}

StringRef ScalarTraits<GlobalHash>::input(StringRef Scalar, void *Ctx,
                                          GlobalHash &GH) {
  return ScalarTraits<BinaryRef>::input(Scalar, Ctx, GH.Hash);
}

} // end namespace yaml
} // end namespace llvm

DebugHSection llvm::CodeViewYAML::fromDebugH(ArrayRef<uint8_t> DebugH) {
  assert(DebugH.size() >= 8);
  assert((DebugH.size() - 8) % 8 == 0);

  BinaryStreamReader Reader(DebugH, llvm::support::little);
  DebugHSection DHS;
  cantFail(Reader.readInteger(DHS.Magic));
  cantFail(Reader.readInteger(DHS.Version));
  cantFail(Reader.readInteger(DHS.HashAlgorithm));

  while (Reader.bytesRemaining() != 0) {
    ArrayRef<uint8_t> S;
    cantFail(Reader.readBytes(S, 8));
    DHS.Hashes.emplace_back(S);
  }
  assert(Reader.bytesRemaining() == 0);
  return DHS;
}

ArrayRef<uint8_t> llvm::CodeViewYAML::toDebugH(const DebugHSection &DebugH,
                                               BumpPtrAllocator &Alloc) {
  uint32_t Size = 8 + 8 * DebugH.Hashes.size();
  uint8_t *Data = Alloc.Allocate<uint8_t>(Size);
  MutableArrayRef<uint8_t> Buffer(Data, Size);
  BinaryStreamWriter Writer(Buffer, llvm::support::little);

  cantFail(Writer.writeInteger(DebugH.Magic));
  cantFail(Writer.writeInteger(DebugH.Version));
  cantFail(Writer.writeInteger(DebugH.HashAlgorithm));
  SmallString<8> Hash;
  for (const auto &H : DebugH.Hashes) {
    Hash.clear();
    raw_svector_ostream OS(Hash);
    H.Hash.writeAsBinary(OS);
    assert((Hash.size() == 8) && "Invalid hash size!");
    cantFail(Writer.writeFixedString(Hash));
  }
  assert(Writer.bytesRemaining() == 0);
  return Buffer;
}
