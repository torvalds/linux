//===- Module.h - Describe a module -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::Module class, which describes a module in the
/// source code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_MODULE_H
#define LLVM_CLANG_BASIC_MODULE_H

#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class raw_ostream;

} // namespace llvm

namespace clang {

class LangOptions;
class TargetInfo;

/// Describes the name of a module.
using ModuleId = SmallVector<std::pair<std::string, SourceLocation>, 2>;

/// The signature of a module, which is a hash of the AST content.
struct ASTFileSignature : std::array<uint32_t, 5> {
  ASTFileSignature(std::array<uint32_t, 5> S = {{0}})
      : std::array<uint32_t, 5>(std::move(S)) {}

  explicit operator bool() const {
    return *this != std::array<uint32_t, 5>({{0}});
  }
};

/// Describes a module or submodule.
class Module {
public:
  /// The name of this module.
  std::string Name;

  /// The location of the module definition.
  SourceLocation DefinitionLoc;

  enum ModuleKind {
    /// This is a module that was defined by a module map and built out
    /// of header files.
    ModuleMapModule,

    /// This is a C++ Modules TS module interface unit.
    ModuleInterfaceUnit,

    /// This is a fragment of the global module within some C++ Modules
    /// TS module.
    GlobalModuleFragment,
  };

  /// The kind of this module.
  ModuleKind Kind = ModuleMapModule;

  /// The parent of this module. This will be NULL for the top-level
  /// module.
  Module *Parent;

  /// The build directory of this module. This is the directory in
  /// which the module is notionally built, and relative to which its headers
  /// are found.
  const DirectoryEntry *Directory = nullptr;

  /// The presumed file name for the module map defining this module.
  /// Only non-empty when building from preprocessed source.
  std::string PresumedModuleMapFile;

  /// The umbrella header or directory.
  llvm::PointerUnion<const DirectoryEntry *, const FileEntry *> Umbrella;

  /// The module signature.
  ASTFileSignature Signature;

  /// The name of the umbrella entry, as written in the module map.
  std::string UmbrellaAsWritten;

  /// The module through which entities defined in this module will
  /// eventually be exposed, for use in "private" modules.
  std::string ExportAsModule;

private:
  /// The submodules of this module, indexed by name.
  std::vector<Module *> SubModules;

  /// A mapping from the submodule name to the index into the
  /// \c SubModules vector at which that submodule resides.
  llvm::StringMap<unsigned> SubModuleIndex;

  /// The AST file if this is a top-level module which has a
  /// corresponding serialized AST file, or null otherwise.
  const FileEntry *ASTFile = nullptr;

  /// The top-level headers associated with this module.
  llvm::SmallSetVector<const FileEntry *, 2> TopHeaders;

  /// top-level header filenames that aren't resolved to FileEntries yet.
  std::vector<std::string> TopHeaderNames;

  /// Cache of modules visible to lookup in this module.
  mutable llvm::DenseSet<const Module*> VisibleModulesCache;

  /// The ID used when referencing this module within a VisibleModuleSet.
  unsigned VisibilityID;

public:
  enum HeaderKind {
    HK_Normal,
    HK_Textual,
    HK_Private,
    HK_PrivateTextual,
    HK_Excluded
  };
  static const int NumHeaderKinds = HK_Excluded + 1;

  /// Information about a header directive as found in the module map
  /// file.
  struct Header {
    std::string NameAsWritten;
    const FileEntry *Entry;

    explicit operator bool() { return Entry; }
  };

  /// Information about a directory name as found in the module map
  /// file.
  struct DirectoryName {
    std::string NameAsWritten;
    const DirectoryEntry *Entry;

    explicit operator bool() { return Entry; }
  };

  /// The headers that are part of this module.
  SmallVector<Header, 2> Headers[5];

  /// Stored information about a header directive that was found in the
  /// module map file but has not been resolved to a file.
  struct UnresolvedHeaderDirective {
    HeaderKind Kind = HK_Normal;
    SourceLocation FileNameLoc;
    std::string FileName;
    bool IsUmbrella = false;
    bool HasBuiltinHeader = false;
    Optional<off_t> Size;
    Optional<time_t> ModTime;
  };

  /// Headers that are mentioned in the module map file but that we have not
  /// yet attempted to resolve to a file on the file system.
  SmallVector<UnresolvedHeaderDirective, 1> UnresolvedHeaders;

  /// Headers that are mentioned in the module map file but could not be
  /// found on the file system.
  SmallVector<UnresolvedHeaderDirective, 1> MissingHeaders;

  /// An individual requirement: a feature name and a flag indicating
  /// the required state of that feature.
  using Requirement = std::pair<std::string, bool>;

  /// The set of language features required to use this module.
  ///
  /// If any of these requirements are not available, the \c IsAvailable bit
  /// will be false to indicate that this (sub)module is not available.
  SmallVector<Requirement, 2> Requirements;

  /// A module with the same name that shadows this module.
  Module *ShadowingModule = nullptr;

  /// Whether this module is missing a feature from \c Requirements.
  unsigned IsMissingRequirement : 1;

  /// Whether we tried and failed to load a module file for this module.
  unsigned HasIncompatibleModuleFile : 1;

  /// Whether this module is available in the current translation unit.
  ///
  /// If the module is missing headers or does not meet all requirements then
  /// this bit will be 0.
  unsigned IsAvailable : 1;

  /// Whether this module was loaded from a module file.
  unsigned IsFromModuleFile : 1;

  /// Whether this is a framework module.
  unsigned IsFramework : 1;

  /// Whether this is an explicit submodule.
  unsigned IsExplicit : 1;

  /// Whether this is a "system" module (which assumes that all
  /// headers in it are system headers).
  unsigned IsSystem : 1;

  /// Whether this is an 'extern "C"' module (which implicitly puts all
  /// headers in it within an 'extern "C"' block, and allows the module to be
  /// imported within such a block).
  unsigned IsExternC : 1;

  /// Whether this is an inferred submodule (module * { ... }).
  unsigned IsInferred : 1;

  /// Whether we should infer submodules for this module based on
  /// the headers.
  ///
  /// Submodules can only be inferred for modules with an umbrella header.
  unsigned InferSubmodules : 1;

  /// Whether, when inferring submodules, the inferred submodules
  /// should be explicit.
  unsigned InferExplicitSubmodules : 1;

  /// Whether, when inferring submodules, the inferr submodules should
  /// export all modules they import (e.g., the equivalent of "export *").
  unsigned InferExportWildcard : 1;

  /// Whether the set of configuration macros is exhaustive.
  ///
  /// When the set of configuration macros is exhaustive, meaning
  /// that no identifier not in this list should affect how the module is
  /// built.
  unsigned ConfigMacrosExhaustive : 1;

  /// Whether files in this module can only include non-modular headers
  /// and headers from used modules.
  unsigned NoUndeclaredIncludes : 1;

  /// Whether this module came from a "private" module map, found next
  /// to a regular (public) module map.
  unsigned ModuleMapIsPrivate : 1;

  /// Describes the visibility of the various names within a
  /// particular module.
  enum NameVisibilityKind {
    /// All of the names in this module are hidden.
    Hidden,
    /// All of the names in this module are visible.
    AllVisible
  };

  /// The visibility of names within this particular module.
  NameVisibilityKind NameVisibility;

  /// The location of the inferred submodule.
  SourceLocation InferredSubmoduleLoc;

  /// The set of modules imported by this module, and on which this
  /// module depends.
  llvm::SmallSetVector<Module *, 2> Imports;

  /// Describes an exported module.
  ///
  /// The pointer is the module being re-exported, while the bit will be true
  /// to indicate that this is a wildcard export.
  using ExportDecl = llvm::PointerIntPair<Module *, 1, bool>;

  /// The set of export declarations.
  SmallVector<ExportDecl, 2> Exports;

  /// Describes an exported module that has not yet been resolved
  /// (perhaps because the module it refers to has not yet been loaded).
  struct UnresolvedExportDecl {
    /// The location of the 'export' keyword in the module map file.
    SourceLocation ExportLoc;

    /// The name of the module.
    ModuleId Id;

    /// Whether this export declaration ends in a wildcard, indicating
    /// that all of its submodules should be exported (rather than the named
    /// module itself).
    bool Wildcard;
  };

  /// The set of export declarations that have yet to be resolved.
  SmallVector<UnresolvedExportDecl, 2> UnresolvedExports;

  /// The directly used modules.
  SmallVector<Module *, 2> DirectUses;

  /// The set of use declarations that have yet to be resolved.
  SmallVector<ModuleId, 2> UnresolvedDirectUses;

  /// A library or framework to link against when an entity from this
  /// module is used.
  struct LinkLibrary {
    LinkLibrary() = default;
    LinkLibrary(const std::string &Library, bool IsFramework)
        : Library(Library), IsFramework(IsFramework) {}

    /// The library to link against.
    ///
    /// This will typically be a library or framework name, but can also
    /// be an absolute path to the library or framework.
    std::string Library;

    /// Whether this is a framework rather than a library.
    bool IsFramework = false;
  };

  /// The set of libraries or frameworks to link against when
  /// an entity from this module is used.
  llvm::SmallVector<LinkLibrary, 2> LinkLibraries;

  /// Autolinking uses the framework name for linking purposes
  /// when this is false and the export_as name otherwise.
  bool UseExportAsModuleLinkName = false;

  /// The set of "configuration macros", which are macros that
  /// (intentionally) change how this module is built.
  std::vector<std::string> ConfigMacros;

  /// An unresolved conflict with another module.
  struct UnresolvedConflict {
    /// The (unresolved) module id.
    ModuleId Id;

    /// The message provided to the user when there is a conflict.
    std::string Message;
  };

  /// The list of conflicts for which the module-id has not yet been
  /// resolved.
  std::vector<UnresolvedConflict> UnresolvedConflicts;

  /// A conflict between two modules.
  struct Conflict {
    /// The module that this module conflicts with.
    Module *Other;

    /// The message provided to the user when there is a conflict.
    std::string Message;
  };

  /// The list of conflicts.
  std::vector<Conflict> Conflicts;

  /// Construct a new module or submodule.
  Module(StringRef Name, SourceLocation DefinitionLoc, Module *Parent,
         bool IsFramework, bool IsExplicit, unsigned VisibilityID);

  ~Module();

  /// Determine whether this module is available for use within the
  /// current translation unit.
  bool isAvailable() const { return IsAvailable; }

  /// Determine whether this module is available for use within the
  /// current translation unit.
  ///
  /// \param LangOpts The language options used for the current
  /// translation unit.
  ///
  /// \param Target The target options used for the current translation unit.
  ///
  /// \param Req If this module is unavailable because of a missing requirement,
  /// this parameter will be set to one of the requirements that is not met for
  /// use of this module.
  ///
  /// \param MissingHeader If this module is unavailable because of a missing
  /// header, this parameter will be set to one of the missing headers.
  ///
  /// \param ShadowingModule If this module is unavailable because it is
  /// shadowed, this parameter will be set to the shadowing module.
  bool isAvailable(const LangOptions &LangOpts,
                   const TargetInfo &Target,
                   Requirement &Req,
                   UnresolvedHeaderDirective &MissingHeader,
                   Module *&ShadowingModule) const;

  /// Determine whether this module is a submodule.
  bool isSubModule() const { return Parent != nullptr; }

  /// Determine whether this module is a submodule of the given other
  /// module.
  bool isSubModuleOf(const Module *Other) const;

  /// Determine whether this module is a part of a framework,
  /// either because it is a framework module or because it is a submodule
  /// of a framework module.
  bool isPartOfFramework() const {
    for (const Module *Mod = this; Mod; Mod = Mod->Parent)
      if (Mod->IsFramework)
        return true;

    return false;
  }

  /// Determine whether this module is a subframework of another
  /// framework.
  bool isSubFramework() const {
    return IsFramework && Parent && Parent->isPartOfFramework();
  }

  /// Set the parent of this module. This should only be used if the parent
  /// could not be set during module creation.
  void setParent(Module *M) {
    assert(!Parent);
    Parent = M;
    Parent->SubModuleIndex[Name] = Parent->SubModules.size();
    Parent->SubModules.push_back(this);
  }

  /// Retrieve the full name of this module, including the path from
  /// its top-level module.
  /// \param AllowStringLiterals If \c true, components that might not be
  ///        lexically valid as identifiers will be emitted as string literals.
  std::string getFullModuleName(bool AllowStringLiterals = false) const;

  /// Whether the full name of this module is equal to joining
  /// \p nameParts with "."s.
  ///
  /// This is more efficient than getFullModuleName().
  bool fullModuleNameIs(ArrayRef<StringRef> nameParts) const;

  /// Retrieve the top-level module for this (sub)module, which may
  /// be this module.
  Module *getTopLevelModule() {
    return const_cast<Module *>(
             const_cast<const Module *>(this)->getTopLevelModule());
  }

  /// Retrieve the top-level module for this (sub)module, which may
  /// be this module.
  const Module *getTopLevelModule() const;

  /// Retrieve the name of the top-level module.
  StringRef getTopLevelModuleName() const {
    return getTopLevelModule()->Name;
  }

  /// The serialized AST file for this module, if one was created.
  const FileEntry *getASTFile() const {
    return getTopLevelModule()->ASTFile;
  }

  /// Set the serialized AST file for the top-level module of this module.
  void setASTFile(const FileEntry *File) {
    assert((File == nullptr || getASTFile() == nullptr ||
            getASTFile() == File) && "file path changed");
    getTopLevelModule()->ASTFile = File;
  }

  /// Retrieve the directory for which this module serves as the
  /// umbrella.
  DirectoryName getUmbrellaDir() const;

  /// Retrieve the header that serves as the umbrella header for this
  /// module.
  Header getUmbrellaHeader() const {
    if (auto *E = Umbrella.dyn_cast<const FileEntry *>())
      return Header{UmbrellaAsWritten, E};
    return Header{};
  }

  /// Determine whether this module has an umbrella directory that is
  /// not based on an umbrella header.
  bool hasUmbrellaDir() const {
    return Umbrella && Umbrella.is<const DirectoryEntry *>();
  }

  /// Add a top-level header associated with this module.
  void addTopHeader(const FileEntry *File) {
    assert(File);
    TopHeaders.insert(File);
  }

  /// Add a top-level header filename associated with this module.
  void addTopHeaderFilename(StringRef Filename) {
    TopHeaderNames.push_back(Filename);
  }

  /// The top-level headers associated with this module.
  ArrayRef<const FileEntry *> getTopHeaders(FileManager &FileMgr);

  /// Determine whether this module has declared its intention to
  /// directly use another module.
  bool directlyUses(const Module *Requested) const;

  /// Add the given feature requirement to the list of features
  /// required by this module.
  ///
  /// \param Feature The feature that is required by this module (and
  /// its submodules).
  ///
  /// \param RequiredState The required state of this feature: \c true
  /// if it must be present, \c false if it must be absent.
  ///
  /// \param LangOpts The set of language options that will be used to
  /// evaluate the availability of this feature.
  ///
  /// \param Target The target options that will be used to evaluate the
  /// availability of this feature.
  void addRequirement(StringRef Feature, bool RequiredState,
                      const LangOptions &LangOpts,
                      const TargetInfo &Target);

  /// Mark this module and all of its submodules as unavailable.
  void markUnavailable(bool MissingRequirement = false);

  /// Find the submodule with the given name.
  ///
  /// \returns The submodule if found, or NULL otherwise.
  Module *findSubmodule(StringRef Name) const;

  /// Determine whether the specified module would be visible to
  /// a lookup at the end of this module.
  ///
  /// FIXME: This may return incorrect results for (submodules of) the
  /// module currently being built, if it's queried before we see all
  /// of its imports.
  bool isModuleVisible(const Module *M) const {
    if (VisibleModulesCache.empty())
      buildVisibleModulesCache();
    return VisibleModulesCache.count(M);
  }

  unsigned getVisibilityID() const { return VisibilityID; }

  using submodule_iterator = std::vector<Module *>::iterator;
  using submodule_const_iterator = std::vector<Module *>::const_iterator;

  submodule_iterator submodule_begin() { return SubModules.begin(); }
  submodule_const_iterator submodule_begin() const {return SubModules.begin();}
  submodule_iterator submodule_end()   { return SubModules.end(); }
  submodule_const_iterator submodule_end() const { return SubModules.end(); }

  llvm::iterator_range<submodule_iterator> submodules() {
    return llvm::make_range(submodule_begin(), submodule_end());
  }
  llvm::iterator_range<submodule_const_iterator> submodules() const {
    return llvm::make_range(submodule_begin(), submodule_end());
  }

  /// Appends this module's list of exported modules to \p Exported.
  ///
  /// This provides a subset of immediately imported modules (the ones that are
  /// directly exported), not the complete set of exported modules.
  void getExportedModules(SmallVectorImpl<Module *> &Exported) const;

  static StringRef getModuleInputBufferName() {
    return "<module-includes>";
  }

  /// Print the module map for this module to the given stream.
  void print(raw_ostream &OS, unsigned Indent = 0) const;

  /// Dump the contents of this module to the given output stream.
  void dump() const;

private:
  void buildVisibleModulesCache() const;
};

/// A set of visible modules.
class VisibleModuleSet {
public:
  VisibleModuleSet() = default;
  VisibleModuleSet(VisibleModuleSet &&O)
      : ImportLocs(std::move(O.ImportLocs)), Generation(O.Generation ? 1 : 0) {
    O.ImportLocs.clear();
    ++O.Generation;
  }

  /// Move from another visible modules set. Guaranteed to leave the source
  /// empty and bump the generation on both.
  VisibleModuleSet &operator=(VisibleModuleSet &&O) {
    ImportLocs = std::move(O.ImportLocs);
    O.ImportLocs.clear();
    ++O.Generation;
    ++Generation;
    return *this;
  }

  /// Get the current visibility generation. Incremented each time the
  /// set of visible modules changes in any way.
  unsigned getGeneration() const { return Generation; }

  /// Determine whether a module is visible.
  bool isVisible(const Module *M) const {
    return getImportLoc(M).isValid();
  }

  /// Get the location at which the import of a module was triggered.
  SourceLocation getImportLoc(const Module *M) const {
    return M->getVisibilityID() < ImportLocs.size()
               ? ImportLocs[M->getVisibilityID()]
               : SourceLocation();
  }

  /// A callback to call when a module is made visible (directly or
  /// indirectly) by a call to \ref setVisible.
  using VisibleCallback = llvm::function_ref<void(Module *M)>;

  /// A callback to call when a module conflict is found. \p Path
  /// consists of a sequence of modules from the conflicting module to the one
  /// made visible, where each was exported by the next.
  using ConflictCallback =
      llvm::function_ref<void(ArrayRef<Module *> Path, Module *Conflict,
                         StringRef Message)>;

  /// Make a specific module visible.
  void setVisible(Module *M, SourceLocation Loc,
                  VisibleCallback Vis = [](Module *) {},
                  ConflictCallback Cb = [](ArrayRef<Module *>, Module *,
                                           StringRef) {});

private:
  /// Import locations for each visible module. Indexed by the module's
  /// VisibilityID.
  std::vector<SourceLocation> ImportLocs;

  /// Visibility generation, bumped every time the visibility state changes.
  unsigned Generation = 0;
};

} // namespace clang

#endif // LLVM_CLANG_BASIC_MODULE_H
