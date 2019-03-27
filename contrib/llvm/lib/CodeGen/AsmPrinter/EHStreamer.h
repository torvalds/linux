//===- EHStreamer.h - Exception Handling Directive Streamer -----*- C++ -*-===//
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

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_EHSTREAMER_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_EHSTREAMER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/AsmPrinterHandler.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class AsmPrinter;
struct LandingPadInfo;
class MachineInstr;
class MachineModuleInfo;
class MCSymbol;
template <typename T> class SmallVectorImpl;

/// Emits exception handling directives.
class LLVM_LIBRARY_VISIBILITY EHStreamer : public AsmPrinterHandler {
protected:
  /// Target of directive emission.
  AsmPrinter *Asm;

  /// Collected machine module information.
  MachineModuleInfo *MMI;

  /// How many leading type ids two landing pads have in common.
  static unsigned sharedTypeIDs(const LandingPadInfo *L,
                                const LandingPadInfo *R);

  /// Structure holding a try-range and the associated landing pad.
  struct PadRange {
    // The index of the landing pad.
    unsigned PadIndex;

    // The index of the begin and end labels in the landing pad's label lists.
    unsigned RangeIndex;
  };

  using RangeMapType = DenseMap<MCSymbol *, PadRange>;

  /// Structure describing an entry in the actions table.
  struct ActionEntry {
    int ValueForTypeID; // The value to write - may not be equal to the type id.
    int NextAction;
    unsigned Previous;
  };

  /// Structure describing an entry in the call-site table.
  struct CallSiteEntry {
    // The 'try-range' is BeginLabel .. EndLabel.
    MCSymbol *BeginLabel; // Null indicates the start of the function.
    MCSymbol *EndLabel;   // Null indicates the end of the function.

    // LPad contains the landing pad start labels.
    const LandingPadInfo *LPad; // Null indicates that there is no landing pad.

    unsigned Action;
  };

  /// Compute the actions table and gather the first action index for each
  /// landing pad site.
  void computeActionsTable(const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
                           SmallVectorImpl<ActionEntry> &Actions,
                           SmallVectorImpl<unsigned> &FirstActions);

  void computePadMap(const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
                     RangeMapType &PadMap);

  /// Compute the call-site table.  The entry for an invoke has a try-range
  /// containing the call, a non-zero landing pad and an appropriate action.
  /// The entry for an ordinary call has a try-range containing the call and
  /// zero for the landing pad and the action.  Calls marked 'nounwind' have
  /// no entry and must not be contained in the try-range of any entry - they
  /// form gaps in the table.  Entries must be ordered by try-range address.
  virtual void computeCallSiteTable(
      SmallVectorImpl<CallSiteEntry> &CallSites,
      const SmallVectorImpl<const LandingPadInfo *> &LandingPads,
      const SmallVectorImpl<unsigned> &FirstActions);

  /// Emit landing pads and actions.
  ///
  /// The general organization of the table is complex, but the basic concepts
  /// are easy.  First there is a header which describes the location and
  /// organization of the three components that follow.
  ///  1. The landing pad site information describes the range of code covered
  ///     by the try.  In our case it's an accumulation of the ranges covered
  ///     by the invokes in the try.  There is also a reference to the landing
  ///     pad that handles the exception once processed.  Finally an index into
  ///     the actions table.
  ///  2. The action table, in our case, is composed of pairs of type ids
  ///     and next action offset.  Starting with the action index from the
  ///     landing pad site, each type Id is checked for a match to the current
  ///     exception.  If it matches then the exception and type id are passed
  ///     on to the landing pad.  Otherwise the next action is looked up.  This
  ///     chain is terminated with a next action of zero.  If no type id is
  ///     found the frame is unwound and handling continues.
  ///  3. Type id table contains references to all the C++ typeinfo for all
  ///     catches in the function.  This tables is reversed indexed base 1.
  ///
  /// Returns the starting symbol of an exception table.
  MCSymbol *emitExceptionTable();

  virtual void emitTypeInfos(unsigned TTypeEncoding, MCSymbol *TTBaseLabel);

  // Helpers for identifying what kind of clause an EH typeid or selector
  // corresponds to. Negative selectors are for filter clauses, the zero
  // selector is for cleanups, and positive selectors are for catch clauses.
  static bool isFilterEHSelector(int Selector) { return Selector < 0; }
  static bool isCleanupEHSelector(int Selector) { return Selector == 0; }
  static bool isCatchEHSelector(int Selector) { return Selector > 0; }

public:
  EHStreamer(AsmPrinter *A);
  ~EHStreamer() override;

  // Unused.
  void setSymbolSize(const MCSymbol *Sym, uint64_t Size) override {}
  void beginInstruction(const MachineInstr *MI) override {}
  void endInstruction() override {}

  /// Return `true' if this is a call to a function marked `nounwind'. Return
  /// `false' otherwise.
  static bool callToNoUnwindFunction(const MachineInstr *MI);
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_ASMPRINTER_EHSTREAMER_H
