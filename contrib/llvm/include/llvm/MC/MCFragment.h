//===- MCFragment.h - Fragment type hierarchy -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCFRAGMENT_H
#define LLVM_MC_MCFRAGMENT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <utility>

namespace llvm {

class MCSection;
class MCSubtargetInfo;
class MCSymbol;

class MCFragment : public ilist_node_with_parent<MCFragment, MCSection> {
  friend class MCAsmLayout;

public:
  enum FragmentType : uint8_t {
    FT_Align,
    FT_Data,
    FT_CompactEncodedInst,
    FT_Fill,
    FT_Relaxable,
    FT_Org,
    FT_Dwarf,
    FT_DwarfFrame,
    FT_LEB,
    FT_Padding,
    FT_SymbolId,
    FT_CVInlineLines,
    FT_CVDefRange,
    FT_Dummy
  };

private:
  FragmentType Kind;

protected:
  bool HasInstructions;

private:
  /// LayoutOrder - The layout order of this fragment.
  unsigned LayoutOrder;

  /// The data for the section this fragment is in.
  MCSection *Parent;

  /// Atom - The atom this fragment is in, as represented by its defining
  /// symbol.
  const MCSymbol *Atom;

  /// \name Assembler Backend Data
  /// @{
  //
  // FIXME: This could all be kept private to the assembler implementation.

  /// Offset - The offset of this fragment in its section. This is ~0 until
  /// initialized.
  uint64_t Offset;

  /// @}

protected:
  MCFragment(FragmentType Kind, bool HasInstructions,
             MCSection *Parent = nullptr);

  ~MCFragment();

public:
  MCFragment() = delete;
  MCFragment(const MCFragment &) = delete;
  MCFragment &operator=(const MCFragment &) = delete;

  /// Destroys the current fragment.
  ///
  /// This must be used instead of delete as MCFragment is non-virtual.
  /// This method will dispatch to the appropriate subclass.
  void destroy();

  FragmentType getKind() const { return Kind; }

  MCSection *getParent() const { return Parent; }
  void setParent(MCSection *Value) { Parent = Value; }

  const MCSymbol *getAtom() const { return Atom; }
  void setAtom(const MCSymbol *Value) { Atom = Value; }

  unsigned getLayoutOrder() const { return LayoutOrder; }
  void setLayoutOrder(unsigned Value) { LayoutOrder = Value; }

  /// Does this fragment have instructions emitted into it? By default
  /// this is false, but specific fragment types may set it to true.
  bool hasInstructions() const { return HasInstructions; }

  /// Return true if given frgment has FT_Dummy type.
  bool isDummy() const { return Kind == FT_Dummy; }

  void dump() const;
};

class MCDummyFragment : public MCFragment {
public:
  explicit MCDummyFragment(MCSection *Sec) : MCFragment(FT_Dummy, false, Sec) {}

  static bool classof(const MCFragment *F) { return F->getKind() == FT_Dummy; }
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data.
///
class MCEncodedFragment : public MCFragment {
  /// Should this fragment be aligned to the end of a bundle?
  bool AlignToBundleEnd = false;

  uint8_t BundlePadding = 0;

protected:
  MCEncodedFragment(MCFragment::FragmentType FType, bool HasInstructions,
                    MCSection *Sec)
      : MCFragment(FType, HasInstructions, Sec) {}

  /// STI - The MCSubtargetInfo in effect when the instruction was encoded.
  /// must be non-null for instructions.
  const MCSubtargetInfo *STI = nullptr;

public:
  static bool classof(const MCFragment *F) {
    MCFragment::FragmentType Kind = F->getKind();
    switch (Kind) {
    default:
      return false;
    case MCFragment::FT_Relaxable:
    case MCFragment::FT_CompactEncodedInst:
    case MCFragment::FT_Data:
    case MCFragment::FT_Dwarf:
      return true;
    }
  }

  /// Should this fragment be placed at the end of an aligned bundle?
  bool alignToBundleEnd() const { return AlignToBundleEnd; }
  void setAlignToBundleEnd(bool V) { AlignToBundleEnd = V; }

  /// Get the padding size that must be inserted before this fragment.
  /// Used for bundling. By default, no padding is inserted.
  /// Note that padding size is restricted to 8 bits. This is an optimization
  /// to reduce the amount of space used for each fragment. In practice, larger
  /// padding should never be required.
  uint8_t getBundlePadding() const { return BundlePadding; }

  /// Set the padding size for this fragment. By default it's a no-op,
  /// and only some fragments have a meaningful implementation.
  void setBundlePadding(uint8_t N) { BundlePadding = N; }

  /// Retrieve the MCSubTargetInfo in effect when the instruction was encoded.
  /// Guaranteed to be non-null if hasInstructions() == true
  const MCSubtargetInfo *getSubtargetInfo() const { return STI; }

  /// Record that the fragment contains instructions with the MCSubtargetInfo in
  /// effect when the instruction was encoded.
  void setHasInstructions(const MCSubtargetInfo &STI) {
    HasInstructions = true;
    this->STI = &STI;
  }
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data.
///
template<unsigned ContentsSize>
class MCEncodedFragmentWithContents : public MCEncodedFragment {
  SmallVector<char, ContentsSize> Contents;

protected:
  MCEncodedFragmentWithContents(MCFragment::FragmentType FType,
                                bool HasInstructions,
                                MCSection *Sec)
      : MCEncodedFragment(FType, HasInstructions, Sec) {}

public:
  SmallVectorImpl<char> &getContents() { return Contents; }
  const SmallVectorImpl<char> &getContents() const { return Contents; }
};

/// Interface implemented by fragments that contain encoded instructions and/or
/// data and also have fixups registered.
///
template<unsigned ContentsSize, unsigned FixupsSize>
class MCEncodedFragmentWithFixups :
  public MCEncodedFragmentWithContents<ContentsSize> {

  /// Fixups - The list of fixups in this fragment.
  SmallVector<MCFixup, FixupsSize> Fixups;

protected:
  MCEncodedFragmentWithFixups(MCFragment::FragmentType FType,
                              bool HasInstructions,
                              MCSection *Sec)
      : MCEncodedFragmentWithContents<ContentsSize>(FType, HasInstructions,
                                                    Sec) {}

public:

  using const_fixup_iterator = SmallVectorImpl<MCFixup>::const_iterator;
  using fixup_iterator = SmallVectorImpl<MCFixup>::iterator;

  SmallVectorImpl<MCFixup> &getFixups() { return Fixups; }
  const SmallVectorImpl<MCFixup> &getFixups() const { return Fixups; }

  fixup_iterator fixup_begin() { return Fixups.begin(); }
  const_fixup_iterator fixup_begin() const { return Fixups.begin(); }

  fixup_iterator fixup_end() { return Fixups.end(); }
  const_fixup_iterator fixup_end() const { return Fixups.end(); }

  static bool classof(const MCFragment *F) {
    MCFragment::FragmentType Kind = F->getKind();
    return Kind == MCFragment::FT_Relaxable || Kind == MCFragment::FT_Data ||
           Kind == MCFragment::FT_CVDefRange || Kind == MCFragment::FT_Dwarf;;
  }
};

/// Fragment for data and encoded instructions.
///
class MCDataFragment : public MCEncodedFragmentWithFixups<32, 4> {
public:
  MCDataFragment(MCSection *Sec = nullptr)
      : MCEncodedFragmentWithFixups<32, 4>(FT_Data, false, Sec) {}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Data;
  }
};

/// This is a compact (memory-size-wise) fragment for holding an encoded
/// instruction (non-relaxable) that has no fixups registered. When applicable,
/// it can be used instead of MCDataFragment and lead to lower memory
/// consumption.
///
class MCCompactEncodedInstFragment : public MCEncodedFragmentWithContents<4> {
public:
  MCCompactEncodedInstFragment(MCSection *Sec = nullptr)
      : MCEncodedFragmentWithContents(FT_CompactEncodedInst, true, Sec) {
  }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CompactEncodedInst;
  }
};

/// A relaxable fragment holds on to its MCInst, since it may need to be
/// relaxed during the assembler layout and relaxation stage.
///
class MCRelaxableFragment : public MCEncodedFragmentWithFixups<8, 1> {

  /// Inst - The instruction this is a fragment for.
  MCInst Inst;

public:
  MCRelaxableFragment(const MCInst &Inst, const MCSubtargetInfo &STI,
                      MCSection *Sec = nullptr)
      : MCEncodedFragmentWithFixups(FT_Relaxable, true, Sec),
        Inst(Inst) { this->STI = &STI; }

  const MCInst &getInst() const { return Inst; }
  void setInst(const MCInst &Value) { Inst = Value; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Relaxable;
  }
};

class MCAlignFragment : public MCFragment {
  /// Alignment - The alignment to ensure, in bytes.
  unsigned Alignment;

  /// EmitNops - Flag to indicate that (optimal) NOPs should be emitted instead
  /// of using the provided value. The exact interpretation of this flag is
  /// target dependent.
  bool EmitNops : 1;

  /// Value - Value to use for filling padding bytes.
  int64_t Value;

  /// ValueSize - The size of the integer (in bytes) of \p Value.
  unsigned ValueSize;

  /// MaxBytesToEmit - The maximum number of bytes to emit; if the alignment
  /// cannot be satisfied in this width then this fragment is ignored.
  unsigned MaxBytesToEmit;

public:
  MCAlignFragment(unsigned Alignment, int64_t Value, unsigned ValueSize,
                  unsigned MaxBytesToEmit, MCSection *Sec = nullptr)
      : MCFragment(FT_Align, false, Sec), Alignment(Alignment), EmitNops(false),
        Value(Value), ValueSize(ValueSize), MaxBytesToEmit(MaxBytesToEmit) {}

  /// \name Accessors
  /// @{

  unsigned getAlignment() const { return Alignment; }

  int64_t getValue() const { return Value; }

  unsigned getValueSize() const { return ValueSize; }

  unsigned getMaxBytesToEmit() const { return MaxBytesToEmit; }

  bool hasEmitNops() const { return EmitNops; }
  void setEmitNops(bool Value) { EmitNops = Value; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Align;
  }
};

/// Fragment for adding required padding.
/// This fragment is always inserted before an instruction, and holds that
/// instruction as context information (as well as a mask of kinds) for
/// determining the padding size.
///
class MCPaddingFragment : public MCFragment {
  /// A mask containing all the kinds relevant to this fragment. i.e. the i'th
  /// bit will be set iff kind i is relevant to this fragment.
  uint64_t PaddingPoliciesMask;
  /// A boolean indicating if this fragment will actually hold padding. If its
  /// value is false, then this fragment serves only as a placeholder,
  /// containing data to assist other insertion point in their decision making.
  bool IsInsertionPoint;

  uint64_t Size;

  struct MCInstInfo {
    bool IsInitialized;
    MCInst Inst;
    /// A boolean indicating whether the instruction pointed by this fragment is
    /// a fixed size instruction or a relaxable instruction held by a
    /// MCRelaxableFragment.
    bool IsImmutableSizedInst;
    union {
      /// If the instruction is a fixed size instruction, hold its size.
      size_t InstSize;
      /// Otherwise, hold a pointer to the MCRelaxableFragment holding it.
      MCRelaxableFragment *InstFragment;
    };
  };
  MCInstInfo InstInfo;

public:
  static const uint64_t PFK_None = UINT64_C(0);

  enum MCPaddingFragmentKind {
    // values 0-7 are reserved for future target independet values.

    FirstTargetPerfNopFragmentKind = 8,

    /// Limit range of target MCPerfNopFragment kinds to fit in uint64_t
    MaxTargetPerfNopFragmentKind = 63
  };

  MCPaddingFragment(MCSection *Sec = nullptr)
      : MCFragment(FT_Padding, false, Sec), PaddingPoliciesMask(PFK_None),
        IsInsertionPoint(false), Size(UINT64_C(0)),
        InstInfo({false, MCInst(), false, {0}}) {}

  bool isInsertionPoint() const { return IsInsertionPoint; }
  void setAsInsertionPoint() { IsInsertionPoint = true; }
  uint64_t getPaddingPoliciesMask() const { return PaddingPoliciesMask; }
  void setPaddingPoliciesMask(uint64_t Value) { PaddingPoliciesMask = Value; }
  bool hasPaddingPolicy(uint64_t PolicyMask) const {
    assert(isPowerOf2_64(PolicyMask) &&
           "Policy mask must contain exactly one policy");
    return (getPaddingPoliciesMask() & PolicyMask) != PFK_None;
  }
  const MCInst &getInst() const {
    assert(isInstructionInitialized() && "Fragment has no instruction!");
    return InstInfo.Inst;
  }
  size_t getInstSize() const {
    assert(isInstructionInitialized() && "Fragment has no instruction!");
    if (InstInfo.IsImmutableSizedInst)
      return InstInfo.InstSize;
    assert(InstInfo.InstFragment != nullptr &&
           "Must have a valid InstFragment to retrieve InstSize from");
    return InstInfo.InstFragment->getContents().size();
  }
  void setInstAndInstSize(const MCInst &Inst, size_t InstSize) {
	InstInfo.IsInitialized = true;
    InstInfo.IsImmutableSizedInst = true;
    InstInfo.Inst = Inst;
    InstInfo.InstSize = InstSize;
  }
  void setInstAndInstFragment(const MCInst &Inst,
                              MCRelaxableFragment *InstFragment) {
    InstInfo.IsInitialized = true;
    InstInfo.IsImmutableSizedInst = false;
    InstInfo.Inst = Inst;
    InstInfo.InstFragment = InstFragment;
  }
  uint64_t getSize() const { return Size; }
  void setSize(uint64_t Value) { Size = Value; }
  bool isInstructionInitialized() const { return InstInfo.IsInitialized; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Padding;
  }
};

class MCFillFragment : public MCFragment {
  /// Value to use for filling bytes.
  uint64_t Value;
  uint8_t ValueSize;
  /// The number of bytes to insert.
  const MCExpr &NumValues;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

public:
  MCFillFragment(uint64_t Value, uint8_t VSize, const MCExpr &NumValues,
                 SMLoc Loc, MCSection *Sec = nullptr)
      : MCFragment(FT_Fill, false, Sec), Value(Value), ValueSize(VSize),
        NumValues(NumValues), Loc(Loc) {}

  uint64_t getValue() const { return Value; }
  uint8_t getValueSize() const { return ValueSize; }
  const MCExpr &getNumValues() const { return NumValues; }

  SMLoc getLoc() const { return Loc; }

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Fill;
  }
};

class MCOrgFragment : public MCFragment {
  /// The offset this fragment should start at.
  const MCExpr *Offset;

  /// Value to use for filling bytes.
  int8_t Value;

  /// Source location of the directive that this fragment was created for.
  SMLoc Loc;

public:
  MCOrgFragment(const MCExpr &Offset, int8_t Value, SMLoc Loc,
                MCSection *Sec = nullptr)
      : MCFragment(FT_Org, false, Sec), Offset(&Offset), Value(Value), Loc(Loc) {}

  /// \name Accessors
  /// @{

  const MCExpr &getOffset() const { return *Offset; }

  uint8_t getValue() const { return Value; }

  SMLoc getLoc() const { return Loc; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Org;
  }
};

class MCLEBFragment : public MCFragment {
  /// Value - The value this fragment should contain.
  const MCExpr *Value;

  /// IsSigned - True if this is a sleb128, false if uleb128.
  bool IsSigned;

  SmallString<8> Contents;

public:
  MCLEBFragment(const MCExpr &Value_, bool IsSigned_, MCSection *Sec = nullptr)
      : MCFragment(FT_LEB, false, Sec), Value(&Value_), IsSigned(IsSigned_) {
    Contents.push_back(0);
  }

  /// \name Accessors
  /// @{

  const MCExpr &getValue() const { return *Value; }

  bool isSigned() const { return IsSigned; }

  SmallString<8> &getContents() { return Contents; }
  const SmallString<8> &getContents() const { return Contents; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_LEB;
  }
};

class MCDwarfLineAddrFragment : public MCEncodedFragmentWithFixups<8, 1> {
  /// LineDelta - the value of the difference between the two line numbers
  /// between two .loc dwarf directives.
  int64_t LineDelta;

  /// AddrDelta - The expression for the difference of the two symbols that
  /// make up the address delta between two .loc dwarf directives.
  const MCExpr *AddrDelta;

public:
  MCDwarfLineAddrFragment(int64_t LineDelta, const MCExpr &AddrDelta,
                          MCSection *Sec = nullptr)
      : MCEncodedFragmentWithFixups<8, 1>(FT_Dwarf, false, Sec),
        LineDelta(LineDelta), AddrDelta(&AddrDelta) {}

  /// \name Accessors
  /// @{

  int64_t getLineDelta() const { return LineDelta; }

  const MCExpr &getAddrDelta() const { return *AddrDelta; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_Dwarf;
  }
};

class MCDwarfCallFrameFragment : public MCFragment {
  /// AddrDelta - The expression for the difference of the two symbols that
  /// make up the address delta between two .cfi_* dwarf directives.
  const MCExpr *AddrDelta;

  SmallString<8> Contents;

public:
  MCDwarfCallFrameFragment(const MCExpr &AddrDelta, MCSection *Sec = nullptr)
      : MCFragment(FT_DwarfFrame, false, Sec), AddrDelta(&AddrDelta) {
    Contents.push_back(0);
  }

  /// \name Accessors
  /// @{

  const MCExpr &getAddrDelta() const { return *AddrDelta; }

  SmallString<8> &getContents() { return Contents; }
  const SmallString<8> &getContents() const { return Contents; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_DwarfFrame;
  }
};

/// Represents a symbol table index fragment.
class MCSymbolIdFragment : public MCFragment {
  const MCSymbol *Sym;

public:
  MCSymbolIdFragment(const MCSymbol *Sym, MCSection *Sec = nullptr)
      : MCFragment(FT_SymbolId, false, Sec), Sym(Sym) {}

  /// \name Accessors
  /// @{

  const MCSymbol *getSymbol() { return Sym; }
  const MCSymbol *getSymbol() const { return Sym; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_SymbolId;
  }
};

/// Fragment representing the binary annotations produced by the
/// .cv_inline_linetable directive.
class MCCVInlineLineTableFragment : public MCFragment {
  unsigned SiteFuncId;
  unsigned StartFileId;
  unsigned StartLineNum;
  const MCSymbol *FnStartSym;
  const MCSymbol *FnEndSym;
  SmallString<8> Contents;

  /// CodeViewContext has the real knowledge about this format, so let it access
  /// our members.
  friend class CodeViewContext;

public:
  MCCVInlineLineTableFragment(unsigned SiteFuncId, unsigned StartFileId,
                              unsigned StartLineNum, const MCSymbol *FnStartSym,
                              const MCSymbol *FnEndSym,
                              MCSection *Sec = nullptr)
      : MCFragment(FT_CVInlineLines, false, Sec), SiteFuncId(SiteFuncId),
        StartFileId(StartFileId), StartLineNum(StartLineNum),
        FnStartSym(FnStartSym), FnEndSym(FnEndSym) {}

  /// \name Accessors
  /// @{

  const MCSymbol *getFnStartSym() const { return FnStartSym; }
  const MCSymbol *getFnEndSym() const { return FnEndSym; }

  SmallString<8> &getContents() { return Contents; }
  const SmallString<8> &getContents() const { return Contents; }

  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CVInlineLines;
  }
};

/// Fragment representing the .cv_def_range directive.
class MCCVDefRangeFragment : public MCEncodedFragmentWithFixups<32, 4> {
  SmallVector<std::pair<const MCSymbol *, const MCSymbol *>, 2> Ranges;
  SmallString<32> FixedSizePortion;

  /// CodeViewContext has the real knowledge about this format, so let it access
  /// our members.
  friend class CodeViewContext;

public:
  MCCVDefRangeFragment(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion, MCSection *Sec = nullptr)
      : MCEncodedFragmentWithFixups<32, 4>(FT_CVDefRange, false, Sec),
        Ranges(Ranges.begin(), Ranges.end()),
        FixedSizePortion(FixedSizePortion) {}

  /// \name Accessors
  /// @{
  ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> getRanges() const {
    return Ranges;
  }

  StringRef getFixedSizePortion() const { return FixedSizePortion; }
  /// @}

  static bool classof(const MCFragment *F) {
    return F->getKind() == MCFragment::FT_CVDefRange;
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCFRAGMENT_H
