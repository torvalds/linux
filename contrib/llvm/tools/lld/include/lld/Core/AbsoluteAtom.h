//===- Core/AbsoluteAtom.h - An absolute Atom -----------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_ABSOLUTE_ATOM_H
#define LLD_CORE_ABSOLUTE_ATOM_H

#include "lld/Core/Atom.h"

namespace lld {

/// An AbsoluteAtom has no content.
/// It exists to represent content at fixed addresses in memory.
class AbsoluteAtom : public Atom {
public:

  virtual uint64_t value() const = 0;

  /// scope - The visibility of this atom to other atoms.  C static functions
  /// have scope scopeTranslationUnit.  Regular C functions have scope
  /// scopeGlobal.  Functions compiled with visibility=hidden have scope
  /// scopeLinkageUnit so they can be see by other atoms being linked but not
  /// by the OS loader.
  virtual Scope scope() const = 0;

  static bool classof(const Atom *a) {
    return a->definition() == definitionAbsolute;
  }

  static bool classof(const AbsoluteAtom *) { return true; }

protected:
  AbsoluteAtom() : Atom(definitionAbsolute) {}
};

} // namespace lld

#endif // LLD_CORE_ABSOLUTE_ATOM_H
