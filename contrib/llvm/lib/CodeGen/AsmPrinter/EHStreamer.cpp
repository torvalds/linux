//===- CodeGen/AsmPrinter/EHStreamer.cpp - Exception Directive Streamer ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing exception info into assembly files.
//
//===----------------------------------------------------------------------===//

#include "EHStreamer.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;

EHStreamer::EHStreamer(AsmPrinter *A) : Asm(A), MMI(Asm->MMI) {}

EHStreamer::~EHStreamer() = default;

/// How many leading type ids two landing pads have in common.
unsigned EHStreamer::sharedTypeIDs(const LandingPadInfo *L,
                                   const LandingPadInfo *R) {
  const std::vector<int> &LIds = L->TypeIds, &RIds = R->TypeIds;
  unsigned LSize = LIds.size(), RSize = RIds.size();
  unsigned MinSize = LSize < RSize ? LSize : RSize;
  unsigned Count = 0;

  for (; Count != MinSize; ++Count)
    if (LIds[Count] != RIds[Count])
      return Count;

  return Count;
}

/// Compute the actions table and gather the first action index for each landing
/// pad site.
void EHStreamer::computeActionsTable(
    const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
    SmallVectorImpl<ActionEntry> &Actions,
    SmallVectorImpl<unsigned> &FirstActions) {
  // The action table follows the call-site table in the LSDA. The individual
  // records are of two types:
  //
  //   * Catch clause
  //   * Exception specification
  //
  // The two record kinds have the same format, with only small differences.
  // They are distinguished by the "switch value" field: Catch clauses
  // (TypeInfos) have strictly positive switch values, and exception
  // specifications (FilterIds) have strictly negative switch values. Value 0
  // indicates a catch-all clause.
  //
  // Negative type IDs index into FilterIds. Positive type IDs index into
  // TypeInfos.  The value written for a positive type ID is just the type ID
  // itself.  For a negative type ID, however, the value written is the
  // (negative) byte offset of the corresponding FilterIds entry.  The byte
  // offset is usually equal to the type ID (because the FilterIds entries are
  // written using a variable width encoding, which outputs one byte per entry
  // as long as the value written is not too large) but can differ.  This kind
  // of complication does not occur for positive type IDs because type infos are
  // output using a fixed width encoding.  FilterOffsets[i] holds the byte
  // offset corresponding to FilterIds[i].

  const std::vector<unsigned> &FilterIds = Asm->MF->getFilterIds();
  SmallVector<int, 16> FilterOffsets;
  FilterOffsets.reserve(FilterIds.size());
  int Offset = -1;

  for (std::vector<unsigned>::const_iterator
         I = FilterIds.begin(), E = FilterIds.end(); I != E; ++I) {
    FilterOffsets.push_back(Offset);
    Offset -= getULEB128Size(*I);
  }

  FirstActions.reserve(LandingPads.size());

  int FirstAction = 0;
  unsigned SizeActions = 0; // Total size of all action entries for a function
  const LandingPadInfo *PrevLPI = nullptr;

  for (SmallVectorImpl<const LandingPadInfo *>::const_iterator
         I = LandingPads.begin(), E = LandingPads.end(); I != E; ++I) {
    const LandingPadInfo *LPI = *I;
    const std::vector<int> &TypeIds = LPI->TypeIds;
    unsigned NumShared = PrevLPI ? sharedTypeIDs(LPI, PrevLPI) : 0;
    unsigned SizeSiteActions = 0; // Total size of all entries for a landingpad

    if (NumShared < TypeIds.size()) {
      // Size of one action entry (typeid + next action)
      unsigned SizeActionEntry = 0;
      unsigned PrevAction = (unsigned)-1;

      if (NumShared) {
        unsigned SizePrevIds = PrevLPI->TypeIds.size();
        assert(Actions.size());
        PrevAction = Actions.size() - 1;
        SizeActionEntry = getSLEB128Size(Actions[PrevAction].NextAction) +
                          getSLEB128Size(Actions[PrevAction].ValueForTypeID);

        for (unsigned j = NumShared; j != SizePrevIds; ++j) {
          assert(PrevAction != (unsigned)-1 && "PrevAction is invalid!");
          SizeActionEntry -= getSLEB128Size(Actions[PrevAction].ValueForTypeID);
          SizeActionEntry += -Actions[PrevAction].NextAction;
          PrevAction = Actions[PrevAction].Previous;
        }
      }

      // Compute the actions.
      for (unsigned J = NumShared, M = TypeIds.size(); J != M; ++J) {
        int TypeID = TypeIds[J];
        assert(-1 - TypeID < (int)FilterOffsets.size() && "Unknown filter id!");
        int ValueForTypeID =
            isFilterEHSelector(TypeID) ? FilterOffsets[-1 - TypeID] : TypeID;
        unsigned SizeTypeID = getSLEB128Size(ValueForTypeID);

        int NextAction = SizeActionEntry ? -(SizeActionEntry + SizeTypeID) : 0;
        SizeActionEntry = SizeTypeID + getSLEB128Size(NextAction);
        SizeSiteActions += SizeActionEntry;

        ActionEntry Action = { ValueForTypeID, NextAction, PrevAction };
        Actions.push_back(Action);
        PrevAction = Actions.size() - 1;
      }

      // Record the first action of the landing pad site.
      FirstAction = SizeActions + SizeSiteActions - SizeActionEntry + 1;
    } // else identical - re-use previous FirstAction

    // Information used when creating the call-site table. The action record
    // field of the call site record is the offset of the first associated
    // action record, relative to the start of the actions table. This value is
    // biased by 1 (1 indicating the start of the actions table), and 0
    // indicates that there are no actions.
    FirstActions.push_back(FirstAction);

    // Compute this sites contribution to size.
    SizeActions += SizeSiteActions;

    PrevLPI = LPI;
  }
}

/// Return `true' if this is a call to a function marked `nounwind'. Return
/// `false' otherwise.
bool EHStreamer::callToNoUnwindFunction(const MachineInstr *MI) {
  assert(MI->isCall() && "This should be a call instruction!");

  bool MarkedNoUnwind = false;
  bool SawFunc = false;

  for (unsigned I = 0, E = MI->getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI->getOperand(I);

    if (!MO.isGlobal()) continue;

    const Function *F = dyn_cast<Function>(MO.getGlobal());
    if (!F) continue;

    if (SawFunc) {
      // Be conservative. If we have more than one function operand for this
      // call, then we can't make the assumption that it's the callee and
      // not a parameter to the call.
      //
      // FIXME: Determine if there's a way to say that `F' is the callee or
      // parameter.
      MarkedNoUnwind = false;
      break;
    }

    MarkedNoUnwind = F->doesNotThrow();
    SawFunc = true;
  }

  return MarkedNoUnwind;
}

void EHStreamer::computePadMap(
    const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
    RangeMapType &PadMap) {
  // Invokes and nounwind calls have entries in PadMap (due to being bracketed
  // by try-range labels when lowered).  Ordinary calls do not, so appropriate
  // try-ranges for them need be deduced so we can put them in the LSDA.
  for (unsigned i = 0, N = LandingPads.size(); i != N; ++i) {
    const LandingPadInfo *LandingPad = LandingPads[i];
    for (unsigned j = 0, E = LandingPad->BeginLabels.size(); j != E; ++j) {
      MCSymbol *BeginLabel = LandingPad->BeginLabels[j];
      assert(!PadMap.count(BeginLabel) && "Duplicate landing pad labels!");
      PadRange P = { i, j };
      PadMap[BeginLabel] = P;
    }
  }
}

/// Compute the call-site table.  The entry for an invoke has a try-range
/// containing the call, a non-zero landing pad, and an appropriate action.  The
/// entry for an ordinary call has a try-range containing the call and zero for
/// the landing pad and the action.  Calls marked 'nounwind' have no entry and
/// must not be contained in the try-range of any entry - they form gaps in the
/// table.  Entries must be ordered by try-range address.
void EHStreamer::
computeCallSiteTable(SmallVectorImpl<CallSiteEntry> &CallSites,
                     const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
                     const SmallVectorImpl<unsigned> &FirstActions) {
  RangeMapType PadMap;
  computePadMap(LandingPads, PadMap);

  // The end label of the previous invoke or nounwind try-range.
  MCSymbol *LastLabel = nullptr;

  // Whether there is a potentially throwing instruction (currently this means
  // an ordinary call) between the end of the previous try-range and now.
  bool SawPotentiallyThrowing = false;

  // Whether the last CallSite entry was for an invoke.
  bool PreviousIsInvoke = false;

  bool IsSJLJ = Asm->MAI->getExceptionHandlingType() == ExceptionHandling::SjLj;

  // Visit all instructions in order of address.
  for (const auto &MBB : *Asm->MF) {
    for (const auto &MI : MBB) {
      if (!MI.isEHLabel()) {
        if (MI.isCall())
          SawPotentiallyThrowing |= !callToNoUnwindFunction(&MI);
        continue;
      }

      // End of the previous try-range?
      MCSymbol *BeginLabel = MI.getOperand(0).getMCSymbol();
      if (BeginLabel == LastLabel)
        SawPotentiallyThrowing = false;

      // Beginning of a new try-range?
      RangeMapType::const_iterator L = PadMap.find(BeginLabel);
      if (L == PadMap.end())
        // Nope, it was just some random label.
        continue;

      const PadRange &P = L->second;
      const LandingPadInfo *LandingPad = LandingPads[P.PadIndex];
      assert(BeginLabel == LandingPad->BeginLabels[P.RangeIndex] &&
             "Inconsistent landing pad map!");

      // For Dwarf exception handling (SjLj handling doesn't use this). If some
      // instruction between the previous try-range and this one may throw,
      // create a call-site entry with no landing pad for the region between the
      // try-ranges.
      if (SawPotentiallyThrowing && Asm->MAI->usesCFIForEH()) {
        CallSiteEntry Site = { LastLabel, BeginLabel, nullptr, 0 };
        CallSites.push_back(Site);
        PreviousIsInvoke = false;
      }

      LastLabel = LandingPad->EndLabels[P.RangeIndex];
      assert(BeginLabel && LastLabel && "Invalid landing pad!");

      if (!LandingPad->LandingPadLabel) {
        // Create a gap.
        PreviousIsInvoke = false;
      } else {
        // This try-range is for an invoke.
        CallSiteEntry Site = {
          BeginLabel,
          LastLabel,
          LandingPad,
          FirstActions[P.PadIndex]
        };

        // Try to merge with the previous call-site. SJLJ doesn't do this
        if (PreviousIsInvoke && !IsSJLJ) {
          CallSiteEntry &Prev = CallSites.back();
          if (Site.LPad == Prev.LPad && Site.Action == Prev.Action) {
            // Extend the range of the previous entry.
            Prev.EndLabel = Site.EndLabel;
            continue;
          }
        }

        // Otherwise, create a new call-site.
        if (!IsSJLJ)
          CallSites.push_back(Site);
        else {
          // SjLj EH must maintain the call sites in the order assigned
          // to them by the SjLjPrepare pass.
          unsigned SiteNo = Asm->MF->getCallSiteBeginLabel(BeginLabel);
          if (CallSites.size() < SiteNo)
            CallSites.resize(SiteNo);
          CallSites[SiteNo - 1] = Site;
        }
        PreviousIsInvoke = true;
      }
    }
  }

  // If some instruction between the previous try-range and the end of the
  // function may throw, create a call-site entry with no landing pad for the
  // region following the try-range.
  if (SawPotentiallyThrowing && !IsSJLJ) {
    CallSiteEntry Site = { LastLabel, nullptr, nullptr, 0 };
    CallSites.push_back(Site);
  }
}

/// Emit landing pads and actions.
///
/// The general organization of the table is complex, but the basic concepts are
/// easy.  First there is a header which describes the location and organization
/// of the three components that follow.
///
///  1. The landing pad site information describes the range of code covered by
///     the try.  In our case it's an accumulation of the ranges covered by the
///     invokes in the try.  There is also a reference to the landing pad that
///     handles the exception once processed.  Finally an index into the actions
///     table.
///  2. The action table, in our case, is composed of pairs of type IDs and next
///     action offset.  Starting with the action index from the landing pad
///     site, each type ID is checked for a match to the current exception.  If
///     it matches then the exception and type id are passed on to the landing
///     pad.  Otherwise the next action is looked up.  This chain is terminated
///     with a next action of zero.  If no type id is found then the frame is
///     unwound and handling continues.
///  3. Type ID table contains references to all the C++ typeinfo for all
///     catches in the function.  This tables is reverse indexed base 1.
///
/// Returns the starting symbol of an exception table.
MCSymbol *EHStreamer::emitExceptionTable() {
  const MachineFunction *MF = Asm->MF;
  const std::vector<const GlobalValue *> &TypeInfos = MF->getTypeInfos();
  const std::vector<unsigned> &FilterIds = MF->getFilterIds();
  const std::vector<LandingPadInfo> &PadInfos = MF->getLandingPads();

  // Sort the landing pads in order of their type ids.  This is used to fold
  // duplicate actions.
  SmallVector<const LandingPadInfo *, 64> LandingPads;
  LandingPads.reserve(PadInfos.size());

  for (unsigned i = 0, N = PadInfos.size(); i != N; ++i)
    LandingPads.push_back(&PadInfos[i]);

  // Order landing pads lexicographically by type id.
  llvm::sort(LandingPads, [](const LandingPadInfo *L, const LandingPadInfo *R) {
    return L->TypeIds < R->TypeIds;
  });

  // Compute the actions table and gather the first action index for each
  // landing pad site.
  SmallVector<ActionEntry, 32> Actions;
  SmallVector<unsigned, 64> FirstActions;
  computeActionsTable(LandingPads, Actions, FirstActions);

  // Compute the call-site table.
  SmallVector<CallSiteEntry, 64> CallSites;
  computeCallSiteTable(CallSites, LandingPads, FirstActions);

  bool IsSJLJ = Asm->MAI->getExceptionHandlingType() == ExceptionHandling::SjLj;
  bool IsWasm = Asm->MAI->getExceptionHandlingType() == ExceptionHandling::Wasm;
  unsigned CallSiteEncoding =
      IsSJLJ ? dwarf::DW_EH_PE_udata4 : dwarf::DW_EH_PE_uleb128;
  bool HaveTTData = !TypeInfos.empty() || !FilterIds.empty();

  // Type infos.
  MCSection *LSDASection = Asm->getObjFileLowering().getLSDASection();
  unsigned TTypeEncoding;

  if (!HaveTTData) {
    // If there is no TypeInfo, then we just explicitly say that we're omitting
    // that bit.
    TTypeEncoding = dwarf::DW_EH_PE_omit;
  } else {
    // Okay, we have actual filters or typeinfos to emit.  As such, we need to
    // pick a type encoding for them.  We're about to emit a list of pointers to
    // typeinfo objects at the end of the LSDA.  However, unless we're in static
    // mode, this reference will require a relocation by the dynamic linker.
    //
    // Because of this, we have a couple of options:
    //
    //   1) If we are in -static mode, we can always use an absolute reference
    //      from the LSDA, because the static linker will resolve it.
    //
    //   2) Otherwise, if the LSDA section is writable, we can output the direct
    //      reference to the typeinfo and allow the dynamic linker to relocate
    //      it.  Since it is in a writable section, the dynamic linker won't
    //      have a problem.
    //
    //   3) Finally, if we're in PIC mode and the LDSA section isn't writable,
    //      we need to use some form of indirection.  For example, on Darwin,
    //      we can output a statically-relocatable reference to a dyld stub. The
    //      offset to the stub is constant, but the contents are in a section
    //      that is updated by the dynamic linker.  This is easy enough, but we
    //      need to tell the personality function of the unwinder to indirect
    //      through the dyld stub.
    //
    // FIXME: When (3) is actually implemented, we'll have to emit the stubs
    // somewhere.  This predicate should be moved to a shared location that is
    // in target-independent code.
    //
    TTypeEncoding = Asm->getObjFileLowering().getTTypeEncoding();
  }

  // Begin the exception table.
  // Sometimes we want not to emit the data into separate section (e.g. ARM
  // EHABI). In this case LSDASection will be NULL.
  if (LSDASection)
    Asm->OutStreamer->SwitchSection(LSDASection);
  Asm->EmitAlignment(2);

  // Emit the LSDA.
  MCSymbol *GCCETSym =
    Asm->OutContext.getOrCreateSymbol(Twine("GCC_except_table")+
                                      Twine(Asm->getFunctionNumber()));
  Asm->OutStreamer->EmitLabel(GCCETSym);
  Asm->OutStreamer->EmitLabel(Asm->getCurExceptionSym());

  // Emit the LSDA header.
  Asm->EmitEncodingByte(dwarf::DW_EH_PE_omit, "@LPStart");
  Asm->EmitEncodingByte(TTypeEncoding, "@TType");

  MCSymbol *TTBaseLabel = nullptr;
  if (HaveTTData) {
    // N.B.: There is a dependency loop between the size of the TTBase uleb128
    // here and the amount of padding before the aligned type table. The
    // assembler must sometimes pad this uleb128 or insert extra padding before
    // the type table. See PR35809 or GNU as bug 4029.
    MCSymbol *TTBaseRefLabel = Asm->createTempSymbol("ttbaseref");
    TTBaseLabel = Asm->createTempSymbol("ttbase");
    Asm->EmitLabelDifferenceAsULEB128(TTBaseLabel, TTBaseRefLabel);
    Asm->OutStreamer->EmitLabel(TTBaseRefLabel);
  }

  bool VerboseAsm = Asm->OutStreamer->isVerboseAsm();

  // Emit the landing pad call site table.
  MCSymbol *CstBeginLabel = Asm->createTempSymbol("cst_begin");
  MCSymbol *CstEndLabel = Asm->createTempSymbol("cst_end");
  Asm->EmitEncodingByte(CallSiteEncoding, "Call site");
  Asm->EmitLabelDifferenceAsULEB128(CstEndLabel, CstBeginLabel);
  Asm->OutStreamer->EmitLabel(CstBeginLabel);

  // SjLj / Wasm Exception handling
  if (IsSJLJ || IsWasm) {
    unsigned idx = 0;
    for (SmallVectorImpl<CallSiteEntry>::const_iterator
         I = CallSites.begin(), E = CallSites.end(); I != E; ++I, ++idx) {
      const CallSiteEntry &S = *I;

      // Index of the call site entry.
      if (VerboseAsm) {
        Asm->OutStreamer->AddComment(">> Call Site " + Twine(idx) + " <<");
        Asm->OutStreamer->AddComment("  On exception at call site "+Twine(idx));
      }
      Asm->EmitULEB128(idx);

      // Offset of the first associated action record, relative to the start of
      // the action table. This value is biased by 1 (1 indicates the start of
      // the action table), and 0 indicates that there are no actions.
      if (VerboseAsm) {
        if (S.Action == 0)
          Asm->OutStreamer->AddComment("  Action: cleanup");
        else
          Asm->OutStreamer->AddComment("  Action: " +
                                       Twine((S.Action - 1) / 2 + 1));
      }
      Asm->EmitULEB128(S.Action);
    }
  } else {
    // Itanium LSDA exception handling

    // The call-site table is a list of all call sites that may throw an
    // exception (including C++ 'throw' statements) in the procedure
    // fragment. It immediately follows the LSDA header. Each entry indicates,
    // for a given call, the first corresponding action record and corresponding
    // landing pad.
    //
    // The table begins with the number of bytes, stored as an LEB128
    // compressed, unsigned integer. The records immediately follow the record
    // count. They are sorted in increasing call-site address. Each record
    // indicates:
    //
    //   * The position of the call-site.
    //   * The position of the landing pad.
    //   * The first action record for that call site.
    //
    // A missing entry in the call-site table indicates that a call is not
    // supposed to throw.

    unsigned Entry = 0;
    for (SmallVectorImpl<CallSiteEntry>::const_iterator
         I = CallSites.begin(), E = CallSites.end(); I != E; ++I) {
      const CallSiteEntry &S = *I;

      MCSymbol *EHFuncBeginSym = Asm->getFunctionBegin();

      MCSymbol *BeginLabel = S.BeginLabel;
      if (!BeginLabel)
        BeginLabel = EHFuncBeginSym;
      MCSymbol *EndLabel = S.EndLabel;
      if (!EndLabel)
        EndLabel = Asm->getFunctionEnd();

      // Offset of the call site relative to the start of the procedure.
      if (VerboseAsm)
        Asm->OutStreamer->AddComment(">> Call Site " + Twine(++Entry) + " <<");
      Asm->EmitLabelDifferenceAsULEB128(BeginLabel, EHFuncBeginSym);
      if (VerboseAsm)
        Asm->OutStreamer->AddComment(Twine("  Call between ") +
                                     BeginLabel->getName() + " and " +
                                     EndLabel->getName());
      Asm->EmitLabelDifferenceAsULEB128(EndLabel, BeginLabel);

      // Offset of the landing pad relative to the start of the procedure.
      if (!S.LPad) {
        if (VerboseAsm)
          Asm->OutStreamer->AddComment("    has no landing pad");
        Asm->EmitULEB128(0);
      } else {
        if (VerboseAsm)
          Asm->OutStreamer->AddComment(Twine("    jumps to ") +
                                       S.LPad->LandingPadLabel->getName());
        Asm->EmitLabelDifferenceAsULEB128(S.LPad->LandingPadLabel,
                                          EHFuncBeginSym);
      }

      // Offset of the first associated action record, relative to the start of
      // the action table. This value is biased by 1 (1 indicates the start of
      // the action table), and 0 indicates that there are no actions.
      if (VerboseAsm) {
        if (S.Action == 0)
          Asm->OutStreamer->AddComment("  On action: cleanup");
        else
          Asm->OutStreamer->AddComment("  On action: " +
                                       Twine((S.Action - 1) / 2 + 1));
      }
      Asm->EmitULEB128(S.Action);
    }
  }
  Asm->OutStreamer->EmitLabel(CstEndLabel);

  // Emit the Action Table.
  int Entry = 0;
  for (SmallVectorImpl<ActionEntry>::const_iterator
         I = Actions.begin(), E = Actions.end(); I != E; ++I) {
    const ActionEntry &Action = *I;

    if (VerboseAsm) {
      // Emit comments that decode the action table.
      Asm->OutStreamer->AddComment(">> Action Record " + Twine(++Entry) + " <<");
    }

    // Type Filter
    //
    //   Used by the runtime to match the type of the thrown exception to the
    //   type of the catch clauses or the types in the exception specification.
    if (VerboseAsm) {
      if (Action.ValueForTypeID > 0)
        Asm->OutStreamer->AddComment("  Catch TypeInfo " +
                                     Twine(Action.ValueForTypeID));
      else if (Action.ValueForTypeID < 0)
        Asm->OutStreamer->AddComment("  Filter TypeInfo " +
                                     Twine(Action.ValueForTypeID));
      else
        Asm->OutStreamer->AddComment("  Cleanup");
    }
    Asm->EmitSLEB128(Action.ValueForTypeID);

    // Action Record
    //
    //   Self-relative signed displacement in bytes of the next action record,
    //   or 0 if there is no next action record.
    if (VerboseAsm) {
      if (Action.NextAction == 0) {
        Asm->OutStreamer->AddComment("  No further actions");
      } else {
        unsigned NextAction = Entry + (Action.NextAction + 1) / 2;
        Asm->OutStreamer->AddComment("  Continue to action "+Twine(NextAction));
      }
    }
    Asm->EmitSLEB128(Action.NextAction);
  }

  if (HaveTTData) {
    Asm->EmitAlignment(2);
    emitTypeInfos(TTypeEncoding, TTBaseLabel);
  }

  Asm->EmitAlignment(2);
  return GCCETSym;
}

void EHStreamer::emitTypeInfos(unsigned TTypeEncoding, MCSymbol *TTBaseLabel) {
  const MachineFunction *MF = Asm->MF;
  const std::vector<const GlobalValue *> &TypeInfos = MF->getTypeInfos();
  const std::vector<unsigned> &FilterIds = MF->getFilterIds();

  bool VerboseAsm = Asm->OutStreamer->isVerboseAsm();

  int Entry = 0;
  // Emit the Catch TypeInfos.
  if (VerboseAsm && !TypeInfos.empty()) {
    Asm->OutStreamer->AddComment(">> Catch TypeInfos <<");
    Asm->OutStreamer->AddBlankLine();
    Entry = TypeInfos.size();
  }

  for (const GlobalValue *GV : make_range(TypeInfos.rbegin(),
                                          TypeInfos.rend())) {
    if (VerboseAsm)
      Asm->OutStreamer->AddComment("TypeInfo " + Twine(Entry--));
    Asm->EmitTTypeReference(GV, TTypeEncoding);
  }

  Asm->OutStreamer->EmitLabel(TTBaseLabel);

  // Emit the Exception Specifications.
  if (VerboseAsm && !FilterIds.empty()) {
    Asm->OutStreamer->AddComment(">> Filter TypeInfos <<");
    Asm->OutStreamer->AddBlankLine();
    Entry = 0;
  }
  for (std::vector<unsigned>::const_iterator
         I = FilterIds.begin(), E = FilterIds.end(); I < E; ++I) {
    unsigned TypeID = *I;
    if (VerboseAsm) {
      --Entry;
      if (isFilterEHSelector(TypeID))
        Asm->OutStreamer->AddComment("FilterInfo " + Twine(Entry));
    }

    Asm->EmitULEB128(TypeID);
  }
}
