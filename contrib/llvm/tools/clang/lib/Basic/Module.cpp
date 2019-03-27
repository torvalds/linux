//===- Module.cpp - Describe a module -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Module class, which describes a module in the source
// code.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Module.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace clang;

Module::Module(StringRef Name, SourceLocation DefinitionLoc, Module *Parent,
               bool IsFramework, bool IsExplicit, unsigned VisibilityID)
    : Name(Name), DefinitionLoc(DefinitionLoc), Parent(Parent),
      VisibilityID(VisibilityID), IsMissingRequirement(false),
      HasIncompatibleModuleFile(false), IsAvailable(true),
      IsFromModuleFile(false), IsFramework(IsFramework), IsExplicit(IsExplicit),
      IsSystem(false), IsExternC(false), IsInferred(false),
      InferSubmodules(false), InferExplicitSubmodules(false),
      InferExportWildcard(false), ConfigMacrosExhaustive(false),
      NoUndeclaredIncludes(false), ModuleMapIsPrivate(false),
      NameVisibility(Hidden) {
  if (Parent) {
    if (!Parent->isAvailable())
      IsAvailable = false;
    if (Parent->IsSystem)
      IsSystem = true;
    if (Parent->IsExternC)
      IsExternC = true;
    if (Parent->NoUndeclaredIncludes)
      NoUndeclaredIncludes = true;
    if (Parent->ModuleMapIsPrivate)
      ModuleMapIsPrivate = true;
    IsMissingRequirement = Parent->IsMissingRequirement;

    Parent->SubModuleIndex[Name] = Parent->SubModules.size();
    Parent->SubModules.push_back(this);
  }
}

Module::~Module() {
  for (submodule_iterator I = submodule_begin(), IEnd = submodule_end();
       I != IEnd; ++I) {
    delete *I;
  }
}

static bool isPlatformEnvironment(const TargetInfo &Target, StringRef Feature) {
  StringRef Platform = Target.getPlatformName();
  StringRef Env = Target.getTriple().getEnvironmentName();

  // Attempt to match platform and environment.
  if (Platform == Feature || Target.getTriple().getOSName() == Feature ||
      Env == Feature)
    return true;

  auto CmpPlatformEnv = [](StringRef LHS, StringRef RHS) {
    auto Pos = LHS.find("-");
    if (Pos == StringRef::npos)
      return false;
    SmallString<128> NewLHS = LHS.slice(0, Pos);
    NewLHS += LHS.slice(Pos+1, LHS.size());
    return NewLHS == RHS;
  };

  SmallString<128> PlatformEnv = Target.getTriple().getOSAndEnvironmentName();
  // Darwin has different but equivalent variants for simulators, example:
  //   1. x86_64-apple-ios-simulator
  //   2. x86_64-apple-iossimulator
  // where both are valid examples of the same platform+environment but in the
  // variant (2) the simulator is hardcoded as part of the platform name. Both
  // forms above should match for "iossimulator" requirement.
  if (Target.getTriple().isOSDarwin() && PlatformEnv.endswith("simulator"))
    return PlatformEnv == Feature || CmpPlatformEnv(PlatformEnv, Feature);

  return PlatformEnv == Feature;
}

/// Determine whether a translation unit built using the current
/// language options has the given feature.
static bool hasFeature(StringRef Feature, const LangOptions &LangOpts,
                       const TargetInfo &Target) {
  bool HasFeature = llvm::StringSwitch<bool>(Feature)
                        .Case("altivec", LangOpts.AltiVec)
                        .Case("blocks", LangOpts.Blocks)
                        .Case("coroutines", LangOpts.CoroutinesTS)
                        .Case("cplusplus", LangOpts.CPlusPlus)
                        .Case("cplusplus11", LangOpts.CPlusPlus11)
                        .Case("cplusplus14", LangOpts.CPlusPlus14)
                        .Case("cplusplus17", LangOpts.CPlusPlus17)
                        .Case("c99", LangOpts.C99)
                        .Case("c11", LangOpts.C11)
                        .Case("c17", LangOpts.C17)
                        .Case("freestanding", LangOpts.Freestanding)
                        .Case("gnuinlineasm", LangOpts.GNUAsm)
                        .Case("objc", LangOpts.ObjC)
                        .Case("objc_arc", LangOpts.ObjCAutoRefCount)
                        .Case("opencl", LangOpts.OpenCL)
                        .Case("tls", Target.isTLSSupported())
                        .Case("zvector", LangOpts.ZVector)
                        .Default(Target.hasFeature(Feature) ||
                                 isPlatformEnvironment(Target, Feature));
  if (!HasFeature)
    HasFeature = std::find(LangOpts.ModuleFeatures.begin(),
                           LangOpts.ModuleFeatures.end(),
                           Feature) != LangOpts.ModuleFeatures.end();
  return HasFeature;
}

bool Module::isAvailable(const LangOptions &LangOpts, const TargetInfo &Target,
                         Requirement &Req,
                         UnresolvedHeaderDirective &MissingHeader,
                         Module *&ShadowingModule) const {
  if (IsAvailable)
    return true;

  for (const Module *Current = this; Current; Current = Current->Parent) {
    if (Current->ShadowingModule) {
      ShadowingModule = Current->ShadowingModule;
      return false;
    }
    for (unsigned I = 0, N = Current->Requirements.size(); I != N; ++I) {
      if (hasFeature(Current->Requirements[I].first, LangOpts, Target) !=
              Current->Requirements[I].second) {
        Req = Current->Requirements[I];
        return false;
      }
    }
    if (!Current->MissingHeaders.empty()) {
      MissingHeader = Current->MissingHeaders.front();
      return false;
    }
  }

  llvm_unreachable("could not find a reason why module is unavailable");
}

bool Module::isSubModuleOf(const Module *Other) const {
  const Module *This = this;
  do {
    if (This == Other)
      return true;

    This = This->Parent;
  } while (This);

  return false;
}

const Module *Module::getTopLevelModule() const {
  const Module *Result = this;
  while (Result->Parent)
    Result = Result->Parent;

  return Result;
}

static StringRef getModuleNameFromComponent(
    const std::pair<std::string, SourceLocation> &IdComponent) {
  return IdComponent.first;
}

static StringRef getModuleNameFromComponent(StringRef R) { return R; }

template<typename InputIter>
static void printModuleId(raw_ostream &OS, InputIter Begin, InputIter End,
                          bool AllowStringLiterals = true) {
  for (InputIter It = Begin; It != End; ++It) {
    if (It != Begin)
      OS << ".";

    StringRef Name = getModuleNameFromComponent(*It);
    if (!AllowStringLiterals || isValidIdentifier(Name))
      OS << Name;
    else {
      OS << '"';
      OS.write_escaped(Name);
      OS << '"';
    }
  }
}

template<typename Container>
static void printModuleId(raw_ostream &OS, const Container &C) {
  return printModuleId(OS, C.begin(), C.end());
}

std::string Module::getFullModuleName(bool AllowStringLiterals) const {
  SmallVector<StringRef, 2> Names;

  // Build up the set of module names (from innermost to outermost).
  for (const Module *M = this; M; M = M->Parent)
    Names.push_back(M->Name);

  std::string Result;

  llvm::raw_string_ostream Out(Result);
  printModuleId(Out, Names.rbegin(), Names.rend(), AllowStringLiterals);
  Out.flush();

  return Result;
}

bool Module::fullModuleNameIs(ArrayRef<StringRef> nameParts) const {
  for (const Module *M = this; M; M = M->Parent) {
    if (nameParts.empty() || M->Name != nameParts.back())
      return false;
    nameParts = nameParts.drop_back();
  }
  return nameParts.empty();
}

Module::DirectoryName Module::getUmbrellaDir() const {
  if (Header U = getUmbrellaHeader())
    return {"", U.Entry->getDir()};

  return {UmbrellaAsWritten, Umbrella.dyn_cast<const DirectoryEntry *>()};
}

ArrayRef<const FileEntry *> Module::getTopHeaders(FileManager &FileMgr) {
  if (!TopHeaderNames.empty()) {
    for (std::vector<std::string>::iterator
           I = TopHeaderNames.begin(), E = TopHeaderNames.end(); I != E; ++I) {
      if (const FileEntry *FE = FileMgr.getFile(*I))
        TopHeaders.insert(FE);
    }
    TopHeaderNames.clear();
  }

  return llvm::makeArrayRef(TopHeaders.begin(), TopHeaders.end());
}

bool Module::directlyUses(const Module *Requested) const {
  auto *Top = getTopLevelModule();

  // A top-level module implicitly uses itself.
  if (Requested->isSubModuleOf(Top))
    return true;

  for (auto *Use : Top->DirectUses)
    if (Requested->isSubModuleOf(Use))
      return true;

  // Anyone is allowed to use our builtin stddef.h and its accompanying module.
  if (!Requested->Parent && Requested->Name == "_Builtin_stddef_max_align_t")
    return true;

  return false;
}

void Module::addRequirement(StringRef Feature, bool RequiredState,
                            const LangOptions &LangOpts,
                            const TargetInfo &Target) {
  Requirements.push_back(Requirement(Feature, RequiredState));

  // If this feature is currently available, we're done.
  if (hasFeature(Feature, LangOpts, Target) == RequiredState)
    return;

  markUnavailable(/*MissingRequirement*/true);
}

void Module::markUnavailable(bool MissingRequirement) {
  auto needUpdate = [MissingRequirement](Module *M) {
    return M->IsAvailable || (!M->IsMissingRequirement && MissingRequirement);
  };

  if (!needUpdate(this))
    return;

  SmallVector<Module *, 2> Stack;
  Stack.push_back(this);
  while (!Stack.empty()) {
    Module *Current = Stack.back();
    Stack.pop_back();

    if (!needUpdate(Current))
      continue;

    Current->IsAvailable = false;
    Current->IsMissingRequirement |= MissingRequirement;
    for (submodule_iterator Sub = Current->submodule_begin(),
                         SubEnd = Current->submodule_end();
         Sub != SubEnd; ++Sub) {
      if (needUpdate(*Sub))
        Stack.push_back(*Sub);
    }
  }
}

Module *Module::findSubmodule(StringRef Name) const {
  llvm::StringMap<unsigned>::const_iterator Pos = SubModuleIndex.find(Name);
  if (Pos == SubModuleIndex.end())
    return nullptr;

  return SubModules[Pos->getValue()];
}

void Module::getExportedModules(SmallVectorImpl<Module *> &Exported) const {
  // All non-explicit submodules are exported.
  for (std::vector<Module *>::const_iterator I = SubModules.begin(),
                                             E = SubModules.end();
       I != E; ++I) {
    Module *Mod = *I;
    if (!Mod->IsExplicit)
      Exported.push_back(Mod);
  }

  // Find re-exported modules by filtering the list of imported modules.
  bool AnyWildcard = false;
  bool UnrestrictedWildcard = false;
  SmallVector<Module *, 4> WildcardRestrictions;
  for (unsigned I = 0, N = Exports.size(); I != N; ++I) {
    Module *Mod = Exports[I].getPointer();
    if (!Exports[I].getInt()) {
      // Export a named module directly; no wildcards involved.
      Exported.push_back(Mod);

      continue;
    }

    // Wildcard export: export all of the imported modules that match
    // the given pattern.
    AnyWildcard = true;
    if (UnrestrictedWildcard)
      continue;

    if (Module *Restriction = Exports[I].getPointer())
      WildcardRestrictions.push_back(Restriction);
    else {
      WildcardRestrictions.clear();
      UnrestrictedWildcard = true;
    }
  }

  // If there were any wildcards, push any imported modules that were
  // re-exported by the wildcard restriction.
  if (!AnyWildcard)
    return;

  for (unsigned I = 0, N = Imports.size(); I != N; ++I) {
    Module *Mod = Imports[I];
    bool Acceptable = UnrestrictedWildcard;
    if (!Acceptable) {
      // Check whether this module meets one of the restrictions.
      for (unsigned R = 0, NR = WildcardRestrictions.size(); R != NR; ++R) {
        Module *Restriction = WildcardRestrictions[R];
        if (Mod == Restriction || Mod->isSubModuleOf(Restriction)) {
          Acceptable = true;
          break;
        }
      }
    }

    if (!Acceptable)
      continue;

    Exported.push_back(Mod);
  }
}

void Module::buildVisibleModulesCache() const {
  assert(VisibleModulesCache.empty() && "cache does not need building");

  // This module is visible to itself.
  VisibleModulesCache.insert(this);

  // Every imported module is visible.
  SmallVector<Module *, 16> Stack(Imports.begin(), Imports.end());
  while (!Stack.empty()) {
    Module *CurrModule = Stack.pop_back_val();

    // Every module transitively exported by an imported module is visible.
    if (VisibleModulesCache.insert(CurrModule).second)
      CurrModule->getExportedModules(Stack);
  }
}

void Module::print(raw_ostream &OS, unsigned Indent) const {
  OS.indent(Indent);
  if (IsFramework)
    OS << "framework ";
  if (IsExplicit)
    OS << "explicit ";
  OS << "module ";
  printModuleId(OS, &Name, &Name + 1);

  if (IsSystem || IsExternC) {
    OS.indent(Indent + 2);
    if (IsSystem)
      OS << " [system]";
    if (IsExternC)
      OS << " [extern_c]";
  }

  OS << " {\n";

  if (!Requirements.empty()) {
    OS.indent(Indent + 2);
    OS << "requires ";
    for (unsigned I = 0, N = Requirements.size(); I != N; ++I) {
      if (I)
        OS << ", ";
      if (!Requirements[I].second)
        OS << "!";
      OS << Requirements[I].first;
    }
    OS << "\n";
  }

  if (Header H = getUmbrellaHeader()) {
    OS.indent(Indent + 2);
    OS << "umbrella header \"";
    OS.write_escaped(H.NameAsWritten);
    OS << "\"\n";
  } else if (DirectoryName D = getUmbrellaDir()) {
    OS.indent(Indent + 2);
    OS << "umbrella \"";
    OS.write_escaped(D.NameAsWritten);
    OS << "\"\n";
  }

  if (!ConfigMacros.empty() || ConfigMacrosExhaustive) {
    OS.indent(Indent + 2);
    OS << "config_macros ";
    if (ConfigMacrosExhaustive)
      OS << "[exhaustive]";
    for (unsigned I = 0, N = ConfigMacros.size(); I != N; ++I) {
      if (I)
        OS << ", ";
      OS << ConfigMacros[I];
    }
    OS << "\n";
  }

  struct {
    StringRef Prefix;
    HeaderKind Kind;
  } Kinds[] = {{"", HK_Normal},
               {"textual ", HK_Textual},
               {"private ", HK_Private},
               {"private textual ", HK_PrivateTextual},
               {"exclude ", HK_Excluded}};

  for (auto &K : Kinds) {
    assert(&K == &Kinds[K.Kind] && "kinds in wrong order");
    for (auto &H : Headers[K.Kind]) {
      OS.indent(Indent + 2);
      OS << K.Prefix << "header \"";
      OS.write_escaped(H.NameAsWritten);
      OS << "\" { size " << H.Entry->getSize()
         << " mtime " << H.Entry->getModificationTime() << " }\n";
    }
  }
  for (auto *Unresolved : {&UnresolvedHeaders, &MissingHeaders}) {
    for (auto &U : *Unresolved) {
      OS.indent(Indent + 2);
      OS << Kinds[U.Kind].Prefix << "header \"";
      OS.write_escaped(U.FileName);
      OS << "\"";
      if (U.Size || U.ModTime) {
        OS << " {";
        if (U.Size)
          OS << " size " << *U.Size;
        if (U.ModTime)
          OS << " mtime " << *U.ModTime;
        OS << " }";
      }
      OS << "\n";
    }
  }

  if (!ExportAsModule.empty()) {
    OS.indent(Indent + 2);
    OS << "export_as" << ExportAsModule << "\n";
  }

  for (submodule_const_iterator MI = submodule_begin(), MIEnd = submodule_end();
       MI != MIEnd; ++MI)
    // Print inferred subframework modules so that we don't need to re-infer
    // them (requires expensive directory iteration + stat calls) when we build
    // the module. Regular inferred submodules are OK, as we need to look at all
    // those header files anyway.
    if (!(*MI)->IsInferred || (*MI)->IsFramework)
      (*MI)->print(OS, Indent + 2);

  for (unsigned I = 0, N = Exports.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "export ";
    if (Module *Restriction = Exports[I].getPointer()) {
      OS << Restriction->getFullModuleName(true);
      if (Exports[I].getInt())
        OS << ".*";
    } else {
      OS << "*";
    }
    OS << "\n";
  }

  for (unsigned I = 0, N = UnresolvedExports.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "export ";
    printModuleId(OS, UnresolvedExports[I].Id);
    if (UnresolvedExports[I].Wildcard)
      OS << (UnresolvedExports[I].Id.empty() ? "*" : ".*");
    OS << "\n";
  }

  for (unsigned I = 0, N = DirectUses.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "use ";
    OS << DirectUses[I]->getFullModuleName(true);
    OS << "\n";
  }

  for (unsigned I = 0, N = UnresolvedDirectUses.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "use ";
    printModuleId(OS, UnresolvedDirectUses[I]);
    OS << "\n";
  }

  for (unsigned I = 0, N = LinkLibraries.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "link ";
    if (LinkLibraries[I].IsFramework)
      OS << "framework ";
    OS << "\"";
    OS.write_escaped(LinkLibraries[I].Library);
    OS << "\"";
  }

  for (unsigned I = 0, N = UnresolvedConflicts.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "conflict ";
    printModuleId(OS, UnresolvedConflicts[I].Id);
    OS << ", \"";
    OS.write_escaped(UnresolvedConflicts[I].Message);
    OS << "\"\n";
  }

  for (unsigned I = 0, N = Conflicts.size(); I != N; ++I) {
    OS.indent(Indent + 2);
    OS << "conflict ";
    OS << Conflicts[I].Other->getFullModuleName(true);
    OS << ", \"";
    OS.write_escaped(Conflicts[I].Message);
    OS << "\"\n";
  }

  if (InferSubmodules) {
    OS.indent(Indent + 2);
    if (InferExplicitSubmodules)
      OS << "explicit ";
    OS << "module * {\n";
    if (InferExportWildcard) {
      OS.indent(Indent + 4);
      OS << "export *\n";
    }
    OS.indent(Indent + 2);
    OS << "}\n";
  }

  OS.indent(Indent);
  OS << "}\n";
}

LLVM_DUMP_METHOD void Module::dump() const {
  print(llvm::errs());
}

void VisibleModuleSet::setVisible(Module *M, SourceLocation Loc,
                                  VisibleCallback Vis, ConflictCallback Cb) {
  assert(Loc.isValid() && "setVisible expects a valid import location");
  if (isVisible(M))
    return;

  ++Generation;

  struct Visiting {
    Module *M;
    Visiting *ExportedBy;
  };

  std::function<void(Visiting)> VisitModule = [&](Visiting V) {
    // Nothing to do for a module that's already visible.
    unsigned ID = V.M->getVisibilityID();
    if (ImportLocs.size() <= ID)
      ImportLocs.resize(ID + 1);
    else if (ImportLocs[ID].isValid())
      return;

    ImportLocs[ID] = Loc;
    Vis(M);

    // Make any exported modules visible.
    SmallVector<Module *, 16> Exports;
    V.M->getExportedModules(Exports);
    for (Module *E : Exports) {
      // Don't recurse to unavailable submodules.
      if (E->isAvailable())
        VisitModule({E, &V});
    }

    for (auto &C : V.M->Conflicts) {
      if (isVisible(C.Other)) {
        llvm::SmallVector<Module*, 8> Path;
        for (Visiting *I = &V; I; I = I->ExportedBy)
          Path.push_back(I->M);
        Cb(Path, C.Other, C.Message);
      }
    }
  };
  VisitModule({M, nullptr});
}
