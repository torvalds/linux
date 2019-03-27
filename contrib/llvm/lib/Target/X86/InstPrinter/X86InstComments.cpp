//===-- X86InstComments.cpp - Generate verbose-asm comments for instrs ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines functionality used to emit comments about X86 instructions to
// an output stream for -fverbose-asm.
//
//===----------------------------------------------------------------------===//

#include "X86InstComments.h"
#include "X86ATTInstPrinter.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "Utils/X86ShuffleDecode.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define CASE_SSE_INS_COMMON(Inst, src)            \
  case X86::Inst##src:

#define CASE_AVX_INS_COMMON(Inst, Suffix, src)    \
  case X86::V##Inst##Suffix##src:

#define CASE_MASK_INS_COMMON(Inst, Suffix, src)   \
  case X86::V##Inst##Suffix##src##k:

#define CASE_MASKZ_INS_COMMON(Inst, Suffix, src)  \
  case X86::V##Inst##Suffix##src##kz:

#define CASE_AVX512_INS_COMMON(Inst, Suffix, src) \
  CASE_AVX_INS_COMMON(Inst, Suffix, src)          \
  CASE_MASK_INS_COMMON(Inst, Suffix, src)         \
  CASE_MASKZ_INS_COMMON(Inst, Suffix, src)

#define CASE_MOVDUP(Inst, src)                    \
  CASE_AVX512_INS_COMMON(Inst, Z, r##src)         \
  CASE_AVX512_INS_COMMON(Inst, Z256, r##src)      \
  CASE_AVX512_INS_COMMON(Inst, Z128, r##src)      \
  CASE_AVX_INS_COMMON(Inst, , r##src)             \
  CASE_AVX_INS_COMMON(Inst, Y, r##src)            \
  CASE_SSE_INS_COMMON(Inst, r##src)

#define CASE_MASK_MOVDUP(Inst, src)               \
  CASE_MASK_INS_COMMON(Inst, Z, r##src)           \
  CASE_MASK_INS_COMMON(Inst, Z256, r##src)        \
  CASE_MASK_INS_COMMON(Inst, Z128, r##src)

#define CASE_MASKZ_MOVDUP(Inst, src)              \
  CASE_MASKZ_INS_COMMON(Inst, Z, r##src)          \
  CASE_MASKZ_INS_COMMON(Inst, Z256, r##src)       \
  CASE_MASKZ_INS_COMMON(Inst, Z128, r##src)

#define CASE_PMOVZX(Inst, src)                    \
  CASE_AVX512_INS_COMMON(Inst, Z, r##src)         \
  CASE_AVX512_INS_COMMON(Inst, Z256, r##src)      \
  CASE_AVX512_INS_COMMON(Inst, Z128, r##src)      \
  CASE_AVX_INS_COMMON(Inst, , r##src)             \
  CASE_AVX_INS_COMMON(Inst, Y, r##src)            \
  CASE_SSE_INS_COMMON(Inst, r##src)

#define CASE_MASK_PMOVZX(Inst, src)               \
  CASE_MASK_INS_COMMON(Inst, Z, r##src)           \
  CASE_MASK_INS_COMMON(Inst, Z256, r##src)        \
  CASE_MASK_INS_COMMON(Inst, Z128, r##src)

#define CASE_MASKZ_PMOVZX(Inst, src)              \
  CASE_MASKZ_INS_COMMON(Inst, Z, r##src)          \
  CASE_MASKZ_INS_COMMON(Inst, Z256, r##src)       \
  CASE_MASKZ_INS_COMMON(Inst, Z128, r##src)

#define CASE_UNPCK(Inst, src)                     \
  CASE_AVX512_INS_COMMON(Inst, Z, r##src)         \
  CASE_AVX512_INS_COMMON(Inst, Z256, r##src)      \
  CASE_AVX512_INS_COMMON(Inst, Z128, r##src)      \
  CASE_AVX_INS_COMMON(Inst, , r##src)             \
  CASE_AVX_INS_COMMON(Inst, Y, r##src)            \
  CASE_SSE_INS_COMMON(Inst, r##src)

#define CASE_MASK_UNPCK(Inst, src)                \
  CASE_MASK_INS_COMMON(Inst, Z, r##src)           \
  CASE_MASK_INS_COMMON(Inst, Z256, r##src)        \
  CASE_MASK_INS_COMMON(Inst, Z128, r##src)

#define CASE_MASKZ_UNPCK(Inst, src)               \
  CASE_MASKZ_INS_COMMON(Inst, Z, r##src)          \
  CASE_MASKZ_INS_COMMON(Inst, Z256, r##src)       \
  CASE_MASKZ_INS_COMMON(Inst, Z128, r##src)

#define CASE_SHUF(Inst, suf)                      \
  CASE_AVX512_INS_COMMON(Inst, Z, suf)            \
  CASE_AVX512_INS_COMMON(Inst, Z256, suf)         \
  CASE_AVX512_INS_COMMON(Inst, Z128, suf)         \
  CASE_AVX_INS_COMMON(Inst, , suf)                \
  CASE_AVX_INS_COMMON(Inst, Y, suf)               \
  CASE_SSE_INS_COMMON(Inst, suf)

#define CASE_MASK_SHUF(Inst, src)                 \
  CASE_MASK_INS_COMMON(Inst, Z, r##src##i)        \
  CASE_MASK_INS_COMMON(Inst, Z256, r##src##i)     \
  CASE_MASK_INS_COMMON(Inst, Z128, r##src##i)

#define CASE_MASKZ_SHUF(Inst, src)                \
  CASE_MASKZ_INS_COMMON(Inst, Z, r##src##i)       \
  CASE_MASKZ_INS_COMMON(Inst, Z256, r##src##i)    \
  CASE_MASKZ_INS_COMMON(Inst, Z128, r##src##i)

#define CASE_VPERMILPI(Inst, src)                 \
  CASE_AVX512_INS_COMMON(Inst, Z, src##i)         \
  CASE_AVX512_INS_COMMON(Inst, Z256, src##i)      \
  CASE_AVX512_INS_COMMON(Inst, Z128, src##i)      \
  CASE_AVX_INS_COMMON(Inst, , src##i)             \
  CASE_AVX_INS_COMMON(Inst, Y, src##i)

#define CASE_MASK_VPERMILPI(Inst, src)            \
  CASE_MASK_INS_COMMON(Inst, Z, src##i)           \
  CASE_MASK_INS_COMMON(Inst, Z256, src##i)        \
  CASE_MASK_INS_COMMON(Inst, Z128, src##i)

#define CASE_MASKZ_VPERMILPI(Inst, src)           \
  CASE_MASKZ_INS_COMMON(Inst, Z, src##i)          \
  CASE_MASKZ_INS_COMMON(Inst, Z256, src##i)       \
  CASE_MASKZ_INS_COMMON(Inst, Z128, src##i)

#define CASE_VPERM(Inst, src)                     \
  CASE_AVX512_INS_COMMON(Inst, Z, src##i)         \
  CASE_AVX512_INS_COMMON(Inst, Z256, src##i)      \
  CASE_AVX_INS_COMMON(Inst, Y, src##i)

#define CASE_MASK_VPERM(Inst, src)                \
  CASE_MASK_INS_COMMON(Inst, Z, src##i)           \
  CASE_MASK_INS_COMMON(Inst, Z256, src##i)

#define CASE_MASKZ_VPERM(Inst, src)               \
  CASE_MASKZ_INS_COMMON(Inst, Z, src##i)          \
  CASE_MASKZ_INS_COMMON(Inst, Z256, src##i)

#define CASE_VSHUF(Inst, src)                          \
  CASE_AVX512_INS_COMMON(SHUFF##Inst, Z, r##src##i)    \
  CASE_AVX512_INS_COMMON(SHUFI##Inst, Z, r##src##i)    \
  CASE_AVX512_INS_COMMON(SHUFF##Inst, Z256, r##src##i) \
  CASE_AVX512_INS_COMMON(SHUFI##Inst, Z256, r##src##i)

#define CASE_MASK_VSHUF(Inst, src)                    \
  CASE_MASK_INS_COMMON(SHUFF##Inst, Z, r##src##i)     \
  CASE_MASK_INS_COMMON(SHUFI##Inst, Z, r##src##i)     \
  CASE_MASK_INS_COMMON(SHUFF##Inst, Z256, r##src##i)  \
  CASE_MASK_INS_COMMON(SHUFI##Inst, Z256, r##src##i)

#define CASE_MASKZ_VSHUF(Inst, src)                   \
  CASE_MASKZ_INS_COMMON(SHUFF##Inst, Z, r##src##i)    \
  CASE_MASKZ_INS_COMMON(SHUFI##Inst, Z, r##src##i)    \
  CASE_MASKZ_INS_COMMON(SHUFF##Inst, Z256, r##src##i) \
  CASE_MASKZ_INS_COMMON(SHUFI##Inst, Z256, r##src##i)

#define CASE_AVX512_FMA(Inst, suf)                \
  CASE_AVX512_INS_COMMON(Inst, Z, suf)            \
  CASE_AVX512_INS_COMMON(Inst, Z256, suf)         \
  CASE_AVX512_INS_COMMON(Inst, Z128, suf)

#define CASE_FMA(Inst, suf)                       \
  CASE_AVX512_FMA(Inst, suf)                      \
  CASE_AVX_INS_COMMON(Inst, , suf)                \
  CASE_AVX_INS_COMMON(Inst, Y, suf)

#define CASE_FMA_PACKED_REG(Inst)                 \
  CASE_FMA(Inst##PD, r)                           \
  CASE_FMA(Inst##PS, r)

#define CASE_FMA_PACKED_MEM(Inst)                 \
  CASE_FMA(Inst##PD, m)                           \
  CASE_FMA(Inst##PS, m)                           \
  CASE_AVX512_FMA(Inst##PD, mb)                   \
  CASE_AVX512_FMA(Inst##PS, mb)

#define CASE_FMA_SCALAR_REG(Inst)                 \
  CASE_AVX_INS_COMMON(Inst##SD, , r)              \
  CASE_AVX_INS_COMMON(Inst##SS, , r)              \
  CASE_AVX_INS_COMMON(Inst##SD, , r_Int)          \
  CASE_AVX_INS_COMMON(Inst##SS, , r_Int)          \
  CASE_AVX_INS_COMMON(Inst##SD, Z, r)             \
  CASE_AVX_INS_COMMON(Inst##SS, Z, r)             \
  CASE_AVX512_INS_COMMON(Inst##SD, Z, r_Int)      \
  CASE_AVX512_INS_COMMON(Inst##SS, Z, r_Int)

#define CASE_FMA_SCALAR_MEM(Inst)                 \
  CASE_AVX_INS_COMMON(Inst##SD, , m)              \
  CASE_AVX_INS_COMMON(Inst##SS, , m)              \
  CASE_AVX_INS_COMMON(Inst##SD, , m_Int)          \
  CASE_AVX_INS_COMMON(Inst##SS, , m_Int)          \
  CASE_AVX_INS_COMMON(Inst##SD, Z, m)             \
  CASE_AVX_INS_COMMON(Inst##SS, Z, m)             \
  CASE_AVX512_INS_COMMON(Inst##SD, Z, m_Int)      \
  CASE_AVX512_INS_COMMON(Inst##SS, Z, m_Int)

static unsigned getVectorRegSize(unsigned RegNo) {
  if (X86::ZMM0 <= RegNo && RegNo <= X86::ZMM31)
    return 512;
  if (X86::YMM0 <= RegNo && RegNo <= X86::YMM31)
    return 256;
  if (X86::XMM0 <= RegNo && RegNo <= X86::XMM31)
    return 128;
  if (X86::MM0 <= RegNo && RegNo <= X86::MM7)
    return 64;

  llvm_unreachable("Unknown vector reg!");
}

static unsigned getRegOperandNumElts(const MCInst *MI, unsigned ScalarSize,
                                     unsigned OperandIndex) {
  unsigned OpReg = MI->getOperand(OperandIndex).getReg();
  return getVectorRegSize(OpReg) / ScalarSize;
}

static const char *getRegName(unsigned Reg) {
  return X86ATTInstPrinter::getRegisterName(Reg);
}

/// Wraps the destination register name with AVX512 mask/maskz filtering.
static void printMasking(raw_ostream &OS, const MCInst *MI,
                         const MCInstrInfo &MCII) {
  const MCInstrDesc &Desc = MCII.get(MI->getOpcode());
  uint64_t TSFlags = Desc.TSFlags;

  if (!(TSFlags & X86II::EVEX_K))
    return;

  bool MaskWithZero = (TSFlags & X86II::EVEX_Z);
  unsigned MaskOp = Desc.getNumDefs();

  if (Desc.getOperandConstraint(MaskOp, MCOI::TIED_TO) != -1)
    ++MaskOp;

  const char *MaskRegName = getRegName(MI->getOperand(MaskOp).getReg());

  // MASK: zmmX {%kY}
  OS << " {%" << MaskRegName << "}";

  // MASKZ: zmmX {%kY} {z}
  if (MaskWithZero)
    OS << " {z}";
}

static bool printFMA3Comments(const MCInst *MI, raw_ostream &OS) {
  const char *Mul1Name = nullptr, *Mul2Name = nullptr, *AccName = nullptr;
  unsigned NumOperands = MI->getNumOperands();
  bool RegForm = false;
  bool Negate = false;
  StringRef AccStr = "+";

  // The operands for FMA instructions without rounding fall into two forms.
  //  dest, src1, src2, src3
  //  dest, src1, mask, src2, src3
  // Where src3 is either a register or 5 memory address operands. So to find
  // dest and src1 we can index from the front. To find src2 and src3 we can
  // index from the end by taking into account memory vs register form when
  // finding src2.

  switch (MI->getOpcode()) {
  default:
    return false;
  CASE_FMA_PACKED_REG(FMADD132)
  CASE_FMA_SCALAR_REG(FMADD132)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMADD132)
  CASE_FMA_SCALAR_MEM(FMADD132)
    AccName = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul1Name = getRegName(MI->getOperand(1).getReg());
    break;

  CASE_FMA_PACKED_REG(FMADD213)
  CASE_FMA_SCALAR_REG(FMADD213)
    AccName = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMADD213)
  CASE_FMA_SCALAR_MEM(FMADD213)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul2Name = getRegName(MI->getOperand(1).getReg());
    break;

  CASE_FMA_PACKED_REG(FMADD231)
  CASE_FMA_SCALAR_REG(FMADD231)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMADD231)
  CASE_FMA_SCALAR_MEM(FMADD231)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    AccName = getRegName(MI->getOperand(1).getReg());
    break;

  CASE_FMA_PACKED_REG(FMSUB132)
  CASE_FMA_SCALAR_REG(FMSUB132)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMSUB132)
  CASE_FMA_SCALAR_MEM(FMSUB132)
    AccName = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul1Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "-";
    break;

  CASE_FMA_PACKED_REG(FMSUB213)
  CASE_FMA_SCALAR_REG(FMSUB213)
    AccName = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMSUB213)
  CASE_FMA_SCALAR_MEM(FMSUB213)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul2Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "-";
    break;

  CASE_FMA_PACKED_REG(FMSUB231)
  CASE_FMA_SCALAR_REG(FMSUB231)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMSUB231)
  CASE_FMA_SCALAR_MEM(FMSUB231)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    AccName = getRegName(MI->getOperand(1).getReg());
    AccStr = "-";
    break;

  CASE_FMA_PACKED_REG(FNMADD132)
  CASE_FMA_SCALAR_REG(FNMADD132)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FNMADD132)
  CASE_FMA_SCALAR_MEM(FNMADD132)
    AccName = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul1Name = getRegName(MI->getOperand(1).getReg());
    Negate = true;
    break;

  CASE_FMA_PACKED_REG(FNMADD213)
  CASE_FMA_SCALAR_REG(FNMADD213)
    AccName = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FNMADD213)
  CASE_FMA_SCALAR_MEM(FNMADD213)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul2Name = getRegName(MI->getOperand(1).getReg());
    Negate = true;
    break;

  CASE_FMA_PACKED_REG(FNMADD231)
  CASE_FMA_SCALAR_REG(FNMADD231)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FNMADD231)
  CASE_FMA_SCALAR_MEM(FNMADD231)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    AccName = getRegName(MI->getOperand(1).getReg());
    Negate = true;
    break;

  CASE_FMA_PACKED_REG(FNMSUB132)
  CASE_FMA_SCALAR_REG(FNMSUB132)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FNMSUB132)
  CASE_FMA_SCALAR_MEM(FNMSUB132)
    AccName = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul1Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "-";
    Negate = true;
    break;

  CASE_FMA_PACKED_REG(FNMSUB213)
  CASE_FMA_SCALAR_REG(FNMSUB213)
    AccName = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FNMSUB213)
  CASE_FMA_SCALAR_MEM(FNMSUB213)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul2Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "-";
    Negate = true;
    break;

  CASE_FMA_PACKED_REG(FNMSUB231)
  CASE_FMA_SCALAR_REG(FNMSUB231)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FNMSUB231)
  CASE_FMA_SCALAR_MEM(FNMSUB231)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    AccName = getRegName(MI->getOperand(1).getReg());
    AccStr = "-";
    Negate = true;
    break;

  CASE_FMA_PACKED_REG(FMADDSUB132)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMADDSUB132)
    AccName = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul1Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "+/-";
    break;

  CASE_FMA_PACKED_REG(FMADDSUB213)
    AccName = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMADDSUB213)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul2Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "+/-";
    break;

  CASE_FMA_PACKED_REG(FMADDSUB231)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMADDSUB231)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    AccName = getRegName(MI->getOperand(1).getReg());
    AccStr = "+/-";
    break;

  CASE_FMA_PACKED_REG(FMSUBADD132)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMSUBADD132)
    AccName = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul1Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "-/+";
    break;

  CASE_FMA_PACKED_REG(FMSUBADD213)
    AccName = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMSUBADD213)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    Mul2Name = getRegName(MI->getOperand(1).getReg());
    AccStr = "-/+";
    break;

  CASE_FMA_PACKED_REG(FMSUBADD231)
    Mul2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;
  CASE_FMA_PACKED_MEM(FMSUBADD231)
    Mul1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    AccName = getRegName(MI->getOperand(1).getReg());
    AccStr = "-/+";
    break;
  }

  const char *DestName = getRegName(MI->getOperand(0).getReg());

  if (!Mul1Name) Mul1Name = "mem";
  if (!Mul2Name) Mul2Name = "mem";
  if (!AccName)  AccName = "mem";

  OS << DestName << " = ";
  // TODO: Print masking information?

  if (Negate)
    OS << '-';

  OS << '(' << Mul1Name << " * " << Mul2Name << ") " << AccStr << ' '
     << AccName;

  return true;
}


//===----------------------------------------------------------------------===//
// Top Level Entrypoint
//===----------------------------------------------------------------------===//

/// EmitAnyX86InstComments - This function decodes x86 instructions and prints
/// newline terminated strings to the specified string if desired.  This
/// information is shown in disassembly dumps when verbose assembly is enabled.
bool llvm::EmitAnyX86InstComments(const MCInst *MI, raw_ostream &OS,
                                  const MCInstrInfo &MCII) {
  // If this is a shuffle operation, the switch should fill in this state.
  SmallVector<int, 8> ShuffleMask;
  const char *DestName = nullptr, *Src1Name = nullptr, *Src2Name = nullptr;
  unsigned NumOperands = MI->getNumOperands();
  bool RegForm = false;

  if (printFMA3Comments(MI, OS))
    return true;

  switch (MI->getOpcode()) {
  default:
    // Not an instruction for which we can decode comments.
    return false;

  case X86::BLENDPDrri:
  case X86::VBLENDPDrri:
  case X86::VBLENDPDYrri:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    LLVM_FALLTHROUGH;
  case X86::BLENDPDrmi:
  case X86::VBLENDPDrmi:
  case X86::VBLENDPDYrmi:
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeBLENDMask(getRegOperandNumElts(MI, 64, 0),
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::BLENDPSrri:
  case X86::VBLENDPSrri:
  case X86::VBLENDPSYrri:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    LLVM_FALLTHROUGH;
  case X86::BLENDPSrmi:
  case X86::VBLENDPSrmi:
  case X86::VBLENDPSYrmi:
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeBLENDMask(getRegOperandNumElts(MI, 32, 0),
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::PBLENDWrri:
  case X86::VPBLENDWrri:
  case X86::VPBLENDWYrri:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    LLVM_FALLTHROUGH;
  case X86::PBLENDWrmi:
  case X86::VPBLENDWrmi:
  case X86::VPBLENDWYrmi:
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeBLENDMask(getRegOperandNumElts(MI, 16, 0),
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::VPBLENDDrri:
  case X86::VPBLENDDYrri:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    LLVM_FALLTHROUGH;
  case X86::VPBLENDDrmi:
  case X86::VPBLENDDYrmi:
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeBLENDMask(getRegOperandNumElts(MI, 32, 0),
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::INSERTPSrr:
  case X86::VINSERTPSrr:
  case X86::VINSERTPSZrr:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    LLVM_FALLTHROUGH;
  case X86::INSERTPSrm:
  case X86::VINSERTPSrm:
  case X86::VINSERTPSZrm:
    DestName = getRegName(MI->getOperand(0).getReg());
    Src1Name = getRegName(MI->getOperand(1).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeINSERTPSMask(MI->getOperand(NumOperands - 1).getImm(),
                         ShuffleMask);
    break;

  case X86::MOVLHPSrr:
  case X86::VMOVLHPSrr:
  case X86::VMOVLHPSZrr:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeMOVLHPSMask(2, ShuffleMask);
    break;

  case X86::MOVHLPSrr:
  case X86::VMOVHLPSrr:
  case X86::VMOVHLPSZrr:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeMOVHLPSMask(2, ShuffleMask);
    break;

  case X86::MOVHPDrm:
  case X86::VMOVHPDrm:
  case X86::VMOVHPDZ128rm:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeInsertElementMask(2, 1, 1, ShuffleMask);
    break;

  case X86::MOVHPSrm:
  case X86::VMOVHPSrm:
  case X86::VMOVHPSZ128rm:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeInsertElementMask(4, 2, 2, ShuffleMask);
    break;

  case X86::MOVLPDrm:
  case X86::VMOVLPDrm:
  case X86::VMOVLPDZ128rm:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeInsertElementMask(2, 0, 1, ShuffleMask);
    break;

  case X86::MOVLPSrm:
  case X86::VMOVLPSrm:
  case X86::VMOVLPSZ128rm:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeInsertElementMask(4, 0, 2, ShuffleMask);
    break;

  CASE_MOVDUP(MOVSLDUP, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;

  CASE_MOVDUP(MOVSLDUP, m)
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeMOVSLDUPMask(getRegOperandNumElts(MI, 32, 0), ShuffleMask);
    break;

  CASE_MOVDUP(MOVSHDUP, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;

  CASE_MOVDUP(MOVSHDUP, m)
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeMOVSHDUPMask(getRegOperandNumElts(MI, 32, 0), ShuffleMask);
    break;

  CASE_MOVDUP(MOVDDUP, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;

  CASE_MOVDUP(MOVDDUP, m)
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeMOVDDUPMask(getRegOperandNumElts(MI, 64, 0), ShuffleMask);
    break;

  case X86::PSLLDQri:
  case X86::VPSLLDQri:
  case X86::VPSLLDQYri:
  case X86::VPSLLDQZ128rr:
  case X86::VPSLLDQZ256rr:
  case X86::VPSLLDQZrr:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    LLVM_FALLTHROUGH;
  case X86::VPSLLDQZ128rm:
  case X86::VPSLLDQZ256rm:
  case X86::VPSLLDQZrm:
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSLLDQMask(getRegOperandNumElts(MI, 8, 0),
                       MI->getOperand(NumOperands - 1).getImm(),
                       ShuffleMask);
    break;

  case X86::PSRLDQri:
  case X86::VPSRLDQri:
  case X86::VPSRLDQYri:
  case X86::VPSRLDQZ128rr:
  case X86::VPSRLDQZ256rr:
  case X86::VPSRLDQZrr:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    LLVM_FALLTHROUGH;
  case X86::VPSRLDQZ128rm:
  case X86::VPSRLDQZ256rm:
  case X86::VPSRLDQZrm:
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSRLDQMask(getRegOperandNumElts(MI, 8, 0),
                       MI->getOperand(NumOperands - 1).getImm(),
                       ShuffleMask);
    break;

  CASE_SHUF(PALIGNR, rri)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_SHUF(PALIGNR, rmi)
    Src2Name = getRegName(MI->getOperand(NumOperands-(RegForm?3:7)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePALIGNRMask(getRegOperandNumElts(MI, 8, 0),
                        MI->getOperand(NumOperands - 1).getImm(),
                        ShuffleMask);
    break;

  CASE_AVX512_INS_COMMON(ALIGNQ, Z, rri)
  CASE_AVX512_INS_COMMON(ALIGNQ, Z256, rri)
  CASE_AVX512_INS_COMMON(ALIGNQ, Z128, rri)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_AVX512_INS_COMMON(ALIGNQ, Z, rmi)
  CASE_AVX512_INS_COMMON(ALIGNQ, Z256, rmi)
  CASE_AVX512_INS_COMMON(ALIGNQ, Z128, rmi)
    Src2Name = getRegName(MI->getOperand(NumOperands-(RegForm?3:7)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeVALIGNMask(getRegOperandNumElts(MI, 64, 0),
                       MI->getOperand(NumOperands - 1).getImm(),
                       ShuffleMask);
    break;

  CASE_AVX512_INS_COMMON(ALIGND, Z, rri)
  CASE_AVX512_INS_COMMON(ALIGND, Z256, rri)
  CASE_AVX512_INS_COMMON(ALIGND, Z128, rri)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_AVX512_INS_COMMON(ALIGND, Z, rmi)
  CASE_AVX512_INS_COMMON(ALIGND, Z256, rmi)
  CASE_AVX512_INS_COMMON(ALIGND, Z128, rmi)
    Src2Name = getRegName(MI->getOperand(NumOperands-(RegForm?3:7)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeVALIGNMask(getRegOperandNumElts(MI, 32, 0),
                       MI->getOperand(NumOperands - 1).getImm(),
                       ShuffleMask);
    break;

  CASE_SHUF(PSHUFD, ri)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    LLVM_FALLTHROUGH;

  CASE_SHUF(PSHUFD, mi)
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSHUFMask(getRegOperandNumElts(MI, 32, 0), 32,
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    break;

  CASE_SHUF(PSHUFHW, ri)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    LLVM_FALLTHROUGH;

  CASE_SHUF(PSHUFHW, mi)
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSHUFHWMask(getRegOperandNumElts(MI, 16, 0),
                        MI->getOperand(NumOperands - 1).getImm(),
                        ShuffleMask);
    break;

  CASE_SHUF(PSHUFLW, ri)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    LLVM_FALLTHROUGH;

  CASE_SHUF(PSHUFLW, mi)
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSHUFLWMask(getRegOperandNumElts(MI, 16, 0),
                        MI->getOperand(NumOperands - 1).getImm(),
                        ShuffleMask);
    break;

  case X86::MMX_PSHUFWri:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    LLVM_FALLTHROUGH;

  case X86::MMX_PSHUFWmi:
    DestName = getRegName(MI->getOperand(0).getReg());
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSHUFMask(4, 16, MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    break;

  case X86::PSWAPDrr:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    LLVM_FALLTHROUGH;

  case X86::PSWAPDrm:
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodePSWAPMask(2, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKHBW, r)
  case X86::MMX_PUNPCKHBWirr:
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKHBW, m)
  case X86::MMX_PUNPCKHBWirm:
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKHMask(getRegOperandNumElts(MI, 8, 0), 8, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKHWD, r)
  case X86::MMX_PUNPCKHWDirr:
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKHWD, m)
  case X86::MMX_PUNPCKHWDirm:
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKHMask(getRegOperandNumElts(MI, 16, 0), 16, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKHDQ, r)
  case X86::MMX_PUNPCKHDQirr:
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKHDQ, m)
  case X86::MMX_PUNPCKHDQirm:
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKHMask(getRegOperandNumElts(MI, 32, 0), 32, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKHQDQ, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKHQDQ, m)
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKHMask(getRegOperandNumElts(MI, 64, 0), 64, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKLBW, r)
  case X86::MMX_PUNPCKLBWirr:
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKLBW, m)
  case X86::MMX_PUNPCKLBWirm:
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKLMask(getRegOperandNumElts(MI, 8, 0), 8, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKLWD, r)
  case X86::MMX_PUNPCKLWDirr:
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKLWD, m)
  case X86::MMX_PUNPCKLWDirm:
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKLMask(getRegOperandNumElts(MI, 16, 0), 16, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKLDQ, r)
  case X86::MMX_PUNPCKLDQirr:
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKLDQ, m)
  case X86::MMX_PUNPCKLDQirm:
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKLMask(getRegOperandNumElts(MI, 32, 0), 32, ShuffleMask);
    break;

  CASE_UNPCK(PUNPCKLQDQ, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(PUNPCKLQDQ, m)
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    DecodeUNPCKLMask(getRegOperandNumElts(MI, 64, 0), 64, ShuffleMask);
    break;

  CASE_SHUF(SHUFPD, rri)
    Src2Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_SHUF(SHUFPD, rmi)
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeSHUFPMask(getRegOperandNumElts(MI, 64, 0), 64,
                      MI->getOperand(NumOperands - 1).getImm(), ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?3:7)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_SHUF(SHUFPS, rri)
    Src2Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_SHUF(SHUFPS, rmi)
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeSHUFPMask(getRegOperandNumElts(MI, 32, 0), 32,
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?3:7)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_VSHUF(64X2, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_VSHUF(64X2, m)
    decodeVSHUF64x2FamilyMask(getRegOperandNumElts(MI, 64, 0), 64,
                              MI->getOperand(NumOperands - 1).getImm(),
                              ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?3:7)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_VSHUF(32X4, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_VSHUF(32X4, m)
    decodeVSHUF64x2FamilyMask(getRegOperandNumElts(MI, 32, 0), 32,
                              MI->getOperand(NumOperands - 1).getImm(),
                              ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?3:7)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_UNPCK(UNPCKLPD, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(UNPCKLPD, m)
    DecodeUNPCKLMask(getRegOperandNumElts(MI, 64, 0), 64, ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_UNPCK(UNPCKLPS, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(UNPCKLPS, m)
    DecodeUNPCKLMask(getRegOperandNumElts(MI, 32, 0), 32, ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_UNPCK(UNPCKHPD, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(UNPCKHPD, m)
    DecodeUNPCKHMask(getRegOperandNumElts(MI, 64, 0), 64, ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_UNPCK(UNPCKHPS, r)
    Src2Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    RegForm = true;
    LLVM_FALLTHROUGH;

  CASE_UNPCK(UNPCKHPS, m)
    DecodeUNPCKHMask(getRegOperandNumElts(MI, 32, 0), 32, ShuffleMask);
    Src1Name = getRegName(MI->getOperand(NumOperands-(RegForm?2:6)).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_VPERMILPI(PERMILPS, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    LLVM_FALLTHROUGH;

  CASE_VPERMILPI(PERMILPS, m)
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSHUFMask(getRegOperandNumElts(MI, 32, 0), 32,
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_VPERMILPI(PERMILPD, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    LLVM_FALLTHROUGH;

  CASE_VPERMILPI(PERMILPD, m)
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodePSHUFMask(getRegOperandNumElts(MI, 64, 0), 64,
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::VPERM2F128rr:
  case X86::VPERM2I128rr:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    LLVM_FALLTHROUGH;

  case X86::VPERM2F128rm:
  case X86::VPERM2I128rm:
    // For instruction comments purpose, assume the 256-bit vector is v4i64.
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeVPERM2X128Mask(4, MI->getOperand(NumOperands - 1).getImm(),
                           ShuffleMask);
    Src1Name = getRegName(MI->getOperand(1).getReg());
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_VPERM(PERMPD, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    LLVM_FALLTHROUGH;

  CASE_VPERM(PERMPD, m)
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeVPERMMask(getRegOperandNumElts(MI, 64, 0),
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_VPERM(PERMQ, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 2).getReg());
    LLVM_FALLTHROUGH;

  CASE_VPERM(PERMQ, m)
    if (MI->getOperand(NumOperands - 1).isImm())
      DecodeVPERMMask(getRegOperandNumElts(MI, 64, 0),
                      MI->getOperand(NumOperands - 1).getImm(),
                      ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::MOVSDrr:
  case X86::VMOVSDrr:
  case X86::VMOVSDZrr:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    Src1Name = getRegName(MI->getOperand(1).getReg());
    LLVM_FALLTHROUGH;

  case X86::MOVSDrm:
  case X86::VMOVSDrm:
  case X86::VMOVSDZrm:
    DecodeScalarMoveMask(2, nullptr == Src2Name, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::MOVSSrr:
  case X86::VMOVSSrr:
  case X86::VMOVSSZrr:
    Src2Name = getRegName(MI->getOperand(2).getReg());
    Src1Name = getRegName(MI->getOperand(1).getReg());
    LLVM_FALLTHROUGH;

  case X86::MOVSSrm:
  case X86::VMOVSSrm:
  case X86::VMOVSSZrm:
    DecodeScalarMoveMask(4, nullptr == Src2Name, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::MOVPQI2QIrr:
  case X86::MOVZPQILo2PQIrr:
  case X86::VMOVPQI2QIrr:
  case X86::VMOVPQI2QIZrr:
  case X86::VMOVZPQILo2PQIrr:
  case X86::VMOVZPQILo2PQIZrr:
    Src1Name = getRegName(MI->getOperand(1).getReg());
    LLVM_FALLTHROUGH;

  case X86::MOVQI2PQIrm:
  case X86::VMOVQI2PQIrm:
  case X86::VMOVQI2PQIZrm:
    DecodeZeroMoveLowMask(2, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::MOVDI2PDIrm:
  case X86::VMOVDI2PDIrm:
  case X86::VMOVDI2PDIZrm:
    DecodeZeroMoveLowMask(4, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  case X86::EXTRQI:
    if (MI->getOperand(2).isImm() &&
        MI->getOperand(3).isImm())
      DecodeEXTRQIMask(16, 8, MI->getOperand(2).getImm(),
                       MI->getOperand(3).getImm(), ShuffleMask);

    DestName = getRegName(MI->getOperand(0).getReg());
    Src1Name = getRegName(MI->getOperand(1).getReg());
    break;

  case X86::INSERTQI:
    if (MI->getOperand(3).isImm() &&
        MI->getOperand(4).isImm())
      DecodeINSERTQIMask(16, 8, MI->getOperand(3).getImm(),
                         MI->getOperand(4).getImm(), ShuffleMask);

    DestName = getRegName(MI->getOperand(0).getReg());
    Src1Name = getRegName(MI->getOperand(1).getReg());
    Src2Name = getRegName(MI->getOperand(2).getReg());
    break;

  case X86::VBROADCASTF128:
  case X86::VBROADCASTI128:
  CASE_AVX512_INS_COMMON(BROADCASTF64X2, Z128, rm)
  CASE_AVX512_INS_COMMON(BROADCASTI64X2, Z128, rm)
    DecodeSubVectorBroadcast(4, 2, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTF64X2, , rm)
  CASE_AVX512_INS_COMMON(BROADCASTI64X2, , rm)
    DecodeSubVectorBroadcast(8, 2, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTF64X4, , rm)
  CASE_AVX512_INS_COMMON(BROADCASTI64X4, , rm)
    DecodeSubVectorBroadcast(8, 4, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTF32X4, Z256, rm)
  CASE_AVX512_INS_COMMON(BROADCASTI32X4, Z256, rm)
    DecodeSubVectorBroadcast(8, 4, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTF32X4, , rm)
  CASE_AVX512_INS_COMMON(BROADCASTI32X4, , rm)
    DecodeSubVectorBroadcast(16, 4, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTF32X8, , rm)
  CASE_AVX512_INS_COMMON(BROADCASTI32X8, , rm)
    DecodeSubVectorBroadcast(16, 8, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTI32X2, Z128, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_AVX512_INS_COMMON(BROADCASTI32X2, Z128, m)
    DecodeSubVectorBroadcast(4, 2, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTF32X2, Z256, r)
  CASE_AVX512_INS_COMMON(BROADCASTI32X2, Z256, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_AVX512_INS_COMMON(BROADCASTF32X2, Z256, m)
  CASE_AVX512_INS_COMMON(BROADCASTI32X2, Z256, m)
    DecodeSubVectorBroadcast(8, 2, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  CASE_AVX512_INS_COMMON(BROADCASTF32X2, Z, r)
  CASE_AVX512_INS_COMMON(BROADCASTI32X2, Z, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_AVX512_INS_COMMON(BROADCASTF32X2, Z, m)
  CASE_AVX512_INS_COMMON(BROADCASTI32X2, Z, m)
    DecodeSubVectorBroadcast(16, 2, ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_PMOVZX(PMOVZXBW, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_PMOVZX(PMOVZXBW, m)
    DecodeZeroExtendMask(8, 16, getRegOperandNumElts(MI, 16, 0), ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_PMOVZX(PMOVZXBD, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_PMOVZX(PMOVZXBD, m)
    DecodeZeroExtendMask(8, 32, getRegOperandNumElts(MI, 32, 0), ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_PMOVZX(PMOVZXBQ, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_PMOVZX(PMOVZXBQ, m)
    DecodeZeroExtendMask(8, 64, getRegOperandNumElts(MI, 64, 0), ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_PMOVZX(PMOVZXWD, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_PMOVZX(PMOVZXWD, m)
    DecodeZeroExtendMask(16, 32, getRegOperandNumElts(MI, 32, 0), ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_PMOVZX(PMOVZXWQ, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_PMOVZX(PMOVZXWQ, m)
    DecodeZeroExtendMask(16, 64, getRegOperandNumElts(MI, 64, 0), ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;

  CASE_PMOVZX(PMOVZXDQ, r)
    Src1Name = getRegName(MI->getOperand(NumOperands - 1).getReg());
    LLVM_FALLTHROUGH;
  CASE_PMOVZX(PMOVZXDQ, m)
    DecodeZeroExtendMask(32, 64, getRegOperandNumElts(MI, 64, 0), ShuffleMask);
    DestName = getRegName(MI->getOperand(0).getReg());
    break;
  }

  // The only comments we decode are shuffles, so give up if we were unable to
  // decode a shuffle mask.
  if (ShuffleMask.empty())
    return false;

  if (!DestName) DestName = Src1Name;
  if (DestName) {
    OS << DestName;
    printMasking(OS, MI, MCII);
  } else
    OS << "mem";

  OS << " = ";

  // If the two sources are the same, canonicalize the input elements to be
  // from the first src so that we get larger element spans.
  if (Src1Name == Src2Name) {
    for (unsigned i = 0, e = ShuffleMask.size(); i != e; ++i) {
      if ((int)ShuffleMask[i] >= 0 && // Not sentinel.
          ShuffleMask[i] >= (int)e)   // From second mask.
        ShuffleMask[i] -= e;
    }
  }

  // The shuffle mask specifies which elements of the src1/src2 fill in the
  // destination, with a few sentinel values.  Loop through and print them
  // out.
  for (unsigned i = 0, e = ShuffleMask.size(); i != e; ++i) {
    if (i != 0)
      OS << ',';
    if (ShuffleMask[i] == SM_SentinelZero) {
      OS << "zero";
      continue;
    }

    // Otherwise, it must come from src1 or src2.  Print the span of elements
    // that comes from this src.
    bool isSrc1 = ShuffleMask[i] < (int)ShuffleMask.size();
    const char *SrcName = isSrc1 ? Src1Name : Src2Name;
    OS << (SrcName ? SrcName : "mem") << '[';
    bool IsFirst = true;
    while (i != e && (int)ShuffleMask[i] != SM_SentinelZero &&
           (ShuffleMask[i] < (int)ShuffleMask.size()) == isSrc1) {
      if (!IsFirst)
        OS << ',';
      else
        IsFirst = false;
      if (ShuffleMask[i] == SM_SentinelUndef)
        OS << "u";
      else
        OS << ShuffleMask[i] % ShuffleMask.size();
      ++i;
    }
    OS << ']';
    --i; // For loop increments element #.
  }

  // We successfully added a comment to this instruction.
  return true;
}
