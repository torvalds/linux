//===- AArch64AsmPrinter.cpp - AArch64 LLVM assembly writer ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the AArch64 assembly language.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64MCInstLower.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64RegisterInfo.h"
#include "AArch64Subtarget.h"
#include "AArch64TargetObjectFile.h"
#include "InstPrinter/AArch64InstPrinter.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "MCTargetDesc/AArch64TargetStreamer.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class AArch64AsmPrinter : public AsmPrinter {
  AArch64MCInstLower MCInstLowering;
  StackMaps SM;
  const AArch64Subtarget *STI;

public:
  AArch64AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), MCInstLowering(OutContext, *this),
        SM(*this) {}

  StringRef getPassName() const override { return "AArch64 Assembly Printer"; }

  /// Wrapper for MCInstLowering.lowerOperand() for the
  /// tblgen'erated pseudo lowering.
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const {
    return MCInstLowering.lowerOperand(MO, MCOp);
  }

  void EmitJumpTableInfo() override;
  void emitJumpTableEntry(const MachineJumpTableInfo *MJTI,
                          const MachineBasicBlock *MBB, unsigned JTI);

  void LowerJumpTableDestSmall(MCStreamer &OutStreamer, const MachineInstr &MI);

  void LowerSTACKMAP(MCStreamer &OutStreamer, StackMaps &SM,
                     const MachineInstr &MI);
  void LowerPATCHPOINT(MCStreamer &OutStreamer, StackMaps &SM,
                       const MachineInstr &MI);

  void LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI);
  void LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI);
  void LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI);

  void EmitSled(const MachineInstr &MI, SledKind Kind);

  /// tblgen'erated driver function for lowering simple MI->MC
  /// pseudo instructions.
  bool emitPseudoExpansionLowering(MCStreamer &OutStreamer,
                                   const MachineInstr *MI);

  void EmitInstruction(const MachineInstr *MI) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AsmPrinter::getAnalysisUsage(AU);
    AU.setPreservesAll();
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    AArch64FI = MF.getInfo<AArch64FunctionInfo>();
    STI = static_cast<const AArch64Subtarget*>(&MF.getSubtarget());

    SetupMachineFunction(MF);

    if (STI->isTargetCOFF()) {
      bool Internal = MF.getFunction().hasInternalLinkage();
      COFF::SymbolStorageClass Scl = Internal ? COFF::IMAGE_SYM_CLASS_STATIC
                                              : COFF::IMAGE_SYM_CLASS_EXTERNAL;
      int Type =
        COFF::IMAGE_SYM_DTYPE_FUNCTION << COFF::SCT_COMPLEX_TYPE_SHIFT;

      OutStreamer->BeginCOFFSymbolDef(CurrentFnSym);
      OutStreamer->EmitCOFFSymbolStorageClass(Scl);
      OutStreamer->EmitCOFFSymbolType(Type);
      OutStreamer->EndCOFFSymbolDef();
    }

    // Emit the rest of the function body.
    EmitFunctionBody();

    // Emit the XRay table for this function.
    emitXRayTable();

    // We didn't modify anything.
    return false;
  }

private:
  void printOperand(const MachineInstr *MI, unsigned OpNum, raw_ostream &O);
  bool printAsmMRegister(const MachineOperand &MO, char Mode, raw_ostream &O);
  bool printAsmRegInClass(const MachineOperand &MO,
                          const TargetRegisterClass *RC, bool isVector,
                          raw_ostream &O);

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                       unsigned AsmVariant, const char *ExtraCode,
                       raw_ostream &O) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNum,
                             unsigned AsmVariant, const char *ExtraCode,
                             raw_ostream &O) override;

  void PrintDebugValueComment(const MachineInstr *MI, raw_ostream &OS);

  void EmitFunctionBodyEnd() override;

  MCSymbol *GetCPISymbol(unsigned CPID) const override;
  void EmitEndOfAsmFile(Module &M) override;

  AArch64FunctionInfo *AArch64FI = nullptr;

  /// Emit the LOHs contained in AArch64FI.
  void EmitLOHs();

  /// Emit instruction to set float register to zero.
  void EmitFMov0(const MachineInstr &MI);

  using MInstToMCSymbol = std::map<const MachineInstr *, MCSymbol *>;

  MInstToMCSymbol LOHInstToLabel;
};

} // end anonymous namespace

void AArch64AsmPrinter::LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI)
{
  EmitSled(MI, SledKind::FUNCTION_ENTER);
}

void AArch64AsmPrinter::LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI)
{
  EmitSled(MI, SledKind::FUNCTION_EXIT);
}

void AArch64AsmPrinter::LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI)
{
  EmitSled(MI, SledKind::TAIL_CALL);
}

void AArch64AsmPrinter::EmitSled(const MachineInstr &MI, SledKind Kind)
{
  static const int8_t NoopsInSledCount = 7;
  // We want to emit the following pattern:
  //
  // .Lxray_sled_N:
  //   ALIGN
  //   B #32
  //   ; 7 NOP instructions (28 bytes)
  // .tmpN
  //
  // We need the 28 bytes (7 instructions) because at runtime, we'd be patching
  // over the full 32 bytes (8 instructions) with the following pattern:
  //
  //   STP X0, X30, [SP, #-16]! ; push X0 and the link register to the stack
  //   LDR W0, #12 ; W0 := function ID
  //   LDR X16,#12 ; X16 := addr of __xray_FunctionEntry or __xray_FunctionExit
  //   BLR X16 ; call the tracing trampoline
  //   ;DATA: 32 bits of function ID
  //   ;DATA: lower 32 bits of the address of the trampoline
  //   ;DATA: higher 32 bits of the address of the trampoline
  //   LDP X0, X30, [SP], #16 ; pop X0 and the link register from the stack
  //
  OutStreamer->EmitCodeAlignment(4);
  auto CurSled = OutContext.createTempSymbol("xray_sled_", true);
  OutStreamer->EmitLabel(CurSled);
  auto Target = OutContext.createTempSymbol();

  // Emit "B #32" instruction, which jumps over the next 28 bytes.
  // The operand has to be the number of 4-byte instructions to jump over,
  // including the current instruction.
  EmitToStreamer(*OutStreamer, MCInstBuilder(AArch64::B).addImm(8));

  for (int8_t I = 0; I < NoopsInSledCount; I++)
    EmitToStreamer(*OutStreamer, MCInstBuilder(AArch64::HINT).addImm(0));

  OutStreamer->EmitLabel(Target);
  recordSled(CurSled, MI, Kind);
}

void AArch64AsmPrinter::EmitEndOfAsmFile(Module &M) {
  const Triple &TT = TM.getTargetTriple();
  if (TT.isOSBinFormatMachO()) {
    // Funny Darwin hack: This flag tells the linker that no global symbols
    // contain code that falls through to other global symbols (e.g. the obvious
    // implementation of multiple entry points).  If this doesn't occur, the
    // linker can safely perform dead code stripping.  Since LLVM never
    // generates code that does this, it is always safe to set.
    OutStreamer->EmitAssemblerFlag(MCAF_SubsectionsViaSymbols);
    emitStackMaps(SM);
  }
}

void AArch64AsmPrinter::EmitLOHs() {
  SmallVector<MCSymbol *, 3> MCArgs;

  for (const auto &D : AArch64FI->getLOHContainer()) {
    for (const MachineInstr *MI : D.getArgs()) {
      MInstToMCSymbol::iterator LabelIt = LOHInstToLabel.find(MI);
      assert(LabelIt != LOHInstToLabel.end() &&
             "Label hasn't been inserted for LOH related instruction");
      MCArgs.push_back(LabelIt->second);
    }
    OutStreamer->EmitLOHDirective(D.getKind(), MCArgs);
    MCArgs.clear();
  }
}

void AArch64AsmPrinter::EmitFunctionBodyEnd() {
  if (!AArch64FI->getLOHRelated().empty())
    EmitLOHs();
}

/// GetCPISymbol - Return the symbol for the specified constant pool entry.
MCSymbol *AArch64AsmPrinter::GetCPISymbol(unsigned CPID) const {
  // Darwin uses a linker-private symbol name for constant-pools (to
  // avoid addends on the relocation?), ELF has no such concept and
  // uses a normal private symbol.
  if (!getDataLayout().getLinkerPrivateGlobalPrefix().empty())
    return OutContext.getOrCreateSymbol(
        Twine(getDataLayout().getLinkerPrivateGlobalPrefix()) + "CPI" +
        Twine(getFunctionNumber()) + "_" + Twine(CPID));

  return AsmPrinter::GetCPISymbol(CPID);
}

void AArch64AsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNum,
                                     raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  switch (MO.getType()) {
  default:
    llvm_unreachable("<unknown operand type>");
  case MachineOperand::MO_Register: {
    unsigned Reg = MO.getReg();
    assert(TargetRegisterInfo::isPhysicalRegister(Reg));
    assert(!MO.getSubReg() && "Subregs should be eliminated!");
    O << AArch64InstPrinter::getRegisterName(Reg);
    break;
  }
  case MachineOperand::MO_Immediate: {
    int64_t Imm = MO.getImm();
    O << '#' << Imm;
    break;
  }
  case MachineOperand::MO_GlobalAddress: {
    const GlobalValue *GV = MO.getGlobal();
    MCSymbol *Sym = getSymbol(GV);

    // FIXME: Can we get anything other than a plain symbol here?
    assert(!MO.getTargetFlags() && "Unknown operand target flag!");

    Sym->print(O, MAI);
    printOffset(MO.getOffset(), O);
    break;
  }
  case MachineOperand::MO_BlockAddress: {
    MCSymbol *Sym = GetBlockAddressSymbol(MO.getBlockAddress());
    Sym->print(O, MAI);
    break;
  }
  }
}

bool AArch64AsmPrinter::printAsmMRegister(const MachineOperand &MO, char Mode,
                                          raw_ostream &O) {
  unsigned Reg = MO.getReg();
  switch (Mode) {
  default:
    return true; // Unknown mode.
  case 'w':
    Reg = getWRegFromXReg(Reg);
    break;
  case 'x':
    Reg = getXRegFromWReg(Reg);
    break;
  }

  O << AArch64InstPrinter::getRegisterName(Reg);
  return false;
}

// Prints the register in MO using class RC using the offset in the
// new register class. This should not be used for cross class
// printing.
bool AArch64AsmPrinter::printAsmRegInClass(const MachineOperand &MO,
                                           const TargetRegisterClass *RC,
                                           bool isVector, raw_ostream &O) {
  assert(MO.isReg() && "Should only get here with a register!");
  const TargetRegisterInfo *RI = STI->getRegisterInfo();
  unsigned Reg = MO.getReg();
  unsigned RegToPrint = RC->getRegister(RI->getEncodingValue(Reg));
  assert(RI->regsOverlap(RegToPrint, Reg));
  O << AArch64InstPrinter::getRegisterName(
           RegToPrint, isVector ? AArch64::vreg : AArch64::NoRegAltName);
  return false;
}

bool AArch64AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                                        unsigned AsmVariant,
                                        const char *ExtraCode, raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);

  // First try the generic code, which knows about modifiers like 'c' and 'n'.
  if (!AsmPrinter::PrintAsmOperand(MI, OpNum, AsmVariant, ExtraCode, O))
    return false;

  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      return true; // Unknown modifier.
    case 'a':      // Print 'a' modifier
      PrintAsmMemoryOperand(MI, OpNum, AsmVariant, ExtraCode, O);
      return false;
    case 'w':      // Print W register
    case 'x':      // Print X register
      if (MO.isReg())
        return printAsmMRegister(MO, ExtraCode[0], O);
      if (MO.isImm() && MO.getImm() == 0) {
        unsigned Reg = ExtraCode[0] == 'w' ? AArch64::WZR : AArch64::XZR;
        O << AArch64InstPrinter::getRegisterName(Reg);
        return false;
      }
      printOperand(MI, OpNum, O);
      return false;
    case 'b': // Print B register.
    case 'h': // Print H register.
    case 's': // Print S register.
    case 'd': // Print D register.
    case 'q': // Print Q register.
      if (MO.isReg()) {
        const TargetRegisterClass *RC;
        switch (ExtraCode[0]) {
        case 'b':
          RC = &AArch64::FPR8RegClass;
          break;
        case 'h':
          RC = &AArch64::FPR16RegClass;
          break;
        case 's':
          RC = &AArch64::FPR32RegClass;
          break;
        case 'd':
          RC = &AArch64::FPR64RegClass;
          break;
        case 'q':
          RC = &AArch64::FPR128RegClass;
          break;
        default:
          return true;
        }
        return printAsmRegInClass(MO, RC, false /* vector */, O);
      }
      printOperand(MI, OpNum, O);
      return false;
    }
  }

  // According to ARM, we should emit x and v registers unless we have a
  // modifier.
  if (MO.isReg()) {
    unsigned Reg = MO.getReg();

    // If this is a w or x register, print an x register.
    if (AArch64::GPR32allRegClass.contains(Reg) ||
        AArch64::GPR64allRegClass.contains(Reg))
      return printAsmMRegister(MO, 'x', O);

    // If this is a b, h, s, d, or q register, print it as a v register.
    return printAsmRegInClass(MO, &AArch64::FPR128RegClass, true /* vector */,
                              O);
  }

  printOperand(MI, OpNum, O);
  return false;
}

bool AArch64AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                              unsigned OpNum,
                                              unsigned AsmVariant,
                                              const char *ExtraCode,
                                              raw_ostream &O) {
  if (ExtraCode && ExtraCode[0] && ExtraCode[0] != 'a')
    return true; // Unknown modifier.

  const MachineOperand &MO = MI->getOperand(OpNum);
  assert(MO.isReg() && "unexpected inline asm memory operand");
  O << "[" << AArch64InstPrinter::getRegisterName(MO.getReg()) << "]";
  return false;
}

void AArch64AsmPrinter::PrintDebugValueComment(const MachineInstr *MI,
                                               raw_ostream &OS) {
  unsigned NOps = MI->getNumOperands();
  assert(NOps == 4);
  OS << '\t' << MAI->getCommentString() << "DEBUG_VALUE: ";
  // cast away const; DIetc do not take const operands for some reason.
  OS << cast<DILocalVariable>(MI->getOperand(NOps - 2).getMetadata())
            ->getName();
  OS << " <- ";
  // Frame address.  Currently handles register +- offset only.
  assert(MI->getOperand(0).isReg() && MI->getOperand(1).isImm());
  OS << '[';
  printOperand(MI, 0, OS);
  OS << '+';
  printOperand(MI, 1, OS);
  OS << ']';
  OS << "+";
  printOperand(MI, NOps - 2, OS);
}

void AArch64AsmPrinter::EmitJumpTableInfo() {
  const MachineJumpTableInfo *MJTI = MF->getJumpTableInfo();
  if (!MJTI) return;

  const std::vector<MachineJumpTableEntry> &JT = MJTI->getJumpTables();
  if (JT.empty()) return;

  const Function &F = MF->getFunction();
  const TargetLoweringObjectFile &TLOF = getObjFileLowering();
  bool JTInDiffSection =
      !STI->isTargetCOFF() ||
      !TLOF.shouldPutJumpTableInFunctionSection(
          MJTI->getEntryKind() == MachineJumpTableInfo::EK_LabelDifference32,
          F);
  if (JTInDiffSection) {
      // Drop it in the readonly section.
      MCSection *ReadOnlySec = TLOF.getSectionForJumpTable(F, TM);
      OutStreamer->SwitchSection(ReadOnlySec);
  }

  auto AFI = MF->getInfo<AArch64FunctionInfo>();
  for (unsigned JTI = 0, e = JT.size(); JTI != e; ++JTI) {
    const std::vector<MachineBasicBlock*> &JTBBs = JT[JTI].MBBs;

    // If this jump table was deleted, ignore it.
    if (JTBBs.empty()) continue;

    unsigned Size = AFI->getJumpTableEntrySize(JTI);
    EmitAlignment(Log2_32(Size));
    OutStreamer->EmitLabel(GetJTISymbol(JTI));

    for (auto *JTBB : JTBBs)
      emitJumpTableEntry(MJTI, JTBB, JTI);
  }
}

void AArch64AsmPrinter::emitJumpTableEntry(const MachineJumpTableInfo *MJTI,
                                           const MachineBasicBlock *MBB,
                                           unsigned JTI) {
  const MCExpr *Value = MCSymbolRefExpr::create(MBB->getSymbol(), OutContext);
  auto AFI = MF->getInfo<AArch64FunctionInfo>();
  unsigned Size = AFI->getJumpTableEntrySize(JTI);

  if (Size == 4) {
    // .word LBB - LJTI
    const TargetLowering *TLI = MF->getSubtarget().getTargetLowering();
    const MCExpr *Base = TLI->getPICJumpTableRelocBaseExpr(MF, JTI, OutContext);
    Value = MCBinaryExpr::createSub(Value, Base, OutContext);
  } else {
    // .byte (LBB - LBB) >> 2 (or .hword)
    const MCSymbol *BaseSym = AFI->getJumpTableEntryPCRelSymbol(JTI);
    const MCExpr *Base = MCSymbolRefExpr::create(BaseSym, OutContext);
    Value = MCBinaryExpr::createSub(Value, Base, OutContext);
    Value = MCBinaryExpr::createLShr(
        Value, MCConstantExpr::create(2, OutContext), OutContext);
  }

  OutStreamer->EmitValue(Value, Size);
}

/// Small jump tables contain an unsigned byte or half, representing the offset
/// from the lowest-addressed possible destination to the desired basic
/// block. Since all instructions are 4-byte aligned, this is further compressed
/// by counting in instructions rather than bytes (i.e. divided by 4). So, to
/// materialize the correct destination we need:
///
///             adr xDest, .LBB0_0
///             ldrb wScratch, [xTable, xEntry]   (with "lsl #1" for ldrh).
///             add xDest, xDest, xScratch, lsl #2
void AArch64AsmPrinter::LowerJumpTableDestSmall(llvm::MCStreamer &OutStreamer,
                                                const llvm::MachineInstr &MI) {
  unsigned DestReg = MI.getOperand(0).getReg();
  unsigned ScratchReg = MI.getOperand(1).getReg();
  unsigned ScratchRegW =
      STI->getRegisterInfo()->getSubReg(ScratchReg, AArch64::sub_32);
  unsigned TableReg = MI.getOperand(2).getReg();
  unsigned EntryReg = MI.getOperand(3).getReg();
  int JTIdx = MI.getOperand(4).getIndex();
  bool IsByteEntry = MI.getOpcode() == AArch64::JumpTableDest8;

  // This has to be first because the compression pass based its reachability
  // calculations on the start of the JumpTableDest instruction.
  auto Label =
      MF->getInfo<AArch64FunctionInfo>()->getJumpTableEntryPCRelSymbol(JTIdx);
  EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::ADR)
                                  .addReg(DestReg)
                                  .addExpr(MCSymbolRefExpr::create(
                                      Label, MF->getContext())));

  // Load the number of instruction-steps to offset from the label.
  unsigned LdrOpcode = IsByteEntry ? AArch64::LDRBBroX : AArch64::LDRHHroX;
  EmitToStreamer(OutStreamer, MCInstBuilder(LdrOpcode)
                                  .addReg(ScratchRegW)
                                  .addReg(TableReg)
                                  .addReg(EntryReg)
                                  .addImm(0)
                                  .addImm(IsByteEntry ? 0 : 1));

  // Multiply the steps by 4 and add to the already materialized base label
  // address.
  EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::ADDXrs)
                                  .addReg(DestReg)
                                  .addReg(DestReg)
                                  .addReg(ScratchReg)
                                  .addImm(2));
}

void AArch64AsmPrinter::LowerSTACKMAP(MCStreamer &OutStreamer, StackMaps &SM,
                                      const MachineInstr &MI) {
  unsigned NumNOPBytes = StackMapOpers(&MI).getNumPatchBytes();

  SM.recordStackMap(MI);
  assert(NumNOPBytes % 4 == 0 && "Invalid number of NOP bytes requested!");

  // Scan ahead to trim the shadow.
  const MachineBasicBlock &MBB = *MI.getParent();
  MachineBasicBlock::const_iterator MII(MI);
  ++MII;
  while (NumNOPBytes > 0) {
    if (MII == MBB.end() || MII->isCall() ||
        MII->getOpcode() == AArch64::DBG_VALUE ||
        MII->getOpcode() == TargetOpcode::PATCHPOINT ||
        MII->getOpcode() == TargetOpcode::STACKMAP)
      break;
    ++MII;
    NumNOPBytes -= 4;
  }

  // Emit nops.
  for (unsigned i = 0; i < NumNOPBytes; i += 4)
    EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::HINT).addImm(0));
}

// Lower a patchpoint of the form:
// [<def>], <id>, <numBytes>, <target>, <numArgs>
void AArch64AsmPrinter::LowerPATCHPOINT(MCStreamer &OutStreamer, StackMaps &SM,
                                        const MachineInstr &MI) {
  SM.recordPatchPoint(MI);

  PatchPointOpers Opers(&MI);

  int64_t CallTarget = Opers.getCallTarget().getImm();
  unsigned EncodedBytes = 0;
  if (CallTarget) {
    assert((CallTarget & 0xFFFFFFFFFFFF) == CallTarget &&
           "High 16 bits of call target should be zero.");
    unsigned ScratchReg = MI.getOperand(Opers.getNextScratchIdx()).getReg();
    EncodedBytes = 16;
    // Materialize the jump address:
    EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::MOVZXi)
                                    .addReg(ScratchReg)
                                    .addImm((CallTarget >> 32) & 0xFFFF)
                                    .addImm(32));
    EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::MOVKXi)
                                    .addReg(ScratchReg)
                                    .addReg(ScratchReg)
                                    .addImm((CallTarget >> 16) & 0xFFFF)
                                    .addImm(16));
    EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::MOVKXi)
                                    .addReg(ScratchReg)
                                    .addReg(ScratchReg)
                                    .addImm(CallTarget & 0xFFFF)
                                    .addImm(0));
    EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::BLR).addReg(ScratchReg));
  }
  // Emit padding.
  unsigned NumBytes = Opers.getNumPatchBytes();
  assert(NumBytes >= EncodedBytes &&
         "Patchpoint can't request size less than the length of a call.");
  assert((NumBytes - EncodedBytes) % 4 == 0 &&
         "Invalid number of NOP bytes requested!");
  for (unsigned i = EncodedBytes; i < NumBytes; i += 4)
    EmitToStreamer(OutStreamer, MCInstBuilder(AArch64::HINT).addImm(0));
}

void AArch64AsmPrinter::EmitFMov0(const MachineInstr &MI) {
  unsigned DestReg = MI.getOperand(0).getReg();
  if (STI->hasZeroCycleZeroingFP() && !STI->hasZeroCycleZeroingFPWorkaround()) {
    // Convert H/S/D register to corresponding Q register
    if (AArch64::H0 <= DestReg && DestReg <= AArch64::H31)
      DestReg = AArch64::Q0 + (DestReg - AArch64::H0);
    else if (AArch64::S0 <= DestReg && DestReg <= AArch64::S31)
      DestReg = AArch64::Q0 + (DestReg - AArch64::S0);
    else {
      assert(AArch64::D0 <= DestReg && DestReg <= AArch64::D31);
      DestReg = AArch64::Q0 + (DestReg - AArch64::D0);
    }
    MCInst MOVI;
    MOVI.setOpcode(AArch64::MOVIv2d_ns);
    MOVI.addOperand(MCOperand::createReg(DestReg));
    MOVI.addOperand(MCOperand::createImm(0));
    EmitToStreamer(*OutStreamer, MOVI);
  } else {
    MCInst FMov;
    switch (MI.getOpcode()) {
    default: llvm_unreachable("Unexpected opcode");
    case AArch64::FMOVH0:
      FMov.setOpcode(AArch64::FMOVWHr);
      FMov.addOperand(MCOperand::createReg(DestReg));
      FMov.addOperand(MCOperand::createReg(AArch64::WZR));
      break;
    case AArch64::FMOVS0:
      FMov.setOpcode(AArch64::FMOVWSr);
      FMov.addOperand(MCOperand::createReg(DestReg));
      FMov.addOperand(MCOperand::createReg(AArch64::WZR));
      break;
    case AArch64::FMOVD0:
      FMov.setOpcode(AArch64::FMOVXDr);
      FMov.addOperand(MCOperand::createReg(DestReg));
      FMov.addOperand(MCOperand::createReg(AArch64::XZR));
      break;
    }
    EmitToStreamer(*OutStreamer, FMov);
  }
}

// Simple pseudo-instructions have their lowering (with expansion to real
// instructions) auto-generated.
#include "AArch64GenMCPseudoLowering.inc"

void AArch64AsmPrinter::EmitInstruction(const MachineInstr *MI) {
  // Do any auto-generated pseudo lowerings.
  if (emitPseudoExpansionLowering(*OutStreamer, MI))
    return;

  if (AArch64FI->getLOHRelated().count(MI)) {
    // Generate a label for LOH related instruction
    MCSymbol *LOHLabel = createTempSymbol("loh");
    // Associate the instruction with the label
    LOHInstToLabel[MI] = LOHLabel;
    OutStreamer->EmitLabel(LOHLabel);
  }

  AArch64TargetStreamer *TS =
    static_cast<AArch64TargetStreamer *>(OutStreamer->getTargetStreamer());
  // Do any manual lowerings.
  switch (MI->getOpcode()) {
  default:
    break;
    case AArch64::MOVMCSym: {
    unsigned DestReg = MI->getOperand(0).getReg();
    const MachineOperand &MO_Sym = MI->getOperand(1);
    MachineOperand Hi_MOSym(MO_Sym), Lo_MOSym(MO_Sym);
    MCOperand Hi_MCSym, Lo_MCSym;

    Hi_MOSym.setTargetFlags(AArch64II::MO_G1 | AArch64II::MO_S);
    Lo_MOSym.setTargetFlags(AArch64II::MO_G0 | AArch64II::MO_NC);

    MCInstLowering.lowerOperand(Hi_MOSym, Hi_MCSym);
    MCInstLowering.lowerOperand(Lo_MOSym, Lo_MCSym);

    MCInst MovZ;
    MovZ.setOpcode(AArch64::MOVZXi);
    MovZ.addOperand(MCOperand::createReg(DestReg));
    MovZ.addOperand(Hi_MCSym);
    MovZ.addOperand(MCOperand::createImm(16));
    EmitToStreamer(*OutStreamer, MovZ);

    MCInst MovK;
    MovK.setOpcode(AArch64::MOVKXi);
    MovK.addOperand(MCOperand::createReg(DestReg));
    MovK.addOperand(MCOperand::createReg(DestReg));
    MovK.addOperand(Lo_MCSym);
    MovK.addOperand(MCOperand::createImm(0));
    EmitToStreamer(*OutStreamer, MovK);
    return;
  }
  case AArch64::MOVIv2d_ns:
    // If the target has <rdar://problem/16473581>, lower this
    // instruction to movi.16b instead.
    if (STI->hasZeroCycleZeroingFPWorkaround() &&
        MI->getOperand(1).getImm() == 0) {
      MCInst TmpInst;
      TmpInst.setOpcode(AArch64::MOVIv16b_ns);
      TmpInst.addOperand(MCOperand::createReg(MI->getOperand(0).getReg()));
      TmpInst.addOperand(MCOperand::createImm(MI->getOperand(1).getImm()));
      EmitToStreamer(*OutStreamer, TmpInst);
      return;
    }
    break;

  case AArch64::DBG_VALUE: {
    if (isVerbose() && OutStreamer->hasRawTextSupport()) {
      SmallString<128> TmpStr;
      raw_svector_ostream OS(TmpStr);
      PrintDebugValueComment(MI, OS);
      OutStreamer->EmitRawText(StringRef(OS.str()));
    }
    return;

  case AArch64::EMITBKEY: {
      ExceptionHandling ExceptionHandlingType = MAI->getExceptionHandlingType();
      if (ExceptionHandlingType != ExceptionHandling::DwarfCFI &&
          ExceptionHandlingType != ExceptionHandling::ARM)
        return;

      if (needsCFIMoves() == CFI_M_None)
        return;

      OutStreamer->EmitCFIBKeyFrame();
      return;
    }
  }

  // Tail calls use pseudo instructions so they have the proper code-gen
  // attributes (isCall, isReturn, etc.). We lower them to the real
  // instruction here.
  case AArch64::TCRETURNri:
  case AArch64::TCRETURNriBTI:
  case AArch64::TCRETURNriALL: {
    MCInst TmpInst;
    TmpInst.setOpcode(AArch64::BR);
    TmpInst.addOperand(MCOperand::createReg(MI->getOperand(0).getReg()));
    EmitToStreamer(*OutStreamer, TmpInst);
    return;
  }
  case AArch64::TCRETURNdi: {
    MCOperand Dest;
    MCInstLowering.lowerOperand(MI->getOperand(0), Dest);
    MCInst TmpInst;
    TmpInst.setOpcode(AArch64::B);
    TmpInst.addOperand(Dest);
    EmitToStreamer(*OutStreamer, TmpInst);
    return;
  }
  case AArch64::TLSDESC_CALLSEQ: {
    /// lower this to:
    ///    adrp  x0, :tlsdesc:var
    ///    ldr   x1, [x0, #:tlsdesc_lo12:var]
    ///    add   x0, x0, #:tlsdesc_lo12:var
    ///    .tlsdesccall var
    ///    blr   x1
    ///    (TPIDR_EL0 offset now in x0)
    const MachineOperand &MO_Sym = MI->getOperand(0);
    MachineOperand MO_TLSDESC_LO12(MO_Sym), MO_TLSDESC(MO_Sym);
    MCOperand Sym, SymTLSDescLo12, SymTLSDesc;
    MO_TLSDESC_LO12.setTargetFlags(AArch64II::MO_TLS | AArch64II::MO_PAGEOFF);
    MO_TLSDESC.setTargetFlags(AArch64II::MO_TLS | AArch64II::MO_PAGE);
    MCInstLowering.lowerOperand(MO_Sym, Sym);
    MCInstLowering.lowerOperand(MO_TLSDESC_LO12, SymTLSDescLo12);
    MCInstLowering.lowerOperand(MO_TLSDESC, SymTLSDesc);

    MCInst Adrp;
    Adrp.setOpcode(AArch64::ADRP);
    Adrp.addOperand(MCOperand::createReg(AArch64::X0));
    Adrp.addOperand(SymTLSDesc);
    EmitToStreamer(*OutStreamer, Adrp);

    MCInst Ldr;
    Ldr.setOpcode(AArch64::LDRXui);
    Ldr.addOperand(MCOperand::createReg(AArch64::X1));
    Ldr.addOperand(MCOperand::createReg(AArch64::X0));
    Ldr.addOperand(SymTLSDescLo12);
    Ldr.addOperand(MCOperand::createImm(0));
    EmitToStreamer(*OutStreamer, Ldr);

    MCInst Add;
    Add.setOpcode(AArch64::ADDXri);
    Add.addOperand(MCOperand::createReg(AArch64::X0));
    Add.addOperand(MCOperand::createReg(AArch64::X0));
    Add.addOperand(SymTLSDescLo12);
    Add.addOperand(MCOperand::createImm(AArch64_AM::getShiftValue(0)));
    EmitToStreamer(*OutStreamer, Add);

    // Emit a relocation-annotation. This expands to no code, but requests
    // the following instruction gets an R_AARCH64_TLSDESC_CALL.
    MCInst TLSDescCall;
    TLSDescCall.setOpcode(AArch64::TLSDESCCALL);
    TLSDescCall.addOperand(Sym);
    EmitToStreamer(*OutStreamer, TLSDescCall);

    MCInst Blr;
    Blr.setOpcode(AArch64::BLR);
    Blr.addOperand(MCOperand::createReg(AArch64::X1));
    EmitToStreamer(*OutStreamer, Blr);

    return;
  }

  case AArch64::JumpTableDest32: {
    // We want:
    //     ldrsw xScratch, [xTable, xEntry, lsl #2]
    //     add xDest, xTable, xScratch
    unsigned DestReg = MI->getOperand(0).getReg(),
             ScratchReg = MI->getOperand(1).getReg(),
             TableReg = MI->getOperand(2).getReg(),
             EntryReg = MI->getOperand(3).getReg();
    EmitToStreamer(*OutStreamer, MCInstBuilder(AArch64::LDRSWroX)
                                     .addReg(ScratchReg)
                                     .addReg(TableReg)
                                     .addReg(EntryReg)
                                     .addImm(0)
                                     .addImm(1));
    EmitToStreamer(*OutStreamer, MCInstBuilder(AArch64::ADDXrs)
                                     .addReg(DestReg)
                                     .addReg(TableReg)
                                     .addReg(ScratchReg)
                                     .addImm(0));
    return;
  }
  case AArch64::JumpTableDest16:
  case AArch64::JumpTableDest8:
    LowerJumpTableDestSmall(*OutStreamer, *MI);
    return;

  case AArch64::FMOVH0:
  case AArch64::FMOVS0:
  case AArch64::FMOVD0:
    EmitFMov0(*MI);
    return;

  case TargetOpcode::STACKMAP:
    return LowerSTACKMAP(*OutStreamer, SM, *MI);

  case TargetOpcode::PATCHPOINT:
    return LowerPATCHPOINT(*OutStreamer, SM, *MI);

  case TargetOpcode::PATCHABLE_FUNCTION_ENTER:
    LowerPATCHABLE_FUNCTION_ENTER(*MI);
    return;

  case TargetOpcode::PATCHABLE_FUNCTION_EXIT:
    LowerPATCHABLE_FUNCTION_EXIT(*MI);
    return;

  case TargetOpcode::PATCHABLE_TAIL_CALL:
    LowerPATCHABLE_TAIL_CALL(*MI);
    return;

  case AArch64::SEH_StackAlloc:
    TS->EmitARM64WinCFIAllocStack(MI->getOperand(0).getImm());
    return;

  case AArch64::SEH_SaveFPLR:
    TS->EmitARM64WinCFISaveFPLR(MI->getOperand(0).getImm());
    return;

  case AArch64::SEH_SaveFPLR_X:
    assert(MI->getOperand(0).getImm() < 0 &&
           "Pre increment SEH opcode must have a negative offset");
    TS->EmitARM64WinCFISaveFPLRX(-MI->getOperand(0).getImm());
    return;

  case AArch64::SEH_SaveReg:
    TS->EmitARM64WinCFISaveReg(MI->getOperand(0).getImm(),
                               MI->getOperand(1).getImm());
    return;

  case AArch64::SEH_SaveReg_X:
    assert(MI->getOperand(1).getImm() < 0 &&
           "Pre increment SEH opcode must have a negative offset");
    TS->EmitARM64WinCFISaveRegX(MI->getOperand(0).getImm(),
		                -MI->getOperand(1).getImm());
    return;

  case AArch64::SEH_SaveRegP:
    assert((MI->getOperand(1).getImm() - MI->getOperand(0).getImm() == 1) &&
            "Non-consecutive registers not allowed for save_regp");
    TS->EmitARM64WinCFISaveRegP(MI->getOperand(0).getImm(),
                                MI->getOperand(2).getImm());
    return;

  case AArch64::SEH_SaveRegP_X:
    assert((MI->getOperand(1).getImm() - MI->getOperand(0).getImm() == 1) &&
            "Non-consecutive registers not allowed for save_regp_x");
    assert(MI->getOperand(2).getImm() < 0 &&
           "Pre increment SEH opcode must have a negative offset");
    TS->EmitARM64WinCFISaveRegPX(MI->getOperand(0).getImm(),
                                 -MI->getOperand(2).getImm());
    return;

  case AArch64::SEH_SaveFReg:
    TS->EmitARM64WinCFISaveFReg(MI->getOperand(0).getImm(),
                                MI->getOperand(1).getImm());
    return;

  case AArch64::SEH_SaveFReg_X:
    assert(MI->getOperand(1).getImm() < 0 &&
           "Pre increment SEH opcode must have a negative offset");
    TS->EmitARM64WinCFISaveFRegX(MI->getOperand(0).getImm(),
                                 -MI->getOperand(1).getImm());
    return;

  case AArch64::SEH_SaveFRegP:
    assert((MI->getOperand(1).getImm() - MI->getOperand(0).getImm() == 1) &&
            "Non-consecutive registers not allowed for save_regp");
    TS->EmitARM64WinCFISaveFRegP(MI->getOperand(0).getImm(),
                                 MI->getOperand(2).getImm());
    return;

  case AArch64::SEH_SaveFRegP_X:
    assert((MI->getOperand(1).getImm() - MI->getOperand(0).getImm() == 1) &&
            "Non-consecutive registers not allowed for save_regp_x");
    assert(MI->getOperand(2).getImm() < 0 &&
           "Pre increment SEH opcode must have a negative offset");
    TS->EmitARM64WinCFISaveFRegPX(MI->getOperand(0).getImm(),
                                  -MI->getOperand(2).getImm());
    return;

  case AArch64::SEH_SetFP:
    TS->EmitARM64WinCFISetFP();
    return;

  case AArch64::SEH_AddFP:
    TS->EmitARM64WinCFIAddFP(MI->getOperand(0).getImm());
    return;

  case AArch64::SEH_Nop:
    TS->EmitARM64WinCFINop();
    return;

  case AArch64::SEH_PrologEnd:
    TS->EmitARM64WinCFIPrologEnd();
    return;

  case AArch64::SEH_EpilogStart:
    TS->EmitARM64WinCFIEpilogStart();
    return;

  case AArch64::SEH_EpilogEnd:
    TS->EmitARM64WinCFIEpilogEnd();
    return;
  }

  // Finally, do the automated lowerings for everything else.
  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

// Force static initialization.
extern "C" void LLVMInitializeAArch64AsmPrinter() {
  RegisterAsmPrinter<AArch64AsmPrinter> X(getTheAArch64leTarget());
  RegisterAsmPrinter<AArch64AsmPrinter> Y(getTheAArch64beTarget());
  RegisterAsmPrinter<AArch64AsmPrinter> Z(getTheARM64Target());
}
