//===-- TestModuleFileExtension.cpp - Module Extension Tester -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "TestModuleFileExtension.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Bitcode/BitstreamWriter.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
using namespace clang;
using namespace clang::serialization;

TestModuleFileExtension::Writer::~Writer() { }

void TestModuleFileExtension::Writer::writeExtensionContents(
       Sema &SemaRef,
       llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  // Write an abbreviation for this record.
  auto Abv = std::make_shared<llvm::BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(FIRST_EXTENSION_RECORD_ID));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # of characters
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));   // message
  auto Abbrev = Stream.EmitAbbrev(std::move(Abv));

  // Write a message into the extension block.
  SmallString<64> Message;
  {
    auto Ext = static_cast<TestModuleFileExtension *>(getExtension());
    raw_svector_ostream OS(Message);
    OS << "Hello from " << Ext->BlockName << " v" << Ext->MajorVersion << "."
       << Ext->MinorVersion;
  }
  uint64_t Record[] = {FIRST_EXTENSION_RECORD_ID, Message.size()};
  Stream.EmitRecordWithBlob(Abbrev, Record, Message);
}

TestModuleFileExtension::Reader::Reader(ModuleFileExtension *Ext,
                                        const llvm::BitstreamCursor &InStream)
  : ModuleFileExtensionReader(Ext), Stream(InStream)
{
  // Read the extension block.
  SmallVector<uint64_t, 4> Record;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advanceSkippingSubblocks();
    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock:
    case llvm::BitstreamEntry::EndBlock:
    case llvm::BitstreamEntry::Error:
      return;

    case llvm::BitstreamEntry::Record:
      break;
    }

    Record.clear();
    StringRef Blob;
    unsigned RecCode = Stream.readRecord(Entry.ID, Record, &Blob);
    switch (RecCode) {
    case FIRST_EXTENSION_RECORD_ID: {
      StringRef Message = Blob.substr(0, Record[0]);
      fprintf(stderr, "Read extension block message: %s\n",
              Message.str().c_str());
      break;
    }
    }
  }
}

TestModuleFileExtension::Reader::~Reader() { }

TestModuleFileExtension::~TestModuleFileExtension() { }

ModuleFileExtensionMetadata
TestModuleFileExtension::getExtensionMetadata() const {
  return { BlockName, MajorVersion, MinorVersion, UserInfo };
}

llvm::hash_code TestModuleFileExtension::hashExtension(
                  llvm::hash_code Code) const {
  if (Hashed) {
    Code = llvm::hash_combine(Code, BlockName);
    Code = llvm::hash_combine(Code, MajorVersion);
    Code = llvm::hash_combine(Code, MinorVersion);
    Code = llvm::hash_combine(Code, UserInfo);
  }

  return Code;
}

std::unique_ptr<ModuleFileExtensionWriter>
TestModuleFileExtension::createExtensionWriter(ASTWriter &) {
  return std::unique_ptr<ModuleFileExtensionWriter>(new Writer(this));
}

std::unique_ptr<ModuleFileExtensionReader>
TestModuleFileExtension::createExtensionReader(
  const ModuleFileExtensionMetadata &Metadata,
  ASTReader &Reader, serialization::ModuleFile &Mod,
  const llvm::BitstreamCursor &Stream)
{
  assert(Metadata.BlockName == BlockName && "Wrong block name");
  if (std::make_pair(Metadata.MajorVersion, Metadata.MinorVersion) !=
        std::make_pair(MajorVersion, MinorVersion)) {
    Reader.getDiags().Report(Mod.ImportLoc,
                             diag::err_test_module_file_extension_version)
      << BlockName << Metadata.MajorVersion << Metadata.MinorVersion
      << MajorVersion << MinorVersion;
    return nullptr;
  }

  return std::unique_ptr<ModuleFileExtensionReader>(
                                                    new TestModuleFileExtension::Reader(this, Stream));
}
