//===- MCObjectStreamer.h - MCStreamer Object File Interface ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCOBJECTSTREAMER_H
#define LLVM_MC_MCOBJECTSTREAMER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {
class MCAssembler;
class MCCodeEmitter;
class MCSubtargetInfo;
class MCExpr;
class MCFragment;
class MCDataFragment;
class MCAsmBackend;
class raw_ostream;
class raw_pwrite_stream;

/// Streaming object file generation interface.
///
/// This class provides an implementation of the MCStreamer interface which is
/// suitable for use with the assembler backend. Specific object file formats
/// are expected to subclass this interface to implement directives specific
/// to that file format or custom semantics expected by the object writer
/// implementation.
class MCObjectStreamer : public MCStreamer {
  std::unique_ptr<MCAssembler> Assembler;
  MCSection::iterator CurInsertionPoint;
  bool EmitEHFrame;
  bool EmitDebugFrame;
  SmallVector<MCSymbol *, 2> PendingLabels;
  struct PendingMCFixup {
    const MCSymbol *Sym;
    MCFixup Fixup;
    MCDataFragment *DF;
    PendingMCFixup(const MCSymbol *McSym, MCDataFragment *F, MCFixup McFixup)
        : Sym(McSym), Fixup(McFixup), DF(F) {}
  };
  SmallVector<PendingMCFixup, 2> PendingFixups;

  virtual void EmitInstToData(const MCInst &Inst, const MCSubtargetInfo&) = 0;
  void EmitCFIStartProcImpl(MCDwarfFrameInfo &Frame) override;
  void EmitCFIEndProcImpl(MCDwarfFrameInfo &Frame) override;
  MCSymbol *EmitCFILabel() override;
  void EmitInstructionImpl(const MCInst &Inst, const MCSubtargetInfo &STI);
  void resolvePendingFixups();

protected:
  MCObjectStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                   std::unique_ptr<MCObjectWriter> OW,
                   std::unique_ptr<MCCodeEmitter> Emitter);
  ~MCObjectStreamer();

public:
  /// state management
  void reset() override;

  /// Object streamers require the integrated assembler.
  bool isIntegratedAssemblerRequired() const override { return true; }

  void EmitFrames(MCAsmBackend *MAB);
  void EmitCFISections(bool EH, bool Debug) override;

  MCFragment *getCurrentFragment() const;

  void insert(MCFragment *F) {
    flushPendingLabels(F);
    MCSection *CurSection = getCurrentSectionOnly();
    CurSection->getFragmentList().insert(CurInsertionPoint, F);
    F->setParent(CurSection);
  }

  /// Get a data fragment to write into, creating a new one if the current
  /// fragment is not a data fragment.
  /// Optionally a \p STI can be passed in so that a new fragment is created
  /// if the Subtarget differs from the current fragment.
  MCDataFragment *getOrCreateDataFragment(const MCSubtargetInfo* STI = nullptr);
  MCPaddingFragment *getOrCreatePaddingFragment();

protected:
  bool changeSectionImpl(MCSection *Section, const MCExpr *Subsection);

  /// If any labels have been emitted but not assigned fragments, ensure that
  /// they get assigned, either to F if possible or to a new data fragment.
  /// Optionally, it is also possible to provide an offset \p FOffset, which
  /// will be used as a symbol offset within the fragment.
  void flushPendingLabels(MCFragment *F, uint64_t FOffset = 0);

public:
  void visitUsedSymbol(const MCSymbol &Sym) override;

  /// Create a dummy fragment to assign any pending labels.
  void flushPendingLabels() { flushPendingLabels(nullptr); }

  MCAssembler &getAssembler() { return *Assembler; }
  MCAssembler *getAssemblerPtr() override;
  /// \name MCStreamer Interface
  /// @{

  void EmitLabel(MCSymbol *Symbol, SMLoc Loc = SMLoc()) override;
  virtual void EmitLabel(MCSymbol *Symbol, SMLoc Loc, MCFragment *F);
  void EmitAssignment(MCSymbol *Symbol, const MCExpr *Value) override;
  void EmitValueImpl(const MCExpr *Value, unsigned Size,
                     SMLoc Loc = SMLoc()) override;
  void EmitULEB128Value(const MCExpr *Value) override;
  void EmitSLEB128Value(const MCExpr *Value) override;
  void EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) override;
  void ChangeSection(MCSection *Section, const MCExpr *Subsection) override;
  void EmitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                       bool = false) override;

  /// Emit an instruction to a special fragment, because this instruction
  /// can change its size during relaxation.
  virtual void EmitInstToFragment(const MCInst &Inst, const MCSubtargetInfo &);

  void EmitBundleAlignMode(unsigned AlignPow2) override;
  void EmitBundleLock(bool AlignToEnd) override;
  void EmitBundleUnlock() override;
  void EmitBytes(StringRef Data) override;
  void EmitValueToAlignment(unsigned ByteAlignment, int64_t Value = 0,
                            unsigned ValueSize = 1,
                            unsigned MaxBytesToEmit = 0) override;
  void EmitCodeAlignment(unsigned ByteAlignment,
                         unsigned MaxBytesToEmit = 0) override;
  void emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                         SMLoc Loc) override;
  void
  EmitCodePaddingBasicBlockStart(const MCCodePaddingContext &Context) override;
  void
  EmitCodePaddingBasicBlockEnd(const MCCodePaddingContext &Context) override;
  void EmitDwarfLocDirective(unsigned FileNo, unsigned Line,
                             unsigned Column, unsigned Flags,
                             unsigned Isa, unsigned Discriminator,
                             StringRef FileName) override;
  void EmitDwarfAdvanceLineAddr(int64_t LineDelta, const MCSymbol *LastLabel,
                                const MCSymbol *Label,
                                unsigned PointerSize);
  void EmitDwarfAdvanceFrameAddr(const MCSymbol *LastLabel,
                                 const MCSymbol *Label);
  void EmitCVLocDirective(unsigned FunctionId, unsigned FileNo, unsigned Line,
                          unsigned Column, bool PrologueEnd, bool IsStmt,
                          StringRef FileName, SMLoc Loc) override;
  void EmitCVLinetableDirective(unsigned FunctionId, const MCSymbol *Begin,
                                const MCSymbol *End) override;
  void EmitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                      unsigned SourceFileId,
                                      unsigned SourceLineNum,
                                      const MCSymbol *FnStartSym,
                                      const MCSymbol *FnEndSym) override;
  void EmitCVDefRangeDirective(
      ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
      StringRef FixedSizePortion) override;
  void EmitCVStringTableDirective() override;
  void EmitCVFileChecksumsDirective() override;
  void EmitCVFileChecksumOffsetDirective(unsigned FileNo) override;
  void EmitDTPRel32Value(const MCExpr *Value) override;
  void EmitDTPRel64Value(const MCExpr *Value) override;
  void EmitTPRel32Value(const MCExpr *Value) override;
  void EmitTPRel64Value(const MCExpr *Value) override;
  void EmitGPRel32Value(const MCExpr *Value) override;
  void EmitGPRel64Value(const MCExpr *Value) override;
  bool EmitRelocDirective(const MCExpr &Offset, StringRef Name,
                          const MCExpr *Expr, SMLoc Loc,
                          const MCSubtargetInfo &STI) override;
  using MCStreamer::emitFill;
  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                SMLoc Loc = SMLoc()) override;
  void emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                SMLoc Loc = SMLoc()) override;
  void EmitFileDirective(StringRef Filename) override;

  void EmitAddrsig() override;
  void EmitAddrsigSym(const MCSymbol *Sym) override;

  void FinishImpl() override;

  /// Emit the absolute difference between two symbols if possible.
  ///
  /// Emit the absolute difference between \c Hi and \c Lo, as long as we can
  /// compute it.  Currently, that requires that both symbols are in the same
  /// data fragment and that the target has not specified that diff expressions
  /// require relocations to be emitted. Otherwise, do nothing and return
  /// \c false.
  ///
  /// \pre Offset of \c Hi is greater than the offset \c Lo.
  void emitAbsoluteSymbolDiff(const MCSymbol *Hi, const MCSymbol *Lo,
                              unsigned Size) override;

  void emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                       const MCSymbol *Lo) override;

  bool mayHaveInstructions(MCSection &Sec) const override;
};

} // end namespace llvm

#endif
