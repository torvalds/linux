//===------ lib/ReaderWriter/MachO/LayoutPass.h - Handles Layout of atoms -===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_MACHO_LAYOUT_PASS_H
#define LLD_READER_WRITER_MACHO_LAYOUT_PASS_H

#include "lld/Core/File.h"
#include "lld/Core/Pass.h"
#include "lld/Core/Reader.h"
#include "lld/Core/Simple.h"
#include "llvm/ADT/DenseMap.h"
#include <map>
#include <string>
#include <vector>

namespace lld {
class DefinedAtom;
class SimpleFile;

namespace mach_o {

/// This linker pass does the layout of the atoms. The pass is done after the
/// order their .o files were found on the command line, then by order of the
/// atoms (address) in the .o file.  But some atoms have a preferred location
/// in their section (such as pinned to the start or end of the section), so
/// the sort must take that into account too.
class LayoutPass : public Pass {
public:
  struct SortKey {
    SortKey(OwningAtomPtr<DefinedAtom> &&atom,
            const DefinedAtom *root, uint64_t override)
    : _atom(std::move(atom)), _root(root), _override(override) {}
    OwningAtomPtr<DefinedAtom> _atom;
    const DefinedAtom *_root;
    uint64_t _override;

    // Note, these are only here to appease MSVC bots which didn't like
    // the same methods being implemented/deleted in OwningAtomPtr.
    SortKey(SortKey &&key) : _atom(std::move(key._atom)), _root(key._root),
                             _override(key._override) {
      key._root = nullptr;
    }

    SortKey &operator=(SortKey &&key) {
      _atom = std::move(key._atom);
      _root = key._root;
      key._root = nullptr;
      _override = key._override;
      return *this;
    }

  private:
    SortKey(const SortKey &) = delete;
    void operator=(const SortKey&) = delete;
  };

  typedef std::function<bool (const DefinedAtom *left, const DefinedAtom *right,
                              bool &leftBeforeRight)> SortOverride;

  LayoutPass(const Registry &registry, SortOverride sorter);

  /// Sorts atoms in mergedFile by content type then by command line order.
  llvm::Error perform(SimpleFile &mergedFile) override;

  ~LayoutPass() override = default;

private:
  // Build the followOn atoms chain as specified by the kindLayoutAfter
  // reference type
  void buildFollowOnTable(const File::AtomRange<DefinedAtom> &range);

  // Build a map of Atoms to ordinals for sorting the atoms
  void buildOrdinalOverrideMap(const File::AtomRange<DefinedAtom> &range);

  const Registry &_registry;
  SortOverride _customSorter;

  typedef llvm::DenseMap<const DefinedAtom *, const DefinedAtom *> AtomToAtomT;
  typedef llvm::DenseMap<const DefinedAtom *, uint64_t> AtomToOrdinalT;

  // A map to be used to sort atoms. It represents the order of atoms in the
  // result; if Atom X is mapped to atom Y in this map, X will be located
  // immediately before Y in the output file. Y might be mapped to another
  // atom, constructing a follow-on chain. An atom cannot be mapped to more
  // than one atom unless all but one atom are of size zero.
  AtomToAtomT _followOnNexts;

  // A map to be used to sort atoms. It's a map from an atom to its root of
  // follow-on chain. A root atom is mapped to itself. If an atom is not in
  // _followOnNexts, the atom is not in this map, and vice versa.
  AtomToAtomT _followOnRoots;

  AtomToOrdinalT _ordinalOverrideMap;

  // Helper methods for buildFollowOnTable().
  const DefinedAtom *findAtomFollowedBy(const DefinedAtom *targetAtom);
  bool checkAllPrevAtomsZeroSize(const DefinedAtom *targetAtom);

  void setChainRoot(const DefinedAtom *targetAtom, const DefinedAtom *root);

  std::vector<SortKey> decorate(File::AtomRange<DefinedAtom> &atomRange) const;

  void undecorate(File::AtomRange<DefinedAtom> &atomRange,
                  std::vector<SortKey> &keys) const;

  // Check if the follow-on graph is a correct structure. For debugging only.
  void checkFollowonChain(const File::AtomRange<DefinedAtom> &range);
};

} // namespace mach_o
} // namespace lld

#endif // LLD_READER_WRITER_MACHO_LAYOUT_PASS_H
