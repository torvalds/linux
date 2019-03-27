//===- DebugInlineeLinesSubsection.cpp ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace llvm::codeview;

Error VarStreamArrayExtractor<InlineeSourceLine>::
operator()(BinaryStreamRef Stream, uint32_t &Len, InlineeSourceLine &Item) {
  BinaryStreamReader Reader(Stream);

  if (auto EC = Reader.readObject(Item.Header))
    return EC;

  if (HasExtraFiles) {
    uint32_t ExtraFileCount;
    if (auto EC = Reader.readInteger(ExtraFileCount))
      return EC;
    if (auto EC = Reader.readArray(Item.ExtraFiles, ExtraFileCount))
      return EC;
  }

  Len = Reader.getOffset();
  return Error::success();
}

DebugInlineeLinesSubsectionRef::DebugInlineeLinesSubsectionRef()
    : DebugSubsectionRef(DebugSubsectionKind::InlineeLines) {}

Error DebugInlineeLinesSubsectionRef::initialize(BinaryStreamReader Reader) {
  if (auto EC = Reader.readEnum(Signature))
    return EC;

  Lines.getExtractor().HasExtraFiles = hasExtraFiles();
  if (auto EC = Reader.readArray(Lines, Reader.bytesRemaining()))
    return EC;

  assert(Reader.bytesRemaining() == 0);
  return Error::success();
}

bool DebugInlineeLinesSubsectionRef::hasExtraFiles() const {
  return Signature == InlineeLinesSignature::ExtraFiles;
}

DebugInlineeLinesSubsection::DebugInlineeLinesSubsection(
    DebugChecksumsSubsection &Checksums, bool HasExtraFiles)
    : DebugSubsection(DebugSubsectionKind::InlineeLines), Checksums(Checksums),
      HasExtraFiles(HasExtraFiles) {}

uint32_t DebugInlineeLinesSubsection::calculateSerializedSize() const {
  // 4 bytes for the signature
  uint32_t Size = sizeof(InlineeLinesSignature);

  // one header for each entry.
  Size += Entries.size() * sizeof(InlineeSourceLineHeader);
  if (HasExtraFiles) {
    // If extra files are enabled, one count for each entry.
    Size += Entries.size() * sizeof(uint32_t);

    // And one file id for each file.
    Size += ExtraFileCount * sizeof(uint32_t);
  }
  assert(Size % 4 == 0);
  return Size;
}

Error DebugInlineeLinesSubsection::commit(BinaryStreamWriter &Writer) const {
  InlineeLinesSignature Sig = InlineeLinesSignature::Normal;
  if (HasExtraFiles)
    Sig = InlineeLinesSignature::ExtraFiles;

  if (auto EC = Writer.writeEnum(Sig))
    return EC;

  for (const auto &E : Entries) {
    if (auto EC = Writer.writeObject(E.Header))
      return EC;

    if (!HasExtraFiles)
      continue;

    if (auto EC = Writer.writeInteger<uint32_t>(E.ExtraFiles.size()))
      return EC;
    if (auto EC = Writer.writeArray(makeArrayRef(E.ExtraFiles)))
      return EC;
  }

  return Error::success();
}

void DebugInlineeLinesSubsection::addExtraFile(StringRef FileName) {
  uint32_t Offset = Checksums.mapChecksumOffset(FileName);

  auto &Entry = Entries.back();
  Entry.ExtraFiles.push_back(ulittle32_t(Offset));
  ++ExtraFileCount;
}

void DebugInlineeLinesSubsection::addInlineSite(TypeIndex FuncId,
                                                StringRef FileName,
                                                uint32_t SourceLine) {
  uint32_t Offset = Checksums.mapChecksumOffset(FileName);

  Entries.emplace_back();
  auto &Entry = Entries.back();
  Entry.Header.FileID = Offset;
  Entry.Header.SourceLineNum = SourceLine;
  Entry.Header.Inlinee = FuncId;
}
