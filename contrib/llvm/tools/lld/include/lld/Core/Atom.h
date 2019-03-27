//===- Core/Atom.h - A node in linking graph --------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_ATOM_H
#define LLD_CORE_ATOM_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/StringRef.h"

namespace lld {

class File;

template<typename T>
class OwningAtomPtr;

///
/// The linker has a Graph Theory model of linking. An object file is seen
/// as a set of Atoms with References to other Atoms.  Each Atom is a node
/// and each Reference is an edge. An Atom can be a DefinedAtom which has
/// content or a UndefinedAtom which is a placeholder and represents an
/// undefined symbol (extern declaration).
///
class Atom {
  template<typename T> friend class OwningAtomPtr;

public:
  /// Whether this atom is defined or a proxy for an undefined symbol
  enum Definition {
    definitionRegular,      ///< Normal C/C++ function or global variable.
    definitionAbsolute,     ///< Asm-only (foo = 10). Not tied to any content.
    definitionUndefined,    ///< Only in .o files to model reference to undef.
    definitionSharedLibrary ///< Only in shared libraries to model export.
  };

  /// The scope in which this atom is acessible to other atoms.
  enum Scope {
    scopeTranslationUnit,  ///< Accessible only to atoms in the same translation
                           ///  unit (e.g. a C static).
    scopeLinkageUnit,      ///< Accessible to atoms being linked but not visible
                           ///  to runtime loader (e.g. visibility=hidden).
    scopeGlobal            ///< Accessible to all atoms and visible to runtime
                           ///  loader (e.g. visibility=default).
  };

  /// file - returns the File that produced/owns this Atom
  virtual const File& file() const = 0;

  /// name - The name of the atom. For a function atom, it is the (mangled)
  /// name of the function.
  virtual StringRef name() const = 0;

  /// definition - Whether this atom is a definition or represents an undefined
  /// symbol.
  Definition definition() const { return _definition; }

  static bool classof(const Atom *a) { return true; }

protected:
  /// Atom is an abstract base class.  Only subclasses can access constructor.
  explicit Atom(Definition def) : _definition(def) {}

  /// The memory for Atom objects is always managed by the owning File
  /// object.  Therefore, no one but the owning File object should call
  /// delete on an Atom.  In fact, some File objects may bulk allocate
  /// an array of Atoms, so they cannot be individually deleted by anyone.
  virtual ~Atom() = default;

private:
  Definition _definition;
};

/// Class which owns an atom pointer and runs the atom destructor when the
/// owning pointer goes out of scope.
template<typename T>
class OwningAtomPtr {
private:
  OwningAtomPtr(const OwningAtomPtr &) = delete;
  void operator=(const OwningAtomPtr &) = delete;

public:
  OwningAtomPtr() = default;
  OwningAtomPtr(T *atom) : atom(atom) { }

  ~OwningAtomPtr() {
    if (atom)
      runDestructor(atom);
  }

  void runDestructor(Atom *atom) {
    atom->~Atom();
  }

  OwningAtomPtr(OwningAtomPtr &&ptr) : atom(ptr.atom) {
    ptr.atom = nullptr;
  }

  void operator=(OwningAtomPtr&& ptr) {
    if (atom)
      runDestructor(atom);
    atom = ptr.atom;
    ptr.atom = nullptr;
  }

  T *const &get() const {
    return atom;
  }

  T *&get() {
    return atom;
  }

  T *release() {
    auto *v = atom;
    atom = nullptr;
    return v;
  }

private:
  T *atom = nullptr;
};

} // end namespace lld

#endif // LLD_CORE_ATOM_H
