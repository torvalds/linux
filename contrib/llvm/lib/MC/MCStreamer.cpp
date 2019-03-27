//===- lib/MC/MCStreamer.cpp - Streaming Machine Code Output --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCStreamer.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <utility>

using namespace llvm;

MCTargetStreamer::MCTargetStreamer(MCStreamer &S) : Streamer(S) {
  S.setTargetStreamer(this);
}

// Pin the vtables to this file.
MCTargetStreamer::~MCTargetStreamer() = default;

void MCTargetStreamer::emitLabel(MCSymbol *Symbol) {}

void MCTargetStreamer::finish() {}

void MCTargetStreamer::changeSection(const MCSection *CurSection,
                                     MCSection *Section,
                                     const MCExpr *Subsection,
                                     raw_ostream &OS) {
  Section->PrintSwitchToSection(
      *Streamer.getContext().getAsmInfo(),
      Streamer.getContext().getObjectFileInfo()->getTargetTriple(), OS,
      Subsection);
}

void MCTargetStreamer::emitDwarfFileDirective(StringRef Directive) {
  Streamer.EmitRawText(Directive);
}

void MCTargetStreamer::emitValue(const MCExpr *Value) {
  SmallString<128> Str;
  raw_svector_ostream OS(Str);

  Value->print(OS, Streamer.getContext().getAsmInfo());
  Streamer.EmitRawText(OS.str());
}

void MCTargetStreamer::emitRawBytes(StringRef Data) {
  const MCAsmInfo *MAI = Streamer.getContext().getAsmInfo();
  const char *Directive = MAI->getData8bitsDirective();
  for (const unsigned char C : Data.bytes()) {
    SmallString<128> Str;
    raw_svector_ostream OS(Str);

    OS << Directive << (unsigned)C;
    Streamer.EmitRawText(OS.str());
  }
}

void MCTargetStreamer::emitAssignment(MCSymbol *Symbol, const MCExpr *Value) {}

MCStreamer::MCStreamer(MCContext &Ctx)
    : Context(Ctx), CurrentWinFrameInfo(nullptr),
      UseAssemblerInfoForParsing(false) {
  SectionStack.push_back(std::pair<MCSectionSubPair, MCSectionSubPair>());
}

MCStreamer::~MCStreamer() {}

void MCStreamer::reset() {
  DwarfFrameInfos.clear();
  CurrentWinFrameInfo = nullptr;
  WinFrameInfos.clear();
  SymbolOrdering.clear();
  SectionStack.clear();
  SectionStack.push_back(std::pair<MCSectionSubPair, MCSectionSubPair>());
}

raw_ostream &MCStreamer::GetCommentOS() {
  // By default, discard comments.
  return nulls();
}

void MCStreamer::emitRawComment(const Twine &T, bool TabPrefix) {}

void MCStreamer::addExplicitComment(const Twine &T) {}
void MCStreamer::emitExplicitComments() {}

void MCStreamer::generateCompactUnwindEncodings(MCAsmBackend *MAB) {
  for (auto &FI : DwarfFrameInfos)
    FI.CompactUnwindEncoding =
        (MAB ? MAB->generateCompactUnwindEncoding(FI.Instructions) : 0);
}

/// EmitIntValue - Special case of EmitValue that avoids the client having to
/// pass in a MCExpr for constant integers.
void MCStreamer::EmitIntValue(uint64_t Value, unsigned Size) {
  assert(1 <= Size && Size <= 8 && "Invalid size");
  assert((isUIntN(8 * Size, Value) || isIntN(8 * Size, Value)) &&
         "Invalid size");
  char buf[8];
  const bool isLittleEndian = Context.getAsmInfo()->isLittleEndian();
  for (unsigned i = 0; i != Size; ++i) {
    unsigned index = isLittleEndian ? i : (Size - i - 1);
    buf[i] = uint8_t(Value >> (index * 8));
  }
  EmitBytes(StringRef(buf, Size));
}

/// EmitULEB128IntValue - Special case of EmitULEB128Value that avoids the
/// client having to pass in a MCExpr for constant integers.
void MCStreamer::EmitULEB128IntValue(uint64_t Value) {
  SmallString<128> Tmp;
  raw_svector_ostream OSE(Tmp);
  encodeULEB128(Value, OSE);
  EmitBytes(OSE.str());
}

/// EmitSLEB128IntValue - Special case of EmitSLEB128Value that avoids the
/// client having to pass in a MCExpr for constant integers.
void MCStreamer::EmitSLEB128IntValue(int64_t Value) {
  SmallString<128> Tmp;
  raw_svector_ostream OSE(Tmp);
  encodeSLEB128(Value, OSE);
  EmitBytes(OSE.str());
}

void MCStreamer::EmitValue(const MCExpr *Value, unsigned Size, SMLoc Loc) {
  EmitValueImpl(Value, Size, Loc);
}

void MCStreamer::EmitSymbolValue(const MCSymbol *Sym, unsigned Size,
                                 bool IsSectionRelative) {
  assert((!IsSectionRelative || Size == 4) &&
         "SectionRelative value requires 4-bytes");

  if (!IsSectionRelative)
    EmitValueImpl(MCSymbolRefExpr::create(Sym, getContext()), Size);
  else
    EmitCOFFSecRel32(Sym, /*Offset=*/0);
}

void MCStreamer::EmitDTPRel64Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::EmitDTPRel32Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::EmitTPRel64Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::EmitTPRel32Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::EmitGPRel64Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::EmitGPRel32Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

/// Emit NumBytes bytes worth of the value specified by FillValue.
/// This implements directives such as '.space'.
void MCStreamer::emitFill(uint64_t NumBytes, uint8_t FillValue) {
  emitFill(*MCConstantExpr::create(NumBytes, getContext()), FillValue);
}

/// The implementation in this class just redirects to emitFill.
void MCStreamer::EmitZeros(uint64_t NumBytes) {
  emitFill(NumBytes, 0);
}

Expected<unsigned>
MCStreamer::tryEmitDwarfFileDirective(unsigned FileNo, StringRef Directory,
                                      StringRef Filename,
                                      MD5::MD5Result *Checksum,
                                      Optional<StringRef> Source,
                                      unsigned CUID) {
  return getContext().getDwarfFile(Directory, Filename, FileNo, Checksum,
                                   Source, CUID);
}

void MCStreamer::emitDwarfFile0Directive(StringRef Directory,
                                         StringRef Filename,
                                         MD5::MD5Result *Checksum,
                                         Optional<StringRef> Source,
                                         unsigned CUID) {
  getContext().setMCLineTableRootFile(CUID, Directory, Filename, Checksum,
                                      Source);
}

void MCStreamer::EmitCFIBKeyFrame() {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->IsBKeyFrame = true;
}

void MCStreamer::EmitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                       unsigned Column, unsigned Flags,
                                       unsigned Isa,
                                       unsigned Discriminator,
                                       StringRef FileName) {
  getContext().setCurrentDwarfLoc(FileNo, Line, Column, Flags, Isa,
                                  Discriminator);
}

MCSymbol *MCStreamer::getDwarfLineTableSymbol(unsigned CUID) {
  MCDwarfLineTable &Table = getContext().getMCDwarfLineTable(CUID);
  if (!Table.getLabel()) {
    StringRef Prefix = Context.getAsmInfo()->getPrivateGlobalPrefix();
    Table.setLabel(
        Context.getOrCreateSymbol(Prefix + "line_table_start" + Twine(CUID)));
  }
  return Table.getLabel();
}

bool MCStreamer::hasUnfinishedDwarfFrameInfo() {
  return !DwarfFrameInfos.empty() && !DwarfFrameInfos.back().End;
}

MCDwarfFrameInfo *MCStreamer::getCurrentDwarfFrameInfo() {
  if (!hasUnfinishedDwarfFrameInfo()) {
    getContext().reportError(SMLoc(), "this directive must appear between "
                                      ".cfi_startproc and .cfi_endproc "
                                      "directives");
    return nullptr;
  }
  return &DwarfFrameInfos.back();
}

bool MCStreamer::EmitCVFileDirective(unsigned FileNo, StringRef Filename,
                                     ArrayRef<uint8_t> Checksum,
                                     unsigned ChecksumKind) {
  return getContext().getCVContext().addFile(*this, FileNo, Filename, Checksum,
                                             ChecksumKind);
}

bool MCStreamer::EmitCVFuncIdDirective(unsigned FunctionId) {
  return getContext().getCVContext().recordFunctionId(FunctionId);
}

bool MCStreamer::EmitCVInlineSiteIdDirective(unsigned FunctionId,
                                             unsigned IAFunc, unsigned IAFile,
                                             unsigned IALine, unsigned IACol,
                                             SMLoc Loc) {
  if (getContext().getCVContext().getCVFunctionInfo(IAFunc) == nullptr) {
    getContext().reportError(Loc, "parent function id not introduced by "
                                  ".cv_func_id or .cv_inline_site_id");
    return true;
  }

  return getContext().getCVContext().recordInlinedCallSiteId(
      FunctionId, IAFunc, IAFile, IALine, IACol);
}

void MCStreamer::EmitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                    unsigned Line, unsigned Column,
                                    bool PrologueEnd, bool IsStmt,
                                    StringRef FileName, SMLoc Loc) {}

bool MCStreamer::checkCVLocSection(unsigned FuncId, unsigned FileNo,
                                   SMLoc Loc) {
  CodeViewContext &CVC = getContext().getCVContext();
  MCCVFunctionInfo *FI = CVC.getCVFunctionInfo(FuncId);
  if (!FI) {
    getContext().reportError(
        Loc, "function id not introduced by .cv_func_id or .cv_inline_site_id");
    return false;
  }

  // Track the section
  if (FI->Section == nullptr)
    FI->Section = getCurrentSectionOnly();
  else if (FI->Section != getCurrentSectionOnly()) {
    getContext().reportError(
        Loc,
        "all .cv_loc directives for a function must be in the same section");
    return false;
  }
  return true;
}

void MCStreamer::EmitCVLinetableDirective(unsigned FunctionId,
                                          const MCSymbol *Begin,
                                          const MCSymbol *End) {}

void MCStreamer::EmitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                                unsigned SourceFileId,
                                                unsigned SourceLineNum,
                                                const MCSymbol *FnStartSym,
                                                const MCSymbol *FnEndSym) {}

void MCStreamer::EmitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    StringRef FixedSizePortion) {}

void MCStreamer::EmitEHSymAttributes(const MCSymbol *Symbol,
                                     MCSymbol *EHSymbol) {
}

void MCStreamer::InitSections(bool NoExecStack) {
  SwitchSection(getContext().getObjectFileInfo()->getTextSection());
}

void MCStreamer::AssignFragment(MCSymbol *Symbol, MCFragment *Fragment) {
  assert(Fragment);
  Symbol->setFragment(Fragment);

  // As we emit symbols into a section, track the order so that they can
  // be sorted upon later. Zero is reserved to mean 'unemitted'.
  SymbolOrdering[Symbol] = 1 + SymbolOrdering.size();
}

void MCStreamer::EmitLabel(MCSymbol *Symbol, SMLoc Loc) {
  Symbol->redefineIfPossible();

  if (!Symbol->isUndefined() || Symbol->isVariable())
    return getContext().reportError(Loc, "invalid symbol redefinition");

  assert(!Symbol->isVariable() && "Cannot emit a variable symbol!");
  assert(getCurrentSectionOnly() && "Cannot emit before setting section!");
  assert(!Symbol->getFragment() && "Unexpected fragment on symbol data!");
  assert(Symbol->isUndefined() && "Cannot define a symbol twice!");

  Symbol->setFragment(&getCurrentSectionOnly()->getDummyFragment());

  MCTargetStreamer *TS = getTargetStreamer();
  if (TS)
    TS->emitLabel(Symbol);
}

void MCStreamer::EmitCFISections(bool EH, bool Debug) {
  assert(EH || Debug);
}

void MCStreamer::EmitCFIStartProc(bool IsSimple, SMLoc Loc) {
  if (hasUnfinishedDwarfFrameInfo())
    return getContext().reportError(
        Loc, "starting new .cfi frame before finishing the previous one");

  MCDwarfFrameInfo Frame;
  Frame.IsSimple = IsSimple;
  EmitCFIStartProcImpl(Frame);

  const MCAsmInfo* MAI = Context.getAsmInfo();
  if (MAI) {
    for (const MCCFIInstruction& Inst : MAI->getInitialFrameState()) {
      if (Inst.getOperation() == MCCFIInstruction::OpDefCfa ||
          Inst.getOperation() == MCCFIInstruction::OpDefCfaRegister) {
        Frame.CurrentCfaRegister = Inst.getRegister();
      }
    }
  }

  DwarfFrameInfos.push_back(Frame);
}

void MCStreamer::EmitCFIStartProcImpl(MCDwarfFrameInfo &Frame) {
}

void MCStreamer::EmitCFIEndProc() {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  EmitCFIEndProcImpl(*CurFrame);
}

void MCStreamer::EmitCFIEndProcImpl(MCDwarfFrameInfo &Frame) {
  // Put a dummy non-null value in Frame.End to mark that this frame has been
  // closed.
  Frame.End = (MCSymbol *)1;
}

MCSymbol *MCStreamer::EmitCFILabel() {
  // Return a dummy non-null value so that label fields appear filled in when
  // generating textual assembly.
  return (MCSymbol *)1;
}

void MCStreamer::EmitCFIDefCfa(int64_t Register, int64_t Offset) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createDefCfa(Label, Register, Offset);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
  CurFrame->CurrentCfaRegister = static_cast<unsigned>(Register);
}

void MCStreamer::EmitCFIDefCfaOffset(int64_t Offset) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createDefCfaOffset(Label, Offset);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIAdjustCfaOffset(int64_t Adjustment) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createAdjustCfaOffset(Label, Adjustment);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIDefCfaRegister(int64_t Register) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createDefCfaRegister(Label, Register);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
  CurFrame->CurrentCfaRegister = static_cast<unsigned>(Register);
}

void MCStreamer::EmitCFIOffset(int64_t Register, int64_t Offset) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createOffset(Label, Register, Offset);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIRelOffset(int64_t Register, int64_t Offset) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createRelOffset(Label, Register, Offset);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIPersonality(const MCSymbol *Sym,
                                    unsigned Encoding) {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Personality = Sym;
  CurFrame->PersonalityEncoding = Encoding;
}

void MCStreamer::EmitCFILsda(const MCSymbol *Sym, unsigned Encoding) {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Lsda = Sym;
  CurFrame->LsdaEncoding = Encoding;
}

void MCStreamer::EmitCFIRememberState() {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction = MCCFIInstruction::createRememberState(Label);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIRestoreState() {
  // FIXME: Error if there is no matching cfi_remember_state.
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction = MCCFIInstruction::createRestoreState(Label);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFISameValue(int64_t Register) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createSameValue(Label, Register);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIRestore(int64_t Register) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createRestore(Label, Register);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIEscape(StringRef Values) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction = MCCFIInstruction::createEscape(Label, Values);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIGnuArgsSize(int64_t Size) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createGnuArgsSize(Label, Size);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFISignalFrame() {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->IsSignalFrame = true;
}

void MCStreamer::EmitCFIUndefined(int64_t Register) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createUndefined(Label, Register);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIRegister(int64_t Register1, int64_t Register2) {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createRegister(Label, Register1, Register2);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIWindowSave() {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction =
    MCCFIInstruction::createWindowSave(Label);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFINegateRAState() {
  MCSymbol *Label = EmitCFILabel();
  MCCFIInstruction Instruction = MCCFIInstruction::createNegateRAState(Label);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::EmitCFIReturnColumn(int64_t Register) {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->RAReg = Register;
}

WinEH::FrameInfo *MCStreamer::EnsureValidWinFrameInfo(SMLoc Loc) {
  const MCAsmInfo *MAI = Context.getAsmInfo();
  if (!MAI->usesWindowsCFI()) {
    getContext().reportError(
        Loc, ".seh_* directives are not supported on this target");
    return nullptr;
  }
  if (!CurrentWinFrameInfo || CurrentWinFrameInfo->End) {
    getContext().reportError(
        Loc, ".seh_ directive must appear within an active frame");
    return nullptr;
  }
  return CurrentWinFrameInfo;
}

void MCStreamer::EmitWinCFIStartProc(const MCSymbol *Symbol, SMLoc Loc) {
  const MCAsmInfo *MAI = Context.getAsmInfo();
  if (!MAI->usesWindowsCFI())
    return getContext().reportError(
        Loc, ".seh_* directives are not supported on this target");
  if (CurrentWinFrameInfo && !CurrentWinFrameInfo->End)
    getContext().reportError(
        Loc, "Starting a function before ending the previous one!");

  MCSymbol *StartProc = EmitCFILabel();

  WinFrameInfos.emplace_back(
      llvm::make_unique<WinEH::FrameInfo>(Symbol, StartProc));
  CurrentWinFrameInfo = WinFrameInfos.back().get();
  CurrentWinFrameInfo->TextSection = getCurrentSectionOnly();
}

void MCStreamer::EmitWinCFIEndProc(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    getContext().reportError(Loc, "Not all chained regions terminated!");

  MCSymbol *Label = EmitCFILabel();
  CurFrame->End = Label;
}

void MCStreamer::EmitWinCFIFuncletOrFuncEnd(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    getContext().reportError(Loc, "Not all chained regions terminated!");

  MCSymbol *Label = EmitCFILabel();
  CurFrame->FuncletOrFuncEnd = Label;
}

void MCStreamer::EmitWinCFIStartChained(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  MCSymbol *StartProc = EmitCFILabel();

  WinFrameInfos.emplace_back(llvm::make_unique<WinEH::FrameInfo>(
      CurFrame->Function, StartProc, CurFrame));
  CurrentWinFrameInfo = WinFrameInfos.back().get();
  CurrentWinFrameInfo->TextSection = getCurrentSectionOnly();
}

void MCStreamer::EmitWinCFIEndChained(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (!CurFrame->ChainedParent)
    return getContext().reportError(
        Loc, "End of a chained region outside a chained region!");

  MCSymbol *Label = EmitCFILabel();

  CurFrame->End = Label;
  CurrentWinFrameInfo = const_cast<WinEH::FrameInfo *>(CurFrame->ChainedParent);
}

void MCStreamer::EmitWinEHHandler(const MCSymbol *Sym, bool Unwind, bool Except,
                                  SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    return getContext().reportError(
        Loc, "Chained unwind areas can't have handlers!");
  CurFrame->ExceptionHandler = Sym;
  if (!Except && !Unwind)
    getContext().reportError(Loc, "Don't know what kind of handler this is!");
  if (Unwind)
    CurFrame->HandlesUnwind = true;
  if (Except)
    CurFrame->HandlesExceptions = true;
}

void MCStreamer::EmitWinEHHandlerData(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    getContext().reportError(Loc, "Chained unwind areas can't have handlers!");
}

void MCStreamer::emitCGProfileEntry(const MCSymbolRefExpr *From,
                                    const MCSymbolRefExpr *To, uint64_t Count) {
}

static MCSection *getWinCFISection(MCContext &Context, unsigned *NextWinCFIID,
                                   MCSection *MainCFISec,
                                   const MCSection *TextSec) {
  // If this is the main .text section, use the main unwind info section.
  if (TextSec == Context.getObjectFileInfo()->getTextSection())
    return MainCFISec;

  const auto *TextSecCOFF = cast<MCSectionCOFF>(TextSec);
  auto *MainCFISecCOFF = cast<MCSectionCOFF>(MainCFISec);
  unsigned UniqueID = TextSecCOFF->getOrAssignWinCFISectionID(NextWinCFIID);

  // If this section is COMDAT, this unwind section should be COMDAT associative
  // with its group.
  const MCSymbol *KeySym = nullptr;
  if (TextSecCOFF->getCharacteristics() & COFF::IMAGE_SCN_LNK_COMDAT) {
    KeySym = TextSecCOFF->getCOMDATSymbol();

    // In a GNU environment, we can't use associative comdats. Instead, do what
    // GCC does, which is to make plain comdat selectany section named like
    // ".[px]data$_Z3foov".
    if (!Context.getAsmInfo()->hasCOFFAssociativeComdats()) {
      std::string SectionName =
          (MainCFISecCOFF->getSectionName() + "$" +
           TextSecCOFF->getSectionName().split('$').second)
              .str();
      return Context.getCOFFSection(
          SectionName,
          MainCFISecCOFF->getCharacteristics() | COFF::IMAGE_SCN_LNK_COMDAT,
          MainCFISecCOFF->getKind(), "", COFF::IMAGE_COMDAT_SELECT_ANY);
    }
  }

  return Context.getAssociativeCOFFSection(MainCFISecCOFF, KeySym, UniqueID);
}

MCSection *MCStreamer::getAssociatedPDataSection(const MCSection *TextSec) {
  return getWinCFISection(getContext(), &NextWinCFIID,
                          getContext().getObjectFileInfo()->getPDataSection(),
                          TextSec);
}

MCSection *MCStreamer::getAssociatedXDataSection(const MCSection *TextSec) {
  return getWinCFISection(getContext(), &NextWinCFIID,
                          getContext().getObjectFileInfo()->getXDataSection(),
                          TextSec);
}

void MCStreamer::EmitSyntaxDirective() {}

void MCStreamer::EmitWinCFIPushReg(unsigned Register, SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  MCSymbol *Label = EmitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::PushNonVol(Label, Register);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::EmitWinCFISetFrame(unsigned Register, unsigned Offset,
                                    SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->LastFrameInst >= 0)
    return getContext().reportError(
        Loc, "frame register and offset can be set at most once");
  if (Offset & 0x0F)
    return getContext().reportError(Loc, "offset is not a multiple of 16");
  if (Offset > 240)
    return getContext().reportError(
        Loc, "frame offset must be less than or equal to 240");

  MCSymbol *Label = EmitCFILabel();

  WinEH::Instruction Inst =
      Win64EH::Instruction::SetFPReg(Label, Register, Offset);
  CurFrame->LastFrameInst = CurFrame->Instructions.size();
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::EmitWinCFIAllocStack(unsigned Size, SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (Size == 0)
    return getContext().reportError(Loc,
                                    "stack allocation size must be non-zero");
  if (Size & 7)
    return getContext().reportError(
        Loc, "stack allocation size is not a multiple of 8");

  MCSymbol *Label = EmitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::Alloc(Label, Size);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::EmitWinCFISaveReg(unsigned Register, unsigned Offset,
                                   SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  if (Offset & 7)
    return getContext().reportError(
        Loc, "register save offset is not 8 byte aligned");

  MCSymbol *Label = EmitCFILabel();

  WinEH::Instruction Inst =
      Win64EH::Instruction::SaveNonVol(Label, Register, Offset);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::EmitWinCFISaveXMM(unsigned Register, unsigned Offset,
                                   SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (Offset & 0x0F)
    return getContext().reportError(Loc, "offset is not a multiple of 16");

  MCSymbol *Label = EmitCFILabel();

  WinEH::Instruction Inst =
      Win64EH::Instruction::SaveXMM(Label, Register, Offset);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::EmitWinCFIPushFrame(bool Code, SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (!CurFrame->Instructions.empty())
    return getContext().reportError(
        Loc, "If present, PushMachFrame must be the first UOP");

  MCSymbol *Label = EmitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::PushMachFrame(Label, Code);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::EmitWinCFIEndProlog(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  MCSymbol *Label = EmitCFILabel();

  CurFrame->PrologEnd = Label;
}

void MCStreamer::EmitCOFFSafeSEH(MCSymbol const *Symbol) {}

void MCStreamer::EmitCOFFSymbolIndex(MCSymbol const *Symbol) {}

void MCStreamer::EmitCOFFSectionIndex(MCSymbol const *Symbol) {}

void MCStreamer::EmitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset) {}

void MCStreamer::EmitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset) {}

/// EmitRawText - If this file is backed by an assembly streamer, this dumps
/// the specified string in the output .s file.  This capability is
/// indicated by the hasRawTextSupport() predicate.
void MCStreamer::EmitRawTextImpl(StringRef String) {
  // This is not llvm_unreachable for the sake of out of tree backend
  // developers who may not have assembly streamers and should serve as a
  // reminder to not accidentally call EmitRawText in the absence of such.
  report_fatal_error("EmitRawText called on an MCStreamer that doesn't support "
                     "it (target backend is likely missing an AsmStreamer "
                     "implementation)");
}

void MCStreamer::EmitRawText(const Twine &T) {
  SmallString<128> Str;
  EmitRawTextImpl(T.toStringRef(Str));
}

void MCStreamer::EmitWindowsUnwindTables() {
}

void MCStreamer::Finish() {
  if ((!DwarfFrameInfos.empty() && !DwarfFrameInfos.back().End) ||
      (!WinFrameInfos.empty() && !WinFrameInfos.back()->End)) {
    getContext().reportError(SMLoc(), "Unfinished frame!");
    return;
  }

  MCTargetStreamer *TS = getTargetStreamer();
  if (TS)
    TS->finish();

  FinishImpl();
}

void MCStreamer::EmitAssignment(MCSymbol *Symbol, const MCExpr *Value) {
  visitUsedExpr(*Value);
  Symbol->setVariableValue(Value);

  MCTargetStreamer *TS = getTargetStreamer();
  if (TS)
    TS->emitAssignment(Symbol, Value);
}

void MCTargetStreamer::prettyPrintAsm(MCInstPrinter &InstPrinter,
                                      raw_ostream &OS, const MCInst &Inst,
                                      const MCSubtargetInfo &STI) {
  InstPrinter.printInst(&Inst, OS, "", STI);
}

void MCStreamer::visitUsedSymbol(const MCSymbol &Sym) {
}

void MCStreamer::visitUsedExpr(const MCExpr &Expr) {
  switch (Expr.getKind()) {
  case MCExpr::Target:
    cast<MCTargetExpr>(Expr).visitUsedExpr(*this);
    break;

  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr &BE = cast<MCBinaryExpr>(Expr);
    visitUsedExpr(*BE.getLHS());
    visitUsedExpr(*BE.getRHS());
    break;
  }

  case MCExpr::SymbolRef:
    visitUsedSymbol(cast<MCSymbolRefExpr>(Expr).getSymbol());
    break;

  case MCExpr::Unary:
    visitUsedExpr(*cast<MCUnaryExpr>(Expr).getSubExpr());
    break;
  }
}

void MCStreamer::EmitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                                 bool) {
  // Scan for values.
  for (unsigned i = Inst.getNumOperands(); i--;)
    if (Inst.getOperand(i).isExpr())
      visitUsedExpr(*Inst.getOperand(i).getExpr());
}

void MCStreamer::emitAbsoluteSymbolDiff(const MCSymbol *Hi, const MCSymbol *Lo,
                                        unsigned Size) {
  // Get the Hi-Lo expression.
  const MCExpr *Diff =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(Hi, Context),
                              MCSymbolRefExpr::create(Lo, Context), Context);

  const MCAsmInfo *MAI = Context.getAsmInfo();
  if (!MAI->doesSetDirectiveSuppressReloc()) {
    EmitValue(Diff, Size);
    return;
  }

  // Otherwise, emit with .set (aka assignment).
  MCSymbol *SetLabel = Context.createTempSymbol("set", true);
  EmitAssignment(SetLabel, Diff);
  EmitSymbolValue(SetLabel, Size);
}

void MCStreamer::emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                                 const MCSymbol *Lo) {
  // Get the Hi-Lo expression.
  const MCExpr *Diff =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(Hi, Context),
                              MCSymbolRefExpr::create(Lo, Context), Context);

  EmitULEB128Value(Diff);
}

void MCStreamer::EmitAssemblerFlag(MCAssemblerFlag Flag) {}
void MCStreamer::EmitThumbFunc(MCSymbol *Func) {}
void MCStreamer::EmitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {}
void MCStreamer::BeginCOFFSymbolDef(const MCSymbol *Symbol) {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::EndCOFFSymbolDef() {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::EmitFileDirective(StringRef Filename) {}
void MCStreamer::EmitCOFFSymbolStorageClass(int StorageClass) {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::EmitCOFFSymbolType(int Type) {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::emitELFSize(MCSymbol *Symbol, const MCExpr *Value) {}
void MCStreamer::emitELFSymverDirective(StringRef AliasName,
                                        const MCSymbol *Aliasee) {}
void MCStreamer::EmitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                       unsigned ByteAlignment) {}
void MCStreamer::EmitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                uint64_t Size, unsigned ByteAlignment) {}
void MCStreamer::ChangeSection(MCSection *, const MCExpr *) {}
void MCStreamer::EmitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) {}
void MCStreamer::EmitBytes(StringRef Data) {}
void MCStreamer::EmitBinaryData(StringRef Data) { EmitBytes(Data); }
void MCStreamer::EmitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) {
  visitUsedExpr(*Value);
}
void MCStreamer::EmitULEB128Value(const MCExpr *Value) {}
void MCStreamer::EmitSLEB128Value(const MCExpr *Value) {}
void MCStreamer::emitFill(const MCExpr &NumBytes, uint64_t Value, SMLoc Loc) {}
void MCStreamer::emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                          SMLoc Loc) {}
void MCStreamer::EmitValueToAlignment(unsigned ByteAlignment, int64_t Value,
                                      unsigned ValueSize,
                                      unsigned MaxBytesToEmit) {}
void MCStreamer::EmitCodeAlignment(unsigned ByteAlignment,
                                   unsigned MaxBytesToEmit) {}
void MCStreamer::emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                                   SMLoc Loc) {}
void MCStreamer::EmitBundleAlignMode(unsigned AlignPow2) {}
void MCStreamer::EmitBundleLock(bool AlignToEnd) {}
void MCStreamer::FinishImpl() {}
void MCStreamer::EmitBundleUnlock() {}

void MCStreamer::SwitchSection(MCSection *Section, const MCExpr *Subsection) {
  assert(Section && "Cannot switch to a null section!");
  MCSectionSubPair curSection = SectionStack.back().first;
  SectionStack.back().second = curSection;
  if (MCSectionSubPair(Section, Subsection) != curSection) {
    ChangeSection(Section, Subsection);
    SectionStack.back().first = MCSectionSubPair(Section, Subsection);
    assert(!Section->hasEnded() && "Section already ended");
    MCSymbol *Sym = Section->getBeginSymbol();
    if (Sym && !Sym->isInSection())
      EmitLabel(Sym);
  }
}

MCSymbol *MCStreamer::endSection(MCSection *Section) {
  // TODO: keep track of the last subsection so that this symbol appears in the
  // correct place.
  MCSymbol *Sym = Section->getEndSymbol(Context);
  if (Sym->isInSection())
    return Sym;

  SwitchSection(Section);
  EmitLabel(Sym);
  return Sym;
}

void MCStreamer::EmitVersionForTarget(const Triple &Target,
                                      const VersionTuple &SDKVersion) {
  if (!Target.isOSBinFormatMachO() || !Target.isOSDarwin())
    return;
  // Do we even know the version?
  if (Target.getOSMajorVersion() == 0)
    return;

  unsigned Major;
  unsigned Minor;
  unsigned Update;
  MCVersionMinType VersionType;
  if (Target.isWatchOS()) {
    VersionType = MCVM_WatchOSVersionMin;
    Target.getWatchOSVersion(Major, Minor, Update);
  } else if (Target.isTvOS()) {
    VersionType = MCVM_TvOSVersionMin;
    Target.getiOSVersion(Major, Minor, Update);
  } else if (Target.isMacOSX()) {
    VersionType = MCVM_OSXVersionMin;
    if (!Target.getMacOSXVersion(Major, Minor, Update))
      Major = 0;
  } else {
    VersionType = MCVM_IOSVersionMin;
    Target.getiOSVersion(Major, Minor, Update);
  }
  if (Major != 0)
    EmitVersionMin(VersionType, Major, Minor, Update, SDKVersion);
}
