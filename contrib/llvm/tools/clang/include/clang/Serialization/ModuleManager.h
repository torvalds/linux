//===- ModuleManager.cpp - Module Manager -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ModuleManager class, which manages a set of loaded
//  modules for the ASTReader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_MODULEMANAGER_H
#define LLVM_CLANG_SERIALIZATION_MODULEMANAGER_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Serialization/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

namespace clang {

class FileEntry;
class FileManager;
class GlobalModuleIndex;
class HeaderSearch;
class MemoryBufferCache;
class ModuleMap;
class PCHContainerReader;

namespace serialization {

/// Manages the set of modules loaded by an AST reader.
class ModuleManager {
  /// The chain of AST files, in the order in which we started to load
  /// them (this order isn't really useful for anything).
  SmallVector<std::unique_ptr<ModuleFile>, 2> Chain;

  /// The chain of non-module PCH files. The first entry is the one named
  /// by the user, the last one is the one that doesn't depend on anything
  /// further.
  SmallVector<ModuleFile *, 2> PCHChain;

  // The roots of the dependency DAG of AST files. This is used
  // to implement short-circuiting logic when running DFS over the dependencies.
  SmallVector<ModuleFile *, 2> Roots;

  /// All loaded modules, indexed by name.
  llvm::DenseMap<const FileEntry *, ModuleFile *> Modules;

  /// FileManager that handles translating between filenames and
  /// FileEntry *.
  FileManager &FileMgr;

  /// Cache of PCM files.
  IntrusiveRefCntPtr<MemoryBufferCache> PCMCache;

  /// Knows how to unwrap module containers.
  const PCHContainerReader &PCHContainerRdr;

  /// Preprocessor's HeaderSearchInfo containing the module map.
  const HeaderSearch &HeaderSearchInfo;

  /// A lookup of in-memory (virtual file) buffers
  llvm::DenseMap<const FileEntry *, std::unique_ptr<llvm::MemoryBuffer>>
      InMemoryBuffers;

  /// The visitation order.
  SmallVector<ModuleFile *, 4> VisitOrder;

  /// The list of module files that both we and the global module index
  /// know about.
  ///
  /// Either the global index or the module manager may have modules that the
  /// other does not know about, because the global index can be out-of-date
  /// (in which case the module manager could have modules it does not) and
  /// this particular translation unit might not have loaded all of the modules
  /// known to the global index.
  SmallVector<ModuleFile *, 4> ModulesInCommonWithGlobalIndex;

  /// The global module index, if one is attached.
  ///
  /// The global module index will actually be owned by the ASTReader; this is
  /// just an non-owning pointer.
  GlobalModuleIndex *GlobalIndex = nullptr;

  /// State used by the "visit" operation to avoid malloc traffic in
  /// calls to visit().
  struct VisitState {
    explicit VisitState(unsigned N) : VisitNumber(N, 0) {
      Stack.reserve(N);
    }

    ~VisitState() {
      delete NextState;
    }

    /// The stack used when marking the imports of a particular module
    /// as not-to-be-visited.
    SmallVector<ModuleFile *, 4> Stack;

    /// The visit number of each module file, which indicates when
    /// this module file was last visited.
    SmallVector<unsigned, 4> VisitNumber;

    /// The next visit number to use to mark visited module files.
    unsigned NextVisitNumber = 1;

    /// The next visit state.
    VisitState *NextState = nullptr;
  };

  /// The first visit() state in the chain.
  VisitState *FirstVisitState = nullptr;

  VisitState *allocateVisitState();
  void returnVisitState(VisitState *State);

public:
  using ModuleIterator = llvm::pointee_iterator<
      SmallVectorImpl<std::unique_ptr<ModuleFile>>::iterator>;
  using ModuleConstIterator = llvm::pointee_iterator<
      SmallVectorImpl<std::unique_ptr<ModuleFile>>::const_iterator>;
  using ModuleReverseIterator = llvm::pointee_iterator<
      SmallVectorImpl<std::unique_ptr<ModuleFile>>::reverse_iterator>;
  using ModuleOffset = std::pair<uint32_t, StringRef>;

  explicit ModuleManager(FileManager &FileMgr, MemoryBufferCache &PCMCache,
                         const PCHContainerReader &PCHContainerRdr,
                         const HeaderSearch &HeaderSearchInfo);
  ~ModuleManager();

  /// Forward iterator to traverse all loaded modules.
  ModuleIterator begin() { return Chain.begin(); }

  /// Forward iterator end-point to traverse all loaded modules
  ModuleIterator end() { return Chain.end(); }

  /// Const forward iterator to traverse all loaded modules.
  ModuleConstIterator begin() const { return Chain.begin(); }

  /// Const forward iterator end-point to traverse all loaded modules
  ModuleConstIterator end() const { return Chain.end(); }

  /// Reverse iterator to traverse all loaded modules.
  ModuleReverseIterator rbegin() { return Chain.rbegin(); }

  /// Reverse iterator end-point to traverse all loaded modules.
  ModuleReverseIterator rend() { return Chain.rend(); }

  /// A range covering the PCH and preamble module files loaded.
  llvm::iterator_range<SmallVectorImpl<ModuleFile *>::const_iterator>
  pch_modules() const {
    return llvm::make_range(PCHChain.begin(), PCHChain.end());
  }

  /// Returns the primary module associated with the manager, that is,
  /// the first module loaded
  ModuleFile &getPrimaryModule() { return *Chain[0]; }

  /// Returns the primary module associated with the manager, that is,
  /// the first module loaded.
  ModuleFile &getPrimaryModule() const { return *Chain[0]; }

  /// Returns the module associated with the given index
  ModuleFile &operator[](unsigned Index) const { return *Chain[Index]; }

  /// Returns the module associated with the given file name.
  ModuleFile *lookupByFileName(StringRef FileName) const;

  /// Returns the module associated with the given module name.
  ModuleFile *lookupByModuleName(StringRef ModName) const;

  /// Returns the module associated with the given module file.
  ModuleFile *lookup(const FileEntry *File) const;

  /// Returns the in-memory (virtual file) buffer with the given name
  std::unique_ptr<llvm::MemoryBuffer> lookupBuffer(StringRef Name);

  /// Number of modules loaded
  unsigned size() const { return Chain.size(); }

  /// The result of attempting to add a new module.
  enum AddModuleResult {
    /// The module file had already been loaded.
    AlreadyLoaded,

    /// The module file was just loaded in response to this call.
    NewlyLoaded,

    /// The module file is missing.
    Missing,

    /// The module file is out-of-date.
    OutOfDate
  };

  using ASTFileSignatureReader = ASTFileSignature (*)(StringRef);

  /// Attempts to create a new module and add it to the list of known
  /// modules.
  ///
  /// \param FileName The file name of the module to be loaded.
  ///
  /// \param Type The kind of module being loaded.
  ///
  /// \param ImportLoc The location at which the module is imported.
  ///
  /// \param ImportedBy The module that is importing this module, or NULL if
  /// this module is imported directly by the user.
  ///
  /// \param Generation The generation in which this module was loaded.
  ///
  /// \param ExpectedSize The expected size of the module file, used for
  /// validation. This will be zero if unknown.
  ///
  /// \param ExpectedModTime The expected modification time of the module
  /// file, used for validation. This will be zero if unknown.
  ///
  /// \param ExpectedSignature The expected signature of the module file, used
  /// for validation. This will be zero if unknown.
  ///
  /// \param ReadSignature Reads the signature from an AST file without actually
  /// loading it.
  ///
  /// \param Module A pointer to the module file if the module was successfully
  /// loaded.
  ///
  /// \param ErrorStr Will be set to a non-empty string if any errors occurred
  /// while trying to load the module.
  ///
  /// \return A pointer to the module that corresponds to this file name,
  /// and a value indicating whether the module was loaded.
  AddModuleResult addModule(StringRef FileName, ModuleKind Type,
                            SourceLocation ImportLoc,
                            ModuleFile *ImportedBy, unsigned Generation,
                            off_t ExpectedSize, time_t ExpectedModTime,
                            ASTFileSignature ExpectedSignature,
                            ASTFileSignatureReader ReadSignature,
                            ModuleFile *&Module,
                            std::string &ErrorStr);

  /// Remove the modules starting from First (to the end).
  void removeModules(ModuleIterator First,
                     llvm::SmallPtrSetImpl<ModuleFile *> &LoadedSuccessfully,
                     ModuleMap *modMap);

  /// Add an in-memory buffer the list of known buffers
  void addInMemoryBuffer(StringRef FileName,
                         std::unique_ptr<llvm::MemoryBuffer> Buffer);

  /// Set the global module index.
  void setGlobalIndex(GlobalModuleIndex *Index);

  /// Notification from the AST reader that the given module file
  /// has been "accepted", and will not (can not) be unloaded.
  void moduleFileAccepted(ModuleFile *MF);

  /// Visit each of the modules.
  ///
  /// This routine visits each of the modules, starting with the
  /// "root" modules that no other loaded modules depend on, and
  /// proceeding to the leaf modules, visiting each module only once
  /// during the traversal.
  ///
  /// This traversal is intended to support various "lookup"
  /// operations that can find data in any of the loaded modules.
  ///
  /// \param Visitor A visitor function that will be invoked with each
  /// module. The return value must be convertible to bool; when false, the
  /// visitation continues to modules that the current module depends on. When
  /// true, the visitation skips any modules that the current module depends on.
  ///
  /// \param ModuleFilesHit If non-NULL, contains the set of module files
  /// that we know we need to visit because the global module index told us to.
  /// Any module that is known to both the global module index and the module
  /// manager that is *not* in this set can be skipped.
  void visit(llvm::function_ref<bool(ModuleFile &M)> Visitor,
             llvm::SmallPtrSetImpl<ModuleFile *> *ModuleFilesHit = nullptr);

  /// Attempt to resolve the given module file name to a file entry.
  ///
  /// \param FileName The name of the module file.
  ///
  /// \param ExpectedSize The size that the module file is expected to have.
  /// If the actual size differs, the resolver should return \c true.
  ///
  /// \param ExpectedModTime The modification time that the module file is
  /// expected to have. If the actual modification time differs, the resolver
  /// should return \c true.
  ///
  /// \param File Will be set to the file if there is one, or null
  /// otherwise.
  ///
  /// \returns True if a file exists but does not meet the size/
  /// modification time criteria, false if the file is either available and
  /// suitable, or is missing.
  bool lookupModuleFile(StringRef FileName,
                        off_t ExpectedSize,
                        time_t ExpectedModTime,
                        const FileEntry *&File);

  /// View the graphviz representation of the module graph.
  void viewGraph();

  MemoryBufferCache &getPCMCache() const { return *PCMCache; }
};

} // namespace serialization

} // namespace clang

#endif // LLVM_CLANG_SERIALIZATION_MODULEMANAGER_H
