//===--- FrontendActions.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/FrontendActions.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/YAMLTraits.h"
#include <memory>
#include <system_error>

using namespace clang;

namespace {
CodeCompleteConsumer *GetCodeCompletionConsumer(CompilerInstance &CI) {
  return CI.hasCodeCompletionConsumer() ? &CI.getCodeCompletionConsumer()
                                        : nullptr;
}

void EnsureSemaIsCreated(CompilerInstance &CI, FrontendAction &Action) {
  if (Action.hasCodeCompletionSupport() &&
      !CI.getFrontendOpts().CodeCompletionAt.FileName.empty())
    CI.createCodeCompletionConsumer();

  if (!CI.hasSema())
    CI.createSema(Action.getTranslationUnitKind(),
                  GetCodeCompletionConsumer(CI));
}
} // namespace

//===----------------------------------------------------------------------===//
// Custom Actions
//===----------------------------------------------------------------------===//

std::unique_ptr<ASTConsumer>
InitOnlyAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return llvm::make_unique<ASTConsumer>();
}

void InitOnlyAction::ExecuteAction() {
}

//===----------------------------------------------------------------------===//
// AST Consumer Actions
//===----------------------------------------------------------------------===//

std::unique_ptr<ASTConsumer>
ASTPrintAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  if (std::unique_ptr<raw_ostream> OS =
          CI.createDefaultOutputFile(false, InFile))
    return CreateASTPrinter(std::move(OS), CI.getFrontendOpts().ASTDumpFilter);
  return nullptr;
}

std::unique_ptr<ASTConsumer>
ASTDumpAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return CreateASTDumper(nullptr /*Dump to stdout.*/,
                         CI.getFrontendOpts().ASTDumpFilter,
                         CI.getFrontendOpts().ASTDumpDecls,
                         CI.getFrontendOpts().ASTDumpAll,
                         CI.getFrontendOpts().ASTDumpLookups);
}

std::unique_ptr<ASTConsumer>
ASTDeclListAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return CreateASTDeclNodeLister();
}

std::unique_ptr<ASTConsumer>
ASTViewAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return CreateASTViewer();
}

std::unique_ptr<ASTConsumer>
GeneratePCHAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  std::string Sysroot;
  if (!ComputeASTConsumerArguments(CI, /*ref*/ Sysroot))
    return nullptr;

  std::string OutputFile;
  std::unique_ptr<raw_pwrite_stream> OS =
      CreateOutputFile(CI, InFile, /*ref*/ OutputFile);
  if (!OS)
    return nullptr;

  if (!CI.getFrontendOpts().RelocatablePCH)
    Sysroot.clear();

  const auto &FrontendOpts = CI.getFrontendOpts();
  auto Buffer = std::make_shared<PCHBuffer>();
  std::vector<std::unique_ptr<ASTConsumer>> Consumers;
  Consumers.push_back(llvm::make_unique<PCHGenerator>(
                        CI.getPreprocessor(), OutputFile, Sysroot,
                        Buffer, FrontendOpts.ModuleFileExtensions,
                        CI.getPreprocessorOpts().AllowPCHWithCompilerErrors,
                        FrontendOpts.IncludeTimestamps));
  Consumers.push_back(CI.getPCHContainerWriter().CreatePCHContainerGenerator(
      CI, InFile, OutputFile, std::move(OS), Buffer));

  return llvm::make_unique<MultiplexConsumer>(std::move(Consumers));
}

bool GeneratePCHAction::ComputeASTConsumerArguments(CompilerInstance &CI,
                                                    std::string &Sysroot) {
  Sysroot = CI.getHeaderSearchOpts().Sysroot;
  if (CI.getFrontendOpts().RelocatablePCH && Sysroot.empty()) {
    CI.getDiagnostics().Report(diag::err_relocatable_without_isysroot);
    return false;
  }

  return true;
}

std::unique_ptr<llvm::raw_pwrite_stream>
GeneratePCHAction::CreateOutputFile(CompilerInstance &CI, StringRef InFile,
                                    std::string &OutputFile) {
  // We use createOutputFile here because this is exposed via libclang, and we
  // must disable the RemoveFileOnSignal behavior.
  // We use a temporary to avoid race conditions.
  std::unique_ptr<raw_pwrite_stream> OS =
      CI.createOutputFile(CI.getFrontendOpts().OutputFile, /*Binary=*/true,
                          /*RemoveFileOnSignal=*/false, InFile,
                          /*Extension=*/"", /*useTemporary=*/true);
  if (!OS)
    return nullptr;

  OutputFile = CI.getFrontendOpts().OutputFile;
  return OS;
}

bool GeneratePCHAction::shouldEraseOutputFiles() {
  if (getCompilerInstance().getPreprocessorOpts().AllowPCHWithCompilerErrors)
    return false;
  return ASTFrontendAction::shouldEraseOutputFiles();
}

bool GeneratePCHAction::BeginSourceFileAction(CompilerInstance &CI) {
  CI.getLangOpts().CompilingPCH = true;
  return true;
}

std::unique_ptr<ASTConsumer>
GenerateModuleAction::CreateASTConsumer(CompilerInstance &CI,
                                        StringRef InFile) {
  std::unique_ptr<raw_pwrite_stream> OS = CreateOutputFile(CI, InFile);
  if (!OS)
    return nullptr;

  std::string OutputFile = CI.getFrontendOpts().OutputFile;
  std::string Sysroot;

  auto Buffer = std::make_shared<PCHBuffer>();
  std::vector<std::unique_ptr<ASTConsumer>> Consumers;

  Consumers.push_back(llvm::make_unique<PCHGenerator>(
                        CI.getPreprocessor(), OutputFile, Sysroot,
                        Buffer, CI.getFrontendOpts().ModuleFileExtensions,
                        /*AllowASTWithErrors=*/false,
                        /*IncludeTimestamps=*/
                          +CI.getFrontendOpts().BuildingImplicitModule));
  Consumers.push_back(CI.getPCHContainerWriter().CreatePCHContainerGenerator(
      CI, InFile, OutputFile, std::move(OS), Buffer));
  return llvm::make_unique<MultiplexConsumer>(std::move(Consumers));
}

bool GenerateModuleFromModuleMapAction::BeginSourceFileAction(
    CompilerInstance &CI) {
  if (!CI.getLangOpts().Modules) {
    CI.getDiagnostics().Report(diag::err_module_build_requires_fmodules);
    return false;
  }

  return GenerateModuleAction::BeginSourceFileAction(CI);
}

std::unique_ptr<raw_pwrite_stream>
GenerateModuleFromModuleMapAction::CreateOutputFile(CompilerInstance &CI,
                                                    StringRef InFile) {
  // If no output file was provided, figure out where this module would go
  // in the module cache.
  if (CI.getFrontendOpts().OutputFile.empty()) {
    StringRef ModuleMapFile = CI.getFrontendOpts().OriginalModuleMap;
    if (ModuleMapFile.empty())
      ModuleMapFile = InFile;

    HeaderSearch &HS = CI.getPreprocessor().getHeaderSearchInfo();
    CI.getFrontendOpts().OutputFile =
        HS.getCachedModuleFileName(CI.getLangOpts().CurrentModule,
                                   ModuleMapFile);
  }

  // We use createOutputFile here because this is exposed via libclang, and we
  // must disable the RemoveFileOnSignal behavior.
  // We use a temporary to avoid race conditions.
  return CI.createOutputFile(CI.getFrontendOpts().OutputFile, /*Binary=*/true,
                             /*RemoveFileOnSignal=*/false, InFile,
                             /*Extension=*/"", /*useTemporary=*/true,
                             /*CreateMissingDirectories=*/true);
}

bool GenerateModuleInterfaceAction::BeginSourceFileAction(
    CompilerInstance &CI) {
  if (!CI.getLangOpts().ModulesTS) {
    CI.getDiagnostics().Report(diag::err_module_interface_requires_modules_ts);
    return false;
  }

  CI.getLangOpts().setCompilingModule(LangOptions::CMK_ModuleInterface);

  return GenerateModuleAction::BeginSourceFileAction(CI);
}

std::unique_ptr<raw_pwrite_stream>
GenerateModuleInterfaceAction::CreateOutputFile(CompilerInstance &CI,
                                                StringRef InFile) {
  return CI.createDefaultOutputFile(/*Binary=*/true, InFile, "pcm");
}

bool GenerateHeaderModuleAction::PrepareToExecuteAction(
    CompilerInstance &CI) {
  if (!CI.getLangOpts().Modules && !CI.getLangOpts().ModulesTS) {
    CI.getDiagnostics().Report(diag::err_header_module_requires_modules);
    return false;
  }

  auto &Inputs = CI.getFrontendOpts().Inputs;
  if (Inputs.empty())
    return GenerateModuleAction::BeginInvocation(CI);

  auto Kind = Inputs[0].getKind();

  // Convert the header file inputs into a single module input buffer.
  SmallString<256> HeaderContents;
  ModuleHeaders.reserve(Inputs.size());
  for (const FrontendInputFile &FIF : Inputs) {
    // FIXME: We should support re-compiling from an AST file.
    if (FIF.getKind().getFormat() != InputKind::Source || !FIF.isFile()) {
      CI.getDiagnostics().Report(diag::err_module_header_file_not_found)
          << (FIF.isFile() ? FIF.getFile()
                           : FIF.getBuffer()->getBufferIdentifier());
      return true;
    }

    HeaderContents += "#include \"";
    HeaderContents += FIF.getFile();
    HeaderContents += "\"\n";
    ModuleHeaders.push_back(FIF.getFile());
  }
  Buffer = llvm::MemoryBuffer::getMemBufferCopy(
      HeaderContents, Module::getModuleInputBufferName());

  // Set that buffer up as our "real" input.
  Inputs.clear();
  Inputs.push_back(FrontendInputFile(Buffer.get(), Kind, /*IsSystem*/false));

  return GenerateModuleAction::PrepareToExecuteAction(CI);
}

bool GenerateHeaderModuleAction::BeginSourceFileAction(
    CompilerInstance &CI) {
  CI.getLangOpts().setCompilingModule(LangOptions::CMK_HeaderModule);

  // Synthesize a Module object for the given headers.
  auto &HS = CI.getPreprocessor().getHeaderSearchInfo();
  SmallVector<Module::Header, 16> Headers;
  for (StringRef Name : ModuleHeaders) {
    const DirectoryLookup *CurDir = nullptr;
    const FileEntry *FE = HS.LookupFile(
        Name, SourceLocation(), /*Angled*/ false, nullptr, CurDir,
        None, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!FE) {
      CI.getDiagnostics().Report(diag::err_module_header_file_not_found)
        << Name;
      continue;
    }
    Headers.push_back({Name, FE});
  }
  HS.getModuleMap().createHeaderModule(CI.getLangOpts().CurrentModule, Headers);

  return GenerateModuleAction::BeginSourceFileAction(CI);
}

std::unique_ptr<raw_pwrite_stream>
GenerateHeaderModuleAction::CreateOutputFile(CompilerInstance &CI,
                                             StringRef InFile) {
  return CI.createDefaultOutputFile(/*Binary=*/true, InFile, "pcm");
}

SyntaxOnlyAction::~SyntaxOnlyAction() {
}

std::unique_ptr<ASTConsumer>
SyntaxOnlyAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return llvm::make_unique<ASTConsumer>();
}

std::unique_ptr<ASTConsumer>
DumpModuleInfoAction::CreateASTConsumer(CompilerInstance &CI,
                                        StringRef InFile) {
  return llvm::make_unique<ASTConsumer>();
}

std::unique_ptr<ASTConsumer>
VerifyPCHAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return llvm::make_unique<ASTConsumer>();
}

void VerifyPCHAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  bool Preamble = CI.getPreprocessorOpts().PrecompiledPreambleBytes.first != 0;
  const std::string &Sysroot = CI.getHeaderSearchOpts().Sysroot;
  std::unique_ptr<ASTReader> Reader(new ASTReader(
      CI.getPreprocessor(), &CI.getASTContext(), CI.getPCHContainerReader(),
      CI.getFrontendOpts().ModuleFileExtensions,
      Sysroot.empty() ? "" : Sysroot.c_str(),
      /*DisableValidation*/ false,
      /*AllowPCHWithCompilerErrors*/ false,
      /*AllowConfigurationMismatch*/ true,
      /*ValidateSystemInputs*/ true));

  Reader->ReadAST(getCurrentFile(),
                  Preamble ? serialization::MK_Preamble
                           : serialization::MK_PCH,
                  SourceLocation(),
                  ASTReader::ARR_ConfigurationMismatch);
}

namespace {
struct TemplightEntry {
  std::string Name;
  std::string Kind;
  std::string Event;
  std::string DefinitionLocation;
  std::string PointOfInstantiation;
};
} // namespace

namespace llvm {
namespace yaml {
template <> struct MappingTraits<TemplightEntry> {
  static void mapping(IO &io, TemplightEntry &fields) {
    io.mapRequired("name", fields.Name);
    io.mapRequired("kind", fields.Kind);
    io.mapRequired("event", fields.Event);
    io.mapRequired("orig", fields.DefinitionLocation);
    io.mapRequired("poi", fields.PointOfInstantiation);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
class DefaultTemplateInstCallback : public TemplateInstantiationCallback {
  using CodeSynthesisContext = Sema::CodeSynthesisContext;

public:
  void initialize(const Sema &) override {}

  void finalize(const Sema &) override {}

  void atTemplateBegin(const Sema &TheSema,
                       const CodeSynthesisContext &Inst) override {
    displayTemplightEntry<true>(llvm::outs(), TheSema, Inst);
  }

  void atTemplateEnd(const Sema &TheSema,
                     const CodeSynthesisContext &Inst) override {
    displayTemplightEntry<false>(llvm::outs(), TheSema, Inst);
  }

private:
  static std::string toString(CodeSynthesisContext::SynthesisKind Kind) {
    switch (Kind) {
    case CodeSynthesisContext::TemplateInstantiation:
      return "TemplateInstantiation";
    case CodeSynthesisContext::DefaultTemplateArgumentInstantiation:
      return "DefaultTemplateArgumentInstantiation";
    case CodeSynthesisContext::DefaultFunctionArgumentInstantiation:
      return "DefaultFunctionArgumentInstantiation";
    case CodeSynthesisContext::ExplicitTemplateArgumentSubstitution:
      return "ExplicitTemplateArgumentSubstitution";
    case CodeSynthesisContext::DeducedTemplateArgumentSubstitution:
      return "DeducedTemplateArgumentSubstitution";
    case CodeSynthesisContext::PriorTemplateArgumentSubstitution:
      return "PriorTemplateArgumentSubstitution";
    case CodeSynthesisContext::DefaultTemplateArgumentChecking:
      return "DefaultTemplateArgumentChecking";
    case CodeSynthesisContext::ExceptionSpecEvaluation:
      return "ExceptionSpecEvaluation";
    case CodeSynthesisContext::ExceptionSpecInstantiation:
      return "ExceptionSpecInstantiation";
    case CodeSynthesisContext::DeclaringSpecialMember:
      return "DeclaringSpecialMember";
    case CodeSynthesisContext::DefiningSynthesizedFunction:
      return "DefiningSynthesizedFunction";
    case CodeSynthesisContext::Memoization:
      return "Memoization";
    }
    return "";
  }

  template <bool BeginInstantiation>
  static void displayTemplightEntry(llvm::raw_ostream &Out, const Sema &TheSema,
                                    const CodeSynthesisContext &Inst) {
    std::string YAML;
    {
      llvm::raw_string_ostream OS(YAML);
      llvm::yaml::Output YO(OS);
      TemplightEntry Entry =
          getTemplightEntry<BeginInstantiation>(TheSema, Inst);
      llvm::yaml::EmptyContext Context;
      llvm::yaml::yamlize(YO, Entry, true, Context);
    }
    Out << "---" << YAML << "\n";
  }

  template <bool BeginInstantiation>
  static TemplightEntry getTemplightEntry(const Sema &TheSema,
                                          const CodeSynthesisContext &Inst) {
    TemplightEntry Entry;
    Entry.Kind = toString(Inst.Kind);
    Entry.Event = BeginInstantiation ? "Begin" : "End";
    if (auto *NamedTemplate = dyn_cast_or_null<NamedDecl>(Inst.Entity)) {
      llvm::raw_string_ostream OS(Entry.Name);
      NamedTemplate->getNameForDiagnostic(OS, TheSema.getLangOpts(), true);
      const PresumedLoc DefLoc =
        TheSema.getSourceManager().getPresumedLoc(Inst.Entity->getLocation());
      if(!DefLoc.isInvalid())
        Entry.DefinitionLocation = std::string(DefLoc.getFilename()) + ":" +
                                   std::to_string(DefLoc.getLine()) + ":" +
                                   std::to_string(DefLoc.getColumn());
    }
    const PresumedLoc PoiLoc =
        TheSema.getSourceManager().getPresumedLoc(Inst.PointOfInstantiation);
    if (!PoiLoc.isInvalid()) {
      Entry.PointOfInstantiation = std::string(PoiLoc.getFilename()) + ":" +
                                   std::to_string(PoiLoc.getLine()) + ":" +
                                   std::to_string(PoiLoc.getColumn());
    }
    return Entry;
  }
};
} // namespace

std::unique_ptr<ASTConsumer>
TemplightDumpAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return llvm::make_unique<ASTConsumer>();
}

void TemplightDumpAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();

  // This part is normally done by ASTFrontEndAction, but needs to happen
  // before Templight observers can be created
  // FIXME: Move the truncation aspect of this into Sema, we delayed this till
  // here so the source manager would be initialized.
  EnsureSemaIsCreated(CI, *this);

  CI.getSema().TemplateInstCallbacks.push_back(
      llvm::make_unique<DefaultTemplateInstCallback>());
  ASTFrontendAction::ExecuteAction();
}

namespace {
  /// AST reader listener that dumps module information for a module
  /// file.
  class DumpModuleInfoListener : public ASTReaderListener {
    llvm::raw_ostream &Out;

  public:
    DumpModuleInfoListener(llvm::raw_ostream &Out) : Out(Out) { }

#define DUMP_BOOLEAN(Value, Text)                       \
    Out.indent(4) << Text << ": " << (Value? "Yes" : "No") << "\n"

    bool ReadFullVersionInformation(StringRef FullVersion) override {
      Out.indent(2)
        << "Generated by "
        << (FullVersion == getClangFullRepositoryVersion()? "this"
                                                          : "a different")
        << " Clang: " << FullVersion << "\n";
      return ASTReaderListener::ReadFullVersionInformation(FullVersion);
    }

    void ReadModuleName(StringRef ModuleName) override {
      Out.indent(2) << "Module name: " << ModuleName << "\n";
    }
    void ReadModuleMapFile(StringRef ModuleMapPath) override {
      Out.indent(2) << "Module map file: " << ModuleMapPath << "\n";
    }

    bool ReadLanguageOptions(const LangOptions &LangOpts, bool Complain,
                             bool AllowCompatibleDifferences) override {
      Out.indent(2) << "Language options:\n";
#define LANGOPT(Name, Bits, Default, Description) \
      DUMP_BOOLEAN(LangOpts.Name, Description);
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
      Out.indent(4) << Description << ": "                   \
                    << static_cast<unsigned>(LangOpts.get##Name()) << "\n";
#define VALUE_LANGOPT(Name, Bits, Default, Description) \
      Out.indent(4) << Description << ": " << LangOpts.Name << "\n";
#define BENIGN_LANGOPT(Name, Bits, Default, Description)
#define BENIGN_ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#include "clang/Basic/LangOptions.def"

      if (!LangOpts.ModuleFeatures.empty()) {
        Out.indent(4) << "Module features:\n";
        for (StringRef Feature : LangOpts.ModuleFeatures)
          Out.indent(6) << Feature << "\n";
      }

      return false;
    }

    bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                           bool AllowCompatibleDifferences) override {
      Out.indent(2) << "Target options:\n";
      Out.indent(4) << "  Triple: " << TargetOpts.Triple << "\n";
      Out.indent(4) << "  CPU: " << TargetOpts.CPU << "\n";
      Out.indent(4) << "  ABI: " << TargetOpts.ABI << "\n";

      if (!TargetOpts.FeaturesAsWritten.empty()) {
        Out.indent(4) << "Target features:\n";
        for (unsigned I = 0, N = TargetOpts.FeaturesAsWritten.size();
             I != N; ++I) {
          Out.indent(6) << TargetOpts.FeaturesAsWritten[I] << "\n";
        }
      }

      return false;
    }

    bool ReadDiagnosticOptions(IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
                               bool Complain) override {
      Out.indent(2) << "Diagnostic options:\n";
#define DIAGOPT(Name, Bits, Default) DUMP_BOOLEAN(DiagOpts->Name, #Name);
#define ENUM_DIAGOPT(Name, Type, Bits, Default) \
      Out.indent(4) << #Name << ": " << DiagOpts->get##Name() << "\n";
#define VALUE_DIAGOPT(Name, Bits, Default) \
      Out.indent(4) << #Name << ": " << DiagOpts->Name << "\n";
#include "clang/Basic/DiagnosticOptions.def"

      Out.indent(4) << "Diagnostic flags:\n";
      for (const std::string &Warning : DiagOpts->Warnings)
        Out.indent(6) << "-W" << Warning << "\n";
      for (const std::string &Remark : DiagOpts->Remarks)
        Out.indent(6) << "-R" << Remark << "\n";

      return false;
    }

    bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                 StringRef SpecificModuleCachePath,
                                 bool Complain) override {
      Out.indent(2) << "Header search options:\n";
      Out.indent(4) << "System root [-isysroot=]: '" << HSOpts.Sysroot << "'\n";
      Out.indent(4) << "Resource dir [ -resource-dir=]: '" << HSOpts.ResourceDir << "'\n";
      Out.indent(4) << "Module Cache: '" << SpecificModuleCachePath << "'\n";
      DUMP_BOOLEAN(HSOpts.UseBuiltinIncludes,
                   "Use builtin include directories [-nobuiltininc]");
      DUMP_BOOLEAN(HSOpts.UseStandardSystemIncludes,
                   "Use standard system include directories [-nostdinc]");
      DUMP_BOOLEAN(HSOpts.UseStandardCXXIncludes,
                   "Use standard C++ include directories [-nostdinc++]");
      DUMP_BOOLEAN(HSOpts.UseLibcxx,
                   "Use libc++ (rather than libstdc++) [-stdlib=]");
      return false;
    }

    bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                 bool Complain,
                                 std::string &SuggestedPredefines) override {
      Out.indent(2) << "Preprocessor options:\n";
      DUMP_BOOLEAN(PPOpts.UsePredefines,
                   "Uses compiler/target-specific predefines [-undef]");
      DUMP_BOOLEAN(PPOpts.DetailedRecord,
                   "Uses detailed preprocessing record (for indexing)");

      if (!PPOpts.Macros.empty()) {
        Out.indent(4) << "Predefined macros:\n";
      }

      for (std::vector<std::pair<std::string, bool/*isUndef*/> >::const_iterator
             I = PPOpts.Macros.begin(), IEnd = PPOpts.Macros.end();
           I != IEnd; ++I) {
        Out.indent(6);
        if (I->second)
          Out << "-U";
        else
          Out << "-D";
        Out << I->first << "\n";
      }
      return false;
    }

    /// Indicates that a particular module file extension has been read.
    void readModuleFileExtension(
           const ModuleFileExtensionMetadata &Metadata) override {
      Out.indent(2) << "Module file extension '"
                    << Metadata.BlockName << "' " << Metadata.MajorVersion
                    << "." << Metadata.MinorVersion;
      if (!Metadata.UserInfo.empty()) {
        Out << ": ";
        Out.write_escaped(Metadata.UserInfo);
      }

      Out << "\n";
    }

    /// Tells the \c ASTReaderListener that we want to receive the
    /// input files of the AST file via \c visitInputFile.
    bool needsInputFileVisitation() override { return true; }

    /// Tells the \c ASTReaderListener that we want to receive the
    /// input files of the AST file via \c visitInputFile.
    bool needsSystemInputFileVisitation() override { return true; }

    /// Indicates that the AST file contains particular input file.
    ///
    /// \returns true to continue receiving the next input file, false to stop.
    bool visitInputFile(StringRef Filename, bool isSystem,
                        bool isOverridden, bool isExplicitModule) override {

      Out.indent(2) << "Input file: " << Filename;

      if (isSystem || isOverridden || isExplicitModule) {
        Out << " [";
        if (isSystem) {
          Out << "System";
          if (isOverridden || isExplicitModule)
            Out << ", ";
        }
        if (isOverridden) {
          Out << "Overridden";
          if (isExplicitModule)
            Out << ", ";
        }
        if (isExplicitModule)
          Out << "ExplicitModule";

        Out << "]";
      }

      Out << "\n";

      return true;
    }

    /// Returns true if this \c ASTReaderListener wants to receive the
    /// imports of the AST file via \c visitImport, false otherwise.
    bool needsImportVisitation() const override { return true; }

    /// If needsImportVisitation returns \c true, this is called for each
    /// AST file imported by this AST file.
    void visitImport(StringRef ModuleName, StringRef Filename) override {
      Out.indent(2) << "Imports module '" << ModuleName
                    << "': " << Filename.str() << "\n";
    }
#undef DUMP_BOOLEAN
  };
}

bool DumpModuleInfoAction::BeginInvocation(CompilerInstance &CI) {
  // The Object file reader also supports raw ast files and there is no point in
  // being strict about the module file format in -module-file-info mode.
  CI.getHeaderSearchOpts().ModuleFormat = "obj";
  return true;
}

void DumpModuleInfoAction::ExecuteAction() {
  // Set up the output file.
  std::unique_ptr<llvm::raw_fd_ostream> OutFile;
  StringRef OutputFileName = getCompilerInstance().getFrontendOpts().OutputFile;
  if (!OutputFileName.empty() && OutputFileName != "-") {
    std::error_code EC;
    OutFile.reset(new llvm::raw_fd_ostream(OutputFileName.str(), EC,
                                           llvm::sys::fs::F_Text));
  }
  llvm::raw_ostream &Out = OutFile.get()? *OutFile.get() : llvm::outs();

  Out << "Information for module file '" << getCurrentFile() << "':\n";
  auto &FileMgr = getCompilerInstance().getFileManager();
  auto Buffer = FileMgr.getBufferForFile(getCurrentFile());
  StringRef Magic = (*Buffer)->getMemBufferRef().getBuffer();
  bool IsRaw = (Magic.size() >= 4 && Magic[0] == 'C' && Magic[1] == 'P' &&
                Magic[2] == 'C' && Magic[3] == 'H');
  Out << "  Module format: " << (IsRaw ? "raw" : "obj") << "\n";

  Preprocessor &PP = getCompilerInstance().getPreprocessor();
  DumpModuleInfoListener Listener(Out);
  HeaderSearchOptions &HSOpts =
      PP.getHeaderSearchInfo().getHeaderSearchOpts();
  ASTReader::readASTFileControlBlock(
      getCurrentFile(), FileMgr, getCompilerInstance().getPCHContainerReader(),
      /*FindModuleFileExtensions=*/true, Listener,
      HSOpts.ModulesValidateDiagnosticOptions);
}

//===----------------------------------------------------------------------===//
// Preprocessor Actions
//===----------------------------------------------------------------------===//

void DumpRawTokensAction::ExecuteAction() {
  Preprocessor &PP = getCompilerInstance().getPreprocessor();
  SourceManager &SM = PP.getSourceManager();

  // Start lexing the specified input file.
  const llvm::MemoryBuffer *FromFile = SM.getBuffer(SM.getMainFileID());
  Lexer RawLex(SM.getMainFileID(), FromFile, SM, PP.getLangOpts());
  RawLex.SetKeepWhitespaceMode(true);

  Token RawTok;
  RawLex.LexFromRawLexer(RawTok);
  while (RawTok.isNot(tok::eof)) {
    PP.DumpToken(RawTok, true);
    llvm::errs() << "\n";
    RawLex.LexFromRawLexer(RawTok);
  }
}

void DumpTokensAction::ExecuteAction() {
  Preprocessor &PP = getCompilerInstance().getPreprocessor();
  // Start preprocessing the specified input file.
  Token Tok;
  PP.EnterMainSourceFile();
  do {
    PP.Lex(Tok);
    PP.DumpToken(Tok, true);
    llvm::errs() << "\n";
  } while (Tok.isNot(tok::eof));
}

void PreprocessOnlyAction::ExecuteAction() {
  Preprocessor &PP = getCompilerInstance().getPreprocessor();

  // Ignore unknown pragmas.
  PP.IgnorePragmas();

  Token Tok;
  // Start parsing the specified input file.
  PP.EnterMainSourceFile();
  do {
    PP.Lex(Tok);
  } while (Tok.isNot(tok::eof));
}

void PrintPreprocessedAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  // Output file may need to be set to 'Binary', to avoid converting Unix style
  // line feeds (<LF>) to Microsoft style line feeds (<CR><LF>).
  //
  // Look to see what type of line endings the file uses. If there's a
  // CRLF, then we won't open the file up in binary mode. If there is
  // just an LF or CR, then we will open the file up in binary mode.
  // In this fashion, the output format should match the input format, unless
  // the input format has inconsistent line endings.
  //
  // This should be a relatively fast operation since most files won't have
  // all of their source code on a single line. However, that is still a
  // concern, so if we scan for too long, we'll just assume the file should
  // be opened in binary mode.
  bool BinaryMode = true;
  bool InvalidFile = false;
  const SourceManager& SM = CI.getSourceManager();
  const llvm::MemoryBuffer *Buffer = SM.getBuffer(SM.getMainFileID(),
                                                     &InvalidFile);
  if (!InvalidFile) {
    const char *cur = Buffer->getBufferStart();
    const char *end = Buffer->getBufferEnd();
    const char *next = (cur != end) ? cur + 1 : end;

    // Limit ourselves to only scanning 256 characters into the source
    // file.  This is mostly a sanity check in case the file has no
    // newlines whatsoever.
    if (end - cur > 256) end = cur + 256;

    while (next < end) {
      if (*cur == 0x0D) {  // CR
        if (*next == 0x0A)  // CRLF
          BinaryMode = false;

        break;
      } else if (*cur == 0x0A)  // LF
        break;

      ++cur;
      ++next;
    }
  }

  std::unique_ptr<raw_ostream> OS =
      CI.createDefaultOutputFile(BinaryMode, getCurrentFileOrBufferName());
  if (!OS) return;

  // If we're preprocessing a module map, start by dumping the contents of the
  // module itself before switching to the input buffer.
  auto &Input = getCurrentInput();
  if (Input.getKind().getFormat() == InputKind::ModuleMap) {
    if (Input.isFile()) {
      (*OS) << "# 1 \"";
      OS->write_escaped(Input.getFile());
      (*OS) << "\"\n";
    }
    getCurrentModule()->print(*OS);
    (*OS) << "#pragma clang module contents\n";
  }

  DoPrintPreprocessedInput(CI.getPreprocessor(), OS.get(),
                           CI.getPreprocessorOutputOpts());
}

void PrintPreambleAction::ExecuteAction() {
  switch (getCurrentFileKind().getLanguage()) {
  case InputKind::C:
  case InputKind::CXX:
  case InputKind::ObjC:
  case InputKind::ObjCXX:
  case InputKind::OpenCL:
  case InputKind::CUDA:
  case InputKind::HIP:
    break;

  case InputKind::Unknown:
  case InputKind::Asm:
  case InputKind::LLVM_IR:
  case InputKind::RenderScript:
    // We can't do anything with these.
    return;
  }

  // We don't expect to find any #include directives in a preprocessed input.
  if (getCurrentFileKind().isPreprocessed())
    return;

  CompilerInstance &CI = getCompilerInstance();
  auto Buffer = CI.getFileManager().getBufferForFile(getCurrentFile());
  if (Buffer) {
    unsigned Preamble =
        Lexer::ComputePreamble((*Buffer)->getBuffer(), CI.getLangOpts()).Size;
    llvm::outs().write((*Buffer)->getBufferStart(), Preamble);
  }
}

void DumpCompilerOptionsAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  std::unique_ptr<raw_ostream> OSP =
      CI.createDefaultOutputFile(false, getCurrentFile());
  if (!OSP)
    return;

  raw_ostream &OS = *OSP;
  const Preprocessor &PP = CI.getPreprocessor();
  const LangOptions &LangOpts = PP.getLangOpts();

  // FIXME: Rather than manually format the JSON (which is awkward due to
  // needing to remove trailing commas), this should make use of a JSON library.
  // FIXME: Instead of printing enums as an integral value and specifying the
  // type as a separate field, use introspection to print the enumerator.

  OS << "{\n";
  OS << "\n\"features\" : [\n";
  {
    llvm::SmallString<128> Str;
#define FEATURE(Name, Predicate)                                               \
  ("\t{\"" #Name "\" : " + llvm::Twine(Predicate ? "true" : "false") + "},\n") \
      .toVector(Str);
#include "clang/Basic/Features.def"
#undef FEATURE
    // Remove the newline and comma from the last entry to ensure this remains
    // valid JSON.
    OS << Str.substr(0, Str.size() - 2);
  }
  OS << "\n],\n";

  OS << "\n\"extensions\" : [\n";
  {
    llvm::SmallString<128> Str;
#define EXTENSION(Name, Predicate)                                             \
  ("\t{\"" #Name "\" : " + llvm::Twine(Predicate ? "true" : "false") + "},\n") \
      .toVector(Str);
#include "clang/Basic/Features.def"
#undef EXTENSION
    // Remove the newline and comma from the last entry to ensure this remains
    // valid JSON.
    OS << Str.substr(0, Str.size() - 2);
  }
  OS << "\n]\n";

  OS << "}";
}
