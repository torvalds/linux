//===-- X86WinCOFFTargetStreamer.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "X86MCTargetDesc.h"
#include "X86TargetStreamer.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;
using namespace llvm::codeview;

namespace {
/// Implements Windows x86-only directives for assembly emission.
class X86WinCOFFAsmTargetStreamer : public X86TargetStreamer {
  formatted_raw_ostream &OS;
  MCInstPrinter &InstPrinter;

public:
  X86WinCOFFAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                              MCInstPrinter &InstPrinter)
      : X86TargetStreamer(S), OS(OS), InstPrinter(InstPrinter) {}

  bool emitFPOProc(const MCSymbol *ProcSym, unsigned ParamsSize,
                   SMLoc L) override;
  bool emitFPOEndPrologue(SMLoc L) override;
  bool emitFPOEndProc(SMLoc L) override;
  bool emitFPOData(const MCSymbol *ProcSym, SMLoc L) override;
  bool emitFPOPushReg(unsigned Reg, SMLoc L) override;
  bool emitFPOStackAlloc(unsigned StackAlloc, SMLoc L) override;
  bool emitFPOStackAlign(unsigned Align, SMLoc L) override;
  bool emitFPOSetFrame(unsigned Reg, SMLoc L) override;
};

/// Represents a single FPO directive.
struct FPOInstruction {
  MCSymbol *Label;
  enum Operation {
    PushReg,
    StackAlloc,
    StackAlign,
    SetFrame,
  } Op;
  unsigned RegOrOffset;
};

struct FPOData {
  const MCSymbol *Function = nullptr;
  MCSymbol *Begin = nullptr;
  MCSymbol *PrologueEnd = nullptr;
  MCSymbol *End = nullptr;
  unsigned ParamsSize = 0;

  SmallVector<FPOInstruction, 5> Instructions;
};

/// Implements Windows x86-only directives for object emission.
class X86WinCOFFTargetStreamer : public X86TargetStreamer {
  /// Map from function symbol to its FPO data.
  DenseMap<const MCSymbol *, std::unique_ptr<FPOData>> AllFPOData;

  /// Current FPO data created by .cv_fpo_proc.
  std::unique_ptr<FPOData> CurFPOData;

  bool haveOpenFPOData() { return !!CurFPOData; }

  /// Diagnoses an error at L if we are not in an FPO prologue. Return true on
  /// error.
  bool checkInFPOPrologue(SMLoc L);

  MCSymbol *emitFPOLabel();

  MCContext &getContext() { return getStreamer().getContext(); }

public:
  X86WinCOFFTargetStreamer(MCStreamer &S) : X86TargetStreamer(S) {}

  bool emitFPOProc(const MCSymbol *ProcSym, unsigned ParamsSize,
                   SMLoc L) override;
  bool emitFPOEndPrologue(SMLoc L) override;
  bool emitFPOEndProc(SMLoc L) override;
  bool emitFPOData(const MCSymbol *ProcSym, SMLoc L) override;
  bool emitFPOPushReg(unsigned Reg, SMLoc L) override;
  bool emitFPOStackAlloc(unsigned StackAlloc, SMLoc L) override;
  bool emitFPOStackAlign(unsigned Align, SMLoc L) override;
  bool emitFPOSetFrame(unsigned Reg, SMLoc L) override;
};
} // end namespace

bool X86WinCOFFAsmTargetStreamer::emitFPOProc(const MCSymbol *ProcSym,
                                              unsigned ParamsSize, SMLoc L) {
  OS << "\t.cv_fpo_proc\t";
  ProcSym->print(OS, getStreamer().getContext().getAsmInfo());
  OS << ' ' << ParamsSize << '\n';
  return false;
}

bool X86WinCOFFAsmTargetStreamer::emitFPOEndPrologue(SMLoc L) {
  OS << "\t.cv_fpo_endprologue\n";
  return false;
}

bool X86WinCOFFAsmTargetStreamer::emitFPOEndProc(SMLoc L) {
  OS << "\t.cv_fpo_endproc\n";
  return false;
}

bool X86WinCOFFAsmTargetStreamer::emitFPOData(const MCSymbol *ProcSym,
                                              SMLoc L) {
  OS << "\t.cv_fpo_data\t";
  ProcSym->print(OS, getStreamer().getContext().getAsmInfo());
  OS << '\n';
  return false;
}

bool X86WinCOFFAsmTargetStreamer::emitFPOPushReg(unsigned Reg, SMLoc L) {
  OS << "\t.cv_fpo_pushreg\t";
  InstPrinter.printRegName(OS, Reg);
  OS << '\n';
  return false;
}

bool X86WinCOFFAsmTargetStreamer::emitFPOStackAlloc(unsigned StackAlloc,
                                                    SMLoc L) {
  OS << "\t.cv_fpo_stackalloc\t" << StackAlloc << '\n';
  return false;
}

bool X86WinCOFFAsmTargetStreamer::emitFPOStackAlign(unsigned Align, SMLoc L) {
  OS << "\t.cv_fpo_stackalign\t" << Align << '\n';
  return false;
}

bool X86WinCOFFAsmTargetStreamer::emitFPOSetFrame(unsigned Reg, SMLoc L) {
  OS << "\t.cv_fpo_setframe\t";
  InstPrinter.printRegName(OS, Reg);
  OS << '\n';
  return false;
}

bool X86WinCOFFTargetStreamer::checkInFPOPrologue(SMLoc L) {
  if (!haveOpenFPOData() || CurFPOData->PrologueEnd) {
    getContext().reportError(
        L,
        "directive must appear between .cv_fpo_proc and .cv_fpo_endprologue");
    return true;
  }
  return false;
}

MCSymbol *X86WinCOFFTargetStreamer::emitFPOLabel() {
  MCSymbol *Label = getContext().createTempSymbol("cfi", true);
  getStreamer().EmitLabel(Label);
  return Label;
}

bool X86WinCOFFTargetStreamer::emitFPOProc(const MCSymbol *ProcSym,
                                           unsigned ParamsSize, SMLoc L) {
  if (haveOpenFPOData()) {
    getContext().reportError(
        L, "opening new .cv_fpo_proc before closing previous frame");
    return true;
  }
  CurFPOData = llvm::make_unique<FPOData>();
  CurFPOData->Function = ProcSym;
  CurFPOData->Begin = emitFPOLabel();
  CurFPOData->ParamsSize = ParamsSize;
  return false;
}

bool X86WinCOFFTargetStreamer::emitFPOEndProc(SMLoc L) {
  if (!haveOpenFPOData()) {
    getContext().reportError(L, ".cv_fpo_endproc must appear after .cv_proc");
    return true;
  }
  if (!CurFPOData->PrologueEnd) {
    // Complain if there were prologue setup instructions but no end prologue.
    if (!CurFPOData->Instructions.empty()) {
      getContext().reportError(L, "missing .cv_fpo_endprologue");
      CurFPOData->Instructions.clear();
    }

    // Claim there is a zero-length prologue to make the label math work out
    // later.
    CurFPOData->PrologueEnd = CurFPOData->Begin;
  }

  CurFPOData->End = emitFPOLabel();
  const MCSymbol *Fn = CurFPOData->Function;
  AllFPOData.insert({Fn, std::move(CurFPOData)});
  return false;
}

bool X86WinCOFFTargetStreamer::emitFPOSetFrame(unsigned Reg, SMLoc L) {
  if (checkInFPOPrologue(L))
    return true;
  FPOInstruction Inst;
  Inst.Label = emitFPOLabel();
  Inst.Op = FPOInstruction::SetFrame;
  Inst.RegOrOffset = Reg;
  CurFPOData->Instructions.push_back(Inst);
  return false;
}

bool X86WinCOFFTargetStreamer::emitFPOPushReg(unsigned Reg, SMLoc L) {
  if (checkInFPOPrologue(L))
    return true;
  FPOInstruction Inst;
  Inst.Label = emitFPOLabel();
  Inst.Op = FPOInstruction::PushReg;
  Inst.RegOrOffset = Reg;
  CurFPOData->Instructions.push_back(Inst);
  return false;
}

bool X86WinCOFFTargetStreamer::emitFPOStackAlloc(unsigned StackAlloc, SMLoc L) {
  if (checkInFPOPrologue(L))
    return true;
  FPOInstruction Inst;
  Inst.Label = emitFPOLabel();
  Inst.Op = FPOInstruction::StackAlloc;
  Inst.RegOrOffset = StackAlloc;
  CurFPOData->Instructions.push_back(Inst);
  return false;
}

bool X86WinCOFFTargetStreamer::emitFPOStackAlign(unsigned Align, SMLoc L) {
  if (checkInFPOPrologue(L))
    return true;
  if (!llvm::any_of(CurFPOData->Instructions, [](const FPOInstruction &Inst) {
        return Inst.Op == FPOInstruction::SetFrame;
      })) {
    getContext().reportError(
        L, "a frame register must be established before aligning the stack");
    return true;
  }
  FPOInstruction Inst;
  Inst.Label = emitFPOLabel();
  Inst.Op = FPOInstruction::StackAlign;
  Inst.RegOrOffset = Align;
  CurFPOData->Instructions.push_back(Inst);
  return false;
}

bool X86WinCOFFTargetStreamer::emitFPOEndPrologue(SMLoc L) {
  if (checkInFPOPrologue(L))
    return true;
  CurFPOData->PrologueEnd = emitFPOLabel();
  return false;
}

namespace {
struct RegSaveOffset {
  RegSaveOffset(unsigned Reg, unsigned Offset) : Reg(Reg), Offset(Offset) {}

  unsigned Reg = 0;
  unsigned Offset = 0;
};

struct FPOStateMachine {
  explicit FPOStateMachine(const FPOData *FPO) : FPO(FPO) {}

  const FPOData *FPO = nullptr;
  unsigned FrameReg = 0;
  unsigned FrameRegOff = 0;
  unsigned CurOffset = 0;
  unsigned LocalSize = 0;
  unsigned SavedRegSize = 0;
  unsigned StackOffsetBeforeAlign = 0;
  unsigned StackAlign = 0;
  unsigned Flags = 0; // FIXME: Set HasSEH / HasEH.

  SmallString<128> FrameFunc;

  SmallVector<RegSaveOffset, 4> RegSaveOffsets;

  void emitFrameDataRecord(MCStreamer &OS, MCSymbol *Label);
};
} // end namespace

static Printable printFPOReg(const MCRegisterInfo *MRI, unsigned LLVMReg) {
  return Printable([MRI, LLVMReg](raw_ostream &OS) {
    switch (LLVMReg) {
    // MSVC only seems to emit symbolic register names for EIP, EBP, and ESP,
    // but the format seems to support more than that, so we emit them.
    case X86::EAX: OS << "$eax"; break;
    case X86::EBX: OS << "$ebx"; break;
    case X86::ECX: OS << "$ecx"; break;
    case X86::EDX: OS << "$edx"; break;
    case X86::EDI: OS << "$edi"; break;
    case X86::ESI: OS << "$esi"; break;
    case X86::ESP: OS << "$esp"; break;
    case X86::EBP: OS << "$ebp"; break;
    case X86::EIP: OS << "$eip"; break;
    // Otherwise, get the codeview register number and print $N.
    default:
      OS << '$' << MRI->getCodeViewRegNum(LLVMReg);
      break;
    }
  });
}

void FPOStateMachine::emitFrameDataRecord(MCStreamer &OS, MCSymbol *Label) {
  unsigned CurFlags = Flags;
  if (Label == FPO->Begin)
    CurFlags |= FrameData::IsFunctionStart;

  // Compute the new FrameFunc string.
  FrameFunc.clear();
  raw_svector_ostream FuncOS(FrameFunc);
  const MCRegisterInfo *MRI = OS.getContext().getRegisterInfo();
  assert((StackAlign == 0 || FrameReg != 0) &&
         "cannot align stack without frame reg");
  StringRef CFAVar = StackAlign == 0 ? "$T0" : "$T1";

  if (FrameReg) {
    // CFA is FrameReg + FrameRegOff.
    FuncOS << CFAVar << ' ' << printFPOReg(MRI, FrameReg) << ' ' << FrameRegOff
           << " + = ";

    // Assign $T0, the VFRAME register, the value of ESP after it is aligned.
    // Starting from the CFA, we subtract the size of all pushed registers, and
    // align the result. While we don't store any CSRs in this area, $T0 is used
    // by S_DEFRANGE_FRAMEPOINTER_REL records to find local variables.
    if (StackAlign) {
      FuncOS << "$T0 " << CFAVar << ' ' << StackOffsetBeforeAlign << " - "
             << StackAlign << " @ = ";
    }
  } else {
    // The address of return address is ESP + CurOffset, but we use .raSearch to
    // match MSVC. This seems to ask the debugger to subtract some combination
    // of LocalSize and SavedRegSize from ESP and grovel around in that memory
    // to find the address of a plausible return address.
    FuncOS << CFAVar << " .raSearch = ";
  }

  // Caller's $eip should be dereferenced CFA, and $esp should be CFA plus 4.
  FuncOS << "$eip " << CFAVar << " ^ = ";
  FuncOS << "$esp " << CFAVar << " 4 + = ";

  // Each saved register is stored at an unchanging negative CFA offset.
  for (RegSaveOffset RO : RegSaveOffsets)
    FuncOS << printFPOReg(MRI, RO.Reg) << ' ' << CFAVar << ' ' << RO.Offset
           << " - ^ = ";

  // Add it to the CV string table.
  CodeViewContext &CVCtx = OS.getContext().getCVContext();
  unsigned FrameFuncStrTabOff = CVCtx.addToStringTable(FuncOS.str()).second;

  // MSVC has only ever been observed to emit a MaxStackSize of zero.
  unsigned MaxStackSize = 0;

  // The FrameData record format is:
  //   ulittle32_t RvaStart;
  //   ulittle32_t CodeSize;
  //   ulittle32_t LocalSize;
  //   ulittle32_t ParamsSize;
  //   ulittle32_t MaxStackSize;
  //   ulittle32_t FrameFunc; // String table offset
  //   ulittle16_t PrologSize;
  //   ulittle16_t SavedRegsSize;
  //   ulittle32_t Flags;

  OS.emitAbsoluteSymbolDiff(Label, FPO->Begin, 4); // RvaStart
  OS.emitAbsoluteSymbolDiff(FPO->End, Label, 4);   // CodeSize
  OS.EmitIntValue(LocalSize, 4);
  OS.EmitIntValue(FPO->ParamsSize, 4);
  OS.EmitIntValue(MaxStackSize, 4);
  OS.EmitIntValue(FrameFuncStrTabOff, 4); // FrameFunc
  OS.emitAbsoluteSymbolDiff(FPO->PrologueEnd, Label, 2);
  OS.EmitIntValue(SavedRegSize, 2);
  OS.EmitIntValue(CurFlags, 4);
}

/// Compute and emit the real CodeView FrameData subsection.
bool X86WinCOFFTargetStreamer::emitFPOData(const MCSymbol *ProcSym, SMLoc L) {
  MCStreamer &OS = getStreamer();
  MCContext &Ctx = OS.getContext();

  auto I = AllFPOData.find(ProcSym);
  if (I == AllFPOData.end()) {
    Ctx.reportError(L, Twine("no FPO data found for symbol ") +
                           ProcSym->getName());
    return true;
  }
  const FPOData *FPO = I->second.get();
  assert(FPO->Begin && FPO->End && FPO->PrologueEnd && "missing FPO label");

  MCSymbol *FrameBegin = Ctx.createTempSymbol(),
           *FrameEnd = Ctx.createTempSymbol();

  OS.EmitIntValue(unsigned(DebugSubsectionKind::FrameData), 4);
  OS.emitAbsoluteSymbolDiff(FrameEnd, FrameBegin, 4);
  OS.EmitLabel(FrameBegin);

  // Start with the RVA of the function in question.
  OS.EmitValue(MCSymbolRefExpr::create(FPO->Function,
                                       MCSymbolRefExpr::VK_COFF_IMGREL32, Ctx),
               4);

  // Emit a sequence of FrameData records.
  FPOStateMachine FSM(FPO);

  FSM.emitFrameDataRecord(OS, FPO->Begin);
  for (const FPOInstruction &Inst : FPO->Instructions) {
    switch (Inst.Op) {
    case FPOInstruction::PushReg:
      FSM.CurOffset += 4;
      FSM.SavedRegSize += 4;
      FSM.RegSaveOffsets.push_back({Inst.RegOrOffset, FSM.CurOffset});
      break;
    case FPOInstruction::SetFrame:
      FSM.FrameReg = Inst.RegOrOffset;
      FSM.FrameRegOff = FSM.CurOffset;
      break;
    case FPOInstruction::StackAlign:
      FSM.StackOffsetBeforeAlign = FSM.CurOffset;
      FSM.StackAlign = Inst.RegOrOffset;
      break;
    case FPOInstruction::StackAlloc:
      FSM.CurOffset += Inst.RegOrOffset;
      FSM.LocalSize += Inst.RegOrOffset;
      // No need to emit FrameData for stack allocations with a frame pointer.
      if (FSM.FrameReg)
        continue;
      break;
    }
    FSM.emitFrameDataRecord(OS, Inst.Label);
  }

  OS.EmitValueToAlignment(4, 0);
  OS.EmitLabel(FrameEnd);
  return false;
}

MCTargetStreamer *llvm::createX86AsmTargetStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS,
                                                   MCInstPrinter *InstPrinter,
                                                   bool IsVerboseAsm) {
  // FIXME: This makes it so we textually assemble COFF directives on ELF.
  // That's kind of nonsensical.
  return new X86WinCOFFAsmTargetStreamer(S, OS, *InstPrinter);
}

MCTargetStreamer *
llvm::createX86ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  // No need to register a target streamer.
  if (!STI.getTargetTriple().isOSBinFormatCOFF())
    return nullptr;
  // Registers itself to the MCStreamer.
  return new X86WinCOFFTargetStreamer(S);
}
