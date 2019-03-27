//===- Core/Resolver.h - Resolves Atom References -------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_RESOLVER_H
#define LLD_CORE_RESOLVER_H

#include "lld/Core/ArchiveLibraryFile.h"
#include "lld/Core/File.h"
#include "lld/Core/SharedLibraryFile.h"
#include "lld/Core/Simple.h"
#include "lld/Core/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/ErrorOr.h"
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lld {

class Atom;
class LinkingContext;

/// The Resolver is responsible for merging all input object files
/// and producing a merged graph.
class Resolver {
public:
  Resolver(LinkingContext &ctx) : _ctx(ctx), _result(new MergedFile()) {}

  // InputFiles::Handler methods
  void doDefinedAtom(OwningAtomPtr<DefinedAtom> atom);
  bool doUndefinedAtom(OwningAtomPtr<UndefinedAtom> atom);
  void doSharedLibraryAtom(OwningAtomPtr<SharedLibraryAtom> atom);
  void doAbsoluteAtom(OwningAtomPtr<AbsoluteAtom> atom);

  // Handle files, this adds atoms from the current file thats
  // being processed by the resolver
  llvm::Expected<bool> handleFile(File &);

  // Handle an archive library file.
  llvm::Expected<bool> handleArchiveFile(File &);

  // Handle a shared library file.
  llvm::Error handleSharedLibrary(File &);

  /// do work of merging and resolving and return list
  bool resolve();

  std::unique_ptr<SimpleFile> resultFile() { return std::move(_result); }

private:
  typedef std::function<llvm::Expected<bool>(StringRef)> UndefCallback;

  bool undefinesAdded(int begin, int end);
  File *getFile(int &index);

  /// The main function that iterates over the files to resolve
  bool resolveUndefines();
  void updateReferences();
  void deadStripOptimize();
  bool checkUndefines();
  void removeCoalescedAwayAtoms();
  llvm::Expected<bool> forEachUndefines(File &file, UndefCallback callback);

  void markLive(const Atom *atom);

  class MergedFile : public SimpleFile {
  public:
    MergedFile() : SimpleFile("<linker-internal>", kindResolverMergedObject) {}
    void addAtoms(llvm::MutableArrayRef<OwningAtomPtr<Atom>> atoms);
  };

  LinkingContext &_ctx;
  SymbolTable _symbolTable;
  std::vector<OwningAtomPtr<Atom>>     _atoms;
  std::set<const Atom *>        _deadStripRoots;
  llvm::DenseSet<const Atom *>  _liveAtoms;
  llvm::DenseSet<const Atom *>  _deadAtoms;
  std::unique_ptr<MergedFile>   _result;
  std::unordered_multimap<const Atom *, const Atom *> _reverseRef;

  // --start-group and --end-group
  std::vector<File *> _files;
  std::map<File *, bool> _newUndefinesAdded;

  // List of undefined symbols.
  std::vector<StringRef> _undefines;

  // Start position in _undefines for each archive/shared library file.
  // Symbols from index 0 to the start position are already searched before.
  // Searching them again would never succeed. When we look for undefined
  // symbols from an archive/shared library file, start from its start
  // position to save time.
  std::map<File *, size_t> _undefineIndex;
};

} // namespace lld

#endif // LLD_CORE_RESOLVER_H
