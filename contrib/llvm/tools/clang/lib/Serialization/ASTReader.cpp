//===- ASTReader.cpp - AST File Reader ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTReader class, which reads AST files.
//
//===----------------------------------------------------------------------===//

#include "clang/Serialization/ASTReader.h"
#include "ASTCommon.h"
#include "ASTReaderInternals.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/ASTUnresolvedSet.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/ODRHash.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/CommentOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MemoryBufferCache.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/PragmaKinds.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceManagerInternals.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Token.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Weak.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTDeserializationListener.h"
#include "clang/Serialization/ContinuousRangeMap.h"
#include "clang/Serialization/GlobalModuleIndex.h"
#include "clang/Serialization/Module.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/Serialization/ModuleManager.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "clang/Serialization/SerializationDiagnostic.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::serialization;
using namespace clang::serialization::reader;
using llvm::BitstreamCursor;

//===----------------------------------------------------------------------===//
// ChainedASTReaderListener implementation
//===----------------------------------------------------------------------===//

bool
ChainedASTReaderListener::ReadFullVersionInformation(StringRef FullVersion) {
  return First->ReadFullVersionInformation(FullVersion) ||
         Second->ReadFullVersionInformation(FullVersion);
}

void ChainedASTReaderListener::ReadModuleName(StringRef ModuleName) {
  First->ReadModuleName(ModuleName);
  Second->ReadModuleName(ModuleName);
}

void ChainedASTReaderListener::ReadModuleMapFile(StringRef ModuleMapPath) {
  First->ReadModuleMapFile(ModuleMapPath);
  Second->ReadModuleMapFile(ModuleMapPath);
}

bool
ChainedASTReaderListener::ReadLanguageOptions(const LangOptions &LangOpts,
                                              bool Complain,
                                              bool AllowCompatibleDifferences) {
  return First->ReadLanguageOptions(LangOpts, Complain,
                                    AllowCompatibleDifferences) ||
         Second->ReadLanguageOptions(LangOpts, Complain,
                                     AllowCompatibleDifferences);
}

bool ChainedASTReaderListener::ReadTargetOptions(
    const TargetOptions &TargetOpts, bool Complain,
    bool AllowCompatibleDifferences) {
  return First->ReadTargetOptions(TargetOpts, Complain,
                                  AllowCompatibleDifferences) ||
         Second->ReadTargetOptions(TargetOpts, Complain,
                                   AllowCompatibleDifferences);
}

bool ChainedASTReaderListener::ReadDiagnosticOptions(
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts, bool Complain) {
  return First->ReadDiagnosticOptions(DiagOpts, Complain) ||
         Second->ReadDiagnosticOptions(DiagOpts, Complain);
}

bool
ChainedASTReaderListener::ReadFileSystemOptions(const FileSystemOptions &FSOpts,
                                                bool Complain) {
  return First->ReadFileSystemOptions(FSOpts, Complain) ||
         Second->ReadFileSystemOptions(FSOpts, Complain);
}

bool ChainedASTReaderListener::ReadHeaderSearchOptions(
    const HeaderSearchOptions &HSOpts, StringRef SpecificModuleCachePath,
    bool Complain) {
  return First->ReadHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                        Complain) ||
         Second->ReadHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                         Complain);
}

bool ChainedASTReaderListener::ReadPreprocessorOptions(
    const PreprocessorOptions &PPOpts, bool Complain,
    std::string &SuggestedPredefines) {
  return First->ReadPreprocessorOptions(PPOpts, Complain,
                                        SuggestedPredefines) ||
         Second->ReadPreprocessorOptions(PPOpts, Complain, SuggestedPredefines);
}

void ChainedASTReaderListener::ReadCounter(const serialization::ModuleFile &M,
                                           unsigned Value) {
  First->ReadCounter(M, Value);
  Second->ReadCounter(M, Value);
}

bool ChainedASTReaderListener::needsInputFileVisitation() {
  return First->needsInputFileVisitation() ||
         Second->needsInputFileVisitation();
}

bool ChainedASTReaderListener::needsSystemInputFileVisitation() {
  return First->needsSystemInputFileVisitation() ||
  Second->needsSystemInputFileVisitation();
}

void ChainedASTReaderListener::visitModuleFile(StringRef Filename,
                                               ModuleKind Kind) {
  First->visitModuleFile(Filename, Kind);
  Second->visitModuleFile(Filename, Kind);
}

bool ChainedASTReaderListener::visitInputFile(StringRef Filename,
                                              bool isSystem,
                                              bool isOverridden,
                                              bool isExplicitModule) {
  bool Continue = false;
  if (First->needsInputFileVisitation() &&
      (!isSystem || First->needsSystemInputFileVisitation()))
    Continue |= First->visitInputFile(Filename, isSystem, isOverridden,
                                      isExplicitModule);
  if (Second->needsInputFileVisitation() &&
      (!isSystem || Second->needsSystemInputFileVisitation()))
    Continue |= Second->visitInputFile(Filename, isSystem, isOverridden,
                                       isExplicitModule);
  return Continue;
}

void ChainedASTReaderListener::readModuleFileExtension(
       const ModuleFileExtensionMetadata &Metadata) {
  First->readModuleFileExtension(Metadata);
  Second->readModuleFileExtension(Metadata);
}

//===----------------------------------------------------------------------===//
// PCH validator implementation
//===----------------------------------------------------------------------===//

ASTReaderListener::~ASTReaderListener() = default;

/// Compare the given set of language options against an existing set of
/// language options.
///
/// \param Diags If non-NULL, diagnostics will be emitted via this engine.
/// \param AllowCompatibleDifferences If true, differences between compatible
///        language options will be permitted.
///
/// \returns true if the languagae options mis-match, false otherwise.
static bool checkLanguageOptions(const LangOptions &LangOpts,
                                 const LangOptions &ExistingLangOpts,
                                 DiagnosticsEngine *Diags,
                                 bool AllowCompatibleDifferences = true) {
#define LANGOPT(Name, Bits, Default, Description)                 \
  if (ExistingLangOpts.Name != LangOpts.Name) {                   \
    if (Diags)                                                    \
      Diags->Report(diag::err_pch_langopt_mismatch)               \
        << Description << LangOpts.Name << ExistingLangOpts.Name; \
    return true;                                                  \
  }

#define VALUE_LANGOPT(Name, Bits, Default, Description)   \
  if (ExistingLangOpts.Name != LangOpts.Name) {           \
    if (Diags)                                            \
      Diags->Report(diag::err_pch_langopt_value_mismatch) \
        << Description;                                   \
    return true;                                          \
  }

#define ENUM_LANGOPT(Name, Type, Bits, Default, Description)   \
  if (ExistingLangOpts.get##Name() != LangOpts.get##Name()) {  \
    if (Diags)                                                 \
      Diags->Report(diag::err_pch_langopt_value_mismatch)      \
        << Description;                                        \
    return true;                                               \
  }

#define COMPATIBLE_LANGOPT(Name, Bits, Default, Description)  \
  if (!AllowCompatibleDifferences)                            \
    LANGOPT(Name, Bits, Default, Description)

#define COMPATIBLE_ENUM_LANGOPT(Name, Bits, Default, Description)  \
  if (!AllowCompatibleDifferences)                                 \
    ENUM_LANGOPT(Name, Bits, Default, Description)

#define COMPATIBLE_VALUE_LANGOPT(Name, Bits, Default, Description) \
  if (!AllowCompatibleDifferences)                                 \
    VALUE_LANGOPT(Name, Bits, Default, Description)

#define BENIGN_LANGOPT(Name, Bits, Default, Description)
#define BENIGN_ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#define BENIGN_VALUE_LANGOPT(Name, Type, Bits, Default, Description)
#include "clang/Basic/LangOptions.def"

  if (ExistingLangOpts.ModuleFeatures != LangOpts.ModuleFeatures) {
    if (Diags)
      Diags->Report(diag::err_pch_langopt_value_mismatch) << "module features";
    return true;
  }

  if (ExistingLangOpts.ObjCRuntime != LangOpts.ObjCRuntime) {
    if (Diags)
      Diags->Report(diag::err_pch_langopt_value_mismatch)
      << "target Objective-C runtime";
    return true;
  }

  if (ExistingLangOpts.CommentOpts.BlockCommandNames !=
      LangOpts.CommentOpts.BlockCommandNames) {
    if (Diags)
      Diags->Report(diag::err_pch_langopt_value_mismatch)
        << "block command names";
    return true;
  }

  // Sanitizer feature mismatches are treated as compatible differences. If
  // compatible differences aren't allowed, we still only want to check for
  // mismatches of non-modular sanitizers (the only ones which can affect AST
  // generation).
  if (!AllowCompatibleDifferences) {
    SanitizerMask ModularSanitizers = getPPTransparentSanitizers();
    SanitizerSet ExistingSanitizers = ExistingLangOpts.Sanitize;
    SanitizerSet ImportedSanitizers = LangOpts.Sanitize;
    ExistingSanitizers.clear(ModularSanitizers);
    ImportedSanitizers.clear(ModularSanitizers);
    if (ExistingSanitizers.Mask != ImportedSanitizers.Mask) {
      const std::string Flag = "-fsanitize=";
      if (Diags) {
#define SANITIZER(NAME, ID)                                                    \
  {                                                                            \
    bool InExistingModule = ExistingSanitizers.has(SanitizerKind::ID);         \
    bool InImportedModule = ImportedSanitizers.has(SanitizerKind::ID);         \
    if (InExistingModule != InImportedModule)                                  \
      Diags->Report(diag::err_pch_targetopt_feature_mismatch)                  \
          << InExistingModule << (Flag + NAME);                                \
  }
#include "clang/Basic/Sanitizers.def"
      }
      return true;
    }
  }

  return false;
}

/// Compare the given set of target options against an existing set of
/// target options.
///
/// \param Diags If non-NULL, diagnostics will be emitted via this engine.
///
/// \returns true if the target options mis-match, false otherwise.
static bool checkTargetOptions(const TargetOptions &TargetOpts,
                               const TargetOptions &ExistingTargetOpts,
                               DiagnosticsEngine *Diags,
                               bool AllowCompatibleDifferences = true) {
#define CHECK_TARGET_OPT(Field, Name)                             \
  if (TargetOpts.Field != ExistingTargetOpts.Field) {             \
    if (Diags)                                                    \
      Diags->Report(diag::err_pch_targetopt_mismatch)             \
        << Name << TargetOpts.Field << ExistingTargetOpts.Field;  \
    return true;                                                  \
  }

  // The triple and ABI must match exactly.
  CHECK_TARGET_OPT(Triple, "target");
  CHECK_TARGET_OPT(ABI, "target ABI");

  // We can tolerate different CPUs in many cases, notably when one CPU
  // supports a strict superset of another. When allowing compatible
  // differences skip this check.
  if (!AllowCompatibleDifferences)
    CHECK_TARGET_OPT(CPU, "target CPU");

#undef CHECK_TARGET_OPT

  // Compare feature sets.
  SmallVector<StringRef, 4> ExistingFeatures(
                                             ExistingTargetOpts.FeaturesAsWritten.begin(),
                                             ExistingTargetOpts.FeaturesAsWritten.end());
  SmallVector<StringRef, 4> ReadFeatures(TargetOpts.FeaturesAsWritten.begin(),
                                         TargetOpts.FeaturesAsWritten.end());
  llvm::sort(ExistingFeatures);
  llvm::sort(ReadFeatures);

  // We compute the set difference in both directions explicitly so that we can
  // diagnose the differences differently.
  SmallVector<StringRef, 4> UnmatchedExistingFeatures, UnmatchedReadFeatures;
  std::set_difference(
      ExistingFeatures.begin(), ExistingFeatures.end(), ReadFeatures.begin(),
      ReadFeatures.end(), std::back_inserter(UnmatchedExistingFeatures));
  std::set_difference(ReadFeatures.begin(), ReadFeatures.end(),
                      ExistingFeatures.begin(), ExistingFeatures.end(),
                      std::back_inserter(UnmatchedReadFeatures));

  // If we are allowing compatible differences and the read feature set is
  // a strict subset of the existing feature set, there is nothing to diagnose.
  if (AllowCompatibleDifferences && UnmatchedReadFeatures.empty())
    return false;

  if (Diags) {
    for (StringRef Feature : UnmatchedReadFeatures)
      Diags->Report(diag::err_pch_targetopt_feature_mismatch)
          << /* is-existing-feature */ false << Feature;
    for (StringRef Feature : UnmatchedExistingFeatures)
      Diags->Report(diag::err_pch_targetopt_feature_mismatch)
          << /* is-existing-feature */ true << Feature;
  }

  return !UnmatchedReadFeatures.empty() || !UnmatchedExistingFeatures.empty();
}

bool
PCHValidator::ReadLanguageOptions(const LangOptions &LangOpts,
                                  bool Complain,
                                  bool AllowCompatibleDifferences) {
  const LangOptions &ExistingLangOpts = PP.getLangOpts();
  return checkLanguageOptions(LangOpts, ExistingLangOpts,
                              Complain ? &Reader.Diags : nullptr,
                              AllowCompatibleDifferences);
}

bool PCHValidator::ReadTargetOptions(const TargetOptions &TargetOpts,
                                     bool Complain,
                                     bool AllowCompatibleDifferences) {
  const TargetOptions &ExistingTargetOpts = PP.getTargetInfo().getTargetOpts();
  return checkTargetOptions(TargetOpts, ExistingTargetOpts,
                            Complain ? &Reader.Diags : nullptr,
                            AllowCompatibleDifferences);
}

namespace {

using MacroDefinitionsMap =
    llvm::StringMap<std::pair<StringRef, bool /*IsUndef*/>>;
using DeclsMap = llvm::DenseMap<DeclarationName, SmallVector<NamedDecl *, 8>>;

} // namespace

static bool checkDiagnosticGroupMappings(DiagnosticsEngine &StoredDiags,
                                         DiagnosticsEngine &Diags,
                                         bool Complain) {
  using Level = DiagnosticsEngine::Level;

  // Check current mappings for new -Werror mappings, and the stored mappings
  // for cases that were explicitly mapped to *not* be errors that are now
  // errors because of options like -Werror.
  DiagnosticsEngine *MappingSources[] = { &Diags, &StoredDiags };

  for (DiagnosticsEngine *MappingSource : MappingSources) {
    for (auto DiagIDMappingPair : MappingSource->getDiagnosticMappings()) {
      diag::kind DiagID = DiagIDMappingPair.first;
      Level CurLevel = Diags.getDiagnosticLevel(DiagID, SourceLocation());
      if (CurLevel < DiagnosticsEngine::Error)
        continue; // not significant
      Level StoredLevel =
          StoredDiags.getDiagnosticLevel(DiagID, SourceLocation());
      if (StoredLevel < DiagnosticsEngine::Error) {
        if (Complain)
          Diags.Report(diag::err_pch_diagopt_mismatch) << "-Werror=" +
              Diags.getDiagnosticIDs()->getWarningOptionForDiag(DiagID).str();
        return true;
      }
    }
  }

  return false;
}

static bool isExtHandlingFromDiagsError(DiagnosticsEngine &Diags) {
  diag::Severity Ext = Diags.getExtensionHandlingBehavior();
  if (Ext == diag::Severity::Warning && Diags.getWarningsAsErrors())
    return true;
  return Ext >= diag::Severity::Error;
}

static bool checkDiagnosticMappings(DiagnosticsEngine &StoredDiags,
                                    DiagnosticsEngine &Diags,
                                    bool IsSystem, bool Complain) {
  // Top-level options
  if (IsSystem) {
    if (Diags.getSuppressSystemWarnings())
      return false;
    // If -Wsystem-headers was not enabled before, be conservative
    if (StoredDiags.getSuppressSystemWarnings()) {
      if (Complain)
        Diags.Report(diag::err_pch_diagopt_mismatch) << "-Wsystem-headers";
      return true;
    }
  }

  if (Diags.getWarningsAsErrors() && !StoredDiags.getWarningsAsErrors()) {
    if (Complain)
      Diags.Report(diag::err_pch_diagopt_mismatch) << "-Werror";
    return true;
  }

  if (Diags.getWarningsAsErrors() && Diags.getEnableAllWarnings() &&
      !StoredDiags.getEnableAllWarnings()) {
    if (Complain)
      Diags.Report(diag::err_pch_diagopt_mismatch) << "-Weverything -Werror";
    return true;
  }

  if (isExtHandlingFromDiagsError(Diags) &&
      !isExtHandlingFromDiagsError(StoredDiags)) {
    if (Complain)
      Diags.Report(diag::err_pch_diagopt_mismatch) << "-pedantic-errors";
    return true;
  }

  return checkDiagnosticGroupMappings(StoredDiags, Diags, Complain);
}

/// Return the top import module if it is implicit, nullptr otherwise.
static Module *getTopImportImplicitModule(ModuleManager &ModuleMgr,
                                          Preprocessor &PP) {
  // If the original import came from a file explicitly generated by the user,
  // don't check the diagnostic mappings.
  // FIXME: currently this is approximated by checking whether this is not a
  // module import of an implicitly-loaded module file.
  // Note: ModuleMgr.rbegin() may not be the current module, but it must be in
  // the transitive closure of its imports, since unrelated modules cannot be
  // imported until after this module finishes validation.
  ModuleFile *TopImport = &*ModuleMgr.rbegin();
  while (!TopImport->ImportedBy.empty())
    TopImport = TopImport->ImportedBy[0];
  if (TopImport->Kind != MK_ImplicitModule)
    return nullptr;

  StringRef ModuleName = TopImport->ModuleName;
  assert(!ModuleName.empty() && "diagnostic options read before module name");

  Module *M = PP.getHeaderSearchInfo().lookupModule(ModuleName);
  assert(M && "missing module");
  return M;
}

bool PCHValidator::ReadDiagnosticOptions(
    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts, bool Complain) {
  DiagnosticsEngine &ExistingDiags = PP.getDiagnostics();
  IntrusiveRefCntPtr<DiagnosticIDs> DiagIDs(ExistingDiags.getDiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagIDs, DiagOpts.get()));
  // This should never fail, because we would have processed these options
  // before writing them to an ASTFile.
  ProcessWarningOptions(*Diags, *DiagOpts, /*Report*/false);

  ModuleManager &ModuleMgr = Reader.getModuleManager();
  assert(ModuleMgr.size() >= 1 && "what ASTFile is this then");

  Module *TopM = getTopImportImplicitModule(ModuleMgr, PP);
  if (!TopM)
    return false;

  // FIXME: if the diagnostics are incompatible, save a DiagnosticOptions that
  // contains the union of their flags.
  return checkDiagnosticMappings(*Diags, ExistingDiags, TopM->IsSystem,
                                 Complain);
}

/// Collect the macro definitions provided by the given preprocessor
/// options.
static void
collectMacroDefinitions(const PreprocessorOptions &PPOpts,
                        MacroDefinitionsMap &Macros,
                        SmallVectorImpl<StringRef> *MacroNames = nullptr) {
  for (unsigned I = 0, N = PPOpts.Macros.size(); I != N; ++I) {
    StringRef Macro = PPOpts.Macros[I].first;
    bool IsUndef = PPOpts.Macros[I].second;

    std::pair<StringRef, StringRef> MacroPair = Macro.split('=');
    StringRef MacroName = MacroPair.first;
    StringRef MacroBody = MacroPair.second;

    // For an #undef'd macro, we only care about the name.
    if (IsUndef) {
      if (MacroNames && !Macros.count(MacroName))
        MacroNames->push_back(MacroName);

      Macros[MacroName] = std::make_pair("", true);
      continue;
    }

    // For a #define'd macro, figure out the actual definition.
    if (MacroName.size() == Macro.size())
      MacroBody = "1";
    else {
      // Note: GCC drops anything following an end-of-line character.
      StringRef::size_type End = MacroBody.find_first_of("\n\r");
      MacroBody = MacroBody.substr(0, End);
    }

    if (MacroNames && !Macros.count(MacroName))
      MacroNames->push_back(MacroName);
    Macros[MacroName] = std::make_pair(MacroBody, false);
  }
}

/// Check the preprocessor options deserialized from the control block
/// against the preprocessor options in an existing preprocessor.
///
/// \param Diags If non-null, produce diagnostics for any mismatches incurred.
/// \param Validate If true, validate preprocessor options. If false, allow
///        macros defined by \p ExistingPPOpts to override those defined by
///        \p PPOpts in SuggestedPredefines.
static bool checkPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                     const PreprocessorOptions &ExistingPPOpts,
                                     DiagnosticsEngine *Diags,
                                     FileManager &FileMgr,
                                     std::string &SuggestedPredefines,
                                     const LangOptions &LangOpts,
                                     bool Validate = true) {
  // Check macro definitions.
  MacroDefinitionsMap ASTFileMacros;
  collectMacroDefinitions(PPOpts, ASTFileMacros);
  MacroDefinitionsMap ExistingMacros;
  SmallVector<StringRef, 4> ExistingMacroNames;
  collectMacroDefinitions(ExistingPPOpts, ExistingMacros, &ExistingMacroNames);

  for (unsigned I = 0, N = ExistingMacroNames.size(); I != N; ++I) {
    // Dig out the macro definition in the existing preprocessor options.
    StringRef MacroName = ExistingMacroNames[I];
    std::pair<StringRef, bool> Existing = ExistingMacros[MacroName];

    // Check whether we know anything about this macro name or not.
    llvm::StringMap<std::pair<StringRef, bool /*IsUndef*/>>::iterator Known =
        ASTFileMacros.find(MacroName);
    if (!Validate || Known == ASTFileMacros.end()) {
      // FIXME: Check whether this identifier was referenced anywhere in the
      // AST file. If so, we should reject the AST file. Unfortunately, this
      // information isn't in the control block. What shall we do about it?

      if (Existing.second) {
        SuggestedPredefines += "#undef ";
        SuggestedPredefines += MacroName.str();
        SuggestedPredefines += '\n';
      } else {
        SuggestedPredefines += "#define ";
        SuggestedPredefines += MacroName.str();
        SuggestedPredefines += ' ';
        SuggestedPredefines += Existing.first.str();
        SuggestedPredefines += '\n';
      }
      continue;
    }

    // If the macro was defined in one but undef'd in the other, we have a
    // conflict.
    if (Existing.second != Known->second.second) {
      if (Diags) {
        Diags->Report(diag::err_pch_macro_def_undef)
          << MacroName << Known->second.second;
      }
      return true;
    }

    // If the macro was #undef'd in both, or if the macro bodies are identical,
    // it's fine.
    if (Existing.second || Existing.first == Known->second.first)
      continue;

    // The macro bodies differ; complain.
    if (Diags) {
      Diags->Report(diag::err_pch_macro_def_conflict)
        << MacroName << Known->second.first << Existing.first;
    }
    return true;
  }

  // Check whether we're using predefines.
  if (PPOpts.UsePredefines != ExistingPPOpts.UsePredefines && Validate) {
    if (Diags) {
      Diags->Report(diag::err_pch_undef) << ExistingPPOpts.UsePredefines;
    }
    return true;
  }

  // Detailed record is important since it is used for the module cache hash.
  if (LangOpts.Modules &&
      PPOpts.DetailedRecord != ExistingPPOpts.DetailedRecord && Validate) {
    if (Diags) {
      Diags->Report(diag::err_pch_pp_detailed_record) << PPOpts.DetailedRecord;
    }
    return true;
  }

  // Compute the #include and #include_macros lines we need.
  for (unsigned I = 0, N = ExistingPPOpts.Includes.size(); I != N; ++I) {
    StringRef File = ExistingPPOpts.Includes[I];

    if (!ExistingPPOpts.ImplicitPCHInclude.empty() &&
        !ExistingPPOpts.PCHThroughHeader.empty()) {
      // In case the through header is an include, we must add all the includes
      // to the predefines so the start point can be determined.
      SuggestedPredefines += "#include \"";
      SuggestedPredefines += File;
      SuggestedPredefines += "\"\n";
      continue;
    }

    if (File == ExistingPPOpts.ImplicitPCHInclude)
      continue;

    if (std::find(PPOpts.Includes.begin(), PPOpts.Includes.end(), File)
          != PPOpts.Includes.end())
      continue;

    SuggestedPredefines += "#include \"";
    SuggestedPredefines += File;
    SuggestedPredefines += "\"\n";
  }

  for (unsigned I = 0, N = ExistingPPOpts.MacroIncludes.size(); I != N; ++I) {
    StringRef File = ExistingPPOpts.MacroIncludes[I];
    if (std::find(PPOpts.MacroIncludes.begin(), PPOpts.MacroIncludes.end(),
                  File)
        != PPOpts.MacroIncludes.end())
      continue;

    SuggestedPredefines += "#__include_macros \"";
    SuggestedPredefines += File;
    SuggestedPredefines += "\"\n##\n";
  }

  return false;
}

bool PCHValidator::ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                           bool Complain,
                                           std::string &SuggestedPredefines) {
  const PreprocessorOptions &ExistingPPOpts = PP.getPreprocessorOpts();

  return checkPreprocessorOptions(PPOpts, ExistingPPOpts,
                                  Complain? &Reader.Diags : nullptr,
                                  PP.getFileManager(),
                                  SuggestedPredefines,
                                  PP.getLangOpts());
}

bool SimpleASTReaderListener::ReadPreprocessorOptions(
                                  const PreprocessorOptions &PPOpts,
                                  bool Complain,
                                  std::string &SuggestedPredefines) {
  return checkPreprocessorOptions(PPOpts,
                                  PP.getPreprocessorOpts(),
                                  nullptr,
                                  PP.getFileManager(),
                                  SuggestedPredefines,
                                  PP.getLangOpts(),
                                  false);
}

/// Check the header search options deserialized from the control block
/// against the header search options in an existing preprocessor.
///
/// \param Diags If non-null, produce diagnostics for any mismatches incurred.
static bool checkHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                     StringRef SpecificModuleCachePath,
                                     StringRef ExistingModuleCachePath,
                                     DiagnosticsEngine *Diags,
                                     const LangOptions &LangOpts) {
  if (LangOpts.Modules) {
    if (SpecificModuleCachePath != ExistingModuleCachePath) {
      if (Diags)
        Diags->Report(diag::err_pch_modulecache_mismatch)
          << SpecificModuleCachePath << ExistingModuleCachePath;
      return true;
    }
  }

  return false;
}

bool PCHValidator::ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                           StringRef SpecificModuleCachePath,
                                           bool Complain) {
  return checkHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                  PP.getHeaderSearchInfo().getModuleCachePath(),
                                  Complain ? &Reader.Diags : nullptr,
                                  PP.getLangOpts());
}

void PCHValidator::ReadCounter(const ModuleFile &M, unsigned Value) {
  PP.setCounterValue(Value);
}

//===----------------------------------------------------------------------===//
// AST reader implementation
//===----------------------------------------------------------------------===//

void ASTReader::setDeserializationListener(ASTDeserializationListener *Listener,
                                           bool TakeOwnership) {
  DeserializationListener = Listener;
  OwnsDeserializationListener = TakeOwnership;
}

unsigned ASTSelectorLookupTrait::ComputeHash(Selector Sel) {
  return serialization::ComputeHash(Sel);
}

std::pair<unsigned, unsigned>
ASTSelectorLookupTrait::ReadKeyDataLength(const unsigned char*& d) {
  using namespace llvm::support;

  unsigned KeyLen = endian::readNext<uint16_t, little, unaligned>(d);
  unsigned DataLen = endian::readNext<uint16_t, little, unaligned>(d);
  return std::make_pair(KeyLen, DataLen);
}

ASTSelectorLookupTrait::internal_key_type
ASTSelectorLookupTrait::ReadKey(const unsigned char* d, unsigned) {
  using namespace llvm::support;

  SelectorTable &SelTable = Reader.getContext().Selectors;
  unsigned N = endian::readNext<uint16_t, little, unaligned>(d);
  IdentifierInfo *FirstII = Reader.getLocalIdentifier(
      F, endian::readNext<uint32_t, little, unaligned>(d));
  if (N == 0)
    return SelTable.getNullarySelector(FirstII);
  else if (N == 1)
    return SelTable.getUnarySelector(FirstII);

  SmallVector<IdentifierInfo *, 16> Args;
  Args.push_back(FirstII);
  for (unsigned I = 1; I != N; ++I)
    Args.push_back(Reader.getLocalIdentifier(
        F, endian::readNext<uint32_t, little, unaligned>(d)));

  return SelTable.getSelector(N, Args.data());
}

ASTSelectorLookupTrait::data_type
ASTSelectorLookupTrait::ReadData(Selector, const unsigned char* d,
                                 unsigned DataLen) {
  using namespace llvm::support;

  data_type Result;

  Result.ID = Reader.getGlobalSelectorID(
      F, endian::readNext<uint32_t, little, unaligned>(d));
  unsigned FullInstanceBits = endian::readNext<uint16_t, little, unaligned>(d);
  unsigned FullFactoryBits = endian::readNext<uint16_t, little, unaligned>(d);
  Result.InstanceBits = FullInstanceBits & 0x3;
  Result.InstanceHasMoreThanOneDecl = (FullInstanceBits >> 2) & 0x1;
  Result.FactoryBits = FullFactoryBits & 0x3;
  Result.FactoryHasMoreThanOneDecl = (FullFactoryBits >> 2) & 0x1;
  unsigned NumInstanceMethods = FullInstanceBits >> 3;
  unsigned NumFactoryMethods = FullFactoryBits >> 3;

  // Load instance methods
  for (unsigned I = 0; I != NumInstanceMethods; ++I) {
    if (ObjCMethodDecl *Method = Reader.GetLocalDeclAs<ObjCMethodDecl>(
            F, endian::readNext<uint32_t, little, unaligned>(d)))
      Result.Instance.push_back(Method);
  }

  // Load factory methods
  for (unsigned I = 0; I != NumFactoryMethods; ++I) {
    if (ObjCMethodDecl *Method = Reader.GetLocalDeclAs<ObjCMethodDecl>(
            F, endian::readNext<uint32_t, little, unaligned>(d)))
      Result.Factory.push_back(Method);
  }

  return Result;
}

unsigned ASTIdentifierLookupTraitBase::ComputeHash(const internal_key_type& a) {
  return llvm::djbHash(a);
}

std::pair<unsigned, unsigned>
ASTIdentifierLookupTraitBase::ReadKeyDataLength(const unsigned char*& d) {
  using namespace llvm::support;

  unsigned DataLen = endian::readNext<uint16_t, little, unaligned>(d);
  unsigned KeyLen = endian::readNext<uint16_t, little, unaligned>(d);
  return std::make_pair(KeyLen, DataLen);
}

ASTIdentifierLookupTraitBase::internal_key_type
ASTIdentifierLookupTraitBase::ReadKey(const unsigned char* d, unsigned n) {
  assert(n >= 2 && d[n-1] == '\0');
  return StringRef((const char*) d, n-1);
}

/// Whether the given identifier is "interesting".
static bool isInterestingIdentifier(ASTReader &Reader, IdentifierInfo &II,
                                    bool IsModule) {
  return II.hadMacroDefinition() ||
         II.isPoisoned() ||
         (IsModule ? II.hasRevertedBuiltin() : II.getObjCOrBuiltinID()) ||
         II.hasRevertedTokenIDToIdentifier() ||
         (!(IsModule && Reader.getPreprocessor().getLangOpts().CPlusPlus) &&
          II.getFETokenInfo());
}

static bool readBit(unsigned &Bits) {
  bool Value = Bits & 0x1;
  Bits >>= 1;
  return Value;
}

IdentID ASTIdentifierLookupTrait::ReadIdentifierID(const unsigned char *d) {
  using namespace llvm::support;

  unsigned RawID = endian::readNext<uint32_t, little, unaligned>(d);
  return Reader.getGlobalIdentifierID(F, RawID >> 1);
}

static void markIdentifierFromAST(ASTReader &Reader, IdentifierInfo &II) {
  if (!II.isFromAST()) {
    II.setIsFromAST();
    bool IsModule = Reader.getPreprocessor().getCurrentModule() != nullptr;
    if (isInterestingIdentifier(Reader, II, IsModule))
      II.setChangedSinceDeserialization();
  }
}

IdentifierInfo *ASTIdentifierLookupTrait::ReadData(const internal_key_type& k,
                                                   const unsigned char* d,
                                                   unsigned DataLen) {
  using namespace llvm::support;

  unsigned RawID = endian::readNext<uint32_t, little, unaligned>(d);
  bool IsInteresting = RawID & 0x01;

  // Wipe out the "is interesting" bit.
  RawID = RawID >> 1;

  // Build the IdentifierInfo and link the identifier ID with it.
  IdentifierInfo *II = KnownII;
  if (!II) {
    II = &Reader.getIdentifierTable().getOwn(k);
    KnownII = II;
  }
  markIdentifierFromAST(Reader, *II);
  Reader.markIdentifierUpToDate(II);

  IdentID ID = Reader.getGlobalIdentifierID(F, RawID);
  if (!IsInteresting) {
    // For uninteresting identifiers, there's nothing else to do. Just notify
    // the reader that we've finished loading this identifier.
    Reader.SetIdentifierInfo(ID, II);
    return II;
  }

  unsigned ObjCOrBuiltinID = endian::readNext<uint16_t, little, unaligned>(d);
  unsigned Bits = endian::readNext<uint16_t, little, unaligned>(d);
  bool CPlusPlusOperatorKeyword = readBit(Bits);
  bool HasRevertedTokenIDToIdentifier = readBit(Bits);
  bool HasRevertedBuiltin = readBit(Bits);
  bool Poisoned = readBit(Bits);
  bool ExtensionToken = readBit(Bits);
  bool HadMacroDefinition = readBit(Bits);

  assert(Bits == 0 && "Extra bits in the identifier?");
  DataLen -= 8;

  // Set or check the various bits in the IdentifierInfo structure.
  // Token IDs are read-only.
  if (HasRevertedTokenIDToIdentifier && II->getTokenID() != tok::identifier)
    II->revertTokenIDToIdentifier();
  if (!F.isModule())
    II->setObjCOrBuiltinID(ObjCOrBuiltinID);
  else if (HasRevertedBuiltin && II->getBuiltinID()) {
    II->revertBuiltin();
    assert((II->hasRevertedBuiltin() ||
            II->getObjCOrBuiltinID() == ObjCOrBuiltinID) &&
           "Incorrect ObjC keyword or builtin ID");
  }
  assert(II->isExtensionToken() == ExtensionToken &&
         "Incorrect extension token flag");
  (void)ExtensionToken;
  if (Poisoned)
    II->setIsPoisoned(true);
  assert(II->isCPlusPlusOperatorKeyword() == CPlusPlusOperatorKeyword &&
         "Incorrect C++ operator keyword flag");
  (void)CPlusPlusOperatorKeyword;

  // If this identifier is a macro, deserialize the macro
  // definition.
  if (HadMacroDefinition) {
    uint32_t MacroDirectivesOffset =
        endian::readNext<uint32_t, little, unaligned>(d);
    DataLen -= 4;

    Reader.addPendingMacro(II, &F, MacroDirectivesOffset);
  }

  Reader.SetIdentifierInfo(ID, II);

  // Read all of the declarations visible at global scope with this
  // name.
  if (DataLen > 0) {
    SmallVector<uint32_t, 4> DeclIDs;
    for (; DataLen > 0; DataLen -= 4)
      DeclIDs.push_back(Reader.getGlobalDeclID(
          F, endian::readNext<uint32_t, little, unaligned>(d)));
    Reader.SetGloballyVisibleDecls(II, DeclIDs);
  }

  return II;
}

DeclarationNameKey::DeclarationNameKey(DeclarationName Name)
    : Kind(Name.getNameKind()) {
  switch (Kind) {
  case DeclarationName::Identifier:
    Data = (uint64_t)Name.getAsIdentifierInfo();
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    Data = (uint64_t)Name.getObjCSelector().getAsOpaquePtr();
    break;
  case DeclarationName::CXXOperatorName:
    Data = Name.getCXXOverloadedOperator();
    break;
  case DeclarationName::CXXLiteralOperatorName:
    Data = (uint64_t)Name.getCXXLiteralIdentifier();
    break;
  case DeclarationName::CXXDeductionGuideName:
    Data = (uint64_t)Name.getCXXDeductionGuideTemplate()
               ->getDeclName().getAsIdentifierInfo();
    break;
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXUsingDirective:
    Data = 0;
    break;
  }
}

unsigned DeclarationNameKey::getHash() const {
  llvm::FoldingSetNodeID ID;
  ID.AddInteger(Kind);

  switch (Kind) {
  case DeclarationName::Identifier:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXDeductionGuideName:
    ID.AddString(((IdentifierInfo*)Data)->getName());
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    ID.AddInteger(serialization::ComputeHash(Selector(Data)));
    break;
  case DeclarationName::CXXOperatorName:
    ID.AddInteger((OverloadedOperatorKind)Data);
    break;
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXUsingDirective:
    break;
  }

  return ID.ComputeHash();
}

ModuleFile *
ASTDeclContextNameLookupTrait::ReadFileRef(const unsigned char *&d) {
  using namespace llvm::support;

  uint32_t ModuleFileID = endian::readNext<uint32_t, little, unaligned>(d);
  return Reader.getLocalModuleFile(F, ModuleFileID);
}

std::pair<unsigned, unsigned>
ASTDeclContextNameLookupTrait::ReadKeyDataLength(const unsigned char *&d) {
  using namespace llvm::support;

  unsigned KeyLen = endian::readNext<uint16_t, little, unaligned>(d);
  unsigned DataLen = endian::readNext<uint16_t, little, unaligned>(d);
  return std::make_pair(KeyLen, DataLen);
}

ASTDeclContextNameLookupTrait::internal_key_type
ASTDeclContextNameLookupTrait::ReadKey(const unsigned char *d, unsigned) {
  using namespace llvm::support;

  auto Kind = (DeclarationName::NameKind)*d++;
  uint64_t Data;
  switch (Kind) {
  case DeclarationName::Identifier:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXDeductionGuideName:
    Data = (uint64_t)Reader.getLocalIdentifier(
        F, endian::readNext<uint32_t, little, unaligned>(d));
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    Data =
        (uint64_t)Reader.getLocalSelector(
                             F, endian::readNext<uint32_t, little, unaligned>(
                                    d)).getAsOpaquePtr();
    break;
  case DeclarationName::CXXOperatorName:
    Data = *d++; // OverloadedOperatorKind
    break;
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
  case DeclarationName::CXXUsingDirective:
    Data = 0;
    break;
  }

  return DeclarationNameKey(Kind, Data);
}

void ASTDeclContextNameLookupTrait::ReadDataInto(internal_key_type,
                                                 const unsigned char *d,
                                                 unsigned DataLen,
                                                 data_type_builder &Val) {
  using namespace llvm::support;

  for (unsigned NumDecls = DataLen / 4; NumDecls; --NumDecls) {
    uint32_t LocalID = endian::readNext<uint32_t, little, unaligned>(d);
    Val.insert(Reader.getGlobalDeclID(F, LocalID));
  }
}

bool ASTReader::ReadLexicalDeclContextStorage(ModuleFile &M,
                                              BitstreamCursor &Cursor,
                                              uint64_t Offset,
                                              DeclContext *DC) {
  assert(Offset != 0);

  SavedStreamPosition SavedPosition(Cursor);
  Cursor.JumpToBit(Offset);

  RecordData Record;
  StringRef Blob;
  unsigned Code = Cursor.ReadCode();
  unsigned RecCode = Cursor.readRecord(Code, Record, &Blob);
  if (RecCode != DECL_CONTEXT_LEXICAL) {
    Error("Expected lexical block");
    return true;
  }

  assert(!isa<TranslationUnitDecl>(DC) &&
         "expected a TU_UPDATE_LEXICAL record for TU");
  // If we are handling a C++ class template instantiation, we can see multiple
  // lexical updates for the same record. It's important that we select only one
  // of them, so that field numbering works properly. Just pick the first one we
  // see.
  auto &Lex = LexicalDecls[DC];
  if (!Lex.first) {
    Lex = std::make_pair(
        &M, llvm::makeArrayRef(
                reinterpret_cast<const llvm::support::unaligned_uint32_t *>(
                    Blob.data()),
                Blob.size() / 4));
  }
  DC->setHasExternalLexicalStorage(true);
  return false;
}

bool ASTReader::ReadVisibleDeclContextStorage(ModuleFile &M,
                                              BitstreamCursor &Cursor,
                                              uint64_t Offset,
                                              DeclID ID) {
  assert(Offset != 0);

  SavedStreamPosition SavedPosition(Cursor);
  Cursor.JumpToBit(Offset);

  RecordData Record;
  StringRef Blob;
  unsigned Code = Cursor.ReadCode();
  unsigned RecCode = Cursor.readRecord(Code, Record, &Blob);
  if (RecCode != DECL_CONTEXT_VISIBLE) {
    Error("Expected visible lookup table block");
    return true;
  }

  // We can't safely determine the primary context yet, so delay attaching the
  // lookup table until we're done with recursive deserialization.
  auto *Data = (const unsigned char*)Blob.data();
  PendingVisibleUpdates[ID].push_back(PendingVisibleUpdate{&M, Data});
  return false;
}

void ASTReader::Error(StringRef Msg) const {
  Error(diag::err_fe_pch_malformed, Msg);
  if (PP.getLangOpts().Modules && !Diags.isDiagnosticInFlight() &&
      !PP.getHeaderSearchInfo().getModuleCachePath().empty()) {
    Diag(diag::note_module_cache_path)
      << PP.getHeaderSearchInfo().getModuleCachePath();
  }
}

void ASTReader::Error(unsigned DiagID,
                      StringRef Arg1, StringRef Arg2) const {
  if (Diags.isDiagnosticInFlight())
    Diags.SetDelayedDiagnostic(DiagID, Arg1, Arg2);
  else
    Diag(DiagID) << Arg1 << Arg2;
}

//===----------------------------------------------------------------------===//
// Source Manager Deserialization
//===----------------------------------------------------------------------===//

/// Read the line table in the source manager block.
/// \returns true if there was an error.
bool ASTReader::ParseLineTable(ModuleFile &F,
                               const RecordData &Record) {
  unsigned Idx = 0;
  LineTableInfo &LineTable = SourceMgr.getLineTable();

  // Parse the file names
  std::map<int, int> FileIDs;
  FileIDs[-1] = -1; // For unspecified filenames.
  for (unsigned I = 0; Record[Idx]; ++I) {
    // Extract the file name
    auto Filename = ReadPath(F, Record, Idx);
    FileIDs[I] = LineTable.getLineTableFilenameID(Filename);
  }
  ++Idx;

  // Parse the line entries
  std::vector<LineEntry> Entries;
  while (Idx < Record.size()) {
    int FID = Record[Idx++];
    assert(FID >= 0 && "Serialized line entries for non-local file.");
    // Remap FileID from 1-based old view.
    FID += F.SLocEntryBaseID - 1;

    // Extract the line entries
    unsigned NumEntries = Record[Idx++];
    assert(NumEntries && "no line entries for file ID");
    Entries.clear();
    Entries.reserve(NumEntries);
    for (unsigned I = 0; I != NumEntries; ++I) {
      unsigned FileOffset = Record[Idx++];
      unsigned LineNo = Record[Idx++];
      int FilenameID = FileIDs[Record[Idx++]];
      SrcMgr::CharacteristicKind FileKind
        = (SrcMgr::CharacteristicKind)Record[Idx++];
      unsigned IncludeOffset = Record[Idx++];
      Entries.push_back(LineEntry::get(FileOffset, LineNo, FilenameID,
                                       FileKind, IncludeOffset));
    }
    LineTable.AddEntry(FileID::get(FID), Entries);
  }

  return false;
}

/// Read a source manager block
bool ASTReader::ReadSourceManagerBlock(ModuleFile &F) {
  using namespace SrcMgr;

  BitstreamCursor &SLocEntryCursor = F.SLocEntryCursor;

  // Set the source-location entry cursor to the current position in
  // the stream. This cursor will be used to read the contents of the
  // source manager block initially, and then lazily read
  // source-location entries as needed.
  SLocEntryCursor = F.Stream;

  // The stream itself is going to skip over the source manager block.
  if (F.Stream.SkipBlock()) {
    Error("malformed block record in AST file");
    return true;
  }

  // Enter the source manager block.
  if (SLocEntryCursor.EnterSubBlock(SOURCE_MANAGER_BLOCK_ID)) {
    Error("malformed source manager block record in AST file");
    return true;
  }

  RecordData Record;
  while (true) {
    llvm::BitstreamEntry E = SLocEntryCursor.advanceSkippingSubblocks();

    switch (E.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return true;
    case llvm::BitstreamEntry::EndBlock:
      return false;
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    StringRef Blob;
    switch (SLocEntryCursor.readRecord(E.ID, Record, &Blob)) {
    default:  // Default behavior: ignore.
      break;

    case SM_SLOC_FILE_ENTRY:
    case SM_SLOC_BUFFER_ENTRY:
    case SM_SLOC_EXPANSION_ENTRY:
      // Once we hit one of the source location entries, we're done.
      return false;
    }
  }
}

/// If a header file is not found at the path that we expect it to be
/// and the PCH file was moved from its original location, try to resolve the
/// file by assuming that header+PCH were moved together and the header is in
/// the same place relative to the PCH.
static std::string
resolveFileRelativeToOriginalDir(const std::string &Filename,
                                 const std::string &OriginalDir,
                                 const std::string &CurrDir) {
  assert(OriginalDir != CurrDir &&
         "No point trying to resolve the file if the PCH dir didn't change");

  using namespace llvm::sys;

  SmallString<128> filePath(Filename);
  fs::make_absolute(filePath);
  assert(path::is_absolute(OriginalDir));
  SmallString<128> currPCHPath(CurrDir);

  path::const_iterator fileDirI = path::begin(path::parent_path(filePath)),
                       fileDirE = path::end(path::parent_path(filePath));
  path::const_iterator origDirI = path::begin(OriginalDir),
                       origDirE = path::end(OriginalDir);
  // Skip the common path components from filePath and OriginalDir.
  while (fileDirI != fileDirE && origDirI != origDirE &&
         *fileDirI == *origDirI) {
    ++fileDirI;
    ++origDirI;
  }
  for (; origDirI != origDirE; ++origDirI)
    path::append(currPCHPath, "..");
  path::append(currPCHPath, fileDirI, fileDirE);
  path::append(currPCHPath, path::filename(Filename));
  return currPCHPath.str();
}

bool ASTReader::ReadSLocEntry(int ID) {
  if (ID == 0)
    return false;

  if (unsigned(-ID) - 2 >= getTotalNumSLocs() || ID > 0) {
    Error("source location entry ID out-of-range for AST file");
    return true;
  }

  // Local helper to read the (possibly-compressed) buffer data following the
  // entry record.
  auto ReadBuffer = [this](
      BitstreamCursor &SLocEntryCursor,
      StringRef Name) -> std::unique_ptr<llvm::MemoryBuffer> {
    RecordData Record;
    StringRef Blob;
    unsigned Code = SLocEntryCursor.ReadCode();
    unsigned RecCode = SLocEntryCursor.readRecord(Code, Record, &Blob);

    if (RecCode == SM_SLOC_BUFFER_BLOB_COMPRESSED) {
      if (!llvm::zlib::isAvailable()) {
        Error("zlib is not available");
        return nullptr;
      }
      SmallString<0> Uncompressed;
      if (llvm::Error E =
              llvm::zlib::uncompress(Blob, Uncompressed, Record[0])) {
        Error("could not decompress embedded file contents: " +
              llvm::toString(std::move(E)));
        return nullptr;
      }
      return llvm::MemoryBuffer::getMemBufferCopy(Uncompressed, Name);
    } else if (RecCode == SM_SLOC_BUFFER_BLOB) {
      return llvm::MemoryBuffer::getMemBuffer(Blob.drop_back(1), Name, true);
    } else {
      Error("AST record has invalid code");
      return nullptr;
    }
  };

  ModuleFile *F = GlobalSLocEntryMap.find(-ID)->second;
  F->SLocEntryCursor.JumpToBit(F->SLocEntryOffsets[ID - F->SLocEntryBaseID]);
  BitstreamCursor &SLocEntryCursor = F->SLocEntryCursor;
  unsigned BaseOffset = F->SLocEntryBaseOffset;

  ++NumSLocEntriesRead;
  llvm::BitstreamEntry Entry = SLocEntryCursor.advance();
  if (Entry.Kind != llvm::BitstreamEntry::Record) {
    Error("incorrectly-formatted source location entry in AST file");
    return true;
  }

  RecordData Record;
  StringRef Blob;
  switch (SLocEntryCursor.readRecord(Entry.ID, Record, &Blob)) {
  default:
    Error("incorrectly-formatted source location entry in AST file");
    return true;

  case SM_SLOC_FILE_ENTRY: {
    // We will detect whether a file changed and return 'Failure' for it, but
    // we will also try to fail gracefully by setting up the SLocEntry.
    unsigned InputID = Record[4];
    InputFile IF = getInputFile(*F, InputID);
    const FileEntry *File = IF.getFile();
    bool OverriddenBuffer = IF.isOverridden();

    // Note that we only check if a File was returned. If it was out-of-date
    // we have complained but we will continue creating a FileID to recover
    // gracefully.
    if (!File)
      return true;

    SourceLocation IncludeLoc = ReadSourceLocation(*F, Record[1]);
    if (IncludeLoc.isInvalid() && F->Kind != MK_MainFile) {
      // This is the module's main file.
      IncludeLoc = getImportLocation(F);
    }
    SrcMgr::CharacteristicKind
      FileCharacter = (SrcMgr::CharacteristicKind)Record[2];
    FileID FID = SourceMgr.createFileID(File, IncludeLoc, FileCharacter,
                                        ID, BaseOffset + Record[0]);
    SrcMgr::FileInfo &FileInfo =
          const_cast<SrcMgr::FileInfo&>(SourceMgr.getSLocEntry(FID).getFile());
    FileInfo.NumCreatedFIDs = Record[5];
    if (Record[3])
      FileInfo.setHasLineDirectives();

    const DeclID *FirstDecl = F->FileSortedDecls + Record[6];
    unsigned NumFileDecls = Record[7];
    if (NumFileDecls && ContextObj) {
      assert(F->FileSortedDecls && "FILE_SORTED_DECLS not encountered yet ?");
      FileDeclIDs[FID] = FileDeclsInfo(F, llvm::makeArrayRef(FirstDecl,
                                                             NumFileDecls));
    }

    const SrcMgr::ContentCache *ContentCache
      = SourceMgr.getOrCreateContentCache(File, isSystem(FileCharacter));
    if (OverriddenBuffer && !ContentCache->BufferOverridden &&
        ContentCache->ContentsEntry == ContentCache->OrigEntry &&
        !ContentCache->getRawBuffer()) {
      auto Buffer = ReadBuffer(SLocEntryCursor, File->getName());
      if (!Buffer)
        return true;
      SourceMgr.overrideFileContents(File, std::move(Buffer));
    }

    break;
  }

  case SM_SLOC_BUFFER_ENTRY: {
    const char *Name = Blob.data();
    unsigned Offset = Record[0];
    SrcMgr::CharacteristicKind
      FileCharacter = (SrcMgr::CharacteristicKind)Record[2];
    SourceLocation IncludeLoc = ReadSourceLocation(*F, Record[1]);
    if (IncludeLoc.isInvalid() && F->isModule()) {
      IncludeLoc = getImportLocation(F);
    }

    auto Buffer = ReadBuffer(SLocEntryCursor, Name);
    if (!Buffer)
      return true;
    SourceMgr.createFileID(std::move(Buffer), FileCharacter, ID,
                           BaseOffset + Offset, IncludeLoc);
    break;
  }

  case SM_SLOC_EXPANSION_ENTRY: {
    SourceLocation SpellingLoc = ReadSourceLocation(*F, Record[1]);
    SourceMgr.createExpansionLoc(SpellingLoc,
                                     ReadSourceLocation(*F, Record[2]),
                                     ReadSourceLocation(*F, Record[3]),
                                     Record[5],
                                     Record[4],
                                     ID,
                                     BaseOffset + Record[0]);
    break;
  }
  }

  return false;
}

std::pair<SourceLocation, StringRef> ASTReader::getModuleImportLoc(int ID) {
  if (ID == 0)
    return std::make_pair(SourceLocation(), "");

  if (unsigned(-ID) - 2 >= getTotalNumSLocs() || ID > 0) {
    Error("source location entry ID out-of-range for AST file");
    return std::make_pair(SourceLocation(), "");
  }

  // Find which module file this entry lands in.
  ModuleFile *M = GlobalSLocEntryMap.find(-ID)->second;
  if (!M->isModule())
    return std::make_pair(SourceLocation(), "");

  // FIXME: Can we map this down to a particular submodule? That would be
  // ideal.
  return std::make_pair(M->ImportLoc, StringRef(M->ModuleName));
}

/// Find the location where the module F is imported.
SourceLocation ASTReader::getImportLocation(ModuleFile *F) {
  if (F->ImportLoc.isValid())
    return F->ImportLoc;

  // Otherwise we have a PCH. It's considered to be "imported" at the first
  // location of its includer.
  if (F->ImportedBy.empty() || !F->ImportedBy[0]) {
    // Main file is the importer.
    assert(SourceMgr.getMainFileID().isValid() && "missing main file");
    return SourceMgr.getLocForStartOfFile(SourceMgr.getMainFileID());
  }
  return F->ImportedBy[0]->FirstLoc;
}

/// ReadBlockAbbrevs - Enter a subblock of the specified BlockID with the
/// specified cursor.  Read the abbreviations that are at the top of the block
/// and then leave the cursor pointing into the block.
bool ASTReader::ReadBlockAbbrevs(BitstreamCursor &Cursor, unsigned BlockID) {
  if (Cursor.EnterSubBlock(BlockID))
    return true;

  while (true) {
    uint64_t Offset = Cursor.GetCurrentBitNo();
    unsigned Code = Cursor.ReadCode();

    // We expect all abbrevs to be at the start of the block.
    if (Code != llvm::bitc::DEFINE_ABBREV) {
      Cursor.JumpToBit(Offset);
      return false;
    }
    Cursor.ReadAbbrevRecord();
  }
}

Token ASTReader::ReadToken(ModuleFile &F, const RecordDataImpl &Record,
                           unsigned &Idx) {
  Token Tok;
  Tok.startToken();
  Tok.setLocation(ReadSourceLocation(F, Record, Idx));
  Tok.setLength(Record[Idx++]);
  if (IdentifierInfo *II = getLocalIdentifier(F, Record[Idx++]))
    Tok.setIdentifierInfo(II);
  Tok.setKind((tok::TokenKind)Record[Idx++]);
  Tok.setFlag((Token::TokenFlags)Record[Idx++]);
  return Tok;
}

MacroInfo *ASTReader::ReadMacroRecord(ModuleFile &F, uint64_t Offset) {
  BitstreamCursor &Stream = F.MacroCursor;

  // Keep track of where we are in the stream, then jump back there
  // after reading this macro.
  SavedStreamPosition SavedPosition(Stream);

  Stream.JumpToBit(Offset);
  RecordData Record;
  SmallVector<IdentifierInfo*, 16> MacroParams;
  MacroInfo *Macro = nullptr;

  while (true) {
    // Advance to the next record, but if we get to the end of the block, don't
    // pop it (removing all the abbreviations from the cursor) since we want to
    // be able to reseek within the block and read entries.
    unsigned Flags = BitstreamCursor::AF_DontPopBlockAtEnd;
    llvm::BitstreamEntry Entry = Stream.advanceSkippingSubblocks(Flags);

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return Macro;
    case llvm::BitstreamEntry::EndBlock:
      return Macro;
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    PreprocessorRecordTypes RecType =
      (PreprocessorRecordTypes)Stream.readRecord(Entry.ID, Record);
    switch (RecType) {
    case PP_MODULE_MACRO:
    case PP_MACRO_DIRECTIVE_HISTORY:
      return Macro;

    case PP_MACRO_OBJECT_LIKE:
    case PP_MACRO_FUNCTION_LIKE: {
      // If we already have a macro, that means that we've hit the end
      // of the definition of the macro we were looking for. We're
      // done.
      if (Macro)
        return Macro;

      unsigned NextIndex = 1; // Skip identifier ID.
      SourceLocation Loc = ReadSourceLocation(F, Record, NextIndex);
      MacroInfo *MI = PP.AllocateMacroInfo(Loc);
      MI->setDefinitionEndLoc(ReadSourceLocation(F, Record, NextIndex));
      MI->setIsUsed(Record[NextIndex++]);
      MI->setUsedForHeaderGuard(Record[NextIndex++]);

      if (RecType == PP_MACRO_FUNCTION_LIKE) {
        // Decode function-like macro info.
        bool isC99VarArgs = Record[NextIndex++];
        bool isGNUVarArgs = Record[NextIndex++];
        bool hasCommaPasting = Record[NextIndex++];
        MacroParams.clear();
        unsigned NumArgs = Record[NextIndex++];
        for (unsigned i = 0; i != NumArgs; ++i)
          MacroParams.push_back(getLocalIdentifier(F, Record[NextIndex++]));

        // Install function-like macro info.
        MI->setIsFunctionLike();
        if (isC99VarArgs) MI->setIsC99Varargs();
        if (isGNUVarArgs) MI->setIsGNUVarargs();
        if (hasCommaPasting) MI->setHasCommaPasting();
        MI->setParameterList(MacroParams, PP.getPreprocessorAllocator());
      }

      // Remember that we saw this macro last so that we add the tokens that
      // form its body to it.
      Macro = MI;

      if (NextIndex + 1 == Record.size() && PP.getPreprocessingRecord() &&
          Record[NextIndex]) {
        // We have a macro definition. Register the association
        PreprocessedEntityID
            GlobalID = getGlobalPreprocessedEntityID(F, Record[NextIndex]);
        PreprocessingRecord &PPRec = *PP.getPreprocessingRecord();
        PreprocessingRecord::PPEntityID PPID =
            PPRec.getPPEntityID(GlobalID - 1, /*isLoaded=*/true);
        MacroDefinitionRecord *PPDef = cast_or_null<MacroDefinitionRecord>(
            PPRec.getPreprocessedEntity(PPID));
        if (PPDef)
          PPRec.RegisterMacroDefinition(Macro, PPDef);
      }

      ++NumMacrosRead;
      break;
    }

    case PP_TOKEN: {
      // If we see a TOKEN before a PP_MACRO_*, then the file is
      // erroneous, just pretend we didn't see this.
      if (!Macro) break;

      unsigned Idx = 0;
      Token Tok = ReadToken(F, Record, Idx);
      Macro->AddTokenToBody(Tok);
      break;
    }
    }
  }
}

PreprocessedEntityID
ASTReader::getGlobalPreprocessedEntityID(ModuleFile &M,
                                         unsigned LocalID) const {
  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::const_iterator
    I = M.PreprocessedEntityRemap.find(LocalID - NUM_PREDEF_PP_ENTITY_IDS);
  assert(I != M.PreprocessedEntityRemap.end()
         && "Invalid index into preprocessed entity index remap");

  return LocalID + I->second;
}

unsigned HeaderFileInfoTrait::ComputeHash(internal_key_ref ikey) {
  return llvm::hash_combine(ikey.Size, ikey.ModTime);
}

HeaderFileInfoTrait::internal_key_type
HeaderFileInfoTrait::GetInternalKey(const FileEntry *FE) {
  internal_key_type ikey = {FE->getSize(),
                            M.HasTimestamps ? FE->getModificationTime() : 0,
                            FE->getName(), /*Imported*/ false};
  return ikey;
}

bool HeaderFileInfoTrait::EqualKey(internal_key_ref a, internal_key_ref b) {
  if (a.Size != b.Size || (a.ModTime && b.ModTime && a.ModTime != b.ModTime))
    return false;

  if (llvm::sys::path::is_absolute(a.Filename) && a.Filename == b.Filename)
    return true;

  // Determine whether the actual files are equivalent.
  FileManager &FileMgr = Reader.getFileManager();
  auto GetFile = [&](const internal_key_type &Key) -> const FileEntry* {
    if (!Key.Imported)
      return FileMgr.getFile(Key.Filename);

    std::string Resolved = Key.Filename;
    Reader.ResolveImportedPath(M, Resolved);
    return FileMgr.getFile(Resolved);
  };

  const FileEntry *FEA = GetFile(a);
  const FileEntry *FEB = GetFile(b);
  return FEA && FEA == FEB;
}

std::pair<unsigned, unsigned>
HeaderFileInfoTrait::ReadKeyDataLength(const unsigned char*& d) {
  using namespace llvm::support;

  unsigned KeyLen = (unsigned) endian::readNext<uint16_t, little, unaligned>(d);
  unsigned DataLen = (unsigned) *d++;
  return std::make_pair(KeyLen, DataLen);
}

HeaderFileInfoTrait::internal_key_type
HeaderFileInfoTrait::ReadKey(const unsigned char *d, unsigned) {
  using namespace llvm::support;

  internal_key_type ikey;
  ikey.Size = off_t(endian::readNext<uint64_t, little, unaligned>(d));
  ikey.ModTime = time_t(endian::readNext<uint64_t, little, unaligned>(d));
  ikey.Filename = (const char *)d;
  ikey.Imported = true;
  return ikey;
}

HeaderFileInfoTrait::data_type
HeaderFileInfoTrait::ReadData(internal_key_ref key, const unsigned char *d,
                              unsigned DataLen) {
  using namespace llvm::support;

  const unsigned char *End = d + DataLen;
  HeaderFileInfo HFI;
  unsigned Flags = *d++;
  // FIXME: Refactor with mergeHeaderFileInfo in HeaderSearch.cpp.
  HFI.isImport |= (Flags >> 5) & 0x01;
  HFI.isPragmaOnce |= (Flags >> 4) & 0x01;
  HFI.DirInfo = (Flags >> 1) & 0x07;
  HFI.IndexHeaderMapHeader = Flags & 0x01;
  // FIXME: Find a better way to handle this. Maybe just store a
  // "has been included" flag?
  HFI.NumIncludes = std::max(endian::readNext<uint16_t, little, unaligned>(d),
                             HFI.NumIncludes);
  HFI.ControllingMacroID = Reader.getGlobalIdentifierID(
      M, endian::readNext<uint32_t, little, unaligned>(d));
  if (unsigned FrameworkOffset =
          endian::readNext<uint32_t, little, unaligned>(d)) {
    // The framework offset is 1 greater than the actual offset,
    // since 0 is used as an indicator for "no framework name".
    StringRef FrameworkName(FrameworkStrings + FrameworkOffset - 1);
    HFI.Framework = HS->getUniqueFrameworkName(FrameworkName);
  }

  assert((End - d) % 4 == 0 &&
         "Wrong data length in HeaderFileInfo deserialization");
  while (d != End) {
    uint32_t LocalSMID = endian::readNext<uint32_t, little, unaligned>(d);
    auto HeaderRole = static_cast<ModuleMap::ModuleHeaderRole>(LocalSMID & 3);
    LocalSMID >>= 2;

    // This header is part of a module. Associate it with the module to enable
    // implicit module import.
    SubmoduleID GlobalSMID = Reader.getGlobalSubmoduleID(M, LocalSMID);
    Module *Mod = Reader.getSubmodule(GlobalSMID);
    FileManager &FileMgr = Reader.getFileManager();
    ModuleMap &ModMap =
        Reader.getPreprocessor().getHeaderSearchInfo().getModuleMap();

    std::string Filename = key.Filename;
    if (key.Imported)
      Reader.ResolveImportedPath(M, Filename);
    // FIXME: This is not always the right filename-as-written, but we're not
    // going to use this information to rebuild the module, so it doesn't make
    // a lot of difference.
    Module::Header H = { key.Filename, FileMgr.getFile(Filename) };
    ModMap.addHeader(Mod, H, HeaderRole, /*Imported*/true);
    HFI.isModuleHeader |= !(HeaderRole & ModuleMap::TextualHeader);
  }

  // This HeaderFileInfo was externally loaded.
  HFI.External = true;
  HFI.IsValid = true;
  return HFI;
}

void ASTReader::addPendingMacro(IdentifierInfo *II,
                                ModuleFile *M,
                                uint64_t MacroDirectivesOffset) {
  assert(NumCurrentElementsDeserializing > 0 &&"Missing deserialization guard");
  PendingMacroIDs[II].push_back(PendingMacroInfo(M, MacroDirectivesOffset));
}

void ASTReader::ReadDefinedMacros() {
  // Note that we are loading defined macros.
  Deserializing Macros(this);

  for (ModuleFile &I : llvm::reverse(ModuleMgr)) {
    BitstreamCursor &MacroCursor = I.MacroCursor;

    // If there was no preprocessor block, skip this file.
    if (MacroCursor.getBitcodeBytes().empty())
      continue;

    BitstreamCursor Cursor = MacroCursor;
    Cursor.JumpToBit(I.MacroStartOffset);

    RecordData Record;
    while (true) {
      llvm::BitstreamEntry E = Cursor.advanceSkippingSubblocks();

      switch (E.Kind) {
      case llvm::BitstreamEntry::SubBlock: // Handled for us already.
      case llvm::BitstreamEntry::Error:
        Error("malformed block record in AST file");
        return;
      case llvm::BitstreamEntry::EndBlock:
        goto NextCursor;

      case llvm::BitstreamEntry::Record:
        Record.clear();
        switch (Cursor.readRecord(E.ID, Record)) {
        default:  // Default behavior: ignore.
          break;

        case PP_MACRO_OBJECT_LIKE:
        case PP_MACRO_FUNCTION_LIKE: {
          IdentifierInfo *II = getLocalIdentifier(I, Record[0]);
          if (II->isOutOfDate())
            updateOutOfDateIdentifier(*II);
          break;
        }

        case PP_TOKEN:
          // Ignore tokens.
          break;
        }
        break;
      }
    }
    NextCursor:  ;
  }
}

namespace {

  /// Visitor class used to look up identifirs in an AST file.
  class IdentifierLookupVisitor {
    StringRef Name;
    unsigned NameHash;
    unsigned PriorGeneration;
    unsigned &NumIdentifierLookups;
    unsigned &NumIdentifierLookupHits;
    IdentifierInfo *Found = nullptr;

  public:
    IdentifierLookupVisitor(StringRef Name, unsigned PriorGeneration,
                            unsigned &NumIdentifierLookups,
                            unsigned &NumIdentifierLookupHits)
      : Name(Name), NameHash(ASTIdentifierLookupTrait::ComputeHash(Name)),
        PriorGeneration(PriorGeneration),
        NumIdentifierLookups(NumIdentifierLookups),
        NumIdentifierLookupHits(NumIdentifierLookupHits) {}

    bool operator()(ModuleFile &M) {
      // If we've already searched this module file, skip it now.
      if (M.Generation <= PriorGeneration)
        return true;

      ASTIdentifierLookupTable *IdTable
        = (ASTIdentifierLookupTable *)M.IdentifierLookupTable;
      if (!IdTable)
        return false;

      ASTIdentifierLookupTrait Trait(IdTable->getInfoObj().getReader(), M,
                                     Found);
      ++NumIdentifierLookups;
      ASTIdentifierLookupTable::iterator Pos =
          IdTable->find_hashed(Name, NameHash, &Trait);
      if (Pos == IdTable->end())
        return false;

      // Dereferencing the iterator has the effect of building the
      // IdentifierInfo node and populating it with the various
      // declarations it needs.
      ++NumIdentifierLookupHits;
      Found = *Pos;
      return true;
    }

    // Retrieve the identifier info found within the module
    // files.
    IdentifierInfo *getIdentifierInfo() const { return Found; }
  };

} // namespace

void ASTReader::updateOutOfDateIdentifier(IdentifierInfo &II) {
  // Note that we are loading an identifier.
  Deserializing AnIdentifier(this);

  unsigned PriorGeneration = 0;
  if (getContext().getLangOpts().Modules)
    PriorGeneration = IdentifierGeneration[&II];

  // If there is a global index, look there first to determine which modules
  // provably do not have any results for this identifier.
  GlobalModuleIndex::HitSet Hits;
  GlobalModuleIndex::HitSet *HitsPtr = nullptr;
  if (!loadGlobalIndex()) {
    if (GlobalIndex->lookupIdentifier(II.getName(), Hits)) {
      HitsPtr = &Hits;
    }
  }

  IdentifierLookupVisitor Visitor(II.getName(), PriorGeneration,
                                  NumIdentifierLookups,
                                  NumIdentifierLookupHits);
  ModuleMgr.visit(Visitor, HitsPtr);
  markIdentifierUpToDate(&II);
}

void ASTReader::markIdentifierUpToDate(IdentifierInfo *II) {
  if (!II)
    return;

  II->setOutOfDate(false);

  // Update the generation for this identifier.
  if (getContext().getLangOpts().Modules)
    IdentifierGeneration[II] = getGeneration();
}

void ASTReader::resolvePendingMacro(IdentifierInfo *II,
                                    const PendingMacroInfo &PMInfo) {
  ModuleFile &M = *PMInfo.M;

  BitstreamCursor &Cursor = M.MacroCursor;
  SavedStreamPosition SavedPosition(Cursor);
  Cursor.JumpToBit(PMInfo.MacroDirectivesOffset);

  struct ModuleMacroRecord {
    SubmoduleID SubModID;
    MacroInfo *MI;
    SmallVector<SubmoduleID, 8> Overrides;
  };
  llvm::SmallVector<ModuleMacroRecord, 8> ModuleMacros;

  // We expect to see a sequence of PP_MODULE_MACRO records listing exported
  // macros, followed by a PP_MACRO_DIRECTIVE_HISTORY record with the complete
  // macro histroy.
  RecordData Record;
  while (true) {
    llvm::BitstreamEntry Entry =
        Cursor.advance(BitstreamCursor::AF_DontPopBlockAtEnd);
    if (Entry.Kind != llvm::BitstreamEntry::Record) {
      Error("malformed block record in AST file");
      return;
    }

    Record.clear();
    switch ((PreprocessorRecordTypes)Cursor.readRecord(Entry.ID, Record)) {
    case PP_MACRO_DIRECTIVE_HISTORY:
      break;

    case PP_MODULE_MACRO: {
      ModuleMacros.push_back(ModuleMacroRecord());
      auto &Info = ModuleMacros.back();
      Info.SubModID = getGlobalSubmoduleID(M, Record[0]);
      Info.MI = getMacro(getGlobalMacroID(M, Record[1]));
      for (int I = 2, N = Record.size(); I != N; ++I)
        Info.Overrides.push_back(getGlobalSubmoduleID(M, Record[I]));
      continue;
    }

    default:
      Error("malformed block record in AST file");
      return;
    }

    // We found the macro directive history; that's the last record
    // for this macro.
    break;
  }

  // Module macros are listed in reverse dependency order.
  {
    std::reverse(ModuleMacros.begin(), ModuleMacros.end());
    llvm::SmallVector<ModuleMacro*, 8> Overrides;
    for (auto &MMR : ModuleMacros) {
      Overrides.clear();
      for (unsigned ModID : MMR.Overrides) {
        Module *Mod = getSubmodule(ModID);
        auto *Macro = PP.getModuleMacro(Mod, II);
        assert(Macro && "missing definition for overridden macro");
        Overrides.push_back(Macro);
      }

      bool Inserted = false;
      Module *Owner = getSubmodule(MMR.SubModID);
      PP.addModuleMacro(Owner, II, MMR.MI, Overrides, Inserted);
    }
  }

  // Don't read the directive history for a module; we don't have anywhere
  // to put it.
  if (M.isModule())
    return;

  // Deserialize the macro directives history in reverse source-order.
  MacroDirective *Latest = nullptr, *Earliest = nullptr;
  unsigned Idx = 0, N = Record.size();
  while (Idx < N) {
    MacroDirective *MD = nullptr;
    SourceLocation Loc = ReadSourceLocation(M, Record, Idx);
    MacroDirective::Kind K = (MacroDirective::Kind)Record[Idx++];
    switch (K) {
    case MacroDirective::MD_Define: {
      MacroInfo *MI = getMacro(getGlobalMacroID(M, Record[Idx++]));
      MD = PP.AllocateDefMacroDirective(MI, Loc);
      break;
    }
    case MacroDirective::MD_Undefine:
      MD = PP.AllocateUndefMacroDirective(Loc);
      break;
    case MacroDirective::MD_Visibility:
      bool isPublic = Record[Idx++];
      MD = PP.AllocateVisibilityMacroDirective(Loc, isPublic);
      break;
    }

    if (!Latest)
      Latest = MD;
    if (Earliest)
      Earliest->setPrevious(MD);
    Earliest = MD;
  }

  if (Latest)
    PP.setLoadedMacroDirective(II, Earliest, Latest);
}

ASTReader::InputFileInfo
ASTReader::readInputFileInfo(ModuleFile &F, unsigned ID) {
  // Go find this input file.
  BitstreamCursor &Cursor = F.InputFilesCursor;
  SavedStreamPosition SavedPosition(Cursor);
  Cursor.JumpToBit(F.InputFileOffsets[ID-1]);

  unsigned Code = Cursor.ReadCode();
  RecordData Record;
  StringRef Blob;

  unsigned Result = Cursor.readRecord(Code, Record, &Blob);
  assert(static_cast<InputFileRecordTypes>(Result) == INPUT_FILE &&
         "invalid record type for input file");
  (void)Result;

  assert(Record[0] == ID && "Bogus stored ID or offset");
  InputFileInfo R;
  R.StoredSize = static_cast<off_t>(Record[1]);
  R.StoredTime = static_cast<time_t>(Record[2]);
  R.Overridden = static_cast<bool>(Record[3]);
  R.Transient = static_cast<bool>(Record[4]);
  R.TopLevelModuleMap = static_cast<bool>(Record[5]);
  R.Filename = Blob;
  ResolveImportedPath(F, R.Filename);
  return R;
}

static unsigned moduleKindForDiagnostic(ModuleKind Kind);
InputFile ASTReader::getInputFile(ModuleFile &F, unsigned ID, bool Complain) {
  // If this ID is bogus, just return an empty input file.
  if (ID == 0 || ID > F.InputFilesLoaded.size())
    return InputFile();

  // If we've already loaded this input file, return it.
  if (F.InputFilesLoaded[ID-1].getFile())
    return F.InputFilesLoaded[ID-1];

  if (F.InputFilesLoaded[ID-1].isNotFound())
    return InputFile();

  // Go find this input file.
  BitstreamCursor &Cursor = F.InputFilesCursor;
  SavedStreamPosition SavedPosition(Cursor);
  Cursor.JumpToBit(F.InputFileOffsets[ID-1]);

  InputFileInfo FI = readInputFileInfo(F, ID);
  off_t StoredSize = FI.StoredSize;
  time_t StoredTime = FI.StoredTime;
  bool Overridden = FI.Overridden;
  bool Transient = FI.Transient;
  StringRef Filename = FI.Filename;

  const FileEntry *File = FileMgr.getFile(Filename, /*OpenFile=*/false);
  // If we didn't find the file, resolve it relative to the
  // original directory from which this AST file was created.
  if (File == nullptr && !F.OriginalDir.empty() && !F.BaseDirectory.empty() &&
      F.OriginalDir != F.BaseDirectory) {
    std::string Resolved = resolveFileRelativeToOriginalDir(
        Filename, F.OriginalDir, F.BaseDirectory);
    if (!Resolved.empty())
      File = FileMgr.getFile(Resolved);
  }

  // For an overridden file, create a virtual file with the stored
  // size/timestamp.
  if ((Overridden || Transient) && File == nullptr)
    File = FileMgr.getVirtualFile(Filename, StoredSize, StoredTime);

  if (File == nullptr) {
    if (Complain) {
      std::string ErrorStr = "could not find file '";
      ErrorStr += Filename;
      ErrorStr += "' referenced by AST file '";
      ErrorStr += F.FileName;
      ErrorStr += "'";
      Error(ErrorStr);
    }
    // Record that we didn't find the file.
    F.InputFilesLoaded[ID-1] = InputFile::getNotFound();
    return InputFile();
  }

  // Check if there was a request to override the contents of the file
  // that was part of the precompiled header. Overriding such a file
  // can lead to problems when lexing using the source locations from the
  // PCH.
  SourceManager &SM = getSourceManager();
  // FIXME: Reject if the overrides are different.
  if ((!Overridden && !Transient) && SM.isFileOverridden(File)) {
    if (Complain)
      Error(diag::err_fe_pch_file_overridden, Filename);
    // After emitting the diagnostic, recover by disabling the override so
    // that the original file will be used.
    //
    // FIXME: This recovery is just as broken as the original state; there may
    // be another precompiled module that's using the overridden contents, or
    // we might be half way through parsing it. Instead, we should treat the
    // overridden contents as belonging to a separate FileEntry.
    SM.disableFileContentsOverride(File);
    // The FileEntry is a virtual file entry with the size of the contents
    // that would override the original contents. Set it to the original's
    // size/time.
    FileMgr.modifyFileEntry(const_cast<FileEntry*>(File),
                            StoredSize, StoredTime);
  }

  bool IsOutOfDate = false;

  // For an overridden file, there is nothing to validate.
  if (!Overridden && //
      (StoredSize != File->getSize() ||
       (StoredTime && StoredTime != File->getModificationTime() &&
        !DisableValidation)
       )) {
    if (Complain) {
      // Build a list of the PCH imports that got us here (in reverse).
      SmallVector<ModuleFile *, 4> ImportStack(1, &F);
      while (!ImportStack.back()->ImportedBy.empty())
        ImportStack.push_back(ImportStack.back()->ImportedBy[0]);

      // The top-level PCH is stale.
      StringRef TopLevelPCHName(ImportStack.back()->FileName);
      unsigned DiagnosticKind = moduleKindForDiagnostic(ImportStack.back()->Kind);
      if (DiagnosticKind == 0)
        Error(diag::err_fe_pch_file_modified, Filename, TopLevelPCHName);
      else if (DiagnosticKind == 1)
        Error(diag::err_fe_module_file_modified, Filename, TopLevelPCHName);
      else
        Error(diag::err_fe_ast_file_modified, Filename, TopLevelPCHName);

      // Print the import stack.
      if (ImportStack.size() > 1 && !Diags.isDiagnosticInFlight()) {
        Diag(diag::note_pch_required_by)
          << Filename << ImportStack[0]->FileName;
        for (unsigned I = 1; I < ImportStack.size(); ++I)
          Diag(diag::note_pch_required_by)
            << ImportStack[I-1]->FileName << ImportStack[I]->FileName;
      }

      if (!Diags.isDiagnosticInFlight())
        Diag(diag::note_pch_rebuild_required) << TopLevelPCHName;
    }

    IsOutOfDate = true;
  }
  // FIXME: If the file is overridden and we've already opened it,
  // issue an error (or split it into a separate FileEntry).

  InputFile IF = InputFile(File, Overridden || Transient, IsOutOfDate);

  // Note that we've loaded this input file.
  F.InputFilesLoaded[ID-1] = IF;
  return IF;
}

/// If we are loading a relocatable PCH or module file, and the filename
/// is not an absolute path, add the system or module root to the beginning of
/// the file name.
void ASTReader::ResolveImportedPath(ModuleFile &M, std::string &Filename) {
  // Resolve relative to the base directory, if we have one.
  if (!M.BaseDirectory.empty())
    return ResolveImportedPath(Filename, M.BaseDirectory);
}

void ASTReader::ResolveImportedPath(std::string &Filename, StringRef Prefix) {
  if (Filename.empty() || llvm::sys::path::is_absolute(Filename))
    return;

  SmallString<128> Buffer;
  llvm::sys::path::append(Buffer, Prefix, Filename);
  Filename.assign(Buffer.begin(), Buffer.end());
}

static bool isDiagnosedResult(ASTReader::ASTReadResult ARR, unsigned Caps) {
  switch (ARR) {
  case ASTReader::Failure: return true;
  case ASTReader::Missing: return !(Caps & ASTReader::ARR_Missing);
  case ASTReader::OutOfDate: return !(Caps & ASTReader::ARR_OutOfDate);
  case ASTReader::VersionMismatch: return !(Caps & ASTReader::ARR_VersionMismatch);
  case ASTReader::ConfigurationMismatch:
    return !(Caps & ASTReader::ARR_ConfigurationMismatch);
  case ASTReader::HadErrors: return true;
  case ASTReader::Success: return false;
  }

  llvm_unreachable("unknown ASTReadResult");
}

ASTReader::ASTReadResult ASTReader::ReadOptionsBlock(
    BitstreamCursor &Stream, unsigned ClientLoadCapabilities,
    bool AllowCompatibleConfigurationMismatch, ASTReaderListener &Listener,
    std::string &SuggestedPredefines) {
  if (Stream.EnterSubBlock(OPTIONS_BLOCK_ID))
    return Failure;

  // Read all of the records in the options block.
  RecordData Record;
  ASTReadResult Result = Success;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::SubBlock:
      return Failure;

    case llvm::BitstreamEntry::EndBlock:
      return Result;

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    switch ((OptionsRecordTypes)Stream.readRecord(Entry.ID, Record)) {
    case LANGUAGE_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (ParseLanguageOptions(Record, Complain, Listener,
                               AllowCompatibleConfigurationMismatch))
        Result = ConfigurationMismatch;
      break;
    }

    case TARGET_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (ParseTargetOptions(Record, Complain, Listener,
                             AllowCompatibleConfigurationMismatch))
        Result = ConfigurationMismatch;
      break;
    }

    case FILE_SYSTEM_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (!AllowCompatibleConfigurationMismatch &&
          ParseFileSystemOptions(Record, Complain, Listener))
        Result = ConfigurationMismatch;
      break;
    }

    case HEADER_SEARCH_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (!AllowCompatibleConfigurationMismatch &&
          ParseHeaderSearchOptions(Record, Complain, Listener))
        Result = ConfigurationMismatch;
      break;
    }

    case PREPROCESSOR_OPTIONS:
      bool Complain = (ClientLoadCapabilities & ARR_ConfigurationMismatch) == 0;
      if (!AllowCompatibleConfigurationMismatch &&
          ParsePreprocessorOptions(Record, Complain, Listener,
                                   SuggestedPredefines))
        Result = ConfigurationMismatch;
      break;
    }
  }
}

ASTReader::ASTReadResult
ASTReader::ReadControlBlock(ModuleFile &F,
                            SmallVectorImpl<ImportedModule> &Loaded,
                            const ModuleFile *ImportedBy,
                            unsigned ClientLoadCapabilities) {
  BitstreamCursor &Stream = F.Stream;
  ASTReadResult Result = Success;

  if (Stream.EnterSubBlock(CONTROL_BLOCK_ID)) {
    Error("malformed block record in AST file");
    return Failure;
  }

  // Lambda to read the unhashed control block the first time it's called.
  //
  // For PCM files, the unhashed control block cannot be read until after the
  // MODULE_NAME record.  However, PCH files have no MODULE_NAME, and yet still
  // need to look ahead before reading the IMPORTS record.  For consistency,
  // this block is always read somehow (see BitstreamEntry::EndBlock).
  bool HasReadUnhashedControlBlock = false;
  auto readUnhashedControlBlockOnce = [&]() {
    if (!HasReadUnhashedControlBlock) {
      HasReadUnhashedControlBlock = true;
      if (ASTReadResult Result =
              readUnhashedControlBlock(F, ImportedBy, ClientLoadCapabilities))
        return Result;
    }
    return Success;
  };

  // Read all of the records and blocks in the control block.
  RecordData Record;
  unsigned NumInputs = 0;
  unsigned NumUserInputs = 0;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return Failure;
    case llvm::BitstreamEntry::EndBlock: {
      // Validate the module before returning.  This call catches an AST with
      // no module name and no imports.
      if (ASTReadResult Result = readUnhashedControlBlockOnce())
        return Result;

      // Validate input files.
      const HeaderSearchOptions &HSOpts =
          PP.getHeaderSearchInfo().getHeaderSearchOpts();

      // All user input files reside at the index range [0, NumUserInputs), and
      // system input files reside at [NumUserInputs, NumInputs). For explicitly
      // loaded module files, ignore missing inputs.
      if (!DisableValidation && F.Kind != MK_ExplicitModule &&
          F.Kind != MK_PrebuiltModule) {
        bool Complain = (ClientLoadCapabilities & ARR_OutOfDate) == 0;

        // If we are reading a module, we will create a verification timestamp,
        // so we verify all input files.  Otherwise, verify only user input
        // files.

        unsigned N = NumUserInputs;
        if (ValidateSystemInputs ||
            (HSOpts.ModulesValidateOncePerBuildSession &&
             F.InputFilesValidationTimestamp <= HSOpts.BuildSessionTimestamp &&
             F.Kind == MK_ImplicitModule))
          N = NumInputs;

        for (unsigned I = 0; I < N; ++I) {
          InputFile IF = getInputFile(F, I+1, Complain);
          if (!IF.getFile() || IF.isOutOfDate())
            return OutOfDate;
        }
      }

      if (Listener)
        Listener->visitModuleFile(F.FileName, F.Kind);

      if (Listener && Listener->needsInputFileVisitation()) {
        unsigned N = Listener->needsSystemInputFileVisitation() ? NumInputs
                                                                : NumUserInputs;
        for (unsigned I = 0; I < N; ++I) {
          bool IsSystem = I >= NumUserInputs;
          InputFileInfo FI = readInputFileInfo(F, I+1);
          Listener->visitInputFile(FI.Filename, IsSystem, FI.Overridden,
                                   F.Kind == MK_ExplicitModule ||
                                   F.Kind == MK_PrebuiltModule);
        }
      }

      return Result;
    }

    case llvm::BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      case INPUT_FILES_BLOCK_ID:
        F.InputFilesCursor = Stream;
        if (Stream.SkipBlock() || // Skip with the main cursor
            // Read the abbreviations
            ReadBlockAbbrevs(F.InputFilesCursor, INPUT_FILES_BLOCK_ID)) {
          Error("malformed block record in AST file");
          return Failure;
        }
        continue;

      case OPTIONS_BLOCK_ID:
        // If we're reading the first module for this group, check its options
        // are compatible with ours. For modules it imports, no further checking
        // is required, because we checked them when we built it.
        if (Listener && !ImportedBy) {
          // Should we allow the configuration of the module file to differ from
          // the configuration of the current translation unit in a compatible
          // way?
          //
          // FIXME: Allow this for files explicitly specified with -include-pch.
          bool AllowCompatibleConfigurationMismatch =
              F.Kind == MK_ExplicitModule || F.Kind == MK_PrebuiltModule;

          Result = ReadOptionsBlock(Stream, ClientLoadCapabilities,
                                    AllowCompatibleConfigurationMismatch,
                                    *Listener, SuggestedPredefines);
          if (Result == Failure) {
            Error("malformed block record in AST file");
            return Result;
          }

          if (DisableValidation ||
              (AllowConfigurationMismatch && Result == ConfigurationMismatch))
            Result = Success;

          // If we can't load the module, exit early since we likely
          // will rebuild the module anyway. The stream may be in the
          // middle of a block.
          if (Result != Success)
            return Result;
        } else if (Stream.SkipBlock()) {
          Error("malformed block record in AST file");
          return Failure;
        }
        continue;

      default:
        if (Stream.SkipBlock()) {
          Error("malformed block record in AST file");
          return Failure;
        }
        continue;
      }

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    StringRef Blob;
    switch ((ControlRecordTypes)Stream.readRecord(Entry.ID, Record, &Blob)) {
    case METADATA: {
      if (Record[0] != VERSION_MAJOR && !DisableValidation) {
        if ((ClientLoadCapabilities & ARR_VersionMismatch) == 0)
          Diag(Record[0] < VERSION_MAJOR? diag::err_pch_version_too_old
                                        : diag::err_pch_version_too_new);
        return VersionMismatch;
      }

      bool hasErrors = Record[7];
      if (hasErrors && !DisableValidation && !AllowASTWithCompilerErrors) {
        Diag(diag::err_pch_with_compiler_errors);
        return HadErrors;
      }
      if (hasErrors) {
        Diags.ErrorOccurred = true;
        Diags.UncompilableErrorOccurred = true;
        Diags.UnrecoverableErrorOccurred = true;
      }

      F.RelocatablePCH = Record[4];
      // Relative paths in a relocatable PCH are relative to our sysroot.
      if (F.RelocatablePCH)
        F.BaseDirectory = isysroot.empty() ? "/" : isysroot;

      F.HasTimestamps = Record[5];

      F.PCHHasObjectFile = Record[6];

      const std::string &CurBranch = getClangFullRepositoryVersion();
      StringRef ASTBranch = Blob;
      if (StringRef(CurBranch) != ASTBranch && !DisableValidation) {
        if ((ClientLoadCapabilities & ARR_VersionMismatch) == 0)
          Diag(diag::err_pch_different_branch) << ASTBranch << CurBranch;
        return VersionMismatch;
      }
      break;
    }

    case IMPORTS: {
      // Validate the AST before processing any imports (otherwise, untangling
      // them can be error-prone and expensive).  A module will have a name and
      // will already have been validated, but this catches the PCH case.
      if (ASTReadResult Result = readUnhashedControlBlockOnce())
        return Result;

      // Load each of the imported PCH files.
      unsigned Idx = 0, N = Record.size();
      while (Idx < N) {
        // Read information about the AST file.
        ModuleKind ImportedKind = (ModuleKind)Record[Idx++];
        // The import location will be the local one for now; we will adjust
        // all import locations of module imports after the global source
        // location info are setup, in ReadAST.
        SourceLocation ImportLoc =
            ReadUntranslatedSourceLocation(Record[Idx++]);
        off_t StoredSize = (off_t)Record[Idx++];
        time_t StoredModTime = (time_t)Record[Idx++];
        ASTFileSignature StoredSignature = {
            {{(uint32_t)Record[Idx++], (uint32_t)Record[Idx++],
              (uint32_t)Record[Idx++], (uint32_t)Record[Idx++],
              (uint32_t)Record[Idx++]}}};

        std::string ImportedName = ReadString(Record, Idx);
        std::string ImportedFile;

        // For prebuilt and explicit modules first consult the file map for
        // an override. Note that here we don't search prebuilt module
        // directories, only the explicit name to file mappings. Also, we will
        // still verify the size/signature making sure it is essentially the
        // same file but perhaps in a different location.
        if (ImportedKind == MK_PrebuiltModule || ImportedKind == MK_ExplicitModule)
          ImportedFile = PP.getHeaderSearchInfo().getPrebuiltModuleFileName(
            ImportedName, /*FileMapOnly*/ true);

        if (ImportedFile.empty())
          ImportedFile = ReadPath(F, Record, Idx);
        else
          SkipPath(Record, Idx);

        // If our client can't cope with us being out of date, we can't cope with
        // our dependency being missing.
        unsigned Capabilities = ClientLoadCapabilities;
        if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
          Capabilities &= ~ARR_Missing;

        // Load the AST file.
        auto Result = ReadASTCore(ImportedFile, ImportedKind, ImportLoc, &F,
                                  Loaded, StoredSize, StoredModTime,
                                  StoredSignature, Capabilities);

        // If we diagnosed a problem, produce a backtrace.
        if (isDiagnosedResult(Result, Capabilities))
          Diag(diag::note_module_file_imported_by)
              << F.FileName << !F.ModuleName.empty() << F.ModuleName;

        switch (Result) {
        case Failure: return Failure;
          // If we have to ignore the dependency, we'll have to ignore this too.
        case Missing:
        case OutOfDate: return OutOfDate;
        case VersionMismatch: return VersionMismatch;
        case ConfigurationMismatch: return ConfigurationMismatch;
        case HadErrors: return HadErrors;
        case Success: break;
        }
      }
      break;
    }

    case ORIGINAL_FILE:
      F.OriginalSourceFileID = FileID::get(Record[0]);
      F.ActualOriginalSourceFileName = Blob;
      F.OriginalSourceFileName = F.ActualOriginalSourceFileName;
      ResolveImportedPath(F, F.OriginalSourceFileName);
      break;

    case ORIGINAL_FILE_ID:
      F.OriginalSourceFileID = FileID::get(Record[0]);
      break;

    case ORIGINAL_PCH_DIR:
      F.OriginalDir = Blob;
      break;

    case MODULE_NAME:
      F.ModuleName = Blob;
      if (Listener)
        Listener->ReadModuleName(F.ModuleName);

      // Validate the AST as soon as we have a name so we can exit early on
      // failure.
      if (ASTReadResult Result = readUnhashedControlBlockOnce())
        return Result;

      break;

    case MODULE_DIRECTORY: {
      assert(!F.ModuleName.empty() &&
             "MODULE_DIRECTORY found before MODULE_NAME");
      // If we've already loaded a module map file covering this module, we may
      // have a better path for it (relative to the current build).
      Module *M = PP.getHeaderSearchInfo().lookupModule(
          F.ModuleName, /*AllowSearch*/ true,
          /*AllowExtraModuleMapSearch*/ true);
      if (M && M->Directory) {
        // If we're implicitly loading a module, the base directory can't
        // change between the build and use.
        // Don't emit module relocation error if we have -fno-validate-pch
        if (!PP.getPreprocessorOpts().DisablePCHValidation &&
            F.Kind != MK_ExplicitModule && F.Kind != MK_PrebuiltModule) {
          const DirectoryEntry *BuildDir =
              PP.getFileManager().getDirectory(Blob);
          if (!BuildDir || BuildDir != M->Directory) {
            if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
              Diag(diag::err_imported_module_relocated)
                  << F.ModuleName << Blob << M->Directory->getName();
            return OutOfDate;
          }
        }
        F.BaseDirectory = M->Directory->getName();
      } else {
        F.BaseDirectory = Blob;
      }
      break;
    }

    case MODULE_MAP_FILE:
      if (ASTReadResult Result =
              ReadModuleMapFileBlock(Record, F, ImportedBy, ClientLoadCapabilities))
        return Result;
      break;

    case INPUT_FILE_OFFSETS:
      NumInputs = Record[0];
      NumUserInputs = Record[1];
      F.InputFileOffsets =
          (const llvm::support::unaligned_uint64_t *)Blob.data();
      F.InputFilesLoaded.resize(NumInputs);
      F.NumUserInputFiles = NumUserInputs;
      break;
    }
  }
}

ASTReader::ASTReadResult
ASTReader::ReadASTBlock(ModuleFile &F, unsigned ClientLoadCapabilities) {
  BitstreamCursor &Stream = F.Stream;

  if (Stream.EnterSubBlock(AST_BLOCK_ID)) {
    Error("malformed block record in AST file");
    return Failure;
  }

  // Read all of the records and blocks for the AST file.
  RecordData Record;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
      Error("error at end of module block in AST file");
      return Failure;
    case llvm::BitstreamEntry::EndBlock:
      // Outside of C++, we do not store a lookup map for the translation unit.
      // Instead, mark it as needing a lookup map to be built if this module
      // contains any declarations lexically within it (which it always does!).
      // This usually has no cost, since we very rarely need the lookup map for
      // the translation unit outside C++.
      if (ASTContext *Ctx = ContextObj) {
        DeclContext *DC = Ctx->getTranslationUnitDecl();
        if (DC->hasExternalLexicalStorage() && !Ctx->getLangOpts().CPlusPlus)
          DC->setMustBuildLookupTable();
      }

      return Success;
    case llvm::BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      case DECLTYPES_BLOCK_ID:
        // We lazily load the decls block, but we want to set up the
        // DeclsCursor cursor to point into it.  Clone our current bitcode
        // cursor to it, enter the block and read the abbrevs in that block.
        // With the main cursor, we just skip over it.
        F.DeclsCursor = Stream;
        if (Stream.SkipBlock() ||  // Skip with the main cursor.
            // Read the abbrevs.
            ReadBlockAbbrevs(F.DeclsCursor, DECLTYPES_BLOCK_ID)) {
          Error("malformed block record in AST file");
          return Failure;
        }
        break;

      case PREPROCESSOR_BLOCK_ID:
        F.MacroCursor = Stream;
        if (!PP.getExternalSource())
          PP.setExternalSource(this);

        if (Stream.SkipBlock() ||
            ReadBlockAbbrevs(F.MacroCursor, PREPROCESSOR_BLOCK_ID)) {
          Error("malformed block record in AST file");
          return Failure;
        }
        F.MacroStartOffset = F.MacroCursor.GetCurrentBitNo();
        break;

      case PREPROCESSOR_DETAIL_BLOCK_ID:
        F.PreprocessorDetailCursor = Stream;
        if (Stream.SkipBlock() ||
            ReadBlockAbbrevs(F.PreprocessorDetailCursor,
                             PREPROCESSOR_DETAIL_BLOCK_ID)) {
              Error("malformed preprocessor detail record in AST file");
              return Failure;
            }
        F.PreprocessorDetailStartOffset
        = F.PreprocessorDetailCursor.GetCurrentBitNo();

        if (!PP.getPreprocessingRecord())
          PP.createPreprocessingRecord();
        if (!PP.getPreprocessingRecord()->getExternalSource())
          PP.getPreprocessingRecord()->SetExternalSource(*this);
        break;

      case SOURCE_MANAGER_BLOCK_ID:
        if (ReadSourceManagerBlock(F))
          return Failure;
        break;

      case SUBMODULE_BLOCK_ID:
        if (ASTReadResult Result =
                ReadSubmoduleBlock(F, ClientLoadCapabilities))
          return Result;
        break;

      case COMMENTS_BLOCK_ID: {
        BitstreamCursor C = Stream;
        if (Stream.SkipBlock() ||
            ReadBlockAbbrevs(C, COMMENTS_BLOCK_ID)) {
          Error("malformed comments block in AST file");
          return Failure;
        }
        CommentsCursors.push_back(std::make_pair(C, &F));
        break;
      }

      default:
        if (Stream.SkipBlock()) {
          Error("malformed block record in AST file");
          return Failure;
        }
        break;
      }
      continue;

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    StringRef Blob;
    auto RecordType =
        (ASTRecordTypes)Stream.readRecord(Entry.ID, Record, &Blob);

    // If we're not loading an AST context, we don't care about most records.
    if (!ContextObj) {
      switch (RecordType) {
      case IDENTIFIER_TABLE:
      case IDENTIFIER_OFFSET:
      case INTERESTING_IDENTIFIERS:
      case STATISTICS:
      case PP_CONDITIONAL_STACK:
      case PP_COUNTER_VALUE:
      case SOURCE_LOCATION_OFFSETS:
      case MODULE_OFFSET_MAP:
      case SOURCE_MANAGER_LINE_TABLE:
      case SOURCE_LOCATION_PRELOADS:
      case PPD_ENTITIES_OFFSETS:
      case HEADER_SEARCH_TABLE:
      case IMPORTED_MODULES:
      case MACRO_OFFSET:
        break;
      default:
        continue;
      }
    }

    switch (RecordType) {
    default:  // Default behavior: ignore.
      break;

    case TYPE_OFFSET: {
      if (F.LocalNumTypes != 0) {
        Error("duplicate TYPE_OFFSET record in AST file");
        return Failure;
      }
      F.TypeOffsets = (const uint32_t *)Blob.data();
      F.LocalNumTypes = Record[0];
      unsigned LocalBaseTypeIndex = Record[1];
      F.BaseTypeIndex = getTotalNumTypes();

      if (F.LocalNumTypes > 0) {
        // Introduce the global -> local mapping for types within this module.
        GlobalTypeMap.insert(std::make_pair(getTotalNumTypes(), &F));

        // Introduce the local -> global mapping for types within this module.
        F.TypeRemap.insertOrReplace(
          std::make_pair(LocalBaseTypeIndex,
                         F.BaseTypeIndex - LocalBaseTypeIndex));

        TypesLoaded.resize(TypesLoaded.size() + F.LocalNumTypes);
      }
      break;
    }

    case DECL_OFFSET: {
      if (F.LocalNumDecls != 0) {
        Error("duplicate DECL_OFFSET record in AST file");
        return Failure;
      }
      F.DeclOffsets = (const DeclOffset *)Blob.data();
      F.LocalNumDecls = Record[0];
      unsigned LocalBaseDeclID = Record[1];
      F.BaseDeclID = getTotalNumDecls();

      if (F.LocalNumDecls > 0) {
        // Introduce the global -> local mapping for declarations within this
        // module.
        GlobalDeclMap.insert(
          std::make_pair(getTotalNumDecls() + NUM_PREDEF_DECL_IDS, &F));

        // Introduce the local -> global mapping for declarations within this
        // module.
        F.DeclRemap.insertOrReplace(
          std::make_pair(LocalBaseDeclID, F.BaseDeclID - LocalBaseDeclID));

        // Introduce the global -> local mapping for declarations within this
        // module.
        F.GlobalToLocalDeclIDs[&F] = LocalBaseDeclID;

        DeclsLoaded.resize(DeclsLoaded.size() + F.LocalNumDecls);
      }
      break;
    }

    case TU_UPDATE_LEXICAL: {
      DeclContext *TU = ContextObj->getTranslationUnitDecl();
      LexicalContents Contents(
          reinterpret_cast<const llvm::support::unaligned_uint32_t *>(
              Blob.data()),
          static_cast<unsigned int>(Blob.size() / 4));
      TULexicalDecls.push_back(std::make_pair(&F, Contents));
      TU->setHasExternalLexicalStorage(true);
      break;
    }

    case UPDATE_VISIBLE: {
      unsigned Idx = 0;
      serialization::DeclID ID = ReadDeclID(F, Record, Idx);
      auto *Data = (const unsigned char*)Blob.data();
      PendingVisibleUpdates[ID].push_back(PendingVisibleUpdate{&F, Data});
      // If we've already loaded the decl, perform the updates when we finish
      // loading this block.
      if (Decl *D = GetExistingDecl(ID))
        PendingUpdateRecords.push_back(
            PendingUpdateRecord(ID, D, /*JustLoaded=*/false));
      break;
    }

    case IDENTIFIER_TABLE:
      F.IdentifierTableData = Blob.data();
      if (Record[0]) {
        F.IdentifierLookupTable = ASTIdentifierLookupTable::Create(
            (const unsigned char *)F.IdentifierTableData + Record[0],
            (const unsigned char *)F.IdentifierTableData + sizeof(uint32_t),
            (const unsigned char *)F.IdentifierTableData,
            ASTIdentifierLookupTrait(*this, F));

        PP.getIdentifierTable().setExternalIdentifierLookup(this);
      }
      break;

    case IDENTIFIER_OFFSET: {
      if (F.LocalNumIdentifiers != 0) {
        Error("duplicate IDENTIFIER_OFFSET record in AST file");
        return Failure;
      }
      F.IdentifierOffsets = (const uint32_t *)Blob.data();
      F.LocalNumIdentifiers = Record[0];
      unsigned LocalBaseIdentifierID = Record[1];
      F.BaseIdentifierID = getTotalNumIdentifiers();

      if (F.LocalNumIdentifiers > 0) {
        // Introduce the global -> local mapping for identifiers within this
        // module.
        GlobalIdentifierMap.insert(std::make_pair(getTotalNumIdentifiers() + 1,
                                                  &F));

        // Introduce the local -> global mapping for identifiers within this
        // module.
        F.IdentifierRemap.insertOrReplace(
          std::make_pair(LocalBaseIdentifierID,
                         F.BaseIdentifierID - LocalBaseIdentifierID));

        IdentifiersLoaded.resize(IdentifiersLoaded.size()
                                 + F.LocalNumIdentifiers);
      }
      break;
    }

    case INTERESTING_IDENTIFIERS:
      F.PreloadIdentifierOffsets.assign(Record.begin(), Record.end());
      break;

    case EAGERLY_DESERIALIZED_DECLS:
      // FIXME: Skip reading this record if our ASTConsumer doesn't care
      // about "interesting" decls (for instance, if we're building a module).
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        EagerlyDeserializedDecls.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case MODULAR_CODEGEN_DECLS:
      // FIXME: Skip reading this record if our ASTConsumer doesn't care about
      // them (ie: if we're not codegenerating this module).
      if (F.Kind == MK_MainFile)
        for (unsigned I = 0, N = Record.size(); I != N; ++I)
          EagerlyDeserializedDecls.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case SPECIAL_TYPES:
      if (SpecialTypes.empty()) {
        for (unsigned I = 0, N = Record.size(); I != N; ++I)
          SpecialTypes.push_back(getGlobalTypeID(F, Record[I]));
        break;
      }

      if (SpecialTypes.size() != Record.size()) {
        Error("invalid special-types record");
        return Failure;
      }

      for (unsigned I = 0, N = Record.size(); I != N; ++I) {
        serialization::TypeID ID = getGlobalTypeID(F, Record[I]);
        if (!SpecialTypes[I])
          SpecialTypes[I] = ID;
        // FIXME: If ID && SpecialTypes[I] != ID, do we need a separate
        // merge step?
      }
      break;

    case STATISTICS:
      TotalNumStatements += Record[0];
      TotalNumMacros += Record[1];
      TotalLexicalDeclContexts += Record[2];
      TotalVisibleDeclContexts += Record[3];
      break;

    case UNUSED_FILESCOPED_DECLS:
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        UnusedFileScopedDecls.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case DELEGATING_CTORS:
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        DelegatingCtorDecls.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case WEAK_UNDECLARED_IDENTIFIERS:
      if (Record.size() % 4 != 0) {
        Error("invalid weak identifiers record");
        return Failure;
      }

      // FIXME: Ignore weak undeclared identifiers from non-original PCH
      // files. This isn't the way to do it :)
      WeakUndeclaredIdentifiers.clear();

      // Translate the weak, undeclared identifiers into global IDs.
      for (unsigned I = 0, N = Record.size(); I < N; /* in loop */) {
        WeakUndeclaredIdentifiers.push_back(
          getGlobalIdentifierID(F, Record[I++]));
        WeakUndeclaredIdentifiers.push_back(
          getGlobalIdentifierID(F, Record[I++]));
        WeakUndeclaredIdentifiers.push_back(
          ReadSourceLocation(F, Record, I).getRawEncoding());
        WeakUndeclaredIdentifiers.push_back(Record[I++]);
      }
      break;

    case SELECTOR_OFFSETS: {
      F.SelectorOffsets = (const uint32_t *)Blob.data();
      F.LocalNumSelectors = Record[0];
      unsigned LocalBaseSelectorID = Record[1];
      F.BaseSelectorID = getTotalNumSelectors();

      if (F.LocalNumSelectors > 0) {
        // Introduce the global -> local mapping for selectors within this
        // module.
        GlobalSelectorMap.insert(std::make_pair(getTotalNumSelectors()+1, &F));

        // Introduce the local -> global mapping for selectors within this
        // module.
        F.SelectorRemap.insertOrReplace(
          std::make_pair(LocalBaseSelectorID,
                         F.BaseSelectorID - LocalBaseSelectorID));

        SelectorsLoaded.resize(SelectorsLoaded.size() + F.LocalNumSelectors);
      }
      break;
    }

    case METHOD_POOL:
      F.SelectorLookupTableData = (const unsigned char *)Blob.data();
      if (Record[0])
        F.SelectorLookupTable
          = ASTSelectorLookupTable::Create(
                        F.SelectorLookupTableData + Record[0],
                        F.SelectorLookupTableData,
                        ASTSelectorLookupTrait(*this, F));
      TotalNumMethodPoolEntries += Record[1];
      break;

    case REFERENCED_SELECTOR_POOL:
      if (!Record.empty()) {
        for (unsigned Idx = 0, N = Record.size() - 1; Idx < N; /* in loop */) {
          ReferencedSelectorsData.push_back(getGlobalSelectorID(F,
                                                                Record[Idx++]));
          ReferencedSelectorsData.push_back(ReadSourceLocation(F, Record, Idx).
                                              getRawEncoding());
        }
      }
      break;

    case PP_CONDITIONAL_STACK:
      if (!Record.empty()) {
        unsigned Idx = 0, End = Record.size() - 1;
        bool ReachedEOFWhileSkipping = Record[Idx++];
        llvm::Optional<Preprocessor::PreambleSkipInfo> SkipInfo;
        if (ReachedEOFWhileSkipping) {
          SourceLocation HashToken = ReadSourceLocation(F, Record, Idx);
          SourceLocation IfTokenLoc = ReadSourceLocation(F, Record, Idx);
          bool FoundNonSkipPortion = Record[Idx++];
          bool FoundElse = Record[Idx++];
          SourceLocation ElseLoc = ReadSourceLocation(F, Record, Idx);
          SkipInfo.emplace(HashToken, IfTokenLoc, FoundNonSkipPortion,
                           FoundElse, ElseLoc);
        }
        SmallVector<PPConditionalInfo, 4> ConditionalStack;
        while (Idx < End) {
          auto Loc = ReadSourceLocation(F, Record, Idx);
          bool WasSkipping = Record[Idx++];
          bool FoundNonSkip = Record[Idx++];
          bool FoundElse = Record[Idx++];
          ConditionalStack.push_back(
              {Loc, WasSkipping, FoundNonSkip, FoundElse});
        }
        PP.setReplayablePreambleConditionalStack(ConditionalStack, SkipInfo);
      }
      break;

    case PP_COUNTER_VALUE:
      if (!Record.empty() && Listener)
        Listener->ReadCounter(F, Record[0]);
      break;

    case FILE_SORTED_DECLS:
      F.FileSortedDecls = (const DeclID *)Blob.data();
      F.NumFileSortedDecls = Record[0];
      break;

    case SOURCE_LOCATION_OFFSETS: {
      F.SLocEntryOffsets = (const uint32_t *)Blob.data();
      F.LocalNumSLocEntries = Record[0];
      unsigned SLocSpaceSize = Record[1];
      std::tie(F.SLocEntryBaseID, F.SLocEntryBaseOffset) =
          SourceMgr.AllocateLoadedSLocEntries(F.LocalNumSLocEntries,
                                              SLocSpaceSize);
      if (!F.SLocEntryBaseID) {
        Error("ran out of source locations");
        break;
      }
      // Make our entry in the range map. BaseID is negative and growing, so
      // we invert it. Because we invert it, though, we need the other end of
      // the range.
      unsigned RangeStart =
          unsigned(-F.SLocEntryBaseID) - F.LocalNumSLocEntries + 1;
      GlobalSLocEntryMap.insert(std::make_pair(RangeStart, &F));
      F.FirstLoc = SourceLocation::getFromRawEncoding(F.SLocEntryBaseOffset);

      // SLocEntryBaseOffset is lower than MaxLoadedOffset and decreasing.
      assert((F.SLocEntryBaseOffset & (1U << 31U)) == 0);
      GlobalSLocOffsetMap.insert(
          std::make_pair(SourceManager::MaxLoadedOffset - F.SLocEntryBaseOffset
                           - SLocSpaceSize,&F));

      // Initialize the remapping table.
      // Invalid stays invalid.
      F.SLocRemap.insertOrReplace(std::make_pair(0U, 0));
      // This module. Base was 2 when being compiled.
      F.SLocRemap.insertOrReplace(std::make_pair(2U,
                                  static_cast<int>(F.SLocEntryBaseOffset - 2)));

      TotalNumSLocEntries += F.LocalNumSLocEntries;
      break;
    }

    case MODULE_OFFSET_MAP:
      F.ModuleOffsetMap = Blob;
      break;

    case SOURCE_MANAGER_LINE_TABLE:
      if (ParseLineTable(F, Record))
        return Failure;
      break;

    case SOURCE_LOCATION_PRELOADS: {
      // Need to transform from the local view (1-based IDs) to the global view,
      // which is based off F.SLocEntryBaseID.
      if (!F.PreloadSLocEntries.empty()) {
        Error("Multiple SOURCE_LOCATION_PRELOADS records in AST file");
        return Failure;
      }

      F.PreloadSLocEntries.swap(Record);
      break;
    }

    case EXT_VECTOR_DECLS:
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        ExtVectorDecls.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case VTABLE_USES:
      if (Record.size() % 3 != 0) {
        Error("Invalid VTABLE_USES record");
        return Failure;
      }

      // Later tables overwrite earlier ones.
      // FIXME: Modules will have some trouble with this. This is clearly not
      // the right way to do this.
      VTableUses.clear();

      for (unsigned Idx = 0, N = Record.size(); Idx != N; /* In loop */) {
        VTableUses.push_back(getGlobalDeclID(F, Record[Idx++]));
        VTableUses.push_back(
          ReadSourceLocation(F, Record, Idx).getRawEncoding());
        VTableUses.push_back(Record[Idx++]);
      }
      break;

    case PENDING_IMPLICIT_INSTANTIATIONS:
      if (PendingInstantiations.size() % 2 != 0) {
        Error("Invalid existing PendingInstantiations");
        return Failure;
      }

      if (Record.size() % 2 != 0) {
        Error("Invalid PENDING_IMPLICIT_INSTANTIATIONS block");
        return Failure;
      }

      for (unsigned I = 0, N = Record.size(); I != N; /* in loop */) {
        PendingInstantiations.push_back(getGlobalDeclID(F, Record[I++]));
        PendingInstantiations.push_back(
          ReadSourceLocation(F, Record, I).getRawEncoding());
      }
      break;

    case SEMA_DECL_REFS:
      if (Record.size() != 3) {
        Error("Invalid SEMA_DECL_REFS block");
        return Failure;
      }
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        SemaDeclRefs.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case PPD_ENTITIES_OFFSETS: {
      F.PreprocessedEntityOffsets = (const PPEntityOffset *)Blob.data();
      assert(Blob.size() % sizeof(PPEntityOffset) == 0);
      F.NumPreprocessedEntities = Blob.size() / sizeof(PPEntityOffset);

      unsigned LocalBasePreprocessedEntityID = Record[0];

      unsigned StartingID;
      if (!PP.getPreprocessingRecord())
        PP.createPreprocessingRecord();
      if (!PP.getPreprocessingRecord()->getExternalSource())
        PP.getPreprocessingRecord()->SetExternalSource(*this);
      StartingID
        = PP.getPreprocessingRecord()
            ->allocateLoadedEntities(F.NumPreprocessedEntities);
      F.BasePreprocessedEntityID = StartingID;

      if (F.NumPreprocessedEntities > 0) {
        // Introduce the global -> local mapping for preprocessed entities in
        // this module.
        GlobalPreprocessedEntityMap.insert(std::make_pair(StartingID, &F));

        // Introduce the local -> global mapping for preprocessed entities in
        // this module.
        F.PreprocessedEntityRemap.insertOrReplace(
          std::make_pair(LocalBasePreprocessedEntityID,
            F.BasePreprocessedEntityID - LocalBasePreprocessedEntityID));
      }

      break;
    }

    case PPD_SKIPPED_RANGES: {
      F.PreprocessedSkippedRangeOffsets = (const PPSkippedRange*)Blob.data();
      assert(Blob.size() % sizeof(PPSkippedRange) == 0);
      F.NumPreprocessedSkippedRanges = Blob.size() / sizeof(PPSkippedRange);

      if (!PP.getPreprocessingRecord())
        PP.createPreprocessingRecord();
      if (!PP.getPreprocessingRecord()->getExternalSource())
        PP.getPreprocessingRecord()->SetExternalSource(*this);
      F.BasePreprocessedSkippedRangeID = PP.getPreprocessingRecord()
          ->allocateSkippedRanges(F.NumPreprocessedSkippedRanges);

      if (F.NumPreprocessedSkippedRanges > 0)
        GlobalSkippedRangeMap.insert(
            std::make_pair(F.BasePreprocessedSkippedRangeID, &F));
      break;
    }

    case DECL_UPDATE_OFFSETS:
      if (Record.size() % 2 != 0) {
        Error("invalid DECL_UPDATE_OFFSETS block in AST file");
        return Failure;
      }
      for (unsigned I = 0, N = Record.size(); I != N; I += 2) {
        GlobalDeclID ID = getGlobalDeclID(F, Record[I]);
        DeclUpdateOffsets[ID].push_back(std::make_pair(&F, Record[I + 1]));

        // If we've already loaded the decl, perform the updates when we finish
        // loading this block.
        if (Decl *D = GetExistingDecl(ID))
          PendingUpdateRecords.push_back(
              PendingUpdateRecord(ID, D, /*JustLoaded=*/false));
      }
      break;

    case OBJC_CATEGORIES_MAP:
      if (F.LocalNumObjCCategoriesInMap != 0) {
        Error("duplicate OBJC_CATEGORIES_MAP record in AST file");
        return Failure;
      }

      F.LocalNumObjCCategoriesInMap = Record[0];
      F.ObjCCategoriesMap = (const ObjCCategoriesInfo *)Blob.data();
      break;

    case OBJC_CATEGORIES:
      F.ObjCCategories.swap(Record);
      break;

    case CUDA_SPECIAL_DECL_REFS:
      // Later tables overwrite earlier ones.
      // FIXME: Modules will have trouble with this.
      CUDASpecialDeclRefs.clear();
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        CUDASpecialDeclRefs.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case HEADER_SEARCH_TABLE:
      F.HeaderFileInfoTableData = Blob.data();
      F.LocalNumHeaderFileInfos = Record[1];
      if (Record[0]) {
        F.HeaderFileInfoTable
          = HeaderFileInfoLookupTable::Create(
                   (const unsigned char *)F.HeaderFileInfoTableData + Record[0],
                   (const unsigned char *)F.HeaderFileInfoTableData,
                   HeaderFileInfoTrait(*this, F,
                                       &PP.getHeaderSearchInfo(),
                                       Blob.data() + Record[2]));

        PP.getHeaderSearchInfo().SetExternalSource(this);
        if (!PP.getHeaderSearchInfo().getExternalLookup())
          PP.getHeaderSearchInfo().SetExternalLookup(this);
      }
      break;

    case FP_PRAGMA_OPTIONS:
      // Later tables overwrite earlier ones.
      FPPragmaOptions.swap(Record);
      break;

    case OPENCL_EXTENSIONS:
      for (unsigned I = 0, E = Record.size(); I != E; ) {
        auto Name = ReadString(Record, I);
        auto &Opt = OpenCLExtensions.OptMap[Name];
        Opt.Supported = Record[I++] != 0;
        Opt.Enabled = Record[I++] != 0;
        Opt.Avail = Record[I++];
        Opt.Core = Record[I++];
      }
      break;

    case OPENCL_EXTENSION_TYPES:
      for (unsigned I = 0, E = Record.size(); I != E;) {
        auto TypeID = static_cast<::TypeID>(Record[I++]);
        auto *Type = GetType(TypeID).getTypePtr();
        auto NumExt = static_cast<unsigned>(Record[I++]);
        for (unsigned II = 0; II != NumExt; ++II) {
          auto Ext = ReadString(Record, I);
          OpenCLTypeExtMap[Type].insert(Ext);
        }
      }
      break;

    case OPENCL_EXTENSION_DECLS:
      for (unsigned I = 0, E = Record.size(); I != E;) {
        auto DeclID = static_cast<::DeclID>(Record[I++]);
        auto *Decl = GetDecl(DeclID);
        auto NumExt = static_cast<unsigned>(Record[I++]);
        for (unsigned II = 0; II != NumExt; ++II) {
          auto Ext = ReadString(Record, I);
          OpenCLDeclExtMap[Decl].insert(Ext);
        }
      }
      break;

    case TENTATIVE_DEFINITIONS:
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        TentativeDefinitions.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case KNOWN_NAMESPACES:
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        KnownNamespaces.push_back(getGlobalDeclID(F, Record[I]));
      break;

    case UNDEFINED_BUT_USED:
      if (UndefinedButUsed.size() % 2 != 0) {
        Error("Invalid existing UndefinedButUsed");
        return Failure;
      }

      if (Record.size() % 2 != 0) {
        Error("invalid undefined-but-used record");
        return Failure;
      }
      for (unsigned I = 0, N = Record.size(); I != N; /* in loop */) {
        UndefinedButUsed.push_back(getGlobalDeclID(F, Record[I++]));
        UndefinedButUsed.push_back(
            ReadSourceLocation(F, Record, I).getRawEncoding());
      }
      break;

    case DELETE_EXPRS_TO_ANALYZE:
      for (unsigned I = 0, N = Record.size(); I != N;) {
        DelayedDeleteExprs.push_back(getGlobalDeclID(F, Record[I++]));
        const uint64_t Count = Record[I++];
        DelayedDeleteExprs.push_back(Count);
        for (uint64_t C = 0; C < Count; ++C) {
          DelayedDeleteExprs.push_back(ReadSourceLocation(F, Record, I).getRawEncoding());
          bool IsArrayForm = Record[I++] == 1;
          DelayedDeleteExprs.push_back(IsArrayForm);
        }
      }
      break;

    case IMPORTED_MODULES:
      if (!F.isModule()) {
        // If we aren't loading a module (which has its own exports), make
        // all of the imported modules visible.
        // FIXME: Deal with macros-only imports.
        for (unsigned I = 0, N = Record.size(); I != N; /**/) {
          unsigned GlobalID = getGlobalSubmoduleID(F, Record[I++]);
          SourceLocation Loc = ReadSourceLocation(F, Record, I);
          if (GlobalID) {
            ImportedModules.push_back(ImportedSubmodule(GlobalID, Loc));
            if (DeserializationListener)
              DeserializationListener->ModuleImportRead(GlobalID, Loc);
          }
        }
      }
      break;

    case MACRO_OFFSET: {
      if (F.LocalNumMacros != 0) {
        Error("duplicate MACRO_OFFSET record in AST file");
        return Failure;
      }
      F.MacroOffsets = (const uint32_t *)Blob.data();
      F.LocalNumMacros = Record[0];
      unsigned LocalBaseMacroID = Record[1];
      F.BaseMacroID = getTotalNumMacros();

      if (F.LocalNumMacros > 0) {
        // Introduce the global -> local mapping for macros within this module.
        GlobalMacroMap.insert(std::make_pair(getTotalNumMacros() + 1, &F));

        // Introduce the local -> global mapping for macros within this module.
        F.MacroRemap.insertOrReplace(
          std::make_pair(LocalBaseMacroID,
                         F.BaseMacroID - LocalBaseMacroID));

        MacrosLoaded.resize(MacrosLoaded.size() + F.LocalNumMacros);
      }
      break;
    }

    case LATE_PARSED_TEMPLATE:
      LateParsedTemplates.append(Record.begin(), Record.end());
      break;

    case OPTIMIZE_PRAGMA_OPTIONS:
      if (Record.size() != 1) {
        Error("invalid pragma optimize record");
        return Failure;
      }
      OptimizeOffPragmaLocation = ReadSourceLocation(F, Record[0]);
      break;

    case MSSTRUCT_PRAGMA_OPTIONS:
      if (Record.size() != 1) {
        Error("invalid pragma ms_struct record");
        return Failure;
      }
      PragmaMSStructState = Record[0];
      break;

    case POINTERS_TO_MEMBERS_PRAGMA_OPTIONS:
      if (Record.size() != 2) {
        Error("invalid pragma ms_struct record");
        return Failure;
      }
      PragmaMSPointersToMembersState = Record[0];
      PointersToMembersPragmaLocation = ReadSourceLocation(F, Record[1]);
      break;

    case UNUSED_LOCAL_TYPEDEF_NAME_CANDIDATES:
      for (unsigned I = 0, N = Record.size(); I != N; ++I)
        UnusedLocalTypedefNameCandidates.push_back(
            getGlobalDeclID(F, Record[I]));
      break;

    case CUDA_PRAGMA_FORCE_HOST_DEVICE_DEPTH:
      if (Record.size() != 1) {
        Error("invalid cuda pragma options record");
        return Failure;
      }
      ForceCUDAHostDeviceDepth = Record[0];
      break;

    case PACK_PRAGMA_OPTIONS: {
      if (Record.size() < 3) {
        Error("invalid pragma pack record");
        return Failure;
      }
      PragmaPackCurrentValue = Record[0];
      PragmaPackCurrentLocation = ReadSourceLocation(F, Record[1]);
      unsigned NumStackEntries = Record[2];
      unsigned Idx = 3;
      // Reset the stack when importing a new module.
      PragmaPackStack.clear();
      for (unsigned I = 0; I < NumStackEntries; ++I) {
        PragmaPackStackEntry Entry;
        Entry.Value = Record[Idx++];
        Entry.Location = ReadSourceLocation(F, Record[Idx++]);
        Entry.PushLocation = ReadSourceLocation(F, Record[Idx++]);
        PragmaPackStrings.push_back(ReadString(Record, Idx));
        Entry.SlotLabel = PragmaPackStrings.back();
        PragmaPackStack.push_back(Entry);
      }
      break;
    }
    }
  }
}

void ASTReader::ReadModuleOffsetMap(ModuleFile &F) const {
  assert(!F.ModuleOffsetMap.empty() && "no module offset map to read");

  // Additional remapping information.
  const unsigned char *Data = (const unsigned char*)F.ModuleOffsetMap.data();
  const unsigned char *DataEnd = Data + F.ModuleOffsetMap.size();
  F.ModuleOffsetMap = StringRef();

  // If we see this entry before SOURCE_LOCATION_OFFSETS, add placeholders.
  if (F.SLocRemap.find(0) == F.SLocRemap.end()) {
    F.SLocRemap.insert(std::make_pair(0U, 0));
    F.SLocRemap.insert(std::make_pair(2U, 1));
  }

  // Continuous range maps we may be updating in our module.
  using RemapBuilder = ContinuousRangeMap<uint32_t, int, 2>::Builder;
  RemapBuilder SLocRemap(F.SLocRemap);
  RemapBuilder IdentifierRemap(F.IdentifierRemap);
  RemapBuilder MacroRemap(F.MacroRemap);
  RemapBuilder PreprocessedEntityRemap(F.PreprocessedEntityRemap);
  RemapBuilder SubmoduleRemap(F.SubmoduleRemap);
  RemapBuilder SelectorRemap(F.SelectorRemap);
  RemapBuilder DeclRemap(F.DeclRemap);
  RemapBuilder TypeRemap(F.TypeRemap);

  while (Data < DataEnd) {
    // FIXME: Looking up dependency modules by filename is horrible. Let's
    // start fixing this with prebuilt and explicit modules and see how it
    // goes...
    using namespace llvm::support;
    ModuleKind Kind = static_cast<ModuleKind>(
      endian::readNext<uint8_t, little, unaligned>(Data));
    uint16_t Len = endian::readNext<uint16_t, little, unaligned>(Data);
    StringRef Name = StringRef((const char*)Data, Len);
    Data += Len;
    ModuleFile *OM = (Kind == MK_PrebuiltModule || Kind == MK_ExplicitModule
                      ? ModuleMgr.lookupByModuleName(Name)
                      : ModuleMgr.lookupByFileName(Name));
    if (!OM) {
      std::string Msg =
          "SourceLocation remap refers to unknown module, cannot find ";
      Msg.append(Name);
      Error(Msg);
      return;
    }

    uint32_t SLocOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);
    uint32_t IdentifierIDOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);
    uint32_t MacroIDOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);
    uint32_t PreprocessedEntityIDOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);
    uint32_t SubmoduleIDOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);
    uint32_t SelectorIDOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);
    uint32_t DeclIDOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);
    uint32_t TypeIndexOffset =
        endian::readNext<uint32_t, little, unaligned>(Data);

    uint32_t None = std::numeric_limits<uint32_t>::max();

    auto mapOffset = [&](uint32_t Offset, uint32_t BaseOffset,
                         RemapBuilder &Remap) {
      if (Offset != None)
        Remap.insert(std::make_pair(Offset,
                                    static_cast<int>(BaseOffset - Offset)));
    };
    mapOffset(SLocOffset, OM->SLocEntryBaseOffset, SLocRemap);
    mapOffset(IdentifierIDOffset, OM->BaseIdentifierID, IdentifierRemap);
    mapOffset(MacroIDOffset, OM->BaseMacroID, MacroRemap);
    mapOffset(PreprocessedEntityIDOffset, OM->BasePreprocessedEntityID,
              PreprocessedEntityRemap);
    mapOffset(SubmoduleIDOffset, OM->BaseSubmoduleID, SubmoduleRemap);
    mapOffset(SelectorIDOffset, OM->BaseSelectorID, SelectorRemap);
    mapOffset(DeclIDOffset, OM->BaseDeclID, DeclRemap);
    mapOffset(TypeIndexOffset, OM->BaseTypeIndex, TypeRemap);

    // Global -> local mappings.
    F.GlobalToLocalDeclIDs[OM] = DeclIDOffset;
  }
}

ASTReader::ASTReadResult
ASTReader::ReadModuleMapFileBlock(RecordData &Record, ModuleFile &F,
                                  const ModuleFile *ImportedBy,
                                  unsigned ClientLoadCapabilities) {
  unsigned Idx = 0;
  F.ModuleMapPath = ReadPath(F, Record, Idx);

  // Try to resolve ModuleName in the current header search context and
  // verify that it is found in the same module map file as we saved. If the
  // top-level AST file is a main file, skip this check because there is no
  // usable header search context.
  assert(!F.ModuleName.empty() &&
         "MODULE_NAME should come before MODULE_MAP_FILE");
  if (F.Kind == MK_ImplicitModule && ModuleMgr.begin()->Kind != MK_MainFile) {
    // An implicitly-loaded module file should have its module listed in some
    // module map file that we've already loaded.
    Module *M = PP.getHeaderSearchInfo().lookupModule(F.ModuleName);
    auto &Map = PP.getHeaderSearchInfo().getModuleMap();
    const FileEntry *ModMap = M ? Map.getModuleMapFileForUniquing(M) : nullptr;
    // Don't emit module relocation error if we have -fno-validate-pch
    if (!PP.getPreprocessorOpts().DisablePCHValidation && !ModMap) {
      assert(ImportedBy && "top-level import should be verified");
      if ((ClientLoadCapabilities & ARR_OutOfDate) == 0) {
        if (auto *ASTFE = M ? M->getASTFile() : nullptr) {
          // This module was defined by an imported (explicit) module.
          Diag(diag::err_module_file_conflict) << F.ModuleName << F.FileName
                                               << ASTFE->getName();
        } else {
          // This module was built with a different module map.
          Diag(diag::err_imported_module_not_found)
              << F.ModuleName << F.FileName << ImportedBy->FileName
              << F.ModuleMapPath;
          // In case it was imported by a PCH, there's a chance the user is
          // just missing to include the search path to the directory containing
          // the modulemap.
          if (ImportedBy->Kind == MK_PCH)
            Diag(diag::note_imported_by_pch_module_not_found)
                << llvm::sys::path::parent_path(F.ModuleMapPath);
        }
      }
      return OutOfDate;
    }

    assert(M->Name == F.ModuleName && "found module with different name");

    // Check the primary module map file.
    const FileEntry *StoredModMap = FileMgr.getFile(F.ModuleMapPath);
    if (StoredModMap == nullptr || StoredModMap != ModMap) {
      assert(ModMap && "found module is missing module map file");
      assert(ImportedBy && "top-level import should be verified");
      if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
        Diag(diag::err_imported_module_modmap_changed)
          << F.ModuleName << ImportedBy->FileName
          << ModMap->getName() << F.ModuleMapPath;
      return OutOfDate;
    }

    llvm::SmallPtrSet<const FileEntry *, 1> AdditionalStoredMaps;
    for (unsigned I = 0, N = Record[Idx++]; I < N; ++I) {
      // FIXME: we should use input files rather than storing names.
      std::string Filename = ReadPath(F, Record, Idx);
      const FileEntry *F =
          FileMgr.getFile(Filename, false, false);
      if (F == nullptr) {
        if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
          Error("could not find file '" + Filename +"' referenced by AST file");
        return OutOfDate;
      }
      AdditionalStoredMaps.insert(F);
    }

    // Check any additional module map files (e.g. module.private.modulemap)
    // that are not in the pcm.
    if (auto *AdditionalModuleMaps = Map.getAdditionalModuleMapFiles(M)) {
      for (const FileEntry *ModMap : *AdditionalModuleMaps) {
        // Remove files that match
        // Note: SmallPtrSet::erase is really remove
        if (!AdditionalStoredMaps.erase(ModMap)) {
          if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
            Diag(diag::err_module_different_modmap)
              << F.ModuleName << /*new*/0 << ModMap->getName();
          return OutOfDate;
        }
      }
    }

    // Check any additional module map files that are in the pcm, but not
    // found in header search. Cases that match are already removed.
    for (const FileEntry *ModMap : AdditionalStoredMaps) {
      if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
        Diag(diag::err_module_different_modmap)
          << F.ModuleName << /*not new*/1 << ModMap->getName();
      return OutOfDate;
    }
  }

  if (Listener)
    Listener->ReadModuleMapFile(F.ModuleMapPath);
  return Success;
}

/// Move the given method to the back of the global list of methods.
static void moveMethodToBackOfGlobalList(Sema &S, ObjCMethodDecl *Method) {
  // Find the entry for this selector in the method pool.
  Sema::GlobalMethodPool::iterator Known
    = S.MethodPool.find(Method->getSelector());
  if (Known == S.MethodPool.end())
    return;

  // Retrieve the appropriate method list.
  ObjCMethodList &Start = Method->isInstanceMethod()? Known->second.first
                                                    : Known->second.second;
  bool Found = false;
  for (ObjCMethodList *List = &Start; List; List = List->getNext()) {
    if (!Found) {
      if (List->getMethod() == Method) {
        Found = true;
      } else {
        // Keep searching.
        continue;
      }
    }

    if (List->getNext())
      List->setMethod(List->getNext()->getMethod());
    else
      List->setMethod(Method);
  }
}

void ASTReader::makeNamesVisible(const HiddenNames &Names, Module *Owner) {
  assert(Owner->NameVisibility != Module::Hidden && "nothing to make visible?");
  for (Decl *D : Names) {
    bool wasHidden = D->isHidden();
    D->setVisibleDespiteOwningModule();

    if (wasHidden && SemaObj) {
      if (ObjCMethodDecl *Method = dyn_cast<ObjCMethodDecl>(D)) {
        moveMethodToBackOfGlobalList(*SemaObj, Method);
      }
    }
  }
}

void ASTReader::makeModuleVisible(Module *Mod,
                                  Module::NameVisibilityKind NameVisibility,
                                  SourceLocation ImportLoc) {
  llvm::SmallPtrSet<Module *, 4> Visited;
  SmallVector<Module *, 4> Stack;
  Stack.push_back(Mod);
  while (!Stack.empty()) {
    Mod = Stack.pop_back_val();

    if (NameVisibility <= Mod->NameVisibility) {
      // This module already has this level of visibility (or greater), so
      // there is nothing more to do.
      continue;
    }

    if (!Mod->isAvailable()) {
      // Modules that aren't available cannot be made visible.
      continue;
    }

    // Update the module's name visibility.
    Mod->NameVisibility = NameVisibility;

    // If we've already deserialized any names from this module,
    // mark them as visible.
    HiddenNamesMapType::iterator Hidden = HiddenNamesMap.find(Mod);
    if (Hidden != HiddenNamesMap.end()) {
      auto HiddenNames = std::move(*Hidden);
      HiddenNamesMap.erase(Hidden);
      makeNamesVisible(HiddenNames.second, HiddenNames.first);
      assert(HiddenNamesMap.find(Mod) == HiddenNamesMap.end() &&
             "making names visible added hidden names");
    }

    // Push any exported modules onto the stack to be marked as visible.
    SmallVector<Module *, 16> Exports;
    Mod->getExportedModules(Exports);
    for (SmallVectorImpl<Module *>::iterator
           I = Exports.begin(), E = Exports.end(); I != E; ++I) {
      Module *Exported = *I;
      if (Visited.insert(Exported).second)
        Stack.push_back(Exported);
    }
  }
}

/// We've merged the definition \p MergedDef into the existing definition
/// \p Def. Ensure that \p Def is made visible whenever \p MergedDef is made
/// visible.
void ASTReader::mergeDefinitionVisibility(NamedDecl *Def,
                                          NamedDecl *MergedDef) {
  if (Def->isHidden()) {
    // If MergedDef is visible or becomes visible, make the definition visible.
    if (!MergedDef->isHidden())
      Def->setVisibleDespiteOwningModule();
    else {
      getContext().mergeDefinitionIntoModule(
          Def, MergedDef->getImportedOwningModule(),
          /*NotifyListeners*/ false);
      PendingMergedDefinitionsToDeduplicate.insert(Def);
    }
  }
}

bool ASTReader::loadGlobalIndex() {
  if (GlobalIndex)
    return false;

  if (TriedLoadingGlobalIndex || !UseGlobalIndex ||
      !PP.getLangOpts().Modules)
    return true;

  // Try to load the global index.
  TriedLoadingGlobalIndex = true;
  StringRef ModuleCachePath
    = getPreprocessor().getHeaderSearchInfo().getModuleCachePath();
  std::pair<GlobalModuleIndex *, GlobalModuleIndex::ErrorCode> Result
    = GlobalModuleIndex::readIndex(ModuleCachePath);
  if (!Result.first)
    return true;

  GlobalIndex.reset(Result.first);
  ModuleMgr.setGlobalIndex(GlobalIndex.get());
  return false;
}

bool ASTReader::isGlobalIndexUnavailable() const {
  return PP.getLangOpts().Modules && UseGlobalIndex &&
         !hasGlobalIndex() && TriedLoadingGlobalIndex;
}

static void updateModuleTimestamp(ModuleFile &MF) {
  // Overwrite the timestamp file contents so that file's mtime changes.
  std::string TimestampFilename = MF.getTimestampFilename();
  std::error_code EC;
  llvm::raw_fd_ostream OS(TimestampFilename, EC, llvm::sys::fs::F_Text);
  if (EC)
    return;
  OS << "Timestamp file\n";
  OS.close();
  OS.clear_error(); // Avoid triggering a fatal error.
}

/// Given a cursor at the start of an AST file, scan ahead and drop the
/// cursor into the start of the given block ID, returning false on success and
/// true on failure.
static bool SkipCursorToBlock(BitstreamCursor &Cursor, unsigned BlockID) {
  while (true) {
    llvm::BitstreamEntry Entry = Cursor.advance();
    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::EndBlock:
      return true;

    case llvm::BitstreamEntry::Record:
      // Ignore top-level records.
      Cursor.skipRecord(Entry.ID);
      break;

    case llvm::BitstreamEntry::SubBlock:
      if (Entry.ID == BlockID) {
        if (Cursor.EnterSubBlock(BlockID))
          return true;
        // Found it!
        return false;
      }

      if (Cursor.SkipBlock())
        return true;
    }
  }
}

ASTReader::ASTReadResult ASTReader::ReadAST(StringRef FileName,
                                            ModuleKind Type,
                                            SourceLocation ImportLoc,
                                            unsigned ClientLoadCapabilities,
                                            SmallVectorImpl<ImportedSubmodule> *Imported) {
  llvm::SaveAndRestore<SourceLocation>
    SetCurImportLocRAII(CurrentImportLoc, ImportLoc);

  // Defer any pending actions until we get to the end of reading the AST file.
  Deserializing AnASTFile(this);

  // Bump the generation number.
  unsigned PreviousGeneration = 0;
  if (ContextObj)
    PreviousGeneration = incrementGeneration(*ContextObj);

  unsigned NumModules = ModuleMgr.size();
  SmallVector<ImportedModule, 4> Loaded;
  switch (ASTReadResult ReadResult =
              ReadASTCore(FileName, Type, ImportLoc,
                          /*ImportedBy=*/nullptr, Loaded, 0, 0,
                          ASTFileSignature(), ClientLoadCapabilities)) {
  case Failure:
  case Missing:
  case OutOfDate:
  case VersionMismatch:
  case ConfigurationMismatch:
  case HadErrors: {
    llvm::SmallPtrSet<ModuleFile *, 4> LoadedSet;
    for (const ImportedModule &IM : Loaded)
      LoadedSet.insert(IM.Mod);

    ModuleMgr.removeModules(ModuleMgr.begin() + NumModules, LoadedSet,
                            PP.getLangOpts().Modules
                                ? &PP.getHeaderSearchInfo().getModuleMap()
                                : nullptr);

    // If we find that any modules are unusable, the global index is going
    // to be out-of-date. Just remove it.
    GlobalIndex.reset();
    ModuleMgr.setGlobalIndex(nullptr);
    return ReadResult;
  }
  case Success:
    break;
  }

  // Here comes stuff that we only do once the entire chain is loaded.

  // Load the AST blocks of all of the modules that we loaded.
  for (SmallVectorImpl<ImportedModule>::iterator M = Loaded.begin(),
                                              MEnd = Loaded.end();
       M != MEnd; ++M) {
    ModuleFile &F = *M->Mod;

    // Read the AST block.
    if (ASTReadResult Result = ReadASTBlock(F, ClientLoadCapabilities))
      return Result;

    // Read the extension blocks.
    while (!SkipCursorToBlock(F.Stream, EXTENSION_BLOCK_ID)) {
      if (ASTReadResult Result = ReadExtensionBlock(F))
        return Result;
    }

    // Once read, set the ModuleFile bit base offset and update the size in
    // bits of all files we've seen.
    F.GlobalBitOffset = TotalModulesSizeInBits;
    TotalModulesSizeInBits += F.SizeInBits;
    GlobalBitOffsetsMap.insert(std::make_pair(F.GlobalBitOffset, &F));

    // Preload SLocEntries.
    for (unsigned I = 0, N = F.PreloadSLocEntries.size(); I != N; ++I) {
      int Index = int(F.PreloadSLocEntries[I] - 1) + F.SLocEntryBaseID;
      // Load it through the SourceManager and don't call ReadSLocEntry()
      // directly because the entry may have already been loaded in which case
      // calling ReadSLocEntry() directly would trigger an assertion in
      // SourceManager.
      SourceMgr.getLoadedSLocEntryByID(Index);
    }

    // Map the original source file ID into the ID space of the current
    // compilation.
    if (F.OriginalSourceFileID.isValid()) {
      F.OriginalSourceFileID = FileID::get(
          F.SLocEntryBaseID + F.OriginalSourceFileID.getOpaqueValue() - 1);
    }

    // Preload all the pending interesting identifiers by marking them out of
    // date.
    for (auto Offset : F.PreloadIdentifierOffsets) {
      const unsigned char *Data = reinterpret_cast<const unsigned char *>(
          F.IdentifierTableData + Offset);

      ASTIdentifierLookupTrait Trait(*this, F);
      auto KeyDataLen = Trait.ReadKeyDataLength(Data);
      auto Key = Trait.ReadKey(Data, KeyDataLen.first);
      auto &II = PP.getIdentifierTable().getOwn(Key);
      II.setOutOfDate(true);

      // Mark this identifier as being from an AST file so that we can track
      // whether we need to serialize it.
      markIdentifierFromAST(*this, II);

      // Associate the ID with the identifier so that the writer can reuse it.
      auto ID = Trait.ReadIdentifierID(Data + KeyDataLen.first);
      SetIdentifierInfo(ID, &II);
    }
  }

  // Setup the import locations and notify the module manager that we've
  // committed to these module files.
  for (SmallVectorImpl<ImportedModule>::iterator M = Loaded.begin(),
                                              MEnd = Loaded.end();
       M != MEnd; ++M) {
    ModuleFile &F = *M->Mod;

    ModuleMgr.moduleFileAccepted(&F);

    // Set the import location.
    F.DirectImportLoc = ImportLoc;
    // FIXME: We assume that locations from PCH / preamble do not need
    // any translation.
    if (!M->ImportedBy)
      F.ImportLoc = M->ImportLoc;
    else
      F.ImportLoc = TranslateSourceLocation(*M->ImportedBy, M->ImportLoc);
  }

  if (!PP.getLangOpts().CPlusPlus ||
      (Type != MK_ImplicitModule && Type != MK_ExplicitModule &&
       Type != MK_PrebuiltModule)) {
    // Mark all of the identifiers in the identifier table as being out of date,
    // so that various accessors know to check the loaded modules when the
    // identifier is used.
    //
    // For C++ modules, we don't need information on many identifiers (just
    // those that provide macros or are poisoned), so we mark all of
    // the interesting ones via PreloadIdentifierOffsets.
    for (IdentifierTable::iterator Id = PP.getIdentifierTable().begin(),
                                IdEnd = PP.getIdentifierTable().end();
         Id != IdEnd; ++Id)
      Id->second->setOutOfDate(true);
  }
  // Mark selectors as out of date.
  for (auto Sel : SelectorGeneration)
    SelectorOutOfDate[Sel.first] = true;

  // Resolve any unresolved module exports.
  for (unsigned I = 0, N = UnresolvedModuleRefs.size(); I != N; ++I) {
    UnresolvedModuleRef &Unresolved = UnresolvedModuleRefs[I];
    SubmoduleID GlobalID = getGlobalSubmoduleID(*Unresolved.File,Unresolved.ID);
    Module *ResolvedMod = getSubmodule(GlobalID);

    switch (Unresolved.Kind) {
    case UnresolvedModuleRef::Conflict:
      if (ResolvedMod) {
        Module::Conflict Conflict;
        Conflict.Other = ResolvedMod;
        Conflict.Message = Unresolved.String.str();
        Unresolved.Mod->Conflicts.push_back(Conflict);
      }
      continue;

    case UnresolvedModuleRef::Import:
      if (ResolvedMod)
        Unresolved.Mod->Imports.insert(ResolvedMod);
      continue;

    case UnresolvedModuleRef::Export:
      if (ResolvedMod || Unresolved.IsWildcard)
        Unresolved.Mod->Exports.push_back(
          Module::ExportDecl(ResolvedMod, Unresolved.IsWildcard));
      continue;
    }
  }
  UnresolvedModuleRefs.clear();

  if (Imported)
    Imported->append(ImportedModules.begin(),
                     ImportedModules.end());

  // FIXME: How do we load the 'use'd modules? They may not be submodules.
  // Might be unnecessary as use declarations are only used to build the
  // module itself.

  if (ContextObj)
    InitializeContext();

  if (SemaObj)
    UpdateSema();

  if (DeserializationListener)
    DeserializationListener->ReaderInitialized(this);

  ModuleFile &PrimaryModule = ModuleMgr.getPrimaryModule();
  if (PrimaryModule.OriginalSourceFileID.isValid()) {
    // If this AST file is a precompiled preamble, then set the
    // preamble file ID of the source manager to the file source file
    // from which the preamble was built.
    if (Type == MK_Preamble) {
      SourceMgr.setPreambleFileID(PrimaryModule.OriginalSourceFileID);
    } else if (Type == MK_MainFile) {
      SourceMgr.setMainFileID(PrimaryModule.OriginalSourceFileID);
    }
  }

  // For any Objective-C class definitions we have already loaded, make sure
  // that we load any additional categories.
  if (ContextObj) {
    for (unsigned I = 0, N = ObjCClassesLoaded.size(); I != N; ++I) {
      loadObjCCategories(ObjCClassesLoaded[I]->getGlobalID(),
                         ObjCClassesLoaded[I],
                         PreviousGeneration);
    }
  }

  if (PP.getHeaderSearchInfo()
          .getHeaderSearchOpts()
          .ModulesValidateOncePerBuildSession) {
    // Now we are certain that the module and all modules it depends on are
    // up to date.  Create or update timestamp files for modules that are
    // located in the module cache (not for PCH files that could be anywhere
    // in the filesystem).
    for (unsigned I = 0, N = Loaded.size(); I != N; ++I) {
      ImportedModule &M = Loaded[I];
      if (M.Mod->Kind == MK_ImplicitModule) {
        updateModuleTimestamp(*M.Mod);
      }
    }
  }

  return Success;
}

static ASTFileSignature readASTFileSignature(StringRef PCH);

/// Whether \p Stream starts with the AST/PCH file magic number 'CPCH'.
static bool startsWithASTFileMagic(BitstreamCursor &Stream) {
  return Stream.canSkipToPos(4) &&
         Stream.Read(8) == 'C' &&
         Stream.Read(8) == 'P' &&
         Stream.Read(8) == 'C' &&
         Stream.Read(8) == 'H';
}

static unsigned moduleKindForDiagnostic(ModuleKind Kind) {
  switch (Kind) {
  case MK_PCH:
    return 0; // PCH
  case MK_ImplicitModule:
  case MK_ExplicitModule:
  case MK_PrebuiltModule:
    return 1; // module
  case MK_MainFile:
  case MK_Preamble:
    return 2; // main source file
  }
  llvm_unreachable("unknown module kind");
}

ASTReader::ASTReadResult
ASTReader::ReadASTCore(StringRef FileName,
                       ModuleKind Type,
                       SourceLocation ImportLoc,
                       ModuleFile *ImportedBy,
                       SmallVectorImpl<ImportedModule> &Loaded,
                       off_t ExpectedSize, time_t ExpectedModTime,
                       ASTFileSignature ExpectedSignature,
                       unsigned ClientLoadCapabilities) {
  ModuleFile *M;
  std::string ErrorStr;
  ModuleManager::AddModuleResult AddResult
    = ModuleMgr.addModule(FileName, Type, ImportLoc, ImportedBy,
                          getGeneration(), ExpectedSize, ExpectedModTime,
                          ExpectedSignature, readASTFileSignature,
                          M, ErrorStr);

  switch (AddResult) {
  case ModuleManager::AlreadyLoaded:
    return Success;

  case ModuleManager::NewlyLoaded:
    // Load module file below.
    break;

  case ModuleManager::Missing:
    // The module file was missing; if the client can handle that, return
    // it.
    if (ClientLoadCapabilities & ARR_Missing)
      return Missing;

    // Otherwise, return an error.
    Diag(diag::err_module_file_not_found) << moduleKindForDiagnostic(Type)
                                          << FileName << !ErrorStr.empty()
                                          << ErrorStr;
    return Failure;

  case ModuleManager::OutOfDate:
    // We couldn't load the module file because it is out-of-date. If the
    // client can handle out-of-date, return it.
    if (ClientLoadCapabilities & ARR_OutOfDate)
      return OutOfDate;

    // Otherwise, return an error.
    Diag(diag::err_module_file_out_of_date) << moduleKindForDiagnostic(Type)
                                            << FileName << !ErrorStr.empty()
                                            << ErrorStr;
    return Failure;
  }

  assert(M && "Missing module file");

  ModuleFile &F = *M;
  BitstreamCursor &Stream = F.Stream;
  Stream = BitstreamCursor(PCHContainerRdr.ExtractPCH(*F.Buffer));
  F.SizeInBits = F.Buffer->getBufferSize() * 8;

  // Sniff for the signature.
  if (!startsWithASTFileMagic(Stream)) {
    Diag(diag::err_module_file_invalid) << moduleKindForDiagnostic(Type)
                                        << FileName;
    return Failure;
  }

  // This is used for compatibility with older PCH formats.
  bool HaveReadControlBlock = false;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::Record:
    case llvm::BitstreamEntry::EndBlock:
      Error("invalid record at top-level of AST file");
      return Failure;

    case llvm::BitstreamEntry::SubBlock:
      break;
    }

    switch (Entry.ID) {
    case CONTROL_BLOCK_ID:
      HaveReadControlBlock = true;
      switch (ReadControlBlock(F, Loaded, ImportedBy, ClientLoadCapabilities)) {
      case Success:
        // Check that we didn't try to load a non-module AST file as a module.
        //
        // FIXME: Should we also perform the converse check? Loading a module as
        // a PCH file sort of works, but it's a bit wonky.
        if ((Type == MK_ImplicitModule || Type == MK_ExplicitModule ||
             Type == MK_PrebuiltModule) &&
            F.ModuleName.empty()) {
          auto Result = (Type == MK_ImplicitModule) ? OutOfDate : Failure;
          if (Result != OutOfDate ||
              (ClientLoadCapabilities & ARR_OutOfDate) == 0)
            Diag(diag::err_module_file_not_module) << FileName;
          return Result;
        }
        break;

      case Failure: return Failure;
      case Missing: return Missing;
      case OutOfDate: return OutOfDate;
      case VersionMismatch: return VersionMismatch;
      case ConfigurationMismatch: return ConfigurationMismatch;
      case HadErrors: return HadErrors;
      }
      break;

    case AST_BLOCK_ID:
      if (!HaveReadControlBlock) {
        if ((ClientLoadCapabilities & ARR_VersionMismatch) == 0)
          Diag(diag::err_pch_version_too_old);
        return VersionMismatch;
      }

      // Record that we've loaded this module.
      Loaded.push_back(ImportedModule(M, ImportedBy, ImportLoc));
      return Success;

    case UNHASHED_CONTROL_BLOCK_ID:
      // This block is handled using look-ahead during ReadControlBlock.  We
      // shouldn't get here!
      Error("malformed block record in AST file");
      return Failure;

    default:
      if (Stream.SkipBlock()) {
        Error("malformed block record in AST file");
        return Failure;
      }
      break;
    }
  }

  return Success;
}

ASTReader::ASTReadResult
ASTReader::readUnhashedControlBlock(ModuleFile &F, bool WasImportedBy,
                                    unsigned ClientLoadCapabilities) {
  const HeaderSearchOptions &HSOpts =
      PP.getHeaderSearchInfo().getHeaderSearchOpts();
  bool AllowCompatibleConfigurationMismatch =
      F.Kind == MK_ExplicitModule || F.Kind == MK_PrebuiltModule;

  ASTReadResult Result = readUnhashedControlBlockImpl(
      &F, F.Data, ClientLoadCapabilities, AllowCompatibleConfigurationMismatch,
      Listener.get(),
      WasImportedBy ? false : HSOpts.ModulesValidateDiagnosticOptions);

  // If F was directly imported by another module, it's implicitly validated by
  // the importing module.
  if (DisableValidation || WasImportedBy ||
      (AllowConfigurationMismatch && Result == ConfigurationMismatch))
    return Success;

  if (Result == Failure) {
    Error("malformed block record in AST file");
    return Failure;
  }

  if (Result == OutOfDate && F.Kind == MK_ImplicitModule) {
    // If this module has already been finalized in the PCMCache, we're stuck
    // with it; we can only load a single version of each module.
    //
    // This can happen when a module is imported in two contexts: in one, as a
    // user module; in another, as a system module (due to an import from
    // another module marked with the [system] flag).  It usually indicates a
    // bug in the module map: this module should also be marked with [system].
    //
    // If -Wno-system-headers (the default), and the first import is as a
    // system module, then validation will fail during the as-user import,
    // since -Werror flags won't have been validated.  However, it's reasonable
    // to treat this consistently as a system module.
    //
    // If -Wsystem-headers, the PCM on disk was built with
    // -Wno-system-headers, and the first import is as a user module, then
    // validation will fail during the as-system import since the PCM on disk
    // doesn't guarantee that -Werror was respected.  However, the -Werror
    // flags were checked during the initial as-user import.
    if (PCMCache.isBufferFinal(F.FileName)) {
      Diag(diag::warn_module_system_bit_conflict) << F.FileName;
      return Success;
    }
  }

  return Result;
}

ASTReader::ASTReadResult ASTReader::readUnhashedControlBlockImpl(
    ModuleFile *F, llvm::StringRef StreamData, unsigned ClientLoadCapabilities,
    bool AllowCompatibleConfigurationMismatch, ASTReaderListener *Listener,
    bool ValidateDiagnosticOptions) {
  // Initialize a stream.
  BitstreamCursor Stream(StreamData);

  // Sniff for the signature.
  if (!startsWithASTFileMagic(Stream))
    return Failure;

  // Scan for the UNHASHED_CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, UNHASHED_CONTROL_BLOCK_ID))
    return Failure;

  // Read all of the records in the options block.
  RecordData Record;
  ASTReadResult Result = Success;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
    case llvm::BitstreamEntry::SubBlock:
      return Failure;

    case llvm::BitstreamEntry::EndBlock:
      return Result;

    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read and process a record.
    Record.clear();
    switch (
        (UnhashedControlBlockRecordTypes)Stream.readRecord(Entry.ID, Record)) {
    case SIGNATURE:
      if (F)
        std::copy(Record.begin(), Record.end(), F->Signature.data());
      break;
    case DIAGNOSTIC_OPTIONS: {
      bool Complain = (ClientLoadCapabilities & ARR_OutOfDate) == 0;
      if (Listener && ValidateDiagnosticOptions &&
          !AllowCompatibleConfigurationMismatch &&
          ParseDiagnosticOptions(Record, Complain, *Listener))
        Result = OutOfDate; // Don't return early.  Read the signature.
      break;
    }
    case DIAG_PRAGMA_MAPPINGS:
      if (!F)
        break;
      if (F->PragmaDiagMappings.empty())
        F->PragmaDiagMappings.swap(Record);
      else
        F->PragmaDiagMappings.insert(F->PragmaDiagMappings.end(),
                                     Record.begin(), Record.end());
      break;
    }
  }
}

/// Parse a record and blob containing module file extension metadata.
static bool parseModuleFileExtensionMetadata(
              const SmallVectorImpl<uint64_t> &Record,
              StringRef Blob,
              ModuleFileExtensionMetadata &Metadata) {
  if (Record.size() < 4) return true;

  Metadata.MajorVersion = Record[0];
  Metadata.MinorVersion = Record[1];

  unsigned BlockNameLen = Record[2];
  unsigned UserInfoLen = Record[3];

  if (BlockNameLen + UserInfoLen > Blob.size()) return true;

  Metadata.BlockName = std::string(Blob.data(), Blob.data() + BlockNameLen);
  Metadata.UserInfo = std::string(Blob.data() + BlockNameLen,
                                  Blob.data() + BlockNameLen + UserInfoLen);
  return false;
}

ASTReader::ASTReadResult ASTReader::ReadExtensionBlock(ModuleFile &F) {
  BitstreamCursor &Stream = F.Stream;

  RecordData Record;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advance();
    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock:
      if (Stream.SkipBlock())
        return Failure;

      continue;

    case llvm::BitstreamEntry::EndBlock:
      return Success;

    case llvm::BitstreamEntry::Error:
      return HadErrors;

    case llvm::BitstreamEntry::Record:
      break;
    }

    Record.clear();
    StringRef Blob;
    unsigned RecCode = Stream.readRecord(Entry.ID, Record, &Blob);
    switch (RecCode) {
    case EXTENSION_METADATA: {
      ModuleFileExtensionMetadata Metadata;
      if (parseModuleFileExtensionMetadata(Record, Blob, Metadata))
        return Failure;

      // Find a module file extension with this block name.
      auto Known = ModuleFileExtensions.find(Metadata.BlockName);
      if (Known == ModuleFileExtensions.end()) break;

      // Form a reader.
      if (auto Reader = Known->second->createExtensionReader(Metadata, *this,
                                                             F, Stream)) {
        F.ExtensionReaders.push_back(std::move(Reader));
      }

      break;
    }
    }
  }

  return Success;
}

void ASTReader::InitializeContext() {
  assert(ContextObj && "no context to initialize");
  ASTContext &Context = *ContextObj;

  // If there's a listener, notify them that we "read" the translation unit.
  if (DeserializationListener)
    DeserializationListener->DeclRead(PREDEF_DECL_TRANSLATION_UNIT_ID,
                                      Context.getTranslationUnitDecl());

  // FIXME: Find a better way to deal with collisions between these
  // built-in types. Right now, we just ignore the problem.

  // Load the special types.
  if (SpecialTypes.size() >= NumSpecialTypeIDs) {
    if (unsigned String = SpecialTypes[SPECIAL_TYPE_CF_CONSTANT_STRING]) {
      if (!Context.CFConstantStringTypeDecl)
        Context.setCFConstantStringType(GetType(String));
    }

    if (unsigned File = SpecialTypes[SPECIAL_TYPE_FILE]) {
      QualType FileType = GetType(File);
      if (FileType.isNull()) {
        Error("FILE type is NULL");
        return;
      }

      if (!Context.FILEDecl) {
        if (const TypedefType *Typedef = FileType->getAs<TypedefType>())
          Context.setFILEDecl(Typedef->getDecl());
        else {
          const TagType *Tag = FileType->getAs<TagType>();
          if (!Tag) {
            Error("Invalid FILE type in AST file");
            return;
          }
          Context.setFILEDecl(Tag->getDecl());
        }
      }
    }

    if (unsigned Jmp_buf = SpecialTypes[SPECIAL_TYPE_JMP_BUF]) {
      QualType Jmp_bufType = GetType(Jmp_buf);
      if (Jmp_bufType.isNull()) {
        Error("jmp_buf type is NULL");
        return;
      }

      if (!Context.jmp_bufDecl) {
        if (const TypedefType *Typedef = Jmp_bufType->getAs<TypedefType>())
          Context.setjmp_bufDecl(Typedef->getDecl());
        else {
          const TagType *Tag = Jmp_bufType->getAs<TagType>();
          if (!Tag) {
            Error("Invalid jmp_buf type in AST file");
            return;
          }
          Context.setjmp_bufDecl(Tag->getDecl());
        }
      }
    }

    if (unsigned Sigjmp_buf = SpecialTypes[SPECIAL_TYPE_SIGJMP_BUF]) {
      QualType Sigjmp_bufType = GetType(Sigjmp_buf);
      if (Sigjmp_bufType.isNull()) {
        Error("sigjmp_buf type is NULL");
        return;
      }

      if (!Context.sigjmp_bufDecl) {
        if (const TypedefType *Typedef = Sigjmp_bufType->getAs<TypedefType>())
          Context.setsigjmp_bufDecl(Typedef->getDecl());
        else {
          const TagType *Tag = Sigjmp_bufType->getAs<TagType>();
          assert(Tag && "Invalid sigjmp_buf type in AST file");
          Context.setsigjmp_bufDecl(Tag->getDecl());
        }
      }
    }

    if (unsigned ObjCIdRedef
          = SpecialTypes[SPECIAL_TYPE_OBJC_ID_REDEFINITION]) {
      if (Context.ObjCIdRedefinitionType.isNull())
        Context.ObjCIdRedefinitionType = GetType(ObjCIdRedef);
    }

    if (unsigned ObjCClassRedef
          = SpecialTypes[SPECIAL_TYPE_OBJC_CLASS_REDEFINITION]) {
      if (Context.ObjCClassRedefinitionType.isNull())
        Context.ObjCClassRedefinitionType = GetType(ObjCClassRedef);
    }

    if (unsigned ObjCSelRedef
          = SpecialTypes[SPECIAL_TYPE_OBJC_SEL_REDEFINITION]) {
      if (Context.ObjCSelRedefinitionType.isNull())
        Context.ObjCSelRedefinitionType = GetType(ObjCSelRedef);
    }

    if (unsigned Ucontext_t = SpecialTypes[SPECIAL_TYPE_UCONTEXT_T]) {
      QualType Ucontext_tType = GetType(Ucontext_t);
      if (Ucontext_tType.isNull()) {
        Error("ucontext_t type is NULL");
        return;
      }

      if (!Context.ucontext_tDecl) {
        if (const TypedefType *Typedef = Ucontext_tType->getAs<TypedefType>())
          Context.setucontext_tDecl(Typedef->getDecl());
        else {
          const TagType *Tag = Ucontext_tType->getAs<TagType>();
          assert(Tag && "Invalid ucontext_t type in AST file");
          Context.setucontext_tDecl(Tag->getDecl());
        }
      }
    }
  }

  ReadPragmaDiagnosticMappings(Context.getDiagnostics());

  // If there were any CUDA special declarations, deserialize them.
  if (!CUDASpecialDeclRefs.empty()) {
    assert(CUDASpecialDeclRefs.size() == 1 && "More decl refs than expected!");
    Context.setcudaConfigureCallDecl(
                           cast<FunctionDecl>(GetDecl(CUDASpecialDeclRefs[0])));
  }

  // Re-export any modules that were imported by a non-module AST file.
  // FIXME: This does not make macro-only imports visible again.
  for (auto &Import : ImportedModules) {
    if (Module *Imported = getSubmodule(Import.ID)) {
      makeModuleVisible(Imported, Module::AllVisible,
                        /*ImportLoc=*/Import.ImportLoc);
      if (Import.ImportLoc.isValid())
        PP.makeModuleVisible(Imported, Import.ImportLoc);
      // FIXME: should we tell Sema to make the module visible too?
    }
  }
  ImportedModules.clear();
}

void ASTReader::finalizeForWriting() {
  // Nothing to do for now.
}

/// Reads and return the signature record from \p PCH's control block, or
/// else returns 0.
static ASTFileSignature readASTFileSignature(StringRef PCH) {
  BitstreamCursor Stream(PCH);
  if (!startsWithASTFileMagic(Stream))
    return ASTFileSignature();

  // Scan for the UNHASHED_CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, UNHASHED_CONTROL_BLOCK_ID))
    return ASTFileSignature();

  // Scan for SIGNATURE inside the diagnostic options block.
  ASTReader::RecordData Record;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advanceSkippingSubblocks();
    if (Entry.Kind != llvm::BitstreamEntry::Record)
      return ASTFileSignature();

    Record.clear();
    StringRef Blob;
    if (SIGNATURE == Stream.readRecord(Entry.ID, Record, &Blob))
      return {{{(uint32_t)Record[0], (uint32_t)Record[1], (uint32_t)Record[2],
                (uint32_t)Record[3], (uint32_t)Record[4]}}};
  }
}

/// Retrieve the name of the original source file name
/// directly from the AST file, without actually loading the AST
/// file.
std::string ASTReader::getOriginalSourceFile(
    const std::string &ASTFileName, FileManager &FileMgr,
    const PCHContainerReader &PCHContainerRdr, DiagnosticsEngine &Diags) {
  // Open the AST file.
  auto Buffer = FileMgr.getBufferForFile(ASTFileName);
  if (!Buffer) {
    Diags.Report(diag::err_fe_unable_to_read_pch_file)
        << ASTFileName << Buffer.getError().message();
    return std::string();
  }

  // Initialize the stream
  BitstreamCursor Stream(PCHContainerRdr.ExtractPCH(**Buffer));

  // Sniff for the signature.
  if (!startsWithASTFileMagic(Stream)) {
    Diags.Report(diag::err_fe_not_a_pch_file) << ASTFileName;
    return std::string();
  }

  // Scan for the CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, CONTROL_BLOCK_ID)) {
    Diags.Report(diag::err_fe_pch_malformed_block) << ASTFileName;
    return std::string();
  }

  // Scan for ORIGINAL_FILE inside the control block.
  RecordData Record;
  while (true) {
    llvm::BitstreamEntry Entry = Stream.advanceSkippingSubblocks();
    if (Entry.Kind == llvm::BitstreamEntry::EndBlock)
      return std::string();

    if (Entry.Kind != llvm::BitstreamEntry::Record) {
      Diags.Report(diag::err_fe_pch_malformed_block) << ASTFileName;
      return std::string();
    }

    Record.clear();
    StringRef Blob;
    if (Stream.readRecord(Entry.ID, Record, &Blob) == ORIGINAL_FILE)
      return Blob.str();
  }
}

namespace {

  class SimplePCHValidator : public ASTReaderListener {
    const LangOptions &ExistingLangOpts;
    const TargetOptions &ExistingTargetOpts;
    const PreprocessorOptions &ExistingPPOpts;
    std::string ExistingModuleCachePath;
    FileManager &FileMgr;

  public:
    SimplePCHValidator(const LangOptions &ExistingLangOpts,
                       const TargetOptions &ExistingTargetOpts,
                       const PreprocessorOptions &ExistingPPOpts,
                       StringRef ExistingModuleCachePath,
                       FileManager &FileMgr)
      : ExistingLangOpts(ExistingLangOpts),
        ExistingTargetOpts(ExistingTargetOpts),
        ExistingPPOpts(ExistingPPOpts),
        ExistingModuleCachePath(ExistingModuleCachePath),
        FileMgr(FileMgr) {}

    bool ReadLanguageOptions(const LangOptions &LangOpts, bool Complain,
                             bool AllowCompatibleDifferences) override {
      return checkLanguageOptions(ExistingLangOpts, LangOpts, nullptr,
                                  AllowCompatibleDifferences);
    }

    bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                           bool AllowCompatibleDifferences) override {
      return checkTargetOptions(ExistingTargetOpts, TargetOpts, nullptr,
                                AllowCompatibleDifferences);
    }

    bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                 StringRef SpecificModuleCachePath,
                                 bool Complain) override {
      return checkHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                      ExistingModuleCachePath,
                                      nullptr, ExistingLangOpts);
    }

    bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                 bool Complain,
                                 std::string &SuggestedPredefines) override {
      return checkPreprocessorOptions(ExistingPPOpts, PPOpts, nullptr, FileMgr,
                                      SuggestedPredefines, ExistingLangOpts);
    }
  };

} // namespace

bool ASTReader::readASTFileControlBlock(
    StringRef Filename, FileManager &FileMgr,
    const PCHContainerReader &PCHContainerRdr,
    bool FindModuleFileExtensions,
    ASTReaderListener &Listener, bool ValidateDiagnosticOptions) {
  // Open the AST file.
  // FIXME: This allows use of the VFS; we do not allow use of the
  // VFS when actually loading a module.
  auto Buffer = FileMgr.getBufferForFile(Filename);
  if (!Buffer) {
    return true;
  }

  // Initialize the stream
  StringRef Bytes = PCHContainerRdr.ExtractPCH(**Buffer);
  BitstreamCursor Stream(Bytes);

  // Sniff for the signature.
  if (!startsWithASTFileMagic(Stream))
    return true;

  // Scan for the CONTROL_BLOCK_ID block.
  if (SkipCursorToBlock(Stream, CONTROL_BLOCK_ID))
    return true;

  bool NeedsInputFiles = Listener.needsInputFileVisitation();
  bool NeedsSystemInputFiles = Listener.needsSystemInputFileVisitation();
  bool NeedsImports = Listener.needsImportVisitation();
  BitstreamCursor InputFilesCursor;

  RecordData Record;
  std::string ModuleDir;
  bool DoneWithControlBlock = false;
  while (!DoneWithControlBlock) {
    llvm::BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: {
      switch (Entry.ID) {
      case OPTIONS_BLOCK_ID: {
        std::string IgnoredSuggestedPredefines;
        if (ReadOptionsBlock(Stream, ARR_ConfigurationMismatch | ARR_OutOfDate,
                             /*AllowCompatibleConfigurationMismatch*/ false,
                             Listener, IgnoredSuggestedPredefines) != Success)
          return true;
        break;
      }

      case INPUT_FILES_BLOCK_ID:
        InputFilesCursor = Stream;
        if (Stream.SkipBlock() ||
            (NeedsInputFiles &&
             ReadBlockAbbrevs(InputFilesCursor, INPUT_FILES_BLOCK_ID)))
          return true;
        break;

      default:
        if (Stream.SkipBlock())
          return true;
        break;
      }

      continue;
    }

    case llvm::BitstreamEntry::EndBlock:
      DoneWithControlBlock = true;
      break;

    case llvm::BitstreamEntry::Error:
      return true;

    case llvm::BitstreamEntry::Record:
      break;
    }

    if (DoneWithControlBlock) break;

    Record.clear();
    StringRef Blob;
    unsigned RecCode = Stream.readRecord(Entry.ID, Record, &Blob);
    switch ((ControlRecordTypes)RecCode) {
    case METADATA:
      if (Record[0] != VERSION_MAJOR)
        return true;
      if (Listener.ReadFullVersionInformation(Blob))
        return true;
      break;
    case MODULE_NAME:
      Listener.ReadModuleName(Blob);
      break;
    case MODULE_DIRECTORY:
      ModuleDir = Blob;
      break;
    case MODULE_MAP_FILE: {
      unsigned Idx = 0;
      auto Path = ReadString(Record, Idx);
      ResolveImportedPath(Path, ModuleDir);
      Listener.ReadModuleMapFile(Path);
      break;
    }
    case INPUT_FILE_OFFSETS: {
      if (!NeedsInputFiles)
        break;

      unsigned NumInputFiles = Record[0];
      unsigned NumUserFiles = Record[1];
      const llvm::support::unaligned_uint64_t *InputFileOffs =
          (const llvm::support::unaligned_uint64_t *)Blob.data();
      for (unsigned I = 0; I != NumInputFiles; ++I) {
        // Go find this input file.
        bool isSystemFile = I >= NumUserFiles;

        if (isSystemFile && !NeedsSystemInputFiles)
          break; // the rest are system input files

        BitstreamCursor &Cursor = InputFilesCursor;
        SavedStreamPosition SavedPosition(Cursor);
        Cursor.JumpToBit(InputFileOffs[I]);

        unsigned Code = Cursor.ReadCode();
        RecordData Record;
        StringRef Blob;
        bool shouldContinue = false;
        switch ((InputFileRecordTypes)Cursor.readRecord(Code, Record, &Blob)) {
        case INPUT_FILE:
          bool Overridden = static_cast<bool>(Record[3]);
          std::string Filename = Blob;
          ResolveImportedPath(Filename, ModuleDir);
          shouldContinue = Listener.visitInputFile(
              Filename, isSystemFile, Overridden, /*IsExplicitModule*/false);
          break;
        }
        if (!shouldContinue)
          break;
      }
      break;
    }

    case IMPORTS: {
      if (!NeedsImports)
        break;

      unsigned Idx = 0, N = Record.size();
      while (Idx < N) {
        // Read information about the AST file.
        Idx += 1+1+1+1+5; // Kind, ImportLoc, Size, ModTime, Signature
        std::string ModuleName = ReadString(Record, Idx);
        std::string Filename = ReadString(Record, Idx);
        ResolveImportedPath(Filename, ModuleDir);
        Listener.visitImport(ModuleName, Filename);
      }
      break;
    }

    default:
      // No other validation to perform.
      break;
    }
  }

  // Look for module file extension blocks, if requested.
  if (FindModuleFileExtensions) {
    BitstreamCursor SavedStream = Stream;
    while (!SkipCursorToBlock(Stream, EXTENSION_BLOCK_ID)) {
      bool DoneWithExtensionBlock = false;
      while (!DoneWithExtensionBlock) {
       llvm::BitstreamEntry Entry = Stream.advance();

       switch (Entry.Kind) {
       case llvm::BitstreamEntry::SubBlock:
         if (Stream.SkipBlock())
           return true;

         continue;

       case llvm::BitstreamEntry::EndBlock:
         DoneWithExtensionBlock = true;
         continue;

       case llvm::BitstreamEntry::Error:
         return true;

       case llvm::BitstreamEntry::Record:
         break;
       }

       Record.clear();
       StringRef Blob;
       unsigned RecCode = Stream.readRecord(Entry.ID, Record, &Blob);
       switch (RecCode) {
       case EXTENSION_METADATA: {
         ModuleFileExtensionMetadata Metadata;
         if (parseModuleFileExtensionMetadata(Record, Blob, Metadata))
           return true;

         Listener.readModuleFileExtension(Metadata);
         break;
       }
       }
      }
    }
    Stream = SavedStream;
  }

  // Scan for the UNHASHED_CONTROL_BLOCK_ID block.
  if (readUnhashedControlBlockImpl(
          nullptr, Bytes, ARR_ConfigurationMismatch | ARR_OutOfDate,
          /*AllowCompatibleConfigurationMismatch*/ false, &Listener,
          ValidateDiagnosticOptions) != Success)
    return true;

  return false;
}

bool ASTReader::isAcceptableASTFile(StringRef Filename, FileManager &FileMgr,
                                    const PCHContainerReader &PCHContainerRdr,
                                    const LangOptions &LangOpts,
                                    const TargetOptions &TargetOpts,
                                    const PreprocessorOptions &PPOpts,
                                    StringRef ExistingModuleCachePath) {
  SimplePCHValidator validator(LangOpts, TargetOpts, PPOpts,
                               ExistingModuleCachePath, FileMgr);
  return !readASTFileControlBlock(Filename, FileMgr, PCHContainerRdr,
                                  /*FindModuleFileExtensions=*/false,
                                  validator,
                                  /*ValidateDiagnosticOptions=*/true);
}

ASTReader::ASTReadResult
ASTReader::ReadSubmoduleBlock(ModuleFile &F, unsigned ClientLoadCapabilities) {
  // Enter the submodule block.
  if (F.Stream.EnterSubBlock(SUBMODULE_BLOCK_ID)) {
    Error("malformed submodule block record in AST file");
    return Failure;
  }

  ModuleMap &ModMap = PP.getHeaderSearchInfo().getModuleMap();
  bool First = true;
  Module *CurrentModule = nullptr;
  RecordData Record;
  while (true) {
    llvm::BitstreamEntry Entry = F.Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return Failure;
    case llvm::BitstreamEntry::EndBlock:
      return Success;
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    StringRef Blob;
    Record.clear();
    auto Kind = F.Stream.readRecord(Entry.ID, Record, &Blob);

    if ((Kind == SUBMODULE_METADATA) != First) {
      Error("submodule metadata record should be at beginning of block");
      return Failure;
    }
    First = false;

    // Submodule information is only valid if we have a current module.
    // FIXME: Should we error on these cases?
    if (!CurrentModule && Kind != SUBMODULE_METADATA &&
        Kind != SUBMODULE_DEFINITION)
      continue;

    switch (Kind) {
    default:  // Default behavior: ignore.
      break;

    case SUBMODULE_DEFINITION: {
      if (Record.size() < 12) {
        Error("malformed module definition");
        return Failure;
      }

      StringRef Name = Blob;
      unsigned Idx = 0;
      SubmoduleID GlobalID = getGlobalSubmoduleID(F, Record[Idx++]);
      SubmoduleID Parent = getGlobalSubmoduleID(F, Record[Idx++]);
      Module::ModuleKind Kind = (Module::ModuleKind)Record[Idx++];
      bool IsFramework = Record[Idx++];
      bool IsExplicit = Record[Idx++];
      bool IsSystem = Record[Idx++];
      bool IsExternC = Record[Idx++];
      bool InferSubmodules = Record[Idx++];
      bool InferExplicitSubmodules = Record[Idx++];
      bool InferExportWildcard = Record[Idx++];
      bool ConfigMacrosExhaustive = Record[Idx++];
      bool ModuleMapIsPrivate = Record[Idx++];

      Module *ParentModule = nullptr;
      if (Parent)
        ParentModule = getSubmodule(Parent);

      // Retrieve this (sub)module from the module map, creating it if
      // necessary.
      CurrentModule =
          ModMap.findOrCreateModule(Name, ParentModule, IsFramework, IsExplicit)
              .first;

      // FIXME: set the definition loc for CurrentModule, or call
      // ModMap.setInferredModuleAllowedBy()

      SubmoduleID GlobalIndex = GlobalID - NUM_PREDEF_SUBMODULE_IDS;
      if (GlobalIndex >= SubmodulesLoaded.size() ||
          SubmodulesLoaded[GlobalIndex]) {
        Error("too many submodules");
        return Failure;
      }

      if (!ParentModule) {
        if (const FileEntry *CurFile = CurrentModule->getASTFile()) {
          // Don't emit module relocation error if we have -fno-validate-pch
          if (!PP.getPreprocessorOpts().DisablePCHValidation &&
              CurFile != F.File) {
            if (!Diags.isDiagnosticInFlight()) {
              Diag(diag::err_module_file_conflict)
                << CurrentModule->getTopLevelModuleName()
                << CurFile->getName()
                << F.File->getName();
            }
            return Failure;
          }
        }

        CurrentModule->setASTFile(F.File);
        CurrentModule->PresumedModuleMapFile = F.ModuleMapPath;
      }

      CurrentModule->Kind = Kind;
      CurrentModule->Signature = F.Signature;
      CurrentModule->IsFromModuleFile = true;
      CurrentModule->IsSystem = IsSystem || CurrentModule->IsSystem;
      CurrentModule->IsExternC = IsExternC;
      CurrentModule->InferSubmodules = InferSubmodules;
      CurrentModule->InferExplicitSubmodules = InferExplicitSubmodules;
      CurrentModule->InferExportWildcard = InferExportWildcard;
      CurrentModule->ConfigMacrosExhaustive = ConfigMacrosExhaustive;
      CurrentModule->ModuleMapIsPrivate = ModuleMapIsPrivate;
      if (DeserializationListener)
        DeserializationListener->ModuleRead(GlobalID, CurrentModule);

      SubmodulesLoaded[GlobalIndex] = CurrentModule;

      // Clear out data that will be replaced by what is in the module file.
      CurrentModule->LinkLibraries.clear();
      CurrentModule->ConfigMacros.clear();
      CurrentModule->UnresolvedConflicts.clear();
      CurrentModule->Conflicts.clear();

      // The module is available unless it's missing a requirement; relevant
      // requirements will be (re-)added by SUBMODULE_REQUIRES records.
      // Missing headers that were present when the module was built do not
      // make it unavailable -- if we got this far, this must be an explicitly
      // imported module file.
      CurrentModule->Requirements.clear();
      CurrentModule->MissingHeaders.clear();
      CurrentModule->IsMissingRequirement =
          ParentModule && ParentModule->IsMissingRequirement;
      CurrentModule->IsAvailable = !CurrentModule->IsMissingRequirement;
      break;
    }

    case SUBMODULE_UMBRELLA_HEADER: {
      std::string Filename = Blob;
      ResolveImportedPath(F, Filename);
      if (auto *Umbrella = PP.getFileManager().getFile(Filename)) {
        if (!CurrentModule->getUmbrellaHeader())
          ModMap.setUmbrellaHeader(CurrentModule, Umbrella, Blob);
        else if (CurrentModule->getUmbrellaHeader().Entry != Umbrella) {
          if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
            Error("mismatched umbrella headers in submodule");
          return OutOfDate;
        }
      }
      break;
    }

    case SUBMODULE_HEADER:
    case SUBMODULE_EXCLUDED_HEADER:
    case SUBMODULE_PRIVATE_HEADER:
      // We lazily associate headers with their modules via the HeaderInfo table.
      // FIXME: Re-evaluate this section; maybe only store InputFile IDs instead
      // of complete filenames or remove it entirely.
      break;

    case SUBMODULE_TEXTUAL_HEADER:
    case SUBMODULE_PRIVATE_TEXTUAL_HEADER:
      // FIXME: Textual headers are not marked in the HeaderInfo table. Load
      // them here.
      break;

    case SUBMODULE_TOPHEADER:
      CurrentModule->addTopHeaderFilename(Blob);
      break;

    case SUBMODULE_UMBRELLA_DIR: {
      std::string Dirname = Blob;
      ResolveImportedPath(F, Dirname);
      if (auto *Umbrella = PP.getFileManager().getDirectory(Dirname)) {
        if (!CurrentModule->getUmbrellaDir())
          ModMap.setUmbrellaDir(CurrentModule, Umbrella, Blob);
        else if (CurrentModule->getUmbrellaDir().Entry != Umbrella) {
          if ((ClientLoadCapabilities & ARR_OutOfDate) == 0)
            Error("mismatched umbrella directories in submodule");
          return OutOfDate;
        }
      }
      break;
    }

    case SUBMODULE_METADATA: {
      F.BaseSubmoduleID = getTotalNumSubmodules();
      F.LocalNumSubmodules = Record[0];
      unsigned LocalBaseSubmoduleID = Record[1];
      if (F.LocalNumSubmodules > 0) {
        // Introduce the global -> local mapping for submodules within this
        // module.
        GlobalSubmoduleMap.insert(std::make_pair(getTotalNumSubmodules()+1,&F));

        // Introduce the local -> global mapping for submodules within this
        // module.
        F.SubmoduleRemap.insertOrReplace(
          std::make_pair(LocalBaseSubmoduleID,
                         F.BaseSubmoduleID - LocalBaseSubmoduleID));

        SubmodulesLoaded.resize(SubmodulesLoaded.size() + F.LocalNumSubmodules);
      }
      break;
    }

    case SUBMODULE_IMPORTS:
      for (unsigned Idx = 0; Idx != Record.size(); ++Idx) {
        UnresolvedModuleRef Unresolved;
        Unresolved.File = &F;
        Unresolved.Mod = CurrentModule;
        Unresolved.ID = Record[Idx];
        Unresolved.Kind = UnresolvedModuleRef::Import;
        Unresolved.IsWildcard = false;
        UnresolvedModuleRefs.push_back(Unresolved);
      }
      break;

    case SUBMODULE_EXPORTS:
      for (unsigned Idx = 0; Idx + 1 < Record.size(); Idx += 2) {
        UnresolvedModuleRef Unresolved;
        Unresolved.File = &F;
        Unresolved.Mod = CurrentModule;
        Unresolved.ID = Record[Idx];
        Unresolved.Kind = UnresolvedModuleRef::Export;
        Unresolved.IsWildcard = Record[Idx + 1];
        UnresolvedModuleRefs.push_back(Unresolved);
      }

      // Once we've loaded the set of exports, there's no reason to keep
      // the parsed, unresolved exports around.
      CurrentModule->UnresolvedExports.clear();
      break;

    case SUBMODULE_REQUIRES:
      CurrentModule->addRequirement(Blob, Record[0], PP.getLangOpts(),
                                    PP.getTargetInfo());
      break;

    case SUBMODULE_LINK_LIBRARY:
      ModMap.resolveLinkAsDependencies(CurrentModule);
      CurrentModule->LinkLibraries.push_back(
                                         Module::LinkLibrary(Blob, Record[0]));
      break;

    case SUBMODULE_CONFIG_MACRO:
      CurrentModule->ConfigMacros.push_back(Blob.str());
      break;

    case SUBMODULE_CONFLICT: {
      UnresolvedModuleRef Unresolved;
      Unresolved.File = &F;
      Unresolved.Mod = CurrentModule;
      Unresolved.ID = Record[0];
      Unresolved.Kind = UnresolvedModuleRef::Conflict;
      Unresolved.IsWildcard = false;
      Unresolved.String = Blob;
      UnresolvedModuleRefs.push_back(Unresolved);
      break;
    }

    case SUBMODULE_INITIALIZERS: {
      if (!ContextObj)
        break;
      SmallVector<uint32_t, 16> Inits;
      for (auto &ID : Record)
        Inits.push_back(getGlobalDeclID(F, ID));
      ContextObj->addLazyModuleInitializers(CurrentModule, Inits);
      break;
    }

    case SUBMODULE_EXPORT_AS:
      CurrentModule->ExportAsModule = Blob.str();
      ModMap.addLinkAsDependency(CurrentModule);
      break;
    }
  }
}

/// Parse the record that corresponds to a LangOptions data
/// structure.
///
/// This routine parses the language options from the AST file and then gives
/// them to the AST listener if one is set.
///
/// \returns true if the listener deems the file unacceptable, false otherwise.
bool ASTReader::ParseLanguageOptions(const RecordData &Record,
                                     bool Complain,
                                     ASTReaderListener &Listener,
                                     bool AllowCompatibleDifferences) {
  LangOptions LangOpts;
  unsigned Idx = 0;
#define LANGOPT(Name, Bits, Default, Description) \
  LangOpts.Name = Record[Idx++];
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
  LangOpts.set##Name(static_cast<LangOptions::Type>(Record[Idx++]));
#include "clang/Basic/LangOptions.def"
#define SANITIZER(NAME, ID)                                                    \
  LangOpts.Sanitize.set(SanitizerKind::ID, Record[Idx++]);
#include "clang/Basic/Sanitizers.def"

  for (unsigned N = Record[Idx++]; N; --N)
    LangOpts.ModuleFeatures.push_back(ReadString(Record, Idx));

  ObjCRuntime::Kind runtimeKind = (ObjCRuntime::Kind) Record[Idx++];
  VersionTuple runtimeVersion = ReadVersionTuple(Record, Idx);
  LangOpts.ObjCRuntime = ObjCRuntime(runtimeKind, runtimeVersion);

  LangOpts.CurrentModule = ReadString(Record, Idx);

  // Comment options.
  for (unsigned N = Record[Idx++]; N; --N) {
    LangOpts.CommentOpts.BlockCommandNames.push_back(
      ReadString(Record, Idx));
  }
  LangOpts.CommentOpts.ParseAllComments = Record[Idx++];

  // OpenMP offloading options.
  for (unsigned N = Record[Idx++]; N; --N) {
    LangOpts.OMPTargetTriples.push_back(llvm::Triple(ReadString(Record, Idx)));
  }

  LangOpts.OMPHostIRFile = ReadString(Record, Idx);

  return Listener.ReadLanguageOptions(LangOpts, Complain,
                                      AllowCompatibleDifferences);
}

bool ASTReader::ParseTargetOptions(const RecordData &Record, bool Complain,
                                   ASTReaderListener &Listener,
                                   bool AllowCompatibleDifferences) {
  unsigned Idx = 0;
  TargetOptions TargetOpts;
  TargetOpts.Triple = ReadString(Record, Idx);
  TargetOpts.CPU = ReadString(Record, Idx);
  TargetOpts.ABI = ReadString(Record, Idx);
  for (unsigned N = Record[Idx++]; N; --N) {
    TargetOpts.FeaturesAsWritten.push_back(ReadString(Record, Idx));
  }
  for (unsigned N = Record[Idx++]; N; --N) {
    TargetOpts.Features.push_back(ReadString(Record, Idx));
  }

  return Listener.ReadTargetOptions(TargetOpts, Complain,
                                    AllowCompatibleDifferences);
}

bool ASTReader::ParseDiagnosticOptions(const RecordData &Record, bool Complain,
                                       ASTReaderListener &Listener) {
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts(new DiagnosticOptions);
  unsigned Idx = 0;
#define DIAGOPT(Name, Bits, Default) DiagOpts->Name = Record[Idx++];
#define ENUM_DIAGOPT(Name, Type, Bits, Default) \
  DiagOpts->set##Name(static_cast<Type>(Record[Idx++]));
#include "clang/Basic/DiagnosticOptions.def"

  for (unsigned N = Record[Idx++]; N; --N)
    DiagOpts->Warnings.push_back(ReadString(Record, Idx));
  for (unsigned N = Record[Idx++]; N; --N)
    DiagOpts->Remarks.push_back(ReadString(Record, Idx));

  return Listener.ReadDiagnosticOptions(DiagOpts, Complain);
}

bool ASTReader::ParseFileSystemOptions(const RecordData &Record, bool Complain,
                                       ASTReaderListener &Listener) {
  FileSystemOptions FSOpts;
  unsigned Idx = 0;
  FSOpts.WorkingDir = ReadString(Record, Idx);
  return Listener.ReadFileSystemOptions(FSOpts, Complain);
}

bool ASTReader::ParseHeaderSearchOptions(const RecordData &Record,
                                         bool Complain,
                                         ASTReaderListener &Listener) {
  HeaderSearchOptions HSOpts;
  unsigned Idx = 0;
  HSOpts.Sysroot = ReadString(Record, Idx);

  // Include entries.
  for (unsigned N = Record[Idx++]; N; --N) {
    std::string Path = ReadString(Record, Idx);
    frontend::IncludeDirGroup Group
      = static_cast<frontend::IncludeDirGroup>(Record[Idx++]);
    bool IsFramework = Record[Idx++];
    bool IgnoreSysRoot = Record[Idx++];
    HSOpts.UserEntries.emplace_back(std::move(Path), Group, IsFramework,
                                    IgnoreSysRoot);
  }

  // System header prefixes.
  for (unsigned N = Record[Idx++]; N; --N) {
    std::string Prefix = ReadString(Record, Idx);
    bool IsSystemHeader = Record[Idx++];
    HSOpts.SystemHeaderPrefixes.emplace_back(std::move(Prefix), IsSystemHeader);
  }

  HSOpts.ResourceDir = ReadString(Record, Idx);
  HSOpts.ModuleCachePath = ReadString(Record, Idx);
  HSOpts.ModuleUserBuildPath = ReadString(Record, Idx);
  HSOpts.DisableModuleHash = Record[Idx++];
  HSOpts.ImplicitModuleMaps = Record[Idx++];
  HSOpts.ModuleMapFileHomeIsCwd = Record[Idx++];
  HSOpts.UseBuiltinIncludes = Record[Idx++];
  HSOpts.UseStandardSystemIncludes = Record[Idx++];
  HSOpts.UseStandardCXXIncludes = Record[Idx++];
  HSOpts.UseLibcxx = Record[Idx++];
  std::string SpecificModuleCachePath = ReadString(Record, Idx);

  return Listener.ReadHeaderSearchOptions(HSOpts, SpecificModuleCachePath,
                                          Complain);
}

bool ASTReader::ParsePreprocessorOptions(const RecordData &Record,
                                         bool Complain,
                                         ASTReaderListener &Listener,
                                         std::string &SuggestedPredefines) {
  PreprocessorOptions PPOpts;
  unsigned Idx = 0;

  // Macro definitions/undefs
  for (unsigned N = Record[Idx++]; N; --N) {
    std::string Macro = ReadString(Record, Idx);
    bool IsUndef = Record[Idx++];
    PPOpts.Macros.push_back(std::make_pair(Macro, IsUndef));
  }

  // Includes
  for (unsigned N = Record[Idx++]; N; --N) {
    PPOpts.Includes.push_back(ReadString(Record, Idx));
  }

  // Macro Includes
  for (unsigned N = Record[Idx++]; N; --N) {
    PPOpts.MacroIncludes.push_back(ReadString(Record, Idx));
  }

  PPOpts.UsePredefines = Record[Idx++];
  PPOpts.DetailedRecord = Record[Idx++];
  PPOpts.ImplicitPCHInclude = ReadString(Record, Idx);
  PPOpts.ObjCXXARCStandardLibrary =
    static_cast<ObjCXXARCStandardLibraryKind>(Record[Idx++]);
  SuggestedPredefines.clear();
  return Listener.ReadPreprocessorOptions(PPOpts, Complain,
                                          SuggestedPredefines);
}

std::pair<ModuleFile *, unsigned>
ASTReader::getModulePreprocessedEntity(unsigned GlobalIndex) {
  GlobalPreprocessedEntityMapType::iterator
  I = GlobalPreprocessedEntityMap.find(GlobalIndex);
  assert(I != GlobalPreprocessedEntityMap.end() &&
         "Corrupted global preprocessed entity map");
  ModuleFile *M = I->second;
  unsigned LocalIndex = GlobalIndex - M->BasePreprocessedEntityID;
  return std::make_pair(M, LocalIndex);
}

llvm::iterator_range<PreprocessingRecord::iterator>
ASTReader::getModulePreprocessedEntities(ModuleFile &Mod) const {
  if (PreprocessingRecord *PPRec = PP.getPreprocessingRecord())
    return PPRec->getIteratorsForLoadedRange(Mod.BasePreprocessedEntityID,
                                             Mod.NumPreprocessedEntities);

  return llvm::make_range(PreprocessingRecord::iterator(),
                          PreprocessingRecord::iterator());
}

llvm::iterator_range<ASTReader::ModuleDeclIterator>
ASTReader::getModuleFileLevelDecls(ModuleFile &Mod) {
  return llvm::make_range(
      ModuleDeclIterator(this, &Mod, Mod.FileSortedDecls),
      ModuleDeclIterator(this, &Mod,
                         Mod.FileSortedDecls + Mod.NumFileSortedDecls));
}

SourceRange ASTReader::ReadSkippedRange(unsigned GlobalIndex) {
  auto I = GlobalSkippedRangeMap.find(GlobalIndex);
  assert(I != GlobalSkippedRangeMap.end() &&
    "Corrupted global skipped range map");
  ModuleFile *M = I->second;
  unsigned LocalIndex = GlobalIndex - M->BasePreprocessedSkippedRangeID;
  assert(LocalIndex < M->NumPreprocessedSkippedRanges);
  PPSkippedRange RawRange = M->PreprocessedSkippedRangeOffsets[LocalIndex];
  SourceRange Range(TranslateSourceLocation(*M, RawRange.getBegin()),
                    TranslateSourceLocation(*M, RawRange.getEnd()));
  assert(Range.isValid());
  return Range;
}

PreprocessedEntity *ASTReader::ReadPreprocessedEntity(unsigned Index) {
  PreprocessedEntityID PPID = Index+1;
  std::pair<ModuleFile *, unsigned> PPInfo = getModulePreprocessedEntity(Index);
  ModuleFile &M = *PPInfo.first;
  unsigned LocalIndex = PPInfo.second;
  const PPEntityOffset &PPOffs = M.PreprocessedEntityOffsets[LocalIndex];

  if (!PP.getPreprocessingRecord()) {
    Error("no preprocessing record");
    return nullptr;
  }

  SavedStreamPosition SavedPosition(M.PreprocessorDetailCursor);
  M.PreprocessorDetailCursor.JumpToBit(PPOffs.BitOffset);

  llvm::BitstreamEntry Entry =
    M.PreprocessorDetailCursor.advance(BitstreamCursor::AF_DontPopBlockAtEnd);
  if (Entry.Kind != llvm::BitstreamEntry::Record)
    return nullptr;

  // Read the record.
  SourceRange Range(TranslateSourceLocation(M, PPOffs.getBegin()),
                    TranslateSourceLocation(M, PPOffs.getEnd()));
  PreprocessingRecord &PPRec = *PP.getPreprocessingRecord();
  StringRef Blob;
  RecordData Record;
  PreprocessorDetailRecordTypes RecType =
    (PreprocessorDetailRecordTypes)M.PreprocessorDetailCursor.readRecord(
                                          Entry.ID, Record, &Blob);
  switch (RecType) {
  case PPD_MACRO_EXPANSION: {
    bool isBuiltin = Record[0];
    IdentifierInfo *Name = nullptr;
    MacroDefinitionRecord *Def = nullptr;
    if (isBuiltin)
      Name = getLocalIdentifier(M, Record[1]);
    else {
      PreprocessedEntityID GlobalID =
          getGlobalPreprocessedEntityID(M, Record[1]);
      Def = cast<MacroDefinitionRecord>(
          PPRec.getLoadedPreprocessedEntity(GlobalID - 1));
    }

    MacroExpansion *ME;
    if (isBuiltin)
      ME = new (PPRec) MacroExpansion(Name, Range);
    else
      ME = new (PPRec) MacroExpansion(Def, Range);

    return ME;
  }

  case PPD_MACRO_DEFINITION: {
    // Decode the identifier info and then check again; if the macro is
    // still defined and associated with the identifier,
    IdentifierInfo *II = getLocalIdentifier(M, Record[0]);
    MacroDefinitionRecord *MD = new (PPRec) MacroDefinitionRecord(II, Range);

    if (DeserializationListener)
      DeserializationListener->MacroDefinitionRead(PPID, MD);

    return MD;
  }

  case PPD_INCLUSION_DIRECTIVE: {
    const char *FullFileNameStart = Blob.data() + Record[0];
    StringRef FullFileName(FullFileNameStart, Blob.size() - Record[0]);
    const FileEntry *File = nullptr;
    if (!FullFileName.empty())
      File = PP.getFileManager().getFile(FullFileName);

    // FIXME: Stable encoding
    InclusionDirective::InclusionKind Kind
      = static_cast<InclusionDirective::InclusionKind>(Record[2]);
    InclusionDirective *ID
      = new (PPRec) InclusionDirective(PPRec, Kind,
                                       StringRef(Blob.data(), Record[0]),
                                       Record[1], Record[3],
                                       File,
                                       Range);
    return ID;
  }
  }

  llvm_unreachable("Invalid PreprocessorDetailRecordTypes");
}

/// Find the next module that contains entities and return the ID
/// of the first entry.
///
/// \param SLocMapI points at a chunk of a module that contains no
/// preprocessed entities or the entities it contains are not the ones we are
/// looking for.
PreprocessedEntityID ASTReader::findNextPreprocessedEntity(
                       GlobalSLocOffsetMapType::const_iterator SLocMapI) const {
  ++SLocMapI;
  for (GlobalSLocOffsetMapType::const_iterator
         EndI = GlobalSLocOffsetMap.end(); SLocMapI != EndI; ++SLocMapI) {
    ModuleFile &M = *SLocMapI->second;
    if (M.NumPreprocessedEntities)
      return M.BasePreprocessedEntityID;
  }

  return getTotalNumPreprocessedEntities();
}

namespace {

struct PPEntityComp {
  const ASTReader &Reader;
  ModuleFile &M;

  PPEntityComp(const ASTReader &Reader, ModuleFile &M) : Reader(Reader), M(M) {}

  bool operator()(const PPEntityOffset &L, const PPEntityOffset &R) const {
    SourceLocation LHS = getLoc(L);
    SourceLocation RHS = getLoc(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(const PPEntityOffset &L, SourceLocation RHS) const {
    SourceLocation LHS = getLoc(L);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(SourceLocation LHS, const PPEntityOffset &R) const {
    SourceLocation RHS = getLoc(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  SourceLocation getLoc(const PPEntityOffset &PPE) const {
    return Reader.TranslateSourceLocation(M, PPE.getBegin());
  }
};

} // namespace

PreprocessedEntityID ASTReader::findPreprocessedEntity(SourceLocation Loc,
                                                       bool EndsAfter) const {
  if (SourceMgr.isLocalSourceLocation(Loc))
    return getTotalNumPreprocessedEntities();

  GlobalSLocOffsetMapType::const_iterator SLocMapI = GlobalSLocOffsetMap.find(
      SourceManager::MaxLoadedOffset - Loc.getOffset() - 1);
  assert(SLocMapI != GlobalSLocOffsetMap.end() &&
         "Corrupted global sloc offset map");

  if (SLocMapI->second->NumPreprocessedEntities == 0)
    return findNextPreprocessedEntity(SLocMapI);

  ModuleFile &M = *SLocMapI->second;

  using pp_iterator = const PPEntityOffset *;

  pp_iterator pp_begin = M.PreprocessedEntityOffsets;
  pp_iterator pp_end = pp_begin + M.NumPreprocessedEntities;

  size_t Count = M.NumPreprocessedEntities;
  size_t Half;
  pp_iterator First = pp_begin;
  pp_iterator PPI;

  if (EndsAfter) {
    PPI = std::upper_bound(pp_begin, pp_end, Loc,
                           PPEntityComp(*this, M));
  } else {
    // Do a binary search manually instead of using std::lower_bound because
    // The end locations of entities may be unordered (when a macro expansion
    // is inside another macro argument), but for this case it is not important
    // whether we get the first macro expansion or its containing macro.
    while (Count > 0) {
      Half = Count / 2;
      PPI = First;
      std::advance(PPI, Half);
      if (SourceMgr.isBeforeInTranslationUnit(
              TranslateSourceLocation(M, PPI->getEnd()), Loc)) {
        First = PPI;
        ++First;
        Count = Count - Half - 1;
      } else
        Count = Half;
    }
  }

  if (PPI == pp_end)
    return findNextPreprocessedEntity(SLocMapI);

  return M.BasePreprocessedEntityID + (PPI - pp_begin);
}

/// Returns a pair of [Begin, End) indices of preallocated
/// preprocessed entities that \arg Range encompasses.
std::pair<unsigned, unsigned>
    ASTReader::findPreprocessedEntitiesInRange(SourceRange Range) {
  if (Range.isInvalid())
    return std::make_pair(0,0);
  assert(!SourceMgr.isBeforeInTranslationUnit(Range.getEnd(),Range.getBegin()));

  PreprocessedEntityID BeginID =
      findPreprocessedEntity(Range.getBegin(), false);
  PreprocessedEntityID EndID = findPreprocessedEntity(Range.getEnd(), true);
  return std::make_pair(BeginID, EndID);
}

/// Optionally returns true or false if the preallocated preprocessed
/// entity with index \arg Index came from file \arg FID.
Optional<bool> ASTReader::isPreprocessedEntityInFileID(unsigned Index,
                                                             FileID FID) {
  if (FID.isInvalid())
    return false;

  std::pair<ModuleFile *, unsigned> PPInfo = getModulePreprocessedEntity(Index);
  ModuleFile &M = *PPInfo.first;
  unsigned LocalIndex = PPInfo.second;
  const PPEntityOffset &PPOffs = M.PreprocessedEntityOffsets[LocalIndex];

  SourceLocation Loc = TranslateSourceLocation(M, PPOffs.getBegin());
  if (Loc.isInvalid())
    return false;

  if (SourceMgr.isInFileID(SourceMgr.getFileLoc(Loc), FID))
    return true;
  else
    return false;
}

namespace {

  /// Visitor used to search for information about a header file.
  class HeaderFileInfoVisitor {
    const FileEntry *FE;
    Optional<HeaderFileInfo> HFI;

  public:
    explicit HeaderFileInfoVisitor(const FileEntry *FE) : FE(FE) {}

    bool operator()(ModuleFile &M) {
      HeaderFileInfoLookupTable *Table
        = static_cast<HeaderFileInfoLookupTable *>(M.HeaderFileInfoTable);
      if (!Table)
        return false;

      // Look in the on-disk hash table for an entry for this file name.
      HeaderFileInfoLookupTable::iterator Pos = Table->find(FE);
      if (Pos == Table->end())
        return false;

      HFI = *Pos;
      return true;
    }

    Optional<HeaderFileInfo> getHeaderFileInfo() const { return HFI; }
  };

} // namespace

HeaderFileInfo ASTReader::GetHeaderFileInfo(const FileEntry *FE) {
  HeaderFileInfoVisitor Visitor(FE);
  ModuleMgr.visit(Visitor);
  if (Optional<HeaderFileInfo> HFI = Visitor.getHeaderFileInfo())
    return *HFI;

  return HeaderFileInfo();
}

void ASTReader::ReadPragmaDiagnosticMappings(DiagnosticsEngine &Diag) {
  using DiagState = DiagnosticsEngine::DiagState;
  SmallVector<DiagState *, 32> DiagStates;

  for (ModuleFile &F : ModuleMgr) {
    unsigned Idx = 0;
    auto &Record = F.PragmaDiagMappings;
    if (Record.empty())
      continue;

    DiagStates.clear();

    auto ReadDiagState =
        [&](const DiagState &BasedOn, SourceLocation Loc,
            bool IncludeNonPragmaStates) -> DiagnosticsEngine::DiagState * {
      unsigned BackrefID = Record[Idx++];
      if (BackrefID != 0)
        return DiagStates[BackrefID - 1];

      // A new DiagState was created here.
      Diag.DiagStates.push_back(BasedOn);
      DiagState *NewState = &Diag.DiagStates.back();
      DiagStates.push_back(NewState);
      unsigned Size = Record[Idx++];
      assert(Idx + Size * 2 <= Record.size() &&
             "Invalid data, not enough diag/map pairs");
      while (Size--) {
        unsigned DiagID = Record[Idx++];
        DiagnosticMapping NewMapping =
            DiagnosticMapping::deserialize(Record[Idx++]);
        if (!NewMapping.isPragma() && !IncludeNonPragmaStates)
          continue;

        DiagnosticMapping &Mapping = NewState->getOrAddMapping(DiagID);

        // If this mapping was specified as a warning but the severity was
        // upgraded due to diagnostic settings, simulate the current diagnostic
        // settings (and use a warning).
        if (NewMapping.wasUpgradedFromWarning() && !Mapping.isErrorOrFatal()) {
          NewMapping.setSeverity(diag::Severity::Warning);
          NewMapping.setUpgradedFromWarning(false);
        }

        Mapping = NewMapping;
      }
      return NewState;
    };

    // Read the first state.
    DiagState *FirstState;
    if (F.Kind == MK_ImplicitModule) {
      // Implicitly-built modules are reused with different diagnostic
      // settings.  Use the initial diagnostic state from Diag to simulate this
      // compilation's diagnostic settings.
      FirstState = Diag.DiagStatesByLoc.FirstDiagState;
      DiagStates.push_back(FirstState);

      // Skip the initial diagnostic state from the serialized module.
      assert(Record[1] == 0 &&
             "Invalid data, unexpected backref in initial state");
      Idx = 3 + Record[2] * 2;
      assert(Idx < Record.size() &&
             "Invalid data, not enough state change pairs in initial state");
    } else if (F.isModule()) {
      // For an explicit module, preserve the flags from the module build
      // command line (-w, -Weverything, -Werror, ...) along with any explicit
      // -Wblah flags.
      unsigned Flags = Record[Idx++];
      DiagState Initial;
      Initial.SuppressSystemWarnings = Flags & 1; Flags >>= 1;
      Initial.ErrorsAsFatal = Flags & 1; Flags >>= 1;
      Initial.WarningsAsErrors = Flags & 1; Flags >>= 1;
      Initial.EnableAllWarnings = Flags & 1; Flags >>= 1;
      Initial.IgnoreAllWarnings = Flags & 1; Flags >>= 1;
      Initial.ExtBehavior = (diag::Severity)Flags;
      FirstState = ReadDiagState(Initial, SourceLocation(), true);

      assert(F.OriginalSourceFileID.isValid());

      // Set up the root buffer of the module to start with the initial
      // diagnostic state of the module itself, to cover files that contain no
      // explicit transitions (for which we did not serialize anything).
      Diag.DiagStatesByLoc.Files[F.OriginalSourceFileID]
          .StateTransitions.push_back({FirstState, 0});
    } else {
      // For prefix ASTs, start with whatever the user configured on the
      // command line.
      Idx++; // Skip flags.
      FirstState = ReadDiagState(*Diag.DiagStatesByLoc.CurDiagState,
                                 SourceLocation(), false);
    }

    // Read the state transitions.
    unsigned NumLocations = Record[Idx++];
    while (NumLocations--) {
      assert(Idx < Record.size() &&
             "Invalid data, missing pragma diagnostic states");
      SourceLocation Loc = ReadSourceLocation(F, Record[Idx++]);
      auto IDAndOffset = SourceMgr.getDecomposedLoc(Loc);
      assert(IDAndOffset.first.isValid() && "invalid FileID for transition");
      assert(IDAndOffset.second == 0 && "not a start location for a FileID");
      unsigned Transitions = Record[Idx++];

      // Note that we don't need to set up Parent/ParentOffset here, because
      // we won't be changing the diagnostic state within imported FileIDs
      // (other than perhaps appending to the main source file, which has no
      // parent).
      auto &F = Diag.DiagStatesByLoc.Files[IDAndOffset.first];
      F.StateTransitions.reserve(F.StateTransitions.size() + Transitions);
      for (unsigned I = 0; I != Transitions; ++I) {
        unsigned Offset = Record[Idx++];
        auto *State =
            ReadDiagState(*FirstState, Loc.getLocWithOffset(Offset), false);
        F.StateTransitions.push_back({State, Offset});
      }
    }

    // Read the final state.
    assert(Idx < Record.size() &&
           "Invalid data, missing final pragma diagnostic state");
    SourceLocation CurStateLoc =
        ReadSourceLocation(F, F.PragmaDiagMappings[Idx++]);
    auto *CurState = ReadDiagState(*FirstState, CurStateLoc, false);

    if (!F.isModule()) {
      Diag.DiagStatesByLoc.CurDiagState = CurState;
      Diag.DiagStatesByLoc.CurDiagStateLoc = CurStateLoc;

      // Preserve the property that the imaginary root file describes the
      // current state.
      FileID NullFile;
      auto &T = Diag.DiagStatesByLoc.Files[NullFile].StateTransitions;
      if (T.empty())
        T.push_back({CurState, 0});
      else
        T[0].State = CurState;
    }

    // Don't try to read these mappings again.
    Record.clear();
  }
}

/// Get the correct cursor and offset for loading a type.
ASTReader::RecordLocation ASTReader::TypeCursorForIndex(unsigned Index) {
  GlobalTypeMapType::iterator I = GlobalTypeMap.find(Index);
  assert(I != GlobalTypeMap.end() && "Corrupted global type map");
  ModuleFile *M = I->second;
  return RecordLocation(M, M->TypeOffsets[Index - M->BaseTypeIndex]);
}

/// Read and return the type with the given index..
///
/// The index is the type ID, shifted and minus the number of predefs. This
/// routine actually reads the record corresponding to the type at the given
/// location. It is a helper routine for GetType, which deals with reading type
/// IDs.
QualType ASTReader::readTypeRecord(unsigned Index) {
  assert(ContextObj && "reading type with no AST context");
  ASTContext &Context = *ContextObj;
  RecordLocation Loc = TypeCursorForIndex(Index);
  BitstreamCursor &DeclsCursor = Loc.F->DeclsCursor;

  // Keep track of where we are in the stream, then jump back there
  // after reading this type.
  SavedStreamPosition SavedPosition(DeclsCursor);

  ReadingKindTracker ReadingKind(Read_Type, *this);

  // Note that we are loading a type record.
  Deserializing AType(this);

  unsigned Idx = 0;
  DeclsCursor.JumpToBit(Loc.Offset);
  RecordData Record;
  unsigned Code = DeclsCursor.ReadCode();
  switch ((TypeCode)DeclsCursor.readRecord(Code, Record)) {
  case TYPE_EXT_QUAL: {
    if (Record.size() != 2) {
      Error("Incorrect encoding of extended qualifier type");
      return QualType();
    }
    QualType Base = readType(*Loc.F, Record, Idx);
    Qualifiers Quals = Qualifiers::fromOpaqueValue(Record[Idx++]);
    return Context.getQualifiedType(Base, Quals);
  }

  case TYPE_COMPLEX: {
    if (Record.size() != 1) {
      Error("Incorrect encoding of complex type");
      return QualType();
    }
    QualType ElemType = readType(*Loc.F, Record, Idx);
    return Context.getComplexType(ElemType);
  }

  case TYPE_POINTER: {
    if (Record.size() != 1) {
      Error("Incorrect encoding of pointer type");
      return QualType();
    }
    QualType PointeeType = readType(*Loc.F, Record, Idx);
    return Context.getPointerType(PointeeType);
  }

  case TYPE_DECAYED: {
    if (Record.size() != 1) {
      Error("Incorrect encoding of decayed type");
      return QualType();
    }
    QualType OriginalType = readType(*Loc.F, Record, Idx);
    QualType DT = Context.getAdjustedParameterType(OriginalType);
    if (!isa<DecayedType>(DT))
      Error("Decayed type does not decay");
    return DT;
  }

  case TYPE_ADJUSTED: {
    if (Record.size() != 2) {
      Error("Incorrect encoding of adjusted type");
      return QualType();
    }
    QualType OriginalTy = readType(*Loc.F, Record, Idx);
    QualType AdjustedTy = readType(*Loc.F, Record, Idx);
    return Context.getAdjustedType(OriginalTy, AdjustedTy);
  }

  case TYPE_BLOCK_POINTER: {
    if (Record.size() != 1) {
      Error("Incorrect encoding of block pointer type");
      return QualType();
    }
    QualType PointeeType = readType(*Loc.F, Record, Idx);
    return Context.getBlockPointerType(PointeeType);
  }

  case TYPE_LVALUE_REFERENCE: {
    if (Record.size() != 2) {
      Error("Incorrect encoding of lvalue reference type");
      return QualType();
    }
    QualType PointeeType = readType(*Loc.F, Record, Idx);
    return Context.getLValueReferenceType(PointeeType, Record[1]);
  }

  case TYPE_RVALUE_REFERENCE: {
    if (Record.size() != 1) {
      Error("Incorrect encoding of rvalue reference type");
      return QualType();
    }
    QualType PointeeType = readType(*Loc.F, Record, Idx);
    return Context.getRValueReferenceType(PointeeType);
  }

  case TYPE_MEMBER_POINTER: {
    if (Record.size() != 2) {
      Error("Incorrect encoding of member pointer type");
      return QualType();
    }
    QualType PointeeType = readType(*Loc.F, Record, Idx);
    QualType ClassType = readType(*Loc.F, Record, Idx);
    if (PointeeType.isNull() || ClassType.isNull())
      return QualType();

    return Context.getMemberPointerType(PointeeType, ClassType.getTypePtr());
  }

  case TYPE_CONSTANT_ARRAY: {
    QualType ElementType = readType(*Loc.F, Record, Idx);
    ArrayType::ArraySizeModifier ASM = (ArrayType::ArraySizeModifier)Record[1];
    unsigned IndexTypeQuals = Record[2];
    unsigned Idx = 3;
    llvm::APInt Size = ReadAPInt(Record, Idx);
    return Context.getConstantArrayType(ElementType, Size,
                                         ASM, IndexTypeQuals);
  }

  case TYPE_INCOMPLETE_ARRAY: {
    QualType ElementType = readType(*Loc.F, Record, Idx);
    ArrayType::ArraySizeModifier ASM = (ArrayType::ArraySizeModifier)Record[1];
    unsigned IndexTypeQuals = Record[2];
    return Context.getIncompleteArrayType(ElementType, ASM, IndexTypeQuals);
  }

  case TYPE_VARIABLE_ARRAY: {
    QualType ElementType = readType(*Loc.F, Record, Idx);
    ArrayType::ArraySizeModifier ASM = (ArrayType::ArraySizeModifier)Record[1];
    unsigned IndexTypeQuals = Record[2];
    SourceLocation LBLoc = ReadSourceLocation(*Loc.F, Record[3]);
    SourceLocation RBLoc = ReadSourceLocation(*Loc.F, Record[4]);
    return Context.getVariableArrayType(ElementType, ReadExpr(*Loc.F),
                                         ASM, IndexTypeQuals,
                                         SourceRange(LBLoc, RBLoc));
  }

  case TYPE_VECTOR: {
    if (Record.size() != 3) {
      Error("incorrect encoding of vector type in AST file");
      return QualType();
    }

    QualType ElementType = readType(*Loc.F, Record, Idx);
    unsigned NumElements = Record[1];
    unsigned VecKind = Record[2];
    return Context.getVectorType(ElementType, NumElements,
                                  (VectorType::VectorKind)VecKind);
  }

  case TYPE_EXT_VECTOR: {
    if (Record.size() != 3) {
      Error("incorrect encoding of extended vector type in AST file");
      return QualType();
    }

    QualType ElementType = readType(*Loc.F, Record, Idx);
    unsigned NumElements = Record[1];
    return Context.getExtVectorType(ElementType, NumElements);
  }

  case TYPE_FUNCTION_NO_PROTO: {
    if (Record.size() != 8) {
      Error("incorrect encoding of no-proto function type");
      return QualType();
    }
    QualType ResultType = readType(*Loc.F, Record, Idx);
    FunctionType::ExtInfo Info(Record[1], Record[2], Record[3],
                               (CallingConv)Record[4], Record[5], Record[6],
                               Record[7]);
    return Context.getFunctionNoProtoType(ResultType, Info);
  }

  case TYPE_FUNCTION_PROTO: {
    QualType ResultType = readType(*Loc.F, Record, Idx);

    FunctionProtoType::ExtProtoInfo EPI;
    EPI.ExtInfo = FunctionType::ExtInfo(/*noreturn*/ Record[1],
                                        /*hasregparm*/ Record[2],
                                        /*regparm*/ Record[3],
                                        static_cast<CallingConv>(Record[4]),
                                        /*produces*/ Record[5],
                                        /*nocallersavedregs*/ Record[6],
                                        /*nocfcheck*/ Record[7]);

    unsigned Idx = 8;

    EPI.Variadic = Record[Idx++];
    EPI.HasTrailingReturn = Record[Idx++];
    EPI.TypeQuals = Qualifiers::fromOpaqueValue(Record[Idx++]);
    EPI.RefQualifier = static_cast<RefQualifierKind>(Record[Idx++]);
    SmallVector<QualType, 8> ExceptionStorage;
    readExceptionSpec(*Loc.F, ExceptionStorage, EPI.ExceptionSpec, Record, Idx);

    unsigned NumParams = Record[Idx++];
    SmallVector<QualType, 16> ParamTypes;
    for (unsigned I = 0; I != NumParams; ++I)
      ParamTypes.push_back(readType(*Loc.F, Record, Idx));

    SmallVector<FunctionProtoType::ExtParameterInfo, 4> ExtParameterInfos;
    if (Idx != Record.size()) {
      for (unsigned I = 0; I != NumParams; ++I)
        ExtParameterInfos.push_back(
          FunctionProtoType::ExtParameterInfo
                           ::getFromOpaqueValue(Record[Idx++]));
      EPI.ExtParameterInfos = ExtParameterInfos.data();
    }

    assert(Idx == Record.size());

    return Context.getFunctionType(ResultType, ParamTypes, EPI);
  }

  case TYPE_UNRESOLVED_USING: {
    unsigned Idx = 0;
    return Context.getTypeDeclType(
                  ReadDeclAs<UnresolvedUsingTypenameDecl>(*Loc.F, Record, Idx));
  }

  case TYPE_TYPEDEF: {
    if (Record.size() != 2) {
      Error("incorrect encoding of typedef type");
      return QualType();
    }
    unsigned Idx = 0;
    TypedefNameDecl *Decl = ReadDeclAs<TypedefNameDecl>(*Loc.F, Record, Idx);
    QualType Canonical = readType(*Loc.F, Record, Idx);
    if (!Canonical.isNull())
      Canonical = Context.getCanonicalType(Canonical);
    return Context.getTypedefType(Decl, Canonical);
  }

  case TYPE_TYPEOF_EXPR:
    return Context.getTypeOfExprType(ReadExpr(*Loc.F));

  case TYPE_TYPEOF: {
    if (Record.size() != 1) {
      Error("incorrect encoding of typeof(type) in AST file");
      return QualType();
    }
    QualType UnderlyingType = readType(*Loc.F, Record, Idx);
    return Context.getTypeOfType(UnderlyingType);
  }

  case TYPE_DECLTYPE: {
    QualType UnderlyingType = readType(*Loc.F, Record, Idx);
    return Context.getDecltypeType(ReadExpr(*Loc.F), UnderlyingType);
  }

  case TYPE_UNARY_TRANSFORM: {
    QualType BaseType = readType(*Loc.F, Record, Idx);
    QualType UnderlyingType = readType(*Loc.F, Record, Idx);
    UnaryTransformType::UTTKind UKind = (UnaryTransformType::UTTKind)Record[2];
    return Context.getUnaryTransformType(BaseType, UnderlyingType, UKind);
  }

  case TYPE_AUTO: {
    QualType Deduced = readType(*Loc.F, Record, Idx);
    AutoTypeKeyword Keyword = (AutoTypeKeyword)Record[Idx++];
    bool IsDependent = Deduced.isNull() ? Record[Idx++] : false;
    return Context.getAutoType(Deduced, Keyword, IsDependent);
  }

  case TYPE_DEDUCED_TEMPLATE_SPECIALIZATION: {
    TemplateName Name = ReadTemplateName(*Loc.F, Record, Idx);
    QualType Deduced = readType(*Loc.F, Record, Idx);
    bool IsDependent = Deduced.isNull() ? Record[Idx++] : false;
    return Context.getDeducedTemplateSpecializationType(Name, Deduced,
                                                        IsDependent);
  }

  case TYPE_RECORD: {
    if (Record.size() != 2) {
      Error("incorrect encoding of record type");
      return QualType();
    }
    unsigned Idx = 0;
    bool IsDependent = Record[Idx++];
    RecordDecl *RD = ReadDeclAs<RecordDecl>(*Loc.F, Record, Idx);
    RD = cast_or_null<RecordDecl>(RD->getCanonicalDecl());
    QualType T = Context.getRecordType(RD);
    const_cast<Type*>(T.getTypePtr())->setDependent(IsDependent);
    return T;
  }

  case TYPE_ENUM: {
    if (Record.size() != 2) {
      Error("incorrect encoding of enum type");
      return QualType();
    }
    unsigned Idx = 0;
    bool IsDependent = Record[Idx++];
    QualType T
      = Context.getEnumType(ReadDeclAs<EnumDecl>(*Loc.F, Record, Idx));
    const_cast<Type*>(T.getTypePtr())->setDependent(IsDependent);
    return T;
  }

  case TYPE_ATTRIBUTED: {
    if (Record.size() != 3) {
      Error("incorrect encoding of attributed type");
      return QualType();
    }
    QualType modifiedType = readType(*Loc.F, Record, Idx);
    QualType equivalentType = readType(*Loc.F, Record, Idx);
    AttributedType::Kind kind = static_cast<AttributedType::Kind>(Record[2]);
    return Context.getAttributedType(kind, modifiedType, equivalentType);
  }

  case TYPE_PAREN: {
    if (Record.size() != 1) {
      Error("incorrect encoding of paren type");
      return QualType();
    }
    QualType InnerType = readType(*Loc.F, Record, Idx);
    return Context.getParenType(InnerType);
  }

  case TYPE_PACK_EXPANSION: {
    if (Record.size() != 2) {
      Error("incorrect encoding of pack expansion type");
      return QualType();
    }
    QualType Pattern = readType(*Loc.F, Record, Idx);
    if (Pattern.isNull())
      return QualType();
    Optional<unsigned> NumExpansions;
    if (Record[1])
      NumExpansions = Record[1] - 1;
    return Context.getPackExpansionType(Pattern, NumExpansions);
  }

  case TYPE_ELABORATED: {
    unsigned Idx = 0;
    ElaboratedTypeKeyword Keyword = (ElaboratedTypeKeyword)Record[Idx++];
    NestedNameSpecifier *NNS = ReadNestedNameSpecifier(*Loc.F, Record, Idx);
    QualType NamedType = readType(*Loc.F, Record, Idx);
    TagDecl *OwnedTagDecl = ReadDeclAs<TagDecl>(*Loc.F, Record, Idx);
    return Context.getElaboratedType(Keyword, NNS, NamedType, OwnedTagDecl);
  }

  case TYPE_OBJC_INTERFACE: {
    unsigned Idx = 0;
    ObjCInterfaceDecl *ItfD
      = ReadDeclAs<ObjCInterfaceDecl>(*Loc.F, Record, Idx);
    return Context.getObjCInterfaceType(ItfD->getCanonicalDecl());
  }

  case TYPE_OBJC_TYPE_PARAM: {
    unsigned Idx = 0;
    ObjCTypeParamDecl *Decl
      = ReadDeclAs<ObjCTypeParamDecl>(*Loc.F, Record, Idx);
    unsigned NumProtos = Record[Idx++];
    SmallVector<ObjCProtocolDecl*, 4> Protos;
    for (unsigned I = 0; I != NumProtos; ++I)
      Protos.push_back(ReadDeclAs<ObjCProtocolDecl>(*Loc.F, Record, Idx));
    return Context.getObjCTypeParamType(Decl, Protos);
  }

  case TYPE_OBJC_OBJECT: {
    unsigned Idx = 0;
    QualType Base = readType(*Loc.F, Record, Idx);
    unsigned NumTypeArgs = Record[Idx++];
    SmallVector<QualType, 4> TypeArgs;
    for (unsigned I = 0; I != NumTypeArgs; ++I)
      TypeArgs.push_back(readType(*Loc.F, Record, Idx));
    unsigned NumProtos = Record[Idx++];
    SmallVector<ObjCProtocolDecl*, 4> Protos;
    for (unsigned I = 0; I != NumProtos; ++I)
      Protos.push_back(ReadDeclAs<ObjCProtocolDecl>(*Loc.F, Record, Idx));
    bool IsKindOf = Record[Idx++];
    return Context.getObjCObjectType(Base, TypeArgs, Protos, IsKindOf);
  }

  case TYPE_OBJC_OBJECT_POINTER: {
    unsigned Idx = 0;
    QualType Pointee = readType(*Loc.F, Record, Idx);
    return Context.getObjCObjectPointerType(Pointee);
  }

  case TYPE_SUBST_TEMPLATE_TYPE_PARM: {
    unsigned Idx = 0;
    QualType Parm = readType(*Loc.F, Record, Idx);
    QualType Replacement = readType(*Loc.F, Record, Idx);
    return Context.getSubstTemplateTypeParmType(
        cast<TemplateTypeParmType>(Parm),
        Context.getCanonicalType(Replacement));
  }

  case TYPE_SUBST_TEMPLATE_TYPE_PARM_PACK: {
    unsigned Idx = 0;
    QualType Parm = readType(*Loc.F, Record, Idx);
    TemplateArgument ArgPack = ReadTemplateArgument(*Loc.F, Record, Idx);
    return Context.getSubstTemplateTypeParmPackType(
                                               cast<TemplateTypeParmType>(Parm),
                                                     ArgPack);
  }

  case TYPE_INJECTED_CLASS_NAME: {
    CXXRecordDecl *D = ReadDeclAs<CXXRecordDecl>(*Loc.F, Record, Idx);
    QualType TST = readType(*Loc.F, Record, Idx); // probably derivable
    // FIXME: ASTContext::getInjectedClassNameType is not currently suitable
    // for AST reading, too much interdependencies.
    const Type *T = nullptr;
    for (auto *DI = D; DI; DI = DI->getPreviousDecl()) {
      if (const Type *Existing = DI->getTypeForDecl()) {
        T = Existing;
        break;
      }
    }
    if (!T) {
      T = new (Context, TypeAlignment) InjectedClassNameType(D, TST);
      for (auto *DI = D; DI; DI = DI->getPreviousDecl())
        DI->setTypeForDecl(T);
    }
    return QualType(T, 0);
  }

  case TYPE_TEMPLATE_TYPE_PARM: {
    unsigned Idx = 0;
    unsigned Depth = Record[Idx++];
    unsigned Index = Record[Idx++];
    bool Pack = Record[Idx++];
    TemplateTypeParmDecl *D
      = ReadDeclAs<TemplateTypeParmDecl>(*Loc.F, Record, Idx);
    return Context.getTemplateTypeParmType(Depth, Index, Pack, D);
  }

  case TYPE_DEPENDENT_NAME: {
    unsigned Idx = 0;
    ElaboratedTypeKeyword Keyword = (ElaboratedTypeKeyword)Record[Idx++];
    NestedNameSpecifier *NNS = ReadNestedNameSpecifier(*Loc.F, Record, Idx);
    const IdentifierInfo *Name = GetIdentifierInfo(*Loc.F, Record, Idx);
    QualType Canon = readType(*Loc.F, Record, Idx);
    if (!Canon.isNull())
      Canon = Context.getCanonicalType(Canon);
    return Context.getDependentNameType(Keyword, NNS, Name, Canon);
  }

  case TYPE_DEPENDENT_TEMPLATE_SPECIALIZATION: {
    unsigned Idx = 0;
    ElaboratedTypeKeyword Keyword = (ElaboratedTypeKeyword)Record[Idx++];
    NestedNameSpecifier *NNS = ReadNestedNameSpecifier(*Loc.F, Record, Idx);
    const IdentifierInfo *Name = GetIdentifierInfo(*Loc.F, Record, Idx);
    unsigned NumArgs = Record[Idx++];
    SmallVector<TemplateArgument, 8> Args;
    Args.reserve(NumArgs);
    while (NumArgs--)
      Args.push_back(ReadTemplateArgument(*Loc.F, Record, Idx));
    return Context.getDependentTemplateSpecializationType(Keyword, NNS, Name,
                                                          Args);
  }

  case TYPE_DEPENDENT_SIZED_ARRAY: {
    unsigned Idx = 0;

    // ArrayType
    QualType ElementType = readType(*Loc.F, Record, Idx);
    ArrayType::ArraySizeModifier ASM
      = (ArrayType::ArraySizeModifier)Record[Idx++];
    unsigned IndexTypeQuals = Record[Idx++];

    // DependentSizedArrayType
    Expr *NumElts = ReadExpr(*Loc.F);
    SourceRange Brackets = ReadSourceRange(*Loc.F, Record, Idx);

    return Context.getDependentSizedArrayType(ElementType, NumElts, ASM,
                                               IndexTypeQuals, Brackets);
  }

  case TYPE_TEMPLATE_SPECIALIZATION: {
    unsigned Idx = 0;
    bool IsDependent = Record[Idx++];
    TemplateName Name = ReadTemplateName(*Loc.F, Record, Idx);
    SmallVector<TemplateArgument, 8> Args;
    ReadTemplateArgumentList(Args, *Loc.F, Record, Idx);
    QualType Underlying = readType(*Loc.F, Record, Idx);
    QualType T;
    if (Underlying.isNull())
      T = Context.getCanonicalTemplateSpecializationType(Name, Args);
    else
      T = Context.getTemplateSpecializationType(Name, Args, Underlying);
    const_cast<Type*>(T.getTypePtr())->setDependent(IsDependent);
    return T;
  }

  case TYPE_ATOMIC: {
    if (Record.size() != 1) {
      Error("Incorrect encoding of atomic type");
      return QualType();
    }
    QualType ValueType = readType(*Loc.F, Record, Idx);
    return Context.getAtomicType(ValueType);
  }

  case TYPE_PIPE: {
    if (Record.size() != 2) {
      Error("Incorrect encoding of pipe type");
      return QualType();
    }

    // Reading the pipe element type.
    QualType ElementType = readType(*Loc.F, Record, Idx);
    unsigned ReadOnly = Record[1];
    return Context.getPipeType(ElementType, ReadOnly);
  }

  case TYPE_DEPENDENT_SIZED_VECTOR: {
    unsigned Idx = 0;
    QualType ElementType = readType(*Loc.F, Record, Idx);
    Expr *SizeExpr = ReadExpr(*Loc.F);
    SourceLocation AttrLoc = ReadSourceLocation(*Loc.F, Record, Idx);
    unsigned VecKind = Record[Idx];

    return Context.getDependentVectorType(ElementType, SizeExpr, AttrLoc,
                                               (VectorType::VectorKind)VecKind);
  }

  case TYPE_DEPENDENT_SIZED_EXT_VECTOR: {
    unsigned Idx = 0;

    // DependentSizedExtVectorType
    QualType ElementType = readType(*Loc.F, Record, Idx);
    Expr *SizeExpr = ReadExpr(*Loc.F);
    SourceLocation AttrLoc = ReadSourceLocation(*Loc.F, Record, Idx);

    return Context.getDependentSizedExtVectorType(ElementType, SizeExpr,
                                                  AttrLoc);
  }

  case TYPE_DEPENDENT_ADDRESS_SPACE: {
    unsigned Idx = 0;

    // DependentAddressSpaceType
    QualType PointeeType = readType(*Loc.F, Record, Idx);
    Expr *AddrSpaceExpr = ReadExpr(*Loc.F);
    SourceLocation AttrLoc = ReadSourceLocation(*Loc.F, Record, Idx);

    return Context.getDependentAddressSpaceType(PointeeType, AddrSpaceExpr,
                                                   AttrLoc);
  }
  }
  llvm_unreachable("Invalid TypeCode!");
}

void ASTReader::readExceptionSpec(ModuleFile &ModuleFile,
                                  SmallVectorImpl<QualType> &Exceptions,
                                  FunctionProtoType::ExceptionSpecInfo &ESI,
                                  const RecordData &Record, unsigned &Idx) {
  ExceptionSpecificationType EST =
      static_cast<ExceptionSpecificationType>(Record[Idx++]);
  ESI.Type = EST;
  if (EST == EST_Dynamic) {
    for (unsigned I = 0, N = Record[Idx++]; I != N; ++I)
      Exceptions.push_back(readType(ModuleFile, Record, Idx));
    ESI.Exceptions = Exceptions;
  } else if (isComputedNoexcept(EST)) {
    ESI.NoexceptExpr = ReadExpr(ModuleFile);
  } else if (EST == EST_Uninstantiated) {
    ESI.SourceDecl = ReadDeclAs<FunctionDecl>(ModuleFile, Record, Idx);
    ESI.SourceTemplate = ReadDeclAs<FunctionDecl>(ModuleFile, Record, Idx);
  } else if (EST == EST_Unevaluated) {
    ESI.SourceDecl = ReadDeclAs<FunctionDecl>(ModuleFile, Record, Idx);
  }
}

namespace clang {

class TypeLocReader : public TypeLocVisitor<TypeLocReader> {
  ModuleFile *F;
  ASTReader *Reader;
  const ASTReader::RecordData &Record;
  unsigned &Idx;

  SourceLocation ReadSourceLocation() {
    return Reader->ReadSourceLocation(*F, Record, Idx);
  }

  TypeSourceInfo *GetTypeSourceInfo() {
    return Reader->GetTypeSourceInfo(*F, Record, Idx);
  }

  NestedNameSpecifierLoc ReadNestedNameSpecifierLoc() {
    return Reader->ReadNestedNameSpecifierLoc(*F, Record, Idx);
  }

  Attr *ReadAttr() {
    return Reader->ReadAttr(*F, Record, Idx);
  }

public:
  TypeLocReader(ModuleFile &F, ASTReader &Reader,
                const ASTReader::RecordData &Record, unsigned &Idx)
      : F(&F), Reader(&Reader), Record(Record), Idx(Idx) {}

  // We want compile-time assurance that we've enumerated all of
  // these, so unfortunately we have to declare them first, then
  // define them out-of-line.
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
  void Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc);
#include "clang/AST/TypeLocNodes.def"

  void VisitFunctionTypeLoc(FunctionTypeLoc);
  void VisitArrayTypeLoc(ArrayTypeLoc);
};

} // namespace clang

void TypeLocReader::VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
  // nothing to do
}

void TypeLocReader::VisitBuiltinTypeLoc(BuiltinTypeLoc TL) {
  TL.setBuiltinLoc(ReadSourceLocation());
  if (TL.needsExtraLocalData()) {
    TL.setWrittenTypeSpec(static_cast<DeclSpec::TST>(Record[Idx++]));
    TL.setWrittenSignSpec(static_cast<DeclSpec::TSS>(Record[Idx++]));
    TL.setWrittenWidthSpec(static_cast<DeclSpec::TSW>(Record[Idx++]));
    TL.setModeAttr(Record[Idx++]);
  }
}

void TypeLocReader::VisitComplexTypeLoc(ComplexTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitPointerTypeLoc(PointerTypeLoc TL) {
  TL.setStarLoc(ReadSourceLocation());
}

void TypeLocReader::VisitDecayedTypeLoc(DecayedTypeLoc TL) {
  // nothing to do
}

void TypeLocReader::VisitAdjustedTypeLoc(AdjustedTypeLoc TL) {
  // nothing to do
}

void TypeLocReader::VisitBlockPointerTypeLoc(BlockPointerTypeLoc TL) {
  TL.setCaretLoc(ReadSourceLocation());
}

void TypeLocReader::VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc TL) {
  TL.setAmpLoc(ReadSourceLocation());
}

void TypeLocReader::VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc TL) {
  TL.setAmpAmpLoc(ReadSourceLocation());
}

void TypeLocReader::VisitMemberPointerTypeLoc(MemberPointerTypeLoc TL) {
  TL.setStarLoc(ReadSourceLocation());
  TL.setClassTInfo(GetTypeSourceInfo());
}

void TypeLocReader::VisitArrayTypeLoc(ArrayTypeLoc TL) {
  TL.setLBracketLoc(ReadSourceLocation());
  TL.setRBracketLoc(ReadSourceLocation());
  if (Record[Idx++])
    TL.setSizeExpr(Reader->ReadExpr(*F));
  else
    TL.setSizeExpr(nullptr);
}

void TypeLocReader::VisitConstantArrayTypeLoc(ConstantArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitIncompleteArrayTypeLoc(IncompleteArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitVariableArrayTypeLoc(VariableArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitDependentSizedArrayTypeLoc(
                                            DependentSizedArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocReader::VisitDependentAddressSpaceTypeLoc(
    DependentAddressSpaceTypeLoc TL) {

    TL.setAttrNameLoc(ReadSourceLocation());
    SourceRange range;
    range.setBegin(ReadSourceLocation());
    range.setEnd(ReadSourceLocation());
    TL.setAttrOperandParensRange(range);
    TL.setAttrExprOperand(Reader->ReadExpr(*F));
}

void TypeLocReader::VisitDependentSizedExtVectorTypeLoc(
                                        DependentSizedExtVectorTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitVectorTypeLoc(VectorTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitDependentVectorTypeLoc(
    DependentVectorTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitExtVectorTypeLoc(ExtVectorTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitFunctionTypeLoc(FunctionTypeLoc TL) {
  TL.setLocalRangeBegin(ReadSourceLocation());
  TL.setLParenLoc(ReadSourceLocation());
  TL.setRParenLoc(ReadSourceLocation());
  TL.setExceptionSpecRange(SourceRange(Reader->ReadSourceLocation(*F, Record, Idx),
                                       Reader->ReadSourceLocation(*F, Record, Idx)));
  TL.setLocalRangeEnd(ReadSourceLocation());
  for (unsigned i = 0, e = TL.getNumParams(); i != e; ++i) {
    TL.setParam(i, Reader->ReadDeclAs<ParmVarDecl>(*F, Record, Idx));
  }
}

void TypeLocReader::VisitFunctionProtoTypeLoc(FunctionProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocReader::VisitFunctionNoProtoTypeLoc(FunctionNoProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocReader::VisitUnresolvedUsingTypeLoc(UnresolvedUsingTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitTypedefTypeLoc(TypedefTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
  TL.setTypeofLoc(ReadSourceLocation());
  TL.setLParenLoc(ReadSourceLocation());
  TL.setRParenLoc(ReadSourceLocation());
}

void TypeLocReader::VisitTypeOfTypeLoc(TypeOfTypeLoc TL) {
  TL.setTypeofLoc(ReadSourceLocation());
  TL.setLParenLoc(ReadSourceLocation());
  TL.setRParenLoc(ReadSourceLocation());
  TL.setUnderlyingTInfo(GetTypeSourceInfo());
}

void TypeLocReader::VisitDecltypeTypeLoc(DecltypeTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitUnaryTransformTypeLoc(UnaryTransformTypeLoc TL) {
  TL.setKWLoc(ReadSourceLocation());
  TL.setLParenLoc(ReadSourceLocation());
  TL.setRParenLoc(ReadSourceLocation());
  TL.setUnderlyingTInfo(GetTypeSourceInfo());
}

void TypeLocReader::VisitAutoTypeLoc(AutoTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitDeducedTemplateSpecializationTypeLoc(
    DeducedTemplateSpecializationTypeLoc TL) {
  TL.setTemplateNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitRecordTypeLoc(RecordTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitEnumTypeLoc(EnumTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitAttributedTypeLoc(AttributedTypeLoc TL) {
  TL.setAttr(ReadAttr());
}

void TypeLocReader::VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitSubstTemplateTypeParmTypeLoc(
                                            SubstTemplateTypeParmTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitSubstTemplateTypeParmPackTypeLoc(
                                          SubstTemplateTypeParmPackTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitTemplateSpecializationTypeLoc(
                                           TemplateSpecializationTypeLoc TL) {
  TL.setTemplateKeywordLoc(ReadSourceLocation());
  TL.setTemplateNameLoc(ReadSourceLocation());
  TL.setLAngleLoc(ReadSourceLocation());
  TL.setRAngleLoc(ReadSourceLocation());
  for (unsigned i = 0, e = TL.getNumArgs(); i != e; ++i)
    TL.setArgLocInfo(
        i,
        Reader->GetTemplateArgumentLocInfo(
            *F, TL.getTypePtr()->getArg(i).getKind(), Record, Idx));
}

void TypeLocReader::VisitParenTypeLoc(ParenTypeLoc TL) {
  TL.setLParenLoc(ReadSourceLocation());
  TL.setRParenLoc(ReadSourceLocation());
}

void TypeLocReader::VisitElaboratedTypeLoc(ElaboratedTypeLoc TL) {
  TL.setElaboratedKeywordLoc(ReadSourceLocation());
  TL.setQualifierLoc(ReadNestedNameSpecifierLoc());
}

void TypeLocReader::VisitInjectedClassNameTypeLoc(InjectedClassNameTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
  TL.setElaboratedKeywordLoc(ReadSourceLocation());
  TL.setQualifierLoc(ReadNestedNameSpecifierLoc());
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitDependentTemplateSpecializationTypeLoc(
       DependentTemplateSpecializationTypeLoc TL) {
  TL.setElaboratedKeywordLoc(ReadSourceLocation());
  TL.setQualifierLoc(ReadNestedNameSpecifierLoc());
  TL.setTemplateKeywordLoc(ReadSourceLocation());
  TL.setTemplateNameLoc(ReadSourceLocation());
  TL.setLAngleLoc(ReadSourceLocation());
  TL.setRAngleLoc(ReadSourceLocation());
  for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I)
    TL.setArgLocInfo(
        I,
        Reader->GetTemplateArgumentLocInfo(
            *F, TL.getTypePtr()->getArg(I).getKind(), Record, Idx));
}

void TypeLocReader::VisitPackExpansionTypeLoc(PackExpansionTypeLoc TL) {
  TL.setEllipsisLoc(ReadSourceLocation());
}

void TypeLocReader::VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
  TL.setNameLoc(ReadSourceLocation());
}

void TypeLocReader::VisitObjCTypeParamTypeLoc(ObjCTypeParamTypeLoc TL) {
  if (TL.getNumProtocols()) {
    TL.setProtocolLAngleLoc(ReadSourceLocation());
    TL.setProtocolRAngleLoc(ReadSourceLocation());
  }
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    TL.setProtocolLoc(i, ReadSourceLocation());
}

void TypeLocReader::VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
  TL.setHasBaseTypeAsWritten(Record[Idx++]);
  TL.setTypeArgsLAngleLoc(ReadSourceLocation());
  TL.setTypeArgsRAngleLoc(ReadSourceLocation());
  for (unsigned i = 0, e = TL.getNumTypeArgs(); i != e; ++i)
    TL.setTypeArgTInfo(i, GetTypeSourceInfo());
  TL.setProtocolLAngleLoc(ReadSourceLocation());
  TL.setProtocolRAngleLoc(ReadSourceLocation());
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    TL.setProtocolLoc(i, ReadSourceLocation());
}

void TypeLocReader::VisitObjCObjectPointerTypeLoc(ObjCObjectPointerTypeLoc TL) {
  TL.setStarLoc(ReadSourceLocation());
}

void TypeLocReader::VisitAtomicTypeLoc(AtomicTypeLoc TL) {
  TL.setKWLoc(ReadSourceLocation());
  TL.setLParenLoc(ReadSourceLocation());
  TL.setRParenLoc(ReadSourceLocation());
}

void TypeLocReader::VisitPipeTypeLoc(PipeTypeLoc TL) {
  TL.setKWLoc(ReadSourceLocation());
}

void ASTReader::ReadTypeLoc(ModuleFile &F, const ASTReader::RecordData &Record,
                            unsigned &Idx, TypeLoc TL) {
  TypeLocReader TLR(F, *this, Record, Idx);
  for (; !TL.isNull(); TL = TL.getNextTypeLoc())
    TLR.Visit(TL);
}

TypeSourceInfo *
ASTReader::GetTypeSourceInfo(ModuleFile &F, const ASTReader::RecordData &Record,
                             unsigned &Idx) {
  QualType InfoTy = readType(F, Record, Idx);
  if (InfoTy.isNull())
    return nullptr;

  TypeSourceInfo *TInfo = getContext().CreateTypeSourceInfo(InfoTy);
  ReadTypeLoc(F, Record, Idx, TInfo->getTypeLoc());
  return TInfo;
}

QualType ASTReader::GetType(TypeID ID) {
  assert(ContextObj && "reading type with no AST context");
  ASTContext &Context = *ContextObj;

  unsigned FastQuals = ID & Qualifiers::FastMask;
  unsigned Index = ID >> Qualifiers::FastWidth;

  if (Index < NUM_PREDEF_TYPE_IDS) {
    QualType T;
    switch ((PredefinedTypeIDs)Index) {
    case PREDEF_TYPE_NULL_ID:
      return QualType();
    case PREDEF_TYPE_VOID_ID:
      T = Context.VoidTy;
      break;
    case PREDEF_TYPE_BOOL_ID:
      T = Context.BoolTy;
      break;
    case PREDEF_TYPE_CHAR_U_ID:
    case PREDEF_TYPE_CHAR_S_ID:
      // FIXME: Check that the signedness of CharTy is correct!
      T = Context.CharTy;
      break;
    case PREDEF_TYPE_UCHAR_ID:
      T = Context.UnsignedCharTy;
      break;
    case PREDEF_TYPE_USHORT_ID:
      T = Context.UnsignedShortTy;
      break;
    case PREDEF_TYPE_UINT_ID:
      T = Context.UnsignedIntTy;
      break;
    case PREDEF_TYPE_ULONG_ID:
      T = Context.UnsignedLongTy;
      break;
    case PREDEF_TYPE_ULONGLONG_ID:
      T = Context.UnsignedLongLongTy;
      break;
    case PREDEF_TYPE_UINT128_ID:
      T = Context.UnsignedInt128Ty;
      break;
    case PREDEF_TYPE_SCHAR_ID:
      T = Context.SignedCharTy;
      break;
    case PREDEF_TYPE_WCHAR_ID:
      T = Context.WCharTy;
      break;
    case PREDEF_TYPE_SHORT_ID:
      T = Context.ShortTy;
      break;
    case PREDEF_TYPE_INT_ID:
      T = Context.IntTy;
      break;
    case PREDEF_TYPE_LONG_ID:
      T = Context.LongTy;
      break;
    case PREDEF_TYPE_LONGLONG_ID:
      T = Context.LongLongTy;
      break;
    case PREDEF_TYPE_INT128_ID:
      T = Context.Int128Ty;
      break;
    case PREDEF_TYPE_HALF_ID:
      T = Context.HalfTy;
      break;
    case PREDEF_TYPE_FLOAT_ID:
      T = Context.FloatTy;
      break;
    case PREDEF_TYPE_DOUBLE_ID:
      T = Context.DoubleTy;
      break;
    case PREDEF_TYPE_LONGDOUBLE_ID:
      T = Context.LongDoubleTy;
      break;
    case PREDEF_TYPE_SHORT_ACCUM_ID:
      T = Context.ShortAccumTy;
      break;
    case PREDEF_TYPE_ACCUM_ID:
      T = Context.AccumTy;
      break;
    case PREDEF_TYPE_LONG_ACCUM_ID:
      T = Context.LongAccumTy;
      break;
    case PREDEF_TYPE_USHORT_ACCUM_ID:
      T = Context.UnsignedShortAccumTy;
      break;
    case PREDEF_TYPE_UACCUM_ID:
      T = Context.UnsignedAccumTy;
      break;
    case PREDEF_TYPE_ULONG_ACCUM_ID:
      T = Context.UnsignedLongAccumTy;
      break;
    case PREDEF_TYPE_SHORT_FRACT_ID:
      T = Context.ShortFractTy;
      break;
    case PREDEF_TYPE_FRACT_ID:
      T = Context.FractTy;
      break;
    case PREDEF_TYPE_LONG_FRACT_ID:
      T = Context.LongFractTy;
      break;
    case PREDEF_TYPE_USHORT_FRACT_ID:
      T = Context.UnsignedShortFractTy;
      break;
    case PREDEF_TYPE_UFRACT_ID:
      T = Context.UnsignedFractTy;
      break;
    case PREDEF_TYPE_ULONG_FRACT_ID:
      T = Context.UnsignedLongFractTy;
      break;
    case PREDEF_TYPE_SAT_SHORT_ACCUM_ID:
      T = Context.SatShortAccumTy;
      break;
    case PREDEF_TYPE_SAT_ACCUM_ID:
      T = Context.SatAccumTy;
      break;
    case PREDEF_TYPE_SAT_LONG_ACCUM_ID:
      T = Context.SatLongAccumTy;
      break;
    case PREDEF_TYPE_SAT_USHORT_ACCUM_ID:
      T = Context.SatUnsignedShortAccumTy;
      break;
    case PREDEF_TYPE_SAT_UACCUM_ID:
      T = Context.SatUnsignedAccumTy;
      break;
    case PREDEF_TYPE_SAT_ULONG_ACCUM_ID:
      T = Context.SatUnsignedLongAccumTy;
      break;
    case PREDEF_TYPE_SAT_SHORT_FRACT_ID:
      T = Context.SatShortFractTy;
      break;
    case PREDEF_TYPE_SAT_FRACT_ID:
      T = Context.SatFractTy;
      break;
    case PREDEF_TYPE_SAT_LONG_FRACT_ID:
      T = Context.SatLongFractTy;
      break;
    case PREDEF_TYPE_SAT_USHORT_FRACT_ID:
      T = Context.SatUnsignedShortFractTy;
      break;
    case PREDEF_TYPE_SAT_UFRACT_ID:
      T = Context.SatUnsignedFractTy;
      break;
    case PREDEF_TYPE_SAT_ULONG_FRACT_ID:
      T = Context.SatUnsignedLongFractTy;
      break;
    case PREDEF_TYPE_FLOAT16_ID:
      T = Context.Float16Ty;
      break;
    case PREDEF_TYPE_FLOAT128_ID:
      T = Context.Float128Ty;
      break;
    case PREDEF_TYPE_OVERLOAD_ID:
      T = Context.OverloadTy;
      break;
    case PREDEF_TYPE_BOUND_MEMBER:
      T = Context.BoundMemberTy;
      break;
    case PREDEF_TYPE_PSEUDO_OBJECT:
      T = Context.PseudoObjectTy;
      break;
    case PREDEF_TYPE_DEPENDENT_ID:
      T = Context.DependentTy;
      break;
    case PREDEF_TYPE_UNKNOWN_ANY:
      T = Context.UnknownAnyTy;
      break;
    case PREDEF_TYPE_NULLPTR_ID:
      T = Context.NullPtrTy;
      break;
    case PREDEF_TYPE_CHAR8_ID:
      T = Context.Char8Ty;
      break;
    case PREDEF_TYPE_CHAR16_ID:
      T = Context.Char16Ty;
      break;
    case PREDEF_TYPE_CHAR32_ID:
      T = Context.Char32Ty;
      break;
    case PREDEF_TYPE_OBJC_ID:
      T = Context.ObjCBuiltinIdTy;
      break;
    case PREDEF_TYPE_OBJC_CLASS:
      T = Context.ObjCBuiltinClassTy;
      break;
    case PREDEF_TYPE_OBJC_SEL:
      T = Context.ObjCBuiltinSelTy;
      break;
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
    case PREDEF_TYPE_##Id##_ID: \
      T = Context.SingletonId; \
      break;
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
    case PREDEF_TYPE_##Id##_ID: \
      T = Context.Id##Ty; \
      break;
#include "clang/Basic/OpenCLExtensionTypes.def"
    case PREDEF_TYPE_SAMPLER_ID:
      T = Context.OCLSamplerTy;
      break;
    case PREDEF_TYPE_EVENT_ID:
      T = Context.OCLEventTy;
      break;
    case PREDEF_TYPE_CLK_EVENT_ID:
      T = Context.OCLClkEventTy;
      break;
    case PREDEF_TYPE_QUEUE_ID:
      T = Context.OCLQueueTy;
      break;
    case PREDEF_TYPE_RESERVE_ID_ID:
      T = Context.OCLReserveIDTy;
      break;
    case PREDEF_TYPE_AUTO_DEDUCT:
      T = Context.getAutoDeductType();
      break;
    case PREDEF_TYPE_AUTO_RREF_DEDUCT:
      T = Context.getAutoRRefDeductType();
      break;
    case PREDEF_TYPE_ARC_UNBRIDGED_CAST:
      T = Context.ARCUnbridgedCastTy;
      break;
    case PREDEF_TYPE_BUILTIN_FN:
      T = Context.BuiltinFnTy;
      break;
    case PREDEF_TYPE_OMP_ARRAY_SECTION:
      T = Context.OMPArraySectionTy;
      break;
    }

    assert(!T.isNull() && "Unknown predefined type");
    return T.withFastQualifiers(FastQuals);
  }

  Index -= NUM_PREDEF_TYPE_IDS;
  assert(Index < TypesLoaded.size() && "Type index out-of-range");
  if (TypesLoaded[Index].isNull()) {
    TypesLoaded[Index] = readTypeRecord(Index);
    if (TypesLoaded[Index].isNull())
      return QualType();

    TypesLoaded[Index]->setFromAST();
    if (DeserializationListener)
      DeserializationListener->TypeRead(TypeIdx::fromTypeID(ID),
                                        TypesLoaded[Index]);
  }

  return TypesLoaded[Index].withFastQualifiers(FastQuals);
}

QualType ASTReader::getLocalType(ModuleFile &F, unsigned LocalID) {
  return GetType(getGlobalTypeID(F, LocalID));
}

serialization::TypeID
ASTReader::getGlobalTypeID(ModuleFile &F, unsigned LocalID) const {
  unsigned FastQuals = LocalID & Qualifiers::FastMask;
  unsigned LocalIndex = LocalID >> Qualifiers::FastWidth;

  if (LocalIndex < NUM_PREDEF_TYPE_IDS)
    return LocalID;

  if (!F.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(F);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = F.TypeRemap.find(LocalIndex - NUM_PREDEF_TYPE_IDS);
  assert(I != F.TypeRemap.end() && "Invalid index into type index remap");

  unsigned GlobalIndex = LocalIndex + I->second;
  return (GlobalIndex << Qualifiers::FastWidth) | FastQuals;
}

TemplateArgumentLocInfo
ASTReader::GetTemplateArgumentLocInfo(ModuleFile &F,
                                      TemplateArgument::ArgKind Kind,
                                      const RecordData &Record,
                                      unsigned &Index) {
  switch (Kind) {
  case TemplateArgument::Expression:
    return ReadExpr(F);
  case TemplateArgument::Type:
    return GetTypeSourceInfo(F, Record, Index);
  case TemplateArgument::Template: {
    NestedNameSpecifierLoc QualifierLoc = ReadNestedNameSpecifierLoc(F, Record,
                                                                     Index);
    SourceLocation TemplateNameLoc = ReadSourceLocation(F, Record, Index);
    return TemplateArgumentLocInfo(QualifierLoc, TemplateNameLoc,
                                   SourceLocation());
  }
  case TemplateArgument::TemplateExpansion: {
    NestedNameSpecifierLoc QualifierLoc = ReadNestedNameSpecifierLoc(F, Record,
                                                                     Index);
    SourceLocation TemplateNameLoc = ReadSourceLocation(F, Record, Index);
    SourceLocation EllipsisLoc = ReadSourceLocation(F, Record, Index);
    return TemplateArgumentLocInfo(QualifierLoc, TemplateNameLoc,
                                   EllipsisLoc);
  }
  case TemplateArgument::Null:
  case TemplateArgument::Integral:
  case TemplateArgument::Declaration:
  case TemplateArgument::NullPtr:
  case TemplateArgument::Pack:
    // FIXME: Is this right?
    return TemplateArgumentLocInfo();
  }
  llvm_unreachable("unexpected template argument loc");
}

TemplateArgumentLoc
ASTReader::ReadTemplateArgumentLoc(ModuleFile &F,
                                   const RecordData &Record, unsigned &Index) {
  TemplateArgument Arg = ReadTemplateArgument(F, Record, Index);

  if (Arg.getKind() == TemplateArgument::Expression) {
    if (Record[Index++]) // bool InfoHasSameExpr.
      return TemplateArgumentLoc(Arg, TemplateArgumentLocInfo(Arg.getAsExpr()));
  }
  return TemplateArgumentLoc(Arg, GetTemplateArgumentLocInfo(F, Arg.getKind(),
                                                             Record, Index));
}

const ASTTemplateArgumentListInfo*
ASTReader::ReadASTTemplateArgumentListInfo(ModuleFile &F,
                                           const RecordData &Record,
                                           unsigned &Index) {
  SourceLocation LAngleLoc = ReadSourceLocation(F, Record, Index);
  SourceLocation RAngleLoc = ReadSourceLocation(F, Record, Index);
  unsigned NumArgsAsWritten = Record[Index++];
  TemplateArgumentListInfo TemplArgsInfo(LAngleLoc, RAngleLoc);
  for (unsigned i = 0; i != NumArgsAsWritten; ++i)
    TemplArgsInfo.addArgument(ReadTemplateArgumentLoc(F, Record, Index));
  return ASTTemplateArgumentListInfo::Create(getContext(), TemplArgsInfo);
}

Decl *ASTReader::GetExternalDecl(uint32_t ID) {
  return GetDecl(ID);
}

void ASTReader::CompleteRedeclChain(const Decl *D) {
  if (NumCurrentElementsDeserializing) {
    // We arrange to not care about the complete redeclaration chain while we're
    // deserializing. Just remember that the AST has marked this one as complete
    // but that it's not actually complete yet, so we know we still need to
    // complete it later.
    PendingIncompleteDeclChains.push_back(const_cast<Decl*>(D));
    return;
  }

  const DeclContext *DC = D->getDeclContext()->getRedeclContext();

  // If this is a named declaration, complete it by looking it up
  // within its context.
  //
  // FIXME: Merging a function definition should merge
  // all mergeable entities within it.
  if (isa<TranslationUnitDecl>(DC) || isa<NamespaceDecl>(DC) ||
      isa<CXXRecordDecl>(DC) || isa<EnumDecl>(DC)) {
    if (DeclarationName Name = cast<NamedDecl>(D)->getDeclName()) {
      if (!getContext().getLangOpts().CPlusPlus &&
          isa<TranslationUnitDecl>(DC)) {
        // Outside of C++, we don't have a lookup table for the TU, so update
        // the identifier instead. (For C++ modules, we don't store decls
        // in the serialized identifier table, so we do the lookup in the TU.)
        auto *II = Name.getAsIdentifierInfo();
        assert(II && "non-identifier name in C?");
        if (II->isOutOfDate())
          updateOutOfDateIdentifier(*II);
      } else
        DC->lookup(Name);
    } else if (needsAnonymousDeclarationNumber(cast<NamedDecl>(D))) {
      // Find all declarations of this kind from the relevant context.
      for (auto *DCDecl : cast<Decl>(D->getLexicalDeclContext())->redecls()) {
        auto *DC = cast<DeclContext>(DCDecl);
        SmallVector<Decl*, 8> Decls;
        FindExternalLexicalDecls(
            DC, [&](Decl::Kind K) { return K == D->getKind(); }, Decls);
      }
    }
  }

  if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
    CTSD->getSpecializedTemplate()->LoadLazySpecializations();
  if (auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(D))
    VTSD->getSpecializedTemplate()->LoadLazySpecializations();
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (auto *Template = FD->getPrimaryTemplate())
      Template->LoadLazySpecializations();
  }
}

CXXCtorInitializer **
ASTReader::GetExternalCXXCtorInitializers(uint64_t Offset) {
  RecordLocation Loc = getLocalBitOffset(Offset);
  BitstreamCursor &Cursor = Loc.F->DeclsCursor;
  SavedStreamPosition SavedPosition(Cursor);
  Cursor.JumpToBit(Loc.Offset);
  ReadingKindTracker ReadingKind(Read_Decl, *this);

  RecordData Record;
  unsigned Code = Cursor.ReadCode();
  unsigned RecCode = Cursor.readRecord(Code, Record);
  if (RecCode != DECL_CXX_CTOR_INITIALIZERS) {
    Error("malformed AST file: missing C++ ctor initializers");
    return nullptr;
  }

  unsigned Idx = 0;
  return ReadCXXCtorInitializers(*Loc.F, Record, Idx);
}

CXXBaseSpecifier *ASTReader::GetExternalCXXBaseSpecifiers(uint64_t Offset) {
  assert(ContextObj && "reading base specifiers with no AST context");
  ASTContext &Context = *ContextObj;

  RecordLocation Loc = getLocalBitOffset(Offset);
  BitstreamCursor &Cursor = Loc.F->DeclsCursor;
  SavedStreamPosition SavedPosition(Cursor);
  Cursor.JumpToBit(Loc.Offset);
  ReadingKindTracker ReadingKind(Read_Decl, *this);
  RecordData Record;
  unsigned Code = Cursor.ReadCode();
  unsigned RecCode = Cursor.readRecord(Code, Record);
  if (RecCode != DECL_CXX_BASE_SPECIFIERS) {
    Error("malformed AST file: missing C++ base specifiers");
    return nullptr;
  }

  unsigned Idx = 0;
  unsigned NumBases = Record[Idx++];
  void *Mem = Context.Allocate(sizeof(CXXBaseSpecifier) * NumBases);
  CXXBaseSpecifier *Bases = new (Mem) CXXBaseSpecifier [NumBases];
  for (unsigned I = 0; I != NumBases; ++I)
    Bases[I] = ReadCXXBaseSpecifier(*Loc.F, Record, Idx);
  return Bases;
}

serialization::DeclID
ASTReader::getGlobalDeclID(ModuleFile &F, LocalDeclID LocalID) const {
  if (LocalID < NUM_PREDEF_DECL_IDS)
    return LocalID;

  if (!F.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(F);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = F.DeclRemap.find(LocalID - NUM_PREDEF_DECL_IDS);
  assert(I != F.DeclRemap.end() && "Invalid index into decl index remap");

  return LocalID + I->second;
}

bool ASTReader::isDeclIDFromModule(serialization::GlobalDeclID ID,
                                   ModuleFile &M) const {
  // Predefined decls aren't from any module.
  if (ID < NUM_PREDEF_DECL_IDS)
    return false;

  return ID - NUM_PREDEF_DECL_IDS >= M.BaseDeclID &&
         ID - NUM_PREDEF_DECL_IDS < M.BaseDeclID + M.LocalNumDecls;
}

ModuleFile *ASTReader::getOwningModuleFile(const Decl *D) {
  if (!D->isFromASTFile())
    return nullptr;
  GlobalDeclMapType::const_iterator I = GlobalDeclMap.find(D->getGlobalID());
  assert(I != GlobalDeclMap.end() && "Corrupted global declaration map");
  return I->second;
}

SourceLocation ASTReader::getSourceLocationForDeclID(GlobalDeclID ID) {
  if (ID < NUM_PREDEF_DECL_IDS)
    return SourceLocation();

  unsigned Index = ID - NUM_PREDEF_DECL_IDS;

  if (Index > DeclsLoaded.size()) {
    Error("declaration ID out-of-range for AST file");
    return SourceLocation();
  }

  if (Decl *D = DeclsLoaded[Index])
    return D->getLocation();

  SourceLocation Loc;
  DeclCursorForID(ID, Loc);
  return Loc;
}

static Decl *getPredefinedDecl(ASTContext &Context, PredefinedDeclIDs ID) {
  switch (ID) {
  case PREDEF_DECL_NULL_ID:
    return nullptr;

  case PREDEF_DECL_TRANSLATION_UNIT_ID:
    return Context.getTranslationUnitDecl();

  case PREDEF_DECL_OBJC_ID_ID:
    return Context.getObjCIdDecl();

  case PREDEF_DECL_OBJC_SEL_ID:
    return Context.getObjCSelDecl();

  case PREDEF_DECL_OBJC_CLASS_ID:
    return Context.getObjCClassDecl();

  case PREDEF_DECL_OBJC_PROTOCOL_ID:
    return Context.getObjCProtocolDecl();

  case PREDEF_DECL_INT_128_ID:
    return Context.getInt128Decl();

  case PREDEF_DECL_UNSIGNED_INT_128_ID:
    return Context.getUInt128Decl();

  case PREDEF_DECL_OBJC_INSTANCETYPE_ID:
    return Context.getObjCInstanceTypeDecl();

  case PREDEF_DECL_BUILTIN_VA_LIST_ID:
    return Context.getBuiltinVaListDecl();

  case PREDEF_DECL_VA_LIST_TAG:
    return Context.getVaListTagDecl();

  case PREDEF_DECL_BUILTIN_MS_VA_LIST_ID:
    return Context.getBuiltinMSVaListDecl();

  case PREDEF_DECL_EXTERN_C_CONTEXT_ID:
    return Context.getExternCContextDecl();

  case PREDEF_DECL_MAKE_INTEGER_SEQ_ID:
    return Context.getMakeIntegerSeqDecl();

  case PREDEF_DECL_CF_CONSTANT_STRING_ID:
    return Context.getCFConstantStringDecl();

  case PREDEF_DECL_CF_CONSTANT_STRING_TAG_ID:
    return Context.getCFConstantStringTagDecl();

  case PREDEF_DECL_TYPE_PACK_ELEMENT_ID:
    return Context.getTypePackElementDecl();
  }
  llvm_unreachable("PredefinedDeclIDs unknown enum value");
}

Decl *ASTReader::GetExistingDecl(DeclID ID) {
  assert(ContextObj && "reading decl with no AST context");
  if (ID < NUM_PREDEF_DECL_IDS) {
    Decl *D = getPredefinedDecl(*ContextObj, (PredefinedDeclIDs)ID);
    if (D) {
      // Track that we have merged the declaration with ID \p ID into the
      // pre-existing predefined declaration \p D.
      auto &Merged = KeyDecls[D->getCanonicalDecl()];
      if (Merged.empty())
        Merged.push_back(ID);
    }
    return D;
  }

  unsigned Index = ID - NUM_PREDEF_DECL_IDS;

  if (Index >= DeclsLoaded.size()) {
    assert(0 && "declaration ID out-of-range for AST file");
    Error("declaration ID out-of-range for AST file");
    return nullptr;
  }

  return DeclsLoaded[Index];
}

Decl *ASTReader::GetDecl(DeclID ID) {
  if (ID < NUM_PREDEF_DECL_IDS)
    return GetExistingDecl(ID);

  unsigned Index = ID - NUM_PREDEF_DECL_IDS;

  if (Index >= DeclsLoaded.size()) {
    assert(0 && "declaration ID out-of-range for AST file");
    Error("declaration ID out-of-range for AST file");
    return nullptr;
  }

  if (!DeclsLoaded[Index]) {
    ReadDeclRecord(ID);
    if (DeserializationListener)
      DeserializationListener->DeclRead(ID, DeclsLoaded[Index]);
  }

  return DeclsLoaded[Index];
}

DeclID ASTReader::mapGlobalIDToModuleFileGlobalID(ModuleFile &M,
                                                  DeclID GlobalID) {
  if (GlobalID < NUM_PREDEF_DECL_IDS)
    return GlobalID;

  GlobalDeclMapType::const_iterator I = GlobalDeclMap.find(GlobalID);
  assert(I != GlobalDeclMap.end() && "Corrupted global declaration map");
  ModuleFile *Owner = I->second;

  llvm::DenseMap<ModuleFile *, serialization::DeclID>::iterator Pos
    = M.GlobalToLocalDeclIDs.find(Owner);
  if (Pos == M.GlobalToLocalDeclIDs.end())
    return 0;

  return GlobalID - Owner->BaseDeclID + Pos->second;
}

serialization::DeclID ASTReader::ReadDeclID(ModuleFile &F,
                                            const RecordData &Record,
                                            unsigned &Idx) {
  if (Idx >= Record.size()) {
    Error("Corrupted AST file");
    return 0;
  }

  return getGlobalDeclID(F, Record[Idx++]);
}

/// Resolve the offset of a statement into a statement.
///
/// This operation will read a new statement from the external
/// source each time it is called, and is meant to be used via a
/// LazyOffsetPtr (which is used by Decls for the body of functions, etc).
Stmt *ASTReader::GetExternalDeclStmt(uint64_t Offset) {
  // Switch case IDs are per Decl.
  ClearSwitchCaseIDs();

  // Offset here is a global offset across the entire chain.
  RecordLocation Loc = getLocalBitOffset(Offset);
  Loc.F->DeclsCursor.JumpToBit(Loc.Offset);
  assert(NumCurrentElementsDeserializing == 0 &&
         "should not be called while already deserializing");
  Deserializing D(this);
  return ReadStmtFromStream(*Loc.F);
}

void ASTReader::FindExternalLexicalDecls(
    const DeclContext *DC, llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
    SmallVectorImpl<Decl *> &Decls) {
  bool PredefsVisited[NUM_PREDEF_DECL_IDS] = {};

  auto Visit = [&] (ModuleFile *M, LexicalContents LexicalDecls) {
    assert(LexicalDecls.size() % 2 == 0 && "expected an even number of entries");
    for (int I = 0, N = LexicalDecls.size(); I != N; I += 2) {
      auto K = (Decl::Kind)+LexicalDecls[I];
      if (!IsKindWeWant(K))
        continue;

      auto ID = (serialization::DeclID)+LexicalDecls[I + 1];

      // Don't add predefined declarations to the lexical context more
      // than once.
      if (ID < NUM_PREDEF_DECL_IDS) {
        if (PredefsVisited[ID])
          continue;

        PredefsVisited[ID] = true;
      }

      if (Decl *D = GetLocalDecl(*M, ID)) {
        assert(D->getKind() == K && "wrong kind for lexical decl");
        if (!DC->isDeclInLexicalTraversal(D))
          Decls.push_back(D);
      }
    }
  };

  if (isa<TranslationUnitDecl>(DC)) {
    for (auto Lexical : TULexicalDecls)
      Visit(Lexical.first, Lexical.second);
  } else {
    auto I = LexicalDecls.find(DC);
    if (I != LexicalDecls.end())
      Visit(I->second.first, I->second.second);
  }

  ++NumLexicalDeclContextsRead;
}

namespace {

class DeclIDComp {
  ASTReader &Reader;
  ModuleFile &Mod;

public:
  DeclIDComp(ASTReader &Reader, ModuleFile &M) : Reader(Reader), Mod(M) {}

  bool operator()(LocalDeclID L, LocalDeclID R) const {
    SourceLocation LHS = getLocation(L);
    SourceLocation RHS = getLocation(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(SourceLocation LHS, LocalDeclID R) const {
    SourceLocation RHS = getLocation(R);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  bool operator()(LocalDeclID L, SourceLocation RHS) const {
    SourceLocation LHS = getLocation(L);
    return Reader.getSourceManager().isBeforeInTranslationUnit(LHS, RHS);
  }

  SourceLocation getLocation(LocalDeclID ID) const {
    return Reader.getSourceManager().getFileLoc(
            Reader.getSourceLocationForDeclID(Reader.getGlobalDeclID(Mod, ID)));
  }
};

} // namespace

void ASTReader::FindFileRegionDecls(FileID File,
                                    unsigned Offset, unsigned Length,
                                    SmallVectorImpl<Decl *> &Decls) {
  SourceManager &SM = getSourceManager();

  llvm::DenseMap<FileID, FileDeclsInfo>::iterator I = FileDeclIDs.find(File);
  if (I == FileDeclIDs.end())
    return;

  FileDeclsInfo &DInfo = I->second;
  if (DInfo.Decls.empty())
    return;

  SourceLocation
    BeginLoc = SM.getLocForStartOfFile(File).getLocWithOffset(Offset);
  SourceLocation EndLoc = BeginLoc.getLocWithOffset(Length);

  DeclIDComp DIDComp(*this, *DInfo.Mod);
  ArrayRef<serialization::LocalDeclID>::iterator
    BeginIt = std::lower_bound(DInfo.Decls.begin(), DInfo.Decls.end(),
                               BeginLoc, DIDComp);
  if (BeginIt != DInfo.Decls.begin())
    --BeginIt;

  // If we are pointing at a top-level decl inside an objc container, we need
  // to backtrack until we find it otherwise we will fail to report that the
  // region overlaps with an objc container.
  while (BeginIt != DInfo.Decls.begin() &&
         GetDecl(getGlobalDeclID(*DInfo.Mod, *BeginIt))
             ->isTopLevelDeclInObjCContainer())
    --BeginIt;

  ArrayRef<serialization::LocalDeclID>::iterator
    EndIt = std::upper_bound(DInfo.Decls.begin(), DInfo.Decls.end(),
                             EndLoc, DIDComp);
  if (EndIt != DInfo.Decls.end())
    ++EndIt;

  for (ArrayRef<serialization::LocalDeclID>::iterator
         DIt = BeginIt; DIt != EndIt; ++DIt)
    Decls.push_back(GetDecl(getGlobalDeclID(*DInfo.Mod, *DIt)));
}

bool
ASTReader::FindExternalVisibleDeclsByName(const DeclContext *DC,
                                          DeclarationName Name) {
  assert(DC->hasExternalVisibleStorage() && DC == DC->getPrimaryContext() &&
         "DeclContext has no visible decls in storage");
  if (!Name)
    return false;

  auto It = Lookups.find(DC);
  if (It == Lookups.end())
    return false;

  Deserializing LookupResults(this);

  // Load the list of declarations.
  SmallVector<NamedDecl *, 64> Decls;
  for (DeclID ID : It->second.Table.find(Name)) {
    NamedDecl *ND = cast<NamedDecl>(GetDecl(ID));
    if (ND->getDeclName() == Name)
      Decls.push_back(ND);
  }

  ++NumVisibleDeclContextsRead;
  SetExternalVisibleDeclsForName(DC, Name, Decls);
  return !Decls.empty();
}

void ASTReader::completeVisibleDeclsMap(const DeclContext *DC) {
  if (!DC->hasExternalVisibleStorage())
    return;

  auto It = Lookups.find(DC);
  assert(It != Lookups.end() &&
         "have external visible storage but no lookup tables");

  DeclsMap Decls;

  for (DeclID ID : It->second.Table.findAll()) {
    NamedDecl *ND = cast<NamedDecl>(GetDecl(ID));
    Decls[ND->getDeclName()].push_back(ND);
  }

  ++NumVisibleDeclContextsRead;

  for (DeclsMap::iterator I = Decls.begin(), E = Decls.end(); I != E; ++I) {
    SetExternalVisibleDeclsForName(DC, I->first, I->second);
  }
  const_cast<DeclContext *>(DC)->setHasExternalVisibleStorage(false);
}

const serialization::reader::DeclContextLookupTable *
ASTReader::getLoadedLookupTables(DeclContext *Primary) const {
  auto I = Lookups.find(Primary);
  return I == Lookups.end() ? nullptr : &I->second;
}

/// Under non-PCH compilation the consumer receives the objc methods
/// before receiving the implementation, and codegen depends on this.
/// We simulate this by deserializing and passing to consumer the methods of the
/// implementation before passing the deserialized implementation decl.
static void PassObjCImplDeclToConsumer(ObjCImplDecl *ImplD,
                                       ASTConsumer *Consumer) {
  assert(ImplD && Consumer);

  for (auto *I : ImplD->methods())
    Consumer->HandleInterestingDecl(DeclGroupRef(I));

  Consumer->HandleInterestingDecl(DeclGroupRef(ImplD));
}

void ASTReader::PassInterestingDeclToConsumer(Decl *D) {
  if (ObjCImplDecl *ImplD = dyn_cast<ObjCImplDecl>(D))
    PassObjCImplDeclToConsumer(ImplD, Consumer);
  else
    Consumer->HandleInterestingDecl(DeclGroupRef(D));
}

void ASTReader::StartTranslationUnit(ASTConsumer *Consumer) {
  this->Consumer = Consumer;

  if (Consumer)
    PassInterestingDeclsToConsumer();

  if (DeserializationListener)
    DeserializationListener->ReaderInitialized(this);
}

void ASTReader::PrintStats() {
  std::fprintf(stderr, "*** AST File Statistics:\n");

  unsigned NumTypesLoaded
    = TypesLoaded.size() - std::count(TypesLoaded.begin(), TypesLoaded.end(),
                                      QualType());
  unsigned NumDeclsLoaded
    = DeclsLoaded.size() - std::count(DeclsLoaded.begin(), DeclsLoaded.end(),
                                      (Decl *)nullptr);
  unsigned NumIdentifiersLoaded
    = IdentifiersLoaded.size() - std::count(IdentifiersLoaded.begin(),
                                            IdentifiersLoaded.end(),
                                            (IdentifierInfo *)nullptr);
  unsigned NumMacrosLoaded
    = MacrosLoaded.size() - std::count(MacrosLoaded.begin(),
                                       MacrosLoaded.end(),
                                       (MacroInfo *)nullptr);
  unsigned NumSelectorsLoaded
    = SelectorsLoaded.size() - std::count(SelectorsLoaded.begin(),
                                          SelectorsLoaded.end(),
                                          Selector());

  if (unsigned TotalNumSLocEntries = getTotalNumSLocs())
    std::fprintf(stderr, "  %u/%u source location entries read (%f%%)\n",
                 NumSLocEntriesRead, TotalNumSLocEntries,
                 ((float)NumSLocEntriesRead/TotalNumSLocEntries * 100));
  if (!TypesLoaded.empty())
    std::fprintf(stderr, "  %u/%u types read (%f%%)\n",
                 NumTypesLoaded, (unsigned)TypesLoaded.size(),
                 ((float)NumTypesLoaded/TypesLoaded.size() * 100));
  if (!DeclsLoaded.empty())
    std::fprintf(stderr, "  %u/%u declarations read (%f%%)\n",
                 NumDeclsLoaded, (unsigned)DeclsLoaded.size(),
                 ((float)NumDeclsLoaded/DeclsLoaded.size() * 100));
  if (!IdentifiersLoaded.empty())
    std::fprintf(stderr, "  %u/%u identifiers read (%f%%)\n",
                 NumIdentifiersLoaded, (unsigned)IdentifiersLoaded.size(),
                 ((float)NumIdentifiersLoaded/IdentifiersLoaded.size() * 100));
  if (!MacrosLoaded.empty())
    std::fprintf(stderr, "  %u/%u macros read (%f%%)\n",
                 NumMacrosLoaded, (unsigned)MacrosLoaded.size(),
                 ((float)NumMacrosLoaded/MacrosLoaded.size() * 100));
  if (!SelectorsLoaded.empty())
    std::fprintf(stderr, "  %u/%u selectors read (%f%%)\n",
                 NumSelectorsLoaded, (unsigned)SelectorsLoaded.size(),
                 ((float)NumSelectorsLoaded/SelectorsLoaded.size() * 100));
  if (TotalNumStatements)
    std::fprintf(stderr, "  %u/%u statements read (%f%%)\n",
                 NumStatementsRead, TotalNumStatements,
                 ((float)NumStatementsRead/TotalNumStatements * 100));
  if (TotalNumMacros)
    std::fprintf(stderr, "  %u/%u macros read (%f%%)\n",
                 NumMacrosRead, TotalNumMacros,
                 ((float)NumMacrosRead/TotalNumMacros * 100));
  if (TotalLexicalDeclContexts)
    std::fprintf(stderr, "  %u/%u lexical declcontexts read (%f%%)\n",
                 NumLexicalDeclContextsRead, TotalLexicalDeclContexts,
                 ((float)NumLexicalDeclContextsRead/TotalLexicalDeclContexts
                  * 100));
  if (TotalVisibleDeclContexts)
    std::fprintf(stderr, "  %u/%u visible declcontexts read (%f%%)\n",
                 NumVisibleDeclContextsRead, TotalVisibleDeclContexts,
                 ((float)NumVisibleDeclContextsRead/TotalVisibleDeclContexts
                  * 100));
  if (TotalNumMethodPoolEntries)
    std::fprintf(stderr, "  %u/%u method pool entries read (%f%%)\n",
                 NumMethodPoolEntriesRead, TotalNumMethodPoolEntries,
                 ((float)NumMethodPoolEntriesRead/TotalNumMethodPoolEntries
                  * 100));
  if (NumMethodPoolLookups)
    std::fprintf(stderr, "  %u/%u method pool lookups succeeded (%f%%)\n",
                 NumMethodPoolHits, NumMethodPoolLookups,
                 ((float)NumMethodPoolHits/NumMethodPoolLookups * 100.0));
  if (NumMethodPoolTableLookups)
    std::fprintf(stderr, "  %u/%u method pool table lookups succeeded (%f%%)\n",
                 NumMethodPoolTableHits, NumMethodPoolTableLookups,
                 ((float)NumMethodPoolTableHits/NumMethodPoolTableLookups
                  * 100.0));
  if (NumIdentifierLookupHits)
    std::fprintf(stderr,
                 "  %u / %u identifier table lookups succeeded (%f%%)\n",
                 NumIdentifierLookupHits, NumIdentifierLookups,
                 (double)NumIdentifierLookupHits*100.0/NumIdentifierLookups);

  if (GlobalIndex) {
    std::fprintf(stderr, "\n");
    GlobalIndex->printStats();
  }

  std::fprintf(stderr, "\n");
  dump();
  std::fprintf(stderr, "\n");
}

template<typename Key, typename ModuleFile, unsigned InitialCapacity>
LLVM_DUMP_METHOD static void
dumpModuleIDMap(StringRef Name,
                const ContinuousRangeMap<Key, ModuleFile *,
                                         InitialCapacity> &Map) {
  if (Map.begin() == Map.end())
    return;

  using MapType = ContinuousRangeMap<Key, ModuleFile *, InitialCapacity>;

  llvm::errs() << Name << ":\n";
  for (typename MapType::const_iterator I = Map.begin(), IEnd = Map.end();
       I != IEnd; ++I) {
    llvm::errs() << "  " << I->first << " -> " << I->second->FileName
      << "\n";
  }
}

LLVM_DUMP_METHOD void ASTReader::dump() {
  llvm::errs() << "*** PCH/ModuleFile Remappings:\n";
  dumpModuleIDMap("Global bit offset map", GlobalBitOffsetsMap);
  dumpModuleIDMap("Global source location entry map", GlobalSLocEntryMap);
  dumpModuleIDMap("Global type map", GlobalTypeMap);
  dumpModuleIDMap("Global declaration map", GlobalDeclMap);
  dumpModuleIDMap("Global identifier map", GlobalIdentifierMap);
  dumpModuleIDMap("Global macro map", GlobalMacroMap);
  dumpModuleIDMap("Global submodule map", GlobalSubmoduleMap);
  dumpModuleIDMap("Global selector map", GlobalSelectorMap);
  dumpModuleIDMap("Global preprocessed entity map",
                  GlobalPreprocessedEntityMap);

  llvm::errs() << "\n*** PCH/Modules Loaded:";
  for (ModuleFile &M : ModuleMgr)
    M.dump();
}

/// Return the amount of memory used by memory buffers, breaking down
/// by heap-backed versus mmap'ed memory.
void ASTReader::getMemoryBufferSizes(MemoryBufferSizes &sizes) const {
  for (ModuleFile &I : ModuleMgr) {
    if (llvm::MemoryBuffer *buf = I.Buffer) {
      size_t bytes = buf->getBufferSize();
      switch (buf->getBufferKind()) {
        case llvm::MemoryBuffer::MemoryBuffer_Malloc:
          sizes.malloc_bytes += bytes;
          break;
        case llvm::MemoryBuffer::MemoryBuffer_MMap:
          sizes.mmap_bytes += bytes;
          break;
      }
    }
  }
}

void ASTReader::InitializeSema(Sema &S) {
  SemaObj = &S;
  S.addExternalSource(this);

  // Makes sure any declarations that were deserialized "too early"
  // still get added to the identifier's declaration chains.
  for (uint64_t ID : PreloadedDeclIDs) {
    NamedDecl *D = cast<NamedDecl>(GetDecl(ID));
    pushExternalDeclIntoScope(D, D->getDeclName());
  }
  PreloadedDeclIDs.clear();

  // FIXME: What happens if these are changed by a module import?
  if (!FPPragmaOptions.empty()) {
    assert(FPPragmaOptions.size() == 1 && "Wrong number of FP_PRAGMA_OPTIONS");
    SemaObj->FPFeatures = FPOptions(FPPragmaOptions[0]);
  }

  SemaObj->OpenCLFeatures.copy(OpenCLExtensions);
  SemaObj->OpenCLTypeExtMap = OpenCLTypeExtMap;
  SemaObj->OpenCLDeclExtMap = OpenCLDeclExtMap;

  UpdateSema();
}

void ASTReader::UpdateSema() {
  assert(SemaObj && "no Sema to update");

  // Load the offsets of the declarations that Sema references.
  // They will be lazily deserialized when needed.
  if (!SemaDeclRefs.empty()) {
    assert(SemaDeclRefs.size() % 3 == 0);
    for (unsigned I = 0; I != SemaDeclRefs.size(); I += 3) {
      if (!SemaObj->StdNamespace)
        SemaObj->StdNamespace = SemaDeclRefs[I];
      if (!SemaObj->StdBadAlloc)
        SemaObj->StdBadAlloc = SemaDeclRefs[I+1];
      if (!SemaObj->StdAlignValT)
        SemaObj->StdAlignValT = SemaDeclRefs[I+2];
    }
    SemaDeclRefs.clear();
  }

  // Update the state of pragmas. Use the same API as if we had encountered the
  // pragma in the source.
  if(OptimizeOffPragmaLocation.isValid())
    SemaObj->ActOnPragmaOptimize(/* IsOn = */ false, OptimizeOffPragmaLocation);
  if (PragmaMSStructState != -1)
    SemaObj->ActOnPragmaMSStruct((PragmaMSStructKind)PragmaMSStructState);
  if (PointersToMembersPragmaLocation.isValid()) {
    SemaObj->ActOnPragmaMSPointersToMembers(
        (LangOptions::PragmaMSPointersToMembersKind)
            PragmaMSPointersToMembersState,
        PointersToMembersPragmaLocation);
  }
  SemaObj->ForceCUDAHostDeviceDepth = ForceCUDAHostDeviceDepth;

  if (PragmaPackCurrentValue) {
    // The bottom of the stack might have a default value. It must be adjusted
    // to the current value to ensure that the packing state is preserved after
    // popping entries that were included/imported from a PCH/module.
    bool DropFirst = false;
    if (!PragmaPackStack.empty() &&
        PragmaPackStack.front().Location.isInvalid()) {
      assert(PragmaPackStack.front().Value == SemaObj->PackStack.DefaultValue &&
             "Expected a default alignment value");
      SemaObj->PackStack.Stack.emplace_back(
          PragmaPackStack.front().SlotLabel, SemaObj->PackStack.CurrentValue,
          SemaObj->PackStack.CurrentPragmaLocation,
          PragmaPackStack.front().PushLocation);
      DropFirst = true;
    }
    for (const auto &Entry :
         llvm::makeArrayRef(PragmaPackStack).drop_front(DropFirst ? 1 : 0))
      SemaObj->PackStack.Stack.emplace_back(Entry.SlotLabel, Entry.Value,
                                            Entry.Location, Entry.PushLocation);
    if (PragmaPackCurrentLocation.isInvalid()) {
      assert(*PragmaPackCurrentValue == SemaObj->PackStack.DefaultValue &&
             "Expected a default alignment value");
      // Keep the current values.
    } else {
      SemaObj->PackStack.CurrentValue = *PragmaPackCurrentValue;
      SemaObj->PackStack.CurrentPragmaLocation = PragmaPackCurrentLocation;
    }
  }
}

IdentifierInfo *ASTReader::get(StringRef Name) {
  // Note that we are loading an identifier.
  Deserializing AnIdentifier(this);

  IdentifierLookupVisitor Visitor(Name, /*PriorGeneration=*/0,
                                  NumIdentifierLookups,
                                  NumIdentifierLookupHits);

  // We don't need to do identifier table lookups in C++ modules (we preload
  // all interesting declarations, and don't need to use the scope for name
  // lookups). Perform the lookup in PCH files, though, since we don't build
  // a complete initial identifier table if we're carrying on from a PCH.
  if (PP.getLangOpts().CPlusPlus) {
    for (auto F : ModuleMgr.pch_modules())
      if (Visitor(*F))
        break;
  } else {
    // If there is a global index, look there first to determine which modules
    // provably do not have any results for this identifier.
    GlobalModuleIndex::HitSet Hits;
    GlobalModuleIndex::HitSet *HitsPtr = nullptr;
    if (!loadGlobalIndex()) {
      if (GlobalIndex->lookupIdentifier(Name, Hits)) {
        HitsPtr = &Hits;
      }
    }

    ModuleMgr.visit(Visitor, HitsPtr);
  }

  IdentifierInfo *II = Visitor.getIdentifierInfo();
  markIdentifierUpToDate(II);
  return II;
}

namespace clang {

  /// An identifier-lookup iterator that enumerates all of the
  /// identifiers stored within a set of AST files.
  class ASTIdentifierIterator : public IdentifierIterator {
    /// The AST reader whose identifiers are being enumerated.
    const ASTReader &Reader;

    /// The current index into the chain of AST files stored in
    /// the AST reader.
    unsigned Index;

    /// The current position within the identifier lookup table
    /// of the current AST file.
    ASTIdentifierLookupTable::key_iterator Current;

    /// The end position within the identifier lookup table of
    /// the current AST file.
    ASTIdentifierLookupTable::key_iterator End;

    /// Whether to skip any modules in the ASTReader.
    bool SkipModules;

  public:
    explicit ASTIdentifierIterator(const ASTReader &Reader,
                                   bool SkipModules = false);

    StringRef Next() override;
  };

} // namespace clang

ASTIdentifierIterator::ASTIdentifierIterator(const ASTReader &Reader,
                                             bool SkipModules)
    : Reader(Reader), Index(Reader.ModuleMgr.size()), SkipModules(SkipModules) {
}

StringRef ASTIdentifierIterator::Next() {
  while (Current == End) {
    // If we have exhausted all of our AST files, we're done.
    if (Index == 0)
      return StringRef();

    --Index;
    ModuleFile &F = Reader.ModuleMgr[Index];
    if (SkipModules && F.isModule())
      continue;

    ASTIdentifierLookupTable *IdTable =
        (ASTIdentifierLookupTable *)F.IdentifierLookupTable;
    Current = IdTable->key_begin();
    End = IdTable->key_end();
  }

  // We have any identifiers remaining in the current AST file; return
  // the next one.
  StringRef Result = *Current;
  ++Current;
  return Result;
}

namespace {

/// A utility for appending two IdentifierIterators.
class ChainedIdentifierIterator : public IdentifierIterator {
  std::unique_ptr<IdentifierIterator> Current;
  std::unique_ptr<IdentifierIterator> Queued;

public:
  ChainedIdentifierIterator(std::unique_ptr<IdentifierIterator> First,
                            std::unique_ptr<IdentifierIterator> Second)
      : Current(std::move(First)), Queued(std::move(Second)) {}

  StringRef Next() override {
    if (!Current)
      return StringRef();

    StringRef result = Current->Next();
    if (!result.empty())
      return result;

    // Try the queued iterator, which may itself be empty.
    Current.reset();
    std::swap(Current, Queued);
    return Next();
  }
};

} // namespace

IdentifierIterator *ASTReader::getIdentifiers() {
  if (!loadGlobalIndex()) {
    std::unique_ptr<IdentifierIterator> ReaderIter(
        new ASTIdentifierIterator(*this, /*SkipModules=*/true));
    std::unique_ptr<IdentifierIterator> ModulesIter(
        GlobalIndex->createIdentifierIterator());
    return new ChainedIdentifierIterator(std::move(ReaderIter),
                                         std::move(ModulesIter));
  }

  return new ASTIdentifierIterator(*this);
}

namespace clang {
namespace serialization {

  class ReadMethodPoolVisitor {
    ASTReader &Reader;
    Selector Sel;
    unsigned PriorGeneration;
    unsigned InstanceBits = 0;
    unsigned FactoryBits = 0;
    bool InstanceHasMoreThanOneDecl = false;
    bool FactoryHasMoreThanOneDecl = false;
    SmallVector<ObjCMethodDecl *, 4> InstanceMethods;
    SmallVector<ObjCMethodDecl *, 4> FactoryMethods;

  public:
    ReadMethodPoolVisitor(ASTReader &Reader, Selector Sel,
                          unsigned PriorGeneration)
        : Reader(Reader), Sel(Sel), PriorGeneration(PriorGeneration) {}

    bool operator()(ModuleFile &M) {
      if (!M.SelectorLookupTable)
        return false;

      // If we've already searched this module file, skip it now.
      if (M.Generation <= PriorGeneration)
        return true;

      ++Reader.NumMethodPoolTableLookups;
      ASTSelectorLookupTable *PoolTable
        = (ASTSelectorLookupTable*)M.SelectorLookupTable;
      ASTSelectorLookupTable::iterator Pos = PoolTable->find(Sel);
      if (Pos == PoolTable->end())
        return false;

      ++Reader.NumMethodPoolTableHits;
      ++Reader.NumSelectorsRead;
      // FIXME: Not quite happy with the statistics here. We probably should
      // disable this tracking when called via LoadSelector.
      // Also, should entries without methods count as misses?
      ++Reader.NumMethodPoolEntriesRead;
      ASTSelectorLookupTrait::data_type Data = *Pos;
      if (Reader.DeserializationListener)
        Reader.DeserializationListener->SelectorRead(Data.ID, Sel);

      InstanceMethods.append(Data.Instance.begin(), Data.Instance.end());
      FactoryMethods.append(Data.Factory.begin(), Data.Factory.end());
      InstanceBits = Data.InstanceBits;
      FactoryBits = Data.FactoryBits;
      InstanceHasMoreThanOneDecl = Data.InstanceHasMoreThanOneDecl;
      FactoryHasMoreThanOneDecl = Data.FactoryHasMoreThanOneDecl;
      return true;
    }

    /// Retrieve the instance methods found by this visitor.
    ArrayRef<ObjCMethodDecl *> getInstanceMethods() const {
      return InstanceMethods;
    }

    /// Retrieve the instance methods found by this visitor.
    ArrayRef<ObjCMethodDecl *> getFactoryMethods() const {
      return FactoryMethods;
    }

    unsigned getInstanceBits() const { return InstanceBits; }
    unsigned getFactoryBits() const { return FactoryBits; }

    bool instanceHasMoreThanOneDecl() const {
      return InstanceHasMoreThanOneDecl;
    }

    bool factoryHasMoreThanOneDecl() const { return FactoryHasMoreThanOneDecl; }
  };

} // namespace serialization
} // namespace clang

/// Add the given set of methods to the method list.
static void addMethodsToPool(Sema &S, ArrayRef<ObjCMethodDecl *> Methods,
                             ObjCMethodList &List) {
  for (unsigned I = 0, N = Methods.size(); I != N; ++I) {
    S.addMethodToGlobalList(&List, Methods[I]);
  }
}

void ASTReader::ReadMethodPool(Selector Sel) {
  // Get the selector generation and update it to the current generation.
  unsigned &Generation = SelectorGeneration[Sel];
  unsigned PriorGeneration = Generation;
  Generation = getGeneration();
  SelectorOutOfDate[Sel] = false;

  // Search for methods defined with this selector.
  ++NumMethodPoolLookups;
  ReadMethodPoolVisitor Visitor(*this, Sel, PriorGeneration);
  ModuleMgr.visit(Visitor);

  if (Visitor.getInstanceMethods().empty() &&
      Visitor.getFactoryMethods().empty())
    return;

  ++NumMethodPoolHits;

  if (!getSema())
    return;

  Sema &S = *getSema();
  Sema::GlobalMethodPool::iterator Pos
    = S.MethodPool.insert(std::make_pair(Sel, Sema::GlobalMethods())).first;

  Pos->second.first.setBits(Visitor.getInstanceBits());
  Pos->second.first.setHasMoreThanOneDecl(Visitor.instanceHasMoreThanOneDecl());
  Pos->second.second.setBits(Visitor.getFactoryBits());
  Pos->second.second.setHasMoreThanOneDecl(Visitor.factoryHasMoreThanOneDecl());

  // Add methods to the global pool *after* setting hasMoreThanOneDecl, since
  // when building a module we keep every method individually and may need to
  // update hasMoreThanOneDecl as we add the methods.
  addMethodsToPool(S, Visitor.getInstanceMethods(), Pos->second.first);
  addMethodsToPool(S, Visitor.getFactoryMethods(), Pos->second.second);
}

void ASTReader::updateOutOfDateSelector(Selector Sel) {
  if (SelectorOutOfDate[Sel])
    ReadMethodPool(Sel);
}

void ASTReader::ReadKnownNamespaces(
                          SmallVectorImpl<NamespaceDecl *> &Namespaces) {
  Namespaces.clear();

  for (unsigned I = 0, N = KnownNamespaces.size(); I != N; ++I) {
    if (NamespaceDecl *Namespace
                = dyn_cast_or_null<NamespaceDecl>(GetDecl(KnownNamespaces[I])))
      Namespaces.push_back(Namespace);
  }
}

void ASTReader::ReadUndefinedButUsed(
    llvm::MapVector<NamedDecl *, SourceLocation> &Undefined) {
  for (unsigned Idx = 0, N = UndefinedButUsed.size(); Idx != N;) {
    NamedDecl *D = cast<NamedDecl>(GetDecl(UndefinedButUsed[Idx++]));
    SourceLocation Loc =
        SourceLocation::getFromRawEncoding(UndefinedButUsed[Idx++]);
    Undefined.insert(std::make_pair(D, Loc));
  }
}

void ASTReader::ReadMismatchingDeleteExpressions(llvm::MapVector<
    FieldDecl *, llvm::SmallVector<std::pair<SourceLocation, bool>, 4>> &
                                                     Exprs) {
  for (unsigned Idx = 0, N = DelayedDeleteExprs.size(); Idx != N;) {
    FieldDecl *FD = cast<FieldDecl>(GetDecl(DelayedDeleteExprs[Idx++]));
    uint64_t Count = DelayedDeleteExprs[Idx++];
    for (uint64_t C = 0; C < Count; ++C) {
      SourceLocation DeleteLoc =
          SourceLocation::getFromRawEncoding(DelayedDeleteExprs[Idx++]);
      const bool IsArrayForm = DelayedDeleteExprs[Idx++];
      Exprs[FD].push_back(std::make_pair(DeleteLoc, IsArrayForm));
    }
  }
}

void ASTReader::ReadTentativeDefinitions(
                  SmallVectorImpl<VarDecl *> &TentativeDefs) {
  for (unsigned I = 0, N = TentativeDefinitions.size(); I != N; ++I) {
    VarDecl *Var = dyn_cast_or_null<VarDecl>(GetDecl(TentativeDefinitions[I]));
    if (Var)
      TentativeDefs.push_back(Var);
  }
  TentativeDefinitions.clear();
}

void ASTReader::ReadUnusedFileScopedDecls(
                               SmallVectorImpl<const DeclaratorDecl *> &Decls) {
  for (unsigned I = 0, N = UnusedFileScopedDecls.size(); I != N; ++I) {
    DeclaratorDecl *D
      = dyn_cast_or_null<DeclaratorDecl>(GetDecl(UnusedFileScopedDecls[I]));
    if (D)
      Decls.push_back(D);
  }
  UnusedFileScopedDecls.clear();
}

void ASTReader::ReadDelegatingConstructors(
                                 SmallVectorImpl<CXXConstructorDecl *> &Decls) {
  for (unsigned I = 0, N = DelegatingCtorDecls.size(); I != N; ++I) {
    CXXConstructorDecl *D
      = dyn_cast_or_null<CXXConstructorDecl>(GetDecl(DelegatingCtorDecls[I]));
    if (D)
      Decls.push_back(D);
  }
  DelegatingCtorDecls.clear();
}

void ASTReader::ReadExtVectorDecls(SmallVectorImpl<TypedefNameDecl *> &Decls) {
  for (unsigned I = 0, N = ExtVectorDecls.size(); I != N; ++I) {
    TypedefNameDecl *D
      = dyn_cast_or_null<TypedefNameDecl>(GetDecl(ExtVectorDecls[I]));
    if (D)
      Decls.push_back(D);
  }
  ExtVectorDecls.clear();
}

void ASTReader::ReadUnusedLocalTypedefNameCandidates(
    llvm::SmallSetVector<const TypedefNameDecl *, 4> &Decls) {
  for (unsigned I = 0, N = UnusedLocalTypedefNameCandidates.size(); I != N;
       ++I) {
    TypedefNameDecl *D = dyn_cast_or_null<TypedefNameDecl>(
        GetDecl(UnusedLocalTypedefNameCandidates[I]));
    if (D)
      Decls.insert(D);
  }
  UnusedLocalTypedefNameCandidates.clear();
}

void ASTReader::ReadReferencedSelectors(
       SmallVectorImpl<std::pair<Selector, SourceLocation>> &Sels) {
  if (ReferencedSelectorsData.empty())
    return;

  // If there are @selector references added them to its pool. This is for
  // implementation of -Wselector.
  unsigned int DataSize = ReferencedSelectorsData.size()-1;
  unsigned I = 0;
  while (I < DataSize) {
    Selector Sel = DecodeSelector(ReferencedSelectorsData[I++]);
    SourceLocation SelLoc
      = SourceLocation::getFromRawEncoding(ReferencedSelectorsData[I++]);
    Sels.push_back(std::make_pair(Sel, SelLoc));
  }
  ReferencedSelectorsData.clear();
}

void ASTReader::ReadWeakUndeclaredIdentifiers(
       SmallVectorImpl<std::pair<IdentifierInfo *, WeakInfo>> &WeakIDs) {
  if (WeakUndeclaredIdentifiers.empty())
    return;

  for (unsigned I = 0, N = WeakUndeclaredIdentifiers.size(); I < N; /*none*/) {
    IdentifierInfo *WeakId
      = DecodeIdentifierInfo(WeakUndeclaredIdentifiers[I++]);
    IdentifierInfo *AliasId
      = DecodeIdentifierInfo(WeakUndeclaredIdentifiers[I++]);
    SourceLocation Loc
      = SourceLocation::getFromRawEncoding(WeakUndeclaredIdentifiers[I++]);
    bool Used = WeakUndeclaredIdentifiers[I++];
    WeakInfo WI(AliasId, Loc);
    WI.setUsed(Used);
    WeakIDs.push_back(std::make_pair(WeakId, WI));
  }
  WeakUndeclaredIdentifiers.clear();
}

void ASTReader::ReadUsedVTables(SmallVectorImpl<ExternalVTableUse> &VTables) {
  for (unsigned Idx = 0, N = VTableUses.size(); Idx < N; /* In loop */) {
    ExternalVTableUse VT;
    VT.Record = dyn_cast_or_null<CXXRecordDecl>(GetDecl(VTableUses[Idx++]));
    VT.Location = SourceLocation::getFromRawEncoding(VTableUses[Idx++]);
    VT.DefinitionRequired = VTableUses[Idx++];
    VTables.push_back(VT);
  }

  VTableUses.clear();
}

void ASTReader::ReadPendingInstantiations(
       SmallVectorImpl<std::pair<ValueDecl *, SourceLocation>> &Pending) {
  for (unsigned Idx = 0, N = PendingInstantiations.size(); Idx < N;) {
    ValueDecl *D = cast<ValueDecl>(GetDecl(PendingInstantiations[Idx++]));
    SourceLocation Loc
      = SourceLocation::getFromRawEncoding(PendingInstantiations[Idx++]);

    Pending.push_back(std::make_pair(D, Loc));
  }
  PendingInstantiations.clear();
}

void ASTReader::ReadLateParsedTemplates(
    llvm::MapVector<const FunctionDecl *, std::unique_ptr<LateParsedTemplate>>
        &LPTMap) {
  for (unsigned Idx = 0, N = LateParsedTemplates.size(); Idx < N;
       /* In loop */) {
    FunctionDecl *FD = cast<FunctionDecl>(GetDecl(LateParsedTemplates[Idx++]));

    auto LT = llvm::make_unique<LateParsedTemplate>();
    LT->D = GetDecl(LateParsedTemplates[Idx++]);

    ModuleFile *F = getOwningModuleFile(LT->D);
    assert(F && "No module");

    unsigned TokN = LateParsedTemplates[Idx++];
    LT->Toks.reserve(TokN);
    for (unsigned T = 0; T < TokN; ++T)
      LT->Toks.push_back(ReadToken(*F, LateParsedTemplates, Idx));

    LPTMap.insert(std::make_pair(FD, std::move(LT)));
  }

  LateParsedTemplates.clear();
}

void ASTReader::LoadSelector(Selector Sel) {
  // It would be complicated to avoid reading the methods anyway. So don't.
  ReadMethodPool(Sel);
}

void ASTReader::SetIdentifierInfo(IdentifierID ID, IdentifierInfo *II) {
  assert(ID && "Non-zero identifier ID required");
  assert(ID <= IdentifiersLoaded.size() && "identifier ID out of range");
  IdentifiersLoaded[ID - 1] = II;
  if (DeserializationListener)
    DeserializationListener->IdentifierRead(ID, II);
}

/// Set the globally-visible declarations associated with the given
/// identifier.
///
/// If the AST reader is currently in a state where the given declaration IDs
/// cannot safely be resolved, they are queued until it is safe to resolve
/// them.
///
/// \param II an IdentifierInfo that refers to one or more globally-visible
/// declarations.
///
/// \param DeclIDs the set of declaration IDs with the name @p II that are
/// visible at global scope.
///
/// \param Decls if non-null, this vector will be populated with the set of
/// deserialized declarations. These declarations will not be pushed into
/// scope.
void
ASTReader::SetGloballyVisibleDecls(IdentifierInfo *II,
                              const SmallVectorImpl<uint32_t> &DeclIDs,
                                   SmallVectorImpl<Decl *> *Decls) {
  if (NumCurrentElementsDeserializing && !Decls) {
    PendingIdentifierInfos[II].append(DeclIDs.begin(), DeclIDs.end());
    return;
  }

  for (unsigned I = 0, N = DeclIDs.size(); I != N; ++I) {
    if (!SemaObj) {
      // Queue this declaration so that it will be added to the
      // translation unit scope and identifier's declaration chain
      // once a Sema object is known.
      PreloadedDeclIDs.push_back(DeclIDs[I]);
      continue;
    }

    NamedDecl *D = cast<NamedDecl>(GetDecl(DeclIDs[I]));

    // If we're simply supposed to record the declarations, do so now.
    if (Decls) {
      Decls->push_back(D);
      continue;
    }

    // Introduce this declaration into the translation-unit scope
    // and add it to the declaration chain for this identifier, so
    // that (unqualified) name lookup will find it.
    pushExternalDeclIntoScope(D, II);
  }
}

IdentifierInfo *ASTReader::DecodeIdentifierInfo(IdentifierID ID) {
  if (ID == 0)
    return nullptr;

  if (IdentifiersLoaded.empty()) {
    Error("no identifier table in AST file");
    return nullptr;
  }

  ID -= 1;
  if (!IdentifiersLoaded[ID]) {
    GlobalIdentifierMapType::iterator I = GlobalIdentifierMap.find(ID + 1);
    assert(I != GlobalIdentifierMap.end() && "Corrupted global identifier map");
    ModuleFile *M = I->second;
    unsigned Index = ID - M->BaseIdentifierID;
    const char *Str = M->IdentifierTableData + M->IdentifierOffsets[Index];

    // All of the strings in the AST file are preceded by a 16-bit length.
    // Extract that 16-bit length to avoid having to execute strlen().
    // NOTE: 'StrLenPtr' is an 'unsigned char*' so that we load bytes as
    //  unsigned integers.  This is important to avoid integer overflow when
    //  we cast them to 'unsigned'.
    const unsigned char *StrLenPtr = (const unsigned char*) Str - 2;
    unsigned StrLen = (((unsigned) StrLenPtr[0])
                       | (((unsigned) StrLenPtr[1]) << 8)) - 1;
    auto &II = PP.getIdentifierTable().get(StringRef(Str, StrLen));
    IdentifiersLoaded[ID] = &II;
    markIdentifierFromAST(*this,  II);
    if (DeserializationListener)
      DeserializationListener->IdentifierRead(ID + 1, &II);
  }

  return IdentifiersLoaded[ID];
}

IdentifierInfo *ASTReader::getLocalIdentifier(ModuleFile &M, unsigned LocalID) {
  return DecodeIdentifierInfo(getGlobalIdentifierID(M, LocalID));
}

IdentifierID ASTReader::getGlobalIdentifierID(ModuleFile &M, unsigned LocalID) {
  if (LocalID < NUM_PREDEF_IDENT_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = M.IdentifierRemap.find(LocalID - NUM_PREDEF_IDENT_IDS);
  assert(I != M.IdentifierRemap.end()
         && "Invalid index into identifier index remap");

  return LocalID + I->second;
}

MacroInfo *ASTReader::getMacro(MacroID ID) {
  if (ID == 0)
    return nullptr;

  if (MacrosLoaded.empty()) {
    Error("no macro table in AST file");
    return nullptr;
  }

  ID -= NUM_PREDEF_MACRO_IDS;
  if (!MacrosLoaded[ID]) {
    GlobalMacroMapType::iterator I
      = GlobalMacroMap.find(ID + NUM_PREDEF_MACRO_IDS);
    assert(I != GlobalMacroMap.end() && "Corrupted global macro map");
    ModuleFile *M = I->second;
    unsigned Index = ID - M->BaseMacroID;
    MacrosLoaded[ID] = ReadMacroRecord(*M, M->MacroOffsets[Index]);

    if (DeserializationListener)
      DeserializationListener->MacroRead(ID + NUM_PREDEF_MACRO_IDS,
                                         MacrosLoaded[ID]);
  }

  return MacrosLoaded[ID];
}

MacroID ASTReader::getGlobalMacroID(ModuleFile &M, unsigned LocalID) {
  if (LocalID < NUM_PREDEF_MACRO_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = M.MacroRemap.find(LocalID - NUM_PREDEF_MACRO_IDS);
  assert(I != M.MacroRemap.end() && "Invalid index into macro index remap");

  return LocalID + I->second;
}

serialization::SubmoduleID
ASTReader::getGlobalSubmoduleID(ModuleFile &M, unsigned LocalID) {
  if (LocalID < NUM_PREDEF_SUBMODULE_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = M.SubmoduleRemap.find(LocalID - NUM_PREDEF_SUBMODULE_IDS);
  assert(I != M.SubmoduleRemap.end()
         && "Invalid index into submodule index remap");

  return LocalID + I->second;
}

Module *ASTReader::getSubmodule(SubmoduleID GlobalID) {
  if (GlobalID < NUM_PREDEF_SUBMODULE_IDS) {
    assert(GlobalID == 0 && "Unhandled global submodule ID");
    return nullptr;
  }

  if (GlobalID > SubmodulesLoaded.size()) {
    Error("submodule ID out of range in AST file");
    return nullptr;
  }

  return SubmodulesLoaded[GlobalID - NUM_PREDEF_SUBMODULE_IDS];
}

Module *ASTReader::getModule(unsigned ID) {
  return getSubmodule(ID);
}

bool ASTReader::DeclIsFromPCHWithObjectFile(const Decl *D) {
  ModuleFile *MF = getOwningModuleFile(D);
  return MF && MF->PCHHasObjectFile;
}

ModuleFile *ASTReader::getLocalModuleFile(ModuleFile &F, unsigned ID) {
  if (ID & 1) {
    // It's a module, look it up by submodule ID.
    auto I = GlobalSubmoduleMap.find(getGlobalSubmoduleID(F, ID >> 1));
    return I == GlobalSubmoduleMap.end() ? nullptr : I->second;
  } else {
    // It's a prefix (preamble, PCH, ...). Look it up by index.
    unsigned IndexFromEnd = ID >> 1;
    assert(IndexFromEnd && "got reference to unknown module file");
    return getModuleManager().pch_modules().end()[-IndexFromEnd];
  }
}

unsigned ASTReader::getModuleFileID(ModuleFile *F) {
  if (!F)
    return 1;

  // For a file representing a module, use the submodule ID of the top-level
  // module as the file ID. For any other kind of file, the number of such
  // files loaded beforehand will be the same on reload.
  // FIXME: Is this true even if we have an explicit module file and a PCH?
  if (F->isModule())
    return ((F->BaseSubmoduleID + NUM_PREDEF_SUBMODULE_IDS) << 1) | 1;

  auto PCHModules = getModuleManager().pch_modules();
  auto I = std::find(PCHModules.begin(), PCHModules.end(), F);
  assert(I != PCHModules.end() && "emitting reference to unknown file");
  return (I - PCHModules.end()) << 1;
}

llvm::Optional<ExternalASTSource::ASTSourceDescriptor>
ASTReader::getSourceDescriptor(unsigned ID) {
  if (const Module *M = getSubmodule(ID))
    return ExternalASTSource::ASTSourceDescriptor(*M);

  // If there is only a single PCH, return it instead.
  // Chained PCH are not supported.
  const auto &PCHChain = ModuleMgr.pch_modules();
  if (std::distance(std::begin(PCHChain), std::end(PCHChain))) {
    ModuleFile &MF = ModuleMgr.getPrimaryModule();
    StringRef ModuleName = llvm::sys::path::filename(MF.OriginalSourceFileName);
    StringRef FileName = llvm::sys::path::filename(MF.FileName);
    return ASTReader::ASTSourceDescriptor(ModuleName, MF.OriginalDir, FileName,
                                          MF.Signature);
  }
  return None;
}

ExternalASTSource::ExtKind ASTReader::hasExternalDefinitions(const Decl *FD) {
  auto I = DefinitionSource.find(FD);
  if (I == DefinitionSource.end())
    return EK_ReplyHazy;
  return I->second ? EK_Never : EK_Always;
}

Selector ASTReader::getLocalSelector(ModuleFile &M, unsigned LocalID) {
  return DecodeSelector(getGlobalSelectorID(M, LocalID));
}

Selector ASTReader::DecodeSelector(serialization::SelectorID ID) {
  if (ID == 0)
    return Selector();

  if (ID > SelectorsLoaded.size()) {
    Error("selector ID out of range in AST file");
    return Selector();
  }

  if (SelectorsLoaded[ID - 1].getAsOpaquePtr() == nullptr) {
    // Load this selector from the selector table.
    GlobalSelectorMapType::iterator I = GlobalSelectorMap.find(ID);
    assert(I != GlobalSelectorMap.end() && "Corrupted global selector map");
    ModuleFile &M = *I->second;
    ASTSelectorLookupTrait Trait(*this, M);
    unsigned Idx = ID - M.BaseSelectorID - NUM_PREDEF_SELECTOR_IDS;
    SelectorsLoaded[ID - 1] =
      Trait.ReadKey(M.SelectorLookupTableData + M.SelectorOffsets[Idx], 0);
    if (DeserializationListener)
      DeserializationListener->SelectorRead(ID, SelectorsLoaded[ID - 1]);
  }

  return SelectorsLoaded[ID - 1];
}

Selector ASTReader::GetExternalSelector(serialization::SelectorID ID) {
  return DecodeSelector(ID);
}

uint32_t ASTReader::GetNumExternalSelectors() {
  // ID 0 (the null selector) is considered an external selector.
  return getTotalNumSelectors() + 1;
}

serialization::SelectorID
ASTReader::getGlobalSelectorID(ModuleFile &M, unsigned LocalID) const {
  if (LocalID < NUM_PREDEF_SELECTOR_IDS)
    return LocalID;

  if (!M.ModuleOffsetMap.empty())
    ReadModuleOffsetMap(M);

  ContinuousRangeMap<uint32_t, int, 2>::iterator I
    = M.SelectorRemap.find(LocalID - NUM_PREDEF_SELECTOR_IDS);
  assert(I != M.SelectorRemap.end()
         && "Invalid index into selector index remap");

  return LocalID + I->second;
}

DeclarationName
ASTReader::ReadDeclarationName(ModuleFile &F,
                               const RecordData &Record, unsigned &Idx) {
  ASTContext &Context = getContext();
  DeclarationName::NameKind Kind = (DeclarationName::NameKind)Record[Idx++];
  switch (Kind) {
  case DeclarationName::Identifier:
    return DeclarationName(GetIdentifierInfo(F, Record, Idx));

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    return DeclarationName(ReadSelector(F, Record, Idx));

  case DeclarationName::CXXConstructorName:
    return Context.DeclarationNames.getCXXConstructorName(
                          Context.getCanonicalType(readType(F, Record, Idx)));

  case DeclarationName::CXXDestructorName:
    return Context.DeclarationNames.getCXXDestructorName(
                          Context.getCanonicalType(readType(F, Record, Idx)));

  case DeclarationName::CXXDeductionGuideName:
    return Context.DeclarationNames.getCXXDeductionGuideName(
                          ReadDeclAs<TemplateDecl>(F, Record, Idx));

  case DeclarationName::CXXConversionFunctionName:
    return Context.DeclarationNames.getCXXConversionFunctionName(
                          Context.getCanonicalType(readType(F, Record, Idx)));

  case DeclarationName::CXXOperatorName:
    return Context.DeclarationNames.getCXXOperatorName(
                                       (OverloadedOperatorKind)Record[Idx++]);

  case DeclarationName::CXXLiteralOperatorName:
    return Context.DeclarationNames.getCXXLiteralOperatorName(
                                       GetIdentifierInfo(F, Record, Idx));

  case DeclarationName::CXXUsingDirective:
    return DeclarationName::getUsingDirectiveName();
  }

  llvm_unreachable("Invalid NameKind!");
}

void ASTReader::ReadDeclarationNameLoc(ModuleFile &F,
                                       DeclarationNameLoc &DNLoc,
                                       DeclarationName Name,
                                      const RecordData &Record, unsigned &Idx) {
  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    DNLoc.NamedType.TInfo = GetTypeSourceInfo(F, Record, Idx);
    break;

  case DeclarationName::CXXOperatorName:
    DNLoc.CXXOperatorName.BeginOpNameLoc
        = ReadSourceLocation(F, Record, Idx).getRawEncoding();
    DNLoc.CXXOperatorName.EndOpNameLoc
        = ReadSourceLocation(F, Record, Idx).getRawEncoding();
    break;

  case DeclarationName::CXXLiteralOperatorName:
    DNLoc.CXXLiteralOperatorName.OpNameLoc
        = ReadSourceLocation(F, Record, Idx).getRawEncoding();
    break;

  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    break;
  }
}

void ASTReader::ReadDeclarationNameInfo(ModuleFile &F,
                                        DeclarationNameInfo &NameInfo,
                                      const RecordData &Record, unsigned &Idx) {
  NameInfo.setName(ReadDeclarationName(F, Record, Idx));
  NameInfo.setLoc(ReadSourceLocation(F, Record, Idx));
  DeclarationNameLoc DNLoc;
  ReadDeclarationNameLoc(F, DNLoc, NameInfo.getName(), Record, Idx);
  NameInfo.setInfo(DNLoc);
}

void ASTReader::ReadQualifierInfo(ModuleFile &F, QualifierInfo &Info,
                                  const RecordData &Record, unsigned &Idx) {
  Info.QualifierLoc = ReadNestedNameSpecifierLoc(F, Record, Idx);
  unsigned NumTPLists = Record[Idx++];
  Info.NumTemplParamLists = NumTPLists;
  if (NumTPLists) {
    Info.TemplParamLists =
        new (getContext()) TemplateParameterList *[NumTPLists];
    for (unsigned i = 0; i != NumTPLists; ++i)
      Info.TemplParamLists[i] = ReadTemplateParameterList(F, Record, Idx);
  }
}

TemplateName
ASTReader::ReadTemplateName(ModuleFile &F, const RecordData &Record,
                            unsigned &Idx) {
  ASTContext &Context = getContext();
  TemplateName::NameKind Kind = (TemplateName::NameKind)Record[Idx++];
  switch (Kind) {
  case TemplateName::Template:
      return TemplateName(ReadDeclAs<TemplateDecl>(F, Record, Idx));

  case TemplateName::OverloadedTemplate: {
    unsigned size = Record[Idx++];
    UnresolvedSet<8> Decls;
    while (size--)
      Decls.addDecl(ReadDeclAs<NamedDecl>(F, Record, Idx));

    return Context.getOverloadedTemplateName(Decls.begin(), Decls.end());
  }

  case TemplateName::QualifiedTemplate: {
    NestedNameSpecifier *NNS = ReadNestedNameSpecifier(F, Record, Idx);
    bool hasTemplKeyword = Record[Idx++];
    TemplateDecl *Template = ReadDeclAs<TemplateDecl>(F, Record, Idx);
    return Context.getQualifiedTemplateName(NNS, hasTemplKeyword, Template);
  }

  case TemplateName::DependentTemplate: {
    NestedNameSpecifier *NNS = ReadNestedNameSpecifier(F, Record, Idx);
    if (Record[Idx++])  // isIdentifier
      return Context.getDependentTemplateName(NNS,
                                               GetIdentifierInfo(F, Record,
                                                                 Idx));
    return Context.getDependentTemplateName(NNS,
                                         (OverloadedOperatorKind)Record[Idx++]);
  }

  case TemplateName::SubstTemplateTemplateParm: {
    TemplateTemplateParmDecl *param
      = ReadDeclAs<TemplateTemplateParmDecl>(F, Record, Idx);
    if (!param) return TemplateName();
    TemplateName replacement = ReadTemplateName(F, Record, Idx);
    return Context.getSubstTemplateTemplateParm(param, replacement);
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    TemplateTemplateParmDecl *Param
      = ReadDeclAs<TemplateTemplateParmDecl>(F, Record, Idx);
    if (!Param)
      return TemplateName();

    TemplateArgument ArgPack = ReadTemplateArgument(F, Record, Idx);
    if (ArgPack.getKind() != TemplateArgument::Pack)
      return TemplateName();

    return Context.getSubstTemplateTemplateParmPack(Param, ArgPack);
  }
  }

  llvm_unreachable("Unhandled template name kind!");
}

TemplateArgument ASTReader::ReadTemplateArgument(ModuleFile &F,
                                                 const RecordData &Record,
                                                 unsigned &Idx,
                                                 bool Canonicalize) {
  ASTContext &Context = getContext();
  if (Canonicalize) {
    // The caller wants a canonical template argument. Sometimes the AST only
    // wants template arguments in canonical form (particularly as the template
    // argument lists of template specializations) so ensure we preserve that
    // canonical form across serialization.
    TemplateArgument Arg = ReadTemplateArgument(F, Record, Idx, false);
    return Context.getCanonicalTemplateArgument(Arg);
  }

  TemplateArgument::ArgKind Kind = (TemplateArgument::ArgKind)Record[Idx++];
  switch (Kind) {
  case TemplateArgument::Null:
    return TemplateArgument();
  case TemplateArgument::Type:
    return TemplateArgument(readType(F, Record, Idx));
  case TemplateArgument::Declaration: {
    ValueDecl *D = ReadDeclAs<ValueDecl>(F, Record, Idx);
    return TemplateArgument(D, readType(F, Record, Idx));
  }
  case TemplateArgument::NullPtr:
    return TemplateArgument(readType(F, Record, Idx), /*isNullPtr*/true);
  case TemplateArgument::Integral: {
    llvm::APSInt Value = ReadAPSInt(Record, Idx);
    QualType T = readType(F, Record, Idx);
    return TemplateArgument(Context, Value, T);
  }
  case TemplateArgument::Template:
    return TemplateArgument(ReadTemplateName(F, Record, Idx));
  case TemplateArgument::TemplateExpansion: {
    TemplateName Name = ReadTemplateName(F, Record, Idx);
    Optional<unsigned> NumTemplateExpansions;
    if (unsigned NumExpansions = Record[Idx++])
      NumTemplateExpansions = NumExpansions - 1;
    return TemplateArgument(Name, NumTemplateExpansions);
  }
  case TemplateArgument::Expression:
    return TemplateArgument(ReadExpr(F));
  case TemplateArgument::Pack: {
    unsigned NumArgs = Record[Idx++];
    TemplateArgument *Args = new (Context) TemplateArgument[NumArgs];
    for (unsigned I = 0; I != NumArgs; ++I)
      Args[I] = ReadTemplateArgument(F, Record, Idx);
    return TemplateArgument(llvm::makeArrayRef(Args, NumArgs));
  }
  }

  llvm_unreachable("Unhandled template argument kind!");
}

TemplateParameterList *
ASTReader::ReadTemplateParameterList(ModuleFile &F,
                                     const RecordData &Record, unsigned &Idx) {
  SourceLocation TemplateLoc = ReadSourceLocation(F, Record, Idx);
  SourceLocation LAngleLoc = ReadSourceLocation(F, Record, Idx);
  SourceLocation RAngleLoc = ReadSourceLocation(F, Record, Idx);

  unsigned NumParams = Record[Idx++];
  SmallVector<NamedDecl *, 16> Params;
  Params.reserve(NumParams);
  while (NumParams--)
    Params.push_back(ReadDeclAs<NamedDecl>(F, Record, Idx));

  // TODO: Concepts
  TemplateParameterList *TemplateParams = TemplateParameterList::Create(
      getContext(), TemplateLoc, LAngleLoc, Params, RAngleLoc, nullptr);
  return TemplateParams;
}

void
ASTReader::
ReadTemplateArgumentList(SmallVectorImpl<TemplateArgument> &TemplArgs,
                         ModuleFile &F, const RecordData &Record,
                         unsigned &Idx, bool Canonicalize) {
  unsigned NumTemplateArgs = Record[Idx++];
  TemplArgs.reserve(NumTemplateArgs);
  while (NumTemplateArgs--)
    TemplArgs.push_back(ReadTemplateArgument(F, Record, Idx, Canonicalize));
}

/// Read a UnresolvedSet structure.
void ASTReader::ReadUnresolvedSet(ModuleFile &F, LazyASTUnresolvedSet &Set,
                                  const RecordData &Record, unsigned &Idx) {
  unsigned NumDecls = Record[Idx++];
  Set.reserve(getContext(), NumDecls);
  while (NumDecls--) {
    DeclID ID = ReadDeclID(F, Record, Idx);
    AccessSpecifier AS = (AccessSpecifier)Record[Idx++];
    Set.addLazyDecl(getContext(), ID, AS);
  }
}

CXXBaseSpecifier
ASTReader::ReadCXXBaseSpecifier(ModuleFile &F,
                                const RecordData &Record, unsigned &Idx) {
  bool isVirtual = static_cast<bool>(Record[Idx++]);
  bool isBaseOfClass = static_cast<bool>(Record[Idx++]);
  AccessSpecifier AS = static_cast<AccessSpecifier>(Record[Idx++]);
  bool inheritConstructors = static_cast<bool>(Record[Idx++]);
  TypeSourceInfo *TInfo = GetTypeSourceInfo(F, Record, Idx);
  SourceRange Range = ReadSourceRange(F, Record, Idx);
  SourceLocation EllipsisLoc = ReadSourceLocation(F, Record, Idx);
  CXXBaseSpecifier Result(Range, isVirtual, isBaseOfClass, AS, TInfo,
                          EllipsisLoc);
  Result.setInheritConstructors(inheritConstructors);
  return Result;
}

CXXCtorInitializer **
ASTReader::ReadCXXCtorInitializers(ModuleFile &F, const RecordData &Record,
                                   unsigned &Idx) {
  ASTContext &Context = getContext();
  unsigned NumInitializers = Record[Idx++];
  assert(NumInitializers && "wrote ctor initializers but have no inits");
  auto **CtorInitializers = new (Context) CXXCtorInitializer*[NumInitializers];
  for (unsigned i = 0; i != NumInitializers; ++i) {
    TypeSourceInfo *TInfo = nullptr;
    bool IsBaseVirtual = false;
    FieldDecl *Member = nullptr;
    IndirectFieldDecl *IndirectMember = nullptr;

    CtorInitializerType Type = (CtorInitializerType)Record[Idx++];
    switch (Type) {
    case CTOR_INITIALIZER_BASE:
      TInfo = GetTypeSourceInfo(F, Record, Idx);
      IsBaseVirtual = Record[Idx++];
      break;

    case CTOR_INITIALIZER_DELEGATING:
      TInfo = GetTypeSourceInfo(F, Record, Idx);
      break;

     case CTOR_INITIALIZER_MEMBER:
      Member = ReadDeclAs<FieldDecl>(F, Record, Idx);
      break;

     case CTOR_INITIALIZER_INDIRECT_MEMBER:
      IndirectMember = ReadDeclAs<IndirectFieldDecl>(F, Record, Idx);
      break;
    }

    SourceLocation MemberOrEllipsisLoc = ReadSourceLocation(F, Record, Idx);
    Expr *Init = ReadExpr(F);
    SourceLocation LParenLoc = ReadSourceLocation(F, Record, Idx);
    SourceLocation RParenLoc = ReadSourceLocation(F, Record, Idx);

    CXXCtorInitializer *BOMInit;
    if (Type == CTOR_INITIALIZER_BASE)
      BOMInit = new (Context)
          CXXCtorInitializer(Context, TInfo, IsBaseVirtual, LParenLoc, Init,
                             RParenLoc, MemberOrEllipsisLoc);
    else if (Type == CTOR_INITIALIZER_DELEGATING)
      BOMInit = new (Context)
          CXXCtorInitializer(Context, TInfo, LParenLoc, Init, RParenLoc);
    else if (Member)
      BOMInit = new (Context)
          CXXCtorInitializer(Context, Member, MemberOrEllipsisLoc, LParenLoc,
                             Init, RParenLoc);
    else
      BOMInit = new (Context)
          CXXCtorInitializer(Context, IndirectMember, MemberOrEllipsisLoc,
                             LParenLoc, Init, RParenLoc);

    if (/*IsWritten*/Record[Idx++]) {
      unsigned SourceOrder = Record[Idx++];
      BOMInit->setSourceOrder(SourceOrder);
    }

    CtorInitializers[i] = BOMInit;
  }

  return CtorInitializers;
}

NestedNameSpecifier *
ASTReader::ReadNestedNameSpecifier(ModuleFile &F,
                                   const RecordData &Record, unsigned &Idx) {
  ASTContext &Context = getContext();
  unsigned N = Record[Idx++];
  NestedNameSpecifier *NNS = nullptr, *Prev = nullptr;
  for (unsigned I = 0; I != N; ++I) {
    NestedNameSpecifier::SpecifierKind Kind
      = (NestedNameSpecifier::SpecifierKind)Record[Idx++];
    switch (Kind) {
    case NestedNameSpecifier::Identifier: {
      IdentifierInfo *II = GetIdentifierInfo(F, Record, Idx);
      NNS = NestedNameSpecifier::Create(Context, Prev, II);
      break;
    }

    case NestedNameSpecifier::Namespace: {
      NamespaceDecl *NS = ReadDeclAs<NamespaceDecl>(F, Record, Idx);
      NNS = NestedNameSpecifier::Create(Context, Prev, NS);
      break;
    }

    case NestedNameSpecifier::NamespaceAlias: {
      NamespaceAliasDecl *Alias =ReadDeclAs<NamespaceAliasDecl>(F, Record, Idx);
      NNS = NestedNameSpecifier::Create(Context, Prev, Alias);
      break;
    }

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate: {
      const Type *T = readType(F, Record, Idx).getTypePtrOrNull();
      if (!T)
        return nullptr;

      bool Template = Record[Idx++];
      NNS = NestedNameSpecifier::Create(Context, Prev, Template, T);
      break;
    }

    case NestedNameSpecifier::Global:
      NNS = NestedNameSpecifier::GlobalSpecifier(Context);
      // No associated value, and there can't be a prefix.
      break;

    case NestedNameSpecifier::Super: {
      CXXRecordDecl *RD = ReadDeclAs<CXXRecordDecl>(F, Record, Idx);
      NNS = NestedNameSpecifier::SuperSpecifier(Context, RD);
      break;
    }
    }
    Prev = NNS;
  }
  return NNS;
}

NestedNameSpecifierLoc
ASTReader::ReadNestedNameSpecifierLoc(ModuleFile &F, const RecordData &Record,
                                      unsigned &Idx) {
  ASTContext &Context = getContext();
  unsigned N = Record[Idx++];
  NestedNameSpecifierLocBuilder Builder;
  for (unsigned I = 0; I != N; ++I) {
    NestedNameSpecifier::SpecifierKind Kind
      = (NestedNameSpecifier::SpecifierKind)Record[Idx++];
    switch (Kind) {
    case NestedNameSpecifier::Identifier: {
      IdentifierInfo *II = GetIdentifierInfo(F, Record, Idx);
      SourceRange Range = ReadSourceRange(F, Record, Idx);
      Builder.Extend(Context, II, Range.getBegin(), Range.getEnd());
      break;
    }

    case NestedNameSpecifier::Namespace: {
      NamespaceDecl *NS = ReadDeclAs<NamespaceDecl>(F, Record, Idx);
      SourceRange Range = ReadSourceRange(F, Record, Idx);
      Builder.Extend(Context, NS, Range.getBegin(), Range.getEnd());
      break;
    }

    case NestedNameSpecifier::NamespaceAlias: {
      NamespaceAliasDecl *Alias =ReadDeclAs<NamespaceAliasDecl>(F, Record, Idx);
      SourceRange Range = ReadSourceRange(F, Record, Idx);
      Builder.Extend(Context, Alias, Range.getBegin(), Range.getEnd());
      break;
    }

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate: {
      bool Template = Record[Idx++];
      TypeSourceInfo *T = GetTypeSourceInfo(F, Record, Idx);
      if (!T)
        return NestedNameSpecifierLoc();
      SourceLocation ColonColonLoc = ReadSourceLocation(F, Record, Idx);

      // FIXME: 'template' keyword location not saved anywhere, so we fake it.
      Builder.Extend(Context,
                     Template? T->getTypeLoc().getBeginLoc() : SourceLocation(),
                     T->getTypeLoc(), ColonColonLoc);
      break;
    }

    case NestedNameSpecifier::Global: {
      SourceLocation ColonColonLoc = ReadSourceLocation(F, Record, Idx);
      Builder.MakeGlobal(Context, ColonColonLoc);
      break;
    }

    case NestedNameSpecifier::Super: {
      CXXRecordDecl *RD = ReadDeclAs<CXXRecordDecl>(F, Record, Idx);
      SourceRange Range = ReadSourceRange(F, Record, Idx);
      Builder.MakeSuper(Context, RD, Range.getBegin(), Range.getEnd());
      break;
    }
    }
  }

  return Builder.getWithLocInContext(Context);
}

SourceRange
ASTReader::ReadSourceRange(ModuleFile &F, const RecordData &Record,
                           unsigned &Idx) {
  SourceLocation beg = ReadSourceLocation(F, Record, Idx);
  SourceLocation end = ReadSourceLocation(F, Record, Idx);
  return SourceRange(beg, end);
}

/// Read an integral value
llvm::APInt ASTReader::ReadAPInt(const RecordData &Record, unsigned &Idx) {
  unsigned BitWidth = Record[Idx++];
  unsigned NumWords = llvm::APInt::getNumWords(BitWidth);
  llvm::APInt Result(BitWidth, NumWords, &Record[Idx]);
  Idx += NumWords;
  return Result;
}

/// Read a signed integral value
llvm::APSInt ASTReader::ReadAPSInt(const RecordData &Record, unsigned &Idx) {
  bool isUnsigned = Record[Idx++];
  return llvm::APSInt(ReadAPInt(Record, Idx), isUnsigned);
}

/// Read a floating-point value
llvm::APFloat ASTReader::ReadAPFloat(const RecordData &Record,
                                     const llvm::fltSemantics &Sem,
                                     unsigned &Idx) {
  return llvm::APFloat(Sem, ReadAPInt(Record, Idx));
}

// Read a string
std::string ASTReader::ReadString(const RecordData &Record, unsigned &Idx) {
  unsigned Len = Record[Idx++];
  std::string Result(Record.data() + Idx, Record.data() + Idx + Len);
  Idx += Len;
  return Result;
}

std::string ASTReader::ReadPath(ModuleFile &F, const RecordData &Record,
                                unsigned &Idx) {
  std::string Filename = ReadString(Record, Idx);
  ResolveImportedPath(F, Filename);
  return Filename;
}

VersionTuple ASTReader::ReadVersionTuple(const RecordData &Record,
                                         unsigned &Idx) {
  unsigned Major = Record[Idx++];
  unsigned Minor = Record[Idx++];
  unsigned Subminor = Record[Idx++];
  if (Minor == 0)
    return VersionTuple(Major);
  if (Subminor == 0)
    return VersionTuple(Major, Minor - 1);
  return VersionTuple(Major, Minor - 1, Subminor - 1);
}

CXXTemporary *ASTReader::ReadCXXTemporary(ModuleFile &F,
                                          const RecordData &Record,
                                          unsigned &Idx) {
  CXXDestructorDecl *Decl = ReadDeclAs<CXXDestructorDecl>(F, Record, Idx);
  return CXXTemporary::Create(getContext(), Decl);
}

DiagnosticBuilder ASTReader::Diag(unsigned DiagID) const {
  return Diag(CurrentImportLoc, DiagID);
}

DiagnosticBuilder ASTReader::Diag(SourceLocation Loc, unsigned DiagID) const {
  return Diags.Report(Loc, DiagID);
}

/// Retrieve the identifier table associated with the
/// preprocessor.
IdentifierTable &ASTReader::getIdentifierTable() {
  return PP.getIdentifierTable();
}

/// Record that the given ID maps to the given switch-case
/// statement.
void ASTReader::RecordSwitchCaseID(SwitchCase *SC, unsigned ID) {
  assert((*CurrSwitchCaseStmts)[ID] == nullptr &&
         "Already have a SwitchCase with this ID");
  (*CurrSwitchCaseStmts)[ID] = SC;
}

/// Retrieve the switch-case statement with the given ID.
SwitchCase *ASTReader::getSwitchCaseWithID(unsigned ID) {
  assert((*CurrSwitchCaseStmts)[ID] != nullptr && "No SwitchCase with this ID");
  return (*CurrSwitchCaseStmts)[ID];
}

void ASTReader::ClearSwitchCaseIDs() {
  CurrSwitchCaseStmts->clear();
}

void ASTReader::ReadComments() {
  ASTContext &Context = getContext();
  std::vector<RawComment *> Comments;
  for (SmallVectorImpl<std::pair<BitstreamCursor,
                                 serialization::ModuleFile *>>::iterator
       I = CommentsCursors.begin(),
       E = CommentsCursors.end();
       I != E; ++I) {
    Comments.clear();
    BitstreamCursor &Cursor = I->first;
    serialization::ModuleFile &F = *I->second;
    SavedStreamPosition SavedPosition(Cursor);

    RecordData Record;
    while (true) {
      llvm::BitstreamEntry Entry =
        Cursor.advanceSkippingSubblocks(BitstreamCursor::AF_DontPopBlockAtEnd);

      switch (Entry.Kind) {
      case llvm::BitstreamEntry::SubBlock: // Handled for us already.
      case llvm::BitstreamEntry::Error:
        Error("malformed block record in AST file");
        return;
      case llvm::BitstreamEntry::EndBlock:
        goto NextCursor;
      case llvm::BitstreamEntry::Record:
        // The interesting case.
        break;
      }

      // Read a record.
      Record.clear();
      switch ((CommentRecordTypes)Cursor.readRecord(Entry.ID, Record)) {
      case COMMENTS_RAW_COMMENT: {
        unsigned Idx = 0;
        SourceRange SR = ReadSourceRange(F, Record, Idx);
        RawComment::CommentKind Kind =
            (RawComment::CommentKind) Record[Idx++];
        bool IsTrailingComment = Record[Idx++];
        bool IsAlmostTrailingComment = Record[Idx++];
        Comments.push_back(new (Context) RawComment(
            SR, Kind, IsTrailingComment, IsAlmostTrailingComment));
        break;
      }
      }
    }
  NextCursor:
    // De-serialized SourceLocations get negative FileIDs for other modules,
    // potentially invalidating the original order. Sort it again.
    llvm::sort(Comments, BeforeThanCompare<RawComment>(SourceMgr));
    Context.Comments.addDeserializedComments(Comments);
  }
}

void ASTReader::visitInputFiles(serialization::ModuleFile &MF,
                                bool IncludeSystem, bool Complain,
                    llvm::function_ref<void(const serialization::InputFile &IF,
                                            bool isSystem)> Visitor) {
  unsigned NumUserInputs = MF.NumUserInputFiles;
  unsigned NumInputs = MF.InputFilesLoaded.size();
  assert(NumUserInputs <= NumInputs);
  unsigned N = IncludeSystem ? NumInputs : NumUserInputs;
  for (unsigned I = 0; I < N; ++I) {
    bool IsSystem = I >= NumUserInputs;
    InputFile IF = getInputFile(MF, I+1, Complain);
    Visitor(IF, IsSystem);
  }
}

void ASTReader::visitTopLevelModuleMaps(
    serialization::ModuleFile &MF,
    llvm::function_ref<void(const FileEntry *FE)> Visitor) {
  unsigned NumInputs = MF.InputFilesLoaded.size();
  for (unsigned I = 0; I < NumInputs; ++I) {
    InputFileInfo IFI = readInputFileInfo(MF, I + 1);
    if (IFI.TopLevelModuleMap)
      // FIXME: This unnecessarily re-reads the InputFileInfo.
      if (auto *FE = getInputFile(MF, I + 1).getFile())
        Visitor(FE);
  }
}

std::string ASTReader::getOwningModuleNameForDiagnostic(const Decl *D) {
  // If we know the owning module, use it.
  if (Module *M = D->getImportedOwningModule())
    return M->getFullModuleName();

  // Otherwise, use the name of the top-level module the decl is within.
  if (ModuleFile *M = getOwningModuleFile(D))
    return M->ModuleName;

  // Not from a module.
  return {};
}

void ASTReader::finishPendingActions() {
  while (!PendingIdentifierInfos.empty() || !PendingFunctionTypes.empty() ||
         !PendingIncompleteDeclChains.empty() || !PendingDeclChains.empty() ||
         !PendingMacroIDs.empty() || !PendingDeclContextInfos.empty() ||
         !PendingUpdateRecords.empty()) {
    // If any identifiers with corresponding top-level declarations have
    // been loaded, load those declarations now.
    using TopLevelDeclsMap =
        llvm::DenseMap<IdentifierInfo *, SmallVector<Decl *, 2>>;
    TopLevelDeclsMap TopLevelDecls;

    while (!PendingIdentifierInfos.empty()) {
      IdentifierInfo *II = PendingIdentifierInfos.back().first;
      SmallVector<uint32_t, 4> DeclIDs =
          std::move(PendingIdentifierInfos.back().second);
      PendingIdentifierInfos.pop_back();

      SetGloballyVisibleDecls(II, DeclIDs, &TopLevelDecls[II]);
    }

    // Load each function type that we deferred loading because it was a
    // deduced type that might refer to a local type declared within itself.
    for (unsigned I = 0; I != PendingFunctionTypes.size(); ++I) {
      auto *FD = PendingFunctionTypes[I].first;
      FD->setType(GetType(PendingFunctionTypes[I].second));

      // If we gave a function a deduced return type, remember that we need to
      // propagate that along the redeclaration chain.
      auto *DT = FD->getReturnType()->getContainedDeducedType();
      if (DT && DT->isDeduced())
        PendingDeducedTypeUpdates.insert(
            {FD->getCanonicalDecl(), FD->getReturnType()});
    }
    PendingFunctionTypes.clear();

    // For each decl chain that we wanted to complete while deserializing, mark
    // it as "still needs to be completed".
    for (unsigned I = 0; I != PendingIncompleteDeclChains.size(); ++I) {
      markIncompleteDeclChain(PendingIncompleteDeclChains[I]);
    }
    PendingIncompleteDeclChains.clear();

    // Load pending declaration chains.
    for (unsigned I = 0; I != PendingDeclChains.size(); ++I)
      loadPendingDeclChain(PendingDeclChains[I].first,
                           PendingDeclChains[I].second);
    PendingDeclChains.clear();

    // Make the most recent of the top-level declarations visible.
    for (TopLevelDeclsMap::iterator TLD = TopLevelDecls.begin(),
           TLDEnd = TopLevelDecls.end(); TLD != TLDEnd; ++TLD) {
      IdentifierInfo *II = TLD->first;
      for (unsigned I = 0, N = TLD->second.size(); I != N; ++I) {
        pushExternalDeclIntoScope(cast<NamedDecl>(TLD->second[I]), II);
      }
    }

    // Load any pending macro definitions.
    for (unsigned I = 0; I != PendingMacroIDs.size(); ++I) {
      IdentifierInfo *II = PendingMacroIDs.begin()[I].first;
      SmallVector<PendingMacroInfo, 2> GlobalIDs;
      GlobalIDs.swap(PendingMacroIDs.begin()[I].second);
      // Initialize the macro history from chained-PCHs ahead of module imports.
      for (unsigned IDIdx = 0, NumIDs = GlobalIDs.size(); IDIdx != NumIDs;
           ++IDIdx) {
        const PendingMacroInfo &Info = GlobalIDs[IDIdx];
        if (!Info.M->isModule())
          resolvePendingMacro(II, Info);
      }
      // Handle module imports.
      for (unsigned IDIdx = 0, NumIDs = GlobalIDs.size(); IDIdx != NumIDs;
           ++IDIdx) {
        const PendingMacroInfo &Info = GlobalIDs[IDIdx];
        if (Info.M->isModule())
          resolvePendingMacro(II, Info);
      }
    }
    PendingMacroIDs.clear();

    // Wire up the DeclContexts for Decls that we delayed setting until
    // recursive loading is completed.
    while (!PendingDeclContextInfos.empty()) {
      PendingDeclContextInfo Info = PendingDeclContextInfos.front();
      PendingDeclContextInfos.pop_front();
      DeclContext *SemaDC = cast<DeclContext>(GetDecl(Info.SemaDC));
      DeclContext *LexicalDC = cast<DeclContext>(GetDecl(Info.LexicalDC));
      Info.D->setDeclContextsImpl(SemaDC, LexicalDC, getContext());
    }

    // Perform any pending declaration updates.
    while (!PendingUpdateRecords.empty()) {
      auto Update = PendingUpdateRecords.pop_back_val();
      ReadingKindTracker ReadingKind(Read_Decl, *this);
      loadDeclUpdateRecords(Update);
    }
  }

  // At this point, all update records for loaded decls are in place, so any
  // fake class definitions should have become real.
  assert(PendingFakeDefinitionData.empty() &&
         "faked up a class definition but never saw the real one");

  // If we deserialized any C++ or Objective-C class definitions, any
  // Objective-C protocol definitions, or any redeclarable templates, make sure
  // that all redeclarations point to the definitions. Note that this can only
  // happen now, after the redeclaration chains have been fully wired.
  for (Decl *D : PendingDefinitions) {
    if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
      if (const TagType *TagT = dyn_cast<TagType>(TD->getTypeForDecl())) {
        // Make sure that the TagType points at the definition.
        const_cast<TagType*>(TagT)->decl = TD;
      }

      if (auto RD = dyn_cast<CXXRecordDecl>(D)) {
        for (auto *R = getMostRecentExistingDecl(RD); R;
             R = R->getPreviousDecl()) {
          assert((R == D) ==
                     cast<CXXRecordDecl>(R)->isThisDeclarationADefinition() &&
                 "declaration thinks it's the definition but it isn't");
          cast<CXXRecordDecl>(R)->DefinitionData = RD->DefinitionData;
        }
      }

      continue;
    }

    if (auto ID = dyn_cast<ObjCInterfaceDecl>(D)) {
      // Make sure that the ObjCInterfaceType points at the definition.
      const_cast<ObjCInterfaceType *>(cast<ObjCInterfaceType>(ID->TypeForDecl))
        ->Decl = ID;

      for (auto *R = getMostRecentExistingDecl(ID); R; R = R->getPreviousDecl())
        cast<ObjCInterfaceDecl>(R)->Data = ID->Data;

      continue;
    }

    if (auto PD = dyn_cast<ObjCProtocolDecl>(D)) {
      for (auto *R = getMostRecentExistingDecl(PD); R; R = R->getPreviousDecl())
        cast<ObjCProtocolDecl>(R)->Data = PD->Data;

      continue;
    }

    auto RTD = cast<RedeclarableTemplateDecl>(D)->getCanonicalDecl();
    for (auto *R = getMostRecentExistingDecl(RTD); R; R = R->getPreviousDecl())
      cast<RedeclarableTemplateDecl>(R)->Common = RTD->Common;
  }
  PendingDefinitions.clear();

  // Load the bodies of any functions or methods we've encountered. We do
  // this now (delayed) so that we can be sure that the declaration chains
  // have been fully wired up (hasBody relies on this).
  // FIXME: We shouldn't require complete redeclaration chains here.
  for (PendingBodiesMap::iterator PB = PendingBodies.begin(),
                               PBEnd = PendingBodies.end();
       PB != PBEnd; ++PB) {
    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(PB->first)) {
      // For a function defined inline within a class template, force the
      // canonical definition to be the one inside the canonical definition of
      // the template. This ensures that we instantiate from a correct view
      // of the template.
      //
      // Sadly we can't do this more generally: we can't be sure that all
      // copies of an arbitrary class definition will have the same members
      // defined (eg, some member functions may not be instantiated, and some
      // special members may or may not have been implicitly defined).
      if (auto *RD = dyn_cast<CXXRecordDecl>(FD->getLexicalParent()))
        if (RD->isDependentContext() && !RD->isThisDeclarationADefinition())
          continue;

      // FIXME: Check for =delete/=default?
      // FIXME: Complain about ODR violations here?
      const FunctionDecl *Defn = nullptr;
      if (!getContext().getLangOpts().Modules || !FD->hasBody(Defn)) {
        FD->setLazyBody(PB->second);
      } else {
        auto *NonConstDefn = const_cast<FunctionDecl*>(Defn);
        mergeDefinitionVisibility(NonConstDefn, FD);

        if (!FD->isLateTemplateParsed() &&
            !NonConstDefn->isLateTemplateParsed() &&
            FD->getODRHash() != NonConstDefn->getODRHash()) {
          if (!isa<CXXMethodDecl>(FD)) {
            PendingFunctionOdrMergeFailures[FD].push_back(NonConstDefn);
          } else if (FD->getLexicalParent()->isFileContext() &&
                     NonConstDefn->getLexicalParent()->isFileContext()) {
            // Only diagnose out-of-line method definitions.  If they are
            // in class definitions, then an error will be generated when
            // processing the class bodies.
            PendingFunctionOdrMergeFailures[FD].push_back(NonConstDefn);
          }
        }
      }
      continue;
    }

    ObjCMethodDecl *MD = cast<ObjCMethodDecl>(PB->first);
    if (!getContext().getLangOpts().Modules || !MD->hasBody())
      MD->setLazyBody(PB->second);
  }
  PendingBodies.clear();

  // Do some cleanup.
  for (auto *ND : PendingMergedDefinitionsToDeduplicate)
    getContext().deduplicateMergedDefinitonsFor(ND);
  PendingMergedDefinitionsToDeduplicate.clear();
}

void ASTReader::diagnoseOdrViolations() {
  if (PendingOdrMergeFailures.empty() && PendingOdrMergeChecks.empty() &&
      PendingFunctionOdrMergeFailures.empty() &&
      PendingEnumOdrMergeFailures.empty())
    return;

  // Trigger the import of the full definition of each class that had any
  // odr-merging problems, so we can produce better diagnostics for them.
  // These updates may in turn find and diagnose some ODR failures, so take
  // ownership of the set first.
  auto OdrMergeFailures = std::move(PendingOdrMergeFailures);
  PendingOdrMergeFailures.clear();
  for (auto &Merge : OdrMergeFailures) {
    Merge.first->buildLookup();
    Merge.first->decls_begin();
    Merge.first->bases_begin();
    Merge.first->vbases_begin();
    for (auto &RecordPair : Merge.second) {
      auto *RD = RecordPair.first;
      RD->decls_begin();
      RD->bases_begin();
      RD->vbases_begin();
    }
  }

  // Trigger the import of functions.
  auto FunctionOdrMergeFailures = std::move(PendingFunctionOdrMergeFailures);
  PendingFunctionOdrMergeFailures.clear();
  for (auto &Merge : FunctionOdrMergeFailures) {
    Merge.first->buildLookup();
    Merge.first->decls_begin();
    Merge.first->getBody();
    for (auto &FD : Merge.second) {
      FD->buildLookup();
      FD->decls_begin();
      FD->getBody();
    }
  }

  // Trigger the import of enums.
  auto EnumOdrMergeFailures = std::move(PendingEnumOdrMergeFailures);
  PendingEnumOdrMergeFailures.clear();
  for (auto &Merge : EnumOdrMergeFailures) {
    Merge.first->decls_begin();
    for (auto &Enum : Merge.second) {
      Enum->decls_begin();
    }
  }

  // For each declaration from a merged context, check that the canonical
  // definition of that context also contains a declaration of the same
  // entity.
  //
  // Caution: this loop does things that might invalidate iterators into
  // PendingOdrMergeChecks. Don't turn this into a range-based for loop!
  while (!PendingOdrMergeChecks.empty()) {
    NamedDecl *D = PendingOdrMergeChecks.pop_back_val();

    // FIXME: Skip over implicit declarations for now. This matters for things
    // like implicitly-declared special member functions. This isn't entirely
    // correct; we can end up with multiple unmerged declarations of the same
    // implicit entity.
    if (D->isImplicit())
      continue;

    DeclContext *CanonDef = D->getDeclContext();

    bool Found = false;
    const Decl *DCanon = D->getCanonicalDecl();

    for (auto RI : D->redecls()) {
      if (RI->getLexicalDeclContext() == CanonDef) {
        Found = true;
        break;
      }
    }
    if (Found)
      continue;

    // Quick check failed, time to do the slow thing. Note, we can't just
    // look up the name of D in CanonDef here, because the member that is
    // in CanonDef might not be found by name lookup (it might have been
    // replaced by a more recent declaration in the lookup table), and we
    // can't necessarily find it in the redeclaration chain because it might
    // be merely mergeable, not redeclarable.
    llvm::SmallVector<const NamedDecl*, 4> Candidates;
    for (auto *CanonMember : CanonDef->decls()) {
      if (CanonMember->getCanonicalDecl() == DCanon) {
        // This can happen if the declaration is merely mergeable and not
        // actually redeclarable (we looked for redeclarations earlier).
        //
        // FIXME: We should be able to detect this more efficiently, without
        // pulling in all of the members of CanonDef.
        Found = true;
        break;
      }
      if (auto *ND = dyn_cast<NamedDecl>(CanonMember))
        if (ND->getDeclName() == D->getDeclName())
          Candidates.push_back(ND);
    }

    if (!Found) {
      // The AST doesn't like TagDecls becoming invalid after they've been
      // completed. We only really need to mark FieldDecls as invalid here.
      if (!isa<TagDecl>(D))
        D->setInvalidDecl();

      // Ensure we don't accidentally recursively enter deserialization while
      // we're producing our diagnostic.
      Deserializing RecursionGuard(this);

      std::string CanonDefModule =
          getOwningModuleNameForDiagnostic(cast<Decl>(CanonDef));
      Diag(D->getLocation(), diag::err_module_odr_violation_missing_decl)
        << D << getOwningModuleNameForDiagnostic(D)
        << CanonDef << CanonDefModule.empty() << CanonDefModule;

      if (Candidates.empty())
        Diag(cast<Decl>(CanonDef)->getLocation(),
             diag::note_module_odr_violation_no_possible_decls) << D;
      else {
        for (unsigned I = 0, N = Candidates.size(); I != N; ++I)
          Diag(Candidates[I]->getLocation(),
               diag::note_module_odr_violation_possible_decl)
            << Candidates[I];
      }

      DiagnosedOdrMergeFailures.insert(CanonDef);
    }
  }

  if (OdrMergeFailures.empty() && FunctionOdrMergeFailures.empty() &&
      EnumOdrMergeFailures.empty())
    return;

  // Ensure we don't accidentally recursively enter deserialization while
  // we're producing our diagnostics.
  Deserializing RecursionGuard(this);

  // Common code for hashing helpers.
  ODRHash Hash;
  auto ComputeQualTypeODRHash = [&Hash](QualType Ty) {
    Hash.clear();
    Hash.AddQualType(Ty);
    return Hash.CalculateHash();
  };

  auto ComputeODRHash = [&Hash](const Stmt *S) {
    assert(S);
    Hash.clear();
    Hash.AddStmt(S);
    return Hash.CalculateHash();
  };

  auto ComputeSubDeclODRHash = [&Hash](const Decl *D) {
    assert(D);
    Hash.clear();
    Hash.AddSubDecl(D);
    return Hash.CalculateHash();
  };

  auto ComputeTemplateArgumentODRHash = [&Hash](const TemplateArgument &TA) {
    Hash.clear();
    Hash.AddTemplateArgument(TA);
    return Hash.CalculateHash();
  };

  auto ComputeTemplateParameterListODRHash =
      [&Hash](const TemplateParameterList *TPL) {
        assert(TPL);
        Hash.clear();
        Hash.AddTemplateParameterList(TPL);
        return Hash.CalculateHash();
      };

  // Issue any pending ODR-failure diagnostics.
  for (auto &Merge : OdrMergeFailures) {
    // If we've already pointed out a specific problem with this class, don't
    // bother issuing a general "something's different" diagnostic.
    if (!DiagnosedOdrMergeFailures.insert(Merge.first).second)
      continue;

    bool Diagnosed = false;
    CXXRecordDecl *FirstRecord = Merge.first;
    std::string FirstModule = getOwningModuleNameForDiagnostic(FirstRecord);
    for (auto &RecordPair : Merge.second) {
      CXXRecordDecl *SecondRecord = RecordPair.first;
      // Multiple different declarations got merged together; tell the user
      // where they came from.
      if (FirstRecord == SecondRecord)
        continue;

      std::string SecondModule = getOwningModuleNameForDiagnostic(SecondRecord);

      auto *FirstDD = FirstRecord->DefinitionData;
      auto *SecondDD = RecordPair.second;

      assert(FirstDD && SecondDD && "Definitions without DefinitionData");

      // Diagnostics from DefinitionData are emitted here.
      if (FirstDD != SecondDD) {
        enum ODRDefinitionDataDifference {
          NumBases,
          NumVBases,
          BaseType,
          BaseVirtual,
          BaseAccess,
        };
        auto ODRDiagError = [FirstRecord, &FirstModule,
                             this](SourceLocation Loc, SourceRange Range,
                                   ODRDefinitionDataDifference DiffType) {
          return Diag(Loc, diag::err_module_odr_violation_definition_data)
                 << FirstRecord << FirstModule.empty() << FirstModule << Range
                 << DiffType;
        };
        auto ODRDiagNote = [&SecondModule,
                            this](SourceLocation Loc, SourceRange Range,
                                  ODRDefinitionDataDifference DiffType) {
          return Diag(Loc, diag::note_module_odr_violation_definition_data)
                 << SecondModule << Range << DiffType;
        };

        unsigned FirstNumBases = FirstDD->NumBases;
        unsigned FirstNumVBases = FirstDD->NumVBases;
        unsigned SecondNumBases = SecondDD->NumBases;
        unsigned SecondNumVBases = SecondDD->NumVBases;

        auto GetSourceRange = [](struct CXXRecordDecl::DefinitionData *DD) {
          unsigned NumBases = DD->NumBases;
          if (NumBases == 0) return SourceRange();
          auto bases = DD->bases();
          return SourceRange(bases[0].getBeginLoc(),
                             bases[NumBases - 1].getEndLoc());
        };

        if (FirstNumBases != SecondNumBases) {
          ODRDiagError(FirstRecord->getLocation(), GetSourceRange(FirstDD),
                       NumBases)
              << FirstNumBases;
          ODRDiagNote(SecondRecord->getLocation(), GetSourceRange(SecondDD),
                      NumBases)
              << SecondNumBases;
          Diagnosed = true;
          break;
        }

        if (FirstNumVBases != SecondNumVBases) {
          ODRDiagError(FirstRecord->getLocation(), GetSourceRange(FirstDD),
                       NumVBases)
              << FirstNumVBases;
          ODRDiagNote(SecondRecord->getLocation(), GetSourceRange(SecondDD),
                      NumVBases)
              << SecondNumVBases;
          Diagnosed = true;
          break;
        }

        auto FirstBases = FirstDD->bases();
        auto SecondBases = SecondDD->bases();
        unsigned i = 0;
        for (i = 0; i < FirstNumBases; ++i) {
          auto FirstBase = FirstBases[i];
          auto SecondBase = SecondBases[i];
          if (ComputeQualTypeODRHash(FirstBase.getType()) !=
              ComputeQualTypeODRHash(SecondBase.getType())) {
            ODRDiagError(FirstRecord->getLocation(), FirstBase.getSourceRange(),
                         BaseType)
                << (i + 1) << FirstBase.getType();
            ODRDiagNote(SecondRecord->getLocation(),
                        SecondBase.getSourceRange(), BaseType)
                << (i + 1) << SecondBase.getType();
            break;
          }

          if (FirstBase.isVirtual() != SecondBase.isVirtual()) {
            ODRDiagError(FirstRecord->getLocation(), FirstBase.getSourceRange(),
                         BaseVirtual)
                << (i + 1) << FirstBase.isVirtual() << FirstBase.getType();
            ODRDiagNote(SecondRecord->getLocation(),
                        SecondBase.getSourceRange(), BaseVirtual)
                << (i + 1) << SecondBase.isVirtual() << SecondBase.getType();
            break;
          }

          if (FirstBase.getAccessSpecifierAsWritten() !=
              SecondBase.getAccessSpecifierAsWritten()) {
            ODRDiagError(FirstRecord->getLocation(), FirstBase.getSourceRange(),
                         BaseAccess)
                << (i + 1) << FirstBase.getType()
                << (int)FirstBase.getAccessSpecifierAsWritten();
            ODRDiagNote(SecondRecord->getLocation(),
                        SecondBase.getSourceRange(), BaseAccess)
                << (i + 1) << SecondBase.getType()
                << (int)SecondBase.getAccessSpecifierAsWritten();
            break;
          }
        }

        if (i != FirstNumBases) {
          Diagnosed = true;
          break;
        }
      }

      using DeclHashes = llvm::SmallVector<std::pair<Decl *, unsigned>, 4>;

      const ClassTemplateDecl *FirstTemplate =
          FirstRecord->getDescribedClassTemplate();
      const ClassTemplateDecl *SecondTemplate =
          SecondRecord->getDescribedClassTemplate();

      assert(!FirstTemplate == !SecondTemplate &&
             "Both pointers should be null or non-null");

      enum ODRTemplateDifference {
        ParamEmptyName,
        ParamName,
        ParamSingleDefaultArgument,
        ParamDifferentDefaultArgument,
      };

      if (FirstTemplate && SecondTemplate) {
        DeclHashes FirstTemplateHashes;
        DeclHashes SecondTemplateHashes;

        auto PopulateTemplateParameterHashs =
            [&ComputeSubDeclODRHash](DeclHashes &Hashes,
                                     const ClassTemplateDecl *TD) {
              for (auto *D : TD->getTemplateParameters()->asArray()) {
                Hashes.emplace_back(D, ComputeSubDeclODRHash(D));
              }
            };

        PopulateTemplateParameterHashs(FirstTemplateHashes, FirstTemplate);
        PopulateTemplateParameterHashs(SecondTemplateHashes, SecondTemplate);

        assert(FirstTemplateHashes.size() == SecondTemplateHashes.size() &&
               "Number of template parameters should be equal.");

        auto FirstIt = FirstTemplateHashes.begin();
        auto FirstEnd = FirstTemplateHashes.end();
        auto SecondIt = SecondTemplateHashes.begin();
        for (; FirstIt != FirstEnd; ++FirstIt, ++SecondIt) {
          if (FirstIt->second == SecondIt->second)
            continue;

          auto ODRDiagError = [FirstRecord, &FirstModule,
                               this](SourceLocation Loc, SourceRange Range,
                                     ODRTemplateDifference DiffType) {
            return Diag(Loc, diag::err_module_odr_violation_template_parameter)
                   << FirstRecord << FirstModule.empty() << FirstModule << Range
                   << DiffType;
          };
          auto ODRDiagNote = [&SecondModule,
                              this](SourceLocation Loc, SourceRange Range,
                                    ODRTemplateDifference DiffType) {
            return Diag(Loc, diag::note_module_odr_violation_template_parameter)
                   << SecondModule << Range << DiffType;
          };

          const NamedDecl* FirstDecl = cast<NamedDecl>(FirstIt->first);
          const NamedDecl* SecondDecl = cast<NamedDecl>(SecondIt->first);

          assert(FirstDecl->getKind() == SecondDecl->getKind() &&
                 "Parameter Decl's should be the same kind.");

          DeclarationName FirstName = FirstDecl->getDeclName();
          DeclarationName SecondName = SecondDecl->getDeclName();

          if (FirstName != SecondName) {
            const bool FirstNameEmpty =
                FirstName.isIdentifier() && !FirstName.getAsIdentifierInfo();
            const bool SecondNameEmpty =
                SecondName.isIdentifier() && !SecondName.getAsIdentifierInfo();
            assert((!FirstNameEmpty || !SecondNameEmpty) &&
                   "Both template parameters cannot be unnamed.");
            ODRDiagError(FirstDecl->getLocation(), FirstDecl->getSourceRange(),
                         FirstNameEmpty ? ParamEmptyName : ParamName)
                << FirstName;
            ODRDiagNote(SecondDecl->getLocation(), SecondDecl->getSourceRange(),
                        SecondNameEmpty ? ParamEmptyName : ParamName)
                << SecondName;
            break;
          }

          switch (FirstDecl->getKind()) {
          default:
            llvm_unreachable("Invalid template parameter type.");
          case Decl::TemplateTypeParm: {
            const auto *FirstParam = cast<TemplateTypeParmDecl>(FirstDecl);
            const auto *SecondParam = cast<TemplateTypeParmDecl>(SecondDecl);
            const bool HasFirstDefaultArgument =
                FirstParam->hasDefaultArgument() &&
                !FirstParam->defaultArgumentWasInherited();
            const bool HasSecondDefaultArgument =
                SecondParam->hasDefaultArgument() &&
                !SecondParam->defaultArgumentWasInherited();

            if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
              ODRDiagError(FirstDecl->getLocation(),
                           FirstDecl->getSourceRange(),
                           ParamSingleDefaultArgument)
                  << HasFirstDefaultArgument;
              ODRDiagNote(SecondDecl->getLocation(),
                          SecondDecl->getSourceRange(),
                          ParamSingleDefaultArgument)
                  << HasSecondDefaultArgument;
              break;
            }

            assert(HasFirstDefaultArgument && HasSecondDefaultArgument &&
                   "Expecting default arguments.");

            ODRDiagError(FirstDecl->getLocation(), FirstDecl->getSourceRange(),
                         ParamDifferentDefaultArgument);
            ODRDiagNote(SecondDecl->getLocation(), SecondDecl->getSourceRange(),
                        ParamDifferentDefaultArgument);

            break;
          }
          case Decl::NonTypeTemplateParm: {
            const auto *FirstParam = cast<NonTypeTemplateParmDecl>(FirstDecl);
            const auto *SecondParam = cast<NonTypeTemplateParmDecl>(SecondDecl);
            const bool HasFirstDefaultArgument =
                FirstParam->hasDefaultArgument() &&
                !FirstParam->defaultArgumentWasInherited();
            const bool HasSecondDefaultArgument =
                SecondParam->hasDefaultArgument() &&
                !SecondParam->defaultArgumentWasInherited();

            if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
              ODRDiagError(FirstDecl->getLocation(),
                           FirstDecl->getSourceRange(),
                           ParamSingleDefaultArgument)
                  << HasFirstDefaultArgument;
              ODRDiagNote(SecondDecl->getLocation(),
                          SecondDecl->getSourceRange(),
                          ParamSingleDefaultArgument)
                  << HasSecondDefaultArgument;
              break;
            }

            assert(HasFirstDefaultArgument && HasSecondDefaultArgument &&
                   "Expecting default arguments.");

            ODRDiagError(FirstDecl->getLocation(), FirstDecl->getSourceRange(),
                         ParamDifferentDefaultArgument);
            ODRDiagNote(SecondDecl->getLocation(), SecondDecl->getSourceRange(),
                        ParamDifferentDefaultArgument);

            break;
          }
          case Decl::TemplateTemplateParm: {
            const auto *FirstParam = cast<TemplateTemplateParmDecl>(FirstDecl);
            const auto *SecondParam =
                cast<TemplateTemplateParmDecl>(SecondDecl);
            const bool HasFirstDefaultArgument =
                FirstParam->hasDefaultArgument() &&
                !FirstParam->defaultArgumentWasInherited();
            const bool HasSecondDefaultArgument =
                SecondParam->hasDefaultArgument() &&
                !SecondParam->defaultArgumentWasInherited();

            if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
              ODRDiagError(FirstDecl->getLocation(),
                           FirstDecl->getSourceRange(),
                           ParamSingleDefaultArgument)
                  << HasFirstDefaultArgument;
              ODRDiagNote(SecondDecl->getLocation(),
                          SecondDecl->getSourceRange(),
                          ParamSingleDefaultArgument)
                  << HasSecondDefaultArgument;
              break;
            }

            assert(HasFirstDefaultArgument && HasSecondDefaultArgument &&
                   "Expecting default arguments.");

            ODRDiagError(FirstDecl->getLocation(), FirstDecl->getSourceRange(),
                         ParamDifferentDefaultArgument);
            ODRDiagNote(SecondDecl->getLocation(), SecondDecl->getSourceRange(),
                        ParamDifferentDefaultArgument);

            break;
          }
          }

          break;
        }

        if (FirstIt != FirstEnd) {
          Diagnosed = true;
          break;
        }
      }

      DeclHashes FirstHashes;
      DeclHashes SecondHashes;

      auto PopulateHashes = [&ComputeSubDeclODRHash, FirstRecord](
                                DeclHashes &Hashes, CXXRecordDecl *Record) {
        for (auto *D : Record->decls()) {
          // Due to decl merging, the first CXXRecordDecl is the parent of
          // Decls in both records.
          if (!ODRHash::isWhitelistedDecl(D, FirstRecord))
            continue;
          Hashes.emplace_back(D, ComputeSubDeclODRHash(D));
        }
      };
      PopulateHashes(FirstHashes, FirstRecord);
      PopulateHashes(SecondHashes, SecondRecord);

      // Used with err_module_odr_violation_mismatch_decl and
      // note_module_odr_violation_mismatch_decl
      // This list should be the same Decl's as in ODRHash::isWhiteListedDecl
      enum {
        EndOfClass,
        PublicSpecifer,
        PrivateSpecifer,
        ProtectedSpecifer,
        StaticAssert,
        Field,
        CXXMethod,
        TypeAlias,
        TypeDef,
        Var,
        Friend,
        FunctionTemplate,
        Other
      } FirstDiffType = Other,
        SecondDiffType = Other;

      auto DifferenceSelector = [](Decl *D) {
        assert(D && "valid Decl required");
        switch (D->getKind()) {
        default:
          return Other;
        case Decl::AccessSpec:
          switch (D->getAccess()) {
          case AS_public:
            return PublicSpecifer;
          case AS_private:
            return PrivateSpecifer;
          case AS_protected:
            return ProtectedSpecifer;
          case AS_none:
            break;
          }
          llvm_unreachable("Invalid access specifier");
        case Decl::StaticAssert:
          return StaticAssert;
        case Decl::Field:
          return Field;
        case Decl::CXXMethod:
        case Decl::CXXConstructor:
        case Decl::CXXDestructor:
          return CXXMethod;
        case Decl::TypeAlias:
          return TypeAlias;
        case Decl::Typedef:
          return TypeDef;
        case Decl::Var:
          return Var;
        case Decl::Friend:
          return Friend;
        case Decl::FunctionTemplate:
          return FunctionTemplate;
        }
      };

      Decl *FirstDecl = nullptr;
      Decl *SecondDecl = nullptr;
      auto FirstIt = FirstHashes.begin();
      auto SecondIt = SecondHashes.begin();

      // If there is a diagnoseable difference, FirstDiffType and
      // SecondDiffType will not be Other and FirstDecl and SecondDecl will be
      // filled in if not EndOfClass.
      while (FirstIt != FirstHashes.end() || SecondIt != SecondHashes.end()) {
        if (FirstIt != FirstHashes.end() && SecondIt != SecondHashes.end() &&
            FirstIt->second == SecondIt->second) {
          ++FirstIt;
          ++SecondIt;
          continue;
        }

        FirstDecl = FirstIt == FirstHashes.end() ? nullptr : FirstIt->first;
        SecondDecl = SecondIt == SecondHashes.end() ? nullptr : SecondIt->first;

        FirstDiffType = FirstDecl ? DifferenceSelector(FirstDecl) : EndOfClass;
        SecondDiffType =
            SecondDecl ? DifferenceSelector(SecondDecl) : EndOfClass;

        break;
      }

      if (FirstDiffType == Other || SecondDiffType == Other) {
        // Reaching this point means an unexpected Decl was encountered
        // or no difference was detected.  This causes a generic error
        // message to be emitted.
        Diag(FirstRecord->getLocation(),
             diag::err_module_odr_violation_different_definitions)
            << FirstRecord << FirstModule.empty() << FirstModule;

        if (FirstDecl) {
          Diag(FirstDecl->getLocation(), diag::note_first_module_difference)
              << FirstRecord << FirstDecl->getSourceRange();
        }

        Diag(SecondRecord->getLocation(),
             diag::note_module_odr_violation_different_definitions)
            << SecondModule;

        if (SecondDecl) {
          Diag(SecondDecl->getLocation(), diag::note_second_module_difference)
              << SecondDecl->getSourceRange();
        }

        Diagnosed = true;
        break;
      }

      if (FirstDiffType != SecondDiffType) {
        SourceLocation FirstLoc;
        SourceRange FirstRange;
        if (FirstDiffType == EndOfClass) {
          FirstLoc = FirstRecord->getBraceRange().getEnd();
        } else {
          FirstLoc = FirstIt->first->getLocation();
          FirstRange = FirstIt->first->getSourceRange();
        }
        Diag(FirstLoc, diag::err_module_odr_violation_mismatch_decl)
            << FirstRecord << FirstModule.empty() << FirstModule << FirstRange
            << FirstDiffType;

        SourceLocation SecondLoc;
        SourceRange SecondRange;
        if (SecondDiffType == EndOfClass) {
          SecondLoc = SecondRecord->getBraceRange().getEnd();
        } else {
          SecondLoc = SecondDecl->getLocation();
          SecondRange = SecondDecl->getSourceRange();
        }
        Diag(SecondLoc, diag::note_module_odr_violation_mismatch_decl)
            << SecondModule << SecondRange << SecondDiffType;
        Diagnosed = true;
        break;
      }

      assert(FirstDiffType == SecondDiffType);

      // Used with err_module_odr_violation_mismatch_decl_diff and
      // note_module_odr_violation_mismatch_decl_diff
      enum ODRDeclDifference {
        StaticAssertCondition,
        StaticAssertMessage,
        StaticAssertOnlyMessage,
        FieldName,
        FieldTypeName,
        FieldSingleBitField,
        FieldDifferentWidthBitField,
        FieldSingleMutable,
        FieldSingleInitializer,
        FieldDifferentInitializers,
        MethodName,
        MethodDeleted,
        MethodDefaulted,
        MethodVirtual,
        MethodStatic,
        MethodVolatile,
        MethodConst,
        MethodInline,
        MethodNumberParameters,
        MethodParameterType,
        MethodParameterName,
        MethodParameterSingleDefaultArgument,
        MethodParameterDifferentDefaultArgument,
        MethodNoTemplateArguments,
        MethodDifferentNumberTemplateArguments,
        MethodDifferentTemplateArgument,
        MethodSingleBody,
        MethodDifferentBody,
        TypedefName,
        TypedefType,
        VarName,
        VarType,
        VarSingleInitializer,
        VarDifferentInitializer,
        VarConstexpr,
        FriendTypeFunction,
        FriendType,
        FriendFunction,
        FunctionTemplateDifferentNumberParameters,
        FunctionTemplateParameterDifferentKind,
        FunctionTemplateParameterName,
        FunctionTemplateParameterSingleDefaultArgument,
        FunctionTemplateParameterDifferentDefaultArgument,
        FunctionTemplateParameterDifferentType,
        FunctionTemplatePackParameter,
      };

      // These lambdas have the common portions of the ODR diagnostics.  This
      // has the same return as Diag(), so addition parameters can be passed
      // in with operator<<
      auto ODRDiagError = [FirstRecord, &FirstModule, this](
          SourceLocation Loc, SourceRange Range, ODRDeclDifference DiffType) {
        return Diag(Loc, diag::err_module_odr_violation_mismatch_decl_diff)
               << FirstRecord << FirstModule.empty() << FirstModule << Range
               << DiffType;
      };
      auto ODRDiagNote = [&SecondModule, this](
          SourceLocation Loc, SourceRange Range, ODRDeclDifference DiffType) {
        return Diag(Loc, diag::note_module_odr_violation_mismatch_decl_diff)
               << SecondModule << Range << DiffType;
      };

      switch (FirstDiffType) {
      case Other:
      case EndOfClass:
      case PublicSpecifer:
      case PrivateSpecifer:
      case ProtectedSpecifer:
        llvm_unreachable("Invalid diff type");

      case StaticAssert: {
        StaticAssertDecl *FirstSA = cast<StaticAssertDecl>(FirstDecl);
        StaticAssertDecl *SecondSA = cast<StaticAssertDecl>(SecondDecl);

        Expr *FirstExpr = FirstSA->getAssertExpr();
        Expr *SecondExpr = SecondSA->getAssertExpr();
        unsigned FirstODRHash = ComputeODRHash(FirstExpr);
        unsigned SecondODRHash = ComputeODRHash(SecondExpr);
        if (FirstODRHash != SecondODRHash) {
          ODRDiagError(FirstExpr->getBeginLoc(), FirstExpr->getSourceRange(),
                       StaticAssertCondition);
          ODRDiagNote(SecondExpr->getBeginLoc(), SecondExpr->getSourceRange(),
                      StaticAssertCondition);
          Diagnosed = true;
          break;
        }

        StringLiteral *FirstStr = FirstSA->getMessage();
        StringLiteral *SecondStr = SecondSA->getMessage();
        assert((FirstStr || SecondStr) && "Both messages cannot be empty");
        if ((FirstStr && !SecondStr) || (!FirstStr && SecondStr)) {
          SourceLocation FirstLoc, SecondLoc;
          SourceRange FirstRange, SecondRange;
          if (FirstStr) {
            FirstLoc = FirstStr->getBeginLoc();
            FirstRange = FirstStr->getSourceRange();
          } else {
            FirstLoc = FirstSA->getBeginLoc();
            FirstRange = FirstSA->getSourceRange();
          }
          if (SecondStr) {
            SecondLoc = SecondStr->getBeginLoc();
            SecondRange = SecondStr->getSourceRange();
          } else {
            SecondLoc = SecondSA->getBeginLoc();
            SecondRange = SecondSA->getSourceRange();
          }
          ODRDiagError(FirstLoc, FirstRange, StaticAssertOnlyMessage)
              << (FirstStr == nullptr);
          ODRDiagNote(SecondLoc, SecondRange, StaticAssertOnlyMessage)
              << (SecondStr == nullptr);
          Diagnosed = true;
          break;
        }

        if (FirstStr && SecondStr &&
            FirstStr->getString() != SecondStr->getString()) {
          ODRDiagError(FirstStr->getBeginLoc(), FirstStr->getSourceRange(),
                       StaticAssertMessage);
          ODRDiagNote(SecondStr->getBeginLoc(), SecondStr->getSourceRange(),
                      StaticAssertMessage);
          Diagnosed = true;
          break;
        }
        break;
      }
      case Field: {
        FieldDecl *FirstField = cast<FieldDecl>(FirstDecl);
        FieldDecl *SecondField = cast<FieldDecl>(SecondDecl);
        IdentifierInfo *FirstII = FirstField->getIdentifier();
        IdentifierInfo *SecondII = SecondField->getIdentifier();
        if (FirstII->getName() != SecondII->getName()) {
          ODRDiagError(FirstField->getLocation(), FirstField->getSourceRange(),
                       FieldName)
              << FirstII;
          ODRDiagNote(SecondField->getLocation(), SecondField->getSourceRange(),
                      FieldName)
              << SecondII;

          Diagnosed = true;
          break;
        }

        assert(getContext().hasSameType(FirstField->getType(),
                                        SecondField->getType()));

        QualType FirstType = FirstField->getType();
        QualType SecondType = SecondField->getType();
        if (ComputeQualTypeODRHash(FirstType) !=
            ComputeQualTypeODRHash(SecondType)) {
          ODRDiagError(FirstField->getLocation(), FirstField->getSourceRange(),
                       FieldTypeName)
              << FirstII << FirstType;
          ODRDiagNote(SecondField->getLocation(), SecondField->getSourceRange(),
                      FieldTypeName)
              << SecondII << SecondType;

          Diagnosed = true;
          break;
        }

        const bool IsFirstBitField = FirstField->isBitField();
        const bool IsSecondBitField = SecondField->isBitField();
        if (IsFirstBitField != IsSecondBitField) {
          ODRDiagError(FirstField->getLocation(), FirstField->getSourceRange(),
                       FieldSingleBitField)
              << FirstII << IsFirstBitField;
          ODRDiagNote(SecondField->getLocation(), SecondField->getSourceRange(),
                      FieldSingleBitField)
              << SecondII << IsSecondBitField;
          Diagnosed = true;
          break;
        }

        if (IsFirstBitField && IsSecondBitField) {
          ODRDiagError(FirstField->getLocation(), FirstField->getSourceRange(),
                       FieldDifferentWidthBitField)
              << FirstII << FirstField->getBitWidth()->getSourceRange();
          ODRDiagNote(SecondField->getLocation(), SecondField->getSourceRange(),
                      FieldDifferentWidthBitField)
              << SecondII << SecondField->getBitWidth()->getSourceRange();
          Diagnosed = true;
          break;
        }

        const bool IsFirstMutable = FirstField->isMutable();
        const bool IsSecondMutable = SecondField->isMutable();
        if (IsFirstMutable != IsSecondMutable) {
          ODRDiagError(FirstField->getLocation(), FirstField->getSourceRange(),
                       FieldSingleMutable)
              << FirstII << IsFirstMutable;
          ODRDiagNote(SecondField->getLocation(), SecondField->getSourceRange(),
                      FieldSingleMutable)
              << SecondII << IsSecondMutable;
          Diagnosed = true;
          break;
        }

        const Expr *FirstInitializer = FirstField->getInClassInitializer();
        const Expr *SecondInitializer = SecondField->getInClassInitializer();
        if ((!FirstInitializer && SecondInitializer) ||
            (FirstInitializer && !SecondInitializer)) {
          ODRDiagError(FirstField->getLocation(), FirstField->getSourceRange(),
                       FieldSingleInitializer)
              << FirstII << (FirstInitializer != nullptr);
          ODRDiagNote(SecondField->getLocation(), SecondField->getSourceRange(),
                      FieldSingleInitializer)
              << SecondII << (SecondInitializer != nullptr);
          Diagnosed = true;
          break;
        }

        if (FirstInitializer && SecondInitializer) {
          unsigned FirstInitHash = ComputeODRHash(FirstInitializer);
          unsigned SecondInitHash = ComputeODRHash(SecondInitializer);
          if (FirstInitHash != SecondInitHash) {
            ODRDiagError(FirstField->getLocation(),
                         FirstField->getSourceRange(),
                         FieldDifferentInitializers)
                << FirstII << FirstInitializer->getSourceRange();
            ODRDiagNote(SecondField->getLocation(),
                        SecondField->getSourceRange(),
                        FieldDifferentInitializers)
                << SecondII << SecondInitializer->getSourceRange();
            Diagnosed = true;
            break;
          }
        }

        break;
      }
      case CXXMethod: {
        enum {
          DiagMethod,
          DiagConstructor,
          DiagDestructor,
        } FirstMethodType,
            SecondMethodType;
        auto GetMethodTypeForDiagnostics = [](const CXXMethodDecl* D) {
          if (isa<CXXConstructorDecl>(D)) return DiagConstructor;
          if (isa<CXXDestructorDecl>(D)) return DiagDestructor;
          return DiagMethod;
        };
        const CXXMethodDecl *FirstMethod = cast<CXXMethodDecl>(FirstDecl);
        const CXXMethodDecl *SecondMethod = cast<CXXMethodDecl>(SecondDecl);
        FirstMethodType = GetMethodTypeForDiagnostics(FirstMethod);
        SecondMethodType = GetMethodTypeForDiagnostics(SecondMethod);
        auto FirstName = FirstMethod->getDeclName();
        auto SecondName = SecondMethod->getDeclName();
        if (FirstMethodType != SecondMethodType || FirstName != SecondName) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodName)
              << FirstMethodType << FirstName;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodName)
              << SecondMethodType << SecondName;

          Diagnosed = true;
          break;
        }

        const bool FirstDeleted = FirstMethod->isDeletedAsWritten();
        const bool SecondDeleted = SecondMethod->isDeletedAsWritten();
        if (FirstDeleted != SecondDeleted) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodDeleted)
              << FirstMethodType << FirstName << FirstDeleted;

          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodDeleted)
              << SecondMethodType << SecondName << SecondDeleted;
          Diagnosed = true;
          break;
        }

        const bool FirstDefaulted = FirstMethod->isExplicitlyDefaulted();
        const bool SecondDefaulted = SecondMethod->isExplicitlyDefaulted();
        if (FirstDefaulted != SecondDefaulted) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodDefaulted)
              << FirstMethodType << FirstName << FirstDefaulted;

          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodDefaulted)
              << SecondMethodType << SecondName << SecondDefaulted;
          Diagnosed = true;
          break;
        }

        const bool FirstVirtual = FirstMethod->isVirtualAsWritten();
        const bool SecondVirtual = SecondMethod->isVirtualAsWritten();
        const bool FirstPure = FirstMethod->isPure();
        const bool SecondPure = SecondMethod->isPure();
        if ((FirstVirtual || SecondVirtual) &&
            (FirstVirtual != SecondVirtual || FirstPure != SecondPure)) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodVirtual)
              << FirstMethodType << FirstName << FirstPure << FirstVirtual;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodVirtual)
              << SecondMethodType << SecondName << SecondPure << SecondVirtual;
          Diagnosed = true;
          break;
        }

        // CXXMethodDecl::isStatic uses the canonical Decl.  With Decl merging,
        // FirstDecl is the canonical Decl of SecondDecl, so the storage
        // class needs to be checked instead.
        const auto FirstStorage = FirstMethod->getStorageClass();
        const auto SecondStorage = SecondMethod->getStorageClass();
        const bool FirstStatic = FirstStorage == SC_Static;
        const bool SecondStatic = SecondStorage == SC_Static;
        if (FirstStatic != SecondStatic) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodStatic)
              << FirstMethodType << FirstName << FirstStatic;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodStatic)
              << SecondMethodType << SecondName << SecondStatic;
          Diagnosed = true;
          break;
        }

        const bool FirstVolatile = FirstMethod->isVolatile();
        const bool SecondVolatile = SecondMethod->isVolatile();
        if (FirstVolatile != SecondVolatile) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodVolatile)
              << FirstMethodType << FirstName << FirstVolatile;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodVolatile)
              << SecondMethodType << SecondName << SecondVolatile;
          Diagnosed = true;
          break;
        }

        const bool FirstConst = FirstMethod->isConst();
        const bool SecondConst = SecondMethod->isConst();
        if (FirstConst != SecondConst) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodConst)
              << FirstMethodType << FirstName << FirstConst;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodConst)
              << SecondMethodType << SecondName << SecondConst;
          Diagnosed = true;
          break;
        }

        const bool FirstInline = FirstMethod->isInlineSpecified();
        const bool SecondInline = SecondMethod->isInlineSpecified();
        if (FirstInline != SecondInline) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodInline)
              << FirstMethodType << FirstName << FirstInline;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodInline)
              << SecondMethodType << SecondName << SecondInline;
          Diagnosed = true;
          break;
        }

        const unsigned FirstNumParameters = FirstMethod->param_size();
        const unsigned SecondNumParameters = SecondMethod->param_size();
        if (FirstNumParameters != SecondNumParameters) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodNumberParameters)
              << FirstMethodType << FirstName << FirstNumParameters;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodNumberParameters)
              << SecondMethodType << SecondName << SecondNumParameters;
          Diagnosed = true;
          break;
        }

        // Need this status boolean to know when break out of the switch.
        bool ParameterMismatch = false;
        for (unsigned I = 0; I < FirstNumParameters; ++I) {
          const ParmVarDecl *FirstParam = FirstMethod->getParamDecl(I);
          const ParmVarDecl *SecondParam = SecondMethod->getParamDecl(I);

          QualType FirstParamType = FirstParam->getType();
          QualType SecondParamType = SecondParam->getType();
          if (FirstParamType != SecondParamType &&
              ComputeQualTypeODRHash(FirstParamType) !=
                  ComputeQualTypeODRHash(SecondParamType)) {
            if (const DecayedType *ParamDecayedType =
                    FirstParamType->getAs<DecayedType>()) {
              ODRDiagError(FirstMethod->getLocation(),
                           FirstMethod->getSourceRange(), MethodParameterType)
                  << FirstMethodType << FirstName << (I + 1) << FirstParamType
                  << true << ParamDecayedType->getOriginalType();
            } else {
              ODRDiagError(FirstMethod->getLocation(),
                           FirstMethod->getSourceRange(), MethodParameterType)
                  << FirstMethodType << FirstName << (I + 1) << FirstParamType
                  << false;
            }

            if (const DecayedType *ParamDecayedType =
                    SecondParamType->getAs<DecayedType>()) {
              ODRDiagNote(SecondMethod->getLocation(),
                          SecondMethod->getSourceRange(), MethodParameterType)
                  << SecondMethodType << SecondName << (I + 1)
                  << SecondParamType << true
                  << ParamDecayedType->getOriginalType();
            } else {
              ODRDiagNote(SecondMethod->getLocation(),
                          SecondMethod->getSourceRange(), MethodParameterType)
                  << SecondMethodType << SecondName << (I + 1)
                  << SecondParamType << false;
            }
            ParameterMismatch = true;
            break;
          }

          DeclarationName FirstParamName = FirstParam->getDeclName();
          DeclarationName SecondParamName = SecondParam->getDeclName();
          if (FirstParamName != SecondParamName) {
            ODRDiagError(FirstMethod->getLocation(),
                         FirstMethod->getSourceRange(), MethodParameterName)
                << FirstMethodType << FirstName << (I + 1) << FirstParamName;
            ODRDiagNote(SecondMethod->getLocation(),
                        SecondMethod->getSourceRange(), MethodParameterName)
                << SecondMethodType << SecondName << (I + 1) << SecondParamName;
            ParameterMismatch = true;
            break;
          }

          const Expr *FirstInit = FirstParam->getInit();
          const Expr *SecondInit = SecondParam->getInit();
          if ((FirstInit == nullptr) != (SecondInit == nullptr)) {
            ODRDiagError(FirstMethod->getLocation(),
                         FirstMethod->getSourceRange(),
                         MethodParameterSingleDefaultArgument)
                << FirstMethodType << FirstName << (I + 1)
                << (FirstInit == nullptr)
                << (FirstInit ? FirstInit->getSourceRange() : SourceRange());
            ODRDiagNote(SecondMethod->getLocation(),
                        SecondMethod->getSourceRange(),
                        MethodParameterSingleDefaultArgument)
                << SecondMethodType << SecondName << (I + 1)
                << (SecondInit == nullptr)
                << (SecondInit ? SecondInit->getSourceRange() : SourceRange());
            ParameterMismatch = true;
            break;
          }

          if (FirstInit && SecondInit &&
              ComputeODRHash(FirstInit) != ComputeODRHash(SecondInit)) {
            ODRDiagError(FirstMethod->getLocation(),
                         FirstMethod->getSourceRange(),
                         MethodParameterDifferentDefaultArgument)
                << FirstMethodType << FirstName << (I + 1)
                << FirstInit->getSourceRange();
            ODRDiagNote(SecondMethod->getLocation(),
                        SecondMethod->getSourceRange(),
                        MethodParameterDifferentDefaultArgument)
                << SecondMethodType << SecondName << (I + 1)
                << SecondInit->getSourceRange();
            ParameterMismatch = true;
            break;

          }
        }

        if (ParameterMismatch) {
          Diagnosed = true;
          break;
        }

        const auto *FirstTemplateArgs =
            FirstMethod->getTemplateSpecializationArgs();
        const auto *SecondTemplateArgs =
            SecondMethod->getTemplateSpecializationArgs();

        if ((FirstTemplateArgs && !SecondTemplateArgs) ||
            (!FirstTemplateArgs && SecondTemplateArgs)) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodNoTemplateArguments)
              << FirstMethodType << FirstName << (FirstTemplateArgs != nullptr);
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodNoTemplateArguments)
              << SecondMethodType << SecondName
              << (SecondTemplateArgs != nullptr);

          Diagnosed = true;
          break;
        }

        if (FirstTemplateArgs && SecondTemplateArgs) {
          // Remove pack expansions from argument list.
          auto ExpandTemplateArgumentList =
              [](const TemplateArgumentList *TAL) {
                llvm::SmallVector<const TemplateArgument *, 8> ExpandedList;
                for (const TemplateArgument &TA : TAL->asArray()) {
                  if (TA.getKind() != TemplateArgument::Pack) {
                    ExpandedList.push_back(&TA);
                    continue;
                  }
                  for (const TemplateArgument &PackTA : TA.getPackAsArray()) {
                    ExpandedList.push_back(&PackTA);
                  }
                }
                return ExpandedList;
              };
          llvm::SmallVector<const TemplateArgument *, 8> FirstExpandedList =
              ExpandTemplateArgumentList(FirstTemplateArgs);
          llvm::SmallVector<const TemplateArgument *, 8> SecondExpandedList =
              ExpandTemplateArgumentList(SecondTemplateArgs);

          if (FirstExpandedList.size() != SecondExpandedList.size()) {
            ODRDiagError(FirstMethod->getLocation(),
                         FirstMethod->getSourceRange(),
                         MethodDifferentNumberTemplateArguments)
                << FirstMethodType << FirstName
                << (unsigned)FirstExpandedList.size();
            ODRDiagNote(SecondMethod->getLocation(),
                        SecondMethod->getSourceRange(),
                        MethodDifferentNumberTemplateArguments)
                << SecondMethodType << SecondName
                << (unsigned)SecondExpandedList.size();

            Diagnosed = true;
            break;
          }

          bool TemplateArgumentMismatch = false;
          for (unsigned i = 0, e = FirstExpandedList.size(); i != e; ++i) {
            const TemplateArgument &FirstTA = *FirstExpandedList[i],
                                   &SecondTA = *SecondExpandedList[i];
            if (ComputeTemplateArgumentODRHash(FirstTA) ==
                ComputeTemplateArgumentODRHash(SecondTA)) {
              continue;
            }

            ODRDiagError(FirstMethod->getLocation(),
                         FirstMethod->getSourceRange(),
                         MethodDifferentTemplateArgument)
                << FirstMethodType << FirstName << FirstTA << i + 1;
            ODRDiagNote(SecondMethod->getLocation(),
                        SecondMethod->getSourceRange(),
                        MethodDifferentTemplateArgument)
                << SecondMethodType << SecondName << SecondTA << i + 1;

            TemplateArgumentMismatch = true;
            break;
          }

          if (TemplateArgumentMismatch) {
            Diagnosed = true;
            break;
          }
        }

        // Compute the hash of the method as if it has no body.
        auto ComputeCXXMethodODRHash = [&Hash](const CXXMethodDecl *D) {
          Hash.clear();
          Hash.AddFunctionDecl(D, true /*SkipBody*/);
          return Hash.CalculateHash();
        };

        // Compare the hash generated to the hash stored.  A difference means
        // that a body was present in the original source.  Due to merging,
        // the stardard way of detecting a body will not work.
        const bool HasFirstBody =
            ComputeCXXMethodODRHash(FirstMethod) != FirstMethod->getODRHash();
        const bool HasSecondBody =
            ComputeCXXMethodODRHash(SecondMethod) != SecondMethod->getODRHash();

        if (HasFirstBody != HasSecondBody) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodSingleBody)
              << FirstMethodType << FirstName << HasFirstBody;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodSingleBody)
              << SecondMethodType << SecondName << HasSecondBody;
          Diagnosed = true;
          break;
        }

        if (HasFirstBody && HasSecondBody) {
          ODRDiagError(FirstMethod->getLocation(),
                       FirstMethod->getSourceRange(), MethodDifferentBody)
              << FirstMethodType << FirstName;
          ODRDiagNote(SecondMethod->getLocation(),
                      SecondMethod->getSourceRange(), MethodDifferentBody)
              << SecondMethodType << SecondName;
          Diagnosed = true;
          break;
        }

        break;
      }
      case TypeAlias:
      case TypeDef: {
        TypedefNameDecl *FirstTD = cast<TypedefNameDecl>(FirstDecl);
        TypedefNameDecl *SecondTD = cast<TypedefNameDecl>(SecondDecl);
        auto FirstName = FirstTD->getDeclName();
        auto SecondName = SecondTD->getDeclName();
        if (FirstName != SecondName) {
          ODRDiagError(FirstTD->getLocation(), FirstTD->getSourceRange(),
                       TypedefName)
              << (FirstDiffType == TypeAlias) << FirstName;
          ODRDiagNote(SecondTD->getLocation(), SecondTD->getSourceRange(),
                      TypedefName)
              << (FirstDiffType == TypeAlias) << SecondName;
          Diagnosed = true;
          break;
        }

        QualType FirstType = FirstTD->getUnderlyingType();
        QualType SecondType = SecondTD->getUnderlyingType();
        if (ComputeQualTypeODRHash(FirstType) !=
            ComputeQualTypeODRHash(SecondType)) {
          ODRDiagError(FirstTD->getLocation(), FirstTD->getSourceRange(),
                       TypedefType)
              << (FirstDiffType == TypeAlias) << FirstName << FirstType;
          ODRDiagNote(SecondTD->getLocation(), SecondTD->getSourceRange(),
                      TypedefType)
              << (FirstDiffType == TypeAlias) << SecondName << SecondType;
          Diagnosed = true;
          break;
        }
        break;
      }
      case Var: {
        VarDecl *FirstVD = cast<VarDecl>(FirstDecl);
        VarDecl *SecondVD = cast<VarDecl>(SecondDecl);
        auto FirstName = FirstVD->getDeclName();
        auto SecondName = SecondVD->getDeclName();
        if (FirstName != SecondName) {
          ODRDiagError(FirstVD->getLocation(), FirstVD->getSourceRange(),
                       VarName)
              << FirstName;
          ODRDiagNote(SecondVD->getLocation(), SecondVD->getSourceRange(),
                      VarName)
              << SecondName;
          Diagnosed = true;
          break;
        }

        QualType FirstType = FirstVD->getType();
        QualType SecondType = SecondVD->getType();
        if (ComputeQualTypeODRHash(FirstType) !=
                        ComputeQualTypeODRHash(SecondType)) {
          ODRDiagError(FirstVD->getLocation(), FirstVD->getSourceRange(),
                       VarType)
              << FirstName << FirstType;
          ODRDiagNote(SecondVD->getLocation(), SecondVD->getSourceRange(),
                      VarType)
              << SecondName << SecondType;
          Diagnosed = true;
          break;
        }

        const Expr *FirstInit = FirstVD->getInit();
        const Expr *SecondInit = SecondVD->getInit();
        if ((FirstInit == nullptr) != (SecondInit == nullptr)) {
          ODRDiagError(FirstVD->getLocation(), FirstVD->getSourceRange(),
                       VarSingleInitializer)
              << FirstName << (FirstInit == nullptr)
              << (FirstInit ? FirstInit->getSourceRange(): SourceRange());
          ODRDiagNote(SecondVD->getLocation(), SecondVD->getSourceRange(),
                      VarSingleInitializer)
              << SecondName << (SecondInit == nullptr)
              << (SecondInit ? SecondInit->getSourceRange() : SourceRange());
          Diagnosed = true;
          break;
        }

        if (FirstInit && SecondInit &&
            ComputeODRHash(FirstInit) != ComputeODRHash(SecondInit)) {
          ODRDiagError(FirstVD->getLocation(), FirstVD->getSourceRange(),
                       VarDifferentInitializer)
              << FirstName << FirstInit->getSourceRange();
          ODRDiagNote(SecondVD->getLocation(), SecondVD->getSourceRange(),
                      VarDifferentInitializer)
              << SecondName << SecondInit->getSourceRange();
          Diagnosed = true;
          break;
        }

        const bool FirstIsConstexpr = FirstVD->isConstexpr();
        const bool SecondIsConstexpr = SecondVD->isConstexpr();
        if (FirstIsConstexpr != SecondIsConstexpr) {
          ODRDiagError(FirstVD->getLocation(), FirstVD->getSourceRange(),
                       VarConstexpr)
              << FirstName << FirstIsConstexpr;
          ODRDiagNote(SecondVD->getLocation(), SecondVD->getSourceRange(),
                      VarConstexpr)
              << SecondName << SecondIsConstexpr;
          Diagnosed = true;
          break;
        }
        break;
      }
      case Friend: {
        FriendDecl *FirstFriend = cast<FriendDecl>(FirstDecl);
        FriendDecl *SecondFriend = cast<FriendDecl>(SecondDecl);

        NamedDecl *FirstND = FirstFriend->getFriendDecl();
        NamedDecl *SecondND = SecondFriend->getFriendDecl();

        TypeSourceInfo *FirstTSI = FirstFriend->getFriendType();
        TypeSourceInfo *SecondTSI = SecondFriend->getFriendType();

        if (FirstND && SecondND) {
          ODRDiagError(FirstFriend->getFriendLoc(),
                       FirstFriend->getSourceRange(), FriendFunction)
              << FirstND;
          ODRDiagNote(SecondFriend->getFriendLoc(),
                      SecondFriend->getSourceRange(), FriendFunction)
              << SecondND;

          Diagnosed = true;
          break;
        }

        if (FirstTSI && SecondTSI) {
          QualType FirstFriendType = FirstTSI->getType();
          QualType SecondFriendType = SecondTSI->getType();
          assert(ComputeQualTypeODRHash(FirstFriendType) !=
                 ComputeQualTypeODRHash(SecondFriendType));
          ODRDiagError(FirstFriend->getFriendLoc(),
                       FirstFriend->getSourceRange(), FriendType)
              << FirstFriendType;
          ODRDiagNote(SecondFriend->getFriendLoc(),
                      SecondFriend->getSourceRange(), FriendType)
              << SecondFriendType;
          Diagnosed = true;
          break;
        }

        ODRDiagError(FirstFriend->getFriendLoc(), FirstFriend->getSourceRange(),
                     FriendTypeFunction)
            << (FirstTSI == nullptr);
        ODRDiagNote(SecondFriend->getFriendLoc(),
                    SecondFriend->getSourceRange(), FriendTypeFunction)
            << (SecondTSI == nullptr);

        Diagnosed = true;
        break;
      }
      case FunctionTemplate: {
        FunctionTemplateDecl *FirstTemplate =
            cast<FunctionTemplateDecl>(FirstDecl);
        FunctionTemplateDecl *SecondTemplate =
            cast<FunctionTemplateDecl>(SecondDecl);

        TemplateParameterList *FirstTPL =
            FirstTemplate->getTemplateParameters();
        TemplateParameterList *SecondTPL =
            SecondTemplate->getTemplateParameters();

        if (FirstTPL->size() != SecondTPL->size()) {
          ODRDiagError(FirstTemplate->getLocation(),
                       FirstTemplate->getSourceRange(),
                       FunctionTemplateDifferentNumberParameters)
              << FirstTemplate << FirstTPL->size();
          ODRDiagNote(SecondTemplate->getLocation(),
                      SecondTemplate->getSourceRange(),
                      FunctionTemplateDifferentNumberParameters)
              << SecondTemplate  << SecondTPL->size();

          Diagnosed = true;
          break;
        }

        bool ParameterMismatch = false;
        for (unsigned i = 0, e = FirstTPL->size(); i != e; ++i) {
          NamedDecl *FirstParam = FirstTPL->getParam(i);
          NamedDecl *SecondParam = SecondTPL->getParam(i);

          if (FirstParam->getKind() != SecondParam->getKind()) {
            enum {
              TemplateTypeParameter,
              NonTypeTemplateParameter,
              TemplateTemplateParameter,
            };
            auto GetParamType = [](NamedDecl *D) {
              switch (D->getKind()) {
                default:
                  llvm_unreachable("Unexpected template parameter type");
                case Decl::TemplateTypeParm:
                  return TemplateTypeParameter;
                case Decl::NonTypeTemplateParm:
                  return NonTypeTemplateParameter;
                case Decl::TemplateTemplateParm:
                  return TemplateTemplateParameter;
              }
            };

            ODRDiagError(FirstTemplate->getLocation(),
                         FirstTemplate->getSourceRange(),
                         FunctionTemplateParameterDifferentKind)
                << FirstTemplate << (i + 1) << GetParamType(FirstParam);
            ODRDiagNote(SecondTemplate->getLocation(),
                        SecondTemplate->getSourceRange(),
                        FunctionTemplateParameterDifferentKind)
                << SecondTemplate << (i + 1) << GetParamType(SecondParam);

            ParameterMismatch = true;
            break;
          }

          if (FirstParam->getName() != SecondParam->getName()) {
            ODRDiagError(FirstTemplate->getLocation(),
                         FirstTemplate->getSourceRange(),
                         FunctionTemplateParameterName)
                << FirstTemplate << (i + 1) << (bool)FirstParam->getIdentifier()
                << FirstParam;
            ODRDiagNote(SecondTemplate->getLocation(),
                        SecondTemplate->getSourceRange(),
                        FunctionTemplateParameterName)
                << SecondTemplate << (i + 1)
                << (bool)SecondParam->getIdentifier() << SecondParam;
            ParameterMismatch = true;
            break;
          }

          if (isa<TemplateTypeParmDecl>(FirstParam) &&
              isa<TemplateTypeParmDecl>(SecondParam)) {
            TemplateTypeParmDecl *FirstTTPD =
                cast<TemplateTypeParmDecl>(FirstParam);
            TemplateTypeParmDecl *SecondTTPD =
                cast<TemplateTypeParmDecl>(SecondParam);
            bool HasFirstDefaultArgument =
                FirstTTPD->hasDefaultArgument() &&
                !FirstTTPD->defaultArgumentWasInherited();
            bool HasSecondDefaultArgument =
                SecondTTPD->hasDefaultArgument() &&
                !SecondTTPD->defaultArgumentWasInherited();
            if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplateParameterSingleDefaultArgument)
                  << FirstTemplate << (i + 1) << HasFirstDefaultArgument;
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplateParameterSingleDefaultArgument)
                  << SecondTemplate << (i + 1) << HasSecondDefaultArgument;
              ParameterMismatch = true;
              break;
            }

            if (HasFirstDefaultArgument && HasSecondDefaultArgument) {
              QualType FirstType = FirstTTPD->getDefaultArgument();
              QualType SecondType = SecondTTPD->getDefaultArgument();
              if (ComputeQualTypeODRHash(FirstType) !=
                  ComputeQualTypeODRHash(SecondType)) {
                ODRDiagError(FirstTemplate->getLocation(),
                             FirstTemplate->getSourceRange(),
                             FunctionTemplateParameterDifferentDefaultArgument)
                    << FirstTemplate << (i + 1) << FirstType;
                ODRDiagNote(SecondTemplate->getLocation(),
                            SecondTemplate->getSourceRange(),
                            FunctionTemplateParameterDifferentDefaultArgument)
                    << SecondTemplate << (i + 1) << SecondType;
                ParameterMismatch = true;
                break;
              }
            }

            if (FirstTTPD->isParameterPack() !=
                SecondTTPD->isParameterPack()) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplatePackParameter)
                  << FirstTemplate << (i + 1) << FirstTTPD->isParameterPack();
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplatePackParameter)
                  << SecondTemplate << (i + 1) << SecondTTPD->isParameterPack();
              ParameterMismatch = true;
              break;
            }
          }

          if (isa<TemplateTemplateParmDecl>(FirstParam) &&
              isa<TemplateTemplateParmDecl>(SecondParam)) {
            TemplateTemplateParmDecl *FirstTTPD =
                cast<TemplateTemplateParmDecl>(FirstParam);
            TemplateTemplateParmDecl *SecondTTPD =
                cast<TemplateTemplateParmDecl>(SecondParam);

            TemplateParameterList *FirstTPL =
                FirstTTPD->getTemplateParameters();
            TemplateParameterList *SecondTPL =
                SecondTTPD->getTemplateParameters();

            if (ComputeTemplateParameterListODRHash(FirstTPL) !=
                ComputeTemplateParameterListODRHash(SecondTPL)) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplateParameterDifferentType)
                  << FirstTemplate << (i + 1);
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplateParameterDifferentType)
                  << SecondTemplate << (i + 1);
              ParameterMismatch = true;
              break;
            }

            bool HasFirstDefaultArgument =
                FirstTTPD->hasDefaultArgument() &&
                !FirstTTPD->defaultArgumentWasInherited();
            bool HasSecondDefaultArgument =
                SecondTTPD->hasDefaultArgument() &&
                !SecondTTPD->defaultArgumentWasInherited();
            if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplateParameterSingleDefaultArgument)
                  << FirstTemplate << (i + 1) << HasFirstDefaultArgument;
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplateParameterSingleDefaultArgument)
                  << SecondTemplate << (i + 1) << HasSecondDefaultArgument;
              ParameterMismatch = true;
              break;
            }

            if (HasFirstDefaultArgument && HasSecondDefaultArgument) {
              TemplateArgument FirstTA =
                  FirstTTPD->getDefaultArgument().getArgument();
              TemplateArgument SecondTA =
                  SecondTTPD->getDefaultArgument().getArgument();
              if (ComputeTemplateArgumentODRHash(FirstTA) !=
                  ComputeTemplateArgumentODRHash(SecondTA)) {
                ODRDiagError(FirstTemplate->getLocation(),
                             FirstTemplate->getSourceRange(),
                             FunctionTemplateParameterDifferentDefaultArgument)
                    << FirstTemplate << (i + 1) << FirstTA;
                ODRDiagNote(SecondTemplate->getLocation(),
                            SecondTemplate->getSourceRange(),
                            FunctionTemplateParameterDifferentDefaultArgument)
                    << SecondTemplate << (i + 1) << SecondTA;
                ParameterMismatch = true;
                break;
              }
            }

            if (FirstTTPD->isParameterPack() !=
                SecondTTPD->isParameterPack()) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplatePackParameter)
                  << FirstTemplate << (i + 1) << FirstTTPD->isParameterPack();
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplatePackParameter)
                  << SecondTemplate << (i + 1) << SecondTTPD->isParameterPack();
              ParameterMismatch = true;
              break;
            }
          }

          if (isa<NonTypeTemplateParmDecl>(FirstParam) &&
              isa<NonTypeTemplateParmDecl>(SecondParam)) {
            NonTypeTemplateParmDecl *FirstNTTPD =
                cast<NonTypeTemplateParmDecl>(FirstParam);
            NonTypeTemplateParmDecl *SecondNTTPD =
                cast<NonTypeTemplateParmDecl>(SecondParam);

            QualType FirstType = FirstNTTPD->getType();
            QualType SecondType = SecondNTTPD->getType();
            if (ComputeQualTypeODRHash(FirstType) !=
                ComputeQualTypeODRHash(SecondType)) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplateParameterDifferentType)
                  << FirstTemplate << (i + 1);
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplateParameterDifferentType)
                  << SecondTemplate << (i + 1);
              ParameterMismatch = true;
              break;
            }

            bool HasFirstDefaultArgument =
                FirstNTTPD->hasDefaultArgument() &&
                !FirstNTTPD->defaultArgumentWasInherited();
            bool HasSecondDefaultArgument =
                SecondNTTPD->hasDefaultArgument() &&
                !SecondNTTPD->defaultArgumentWasInherited();
            if (HasFirstDefaultArgument != HasSecondDefaultArgument) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplateParameterSingleDefaultArgument)
                  << FirstTemplate << (i + 1) << HasFirstDefaultArgument;
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplateParameterSingleDefaultArgument)
                  << SecondTemplate << (i + 1) << HasSecondDefaultArgument;
              ParameterMismatch = true;
              break;
            }

            if (HasFirstDefaultArgument && HasSecondDefaultArgument) {
              Expr *FirstDefaultArgument = FirstNTTPD->getDefaultArgument();
              Expr *SecondDefaultArgument = SecondNTTPD->getDefaultArgument();
              if (ComputeODRHash(FirstDefaultArgument) !=
                  ComputeODRHash(SecondDefaultArgument)) {
                ODRDiagError(FirstTemplate->getLocation(),
                             FirstTemplate->getSourceRange(),
                             FunctionTemplateParameterDifferentDefaultArgument)
                    << FirstTemplate << (i + 1) << FirstDefaultArgument;
                ODRDiagNote(SecondTemplate->getLocation(),
                            SecondTemplate->getSourceRange(),
                            FunctionTemplateParameterDifferentDefaultArgument)
                    << SecondTemplate << (i + 1) << SecondDefaultArgument;
                ParameterMismatch = true;
                break;
              }
            }

            if (FirstNTTPD->isParameterPack() !=
                SecondNTTPD->isParameterPack()) {
              ODRDiagError(FirstTemplate->getLocation(),
                           FirstTemplate->getSourceRange(),
                           FunctionTemplatePackParameter)
                  << FirstTemplate << (i + 1) << FirstNTTPD->isParameterPack();
              ODRDiagNote(SecondTemplate->getLocation(),
                          SecondTemplate->getSourceRange(),
                          FunctionTemplatePackParameter)
                  << SecondTemplate << (i + 1)
                  << SecondNTTPD->isParameterPack();
              ParameterMismatch = true;
              break;
            }
          }
        }

        if (ParameterMismatch) {
          Diagnosed = true;
          break;
        }

        break;
      }
      }

      if (Diagnosed)
        continue;

      Diag(FirstDecl->getLocation(),
           diag::err_module_odr_violation_mismatch_decl_unknown)
          << FirstRecord << FirstModule.empty() << FirstModule << FirstDiffType
          << FirstDecl->getSourceRange();
      Diag(SecondDecl->getLocation(),
           diag::note_module_odr_violation_mismatch_decl_unknown)
          << SecondModule << FirstDiffType << SecondDecl->getSourceRange();
      Diagnosed = true;
    }

    if (!Diagnosed) {
      // All definitions are updates to the same declaration. This happens if a
      // module instantiates the declaration of a class template specialization
      // and two or more other modules instantiate its definition.
      //
      // FIXME: Indicate which modules had instantiations of this definition.
      // FIXME: How can this even happen?
      Diag(Merge.first->getLocation(),
           diag::err_module_odr_violation_different_instantiations)
        << Merge.first;
    }
  }

  // Issue ODR failures diagnostics for functions.
  for (auto &Merge : FunctionOdrMergeFailures) {
    enum ODRFunctionDifference {
      ReturnType,
      ParameterName,
      ParameterType,
      ParameterSingleDefaultArgument,
      ParameterDifferentDefaultArgument,
      FunctionBody,
    };

    FunctionDecl *FirstFunction = Merge.first;
    std::string FirstModule = getOwningModuleNameForDiagnostic(FirstFunction);

    bool Diagnosed = false;
    for (auto &SecondFunction : Merge.second) {

      if (FirstFunction == SecondFunction)
        continue;

      std::string SecondModule =
          getOwningModuleNameForDiagnostic(SecondFunction);

      auto ODRDiagError = [FirstFunction, &FirstModule,
                           this](SourceLocation Loc, SourceRange Range,
                                 ODRFunctionDifference DiffType) {
        return Diag(Loc, diag::err_module_odr_violation_function)
               << FirstFunction << FirstModule.empty() << FirstModule << Range
               << DiffType;
      };
      auto ODRDiagNote = [&SecondModule, this](SourceLocation Loc,
                                               SourceRange Range,
                                               ODRFunctionDifference DiffType) {
        return Diag(Loc, diag::note_module_odr_violation_function)
               << SecondModule << Range << DiffType;
      };

      if (ComputeQualTypeODRHash(FirstFunction->getReturnType()) !=
          ComputeQualTypeODRHash(SecondFunction->getReturnType())) {
        ODRDiagError(FirstFunction->getReturnTypeSourceRange().getBegin(),
                     FirstFunction->getReturnTypeSourceRange(), ReturnType)
            << FirstFunction->getReturnType();
        ODRDiagNote(SecondFunction->getReturnTypeSourceRange().getBegin(),
                    SecondFunction->getReturnTypeSourceRange(), ReturnType)
            << SecondFunction->getReturnType();
        Diagnosed = true;
        break;
      }

      assert(FirstFunction->param_size() == SecondFunction->param_size() &&
             "Merged functions with different number of parameters");

      auto ParamSize = FirstFunction->param_size();
      bool ParameterMismatch = false;
      for (unsigned I = 0; I < ParamSize; ++I) {
        auto *FirstParam = FirstFunction->getParamDecl(I);
        auto *SecondParam = SecondFunction->getParamDecl(I);

        assert(getContext().hasSameType(FirstParam->getType(),
                                      SecondParam->getType()) &&
               "Merged function has different parameter types.");

        if (FirstParam->getDeclName() != SecondParam->getDeclName()) {
          ODRDiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                       ParameterName)
              << I + 1 << FirstParam->getDeclName();
          ODRDiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
                      ParameterName)
              << I + 1 << SecondParam->getDeclName();
          ParameterMismatch = true;
          break;
        };

        QualType FirstParamType = FirstParam->getType();
        QualType SecondParamType = SecondParam->getType();
        if (FirstParamType != SecondParamType &&
            ComputeQualTypeODRHash(FirstParamType) !=
                ComputeQualTypeODRHash(SecondParamType)) {
          if (const DecayedType *ParamDecayedType =
                  FirstParamType->getAs<DecayedType>()) {
            ODRDiagError(FirstParam->getLocation(),
                         FirstParam->getSourceRange(), ParameterType)
                << (I + 1) << FirstParamType << true
                << ParamDecayedType->getOriginalType();
          } else {
            ODRDiagError(FirstParam->getLocation(),
                         FirstParam->getSourceRange(), ParameterType)
                << (I + 1) << FirstParamType << false;
          }

          if (const DecayedType *ParamDecayedType =
                  SecondParamType->getAs<DecayedType>()) {
            ODRDiagNote(SecondParam->getLocation(),
                        SecondParam->getSourceRange(), ParameterType)
                << (I + 1) << SecondParamType << true
                << ParamDecayedType->getOriginalType();
          } else {
            ODRDiagNote(SecondParam->getLocation(),
                        SecondParam->getSourceRange(), ParameterType)
                << (I + 1) << SecondParamType << false;
          }
          ParameterMismatch = true;
          break;
        }

        const Expr *FirstInit = FirstParam->getInit();
        const Expr *SecondInit = SecondParam->getInit();
        if ((FirstInit == nullptr) != (SecondInit == nullptr)) {
          ODRDiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                       ParameterSingleDefaultArgument)
              << (I + 1) << (FirstInit == nullptr)
              << (FirstInit ? FirstInit->getSourceRange() : SourceRange());
          ODRDiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
                      ParameterSingleDefaultArgument)
              << (I + 1) << (SecondInit == nullptr)
              << (SecondInit ? SecondInit->getSourceRange() : SourceRange());
          ParameterMismatch = true;
          break;
        }

        if (FirstInit && SecondInit &&
            ComputeODRHash(FirstInit) != ComputeODRHash(SecondInit)) {
          ODRDiagError(FirstParam->getLocation(), FirstParam->getSourceRange(),
                       ParameterDifferentDefaultArgument)
              << (I + 1) << FirstInit->getSourceRange();
          ODRDiagNote(SecondParam->getLocation(), SecondParam->getSourceRange(),
                      ParameterDifferentDefaultArgument)
              << (I + 1) << SecondInit->getSourceRange();
          ParameterMismatch = true;
          break;
        }

        assert(ComputeSubDeclODRHash(FirstParam) ==
                   ComputeSubDeclODRHash(SecondParam) &&
               "Undiagnosed parameter difference.");
      }

      if (ParameterMismatch) {
        Diagnosed = true;
        break;
      }

      // If no error has been generated before now, assume the problem is in
      // the body and generate a message.
      ODRDiagError(FirstFunction->getLocation(),
                   FirstFunction->getSourceRange(), FunctionBody);
      ODRDiagNote(SecondFunction->getLocation(),
                  SecondFunction->getSourceRange(), FunctionBody);
      Diagnosed = true;
      break;
    }
    (void)Diagnosed;
    assert(Diagnosed && "Unable to emit ODR diagnostic.");
  }

  // Issue ODR failures diagnostics for enums.
  for (auto &Merge : EnumOdrMergeFailures) {
    enum ODREnumDifference {
      SingleScopedEnum,
      EnumTagKeywordMismatch,
      SingleSpecifiedType,
      DifferentSpecifiedTypes,
      DifferentNumberEnumConstants,
      EnumConstantName,
      EnumConstantSingleInitilizer,
      EnumConstantDifferentInitilizer,
    };

    // If we've already pointed out a specific problem with this enum, don't
    // bother issuing a general "something's different" diagnostic.
    if (!DiagnosedOdrMergeFailures.insert(Merge.first).second)
      continue;

    EnumDecl *FirstEnum = Merge.first;
    std::string FirstModule = getOwningModuleNameForDiagnostic(FirstEnum);

    using DeclHashes =
        llvm::SmallVector<std::pair<EnumConstantDecl *, unsigned>, 4>;
    auto PopulateHashes = [&ComputeSubDeclODRHash, FirstEnum](
                              DeclHashes &Hashes, EnumDecl *Enum) {
      for (auto *D : Enum->decls()) {
        // Due to decl merging, the first EnumDecl is the parent of
        // Decls in both records.
        if (!ODRHash::isWhitelistedDecl(D, FirstEnum))
          continue;
        assert(isa<EnumConstantDecl>(D) && "Unexpected Decl kind");
        Hashes.emplace_back(cast<EnumConstantDecl>(D),
                            ComputeSubDeclODRHash(D));
      }
    };
    DeclHashes FirstHashes;
    PopulateHashes(FirstHashes, FirstEnum);
    bool Diagnosed = false;
    for (auto &SecondEnum : Merge.second) {

      if (FirstEnum == SecondEnum)
        continue;

      std::string SecondModule =
          getOwningModuleNameForDiagnostic(SecondEnum);

      auto ODRDiagError = [FirstEnum, &FirstModule,
                           this](SourceLocation Loc, SourceRange Range,
                                 ODREnumDifference DiffType) {
        return Diag(Loc, diag::err_module_odr_violation_enum)
               << FirstEnum << FirstModule.empty() << FirstModule << Range
               << DiffType;
      };
      auto ODRDiagNote = [&SecondModule, this](SourceLocation Loc,
                                               SourceRange Range,
                                               ODREnumDifference DiffType) {
        return Diag(Loc, diag::note_module_odr_violation_enum)
               << SecondModule << Range << DiffType;
      };

      if (FirstEnum->isScoped() != SecondEnum->isScoped()) {
        ODRDiagError(FirstEnum->getLocation(), FirstEnum->getSourceRange(),
                     SingleScopedEnum)
            << FirstEnum->isScoped();
        ODRDiagNote(SecondEnum->getLocation(), SecondEnum->getSourceRange(),
                    SingleScopedEnum)
            << SecondEnum->isScoped();
        Diagnosed = true;
        continue;
      }

      if (FirstEnum->isScoped() && SecondEnum->isScoped()) {
        if (FirstEnum->isScopedUsingClassTag() !=
            SecondEnum->isScopedUsingClassTag()) {
          ODRDiagError(FirstEnum->getLocation(), FirstEnum->getSourceRange(),
                       EnumTagKeywordMismatch)
              << FirstEnum->isScopedUsingClassTag();
          ODRDiagNote(SecondEnum->getLocation(), SecondEnum->getSourceRange(),
                      EnumTagKeywordMismatch)
              << SecondEnum->isScopedUsingClassTag();
          Diagnosed = true;
          continue;
        }
      }

      QualType FirstUnderlyingType =
          FirstEnum->getIntegerTypeSourceInfo()
              ? FirstEnum->getIntegerTypeSourceInfo()->getType()
              : QualType();
      QualType SecondUnderlyingType =
          SecondEnum->getIntegerTypeSourceInfo()
              ? SecondEnum->getIntegerTypeSourceInfo()->getType()
              : QualType();
      if (FirstUnderlyingType.isNull() != SecondUnderlyingType.isNull()) {
          ODRDiagError(FirstEnum->getLocation(), FirstEnum->getSourceRange(),
                       SingleSpecifiedType)
              << !FirstUnderlyingType.isNull();
          ODRDiagNote(SecondEnum->getLocation(), SecondEnum->getSourceRange(),
                      SingleSpecifiedType)
              << !SecondUnderlyingType.isNull();
          Diagnosed = true;
          continue;
      }

      if (!FirstUnderlyingType.isNull() && !SecondUnderlyingType.isNull()) {
        if (ComputeQualTypeODRHash(FirstUnderlyingType) !=
            ComputeQualTypeODRHash(SecondUnderlyingType)) {
          ODRDiagError(FirstEnum->getLocation(), FirstEnum->getSourceRange(),
                       DifferentSpecifiedTypes)
              << FirstUnderlyingType;
          ODRDiagNote(SecondEnum->getLocation(), SecondEnum->getSourceRange(),
                      DifferentSpecifiedTypes)
              << SecondUnderlyingType;
          Diagnosed = true;
          continue;
        }
      }

      DeclHashes SecondHashes;
      PopulateHashes(SecondHashes, SecondEnum);

      if (FirstHashes.size() != SecondHashes.size()) {
        ODRDiagError(FirstEnum->getLocation(), FirstEnum->getSourceRange(),
                     DifferentNumberEnumConstants)
            << (int)FirstHashes.size();
        ODRDiagNote(SecondEnum->getLocation(), SecondEnum->getSourceRange(),
                    DifferentNumberEnumConstants)
            << (int)SecondHashes.size();
        Diagnosed = true;
        continue;
      }

      for (unsigned I = 0; I < FirstHashes.size(); ++I) {
        if (FirstHashes[I].second == SecondHashes[I].second)
          continue;
        const EnumConstantDecl *FirstEnumConstant = FirstHashes[I].first;
        const EnumConstantDecl *SecondEnumConstant = SecondHashes[I].first;

        if (FirstEnumConstant->getDeclName() !=
            SecondEnumConstant->getDeclName()) {

          ODRDiagError(FirstEnumConstant->getLocation(),
                       FirstEnumConstant->getSourceRange(), EnumConstantName)
              << I + 1 << FirstEnumConstant;
          ODRDiagNote(SecondEnumConstant->getLocation(),
                      SecondEnumConstant->getSourceRange(), EnumConstantName)
              << I + 1 << SecondEnumConstant;
          Diagnosed = true;
          break;
        }

        const Expr *FirstInit = FirstEnumConstant->getInitExpr();
        const Expr *SecondInit = SecondEnumConstant->getInitExpr();
        if (!FirstInit && !SecondInit)
          continue;

        if (!FirstInit || !SecondInit) {
          ODRDiagError(FirstEnumConstant->getLocation(),
                       FirstEnumConstant->getSourceRange(),
                       EnumConstantSingleInitilizer)
              << I + 1 << FirstEnumConstant << (FirstInit != nullptr);
          ODRDiagNote(SecondEnumConstant->getLocation(),
                      SecondEnumConstant->getSourceRange(),
                      EnumConstantSingleInitilizer)
              << I + 1 << SecondEnumConstant << (SecondInit != nullptr);
          Diagnosed = true;
          break;
        }

        if (ComputeODRHash(FirstInit) != ComputeODRHash(SecondInit)) {
          ODRDiagError(FirstEnumConstant->getLocation(),
                       FirstEnumConstant->getSourceRange(),
                       EnumConstantDifferentInitilizer)
              << I + 1 << FirstEnumConstant;
          ODRDiagNote(SecondEnumConstant->getLocation(),
                      SecondEnumConstant->getSourceRange(),
                      EnumConstantDifferentInitilizer)
              << I + 1 << SecondEnumConstant;
          Diagnosed = true;
          break;
        }
      }
    }

    (void)Diagnosed;
    assert(Diagnosed && "Unable to emit ODR diagnostic.");
  }
}

void ASTReader::StartedDeserializing() {
  if (++NumCurrentElementsDeserializing == 1 && ReadTimer.get())
    ReadTimer->startTimer();
}

void ASTReader::FinishedDeserializing() {
  assert(NumCurrentElementsDeserializing &&
         "FinishedDeserializing not paired with StartedDeserializing");
  if (NumCurrentElementsDeserializing == 1) {
    // We decrease NumCurrentElementsDeserializing only after pending actions
    // are finished, to avoid recursively re-calling finishPendingActions().
    finishPendingActions();
  }
  --NumCurrentElementsDeserializing;

  if (NumCurrentElementsDeserializing == 0) {
    // Propagate exception specification and deduced type updates along
    // redeclaration chains.
    //
    // We do this now rather than in finishPendingActions because we want to
    // be able to walk the complete redeclaration chains of the updated decls.
    while (!PendingExceptionSpecUpdates.empty() ||
           !PendingDeducedTypeUpdates.empty()) {
      auto ESUpdates = std::move(PendingExceptionSpecUpdates);
      PendingExceptionSpecUpdates.clear();
      for (auto Update : ESUpdates) {
        ProcessingUpdatesRAIIObj ProcessingUpdates(*this);
        auto *FPT = Update.second->getType()->castAs<FunctionProtoType>();
        auto ESI = FPT->getExtProtoInfo().ExceptionSpec;
        if (auto *Listener = getContext().getASTMutationListener())
          Listener->ResolvedExceptionSpec(cast<FunctionDecl>(Update.second));
        for (auto *Redecl : Update.second->redecls())
          getContext().adjustExceptionSpec(cast<FunctionDecl>(Redecl), ESI);
      }

      auto DTUpdates = std::move(PendingDeducedTypeUpdates);
      PendingDeducedTypeUpdates.clear();
      for (auto Update : DTUpdates) {
        ProcessingUpdatesRAIIObj ProcessingUpdates(*this);
        // FIXME: If the return type is already deduced, check that it matches.
        getContext().adjustDeducedFunctionResultType(Update.first,
                                                     Update.second);
      }
    }

    if (ReadTimer)
      ReadTimer->stopTimer();

    diagnoseOdrViolations();

    // We are not in recursive loading, so it's safe to pass the "interesting"
    // decls to the consumer.
    if (Consumer)
      PassInterestingDeclsToConsumer();
  }
}

void ASTReader::pushExternalDeclIntoScope(NamedDecl *D, DeclarationName Name) {
  if (IdentifierInfo *II = Name.getAsIdentifierInfo()) {
    // Remove any fake results before adding any real ones.
    auto It = PendingFakeLookupResults.find(II);
    if (It != PendingFakeLookupResults.end()) {
      for (auto *ND : It->second)
        SemaObj->IdResolver.RemoveDecl(ND);
      // FIXME: this works around module+PCH performance issue.
      // Rather than erase the result from the map, which is O(n), just clear
      // the vector of NamedDecls.
      It->second.clear();
    }
  }

  if (SemaObj->IdResolver.tryAddTopLevelDecl(D, Name) && SemaObj->TUScope) {
    SemaObj->TUScope->AddDecl(D);
  } else if (SemaObj->TUScope) {
    // Adding the decl to IdResolver may have failed because it was already in
    // (even though it was not added in scope). If it is already in, make sure
    // it gets in the scope as well.
    if (std::find(SemaObj->IdResolver.begin(Name),
                  SemaObj->IdResolver.end(), D) != SemaObj->IdResolver.end())
      SemaObj->TUScope->AddDecl(D);
  }
}

ASTReader::ASTReader(Preprocessor &PP, ASTContext *Context,
                     const PCHContainerReader &PCHContainerRdr,
                     ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
                     StringRef isysroot, bool DisableValidation,
                     bool AllowASTWithCompilerErrors,
                     bool AllowConfigurationMismatch, bool ValidateSystemInputs,
                     bool UseGlobalIndex,
                     std::unique_ptr<llvm::Timer> ReadTimer)
    : Listener(DisableValidation
                   ? cast<ASTReaderListener>(new SimpleASTReaderListener(PP))
                   : cast<ASTReaderListener>(new PCHValidator(PP, *this))),
      SourceMgr(PP.getSourceManager()), FileMgr(PP.getFileManager()),
      PCHContainerRdr(PCHContainerRdr), Diags(PP.getDiagnostics()), PP(PP),
      ContextObj(Context),
      ModuleMgr(PP.getFileManager(), PP.getPCMCache(), PCHContainerRdr,
                PP.getHeaderSearchInfo()),
      PCMCache(PP.getPCMCache()), DummyIdResolver(PP),
      ReadTimer(std::move(ReadTimer)), isysroot(isysroot),
      DisableValidation(DisableValidation),
      AllowASTWithCompilerErrors(AllowASTWithCompilerErrors),
      AllowConfigurationMismatch(AllowConfigurationMismatch),
      ValidateSystemInputs(ValidateSystemInputs),
      UseGlobalIndex(UseGlobalIndex), CurrSwitchCaseStmts(&SwitchCaseStmts) {
  SourceMgr.setExternalSLocEntrySource(this);

  for (const auto &Ext : Extensions) {
    auto BlockName = Ext->getExtensionMetadata().BlockName;
    auto Known = ModuleFileExtensions.find(BlockName);
    if (Known != ModuleFileExtensions.end()) {
      Diags.Report(diag::warn_duplicate_module_file_extension)
        << BlockName;
      continue;
    }

    ModuleFileExtensions.insert({BlockName, Ext});
  }
}

ASTReader::~ASTReader() {
  if (OwnsDeserializationListener)
    delete DeserializationListener;
}

IdentifierResolver &ASTReader::getIdResolver() {
  return SemaObj ? SemaObj->IdResolver : DummyIdResolver;
}

unsigned ASTRecordReader::readRecord(llvm::BitstreamCursor &Cursor,
                                     unsigned AbbrevID) {
  Idx = 0;
  Record.clear();
  return Cursor.readRecord(AbbrevID, Record);
}
//===----------------------------------------------------------------------===//
//// OMPClauseReader implementation
////===----------------------------------------------------------------------===//

OMPClause *OMPClauseReader::readClause() {
  OMPClause *C;
  switch (Record.readInt()) {
  case OMPC_if:
    C = new (Context) OMPIfClause();
    break;
  case OMPC_final:
    C = new (Context) OMPFinalClause();
    break;
  case OMPC_num_threads:
    C = new (Context) OMPNumThreadsClause();
    break;
  case OMPC_safelen:
    C = new (Context) OMPSafelenClause();
    break;
  case OMPC_simdlen:
    C = new (Context) OMPSimdlenClause();
    break;
  case OMPC_collapse:
    C = new (Context) OMPCollapseClause();
    break;
  case OMPC_default:
    C = new (Context) OMPDefaultClause();
    break;
  case OMPC_proc_bind:
    C = new (Context) OMPProcBindClause();
    break;
  case OMPC_schedule:
    C = new (Context) OMPScheduleClause();
    break;
  case OMPC_ordered:
    C = OMPOrderedClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_nowait:
    C = new (Context) OMPNowaitClause();
    break;
  case OMPC_untied:
    C = new (Context) OMPUntiedClause();
    break;
  case OMPC_mergeable:
    C = new (Context) OMPMergeableClause();
    break;
  case OMPC_read:
    C = new (Context) OMPReadClause();
    break;
  case OMPC_write:
    C = new (Context) OMPWriteClause();
    break;
  case OMPC_update:
    C = new (Context) OMPUpdateClause();
    break;
  case OMPC_capture:
    C = new (Context) OMPCaptureClause();
    break;
  case OMPC_seq_cst:
    C = new (Context) OMPSeqCstClause();
    break;
  case OMPC_threads:
    C = new (Context) OMPThreadsClause();
    break;
  case OMPC_simd:
    C = new (Context) OMPSIMDClause();
    break;
  case OMPC_nogroup:
    C = new (Context) OMPNogroupClause();
    break;
  case OMPC_unified_address:
    C = new (Context) OMPUnifiedAddressClause();
    break;
  case OMPC_unified_shared_memory:
    C = new (Context) OMPUnifiedSharedMemoryClause();
    break;
  case OMPC_reverse_offload:
    C = new (Context) OMPReverseOffloadClause();
    break;
  case OMPC_dynamic_allocators:
    C = new (Context) OMPDynamicAllocatorsClause();
    break;
  case OMPC_atomic_default_mem_order:
    C = new (Context) OMPAtomicDefaultMemOrderClause();
    break;
 case OMPC_private:
    C = OMPPrivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_firstprivate:
    C = OMPFirstprivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_lastprivate:
    C = OMPLastprivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_shared:
    C = OMPSharedClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_reduction:
    C = OMPReductionClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_task_reduction:
    C = OMPTaskReductionClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_in_reduction:
    C = OMPInReductionClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_linear:
    C = OMPLinearClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_aligned:
    C = OMPAlignedClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_copyin:
    C = OMPCopyinClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_copyprivate:
    C = OMPCopyprivateClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_flush:
    C = OMPFlushClause::CreateEmpty(Context, Record.readInt());
    break;
  case OMPC_depend: {
    unsigned NumVars = Record.readInt();
    unsigned NumLoops = Record.readInt();
    C = OMPDependClause::CreateEmpty(Context, NumVars, NumLoops);
    break;
  }
  case OMPC_device:
    C = new (Context) OMPDeviceClause();
    break;
  case OMPC_map: {
    unsigned NumVars = Record.readInt();
    unsigned NumDeclarations = Record.readInt();
    unsigned NumLists = Record.readInt();
    unsigned NumComponents = Record.readInt();
    C = OMPMapClause::CreateEmpty(Context, NumVars, NumDeclarations, NumLists,
                                  NumComponents);
    break;
  }
  case OMPC_num_teams:
    C = new (Context) OMPNumTeamsClause();
    break;
  case OMPC_thread_limit:
    C = new (Context) OMPThreadLimitClause();
    break;
  case OMPC_priority:
    C = new (Context) OMPPriorityClause();
    break;
  case OMPC_grainsize:
    C = new (Context) OMPGrainsizeClause();
    break;
  case OMPC_num_tasks:
    C = new (Context) OMPNumTasksClause();
    break;
  case OMPC_hint:
    C = new (Context) OMPHintClause();
    break;
  case OMPC_dist_schedule:
    C = new (Context) OMPDistScheduleClause();
    break;
  case OMPC_defaultmap:
    C = new (Context) OMPDefaultmapClause();
    break;
  case OMPC_to: {
    unsigned NumVars = Record.readInt();
    unsigned NumDeclarations = Record.readInt();
    unsigned NumLists = Record.readInt();
    unsigned NumComponents = Record.readInt();
    C = OMPToClause::CreateEmpty(Context, NumVars, NumDeclarations, NumLists,
                                 NumComponents);
    break;
  }
  case OMPC_from: {
    unsigned NumVars = Record.readInt();
    unsigned NumDeclarations = Record.readInt();
    unsigned NumLists = Record.readInt();
    unsigned NumComponents = Record.readInt();
    C = OMPFromClause::CreateEmpty(Context, NumVars, NumDeclarations, NumLists,
                                   NumComponents);
    break;
  }
  case OMPC_use_device_ptr: {
    unsigned NumVars = Record.readInt();
    unsigned NumDeclarations = Record.readInt();
    unsigned NumLists = Record.readInt();
    unsigned NumComponents = Record.readInt();
    C = OMPUseDevicePtrClause::CreateEmpty(Context, NumVars, NumDeclarations,
                                           NumLists, NumComponents);
    break;
  }
  case OMPC_is_device_ptr: {
    unsigned NumVars = Record.readInt();
    unsigned NumDeclarations = Record.readInt();
    unsigned NumLists = Record.readInt();
    unsigned NumComponents = Record.readInt();
    C = OMPIsDevicePtrClause::CreateEmpty(Context, NumVars, NumDeclarations,
                                          NumLists, NumComponents);
    break;
  }
  }
  Visit(C);
  C->setLocStart(Record.readSourceLocation());
  C->setLocEnd(Record.readSourceLocation());

  return C;
}

void OMPClauseReader::VisitOMPClauseWithPreInit(OMPClauseWithPreInit *C) {
  C->setPreInitStmt(Record.readSubStmt(),
                    static_cast<OpenMPDirectiveKind>(Record.readInt()));
}

void OMPClauseReader::VisitOMPClauseWithPostUpdate(OMPClauseWithPostUpdate *C) {
  VisitOMPClauseWithPreInit(C);
  C->setPostUpdateExpr(Record.readSubExpr());
}

void OMPClauseReader::VisitOMPIfClause(OMPIfClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setNameModifier(static_cast<OpenMPDirectiveKind>(Record.readInt()));
  C->setNameModifierLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  C->setCondition(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPFinalClause(OMPFinalClause *C) {
  C->setCondition(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNumThreadsClause(OMPNumThreadsClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setNumThreads(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPSafelenClause(OMPSafelenClause *C) {
  C->setSafelen(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPSimdlenClause(OMPSimdlenClause *C) {
  C->setSimdlen(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPCollapseClause(OMPCollapseClause *C) {
  C->setNumForLoops(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDefaultClause(OMPDefaultClause *C) {
  C->setDefaultKind(
       static_cast<OpenMPDefaultClauseKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setDefaultKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPProcBindClause(OMPProcBindClause *C) {
  C->setProcBindKind(
       static_cast<OpenMPProcBindClauseKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setProcBindKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPScheduleClause(OMPScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setScheduleKind(
       static_cast<OpenMPScheduleClauseKind>(Record.readInt()));
  C->setFirstScheduleModifier(
      static_cast<OpenMPScheduleClauseModifier>(Record.readInt()));
  C->setSecondScheduleModifier(
      static_cast<OpenMPScheduleClauseModifier>(Record.readInt()));
  C->setChunkSize(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
  C->setFirstScheduleModifierLoc(Record.readSourceLocation());
  C->setSecondScheduleModifierLoc(Record.readSourceLocation());
  C->setScheduleKindLoc(Record.readSourceLocation());
  C->setCommaLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPOrderedClause(OMPOrderedClause *C) {
  C->setNumForLoops(Record.readSubExpr());
  for (unsigned I = 0, E = C->NumberOfLoops; I < E; ++I)
    C->setLoopNumIterations(I, Record.readSubExpr());
  for (unsigned I = 0, E = C->NumberOfLoops; I < E; ++I)
    C->setLoopCounter(I, Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNowaitClause(OMPNowaitClause *) {}

void OMPClauseReader::VisitOMPUntiedClause(OMPUntiedClause *) {}

void OMPClauseReader::VisitOMPMergeableClause(OMPMergeableClause *) {}

void OMPClauseReader::VisitOMPReadClause(OMPReadClause *) {}

void OMPClauseReader::VisitOMPWriteClause(OMPWriteClause *) {}

void OMPClauseReader::VisitOMPUpdateClause(OMPUpdateClause *) {}

void OMPClauseReader::VisitOMPCaptureClause(OMPCaptureClause *) {}

void OMPClauseReader::VisitOMPSeqCstClause(OMPSeqCstClause *) {}

void OMPClauseReader::VisitOMPThreadsClause(OMPThreadsClause *) {}

void OMPClauseReader::VisitOMPSIMDClause(OMPSIMDClause *) {}

void OMPClauseReader::VisitOMPNogroupClause(OMPNogroupClause *) {}

void OMPClauseReader::VisitOMPUnifiedAddressClause(OMPUnifiedAddressClause *) {}

void OMPClauseReader::VisitOMPUnifiedSharedMemoryClause(
    OMPUnifiedSharedMemoryClause *) {}

void OMPClauseReader::VisitOMPReverseOffloadClause(OMPReverseOffloadClause *) {}

void
OMPClauseReader::VisitOMPDynamicAllocatorsClause(OMPDynamicAllocatorsClause *) {
}

void OMPClauseReader::VisitOMPAtomicDefaultMemOrderClause(
    OMPAtomicDefaultMemOrderClause *C) {
  C->setAtomicDefaultMemOrderKind(
      static_cast<OpenMPAtomicDefaultMemOrderClauseKind>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setAtomicDefaultMemOrderKindKwLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPPrivateClause(OMPPrivateClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
}

void OMPClauseReader::VisitOMPFirstprivateClause(OMPFirstprivateClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setInits(Vars);
}

void OMPClauseReader::VisitOMPLastprivateClause(OMPLastprivateClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setSourceExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setDestinationExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setAssignmentOps(Vars);
}

void OMPClauseReader::VisitOMPSharedClause(OMPSharedClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
}

void OMPClauseReader::VisitOMPReductionClause(OMPReductionClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  NestedNameSpecifierLoc NNSL = Record.readNestedNameSpecifierLoc();
  DeclarationNameInfo DNI;
  Record.readDeclarationNameInfo(DNI);
  C->setQualifierLoc(NNSL);
  C->setNameInfo(DNI);

  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setLHSExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setRHSExprs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setReductionOps(Vars);
}

void OMPClauseReader::VisitOMPTaskReductionClause(OMPTaskReductionClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  NestedNameSpecifierLoc NNSL = Record.readNestedNameSpecifierLoc();
  DeclarationNameInfo DNI;
  Record.readDeclarationNameInfo(DNI);
  C->setQualifierLoc(NNSL);
  C->setNameInfo(DNI);

  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setLHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setRHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setReductionOps(Vars);
}

void OMPClauseReader::VisitOMPInReductionClause(OMPInReductionClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  NestedNameSpecifierLoc NNSL = Record.readNestedNameSpecifierLoc();
  DeclarationNameInfo DNI;
  Record.readDeclarationNameInfo(DNI);
  C->setQualifierLoc(NNSL);
  C->setNameInfo(DNI);

  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setLHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setRHSExprs(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setReductionOps(Vars);
  Vars.clear();
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setTaskgroupDescriptors(Vars);
}

void OMPClauseReader::VisitOMPLinearClause(OMPLinearClause *C) {
  VisitOMPClauseWithPostUpdate(C);
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  C->setModifier(static_cast<OpenMPLinearClauseKind>(Record.readInt()));
  C->setModifierLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivates(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setInits(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setUpdates(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setFinals(Vars);
  C->setStep(Record.readSubExpr());
  C->setCalcStep(Record.readSubExpr());
}

void OMPClauseReader::VisitOMPAlignedClause(OMPAlignedClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  C->setAlignment(Record.readSubExpr());
}

void OMPClauseReader::VisitOMPCopyinClause(OMPCopyinClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Exprs;
  Exprs.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setVarRefs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setSourceExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setDestinationExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setAssignmentOps(Exprs);
}

void OMPClauseReader::VisitOMPCopyprivateClause(OMPCopyprivateClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Exprs;
  Exprs.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setVarRefs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setSourceExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setDestinationExprs(Exprs);
  Exprs.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Exprs.push_back(Record.readSubExpr());
  C->setAssignmentOps(Exprs);
}

void OMPClauseReader::VisitOMPFlushClause(OMPFlushClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
}

void OMPClauseReader::VisitOMPDependClause(OMPDependClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  C->setDependencyKind(
      static_cast<OpenMPDependClauseKind>(Record.readInt()));
  C->setDependencyLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  unsigned NumVars = C->varlist_size();
  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned I = 0; I != NumVars; ++I)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I)
    C->setLoopData(I, Record.readSubExpr());
}

void OMPClauseReader::VisitOMPDeviceClause(OMPDeviceClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setDevice(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPMapClause(OMPMapClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  for (unsigned I = 0; I < OMPMapClause::NumberOfModifiers; ++I) {
    C->setMapTypeModifier(
        I, static_cast<OpenMPMapModifierKind>(Record.readInt()));
    C->setMapTypeModifierLoc(I, Record.readSourceLocation());
  }
  C->setMapType(
     static_cast<OpenMPMapClauseKind>(Record.readInt()));
  C->setMapLoc(Record.readSourceLocation());
  C->setColonLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.push_back(OMPClauseMappableExprCommon::MappableComponent(
        AssociatedExpr, AssociatedDecl));
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPNumTeamsClause(OMPNumTeamsClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setNumTeams(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPThreadLimitClause(OMPThreadLimitClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setThreadLimit(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPPriorityClause(OMPPriorityClause *C) {
  C->setPriority(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPGrainsizeClause(OMPGrainsizeClause *C) {
  C->setGrainsize(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPNumTasksClause(OMPNumTasksClause *C) {
  C->setNumTasks(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPHintClause(OMPHintClause *C) {
  C->setHint(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDistScheduleClause(OMPDistScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  C->setDistScheduleKind(
      static_cast<OpenMPDistScheduleClauseKind>(Record.readInt()));
  C->setChunkSize(Record.readSubExpr());
  C->setLParenLoc(Record.readSourceLocation());
  C->setDistScheduleKindLoc(Record.readSourceLocation());
  C->setCommaLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPDefaultmapClause(OMPDefaultmapClause *C) {
  C->setDefaultmapKind(
       static_cast<OpenMPDefaultmapClauseKind>(Record.readInt()));
  C->setDefaultmapModifier(
      static_cast<OpenMPDefaultmapClauseModifier>(Record.readInt()));
  C->setLParenLoc(Record.readSourceLocation());
  C->setDefaultmapModifierLoc(Record.readSourceLocation());
  C->setDefaultmapKindLoc(Record.readSourceLocation());
}

void OMPClauseReader::VisitOMPToClause(OMPToClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.push_back(OMPClauseMappableExprCommon::MappableComponent(
        AssociatedExpr, AssociatedDecl));
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPFromClause(OMPFromClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.push_back(OMPClauseMappableExprCommon::MappableComponent(
        AssociatedExpr, AssociatedDecl));
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPUseDevicePtrClause(OMPUseDevicePtrClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setPrivateCopies(Vars);
  Vars.clear();
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setInits(Vars);

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.push_back(OMPClauseMappableExprCommon::MappableComponent(
        AssociatedExpr, AssociatedDecl));
  }
  C->setComponents(Components, ListSizes);
}

void OMPClauseReader::VisitOMPIsDevicePtrClause(OMPIsDevicePtrClause *C) {
  C->setLParenLoc(Record.readSourceLocation());
  auto NumVars = C->varlist_size();
  auto UniqueDecls = C->getUniqueDeclarationsNum();
  auto TotalLists = C->getTotalComponentListNum();
  auto TotalComponents = C->getTotalComponentsNum();

  SmallVector<Expr *, 16> Vars;
  Vars.reserve(NumVars);
  for (unsigned i = 0; i != NumVars; ++i)
    Vars.push_back(Record.readSubExpr());
  C->setVarRefs(Vars);
  Vars.clear();

  SmallVector<ValueDecl *, 16> Decls;
  Decls.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    Decls.push_back(Record.readDeclAs<ValueDecl>());
  C->setUniqueDecls(Decls);

  SmallVector<unsigned, 16> ListsPerDecl;
  ListsPerDecl.reserve(UniqueDecls);
  for (unsigned i = 0; i < UniqueDecls; ++i)
    ListsPerDecl.push_back(Record.readInt());
  C->setDeclNumLists(ListsPerDecl);

  SmallVector<unsigned, 32> ListSizes;
  ListSizes.reserve(TotalLists);
  for (unsigned i = 0; i < TotalLists; ++i)
    ListSizes.push_back(Record.readInt());
  C->setComponentListSizes(ListSizes);

  SmallVector<OMPClauseMappableExprCommon::MappableComponent, 32> Components;
  Components.reserve(TotalComponents);
  for (unsigned i = 0; i < TotalComponents; ++i) {
    Expr *AssociatedExpr = Record.readSubExpr();
    auto *AssociatedDecl = Record.readDeclAs<ValueDecl>();
    Components.push_back(OMPClauseMappableExprCommon::MappableComponent(
        AssociatedExpr, AssociatedDecl));
  }
  C->setComponents(Components, ListSizes);
}
