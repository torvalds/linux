//===- MCSection.h - Machine Code Sections ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSection class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTION_H
#define LLVM_MC_MCSECTION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/SectionKind.h"
#include <cassert>
#include <utility>

namespace llvm {

class MCAsmInfo;
class MCContext;
class MCExpr;
class MCSymbol;
class raw_ostream;
class Triple;

template <> struct ilist_alloc_traits<MCFragment> {
  static void deleteNode(MCFragment *V);
};

/// Instances of this class represent a uniqued identifier for a section in the
/// current translation unit.  The MCContext class uniques and creates these.
class MCSection {
public:
  enum SectionVariant { SV_COFF = 0, SV_ELF, SV_MachO, SV_Wasm };

  /// Express the state of bundle locked groups while emitting code.
  enum BundleLockStateType {
    NotBundleLocked,
    BundleLocked,
    BundleLockedAlignToEnd
  };

  using FragmentListType = iplist<MCFragment>;

  using const_iterator = FragmentListType::const_iterator;
  using iterator = FragmentListType::iterator;

  using const_reverse_iterator = FragmentListType::const_reverse_iterator;
  using reverse_iterator = FragmentListType::reverse_iterator;

private:
  MCSymbol *Begin;
  MCSymbol *End = nullptr;
  /// The alignment requirement of this section.
  unsigned Alignment = 1;
  /// The section index in the assemblers section list.
  unsigned Ordinal = 0;
  /// The index of this section in the layout order.
  unsigned LayoutOrder;

  /// Keeping track of bundle-locked state.
  BundleLockStateType BundleLockState = NotBundleLocked;

  /// Current nesting depth of bundle_lock directives.
  unsigned BundleLockNestingDepth = 0;

  /// We've seen a bundle_lock directive but not its first instruction
  /// yet.
  bool BundleGroupBeforeFirstInst : 1;

  /// Whether this section has had instructions emitted into it.
  bool HasInstructions : 1;

  /// Whether this section has had data emitted into it.
  /// Right now this is only used by the ARM backend.
  bool HasData : 1;

  bool IsRegistered : 1;

  MCDummyFragment DummyFragment;

  FragmentListType Fragments;

  /// Mapping from subsection number to insertion point for subsection numbers
  /// below that number.
  SmallVector<std::pair<unsigned, MCFragment *>, 1> SubsectionFragmentMap;

protected:
  SectionVariant Variant;
  SectionKind Kind;

  MCSection(SectionVariant V, SectionKind K, MCSymbol *Begin);
  ~MCSection();

public:
  MCSection(const MCSection &) = delete;
  MCSection &operator=(const MCSection &) = delete;

  SectionKind getKind() const { return Kind; }

  SectionVariant getVariant() const { return Variant; }

  MCSymbol *getBeginSymbol() { return Begin; }
  const MCSymbol *getBeginSymbol() const {
    return const_cast<MCSection *>(this)->getBeginSymbol();
  }
  void setBeginSymbol(MCSymbol *Sym) {
    assert(!Begin);
    Begin = Sym;
  }
  MCSymbol *getEndSymbol(MCContext &Ctx);
  bool hasEnded() const;

  unsigned getAlignment() const { return Alignment; }
  void setAlignment(unsigned Value) { Alignment = Value; }

  unsigned getOrdinal() const { return Ordinal; }
  void setOrdinal(unsigned Value) { Ordinal = Value; }

  unsigned getLayoutOrder() const { return LayoutOrder; }
  void setLayoutOrder(unsigned Value) { LayoutOrder = Value; }

  BundleLockStateType getBundleLockState() const { return BundleLockState; }
  void setBundleLockState(BundleLockStateType NewState);
  bool isBundleLocked() const { return BundleLockState != NotBundleLocked; }

  bool isBundleGroupBeforeFirstInst() const {
    return BundleGroupBeforeFirstInst;
  }
  void setBundleGroupBeforeFirstInst(bool IsFirst) {
    BundleGroupBeforeFirstInst = IsFirst;
  }

  bool hasInstructions() const { return HasInstructions; }
  void setHasInstructions(bool Value) { HasInstructions = Value; }

  bool hasData() const { return HasData; }
  void setHasData(bool Value) { HasData = Value; }

  bool isRegistered() const { return IsRegistered; }
  void setIsRegistered(bool Value) { IsRegistered = Value; }

  MCSection::FragmentListType &getFragmentList() { return Fragments; }
  const MCSection::FragmentListType &getFragmentList() const {
    return const_cast<MCSection *>(this)->getFragmentList();
  }

  /// Support for MCFragment::getNextNode().
  static FragmentListType MCSection::*getSublistAccess(MCFragment *) {
    return &MCSection::Fragments;
  }

  const MCDummyFragment &getDummyFragment() const { return DummyFragment; }
  MCDummyFragment &getDummyFragment() { return DummyFragment; }

  iterator begin() { return Fragments.begin(); }
  const_iterator begin() const { return Fragments.begin(); }

  iterator end() { return Fragments.end(); }
  const_iterator end() const { return Fragments.end(); }

  reverse_iterator rbegin() { return Fragments.rbegin(); }
  const_reverse_iterator rbegin() const { return Fragments.rbegin(); }

  reverse_iterator rend() { return Fragments.rend(); }
  const_reverse_iterator rend() const  { return Fragments.rend(); }

  MCSection::iterator getSubsectionInsertionPoint(unsigned Subsection);

  void dump() const;

  virtual void PrintSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                                    raw_ostream &OS,
                                    const MCExpr *Subsection) const = 0;

  /// Return true if a .align directive should use "optimized nops" to fill
  /// instead of 0s.
  virtual bool UseCodeAlign() const = 0;

  /// Check whether this section is "virtual", that is has no actual object
  /// file contents.
  virtual bool isVirtualSection() const = 0;
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTION_H
