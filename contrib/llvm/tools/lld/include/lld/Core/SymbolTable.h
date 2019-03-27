//===- Core/SymbolTable.h - Main Symbol Table -----------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_SYMBOL_TABLE_H
#define LLD_CORE_SYMBOL_TABLE_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/DJB.h"
#include <cstring>
#include <map>
#include <vector>

namespace lld {

class AbsoluteAtom;
class Atom;
class DefinedAtom;
class LinkingContext;
class ResolverOptions;
class SharedLibraryAtom;
class UndefinedAtom;

/// The SymbolTable class is responsible for coalescing atoms.
///
/// All atoms coalescable by-name or by-content should be added.
/// The method replacement() can be used to find the replacement atom
/// if an atom has been coalesced away.
class SymbolTable {
public:
  /// add atom to symbol table
  bool add(const DefinedAtom &);

  /// add atom to symbol table
  bool add(const UndefinedAtom &);

  /// add atom to symbol table
  bool add(const SharedLibraryAtom &);

  /// add atom to symbol table
  bool add(const AbsoluteAtom &);

  /// returns atom in symbol table for specified name (or nullptr)
  const Atom *findByName(StringRef sym);

  /// returns vector of remaining UndefinedAtoms
  std::vector<const UndefinedAtom *> undefines();

  /// if atom has been coalesced away, return replacement, else return atom
  const Atom *replacement(const Atom *);

  /// if atom has been coalesced away, return true
  bool isCoalescedAway(const Atom *);

private:
  typedef llvm::DenseMap<const Atom *, const Atom *> AtomToAtom;

  struct StringRefMappingInfo {
    static StringRef getEmptyKey() { return StringRef(); }
    static StringRef getTombstoneKey() { return StringRef(" ", 1); }
    static unsigned getHashValue(StringRef const val) {
      return llvm::djbHash(val, 0);
    }
    static bool isEqual(StringRef const lhs, StringRef const rhs) {
      return lhs.equals(rhs);
    }
  };
  typedef llvm::DenseMap<StringRef, const Atom *,
                                           StringRefMappingInfo> NameToAtom;

  struct AtomMappingInfo {
    static const DefinedAtom * getEmptyKey() { return nullptr; }
    static const DefinedAtom * getTombstoneKey() { return (DefinedAtom*)(-1); }
    static unsigned getHashValue(const DefinedAtom * const Val);
    static bool isEqual(const DefinedAtom * const LHS,
                        const DefinedAtom * const RHS);
  };
  typedef llvm::DenseSet<const DefinedAtom*, AtomMappingInfo> AtomContentSet;

  bool addByName(const Atom &);
  bool addByContent(const DefinedAtom &);

  AtomToAtom _replacedAtoms;
  NameToAtom _nameTable;
  AtomContentSet _contentTable;
};

} // namespace lld

#endif // LLD_CORE_SYMBOL_TABLE_H
