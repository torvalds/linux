//===- MCCodePadder.cpp - Target MC Code Padder ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCCodePadder.h"
#include "llvm/MC/MCObjectStreamer.h"
#include <algorithm>
#include <limits>
#include <numeric>

using namespace llvm;

//---------------------------------------------------------------------------
// MCCodePadder
//

MCCodePadder::~MCCodePadder() {
  for (auto *Policy : CodePaddingPolicies)
    delete Policy;
}

bool MCCodePadder::addPolicy(MCCodePaddingPolicy *Policy) {
  assert(Policy && "Policy must be valid");
  return CodePaddingPolicies.insert(Policy).second;
}

void MCCodePadder::handleBasicBlockStart(MCObjectStreamer *OS,
                                         const MCCodePaddingContext &Context) {
  assert(OS != nullptr && "OS must be valid");
  assert(this->OS == nullptr && "Still handling another basic block");
  this->OS = OS;

  ArePoliciesActive = usePoliciesForBasicBlock(Context);

  bool InsertionPoint = basicBlockRequiresInsertionPoint(Context);
  assert((!InsertionPoint ||
          OS->getCurrentFragment()->getKind() != MCFragment::FT_Align) &&
         "Cannot insert padding nops right after an alignment fragment as it "
         "will ruin the alignment");

  uint64_t PoliciesMask = MCPaddingFragment::PFK_None;
  if (ArePoliciesActive) {
    PoliciesMask = std::accumulate(
        CodePaddingPolicies.begin(), CodePaddingPolicies.end(),
        MCPaddingFragment::PFK_None,
        [&Context](uint64_t Mask,
                   const MCCodePaddingPolicy *Policy) -> uint64_t {
          return Policy->basicBlockRequiresPaddingFragment(Context)
                     ? (Mask | Policy->getKindMask())
                     : Mask;
        });
  }

  if (InsertionPoint || PoliciesMask != MCPaddingFragment::PFK_None) {
    MCPaddingFragment *PaddingFragment = OS->getOrCreatePaddingFragment();
    if (InsertionPoint)
      PaddingFragment->setAsInsertionPoint();
    PaddingFragment->setPaddingPoliciesMask(
        PaddingFragment->getPaddingPoliciesMask() | PoliciesMask);
  }
}

void MCCodePadder::handleBasicBlockEnd(const MCCodePaddingContext &Context) {
  assert(this->OS != nullptr && "Not handling a basic block");
  OS = nullptr;
}

void MCCodePadder::handleInstructionBegin(const MCInst &Inst) {
  if (!OS)
    return; // instruction was emitted outside a function

  assert(CurrHandledInstFragment == nullptr && "Can't start handling an "
                                               "instruction while still "
                                               "handling another instruction");

  bool InsertionPoint = instructionRequiresInsertionPoint(Inst);
  assert((!InsertionPoint ||
          OS->getCurrentFragment()->getKind() != MCFragment::FT_Align) &&
         "Cannot insert padding nops right after an alignment fragment as it "
         "will ruin the alignment");

  uint64_t PoliciesMask = MCPaddingFragment::PFK_None;
  if (ArePoliciesActive) {
    PoliciesMask = std::accumulate(
        CodePaddingPolicies.begin(), CodePaddingPolicies.end(),
        MCPaddingFragment::PFK_None,
        [&Inst](uint64_t Mask, const MCCodePaddingPolicy *Policy) -> uint64_t {
          return Policy->instructionRequiresPaddingFragment(Inst)
                     ? (Mask | Policy->getKindMask())
                     : Mask;
        });
  }
  MCFragment *CurrFragment = OS->getCurrentFragment();
  // CurrFragment can be a previously created MCPaddingFragment. If so, let's
  // update it with the information we have, such as the instruction that it
  // should point to.
  bool needToUpdateCurrFragment =
      CurrFragment != nullptr &&
      CurrFragment->getKind() == MCFragment::FT_Padding;
  if (InsertionPoint || PoliciesMask != MCPaddingFragment::PFK_None ||
      needToUpdateCurrFragment) {
    // temporarily holding the fragment as CurrHandledInstFragment, to be
    // updated after the instruction will be written
    CurrHandledInstFragment = OS->getOrCreatePaddingFragment();
    if (InsertionPoint)
      CurrHandledInstFragment->setAsInsertionPoint();
    CurrHandledInstFragment->setPaddingPoliciesMask(
        CurrHandledInstFragment->getPaddingPoliciesMask() | PoliciesMask);
  }
}

void MCCodePadder::handleInstructionEnd(const MCInst &Inst) {
  if (!OS)
    return; // instruction was emitted outside a function
  if (CurrHandledInstFragment == nullptr)
    return;

  MCFragment *InstFragment = OS->getCurrentFragment();
  if (MCDataFragment *InstDataFragment =
          dyn_cast_or_null<MCDataFragment>(InstFragment))
    // Inst is a fixed size instruction and was encoded into a MCDataFragment.
    // Let the fragment hold it and its size. Its size is the current size of
    // the data fragment, as the padding fragment was inserted right before it
    // and nothing was written yet except Inst
    CurrHandledInstFragment->setInstAndInstSize(
        Inst, InstDataFragment->getContents().size());
  else if (MCRelaxableFragment *InstRelaxableFragment =
               dyn_cast_or_null<MCRelaxableFragment>(InstFragment))
    // Inst may be relaxed and its size may vary.
    // Let the fragment hold the instruction and the MCRelaxableFragment
    // that's holding it.
    CurrHandledInstFragment->setInstAndInstFragment(Inst,
                                                    InstRelaxableFragment);
  else
    llvm_unreachable("After encoding an instruction current fragment must be "
                     "either a MCDataFragment or a MCRelaxableFragment");

  CurrHandledInstFragment = nullptr;
}

MCPFRange &MCCodePadder::getJurisdiction(MCPaddingFragment *Fragment,
                                         MCAsmLayout &Layout) {
  auto JurisdictionLocation = FragmentToJurisdiction.find(Fragment);
  if (JurisdictionLocation != FragmentToJurisdiction.end())
    return JurisdictionLocation->second;

  MCPFRange Jurisdiction;

  // Forward scanning the fragments in this section, starting from the given
  // fragments, and adding relevant MCPaddingFragments to the Jurisdiction
  for (MCFragment *CurrFragment = Fragment; CurrFragment != nullptr;
       CurrFragment = CurrFragment->getNextNode()) {

    MCPaddingFragment *CurrPaddingFragment =
        dyn_cast<MCPaddingFragment>(CurrFragment);
    if (CurrPaddingFragment == nullptr)
      continue;

    if (CurrPaddingFragment != Fragment &&
        CurrPaddingFragment->isInsertionPoint())
      // Found next insertion point Fragment. From now on it's its jurisdiction.
      break;
    for (const auto *Policy : CodePaddingPolicies) {
      if (CurrPaddingFragment->hasPaddingPolicy(Policy->getKindMask())) {
        Jurisdiction.push_back(CurrPaddingFragment);
        break;
      }
    }
  }

  auto InsertionResult =
      FragmentToJurisdiction.insert(std::make_pair(Fragment, Jurisdiction));
  assert(InsertionResult.second &&
         "Insertion to FragmentToJurisdiction failed");
  return InsertionResult.first->second;
}

uint64_t MCCodePadder::getMaxWindowSize(MCPaddingFragment *Fragment,
                                        MCAsmLayout &Layout) {
  auto MaxFragmentSizeLocation = FragmentToMaxWindowSize.find(Fragment);
  if (MaxFragmentSizeLocation != FragmentToMaxWindowSize.end())
    return MaxFragmentSizeLocation->second;

  MCPFRange &Jurisdiction = getJurisdiction(Fragment, Layout);
  uint64_t JurisdictionMask = MCPaddingFragment::PFK_None;
  for (const auto *Protege : Jurisdiction)
    JurisdictionMask |= Protege->getPaddingPoliciesMask();

  uint64_t MaxFragmentSize = UINT64_C(0);
  for (const auto *Policy : CodePaddingPolicies)
    if ((JurisdictionMask & Policy->getKindMask()) !=
        MCPaddingFragment::PFK_None)
      MaxFragmentSize = std::max(MaxFragmentSize, Policy->getWindowSize());

  auto InsertionResult =
      FragmentToMaxWindowSize.insert(std::make_pair(Fragment, MaxFragmentSize));
  assert(InsertionResult.second &&
         "Insertion to FragmentToMaxWindowSize failed");
  return InsertionResult.first->second;
}

bool MCCodePadder::relaxFragment(MCPaddingFragment *Fragment,
                                 MCAsmLayout &Layout) {
  if (!Fragment->isInsertionPoint())
    return false;
  uint64_t OldSize = Fragment->getSize();

  uint64_t MaxWindowSize = getMaxWindowSize(Fragment, Layout);
  if (MaxWindowSize == UINT64_C(0))
    return false;
  assert(isPowerOf2_64(MaxWindowSize) &&
         "MaxWindowSize must be an integer power of 2");
  uint64_t SectionAlignment = Fragment->getParent()->getAlignment();
  assert(isPowerOf2_64(SectionAlignment) &&
         "SectionAlignment must be an integer power of 2");

  MCPFRange &Jurisdiction = getJurisdiction(Fragment, Layout);
  uint64_t OptimalSize = UINT64_C(0);
  double OptimalWeight = std::numeric_limits<double>::max();
  uint64_t MaxFragmentSize = MaxWindowSize - UINT16_C(1);
  for (uint64_t Size = UINT64_C(0); Size <= MaxFragmentSize; ++Size) {
    Fragment->setSize(Size);
    Layout.invalidateFragmentsFrom(Fragment);
    double SizeWeight = 0.0;
    // The section is guaranteed to be aligned to SectionAlignment, but that
    // doesn't guarantee the exact section offset w.r.t. the policies window
    // size.
    // As a concrete example, the section could be aligned to 16B, but a
    // policy's window size can be 32B. That means that the section actual start
    // address can either be 0mod32 or 16mod32. The said policy will act
    // differently for each case, so we need to take both into consideration.
    for (uint64_t Offset = UINT64_C(0); Offset < MaxWindowSize;
         Offset += SectionAlignment) {
      double OffsetWeight = std::accumulate(
          CodePaddingPolicies.begin(), CodePaddingPolicies.end(), 0.0,
          [&Jurisdiction, &Offset, &Layout](
              double Weight, const MCCodePaddingPolicy *Policy) -> double {
            double PolicyWeight =
                Policy->computeRangePenaltyWeight(Jurisdiction, Offset, Layout);
            assert(PolicyWeight >= 0.0 && "A penalty weight must be positive");
            return Weight + PolicyWeight;
          });
      SizeWeight = std::max(SizeWeight, OffsetWeight);
    }
    if (SizeWeight < OptimalWeight) {
      OptimalWeight = SizeWeight;
      OptimalSize = Size;
    }
    if (OptimalWeight == 0.0)
      break;
  }

  Fragment->setSize(OptimalSize);
  Layout.invalidateFragmentsFrom(Fragment);
  return OldSize != OptimalSize;
}

//---------------------------------------------------------------------------
// MCCodePaddingPolicy
//

uint64_t MCCodePaddingPolicy::getNextFragmentOffset(const MCFragment *Fragment,
                                                    const MCAsmLayout &Layout) {
  assert(Fragment != nullptr && "Fragment cannot be null");
  MCFragment const *NextFragment = Fragment->getNextNode();
  return NextFragment == nullptr
             ? Layout.getSectionAddressSize(Fragment->getParent())
             : Layout.getFragmentOffset(NextFragment);
}

uint64_t
MCCodePaddingPolicy::getFragmentInstByte(const MCPaddingFragment *Fragment,
                                         MCAsmLayout &Layout) const {
  uint64_t InstByte = getNextFragmentOffset(Fragment, Layout);
  if (InstByteIsLastByte)
    InstByte += Fragment->getInstSize() - UINT64_C(1);
  return InstByte;
}

uint64_t
MCCodePaddingPolicy::computeWindowEndAddress(const MCPaddingFragment *Fragment,
                                             uint64_t Offset,
                                             MCAsmLayout &Layout) const {
  uint64_t InstByte = getFragmentInstByte(Fragment, Layout);
  return alignTo(InstByte + UINT64_C(1) + Offset, WindowSize) - Offset;
}

double MCCodePaddingPolicy::computeRangePenaltyWeight(
    const MCPFRange &Range, uint64_t Offset, MCAsmLayout &Layout) const {

  SmallVector<MCPFRange, 8> Windows;
  SmallVector<MCPFRange, 8>::iterator CurrWindowLocation = Windows.end();
  for (const MCPaddingFragment *Fragment : Range) {
    if (!Fragment->hasPaddingPolicy(getKindMask()))
      continue;
    uint64_t FragmentWindowEndAddress =
        computeWindowEndAddress(Fragment, Offset, Layout);
    if (CurrWindowLocation == Windows.end() ||
        FragmentWindowEndAddress !=
            computeWindowEndAddress(*CurrWindowLocation->begin(), Offset,
                                    Layout)) {
      // next window is starting
      Windows.push_back(MCPFRange());
      CurrWindowLocation = Windows.end() - 1;
    }
    CurrWindowLocation->push_back(Fragment);
  }

  if (Windows.empty())
    return 0.0;

  double RangeWeight = 0.0;
  SmallVector<MCPFRange, 8>::iterator I = Windows.begin();
  RangeWeight += computeFirstWindowPenaltyWeight(*I, Offset, Layout);
  ++I;
  RangeWeight += std::accumulate(
      I, Windows.end(), 0.0,
      [this, &Layout, &Offset](double Weight, MCPFRange &Window) -> double {
        return Weight += computeWindowPenaltyWeight(Window, Offset, Layout);
      });
  return RangeWeight;
}

double MCCodePaddingPolicy::computeFirstWindowPenaltyWeight(
    const MCPFRange &Window, uint64_t Offset, MCAsmLayout &Layout) const {
  if (Window.empty())
    return 0.0;
  uint64_t WindowEndAddress =
      computeWindowEndAddress(*Window.begin(), Offset, Layout);

  MCPFRange FullWindowFirstPart; // will hold all the fragments that are in the
								 // same window as the fragments in the given
								 // window but their penalty weight should not
								 // be added
  for (const MCFragment *Fragment = (*Window.begin())->getPrevNode();
       Fragment != nullptr; Fragment = Fragment->getPrevNode()) {
    const MCPaddingFragment *PaddingNopFragment =
        dyn_cast<MCPaddingFragment>(Fragment);
    if (PaddingNopFragment == nullptr ||
        !PaddingNopFragment->hasPaddingPolicy(getKindMask()))
      continue;
    if (WindowEndAddress !=
        computeWindowEndAddress(PaddingNopFragment, Offset, Layout))
      break;

    FullWindowFirstPart.push_back(PaddingNopFragment);
  }

  std::reverse(FullWindowFirstPart.begin(), FullWindowFirstPart.end());
  double FullWindowFirstPartWeight =
      computeWindowPenaltyWeight(FullWindowFirstPart, Offset, Layout);

  MCPFRange FullWindow(
      FullWindowFirstPart); // will hold all the fragments that are in the
                            // same window as the fragments in the given
                            // window, whether their weight should be added
                            // or not
  FullWindow.append(Window.begin(), Window.end());
  double FullWindowWeight =
      computeWindowPenaltyWeight(FullWindow, Offset, Layout);

  assert(FullWindowWeight >= FullWindowFirstPartWeight &&
         "More fragments necessarily means bigger weight");
  return FullWindowWeight - FullWindowFirstPartWeight;
}
