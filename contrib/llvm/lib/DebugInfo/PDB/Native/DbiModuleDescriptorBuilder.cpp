//===- DbiModuleDescriptorBuilder.cpp - PDB Mod Info Creation ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

static uint32_t calculateDiSymbolStreamSize(uint32_t SymbolByteSize,
                                            uint32_t C13Size) {
  uint32_t Size = sizeof(uint32_t);   // Signature
  Size += alignTo(SymbolByteSize, 4); // Symbol Data
  Size += 0;                          // TODO: Layout.C11Bytes
  Size += C13Size;                    // C13 Debug Info Size
  Size += sizeof(uint32_t);           // GlobalRefs substream size (always 0)
  Size += 0;                          // GlobalRefs substream bytes
  return Size;
}

DbiModuleDescriptorBuilder::DbiModuleDescriptorBuilder(StringRef ModuleName,
                                                       uint32_t ModIndex,
                                                       msf::MSFBuilder &Msf)
    : MSF(Msf), ModuleName(ModuleName) {
  ::memset(&Layout, 0, sizeof(Layout));
  Layout.Mod = ModIndex;
}

DbiModuleDescriptorBuilder::~DbiModuleDescriptorBuilder() {}

uint16_t DbiModuleDescriptorBuilder::getStreamIndex() const {
  return Layout.ModDiStream;
}

void DbiModuleDescriptorBuilder::setObjFileName(StringRef Name) {
  ObjFileName = Name;
}

void DbiModuleDescriptorBuilder::setPdbFilePathNI(uint32_t NI) {
  PdbFilePathNI = NI;
}

void DbiModuleDescriptorBuilder::setFirstSectionContrib(
    const SectionContrib &SC) {
  Layout.SC = SC;
}

void DbiModuleDescriptorBuilder::addSymbol(CVSymbol Symbol) {
  // Defer to the bulk API. It does the same thing.
  addSymbolsInBulk(Symbol.data());
}

void DbiModuleDescriptorBuilder::addSymbolsInBulk(
    ArrayRef<uint8_t> BulkSymbols) {
  // Do nothing for empty runs of symbols.
  if (BulkSymbols.empty())
    return;

  Symbols.push_back(BulkSymbols);
  // Symbols written to a PDB file are required to be 4 byte aligned. The same
  // is not true of object files.
  assert(BulkSymbols.size() % alignOf(CodeViewContainer::Pdb) == 0 &&
         "Invalid Symbol alignment!");
  SymbolByteSize += BulkSymbols.size();
}

void DbiModuleDescriptorBuilder::addSourceFile(StringRef Path) {
  SourceFiles.push_back(Path);
}

uint32_t DbiModuleDescriptorBuilder::calculateC13DebugInfoSize() const {
  uint32_t Result = 0;
  for (const auto &Builder : C13Builders) {
    assert(Builder && "Empty C13 Fragment Builder!");
    Result += Builder->calculateSerializedLength();
  }
  return Result;
}

uint32_t DbiModuleDescriptorBuilder::calculateSerializedLength() const {
  uint32_t L = sizeof(Layout);
  uint32_t M = ModuleName.size() + 1;
  uint32_t O = ObjFileName.size() + 1;
  return alignTo(L + M + O, sizeof(uint32_t));
}

void DbiModuleDescriptorBuilder::finalize() {
  Layout.SC.Imod = Layout.Mod;
  Layout.FileNameOffs = 0; // TODO: Fix this
  Layout.Flags = 0;        // TODO: Fix this
  Layout.C11Bytes = 0;
  Layout.C13Bytes = calculateC13DebugInfoSize();
  (void)Layout.Mod;         // Set in constructor
  (void)Layout.ModDiStream; // Set in finalizeMsfLayout
  Layout.NumFiles = SourceFiles.size();
  Layout.PdbFilePathNI = PdbFilePathNI;
  Layout.SrcFileNameNI = 0;

  // This value includes both the signature field as well as the record bytes
  // from the symbol stream.
  Layout.SymBytes = SymbolByteSize + sizeof(uint32_t);
}

Error DbiModuleDescriptorBuilder::finalizeMsfLayout() {
  this->Layout.ModDiStream = kInvalidStreamIndex;
  uint32_t C13Size = calculateC13DebugInfoSize();
  auto ExpectedSN =
      MSF.addStream(calculateDiSymbolStreamSize(SymbolByteSize, C13Size));
  if (!ExpectedSN)
    return ExpectedSN.takeError();
  Layout.ModDiStream = *ExpectedSN;
  return Error::success();
}

Error DbiModuleDescriptorBuilder::commit(BinaryStreamWriter &ModiWriter,
                                         const msf::MSFLayout &MsfLayout,
                                         WritableBinaryStreamRef MsfBuffer) {
  // We write the Modi record to the `ModiWriter`, but we additionally write its
  // symbol stream to a brand new stream.
  if (auto EC = ModiWriter.writeObject(Layout))
    return EC;
  if (auto EC = ModiWriter.writeCString(ModuleName))
    return EC;
  if (auto EC = ModiWriter.writeCString(ObjFileName))
    return EC;
  if (auto EC = ModiWriter.padToAlignment(sizeof(uint32_t)))
    return EC;

  if (Layout.ModDiStream != kInvalidStreamIndex) {
    auto NS = WritableMappedBlockStream::createIndexedStream(
        MsfLayout, MsfBuffer, Layout.ModDiStream, MSF.getAllocator());
    WritableBinaryStreamRef Ref(*NS);
    BinaryStreamWriter SymbolWriter(Ref);
    // Write the symbols.
    if (auto EC =
            SymbolWriter.writeInteger<uint32_t>(COFF::DEBUG_SECTION_MAGIC))
      return EC;
    for (ArrayRef<uint8_t> Syms : Symbols) {
      if (auto EC = SymbolWriter.writeBytes(Syms))
        return EC;
    }
    assert(SymbolWriter.getOffset() % alignOf(CodeViewContainer::Pdb) == 0 &&
           "Invalid debug section alignment!");
    // TODO: Write C11 Line data
    for (const auto &Builder : C13Builders) {
      assert(Builder && "Empty C13 Fragment Builder!");
      if (auto EC = Builder->commit(SymbolWriter))
        return EC;
    }

    // TODO: Figure out what GlobalRefs substream actually is and populate it.
    if (auto EC = SymbolWriter.writeInteger<uint32_t>(0))
      return EC;
    if (SymbolWriter.bytesRemaining() > 0)
      return make_error<RawError>(raw_error_code::stream_too_long);
  }
  return Error::success();
}

void DbiModuleDescriptorBuilder::addDebugSubsection(
    std::shared_ptr<DebugSubsection> Subsection) {
  assert(Subsection);
  C13Builders.push_back(llvm::make_unique<DebugSubsectionRecordBuilder>(
      std::move(Subsection), CodeViewContainer::Pdb));
}

void DbiModuleDescriptorBuilder::addDebugSubsection(
    const DebugSubsectionRecord &SubsectionContents) {
  C13Builders.push_back(llvm::make_unique<DebugSubsectionRecordBuilder>(
      SubsectionContents, CodeViewContainer::Pdb));
}
