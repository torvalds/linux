//===--- GlobalModuleIndex.h - Global Module Index --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the GlobalModuleIndex class, which manages a global index
// containing all of the identifiers known to the various modules within a given
// subdirectory of the module cache. It is used to improve the performance of
// queries such as "do any modules know about this identifier?"
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_SERIALIZATION_GLOBALMODULEINDEX_H
#define LLVM_CLANG_SERIALIZATION_GLOBALMODULEINDEX_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <utility>

namespace llvm {
class BitstreamCursor;
class MemoryBuffer;
}

namespace clang {

class DirectoryEntry;
class FileEntry;
class FileManager;
class IdentifierIterator;
class PCHContainerOperations;
class PCHContainerReader;

namespace serialization {
  class ModuleFile;
}

/// A global index for a set of module files, providing information about
/// the identifiers within those module files.
///
/// The global index is an aid for name lookup into modules, offering a central
/// place where one can look for identifiers determine which
/// module files contain any information about that identifier. This
/// allows the client to restrict the search to only those module files known
/// to have a information about that identifier, improving performance. Moreover,
/// the global module index may know about module files that have not been
/// imported, and can be queried to determine which modules the current
/// translation could or should load to fix a problem.
class GlobalModuleIndex {
  using ModuleFile = serialization::ModuleFile;

  /// Buffer containing the index file, which is lazily accessed so long
  /// as the global module index is live.
  std::unique_ptr<llvm::MemoryBuffer> Buffer;

  /// The hash table.
  ///
  /// This pointer actually points to a IdentifierIndexTable object,
  /// but that type is only accessible within the implementation of
  /// GlobalModuleIndex.
  void *IdentifierIndex;

  /// Information about a given module file.
  struct ModuleInfo {
    ModuleInfo() : File(), Size(), ModTime() { }

    /// The module file, once it has been resolved.
    ModuleFile *File;

    /// The module file name.
    std::string FileName;

    /// Size of the module file at the time the global index was built.
    off_t Size;

    /// Modification time of the module file at the time the global
    /// index was built.
    time_t ModTime;

    /// The module IDs on which this module directly depends.
    /// FIXME: We don't really need a vector here.
    llvm::SmallVector<unsigned, 4> Dependencies;
  };

  /// A mapping from module IDs to information about each module.
  ///
  /// This vector may have gaps, if module files have been removed or have
  /// been updated since the index was built. A gap is indicated by an empty
  /// file name.
  llvm::SmallVector<ModuleInfo, 16> Modules;

  /// Lazily-populated mapping from module files to their
  /// corresponding index into the \c Modules vector.
  llvm::DenseMap<ModuleFile *, unsigned> ModulesByFile;

  /// The set of modules that have not yet been resolved.
  ///
  /// The string is just the name of the module itself, which maps to the
  /// module ID.
  llvm::StringMap<unsigned> UnresolvedModules;

  /// The number of identifier lookups we performed.
  unsigned NumIdentifierLookups;

  /// The number of identifier lookup hits, where we recognize the
  /// identifier.
  unsigned NumIdentifierLookupHits;

  /// Internal constructor. Use \c readIndex() to read an index.
  explicit GlobalModuleIndex(std::unique_ptr<llvm::MemoryBuffer> Buffer,
                             llvm::BitstreamCursor Cursor);

  GlobalModuleIndex(const GlobalModuleIndex &) = delete;
  GlobalModuleIndex &operator=(const GlobalModuleIndex &) = delete;

public:
  ~GlobalModuleIndex();

  /// An error code returned when trying to read an index.
  enum ErrorCode {
    /// No error occurred.
    EC_None,
    /// No index was found.
    EC_NotFound,
    /// Some other process is currently building the index; it is not
    /// available yet.
    EC_Building,
    /// There was an unspecified I/O error reading or writing the index.
    EC_IOError
  };

  /// Read a global index file for the given directory.
  ///
  /// \param Path The path to the specific module cache where the module files
  /// for the intended configuration reside.
  ///
  /// \returns A pair containing the global module index (if it exists) and
  /// the error code.
  static std::pair<GlobalModuleIndex *, ErrorCode>
  readIndex(llvm::StringRef Path);

  /// Returns an iterator for identifiers stored in the index table.
  ///
  /// The caller accepts ownership of the returned object.
  IdentifierIterator *createIdentifierIterator() const;

  /// Retrieve the set of modules that have up-to-date indexes.
  ///
  /// \param ModuleFiles Will be populated with the set of module files that
  /// have been indexed.
  void getKnownModules(llvm::SmallVectorImpl<ModuleFile *> &ModuleFiles);

  /// Retrieve the set of module files on which the given module file
  /// directly depends.
  void getModuleDependencies(ModuleFile *File,
                             llvm::SmallVectorImpl<ModuleFile *> &Dependencies);

  /// A set of module files in which we found a result.
  typedef llvm::SmallPtrSet<ModuleFile *, 4> HitSet;

  /// Look for all of the module files with information about the given
  /// identifier, e.g., a global function, variable, or type with that name.
  ///
  /// \param Name The identifier to look for.
  ///
  /// \param Hits Will be populated with the set of module files that have
  /// information about this name.
  ///
  /// \returns true if the identifier is known to the index, false otherwise.
  bool lookupIdentifier(llvm::StringRef Name, HitSet &Hits);

  /// Note that the given module file has been loaded.
  ///
  /// \returns false if the global module index has information about this
  /// module file, and true otherwise.
  bool loadedModuleFile(ModuleFile *File);

  /// Print statistics to standard error.
  void printStats();

  /// Print debugging view to standard error.
  void dump();

  /// Write a global index into the given
  ///
  /// \param FileMgr The file manager to use to load module files.
  /// \param PCHContainerRdr - The PCHContainerOperations to use for loading and
  /// creating modules.
  /// \param Path The path to the directory containing module files, into
  /// which the global index will be written.
  static ErrorCode writeIndex(FileManager &FileMgr,
                              const PCHContainerReader &PCHContainerRdr,
                              llvm::StringRef Path);
};
}

#endif
