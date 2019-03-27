//===- lib/MC/MCObjectStreamer.cpp - Object File MCStreamer Interface -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
using namespace llvm;

MCObjectStreamer::MCObjectStreamer(MCContext &Context,
                                   std::unique_ptr<MCAsmBackend> TAB,
                                   std::unique_ptr<MCObjectWriter> OW,
                                   std::unique_ptr<MCCodeEmitter> Emitter)
    : MCStreamer(Context),
      Assembler(llvm::make_unique<MCAssembler>(
          Context, std::move(TAB), std::move(Emitter), std::move(OW))),
      EmitEHFrame(true), EmitDebugFrame(false) {}

MCObjectStreamer::~MCObjectStreamer() {}

// AssemblerPtr is used for evaluation of expressions and causes
// difference between asm and object outputs. Return nullptr to in
// inline asm mode to limit divergence to assembly inputs.
MCAssembler *MCObjectStreamer::getAssemblerPtr() {
  if (getUseAssemblerInfoForParsing())
    return Assembler.get();
  return nullptr;
}

void MCObjectStreamer::flushPendingLabels(MCFragment *F, uint64_t FOffset) {
  if (PendingLabels.empty())
    return;
  if (!F) {
    F = new MCDataFragment();
    MCSection *CurSection = getCurrentSectionOnly();
    CurSection->getFragmentList().insert(CurInsertionPoint, F);
    F->setParent(CurSection);
  }
  for (MCSymbol *Sym : PendingLabels) {
    Sym->setFragment(F);
    Sym->setOffset(FOffset);
  }
  PendingLabels.clear();
}

// When fixup's offset is a forward declared label, e.g.:
//
//   .reloc 1f, R_MIPS_JALR, foo
// 1: nop
//
// postpone adding it to Fixups vector until the label is defined and its offset
// is known.
void MCObjectStreamer::resolvePendingFixups() {
  for (PendingMCFixup &PendingFixup : PendingFixups) {
    if (!PendingFixup.Sym || PendingFixup.Sym->isUndefined ()) {
      getContext().reportError(PendingFixup.Fixup.getLoc(),
                               "unresolved relocation offset");
      continue;
    }
    flushPendingLabels(PendingFixup.DF, PendingFixup.DF->getContents().size());
    PendingFixup.Fixup.setOffset(PendingFixup.Sym->getOffset());
    PendingFixup.DF->getFixups().push_back(PendingFixup.Fixup);
  }
  PendingFixups.clear();
}

// As a compile-time optimization, avoid allocating and evaluating an MCExpr
// tree for (Hi - Lo) when Hi and Lo are offsets into the same fragment.
static Optional<uint64_t>
absoluteSymbolDiff(MCAssembler &Asm, const MCSymbol *Hi, const MCSymbol *Lo) {
  assert(Hi && Lo);
  if (Asm.getBackendPtr()->requiresDiffExpressionRelocations())
    return None;

  if (!Hi->getFragment() || Hi->getFragment() != Lo->getFragment() ||
      Hi->isVariable() || Lo->isVariable())
    return None;

  return Hi->getOffset() - Lo->getOffset();
}

void MCObjectStreamer::emitAbsoluteSymbolDiff(const MCSymbol *Hi,
                                              const MCSymbol *Lo,
                                              unsigned Size) {
  if (Optional<uint64_t> Diff = absoluteSymbolDiff(getAssembler(), Hi, Lo)) {
    EmitIntValue(*Diff, Size);
    return;
  }
  MCStreamer::emitAbsoluteSymbolDiff(Hi, Lo, Size);
}

void MCObjectStreamer::emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                                       const MCSymbol *Lo) {
  if (Optional<uint64_t> Diff = absoluteSymbolDiff(getAssembler(), Hi, Lo)) {
    EmitULEB128IntValue(*Diff);
    return;
  }
  MCStreamer::emitAbsoluteSymbolDiffAsULEB128(Hi, Lo);
}

void MCObjectStreamer::reset() {
  if (Assembler)
    Assembler->reset();
  CurInsertionPoint = MCSection::iterator();
  EmitEHFrame = true;
  EmitDebugFrame = false;
  PendingLabels.clear();
  MCStreamer::reset();
}

void MCObjectStreamer::EmitFrames(MCAsmBackend *MAB) {
  if (!getNumFrameInfos())
    return;

  if (EmitEHFrame)
    MCDwarfFrameEmitter::Emit(*this, MAB, true);

  if (EmitDebugFrame)
    MCDwarfFrameEmitter::Emit(*this, MAB, false);
}

MCFragment *MCObjectStreamer::getCurrentFragment() const {
  assert(getCurrentSectionOnly() && "No current section!");

  if (CurInsertionPoint != getCurrentSectionOnly()->getFragmentList().begin())
    return &*std::prev(CurInsertionPoint);

  return nullptr;
}

static bool CanReuseDataFragment(const MCDataFragment &F,
                                 const MCAssembler &Assembler,
                                 const MCSubtargetInfo *STI) {
  if (!F.hasInstructions())
    return true;
  // When bundling is enabled, we don't want to add data to a fragment that
  // already has instructions (see MCELFStreamer::EmitInstToData for details)
  if (Assembler.isBundlingEnabled())
    return Assembler.getRelaxAll();
  // If the subtarget is changed mid fragment we start a new fragment to record
  // the new STI.
  return !STI || F.getSubtargetInfo() == STI;
}

MCDataFragment *
MCObjectStreamer::getOrCreateDataFragment(const MCSubtargetInfo *STI) {
  MCDataFragment *F = dyn_cast_or_null<MCDataFragment>(getCurrentFragment());
  if (!F || !CanReuseDataFragment(*F, *Assembler, STI)) {
    F = new MCDataFragment();
    insert(F);
  }
  return F;
}

MCPaddingFragment *MCObjectStreamer::getOrCreatePaddingFragment() {
  MCPaddingFragment *F =
      dyn_cast_or_null<MCPaddingFragment>(getCurrentFragment());
  if (!F) {
    F = new MCPaddingFragment();
    insert(F);
  }
  return F;
}

void MCObjectStreamer::visitUsedSymbol(const MCSymbol &Sym) {
  Assembler->registerSymbol(Sym);
}

void MCObjectStreamer::EmitCFISections(bool EH, bool Debug) {
  MCStreamer::EmitCFISections(EH, Debug);
  EmitEHFrame = EH;
  EmitDebugFrame = Debug;
}

void MCObjectStreamer::EmitValueImpl(const MCExpr *Value, unsigned Size,
                                     SMLoc Loc) {
  MCStreamer::EmitValueImpl(Value, Size, Loc);
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  MCDwarfLineEntry::Make(this, getCurrentSectionOnly());

  // Avoid fixups when possible.
  int64_t AbsValue;
  if (Value->evaluateAsAbsolute(AbsValue, getAssemblerPtr())) {
    if (!isUIntN(8 * Size, AbsValue) && !isIntN(8 * Size, AbsValue)) {
      getContext().reportError(
          Loc, "value evaluated as " + Twine(AbsValue) + " is out of range.");
      return;
    }
    EmitIntValue(AbsValue, Size);
    return;
  }
  DF->getFixups().push_back(
      MCFixup::create(DF->getContents().size(), Value,
                      MCFixup::getKindForSize(Size, false), Loc));
  DF->getContents().resize(DF->getContents().size() + Size, 0);
}

MCSymbol *MCObjectStreamer::EmitCFILabel() {
  MCSymbol *Label = getContext().createTempSymbol("cfi", true);
  EmitLabel(Label);
  return Label;
}

void MCObjectStreamer::EmitCFIStartProcImpl(MCDwarfFrameInfo &Frame) {
  // We need to create a local symbol to avoid relocations.
  Frame.Begin = getContext().createTempSymbol();
  EmitLabel(Frame.Begin);
}

void MCObjectStreamer::EmitCFIEndProcImpl(MCDwarfFrameInfo &Frame) {
  Frame.End = getContext().createTempSymbol();
  EmitLabel(Frame.End);
}

void MCObjectStreamer::EmitLabel(MCSymbol *Symbol, SMLoc Loc) {
  MCStreamer::EmitLabel(Symbol, Loc);

  getAssembler().registerSymbol(*Symbol);

  // If there is a current fragment, mark the symbol as pointing into it.
  // Otherwise queue the label and set its fragment pointer when we emit the
  // next fragment.
  auto *F = dyn_cast_or_null<MCDataFragment>(getCurrentFragment());
  if (F && !(getAssembler().isBundlingEnabled() &&
             getAssembler().getRelaxAll())) {
    Symbol->setFragment(F);
    Symbol->setOffset(F->getContents().size());
  } else {
    PendingLabels.push_back(Symbol);
  }
}

void MCObjectStreamer::EmitLabel(MCSymbol *Symbol, SMLoc Loc, MCFragment *F) {
  MCStreamer::EmitLabel(Symbol, Loc);
  getAssembler().registerSymbol(*Symbol);
  auto *DF = dyn_cast_or_null<MCDataFragment>(F);
  if (DF)
    Symbol->setFragment(F);
  else
    PendingLabels.push_back(Symbol);
}

void MCObjectStreamer::EmitULEB128Value(const MCExpr *Value) {
  int64_t IntValue;
  if (Value->evaluateAsAbsolute(IntValue, getAssemblerPtr())) {
    EmitULEB128IntValue(IntValue);
    return;
  }
  insert(new MCLEBFragment(*Value, false));
}

void MCObjectStreamer::EmitSLEB128Value(const MCExpr *Value) {
  int64_t IntValue;
  if (Value->evaluateAsAbsolute(IntValue, getAssemblerPtr())) {
    EmitSLEB128IntValue(IntValue);
    return;
  }
  insert(new MCLEBFragment(*Value, true));
}

void MCObjectStreamer::EmitWeakReference(MCSymbol *Alias,
                                         const MCSymbol *Symbol) {
  report_fatal_error("This file format doesn't support weak aliases.");
}

void MCObjectStreamer::ChangeSection(MCSection *Section,
                                     const MCExpr *Subsection) {
  changeSectionImpl(Section, Subsection);
}

bool MCObjectStreamer::changeSectionImpl(MCSection *Section,
                                         const MCExpr *Subsection) {
  assert(Section && "Cannot switch to a null section!");
  flushPendingLabels(nullptr);
  getContext().clearDwarfLocSeen();

  bool Created = getAssembler().registerSection(*Section);

  int64_t IntSubsection = 0;
  if (Subsection &&
      !Subsection->evaluateAsAbsolute(IntSubsection, getAssemblerPtr()))
    report_fatal_error("Cannot evaluate subsection number");
  if (IntSubsection < 0 || IntSubsection > 8192)
    report_fatal_error("Subsection number out of range");
  CurInsertionPoint =
      Section->getSubsectionInsertionPoint(unsigned(IntSubsection));
  return Created;
}

void MCObjectStreamer::EmitAssignment(MCSymbol *Symbol, const MCExpr *Value) {
  getAssembler().registerSymbol(*Symbol);
  MCStreamer::EmitAssignment(Symbol, Value);
}

bool MCObjectStreamer::mayHaveInstructions(MCSection &Sec) const {
  return Sec.hasInstructions();
}

void MCObjectStreamer::EmitInstruction(const MCInst &Inst,
                                       const MCSubtargetInfo &STI, bool) {
  getAssembler().getBackend().handleCodePaddingInstructionBegin(Inst);
  EmitInstructionImpl(Inst, STI);
  getAssembler().getBackend().handleCodePaddingInstructionEnd(Inst);
}

void MCObjectStreamer::EmitInstructionImpl(const MCInst &Inst,
                                           const MCSubtargetInfo &STI) {
  MCStreamer::EmitInstruction(Inst, STI);

  MCSection *Sec = getCurrentSectionOnly();
  Sec->setHasInstructions(true);

  // Now that a machine instruction has been assembled into this section, make
  // a line entry for any .loc directive that has been seen.
  MCDwarfLineEntry::Make(this, getCurrentSectionOnly());

  // If this instruction doesn't need relaxation, just emit it as data.
  MCAssembler &Assembler = getAssembler();
  if (!Assembler.getBackend().mayNeedRelaxation(Inst, STI)) {
    EmitInstToData(Inst, STI);
    return;
  }

  // Otherwise, relax and emit it as data if either:
  // - The RelaxAll flag was passed
  // - Bundling is enabled and this instruction is inside a bundle-locked
  //   group. We want to emit all such instructions into the same data
  //   fragment.
  if (Assembler.getRelaxAll() ||
      (Assembler.isBundlingEnabled() && Sec->isBundleLocked())) {
    MCInst Relaxed;
    getAssembler().getBackend().relaxInstruction(Inst, STI, Relaxed);
    while (getAssembler().getBackend().mayNeedRelaxation(Relaxed, STI))
      getAssembler().getBackend().relaxInstruction(Relaxed, STI, Relaxed);
    EmitInstToData(Relaxed, STI);
    return;
  }

  // Otherwise emit to a separate fragment.
  EmitInstToFragment(Inst, STI);
}

void MCObjectStreamer::EmitInstToFragment(const MCInst &Inst,
                                          const MCSubtargetInfo &STI) {
  if (getAssembler().getRelaxAll() && getAssembler().isBundlingEnabled())
    llvm_unreachable("All instructions should have already been relaxed");

  // Always create a new, separate fragment here, because its size can change
  // during relaxation.
  MCRelaxableFragment *IF = new MCRelaxableFragment(Inst, STI);
  insert(IF);

  SmallString<128> Code;
  raw_svector_ostream VecOS(Code);
  getAssembler().getEmitter().encodeInstruction(Inst, VecOS, IF->getFixups(),
                                                STI);
  IF->getContents().append(Code.begin(), Code.end());
}

#ifndef NDEBUG
static const char *const BundlingNotImplementedMsg =
  "Aligned bundling is not implemented for this object format";
#endif

void MCObjectStreamer::EmitBundleAlignMode(unsigned AlignPow2) {
  llvm_unreachable(BundlingNotImplementedMsg);
}

void MCObjectStreamer::EmitBundleLock(bool AlignToEnd) {
  llvm_unreachable(BundlingNotImplementedMsg);
}

void MCObjectStreamer::EmitBundleUnlock() {
  llvm_unreachable(BundlingNotImplementedMsg);
}

void MCObjectStreamer::EmitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                             unsigned Column, unsigned Flags,
                                             unsigned Isa,
                                             unsigned Discriminator,
                                             StringRef FileName) {
  // In case we see two .loc directives in a row, make sure the
  // first one gets a line entry.
  MCDwarfLineEntry::Make(this, getCurrentSectionOnly());

  this->MCStreamer::EmitDwarfLocDirective(FileNo, Line, Column, Flags,
                                          Isa, Discriminator, FileName);
}

static const MCExpr *buildSymbolDiff(MCObjectStreamer &OS, const MCSymbol *A,
                                     const MCSymbol *B) {
  MCContext &Context = OS.getContext();
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  const MCExpr *ARef = MCSymbolRefExpr::create(A, Variant, Context);
  const MCExpr *BRef = MCSymbolRefExpr::create(B, Variant, Context);
  const MCExpr *AddrDelta =
      MCBinaryExpr::create(MCBinaryExpr::Sub, ARef, BRef, Context);
  return AddrDelta;
}

static void emitDwarfSetLineAddr(MCObjectStreamer &OS,
                                 MCDwarfLineTableParams Params,
                                 int64_t LineDelta, const MCSymbol *Label,
                                 int PointerSize) {
  // emit the sequence to set the address
  OS.EmitIntValue(dwarf::DW_LNS_extended_op, 1);
  OS.EmitULEB128IntValue(PointerSize + 1);
  OS.EmitIntValue(dwarf::DW_LNE_set_address, 1);
  OS.EmitSymbolValue(Label, PointerSize);

  // emit the sequence for the LineDelta (from 1) and a zero address delta.
  MCDwarfLineAddr::Emit(&OS, Params, LineDelta, 0);
}

void MCObjectStreamer::EmitDwarfAdvanceLineAddr(int64_t LineDelta,
                                                const MCSymbol *LastLabel,
                                                const MCSymbol *Label,
                                                unsigned PointerSize) {
  if (!LastLabel) {
    emitDwarfSetLineAddr(*this, Assembler->getDWARFLinetableParams(), LineDelta,
                         Label, PointerSize);
    return;
  }
  const MCExpr *AddrDelta = buildSymbolDiff(*this, Label, LastLabel);
  int64_t Res;
  if (AddrDelta->evaluateAsAbsolute(Res, getAssemblerPtr())) {
    MCDwarfLineAddr::Emit(this, Assembler->getDWARFLinetableParams(), LineDelta,
                          Res);
    return;
  }
  insert(new MCDwarfLineAddrFragment(LineDelta, *AddrDelta));
}

void MCObjectStreamer::EmitDwarfAdvanceFrameAddr(const MCSymbol *LastLabel,
                                                 const MCSymbol *Label) {
  const MCExpr *AddrDelta = buildSymbolDiff(*this, Label, LastLabel);
  int64_t Res;
  if (AddrDelta->evaluateAsAbsolute(Res, getAssemblerPtr())) {
    MCDwarfFrameEmitter::EmitAdvanceLoc(*this, Res);
    return;
  }
  insert(new MCDwarfCallFrameFragment(*AddrDelta));
}

void MCObjectStreamer::EmitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                          unsigned Line, unsigned Column,
                                          bool PrologueEnd, bool IsStmt,
                                          StringRef FileName, SMLoc Loc) {
  // Validate the directive.
  if (!checkCVLocSection(FunctionId, FileNo, Loc))
    return;

  // Emit a label at the current position and record it in the CodeViewContext.
  MCSymbol *LineSym = getContext().createTempSymbol();
  EmitLabel(LineSym);
  getContext().getCVContext().recordCVLoc(getContext(), LineSym, FunctionId,
                                          FileNo, Line, Column, PrologueEnd,
                                          IsStmt);
}

void MCObjectStreamer::EmitCVLinetableDirective(unsigned FunctionId,
                                                const MCSymbol *Begin,
                                                const MCSymbol *End) {
  getContext().getCVContext().emitLineTableForFunction(*this, FunctionId, Begin,
                                                       End);
  this->MCStreamer::EmitCVLinetableDirective(FunctionId, Begin, End);
}

void MCObjectStreamer::EmitCVInlineLinetableDirective(
    unsigned PrimaryFunctionId, unsigned SourceFileId, unsigned SourceLineNum,
    const MCSymbol *FnStartSym, const MCSymbol *FnEndSym) {
  getContext().getCVContext().emitInlineLineTableForFunction(
      *this, PrimaryFunctionId, SourceFileId, SourceLineNum, FnStartSym,
      FnEndSym);
  this->MCStreamer::EmitCVInlineLinetableDirective(
      PrimaryFunctionId, SourceFileId, SourceLineNum, FnStartSym, FnEndSym);
}

void MCObjectStreamer::EmitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    StringRef FixedSizePortion) {
  MCFragment *Frag =
      getContext().getCVContext().emitDefRange(*this, Ranges, FixedSizePortion);
  // Attach labels that were pending before we created the defrange fragment to
  // the beginning of the new fragment.
  flushPendingLabels(Frag, 0);
  this->MCStreamer::EmitCVDefRangeDirective(Ranges, FixedSizePortion);
}

void MCObjectStreamer::EmitCVStringTableDirective() {
  getContext().getCVContext().emitStringTable(*this);
}
void MCObjectStreamer::EmitCVFileChecksumsDirective() {
  getContext().getCVContext().emitFileChecksums(*this);
}

void MCObjectStreamer::EmitCVFileChecksumOffsetDirective(unsigned FileNo) {
  getContext().getCVContext().emitFileChecksumOffset(*this, FileNo);
}

void MCObjectStreamer::EmitBytes(StringRef Data) {
  MCDwarfLineEntry::Make(this, getCurrentSectionOnly());
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());
  DF->getContents().append(Data.begin(), Data.end());

  // EmitBytes might not cover all possible ways we emit data (or could be used
  // to emit executable code in some cases), but is the best method we have
  // right now for checking this.
  MCSection *Sec = getCurrentSectionOnly();
  Sec->setHasData(true);
}

void MCObjectStreamer::EmitValueToAlignment(unsigned ByteAlignment,
                                            int64_t Value,
                                            unsigned ValueSize,
                                            unsigned MaxBytesToEmit) {
  if (MaxBytesToEmit == 0)
    MaxBytesToEmit = ByteAlignment;
  insert(new MCAlignFragment(ByteAlignment, Value, ValueSize, MaxBytesToEmit));

  // Update the maximum alignment on the current section if necessary.
  MCSection *CurSec = getCurrentSectionOnly();
  if (ByteAlignment > CurSec->getAlignment())
    CurSec->setAlignment(ByteAlignment);
}

void MCObjectStreamer::EmitCodeAlignment(unsigned ByteAlignment,
                                         unsigned MaxBytesToEmit) {
  EmitValueToAlignment(ByteAlignment, 0, 1, MaxBytesToEmit);
  cast<MCAlignFragment>(getCurrentFragment())->setEmitNops(true);
}

void MCObjectStreamer::emitValueToOffset(const MCExpr *Offset,
                                         unsigned char Value,
                                         SMLoc Loc) {
  insert(new MCOrgFragment(*Offset, Value, Loc));
}

void MCObjectStreamer::EmitCodePaddingBasicBlockStart(
    const MCCodePaddingContext &Context) {
  getAssembler().getBackend().handleCodePaddingBasicBlockStart(this, Context);
}

void MCObjectStreamer::EmitCodePaddingBasicBlockEnd(
    const MCCodePaddingContext &Context) {
  getAssembler().getBackend().handleCodePaddingBasicBlockEnd(Context);
}

// Associate DTPRel32 fixup with data and resize data area
void MCObjectStreamer::EmitDTPRel32Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_DTPRel_4));
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

// Associate DTPRel64 fixup with data and resize data area
void MCObjectStreamer::EmitDTPRel64Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_DTPRel_8));
  DF->getContents().resize(DF->getContents().size() + 8, 0);
}

// Associate TPRel32 fixup with data and resize data area
void MCObjectStreamer::EmitTPRel32Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_TPRel_4));
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

// Associate TPRel64 fixup with data and resize data area
void MCObjectStreamer::EmitTPRel64Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_TPRel_8));
  DF->getContents().resize(DF->getContents().size() + 8, 0);
}

// Associate GPRel32 fixup with data and resize data area
void MCObjectStreamer::EmitGPRel32Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  DF->getFixups().push_back(
      MCFixup::create(DF->getContents().size(), Value, FK_GPRel_4));
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

// Associate GPRel64 fixup with data and resize data area
void MCObjectStreamer::EmitGPRel64Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  DF->getFixups().push_back(
      MCFixup::create(DF->getContents().size(), Value, FK_GPRel_4));
  DF->getContents().resize(DF->getContents().size() + 8, 0);
}

bool MCObjectStreamer::EmitRelocDirective(const MCExpr &Offset, StringRef Name,
                                          const MCExpr *Expr, SMLoc Loc,
                                          const MCSubtargetInfo &STI) {
  Optional<MCFixupKind> MaybeKind = Assembler->getBackend().getFixupKind(Name);
  if (!MaybeKind.hasValue())
    return true;

  MCFixupKind Kind = *MaybeKind;

  if (Expr == nullptr)
    Expr =
        MCSymbolRefExpr::create(getContext().createTempSymbol(), getContext());

  MCDataFragment *DF = getOrCreateDataFragment(&STI);
  flushPendingLabels(DF, DF->getContents().size());

  int64_t OffsetValue;
  if (Offset.evaluateAsAbsolute(OffsetValue)) {
    if (OffsetValue < 0)
      llvm_unreachable(".reloc offset is negative");
    DF->getFixups().push_back(MCFixup::create(OffsetValue, Expr, Kind, Loc));
    return false;
  }

  if (Offset.getKind() != llvm::MCExpr::SymbolRef)
    llvm_unreachable(".reloc offset is not absolute nor a label");

  const MCSymbolRefExpr &SRE = cast<MCSymbolRefExpr>(Offset);
  if (SRE.getSymbol().isDefined()) {
    DF->getFixups().push_back(MCFixup::create(SRE.getSymbol().getOffset(),
                                              Expr, Kind, Loc));
    return false;
  }

  PendingFixups.emplace_back(&SRE.getSymbol(), DF,
                                         MCFixup::create(-1, Expr, Kind, Loc));
  return false;
}

void MCObjectStreamer::emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                                SMLoc Loc) {
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  assert(getCurrentSectionOnly() && "need a section");
  insert(new MCFillFragment(FillValue, 1, NumBytes, Loc));
}

void MCObjectStreamer::emitFill(const MCExpr &NumValues, int64_t Size,
                                int64_t Expr, SMLoc Loc) {
  int64_t IntNumValues;
  // Do additional checking now if we can resolve the value.
  if (NumValues.evaluateAsAbsolute(IntNumValues, getAssemblerPtr())) {
    if (IntNumValues < 0) {
      getContext().getSourceManager()->PrintMessage(
          Loc, SourceMgr::DK_Warning,
          "'.fill' directive with negative repeat count has no effect");
      return;
    }
    // Emit now if we can for better errors.
    int64_t NonZeroSize = Size > 4 ? 4 : Size;
    Expr &= ~0ULL >> (64 - NonZeroSize * 8);
    for (uint64_t i = 0, e = IntNumValues; i != e; ++i) {
      EmitIntValue(Expr, NonZeroSize);
      if (NonZeroSize < Size)
        EmitIntValue(0, Size - NonZeroSize);
    }
    return;
  }

  // Otherwise emit as fragment.
  MCDataFragment *DF = getOrCreateDataFragment();
  flushPendingLabels(DF, DF->getContents().size());

  assert(getCurrentSectionOnly() && "need a section");
  insert(new MCFillFragment(Expr, Size, NumValues, Loc));
}

void MCObjectStreamer::EmitFileDirective(StringRef Filename) {
  getAssembler().addFileName(Filename);
}

void MCObjectStreamer::EmitAddrsig() {
  getAssembler().getWriter().emitAddrsigSection();
}

void MCObjectStreamer::EmitAddrsigSym(const MCSymbol *Sym) {
  getAssembler().registerSymbol(*Sym);
  getAssembler().getWriter().addAddrsigSymbol(Sym);
}

void MCObjectStreamer::FinishImpl() {
  getContext().RemapDebugPaths();

  // If we are generating dwarf for assembly source files dump out the sections.
  if (getContext().getGenDwarfForAssembly())
    MCGenDwarfInfo::Emit(this);

  // Dump out the dwarf file & directory tables and line tables.
  MCDwarfLineTable::Emit(this, getAssembler().getDWARFLinetableParams());

  flushPendingLabels();
  resolvePendingFixups();
  getAssembler().Finish();
}
