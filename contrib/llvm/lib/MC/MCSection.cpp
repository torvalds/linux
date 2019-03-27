//===- lib/MC/MCSection.cpp - Machine Code Section Representation ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSection.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <utility>

using namespace llvm;

MCSection::MCSection(SectionVariant V, SectionKind K, MCSymbol *Begin)
    : Begin(Begin), BundleGroupBeforeFirstInst(false), HasInstructions(false),
      HasData(false), IsRegistered(false), DummyFragment(this), Variant(V),
      Kind(K) {}

MCSymbol *MCSection::getEndSymbol(MCContext &Ctx) {
  if (!End)
    End = Ctx.createTempSymbol("sec_end", true);
  return End;
}

bool MCSection::hasEnded() const { return End && End->isInSection(); }

MCSection::~MCSection() = default;

void MCSection::setBundleLockState(BundleLockStateType NewState) {
  if (NewState == NotBundleLocked) {
    if (BundleLockNestingDepth == 0) {
      report_fatal_error("Mismatched bundle_lock/unlock directives");
    }
    if (--BundleLockNestingDepth == 0) {
      BundleLockState = NotBundleLocked;
    }
    return;
  }

  // If any of the directives is an align_to_end directive, the whole nested
  // group is align_to_end. So don't downgrade from align_to_end to just locked.
  if (BundleLockState != BundleLockedAlignToEnd) {
    BundleLockState = NewState;
  }
  ++BundleLockNestingDepth;
}

MCSection::iterator
MCSection::getSubsectionInsertionPoint(unsigned Subsection) {
  if (Subsection == 0 && SubsectionFragmentMap.empty())
    return end();

  SmallVectorImpl<std::pair<unsigned, MCFragment *>>::iterator MI =
      std::lower_bound(SubsectionFragmentMap.begin(),
                       SubsectionFragmentMap.end(),
                       std::make_pair(Subsection, (MCFragment *)nullptr));
  bool ExactMatch = false;
  if (MI != SubsectionFragmentMap.end()) {
    ExactMatch = MI->first == Subsection;
    if (ExactMatch)
      ++MI;
  }
  iterator IP;
  if (MI == SubsectionFragmentMap.end())
    IP = end();
  else
    IP = MI->second->getIterator();
  if (!ExactMatch && Subsection != 0) {
    // The GNU as documentation claims that subsections have an alignment of 4,
    // although this appears not to be the case.
    MCFragment *F = new MCDataFragment();
    SubsectionFragmentMap.insert(MI, std::make_pair(Subsection, F));
    getFragmentList().insert(IP, F);
    F->setParent(this);
  }

  return IP;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MCSection::dump() const {
  raw_ostream &OS = errs();

  OS << "<MCSection";
  OS << " Fragments:[\n      ";
  for (auto it = begin(), ie = end(); it != ie; ++it) {
    if (it != begin())
      OS << ",\n      ";
    it->dump();
  }
  OS << "]>";
}
#endif
