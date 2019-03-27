//===- lld/Core/Simple.h - Simple implementations of Atom and File --------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provide simple implementations for Atoms and File.
///
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_SIMPLE_H
#define LLD_CORE_SIMPLE_H

#include "lld/Core/AbsoluteAtom.h"
#include "lld/Core/Atom.h"
#include "lld/Core/DefinedAtom.h"
#include "lld/Core/File.h"
#include "lld/Core/Reference.h"
#include "lld/Core/SharedLibraryAtom.h"
#include "lld/Core/UndefinedAtom.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>

namespace lld {

class SimpleFile : public File {
public:
  SimpleFile(StringRef path, File::Kind kind)
    : File(path, kind) {}

  ~SimpleFile() override {
    _defined.clear();
    _undefined.clear();
    _shared.clear();
    _absolute.clear();
  }

  void addAtom(DefinedAtom &a) {
    _defined.push_back(OwningAtomPtr<DefinedAtom>(&a));
  }
  void addAtom(UndefinedAtom &a) {
    _undefined.push_back(OwningAtomPtr<UndefinedAtom>(&a));
  }
  void addAtom(SharedLibraryAtom &a) {
    _shared.push_back(OwningAtomPtr<SharedLibraryAtom>(&a));
  }
  void addAtom(AbsoluteAtom &a) {
    _absolute.push_back(OwningAtomPtr<AbsoluteAtom>(&a));
  }

  void addAtom(const Atom &atom) {
    if (auto *p = dyn_cast<DefinedAtom>(&atom)) {
      addAtom(const_cast<DefinedAtom &>(*p));
    } else if (auto *p = dyn_cast<UndefinedAtom>(&atom)) {
      addAtom(const_cast<UndefinedAtom &>(*p));
    } else if (auto *p = dyn_cast<SharedLibraryAtom>(&atom)) {
      addAtom(const_cast<SharedLibraryAtom &>(*p));
    } else if (auto *p = dyn_cast<AbsoluteAtom>(&atom)) {
      addAtom(const_cast<AbsoluteAtom &>(*p));
    } else {
      llvm_unreachable("atom has unknown definition kind");
    }
  }

  void removeDefinedAtomsIf(std::function<bool(const DefinedAtom *)> pred) {
    auto &atoms = _defined;
    auto newEnd = std::remove_if(atoms.begin(), atoms.end(),
                                 [&pred](OwningAtomPtr<DefinedAtom> &p) {
                                   return pred(p.get());
                                 });
    atoms.erase(newEnd, atoms.end());
  }

  const AtomRange<DefinedAtom> defined() const override { return _defined; }

  const AtomRange<UndefinedAtom> undefined() const override {
    return _undefined;
  }

  const AtomRange<SharedLibraryAtom> sharedLibrary() const override {
    return _shared;
  }

  const AtomRange<AbsoluteAtom> absolute() const override {
    return _absolute;
  }

  void clearAtoms() override {
    _defined.clear();
    _undefined.clear();
    _shared.clear();
    _absolute.clear();
  }

private:
  AtomVector<DefinedAtom> _defined;
  AtomVector<UndefinedAtom> _undefined;
  AtomVector<SharedLibraryAtom> _shared;
  AtomVector<AbsoluteAtom> _absolute;
};

class SimpleReference : public Reference,
                        public llvm::ilist_node<SimpleReference> {
public:
  SimpleReference(Reference::KindNamespace ns, Reference::KindArch arch,
                  Reference::KindValue value, uint64_t off, const Atom *t,
                  Reference::Addend a)
      : Reference(ns, arch, value), _target(t), _offsetInAtom(off), _addend(a) {
  }
  SimpleReference()
      : Reference(Reference::KindNamespace::all, Reference::KindArch::all, 0),
        _target(nullptr), _offsetInAtom(0), _addend(0) {}

  uint64_t offsetInAtom() const override { return _offsetInAtom; }

  const Atom *target() const override {
    assert(_target);
    return _target;
  }

  Addend addend() const override { return _addend; }
  void setAddend(Addend a) override { _addend = a; }
  void setTarget(const Atom *newAtom) override { _target = newAtom; }

private:
  const Atom *_target;
  uint64_t _offsetInAtom;
  Addend _addend;
};

class SimpleDefinedAtom : public DefinedAtom {
public:
  explicit SimpleDefinedAtom(const File &f)
      : _file(f), _ordinal(f.getNextAtomOrdinalAndIncrement()) {}

  ~SimpleDefinedAtom() override {
    _references.clearAndLeakNodesUnsafely();
  }

  const File &file() const override { return _file; }

  StringRef name() const override { return StringRef(); }

  uint64_t ordinal() const override { return _ordinal; }

  Scope scope() const override { return DefinedAtom::scopeLinkageUnit; }

  Interposable interposable() const override {
    return DefinedAtom::interposeNo;
  }

  Merge merge() const override { return DefinedAtom::mergeNo; }

  Alignment alignment() const override { return 1; }

  SectionChoice sectionChoice() const override {
    return DefinedAtom::sectionBasedOnContent;
  }

  StringRef customSectionName() const override { return StringRef(); }
  DeadStripKind deadStrip() const override {
    return DefinedAtom::deadStripNormal;
  }

  DefinedAtom::reference_iterator begin() const override {
    const void *it =
        reinterpret_cast<const void *>(_references.begin().getNodePtr());
    return reference_iterator(*this, it);
  }

  DefinedAtom::reference_iterator end() const override {
    const void *it =
        reinterpret_cast<const void *>(_references.end().getNodePtr());
    return reference_iterator(*this, it);
  }

  const Reference *derefIterator(const void *it) const override {
    return &*RefList::const_iterator(
        *reinterpret_cast<const llvm::ilist_node<SimpleReference> *>(it));
  }

  void incrementIterator(const void *&it) const override {
    RefList::const_iterator ref(
        *reinterpret_cast<const llvm::ilist_node<SimpleReference> *>(it));
    it = reinterpret_cast<const void *>(std::next(ref).getNodePtr());
  }

  void addReference(Reference::KindNamespace ns,
                    Reference::KindArch arch,
                    Reference::KindValue kindValue, uint64_t off,
                    const Atom *target, Reference::Addend a) override {
    assert(target && "trying to create reference to nothing");
    auto node = new (_file.allocator())
        SimpleReference(ns, arch, kindValue, off, target, a);
    _references.push_back(node);
  }

  /// Sort references in a canonical order (by offset, then by kind).
  void sortReferences() const {
    // Cannot sort a linked  list, so move elements into a temporary vector,
    // sort the vector, then reconstruct the list.
    llvm::SmallVector<SimpleReference *, 16> elements;
    for (SimpleReference &node : _references) {
      elements.push_back(&node);
    }
    std::sort(elements.begin(), elements.end(),
        [] (const SimpleReference *lhs, const SimpleReference *rhs) -> bool {
          uint64_t lhsOffset = lhs->offsetInAtom();
          uint64_t rhsOffset = rhs->offsetInAtom();
          if (rhsOffset != lhsOffset)
            return (lhsOffset < rhsOffset);
          if (rhs->kindNamespace() != lhs->kindNamespace())
            return (lhs->kindNamespace() < rhs->kindNamespace());
          if (rhs->kindArch() != lhs->kindArch())
            return (lhs->kindArch() < rhs->kindArch());
          return (lhs->kindValue() < rhs->kindValue());
        });
    _references.clearAndLeakNodesUnsafely();
    for (SimpleReference *node : elements) {
      _references.push_back(node);
    }
  }

  void setOrdinal(uint64_t ord) { _ordinal = ord; }

private:
  typedef llvm::ilist<SimpleReference> RefList;

  const File &_file;
  uint64_t _ordinal;
  mutable RefList _references;
};

class SimpleUndefinedAtom : public UndefinedAtom {
public:
  SimpleUndefinedAtom(const File &f, StringRef name) : _file(f), _name(name) {
    assert(!name.empty() && "UndefinedAtoms must have a name");
  }

  ~SimpleUndefinedAtom() override = default;

  /// file - returns the File that produced/owns this Atom
  const File &file() const override { return _file; }

  /// name - The name of the atom. For a function atom, it is the (mangled)
  /// name of the function.
  StringRef name() const override { return _name; }

  CanBeNull canBeNull() const override { return UndefinedAtom::canBeNullNever; }

private:
  const File &_file;
  StringRef _name;
};

} // end namespace lld

#endif // LLD_CORE_SIMPLE_H
