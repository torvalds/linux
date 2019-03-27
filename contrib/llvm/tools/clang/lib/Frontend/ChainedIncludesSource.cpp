//===- ChainedIncludesSource.cpp - Chained PCHs in Memory -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ChainedIncludesSource class, which converts headers
//  to chained PCHs in memory, mainly used for testing.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace clang;

namespace {
class ChainedIncludesSourceImpl : public ExternalSemaSource {
public:
  ChainedIncludesSourceImpl(std::vector<std::unique_ptr<CompilerInstance>> CIs)
      : CIs(std::move(CIs)) {}

protected:
  //===----------------------------------------------------------------------===//
  // ExternalASTSource interface.
  //===----------------------------------------------------------------------===//

  /// Return the amount of memory used by memory buffers, breaking down
  /// by heap-backed versus mmap'ed memory.
  void getMemoryBufferSizes(MemoryBufferSizes &sizes) const override {
    for (unsigned i = 0, e = CIs.size(); i != e; ++i) {
      if (const ExternalASTSource *eSrc =
          CIs[i]->getASTContext().getExternalSource()) {
        eSrc->getMemoryBufferSizes(sizes);
      }
    }
  }

private:
  std::vector<std::unique_ptr<CompilerInstance>> CIs;
};

/// Members of ChainedIncludesSource, factored out so we can initialize
/// them before we initialize the ExternalSemaSource base class.
struct ChainedIncludesSourceMembers {
  ChainedIncludesSourceMembers(
      std::vector<std::unique_ptr<CompilerInstance>> CIs,
      IntrusiveRefCntPtr<ExternalSemaSource> FinalReader)
      : Impl(std::move(CIs)), FinalReader(std::move(FinalReader)) {}
  ChainedIncludesSourceImpl Impl;
  IntrusiveRefCntPtr<ExternalSemaSource> FinalReader;
};

/// Use MultiplexExternalSemaSource to dispatch all ExternalSemaSource
/// calls to the final reader.
class ChainedIncludesSource
    : private ChainedIncludesSourceMembers,
      public MultiplexExternalSemaSource {
public:
  ChainedIncludesSource(std::vector<std::unique_ptr<CompilerInstance>> CIs,
                        IntrusiveRefCntPtr<ExternalSemaSource> FinalReader)
      : ChainedIncludesSourceMembers(std::move(CIs), std::move(FinalReader)),
        MultiplexExternalSemaSource(Impl, *this->FinalReader) {}
};
}

static ASTReader *
createASTReader(CompilerInstance &CI, StringRef pchFile,
                SmallVectorImpl<std::unique_ptr<llvm::MemoryBuffer>> &MemBufs,
                SmallVectorImpl<std::string> &bufNames,
                ASTDeserializationListener *deserialListener = nullptr) {
  Preprocessor &PP = CI.getPreprocessor();
  std::unique_ptr<ASTReader> Reader;
  Reader.reset(new ASTReader(PP, &CI.getASTContext(),
                             CI.getPCHContainerReader(),
                             /*Extensions=*/{ },
                             /*isysroot=*/"", /*DisableValidation=*/true));
  for (unsigned ti = 0; ti < bufNames.size(); ++ti) {
    StringRef sr(bufNames[ti]);
    Reader->addInMemoryBuffer(sr, std::move(MemBufs[ti]));
  }
  Reader->setDeserializationListener(deserialListener);
  switch (Reader->ReadAST(pchFile, serialization::MK_PCH, SourceLocation(),
                          ASTReader::ARR_None)) {
  case ASTReader::Success:
    // Set the predefines buffer as suggested by the PCH reader.
    PP.setPredefines(Reader->getSuggestedPredefines());
    return Reader.release();

  case ASTReader::Failure:
  case ASTReader::Missing:
  case ASTReader::OutOfDate:
  case ASTReader::VersionMismatch:
  case ASTReader::ConfigurationMismatch:
  case ASTReader::HadErrors:
    break;
  }
  return nullptr;
}

IntrusiveRefCntPtr<ExternalSemaSource> clang::createChainedIncludesSource(
    CompilerInstance &CI, IntrusiveRefCntPtr<ExternalSemaSource> &Reader) {

  std::vector<std::string> &includes = CI.getPreprocessorOpts().ChainedIncludes;
  assert(!includes.empty() && "No '-chain-include' in options!");

  std::vector<std::unique_ptr<CompilerInstance>> CIs;
  InputKind IK = CI.getFrontendOpts().Inputs[0].getKind();

  SmallVector<std::unique_ptr<llvm::MemoryBuffer>, 4> SerialBufs;
  SmallVector<std::string, 4> serialBufNames;

  for (unsigned i = 0, e = includes.size(); i != e; ++i) {
    bool firstInclude = (i == 0);
    std::unique_ptr<CompilerInvocation> CInvok;
    CInvok.reset(new CompilerInvocation(CI.getInvocation()));

    CInvok->getPreprocessorOpts().ChainedIncludes.clear();
    CInvok->getPreprocessorOpts().ImplicitPCHInclude.clear();
    CInvok->getPreprocessorOpts().DisablePCHValidation = true;
    CInvok->getPreprocessorOpts().Includes.clear();
    CInvok->getPreprocessorOpts().MacroIncludes.clear();
    CInvok->getPreprocessorOpts().Macros.clear();

    CInvok->getFrontendOpts().Inputs.clear();
    FrontendInputFile InputFile(includes[i], IK);
    CInvok->getFrontendOpts().Inputs.push_back(InputFile);

    TextDiagnosticPrinter *DiagClient =
      new TextDiagnosticPrinter(llvm::errs(), new DiagnosticOptions());
    IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
        new DiagnosticsEngine(DiagID, &CI.getDiagnosticOpts(), DiagClient));

    std::unique_ptr<CompilerInstance> Clang(
        new CompilerInstance(CI.getPCHContainerOperations()));
    Clang->setInvocation(std::move(CInvok));
    Clang->setDiagnostics(Diags.get());
    Clang->setTarget(TargetInfo::CreateTargetInfo(
        Clang->getDiagnostics(), Clang->getInvocation().TargetOpts));
    Clang->createFileManager();
    Clang->createSourceManager(Clang->getFileManager());
    Clang->createPreprocessor(TU_Prefix);
    Clang->getDiagnosticClient().BeginSourceFile(Clang->getLangOpts(),
                                                 &Clang->getPreprocessor());
    Clang->createASTContext();

    auto Buffer = std::make_shared<PCHBuffer>();
    ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions;
    auto consumer = llvm::make_unique<PCHGenerator>(
        Clang->getPreprocessor(), "-", /*isysroot=*/"", Buffer,
        Extensions, /*AllowASTWithErrors=*/true);
    Clang->getASTContext().setASTMutationListener(
                                            consumer->GetASTMutationListener());
    Clang->setASTConsumer(std::move(consumer));
    Clang->createSema(TU_Prefix, nullptr);

    if (firstInclude) {
      Preprocessor &PP = Clang->getPreprocessor();
      PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
                                             PP.getLangOpts());
    } else {
      assert(!SerialBufs.empty());
      SmallVector<std::unique_ptr<llvm::MemoryBuffer>, 4> Bufs;
      // TODO: Pass through the existing MemoryBuffer instances instead of
      // allocating new ones.
      for (auto &SB : SerialBufs)
        Bufs.push_back(llvm::MemoryBuffer::getMemBuffer(SB->getBuffer()));
      std::string pchName = includes[i-1];
      llvm::raw_string_ostream os(pchName);
      os << ".pch" << i-1;
      serialBufNames.push_back(os.str());

      IntrusiveRefCntPtr<ASTReader> Reader;
      Reader = createASTReader(
          *Clang, pchName, Bufs, serialBufNames,
          Clang->getASTConsumer().GetASTDeserializationListener());
      if (!Reader)
        return nullptr;
      Clang->setModuleManager(Reader);
      Clang->getASTContext().setExternalSource(Reader);
    }

    if (!Clang->InitializeSourceManager(InputFile))
      return nullptr;

    ParseAST(Clang->getSema());
    Clang->getDiagnosticClient().EndSourceFile();
    assert(Buffer->IsComplete && "serialization did not complete");
    auto &serialAST = Buffer->Data;
    SerialBufs.push_back(llvm::MemoryBuffer::getMemBufferCopy(
        StringRef(serialAST.data(), serialAST.size())));
    serialAST.clear();
    CIs.push_back(std::move(Clang));
  }

  assert(!SerialBufs.empty());
  std::string pchName = includes.back() + ".pch-final";
  serialBufNames.push_back(pchName);
  Reader = createASTReader(CI, pchName, SerialBufs, serialBufNames);
  if (!Reader)
    return nullptr;

  return IntrusiveRefCntPtr<ChainedIncludesSource>(
      new ChainedIncludesSource(std::move(CIs), Reader));
}
