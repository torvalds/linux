//===--- ARCMT.cpp - Migration to ARC mode --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Internals.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/DiagnosticCategories.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/MemoryBuffer.h"
#include <utility>
using namespace clang;
using namespace arcmt;

bool CapturedDiagList::clearDiagnostic(ArrayRef<unsigned> IDs,
                                       SourceRange range) {
  if (range.isInvalid())
    return false;

  bool cleared = false;
  ListTy::iterator I = List.begin();
  while (I != List.end()) {
    FullSourceLoc diagLoc = I->getLocation();
    if ((IDs.empty() || // empty means clear all diagnostics in the range.
         std::find(IDs.begin(), IDs.end(), I->getID()) != IDs.end()) &&
        !diagLoc.isBeforeInTranslationUnitThan(range.getBegin()) &&
        (diagLoc == range.getEnd() ||
           diagLoc.isBeforeInTranslationUnitThan(range.getEnd()))) {
      cleared = true;
      ListTy::iterator eraseS = I++;
      if (eraseS->getLevel() != DiagnosticsEngine::Note)
        while (I != List.end() && I->getLevel() == DiagnosticsEngine::Note)
          ++I;
      // Clear the diagnostic and any notes following it.
      I = List.erase(eraseS, I);
      continue;
    }

    ++I;
  }

  return cleared;
}

bool CapturedDiagList::hasDiagnostic(ArrayRef<unsigned> IDs,
                                     SourceRange range) const {
  if (range.isInvalid())
    return false;

  ListTy::const_iterator I = List.begin();
  while (I != List.end()) {
    FullSourceLoc diagLoc = I->getLocation();
    if ((IDs.empty() || // empty means any diagnostic in the range.
         std::find(IDs.begin(), IDs.end(), I->getID()) != IDs.end()) &&
        !diagLoc.isBeforeInTranslationUnitThan(range.getBegin()) &&
        (diagLoc == range.getEnd() ||
           diagLoc.isBeforeInTranslationUnitThan(range.getEnd()))) {
      return true;
    }

    ++I;
  }

  return false;
}

void CapturedDiagList::reportDiagnostics(DiagnosticsEngine &Diags) const {
  for (ListTy::const_iterator I = List.begin(), E = List.end(); I != E; ++I)
    Diags.Report(*I);
}

bool CapturedDiagList::hasErrors() const {
  for (ListTy::const_iterator I = List.begin(), E = List.end(); I != E; ++I)
    if (I->getLevel() >= DiagnosticsEngine::Error)
      return true;

  return false;
}

namespace {

class CaptureDiagnosticConsumer : public DiagnosticConsumer {
  DiagnosticsEngine &Diags;
  DiagnosticConsumer &DiagClient;
  CapturedDiagList &CapturedDiags;
  bool HasBegunSourceFile;
public:
  CaptureDiagnosticConsumer(DiagnosticsEngine &diags,
                            DiagnosticConsumer &client,
                            CapturedDiagList &capturedDiags)
    : Diags(diags), DiagClient(client), CapturedDiags(capturedDiags),
      HasBegunSourceFile(false) { }

  void BeginSourceFile(const LangOptions &Opts,
                       const Preprocessor *PP) override {
    // Pass BeginSourceFile message onto DiagClient on first call.
    // The corresponding EndSourceFile call will be made from an
    // explicit call to FinishCapture.
    if (!HasBegunSourceFile) {
      DiagClient.BeginSourceFile(Opts, PP);
      HasBegunSourceFile = true;
    }
  }

  void FinishCapture() {
    // Call EndSourceFile on DiagClient on completion of capture to
    // enable VerifyDiagnosticConsumer to check diagnostics *after*
    // it has received the diagnostic list.
    if (HasBegunSourceFile) {
      DiagClient.EndSourceFile();
      HasBegunSourceFile = false;
    }
  }

  ~CaptureDiagnosticConsumer() override {
    assert(!HasBegunSourceFile && "FinishCapture not called!");
  }

  void HandleDiagnostic(DiagnosticsEngine::Level level,
                        const Diagnostic &Info) override {
    if (DiagnosticIDs::isARCDiagnostic(Info.getID()) ||
        level >= DiagnosticsEngine::Error || level == DiagnosticsEngine::Note) {
      if (Info.getLocation().isValid())
        CapturedDiags.push_back(StoredDiagnostic(level, Info));
      return;
    }

    // Non-ARC warnings are ignored.
    Diags.setLastDiagnosticIgnored();
  }
};

} // end anonymous namespace

static bool HasARCRuntime(CompilerInvocation &origCI) {
  // This duplicates some functionality from Darwin::AddDeploymentTarget
  // but this function is well defined, so keep it decoupled from the driver
  // and avoid unrelated complications.
  llvm::Triple triple(origCI.getTargetOpts().Triple);

  if (triple.isiOS())
    return triple.getOSMajorVersion() >= 5;

  if (triple.isWatchOS())
    return true;

  if (triple.getOS() == llvm::Triple::Darwin)
    return triple.getOSMajorVersion() >= 11;

  if (triple.getOS() == llvm::Triple::MacOSX) {
    unsigned Major, Minor, Micro;
    triple.getOSVersion(Major, Minor, Micro);
    return Major > 10 || (Major == 10 && Minor >= 7);
  }

  return false;
}

static CompilerInvocation *
createInvocationForMigration(CompilerInvocation &origCI,
                             const PCHContainerReader &PCHContainerRdr) {
  std::unique_ptr<CompilerInvocation> CInvok;
  CInvok.reset(new CompilerInvocation(origCI));
  PreprocessorOptions &PPOpts = CInvok->getPreprocessorOpts();
  if (!PPOpts.ImplicitPCHInclude.empty()) {
    // We can't use a PCH because it was likely built in non-ARC mode and we
    // want to parse in ARC. Include the original header.
    FileManager FileMgr(origCI.getFileSystemOpts());
    IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
        new DiagnosticsEngine(DiagID, &origCI.getDiagnosticOpts(),
                              new IgnoringDiagConsumer()));
    std::string OriginalFile = ASTReader::getOriginalSourceFile(
        PPOpts.ImplicitPCHInclude, FileMgr, PCHContainerRdr, *Diags);
    if (!OriginalFile.empty())
      PPOpts.Includes.insert(PPOpts.Includes.begin(), OriginalFile);
    PPOpts.ImplicitPCHInclude.clear();
  }
  std::string define = getARCMTMacroName();
  define += '=';
  CInvok->getPreprocessorOpts().addMacroDef(define);
  CInvok->getLangOpts()->ObjCAutoRefCount = true;
  CInvok->getLangOpts()->setGC(LangOptions::NonGC);
  CInvok->getDiagnosticOpts().ErrorLimit = 0;
  CInvok->getDiagnosticOpts().PedanticErrors = 0;

  // Ignore -Werror flags when migrating.
  std::vector<std::string> WarnOpts;
  for (std::vector<std::string>::iterator
         I = CInvok->getDiagnosticOpts().Warnings.begin(),
         E = CInvok->getDiagnosticOpts().Warnings.end(); I != E; ++I) {
    if (!StringRef(*I).startswith("error"))
      WarnOpts.push_back(*I);
  }
  WarnOpts.push_back("error=arc-unsafe-retained-assign");
  CInvok->getDiagnosticOpts().Warnings = std::move(WarnOpts);

  CInvok->getLangOpts()->ObjCWeakRuntime = HasARCRuntime(origCI);
  CInvok->getLangOpts()->ObjCWeak = CInvok->getLangOpts()->ObjCWeakRuntime;

  return CInvok.release();
}

static void emitPremigrationErrors(const CapturedDiagList &arcDiags,
                                   DiagnosticOptions *diagOpts,
                                   Preprocessor &PP) {
  TextDiagnosticPrinter printer(llvm::errs(), diagOpts);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, diagOpts, &printer,
                            /*ShouldOwnClient=*/false));
  Diags->setSourceManager(&PP.getSourceManager());

  printer.BeginSourceFile(PP.getLangOpts(), &PP);
  arcDiags.reportDiagnostics(*Diags);
  printer.EndSourceFile();
}

//===----------------------------------------------------------------------===//
// checkForManualIssues.
//===----------------------------------------------------------------------===//

bool arcmt::checkForManualIssues(
    CompilerInvocation &origCI, const FrontendInputFile &Input,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    DiagnosticConsumer *DiagClient, bool emitPremigrationARCErrors,
    StringRef plistOut) {
  if (!origCI.getLangOpts()->ObjC)
    return false;

  LangOptions::GCMode OrigGCMode = origCI.getLangOpts()->getGC();
  bool NoNSAllocReallocError = origCI.getMigratorOpts().NoNSAllocReallocError;
  bool NoFinalizeRemoval = origCI.getMigratorOpts().NoFinalizeRemoval;

  std::vector<TransformFn> transforms = arcmt::getAllTransformations(OrigGCMode,
                                                                     NoFinalizeRemoval);
  assert(!transforms.empty());

  std::unique_ptr<CompilerInvocation> CInvok;
  CInvok.reset(
      createInvocationForMigration(origCI, PCHContainerOps->getRawReader()));
  CInvok->getFrontendOpts().Inputs.clear();
  CInvok->getFrontendOpts().Inputs.push_back(Input);

  CapturedDiagList capturedDiags;

  assert(DiagClient);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, &origCI.getDiagnosticOpts(),
                            DiagClient, /*ShouldOwnClient=*/false));

  // Filter of all diagnostics.
  CaptureDiagnosticConsumer errRec(*Diags, *DiagClient, capturedDiags);
  Diags->setClient(&errRec, /*ShouldOwnClient=*/false);

  std::unique_ptr<ASTUnit> Unit(ASTUnit::LoadFromCompilerInvocationAction(
      std::move(CInvok), PCHContainerOps, Diags));
  if (!Unit) {
    errRec.FinishCapture();
    return true;
  }

  // Don't filter diagnostics anymore.
  Diags->setClient(DiagClient, /*ShouldOwnClient=*/false);

  ASTContext &Ctx = Unit->getASTContext();

  if (Diags->hasFatalErrorOccurred()) {
    Diags->Reset();
    DiagClient->BeginSourceFile(Ctx.getLangOpts(), &Unit->getPreprocessor());
    capturedDiags.reportDiagnostics(*Diags);
    DiagClient->EndSourceFile();
    errRec.FinishCapture();
    return true;
  }

  if (emitPremigrationARCErrors)
    emitPremigrationErrors(capturedDiags, &origCI.getDiagnosticOpts(),
                           Unit->getPreprocessor());
  if (!plistOut.empty()) {
    SmallVector<StoredDiagnostic, 8> arcDiags;
    for (CapturedDiagList::iterator
           I = capturedDiags.begin(), E = capturedDiags.end(); I != E; ++I)
      arcDiags.push_back(*I);
    writeARCDiagsToPlist(plistOut, arcDiags,
                         Ctx.getSourceManager(), Ctx.getLangOpts());
  }

  // After parsing of source files ended, we want to reuse the
  // diagnostics objects to emit further diagnostics.
  // We call BeginSourceFile because DiagnosticConsumer requires that
  // diagnostics with source range information are emitted only in between
  // BeginSourceFile() and EndSourceFile().
  DiagClient->BeginSourceFile(Ctx.getLangOpts(), &Unit->getPreprocessor());

  // No macros will be added since we are just checking and we won't modify
  // source code.
  std::vector<SourceLocation> ARCMTMacroLocs;

  TransformActions testAct(*Diags, capturedDiags, Ctx, Unit->getPreprocessor());
  MigrationPass pass(Ctx, OrigGCMode, Unit->getSema(), testAct, capturedDiags,
                     ARCMTMacroLocs);
  pass.setNoFinalizeRemoval(NoFinalizeRemoval);
  if (!NoNSAllocReallocError)
    Diags->setSeverity(diag::warn_arcmt_nsalloc_realloc, diag::Severity::Error,
                       SourceLocation());

  for (unsigned i=0, e = transforms.size(); i != e; ++i)
    transforms[i](pass);

  capturedDiags.reportDiagnostics(*Diags);

  DiagClient->EndSourceFile();
  errRec.FinishCapture();

  return capturedDiags.hasErrors() || testAct.hasReportedErrors();
}

//===----------------------------------------------------------------------===//
// applyTransformations.
//===----------------------------------------------------------------------===//

static bool
applyTransforms(CompilerInvocation &origCI, const FrontendInputFile &Input,
                std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                DiagnosticConsumer *DiagClient, StringRef outputDir,
                bool emitPremigrationARCErrors, StringRef plistOut) {
  if (!origCI.getLangOpts()->ObjC)
    return false;

  LangOptions::GCMode OrigGCMode = origCI.getLangOpts()->getGC();

  // Make sure checking is successful first.
  CompilerInvocation CInvokForCheck(origCI);
  if (arcmt::checkForManualIssues(CInvokForCheck, Input, PCHContainerOps,
                                  DiagClient, emitPremigrationARCErrors,
                                  plistOut))
    return true;

  CompilerInvocation CInvok(origCI);
  CInvok.getFrontendOpts().Inputs.clear();
  CInvok.getFrontendOpts().Inputs.push_back(Input);

  MigrationProcess migration(CInvok, PCHContainerOps, DiagClient, outputDir);
  bool NoFinalizeRemoval = origCI.getMigratorOpts().NoFinalizeRemoval;

  std::vector<TransformFn> transforms = arcmt::getAllTransformations(OrigGCMode,
                                                                     NoFinalizeRemoval);
  assert(!transforms.empty());

  for (unsigned i=0, e = transforms.size(); i != e; ++i) {
    bool err = migration.applyTransform(transforms[i]);
    if (err) return true;
  }

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, &origCI.getDiagnosticOpts(),
                            DiagClient, /*ShouldOwnClient=*/false));

  if (outputDir.empty()) {
    origCI.getLangOpts()->ObjCAutoRefCount = true;
    return migration.getRemapper().overwriteOriginal(*Diags);
  } else {
    return migration.getRemapper().flushToDisk(outputDir, *Diags);
  }
}

bool arcmt::applyTransformations(
    CompilerInvocation &origCI, const FrontendInputFile &Input,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    DiagnosticConsumer *DiagClient) {
  return applyTransforms(origCI, Input, PCHContainerOps, DiagClient,
                         StringRef(), false, StringRef());
}

bool arcmt::migrateWithTemporaryFiles(
    CompilerInvocation &origCI, const FrontendInputFile &Input,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    DiagnosticConsumer *DiagClient, StringRef outputDir,
    bool emitPremigrationARCErrors, StringRef plistOut) {
  assert(!outputDir.empty() && "Expected output directory path");
  return applyTransforms(origCI, Input, PCHContainerOps, DiagClient, outputDir,
                         emitPremigrationARCErrors, plistOut);
}

bool arcmt::getFileRemappings(std::vector<std::pair<std::string,std::string> > &
                                  remap,
                              StringRef outputDir,
                              DiagnosticConsumer *DiagClient) {
  assert(!outputDir.empty());

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, new DiagnosticOptions,
                            DiagClient, /*ShouldOwnClient=*/false));

  FileRemapper remapper;
  bool err = remapper.initFromDisk(outputDir, *Diags,
                                   /*ignoreIfFilesChanged=*/true);
  if (err)
    return true;

  PreprocessorOptions PPOpts;
  remapper.applyMappings(PPOpts);
  remap = PPOpts.RemappedFiles;

  return false;
}


//===----------------------------------------------------------------------===//
// CollectTransformActions.
//===----------------------------------------------------------------------===//

namespace {

class ARCMTMacroTrackerPPCallbacks : public PPCallbacks {
  std::vector<SourceLocation> &ARCMTMacroLocs;

public:
  ARCMTMacroTrackerPPCallbacks(std::vector<SourceLocation> &ARCMTMacroLocs)
    : ARCMTMacroLocs(ARCMTMacroLocs) { }

  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *Args) override {
    if (MacroNameTok.getIdentifierInfo()->getName() == getARCMTMacroName())
      ARCMTMacroLocs.push_back(MacroNameTok.getLocation());
  }
};

class ARCMTMacroTrackerAction : public ASTFrontendAction {
  std::vector<SourceLocation> &ARCMTMacroLocs;

public:
  ARCMTMacroTrackerAction(std::vector<SourceLocation> &ARCMTMacroLocs)
    : ARCMTMacroLocs(ARCMTMacroLocs) { }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    CI.getPreprocessor().addPPCallbacks(
               llvm::make_unique<ARCMTMacroTrackerPPCallbacks>(ARCMTMacroLocs));
    return llvm::make_unique<ASTConsumer>();
  }
};

class RewritesApplicator : public TransformActions::RewriteReceiver {
  Rewriter &rewriter;
  MigrationProcess::RewriteListener *Listener;

public:
  RewritesApplicator(Rewriter &rewriter, ASTContext &ctx,
                     MigrationProcess::RewriteListener *listener)
    : rewriter(rewriter), Listener(listener) {
    if (Listener)
      Listener->start(ctx);
  }
  ~RewritesApplicator() override {
    if (Listener)
      Listener->finish();
  }

  void insert(SourceLocation loc, StringRef text) override {
    bool err = rewriter.InsertText(loc, text, /*InsertAfter=*/true,
                                   /*indentNewLines=*/true);
    if (!err && Listener)
      Listener->insert(loc, text);
  }

  void remove(CharSourceRange range) override {
    Rewriter::RewriteOptions removeOpts;
    removeOpts.IncludeInsertsAtBeginOfRange = false;
    removeOpts.IncludeInsertsAtEndOfRange = false;
    removeOpts.RemoveLineIfEmpty = true;

    bool err = rewriter.RemoveText(range, removeOpts);
    if (!err && Listener)
      Listener->remove(range);
  }

  void increaseIndentation(CharSourceRange range,
                            SourceLocation parentIndent) override {
    rewriter.IncreaseIndentation(range, parentIndent);
  }
};

} // end anonymous namespace.

/// Anchor for VTable.
MigrationProcess::RewriteListener::~RewriteListener() { }

MigrationProcess::MigrationProcess(
    const CompilerInvocation &CI,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    DiagnosticConsumer *diagClient, StringRef outputDir)
    : OrigCI(CI), PCHContainerOps(std::move(PCHContainerOps)),
      DiagClient(diagClient), HadARCErrors(false) {
  if (!outputDir.empty()) {
    IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, &CI.getDiagnosticOpts(),
                            DiagClient, /*ShouldOwnClient=*/false));
    Remapper.initFromDisk(outputDir, *Diags, /*ignoreIfFilesChanges=*/true);
  }
}

bool MigrationProcess::applyTransform(TransformFn trans,
                                      RewriteListener *listener) {
  std::unique_ptr<CompilerInvocation> CInvok;
  CInvok.reset(
      createInvocationForMigration(OrigCI, PCHContainerOps->getRawReader()));
  CInvok->getDiagnosticOpts().IgnoreWarnings = true;

  Remapper.applyMappings(CInvok->getPreprocessorOpts());

  CapturedDiagList capturedDiags;
  std::vector<SourceLocation> ARCMTMacroLocs;

  assert(DiagClient);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, new DiagnosticOptions,
                            DiagClient, /*ShouldOwnClient=*/false));

  // Filter of all diagnostics.
  CaptureDiagnosticConsumer errRec(*Diags, *DiagClient, capturedDiags);
  Diags->setClient(&errRec, /*ShouldOwnClient=*/false);

  std::unique_ptr<ARCMTMacroTrackerAction> ASTAction;
  ASTAction.reset(new ARCMTMacroTrackerAction(ARCMTMacroLocs));

  std::unique_ptr<ASTUnit> Unit(ASTUnit::LoadFromCompilerInvocationAction(
      std::move(CInvok), PCHContainerOps, Diags, ASTAction.get()));
  if (!Unit) {
    errRec.FinishCapture();
    return true;
  }
  Unit->setOwnsRemappedFileBuffers(false); // FileRemapper manages that.

  HadARCErrors = HadARCErrors || capturedDiags.hasErrors();

  // Don't filter diagnostics anymore.
  Diags->setClient(DiagClient, /*ShouldOwnClient=*/false);

  ASTContext &Ctx = Unit->getASTContext();

  if (Diags->hasFatalErrorOccurred()) {
    Diags->Reset();
    DiagClient->BeginSourceFile(Ctx.getLangOpts(), &Unit->getPreprocessor());
    capturedDiags.reportDiagnostics(*Diags);
    DiagClient->EndSourceFile();
    errRec.FinishCapture();
    return true;
  }

  // After parsing of source files ended, we want to reuse the
  // diagnostics objects to emit further diagnostics.
  // We call BeginSourceFile because DiagnosticConsumer requires that
  // diagnostics with source range information are emitted only in between
  // BeginSourceFile() and EndSourceFile().
  DiagClient->BeginSourceFile(Ctx.getLangOpts(), &Unit->getPreprocessor());

  Rewriter rewriter(Ctx.getSourceManager(), Ctx.getLangOpts());
  TransformActions TA(*Diags, capturedDiags, Ctx, Unit->getPreprocessor());
  MigrationPass pass(Ctx, OrigCI.getLangOpts()->getGC(),
                     Unit->getSema(), TA, capturedDiags, ARCMTMacroLocs);

  trans(pass);

  {
    RewritesApplicator applicator(rewriter, Ctx, listener);
    TA.applyRewrites(applicator);
  }

  DiagClient->EndSourceFile();
  errRec.FinishCapture();

  if (DiagClient->getNumErrors())
    return true;

  for (Rewriter::buffer_iterator
        I = rewriter.buffer_begin(), E = rewriter.buffer_end(); I != E; ++I) {
    FileID FID = I->first;
    RewriteBuffer &buf = I->second;
    const FileEntry *file = Ctx.getSourceManager().getFileEntryForID(FID);
    assert(file);
    std::string newFname = file->getName();
    newFname += "-trans";
    SmallString<512> newText;
    llvm::raw_svector_ostream vecOS(newText);
    buf.write(vecOS);
    std::unique_ptr<llvm::MemoryBuffer> memBuf(
        llvm::MemoryBuffer::getMemBufferCopy(
            StringRef(newText.data(), newText.size()), newFname));
    SmallString<64> filePath(file->getName());
    Unit->getFileManager().FixupRelativePath(filePath);
    Remapper.remap(filePath.str(), std::move(memBuf));
  }

  return false;
}
