//===- Core/SymbolTable.cpp - Main Symbol Table ---------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lld/Core/SymbolTable.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/AbsoluteAtom.h"
#include "lld/Core/Atom.h"
#include "lld/Core/DefinedAtom.h"
#include "lld/Core/File.h"
#include "lld/Core/LinkingContext.h"
#include "lld/Core/Resolver.h"
#include "lld/Core/SharedLibraryAtom.h"
#include "lld/Core/UndefinedAtom.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <vector>

namespace lld {
bool SymbolTable::add(const UndefinedAtom &atom) { return addByName(atom); }

bool SymbolTable::add(const SharedLibraryAtom &atom) { return addByName(atom); }

bool SymbolTable::add(const AbsoluteAtom &atom) { return addByName(atom); }

bool SymbolTable::add(const DefinedAtom &atom) {
  if (!atom.name().empty() &&
      atom.scope() != DefinedAtom::scopeTranslationUnit) {
    // Named atoms cannot be merged by content.
    assert(atom.merge() != DefinedAtom::mergeByContent);
    // Track named atoms that are not scoped to file (static).
    return addByName(atom);
  }
  if (atom.merge() == DefinedAtom::mergeByContent) {
    // Named atoms cannot be merged by content.
    assert(atom.name().empty());
    // Currently only read-only constants can be merged.
    if (atom.permissions() == DefinedAtom::permR__)
      return addByContent(atom);
    // TODO: support mergeByContent of data atoms by comparing content & fixups.
  }
  return false;
}

enum NameCollisionResolution {
  NCR_First,
  NCR_Second,
  NCR_DupDef,
  NCR_DupUndef,
  NCR_DupShLib,
  NCR_Error
};

static NameCollisionResolution cases[4][4] = {
  //regular     absolute    undef      sharedLib
  {
    // first is regular
    NCR_DupDef, NCR_Error,   NCR_First, NCR_First
  },
  {
    // first is absolute
    NCR_Error,  NCR_Error,  NCR_First, NCR_First
  },
  {
    // first is undef
    NCR_Second, NCR_Second, NCR_DupUndef, NCR_Second
  },
  {
    // first is sharedLib
    NCR_Second, NCR_Second, NCR_First, NCR_DupShLib
  }
};

static NameCollisionResolution collide(Atom::Definition first,
                                       Atom::Definition second) {
  return cases[first][second];
}

enum MergeResolution {
  MCR_First,
  MCR_Second,
  MCR_Largest,
  MCR_SameSize,
  MCR_Error
};

static MergeResolution mergeCases[][6] = {
  // no          tentative      weak          weakAddress   sameNameAndSize largest
  {MCR_Error,    MCR_First,     MCR_First,    MCR_First,    MCR_SameSize,   MCR_Largest},  // no
  {MCR_Second,   MCR_Largest,   MCR_Second,   MCR_Second,   MCR_SameSize,   MCR_Largest},  // tentative
  {MCR_Second,   MCR_First,     MCR_First,    MCR_Second,   MCR_SameSize,   MCR_Largest},  // weak
  {MCR_Second,   MCR_First,     MCR_First,    MCR_First,    MCR_SameSize,   MCR_Largest},  // weakAddress
  {MCR_SameSize, MCR_SameSize,  MCR_SameSize, MCR_SameSize, MCR_SameSize,   MCR_SameSize}, // sameSize
  {MCR_Largest,  MCR_Largest,   MCR_Largest,  MCR_Largest,  MCR_SameSize,   MCR_Largest},  // largest
};

static MergeResolution mergeSelect(DefinedAtom::Merge first,
                                   DefinedAtom::Merge second) {
  assert(first != DefinedAtom::mergeByContent);
  assert(second != DefinedAtom::mergeByContent);
  return mergeCases[first][second];
}

bool SymbolTable::addByName(const Atom &newAtom) {
  StringRef name = newAtom.name();
  assert(!name.empty());
  const Atom *existing = findByName(name);
  if (existing == nullptr) {
    // Name is not in symbol table yet, add it associate with this atom.
    _nameTable[name] = &newAtom;
    return true;
  }

  // Do nothing if the same object is added more than once.
  if (existing == &newAtom)
    return false;

  // Name is already in symbol table and associated with another atom.
  bool useNew = true;
  switch (collide(existing->definition(), newAtom.definition())) {
  case NCR_First:
    useNew = false;
    break;
  case NCR_Second:
    useNew = true;
    break;
  case NCR_DupDef: {
    const auto *existingDef = cast<DefinedAtom>(existing);
    const auto *newDef = cast<DefinedAtom>(&newAtom);
    switch (mergeSelect(existingDef->merge(), newDef->merge())) {
    case MCR_First:
      useNew = false;
      break;
    case MCR_Second:
      useNew = true;
      break;
    case MCR_Largest: {
      uint64_t existingSize = existingDef->sectionSize();
      uint64_t newSize = newDef->sectionSize();
      useNew = (newSize >= existingSize);
      break;
    }
    case MCR_SameSize: {
      uint64_t existingSize = existingDef->sectionSize();
      uint64_t newSize = newDef->sectionSize();
      if (existingSize == newSize) {
        useNew = true;
        break;
      }
      llvm::errs() << "Size mismatch: "
                   << existing->name() << " (" << existingSize << ") "
                   << newAtom.name() << " (" << newSize << ")\n";
      LLVM_FALLTHROUGH;
    }
    case MCR_Error:
      llvm::errs() << "Duplicate symbols: "
                   << existing->name()
                   << ":"
                   << existing->file().path()
                   << " and "
                   << newAtom.name()
                   << ":"
                   << newAtom.file().path()
                   << "\n";
      llvm::report_fatal_error("duplicate symbol error");
      break;
    }
    break;
  }
  case NCR_DupUndef: {
    const UndefinedAtom* existingUndef = cast<UndefinedAtom>(existing);
    const UndefinedAtom* newUndef = cast<UndefinedAtom>(&newAtom);

    bool sameCanBeNull = (existingUndef->canBeNull() == newUndef->canBeNull());
    if (sameCanBeNull)
      useNew = false;
    else
      useNew = (newUndef->canBeNull() < existingUndef->canBeNull());
    break;
  }
  case NCR_DupShLib: {
    useNew = false;
    break;
  }
  case NCR_Error:
    llvm::errs() << "SymbolTable: error while merging " << name << "\n";
    llvm::report_fatal_error("duplicate symbol error");
    break;
  }

  if (useNew) {
    // Update name table to use new atom.
    _nameTable[name] = &newAtom;
    // Add existing atom to replacement table.
    _replacedAtoms[existing] = &newAtom;
  } else {
    // New atom is not being used.  Add it to replacement table.
    _replacedAtoms[&newAtom] = existing;
  }
  return false;
}

unsigned SymbolTable::AtomMappingInfo::getHashValue(const DefinedAtom *atom) {
  auto content = atom->rawContent();
  return llvm::hash_combine(atom->size(),
                            atom->contentType(),
                            llvm::hash_combine_range(content.begin(),
                                                     content.end()));
}

bool SymbolTable::AtomMappingInfo::isEqual(const DefinedAtom * const l,
                                           const DefinedAtom * const r) {
  if (l == r)
    return true;
  if (l == getEmptyKey() || r == getEmptyKey())
    return false;
  if (l == getTombstoneKey() || r == getTombstoneKey())
    return false;
  if (l->contentType() != r->contentType())
    return false;
  if (l->size() != r->size())
    return false;
  if (l->sectionChoice() != r->sectionChoice())
    return false;
  if (l->sectionChoice() == DefinedAtom::sectionCustomRequired) {
    if (!l->customSectionName().equals(r->customSectionName()))
      return false;
  }
  ArrayRef<uint8_t> lc = l->rawContent();
  ArrayRef<uint8_t> rc = r->rawContent();
  return memcmp(lc.data(), rc.data(), lc.size()) == 0;
}

bool SymbolTable::addByContent(const DefinedAtom &newAtom) {
  AtomContentSet::iterator pos = _contentTable.find(&newAtom);
  if (pos == _contentTable.end()) {
    _contentTable.insert(&newAtom);
    return true;
  }
  const Atom* existing = *pos;
  // New atom is not being used.  Add it to replacement table.
  _replacedAtoms[&newAtom] = existing;
  return false;
}

const Atom *SymbolTable::findByName(StringRef sym) {
  NameToAtom::iterator pos = _nameTable.find(sym);
  if (pos == _nameTable.end())
    return nullptr;
  return pos->second;
}

const Atom *SymbolTable::replacement(const Atom *atom) {
  // Find the replacement for a given atom. Atoms in _replacedAtoms
  // may be chained, so find the last one.
  for (;;) {
    AtomToAtom::iterator pos = _replacedAtoms.find(atom);
    if (pos == _replacedAtoms.end())
      return atom;
    atom = pos->second;
  }
}

bool SymbolTable::isCoalescedAway(const Atom *atom) {
  return _replacedAtoms.count(atom) > 0;
}

std::vector<const UndefinedAtom *> SymbolTable::undefines() {
  std::vector<const UndefinedAtom *> ret;
  for (auto it : _nameTable) {
    const Atom *atom = it.second;
    assert(atom != nullptr);
    if (const auto *undef = dyn_cast<const UndefinedAtom>(atom))
      if (_replacedAtoms.count(undef) == 0)
        ret.push_back(undef);
  }
  return ret;
}

} // namespace lld
