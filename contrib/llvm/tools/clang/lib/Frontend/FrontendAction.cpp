//===--- FrontendAction.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/FrontendAction.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclGroup.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/LayoutOverrideSource.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Serialization/ASTDeserializationListener.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/GlobalModuleIndex.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <system_error>
using namespace clang;

LLVM_INSTANTIATE_REGISTRY(FrontendPluginRegistry)

namespace {

class DelegatingDeserializationListener : public ASTDeserializationListener {
  ASTDeserializationListener *Previous;
  bool DeletePrevious;

public:
  explicit DelegatingDeserializationListener(
      ASTDeserializationListener *Previous, bool DeletePrevious)
      : Previous(Previous), DeletePrevious(DeletePrevious) {}
  ~DelegatingDeserializationListener() override {
    if (DeletePrevious)
      delete Previous;
  }

  void ReaderInitialized(ASTReader *Reader) override {
    if (Previous)
      Previous->ReaderInitialized(Reader);
  }
  void IdentifierRead(serialization::IdentID ID,
                      IdentifierInfo *II) override {
    if (Previous)
      Previous->IdentifierRead(ID, II);
  }
  void TypeRead(serialization::TypeIdx Idx, QualType T) override {
    if (Previous)
      Previous->TypeRead(Idx, T);
  }
  void DeclRead(serialization::DeclID ID, const Decl *D) override {
    if (Previous)
      Previous->DeclRead(ID, D);
  }
  void SelectorRead(serialization::SelectorID ID, Selector Sel) override {
    if (Previous)
      Previous->SelectorRead(ID, Sel);
  }
  void MacroDefinitionRead(serialization::PreprocessedEntityID PPID,
                           MacroDefinitionRecord *MD) override {
    if (Previous)
      Previous->MacroDefinitionRead(PPID, MD);
  }
};

/// Dumps deserialized declarations.
class DeserializedDeclsDumper : public DelegatingDeserializationListener {
public:
  explicit DeserializedDeclsDumper(ASTDeserializationListener *Previous,
                                   bool DeletePrevious)
      : DelegatingDeserializationListener(Previous, DeletePrevious) {}

  void DeclRead(serialization::DeclID ID, const Decl *D) override {
    llvm::outs() << "PCH DECL: " << D->getDeclKindName();
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(D)) {
      llvm::outs() << " - ";
      ND->printQualifiedName(llvm::outs());
    }
    llvm::outs() << "\n";

    DelegatingDeserializationListener::DeclRead(ID, D);
  }
};

/// Checks deserialized declarations and emits error if a name
/// matches one given in command-line using -error-on-deserialized-decl.
class DeserializedDeclsChecker : public DelegatingDeserializationListener {
  ASTContext &Ctx;
  std::set<std::string> NamesToCheck;

public:
  DeserializedDeclsChecker(ASTContext &Ctx,
                           const std::set<std::string> &NamesToCheck,
                           ASTDeserializationListener *Previous,
                           bool DeletePrevious)
      : DelegatingDeserializationListener(Previous, DeletePrevious), Ctx(Ctx),
        NamesToCheck(NamesToCheck) {}

  void DeclRead(serialization::DeclID ID, const Decl *D) override {
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(D))
      if (NamesToCheck.find(ND->getNameAsString()) != NamesToCheck.end()) {
        unsigned DiagID
          = Ctx.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error,
                                                 "%0 was deserialized");
        Ctx.getDiagnostics().Report(Ctx.getFullLoc(D->getLocation()), DiagID)
            << ND->getNameAsString();
      }

    DelegatingDeserializationListener::DeclRead(ID, D);
  }
};

} // end anonymous namespace

FrontendAction::FrontendAction() : Instance(nullptr) {}

FrontendAction::~FrontendAction() {}

void FrontendAction::setCurrentInput(const FrontendInputFile &CurrentInput,
                                     std::unique_ptr<ASTUnit> AST) {
  this->CurrentInput = CurrentInput;
  CurrentASTUnit = std::move(AST);
}

Module *FrontendAction::getCurrentModule() const {
  CompilerInstance &CI = getCompilerInstance();
  return CI.getPreprocessor().getHeaderSearchInfo().lookupModule(
      CI.getLangOpts().CurrentModule, /*AllowSearch*/false);
}

std::unique_ptr<ASTConsumer>
FrontendAction::CreateWrappedASTConsumer(CompilerInstance &CI,
                                         StringRef InFile) {
  std::unique_ptr<ASTConsumer> Consumer = CreateASTConsumer(CI, InFile);
  if (!Consumer)
    return nullptr;

  // Validate -add-plugin args.
  bool FoundAllPlugins = true;
  for (const std::string &Arg : CI.getFrontendOpts().AddPluginActions) {
    bool Found = false;
    for (FrontendPluginRegistry::iterator it = FrontendPluginRegistry::begin(),
                                          ie = FrontendPluginRegistry::end();
         it != ie; ++it) {
      if (it->getName() == Arg)
        Found = true;
    }
    if (!Found) {
      CI.getDiagnostics().Report(diag::err_fe_invalid_plugin_name) << Arg;
      FoundAllPlugins = false;
    }
  }
  if (!FoundAllPlugins)
    return nullptr;

  // If there are no registered plugins we don't need to wrap the consumer
  if (FrontendPluginRegistry::begin() == FrontendPluginRegistry::end())
    return Consumer;

  // If this is a code completion run, avoid invoking the plugin consumers
  if (CI.hasCodeCompletionConsumer())
    return Consumer;

  // Collect the list of plugins that go before the main action (in Consumers)
  // or after it (in AfterConsumers)
  std::vector<std::unique_ptr<ASTConsumer>> Consumers;
  std::vector<std::unique_ptr<ASTConsumer>> AfterConsumers;
  for (FrontendPluginRegistry::iterator it = FrontendPluginRegistry::begin(),
                                        ie = FrontendPluginRegistry::end();
       it != ie; ++it) {
    std::unique_ptr<PluginASTAction> P = it->instantiate();
    PluginASTAction::ActionType ActionType = P->getActionType();
    if (ActionType == PluginASTAction::Cmdline) {
      // This is O(|plugins| * |add_plugins|), but since both numbers are
      // way below 50 in practice, that's ok.
      for (size_t i = 0, e = CI.getFrontendOpts().AddPluginActions.size();
           i != e; ++i) {
        if (it->getName() == CI.getFrontendOpts().AddPluginActions[i]) {
          ActionType = PluginASTAction::AddAfterMainAction;
          break;
        }
      }
    }
    if ((ActionType == PluginASTAction::AddBeforeMainAction ||
         ActionType == PluginASTAction::AddAfterMainAction) &&
        P->ParseArgs(CI, CI.getFrontendOpts().PluginArgs[it->getName()])) {
      std::unique_ptr<ASTConsumer> PluginConsumer = P->CreateASTConsumer(CI, InFile);
      if (ActionType == PluginASTAction::AddBeforeMainAction) {
        Consumers.push_back(std::move(PluginConsumer));
      } else {
        AfterConsumers.push_back(std::move(PluginConsumer));
      }
    }
  }

  // Add to Consumers the main consumer, then all the plugins that go after it
  Consumers.push_back(std::move(Consumer));
  for (auto &C : AfterConsumers) {
    Consumers.push_back(std::move(C));
  }

  return llvm::make_unique<MultiplexConsumer>(std::move(Consumers));
}

/// For preprocessed files, if the first line is the linemarker and specifies
/// the original source file name, use that name as the input file name.
/// Returns the location of the first token after the line marker directive.
///
/// \param CI The compiler instance.
/// \param InputFile Populated with the filename from the line marker.
/// \param IsModuleMap If \c true, add a line note corresponding to this line
///        directive. (We need to do this because the directive will not be
///        visited by the preprocessor.)
static SourceLocation ReadOriginalFileName(CompilerInstance &CI,
                                           std::string &InputFile,
                                           bool IsModuleMap = false) {
  auto &SourceMgr = CI.getSourceManager();
  auto MainFileID = SourceMgr.getMainFileID();

  bool Invalid = false;
  const auto *MainFileBuf = SourceMgr.getBuffer(MainFileID, &Invalid);
  if (Invalid)
    return SourceLocation();

  std::unique_ptr<Lexer> RawLexer(
      new Lexer(MainFileID, MainFileBuf, SourceMgr, CI.getLangOpts()));

  // If the first line has the syntax of
  //
  // # NUM "FILENAME"
  //
  // we use FILENAME as the input file name.
  Token T;
  if (RawLexer->LexFromRawLexer(T) || T.getKind() != tok::hash)
    return SourceLocation();
  if (RawLexer->LexFromRawLexer(T) || T.isAtStartOfLine() ||
      T.getKind() != tok::numeric_constant)
    return SourceLocation();

  unsigned LineNo;
  SourceLocation LineNoLoc = T.getLocation();
  if (IsModuleMap) {
    llvm::SmallString<16> Buffer;
    if (Lexer::getSpelling(LineNoLoc, Buffer, SourceMgr, CI.getLangOpts())
            .getAsInteger(10, LineNo))
      return SourceLocation();
  }

  RawLexer->LexFromRawLexer(T);
  if (T.isAtStartOfLine() || T.getKind() != tok::string_literal)
    return SourceLocation();

  StringLiteralParser Literal(T, CI.getPreprocessor());
  if (Literal.hadError)
    return SourceLocation();
  RawLexer->LexFromRawLexer(T);
  if (T.isNot(tok::eof) && !T.isAtStartOfLine())
    return SourceLocation();
  InputFile = Literal.GetString().str();

  if (IsModuleMap)
    CI.getSourceManager().AddLineNote(
        LineNoLoc, LineNo, SourceMgr.getLineTableFilenameID(InputFile), false,
        false, SrcMgr::C_User_ModuleMap);

  return T.getLocation();
}

static SmallVectorImpl<char> &
operator+=(SmallVectorImpl<char> &Includes, StringRef RHS) {
  Includes.append(RHS.begin(), RHS.end());
  return Includes;
}

static void addHeaderInclude(StringRef HeaderName,
                             SmallVectorImpl<char> &Includes,
                             const LangOptions &LangOpts,
                             bool IsExternC) {
  if (IsExternC && LangOpts.CPlusPlus)
    Includes += "extern \"C\" {\n";
  if (LangOpts.ObjC)
    Includes += "#import \"";
  else
    Includes += "#include \"";

  Includes += HeaderName;

  Includes += "\"\n";
  if (IsExternC && LangOpts.CPlusPlus)
    Includes += "}\n";
}

/// Collect the set of header includes needed to construct the given
/// module and update the TopHeaders file set of the module.
///
/// \param Module The module we're collecting includes from.
///
/// \param Includes Will be augmented with the set of \#includes or \#imports
/// needed to load all of the named headers.
static std::error_code collectModuleHeaderIncludes(
    const LangOptions &LangOpts, FileManager &FileMgr, DiagnosticsEngine &Diag,
    ModuleMap &ModMap, clang::Module *Module, SmallVectorImpl<char> &Includes) {
  // Don't collect any headers for unavailable modules.
  if (!Module->isAvailable())
    return std::error_code();

  // Resolve all lazy header directives to header files.
  ModMap.resolveHeaderDirectives(Module);

  // If any headers are missing, we can't build this module. In most cases,
  // diagnostics for this should have already been produced; we only get here
  // if explicit stat information was provided.
  // FIXME: If the name resolves to a file with different stat information,
  // produce a better diagnostic.
  if (!Module->MissingHeaders.empty()) {
    auto &MissingHeader = Module->MissingHeaders.front();
    Diag.Report(MissingHeader.FileNameLoc, diag::err_module_header_missing)
      << MissingHeader.IsUmbrella << MissingHeader.FileName;
    return std::error_code();
  }

  // Add includes for each of these headers.
  for (auto HK : {Module::HK_Normal, Module::HK_Private}) {
    for (Module::Header &H : Module->Headers[HK]) {
      Module->addTopHeader(H.Entry);
      // Use the path as specified in the module map file. We'll look for this
      // file relative to the module build directory (the directory containing
      // the module map file) so this will find the same file that we found
      // while parsing the module map.
      addHeaderInclude(H.NameAsWritten, Includes, LangOpts, Module->IsExternC);
    }
  }
  // Note that Module->PrivateHeaders will not be a TopHeader.

  if (Module::Header UmbrellaHeader = Module->getUmbrellaHeader()) {
    Module->addTopHeader(UmbrellaHeader.Entry);
    if (Module->Parent)
      // Include the umbrella header for submodules.
      addHeaderInclude(UmbrellaHeader.NameAsWritten, Includes, LangOpts,
                       Module->IsExternC);
  } else if (Module::DirectoryName UmbrellaDir = Module->getUmbrellaDir()) {
    // Add all of the headers we find in this subdirectory.
    std::error_code EC;
    SmallString<128> DirNative;
    llvm::sys::path::native(UmbrellaDir.Entry->getName(), DirNative);

    llvm::vfs::FileSystem &FS = *FileMgr.getVirtualFileSystem();
    for (llvm::vfs::recursive_directory_iterator Dir(FS, DirNative, EC), End;
         Dir != End && !EC; Dir.increment(EC)) {
      // Check whether this entry has an extension typically associated with
      // headers.
      if (!llvm::StringSwitch<bool>(llvm::sys::path::extension(Dir->path()))
               .Cases(".h", ".H", ".hh", ".hpp", true)
               .Default(false))
        continue;

      const FileEntry *Header = FileMgr.getFile(Dir->path());
      // FIXME: This shouldn't happen unless there is a file system race. Is
      // that worth diagnosing?
      if (!Header)
        continue;

      // If this header is marked 'unavailable' in this module, don't include
      // it.
      if (ModMap.isHeaderUnavailableInModule(Header, Module))
        continue;

      // Compute the relative path from the directory to this file.
      SmallVector<StringRef, 16> Components;
      auto PathIt = llvm::sys::path::rbegin(Dir->path());
      for (int I = 0; I != Dir.level() + 1; ++I, ++PathIt)
        Components.push_back(*PathIt);
      SmallString<128> RelativeHeader(UmbrellaDir.NameAsWritten);
      for (auto It = Components.rbegin(), End = Components.rend(); It != End;
           ++It)
        llvm::sys::path::append(RelativeHeader, *It);

      // Include this header as part of the umbrella directory.
      Module->addTopHeader(Header);
      addHeaderInclude(RelativeHeader, Includes, LangOpts, Module->IsExternC);
    }

    if (EC)
      return EC;
  }

  // Recurse into submodules.
  for (clang::Module::submodule_iterator Sub = Module->submodule_begin(),
                                      SubEnd = Module->submodule_end();
       Sub != SubEnd; ++Sub)
    if (std::error_code Err = collectModuleHeaderIncludes(
            LangOpts, FileMgr, Diag, ModMap, *Sub, Includes))
      return Err;

  return std::error_code();
}

static bool loadModuleMapForModuleBuild(CompilerInstance &CI, bool IsSystem,
                                        bool IsPreprocessed,
                                        std::string &PresumedModuleMapFile,
                                        unsigned &Offset) {
  auto &SrcMgr = CI.getSourceManager();
  HeaderSearch &HS = CI.getPreprocessor().getHeaderSearchInfo();

  // Map the current input to a file.
  FileID ModuleMapID = SrcMgr.getMainFileID();
  const FileEntry *ModuleMap = SrcMgr.getFileEntryForID(ModuleMapID);

  // If the module map is preprocessed, handle the initial line marker;
  // line directives are not part of the module map syntax in general.
  Offset = 0;
  if (IsPreprocessed) {
    SourceLocation EndOfLineMarker =
        ReadOriginalFileName(CI, PresumedModuleMapFile, /*IsModuleMap*/ true);
    if (EndOfLineMarker.isValid())
      Offset = CI.getSourceManager().getDecomposedLoc(EndOfLineMarker).second;
  }

  // Load the module map file.
  if (HS.loadModuleMapFile(ModuleMap, IsSystem, ModuleMapID, &Offset,
                           PresumedModuleMapFile))
    return true;

  if (SrcMgr.getBuffer(ModuleMapID)->getBufferSize() == Offset)
    Offset = 0;

  return false;
}

static Module *prepareToBuildModule(CompilerInstance &CI,
                                    StringRef ModuleMapFilename) {
  if (CI.getLangOpts().CurrentModule.empty()) {
    CI.getDiagnostics().Report(diag::err_missing_module_name);

    // FIXME: Eventually, we could consider asking whether there was just
    // a single module described in the module map, and use that as a
    // default. Then it would be fairly trivial to just "compile" a module
    // map with a single module (the common case).
    return nullptr;
  }

  // Dig out the module definition.
  HeaderSearch &HS = CI.getPreprocessor().getHeaderSearchInfo();
  Module *M = HS.lookupModule(CI.getLangOpts().CurrentModule,
                              /*AllowSearch=*/false);
  if (!M) {
    CI.getDiagnostics().Report(diag::err_missing_module)
      << CI.getLangOpts().CurrentModule << ModuleMapFilename;

    return nullptr;
  }

  // Check whether we can build this module at all.
  if (Preprocessor::checkModuleIsAvailable(CI.getLangOpts(), CI.getTarget(),
                                           CI.getDiagnostics(), M))
    return nullptr;

  // Inform the preprocessor that includes from within the input buffer should
  // be resolved relative to the build directory of the module map file.
  CI.getPreprocessor().setMainFileDir(M->Directory);

  // If the module was inferred from a different module map (via an expanded
  // umbrella module definition), track that fact.
  // FIXME: It would be preferable to fill this in as part of processing
  // the module map, rather than adding it after the fact.
  StringRef OriginalModuleMapName = CI.getFrontendOpts().OriginalModuleMap;
  if (!OriginalModuleMapName.empty()) {
    auto *OriginalModuleMap =
        CI.getFileManager().getFile(OriginalModuleMapName,
                                    /*openFile*/ true);
    if (!OriginalModuleMap) {
      CI.getDiagnostics().Report(diag::err_module_map_not_found)
        << OriginalModuleMapName;
      return nullptr;
    }
    if (OriginalModuleMap != CI.getSourceManager().getFileEntryForID(
                                 CI.getSourceManager().getMainFileID())) {
      M->IsInferred = true;
      CI.getPreprocessor().getHeaderSearchInfo().getModuleMap()
        .setInferredModuleAllowedBy(M, OriginalModuleMap);
    }
  }

  // If we're being run from the command-line, the module build stack will not
  // have been filled in yet, so complete it now in order to allow us to detect
  // module cycles.
  SourceManager &SourceMgr = CI.getSourceManager();
  if (SourceMgr.getModuleBuildStack().empty())
    SourceMgr.pushModuleBuildStack(CI.getLangOpts().CurrentModule,
                                   FullSourceLoc(SourceLocation(), SourceMgr));
  return M;
}

/// Compute the input buffer that should be used to build the specified module.
static std::unique_ptr<llvm::MemoryBuffer>
getInputBufferForModule(CompilerInstance &CI, Module *M) {
  FileManager &FileMgr = CI.getFileManager();

  // Collect the set of #includes we need to build the module.
  SmallString<256> HeaderContents;
  std::error_code Err = std::error_code();
  if (Module::Header UmbrellaHeader = M->getUmbrellaHeader())
    addHeaderInclude(UmbrellaHeader.NameAsWritten, HeaderContents,
                     CI.getLangOpts(), M->IsExternC);
  Err = collectModuleHeaderIncludes(
      CI.getLangOpts(), FileMgr, CI.getDiagnostics(),
      CI.getPreprocessor().getHeaderSearchInfo().getModuleMap(), M,
      HeaderContents);

  if (Err) {
    CI.getDiagnostics().Report(diag::err_module_cannot_create_includes)
      << M->getFullModuleName() << Err.message();
    return nullptr;
  }

  return llvm::MemoryBuffer::getMemBufferCopy(
      HeaderContents, Module::getModuleInputBufferName());
}

bool FrontendAction::BeginSourceFile(CompilerInstance &CI,
                                     const FrontendInputFile &RealInput) {
  FrontendInputFile Input(RealInput);
  assert(!Instance && "Already processing a source file!");
  assert(!Input.isEmpty() && "Unexpected empty filename!");
  setCurrentInput(Input);
  setCompilerInstance(&CI);

  bool HasBegunSourceFile = false;
  bool ReplayASTFile = Input.getKind().getFormat() == InputKind::Precompiled &&
                       usesPreprocessorOnly();
  if (!BeginInvocation(CI))
    goto failure;

  // If we're replaying the build of an AST file, import it and set up
  // the initial state from its build.
  if (ReplayASTFile) {
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(&CI.getDiagnostics());

    // The AST unit populates its own diagnostics engine rather than ours.
    IntrusiveRefCntPtr<DiagnosticsEngine> ASTDiags(
        new DiagnosticsEngine(Diags->getDiagnosticIDs(),
                              &Diags->getDiagnosticOptions()));
    ASTDiags->setClient(Diags->getClient(), /*OwnsClient*/false);

    // FIXME: What if the input is a memory buffer?
    StringRef InputFile = Input.getFile();

    std::unique_ptr<ASTUnit> AST = ASTUnit::LoadFromASTFile(
        InputFile, CI.getPCHContainerReader(), ASTUnit::LoadPreprocessorOnly,
        ASTDiags, CI.getFileSystemOpts(), CI.getCodeGenOpts().DebugTypeExtRefs);
    if (!AST)
      goto failure;

    // Options relating to how we treat the input (but not what we do with it)
    // are inherited from the AST unit.
    CI.getHeaderSearchOpts() = AST->getHeaderSearchOpts();
    CI.getPreprocessorOpts() = AST->getPreprocessorOpts();
    CI.getLangOpts() = AST->getLangOpts();

    // Set the shared objects, these are reset when we finish processing the
    // file, otherwise the CompilerInstance will happily destroy them.
    CI.setFileManager(&AST->getFileManager());
    CI.createSourceManager(CI.getFileManager());
    CI.getSourceManager().initializeForReplay(AST->getSourceManager());

    // Preload all the module files loaded transitively by the AST unit. Also
    // load all module map files that were parsed as part of building the AST
    // unit.
    if (auto ASTReader = AST->getASTReader()) {
      auto &MM = ASTReader->getModuleManager();
      auto &PrimaryModule = MM.getPrimaryModule();

      for (serialization::ModuleFile &MF : MM)
        if (&MF != &PrimaryModule)
          CI.getFrontendOpts().ModuleFiles.push_back(MF.FileName);

      ASTReader->visitTopLevelModuleMaps(PrimaryModule,
                                         [&](const FileEntry *FE) {
        CI.getFrontendOpts().ModuleMapFiles.push_back(FE->getName());
      });
    }

    // Set up the input file for replay purposes.
    auto Kind = AST->getInputKind();
    if (Kind.getFormat() == InputKind::ModuleMap) {
      Module *ASTModule =
          AST->getPreprocessor().getHeaderSearchInfo().lookupModule(
              AST->getLangOpts().CurrentModule, /*AllowSearch*/ false);
      assert(ASTModule && "module file does not define its own module");
      Input = FrontendInputFile(ASTModule->PresumedModuleMapFile, Kind);
    } else {
      auto &OldSM = AST->getSourceManager();
      FileID ID = OldSM.getMainFileID();
      if (auto *File = OldSM.getFileEntryForID(ID))
        Input = FrontendInputFile(File->getName(), Kind);
      else
        Input = FrontendInputFile(OldSM.getBuffer(ID), Kind);
    }
    setCurrentInput(Input, std::move(AST));
  }

  // AST files follow a very different path, since they share objects via the
  // AST unit.
  if (Input.getKind().getFormat() == InputKind::Precompiled) {
    assert(!usesPreprocessorOnly() && "this case was handled above");
    assert(hasASTFileSupport() &&
           "This action does not have AST file support!");

    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(&CI.getDiagnostics());

    // FIXME: What if the input is a memory buffer?
    StringRef InputFile = Input.getFile();

    std::unique_ptr<ASTUnit> AST = ASTUnit::LoadFromASTFile(
        InputFile, CI.getPCHContainerReader(), ASTUnit::LoadEverything, Diags,
        CI.getFileSystemOpts(), CI.getCodeGenOpts().DebugTypeExtRefs);

    if (!AST)
      goto failure;

    // Inform the diagnostic client we are processing a source file.
    CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(), nullptr);
    HasBegunSourceFile = true;

    // Set the shared objects, these are reset when we finish processing the
    // file, otherwise the CompilerInstance will happily destroy them.
    CI.setFileManager(&AST->getFileManager());
    CI.setSourceManager(&AST->getSourceManager());
    CI.setPreprocessor(AST->getPreprocessorPtr());
    Preprocessor &PP = CI.getPreprocessor();
    PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
                                           PP.getLangOpts());
    CI.setASTContext(&AST->getASTContext());

    setCurrentInput(Input, std::move(AST));

    // Initialize the action.
    if (!BeginSourceFileAction(CI))
      goto failure;

    // Create the AST consumer.
    CI.setASTConsumer(CreateWrappedASTConsumer(CI, InputFile));
    if (!CI.hasASTConsumer())
      goto failure;

    return true;
  }

  // Set up the file and source managers, if needed.
  if (!CI.hasFileManager()) {
    if (!CI.createFileManager()) {
      goto failure;
    }
  }
  if (!CI.hasSourceManager())
    CI.createSourceManager(CI.getFileManager());

  // Set up embedding for any specified files. Do this before we load any
  // source files, including the primary module map for the compilation.
  for (const auto &F : CI.getFrontendOpts().ModulesEmbedFiles) {
    if (const auto *FE = CI.getFileManager().getFile(F, /*openFile*/true))
      CI.getSourceManager().setFileIsTransient(FE);
    else
      CI.getDiagnostics().Report(diag::err_modules_embed_file_not_found) << F;
  }
  if (CI.getFrontendOpts().ModulesEmbedAllFiles)
    CI.getSourceManager().setAllFilesAreTransient(true);

  // IR files bypass the rest of initialization.
  if (Input.getKind().getLanguage() == InputKind::LLVM_IR) {
    assert(hasIRSupport() &&
           "This action does not have IR file support!");

    // Inform the diagnostic client we are processing a source file.
    CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(), nullptr);
    HasBegunSourceFile = true;

    // Initialize the action.
    if (!BeginSourceFileAction(CI))
      goto failure;

    // Initialize the main file entry.
    if (!CI.InitializeSourceManager(CurrentInput))
      goto failure;

    return true;
  }

  // If the implicit PCH include is actually a directory, rather than
  // a single file, search for a suitable PCH file in that directory.
  if (!CI.getPreprocessorOpts().ImplicitPCHInclude.empty()) {
    FileManager &FileMgr = CI.getFileManager();
    PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
    StringRef PCHInclude = PPOpts.ImplicitPCHInclude;
    std::string SpecificModuleCachePath = CI.getSpecificModuleCachePath();
    if (const DirectoryEntry *PCHDir = FileMgr.getDirectory(PCHInclude)) {
      std::error_code EC;
      SmallString<128> DirNative;
      llvm::sys::path::native(PCHDir->getName(), DirNative);
      bool Found = false;
      llvm::vfs::FileSystem &FS = *FileMgr.getVirtualFileSystem();
      for (llvm::vfs::directory_iterator Dir = FS.dir_begin(DirNative, EC),
                                         DirEnd;
           Dir != DirEnd && !EC; Dir.increment(EC)) {
        // Check whether this is an acceptable AST file.
        if (ASTReader::isAcceptableASTFile(
                Dir->path(), FileMgr, CI.getPCHContainerReader(),
                CI.getLangOpts(), CI.getTargetOpts(), CI.getPreprocessorOpts(),
                SpecificModuleCachePath)) {
          PPOpts.ImplicitPCHInclude = Dir->path();
          Found = true;
          break;
        }
      }

      if (!Found) {
        CI.getDiagnostics().Report(diag::err_fe_no_pch_in_dir) << PCHInclude;
        goto failure;
      }
    }
  }

  // Set up the preprocessor if needed. When parsing model files the
  // preprocessor of the original source is reused.
  if (!isModelParsingAction())
    CI.createPreprocessor(getTranslationUnitKind());

  // Inform the diagnostic client we are processing a source file.
  CI.getDiagnosticClient().BeginSourceFile(CI.getLangOpts(),
                                           &CI.getPreprocessor());
  HasBegunSourceFile = true;

  // Initialize the main file entry.
  if (!CI.InitializeSourceManager(Input))
    goto failure;

  // For module map files, we first parse the module map and synthesize a
  // "<module-includes>" buffer before more conventional processing.
  if (Input.getKind().getFormat() == InputKind::ModuleMap) {
    CI.getLangOpts().setCompilingModule(LangOptions::CMK_ModuleMap);

    std::string PresumedModuleMapFile;
    unsigned OffsetToContents;
    if (loadModuleMapForModuleBuild(CI, Input.isSystem(),
                                    Input.isPreprocessed(),
                                    PresumedModuleMapFile, OffsetToContents))
      goto failure;

    auto *CurrentModule = prepareToBuildModule(CI, Input.getFile());
    if (!CurrentModule)
      goto failure;

    CurrentModule->PresumedModuleMapFile = PresumedModuleMapFile;

    if (OffsetToContents)
      // If the module contents are in the same file, skip to them.
      CI.getPreprocessor().setSkipMainFilePreamble(OffsetToContents, true);
    else {
      // Otherwise, convert the module description to a suitable input buffer.
      auto Buffer = getInputBufferForModule(CI, CurrentModule);
      if (!Buffer)
        goto failure;

      // Reinitialize the main file entry to refer to the new input.
      auto Kind = CurrentModule->IsSystem ? SrcMgr::C_System : SrcMgr::C_User;
      auto &SourceMgr = CI.getSourceManager();
      auto BufferID = SourceMgr.createFileID(std::move(Buffer), Kind);
      assert(BufferID.isValid() && "couldn't creaate module buffer ID");
      SourceMgr.setMainFileID(BufferID);
    }
  }

  // Initialize the action.
  if (!BeginSourceFileAction(CI))
    goto failure;

  // If we were asked to load any module map files, do so now.
  for (const auto &Filename : CI.getFrontendOpts().ModuleMapFiles) {
    if (auto *File = CI.getFileManager().getFile(Filename))
      CI.getPreprocessor().getHeaderSearchInfo().loadModuleMapFile(
          File, /*IsSystem*/false);
    else
      CI.getDiagnostics().Report(diag::err_module_map_not_found) << Filename;
  }

  // Add a module declaration scope so that modules from -fmodule-map-file
  // arguments may shadow modules found implicitly in search paths.
  CI.getPreprocessor()
      .getHeaderSearchInfo()
      .getModuleMap()
      .finishModuleDeclarationScope();

  // Create the AST context and consumer unless this is a preprocessor only
  // action.
  if (!usesPreprocessorOnly()) {
    // Parsing a model file should reuse the existing ASTContext.
    if (!isModelParsingAction())
      CI.createASTContext();

    // For preprocessed files, check if the first line specifies the original
    // source file name with a linemarker.
    std::string PresumedInputFile = getCurrentFileOrBufferName();
    if (Input.isPreprocessed())
      ReadOriginalFileName(CI, PresumedInputFile);

    std::unique_ptr<ASTConsumer> Consumer =
        CreateWrappedASTConsumer(CI, PresumedInputFile);
    if (!Consumer)
      goto failure;

    // FIXME: should not overwrite ASTMutationListener when parsing model files?
    if (!isModelParsingAction())
      CI.getASTContext().setASTMutationListener(Consumer->GetASTMutationListener());

    if (!CI.getPreprocessorOpts().ChainedIncludes.empty()) {
      // Convert headers to PCH and chain them.
      IntrusiveRefCntPtr<ExternalSemaSource> source, FinalReader;
      source = createChainedIncludesSource(CI, FinalReader);
      if (!source)
        goto failure;
      CI.setModuleManager(static_cast<ASTReader *>(FinalReader.get()));
      CI.getASTContext().setExternalSource(source);
    } else if (CI.getLangOpts().Modules ||
               !CI.getPreprocessorOpts().ImplicitPCHInclude.empty()) {
      // Use PCM or PCH.
      assert(hasPCHSupport() && "This action does not have PCH support!");
      ASTDeserializationListener *DeserialListener =
          Consumer->GetASTDeserializationListener();
      bool DeleteDeserialListener = false;
      if (CI.getPreprocessorOpts().DumpDeserializedPCHDecls) {
        DeserialListener = new DeserializedDeclsDumper(DeserialListener,
                                                       DeleteDeserialListener);
        DeleteDeserialListener = true;
      }
      if (!CI.getPreprocessorOpts().DeserializedPCHDeclsToErrorOn.empty()) {
        DeserialListener = new DeserializedDeclsChecker(
            CI.getASTContext(),
            CI.getPreprocessorOpts().DeserializedPCHDeclsToErrorOn,
            DeserialListener, DeleteDeserialListener);
        DeleteDeserialListener = true;
      }
      if (!CI.getPreprocessorOpts().ImplicitPCHInclude.empty()) {
        CI.createPCHExternalASTSource(
            CI.getPreprocessorOpts().ImplicitPCHInclude,
            CI.getPreprocessorOpts().DisablePCHValidation,
          CI.getPreprocessorOpts().AllowPCHWithCompilerErrors, DeserialListener,
            DeleteDeserialListener);
        if (!CI.getASTContext().getExternalSource())
          goto failure;
      }
      // If modules are enabled, create the module manager before creating
      // any builtins, so that all declarations know that they might be
      // extended by an external source.
      if (CI.getLangOpts().Modules || !CI.hasASTContext() ||
          !CI.getASTContext().getExternalSource()) {
        CI.createModuleManager();
        CI.getModuleManager()->setDeserializationListener(DeserialListener,
                                                        DeleteDeserialListener);
      }
    }

    CI.setASTConsumer(std::move(Consumer));
    if (!CI.hasASTConsumer())
      goto failure;
  }

  // Initialize built-in info as long as we aren't using an external AST
  // source.
  if (CI.getLangOpts().Modules || !CI.hasASTContext() ||
      !CI.getASTContext().getExternalSource()) {
    Preprocessor &PP = CI.getPreprocessor();
    PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
                                           PP.getLangOpts());
  } else {
    // FIXME: If this is a problem, recover from it by creating a multiplex
    // source.
    assert((!CI.getLangOpts().Modules || CI.getModuleManager()) &&
           "modules enabled but created an external source that "
           "doesn't support modules");
  }

  // If we were asked to load any module files, do so now.
  for (const auto &ModuleFile : CI.getFrontendOpts().ModuleFiles)
    if (!CI.loadModuleFile(ModuleFile))
      goto failure;

  // If there is a layout overrides file, attach an external AST source that
  // provides the layouts from that file.
  if (!CI.getFrontendOpts().OverrideRecordLayoutsFile.empty() &&
      CI.hasASTContext() && !CI.getASTContext().getExternalSource()) {
    IntrusiveRefCntPtr<ExternalASTSource>
      Override(new LayoutOverrideSource(
                     CI.getFrontendOpts().OverrideRecordLayoutsFile));
    CI.getASTContext().setExternalSource(Override);
  }

  return true;

  // If we failed, reset state since the client will not end up calling the
  // matching EndSourceFile().
failure:
  if (HasBegunSourceFile)
    CI.getDiagnosticClient().EndSourceFile();
  CI.clearOutputFiles(/*EraseFiles=*/true);
  CI.getLangOpts().setCompilingModule(LangOptions::CMK_None);
  setCurrentInput(FrontendInputFile());
  setCompilerInstance(nullptr);
  return false;
}

bool FrontendAction::Execute() {
  CompilerInstance &CI = getCompilerInstance();

  if (CI.hasFrontendTimer()) {
    llvm::TimeRegion Timer(CI.getFrontendTimer());
    ExecuteAction();
  }
  else ExecuteAction();

  // If we are supposed to rebuild the global module index, do so now unless
  // there were any module-build failures.
  if (CI.shouldBuildGlobalModuleIndex() && CI.hasFileManager() &&
      CI.hasPreprocessor()) {
    StringRef Cache =
        CI.getPreprocessor().getHeaderSearchInfo().getModuleCachePath();
    if (!Cache.empty())
      GlobalModuleIndex::writeIndex(CI.getFileManager(),
                                    CI.getPCHContainerReader(), Cache);
  }

  return true;
}

void FrontendAction::EndSourceFile() {
  CompilerInstance &CI = getCompilerInstance();

  // Inform the diagnostic client we are done with this source file.
  CI.getDiagnosticClient().EndSourceFile();

  // Inform the preprocessor we are done.
  if (CI.hasPreprocessor())
    CI.getPreprocessor().EndSourceFile();

  // Finalize the action.
  EndSourceFileAction();

  // Sema references the ast consumer, so reset sema first.
  //
  // FIXME: There is more per-file stuff we could just drop here?
  bool DisableFree = CI.getFrontendOpts().DisableFree;
  if (DisableFree) {
    CI.resetAndLeakSema();
    CI.resetAndLeakASTContext();
    llvm::BuryPointer(CI.takeASTConsumer().get());
  } else {
    CI.setSema(nullptr);
    CI.setASTContext(nullptr);
    CI.setASTConsumer(nullptr);
  }

  if (CI.getFrontendOpts().ShowStats) {
    llvm::errs() << "\nSTATISTICS FOR '" << getCurrentFile() << "':\n";
    CI.getPreprocessor().PrintStats();
    CI.getPreprocessor().getIdentifierTable().PrintStats();
    CI.getPreprocessor().getHeaderSearchInfo().PrintStats();
    CI.getSourceManager().PrintStats();
    llvm::errs() << "\n";
  }

  // Cleanup the output streams, and erase the output files if instructed by the
  // FrontendAction.
  CI.clearOutputFiles(/*EraseFiles=*/shouldEraseOutputFiles());

  if (isCurrentFileAST()) {
    if (DisableFree) {
      CI.resetAndLeakPreprocessor();
      CI.resetAndLeakSourceManager();
      CI.resetAndLeakFileManager();
      llvm::BuryPointer(std::move(CurrentASTUnit));
    } else {
      CI.setPreprocessor(nullptr);
      CI.setSourceManager(nullptr);
      CI.setFileManager(nullptr);
    }
  }

  setCompilerInstance(nullptr);
  setCurrentInput(FrontendInputFile());
  CI.getLangOpts().setCompilingModule(LangOptions::CMK_None);
}

bool FrontendAction::shouldEraseOutputFiles() {
  return getCompilerInstance().getDiagnostics().hasErrorOccurred();
}

//===----------------------------------------------------------------------===//
// Utility Actions
//===----------------------------------------------------------------------===//

void ASTFrontendAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  if (!CI.hasPreprocessor())
    return;

  // FIXME: Move the truncation aspect of this into Sema, we delayed this till
  // here so the source manager would be initialized.
  if (hasCodeCompletionSupport() &&
      !CI.getFrontendOpts().CodeCompletionAt.FileName.empty())
    CI.createCodeCompletionConsumer();

  // Use a code completion consumer?
  CodeCompleteConsumer *CompletionConsumer = nullptr;
  if (CI.hasCodeCompletionConsumer())
    CompletionConsumer = &CI.getCodeCompletionConsumer();

  if (!CI.hasSema())
    CI.createSema(getTranslationUnitKind(), CompletionConsumer);

  ParseAST(CI.getSema(), CI.getFrontendOpts().ShowStats,
           CI.getFrontendOpts().SkipFunctionBodies);
}

void PluginASTAction::anchor() { }

std::unique_ptr<ASTConsumer>
PreprocessorFrontendAction::CreateASTConsumer(CompilerInstance &CI,
                                              StringRef InFile) {
  llvm_unreachable("Invalid CreateASTConsumer on preprocessor action!");
}

std::unique_ptr<ASTConsumer>
WrapperFrontendAction::CreateASTConsumer(CompilerInstance &CI,
                                         StringRef InFile) {
  return WrappedAction->CreateASTConsumer(CI, InFile);
}
bool WrapperFrontendAction::BeginInvocation(CompilerInstance &CI) {
  return WrappedAction->BeginInvocation(CI);
}
bool WrapperFrontendAction::BeginSourceFileAction(CompilerInstance &CI) {
  WrappedAction->setCurrentInput(getCurrentInput());
  WrappedAction->setCompilerInstance(&CI);
  auto Ret = WrappedAction->BeginSourceFileAction(CI);
  // BeginSourceFileAction may change CurrentInput, e.g. during module builds.
  setCurrentInput(WrappedAction->getCurrentInput());
  return Ret;
}
void WrapperFrontendAction::ExecuteAction() {
  WrappedAction->ExecuteAction();
}
void WrapperFrontendAction::EndSourceFileAction() {
  WrappedAction->EndSourceFileAction();
}

bool WrapperFrontendAction::usesPreprocessorOnly() const {
  return WrappedAction->usesPreprocessorOnly();
}
TranslationUnitKind WrapperFrontendAction::getTranslationUnitKind() {
  return WrappedAction->getTranslationUnitKind();
}
bool WrapperFrontendAction::hasPCHSupport() const {
  return WrappedAction->hasPCHSupport();
}
bool WrapperFrontendAction::hasASTFileSupport() const {
  return WrappedAction->hasASTFileSupport();
}
bool WrapperFrontendAction::hasIRSupport() const {
  return WrappedAction->hasIRSupport();
}
bool WrapperFrontendAction::hasCodeCompletionSupport() const {
  return WrappedAction->hasCodeCompletionSupport();
}

WrapperFrontendAction::WrapperFrontendAction(
    std::unique_ptr<FrontendAction> WrappedAction)
  : WrappedAction(std::move(WrappedAction)) {}

