//===- lib/ReaderWriter/MachO/ShimPass.cpp -------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This linker pass updates branch-sites whose target is a different mode
// (thumb vs arm).
//
// Arm code has two instruction encodings thumb and arm.  When branching from
// one code encoding to another, you need to use an instruction that switches
// the instruction mode.  Usually the transition only happens at call sites, and
// the linker can transform a BL instruction in BLX (or vice versa).  But if the
// compiler did a tail call optimization and a function ends with a branch (not
// branch and link), there is no pc-rel BX instruction.
//
// The ShimPass looks for pc-rel B instructions that will need to switch mode.
// For those cases it synthesizes a shim which does the transition, then
// modifies the original atom with the B instruction to target to the shim atom.
//
//===----------------------------------------------------------------------===//

#include "ArchHandler.h"
#include "File.h"
#include "MachOPasses.h"
#include "lld/Common/LLVM.h"
#include "lld/Core/DefinedAtom.h"
#include "lld/Core/File.h"
#include "lld/Core/Reference.h"
#include "lld/Core/Simple.h"
#include "lld/ReaderWriter/MachOLinkingContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

namespace lld {
namespace mach_o {

class ShimPass : public Pass {
public:
  ShimPass(const MachOLinkingContext &context)
      : _ctx(context), _archHandler(_ctx.archHandler()),
        _stubInfo(_archHandler.stubInfo()),
        _file(*_ctx.make_file<MachOFile>("<mach-o shim pass>")) {
    _file.setOrdinal(_ctx.getNextOrdinalAndIncrement());
  }

  llvm::Error perform(SimpleFile &mergedFile) override {
    // Scan all references in all atoms.
    for (const DefinedAtom *atom : mergedFile.defined()) {
      for (const Reference *ref : *atom) {
        // Look at non-call branches.
        if (!_archHandler.isNonCallBranch(*ref))
          continue;
        const Atom *target = ref->target();
        assert(target != nullptr);
        if (const lld::DefinedAtom *daTarget = dyn_cast<DefinedAtom>(target)) {
          bool atomIsThumb = _archHandler.isThumbFunction(*atom);
          bool targetIsThumb = _archHandler.isThumbFunction(*daTarget);
          if (atomIsThumb != targetIsThumb)
            updateBranchToUseShim(atomIsThumb, *daTarget, ref);
        }
      }
    }
    // Exit early if no shims needed.
    if (_targetToShim.empty())
      return llvm::Error::success();

    // Sort shim atoms so the layout order is stable.
    std::vector<const DefinedAtom *> shims;
    shims.reserve(_targetToShim.size());
    for (auto element : _targetToShim) {
      shims.push_back(element.second);
    }
    std::sort(shims.begin(), shims.end(),
              [](const DefinedAtom *l, const DefinedAtom *r) {
                return (l->name() < r->name());
              });

    // Add all shims to master file.
    for (const DefinedAtom *shim : shims)
      mergedFile.addAtom(*shim);

    return llvm::Error::success();
  }

private:

  void updateBranchToUseShim(bool thumbToArm, const DefinedAtom& target,
                             const Reference *ref) {
    // Make file-format specific stub and other support atoms.
    const DefinedAtom *shim = this->getShim(thumbToArm, target);
    assert(shim != nullptr);
    // Switch branch site to target shim atom.
    const_cast<Reference *>(ref)->setTarget(shim);
  }

  const DefinedAtom* getShim(bool thumbToArm, const DefinedAtom& target) {
    auto pos = _targetToShim.find(&target);
    if ( pos != _targetToShim.end() ) {
      // Reuse an existing shim.
      assert(pos->second != nullptr);
      return pos->second;
    } else {
      // There is no existing shim, so create a new one.
      const DefinedAtom *shim = _archHandler.createShim(_file, thumbToArm,
                                                        target);
       _targetToShim[&target] = shim;
       return shim;
    }
  }

  const MachOLinkingContext &_ctx;
  mach_o::ArchHandler                            &_archHandler;
  const ArchHandler::StubInfo                    &_stubInfo;
  MachOFile                                      &_file;
  llvm::DenseMap<const Atom*, const DefinedAtom*> _targetToShim;
};



void addShimPass(PassManager &pm, const MachOLinkingContext &ctx) {
  pm.add(llvm::make_unique<ShimPass>(ctx));
}

} // end namespace mach_o
} // end namespace lld
