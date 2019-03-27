//===-- X86AsmPrinter.cpp - Convert X86 LLVM code to AT&T assembly --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to X86 machine code.
//
//===----------------------------------------------------------------------===//

#include "X86AsmPrinter.h"
#include "InstPrinter/X86ATTInstPrinter.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86TargetStreamer.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

X86AsmPrinter::X86AsmPrinter(TargetMachine &TM,
                             std::unique_ptr<MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)), SM(*this), FM(*this) {}

//===----------------------------------------------------------------------===//
// Primitive Helper Functions.
//===----------------------------------------------------------------------===//

/// runOnMachineFunction - Emit the function body.
///
bool X86AsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<X86Subtarget>();

  SMShadowTracker.startFunction(MF);
  CodeEmitter.reset(TM.getTarget().createMCCodeEmitter(
      *Subtarget->getInstrInfo(), *Subtarget->getRegisterInfo(),
      MF.getContext()));

  EmitFPOData =
      Subtarget->isTargetWin32() && MF.getMMI().getModule()->getCodeViewFlag();

  SetupMachineFunction(MF);

  if (Subtarget->isTargetCOFF()) {
    bool Local = MF.getFunction().hasLocalLinkage();
    OutStreamer->BeginCOFFSymbolDef(CurrentFnSym);
    OutStreamer->EmitCOFFSymbolStorageClass(
        Local ? COFF::IMAGE_SYM_CLASS_STATIC : COFF::IMAGE_SYM_CLASS_EXTERNAL);
    OutStreamer->EmitCOFFSymbolType(COFF::IMAGE_SYM_DTYPE_FUNCTION
                                               << COFF::SCT_COMPLEX_TYPE_SHIFT);
    OutStreamer->EndCOFFSymbolDef();
  }

  // Emit the rest of the function body.
  EmitFunctionBody();

  // Emit the XRay table for this function.
  emitXRayTable();

  EmitFPOData = false;

  // We didn't modify anything.
  return false;
}

void X86AsmPrinter::EmitFunctionBodyStart() {
  if (EmitFPOData) {
    if (auto *XTS =
        static_cast<X86TargetStreamer *>(OutStreamer->getTargetStreamer()))
      XTS->emitFPOProc(
          CurrentFnSym,
          MF->getInfo<X86MachineFunctionInfo>()->getArgumentStackSize());
  }
}

void X86AsmPrinter::EmitFunctionBodyEnd() {
  if (EmitFPOData) {
    if (auto *XTS =
            static_cast<X86TargetStreamer *>(OutStreamer->getTargetStreamer()))
      XTS->emitFPOEndProc();
  }
}

/// printSymbolOperand - Print a raw symbol reference operand.  This handles
/// jump tables, constant pools, global address and external symbols, all of
/// which print to a label with various suffixes for relocation types etc.
static void printSymbolOperand(X86AsmPrinter &P, const MachineOperand &MO,
                               raw_ostream &O) {
  switch (MO.getType()) {
  default: llvm_unreachable("unknown symbol type!");
  case MachineOperand::MO_ConstantPoolIndex:
    P.GetCPISymbol(MO.getIndex())->print(O, P.MAI);
    P.printOffset(MO.getOffset(), O);
    break;
  case MachineOperand::MO_GlobalAddress: {
    const GlobalValue *GV = MO.getGlobal();

    MCSymbol *GVSym;
    if (MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY ||
        MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY_PIC_BASE)
      GVSym = P.getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr");
    else
      GVSym = P.getSymbol(GV);

    // Handle dllimport linkage.
    if (MO.getTargetFlags() == X86II::MO_DLLIMPORT)
      GVSym =
          P.OutContext.getOrCreateSymbol(Twine("__imp_") + GVSym->getName());
    else if (MO.getTargetFlags() == X86II::MO_COFFSTUB)
      GVSym =
          P.OutContext.getOrCreateSymbol(Twine(".refptr.") + GVSym->getName());

    if (MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY ||
        MO.getTargetFlags() == X86II::MO_DARWIN_NONLAZY_PIC_BASE) {
      MCSymbol *Sym = P.getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr");
      MachineModuleInfoImpl::StubValueTy &StubSym =
          P.MMI->getObjFileInfo<MachineModuleInfoMachO>().getGVStubEntry(Sym);
      if (!StubSym.getPointer())
        StubSym = MachineModuleInfoImpl::
          StubValueTy(P.getSymbol(GV), !GV->hasInternalLinkage());
    }

    // If the name begins with a dollar-sign, enclose it in parens.  We do this
    // to avoid having it look like an integer immediate to the assembler.
    if (GVSym->getName()[0] != '$')
      GVSym->print(O, P.MAI);
    else {
      O << '(';
      GVSym->print(O, P.MAI);
      O << ')';
    }
    P.printOffset(MO.getOffset(), O);
    break;
  }
  }

  switch (MO.getTargetFlags()) {
  default:
    llvm_unreachable("Unknown target flag on GV operand");
  case X86II::MO_NO_FLAG:    // No flag.
    break;
  case X86II::MO_DARWIN_NONLAZY:
  case X86II::MO_DLLIMPORT:
  case X86II::MO_COFFSTUB:
    // These affect the name of the symbol, not any suffix.
    break;
  case X86II::MO_GOT_ABSOLUTE_ADDRESS:
    O << " + [.-";
    P.MF->getPICBaseSymbol()->print(O, P.MAI);
    O << ']';
    break;
  case X86II::MO_PIC_BASE_OFFSET:
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE:
    O << '-';
    P.MF->getPICBaseSymbol()->print(O, P.MAI);
    break;
  case X86II::MO_TLSGD:     O << "@TLSGD";     break;
  case X86II::MO_TLSLD:     O << "@TLSLD";     break;
  case X86II::MO_TLSLDM:    O << "@TLSLDM";    break;
  case X86II::MO_GOTTPOFF:  O << "@GOTTPOFF";  break;
  case X86II::MO_INDNTPOFF: O << "@INDNTPOFF"; break;
  case X86II::MO_TPOFF:     O << "@TPOFF";     break;
  case X86II::MO_DTPOFF:    O << "@DTPOFF";    break;
  case X86II::MO_NTPOFF:    O << "@NTPOFF";    break;
  case X86II::MO_GOTNTPOFF: O << "@GOTNTPOFF"; break;
  case X86II::MO_GOTPCREL:  O << "@GOTPCREL";  break;
  case X86II::MO_GOT:       O << "@GOT";       break;
  case X86II::MO_GOTOFF:    O << "@GOTOFF";    break;
  case X86II::MO_PLT:       O << "@PLT";       break;
  case X86II::MO_TLVP:      O << "@TLVP";      break;
  case X86II::MO_TLVP_PIC_BASE:
    O << "@TLVP" << '-';
    P.MF->getPICBaseSymbol()->print(O, P.MAI);
    break;
  case X86II::MO_SECREL:    O << "@SECREL32";  break;
  }
}

static void printOperand(X86AsmPrinter &P, const MachineInstr *MI,
                         unsigned OpNo, raw_ostream &O,
                         const char *Modifier = nullptr, unsigned AsmVariant = 0);

/// printPCRelImm - This is used to print an immediate value that ends up
/// being encoded as a pc-relative value.  These print slightly differently, for
/// example, a $ is not emitted.
static void printPCRelImm(X86AsmPrinter &P, const MachineInstr *MI,
                          unsigned OpNo, raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  default: llvm_unreachable("Unknown pcrel immediate operand");
  case MachineOperand::MO_Register:
    // pc-relativeness was handled when computing the value in the reg.
    printOperand(P, MI, OpNo, O);
    return;
  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    return;
  case MachineOperand::MO_GlobalAddress:
    printSymbolOperand(P, MO, O);
    return;
  }
}

static void printOperand(X86AsmPrinter &P, const MachineInstr *MI,
                         unsigned OpNo, raw_ostream &O, const char *Modifier,
                         unsigned AsmVariant) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  default: llvm_unreachable("unknown operand type!");
  case MachineOperand::MO_Register: {
    // FIXME: Enumerating AsmVariant, so we can remove magic number.
    if (AsmVariant == 0) O << '%';
    unsigned Reg = MO.getReg();
    if (Modifier && strncmp(Modifier, "subreg", strlen("subreg")) == 0) {
      unsigned Size = (strcmp(Modifier+6,"64") == 0) ? 64 :
                      (strcmp(Modifier+6,"32") == 0) ? 32 :
                      (strcmp(Modifier+6,"16") == 0) ? 16 : 8;
      Reg = getX86SubSuperRegister(Reg, Size);
    }
    O << X86ATTInstPrinter::getRegisterName(Reg);
    return;
  }

  case MachineOperand::MO_Immediate:
    if (AsmVariant == 0) O << '$';
    O << MO.getImm();
    return;

  case MachineOperand::MO_GlobalAddress: {
    if (AsmVariant == 0) O << '$';
    printSymbolOperand(P, MO, O);
    break;
  }
  }
}

static void printLeaMemReference(X86AsmPrinter &P, const MachineInstr *MI,
                                 unsigned Op, raw_ostream &O,
                                 const char *Modifier = nullptr) {
  const MachineOperand &BaseReg  = MI->getOperand(Op+X86::AddrBaseReg);
  const MachineOperand &IndexReg = MI->getOperand(Op+X86::AddrIndexReg);
  const MachineOperand &DispSpec = MI->getOperand(Op+X86::AddrDisp);

  // If we really don't want to print out (rip), don't.
  bool HasBaseReg = BaseReg.getReg() != 0;
  if (HasBaseReg && Modifier && !strcmp(Modifier, "no-rip") &&
      BaseReg.getReg() == X86::RIP)
    HasBaseReg = false;

  // HasParenPart - True if we will print out the () part of the mem ref.
  bool HasParenPart = IndexReg.getReg() || HasBaseReg;

  switch (DispSpec.getType()) {
  default:
    llvm_unreachable("unknown operand type!");
  case MachineOperand::MO_Immediate: {
    int DispVal = DispSpec.getImm();
    if (DispVal || !HasParenPart)
      O << DispVal;
    break;
  }
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ConstantPoolIndex:
    printSymbolOperand(P, DispSpec, O);
  }

  if (Modifier && strcmp(Modifier, "H") == 0)
    O << "+8";

  if (HasParenPart) {
    assert(IndexReg.getReg() != X86::ESP &&
           "X86 doesn't allow scaling by ESP");

    O << '(';
    if (HasBaseReg)
      printOperand(P, MI, Op+X86::AddrBaseReg, O, Modifier);

    if (IndexReg.getReg()) {
      O << ',';
      printOperand(P, MI, Op+X86::AddrIndexReg, O, Modifier);
      unsigned ScaleVal = MI->getOperand(Op+X86::AddrScaleAmt).getImm();
      if (ScaleVal != 1)
        O << ',' << ScaleVal;
    }
    O << ')';
  }
}

static void printMemReference(X86AsmPrinter &P, const MachineInstr *MI,
                              unsigned Op, raw_ostream &O,
                              const char *Modifier = nullptr) {
  assert(isMem(*MI, Op) && "Invalid memory reference!");
  const MachineOperand &Segment = MI->getOperand(Op+X86::AddrSegmentReg);
  if (Segment.getReg()) {
    printOperand(P, MI, Op+X86::AddrSegmentReg, O, Modifier);
    O << ':';
  }
  printLeaMemReference(P, MI, Op, O, Modifier);
}

static void printIntelMemReference(X86AsmPrinter &P, const MachineInstr *MI,
                                   unsigned Op, raw_ostream &O,
                                   const char *Modifier = nullptr,
                                   unsigned AsmVariant = 1) {
  const MachineOperand &BaseReg  = MI->getOperand(Op+X86::AddrBaseReg);
  unsigned ScaleVal = MI->getOperand(Op+X86::AddrScaleAmt).getImm();
  const MachineOperand &IndexReg = MI->getOperand(Op+X86::AddrIndexReg);
  const MachineOperand &DispSpec = MI->getOperand(Op+X86::AddrDisp);
  const MachineOperand &SegReg   = MI->getOperand(Op+X86::AddrSegmentReg);

  // If this has a segment register, print it.
  if (SegReg.getReg()) {
    printOperand(P, MI, Op+X86::AddrSegmentReg, O, Modifier, AsmVariant);
    O << ':';
  }

  O << '[';

  bool NeedPlus = false;
  if (BaseReg.getReg()) {
    printOperand(P, MI, Op+X86::AddrBaseReg, O, Modifier, AsmVariant);
    NeedPlus = true;
  }

  if (IndexReg.getReg()) {
    if (NeedPlus) O << " + ";
    if (ScaleVal != 1)
      O << ScaleVal << '*';
    printOperand(P, MI, Op+X86::AddrIndexReg, O, Modifier, AsmVariant);
    NeedPlus = true;
  }

  if (!DispSpec.isImm()) {
    if (NeedPlus) O << " + ";
    printOperand(P, MI, Op+X86::AddrDisp, O, Modifier, AsmVariant);
  } else {
    int64_t DispVal = DispSpec.getImm();
    if (DispVal || (!IndexReg.getReg() && !BaseReg.getReg())) {
      if (NeedPlus) {
        if (DispVal > 0)
          O << " + ";
        else {
          O << " - ";
          DispVal = -DispVal;
        }
      }
      O << DispVal;
    }
  }
  O << ']';
}

static bool printAsmMRegister(X86AsmPrinter &P, const MachineOperand &MO,
                              char Mode, raw_ostream &O) {
  unsigned Reg = MO.getReg();
  bool EmitPercent = true;

  if (!X86::GR8RegClass.contains(Reg) &&
      !X86::GR16RegClass.contains(Reg) &&
      !X86::GR32RegClass.contains(Reg) &&
      !X86::GR64RegClass.contains(Reg))
    return true;

  switch (Mode) {
  default: return true;  // Unknown mode.
  case 'b': // Print QImode register
    Reg = getX86SubSuperRegister(Reg, 8);
    break;
  case 'h': // Print QImode high register
    Reg = getX86SubSuperRegister(Reg, 8, true);
    break;
  case 'w': // Print HImode register
    Reg = getX86SubSuperRegister(Reg, 16);
    break;
  case 'k': // Print SImode register
    Reg = getX86SubSuperRegister(Reg, 32);
    break;
  case 'V':
    EmitPercent = false;
    LLVM_FALLTHROUGH;
  case 'q':
    // Print 64-bit register names if 64-bit integer registers are available.
    // Otherwise, print 32-bit register names.
    Reg = getX86SubSuperRegister(Reg, P.getSubtarget().is64Bit() ? 64 : 32);
    break;
  }

  if (EmitPercent)
    O << '%';

  O << X86ATTInstPrinter::getRegisterName(Reg);
  return false;
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
///
bool X86AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                    unsigned AsmVariant,
                                    const char *ExtraCode, raw_ostream &O) {
  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    const MachineOperand &MO = MI->getOperand(OpNo);

    switch (ExtraCode[0]) {
    default:
      // See if this is a generic print operand
      return AsmPrinter::PrintAsmOperand(MI, OpNo, AsmVariant, ExtraCode, O);
    case 'a': // This is an address.  Currently only 'i' and 'r' are expected.
      switch (MO.getType()) {
      default:
        return true;
      case MachineOperand::MO_Immediate:
        O << MO.getImm();
        return false;
      case MachineOperand::MO_ConstantPoolIndex:
      case MachineOperand::MO_JumpTableIndex:
      case MachineOperand::MO_ExternalSymbol:
        llvm_unreachable("unexpected operand type!");
      case MachineOperand::MO_GlobalAddress:
        printSymbolOperand(*this, MO, O);
        if (Subtarget->isPICStyleRIPRel())
          O << "(%rip)";
        return false;
      case MachineOperand::MO_Register:
        O << '(';
        printOperand(*this, MI, OpNo, O);
        O << ')';
        return false;
      }

    case 'c': // Don't print "$" before a global var name or constant.
      switch (MO.getType()) {
      default:
        printOperand(*this, MI, OpNo, O);
        break;
      case MachineOperand::MO_Immediate:
        O << MO.getImm();
        break;
      case MachineOperand::MO_ConstantPoolIndex:
      case MachineOperand::MO_JumpTableIndex:
      case MachineOperand::MO_ExternalSymbol:
        llvm_unreachable("unexpected operand type!");
      case MachineOperand::MO_GlobalAddress:
        printSymbolOperand(*this, MO, O);
        break;
      }
      return false;

    case 'A': // Print '*' before a register (it must be a register)
      if (MO.isReg()) {
        O << '*';
        printOperand(*this, MI, OpNo, O);
        return false;
      }
      return true;

    case 'b': // Print QImode register
    case 'h': // Print QImode high register
    case 'w': // Print HImode register
    case 'k': // Print SImode register
    case 'q': // Print DImode register
    case 'V': // Print native register without '%'
      if (MO.isReg())
        return printAsmMRegister(*this, MO, ExtraCode[0], O);
      printOperand(*this, MI, OpNo, O);
      return false;

    case 'P': // This is the operand of a call, treat specially.
      printPCRelImm(*this, MI, OpNo, O);
      return false;

    case 'n': // Negate the immediate or print a '-' before the operand.
      // Note: this is a temporary solution. It should be handled target
      // independently as part of the 'MC' work.
      if (MO.isImm()) {
        O << -MO.getImm();
        return false;
      }
      O << '-';
    }
  }

  printOperand(*this, MI, OpNo, O, /*Modifier*/ nullptr, AsmVariant);
  return false;
}

bool X86AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                          unsigned OpNo, unsigned AsmVariant,
                                          const char *ExtraCode,
                                          raw_ostream &O) {
  if (AsmVariant) {
    printIntelMemReference(*this, MI, OpNo, O);
    return false;
  }

  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default: return true;  // Unknown modifier.
    case 'b': // Print QImode register
    case 'h': // Print QImode high register
    case 'w': // Print HImode register
    case 'k': // Print SImode register
    case 'q': // Print SImode register
      // These only apply to registers, ignore on mem.
      break;
    case 'H':
      printMemReference(*this, MI, OpNo, O, "H");
      return false;
    case 'P': // Don't print @PLT, but do print as memory.
      printMemReference(*this, MI, OpNo, O, "no-rip");
      return false;
    }
  }
  printMemReference(*this, MI, OpNo, O);
  return false;
}

void X86AsmPrinter::EmitStartOfAsmFile(Module &M) {
  const Triple &TT = TM.getTargetTriple();

  if (TT.isOSBinFormatELF()) {
    // Assemble feature flags that may require creation of a note section.
    unsigned FeatureFlagsAnd = 0;
    if (M.getModuleFlag("cf-protection-branch"))
      FeatureFlagsAnd |= ELF::GNU_PROPERTY_X86_FEATURE_1_IBT;
    if (M.getModuleFlag("cf-protection-return"))
      FeatureFlagsAnd |= ELF::GNU_PROPERTY_X86_FEATURE_1_SHSTK;

    if (FeatureFlagsAnd) {
      // Emit a .note.gnu.property section with the flags.
      if (!TT.isArch32Bit() && !TT.isArch64Bit())
        llvm_unreachable("CFProtection used on invalid architecture!");
      MCSection *Cur = OutStreamer->getCurrentSectionOnly();
      MCSection *Nt = MMI->getContext().getELFSection(
          ".note.gnu.property", ELF::SHT_NOTE, ELF::SHF_ALLOC);
      OutStreamer->SwitchSection(Nt);

      // Emitting note header.
      int WordSize = TT.isArch64Bit() ? 8 : 4;
      EmitAlignment(WordSize == 4 ? 2 : 3);
      OutStreamer->EmitIntValue(4, 4 /*size*/); // data size for "GNU\0"
      OutStreamer->EmitIntValue(8 + WordSize, 4 /*size*/); // Elf_Prop size
      OutStreamer->EmitIntValue(ELF::NT_GNU_PROPERTY_TYPE_0, 4 /*size*/);
      OutStreamer->EmitBytes(StringRef("GNU", 4)); // note name

      // Emitting an Elf_Prop for the CET properties.
      OutStreamer->EmitIntValue(ELF::GNU_PROPERTY_X86_FEATURE_1_AND, 4);
      OutStreamer->EmitIntValue(4, 4);               // data size
      OutStreamer->EmitIntValue(FeatureFlagsAnd, 4); // data
      EmitAlignment(WordSize == 4 ? 2 : 3);          // padding

      OutStreamer->endSection(Nt);
      OutStreamer->SwitchSection(Cur);
    }
  }

  if (TT.isOSBinFormatMachO())
    OutStreamer->SwitchSection(getObjFileLowering().getTextSection());

  if (TT.isOSBinFormatCOFF()) {
    // Emit an absolute @feat.00 symbol.  This appears to be some kind of
    // compiler features bitfield read by link.exe.
    MCSymbol *S = MMI->getContext().getOrCreateSymbol(StringRef("@feat.00"));
    OutStreamer->BeginCOFFSymbolDef(S);
    OutStreamer->EmitCOFFSymbolStorageClass(COFF::IMAGE_SYM_CLASS_STATIC);
    OutStreamer->EmitCOFFSymbolType(COFF::IMAGE_SYM_DTYPE_NULL);
    OutStreamer->EndCOFFSymbolDef();
    int64_t Feat00Flags = 0;

    if (TT.getArch() == Triple::x86) {
      // According to the PE-COFF spec, the LSB of this value marks the object
      // for "registered SEH".  This means that all SEH handler entry points
      // must be registered in .sxdata.  Use of any unregistered handlers will
      // cause the process to terminate immediately.  LLVM does not know how to
      // register any SEH handlers, so its object files should be safe.
      Feat00Flags |= 1;
    }

    if (M.getModuleFlag("cfguardtable"))
      Feat00Flags |= 0x800; // Object is CFG-aware.

    OutStreamer->EmitSymbolAttribute(S, MCSA_Global);
    OutStreamer->EmitAssignment(
        S, MCConstantExpr::create(Feat00Flags, MMI->getContext()));
  }
  OutStreamer->EmitSyntaxDirective();

  // If this is not inline asm and we're in 16-bit
  // mode prefix assembly with .code16.
  bool is16 = TT.getEnvironment() == Triple::CODE16;
  if (M.getModuleInlineAsm().empty() && is16)
    OutStreamer->EmitAssemblerFlag(MCAF_Code16);
}

static void
emitNonLazySymbolPointer(MCStreamer &OutStreamer, MCSymbol *StubLabel,
                         MachineModuleInfoImpl::StubValueTy &MCSym) {
  // L_foo$stub:
  OutStreamer.EmitLabel(StubLabel);
  //   .indirect_symbol _foo
  OutStreamer.EmitSymbolAttribute(MCSym.getPointer(), MCSA_IndirectSymbol);

  if (MCSym.getInt())
    // External to current translation unit.
    OutStreamer.EmitIntValue(0, 4/*size*/);
  else
    // Internal to current translation unit.
    //
    // When we place the LSDA into the TEXT section, the type info
    // pointers need to be indirect and pc-rel. We accomplish this by
    // using NLPs; however, sometimes the types are local to the file.
    // We need to fill in the value for the NLP in those cases.
    OutStreamer.EmitValue(
        MCSymbolRefExpr::create(MCSym.getPointer(), OutStreamer.getContext()),
        4 /*size*/);
}

static void emitNonLazyStubs(MachineModuleInfo *MMI, MCStreamer &OutStreamer) {

  MachineModuleInfoMachO &MMIMacho =
      MMI->getObjFileInfo<MachineModuleInfoMachO>();

  // Output stubs for dynamically-linked functions.
  MachineModuleInfoMachO::SymbolListTy Stubs;

  // Output stubs for external and common global variables.
  Stubs = MMIMacho.GetGVStubList();
  if (!Stubs.empty()) {
    OutStreamer.SwitchSection(MMI->getContext().getMachOSection(
        "__IMPORT", "__pointers", MachO::S_NON_LAZY_SYMBOL_POINTERS,
        SectionKind::getMetadata()));

    for (auto &Stub : Stubs)
      emitNonLazySymbolPointer(OutStreamer, Stub.first, Stub.second);

    Stubs.clear();
    OutStreamer.AddBlankLine();
  }
}

void X86AsmPrinter::EmitEndOfAsmFile(Module &M) {
  const Triple &TT = TM.getTargetTriple();

  if (TT.isOSBinFormatMachO()) {
    // Mach-O uses non-lazy symbol stubs to encode per-TU information into
    // global table for symbol lookup.
    emitNonLazyStubs(MMI, *OutStreamer);

    // Emit stack and fault map information.
    emitStackMaps(SM);
    FM.serializeToFaultMapSection();

    // This flag tells the linker that no global symbols contain code that fall
    // through to other global symbols (e.g. an implementation of multiple entry
    // points). If this doesn't occur, the linker can safely perform dead code
    // stripping. Since LLVM never generates code that does this, it is always
    // safe to set.
    OutStreamer->EmitAssemblerFlag(MCAF_SubsectionsViaSymbols);
    return;
  }

  if (TT.isKnownWindowsMSVCEnvironment() && MMI->usesVAFloatArgument()) {
    StringRef SymbolName =
        (TT.getArch() == Triple::x86_64) ? "_fltused" : "__fltused";
    MCSymbol *S = MMI->getContext().getOrCreateSymbol(SymbolName);
    OutStreamer->EmitSymbolAttribute(S, MCSA_Global);
    return;
  }

  if (TT.isOSBinFormatCOFF()) {
    emitStackMaps(SM);
    return;
  }

  if (TT.isOSBinFormatELF()) {
    emitStackMaps(SM);
    FM.serializeToFaultMapSection();
    return;
  }
}

//===----------------------------------------------------------------------===//
// Target Registry Stuff
//===----------------------------------------------------------------------===//

// Force static initialization.
extern "C" void LLVMInitializeX86AsmPrinter() {
  RegisterAsmPrinter<X86AsmPrinter> X(getTheX86_32Target());
  RegisterAsmPrinter<X86AsmPrinter> Y(getTheX86_64Target());
}
