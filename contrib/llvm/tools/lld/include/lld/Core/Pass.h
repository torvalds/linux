//===------ Core/Pass.h - Base class for linker passes ----------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_CORE_PASS_H
#define LLD_CORE_PASS_H

#include "llvm/Support/Error.h"

namespace lld {

class SimpleFile;

/// Once the core linking is done (which resolves references, coalesces atoms
/// and produces a complete Atom graph), the linker runs a series of passes
/// on the Atom graph. The graph is modeled as a File, which means the pass
/// has access to all the atoms and to File level attributes. Each pass does
/// a particular transformation to the Atom graph or to the File attributes.
///
/// This is the abstract base class for all passes.  A Pass does its
/// actual work in it perform() method.  It can iterator over Atoms in the
/// graph using the *begin()/*end() atom iterator of the File.  It can add
/// new Atoms to the graph using the File's addAtom() method.
class Pass {
public:
  virtual ~Pass() = default;

  /// Do the actual work of the Pass.
  virtual llvm::Error perform(SimpleFile &mergedFile) = 0;

protected:
  // Only subclassess can be instantiated.
  Pass() = default;
};

} // end namespace lld

#endif // LLD_CORE_PASS_H
