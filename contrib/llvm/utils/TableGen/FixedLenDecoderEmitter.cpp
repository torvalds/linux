//===------------ FixedLenDecoderEmitter.cpp - Decoder Generator ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// It contains the tablegen backend that emits the decoder functions for
// targets with fixed length instruction set.
//
//===----------------------------------------------------------------------===//

#include "CodeGenInstruction.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCFixedLenDisassembler.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "decoder-emitter"

namespace {

struct EncodingField {
  unsigned Base, Width, Offset;
  EncodingField(unsigned B, unsigned W, unsigned O)
    : Base(B), Width(W), Offset(O) { }
};

struct OperandInfo {
  std::vector<EncodingField> Fields;
  std::string Decoder;
  bool HasCompleteDecoder;

  OperandInfo(std::string D, bool HCD)
      : Decoder(std::move(D)), HasCompleteDecoder(HCD) {}

  void addField(unsigned Base, unsigned Width, unsigned Offset) {
    Fields.push_back(EncodingField(Base, Width, Offset));
  }

  unsigned numFields() const { return Fields.size(); }

  typedef std::vector<EncodingField>::const_iterator const_iterator;

  const_iterator begin() const { return Fields.begin(); }
  const_iterator end() const   { return Fields.end();   }
};

typedef std::vector<uint8_t> DecoderTable;
typedef uint32_t DecoderFixup;
typedef std::vector<DecoderFixup> FixupList;
typedef std::vector<FixupList> FixupScopeList;
typedef SmallSetVector<CachedHashString, 16> PredicateSet;
typedef SmallSetVector<CachedHashString, 16> DecoderSet;
struct DecoderTableInfo {
  DecoderTable Table;
  FixupScopeList FixupStack;
  PredicateSet Predicates;
  DecoderSet Decoders;
};

struct EncodingAndInst {
  const Record *EncodingDef;
  const CodeGenInstruction *Inst;

  EncodingAndInst(const Record *EncodingDef, const CodeGenInstruction *Inst)
      : EncodingDef(EncodingDef), Inst(Inst) {}
};

raw_ostream &operator<<(raw_ostream &OS, const EncodingAndInst &Value) {
  if (Value.EncodingDef != Value.Inst->TheDef)
    OS << Value.EncodingDef->getName() << ":";
  OS << Value.Inst->TheDef->getName();
  return OS;
}

class FixedLenDecoderEmitter {
  std::vector<EncodingAndInst> NumberedEncodings;

public:
  // Defaults preserved here for documentation, even though they aren't
  // strictly necessary given the way that this is currently being called.
  FixedLenDecoderEmitter(RecordKeeper &R, std::string PredicateNamespace,
                         std::string GPrefix = "if (",
                         std::string GPostfix = " == MCDisassembler::Fail)",
                         std::string ROK = "MCDisassembler::Success",
                         std::string RFail = "MCDisassembler::Fail",
                         std::string L = "")
      : Target(R), PredicateNamespace(std::move(PredicateNamespace)),
        GuardPrefix(std::move(GPrefix)), GuardPostfix(std::move(GPostfix)),
        ReturnOK(std::move(ROK)), ReturnFail(std::move(RFail)),
        Locals(std::move(L)) {}

  // Emit the decoder state machine table.
  void emitTable(formatted_raw_ostream &o, DecoderTable &Table,
                 unsigned Indentation, unsigned BitWidth,
                 StringRef Namespace) const;
  void emitPredicateFunction(formatted_raw_ostream &OS,
                             PredicateSet &Predicates,
                             unsigned Indentation) const;
  void emitDecoderFunction(formatted_raw_ostream &OS,
                           DecoderSet &Decoders,
                           unsigned Indentation) const;

  // run - Output the code emitter
  void run(raw_ostream &o);

private:
  CodeGenTarget Target;

public:
  std::string PredicateNamespace;
  std::string GuardPrefix, GuardPostfix;
  std::string ReturnOK, ReturnFail;
  std::string Locals;
};

} // end anonymous namespace

// The set (BIT_TRUE, BIT_FALSE, BIT_UNSET) represents a ternary logic system
// for a bit value.
//
// BIT_UNFILTERED is used as the init value for a filter position.  It is used
// only for filter processings.
typedef enum {
  BIT_TRUE,      // '1'
  BIT_FALSE,     // '0'
  BIT_UNSET,     // '?'
  BIT_UNFILTERED // unfiltered
} bit_value_t;

static bool ValueSet(bit_value_t V) {
  return (V == BIT_TRUE || V == BIT_FALSE);
}

static bool ValueNotSet(bit_value_t V) {
  return (V == BIT_UNSET);
}

static int Value(bit_value_t V) {
  return ValueNotSet(V) ? -1 : (V == BIT_FALSE ? 0 : 1);
}

static bit_value_t bitFromBits(const BitsInit &bits, unsigned index) {
  if (BitInit *bit = dyn_cast<BitInit>(bits.getBit(index)))
    return bit->getValue() ? BIT_TRUE : BIT_FALSE;

  // The bit is uninitialized.
  return BIT_UNSET;
}

// Prints the bit value for each position.
static void dumpBits(raw_ostream &o, const BitsInit &bits) {
  for (unsigned index = bits.getNumBits(); index > 0; --index) {
    switch (bitFromBits(bits, index - 1)) {
    case BIT_TRUE:
      o << "1";
      break;
    case BIT_FALSE:
      o << "0";
      break;
    case BIT_UNSET:
      o << "_";
      break;
    default:
      llvm_unreachable("unexpected return value from bitFromBits");
    }
  }
}

static BitsInit &getBitsField(const Record &def, StringRef str) {
  BitsInit *bits = def.getValueAsBitsInit(str);
  return *bits;
}

// Representation of the instruction to work on.
typedef std::vector<bit_value_t> insn_t;

namespace {

class FilterChooser;

/// Filter - Filter works with FilterChooser to produce the decoding tree for
/// the ISA.
///
/// It is useful to think of a Filter as governing the switch stmts of the
/// decoding tree in a certain level.  Each case stmt delegates to an inferior
/// FilterChooser to decide what further decoding logic to employ, or in another
/// words, what other remaining bits to look at.  The FilterChooser eventually
/// chooses a best Filter to do its job.
///
/// This recursive scheme ends when the number of Opcodes assigned to the
/// FilterChooser becomes 1 or if there is a conflict.  A conflict happens when
/// the Filter/FilterChooser combo does not know how to distinguish among the
/// Opcodes assigned.
///
/// An example of a conflict is
///
/// Conflict:
///                     111101000.00........00010000....
///                     111101000.00........0001........
///                     1111010...00........0001........
///                     1111010...00....................
///                     1111010.........................
///                     1111............................
///                     ................................
///     VST4q8a         111101000_00________00010000____
///     VST4q8b         111101000_00________00010000____
///
/// The Debug output shows the path that the decoding tree follows to reach the
/// the conclusion that there is a conflict.  VST4q8a is a vst4 to double-spaced
/// even registers, while VST4q8b is a vst4 to double-spaced odd registers.
///
/// The encoding info in the .td files does not specify this meta information,
/// which could have been used by the decoder to resolve the conflict.  The
/// decoder could try to decode the even/odd register numbering and assign to
/// VST4q8a or VST4q8b, but for the time being, the decoder chooses the "a"
/// version and return the Opcode since the two have the same Asm format string.
class Filter {
protected:
  const FilterChooser *Owner;// points to the FilterChooser who owns this filter
  unsigned StartBit; // the starting bit position
  unsigned NumBits; // number of bits to filter
  bool Mixed; // a mixed region contains both set and unset bits

  // Map of well-known segment value to the set of uid's with that value.
  std::map<uint64_t, std::vector<unsigned>> FilteredInstructions;

  // Set of uid's with non-constant segment values.
  std::vector<unsigned> VariableInstructions;

  // Map of well-known segment value to its delegate.
  std::map<unsigned, std::unique_ptr<const FilterChooser>> FilterChooserMap;

  // Number of instructions which fall under FilteredInstructions category.
  unsigned NumFiltered;

  // Keeps track of the last opcode in the filtered bucket.
  unsigned LastOpcFiltered;

public:
  Filter(Filter &&f);
  Filter(FilterChooser &owner, unsigned startBit, unsigned numBits, bool mixed);

  ~Filter() = default;

  unsigned getNumFiltered() const { return NumFiltered; }

  unsigned getSingletonOpc() const {
    assert(NumFiltered == 1);
    return LastOpcFiltered;
  }

  // Return the filter chooser for the group of instructions without constant
  // segment values.
  const FilterChooser &getVariableFC() const {
    assert(NumFiltered == 1);
    assert(FilterChooserMap.size() == 1);
    return *(FilterChooserMap.find((unsigned)-1)->second);
  }

  // Divides the decoding task into sub tasks and delegates them to the
  // inferior FilterChooser's.
  //
  // A special case arises when there's only one entry in the filtered
  // instructions.  In order to unambiguously decode the singleton, we need to
  // match the remaining undecoded encoding bits against the singleton.
  void recurse();

  // Emit table entries to decode instructions given a segment or segments of
  // bits.
  void emitTableEntry(DecoderTableInfo &TableInfo) const;

  // Returns the number of fanout produced by the filter.  More fanout implies
  // the filter distinguishes more categories of instructions.
  unsigned usefulness() const;
}; // end class Filter

} // end anonymous namespace

// These are states of our finite state machines used in FilterChooser's
// filterProcessor() which produces the filter candidates to use.
typedef enum {
  ATTR_NONE,
  ATTR_FILTERED,
  ATTR_ALL_SET,
  ATTR_ALL_UNSET,
  ATTR_MIXED
} bitAttr_t;

/// FilterChooser - FilterChooser chooses the best filter among a set of Filters
/// in order to perform the decoding of instructions at the current level.
///
/// Decoding proceeds from the top down.  Based on the well-known encoding bits
/// of instructions available, FilterChooser builds up the possible Filters that
/// can further the task of decoding by distinguishing among the remaining
/// candidate instructions.
///
/// Once a filter has been chosen, it is called upon to divide the decoding task
/// into sub-tasks and delegates them to its inferior FilterChoosers for further
/// processings.
///
/// It is useful to think of a Filter as governing the switch stmts of the
/// decoding tree.  And each case is delegated to an inferior FilterChooser to
/// decide what further remaining bits to look at.
namespace {

class FilterChooser {
protected:
  friend class Filter;

  // Vector of codegen instructions to choose our filter.
  ArrayRef<EncodingAndInst> AllInstructions;

  // Vector of uid's for this filter chooser to work on.
  const std::vector<unsigned> &Opcodes;

  // Lookup table for the operand decoding of instructions.
  const std::map<unsigned, std::vector<OperandInfo>> &Operands;

  // Vector of candidate filters.
  std::vector<Filter> Filters;

  // Array of bit values passed down from our parent.
  // Set to all BIT_UNFILTERED's for Parent == NULL.
  std::vector<bit_value_t> FilterBitValues;

  // Links to the FilterChooser above us in the decoding tree.
  const FilterChooser *Parent;

  // Index of the best filter from Filters.
  int BestIndex;

  // Width of instructions
  unsigned BitWidth;

  // Parent emitter
  const FixedLenDecoderEmitter *Emitter;

public:
  FilterChooser(ArrayRef<EncodingAndInst> Insts,
                const std::vector<unsigned> &IDs,
                const std::map<unsigned, std::vector<OperandInfo>> &Ops,
                unsigned BW, const FixedLenDecoderEmitter *E)
      : AllInstructions(Insts), Opcodes(IDs), Operands(Ops),
        FilterBitValues(BW, BIT_UNFILTERED), Parent(nullptr), BestIndex(-1),
        BitWidth(BW), Emitter(E) {
    doFilter();
  }

  FilterChooser(ArrayRef<EncodingAndInst> Insts,
                const std::vector<unsigned> &IDs,
                const std::map<unsigned, std::vector<OperandInfo>> &Ops,
                const std::vector<bit_value_t> &ParentFilterBitValues,
                const FilterChooser &parent)
      : AllInstructions(Insts), Opcodes(IDs), Operands(Ops),
        FilterBitValues(ParentFilterBitValues), Parent(&parent), BestIndex(-1),
        BitWidth(parent.BitWidth), Emitter(parent.Emitter) {
    doFilter();
  }

  FilterChooser(const FilterChooser &) = delete;
  void operator=(const FilterChooser &) = delete;

  unsigned getBitWidth() const { return BitWidth; }

protected:
  // Populates the insn given the uid.
  void insnWithID(insn_t &Insn, unsigned Opcode) const {
    BitsInit &Bits = getBitsField(*AllInstructions[Opcode].EncodingDef, "Inst");

    // We may have a SoftFail bitmask, which specifies a mask where an encoding
    // may differ from the value in "Inst" and yet still be valid, but the
    // disassembler should return SoftFail instead of Success.
    //
    // This is used for marking UNPREDICTABLE instructions in the ARM world.
    BitsInit *SFBits =
        AllInstructions[Opcode].EncodingDef->getValueAsBitsInit("SoftFail");

    for (unsigned i = 0; i < BitWidth; ++i) {
      if (SFBits && bitFromBits(*SFBits, i) == BIT_TRUE)
        Insn.push_back(BIT_UNSET);
      else
        Insn.push_back(bitFromBits(Bits, i));
    }
  }

  // Populates the field of the insn given the start position and the number of
  // consecutive bits to scan for.
  //
  // Returns false if there exists any uninitialized bit value in the range.
  // Returns true, otherwise.
  bool fieldFromInsn(uint64_t &Field, insn_t &Insn, unsigned StartBit,
                     unsigned NumBits) const;

  /// dumpFilterArray - dumpFilterArray prints out debugging info for the given
  /// filter array as a series of chars.
  void dumpFilterArray(raw_ostream &o,
                       const std::vector<bit_value_t> & filter) const;

  /// dumpStack - dumpStack traverses the filter chooser chain and calls
  /// dumpFilterArray on each filter chooser up to the top level one.
  void dumpStack(raw_ostream &o, const char *prefix) const;

  Filter &bestFilter() {
    assert(BestIndex != -1 && "BestIndex not set");
    return Filters[BestIndex];
  }

  bool PositionFiltered(unsigned i) const {
    return ValueSet(FilterBitValues[i]);
  }

  // Calculates the island(s) needed to decode the instruction.
  // This returns a lit of undecoded bits of an instructions, for example,
  // Inst{20} = 1 && Inst{3-0} == 0b1111 represents two islands of yet-to-be
  // decoded bits in order to verify that the instruction matches the Opcode.
  unsigned getIslands(std::vector<unsigned> &StartBits,
                      std::vector<unsigned> &EndBits,
                      std::vector<uint64_t> &FieldVals,
                      const insn_t &Insn) const;

  // Emits code to check the Predicates member of an instruction are true.
  // Returns true if predicate matches were emitted, false otherwise.
  bool emitPredicateMatch(raw_ostream &o, unsigned &Indentation,
                          unsigned Opc) const;

  bool doesOpcodeNeedPredicate(unsigned Opc) const;
  unsigned getPredicateIndex(DecoderTableInfo &TableInfo, StringRef P) const;
  void emitPredicateTableEntry(DecoderTableInfo &TableInfo,
                               unsigned Opc) const;

  void emitSoftFailTableEntry(DecoderTableInfo &TableInfo,
                              unsigned Opc) const;

  // Emits table entries to decode the singleton.
  void emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                               unsigned Opc) const;

  // Emits code to decode the singleton, and then to decode the rest.
  void emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                               const Filter &Best) const;

  void emitBinaryParser(raw_ostream &o, unsigned &Indentation,
                        const OperandInfo &OpInfo,
                        bool &OpHasCompleteDecoder) const;

  void emitDecoder(raw_ostream &OS, unsigned Indentation, unsigned Opc,
                   bool &HasCompleteDecoder) const;
  unsigned getDecoderIndex(DecoderSet &Decoders, unsigned Opc,
                           bool &HasCompleteDecoder) const;

  // Assign a single filter and run with it.
  void runSingleFilter(unsigned startBit, unsigned numBit, bool mixed);

  // reportRegion is a helper function for filterProcessor to mark a region as
  // eligible for use as a filter region.
  void reportRegion(bitAttr_t RA, unsigned StartBit, unsigned BitIndex,
                    bool AllowMixed);

  // FilterProcessor scans the well-known encoding bits of the instructions and
  // builds up a list of candidate filters.  It chooses the best filter and
  // recursively descends down the decoding tree.
  bool filterProcessor(bool AllowMixed, bool Greedy = true);

  // Decides on the best configuration of filter(s) to use in order to decode
  // the instructions.  A conflict of instructions may occur, in which case we
  // dump the conflict set to the standard error.
  void doFilter();

public:
  // emitTableEntries - Emit state machine entries to decode our share of
  // instructions.
  void emitTableEntries(DecoderTableInfo &TableInfo) const;
};

} // end anonymous namespace

///////////////////////////
//                       //
// Filter Implementation //
//                       //
///////////////////////////

Filter::Filter(Filter &&f)
  : Owner(f.Owner), StartBit(f.StartBit), NumBits(f.NumBits), Mixed(f.Mixed),
    FilteredInstructions(std::move(f.FilteredInstructions)),
    VariableInstructions(std::move(f.VariableInstructions)),
    FilterChooserMap(std::move(f.FilterChooserMap)), NumFiltered(f.NumFiltered),
    LastOpcFiltered(f.LastOpcFiltered) {
}

Filter::Filter(FilterChooser &owner, unsigned startBit, unsigned numBits,
               bool mixed)
  : Owner(&owner), StartBit(startBit), NumBits(numBits), Mixed(mixed) {
  assert(StartBit + NumBits - 1 < Owner->BitWidth);

  NumFiltered = 0;
  LastOpcFiltered = 0;

  for (unsigned i = 0, e = Owner->Opcodes.size(); i != e; ++i) {
    insn_t Insn;

    // Populates the insn given the uid.
    Owner->insnWithID(Insn, Owner->Opcodes[i]);

    uint64_t Field;
    // Scans the segment for possibly well-specified encoding bits.
    bool ok = Owner->fieldFromInsn(Field, Insn, StartBit, NumBits);

    if (ok) {
      // The encoding bits are well-known.  Lets add the uid of the
      // instruction into the bucket keyed off the constant field value.
      LastOpcFiltered = Owner->Opcodes[i];
      FilteredInstructions[Field].push_back(LastOpcFiltered);
      ++NumFiltered;
    } else {
      // Some of the encoding bit(s) are unspecified.  This contributes to
      // one additional member of "Variable" instructions.
      VariableInstructions.push_back(Owner->Opcodes[i]);
    }
  }

  assert((FilteredInstructions.size() + VariableInstructions.size() > 0)
         && "Filter returns no instruction categories");
}

// Divides the decoding task into sub tasks and delegates them to the
// inferior FilterChooser's.
//
// A special case arises when there's only one entry in the filtered
// instructions.  In order to unambiguously decode the singleton, we need to
// match the remaining undecoded encoding bits against the singleton.
void Filter::recurse() {
  // Starts by inheriting our parent filter chooser's filter bit values.
  std::vector<bit_value_t> BitValueArray(Owner->FilterBitValues);

  if (!VariableInstructions.empty()) {
    // Conservatively marks each segment position as BIT_UNSET.
    for (unsigned bitIndex = 0; bitIndex < NumBits; ++bitIndex)
      BitValueArray[StartBit + bitIndex] = BIT_UNSET;

    // Delegates to an inferior filter chooser for further processing on this
    // group of instructions whose segment values are variable.
    FilterChooserMap.insert(
        std::make_pair(-1U, llvm::make_unique<FilterChooser>(
                                Owner->AllInstructions, VariableInstructions,
                                Owner->Operands, BitValueArray, *Owner)));
  }

  // No need to recurse for a singleton filtered instruction.
  // See also Filter::emit*().
  if (getNumFiltered() == 1) {
    assert(FilterChooserMap.size() == 1);
    return;
  }

  // Otherwise, create sub choosers.
  for (const auto &Inst : FilteredInstructions) {

    // Marks all the segment positions with either BIT_TRUE or BIT_FALSE.
    for (unsigned bitIndex = 0; bitIndex < NumBits; ++bitIndex) {
      if (Inst.first & (1ULL << bitIndex))
        BitValueArray[StartBit + bitIndex] = BIT_TRUE;
      else
        BitValueArray[StartBit + bitIndex] = BIT_FALSE;
    }

    // Delegates to an inferior filter chooser for further processing on this
    // category of instructions.
    FilterChooserMap.insert(std::make_pair(
        Inst.first, llvm::make_unique<FilterChooser>(
                                Owner->AllInstructions, Inst.second,
                                Owner->Operands, BitValueArray, *Owner)));
  }
}

static void resolveTableFixups(DecoderTable &Table, const FixupList &Fixups,
                               uint32_t DestIdx) {
  // Any NumToSkip fixups in the current scope can resolve to the
  // current location.
  for (FixupList::const_reverse_iterator I = Fixups.rbegin(),
                                         E = Fixups.rend();
       I != E; ++I) {
    // Calculate the distance from the byte following the fixup entry byte
    // to the destination. The Target is calculated from after the 16-bit
    // NumToSkip entry itself, so subtract two  from the displacement here
    // to account for that.
    uint32_t FixupIdx = *I;
    uint32_t Delta = DestIdx - FixupIdx - 3;
    // Our NumToSkip entries are 24-bits. Make sure our table isn't too
    // big.
    assert(Delta < (1u << 24));
    Table[FixupIdx] = (uint8_t)Delta;
    Table[FixupIdx + 1] = (uint8_t)(Delta >> 8);
    Table[FixupIdx + 2] = (uint8_t)(Delta >> 16);
  }
}

// Emit table entries to decode instructions given a segment or segments
// of bits.
void Filter::emitTableEntry(DecoderTableInfo &TableInfo) const {
  TableInfo.Table.push_back(MCD::OPC_ExtractField);
  TableInfo.Table.push_back(StartBit);
  TableInfo.Table.push_back(NumBits);

  // A new filter entry begins a new scope for fixup resolution.
  TableInfo.FixupStack.emplace_back();

  DecoderTable &Table = TableInfo.Table;

  size_t PrevFilter = 0;
  bool HasFallthrough = false;
  for (auto &Filter : FilterChooserMap) {
    // Field value -1 implies a non-empty set of variable instructions.
    // See also recurse().
    if (Filter.first == (unsigned)-1) {
      HasFallthrough = true;

      // Each scope should always have at least one filter value to check
      // for.
      assert(PrevFilter != 0 && "empty filter set!");
      FixupList &CurScope = TableInfo.FixupStack.back();
      // Resolve any NumToSkip fixups in the current scope.
      resolveTableFixups(Table, CurScope, Table.size());
      CurScope.clear();
      PrevFilter = 0;  // Don't re-process the filter's fallthrough.
    } else {
      Table.push_back(MCD::OPC_FilterValue);
      // Encode and emit the value to filter against.
      uint8_t Buffer[16];
      unsigned Len = encodeULEB128(Filter.first, Buffer);
      Table.insert(Table.end(), Buffer, Buffer + Len);
      // Reserve space for the NumToSkip entry. We'll backpatch the value
      // later.
      PrevFilter = Table.size();
      Table.push_back(0);
      Table.push_back(0);
      Table.push_back(0);
    }

    // We arrive at a category of instructions with the same segment value.
    // Now delegate to the sub filter chooser for further decodings.
    // The case may fallthrough, which happens if the remaining well-known
    // encoding bits do not match exactly.
    Filter.second->emitTableEntries(TableInfo);

    // Now that we've emitted the body of the handler, update the NumToSkip
    // of the filter itself to be able to skip forward when false. Subtract
    // two as to account for the width of the NumToSkip field itself.
    if (PrevFilter) {
      uint32_t NumToSkip = Table.size() - PrevFilter - 3;
      assert(NumToSkip < (1u << 24) && "disassembler decoding table too large!");
      Table[PrevFilter] = (uint8_t)NumToSkip;
      Table[PrevFilter + 1] = (uint8_t)(NumToSkip >> 8);
      Table[PrevFilter + 2] = (uint8_t)(NumToSkip >> 16);
    }
  }

  // Any remaining unresolved fixups bubble up to the parent fixup scope.
  assert(TableInfo.FixupStack.size() > 1 && "fixup stack underflow!");
  FixupScopeList::iterator Source = TableInfo.FixupStack.end() - 1;
  FixupScopeList::iterator Dest = Source - 1;
  Dest->insert(Dest->end(), Source->begin(), Source->end());
  TableInfo.FixupStack.pop_back();

  // If there is no fallthrough, then the final filter should get fixed
  // up according to the enclosing scope rather than the current position.
  if (!HasFallthrough)
    TableInfo.FixupStack.back().push_back(PrevFilter);
}

// Returns the number of fanout produced by the filter.  More fanout implies
// the filter distinguishes more categories of instructions.
unsigned Filter::usefulness() const {
  if (!VariableInstructions.empty())
    return FilteredInstructions.size();
  else
    return FilteredInstructions.size() + 1;
}

//////////////////////////////////
//                              //
// Filterchooser Implementation //
//                              //
//////////////////////////////////

// Emit the decoder state machine table.
void FixedLenDecoderEmitter::emitTable(formatted_raw_ostream &OS,
                                       DecoderTable &Table,
                                       unsigned Indentation,
                                       unsigned BitWidth,
                                       StringRef Namespace) const {
  OS.indent(Indentation) << "static const uint8_t DecoderTable" << Namespace
    << BitWidth << "[] = {\n";

  Indentation += 2;

  // FIXME: We may be able to use the NumToSkip values to recover
  // appropriate indentation levels.
  DecoderTable::const_iterator I = Table.begin();
  DecoderTable::const_iterator E = Table.end();
  while (I != E) {
    assert (I < E && "incomplete decode table entry!");

    uint64_t Pos = I - Table.begin();
    OS << "/* " << Pos << " */";
    OS.PadToColumn(12);

    switch (*I) {
    default:
      PrintFatalError("invalid decode table opcode");
    case MCD::OPC_ExtractField: {
      ++I;
      unsigned Start = *I++;
      unsigned Len = *I++;
      OS.indent(Indentation) << "MCD::OPC_ExtractField, " << Start << ", "
        << Len << ",  // Inst{";
      if (Len > 1)
        OS << (Start + Len - 1) << "-";
      OS << Start << "} ...\n";
      break;
    }
    case MCD::OPC_FilterValue: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_FilterValue, ";
      // The filter value is ULEB128 encoded.
      while (*I >= 128)
        OS << (unsigned)*I++ << ", ";
      OS << (unsigned)*I++ << ", ";

      // 24-bit numtoskip value.
      uint8_t Byte = *I++;
      uint32_t NumToSkip = Byte;
      OS << (unsigned)Byte << ", ";
      Byte = *I++;
      OS << (unsigned)Byte << ", ";
      NumToSkip |= Byte << 8;
      Byte = *I++;
      OS << utostr(Byte) << ", ";
      NumToSkip |= Byte << 16;
      OS << "// Skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_CheckField: {
      ++I;
      unsigned Start = *I++;
      unsigned Len = *I++;
      OS.indent(Indentation) << "MCD::OPC_CheckField, " << Start << ", "
        << Len << ", ";// << Val << ", " << NumToSkip << ",\n";
      // ULEB128 encoded field value.
      for (; *I >= 128; ++I)
        OS << (unsigned)*I << ", ";
      OS << (unsigned)*I++ << ", ";
      // 24-bit numtoskip value.
      uint8_t Byte = *I++;
      uint32_t NumToSkip = Byte;
      OS << (unsigned)Byte << ", ";
      Byte = *I++;
      OS << (unsigned)Byte << ", ";
      NumToSkip |= Byte << 8;
      Byte = *I++;
      OS << utostr(Byte) << ", ";
      NumToSkip |= Byte << 16;
      OS << "// Skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_CheckPredicate: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_CheckPredicate, ";
      for (; *I >= 128; ++I)
        OS << (unsigned)*I << ", ";
      OS << (unsigned)*I++ << ", ";

      // 24-bit numtoskip value.
      uint8_t Byte = *I++;
      uint32_t NumToSkip = Byte;
      OS << (unsigned)Byte << ", ";
      Byte = *I++;
      OS << (unsigned)Byte << ", ";
      NumToSkip |= Byte << 8;
      Byte = *I++;
      OS << utostr(Byte) << ", ";
      NumToSkip |= Byte << 16;
      OS << "// Skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_Decode:
    case MCD::OPC_TryDecode: {
      bool IsTry = *I == MCD::OPC_TryDecode;
      ++I;
      // Extract the ULEB128 encoded Opcode to a buffer.
      uint8_t Buffer[16], *p = Buffer;
      while ((*p++ = *I++) >= 128)
        assert((p - Buffer) <= (ptrdiff_t)sizeof(Buffer)
               && "ULEB128 value too large!");
      // Decode the Opcode value.
      unsigned Opc = decodeULEB128(Buffer);
      OS.indent(Indentation) << "MCD::OPC_" << (IsTry ? "Try" : "")
        << "Decode, ";
      for (p = Buffer; *p >= 128; ++p)
        OS << (unsigned)*p << ", ";
      OS << (unsigned)*p << ", ";

      // Decoder index.
      for (; *I >= 128; ++I)
        OS << (unsigned)*I << ", ";
      OS << (unsigned)*I++ << ", ";

      if (!IsTry) {
        OS << "// Opcode: " << NumberedEncodings[Opc] << "\n";
        break;
      }

      // Fallthrough for OPC_TryDecode.

      // 24-bit numtoskip value.
      uint8_t Byte = *I++;
      uint32_t NumToSkip = Byte;
      OS << (unsigned)Byte << ", ";
      Byte = *I++;
      OS << (unsigned)Byte << ", ";
      NumToSkip |= Byte << 8;
      Byte = *I++;
      OS << utostr(Byte) << ", ";
      NumToSkip |= Byte << 16;

      OS << "// Opcode: " << NumberedEncodings[Opc]
         << ", skip to: " << ((I - Table.begin()) + NumToSkip) << "\n";
      break;
    }
    case MCD::OPC_SoftFail: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_SoftFail";
      // Positive mask
      uint64_t Value = 0;
      unsigned Shift = 0;
      do {
        OS << ", " << (unsigned)*I;
        Value += (*I & 0x7f) << Shift;
        Shift += 7;
      } while (*I++ >= 128);
      if (Value > 127) {
        OS << " /* 0x";
        OS.write_hex(Value);
        OS << " */";
      }
      // Negative mask
      Value = 0;
      Shift = 0;
      do {
        OS << ", " << (unsigned)*I;
        Value += (*I & 0x7f) << Shift;
        Shift += 7;
      } while (*I++ >= 128);
      if (Value > 127) {
        OS << " /* 0x";
        OS.write_hex(Value);
        OS << " */";
      }
      OS << ",\n";
      break;
    }
    case MCD::OPC_Fail: {
      ++I;
      OS.indent(Indentation) << "MCD::OPC_Fail,\n";
      break;
    }
    }
  }
  OS.indent(Indentation) << "0\n";

  Indentation -= 2;

  OS.indent(Indentation) << "};\n\n";
}

void FixedLenDecoderEmitter::
emitPredicateFunction(formatted_raw_ostream &OS, PredicateSet &Predicates,
                      unsigned Indentation) const {
  // The predicate function is just a big switch statement based on the
  // input predicate index.
  OS.indent(Indentation) << "static bool checkDecoderPredicate(unsigned Idx, "
    << "const FeatureBitset& Bits) {\n";
  Indentation += 2;
  if (!Predicates.empty()) {
    OS.indent(Indentation) << "switch (Idx) {\n";
    OS.indent(Indentation) << "default: llvm_unreachable(\"Invalid index!\");\n";
    unsigned Index = 0;
    for (const auto &Predicate : Predicates) {
      OS.indent(Indentation) << "case " << Index++ << ":\n";
      OS.indent(Indentation+2) << "return (" << Predicate << ");\n";
    }
    OS.indent(Indentation) << "}\n";
  } else {
    // No case statement to emit
    OS.indent(Indentation) << "llvm_unreachable(\"Invalid index!\");\n";
  }
  Indentation -= 2;
  OS.indent(Indentation) << "}\n\n";
}

void FixedLenDecoderEmitter::
emitDecoderFunction(formatted_raw_ostream &OS, DecoderSet &Decoders,
                    unsigned Indentation) const {
  // The decoder function is just a big switch statement based on the
  // input decoder index.
  OS.indent(Indentation) << "template<typename InsnType>\n";
  OS.indent(Indentation) << "static DecodeStatus decodeToMCInst(DecodeStatus S,"
    << " unsigned Idx, InsnType insn, MCInst &MI,\n";
  OS.indent(Indentation) << "                                   uint64_t "
    << "Address, const void *Decoder, bool &DecodeComplete) {\n";
  Indentation += 2;
  OS.indent(Indentation) << "DecodeComplete = true;\n";
  OS.indent(Indentation) << "InsnType tmp;\n";
  OS.indent(Indentation) << "switch (Idx) {\n";
  OS.indent(Indentation) << "default: llvm_unreachable(\"Invalid index!\");\n";
  unsigned Index = 0;
  for (const auto &Decoder : Decoders) {
    OS.indent(Indentation) << "case " << Index++ << ":\n";
    OS << Decoder;
    OS.indent(Indentation+2) << "return S;\n";
  }
  OS.indent(Indentation) << "}\n";
  Indentation -= 2;
  OS.indent(Indentation) << "}\n\n";
}

// Populates the field of the insn given the start position and the number of
// consecutive bits to scan for.
//
// Returns false if and on the first uninitialized bit value encountered.
// Returns true, otherwise.
bool FilterChooser::fieldFromInsn(uint64_t &Field, insn_t &Insn,
                                  unsigned StartBit, unsigned NumBits) const {
  Field = 0;

  for (unsigned i = 0; i < NumBits; ++i) {
    if (Insn[StartBit + i] == BIT_UNSET)
      return false;

    if (Insn[StartBit + i] == BIT_TRUE)
      Field = Field | (1ULL << i);
  }

  return true;
}

/// dumpFilterArray - dumpFilterArray prints out debugging info for the given
/// filter array as a series of chars.
void FilterChooser::dumpFilterArray(raw_ostream &o,
                                 const std::vector<bit_value_t> &filter) const {
  for (unsigned bitIndex = BitWidth; bitIndex > 0; bitIndex--) {
    switch (filter[bitIndex - 1]) {
    case BIT_UNFILTERED:
      o << ".";
      break;
    case BIT_UNSET:
      o << "_";
      break;
    case BIT_TRUE:
      o << "1";
      break;
    case BIT_FALSE:
      o << "0";
      break;
    }
  }
}

/// dumpStack - dumpStack traverses the filter chooser chain and calls
/// dumpFilterArray on each filter chooser up to the top level one.
void FilterChooser::dumpStack(raw_ostream &o, const char *prefix) const {
  const FilterChooser *current = this;

  while (current) {
    o << prefix;
    dumpFilterArray(o, current->FilterBitValues);
    o << '\n';
    current = current->Parent;
  }
}

// Calculates the island(s) needed to decode the instruction.
// This returns a list of undecoded bits of an instructions, for example,
// Inst{20} = 1 && Inst{3-0} == 0b1111 represents two islands of yet-to-be
// decoded bits in order to verify that the instruction matches the Opcode.
unsigned FilterChooser::getIslands(std::vector<unsigned> &StartBits,
                                   std::vector<unsigned> &EndBits,
                                   std::vector<uint64_t> &FieldVals,
                                   const insn_t &Insn) const {
  unsigned Num, BitNo;
  Num = BitNo = 0;

  uint64_t FieldVal = 0;

  // 0: Init
  // 1: Water (the bit value does not affect decoding)
  // 2: Island (well-known bit value needed for decoding)
  int State = 0;
  int Val = -1;

  for (unsigned i = 0; i < BitWidth; ++i) {
    Val = Value(Insn[i]);
    bool Filtered = PositionFiltered(i);
    switch (State) {
    default: llvm_unreachable("Unreachable code!");
    case 0:
    case 1:
      if (Filtered || Val == -1)
        State = 1; // Still in Water
      else {
        State = 2; // Into the Island
        BitNo = 0;
        StartBits.push_back(i);
        FieldVal = Val;
      }
      break;
    case 2:
      if (Filtered || Val == -1) {
        State = 1; // Into the Water
        EndBits.push_back(i - 1);
        FieldVals.push_back(FieldVal);
        ++Num;
      } else {
        State = 2; // Still in Island
        ++BitNo;
        FieldVal = FieldVal | Val << BitNo;
      }
      break;
    }
  }
  // If we are still in Island after the loop, do some housekeeping.
  if (State == 2) {
    EndBits.push_back(BitWidth - 1);
    FieldVals.push_back(FieldVal);
    ++Num;
  }

  assert(StartBits.size() == Num && EndBits.size() == Num &&
         FieldVals.size() == Num);
  return Num;
}

void FilterChooser::emitBinaryParser(raw_ostream &o, unsigned &Indentation,
                                     const OperandInfo &OpInfo,
                                     bool &OpHasCompleteDecoder) const {
  const std::string &Decoder = OpInfo.Decoder;

  if (OpInfo.numFields() != 1)
    o.indent(Indentation) << "tmp = 0;\n";

  for (const EncodingField &EF : OpInfo) {
    o.indent(Indentation) << "tmp ";
    if (OpInfo.numFields() != 1) o << '|';
    o << "= fieldFromInstruction"
      << "(insn, " << EF.Base << ", " << EF.Width << ')';
    if (OpInfo.numFields() != 1 || EF.Offset != 0)
      o << " << " << EF.Offset;
    o << ";\n";
  }

  if (Decoder != "") {
    OpHasCompleteDecoder = OpInfo.HasCompleteDecoder;
    o.indent(Indentation) << Emitter->GuardPrefix << Decoder
      << "(MI, tmp, Address, Decoder)"
      << Emitter->GuardPostfix
      << " { " << (OpHasCompleteDecoder ? "" : "DecodeComplete = false; ")
      << "return MCDisassembler::Fail; }\n";
  } else {
    OpHasCompleteDecoder = true;
    o.indent(Indentation) << "MI.addOperand(MCOperand::createImm(tmp));\n";
  }
}

void FilterChooser::emitDecoder(raw_ostream &OS, unsigned Indentation,
                                unsigned Opc, bool &HasCompleteDecoder) const {
  HasCompleteDecoder = true;

  for (const auto &Op : Operands.find(Opc)->second) {
    // If a custom instruction decoder was specified, use that.
    if (Op.numFields() == 0 && !Op.Decoder.empty()) {
      HasCompleteDecoder = Op.HasCompleteDecoder;
      OS.indent(Indentation) << Emitter->GuardPrefix << Op.Decoder
        << "(MI, insn, Address, Decoder)"
        << Emitter->GuardPostfix
        << " { " << (HasCompleteDecoder ? "" : "DecodeComplete = false; ")
        << "return MCDisassembler::Fail; }\n";
      break;
    }

    bool OpHasCompleteDecoder;
    emitBinaryParser(OS, Indentation, Op, OpHasCompleteDecoder);
    if (!OpHasCompleteDecoder)
      HasCompleteDecoder = false;
  }
}

unsigned FilterChooser::getDecoderIndex(DecoderSet &Decoders,
                                        unsigned Opc,
                                        bool &HasCompleteDecoder) const {
  // Build up the predicate string.
  SmallString<256> Decoder;
  // FIXME: emitDecoder() function can take a buffer directly rather than
  // a stream.
  raw_svector_ostream S(Decoder);
  unsigned I = 4;
  emitDecoder(S, I, Opc, HasCompleteDecoder);

  // Using the full decoder string as the key value here is a bit
  // heavyweight, but is effective. If the string comparisons become a
  // performance concern, we can implement a mangling of the predicate
  // data easily enough with a map back to the actual string. That's
  // overkill for now, though.

  // Make sure the predicate is in the table.
  Decoders.insert(CachedHashString(Decoder));
  // Now figure out the index for when we write out the table.
  DecoderSet::const_iterator P = find(Decoders, Decoder.str());
  return (unsigned)(P - Decoders.begin());
}

static void emitSinglePredicateMatch(raw_ostream &o, StringRef str,
                                     const std::string &PredicateNamespace) {
  if (str[0] == '!')
    o << "!Bits[" << PredicateNamespace << "::"
      << str.slice(1,str.size()) << "]";
  else
    o << "Bits[" << PredicateNamespace << "::" << str << "]";
}

bool FilterChooser::emitPredicateMatch(raw_ostream &o, unsigned &Indentation,
                                       unsigned Opc) const {
  ListInit *Predicates =
      AllInstructions[Opc].EncodingDef->getValueAsListInit("Predicates");
  bool IsFirstEmission = true;
  for (unsigned i = 0; i < Predicates->size(); ++i) {
    Record *Pred = Predicates->getElementAsRecord(i);
    if (!Pred->getValue("AssemblerMatcherPredicate"))
      continue;

    StringRef P = Pred->getValueAsString("AssemblerCondString");

    if (P.empty())
      continue;

    if (!IsFirstEmission)
      o << " && ";

    std::pair<StringRef, StringRef> pairs = P.split(',');
    while (!pairs.second.empty()) {
      emitSinglePredicateMatch(o, pairs.first, Emitter->PredicateNamespace);
      o << " && ";
      pairs = pairs.second.split(',');
    }
    emitSinglePredicateMatch(o, pairs.first, Emitter->PredicateNamespace);
    IsFirstEmission = false;
  }
  return !Predicates->empty();
}

bool FilterChooser::doesOpcodeNeedPredicate(unsigned Opc) const {
  ListInit *Predicates =
      AllInstructions[Opc].EncodingDef->getValueAsListInit("Predicates");
  for (unsigned i = 0; i < Predicates->size(); ++i) {
    Record *Pred = Predicates->getElementAsRecord(i);
    if (!Pred->getValue("AssemblerMatcherPredicate"))
      continue;

    StringRef P = Pred->getValueAsString("AssemblerCondString");

    if (P.empty())
      continue;

    return true;
  }
  return false;
}

unsigned FilterChooser::getPredicateIndex(DecoderTableInfo &TableInfo,
                                          StringRef Predicate) const {
  // Using the full predicate string as the key value here is a bit
  // heavyweight, but is effective. If the string comparisons become a
  // performance concern, we can implement a mangling of the predicate
  // data easily enough with a map back to the actual string. That's
  // overkill for now, though.

  // Make sure the predicate is in the table.
  TableInfo.Predicates.insert(CachedHashString(Predicate));
  // Now figure out the index for when we write out the table.
  PredicateSet::const_iterator P = find(TableInfo.Predicates, Predicate);
  return (unsigned)(P - TableInfo.Predicates.begin());
}

void FilterChooser::emitPredicateTableEntry(DecoderTableInfo &TableInfo,
                                            unsigned Opc) const {
  if (!doesOpcodeNeedPredicate(Opc))
    return;

  // Build up the predicate string.
  SmallString<256> Predicate;
  // FIXME: emitPredicateMatch() functions can take a buffer directly rather
  // than a stream.
  raw_svector_ostream PS(Predicate);
  unsigned I = 0;
  emitPredicateMatch(PS, I, Opc);

  // Figure out the index into the predicate table for the predicate just
  // computed.
  unsigned PIdx = getPredicateIndex(TableInfo, PS.str());
  SmallString<16> PBytes;
  raw_svector_ostream S(PBytes);
  encodeULEB128(PIdx, S);

  TableInfo.Table.push_back(MCD::OPC_CheckPredicate);
  // Predicate index
  for (unsigned i = 0, e = PBytes.size(); i != e; ++i)
    TableInfo.Table.push_back(PBytes[i]);
  // Push location for NumToSkip backpatching.
  TableInfo.FixupStack.back().push_back(TableInfo.Table.size());
  TableInfo.Table.push_back(0);
  TableInfo.Table.push_back(0);
  TableInfo.Table.push_back(0);
}

void FilterChooser::emitSoftFailTableEntry(DecoderTableInfo &TableInfo,
                                           unsigned Opc) const {
  BitsInit *SFBits =
      AllInstructions[Opc].EncodingDef->getValueAsBitsInit("SoftFail");
  if (!SFBits) return;
  BitsInit *InstBits =
      AllInstructions[Opc].EncodingDef->getValueAsBitsInit("Inst");

  APInt PositiveMask(BitWidth, 0ULL);
  APInt NegativeMask(BitWidth, 0ULL);
  for (unsigned i = 0; i < BitWidth; ++i) {
    bit_value_t B = bitFromBits(*SFBits, i);
    bit_value_t IB = bitFromBits(*InstBits, i);

    if (B != BIT_TRUE) continue;

    switch (IB) {
    case BIT_FALSE:
      // The bit is meant to be false, so emit a check to see if it is true.
      PositiveMask.setBit(i);
      break;
    case BIT_TRUE:
      // The bit is meant to be true, so emit a check to see if it is false.
      NegativeMask.setBit(i);
      break;
    default:
      // The bit is not set; this must be an error!
      errs() << "SoftFail Conflict: bit SoftFail{" << i << "} in "
             << AllInstructions[Opc] << " is set but Inst{" << i
             << "} is unset!\n"
             << "  - You can only mark a bit as SoftFail if it is fully defined"
             << " (1/0 - not '?') in Inst\n";
      return;
    }
  }

  bool NeedPositiveMask = PositiveMask.getBoolValue();
  bool NeedNegativeMask = NegativeMask.getBoolValue();

  if (!NeedPositiveMask && !NeedNegativeMask)
    return;

  TableInfo.Table.push_back(MCD::OPC_SoftFail);

  SmallString<16> MaskBytes;
  raw_svector_ostream S(MaskBytes);
  if (NeedPositiveMask) {
    encodeULEB128(PositiveMask.getZExtValue(), S);
    for (unsigned i = 0, e = MaskBytes.size(); i != e; ++i)
      TableInfo.Table.push_back(MaskBytes[i]);
  } else
    TableInfo.Table.push_back(0);
  if (NeedNegativeMask) {
    MaskBytes.clear();
    encodeULEB128(NegativeMask.getZExtValue(), S);
    for (unsigned i = 0, e = MaskBytes.size(); i != e; ++i)
      TableInfo.Table.push_back(MaskBytes[i]);
  } else
    TableInfo.Table.push_back(0);
}

// Emits table entries to decode the singleton.
void FilterChooser::emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                                            unsigned Opc) const {
  std::vector<unsigned> StartBits;
  std::vector<unsigned> EndBits;
  std::vector<uint64_t> FieldVals;
  insn_t Insn;
  insnWithID(Insn, Opc);

  // Look for islands of undecoded bits of the singleton.
  getIslands(StartBits, EndBits, FieldVals, Insn);

  unsigned Size = StartBits.size();

  // Emit the predicate table entry if one is needed.
  emitPredicateTableEntry(TableInfo, Opc);

  // Check any additional encoding fields needed.
  for (unsigned I = Size; I != 0; --I) {
    unsigned NumBits = EndBits[I-1] - StartBits[I-1] + 1;
    TableInfo.Table.push_back(MCD::OPC_CheckField);
    TableInfo.Table.push_back(StartBits[I-1]);
    TableInfo.Table.push_back(NumBits);
    uint8_t Buffer[16], *p;
    encodeULEB128(FieldVals[I-1], Buffer);
    for (p = Buffer; *p >= 128 ; ++p)
      TableInfo.Table.push_back(*p);
    TableInfo.Table.push_back(*p);
    // Push location for NumToSkip backpatching.
    TableInfo.FixupStack.back().push_back(TableInfo.Table.size());
    // The fixup is always 24-bits, so go ahead and allocate the space
    // in the table so all our relative position calculations work OK even
    // before we fully resolve the real value here.
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
  }

  // Check for soft failure of the match.
  emitSoftFailTableEntry(TableInfo, Opc);

  bool HasCompleteDecoder;
  unsigned DIdx = getDecoderIndex(TableInfo.Decoders, Opc, HasCompleteDecoder);

  // Produce OPC_Decode or OPC_TryDecode opcode based on the information
  // whether the instruction decoder is complete or not. If it is complete
  // then it handles all possible values of remaining variable/unfiltered bits
  // and for any value can determine if the bitpattern is a valid instruction
  // or not. This means OPC_Decode will be the final step in the decoding
  // process. If it is not complete, then the Fail return code from the
  // decoder method indicates that additional processing should be done to see
  // if there is any other instruction that also matches the bitpattern and
  // can decode it.
  TableInfo.Table.push_back(HasCompleteDecoder ? MCD::OPC_Decode :
      MCD::OPC_TryDecode);
  uint8_t Buffer[16], *p;
  encodeULEB128(Opc, Buffer);
  for (p = Buffer; *p >= 128 ; ++p)
    TableInfo.Table.push_back(*p);
  TableInfo.Table.push_back(*p);

  SmallString<16> Bytes;
  raw_svector_ostream S(Bytes);
  encodeULEB128(DIdx, S);

  // Decoder index
  for (unsigned i = 0, e = Bytes.size(); i != e; ++i)
    TableInfo.Table.push_back(Bytes[i]);

  if (!HasCompleteDecoder) {
    // Push location for NumToSkip backpatching.
    TableInfo.FixupStack.back().push_back(TableInfo.Table.size());
    // Allocate the space for the fixup.
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
    TableInfo.Table.push_back(0);
  }
}

// Emits table entries to decode the singleton, and then to decode the rest.
void FilterChooser::emitSingletonTableEntry(DecoderTableInfo &TableInfo,
                                            const Filter &Best) const {
  unsigned Opc = Best.getSingletonOpc();

  // complex singletons need predicate checks from the first singleton
  // to refer forward to the variable filterchooser that follows.
  TableInfo.FixupStack.emplace_back();

  emitSingletonTableEntry(TableInfo, Opc);

  resolveTableFixups(TableInfo.Table, TableInfo.FixupStack.back(),
                     TableInfo.Table.size());
  TableInfo.FixupStack.pop_back();

  Best.getVariableFC().emitTableEntries(TableInfo);
}

// Assign a single filter and run with it.  Top level API client can initialize
// with a single filter to start the filtering process.
void FilterChooser::runSingleFilter(unsigned startBit, unsigned numBit,
                                    bool mixed) {
  Filters.clear();
  Filters.emplace_back(*this, startBit, numBit, true);
  BestIndex = 0; // Sole Filter instance to choose from.
  bestFilter().recurse();
}

// reportRegion is a helper function for filterProcessor to mark a region as
// eligible for use as a filter region.
void FilterChooser::reportRegion(bitAttr_t RA, unsigned StartBit,
                                 unsigned BitIndex, bool AllowMixed) {
  if (RA == ATTR_MIXED && AllowMixed)
    Filters.emplace_back(*this, StartBit, BitIndex - StartBit, true);
  else if (RA == ATTR_ALL_SET && !AllowMixed)
    Filters.emplace_back(*this, StartBit, BitIndex - StartBit, false);
}

// FilterProcessor scans the well-known encoding bits of the instructions and
// builds up a list of candidate filters.  It chooses the best filter and
// recursively descends down the decoding tree.
bool FilterChooser::filterProcessor(bool AllowMixed, bool Greedy) {
  Filters.clear();
  BestIndex = -1;
  unsigned numInstructions = Opcodes.size();

  assert(numInstructions && "Filter created with no instructions");

  // No further filtering is necessary.
  if (numInstructions == 1)
    return true;

  // Heuristics.  See also doFilter()'s "Heuristics" comment when num of
  // instructions is 3.
  if (AllowMixed && !Greedy) {
    assert(numInstructions == 3);

    for (unsigned i = 0; i < Opcodes.size(); ++i) {
      std::vector<unsigned> StartBits;
      std::vector<unsigned> EndBits;
      std::vector<uint64_t> FieldVals;
      insn_t Insn;

      insnWithID(Insn, Opcodes[i]);

      // Look for islands of undecoded bits of any instruction.
      if (getIslands(StartBits, EndBits, FieldVals, Insn) > 0) {
        // Found an instruction with island(s).  Now just assign a filter.
        runSingleFilter(StartBits[0], EndBits[0] - StartBits[0] + 1, true);
        return true;
      }
    }
  }

  unsigned BitIndex;

  // We maintain BIT_WIDTH copies of the bitAttrs automaton.
  // The automaton consumes the corresponding bit from each
  // instruction.
  //
  //   Input symbols: 0, 1, and _ (unset).
  //   States:        NONE, FILTERED, ALL_SET, ALL_UNSET, and MIXED.
  //   Initial state: NONE.
  //
  // (NONE) ------- [01] -> (ALL_SET)
  // (NONE) ------- _ ----> (ALL_UNSET)
  // (ALL_SET) ---- [01] -> (ALL_SET)
  // (ALL_SET) ---- _ ----> (MIXED)
  // (ALL_UNSET) -- [01] -> (MIXED)
  // (ALL_UNSET) -- _ ----> (ALL_UNSET)
  // (MIXED) ------ . ----> (MIXED)
  // (FILTERED)---- . ----> (FILTERED)

  std::vector<bitAttr_t> bitAttrs;

  // FILTERED bit positions provide no entropy and are not worthy of pursuing.
  // Filter::recurse() set either BIT_TRUE or BIT_FALSE for each position.
  for (BitIndex = 0; BitIndex < BitWidth; ++BitIndex)
    if (FilterBitValues[BitIndex] == BIT_TRUE ||
        FilterBitValues[BitIndex] == BIT_FALSE)
      bitAttrs.push_back(ATTR_FILTERED);
    else
      bitAttrs.push_back(ATTR_NONE);

  for (unsigned InsnIndex = 0; InsnIndex < numInstructions; ++InsnIndex) {
    insn_t insn;

    insnWithID(insn, Opcodes[InsnIndex]);

    for (BitIndex = 0; BitIndex < BitWidth; ++BitIndex) {
      switch (bitAttrs[BitIndex]) {
      case ATTR_NONE:
        if (insn[BitIndex] == BIT_UNSET)
          bitAttrs[BitIndex] = ATTR_ALL_UNSET;
        else
          bitAttrs[BitIndex] = ATTR_ALL_SET;
        break;
      case ATTR_ALL_SET:
        if (insn[BitIndex] == BIT_UNSET)
          bitAttrs[BitIndex] = ATTR_MIXED;
        break;
      case ATTR_ALL_UNSET:
        if (insn[BitIndex] != BIT_UNSET)
          bitAttrs[BitIndex] = ATTR_MIXED;
        break;
      case ATTR_MIXED:
      case ATTR_FILTERED:
        break;
      }
    }
  }

  // The regionAttr automaton consumes the bitAttrs automatons' state,
  // lowest-to-highest.
  //
  //   Input symbols: F(iltered), (all_)S(et), (all_)U(nset), M(ixed)
  //   States:        NONE, ALL_SET, MIXED
  //   Initial state: NONE
  //
  // (NONE) ----- F --> (NONE)
  // (NONE) ----- S --> (ALL_SET)     ; and set region start
  // (NONE) ----- U --> (NONE)
  // (NONE) ----- M --> (MIXED)       ; and set region start
  // (ALL_SET) -- F --> (NONE)        ; and report an ALL_SET region
  // (ALL_SET) -- S --> (ALL_SET)
  // (ALL_SET) -- U --> (NONE)        ; and report an ALL_SET region
  // (ALL_SET) -- M --> (MIXED)       ; and report an ALL_SET region
  // (MIXED) ---- F --> (NONE)        ; and report a MIXED region
  // (MIXED) ---- S --> (ALL_SET)     ; and report a MIXED region
  // (MIXED) ---- U --> (NONE)        ; and report a MIXED region
  // (MIXED) ---- M --> (MIXED)

  bitAttr_t RA = ATTR_NONE;
  unsigned StartBit = 0;

  for (BitIndex = 0; BitIndex < BitWidth; ++BitIndex) {
    bitAttr_t bitAttr = bitAttrs[BitIndex];

    assert(bitAttr != ATTR_NONE && "Bit without attributes");

    switch (RA) {
    case ATTR_NONE:
      switch (bitAttr) {
      case ATTR_FILTERED:
        break;
      case ATTR_ALL_SET:
        StartBit = BitIndex;
        RA = ATTR_ALL_SET;
        break;
      case ATTR_ALL_UNSET:
        break;
      case ATTR_MIXED:
        StartBit = BitIndex;
        RA = ATTR_MIXED;
        break;
      default:
        llvm_unreachable("Unexpected bitAttr!");
      }
      break;
    case ATTR_ALL_SET:
      switch (bitAttr) {
      case ATTR_FILTERED:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        RA = ATTR_NONE;
        break;
      case ATTR_ALL_SET:
        break;
      case ATTR_ALL_UNSET:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        RA = ATTR_NONE;
        break;
      case ATTR_MIXED:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        StartBit = BitIndex;
        RA = ATTR_MIXED;
        break;
      default:
        llvm_unreachable("Unexpected bitAttr!");
      }
      break;
    case ATTR_MIXED:
      switch (bitAttr) {
      case ATTR_FILTERED:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        StartBit = BitIndex;
        RA = ATTR_NONE;
        break;
      case ATTR_ALL_SET:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        StartBit = BitIndex;
        RA = ATTR_ALL_SET;
        break;
      case ATTR_ALL_UNSET:
        reportRegion(RA, StartBit, BitIndex, AllowMixed);
        RA = ATTR_NONE;
        break;
      case ATTR_MIXED:
        break;
      default:
        llvm_unreachable("Unexpected bitAttr!");
      }
      break;
    case ATTR_ALL_UNSET:
      llvm_unreachable("regionAttr state machine has no ATTR_UNSET state");
    case ATTR_FILTERED:
      llvm_unreachable("regionAttr state machine has no ATTR_FILTERED state");
    }
  }

  // At the end, if we're still in ALL_SET or MIXED states, report a region
  switch (RA) {
  case ATTR_NONE:
    break;
  case ATTR_FILTERED:
    break;
  case ATTR_ALL_SET:
    reportRegion(RA, StartBit, BitIndex, AllowMixed);
    break;
  case ATTR_ALL_UNSET:
    break;
  case ATTR_MIXED:
    reportRegion(RA, StartBit, BitIndex, AllowMixed);
    break;
  }

  // We have finished with the filter processings.  Now it's time to choose
  // the best performing filter.
  BestIndex = 0;
  bool AllUseless = true;
  unsigned BestScore = 0;

  for (unsigned i = 0, e = Filters.size(); i != e; ++i) {
    unsigned Usefulness = Filters[i].usefulness();

    if (Usefulness)
      AllUseless = false;

    if (Usefulness > BestScore) {
      BestIndex = i;
      BestScore = Usefulness;
    }
  }

  if (!AllUseless)
    bestFilter().recurse();

  return !AllUseless;
} // end of FilterChooser::filterProcessor(bool)

// Decides on the best configuration of filter(s) to use in order to decode
// the instructions.  A conflict of instructions may occur, in which case we
// dump the conflict set to the standard error.
void FilterChooser::doFilter() {
  unsigned Num = Opcodes.size();
  assert(Num && "FilterChooser created with no instructions");

  // Try regions of consecutive known bit values first.
  if (filterProcessor(false))
    return;

  // Then regions of mixed bits (both known and unitialized bit values allowed).
  if (filterProcessor(true))
    return;

  // Heuristics to cope with conflict set {t2CMPrs, t2SUBSrr, t2SUBSrs} where
  // no single instruction for the maximum ATTR_MIXED region Inst{14-4} has a
  // well-known encoding pattern.  In such case, we backtrack and scan for the
  // the very first consecutive ATTR_ALL_SET region and assign a filter to it.
  if (Num == 3 && filterProcessor(true, false))
    return;

  // If we come to here, the instruction decoding has failed.
  // Set the BestIndex to -1 to indicate so.
  BestIndex = -1;
}

// emitTableEntries - Emit state machine entries to decode our share of
// instructions.
void FilterChooser::emitTableEntries(DecoderTableInfo &TableInfo) const {
  if (Opcodes.size() == 1) {
    // There is only one instruction in the set, which is great!
    // Call emitSingletonDecoder() to see whether there are any remaining
    // encodings bits.
    emitSingletonTableEntry(TableInfo, Opcodes[0]);
    return;
  }

  // Choose the best filter to do the decodings!
  if (BestIndex != -1) {
    const Filter &Best = Filters[BestIndex];
    if (Best.getNumFiltered() == 1)
      emitSingletonTableEntry(TableInfo, Best);
    else
      Best.emitTableEntry(TableInfo);
    return;
  }

  // We don't know how to decode these instructions!  Dump the
  // conflict set and bail.

  // Print out useful conflict information for postmortem analysis.
  errs() << "Decoding Conflict:\n";

  dumpStack(errs(), "\t\t");

  for (unsigned i = 0; i < Opcodes.size(); ++i) {
    errs() << '\t' << AllInstructions[Opcodes[i]] << " ";
    dumpBits(errs(),
             getBitsField(*AllInstructions[Opcodes[i]].EncodingDef, "Inst"));
    errs() << '\n';
  }
}

static std::string findOperandDecoderMethod(TypedInit *TI) {
  std::string Decoder;

  Record *Record = cast<DefInit>(TI)->getDef();

  RecordVal *DecoderString = Record->getValue("DecoderMethod");
  StringInit *String = DecoderString ?
    dyn_cast<StringInit>(DecoderString->getValue()) : nullptr;
  if (String) {
    Decoder = String->getValue();
    if (!Decoder.empty())
      return Decoder;
  }

  if (Record->isSubClassOf("RegisterOperand"))
    Record = Record->getValueAsDef("RegClass");

  if (Record->isSubClassOf("RegisterClass")) {
    Decoder = "Decode" + Record->getName().str() + "RegisterClass";
  } else if (Record->isSubClassOf("PointerLikeRegClass")) {
    Decoder = "DecodePointerLikeRegClass" +
      utostr(Record->getValueAsInt("RegClassKind"));
  }

  return Decoder;
}

static bool populateInstruction(CodeGenTarget &Target,
                       const CodeGenInstruction &CGI, unsigned Opc,
                       std::map<unsigned, std::vector<OperandInfo>> &Operands){
  const Record &Def = *CGI.TheDef;
  // If all the bit positions are not specified; do not decode this instruction.
  // We are bound to fail!  For proper disassembly, the well-known encoding bits
  // of the instruction must be fully specified.

  BitsInit &Bits = getBitsField(Def, "Inst");
  if (Bits.allInComplete()) return false;

  std::vector<OperandInfo> InsnOperands;

  // If the instruction has specified a custom decoding hook, use that instead
  // of trying to auto-generate the decoder.
  StringRef InstDecoder = Def.getValueAsString("DecoderMethod");
  if (InstDecoder != "") {
    bool HasCompleteInstDecoder = Def.getValueAsBit("hasCompleteDecoder");
    InsnOperands.push_back(OperandInfo(InstDecoder, HasCompleteInstDecoder));
    Operands[Opc] = InsnOperands;
    return true;
  }

  // Generate a description of the operand of the instruction that we know
  // how to decode automatically.
  // FIXME: We'll need to have a way to manually override this as needed.

  // Gather the outputs/inputs of the instruction, so we can find their
  // positions in the encoding.  This assumes for now that they appear in the
  // MCInst in the order that they're listed.
  std::vector<std::pair<Init*, StringRef>> InOutOperands;
  DagInit *Out  = Def.getValueAsDag("OutOperandList");
  DagInit *In  = Def.getValueAsDag("InOperandList");
  for (unsigned i = 0; i < Out->getNumArgs(); ++i)
    InOutOperands.push_back(std::make_pair(Out->getArg(i),
                                           Out->getArgNameStr(i)));
  for (unsigned i = 0; i < In->getNumArgs(); ++i)
    InOutOperands.push_back(std::make_pair(In->getArg(i),
                                           In->getArgNameStr(i)));

  // Search for tied operands, so that we can correctly instantiate
  // operands that are not explicitly represented in the encoding.
  std::map<std::string, std::string> TiedNames;
  for (unsigned i = 0; i < CGI.Operands.size(); ++i) {
    int tiedTo = CGI.Operands[i].getTiedRegister();
    if (tiedTo != -1) {
      std::pair<unsigned, unsigned> SO =
        CGI.Operands.getSubOperandNumber(tiedTo);
      TiedNames[InOutOperands[i].second] = InOutOperands[SO.first].second;
      TiedNames[InOutOperands[SO.first].second] = InOutOperands[i].second;
    }
  }

  std::map<std::string, std::vector<OperandInfo>> NumberedInsnOperands;
  std::set<std::string> NumberedInsnOperandsNoTie;
  if (Target.getInstructionSet()->
        getValueAsBit("decodePositionallyEncodedOperands")) {
    const std::vector<RecordVal> &Vals = Def.getValues();
    unsigned NumberedOp = 0;

    std::set<unsigned> NamedOpIndices;
    if (Target.getInstructionSet()->
         getValueAsBit("noNamedPositionallyEncodedOperands"))
      // Collect the set of operand indices that might correspond to named
      // operand, and skip these when assigning operands based on position.
      for (unsigned i = 0, e = Vals.size(); i != e; ++i) {
        unsigned OpIdx;
        if (!CGI.Operands.hasOperandNamed(Vals[i].getName(), OpIdx))
          continue;

        NamedOpIndices.insert(OpIdx);
      }

    for (unsigned i = 0, e = Vals.size(); i != e; ++i) {
      // Ignore fixed fields in the record, we're looking for values like:
      //    bits<5> RST = { ?, ?, ?, ?, ? };
      if (Vals[i].getPrefix() || Vals[i].getValue()->isComplete())
        continue;

      // Determine if Vals[i] actually contributes to the Inst encoding.
      unsigned bi = 0;
      for (; bi < Bits.getNumBits(); ++bi) {
        VarInit *Var = nullptr;
        VarBitInit *BI = dyn_cast<VarBitInit>(Bits.getBit(bi));
        if (BI)
          Var = dyn_cast<VarInit>(BI->getBitVar());
        else
          Var = dyn_cast<VarInit>(Bits.getBit(bi));

        if (Var && Var->getName() == Vals[i].getName())
          break;
      }

      if (bi == Bits.getNumBits())
        continue;

      // Skip variables that correspond to explicitly-named operands.
      unsigned OpIdx;
      if (CGI.Operands.hasOperandNamed(Vals[i].getName(), OpIdx))
        continue;

      // Get the bit range for this operand:
      unsigned bitStart = bi++, bitWidth = 1;
      for (; bi < Bits.getNumBits(); ++bi) {
        VarInit *Var = nullptr;
        VarBitInit *BI = dyn_cast<VarBitInit>(Bits.getBit(bi));
        if (BI)
          Var = dyn_cast<VarInit>(BI->getBitVar());
        else
          Var = dyn_cast<VarInit>(Bits.getBit(bi));

        if (!Var)
          break;

        if (Var->getName() != Vals[i].getName())
          break;

        ++bitWidth;
      }

      unsigned NumberOps = CGI.Operands.size();
      while (NumberedOp < NumberOps &&
             (CGI.Operands.isFlatOperandNotEmitted(NumberedOp) ||
              (!NamedOpIndices.empty() && NamedOpIndices.count(
                CGI.Operands.getSubOperandNumber(NumberedOp).first))))
        ++NumberedOp;

      OpIdx = NumberedOp++;

      // OpIdx now holds the ordered operand number of Vals[i].
      std::pair<unsigned, unsigned> SO =
        CGI.Operands.getSubOperandNumber(OpIdx);
      const std::string &Name = CGI.Operands[SO.first].Name;

      LLVM_DEBUG(dbgs() << "Numbered operand mapping for " << Def.getName()
                        << ": " << Name << "(" << SO.first << ", " << SO.second
                        << ") => " << Vals[i].getName() << "\n");

      std::string Decoder;
      Record *TypeRecord = CGI.Operands[SO.first].Rec;

      RecordVal *DecoderString = TypeRecord->getValue("DecoderMethod");
      StringInit *String = DecoderString ?
        dyn_cast<StringInit>(DecoderString->getValue()) : nullptr;
      if (String && String->getValue() != "")
        Decoder = String->getValue();

      if (Decoder == "" &&
          CGI.Operands[SO.first].MIOperandInfo &&
          CGI.Operands[SO.first].MIOperandInfo->getNumArgs()) {
        Init *Arg = CGI.Operands[SO.first].MIOperandInfo->
                      getArg(SO.second);
        if (DefInit *DI = cast<DefInit>(Arg))
          TypeRecord = DI->getDef();
      }

      bool isReg = false;
      if (TypeRecord->isSubClassOf("RegisterOperand"))
        TypeRecord = TypeRecord->getValueAsDef("RegClass");
      if (TypeRecord->isSubClassOf("RegisterClass")) {
        Decoder = "Decode" + TypeRecord->getName().str() + "RegisterClass";
        isReg = true;
      } else if (TypeRecord->isSubClassOf("PointerLikeRegClass")) {
        Decoder = "DecodePointerLikeRegClass" +
                  utostr(TypeRecord->getValueAsInt("RegClassKind"));
        isReg = true;
      }

      DecoderString = TypeRecord->getValue("DecoderMethod");
      String = DecoderString ?
        dyn_cast<StringInit>(DecoderString->getValue()) : nullptr;
      if (!isReg && String && String->getValue() != "")
        Decoder = String->getValue();

      RecordVal *HasCompleteDecoderVal =
        TypeRecord->getValue("hasCompleteDecoder");
      BitInit *HasCompleteDecoderBit = HasCompleteDecoderVal ?
        dyn_cast<BitInit>(HasCompleteDecoderVal->getValue()) : nullptr;
      bool HasCompleteDecoder = HasCompleteDecoderBit ?
        HasCompleteDecoderBit->getValue() : true;

      OperandInfo OpInfo(Decoder, HasCompleteDecoder);
      OpInfo.addField(bitStart, bitWidth, 0);

      NumberedInsnOperands[Name].push_back(OpInfo);

      // FIXME: For complex operands with custom decoders we can't handle tied
      // sub-operands automatically. Skip those here and assume that this is
      // fixed up elsewhere.
      if (CGI.Operands[SO.first].MIOperandInfo &&
          CGI.Operands[SO.first].MIOperandInfo->getNumArgs() > 1 &&
          String && String->getValue() != "")
        NumberedInsnOperandsNoTie.insert(Name);
    }
  }

  // For each operand, see if we can figure out where it is encoded.
  for (const auto &Op : InOutOperands) {
    if (!NumberedInsnOperands[Op.second].empty()) {
      InsnOperands.insert(InsnOperands.end(),
                          NumberedInsnOperands[Op.second].begin(),
                          NumberedInsnOperands[Op.second].end());
      continue;
    }
    if (!NumberedInsnOperands[TiedNames[Op.second]].empty()) {
      if (!NumberedInsnOperandsNoTie.count(TiedNames[Op.second])) {
        // Figure out to which (sub)operand we're tied.
        unsigned i = CGI.Operands.getOperandNamed(TiedNames[Op.second]);
        int tiedTo = CGI.Operands[i].getTiedRegister();
        if (tiedTo == -1) {
          i = CGI.Operands.getOperandNamed(Op.second);
          tiedTo = CGI.Operands[i].getTiedRegister();
        }

        if (tiedTo != -1) {
          std::pair<unsigned, unsigned> SO =
            CGI.Operands.getSubOperandNumber(tiedTo);

          InsnOperands.push_back(NumberedInsnOperands[TiedNames[Op.second]]
                                   [SO.second]);
        }
      }
      continue;
    }

    TypedInit *TI = cast<TypedInit>(Op.first);

    // At this point, we can locate the decoder field, but we need to know how
    // to interpret it.  As a first step, require the target to provide
    // callbacks for decoding register classes.
    std::string Decoder = findOperandDecoderMethod(TI);
    Record *TypeRecord = cast<DefInit>(TI)->getDef();

    RecordVal *HasCompleteDecoderVal =
      TypeRecord->getValue("hasCompleteDecoder");
    BitInit *HasCompleteDecoderBit = HasCompleteDecoderVal ?
      dyn_cast<BitInit>(HasCompleteDecoderVal->getValue()) : nullptr;
    bool HasCompleteDecoder = HasCompleteDecoderBit ?
      HasCompleteDecoderBit->getValue() : true;

    OperandInfo OpInfo(Decoder, HasCompleteDecoder);
    unsigned Base = ~0U;
    unsigned Width = 0;
    unsigned Offset = 0;

    for (unsigned bi = 0; bi < Bits.getNumBits(); ++bi) {
      VarInit *Var = nullptr;
      VarBitInit *BI = dyn_cast<VarBitInit>(Bits.getBit(bi));
      if (BI)
        Var = dyn_cast<VarInit>(BI->getBitVar());
      else
        Var = dyn_cast<VarInit>(Bits.getBit(bi));

      if (!Var) {
        if (Base != ~0U) {
          OpInfo.addField(Base, Width, Offset);
          Base = ~0U;
          Width = 0;
          Offset = 0;
        }
        continue;
      }

      if (Var->getName() != Op.second &&
          Var->getName() != TiedNames[Op.second]) {
        if (Base != ~0U) {
          OpInfo.addField(Base, Width, Offset);
          Base = ~0U;
          Width = 0;
          Offset = 0;
        }
        continue;
      }

      if (Base == ~0U) {
        Base = bi;
        Width = 1;
        Offset = BI ? BI->getBitNum() : 0;
      } else if (BI && BI->getBitNum() != Offset + Width) {
        OpInfo.addField(Base, Width, Offset);
        Base = bi;
        Width = 1;
        Offset = BI->getBitNum();
      } else {
        ++Width;
      }
    }

    if (Base != ~0U)
      OpInfo.addField(Base, Width, Offset);

    if (OpInfo.numFields() > 0)
      InsnOperands.push_back(OpInfo);
  }

  Operands[Opc] = InsnOperands;

#if 0
  LLVM_DEBUG({
      // Dumps the instruction encoding bits.
      dumpBits(errs(), Bits);

      errs() << '\n';

      // Dumps the list of operand info.
      for (unsigned i = 0, e = CGI.Operands.size(); i != e; ++i) {
        const CGIOperandList::OperandInfo &Info = CGI.Operands[i];
        const std::string &OperandName = Info.Name;
        const Record &OperandDef = *Info.Rec;

        errs() << "\t" << OperandName << " (" << OperandDef.getName() << ")\n";
      }
    });
#endif

  return true;
}

// emitFieldFromInstruction - Emit the templated helper function
// fieldFromInstruction().
// On Windows we make sure that this function is not inlined when
// using the VS compiler. It has a bug which causes the function
// to be optimized out in some circustances. See llvm.org/pr38292
static void emitFieldFromInstruction(formatted_raw_ostream &OS) {
  OS << "// Helper functions for extracting fields from encoded instructions.\n"
     << "// InsnType must either be integral or an APInt-like object that "
        "must:\n"
     << "// * Have a static const max_size_in_bits equal to the number of bits "
        "in the\n"
     << "//   encoding.\n"
     << "// * be default-constructible and copy-constructible\n"
     << "// * be constructible from a uint64_t\n"
     << "// * be constructible from an APInt (this can be private)\n"
     << "// * Support getBitsSet(loBit, hiBit)\n"
     << "// * be convertible to uint64_t\n"
     << "// * Support the ~, &, ==, !=, and |= operators with other objects of "
        "the same type\n"
     << "// * Support shift (<<, >>) with signed and unsigned integers on the "
        "RHS\n"
     << "// * Support put (<<) to raw_ostream&\n"
     << "template<typename InsnType>\n"
     << "#if defined(_MSC_VER) && !defined(__clang__)\n"
     << "__declspec(noinline)\n"
     << "#endif\n"
     << "static InsnType fieldFromInstruction(InsnType insn, unsigned "
        "startBit,\n"
     << "                                     unsigned numBits, "
        "std::true_type) {\n"
     << "  assert(startBit + numBits <= 64 && \"Cannot support >64-bit "
        "extractions!\");\n"
     << "  assert(startBit + numBits <= (sizeof(InsnType) * 8) &&\n"
     << "         \"Instruction field out of bounds!\");\n"
     << "  InsnType fieldMask;\n"
     << "  if (numBits == sizeof(InsnType) * 8)\n"
     << "    fieldMask = (InsnType)(-1LL);\n"
     << "  else\n"
     << "    fieldMask = (((InsnType)1 << numBits) - 1) << startBit;\n"
     << "  return (insn & fieldMask) >> startBit;\n"
     << "}\n"
     << "\n"
     << "template<typename InsnType>\n"
     << "static InsnType fieldFromInstruction(InsnType insn, unsigned "
        "startBit,\n"
     << "                                     unsigned numBits, "
        "std::false_type) {\n"
     << "  assert(startBit + numBits <= InsnType::max_size_in_bits && "
        "\"Instruction field out of bounds!\");\n"
     << "  InsnType fieldMask = InsnType::getBitsSet(0, numBits);\n"
     << "  return (insn >> startBit) & fieldMask;\n"
     << "}\n"
     << "\n"
     << "template<typename InsnType>\n"
     << "static InsnType fieldFromInstruction(InsnType insn, unsigned "
        "startBit,\n"
     << "                                     unsigned numBits) {\n"
     << "  return fieldFromInstruction(insn, startBit, numBits, "
        "std::is_integral<InsnType>());\n"
     << "}\n\n";
}

// emitDecodeInstruction - Emit the templated helper function
// decodeInstruction().
static void emitDecodeInstruction(formatted_raw_ostream &OS) {
  OS << "template<typename InsnType>\n"
     << "static DecodeStatus decodeInstruction(const uint8_t DecodeTable[], "
        "MCInst &MI,\n"
     << "                                      InsnType insn, uint64_t "
        "Address,\n"
     << "                                      const void *DisAsm,\n"
     << "                                      const MCSubtargetInfo &STI) {\n"
     << "  const FeatureBitset& Bits = STI.getFeatureBits();\n"
     << "\n"
     << "  const uint8_t *Ptr = DecodeTable;\n"
     << "  uint32_t CurFieldValue = 0;\n"
     << "  DecodeStatus S = MCDisassembler::Success;\n"
     << "  while (true) {\n"
     << "    ptrdiff_t Loc = Ptr - DecodeTable;\n"
     << "    switch (*Ptr) {\n"
     << "    default:\n"
     << "      errs() << Loc << \": Unexpected decode table opcode!\\n\";\n"
     << "      return MCDisassembler::Fail;\n"
     << "    case MCD::OPC_ExtractField: {\n"
     << "      unsigned Start = *++Ptr;\n"
     << "      unsigned Len = *++Ptr;\n"
     << "      ++Ptr;\n"
     << "      CurFieldValue = fieldFromInstruction(insn, Start, Len);\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_ExtractField(\" << Start << "
        "\", \"\n"
     << "                   << Len << \"): \" << CurFieldValue << \"\\n\");\n"
     << "      break;\n"
     << "    }\n"
     << "    case MCD::OPC_FilterValue: {\n"
     << "      // Decode the field value.\n"
     << "      unsigned Len;\n"
     << "      InsnType Val = decodeULEB128(++Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      // NumToSkip is a plain 24-bit integer.\n"
     << "      unsigned NumToSkip = *Ptr++;\n"
     << "      NumToSkip |= (*Ptr++) << 8;\n"
     << "      NumToSkip |= (*Ptr++) << 16;\n"
     << "\n"
     << "      // Perform the filter operation.\n"
     << "      if (Val != CurFieldValue)\n"
     << "        Ptr += NumToSkip;\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_FilterValue(\" << Val << "
        "\", \" << NumToSkip\n"
     << "                   << \"): \" << ((Val != CurFieldValue) ? \"FAIL:\" "
        ": \"PASS:\")\n"
     << "                   << \" continuing at \" << (Ptr - DecodeTable) << "
        "\"\\n\");\n"
     << "\n"
     << "      break;\n"
     << "    }\n"
     << "    case MCD::OPC_CheckField: {\n"
     << "      unsigned Start = *++Ptr;\n"
     << "      unsigned Len = *++Ptr;\n"
     << "      InsnType FieldValue = fieldFromInstruction(insn, Start, Len);\n"
     << "      // Decode the field value.\n"
     << "      uint32_t ExpectedValue = decodeULEB128(++Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      // NumToSkip is a plain 24-bit integer.\n"
     << "      unsigned NumToSkip = *Ptr++;\n"
     << "      NumToSkip |= (*Ptr++) << 8;\n"
     << "      NumToSkip |= (*Ptr++) << 16;\n"
     << "\n"
     << "      // If the actual and expected values don't match, skip.\n"
     << "      if (ExpectedValue != FieldValue)\n"
     << "        Ptr += NumToSkip;\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_CheckField(\" << Start << "
        "\", \"\n"
     << "                   << Len << \", \" << ExpectedValue << \", \" << "
        "NumToSkip\n"
     << "                   << \"): FieldValue = \" << FieldValue << \", "
        "ExpectedValue = \"\n"
     << "                   << ExpectedValue << \": \"\n"
     << "                   << ((ExpectedValue == FieldValue) ? \"PASS\\n\" : "
        "\"FAIL\\n\"));\n"
     << "      break;\n"
     << "    }\n"
     << "    case MCD::OPC_CheckPredicate: {\n"
     << "      unsigned Len;\n"
     << "      // Decode the Predicate Index value.\n"
     << "      unsigned PIdx = decodeULEB128(++Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      // NumToSkip is a plain 24-bit integer.\n"
     << "      unsigned NumToSkip = *Ptr++;\n"
     << "      NumToSkip |= (*Ptr++) << 8;\n"
     << "      NumToSkip |= (*Ptr++) << 16;\n"
     << "      // Check the predicate.\n"
     << "      bool Pred;\n"
     << "      if (!(Pred = checkDecoderPredicate(PIdx, Bits)))\n"
     << "        Ptr += NumToSkip;\n"
     << "      (void)Pred;\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_CheckPredicate(\" << PIdx "
        "<< \"): \"\n"
     << "            << (Pred ? \"PASS\\n\" : \"FAIL\\n\"));\n"
     << "\n"
     << "      break;\n"
     << "    }\n"
     << "    case MCD::OPC_Decode: {\n"
     << "      unsigned Len;\n"
     << "      // Decode the Opcode value.\n"
     << "      unsigned Opc = decodeULEB128(++Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      unsigned DecodeIdx = decodeULEB128(Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "\n"
     << "      MI.clear();\n"
     << "      MI.setOpcode(Opc);\n"
     << "      bool DecodeComplete;\n"
     << "      S = decodeToMCInst(S, DecodeIdx, insn, MI, Address, DisAsm, "
        "DecodeComplete);\n"
     << "      assert(DecodeComplete);\n"
     << "\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_Decode: opcode \" << Opc\n"
     << "                   << \", using decoder \" << DecodeIdx << \": \"\n"
     << "                   << (S != MCDisassembler::Fail ? \"PASS\" : "
        "\"FAIL\") << \"\\n\");\n"
     << "      return S;\n"
     << "    }\n"
     << "    case MCD::OPC_TryDecode: {\n"
     << "      unsigned Len;\n"
     << "      // Decode the Opcode value.\n"
     << "      unsigned Opc = decodeULEB128(++Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      unsigned DecodeIdx = decodeULEB128(Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      // NumToSkip is a plain 24-bit integer.\n"
     << "      unsigned NumToSkip = *Ptr++;\n"
     << "      NumToSkip |= (*Ptr++) << 8;\n"
     << "      NumToSkip |= (*Ptr++) << 16;\n"
     << "\n"
     << "      // Perform the decode operation.\n"
     << "      MCInst TmpMI;\n"
     << "      TmpMI.setOpcode(Opc);\n"
     << "      bool DecodeComplete;\n"
     << "      S = decodeToMCInst(S, DecodeIdx, insn, TmpMI, Address, DisAsm, "
        "DecodeComplete);\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_TryDecode: opcode \" << "
        "Opc\n"
     << "                   << \", using decoder \" << DecodeIdx << \": \");\n"
     << "\n"
     << "      if (DecodeComplete) {\n"
     << "        // Decoding complete.\n"
     << "        LLVM_DEBUG(dbgs() << (S != MCDisassembler::Fail ? \"PASS\" : "
        "\"FAIL\") << \"\\n\");\n"
     << "        MI = TmpMI;\n"
     << "        return S;\n"
     << "      } else {\n"
     << "        assert(S == MCDisassembler::Fail);\n"
     << "        // If the decoding was incomplete, skip.\n"
     << "        Ptr += NumToSkip;\n"
     << "        LLVM_DEBUG(dbgs() << \"FAIL: continuing at \" << (Ptr - "
        "DecodeTable) << \"\\n\");\n"
     << "        // Reset decode status. This also drops a SoftFail status "
        "that could be\n"
     << "        // set before the decode attempt.\n"
     << "        S = MCDisassembler::Success;\n"
     << "      }\n"
     << "      break;\n"
     << "    }\n"
     << "    case MCD::OPC_SoftFail: {\n"
     << "      // Decode the mask values.\n"
     << "      unsigned Len;\n"
     << "      InsnType PositiveMask = decodeULEB128(++Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      InsnType NegativeMask = decodeULEB128(Ptr, &Len);\n"
     << "      Ptr += Len;\n"
     << "      bool Fail = (insn & PositiveMask) || (~insn & NegativeMask);\n"
     << "      if (Fail)\n"
     << "        S = MCDisassembler::SoftFail;\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_SoftFail: \" << (Fail ? "
        "\"FAIL\\n\":\"PASS\\n\"));\n"
     << "      break;\n"
     << "    }\n"
     << "    case MCD::OPC_Fail: {\n"
     << "      LLVM_DEBUG(dbgs() << Loc << \": OPC_Fail\\n\");\n"
     << "      return MCDisassembler::Fail;\n"
     << "    }\n"
     << "    }\n"
     << "  }\n"
     << "  llvm_unreachable(\"bogosity detected in disassembler state "
        "machine!\");\n"
     << "}\n\n";
}

// Emits disassembler code for instruction decoding.
void FixedLenDecoderEmitter::run(raw_ostream &o) {
  formatted_raw_ostream OS(o);
  OS << "#include \"llvm/MC/MCInst.h\"\n";
  OS << "#include \"llvm/Support/Debug.h\"\n";
  OS << "#include \"llvm/Support/DataTypes.h\"\n";
  OS << "#include \"llvm/Support/LEB128.h\"\n";
  OS << "#include \"llvm/Support/raw_ostream.h\"\n";
  OS << "#include <assert.h>\n";
  OS << '\n';
  OS << "namespace llvm {\n\n";

  emitFieldFromInstruction(OS);

  Target.reverseBitsForLittleEndianEncoding();

  // Parameterize the decoders based on namespace and instruction width.
  const auto &NumberedInstructions = Target.getInstructionsByEnumValue();
  NumberedEncodings.reserve(NumberedInstructions.size());
  for (const auto &NumberedInstruction : NumberedInstructions)
    NumberedEncodings.emplace_back(NumberedInstruction->TheDef, NumberedInstruction);

  std::map<std::pair<std::string, unsigned>,
           std::vector<unsigned>> OpcMap;
  std::map<unsigned, std::vector<OperandInfo>> Operands;

  for (unsigned i = 0; i < NumberedEncodings.size(); ++i) {
    const CodeGenInstruction *Inst = NumberedEncodings[i].Inst;
    const Record *Def = Inst->TheDef;
    unsigned Size = Def->getValueAsInt("Size");
    if (Def->getValueAsString("Namespace") == "TargetOpcode" ||
        Def->getValueAsBit("isPseudo") ||
        Def->getValueAsBit("isAsmParserOnly") ||
        Def->getValueAsBit("isCodeGenOnly"))
      continue;

    StringRef DecoderNamespace = Def->getValueAsString("DecoderNamespace");

    if (Size) {
      if (populateInstruction(Target, *Inst, i, Operands)) {
        OpcMap[std::make_pair(DecoderNamespace, Size)].push_back(i);
      }
    }
  }

  DecoderTableInfo TableInfo;
  for (const auto &Opc : OpcMap) {
    // Emit the decoder for this namespace+width combination.
    ArrayRef<EncodingAndInst> NumberedEncodingsRef(NumberedEncodings.data(),
                                                   NumberedEncodings.size());
    FilterChooser FC(NumberedEncodingsRef, Opc.second, Operands,
                     8 * Opc.first.second, this);

    // The decode table is cleared for each top level decoder function. The
    // predicates and decoders themselves, however, are shared across all
    // decoders to give more opportunities for uniqueing.
    TableInfo.Table.clear();
    TableInfo.FixupStack.clear();
    TableInfo.Table.reserve(16384);
    TableInfo.FixupStack.emplace_back();
    FC.emitTableEntries(TableInfo);
    // Any NumToSkip fixups in the top level scope can resolve to the
    // OPC_Fail at the end of the table.
    assert(TableInfo.FixupStack.size() == 1 && "fixup stack phasing error!");
    // Resolve any NumToSkip fixups in the current scope.
    resolveTableFixups(TableInfo.Table, TableInfo.FixupStack.back(),
                       TableInfo.Table.size());
    TableInfo.FixupStack.clear();

    TableInfo.Table.push_back(MCD::OPC_Fail);

    // Print the table to the output stream.
    emitTable(OS, TableInfo.Table, 0, FC.getBitWidth(), Opc.first.first);
    OS.flush();
  }

  // Emit the predicate function.
  emitPredicateFunction(OS, TableInfo.Predicates, 0);

  // Emit the decoder function.
  emitDecoderFunction(OS, TableInfo.Decoders, 0);

  // Emit the main entry point for the decoder, decodeInstruction().
  emitDecodeInstruction(OS);

  OS << "\n} // End llvm namespace\n";
}

namespace llvm {

void EmitFixedLenDecoder(RecordKeeper &RK, raw_ostream &OS,
                         const std::string &PredicateNamespace,
                         const std::string &GPrefix,
                         const std::string &GPostfix, const std::string &ROK,
                         const std::string &RFail, const std::string &L) {
  FixedLenDecoderEmitter(RK, PredicateNamespace, GPrefix, GPostfix,
                         ROK, RFail, L).run(OS);
}

} // end namespace llvm
