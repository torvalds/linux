//===-- TestModuleFileExtension.h - Module Extension Tester -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_FRONTEND_TESTMODULEFILEEXTENSION_H
#define LLVM_CLANG_FRONTEND_TESTMODULEFILEEXTENSION_H

#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include <string>

namespace clang {

/// A module file extension used for testing purposes.
class TestModuleFileExtension : public ModuleFileExtension {
  std::string BlockName;
  unsigned MajorVersion;
  unsigned MinorVersion;
  bool Hashed;
  std::string UserInfo;

  class Writer : public ModuleFileExtensionWriter {
  public:
    Writer(ModuleFileExtension *Ext) : ModuleFileExtensionWriter(Ext) { }
    ~Writer() override;

    void writeExtensionContents(Sema &SemaRef,
                                llvm::BitstreamWriter &Stream) override;
  };

  class Reader : public ModuleFileExtensionReader {
    llvm::BitstreamCursor Stream;

  public:
    ~Reader() override;

    Reader(ModuleFileExtension *Ext, const llvm::BitstreamCursor &InStream);
  };

public:
  TestModuleFileExtension(StringRef BlockName,
                          unsigned MajorVersion,
                          unsigned MinorVersion,
                          bool Hashed,
                          StringRef UserInfo)
    : BlockName(BlockName),
      MajorVersion(MajorVersion), MinorVersion(MinorVersion),
      Hashed(Hashed), UserInfo(UserInfo) { }
  ~TestModuleFileExtension() override;

  ModuleFileExtensionMetadata getExtensionMetadata() const override;

  llvm::hash_code hashExtension(llvm::hash_code Code) const override;

  std::unique_ptr<ModuleFileExtensionWriter>
  createExtensionWriter(ASTWriter &Writer) override;

  std::unique_ptr<ModuleFileExtensionReader>
  createExtensionReader(const ModuleFileExtensionMetadata &Metadata,
                        ASTReader &Reader, serialization::ModuleFile &Mod,
                        const llvm::BitstreamCursor &Stream) override;
};

} // end namespace clang

#endif // LLVM_CLANG_FRONTEND_TESTMODULEFILEEXTENSION_H
