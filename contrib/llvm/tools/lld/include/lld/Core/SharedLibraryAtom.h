//===- Core/SharedLibraryAtom.h - A Shared Library Atom -------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_SHARED_LIBRARY_ATOM_H
#define LLD_CORE_SHARED_LIBRARY_ATOM_H

#include "lld/Core/Atom.h"

namespace lld {

/// A SharedLibraryAtom has no content.
/// It exists to represent a symbol which will be bound at runtime.
class SharedLibraryAtom : public Atom {
public:
  enum class Type : uint32_t {
    Unknown,
    Code,
    Data,
  };

  /// Returns shared library name used to load it at runtime.
  /// On Darwin it is the LC_DYLIB_LOAD dylib name.
  virtual StringRef loadName() const = 0;

  /// Returns if shared library symbol can be missing at runtime and if
  /// so the loader should silently resolve address of symbol to be nullptr.
  virtual bool canBeNullAtRuntime() const = 0;

  virtual Type type() const = 0;

  virtual uint64_t size() const = 0;

  static bool classof(const Atom *a) {
    return a->definition() == definitionSharedLibrary;
  }

  static inline bool classof(const SharedLibraryAtom *) { return true; }

protected:
  SharedLibraryAtom() : Atom(definitionSharedLibrary) {}

  ~SharedLibraryAtom() override = default;
};

} // namespace lld

#endif // LLD_CORE_SHARED_LIBRARY_ATOM_H
