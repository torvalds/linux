//===-- AArch64ISelLowering.cpp - AArch64 DAG Lowering Implementation  ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AArch64TargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "AArch64ISelLowering.h"
#include "AArch64CallingConvention.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64PerfectShuffle.h"
#include "AArch64RegisterInfo.h"
#include "AArch64Subtarget.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "aarch64-lower"

STATISTIC(NumTailCalls, "Number of tail calls");
STATISTIC(NumShiftInserts, "Number of vector shift inserts");
STATISTIC(NumOptimizedImms, "Number of times immediates were optimized");

static cl::opt<bool>
EnableAArch64SlrGeneration("aarch64-shift-insert-generation", cl::Hidden,
                           cl::desc("Allow AArch64 SLI/SRI formation"),
                           cl::init(false));

// FIXME: The necessary dtprel relocations don't seem to be supported
// well in the GNU bfd and gold linkers at the moment. Therefore, by
// default, for now, fall back to GeneralDynamic code generation.
cl::opt<bool> EnableAArch64ELFLocalDynamicTLSGeneration(
    "aarch64-elf-ldtls-generation", cl::Hidden,
    cl::desc("Allow AArch64 Local Dynamic TLS code generation"),
    cl::init(false));

static cl::opt<bool>
EnableOptimizeLogicalImm("aarch64-enable-logical-imm", cl::Hidden,
                         cl::desc("Enable AArch64 logical imm instruction "
                                  "optimization"),
                         cl::init(true));

/// Value type used for condition codes.
static const MVT MVT_CC = MVT::i32;

AArch64TargetLowering::AArch64TargetLowering(const TargetMachine &TM,
                                             const AArch64Subtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {
  // AArch64 doesn't have comparisons which set GPRs or setcc instructions, so
  // we have to make something up. Arbitrarily, choose ZeroOrOne.
  setBooleanContents(ZeroOrOneBooleanContent);
  // When comparing vectors the result sets the different elements in the
  // vector to all-one or all-zero.
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  // Set up the register classes.
  addRegisterClass(MVT::i32, &AArch64::GPR32allRegClass);
  addRegisterClass(MVT::i64, &AArch64::GPR64allRegClass);

  if (Subtarget->hasFPARMv8()) {
    addRegisterClass(MVT::f16, &AArch64::FPR16RegClass);
    addRegisterClass(MVT::f32, &AArch64::FPR32RegClass);
    addRegisterClass(MVT::f64, &AArch64::FPR64RegClass);
    addRegisterClass(MVT::f128, &AArch64::FPR128RegClass);
  }

  if (Subtarget->hasNEON()) {
    addRegisterClass(MVT::v16i8, &AArch64::FPR8RegClass);
    addRegisterClass(MVT::v8i16, &AArch64::FPR16RegClass);
    // Someone set us up the NEON.
    addDRTypeForNEON(MVT::v2f32);
    addDRTypeForNEON(MVT::v8i8);
    addDRTypeForNEON(MVT::v4i16);
    addDRTypeForNEON(MVT::v2i32);
    addDRTypeForNEON(MVT::v1i64);
    addDRTypeForNEON(MVT::v1f64);
    addDRTypeForNEON(MVT::v4f16);

    addQRTypeForNEON(MVT::v4f32);
    addQRTypeForNEON(MVT::v2f64);
    addQRTypeForNEON(MVT::v16i8);
    addQRTypeForNEON(MVT::v8i16);
    addQRTypeForNEON(MVT::v4i32);
    addQRTypeForNEON(MVT::v2i64);
    addQRTypeForNEON(MVT::v8f16);
  }

  // Compute derived properties from the register classes
  computeRegisterProperties(Subtarget->getRegisterInfo());

  // Provide all sorts of operation actions
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::SETCC, MVT::i32, Custom);
  setOperationAction(ISD::SETCC, MVT::i64, Custom);
  setOperationAction(ISD::SETCC, MVT::f16, Custom);
  setOperationAction(ISD::SETCC, MVT::f32, Custom);
  setOperationAction(ISD::SETCC, MVT::f64, Custom);
  setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);
  setOperationAction(ISD::BITREVERSE, MVT::i64, Legal);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BR_CC, MVT::i64, Custom);
  setOperationAction(ISD::BR_CC, MVT::f16, Custom);
  setOperationAction(ISD::BR_CC, MVT::f32, Custom);
  setOperationAction(ISD::BR_CC, MVT::f64, Custom);
  setOperationAction(ISD::SELECT, MVT::i32, Custom);
  setOperationAction(ISD::SELECT, MVT::i64, Custom);
  setOperationAction(ISD::SELECT, MVT::f16, Custom);
  setOperationAction(ISD::SELECT, MVT::f32, Custom);
  setOperationAction(ISD::SELECT, MVT::f64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f16, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Custom);
  setOperationAction(ISD::JumpTable, MVT::i64, Custom);

  setOperationAction(ISD::SHL_PARTS, MVT::i64, Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i64, Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i64, Custom);

  setOperationAction(ISD::FREM, MVT::f32, Expand);
  setOperationAction(ISD::FREM, MVT::f64, Expand);
  setOperationAction(ISD::FREM, MVT::f80, Expand);

  setOperationAction(ISD::BUILD_PAIR, MVT::i64, Expand);

  // Custom lowering hooks are needed for XOR
  // to fold it into CSINC/CSINV.
  setOperationAction(ISD::XOR, MVT::i32, Custom);
  setOperationAction(ISD::XOR, MVT::i64, Custom);

  // Virtually no operation on f128 is legal, but LLVM can't expand them when
  // there's a valid register class, so we need custom operations in most cases.
  setOperationAction(ISD::FABS, MVT::f128, Expand);
  setOperationAction(ISD::FADD, MVT::f128, Custom);
  setOperationAction(ISD::FCOPYSIGN, MVT::f128, Expand);
  setOperationAction(ISD::FCOS, MVT::f128, Expand);
  setOperationAction(ISD::FDIV, MVT::f128, Custom);
  setOperationAction(ISD::FMA, MVT::f128, Expand);
  setOperationAction(ISD::FMUL, MVT::f128, Custom);
  setOperationAction(ISD::FNEG, MVT::f128, Expand);
  setOperationAction(ISD::FPOW, MVT::f128, Expand);
  setOperationAction(ISD::FREM, MVT::f128, Expand);
  setOperationAction(ISD::FRINT, MVT::f128, Expand);
  setOperationAction(ISD::FSIN, MVT::f128, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f128, Expand);
  setOperationAction(ISD::FSQRT, MVT::f128, Expand);
  setOperationAction(ISD::FSUB, MVT::f128, Custom);
  setOperationAction(ISD::FTRUNC, MVT::f128, Expand);
  setOperationAction(ISD::SETCC, MVT::f128, Custom);
  setOperationAction(ISD::BR_CC, MVT::f128, Custom);
  setOperationAction(ISD::SELECT, MVT::f128, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f128, Custom);
  setOperationAction(ISD::FP_EXTEND, MVT::f128, Custom);

  // Lowering for many of the conversions is actually specified by the non-f128
  // type. The LowerXXX function will be trivial when f128 isn't involved.
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i128, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i128, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::i128, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::i128, Custom);
  setOperationAction(ISD::FP_ROUND, MVT::f32, Custom);
  setOperationAction(ISD::FP_ROUND, MVT::f64, Custom);

  // Variable arguments.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Custom);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  // Variable-sized objects.
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  if (Subtarget->isTargetWindows())
    setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  else
    setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Expand);

  // Constant pool entries
  setOperationAction(ISD::ConstantPool, MVT::i64, Custom);

  // BlockAddress
  setOperationAction(ISD::BlockAddress, MVT::i64, Custom);

  // Add/Sub overflow ops with MVT::Glues are lowered to NZCV dependences.
  setOperationAction(ISD::ADDC, MVT::i32, Custom);
  setOperationAction(ISD::ADDE, MVT::i32, Custom);
  setOperationAction(ISD::SUBC, MVT::i32, Custom);
  setOperationAction(ISD::SUBE, MVT::i32, Custom);
  setOperationAction(ISD::ADDC, MVT::i64, Custom);
  setOperationAction(ISD::ADDE, MVT::i64, Custom);
  setOperationAction(ISD::SUBC, MVT::i64, Custom);
  setOperationAction(ISD::SUBE, MVT::i64, Custom);

  // AArch64 lacks both left-rotate and popcount instructions.
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  for (MVT VT : MVT::vector_valuetypes()) {
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
  }

  // AArch64 doesn't have {U|S}MUL_LOHI.
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);

  setOperationAction(ISD::CTPOP, MVT::i32, Custom);
  setOperationAction(ISD::CTPOP, MVT::i64, Custom);

  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i64, Expand);
  for (MVT VT : MVT::vector_valuetypes()) {
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
  }
  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::SREM, MVT::i64, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::UREM, MVT::i32, Expand);
  setOperationAction(ISD::UREM, MVT::i64, Expand);

  // Custom lower Add/Sub/Mul with overflow.
  setOperationAction(ISD::SADDO, MVT::i32, Custom);
  setOperationAction(ISD::SADDO, MVT::i64, Custom);
  setOperationAction(ISD::UADDO, MVT::i32, Custom);
  setOperationAction(ISD::UADDO, MVT::i64, Custom);
  setOperationAction(ISD::SSUBO, MVT::i32, Custom);
  setOperationAction(ISD::SSUBO, MVT::i64, Custom);
  setOperationAction(ISD::USUBO, MVT::i32, Custom);
  setOperationAction(ISD::USUBO, MVT::i64, Custom);
  setOperationAction(ISD::SMULO, MVT::i32, Custom);
  setOperationAction(ISD::SMULO, MVT::i64, Custom);
  setOperationAction(ISD::UMULO, MVT::i32, Custom);
  setOperationAction(ISD::UMULO, MVT::i64, Custom);

  setOperationAction(ISD::FSIN, MVT::f32, Expand);
  setOperationAction(ISD::FSIN, MVT::f64, Expand);
  setOperationAction(ISD::FCOS, MVT::f32, Expand);
  setOperationAction(ISD::FCOS, MVT::f64, Expand);
  setOperationAction(ISD::FPOW, MVT::f32, Expand);
  setOperationAction(ISD::FPOW, MVT::f64, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f64, Custom);
  setOperationAction(ISD::FCOPYSIGN, MVT::f32, Custom);
  if (Subtarget->hasFullFP16())
    setOperationAction(ISD::FCOPYSIGN, MVT::f16, Custom);
  else
    setOperationAction(ISD::FCOPYSIGN, MVT::f16, Promote);

  setOperationAction(ISD::FREM,    MVT::f16,   Promote);
  setOperationAction(ISD::FREM,    MVT::v4f16, Expand);
  setOperationAction(ISD::FREM,    MVT::v8f16, Expand);
  setOperationAction(ISD::FPOW,    MVT::f16,   Promote);
  setOperationAction(ISD::FPOW,    MVT::v4f16, Expand);
  setOperationAction(ISD::FPOW,    MVT::v8f16, Expand);
  setOperationAction(ISD::FPOWI,   MVT::f16,   Promote);
  setOperationAction(ISD::FPOWI,   MVT::v4f16, Expand);
  setOperationAction(ISD::FPOWI,   MVT::v8f16, Expand);
  setOperationAction(ISD::FCOS,    MVT::f16,   Promote);
  setOperationAction(ISD::FCOS,    MVT::v4f16, Expand);
  setOperationAction(ISD::FCOS,    MVT::v8f16, Expand);
  setOperationAction(ISD::FSIN,    MVT::f16,   Promote);
  setOperationAction(ISD::FSIN,    MVT::v4f16, Expand);
  setOperationAction(ISD::FSIN,    MVT::v8f16, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f16,   Promote);
  setOperationAction(ISD::FSINCOS, MVT::v4f16, Expand);
  setOperationAction(ISD::FSINCOS, MVT::v8f16, Expand);
  setOperationAction(ISD::FEXP,    MVT::f16,   Promote);
  setOperationAction(ISD::FEXP,    MVT::v4f16, Expand);
  setOperationAction(ISD::FEXP,    MVT::v8f16, Expand);
  setOperationAction(ISD::FEXP2,   MVT::f16,   Promote);
  setOperationAction(ISD::FEXP2,   MVT::v4f16, Expand);
  setOperationAction(ISD::FEXP2,   MVT::v8f16, Expand);
  setOperationAction(ISD::FLOG,    MVT::f16,   Promote);
  setOperationAction(ISD::FLOG,    MVT::v4f16, Expand);
  setOperationAction(ISD::FLOG,    MVT::v8f16, Expand);
  setOperationAction(ISD::FLOG2,   MVT::f16,   Promote);
  setOperationAction(ISD::FLOG2,   MVT::v4f16, Expand);
  setOperationAction(ISD::FLOG2,   MVT::v8f16, Expand);
  setOperationAction(ISD::FLOG10,  MVT::f16,   Promote);
  setOperationAction(ISD::FLOG10,  MVT::v4f16, Expand);
  setOperationAction(ISD::FLOG10,  MVT::v8f16, Expand);

  if (!Subtarget->hasFullFP16()) {
    setOperationAction(ISD::SELECT,      MVT::f16,  Promote);
    setOperationAction(ISD::SELECT_CC,   MVT::f16,  Promote);
    setOperationAction(ISD::SETCC,       MVT::f16,  Promote);
    setOperationAction(ISD::BR_CC,       MVT::f16,  Promote);
    setOperationAction(ISD::FADD,        MVT::f16,  Promote);
    setOperationAction(ISD::FSUB,        MVT::f16,  Promote);
    setOperationAction(ISD::FMUL,        MVT::f16,  Promote);
    setOperationAction(ISD::FDIV,        MVT::f16,  Promote);
    setOperationAction(ISD::FMA,         MVT::f16,  Promote);
    setOperationAction(ISD::FNEG,        MVT::f16,  Promote);
    setOperationAction(ISD::FABS,        MVT::f16,  Promote);
    setOperationAction(ISD::FCEIL,       MVT::f16,  Promote);
    setOperationAction(ISD::FSQRT,       MVT::f16,  Promote);
    setOperationAction(ISD::FFLOOR,      MVT::f16,  Promote);
    setOperationAction(ISD::FNEARBYINT,  MVT::f16,  Promote);
    setOperationAction(ISD::FRINT,       MVT::f16,  Promote);
    setOperationAction(ISD::FROUND,      MVT::f16,  Promote);
    setOperationAction(ISD::FTRUNC,      MVT::f16,  Promote);
    setOperationAction(ISD::FMINNUM,     MVT::f16,  Promote);
    setOperationAction(ISD::FMAXNUM,     MVT::f16,  Promote);
    setOperationAction(ISD::FMINIMUM,    MVT::f16,  Promote);
    setOperationAction(ISD::FMAXIMUM,    MVT::f16,  Promote);

    // promote v4f16 to v4f32 when that is known to be safe.
    setOperationAction(ISD::FADD,        MVT::v4f16, Promote);
    setOperationAction(ISD::FSUB,        MVT::v4f16, Promote);
    setOperationAction(ISD::FMUL,        MVT::v4f16, Promote);
    setOperationAction(ISD::FDIV,        MVT::v4f16, Promote);
    setOperationAction(ISD::FP_EXTEND,   MVT::v4f16, Promote);
    setOperationAction(ISD::FP_ROUND,    MVT::v4f16, Promote);
    AddPromotedToType(ISD::FADD,         MVT::v4f16, MVT::v4f32);
    AddPromotedToType(ISD::FSUB,         MVT::v4f16, MVT::v4f32);
    AddPromotedToType(ISD::FMUL,         MVT::v4f16, MVT::v4f32);
    AddPromotedToType(ISD::FDIV,         MVT::v4f16, MVT::v4f32);
    AddPromotedToType(ISD::FP_EXTEND,    MVT::v4f16, MVT::v4f32);
    AddPromotedToType(ISD::FP_ROUND,     MVT::v4f16, MVT::v4f32);

    setOperationAction(ISD::FABS,        MVT::v4f16, Expand);
    setOperationAction(ISD::FNEG,        MVT::v4f16, Expand);
    setOperationAction(ISD::FROUND,      MVT::v4f16, Expand);
    setOperationAction(ISD::FMA,         MVT::v4f16, Expand);
    setOperationAction(ISD::SETCC,       MVT::v4f16, Expand);
    setOperationAction(ISD::BR_CC,       MVT::v4f16, Expand);
    setOperationAction(ISD::SELECT,      MVT::v4f16, Expand);
    setOperationAction(ISD::SELECT_CC,   MVT::v4f16, Expand);
    setOperationAction(ISD::FTRUNC,      MVT::v4f16, Expand);
    setOperationAction(ISD::FCOPYSIGN,   MVT::v4f16, Expand);
    setOperationAction(ISD::FFLOOR,      MVT::v4f16, Expand);
    setOperationAction(ISD::FCEIL,       MVT::v4f16, Expand);
    setOperationAction(ISD::FRINT,       MVT::v4f16, Expand);
    setOperationAction(ISD::FNEARBYINT,  MVT::v4f16, Expand);
    setOperationAction(ISD::FSQRT,       MVT::v4f16, Expand);

    setOperationAction(ISD::FABS,        MVT::v8f16, Expand);
    setOperationAction(ISD::FADD,        MVT::v8f16, Expand);
    setOperationAction(ISD::FCEIL,       MVT::v8f16, Expand);
    setOperationAction(ISD::FCOPYSIGN,   MVT::v8f16, Expand);
    setOperationAction(ISD::FDIV,        MVT::v8f16, Expand);
    setOperationAction(ISD::FFLOOR,      MVT::v8f16, Expand);
    setOperationAction(ISD::FMA,         MVT::v8f16, Expand);
    setOperationAction(ISD::FMUL,        MVT::v8f16, Expand);
    setOperationAction(ISD::FNEARBYINT,  MVT::v8f16, Expand);
    setOperationAction(ISD::FNEG,        MVT::v8f16, Expand);
    setOperationAction(ISD::FROUND,      MVT::v8f16, Expand);
    setOperationAction(ISD::FRINT,       MVT::v8f16, Expand);
    setOperationAction(ISD::FSQRT,       MVT::v8f16, Expand);
    setOperationAction(ISD::FSUB,        MVT::v8f16, Expand);
    setOperationAction(ISD::FTRUNC,      MVT::v8f16, Expand);
    setOperationAction(ISD::SETCC,       MVT::v8f16, Expand);
    setOperationAction(ISD::BR_CC,       MVT::v8f16, Expand);
    setOperationAction(ISD::SELECT,      MVT::v8f16, Expand);
    setOperationAction(ISD::SELECT_CC,   MVT::v8f16, Expand);
    setOperationAction(ISD::FP_EXTEND,   MVT::v8f16, Expand);
  }

  // AArch64 has implementations of a lot of rounding-like FP operations.
  for (MVT Ty : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::FFLOOR, Ty, Legal);
    setOperationAction(ISD::FNEARBYINT, Ty, Legal);
    setOperationAction(ISD::FCEIL, Ty, Legal);
    setOperationAction(ISD::FRINT, Ty, Legal);
    setOperationAction(ISD::FTRUNC, Ty, Legal);
    setOperationAction(ISD::FROUND, Ty, Legal);
    setOperationAction(ISD::FMINNUM, Ty, Legal);
    setOperationAction(ISD::FMAXNUM, Ty, Legal);
    setOperationAction(ISD::FMINIMUM, Ty, Legal);
    setOperationAction(ISD::FMAXIMUM, Ty, Legal);
  }

  if (Subtarget->hasFullFP16()) {
    setOperationAction(ISD::FNEARBYINT, MVT::f16, Legal);
    setOperationAction(ISD::FFLOOR,  MVT::f16, Legal);
    setOperationAction(ISD::FCEIL,   MVT::f16, Legal);
    setOperationAction(ISD::FRINT,   MVT::f16, Legal);
    setOperationAction(ISD::FTRUNC,  MVT::f16, Legal);
    setOperationAction(ISD::FROUND,  MVT::f16, Legal);
    setOperationAction(ISD::FMINNUM, MVT::f16, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::f16, Legal);
    setOperationAction(ISD::FMINIMUM, MVT::f16, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::f16, Legal);
  }

  setOperationAction(ISD::PREFETCH, MVT::Other, Custom);

  setOperationAction(ISD::FLT_ROUNDS_, MVT::i32, Custom);

  setOperationAction(ISD::ATOMIC_CMP_SWAP, MVT::i128, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_SUB, MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_SUB, MVT::i64, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_AND, MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_AND, MVT::i64, Custom);

  // Lower READCYCLECOUNTER using an mrs from PMCCNTR_EL0.
  // This requires the Performance Monitors extension.
  if (Subtarget->hasPerfMon())
    setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Legal);

  if (getLibcallName(RTLIB::SINCOS_STRET_F32) != nullptr &&
      getLibcallName(RTLIB::SINCOS_STRET_F64) != nullptr) {
    // Issue __sincos_stret if available.
    setOperationAction(ISD::FSINCOS, MVT::f64, Custom);
    setOperationAction(ISD::FSINCOS, MVT::f32, Custom);
  } else {
    setOperationAction(ISD::FSINCOS, MVT::f64, Expand);
    setOperationAction(ISD::FSINCOS, MVT::f32, Expand);
  }

  // Make floating-point constants legal for the large code model, so they don't
  // become loads from the constant pool.
  if (Subtarget->isTargetMachO() && TM.getCodeModel() == CodeModel::Large) {
    setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
    setOperationAction(ISD::ConstantFP, MVT::f64, Legal);
  }

  // AArch64 does not have floating-point extending loads, i1 sign-extending
  // load, floating-point truncating stores, or v2i32->v2i16 truncating store.
  for (MVT VT : MVT::fp_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f16, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f32, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f64, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f80, Expand);
  }
  for (MVT VT : MVT::integer_valuetypes())
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Expand);

  setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f64, MVT::f16, Expand);
  setTruncStoreAction(MVT::f128, MVT::f80, Expand);
  setTruncStoreAction(MVT::f128, MVT::f64, Expand);
  setTruncStoreAction(MVT::f128, MVT::f32, Expand);
  setTruncStoreAction(MVT::f128, MVT::f16, Expand);

  setOperationAction(ISD::BITCAST, MVT::i16, Custom);
  setOperationAction(ISD::BITCAST, MVT::f16, Custom);

  // Indexed loads and stores are supported.
  for (unsigned im = (unsigned)ISD::PRE_INC;
       im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
    setIndexedLoadAction(im, MVT::i8, Legal);
    setIndexedLoadAction(im, MVT::i16, Legal);
    setIndexedLoadAction(im, MVT::i32, Legal);
    setIndexedLoadAction(im, MVT::i64, Legal);
    setIndexedLoadAction(im, MVT::f64, Legal);
    setIndexedLoadAction(im, MVT::f32, Legal);
    setIndexedLoadAction(im, MVT::f16, Legal);
    setIndexedStoreAction(im, MVT::i8, Legal);
    setIndexedStoreAction(im, MVT::i16, Legal);
    setIndexedStoreAction(im, MVT::i32, Legal);
    setIndexedStoreAction(im, MVT::i64, Legal);
    setIndexedStoreAction(im, MVT::f64, Legal);
    setIndexedStoreAction(im, MVT::f32, Legal);
    setIndexedStoreAction(im, MVT::f16, Legal);
  }

  // Trap.
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  // We combine OR nodes for bitfield operations.
  setTargetDAGCombine(ISD::OR);

  // Vector add and sub nodes may conceal a high-half opportunity.
  // Also, try to fold ADD into CSINC/CSINV..
  setTargetDAGCombine(ISD::ADD);
  setTargetDAGCombine(ISD::SUB);
  setTargetDAGCombine(ISD::SRL);
  setTargetDAGCombine(ISD::XOR);
  setTargetDAGCombine(ISD::SINT_TO_FP);
  setTargetDAGCombine(ISD::UINT_TO_FP);

  setTargetDAGCombine(ISD::FP_TO_SINT);
  setTargetDAGCombine(ISD::FP_TO_UINT);
  setTargetDAGCombine(ISD::FDIV);

  setTargetDAGCombine(ISD::INTRINSIC_WO_CHAIN);

  setTargetDAGCombine(ISD::ANY_EXTEND);
  setTargetDAGCombine(ISD::ZERO_EXTEND);
  setTargetDAGCombine(ISD::SIGN_EXTEND);
  setTargetDAGCombine(ISD::BITCAST);
  setTargetDAGCombine(ISD::CONCAT_VECTORS);
  setTargetDAGCombine(ISD::STORE);
  if (Subtarget->supportsAddressTopByteIgnored())
    setTargetDAGCombine(ISD::LOAD);

  setTargetDAGCombine(ISD::MUL);

  setTargetDAGCombine(ISD::SELECT);
  setTargetDAGCombine(ISD::VSELECT);

  setTargetDAGCombine(ISD::INTRINSIC_VOID);
  setTargetDAGCombine(ISD::INTRINSIC_W_CHAIN);
  setTargetDAGCombine(ISD::INSERT_VECTOR_ELT);

  setTargetDAGCombine(ISD::GlobalAddress);

  // In case of strict alignment, avoid an excessive number of byte wide stores.
  MaxStoresPerMemsetOptSize = 8;
  MaxStoresPerMemset = Subtarget->requiresStrictAlign()
                       ? MaxStoresPerMemsetOptSize : 32;

  MaxGluedStoresPerMemcpy = 4;
  MaxStoresPerMemcpyOptSize = 4;
  MaxStoresPerMemcpy = Subtarget->requiresStrictAlign()
                       ? MaxStoresPerMemcpyOptSize : 16;

  MaxStoresPerMemmoveOptSize = MaxStoresPerMemmove = 4;

  setStackPointerRegisterToSaveRestore(AArch64::SP);

  setSchedulingPreference(Sched::Hybrid);

  EnableExtLdPromotion = true;

  // Set required alignment.
  setMinFunctionAlignment(2);
  // Set preferred alignments.
  setPrefFunctionAlignment(STI.getPrefFunctionAlignment());
  setPrefLoopAlignment(STI.getPrefLoopAlignment());

  // Only change the limit for entries in a jump table if specified by
  // the subtarget, but not at the command line.
  unsigned MaxJT = STI.getMaximumJumpTableSize();
  if (MaxJT && getMaximumJumpTableSize() == 0)
    setMaximumJumpTableSize(MaxJT);

  setHasExtractBitsInsn(true);

  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  if (Subtarget->hasNEON()) {
    // FIXME: v1f64 shouldn't be legal if we can avoid it, because it leads to
    // silliness like this:
    setOperationAction(ISD::FABS, MVT::v1f64, Expand);
    setOperationAction(ISD::FADD, MVT::v1f64, Expand);
    setOperationAction(ISD::FCEIL, MVT::v1f64, Expand);
    setOperationAction(ISD::FCOPYSIGN, MVT::v1f64, Expand);
    setOperationAction(ISD::FCOS, MVT::v1f64, Expand);
    setOperationAction(ISD::FDIV, MVT::v1f64, Expand);
    setOperationAction(ISD::FFLOOR, MVT::v1f64, Expand);
    setOperationAction(ISD::FMA, MVT::v1f64, Expand);
    setOperationAction(ISD::FMUL, MVT::v1f64, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v1f64, Expand);
    setOperationAction(ISD::FNEG, MVT::v1f64, Expand);
    setOperationAction(ISD::FPOW, MVT::v1f64, Expand);
    setOperationAction(ISD::FREM, MVT::v1f64, Expand);
    setOperationAction(ISD::FROUND, MVT::v1f64, Expand);
    setOperationAction(ISD::FRINT, MVT::v1f64, Expand);
    setOperationAction(ISD::FSIN, MVT::v1f64, Expand);
    setOperationAction(ISD::FSINCOS, MVT::v1f64, Expand);
    setOperationAction(ISD::FSQRT, MVT::v1f64, Expand);
    setOperationAction(ISD::FSUB, MVT::v1f64, Expand);
    setOperationAction(ISD::FTRUNC, MVT::v1f64, Expand);
    setOperationAction(ISD::SETCC, MVT::v1f64, Expand);
    setOperationAction(ISD::BR_CC, MVT::v1f64, Expand);
    setOperationAction(ISD::SELECT, MVT::v1f64, Expand);
    setOperationAction(ISD::SELECT_CC, MVT::v1f64, Expand);
    setOperationAction(ISD::FP_EXTEND, MVT::v1f64, Expand);

    setOperationAction(ISD::FP_TO_SINT, MVT::v1i64, Expand);
    setOperationAction(ISD::FP_TO_UINT, MVT::v1i64, Expand);
    setOperationAction(ISD::SINT_TO_FP, MVT::v1i64, Expand);
    setOperationAction(ISD::UINT_TO_FP, MVT::v1i64, Expand);
    setOperationAction(ISD::FP_ROUND, MVT::v1f64, Expand);

    setOperationAction(ISD::MUL, MVT::v1i64, Expand);

    // AArch64 doesn't have a direct vector ->f32 conversion instructions for
    // elements smaller than i32, so promote the input to i32 first.
    setOperationPromotedToType(ISD::UINT_TO_FP, MVT::v4i8, MVT::v4i32);
    setOperationPromotedToType(ISD::SINT_TO_FP, MVT::v4i8, MVT::v4i32);
    setOperationPromotedToType(ISD::UINT_TO_FP, MVT::v4i16, MVT::v4i32);
    setOperationPromotedToType(ISD::SINT_TO_FP, MVT::v4i16, MVT::v4i32);
    // i8 and i16 vector elements also need promotion to i32 for v8i8 or v8i16
    // -> v8f16 conversions.
    setOperationPromotedToType(ISD::SINT_TO_FP, MVT::v8i8, MVT::v8i32);
    setOperationPromotedToType(ISD::UINT_TO_FP, MVT::v8i8, MVT::v8i32);
    setOperationPromotedToType(ISD::SINT_TO_FP, MVT::v8i16, MVT::v8i32);
    setOperationPromotedToType(ISD::UINT_TO_FP, MVT::v8i16, MVT::v8i32);
    // Similarly, there is no direct i32 -> f64 vector conversion instruction.
    setOperationAction(ISD::SINT_TO_FP, MVT::v2i32, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v2i32, Custom);
    setOperationAction(ISD::SINT_TO_FP, MVT::v2i64, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v2i64, Custom);
    // Or, direct i32 -> f16 vector conversion.  Set it so custom, so the
    // conversion happens in two steps: v4i32 -> v4f32 -> v4f16
    setOperationAction(ISD::SINT_TO_FP, MVT::v4i32, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v4i32, Custom);

    setOperationAction(ISD::CTLZ,       MVT::v1i64, Expand);
    setOperationAction(ISD::CTLZ,       MVT::v2i64, Expand);

    setOperationAction(ISD::CTTZ,       MVT::v2i8,  Expand);
    setOperationAction(ISD::CTTZ,       MVT::v4i16, Expand);
    setOperationAction(ISD::CTTZ,       MVT::v2i32, Expand);
    setOperationAction(ISD::CTTZ,       MVT::v1i64, Expand);
    setOperationAction(ISD::CTTZ,       MVT::v16i8, Expand);
    setOperationAction(ISD::CTTZ,       MVT::v8i16, Expand);
    setOperationAction(ISD::CTTZ,       MVT::v4i32, Expand);
    setOperationAction(ISD::CTTZ,       MVT::v2i64, Expand);

    // AArch64 doesn't have MUL.2d:
    setOperationAction(ISD::MUL, MVT::v2i64, Expand);
    // Custom handling for some quad-vector types to detect MULL.
    setOperationAction(ISD::MUL, MVT::v8i16, Custom);
    setOperationAction(ISD::MUL, MVT::v4i32, Custom);
    setOperationAction(ISD::MUL, MVT::v2i64, Custom);

    // Vector reductions
    for (MVT VT : MVT::integer_valuetypes()) {
      setOperationAction(ISD::VECREDUCE_ADD, VT, Custom);
      setOperationAction(ISD::VECREDUCE_SMAX, VT, Custom);
      setOperationAction(ISD::VECREDUCE_SMIN, VT, Custom);
      setOperationAction(ISD::VECREDUCE_UMAX, VT, Custom);
      setOperationAction(ISD::VECREDUCE_UMIN, VT, Custom);
    }
    for (MVT VT : MVT::fp_valuetypes()) {
      setOperationAction(ISD::VECREDUCE_FMAX, VT, Custom);
      setOperationAction(ISD::VECREDUCE_FMIN, VT, Custom);
    }

    setOperationAction(ISD::ANY_EXTEND, MVT::v4i32, Legal);
    setTruncStoreAction(MVT::v2i32, MVT::v2i16, Expand);
    // Likewise, narrowing and extending vector loads/stores aren't handled
    // directly.
    for (MVT VT : MVT::vector_valuetypes()) {
      setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);

      if (VT == MVT::v16i8 || VT == MVT::v8i16 || VT == MVT::v4i32) {
        setOperationAction(ISD::MULHS, VT, Legal);
        setOperationAction(ISD::MULHU, VT, Legal);
      } else {
        setOperationAction(ISD::MULHS, VT, Expand);
        setOperationAction(ISD::MULHU, VT, Expand);
      }
      setOperationAction(ISD::SMUL_LOHI, VT, Expand);
      setOperationAction(ISD::UMUL_LOHI, VT, Expand);

      setOperationAction(ISD::BSWAP, VT, Expand);

      for (MVT InnerVT : MVT::vector_valuetypes()) {
        setTruncStoreAction(VT, InnerVT, Expand);
        setLoadExtAction(ISD::SEXTLOAD, VT, InnerVT, Expand);
        setLoadExtAction(ISD::ZEXTLOAD, VT, InnerVT, Expand);
        setLoadExtAction(ISD::EXTLOAD, VT, InnerVT, Expand);
      }
    }

    // AArch64 has implementations of a lot of rounding-like FP operations.
    for (MVT Ty : {MVT::v2f32, MVT::v4f32, MVT::v2f64}) {
      setOperationAction(ISD::FFLOOR, Ty, Legal);
      setOperationAction(ISD::FNEARBYINT, Ty, Legal);
      setOperationAction(ISD::FCEIL, Ty, Legal);
      setOperationAction(ISD::FRINT, Ty, Legal);
      setOperationAction(ISD::FTRUNC, Ty, Legal);
      setOperationAction(ISD::FROUND, Ty, Legal);
    }

    setTruncStoreAction(MVT::v4i16, MVT::v4i8, Custom);
  }

  PredictableSelectIsExpensive = Subtarget->predictableSelectIsExpensive();
}

void AArch64TargetLowering::addTypeForNEON(MVT VT, MVT PromotedBitwiseVT) {
  assert(VT.isVector() && "VT should be a vector type");

  if (VT.isFloatingPoint()) {
    MVT PromoteTo = EVT(VT).changeVectorElementTypeToInteger().getSimpleVT();
    setOperationPromotedToType(ISD::LOAD, VT, PromoteTo);
    setOperationPromotedToType(ISD::STORE, VT, PromoteTo);
  }

  // Mark vector float intrinsics as expand.
  if (VT == MVT::v2f32 || VT == MVT::v4f32 || VT == MVT::v2f64) {
    setOperationAction(ISD::FSIN, VT, Expand);
    setOperationAction(ISD::FCOS, VT, Expand);
    setOperationAction(ISD::FPOW, VT, Expand);
    setOperationAction(ISD::FLOG, VT, Expand);
    setOperationAction(ISD::FLOG2, VT, Expand);
    setOperationAction(ISD::FLOG10, VT, Expand);
    setOperationAction(ISD::FEXP, VT, Expand);
    setOperationAction(ISD::FEXP2, VT, Expand);

    // But we do support custom-lowering for FCOPYSIGN.
    setOperationAction(ISD::FCOPYSIGN, VT, Custom);
  }

  setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Custom);
  setOperationAction(ISD::BUILD_VECTOR, VT, Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, VT, Custom);
  setOperationAction(ISD::SRA, VT, Custom);
  setOperationAction(ISD::SRL, VT, Custom);
  setOperationAction(ISD::SHL, VT, Custom);
  setOperationAction(ISD::AND, VT, Custom);
  setOperationAction(ISD::OR, VT, Custom);
  setOperationAction(ISD::SETCC, VT, Custom);
  setOperationAction(ISD::CONCAT_VECTORS, VT, Legal);

  setOperationAction(ISD::SELECT, VT, Expand);
  setOperationAction(ISD::SELECT_CC, VT, Expand);
  setOperationAction(ISD::VSELECT, VT, Expand);
  for (MVT InnerVT : MVT::all_valuetypes())
    setLoadExtAction(ISD::EXTLOAD, InnerVT, VT, Expand);

  // CNT supports only B element sizes, then use UADDLP to widen.
  if (VT != MVT::v8i8 && VT != MVT::v16i8)
    setOperationAction(ISD::CTPOP, VT, Custom);

  setOperationAction(ISD::UDIV, VT, Expand);
  setOperationAction(ISD::SDIV, VT, Expand);
  setOperationAction(ISD::UREM, VT, Expand);
  setOperationAction(ISD::SREM, VT, Expand);
  setOperationAction(ISD::FREM, VT, Expand);

  setOperationAction(ISD::FP_TO_SINT, VT, Custom);
  setOperationAction(ISD::FP_TO_UINT, VT, Custom);

  if (!VT.isFloatingPoint())
    setOperationAction(ISD::ABS, VT, Legal);

  // [SU][MIN|MAX] are available for all NEON types apart from i64.
  if (!VT.isFloatingPoint() && VT != MVT::v2i64 && VT != MVT::v1i64)
    for (unsigned Opcode : {ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX})
      setOperationAction(Opcode, VT, Legal);

  // F[MIN|MAX][NUM|NAN] are available for all FP NEON types.
  if (VT.isFloatingPoint() &&
      (VT.getVectorElementType() != MVT::f16 || Subtarget->hasFullFP16()))
    for (unsigned Opcode :
         {ISD::FMINIMUM, ISD::FMAXIMUM, ISD::FMINNUM, ISD::FMAXNUM})
      setOperationAction(Opcode, VT, Legal);

  if (Subtarget->isLittleEndian()) {
    for (unsigned im = (unsigned)ISD::PRE_INC;
         im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
      setIndexedLoadAction(im, VT, Legal);
      setIndexedStoreAction(im, VT, Legal);
    }
  }
}

void AArch64TargetLowering::addDRTypeForNEON(MVT VT) {
  addRegisterClass(VT, &AArch64::FPR64RegClass);
  addTypeForNEON(VT, MVT::v2i32);
}

void AArch64TargetLowering::addQRTypeForNEON(MVT VT) {
  addRegisterClass(VT, &AArch64::FPR128RegClass);
  addTypeForNEON(VT, MVT::v4i32);
}

EVT AArch64TargetLowering::getSetCCResultType(const DataLayout &, LLVMContext &,
                                              EVT VT) const {
  if (!VT.isVector())
    return MVT::i32;
  return VT.changeVectorElementTypeToInteger();
}

static bool optimizeLogicalImm(SDValue Op, unsigned Size, uint64_t Imm,
                               const APInt &Demanded,
                               TargetLowering::TargetLoweringOpt &TLO,
                               unsigned NewOpc) {
  uint64_t OldImm = Imm, NewImm, Enc;
  uint64_t Mask = ((uint64_t)(-1LL) >> (64 - Size)), OrigMask = Mask;

  // Return if the immediate is already all zeros, all ones, a bimm32 or a
  // bimm64.
  if (Imm == 0 || Imm == Mask ||
      AArch64_AM::isLogicalImmediate(Imm & Mask, Size))
    return false;

  unsigned EltSize = Size;
  uint64_t DemandedBits = Demanded.getZExtValue();

  // Clear bits that are not demanded.
  Imm &= DemandedBits;

  while (true) {
    // The goal here is to set the non-demanded bits in a way that minimizes
    // the number of switching between 0 and 1. In order to achieve this goal,
    // we set the non-demanded bits to the value of the preceding demanded bits.
    // For example, if we have an immediate 0bx10xx0x1 ('x' indicates a
    // non-demanded bit), we copy bit0 (1) to the least significant 'x',
    // bit2 (0) to 'xx', and bit6 (1) to the most significant 'x'.
    // The final result is 0b11000011.
    uint64_t NonDemandedBits = ~DemandedBits;
    uint64_t InvertedImm = ~Imm & DemandedBits;
    uint64_t RotatedImm =
        ((InvertedImm << 1) | (InvertedImm >> (EltSize - 1) & 1)) &
        NonDemandedBits;
    uint64_t Sum = RotatedImm + NonDemandedBits;
    bool Carry = NonDemandedBits & ~Sum & (1ULL << (EltSize - 1));
    uint64_t Ones = (Sum + Carry) & NonDemandedBits;
    NewImm = (Imm | Ones) & Mask;

    // If NewImm or its bitwise NOT is a shifted mask, it is a bitmask immediate
    // or all-ones or all-zeros, in which case we can stop searching. Otherwise,
    // we halve the element size and continue the search.
    if (isShiftedMask_64(NewImm) || isShiftedMask_64(~(NewImm | ~Mask)))
      break;

    // We cannot shrink the element size any further if it is 2-bits.
    if (EltSize == 2)
      return false;

    EltSize /= 2;
    Mask >>= EltSize;
    uint64_t Hi = Imm >> EltSize, DemandedBitsHi = DemandedBits >> EltSize;

    // Return if there is mismatch in any of the demanded bits of Imm and Hi.
    if (((Imm ^ Hi) & (DemandedBits & DemandedBitsHi) & Mask) != 0)
      return false;

    // Merge the upper and lower halves of Imm and DemandedBits.
    Imm |= Hi;
    DemandedBits |= DemandedBitsHi;
  }

  ++NumOptimizedImms;

  // Replicate the element across the register width.
  while (EltSize < Size) {
    NewImm |= NewImm << EltSize;
    EltSize *= 2;
  }

  (void)OldImm;
  assert(((OldImm ^ NewImm) & Demanded.getZExtValue()) == 0 &&
         "demanded bits should never be altered");
  assert(OldImm != NewImm && "the new imm shouldn't be equal to the old imm");

  // Create the new constant immediate node.
  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  SDValue New;

  // If the new constant immediate is all-zeros or all-ones, let the target
  // independent DAG combine optimize this node.
  if (NewImm == 0 || NewImm == OrigMask) {
    New = TLO.DAG.getNode(Op.getOpcode(), DL, VT, Op.getOperand(0),
                          TLO.DAG.getConstant(NewImm, DL, VT));
  // Otherwise, create a machine node so that target independent DAG combine
  // doesn't undo this optimization.
  } else {
    Enc = AArch64_AM::encodeLogicalImmediate(NewImm, Size);
    SDValue EncConst = TLO.DAG.getTargetConstant(Enc, DL, VT);
    New = SDValue(
        TLO.DAG.getMachineNode(NewOpc, DL, VT, Op.getOperand(0), EncConst), 0);
  }

  return TLO.CombineTo(Op, New);
}

bool AArch64TargetLowering::targetShrinkDemandedConstant(
    SDValue Op, const APInt &Demanded, TargetLoweringOpt &TLO) const {
  // Delay this optimization to as late as possible.
  if (!TLO.LegalOps)
    return false;

  if (!EnableOptimizeLogicalImm)
    return false;

  EVT VT = Op.getValueType();
  if (VT.isVector())
    return false;

  unsigned Size = VT.getSizeInBits();
  assert((Size == 32 || Size == 64) &&
         "i32 or i64 is expected after legalization.");

  // Exit early if we demand all bits.
  if (Demanded.countPopulation() == Size)
    return false;

  unsigned NewOpc;
  switch (Op.getOpcode()) {
  default:
    return false;
  case ISD::AND:
    NewOpc = Size == 32 ? AArch64::ANDWri : AArch64::ANDXri;
    break;
  case ISD::OR:
    NewOpc = Size == 32 ? AArch64::ORRWri : AArch64::ORRXri;
    break;
  case ISD::XOR:
    NewOpc = Size == 32 ? AArch64::EORWri : AArch64::EORXri;
    break;
  }
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!C)
    return false;
  uint64_t Imm = C->getZExtValue();
  return optimizeLogicalImm(Op, Size, Imm, Demanded, TLO, NewOpc);
}

/// computeKnownBitsForTargetNode - Determine which of the bits specified in
/// Mask are known to be either zero or one and return them Known.
void AArch64TargetLowering::computeKnownBitsForTargetNode(
    const SDValue Op, KnownBits &Known,
    const APInt &DemandedElts, const SelectionDAG &DAG, unsigned Depth) const {
  switch (Op.getOpcode()) {
  default:
    break;
  case AArch64ISD::CSEL: {
    KnownBits Known2;
    Known = DAG.computeKnownBits(Op->getOperand(0), Depth + 1);
    Known2 = DAG.computeKnownBits(Op->getOperand(1), Depth + 1);
    Known.Zero &= Known2.Zero;
    Known.One &= Known2.One;
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    ConstantSDNode *CN = cast<ConstantSDNode>(Op->getOperand(1));
    Intrinsic::ID IntID = static_cast<Intrinsic::ID>(CN->getZExtValue());
    switch (IntID) {
    default: return;
    case Intrinsic::aarch64_ldaxr:
    case Intrinsic::aarch64_ldxr: {
      unsigned BitWidth = Known.getBitWidth();
      EVT VT = cast<MemIntrinsicSDNode>(Op)->getMemoryVT();
      unsigned MemBits = VT.getScalarSizeInBits();
      Known.Zero |= APInt::getHighBitsSet(BitWidth, BitWidth - MemBits);
      return;
    }
    }
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID: {
    unsigned IntNo = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
    switch (IntNo) {
    default:
      break;
    case Intrinsic::aarch64_neon_umaxv:
    case Intrinsic::aarch64_neon_uminv: {
      // Figure out the datatype of the vector operand. The UMINV instruction
      // will zero extend the result, so we can mark as known zero all the
      // bits larger than the element datatype. 32-bit or larget doesn't need
      // this as those are legal types and will be handled by isel directly.
      MVT VT = Op.getOperand(1).getValueType().getSimpleVT();
      unsigned BitWidth = Known.getBitWidth();
      if (VT == MVT::v8i8 || VT == MVT::v16i8) {
        assert(BitWidth >= 8 && "Unexpected width!");
        APInt Mask = APInt::getHighBitsSet(BitWidth, BitWidth - 8);
        Known.Zero |= Mask;
      } else if (VT == MVT::v4i16 || VT == MVT::v8i16) {
        assert(BitWidth >= 16 && "Unexpected width!");
        APInt Mask = APInt::getHighBitsSet(BitWidth, BitWidth - 16);
        Known.Zero |= Mask;
      }
      break;
    } break;
    }
  }
  }
}

MVT AArch64TargetLowering::getScalarShiftAmountTy(const DataLayout &DL,
                                                  EVT) const {
  return MVT::i64;
}

bool AArch64TargetLowering::allowsMisalignedMemoryAccesses(EVT VT,
                                                           unsigned AddrSpace,
                                                           unsigned Align,
                                                           bool *Fast) const {
  if (Subtarget->requiresStrictAlign())
    return false;

  if (Fast) {
    // Some CPUs are fine with unaligned stores except for 128-bit ones.
    *Fast = !Subtarget->isMisaligned128StoreSlow() || VT.getStoreSize() != 16 ||
            // See comments in performSTORECombine() for more details about
            // these conditions.

            // Code that uses clang vector extensions can mark that it
            // wants unaligned accesses to be treated as fast by
            // underspecifying alignment to be 1 or 2.
            Align <= 2 ||

            // Disregard v2i64. Memcpy lowering produces those and splitting
            // them regresses performance on micro-benchmarks and olden/bh.
            VT == MVT::v2i64;
  }
  return true;
}

FastISel *
AArch64TargetLowering::createFastISel(FunctionLoweringInfo &funcInfo,
                                      const TargetLibraryInfo *libInfo) const {
  return AArch64::createFastISel(funcInfo, libInfo);
}

const char *AArch64TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((AArch64ISD::NodeType)Opcode) {
  case AArch64ISD::FIRST_NUMBER:      break;
  case AArch64ISD::CALL:              return "AArch64ISD::CALL";
  case AArch64ISD::ADRP:              return "AArch64ISD::ADRP";
  case AArch64ISD::ADR:               return "AArch64ISD::ADR";
  case AArch64ISD::ADDlow:            return "AArch64ISD::ADDlow";
  case AArch64ISD::LOADgot:           return "AArch64ISD::LOADgot";
  case AArch64ISD::RET_FLAG:          return "AArch64ISD::RET_FLAG";
  case AArch64ISD::BRCOND:            return "AArch64ISD::BRCOND";
  case AArch64ISD::CSEL:              return "AArch64ISD::CSEL";
  case AArch64ISD::FCSEL:             return "AArch64ISD::FCSEL";
  case AArch64ISD::CSINV:             return "AArch64ISD::CSINV";
  case AArch64ISD::CSNEG:             return "AArch64ISD::CSNEG";
  case AArch64ISD::CSINC:             return "AArch64ISD::CSINC";
  case AArch64ISD::THREAD_POINTER:    return "AArch64ISD::THREAD_POINTER";
  case AArch64ISD::TLSDESC_CALLSEQ:   return "AArch64ISD::TLSDESC_CALLSEQ";
  case AArch64ISD::ADC:               return "AArch64ISD::ADC";
  case AArch64ISD::SBC:               return "AArch64ISD::SBC";
  case AArch64ISD::ADDS:              return "AArch64ISD::ADDS";
  case AArch64ISD::SUBS:              return "AArch64ISD::SUBS";
  case AArch64ISD::ADCS:              return "AArch64ISD::ADCS";
  case AArch64ISD::SBCS:              return "AArch64ISD::SBCS";
  case AArch64ISD::ANDS:              return "AArch64ISD::ANDS";
  case AArch64ISD::CCMP:              return "AArch64ISD::CCMP";
  case AArch64ISD::CCMN:              return "AArch64ISD::CCMN";
  case AArch64ISD::FCCMP:             return "AArch64ISD::FCCMP";
  case AArch64ISD::FCMP:              return "AArch64ISD::FCMP";
  case AArch64ISD::DUP:               return "AArch64ISD::DUP";
  case AArch64ISD::DUPLANE8:          return "AArch64ISD::DUPLANE8";
  case AArch64ISD::DUPLANE16:         return "AArch64ISD::DUPLANE16";
  case AArch64ISD::DUPLANE32:         return "AArch64ISD::DUPLANE32";
  case AArch64ISD::DUPLANE64:         return "AArch64ISD::DUPLANE64";
  case AArch64ISD::MOVI:              return "AArch64ISD::MOVI";
  case AArch64ISD::MOVIshift:         return "AArch64ISD::MOVIshift";
  case AArch64ISD::MOVIedit:          return "AArch64ISD::MOVIedit";
  case AArch64ISD::MOVImsl:           return "AArch64ISD::MOVImsl";
  case AArch64ISD::FMOV:              return "AArch64ISD::FMOV";
  case AArch64ISD::MVNIshift:         return "AArch64ISD::MVNIshift";
  case AArch64ISD::MVNImsl:           return "AArch64ISD::MVNImsl";
  case AArch64ISD::BICi:              return "AArch64ISD::BICi";
  case AArch64ISD::ORRi:              return "AArch64ISD::ORRi";
  case AArch64ISD::BSL:               return "AArch64ISD::BSL";
  case AArch64ISD::NEG:               return "AArch64ISD::NEG";
  case AArch64ISD::EXTR:              return "AArch64ISD::EXTR";
  case AArch64ISD::ZIP1:              return "AArch64ISD::ZIP1";
  case AArch64ISD::ZIP2:              return "AArch64ISD::ZIP2";
  case AArch64ISD::UZP1:              return "AArch64ISD::UZP1";
  case AArch64ISD::UZP2:              return "AArch64ISD::UZP2";
  case AArch64ISD::TRN1:              return "AArch64ISD::TRN1";
  case AArch64ISD::TRN2:              return "AArch64ISD::TRN2";
  case AArch64ISD::REV16:             return "AArch64ISD::REV16";
  case AArch64ISD::REV32:             return "AArch64ISD::REV32";
  case AArch64ISD::REV64:             return "AArch64ISD::REV64";
  case AArch64ISD::EXT:               return "AArch64ISD::EXT";
  case AArch64ISD::VSHL:              return "AArch64ISD::VSHL";
  case AArch64ISD::VLSHR:             return "AArch64ISD::VLSHR";
  case AArch64ISD::VASHR:             return "AArch64ISD::VASHR";
  case AArch64ISD::CMEQ:              return "AArch64ISD::CMEQ";
  case AArch64ISD::CMGE:              return "AArch64ISD::CMGE";
  case AArch64ISD::CMGT:              return "AArch64ISD::CMGT";
  case AArch64ISD::CMHI:              return "AArch64ISD::CMHI";
  case AArch64ISD::CMHS:              return "AArch64ISD::CMHS";
  case AArch64ISD::FCMEQ:             return "AArch64ISD::FCMEQ";
  case AArch64ISD::FCMGE:             return "AArch64ISD::FCMGE";
  case AArch64ISD::FCMGT:             return "AArch64ISD::FCMGT";
  case AArch64ISD::CMEQz:             return "AArch64ISD::CMEQz";
  case AArch64ISD::CMGEz:             return "AArch64ISD::CMGEz";
  case AArch64ISD::CMGTz:             return "AArch64ISD::CMGTz";
  case AArch64ISD::CMLEz:             return "AArch64ISD::CMLEz";
  case AArch64ISD::CMLTz:             return "AArch64ISD::CMLTz";
  case AArch64ISD::FCMEQz:            return "AArch64ISD::FCMEQz";
  case AArch64ISD::FCMGEz:            return "AArch64ISD::FCMGEz";
  case AArch64ISD::FCMGTz:            return "AArch64ISD::FCMGTz";
  case AArch64ISD::FCMLEz:            return "AArch64ISD::FCMLEz";
  case AArch64ISD::FCMLTz:            return "AArch64ISD::FCMLTz";
  case AArch64ISD::SADDV:             return "AArch64ISD::SADDV";
  case AArch64ISD::UADDV:             return "AArch64ISD::UADDV";
  case AArch64ISD::SMINV:             return "AArch64ISD::SMINV";
  case AArch64ISD::UMINV:             return "AArch64ISD::UMINV";
  case AArch64ISD::SMAXV:             return "AArch64ISD::SMAXV";
  case AArch64ISD::UMAXV:             return "AArch64ISD::UMAXV";
  case AArch64ISD::NOT:               return "AArch64ISD::NOT";
  case AArch64ISD::BIT:               return "AArch64ISD::BIT";
  case AArch64ISD::CBZ:               return "AArch64ISD::CBZ";
  case AArch64ISD::CBNZ:              return "AArch64ISD::CBNZ";
  case AArch64ISD::TBZ:               return "AArch64ISD::TBZ";
  case AArch64ISD::TBNZ:              return "AArch64ISD::TBNZ";
  case AArch64ISD::TC_RETURN:         return "AArch64ISD::TC_RETURN";
  case AArch64ISD::PREFETCH:          return "AArch64ISD::PREFETCH";
  case AArch64ISD::SITOF:             return "AArch64ISD::SITOF";
  case AArch64ISD::UITOF:             return "AArch64ISD::UITOF";
  case AArch64ISD::NVCAST:            return "AArch64ISD::NVCAST";
  case AArch64ISD::SQSHL_I:           return "AArch64ISD::SQSHL_I";
  case AArch64ISD::UQSHL_I:           return "AArch64ISD::UQSHL_I";
  case AArch64ISD::SRSHR_I:           return "AArch64ISD::SRSHR_I";
  case AArch64ISD::URSHR_I:           return "AArch64ISD::URSHR_I";
  case AArch64ISD::SQSHLU_I:          return "AArch64ISD::SQSHLU_I";
  case AArch64ISD::WrapperLarge:      return "AArch64ISD::WrapperLarge";
  case AArch64ISD::LD2post:           return "AArch64ISD::LD2post";
  case AArch64ISD::LD3post:           return "AArch64ISD::LD3post";
  case AArch64ISD::LD4post:           return "AArch64ISD::LD4post";
  case AArch64ISD::ST2post:           return "AArch64ISD::ST2post";
  case AArch64ISD::ST3post:           return "AArch64ISD::ST3post";
  case AArch64ISD::ST4post:           return "AArch64ISD::ST4post";
  case AArch64ISD::LD1x2post:         return "AArch64ISD::LD1x2post";
  case AArch64ISD::LD1x3post:         return "AArch64ISD::LD1x3post";
  case AArch64ISD::LD1x4post:         return "AArch64ISD::LD1x4post";
  case AArch64ISD::ST1x2post:         return "AArch64ISD::ST1x2post";
  case AArch64ISD::ST1x3post:         return "AArch64ISD::ST1x3post";
  case AArch64ISD::ST1x4post:         return "AArch64ISD::ST1x4post";
  case AArch64ISD::LD1DUPpost:        return "AArch64ISD::LD1DUPpost";
  case AArch64ISD::LD2DUPpost:        return "AArch64ISD::LD2DUPpost";
  case AArch64ISD::LD3DUPpost:        return "AArch64ISD::LD3DUPpost";
  case AArch64ISD::LD4DUPpost:        return "AArch64ISD::LD4DUPpost";
  case AArch64ISD::LD1LANEpost:       return "AArch64ISD::LD1LANEpost";
  case AArch64ISD::LD2LANEpost:       return "AArch64ISD::LD2LANEpost";
  case AArch64ISD::LD3LANEpost:       return "AArch64ISD::LD3LANEpost";
  case AArch64ISD::LD4LANEpost:       return "AArch64ISD::LD4LANEpost";
  case AArch64ISD::ST2LANEpost:       return "AArch64ISD::ST2LANEpost";
  case AArch64ISD::ST3LANEpost:       return "AArch64ISD::ST3LANEpost";
  case AArch64ISD::ST4LANEpost:       return "AArch64ISD::ST4LANEpost";
  case AArch64ISD::SMULL:             return "AArch64ISD::SMULL";
  case AArch64ISD::UMULL:             return "AArch64ISD::UMULL";
  case AArch64ISD::FRECPE:            return "AArch64ISD::FRECPE";
  case AArch64ISD::FRECPS:            return "AArch64ISD::FRECPS";
  case AArch64ISD::FRSQRTE:           return "AArch64ISD::FRSQRTE";
  case AArch64ISD::FRSQRTS:           return "AArch64ISD::FRSQRTS";
  }
  return nullptr;
}

MachineBasicBlock *
AArch64TargetLowering::EmitF128CSEL(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const {
  // We materialise the F128CSEL pseudo-instruction as some control flow and a
  // phi node:

  // OrigBB:
  //     [... previous instrs leading to comparison ...]
  //     b.ne TrueBB
  //     b EndBB
  // TrueBB:
  //     ; Fallthrough
  // EndBB:
  //     Dest = PHI [IfTrue, TrueBB], [IfFalse, OrigBB]

  MachineFunction *MF = MBB->getParent();
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  const BasicBlock *LLVM_BB = MBB->getBasicBlock();
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction::iterator It = ++MBB->getIterator();

  unsigned DestReg = MI.getOperand(0).getReg();
  unsigned IfTrueReg = MI.getOperand(1).getReg();
  unsigned IfFalseReg = MI.getOperand(2).getReg();
  unsigned CondCode = MI.getOperand(3).getImm();
  bool NZCVKilled = MI.getOperand(4).isKill();

  MachineBasicBlock *TrueBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *EndBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(It, TrueBB);
  MF->insert(It, EndBB);

  // Transfer rest of current basic-block to EndBB
  EndBB->splice(EndBB->begin(), MBB, std::next(MachineBasicBlock::iterator(MI)),
                MBB->end());
  EndBB->transferSuccessorsAndUpdatePHIs(MBB);

  BuildMI(MBB, DL, TII->get(AArch64::Bcc)).addImm(CondCode).addMBB(TrueBB);
  BuildMI(MBB, DL, TII->get(AArch64::B)).addMBB(EndBB);
  MBB->addSuccessor(TrueBB);
  MBB->addSuccessor(EndBB);

  // TrueBB falls through to the end.
  TrueBB->addSuccessor(EndBB);

  if (!NZCVKilled) {
    TrueBB->addLiveIn(AArch64::NZCV);
    EndBB->addLiveIn(AArch64::NZCV);
  }

  BuildMI(*EndBB, EndBB->begin(), DL, TII->get(AArch64::PHI), DestReg)
      .addReg(IfTrueReg)
      .addMBB(TrueBB)
      .addReg(IfFalseReg)
      .addMBB(MBB);

  MI.eraseFromParent();
  return EndBB;
}

MachineBasicBlock *AArch64TargetLowering::EmitLoweredCatchRet(
       MachineInstr &MI, MachineBasicBlock *BB) const {
  assert(!isAsynchronousEHPersonality(classifyEHPersonality(
             BB->getParent()->getFunction().getPersonalityFn())) &&
         "SEH does not use catchret!");
  return BB;
}

MachineBasicBlock *AArch64TargetLowering::EmitLoweredCatchPad(
     MachineInstr &MI, MachineBasicBlock *BB) const {
  MI.eraseFromParent();
  return BB;
}

MachineBasicBlock *AArch64TargetLowering::EmitInstrWithCustomInserter(
    MachineInstr &MI, MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default:
#ifndef NDEBUG
    MI.dump();
#endif
    llvm_unreachable("Unexpected instruction for custom inserter!");

  case AArch64::F128CSEL:
    return EmitF128CSEL(MI, BB);

  case TargetOpcode::STACKMAP:
  case TargetOpcode::PATCHPOINT:
    return emitPatchPoint(MI, BB);

  case AArch64::CATCHRET:
    return EmitLoweredCatchRet(MI, BB);
  case AArch64::CATCHPAD:
    return EmitLoweredCatchPad(MI, BB);
  }
}

//===----------------------------------------------------------------------===//
// AArch64 Lowering private implementation.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Lowering Code
//===----------------------------------------------------------------------===//

/// changeIntCCToAArch64CC - Convert a DAG integer condition code to an AArch64
/// CC
static AArch64CC::CondCode changeIntCCToAArch64CC(ISD::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Unknown condition code!");
  case ISD::SETNE:
    return AArch64CC::NE;
  case ISD::SETEQ:
    return AArch64CC::EQ;
  case ISD::SETGT:
    return AArch64CC::GT;
  case ISD::SETGE:
    return AArch64CC::GE;
  case ISD::SETLT:
    return AArch64CC::LT;
  case ISD::SETLE:
    return AArch64CC::LE;
  case ISD::SETUGT:
    return AArch64CC::HI;
  case ISD::SETUGE:
    return AArch64CC::HS;
  case ISD::SETULT:
    return AArch64CC::LO;
  case ISD::SETULE:
    return AArch64CC::LS;
  }
}

/// changeFPCCToAArch64CC - Convert a DAG fp condition code to an AArch64 CC.
static void changeFPCCToAArch64CC(ISD::CondCode CC,
                                  AArch64CC::CondCode &CondCode,
                                  AArch64CC::CondCode &CondCode2) {
  CondCode2 = AArch64CC::AL;
  switch (CC) {
  default:
    llvm_unreachable("Unknown FP condition!");
  case ISD::SETEQ:
  case ISD::SETOEQ:
    CondCode = AArch64CC::EQ;
    break;
  case ISD::SETGT:
  case ISD::SETOGT:
    CondCode = AArch64CC::GT;
    break;
  case ISD::SETGE:
  case ISD::SETOGE:
    CondCode = AArch64CC::GE;
    break;
  case ISD::SETOLT:
    CondCode = AArch64CC::MI;
    break;
  case ISD::SETOLE:
    CondCode = AArch64CC::LS;
    break;
  case ISD::SETONE:
    CondCode = AArch64CC::MI;
    CondCode2 = AArch64CC::GT;
    break;
  case ISD::SETO:
    CondCode = AArch64CC::VC;
    break;
  case ISD::SETUO:
    CondCode = AArch64CC::VS;
    break;
  case ISD::SETUEQ:
    CondCode = AArch64CC::EQ;
    CondCode2 = AArch64CC::VS;
    break;
  case ISD::SETUGT:
    CondCode = AArch64CC::HI;
    break;
  case ISD::SETUGE:
    CondCode = AArch64CC::PL;
    break;
  case ISD::SETLT:
  case ISD::SETULT:
    CondCode = AArch64CC::LT;
    break;
  case ISD::SETLE:
  case ISD::SETULE:
    CondCode = AArch64CC::LE;
    break;
  case ISD::SETNE:
  case ISD::SETUNE:
    CondCode = AArch64CC::NE;
    break;
  }
}

/// Convert a DAG fp condition code to an AArch64 CC.
/// This differs from changeFPCCToAArch64CC in that it returns cond codes that
/// should be AND'ed instead of OR'ed.
static void changeFPCCToANDAArch64CC(ISD::CondCode CC,
                                     AArch64CC::CondCode &CondCode,
                                     AArch64CC::CondCode &CondCode2) {
  CondCode2 = AArch64CC::AL;
  switch (CC) {
  default:
    changeFPCCToAArch64CC(CC, CondCode, CondCode2);
    assert(CondCode2 == AArch64CC::AL);
    break;
  case ISD::SETONE:
    // (a one b)
    // == ((a olt b) || (a ogt b))
    // == ((a ord b) && (a une b))
    CondCode = AArch64CC::VC;
    CondCode2 = AArch64CC::NE;
    break;
  case ISD::SETUEQ:
    // (a ueq b)
    // == ((a uno b) || (a oeq b))
    // == ((a ule b) && (a uge b))
    CondCode = AArch64CC::PL;
    CondCode2 = AArch64CC::LE;
    break;
  }
}

/// changeVectorFPCCToAArch64CC - Convert a DAG fp condition code to an AArch64
/// CC usable with the vector instructions. Fewer operations are available
/// without a real NZCV register, so we have to use less efficient combinations
/// to get the same effect.
static void changeVectorFPCCToAArch64CC(ISD::CondCode CC,
                                        AArch64CC::CondCode &CondCode,
                                        AArch64CC::CondCode &CondCode2,
                                        bool &Invert) {
  Invert = false;
  switch (CC) {
  default:
    // Mostly the scalar mappings work fine.
    changeFPCCToAArch64CC(CC, CondCode, CondCode2);
    break;
  case ISD::SETUO:
    Invert = true;
    LLVM_FALLTHROUGH;
  case ISD::SETO:
    CondCode = AArch64CC::MI;
    CondCode2 = AArch64CC::GE;
    break;
  case ISD::SETUEQ:
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETUGT:
  case ISD::SETUGE:
    // All of the compare-mask comparisons are ordered, but we can switch
    // between the two by a double inversion. E.g. ULE == !OGT.
    Invert = true;
    changeFPCCToAArch64CC(getSetCCInverse(CC, false), CondCode, CondCode2);
    break;
  }
}

static bool isLegalArithImmed(uint64_t C) {
  // Matches AArch64DAGToDAGISel::SelectArithImmed().
  bool IsLegal = (C >> 12 == 0) || ((C & 0xFFFULL) == 0 && C >> 24 == 0);
  LLVM_DEBUG(dbgs() << "Is imm " << C
                    << " legal: " << (IsLegal ? "yes\n" : "no\n"));
  return IsLegal;
}

// Can a (CMP op1, (sub 0, op2) be turned into a CMN instruction on
// the grounds that "op1 - (-op2) == op1 + op2" ? Not always, the C and V flags
// can be set differently by this operation. It comes down to whether
// "SInt(~op2)+1 == SInt(~op2+1)" (and the same for UInt). If they are then
// everything is fine. If not then the optimization is wrong. Thus general
// comparisons are only valid if op2 != 0.
//
// So, finally, the only LLVM-native comparisons that don't mention C and V
// are SETEQ and SETNE. They're the only ones we can safely use CMN for in
// the absence of information about op2.
static bool isCMN(SDValue Op, ISD::CondCode CC) {
  return Op.getOpcode() == ISD::SUB && isNullConstant(Op.getOperand(0)) &&
         (CC == ISD::SETEQ || CC == ISD::SETNE);
}

static SDValue emitComparison(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                              const SDLoc &dl, SelectionDAG &DAG) {
  EVT VT = LHS.getValueType();
  const bool FullFP16 =
    static_cast<const AArch64Subtarget &>(DAG.getSubtarget()).hasFullFP16();

  if (VT.isFloatingPoint()) {
    assert(VT != MVT::f128);
    if (VT == MVT::f16 && !FullFP16) {
      LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f32, LHS);
      RHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f32, RHS);
      VT = MVT::f32;
    }
    return DAG.getNode(AArch64ISD::FCMP, dl, VT, LHS, RHS);
  }

  // The CMP instruction is just an alias for SUBS, and representing it as
  // SUBS means that it's possible to get CSE with subtract operations.
  // A later phase can perform the optimization of setting the destination
  // register to WZR/XZR if it ends up being unused.
  unsigned Opcode = AArch64ISD::SUBS;

  if (isCMN(RHS, CC)) {
    // Can we combine a (CMP op1, (sub 0, op2) into a CMN instruction ?
    Opcode = AArch64ISD::ADDS;
    RHS = RHS.getOperand(1);
  } else if (isCMN(LHS, CC)) {
    // As we are looking for EQ/NE compares, the operands can be commuted ; can
    // we combine a (CMP (sub 0, op1), op2) into a CMN instruction ?
    Opcode = AArch64ISD::ADDS;
    LHS = LHS.getOperand(1);
  } else if (LHS.getOpcode() == ISD::AND && isNullConstant(RHS) &&
             !isUnsignedIntSetCC(CC)) {
    // Similarly, (CMP (and X, Y), 0) can be implemented with a TST
    // (a.k.a. ANDS) except that the flags are only guaranteed to work for one
    // of the signed comparisons.
    Opcode = AArch64ISD::ANDS;
    RHS = LHS.getOperand(1);
    LHS = LHS.getOperand(0);
  }

  return DAG.getNode(Opcode, dl, DAG.getVTList(VT, MVT_CC), LHS, RHS)
      .getValue(1);
}

/// \defgroup AArch64CCMP CMP;CCMP matching
///
/// These functions deal with the formation of CMP;CCMP;... sequences.
/// The CCMP/CCMN/FCCMP/FCCMPE instructions allow the conditional execution of
/// a comparison. They set the NZCV flags to a predefined value if their
/// predicate is false. This allows to express arbitrary conjunctions, for
/// example "cmp 0 (and (setCA (cmp A)) (setCB (cmp B)))"
/// expressed as:
///   cmp A
///   ccmp B, inv(CB), CA
///   check for CB flags
///
/// This naturally lets us implement chains of AND operations with SETCC
/// operands. And we can even implement some other situations by transforming
/// them:
///   - We can implement (NEG SETCC) i.e. negating a single comparison by
///     negating the flags used in a CCMP/FCCMP operations.
///   - We can negate the result of a whole chain of CMP/CCMP/FCCMP operations
///     by negating the flags we test for afterwards. i.e.
///     NEG (CMP CCMP CCCMP ...) can be implemented.
///   - Note that we can only ever negate all previously processed results.
///     What we can not implement by flipping the flags to test is a negation
///     of two sub-trees (because the negation affects all sub-trees emitted so
///     far, so the 2nd sub-tree we emit would also affect the first).
/// With those tools we can implement some OR operations:
///   - (OR (SETCC A) (SETCC B)) can be implemented via:
///     NEG (AND (NEG (SETCC A)) (NEG (SETCC B)))
///   - After transforming OR to NEG/AND combinations we may be able to use NEG
///     elimination rules from earlier to implement the whole thing as a
///     CCMP/FCCMP chain.
///
/// As complete example:
///     or (or (setCA (cmp A)) (setCB (cmp B)))
///        (and (setCC (cmp C)) (setCD (cmp D)))"
/// can be reassociated to:
///     or (and (setCC (cmp C)) setCD (cmp D))
//         (or (setCA (cmp A)) (setCB (cmp B)))
/// can be transformed to:
///     not (and (not (and (setCC (cmp C)) (setCD (cmp D))))
///              (and (not (setCA (cmp A)) (not (setCB (cmp B))))))"
/// which can be implemented as:
///   cmp C
///   ccmp D, inv(CD), CC
///   ccmp A, CA, inv(CD)
///   ccmp B, CB, inv(CA)
///   check for CB flags
///
/// A counterexample is "or (and A B) (and C D)" which translates to
/// not (and (not (and (not A) (not B))) (not (and (not C) (not D)))), we
/// can only implement 1 of the inner (not) operations, but not both!
/// @{

/// Create a conditional comparison; Use CCMP, CCMN or FCCMP as appropriate.
static SDValue emitConditionalComparison(SDValue LHS, SDValue RHS,
                                         ISD::CondCode CC, SDValue CCOp,
                                         AArch64CC::CondCode Predicate,
                                         AArch64CC::CondCode OutCC,
                                         const SDLoc &DL, SelectionDAG &DAG) {
  unsigned Opcode = 0;
  const bool FullFP16 =
    static_cast<const AArch64Subtarget &>(DAG.getSubtarget()).hasFullFP16();

  if (LHS.getValueType().isFloatingPoint()) {
    assert(LHS.getValueType() != MVT::f128);
    if (LHS.getValueType() == MVT::f16 && !FullFP16) {
      LHS = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, LHS);
      RHS = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, RHS);
    }
    Opcode = AArch64ISD::FCCMP;
  } else if (RHS.getOpcode() == ISD::SUB) {
    SDValue SubOp0 = RHS.getOperand(0);
    if (isNullConstant(SubOp0) && (CC == ISD::SETEQ || CC == ISD::SETNE)) {
      // See emitComparison() on why we can only do this for SETEQ and SETNE.
      Opcode = AArch64ISD::CCMN;
      RHS = RHS.getOperand(1);
    }
  }
  if (Opcode == 0)
    Opcode = AArch64ISD::CCMP;

  SDValue Condition = DAG.getConstant(Predicate, DL, MVT_CC);
  AArch64CC::CondCode InvOutCC = AArch64CC::getInvertedCondCode(OutCC);
  unsigned NZCV = AArch64CC::getNZCVToSatisfyCondCode(InvOutCC);
  SDValue NZCVOp = DAG.getConstant(NZCV, DL, MVT::i32);
  return DAG.getNode(Opcode, DL, MVT_CC, LHS, RHS, NZCVOp, Condition, CCOp);
}

/// Returns true if @p Val is a tree of AND/OR/SETCC operations that can be
/// expressed as a conjunction. See \ref AArch64CCMP.
/// \param CanNegate    Set to true if we can negate the whole sub-tree just by
///                     changing the conditions on the SETCC tests.
///                     (this means we can call emitConjunctionRec() with
///                      Negate==true on this sub-tree)
/// \param MustBeFirst  Set to true if this subtree needs to be negated and we
///                     cannot do the negation naturally. We are required to
///                     emit the subtree first in this case.
/// \param WillNegate   Is true if are called when the result of this
///                     subexpression must be negated. This happens when the
///                     outer expression is an OR. We can use this fact to know
///                     that we have a double negation (or (or ...) ...) that
///                     can be implemented for free.
static bool canEmitConjunction(const SDValue Val, bool &CanNegate,
                               bool &MustBeFirst, bool WillNegate,
                               unsigned Depth = 0) {
  if (!Val.hasOneUse())
    return false;
  unsigned Opcode = Val->getOpcode();
  if (Opcode == ISD::SETCC) {
    if (Val->getOperand(0).getValueType() == MVT::f128)
      return false;
    CanNegate = true;
    MustBeFirst = false;
    return true;
  }
  // Protect against exponential runtime and stack overflow.
  if (Depth > 6)
    return false;
  if (Opcode == ISD::AND || Opcode == ISD::OR) {
    bool IsOR = Opcode == ISD::OR;
    SDValue O0 = Val->getOperand(0);
    SDValue O1 = Val->getOperand(1);
    bool CanNegateL;
    bool MustBeFirstL;
    if (!canEmitConjunction(O0, CanNegateL, MustBeFirstL, IsOR, Depth+1))
      return false;
    bool CanNegateR;
    bool MustBeFirstR;
    if (!canEmitConjunction(O1, CanNegateR, MustBeFirstR, IsOR, Depth+1))
      return false;

    if (MustBeFirstL && MustBeFirstR)
      return false;

    if (IsOR) {
      // For an OR expression we need to be able to naturally negate at least
      // one side or we cannot do the transformation at all.
      if (!CanNegateL && !CanNegateR)
        return false;
      // If we the result of the OR will be negated and we can naturally negate
      // the leafs, then this sub-tree as a whole negates naturally.
      CanNegate = WillNegate && CanNegateL && CanNegateR;
      // If we cannot naturally negate the whole sub-tree, then this must be
      // emitted first.
      MustBeFirst = !CanNegate;
    } else {
      assert(Opcode == ISD::AND && "Must be OR or AND");
      // We cannot naturally negate an AND operation.
      CanNegate = false;
      MustBeFirst = MustBeFirstL || MustBeFirstR;
    }
    return true;
  }
  return false;
}

/// Emit conjunction or disjunction tree with the CMP/FCMP followed by a chain
/// of CCMP/CFCMP ops. See @ref AArch64CCMP.
/// Tries to transform the given i1 producing node @p Val to a series compare
/// and conditional compare operations. @returns an NZCV flags producing node
/// and sets @p OutCC to the flags that should be tested or returns SDValue() if
/// transformation was not possible.
/// \p Negate is true if we want this sub-tree being negated just by changing
/// SETCC conditions.
static SDValue emitConjunctionRec(SelectionDAG &DAG, SDValue Val,
    AArch64CC::CondCode &OutCC, bool Negate, SDValue CCOp,
    AArch64CC::CondCode Predicate) {
  // We're at a tree leaf, produce a conditional comparison operation.
  unsigned Opcode = Val->getOpcode();
  if (Opcode == ISD::SETCC) {
    SDValue LHS = Val->getOperand(0);
    SDValue RHS = Val->getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(Val->getOperand(2))->get();
    bool isInteger = LHS.getValueType().isInteger();
    if (Negate)
      CC = getSetCCInverse(CC, isInteger);
    SDLoc DL(Val);
    // Determine OutCC and handle FP special case.
    if (isInteger) {
      OutCC = changeIntCCToAArch64CC(CC);
    } else {
      assert(LHS.getValueType().isFloatingPoint());
      AArch64CC::CondCode ExtraCC;
      changeFPCCToANDAArch64CC(CC, OutCC, ExtraCC);
      // Some floating point conditions can't be tested with a single condition
      // code. Construct an additional comparison in this case.
      if (ExtraCC != AArch64CC::AL) {
        SDValue ExtraCmp;
        if (!CCOp.getNode())
          ExtraCmp = emitComparison(LHS, RHS, CC, DL, DAG);
        else
          ExtraCmp = emitConditionalComparison(LHS, RHS, CC, CCOp, Predicate,
                                               ExtraCC, DL, DAG);
        CCOp = ExtraCmp;
        Predicate = ExtraCC;
      }
    }

    // Produce a normal comparison if we are first in the chain
    if (!CCOp)
      return emitComparison(LHS, RHS, CC, DL, DAG);
    // Otherwise produce a ccmp.
    return emitConditionalComparison(LHS, RHS, CC, CCOp, Predicate, OutCC, DL,
                                     DAG);
  }
  assert(Val->hasOneUse() && "Valid conjunction/disjunction tree");

  bool IsOR = Opcode == ISD::OR;

  SDValue LHS = Val->getOperand(0);
  bool CanNegateL;
  bool MustBeFirstL;
  bool ValidL = canEmitConjunction(LHS, CanNegateL, MustBeFirstL, IsOR);
  assert(ValidL && "Valid conjunction/disjunction tree");
  (void)ValidL;

  SDValue RHS = Val->getOperand(1);
  bool CanNegateR;
  bool MustBeFirstR;
  bool ValidR = canEmitConjunction(RHS, CanNegateR, MustBeFirstR, IsOR);
  assert(ValidR && "Valid conjunction/disjunction tree");
  (void)ValidR;

  // Swap sub-tree that must come first to the right side.
  if (MustBeFirstL) {
    assert(!MustBeFirstR && "Valid conjunction/disjunction tree");
    std::swap(LHS, RHS);
    std::swap(CanNegateL, CanNegateR);
    std::swap(MustBeFirstL, MustBeFirstR);
  }

  bool NegateR;
  bool NegateAfterR;
  bool NegateL;
  bool NegateAfterAll;
  if (Opcode == ISD::OR) {
    // Swap the sub-tree that we can negate naturally to the left.
    if (!CanNegateL) {
      assert(CanNegateR && "at least one side must be negatable");
      assert(!MustBeFirstR && "invalid conjunction/disjunction tree");
      assert(!Negate);
      std::swap(LHS, RHS);
      NegateR = false;
      NegateAfterR = true;
    } else {
      // Negate the left sub-tree if possible, otherwise negate the result.
      NegateR = CanNegateR;
      NegateAfterR = !CanNegateR;
    }
    NegateL = true;
    NegateAfterAll = !Negate;
  } else {
    assert(Opcode == ISD::AND && "Valid conjunction/disjunction tree");
    assert(!Negate && "Valid conjunction/disjunction tree");

    NegateL = false;
    NegateR = false;
    NegateAfterR = false;
    NegateAfterAll = false;
  }

  // Emit sub-trees.
  AArch64CC::CondCode RHSCC;
  SDValue CmpR = emitConjunctionRec(DAG, RHS, RHSCC, NegateR, CCOp, Predicate);
  if (NegateAfterR)
    RHSCC = AArch64CC::getInvertedCondCode(RHSCC);
  SDValue CmpL = emitConjunctionRec(DAG, LHS, OutCC, NegateL, CmpR, RHSCC);
  if (NegateAfterAll)
    OutCC = AArch64CC::getInvertedCondCode(OutCC);
  return CmpL;
}

/// Emit expression as a conjunction (a series of CCMP/CFCMP ops).
/// In some cases this is even possible with OR operations in the expression.
/// See \ref AArch64CCMP.
/// \see emitConjunctionRec().
static SDValue emitConjunction(SelectionDAG &DAG, SDValue Val,
                               AArch64CC::CondCode &OutCC) {
  bool DummyCanNegate;
  bool DummyMustBeFirst;
  if (!canEmitConjunction(Val, DummyCanNegate, DummyMustBeFirst, false))
    return SDValue();

  return emitConjunctionRec(DAG, Val, OutCC, false, SDValue(), AArch64CC::AL);
}

/// @}

/// Returns how profitable it is to fold a comparison's operand's shift and/or
/// extension operations.
static unsigned getCmpOperandFoldingProfit(SDValue Op) {
  auto isSupportedExtend = [&](SDValue V) {
    if (V.getOpcode() == ISD::SIGN_EXTEND_INREG)
      return true;

    if (V.getOpcode() == ISD::AND)
      if (ConstantSDNode *MaskCst = dyn_cast<ConstantSDNode>(V.getOperand(1))) {
        uint64_t Mask = MaskCst->getZExtValue();
        return (Mask == 0xFF || Mask == 0xFFFF || Mask == 0xFFFFFFFF);
      }

    return false;
  };

  if (!Op.hasOneUse())
    return 0;

  if (isSupportedExtend(Op))
    return 1;

  unsigned Opc = Op.getOpcode();
  if (Opc == ISD::SHL || Opc == ISD::SRL || Opc == ISD::SRA)
    if (ConstantSDNode *ShiftCst = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
      uint64_t Shift = ShiftCst->getZExtValue();
      if (isSupportedExtend(Op.getOperand(0)))
        return (Shift <= 4) ? 2 : 1;
      EVT VT = Op.getValueType();
      if ((VT == MVT::i32 && Shift <= 31) || (VT == MVT::i64 && Shift <= 63))
        return 1;
    }

  return 0;
}

static SDValue getAArch64Cmp(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                             SDValue &AArch64cc, SelectionDAG &DAG,
                             const SDLoc &dl) {
  if (ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS.getNode())) {
    EVT VT = RHS.getValueType();
    uint64_t C = RHSC->getZExtValue();
    if (!isLegalArithImmed(C)) {
      // Constant does not fit, try adjusting it by one?
      switch (CC) {
      default:
        break;
      case ISD::SETLT:
      case ISD::SETGE:
        if ((VT == MVT::i32 && C != 0x80000000 &&
             isLegalArithImmed((uint32_t)(C - 1))) ||
            (VT == MVT::i64 && C != 0x80000000ULL &&
             isLegalArithImmed(C - 1ULL))) {
          CC = (CC == ISD::SETLT) ? ISD::SETLE : ISD::SETGT;
          C = (VT == MVT::i32) ? (uint32_t)(C - 1) : C - 1;
          RHS = DAG.getConstant(C, dl, VT);
        }
        break;
      case ISD::SETULT:
      case ISD::SETUGE:
        if ((VT == MVT::i32 && C != 0 &&
             isLegalArithImmed((uint32_t)(C - 1))) ||
            (VT == MVT::i64 && C != 0ULL && isLegalArithImmed(C - 1ULL))) {
          CC = (CC == ISD::SETULT) ? ISD::SETULE : ISD::SETUGT;
          C = (VT == MVT::i32) ? (uint32_t)(C - 1) : C - 1;
          RHS = DAG.getConstant(C, dl, VT);
        }
        break;
      case ISD::SETLE:
      case ISD::SETGT:
        if ((VT == MVT::i32 && C != INT32_MAX &&
             isLegalArithImmed((uint32_t)(C + 1))) ||
            (VT == MVT::i64 && C != INT64_MAX &&
             isLegalArithImmed(C + 1ULL))) {
          CC = (CC == ISD::SETLE) ? ISD::SETLT : ISD::SETGE;
          C = (VT == MVT::i32) ? (uint32_t)(C + 1) : C + 1;
          RHS = DAG.getConstant(C, dl, VT);
        }
        break;
      case ISD::SETULE:
      case ISD::SETUGT:
        if ((VT == MVT::i32 && C != UINT32_MAX &&
             isLegalArithImmed((uint32_t)(C + 1))) ||
            (VT == MVT::i64 && C != UINT64_MAX &&
             isLegalArithImmed(C + 1ULL))) {
          CC = (CC == ISD::SETULE) ? ISD::SETULT : ISD::SETUGE;
          C = (VT == MVT::i32) ? (uint32_t)(C + 1) : C + 1;
          RHS = DAG.getConstant(C, dl, VT);
        }
        break;
      }
    }
  }

  // Comparisons are canonicalized so that the RHS operand is simpler than the
  // LHS one, the extreme case being when RHS is an immediate. However, AArch64
  // can fold some shift+extend operations on the RHS operand, so swap the
  // operands if that can be done.
  //
  // For example:
  //    lsl     w13, w11, #1
  //    cmp     w13, w12
  // can be turned into:
  //    cmp     w12, w11, lsl #1
  if (!isa<ConstantSDNode>(RHS) ||
      !isLegalArithImmed(cast<ConstantSDNode>(RHS)->getZExtValue())) {
    SDValue TheLHS = isCMN(LHS, CC) ? LHS.getOperand(1) : LHS;

    if (getCmpOperandFoldingProfit(TheLHS) > getCmpOperandFoldingProfit(RHS)) {
      std::swap(LHS, RHS);
      CC = ISD::getSetCCSwappedOperands(CC);
    }
  }

  SDValue Cmp;
  AArch64CC::CondCode AArch64CC;
  if ((CC == ISD::SETEQ || CC == ISD::SETNE) && isa<ConstantSDNode>(RHS)) {
    const ConstantSDNode *RHSC = cast<ConstantSDNode>(RHS);

    // The imm operand of ADDS is an unsigned immediate, in the range 0 to 4095.
    // For the i8 operand, the largest immediate is 255, so this can be easily
    // encoded in the compare instruction. For the i16 operand, however, the
    // largest immediate cannot be encoded in the compare.
    // Therefore, use a sign extending load and cmn to avoid materializing the
    // -1 constant. For example,
    // movz w1, #65535
    // ldrh w0, [x0, #0]
    // cmp w0, w1
    // >
    // ldrsh w0, [x0, #0]
    // cmn w0, #1
    // Fundamental, we're relying on the property that (zext LHS) == (zext RHS)
    // if and only if (sext LHS) == (sext RHS). The checks are in place to
    // ensure both the LHS and RHS are truly zero extended and to make sure the
    // transformation is profitable.
    if ((RHSC->getZExtValue() >> 16 == 0) && isa<LoadSDNode>(LHS) &&
        cast<LoadSDNode>(LHS)->getExtensionType() == ISD::ZEXTLOAD &&
        cast<LoadSDNode>(LHS)->getMemoryVT() == MVT::i16 &&
        LHS.getNode()->hasNUsesOfValue(1, 0)) {
      int16_t ValueofRHS = cast<ConstantSDNode>(RHS)->getZExtValue();
      if (ValueofRHS < 0 && isLegalArithImmed(-ValueofRHS)) {
        SDValue SExt =
            DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, LHS.getValueType(), LHS,
                        DAG.getValueType(MVT::i16));
        Cmp = emitComparison(SExt, DAG.getConstant(ValueofRHS, dl,
                                                   RHS.getValueType()),
                             CC, dl, DAG);
        AArch64CC = changeIntCCToAArch64CC(CC);
      }
    }

    if (!Cmp && (RHSC->isNullValue() || RHSC->isOne())) {
      if ((Cmp = emitConjunction(DAG, LHS, AArch64CC))) {
        if ((CC == ISD::SETNE) ^ RHSC->isNullValue())
          AArch64CC = AArch64CC::getInvertedCondCode(AArch64CC);
      }
    }
  }

  if (!Cmp) {
    Cmp = emitComparison(LHS, RHS, CC, dl, DAG);
    AArch64CC = changeIntCCToAArch64CC(CC);
  }
  AArch64cc = DAG.getConstant(AArch64CC, dl, MVT_CC);
  return Cmp;
}

static std::pair<SDValue, SDValue>
getAArch64XALUOOp(AArch64CC::CondCode &CC, SDValue Op, SelectionDAG &DAG) {
  assert((Op.getValueType() == MVT::i32 || Op.getValueType() == MVT::i64) &&
         "Unsupported value type");
  SDValue Value, Overflow;
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  unsigned Opc = 0;
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unknown overflow instruction!");
  case ISD::SADDO:
    Opc = AArch64ISD::ADDS;
    CC = AArch64CC::VS;
    break;
  case ISD::UADDO:
    Opc = AArch64ISD::ADDS;
    CC = AArch64CC::HS;
    break;
  case ISD::SSUBO:
    Opc = AArch64ISD::SUBS;
    CC = AArch64CC::VS;
    break;
  case ISD::USUBO:
    Opc = AArch64ISD::SUBS;
    CC = AArch64CC::LO;
    break;
  // Multiply needs a little bit extra work.
  case ISD::SMULO:
  case ISD::UMULO: {
    CC = AArch64CC::NE;
    bool IsSigned = Op.getOpcode() == ISD::SMULO;
    if (Op.getValueType() == MVT::i32) {
      unsigned ExtendOpc = IsSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
      // For a 32 bit multiply with overflow check we want the instruction
      // selector to generate a widening multiply (SMADDL/UMADDL). For that we
      // need to generate the following pattern:
      // (i64 add 0, (i64 mul (i64 sext|zext i32 %a), (i64 sext|zext i32 %b))
      LHS = DAG.getNode(ExtendOpc, DL, MVT::i64, LHS);
      RHS = DAG.getNode(ExtendOpc, DL, MVT::i64, RHS);
      SDValue Mul = DAG.getNode(ISD::MUL, DL, MVT::i64, LHS, RHS);
      SDValue Add = DAG.getNode(ISD::ADD, DL, MVT::i64, Mul,
                                DAG.getConstant(0, DL, MVT::i64));
      // On AArch64 the upper 32 bits are always zero extended for a 32 bit
      // operation. We need to clear out the upper 32 bits, because we used a
      // widening multiply that wrote all 64 bits. In the end this should be a
      // noop.
      Value = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Add);
      if (IsSigned) {
        // The signed overflow check requires more than just a simple check for
        // any bit set in the upper 32 bits of the result. These bits could be
        // just the sign bits of a negative number. To perform the overflow
        // check we have to arithmetic shift right the 32nd bit of the result by
        // 31 bits. Then we compare the result to the upper 32 bits.
        SDValue UpperBits = DAG.getNode(ISD::SRL, DL, MVT::i64, Add,
                                        DAG.getConstant(32, DL, MVT::i64));
        UpperBits = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, UpperBits);
        SDValue LowerBits = DAG.getNode(ISD::SRA, DL, MVT::i32, Value,
                                        DAG.getConstant(31, DL, MVT::i64));
        // It is important that LowerBits is last, otherwise the arithmetic
        // shift will not be folded into the compare (SUBS).
        SDVTList VTs = DAG.getVTList(MVT::i32, MVT::i32);
        Overflow = DAG.getNode(AArch64ISD::SUBS, DL, VTs, UpperBits, LowerBits)
                       .getValue(1);
      } else {
        // The overflow check for unsigned multiply is easy. We only need to
        // check if any of the upper 32 bits are set. This can be done with a
        // CMP (shifted register). For that we need to generate the following
        // pattern:
        // (i64 AArch64ISD::SUBS i64 0, (i64 srl i64 %Mul, i64 32)
        SDValue UpperBits = DAG.getNode(ISD::SRL, DL, MVT::i64, Mul,
                                        DAG.getConstant(32, DL, MVT::i64));
        SDVTList VTs = DAG.getVTList(MVT::i64, MVT::i32);
        Overflow =
            DAG.getNode(AArch64ISD::SUBS, DL, VTs,
                        DAG.getConstant(0, DL, MVT::i64),
                        UpperBits).getValue(1);
      }
      break;
    }
    assert(Op.getValueType() == MVT::i64 && "Expected an i64 value type");
    // For the 64 bit multiply
    Value = DAG.getNode(ISD::MUL, DL, MVT::i64, LHS, RHS);
    if (IsSigned) {
      SDValue UpperBits = DAG.getNode(ISD::MULHS, DL, MVT::i64, LHS, RHS);
      SDValue LowerBits = DAG.getNode(ISD::SRA, DL, MVT::i64, Value,
                                      DAG.getConstant(63, DL, MVT::i64));
      // It is important that LowerBits is last, otherwise the arithmetic
      // shift will not be folded into the compare (SUBS).
      SDVTList VTs = DAG.getVTList(MVT::i64, MVT::i32);
      Overflow = DAG.getNode(AArch64ISD::SUBS, DL, VTs, UpperBits, LowerBits)
                     .getValue(1);
    } else {
      SDValue UpperBits = DAG.getNode(ISD::MULHU, DL, MVT::i64, LHS, RHS);
      SDVTList VTs = DAG.getVTList(MVT::i64, MVT::i32);
      Overflow =
          DAG.getNode(AArch64ISD::SUBS, DL, VTs,
                      DAG.getConstant(0, DL, MVT::i64),
                      UpperBits).getValue(1);
    }
    break;
  }
  } // switch (...)

  if (Opc) {
    SDVTList VTs = DAG.getVTList(Op->getValueType(0), MVT::i32);

    // Emit the AArch64 operation with overflow check.
    Value = DAG.getNode(Opc, DL, VTs, LHS, RHS);
    Overflow = Value.getValue(1);
  }
  return std::make_pair(Value, Overflow);
}

SDValue AArch64TargetLowering::LowerF128Call(SDValue Op, SelectionDAG &DAG,
                                             RTLIB::Libcall Call) const {
  SmallVector<SDValue, 2> Ops(Op->op_begin(), Op->op_end());
  return makeLibCall(DAG, Call, MVT::f128, Ops, false, SDLoc(Op)).first;
}

// Returns true if the given Op is the overflow flag result of an overflow
// intrinsic operation.
static bool isOverflowIntrOpRes(SDValue Op) {
  unsigned Opc = Op.getOpcode();
  return (Op.getResNo() == 1 &&
          (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
           Opc == ISD::USUBO || Opc == ISD::SMULO || Opc == ISD::UMULO));
}

static SDValue LowerXOR(SDValue Op, SelectionDAG &DAG) {
  SDValue Sel = Op.getOperand(0);
  SDValue Other = Op.getOperand(1);
  SDLoc dl(Sel);

  // If the operand is an overflow checking operation, invert the condition
  // code and kill the Not operation. I.e., transform:
  // (xor (overflow_op_bool, 1))
  //   -->
  // (csel 1, 0, invert(cc), overflow_op_bool)
  // ... which later gets transformed to just a cset instruction with an
  // inverted condition code, rather than a cset + eor sequence.
  if (isOneConstant(Other) && isOverflowIntrOpRes(Sel)) {
    // Only lower legal XALUO ops.
    if (!DAG.getTargetLoweringInfo().isTypeLegal(Sel->getValueType(0)))
      return SDValue();

    SDValue TVal = DAG.getConstant(1, dl, MVT::i32);
    SDValue FVal = DAG.getConstant(0, dl, MVT::i32);
    AArch64CC::CondCode CC;
    SDValue Value, Overflow;
    std::tie(Value, Overflow) = getAArch64XALUOOp(CC, Sel.getValue(0), DAG);
    SDValue CCVal = DAG.getConstant(getInvertedCondCode(CC), dl, MVT::i32);
    return DAG.getNode(AArch64ISD::CSEL, dl, Op.getValueType(), TVal, FVal,
                       CCVal, Overflow);
  }
  // If neither operand is a SELECT_CC, give up.
  if (Sel.getOpcode() != ISD::SELECT_CC)
    std::swap(Sel, Other);
  if (Sel.getOpcode() != ISD::SELECT_CC)
    return Op;

  // The folding we want to perform is:
  // (xor x, (select_cc a, b, cc, 0, -1) )
  //   -->
  // (csel x, (xor x, -1), cc ...)
  //
  // The latter will get matched to a CSINV instruction.

  ISD::CondCode CC = cast<CondCodeSDNode>(Sel.getOperand(4))->get();
  SDValue LHS = Sel.getOperand(0);
  SDValue RHS = Sel.getOperand(1);
  SDValue TVal = Sel.getOperand(2);
  SDValue FVal = Sel.getOperand(3);

  // FIXME: This could be generalized to non-integer comparisons.
  if (LHS.getValueType() != MVT::i32 && LHS.getValueType() != MVT::i64)
    return Op;

  ConstantSDNode *CFVal = dyn_cast<ConstantSDNode>(FVal);
  ConstantSDNode *CTVal = dyn_cast<ConstantSDNode>(TVal);

  // The values aren't constants, this isn't the pattern we're looking for.
  if (!CFVal || !CTVal)
    return Op;

  // We can commute the SELECT_CC by inverting the condition.  This
  // might be needed to make this fit into a CSINV pattern.
  if (CTVal->isAllOnesValue() && CFVal->isNullValue()) {
    std::swap(TVal, FVal);
    std::swap(CTVal, CFVal);
    CC = ISD::getSetCCInverse(CC, true);
  }

  // If the constants line up, perform the transform!
  if (CTVal->isNullValue() && CFVal->isAllOnesValue()) {
    SDValue CCVal;
    SDValue Cmp = getAArch64Cmp(LHS, RHS, CC, CCVal, DAG, dl);

    FVal = Other;
    TVal = DAG.getNode(ISD::XOR, dl, Other.getValueType(), Other,
                       DAG.getConstant(-1ULL, dl, Other.getValueType()));

    return DAG.getNode(AArch64ISD::CSEL, dl, Sel.getValueType(), FVal, TVal,
                       CCVal, Cmp);
  }

  return Op;
}

static SDValue LowerADDC_ADDE_SUBC_SUBE(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();

  // Let legalize expand this if it isn't a legal type yet.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  SDVTList VTs = DAG.getVTList(VT, MVT::i32);

  unsigned Opc;
  bool ExtraOp = false;
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Invalid code");
  case ISD::ADDC:
    Opc = AArch64ISD::ADDS;
    break;
  case ISD::SUBC:
    Opc = AArch64ISD::SUBS;
    break;
  case ISD::ADDE:
    Opc = AArch64ISD::ADCS;
    ExtraOp = true;
    break;
  case ISD::SUBE:
    Opc = AArch64ISD::SBCS;
    ExtraOp = true;
    break;
  }

  if (!ExtraOp)
    return DAG.getNode(Opc, SDLoc(Op), VTs, Op.getOperand(0), Op.getOperand(1));
  return DAG.getNode(Opc, SDLoc(Op), VTs, Op.getOperand(0), Op.getOperand(1),
                     Op.getOperand(2));
}

static SDValue LowerXALUO(SDValue Op, SelectionDAG &DAG) {
  // Let legalize expand this if it isn't a legal type yet.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(Op.getValueType()))
    return SDValue();

  SDLoc dl(Op);
  AArch64CC::CondCode CC;
  // The actual operation that sets the overflow or carry flag.
  SDValue Value, Overflow;
  std::tie(Value, Overflow) = getAArch64XALUOOp(CC, Op, DAG);

  // We use 0 and 1 as false and true values.
  SDValue TVal = DAG.getConstant(1, dl, MVT::i32);
  SDValue FVal = DAG.getConstant(0, dl, MVT::i32);

  // We use an inverted condition, because the conditional select is inverted
  // too. This will allow it to be selected to a single instruction:
  // CSINC Wd, WZR, WZR, invert(cond).
  SDValue CCVal = DAG.getConstant(getInvertedCondCode(CC), dl, MVT::i32);
  Overflow = DAG.getNode(AArch64ISD::CSEL, dl, MVT::i32, FVal, TVal,
                         CCVal, Overflow);

  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::i32);
  return DAG.getNode(ISD::MERGE_VALUES, dl, VTs, Value, Overflow);
}

// Prefetch operands are:
// 1: Address to prefetch
// 2: bool isWrite
// 3: int locality (0 = no locality ... 3 = extreme locality)
// 4: bool isDataCache
static SDValue LowerPREFETCH(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  unsigned IsWrite = cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue();
  unsigned Locality = cast<ConstantSDNode>(Op.getOperand(3))->getZExtValue();
  unsigned IsData = cast<ConstantSDNode>(Op.getOperand(4))->getZExtValue();

  bool IsStream = !Locality;
  // When the locality number is set
  if (Locality) {
    // The front-end should have filtered out the out-of-range values
    assert(Locality <= 3 && "Prefetch locality out-of-range");
    // The locality degree is the opposite of the cache speed.
    // Put the number the other way around.
    // The encoding starts at 0 for level 1
    Locality = 3 - Locality;
  }

  // built the mask value encoding the expected behavior.
  unsigned PrfOp = (IsWrite << 4) |     // Load/Store bit
                   (!IsData << 3) |     // IsDataCache bit
                   (Locality << 1) |    // Cache level bits
                   (unsigned)IsStream;  // Stream bit
  return DAG.getNode(AArch64ISD::PREFETCH, DL, MVT::Other, Op.getOperand(0),
                     DAG.getConstant(PrfOp, DL, MVT::i32), Op.getOperand(1));
}

SDValue AArch64TargetLowering::LowerFP_EXTEND(SDValue Op,
                                              SelectionDAG &DAG) const {
  assert(Op.getValueType() == MVT::f128 && "Unexpected lowering");

  RTLIB::Libcall LC;
  LC = RTLIB::getFPEXT(Op.getOperand(0).getValueType(), Op.getValueType());

  return LowerF128Call(Op, DAG, LC);
}

SDValue AArch64TargetLowering::LowerFP_ROUND(SDValue Op,
                                             SelectionDAG &DAG) const {
  if (Op.getOperand(0).getValueType() != MVT::f128) {
    // It's legal except when f128 is involved
    return Op;
  }

  RTLIB::Libcall LC;
  LC = RTLIB::getFPROUND(Op.getOperand(0).getValueType(), Op.getValueType());

  // FP_ROUND node has a second operand indicating whether it is known to be
  // precise. That doesn't take part in the LibCall so we can't directly use
  // LowerF128Call.
  SDValue SrcVal = Op.getOperand(0);
  return makeLibCall(DAG, LC, Op.getValueType(), SrcVal, /*isSigned*/ false,
                     SDLoc(Op)).first;
}

static SDValue LowerVectorFP_TO_INT(SDValue Op, SelectionDAG &DAG) {
  // Warning: We maintain cost tables in AArch64TargetTransformInfo.cpp.
  // Any additional optimization in this function should be recorded
  // in the cost tables.
  EVT InVT = Op.getOperand(0).getValueType();
  EVT VT = Op.getValueType();
  unsigned NumElts = InVT.getVectorNumElements();

  // f16 vectors are promoted to f32 before a conversion.
  if (InVT.getVectorElementType() == MVT::f16) {
    MVT NewVT = MVT::getVectorVT(MVT::f32, NumElts);
    SDLoc dl(Op);
    return DAG.getNode(
        Op.getOpcode(), dl, Op.getValueType(),
        DAG.getNode(ISD::FP_EXTEND, dl, NewVT, Op.getOperand(0)));
  }

  if (VT.getSizeInBits() < InVT.getSizeInBits()) {
    SDLoc dl(Op);
    SDValue Cv =
        DAG.getNode(Op.getOpcode(), dl, InVT.changeVectorElementTypeToInteger(),
                    Op.getOperand(0));
    return DAG.getNode(ISD::TRUNCATE, dl, VT, Cv);
  }

  if (VT.getSizeInBits() > InVT.getSizeInBits()) {
    SDLoc dl(Op);
    MVT ExtVT =
        MVT::getVectorVT(MVT::getFloatingPointVT(VT.getScalarSizeInBits()),
                         VT.getVectorNumElements());
    SDValue Ext = DAG.getNode(ISD::FP_EXTEND, dl, ExtVT, Op.getOperand(0));
    return DAG.getNode(Op.getOpcode(), dl, VT, Ext);
  }

  // Type changing conversions are illegal.
  return Op;
}

SDValue AArch64TargetLowering::LowerFP_TO_INT(SDValue Op,
                                              SelectionDAG &DAG) const {
  if (Op.getOperand(0).getValueType().isVector())
    return LowerVectorFP_TO_INT(Op, DAG);

  // f16 conversions are promoted to f32 when full fp16 is not supported.
  if (Op.getOperand(0).getValueType() == MVT::f16 &&
      !Subtarget->hasFullFP16()) {
    SDLoc dl(Op);
    return DAG.getNode(
        Op.getOpcode(), dl, Op.getValueType(),
        DAG.getNode(ISD::FP_EXTEND, dl, MVT::f32, Op.getOperand(0)));
  }

  if (Op.getOperand(0).getValueType() != MVT::f128) {
    // It's legal except when f128 is involved
    return Op;
  }

  RTLIB::Libcall LC;
  if (Op.getOpcode() == ISD::FP_TO_SINT)
    LC = RTLIB::getFPTOSINT(Op.getOperand(0).getValueType(), Op.getValueType());
  else
    LC = RTLIB::getFPTOUINT(Op.getOperand(0).getValueType(), Op.getValueType());

  SmallVector<SDValue, 2> Ops(Op->op_begin(), Op->op_end());
  return makeLibCall(DAG, LC, Op.getValueType(), Ops, false, SDLoc(Op)).first;
}

static SDValue LowerVectorINT_TO_FP(SDValue Op, SelectionDAG &DAG) {
  // Warning: We maintain cost tables in AArch64TargetTransformInfo.cpp.
  // Any additional optimization in this function should be recorded
  // in the cost tables.
  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  SDValue In = Op.getOperand(0);
  EVT InVT = In.getValueType();

  if (VT.getSizeInBits() < InVT.getSizeInBits()) {
    MVT CastVT =
        MVT::getVectorVT(MVT::getFloatingPointVT(InVT.getScalarSizeInBits()),
                         InVT.getVectorNumElements());
    In = DAG.getNode(Op.getOpcode(), dl, CastVT, In);
    return DAG.getNode(ISD::FP_ROUND, dl, VT, In, DAG.getIntPtrConstant(0, dl));
  }

  if (VT.getSizeInBits() > InVT.getSizeInBits()) {
    unsigned CastOpc =
        Op.getOpcode() == ISD::SINT_TO_FP ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    EVT CastVT = VT.changeVectorElementTypeToInteger();
    In = DAG.getNode(CastOpc, dl, CastVT, In);
    return DAG.getNode(Op.getOpcode(), dl, VT, In);
  }

  return Op;
}

SDValue AArch64TargetLowering::LowerINT_TO_FP(SDValue Op,
                                            SelectionDAG &DAG) const {
  if (Op.getValueType().isVector())
    return LowerVectorINT_TO_FP(Op, DAG);

  // f16 conversions are promoted to f32 when full fp16 is not supported.
  if (Op.getValueType() == MVT::f16 &&
      !Subtarget->hasFullFP16()) {
    SDLoc dl(Op);
    return DAG.getNode(
        ISD::FP_ROUND, dl, MVT::f16,
        DAG.getNode(Op.getOpcode(), dl, MVT::f32, Op.getOperand(0)),
        DAG.getIntPtrConstant(0, dl));
  }

  // i128 conversions are libcalls.
  if (Op.getOperand(0).getValueType() == MVT::i128)
    return SDValue();

  // Other conversions are legal, unless it's to the completely software-based
  // fp128.
  if (Op.getValueType() != MVT::f128)
    return Op;

  RTLIB::Libcall LC;
  if (Op.getOpcode() == ISD::SINT_TO_FP)
    LC = RTLIB::getSINTTOFP(Op.getOperand(0).getValueType(), Op.getValueType());
  else
    LC = RTLIB::getUINTTOFP(Op.getOperand(0).getValueType(), Op.getValueType());

  return LowerF128Call(Op, DAG, LC);
}

SDValue AArch64TargetLowering::LowerFSINCOS(SDValue Op,
                                            SelectionDAG &DAG) const {
  // For iOS, we want to call an alternative entry point: __sincos_stret,
  // which returns the values in two S / D registers.
  SDLoc dl(Op);
  SDValue Arg = Op.getOperand(0);
  EVT ArgVT = Arg.getValueType();
  Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());

  ArgListTy Args;
  ArgListEntry Entry;

  Entry.Node = Arg;
  Entry.Ty = ArgTy;
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  RTLIB::Libcall LC = ArgVT == MVT::f64 ? RTLIB::SINCOS_STRET_F64
                                        : RTLIB::SINCOS_STRET_F32;
  const char *LibcallName = getLibcallName(LC);
  SDValue Callee =
      DAG.getExternalSymbol(LibcallName, getPointerTy(DAG.getDataLayout()));

  StructType *RetTy = StructType::get(ArgTy, ArgTy);
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(DAG.getEntryNode())
      .setLibCallee(CallingConv::Fast, RetTy, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);
  return CallResult.first;
}

static SDValue LowerBITCAST(SDValue Op, SelectionDAG &DAG) {
  if (Op.getValueType() != MVT::f16)
    return SDValue();

  assert(Op.getOperand(0).getValueType() == MVT::i16);
  SDLoc DL(Op);

  Op = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Op.getOperand(0));
  Op = DAG.getNode(ISD::BITCAST, DL, MVT::f32, Op);
  return SDValue(
      DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, DL, MVT::f16, Op,
                         DAG.getTargetConstant(AArch64::hsub, DL, MVT::i32)),
      0);
}

static EVT getExtensionTo64Bits(const EVT &OrigVT) {
  if (OrigVT.getSizeInBits() >= 64)
    return OrigVT;

  assert(OrigVT.isSimple() && "Expecting a simple value type");

  MVT::SimpleValueType OrigSimpleTy = OrigVT.getSimpleVT().SimpleTy;
  switch (OrigSimpleTy) {
  default: llvm_unreachable("Unexpected Vector Type");
  case MVT::v2i8:
  case MVT::v2i16:
     return MVT::v2i32;
  case MVT::v4i8:
    return  MVT::v4i16;
  }
}

static SDValue addRequiredExtensionForVectorMULL(SDValue N, SelectionDAG &DAG,
                                                 const EVT &OrigTy,
                                                 const EVT &ExtTy,
                                                 unsigned ExtOpcode) {
  // The vector originally had a size of OrigTy. It was then extended to ExtTy.
  // We expect the ExtTy to be 128-bits total. If the OrigTy is less than
  // 64-bits we need to insert a new extension so that it will be 64-bits.
  assert(ExtTy.is128BitVector() && "Unexpected extension size");
  if (OrigTy.getSizeInBits() >= 64)
    return N;

  // Must extend size to at least 64 bits to be used as an operand for VMULL.
  EVT NewVT = getExtensionTo64Bits(OrigTy);

  return DAG.getNode(ExtOpcode, SDLoc(N), NewVT, N);
}

static bool isExtendedBUILD_VECTOR(SDNode *N, SelectionDAG &DAG,
                                   bool isSigned) {
  EVT VT = N->getValueType(0);

  if (N->getOpcode() != ISD::BUILD_VECTOR)
    return false;

  for (const SDValue &Elt : N->op_values()) {
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Elt)) {
      unsigned EltSize = VT.getScalarSizeInBits();
      unsigned HalfSize = EltSize / 2;
      if (isSigned) {
        if (!isIntN(HalfSize, C->getSExtValue()))
          return false;
      } else {
        if (!isUIntN(HalfSize, C->getZExtValue()))
          return false;
      }
      continue;
    }
    return false;
  }

  return true;
}

static SDValue skipExtensionForVectorMULL(SDNode *N, SelectionDAG &DAG) {
  if (N->getOpcode() == ISD::SIGN_EXTEND || N->getOpcode() == ISD::ZERO_EXTEND)
    return addRequiredExtensionForVectorMULL(N->getOperand(0), DAG,
                                             N->getOperand(0)->getValueType(0),
                                             N->getValueType(0),
                                             N->getOpcode());

  assert(N->getOpcode() == ISD::BUILD_VECTOR && "expected BUILD_VECTOR");
  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  unsigned EltSize = VT.getScalarSizeInBits() / 2;
  unsigned NumElts = VT.getVectorNumElements();
  MVT TruncVT = MVT::getIntegerVT(EltSize);
  SmallVector<SDValue, 8> Ops;
  for (unsigned i = 0; i != NumElts; ++i) {
    ConstantSDNode *C = cast<ConstantSDNode>(N->getOperand(i));
    const APInt &CInt = C->getAPIntValue();
    // Element types smaller than 32 bits are not legal, so use i32 elements.
    // The values are implicitly truncated so sext vs. zext doesn't matter.
    Ops.push_back(DAG.getConstant(CInt.zextOrTrunc(32), dl, MVT::i32));
  }
  return DAG.getBuildVector(MVT::getVectorVT(TruncVT, NumElts), dl, Ops);
}

static bool isSignExtended(SDNode *N, SelectionDAG &DAG) {
  return N->getOpcode() == ISD::SIGN_EXTEND ||
         isExtendedBUILD_VECTOR(N, DAG, true);
}

static bool isZeroExtended(SDNode *N, SelectionDAG &DAG) {
  return N->getOpcode() == ISD::ZERO_EXTEND ||
         isExtendedBUILD_VECTOR(N, DAG, false);
}

static bool isAddSubSExt(SDNode *N, SelectionDAG &DAG) {
  unsigned Opcode = N->getOpcode();
  if (Opcode == ISD::ADD || Opcode == ISD::SUB) {
    SDNode *N0 = N->getOperand(0).getNode();
    SDNode *N1 = N->getOperand(1).getNode();
    return N0->hasOneUse() && N1->hasOneUse() &&
      isSignExtended(N0, DAG) && isSignExtended(N1, DAG);
  }
  return false;
}

static bool isAddSubZExt(SDNode *N, SelectionDAG &DAG) {
  unsigned Opcode = N->getOpcode();
  if (Opcode == ISD::ADD || Opcode == ISD::SUB) {
    SDNode *N0 = N->getOperand(0).getNode();
    SDNode *N1 = N->getOperand(1).getNode();
    return N0->hasOneUse() && N1->hasOneUse() &&
      isZeroExtended(N0, DAG) && isZeroExtended(N1, DAG);
  }
  return false;
}

SDValue AArch64TargetLowering::LowerFLT_ROUNDS_(SDValue Op,
                                                SelectionDAG &DAG) const {
  // The rounding mode is in bits 23:22 of the FPSCR.
  // The ARM rounding mode value to FLT_ROUNDS mapping is 0->1, 1->2, 2->3, 3->0
  // The formula we use to implement this is (((FPSCR + 1 << 22) >> 22) & 3)
  // so that the shift + and get folded into a bitfield extract.
  SDLoc dl(Op);

  SDValue FPCR_64 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::i64,
                                DAG.getConstant(Intrinsic::aarch64_get_fpcr, dl,
                                                MVT::i64));
  SDValue FPCR_32 = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, FPCR_64);
  SDValue FltRounds = DAG.getNode(ISD::ADD, dl, MVT::i32, FPCR_32,
                                  DAG.getConstant(1U << 22, dl, MVT::i32));
  SDValue RMODE = DAG.getNode(ISD::SRL, dl, MVT::i32, FltRounds,
                              DAG.getConstant(22, dl, MVT::i32));
  return DAG.getNode(ISD::AND, dl, MVT::i32, RMODE,
                     DAG.getConstant(3, dl, MVT::i32));
}

static SDValue LowerMUL(SDValue Op, SelectionDAG &DAG) {
  // Multiplications are only custom-lowered for 128-bit vectors so that
  // VMULL can be detected.  Otherwise v2i64 multiplications are not legal.
  EVT VT = Op.getValueType();
  assert(VT.is128BitVector() && VT.isInteger() &&
         "unexpected type for custom-lowering ISD::MUL");
  SDNode *N0 = Op.getOperand(0).getNode();
  SDNode *N1 = Op.getOperand(1).getNode();
  unsigned NewOpc = 0;
  bool isMLA = false;
  bool isN0SExt = isSignExtended(N0, DAG);
  bool isN1SExt = isSignExtended(N1, DAG);
  if (isN0SExt && isN1SExt)
    NewOpc = AArch64ISD::SMULL;
  else {
    bool isN0ZExt = isZeroExtended(N0, DAG);
    bool isN1ZExt = isZeroExtended(N1, DAG);
    if (isN0ZExt && isN1ZExt)
      NewOpc = AArch64ISD::UMULL;
    else if (isN1SExt || isN1ZExt) {
      // Look for (s/zext A + s/zext B) * (s/zext C). We want to turn these
      // into (s/zext A * s/zext C) + (s/zext B * s/zext C)
      if (isN1SExt && isAddSubSExt(N0, DAG)) {
        NewOpc = AArch64ISD::SMULL;
        isMLA = true;
      } else if (isN1ZExt && isAddSubZExt(N0, DAG)) {
        NewOpc =  AArch64ISD::UMULL;
        isMLA = true;
      } else if (isN0ZExt && isAddSubZExt(N1, DAG)) {
        std::swap(N0, N1);
        NewOpc =  AArch64ISD::UMULL;
        isMLA = true;
      }
    }

    if (!NewOpc) {
      if (VT == MVT::v2i64)
        // Fall through to expand this.  It is not legal.
        return SDValue();
      else
        // Other vector multiplications are legal.
        return Op;
    }
  }

  // Legalize to a S/UMULL instruction
  SDLoc DL(Op);
  SDValue Op0;
  SDValue Op1 = skipExtensionForVectorMULL(N1, DAG);
  if (!isMLA) {
    Op0 = skipExtensionForVectorMULL(N0, DAG);
    assert(Op0.getValueType().is64BitVector() &&
           Op1.getValueType().is64BitVector() &&
           "unexpected types for extended operands to VMULL");
    return DAG.getNode(NewOpc, DL, VT, Op0, Op1);
  }
  // Optimizing (zext A + zext B) * C, to (S/UMULL A, C) + (S/UMULL B, C) during
  // isel lowering to take advantage of no-stall back to back s/umul + s/umla.
  // This is true for CPUs with accumulate forwarding such as Cortex-A53/A57
  SDValue N00 = skipExtensionForVectorMULL(N0->getOperand(0).getNode(), DAG);
  SDValue N01 = skipExtensionForVectorMULL(N0->getOperand(1).getNode(), DAG);
  EVT Op1VT = Op1.getValueType();
  return DAG.getNode(N0->getOpcode(), DL, VT,
                     DAG.getNode(NewOpc, DL, VT,
                               DAG.getNode(ISD::BITCAST, DL, Op1VT, N00), Op1),
                     DAG.getNode(NewOpc, DL, VT,
                               DAG.getNode(ISD::BITCAST, DL, Op1VT, N01), Op1));
}

SDValue AArch64TargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                     SelectionDAG &DAG) const {
  unsigned IntNo = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  SDLoc dl(Op);
  switch (IntNo) {
  default: return SDValue();    // Don't custom lower most intrinsics.
  case Intrinsic::thread_pointer: {
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    return DAG.getNode(AArch64ISD::THREAD_POINTER, dl, PtrVT);
  }
  case Intrinsic::aarch64_neon_abs: {
    EVT Ty = Op.getValueType();
    if (Ty == MVT::i64) {
      SDValue Result = DAG.getNode(ISD::BITCAST, dl, MVT::v1i64,
                                   Op.getOperand(1));
      Result = DAG.getNode(ISD::ABS, dl, MVT::v1i64, Result);
      return DAG.getNode(ISD::BITCAST, dl, MVT::i64, Result);
    } else if (Ty.isVector() && Ty.isInteger() && isTypeLegal(Ty)) {
      return DAG.getNode(ISD::ABS, dl, Ty, Op.getOperand(1));
    } else {
      report_fatal_error("Unexpected type for AArch64 NEON intrinic");
    }
  }
  case Intrinsic::aarch64_neon_smax:
    return DAG.getNode(ISD::SMAX, dl, Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::aarch64_neon_umax:
    return DAG.getNode(ISD::UMAX, dl, Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::aarch64_neon_smin:
    return DAG.getNode(ISD::SMIN, dl, Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::aarch64_neon_umin:
    return DAG.getNode(ISD::UMIN, dl, Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));

  case Intrinsic::localaddress: {
    // Returns one of the stack, base, or frame pointer registers, depending on
    // which is used to reference local variables.
    MachineFunction &MF = DAG.getMachineFunction();
    const AArch64RegisterInfo *RegInfo = Subtarget->getRegisterInfo();
    unsigned Reg;
    if (RegInfo->hasBasePointer(MF))
      Reg = RegInfo->getBaseRegister();
    else // This function handles the SP or FP case.
      Reg = RegInfo->getFrameRegister(MF);
    return DAG.getCopyFromReg(DAG.getEntryNode(), dl, Reg,
                              Op.getSimpleValueType());
  }

  case Intrinsic::eh_recoverfp: {
    // FIXME: This needs to be implemented to correctly handle highly aligned
    // stack objects. For now we simply return the incoming FP. Refer D53541
    // for more details.
    SDValue FnOp = Op.getOperand(1);
    SDValue IncomingFPOp = Op.getOperand(2);
    GlobalAddressSDNode *GSD = dyn_cast<GlobalAddressSDNode>(FnOp);
    auto *Fn = dyn_cast_or_null<Function>(GSD ? GSD->getGlobal() : nullptr);
    if (!Fn)
      report_fatal_error(
          "llvm.eh.recoverfp must take a function as the first argument");
    return IncomingFPOp;
  }
  }
}

// Custom lower trunc store for v4i8 vectors, since it is promoted to v4i16.
static SDValue LowerTruncateVectorStore(SDLoc DL, StoreSDNode *ST,
                                        EVT VT, EVT MemVT,
                                        SelectionDAG &DAG) {
  assert(VT.isVector() && "VT should be a vector type");
  assert(MemVT == MVT::v4i8 && VT == MVT::v4i16);

  SDValue Value = ST->getValue();

  // It first extend the promoted v4i16 to v8i16, truncate to v8i8, and extract
  // the word lane which represent the v4i8 subvector.  It optimizes the store
  // to:
  //
  //   xtn  v0.8b, v0.8h
  //   str  s0, [x0]

  SDValue Undef = DAG.getUNDEF(MVT::i16);
  SDValue UndefVec = DAG.getBuildVector(MVT::v4i16, DL,
                                        {Undef, Undef, Undef, Undef});

  SDValue TruncExt = DAG.getNode(ISD::CONCAT_VECTORS, DL, MVT::v8i16,
                                 Value, UndefVec);
  SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::v8i8, TruncExt);

  Trunc = DAG.getNode(ISD::BITCAST, DL, MVT::v2i32, Trunc);
  SDValue ExtractTrunc = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32,
                                     Trunc, DAG.getConstant(0, DL, MVT::i64));

  return DAG.getStore(ST->getChain(), DL, ExtractTrunc,
                      ST->getBasePtr(), ST->getMemOperand());
}

// Custom lowering for any store, vector or scalar and/or default or with
// a truncate operations.  Currently only custom lower truncate operation
// from vector v4i16 to v4i8.
SDValue AArch64TargetLowering::LowerSTORE(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDLoc Dl(Op);
  StoreSDNode *StoreNode = cast<StoreSDNode>(Op);
  assert (StoreNode && "Can only custom lower store nodes");

  SDValue Value = StoreNode->getValue();

  EVT VT = Value.getValueType();
  EVT MemVT = StoreNode->getMemoryVT();

  assert (VT.isVector() && "Can only custom lower vector store types");

  unsigned AS = StoreNode->getAddressSpace();
  unsigned Align = StoreNode->getAlignment();
  if (Align < MemVT.getStoreSize() &&
      !allowsMisalignedMemoryAccesses(MemVT, AS, Align, nullptr)) {
    return scalarizeVectorStore(StoreNode, DAG);
  }

  if (StoreNode->isTruncatingStore()) {
    return LowerTruncateVectorStore(Dl, StoreNode, VT, MemVT, DAG);
  }

  return SDValue();
}

SDValue AArch64TargetLowering::LowerOperation(SDValue Op,
                                              SelectionDAG &DAG) const {
  LLVM_DEBUG(dbgs() << "Custom lowering: ");
  LLVM_DEBUG(Op.dump());

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("unimplemented operand");
    return SDValue();
  case ISD::BITCAST:
    return LowerBITCAST(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SELECT:
    return LowerSELECT(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::BR_JT:
    return LowerBR_JT(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::VACOPY:
    return LowerVACOPY(Op, DAG);
  case ISD::VAARG:
    return LowerVAARG(Op, DAG);
  case ISD::ADDC:
  case ISD::ADDE:
  case ISD::SUBC:
  case ISD::SUBE:
    return LowerADDC_ADDE_SUBC_SUBE(Op, DAG);
  case ISD::SADDO:
  case ISD::UADDO:
  case ISD::SSUBO:
  case ISD::USUBO:
  case ISD::SMULO:
  case ISD::UMULO:
    return LowerXALUO(Op, DAG);
  case ISD::FADD:
    return LowerF128Call(Op, DAG, RTLIB::ADD_F128);
  case ISD::FSUB:
    return LowerF128Call(Op, DAG, RTLIB::SUB_F128);
  case ISD::FMUL:
    return LowerF128Call(Op, DAG, RTLIB::MUL_F128);
  case ISD::FDIV:
    return LowerF128Call(Op, DAG, RTLIB::DIV_F128);
  case ISD::FP_ROUND:
    return LowerFP_ROUND(Op, DAG);
  case ISD::FP_EXTEND:
    return LowerFP_EXTEND(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  case ISD::SPONENTRY:
    return LowerSPONENTRY(Op, DAG);
  case ISD::RETURNADDR:
    return LowerRETURNADDR(Op, DAG);
  case ISD::ADDROFRETURNADDR:
    return LowerADDROFRETURNADDR(Op, DAG);
  case ISD::INSERT_VECTOR_ELT:
    return LowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT:
    return LowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::BUILD_VECTOR:
    return LowerBUILD_VECTOR(Op, DAG);
  case ISD::VECTOR_SHUFFLE:
    return LowerVECTOR_SHUFFLE(Op, DAG);
  case ISD::EXTRACT_SUBVECTOR:
    return LowerEXTRACT_SUBVECTOR(Op, DAG);
  case ISD::SRA:
  case ISD::SRL:
  case ISD::SHL:
    return LowerVectorSRA_SRL_SHL(Op, DAG);
  case ISD::SHL_PARTS:
    return LowerShiftLeftParts(Op, DAG);
  case ISD::SRL_PARTS:
  case ISD::SRA_PARTS:
    return LowerShiftRightParts(Op, DAG);
  case ISD::CTPOP:
    return LowerCTPOP(Op, DAG);
  case ISD::FCOPYSIGN:
    return LowerFCOPYSIGN(Op, DAG);
  case ISD::AND:
    return LowerVectorAND(Op, DAG);
  case ISD::OR:
    return LowerVectorOR(Op, DAG);
  case ISD::XOR:
    return LowerXOR(Op, DAG);
  case ISD::PREFETCH:
    return LowerPREFETCH(Op, DAG);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    return LowerINT_TO_FP(Op, DAG);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return LowerFP_TO_INT(Op, DAG);
  case ISD::FSINCOS:
    return LowerFSINCOS(Op, DAG);
  case ISD::FLT_ROUNDS_:
    return LowerFLT_ROUNDS_(Op, DAG);
  case ISD::MUL:
    return LowerMUL(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN:
    return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::STORE:
    return LowerSTORE(Op, DAG);
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
    return LowerVECREDUCE(Op, DAG);
  case ISD::ATOMIC_LOAD_SUB:
    return LowerATOMIC_LOAD_SUB(Op, DAG);
  case ISD::ATOMIC_LOAD_AND:
    return LowerATOMIC_LOAD_AND(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  }
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "AArch64GenCallingConv.inc"

/// Selects the correct CCAssignFn for a given CallingConvention value.
CCAssignFn *AArch64TargetLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                     bool IsVarArg) const {
  switch (CC) {
  default:
    report_fatal_error("Unsupported calling convention.");
  case CallingConv::WebKit_JS:
    return CC_AArch64_WebKit_JS;
  case CallingConv::GHC:
    return CC_AArch64_GHC;
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::PreserveMost:
  case CallingConv::CXX_FAST_TLS:
  case CallingConv::Swift:
    if (Subtarget->isTargetWindows() && IsVarArg)
      return CC_AArch64_Win64_VarArg;
    if (!Subtarget->isTargetDarwin())
      return CC_AArch64_AAPCS;
    return IsVarArg ? CC_AArch64_DarwinPCS_VarArg : CC_AArch64_DarwinPCS;
  case CallingConv::Win64:
    return IsVarArg ? CC_AArch64_Win64_VarArg : CC_AArch64_AAPCS;
  case CallingConv::AArch64_VectorCall:
    return CC_AArch64_AAPCS;
  }
}

CCAssignFn *
AArch64TargetLowering::CCAssignFnForReturn(CallingConv::ID CC) const {
  return CC == CallingConv::WebKit_JS ? RetCC_AArch64_WebKit_JS
                                      : RetCC_AArch64_AAPCS;
}

SDValue AArch64TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool IsWin64 = Subtarget->isCallingConvWin64(MF.getFunction().getCallingConv());

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());

  // At this point, Ins[].VT may already be promoted to i32. To correctly
  // handle passing i8 as i8 instead of i32 on stack, we pass in both i32 and
  // i8 to CC_AArch64_AAPCS with i32 being ValVT and i8 being LocVT.
  // Since AnalyzeFormalArguments uses Ins[].VT for both ValVT and LocVT, here
  // we use a special version of AnalyzeFormalArguments to pass in ValVT and
  // LocVT.
  unsigned NumArgs = Ins.size();
  Function::const_arg_iterator CurOrigArg = MF.getFunction().arg_begin();
  unsigned CurArgIdx = 0;
  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT ValVT = Ins[i].VT;
    if (Ins[i].isOrigArg()) {
      std::advance(CurOrigArg, Ins[i].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[i].getOrigArgIndex();

      // Get type of the original argument.
      EVT ActualVT = getValueType(DAG.getDataLayout(), CurOrigArg->getType(),
                                  /*AllowUnknown*/ true);
      MVT ActualMVT = ActualVT.isSimple() ? ActualVT.getSimpleVT() : MVT::Other;
      // If ActualMVT is i1/i8/i16, we should set LocVT to i8/i8/i16.
      if (ActualMVT == MVT::i1 || ActualMVT == MVT::i8)
        ValVT = MVT::i8;
      else if (ActualMVT == MVT::i16)
        ValVT = MVT::i16;
    }
    CCAssignFn *AssignFn = CCAssignFnForCall(CallConv, /*IsVarArg=*/false);
    bool Res =
        AssignFn(i, ValVT, ValVT, CCValAssign::Full, Ins[i].Flags, CCInfo);
    assert(!Res && "Call operand has unhandled type");
    (void)Res;
  }
  assert(ArgLocs.size() == Ins.size());
  SmallVector<SDValue, 16> ArgValues;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    if (Ins[i].Flags.isByVal()) {
      // Byval is used for HFAs in the PCS, but the system should work in a
      // non-compliant manner for larger structs.
      EVT PtrVT = getPointerTy(DAG.getDataLayout());
      int Size = Ins[i].Flags.getByValSize();
      unsigned NumRegs = (Size + 7) / 8;

      // FIXME: This works on big-endian for composite byvals, which are the common
      // case. It should also work for fundamental types too.
      unsigned FrameIdx =
        MFI.CreateFixedObject(8 * NumRegs, VA.getLocMemOffset(), false);
      SDValue FrameIdxN = DAG.getFrameIndex(FrameIdx, PtrVT);
      InVals.push_back(FrameIdxN);

      continue;
    }

    if (VA.isRegLoc()) {
      // Arguments stored in registers.
      EVT RegVT = VA.getLocVT();

      SDValue ArgValue;
      const TargetRegisterClass *RC;

      if (RegVT == MVT::i32)
        RC = &AArch64::GPR32RegClass;
      else if (RegVT == MVT::i64)
        RC = &AArch64::GPR64RegClass;
      else if (RegVT == MVT::f16)
        RC = &AArch64::FPR16RegClass;
      else if (RegVT == MVT::f32)
        RC = &AArch64::FPR32RegClass;
      else if (RegVT == MVT::f64 || RegVT.is64BitVector())
        RC = &AArch64::FPR64RegClass;
      else if (RegVT == MVT::f128 || RegVT.is128BitVector())
        RC = &AArch64::FPR128RegClass;
      else
        llvm_unreachable("RegVT not supported by FORMAL_ARGUMENTS Lowering");

      // Transform the arguments in physical registers into virtual ones.
      unsigned Reg = MF.addLiveIn(VA.getLocReg(), RC);
      ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, RegVT);

      // If this is an 8, 16 or 32-bit value, it is really passed promoted
      // to 64 bits.  Insert an assert[sz]ext to capture this, then
      // truncate to the right size.
      switch (VA.getLocInfo()) {
      default:
        llvm_unreachable("Unknown loc info!");
      case CCValAssign::Full:
        break;
      case CCValAssign::BCvt:
        ArgValue = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), ArgValue);
        break;
      case CCValAssign::AExt:
      case CCValAssign::SExt:
      case CCValAssign::ZExt:
        // SelectionDAGBuilder will insert appropriate AssertZExt & AssertSExt
        // nodes after our lowering.
        assert(RegVT == Ins[i].VT && "incorrect register location selected");
        break;
      }

      InVals.push_back(ArgValue);

    } else { // VA.isRegLoc()
      assert(VA.isMemLoc() && "CCValAssign is neither reg nor mem");
      unsigned ArgOffset = VA.getLocMemOffset();
      unsigned ArgSize = VA.getValVT().getSizeInBits() / 8;

      uint32_t BEAlign = 0;
      if (!Subtarget->isLittleEndian() && ArgSize < 8 &&
          !Ins[i].Flags.isInConsecutiveRegs())
        BEAlign = 8 - ArgSize;

      int FI = MFI.CreateFixedObject(ArgSize, ArgOffset + BEAlign, true);

      // Create load nodes to retrieve arguments from the stack.
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      SDValue ArgValue;

      // For NON_EXTLOAD, generic code in getLoad assert(ValVT == MemVT)
      ISD::LoadExtType ExtType = ISD::NON_EXTLOAD;
      MVT MemVT = VA.getValVT();

      switch (VA.getLocInfo()) {
      default:
        break;
      case CCValAssign::BCvt:
        MemVT = VA.getLocVT();
        break;
      case CCValAssign::SExt:
        ExtType = ISD::SEXTLOAD;
        break;
      case CCValAssign::ZExt:
        ExtType = ISD::ZEXTLOAD;
        break;
      case CCValAssign::AExt:
        ExtType = ISD::EXTLOAD;
        break;
      }

      ArgValue = DAG.getExtLoad(
          ExtType, DL, VA.getLocVT(), Chain, FIN,
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI),
          MemVT);

      InVals.push_back(ArgValue);
    }
  }

  // varargs
  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  if (isVarArg) {
    if (!Subtarget->isTargetDarwin() || IsWin64) {
      // The AAPCS variadic function ABI is identical to the non-variadic
      // one. As a result there may be more arguments in registers and we should
      // save them for future reference.
      // Win64 variadic functions also pass arguments in registers, but all float
      // arguments are passed in integer registers.
      saveVarArgRegisters(CCInfo, DAG, DL, Chain);
    }

    // This will point to the next argument passed via stack.
    unsigned StackOffset = CCInfo.getNextStackOffset();
    // We currently pass all varargs at 8-byte alignment.
    StackOffset = ((StackOffset + 7) & ~7);
    FuncInfo->setVarArgsStackIndex(MFI.CreateFixedObject(4, StackOffset, true));

    if (MFI.hasMustTailInVarArgFunc()) {
      SmallVector<MVT, 2> RegParmTypes;
      RegParmTypes.push_back(MVT::i64);
      RegParmTypes.push_back(MVT::f128);
      // Compute the set of forwarded registers. The rest are scratch.
      SmallVectorImpl<ForwardedRegister> &Forwards =
                                       FuncInfo->getForwardedMustTailRegParms();
      CCInfo.analyzeMustTailForwardedRegisters(Forwards, RegParmTypes,
                                               CC_AArch64_AAPCS);
    }
  }

  unsigned StackArgSize = CCInfo.getNextStackOffset();
  bool TailCallOpt = MF.getTarget().Options.GuaranteedTailCallOpt;
  if (DoesCalleeRestoreStack(CallConv, TailCallOpt)) {
    // This is a non-standard ABI so by fiat I say we're allowed to make full
    // use of the stack area to be popped, which must be aligned to 16 bytes in
    // any case:
    StackArgSize = alignTo(StackArgSize, 16);

    // If we're expected to restore the stack (e.g. fastcc) then we'll be adding
    // a multiple of 16.
    FuncInfo->setArgumentStackToRestore(StackArgSize);

    // This realignment carries over to the available bytes below. Our own
    // callers will guarantee the space is free by giving an aligned value to
    // CALLSEQ_START.
  }
  // Even if we're not expected to free up the space, it's useful to know how
  // much is there while considering tail calls (because we can reuse it).
  FuncInfo->setBytesInStackArgArea(StackArgSize);

  if (Subtarget->hasCustomCallingConv())
    Subtarget->getRegisterInfo()->UpdateCustomCalleeSavedRegs(MF);

  return Chain;
}

void AArch64TargetLowering::saveVarArgRegisters(CCState &CCInfo,
                                                SelectionDAG &DAG,
                                                const SDLoc &DL,
                                                SDValue &Chain) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  auto PtrVT = getPointerTy(DAG.getDataLayout());
  bool IsWin64 = Subtarget->isCallingConvWin64(MF.getFunction().getCallingConv());

  SmallVector<SDValue, 8> MemOps;

  static const MCPhysReg GPRArgRegs[] = { AArch64::X0, AArch64::X1, AArch64::X2,
                                          AArch64::X3, AArch64::X4, AArch64::X5,
                                          AArch64::X6, AArch64::X7 };
  static const unsigned NumGPRArgRegs = array_lengthof(GPRArgRegs);
  unsigned FirstVariadicGPR = CCInfo.getFirstUnallocated(GPRArgRegs);

  unsigned GPRSaveSize = 8 * (NumGPRArgRegs - FirstVariadicGPR);
  int GPRIdx = 0;
  if (GPRSaveSize != 0) {
    if (IsWin64) {
      GPRIdx = MFI.CreateFixedObject(GPRSaveSize, -(int)GPRSaveSize, false);
      if (GPRSaveSize & 15)
        // The extra size here, if triggered, will always be 8.
        MFI.CreateFixedObject(16 - (GPRSaveSize & 15), -(int)alignTo(GPRSaveSize, 16), false);
    } else
      GPRIdx = MFI.CreateStackObject(GPRSaveSize, 8, false);

    SDValue FIN = DAG.getFrameIndex(GPRIdx, PtrVT);

    for (unsigned i = FirstVariadicGPR; i < NumGPRArgRegs; ++i) {
      unsigned VReg = MF.addLiveIn(GPRArgRegs[i], &AArch64::GPR64RegClass);
      SDValue Val = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i64);
      SDValue Store = DAG.getStore(
          Val.getValue(1), DL, Val, FIN,
          IsWin64
              ? MachinePointerInfo::getFixedStack(DAG.getMachineFunction(),
                                                  GPRIdx,
                                                  (i - FirstVariadicGPR) * 8)
              : MachinePointerInfo::getStack(DAG.getMachineFunction(), i * 8));
      MemOps.push_back(Store);
      FIN =
          DAG.getNode(ISD::ADD, DL, PtrVT, FIN, DAG.getConstant(8, DL, PtrVT));
    }
  }
  FuncInfo->setVarArgsGPRIndex(GPRIdx);
  FuncInfo->setVarArgsGPRSize(GPRSaveSize);

  if (Subtarget->hasFPARMv8() && !IsWin64) {
    static const MCPhysReg FPRArgRegs[] = {
        AArch64::Q0, AArch64::Q1, AArch64::Q2, AArch64::Q3,
        AArch64::Q4, AArch64::Q5, AArch64::Q6, AArch64::Q7};
    static const unsigned NumFPRArgRegs = array_lengthof(FPRArgRegs);
    unsigned FirstVariadicFPR = CCInfo.getFirstUnallocated(FPRArgRegs);

    unsigned FPRSaveSize = 16 * (NumFPRArgRegs - FirstVariadicFPR);
    int FPRIdx = 0;
    if (FPRSaveSize != 0) {
      FPRIdx = MFI.CreateStackObject(FPRSaveSize, 16, false);

      SDValue FIN = DAG.getFrameIndex(FPRIdx, PtrVT);

      for (unsigned i = FirstVariadicFPR; i < NumFPRArgRegs; ++i) {
        unsigned VReg = MF.addLiveIn(FPRArgRegs[i], &AArch64::FPR128RegClass);
        SDValue Val = DAG.getCopyFromReg(Chain, DL, VReg, MVT::f128);

        SDValue Store = DAG.getStore(
            Val.getValue(1), DL, Val, FIN,
            MachinePointerInfo::getStack(DAG.getMachineFunction(), i * 16));
        MemOps.push_back(Store);
        FIN = DAG.getNode(ISD::ADD, DL, PtrVT, FIN,
                          DAG.getConstant(16, DL, PtrVT));
      }
    }
    FuncInfo->setVarArgsFPRIndex(FPRIdx);
    FuncInfo->setVarArgsFPRSize(FPRSaveSize);
  }

  if (!MemOps.empty()) {
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOps);
  }
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
SDValue AArch64TargetLowering::LowerCallResult(
    SDValue Chain, SDValue InFlag, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals, bool isThisReturn,
    SDValue ThisVal) const {
  CCAssignFn *RetCC = CallConv == CallingConv::WebKit_JS
                          ? RetCC_AArch64_WebKit_JS
                          : RetCC_AArch64_AAPCS;
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign VA = RVLocs[i];

    // Pass 'this' value directly from the argument to return value, to avoid
    // reg unit interference
    if (i == 0 && isThisReturn) {
      assert(!VA.needsCustom() && VA.getLocVT() == MVT::i64 &&
             "unexpected return calling convention register assignment");
      InVals.push_back(ThisVal);
      continue;
    }

    SDValue Val =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), InFlag);
    Chain = Val.getValue(1);
    InFlag = Val.getValue(2);

    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full:
      break;
    case CCValAssign::BCvt:
      Val = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), Val);
      break;
    }

    InVals.push_back(Val);
  }

  return Chain;
}

/// Return true if the calling convention is one that we can guarantee TCO for.
static bool canGuaranteeTCO(CallingConv::ID CC) {
  return CC == CallingConv::Fast;
}

/// Return true if we might ever do TCO for calls with this calling convention.
static bool mayTailCallThisCC(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::C:
  case CallingConv::PreserveMost:
  case CallingConv::Swift:
    return true;
  default:
    return canGuaranteeTCO(CC);
  }
}

bool AArch64TargetLowering::isEligibleForTailCallOptimization(
    SDValue Callee, CallingConv::ID CalleeCC, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, SelectionDAG &DAG) const {
  if (!mayTailCallThisCC(CalleeCC))
    return false;

  MachineFunction &MF = DAG.getMachineFunction();
  const Function &CallerF = MF.getFunction();
  CallingConv::ID CallerCC = CallerF.getCallingConv();
  bool CCMatch = CallerCC == CalleeCC;

  // Byval parameters hand the function a pointer directly into the stack area
  // we want to reuse during a tail call. Working around this *is* possible (see
  // X86) but less efficient and uglier in LowerCall.
  for (Function::const_arg_iterator i = CallerF.arg_begin(),
                                    e = CallerF.arg_end();
       i != e; ++i)
    if (i->hasByValAttr())
      return false;

  if (getTargetMachine().Options.GuaranteedTailCallOpt)
    return canGuaranteeTCO(CalleeCC) && CCMatch;

  // Externally-defined functions with weak linkage should not be
  // tail-called on AArch64 when the OS does not support dynamic
  // pre-emption of symbols, as the AAELF spec requires normal calls
  // to undefined weak functions to be replaced with a NOP or jump to the
  // next instruction. The behaviour of branch instructions in this
  // situation (as used for tail calls) is implementation-defined, so we
  // cannot rely on the linker replacing the tail call with a return.
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = G->getGlobal();
    const Triple &TT = getTargetMachine().getTargetTriple();
    if (GV->hasExternalWeakLinkage() &&
        (!TT.isOSWindows() || TT.isOSBinFormatELF() || TT.isOSBinFormatMachO()))
      return false;
  }

  // Now we search for cases where we can use a tail call without changing the
  // ABI. Sibcall is used in some places (particularly gcc) to refer to this
  // concept.

  // I want anyone implementing a new calling convention to think long and hard
  // about this assert.
  assert((!isVarArg || CalleeCC == CallingConv::C) &&
         "Unexpected variadic calling convention");

  LLVMContext &C = *DAG.getContext();
  if (isVarArg && !Outs.empty()) {
    // At least two cases here: if caller is fastcc then we can't have any
    // memory arguments (we'd be expected to clean up the stack afterwards). If
    // caller is C then we could potentially use its argument area.

    // FIXME: for now we take the most conservative of these in both cases:
    // disallow all variadic memory operands.
    SmallVector<CCValAssign, 16> ArgLocs;
    CCState CCInfo(CalleeCC, isVarArg, MF, ArgLocs, C);

    CCInfo.AnalyzeCallOperands(Outs, CCAssignFnForCall(CalleeCC, true));
    for (const CCValAssign &ArgLoc : ArgLocs)
      if (!ArgLoc.isRegLoc())
        return false;
  }

  // Check that the call results are passed in the same way.
  if (!CCState::resultsCompatible(CalleeCC, CallerCC, MF, C, Ins,
                                  CCAssignFnForCall(CalleeCC, isVarArg),
                                  CCAssignFnForCall(CallerCC, isVarArg)))
    return false;
  // The callee has to preserve all registers the caller needs to preserve.
  const AArch64RegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  if (!CCMatch) {
    const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
    if (Subtarget->hasCustomCallingConv()) {
      TRI->UpdateCustomCallPreservedMask(MF, &CallerPreserved);
      TRI->UpdateCustomCallPreservedMask(MF, &CalleePreserved);
    }
    if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
      return false;
  }

  // Nothing more to check if the callee is taking no arguments
  if (Outs.empty())
    return true;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CalleeCC, isVarArg, MF, ArgLocs, C);

  CCInfo.AnalyzeCallOperands(Outs, CCAssignFnForCall(CalleeCC, isVarArg));

  const AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();

  // If the stack arguments for this call do not fit into our own save area then
  // the call cannot be made tail.
  if (CCInfo.getNextStackOffset() > FuncInfo->getBytesInStackArgArea())
    return false;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  if (!parametersInCSRMatch(MRI, CallerPreserved, ArgLocs, OutVals))
    return false;

  return true;
}

SDValue AArch64TargetLowering::addTokenForArgument(SDValue Chain,
                                                   SelectionDAG &DAG,
                                                   MachineFrameInfo &MFI,
                                                   int ClobberedFI) const {
  SmallVector<SDValue, 8> ArgChains;
  int64_t FirstByte = MFI.getObjectOffset(ClobberedFI);
  int64_t LastByte = FirstByte + MFI.getObjectSize(ClobberedFI) - 1;

  // Include the original chain at the beginning of the list. When this is
  // used by target LowerCall hooks, this helps legalize find the
  // CALLSEQ_BEGIN node.
  ArgChains.push_back(Chain);

  // Add a chain value for each stack argument corresponding
  for (SDNode::use_iterator U = DAG.getEntryNode().getNode()->use_begin(),
                            UE = DAG.getEntryNode().getNode()->use_end();
       U != UE; ++U)
    if (LoadSDNode *L = dyn_cast<LoadSDNode>(*U))
      if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(L->getBasePtr()))
        if (FI->getIndex() < 0) {
          int64_t InFirstByte = MFI.getObjectOffset(FI->getIndex());
          int64_t InLastByte = InFirstByte;
          InLastByte += MFI.getObjectSize(FI->getIndex()) - 1;

          if ((InFirstByte <= FirstByte && FirstByte <= InLastByte) ||
              (FirstByte <= InFirstByte && InFirstByte <= LastByte))
            ArgChains.push_back(SDValue(L, 1));
        }

  // Build a tokenfactor for all the chains.
  return DAG.getNode(ISD::TokenFactor, SDLoc(Chain), MVT::Other, ArgChains);
}

bool AArch64TargetLowering::DoesCalleeRestoreStack(CallingConv::ID CallCC,
                                                   bool TailCallOpt) const {
  return CallCC == CallingConv::Fast && TailCallOpt;
}

/// LowerCall - Lower a call to a callseq_start + CALL + callseq_end chain,
/// and add input and output parameter nodes.
SDValue
AArch64TargetLowering::LowerCall(CallLoweringInfo &CLI,
                                 SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVector<ISD::OutputArg, 32> &Outs = CLI.Outs;
  SmallVector<SDValue, 32> &OutVals = CLI.OutVals;
  SmallVector<ISD::InputArg, 32> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();
  bool IsThisReturn = false;

  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  bool TailCallOpt = MF.getTarget().Options.GuaranteedTailCallOpt;
  bool IsSibCall = false;

  if (IsTailCall) {
    // Check if it's really possible to do a tail call.
    IsTailCall = isEligibleForTailCallOptimization(
        Callee, CallConv, IsVarArg, Outs, OutVals, Ins, DAG);
    if (!IsTailCall && CLI.CS && CLI.CS.isMustTailCall())
      report_fatal_error("failed to perform tail call elimination on a call "
                         "site marked musttail");

    // A sibling call is one where we're under the usual C ABI and not planning
    // to change that but can still do a tail call:
    if (!TailCallOpt && IsTailCall)
      IsSibCall = true;

    if (IsTailCall)
      ++NumTailCalls;
  }

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());

  if (IsVarArg) {
    // Handle fixed and variable vector arguments differently.
    // Variable vector arguments always go into memory.
    unsigned NumArgs = Outs.size();

    for (unsigned i = 0; i != NumArgs; ++i) {
      MVT ArgVT = Outs[i].VT;
      ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
      CCAssignFn *AssignFn = CCAssignFnForCall(CallConv,
                                               /*IsVarArg=*/ !Outs[i].IsFixed);
      bool Res = AssignFn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, CCInfo);
      assert(!Res && "Call operand has unhandled type");
      (void)Res;
    }
  } else {
    // At this point, Outs[].VT may already be promoted to i32. To correctly
    // handle passing i8 as i8 instead of i32 on stack, we pass in both i32 and
    // i8 to CC_AArch64_AAPCS with i32 being ValVT and i8 being LocVT.
    // Since AnalyzeCallOperands uses Ins[].VT for both ValVT and LocVT, here
    // we use a special version of AnalyzeCallOperands to pass in ValVT and
    // LocVT.
    unsigned NumArgs = Outs.size();
    for (unsigned i = 0; i != NumArgs; ++i) {
      MVT ValVT = Outs[i].VT;
      // Get type of the original argument.
      EVT ActualVT = getValueType(DAG.getDataLayout(),
                                  CLI.getArgs()[Outs[i].OrigArgIndex].Ty,
                                  /*AllowUnknown*/ true);
      MVT ActualMVT = ActualVT.isSimple() ? ActualVT.getSimpleVT() : ValVT;
      ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
      // If ActualMVT is i1/i8/i16, we should set LocVT to i8/i8/i16.
      if (ActualMVT == MVT::i1 || ActualMVT == MVT::i8)
        ValVT = MVT::i8;
      else if (ActualMVT == MVT::i16)
        ValVT = MVT::i16;

      CCAssignFn *AssignFn = CCAssignFnForCall(CallConv, /*IsVarArg=*/false);
      bool Res = AssignFn(i, ValVT, ValVT, CCValAssign::Full, ArgFlags, CCInfo);
      assert(!Res && "Call operand has unhandled type");
      (void)Res;
    }
  }

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getNextStackOffset();

  if (IsSibCall) {
    // Since we're not changing the ABI to make this a tail call, the memory
    // operands are already available in the caller's incoming argument space.
    NumBytes = 0;
  }

  // FPDiff is the byte offset of the call's argument area from the callee's.
  // Stores to callee stack arguments will be placed in FixedStackSlots offset
  // by this amount for a tail call. In a sibling call it must be 0 because the
  // caller will deallocate the entire stack and the callee still expects its
  // arguments to begin at SP+0. Completely unused for non-tail calls.
  int FPDiff = 0;

  if (IsTailCall && !IsSibCall) {
    unsigned NumReusableBytes = FuncInfo->getBytesInStackArgArea();

    // Since callee will pop argument stack as a tail call, we must keep the
    // popped size 16-byte aligned.
    NumBytes = alignTo(NumBytes, 16);

    // FPDiff will be negative if this tail call requires more space than we
    // would automatically have in our incoming argument space. Positive if we
    // can actually shrink the stack.
    FPDiff = NumReusableBytes - NumBytes;

    // The stack pointer must be 16-byte aligned at all times it's used for a
    // memory operation, which in practice means at *all* times and in
    // particular across call boundaries. Therefore our own arguments started at
    // a 16-byte aligned SP and the delta applied for the tail call should
    // satisfy the same constraint.
    assert(FPDiff % 16 == 0 && "unaligned stack on tail call");
  }

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  if (!IsSibCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SDValue StackPtr = DAG.getCopyFromReg(Chain, DL, AArch64::SP,
                                        getPointerTy(DAG.getDataLayout()));

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  if (IsVarArg && CLI.CS && CLI.CS.isMustTailCall()) {
    const auto &Forwards = FuncInfo->getForwardedMustTailRegParms();
    for (const auto &F : Forwards) {
      SDValue Val = DAG.getCopyFromReg(Chain, DL, F.VReg, F.VT);
       RegsToPass.push_back(std::make_pair(unsigned(F.PReg), Val));
    }
  }

  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, realArgIdx = 0, e = ArgLocs.size(); i != e;
       ++i, ++realArgIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[realArgIdx];
    ISD::ArgFlagsTy Flags = Outs[realArgIdx].Flags;

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      if (Outs[realArgIdx].ArgVT == MVT::i1) {
        // AAPCS requires i1 to be zero-extended to 8-bits by the caller.
        Arg = DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Arg);
        Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i8, Arg);
      }
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::FPExt:
      Arg = DAG.getNode(ISD::FP_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    }

    if (VA.isRegLoc()) {
      if (realArgIdx == 0 && Flags.isReturned() && !Flags.isSwiftSelf() &&
          Outs[0].VT == MVT::i64) {
        assert(VA.getLocVT() == MVT::i64 &&
               "unexpected calling convention register assignment");
        assert(!Ins.empty() && Ins[0].VT == MVT::i64 &&
               "unexpected use of 'returned'");
        IsThisReturn = true;
      }
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      assert(VA.isMemLoc());

      SDValue DstAddr;
      MachinePointerInfo DstInfo;

      // FIXME: This works on big-endian for composite byvals, which are the
      // common case. It should also work for fundamental types too.
      uint32_t BEAlign = 0;
      unsigned OpSize = Flags.isByVal() ? Flags.getByValSize() * 8
                                        : VA.getValVT().getSizeInBits();
      OpSize = (OpSize + 7) / 8;
      if (!Subtarget->isLittleEndian() && !Flags.isByVal() &&
          !Flags.isInConsecutiveRegs()) {
        if (OpSize < 8)
          BEAlign = 8 - OpSize;
      }
      unsigned LocMemOffset = VA.getLocMemOffset();
      int32_t Offset = LocMemOffset + BEAlign;
      SDValue PtrOff = DAG.getIntPtrConstant(Offset, DL);
      PtrOff = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, PtrOff);

      if (IsTailCall) {
        Offset = Offset + FPDiff;
        int FI = MF.getFrameInfo().CreateFixedObject(OpSize, Offset, true);

        DstAddr = DAG.getFrameIndex(FI, PtrVT);
        DstInfo =
            MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

        // Make sure any stack arguments overlapping with where we're storing
        // are loaded before this eventual operation. Otherwise they'll be
        // clobbered.
        Chain = addTokenForArgument(Chain, DAG, MF.getFrameInfo(), FI);
      } else {
        SDValue PtrOff = DAG.getIntPtrConstant(Offset, DL);

        DstAddr = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, PtrOff);
        DstInfo = MachinePointerInfo::getStack(DAG.getMachineFunction(),
                                               LocMemOffset);
      }

      if (Outs[i].Flags.isByVal()) {
        SDValue SizeNode =
            DAG.getConstant(Outs[i].Flags.getByValSize(), DL, MVT::i64);
        SDValue Cpy = DAG.getMemcpy(
            Chain, DL, DstAddr, Arg, SizeNode, Outs[i].Flags.getByValAlign(),
            /*isVol = */ false, /*AlwaysInline = */ false,
            /*isTailCall = */ false,
            DstInfo, MachinePointerInfo());

        MemOpChains.push_back(Cpy);
      } else {
        // Since we pass i1/i8/i16 as i1/i8/i16 on stack and Arg is already
        // promoted to a legal register type i32, we should truncate Arg back to
        // i1/i8/i16.
        if (VA.getValVT() == MVT::i1 || VA.getValVT() == MVT::i8 ||
            VA.getValVT() == MVT::i16)
          Arg = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Arg);

        SDValue Store = DAG.getStore(Chain, DL, Arg, DstAddr, DstInfo);
        MemOpChains.push_back(Store);
      }
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InFlag;
  for (auto &RegToPass : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, RegToPass.first,
                             RegToPass.second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    auto GV = G->getGlobal();
    if (Subtarget->classifyGlobalFunctionReference(GV, getTargetMachine()) ==
        AArch64II::MO_GOT) {
      Callee = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, AArch64II::MO_GOT);
      Callee = DAG.getNode(AArch64ISD::LOADgot, DL, PtrVT, Callee);
    } else if (Subtarget->isTargetCOFF() && GV->hasDLLImportStorageClass()) {
      assert(Subtarget->isTargetWindows() &&
             "Windows is the only supported COFF target");
      Callee = getGOT(G, DAG, AArch64II::MO_DLLIMPORT);
    } else {
      const GlobalValue *GV = G->getGlobal();
      Callee = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, 0);
    }
  } else if (auto *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    if (getTargetMachine().getCodeModel() == CodeModel::Large &&
        Subtarget->isTargetMachO()) {
      const char *Sym = S->getSymbol();
      Callee = DAG.getTargetExternalSymbol(Sym, PtrVT, AArch64II::MO_GOT);
      Callee = DAG.getNode(AArch64ISD::LOADgot, DL, PtrVT, Callee);
    } else {
      const char *Sym = S->getSymbol();
      Callee = DAG.getTargetExternalSymbol(Sym, PtrVT, 0);
    }
  }

  // We don't usually want to end the call-sequence here because we would tidy
  // the frame up *after* the call, however in the ABI-changing tail-call case
  // we've carefully laid out the parameters so that when sp is reset they'll be
  // in the correct location.
  if (IsTailCall && !IsSibCall) {
    Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(NumBytes, DL, true),
                               DAG.getIntPtrConstant(0, DL, true), InFlag, DL);
    InFlag = Chain.getValue(1);
  }

  std::vector<SDValue> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  if (IsTailCall) {
    // Each tail call may have to adjust the stack by a different amount, so
    // this information must travel along with the operation for eventual
    // consumption by emitEpilogue.
    Ops.push_back(DAG.getTargetConstant(FPDiff, DL, MVT::i32));
  }

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (auto &RegToPass : RegsToPass)
    Ops.push_back(DAG.getRegister(RegToPass.first,
                                  RegToPass.second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const uint32_t *Mask;
  const AArch64RegisterInfo *TRI = Subtarget->getRegisterInfo();
  if (IsThisReturn) {
    // For 'this' returns, use the X0-preserving mask if applicable
    Mask = TRI->getThisReturnPreservedMask(MF, CallConv);
    if (!Mask) {
      IsThisReturn = false;
      Mask = TRI->getCallPreservedMask(MF, CallConv);
    }
  } else
    Mask = TRI->getCallPreservedMask(MF, CallConv);

  if (Subtarget->hasCustomCallingConv())
    TRI->UpdateCustomCallPreservedMask(MF, &Mask);

  if (TRI->isAnyArgRegReserved(MF))
    TRI->emitReservedArgRegCallError(MF);

  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  // If we're doing a tall call, use a TC_RETURN here rather than an
  // actual call instruction.
  if (IsTailCall) {
    MF.getFrameInfo().setHasTailCall();
    return DAG.getNode(AArch64ISD::TC_RETURN, DL, NodeTys, Ops);
  }

  // Returns a chain and a flag for retval copy to use.
  Chain = DAG.getNode(AArch64ISD::CALL, DL, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  uint64_t CalleePopBytes =
      DoesCalleeRestoreStack(CallConv, TailCallOpt) ? alignTo(NumBytes, 16) : 0;

  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(NumBytes, DL, true),
                             DAG.getIntPtrConstant(CalleePopBytes, DL, true),
                             InFlag, DL);
  if (!Ins.empty())
    InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, IsVarArg, Ins, DL, DAG,
                         InVals, IsThisReturn,
                         IsThisReturn ? OutVals[0] : SDValue());
}

bool AArch64TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context) const {
  CCAssignFn *RetCC = CallConv == CallingConv::WebKit_JS
                          ? RetCC_AArch64_WebKit_JS
                          : RetCC_AArch64_AAPCS;
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC);
}

SDValue
AArch64TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                   bool isVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   const SmallVectorImpl<SDValue> &OutVals,
                                   const SDLoc &DL, SelectionDAG &DAG) const {
  CCAssignFn *RetCC = CallConv == CallingConv::WebKit_JS
                          ? RetCC_AArch64_WebKit_JS
                          : RetCC_AArch64_AAPCS;
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC);

  // Copy the result values into the output registers.
  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  for (unsigned i = 0, realRVLocIdx = 0; i != RVLocs.size();
       ++i, ++realRVLocIdx) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");
    SDValue Arg = OutVals[realRVLocIdx];

    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full:
      if (Outs[i].ArgVT == MVT::i1) {
        // AAPCS requires i1 to be zero-extended to i8 by the producer of the
        // value. This is strictly redundant on Darwin (which uses "zeroext
        // i1"), but will be optimised out before ISel.
        Arg = DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Arg);
        Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      }
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), Arg);
      break;
    }

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Arg, Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }
  const AArch64RegisterInfo *TRI = Subtarget->getRegisterInfo();
  const MCPhysReg *I =
      TRI->getCalleeSavedRegsViaCopy(&DAG.getMachineFunction());
  if (I) {
    for (; *I; ++I) {
      if (AArch64::GPR64RegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::i64));
      else if (AArch64::FPR64RegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::getFloatingPointVT(64)));
      else
        llvm_unreachable("Unexpected register class in CSRsViaCopy!");
    }
  }

  RetOps[0] = Chain; // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(AArch64ISD::RET_FLAG, DL, MVT::Other, RetOps);
}

//===----------------------------------------------------------------------===//
//  Other Lowering Code
//===----------------------------------------------------------------------===//

SDValue AArch64TargetLowering::getTargetNode(GlobalAddressSDNode *N, EVT Ty,
                                             SelectionDAG &DAG,
                                             unsigned Flag) const {
  return DAG.getTargetGlobalAddress(N->getGlobal(), SDLoc(N), Ty,
                                    N->getOffset(), Flag);
}

SDValue AArch64TargetLowering::getTargetNode(JumpTableSDNode *N, EVT Ty,
                                             SelectionDAG &DAG,
                                             unsigned Flag) const {
  return DAG.getTargetJumpTable(N->getIndex(), Ty, Flag);
}

SDValue AArch64TargetLowering::getTargetNode(ConstantPoolSDNode *N, EVT Ty,
                                             SelectionDAG &DAG,
                                             unsigned Flag) const {
  return DAG.getTargetConstantPool(N->getConstVal(), Ty, N->getAlignment(),
                                   N->getOffset(), Flag);
}

SDValue AArch64TargetLowering::getTargetNode(BlockAddressSDNode* N, EVT Ty,
                                             SelectionDAG &DAG,
                                             unsigned Flag) const {
  return DAG.getTargetBlockAddress(N->getBlockAddress(), Ty, 0, Flag);
}

// (loadGOT sym)
template <class NodeTy>
SDValue AArch64TargetLowering::getGOT(NodeTy *N, SelectionDAG &DAG,
                                      unsigned Flags) const {
  LLVM_DEBUG(dbgs() << "AArch64TargetLowering::getGOT\n");
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  SDValue GotAddr = getTargetNode(N, Ty, DAG, AArch64II::MO_GOT | Flags);
  // FIXME: Once remat is capable of dealing with instructions with register
  // operands, expand this into two nodes instead of using a wrapper node.
  return DAG.getNode(AArch64ISD::LOADgot, DL, Ty, GotAddr);
}

// (wrapper %highest(sym), %higher(sym), %hi(sym), %lo(sym))
template <class NodeTy>
SDValue AArch64TargetLowering::getAddrLarge(NodeTy *N, SelectionDAG &DAG,
                                            unsigned Flags) const {
  LLVM_DEBUG(dbgs() << "AArch64TargetLowering::getAddrLarge\n");
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  const unsigned char MO_NC = AArch64II::MO_NC;
  return DAG.getNode(
      AArch64ISD::WrapperLarge, DL, Ty,
      getTargetNode(N, Ty, DAG, AArch64II::MO_G3 | Flags),
      getTargetNode(N, Ty, DAG, AArch64II::MO_G2 | MO_NC | Flags),
      getTargetNode(N, Ty, DAG, AArch64II::MO_G1 | MO_NC | Flags),
      getTargetNode(N, Ty, DAG, AArch64II::MO_G0 | MO_NC | Flags));
}

// (addlow (adrp %hi(sym)) %lo(sym))
template <class NodeTy>
SDValue AArch64TargetLowering::getAddr(NodeTy *N, SelectionDAG &DAG,
                                       unsigned Flags) const {
  LLVM_DEBUG(dbgs() << "AArch64TargetLowering::getAddr\n");
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  SDValue Hi = getTargetNode(N, Ty, DAG, AArch64II::MO_PAGE | Flags);
  SDValue Lo = getTargetNode(N, Ty, DAG,
                             AArch64II::MO_PAGEOFF | AArch64II::MO_NC | Flags);
  SDValue ADRP = DAG.getNode(AArch64ISD::ADRP, DL, Ty, Hi);
  return DAG.getNode(AArch64ISD::ADDlow, DL, Ty, ADRP, Lo);
}

// (adr sym)
template <class NodeTy>
SDValue AArch64TargetLowering::getAddrTiny(NodeTy *N, SelectionDAG &DAG,
                                           unsigned Flags) const {
  LLVM_DEBUG(dbgs() << "AArch64TargetLowering::getAddrTiny\n");
  SDLoc DL(N);
  EVT Ty = getPointerTy(DAG.getDataLayout());
  SDValue Sym = getTargetNode(N, Ty, DAG, Flags);
  return DAG.getNode(AArch64ISD::ADR, DL, Ty, Sym);
}

SDValue AArch64TargetLowering::LowerGlobalAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  GlobalAddressSDNode *GN = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GN->getGlobal();
  unsigned char OpFlags =
      Subtarget->ClassifyGlobalReference(GV, getTargetMachine());

  if (OpFlags != AArch64II::MO_NO_FLAG)
    assert(cast<GlobalAddressSDNode>(Op)->getOffset() == 0 &&
           "unexpected offset in global node");

  // This also catches the large code model case for Darwin, and tiny code
  // model with got relocations.
  if ((OpFlags & AArch64II::MO_GOT) != 0) {
    return getGOT(GN, DAG, OpFlags);
  }

  SDValue Result;
  if (getTargetMachine().getCodeModel() == CodeModel::Large) {
    Result = getAddrLarge(GN, DAG, OpFlags);
  } else if (getTargetMachine().getCodeModel() == CodeModel::Tiny) {
    Result = getAddrTiny(GN, DAG, OpFlags);
  } else {
    Result = getAddr(GN, DAG, OpFlags);
  }
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(GN);
  if (OpFlags & (AArch64II::MO_DLLIMPORT | AArch64II::MO_COFFSTUB))
    Result = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  return Result;
}

/// Convert a TLS address reference into the correct sequence of loads
/// and calls to compute the variable's address (for Darwin, currently) and
/// return an SDValue containing the final node.

/// Darwin only has one TLS scheme which must be capable of dealing with the
/// fully general situation, in the worst case. This means:
///     + "extern __thread" declaration.
///     + Defined in a possibly unknown dynamic library.
///
/// The general system is that each __thread variable has a [3 x i64] descriptor
/// which contains information used by the runtime to calculate the address. The
/// only part of this the compiler needs to know about is the first xword, which
/// contains a function pointer that must be called with the address of the
/// entire descriptor in "x0".
///
/// Since this descriptor may be in a different unit, in general even the
/// descriptor must be accessed via an indirect load. The "ideal" code sequence
/// is:
///     adrp x0, _var@TLVPPAGE
///     ldr x0, [x0, _var@TLVPPAGEOFF]   ; x0 now contains address of descriptor
///     ldr x1, [x0]                     ; x1 contains 1st entry of descriptor,
///                                      ; the function pointer
///     blr x1                           ; Uses descriptor address in x0
///     ; Address of _var is now in x0.
///
/// If the address of _var's descriptor *is* known to the linker, then it can
/// change the first "ldr" instruction to an appropriate "add x0, x0, #imm" for
/// a slight efficiency gain.
SDValue
AArch64TargetLowering::LowerDarwinGlobalTLSAddress(SDValue Op,
                                                   SelectionDAG &DAG) const {
  assert(Subtarget->isTargetDarwin() &&
         "This function expects a Darwin target");

  SDLoc DL(Op);
  MVT PtrVT = getPointerTy(DAG.getDataLayout());
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();

  SDValue TLVPAddr =
      DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, AArch64II::MO_TLS);
  SDValue DescAddr = DAG.getNode(AArch64ISD::LOADgot, DL, PtrVT, TLVPAddr);

  // The first entry in the descriptor is a function pointer that we must call
  // to obtain the address of the variable.
  SDValue Chain = DAG.getEntryNode();
  SDValue FuncTLVGet = DAG.getLoad(
      MVT::i64, DL, Chain, DescAddr,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()),
      /* Alignment = */ 8,
      MachineMemOperand::MONonTemporal | MachineMemOperand::MOInvariant |
          MachineMemOperand::MODereferenceable);
  Chain = FuncTLVGet.getValue(1);

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setAdjustsStack(true);

  // TLS calls preserve all registers except those that absolutely must be
  // trashed: X0 (it takes an argument), LR (it's a call) and NZCV (let's not be
  // silly).
  const AArch64RegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *Mask = TRI->getTLSCallPreservedMask();
  if (Subtarget->hasCustomCallingConv())
    TRI->UpdateCustomCallPreservedMask(DAG.getMachineFunction(), &Mask);

  // Finally, we can make the call. This is just a degenerate version of a
  // normal AArch64 call node: x0 takes the address of the descriptor, and
  // returns the address of the variable in this thread.
  Chain = DAG.getCopyToReg(Chain, DL, AArch64::X0, DescAddr, SDValue());
  Chain =
      DAG.getNode(AArch64ISD::CALL, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                  Chain, FuncTLVGet, DAG.getRegister(AArch64::X0, MVT::i64),
                  DAG.getRegisterMask(Mask), Chain.getValue(1));
  return DAG.getCopyFromReg(Chain, DL, AArch64::X0, PtrVT, Chain.getValue(1));
}

/// When accessing thread-local variables under either the general-dynamic or
/// local-dynamic system, we make a "TLS-descriptor" call. The variable will
/// have a descriptor, accessible via a PC-relative ADRP, and whose first entry
/// is a function pointer to carry out the resolution.
///
/// The sequence is:
///    adrp  x0, :tlsdesc:var
///    ldr   x1, [x0, #:tlsdesc_lo12:var]
///    add   x0, x0, #:tlsdesc_lo12:var
///    .tlsdesccall var
///    blr   x1
///    (TPIDR_EL0 offset now in x0)
///
///  The above sequence must be produced unscheduled, to enable the linker to
///  optimize/relax this sequence.
///  Therefore, a pseudo-instruction (TLSDESC_CALLSEQ) is used to represent the
///  above sequence, and expanded really late in the compilation flow, to ensure
///  the sequence is produced as per above.
SDValue AArch64TargetLowering::LowerELFTLSDescCallSeq(SDValue SymAddr,
                                                      const SDLoc &DL,
                                                      SelectionDAG &DAG) const {
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue Chain = DAG.getEntryNode();
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  Chain =
      DAG.getNode(AArch64ISD::TLSDESC_CALLSEQ, DL, NodeTys, {Chain, SymAddr});
  SDValue Glue = Chain.getValue(1);

  return DAG.getCopyFromReg(Chain, DL, AArch64::X0, PtrVT, Glue);
}

SDValue
AArch64TargetLowering::LowerELFGlobalTLSAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert(Subtarget->isTargetELF() && "This function expects an ELF target");
  if (getTargetMachine().getCodeModel() == CodeModel::Large)
    report_fatal_error("ELF TLS only supported in small memory model");
  // Different choices can be made for the maximum size of the TLS area for a
  // module. For the small address model, the default TLS size is 16MiB and the
  // maximum TLS size is 4GiB.
  // FIXME: add -mtls-size command line option and make it control the 16MiB
  // vs. 4GiB code sequence generation.
  // FIXME: add tiny codemodel support. We currently generate the same code as
  // small, which may be larger than needed.
  const GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);

  TLSModel::Model Model = getTargetMachine().getTLSModel(GA->getGlobal());

  if (!EnableAArch64ELFLocalDynamicTLSGeneration) {
    if (Model == TLSModel::LocalDynamic)
      Model = TLSModel::GeneralDynamic;
  }

  SDValue TPOff;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(Op);
  const GlobalValue *GV = GA->getGlobal();

  SDValue ThreadBase = DAG.getNode(AArch64ISD::THREAD_POINTER, DL, PtrVT);

  if (Model == TLSModel::LocalExec) {
    SDValue HiVar = DAG.getTargetGlobalAddress(
        GV, DL, PtrVT, 0, AArch64II::MO_TLS | AArch64II::MO_HI12);
    SDValue LoVar = DAG.getTargetGlobalAddress(
        GV, DL, PtrVT, 0,
        AArch64II::MO_TLS | AArch64II::MO_PAGEOFF | AArch64II::MO_NC);

    SDValue TPWithOff_lo =
        SDValue(DAG.getMachineNode(AArch64::ADDXri, DL, PtrVT, ThreadBase,
                                   HiVar,
                                   DAG.getTargetConstant(0, DL, MVT::i32)),
                0);
    SDValue TPWithOff =
        SDValue(DAG.getMachineNode(AArch64::ADDXri, DL, PtrVT, TPWithOff_lo,
                                   LoVar,
                                   DAG.getTargetConstant(0, DL, MVT::i32)),
                0);
    return TPWithOff;
  } else if (Model == TLSModel::InitialExec) {
    TPOff = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, AArch64II::MO_TLS);
    TPOff = DAG.getNode(AArch64ISD::LOADgot, DL, PtrVT, TPOff);
  } else if (Model == TLSModel::LocalDynamic) {
    // Local-dynamic accesses proceed in two phases. A general-dynamic TLS
    // descriptor call against the special symbol _TLS_MODULE_BASE_ to calculate
    // the beginning of the module's TLS region, followed by a DTPREL offset
    // calculation.

    // These accesses will need deduplicating if there's more than one.
    AArch64FunctionInfo *MFI =
        DAG.getMachineFunction().getInfo<AArch64FunctionInfo>();
    MFI->incNumLocalDynamicTLSAccesses();

    // The call needs a relocation too for linker relaxation. It doesn't make
    // sense to call it MO_PAGE or MO_PAGEOFF though so we need another copy of
    // the address.
    SDValue SymAddr = DAG.getTargetExternalSymbol("_TLS_MODULE_BASE_", PtrVT,
                                                  AArch64II::MO_TLS);

    // Now we can calculate the offset from TPIDR_EL0 to this module's
    // thread-local area.
    TPOff = LowerELFTLSDescCallSeq(SymAddr, DL, DAG);

    // Now use :dtprel_whatever: operations to calculate this variable's offset
    // in its thread-storage area.
    SDValue HiVar = DAG.getTargetGlobalAddress(
        GV, DL, MVT::i64, 0, AArch64II::MO_TLS | AArch64II::MO_HI12);
    SDValue LoVar = DAG.getTargetGlobalAddress(
        GV, DL, MVT::i64, 0,
        AArch64II::MO_TLS | AArch64II::MO_PAGEOFF | AArch64II::MO_NC);

    TPOff = SDValue(DAG.getMachineNode(AArch64::ADDXri, DL, PtrVT, TPOff, HiVar,
                                       DAG.getTargetConstant(0, DL, MVT::i32)),
                    0);
    TPOff = SDValue(DAG.getMachineNode(AArch64::ADDXri, DL, PtrVT, TPOff, LoVar,
                                       DAG.getTargetConstant(0, DL, MVT::i32)),
                    0);
  } else if (Model == TLSModel::GeneralDynamic) {
    // The call needs a relocation too for linker relaxation. It doesn't make
    // sense to call it MO_PAGE or MO_PAGEOFF though so we need another copy of
    // the address.
    SDValue SymAddr =
        DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, AArch64II::MO_TLS);

    // Finally we can make a call to calculate the offset from tpidr_el0.
    TPOff = LowerELFTLSDescCallSeq(SymAddr, DL, DAG);
  } else
    llvm_unreachable("Unsupported ELF TLS access model");

  return DAG.getNode(ISD::ADD, DL, PtrVT, ThreadBase, TPOff);
}

SDValue
AArch64TargetLowering::LowerWindowsGlobalTLSAddress(SDValue Op,
                                                    SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() && "Windows specific TLS lowering");

  SDValue Chain = DAG.getEntryNode();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(Op);

  SDValue TEB = DAG.getRegister(AArch64::X18, MVT::i64);

  // Load the ThreadLocalStoragePointer from the TEB
  // A pointer to the TLS array is located at offset 0x58 from the TEB.
  SDValue TLSArray =
      DAG.getNode(ISD::ADD, DL, PtrVT, TEB, DAG.getIntPtrConstant(0x58, DL));
  TLSArray = DAG.getLoad(PtrVT, DL, Chain, TLSArray, MachinePointerInfo());
  Chain = TLSArray.getValue(1);

  // Load the TLS index from the C runtime;
  // This does the same as getAddr(), but without having a GlobalAddressSDNode.
  // This also does the same as LOADgot, but using a generic i32 load,
  // while LOADgot only loads i64.
  SDValue TLSIndexHi =
      DAG.getTargetExternalSymbol("_tls_index", PtrVT, AArch64II::MO_PAGE);
  SDValue TLSIndexLo = DAG.getTargetExternalSymbol(
      "_tls_index", PtrVT, AArch64II::MO_PAGEOFF | AArch64II::MO_NC);
  SDValue ADRP = DAG.getNode(AArch64ISD::ADRP, DL, PtrVT, TLSIndexHi);
  SDValue TLSIndex =
      DAG.getNode(AArch64ISD::ADDlow, DL, PtrVT, ADRP, TLSIndexLo);
  TLSIndex = DAG.getLoad(MVT::i32, DL, Chain, TLSIndex, MachinePointerInfo());
  Chain = TLSIndex.getValue(1);

  // The pointer to the thread's TLS data area is at the TLS Index scaled by 8
  // offset into the TLSArray.
  TLSIndex = DAG.getNode(ISD::ZERO_EXTEND, DL, PtrVT, TLSIndex);
  SDValue Slot = DAG.getNode(ISD::SHL, DL, PtrVT, TLSIndex,
                             DAG.getConstant(3, DL, PtrVT));
  SDValue TLS = DAG.getLoad(PtrVT, DL, Chain,
                            DAG.getNode(ISD::ADD, DL, PtrVT, TLSArray, Slot),
                            MachinePointerInfo());
  Chain = TLS.getValue(1);

  const GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GA->getGlobal();
  SDValue TGAHi = DAG.getTargetGlobalAddress(
      GV, DL, PtrVT, 0, AArch64II::MO_TLS | AArch64II::MO_HI12);
  SDValue TGALo = DAG.getTargetGlobalAddress(
      GV, DL, PtrVT, 0,
      AArch64II::MO_TLS | AArch64II::MO_PAGEOFF | AArch64II::MO_NC);

  // Add the offset from the start of the .tls section (section base).
  SDValue Addr =
      SDValue(DAG.getMachineNode(AArch64::ADDXri, DL, PtrVT, TLS, TGAHi,
                                 DAG.getTargetConstant(0, DL, MVT::i32)),
              0);
  Addr = DAG.getNode(AArch64ISD::ADDlow, DL, PtrVT, Addr, TGALo);
  return Addr;
}

SDValue AArch64TargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                     SelectionDAG &DAG) const {
  const GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(GA, DAG);

  if (Subtarget->isTargetDarwin())
    return LowerDarwinGlobalTLSAddress(Op, DAG);
  if (Subtarget->isTargetELF())
    return LowerELFGlobalTLSAddress(Op, DAG);
  if (Subtarget->isTargetWindows())
    return LowerWindowsGlobalTLSAddress(Op, DAG);

  llvm_unreachable("Unexpected platform trying to use TLS");
}

SDValue AArch64TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);

  MachineFunction &MF = DAG.getMachineFunction();
  // Speculation tracking/SLH assumes that optimized TB(N)Z/CB(N)Z instructions
  // will not be produced, as they are conditional branch instructions that do
  // not set flags.
  bool ProduceNonFlagSettingCondBr =
      !MF.getFunction().hasFnAttribute(Attribute::SpeculativeLoadHardening);

  // Handle f128 first, since lowering it will result in comparing the return
  // value of a libcall against zero, which is just what the rest of LowerBR_CC
  // is expecting to deal with.
  if (LHS.getValueType() == MVT::f128) {
    softenSetCCOperands(DAG, MVT::f128, LHS, RHS, CC, dl);

    // If softenSetCCOperands returned a scalar, we need to compare the result
    // against zero to select between true and false values.
    if (!RHS.getNode()) {
      RHS = DAG.getConstant(0, dl, LHS.getValueType());
      CC = ISD::SETNE;
    }
  }

  // Optimize {s|u}{add|sub|mul}.with.overflow feeding into a branch
  // instruction.
  if (isOverflowIntrOpRes(LHS) && isOneConstant(RHS) &&
      (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    // Only lower legal XALUO ops.
    if (!DAG.getTargetLoweringInfo().isTypeLegal(LHS->getValueType(0)))
      return SDValue();

    // The actual operation with overflow check.
    AArch64CC::CondCode OFCC;
    SDValue Value, Overflow;
    std::tie(Value, Overflow) = getAArch64XALUOOp(OFCC, LHS.getValue(0), DAG);

    if (CC == ISD::SETNE)
      OFCC = getInvertedCondCode(OFCC);
    SDValue CCVal = DAG.getConstant(OFCC, dl, MVT::i32);

    return DAG.getNode(AArch64ISD::BRCOND, dl, MVT::Other, Chain, Dest, CCVal,
                       Overflow);
  }

  if (LHS.getValueType().isInteger()) {
    assert((LHS.getValueType() == RHS.getValueType()) &&
           (LHS.getValueType() == MVT::i32 || LHS.getValueType() == MVT::i64));

    // If the RHS of the comparison is zero, we can potentially fold this
    // to a specialized branch.
    const ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS);
    if (RHSC && RHSC->getZExtValue() == 0 && ProduceNonFlagSettingCondBr) {
      if (CC == ISD::SETEQ) {
        // See if we can use a TBZ to fold in an AND as well.
        // TBZ has a smaller branch displacement than CBZ.  If the offset is
        // out of bounds, a late MI-layer pass rewrites branches.
        // 403.gcc is an example that hits this case.
        if (LHS.getOpcode() == ISD::AND &&
            isa<ConstantSDNode>(LHS.getOperand(1)) &&
            isPowerOf2_64(LHS.getConstantOperandVal(1))) {
          SDValue Test = LHS.getOperand(0);
          uint64_t Mask = LHS.getConstantOperandVal(1);
          return DAG.getNode(AArch64ISD::TBZ, dl, MVT::Other, Chain, Test,
                             DAG.getConstant(Log2_64(Mask), dl, MVT::i64),
                             Dest);
        }

        return DAG.getNode(AArch64ISD::CBZ, dl, MVT::Other, Chain, LHS, Dest);
      } else if (CC == ISD::SETNE) {
        // See if we can use a TBZ to fold in an AND as well.
        // TBZ has a smaller branch displacement than CBZ.  If the offset is
        // out of bounds, a late MI-layer pass rewrites branches.
        // 403.gcc is an example that hits this case.
        if (LHS.getOpcode() == ISD::AND &&
            isa<ConstantSDNode>(LHS.getOperand(1)) &&
            isPowerOf2_64(LHS.getConstantOperandVal(1))) {
          SDValue Test = LHS.getOperand(0);
          uint64_t Mask = LHS.getConstantOperandVal(1);
          return DAG.getNode(AArch64ISD::TBNZ, dl, MVT::Other, Chain, Test,
                             DAG.getConstant(Log2_64(Mask), dl, MVT::i64),
                             Dest);
        }

        return DAG.getNode(AArch64ISD::CBNZ, dl, MVT::Other, Chain, LHS, Dest);
      } else if (CC == ISD::SETLT && LHS.getOpcode() != ISD::AND) {
        // Don't combine AND since emitComparison converts the AND to an ANDS
        // (a.k.a. TST) and the test in the test bit and branch instruction
        // becomes redundant.  This would also increase register pressure.
        uint64_t Mask = LHS.getValueSizeInBits() - 1;
        return DAG.getNode(AArch64ISD::TBNZ, dl, MVT::Other, Chain, LHS,
                           DAG.getConstant(Mask, dl, MVT::i64), Dest);
      }
    }
    if (RHSC && RHSC->getSExtValue() == -1 && CC == ISD::SETGT &&
        LHS.getOpcode() != ISD::AND && ProduceNonFlagSettingCondBr) {
      // Don't combine AND since emitComparison converts the AND to an ANDS
      // (a.k.a. TST) and the test in the test bit and branch instruction
      // becomes redundant.  This would also increase register pressure.
      uint64_t Mask = LHS.getValueSizeInBits() - 1;
      return DAG.getNode(AArch64ISD::TBZ, dl, MVT::Other, Chain, LHS,
                         DAG.getConstant(Mask, dl, MVT::i64), Dest);
    }

    SDValue CCVal;
    SDValue Cmp = getAArch64Cmp(LHS, RHS, CC, CCVal, DAG, dl);
    return DAG.getNode(AArch64ISD::BRCOND, dl, MVT::Other, Chain, Dest, CCVal,
                       Cmp);
  }

  assert(LHS.getValueType() == MVT::f16 || LHS.getValueType() == MVT::f32 ||
         LHS.getValueType() == MVT::f64);

  // Unfortunately, the mapping of LLVM FP CC's onto AArch64 CC's isn't totally
  // clean.  Some of them require two branches to implement.
  SDValue Cmp = emitComparison(LHS, RHS, CC, dl, DAG);
  AArch64CC::CondCode CC1, CC2;
  changeFPCCToAArch64CC(CC, CC1, CC2);
  SDValue CC1Val = DAG.getConstant(CC1, dl, MVT::i32);
  SDValue BR1 =
      DAG.getNode(AArch64ISD::BRCOND, dl, MVT::Other, Chain, Dest, CC1Val, Cmp);
  if (CC2 != AArch64CC::AL) {
    SDValue CC2Val = DAG.getConstant(CC2, dl, MVT::i32);
    return DAG.getNode(AArch64ISD::BRCOND, dl, MVT::Other, BR1, Dest, CC2Val,
                       Cmp);
  }

  return BR1;
}

SDValue AArch64TargetLowering::LowerFCOPYSIGN(SDValue Op,
                                              SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc DL(Op);

  SDValue In1 = Op.getOperand(0);
  SDValue In2 = Op.getOperand(1);
  EVT SrcVT = In2.getValueType();

  if (SrcVT.bitsLT(VT))
    In2 = DAG.getNode(ISD::FP_EXTEND, DL, VT, In2);
  else if (SrcVT.bitsGT(VT))
    In2 = DAG.getNode(ISD::FP_ROUND, DL, VT, In2, DAG.getIntPtrConstant(0, DL));

  EVT VecVT;
  uint64_t EltMask;
  SDValue VecVal1, VecVal2;

  auto setVecVal = [&] (int Idx) {
    if (!VT.isVector()) {
      VecVal1 = DAG.getTargetInsertSubreg(Idx, DL, VecVT,
                                          DAG.getUNDEF(VecVT), In1);
      VecVal2 = DAG.getTargetInsertSubreg(Idx, DL, VecVT,
                                          DAG.getUNDEF(VecVT), In2);
    } else {
      VecVal1 = DAG.getNode(ISD::BITCAST, DL, VecVT, In1);
      VecVal2 = DAG.getNode(ISD::BITCAST, DL, VecVT, In2);
    }
  };

  if (VT == MVT::f32 || VT == MVT::v2f32 || VT == MVT::v4f32) {
    VecVT = (VT == MVT::v2f32 ? MVT::v2i32 : MVT::v4i32);
    EltMask = 0x80000000ULL;
    setVecVal(AArch64::ssub);
  } else if (VT == MVT::f64 || VT == MVT::v2f64) {
    VecVT = MVT::v2i64;

    // We want to materialize a mask with the high bit set, but the AdvSIMD
    // immediate moves cannot materialize that in a single instruction for
    // 64-bit elements. Instead, materialize zero and then negate it.
    EltMask = 0;

    setVecVal(AArch64::dsub);
  } else if (VT == MVT::f16 || VT == MVT::v4f16 || VT == MVT::v8f16) {
    VecVT = (VT == MVT::v4f16 ? MVT::v4i16 : MVT::v8i16);
    EltMask = 0x8000ULL;
    setVecVal(AArch64::hsub);
  } else {
    llvm_unreachable("Invalid type for copysign!");
  }

  SDValue BuildVec = DAG.getConstant(EltMask, DL, VecVT);

  // If we couldn't materialize the mask above, then the mask vector will be
  // the zero vector, and we need to negate it here.
  if (VT == MVT::f64 || VT == MVT::v2f64) {
    BuildVec = DAG.getNode(ISD::BITCAST, DL, MVT::v2f64, BuildVec);
    BuildVec = DAG.getNode(ISD::FNEG, DL, MVT::v2f64, BuildVec);
    BuildVec = DAG.getNode(ISD::BITCAST, DL, MVT::v2i64, BuildVec);
  }

  SDValue Sel =
      DAG.getNode(AArch64ISD::BIT, DL, VecVT, VecVal1, VecVal2, BuildVec);

  if (VT == MVT::f16)
    return DAG.getTargetExtractSubreg(AArch64::hsub, DL, VT, Sel);
  if (VT == MVT::f32)
    return DAG.getTargetExtractSubreg(AArch64::ssub, DL, VT, Sel);
  else if (VT == MVT::f64)
    return DAG.getTargetExtractSubreg(AArch64::dsub, DL, VT, Sel);
  else
    return DAG.getNode(ISD::BITCAST, DL, VT, Sel);
}

SDValue AArch64TargetLowering::LowerCTPOP(SDValue Op, SelectionDAG &DAG) const {
  if (DAG.getMachineFunction().getFunction().hasFnAttribute(
          Attribute::NoImplicitFloat))
    return SDValue();

  if (!Subtarget->hasNEON())
    return SDValue();

  // While there is no integer popcount instruction, it can
  // be more efficiently lowered to the following sequence that uses
  // AdvSIMD registers/instructions as long as the copies to/from
  // the AdvSIMD registers are cheap.
  //  FMOV    D0, X0        // copy 64-bit int to vector, high bits zero'd
  //  CNT     V0.8B, V0.8B  // 8xbyte pop-counts
  //  ADDV    B0, V0.8B     // sum 8xbyte pop-counts
  //  UMOV    X0, V0.B[0]   // copy byte result back to integer reg
  SDValue Val = Op.getOperand(0);
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  if (VT == MVT::i32 || VT == MVT::i64) {
    if (VT == MVT::i32)
      Val = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Val);
    Val = DAG.getNode(ISD::BITCAST, DL, MVT::v8i8, Val);

    SDValue CtPop = DAG.getNode(ISD::CTPOP, DL, MVT::v8i8, Val);
    SDValue UaddLV = DAG.getNode(
        ISD::INTRINSIC_WO_CHAIN, DL, MVT::i32,
        DAG.getConstant(Intrinsic::aarch64_neon_uaddlv, DL, MVT::i32), CtPop);

    if (VT == MVT::i64)
      UaddLV = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, UaddLV);
    return UaddLV;
  }

  assert((VT == MVT::v1i64 || VT == MVT::v2i64 || VT == MVT::v2i32 ||
          VT == MVT::v4i32 || VT == MVT::v4i16 || VT == MVT::v8i16) &&
         "Unexpected type for custom ctpop lowering");

  EVT VT8Bit = VT.is64BitVector() ? MVT::v8i8 : MVT::v16i8;
  Val = DAG.getBitcast(VT8Bit, Val);
  Val = DAG.getNode(ISD::CTPOP, DL, VT8Bit, Val);

  // Widen v8i8/v16i8 CTPOP result to VT by repeatedly widening pairwise adds.
  unsigned EltSize = 8;
  unsigned NumElts = VT.is64BitVector() ? 8 : 16;
  while (EltSize != VT.getScalarSizeInBits()) {
    EltSize *= 2;
    NumElts /= 2;
    MVT WidenVT = MVT::getVectorVT(MVT::getIntegerVT(EltSize), NumElts);
    Val = DAG.getNode(
        ISD::INTRINSIC_WO_CHAIN, DL, WidenVT,
        DAG.getConstant(Intrinsic::aarch64_neon_uaddlp, DL, MVT::i32), Val);
  }

  return Val;
}

SDValue AArch64TargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {

  if (Op.getValueType().isVector())
    return LowerVSETCC(Op, DAG);

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc dl(Op);

  // We chose ZeroOrOneBooleanContents, so use zero and one.
  EVT VT = Op.getValueType();
  SDValue TVal = DAG.getConstant(1, dl, VT);
  SDValue FVal = DAG.getConstant(0, dl, VT);

  // Handle f128 first, since one possible outcome is a normal integer
  // comparison which gets picked up by the next if statement.
  if (LHS.getValueType() == MVT::f128) {
    softenSetCCOperands(DAG, MVT::f128, LHS, RHS, CC, dl);

    // If softenSetCCOperands returned a scalar, use it.
    if (!RHS.getNode()) {
      assert(LHS.getValueType() == Op.getValueType() &&
             "Unexpected setcc expansion!");
      return LHS;
    }
  }

  if (LHS.getValueType().isInteger()) {
    SDValue CCVal;
    SDValue Cmp =
        getAArch64Cmp(LHS, RHS, ISD::getSetCCInverse(CC, true), CCVal, DAG, dl);

    // Note that we inverted the condition above, so we reverse the order of
    // the true and false operands here.  This will allow the setcc to be
    // matched to a single CSINC instruction.
    return DAG.getNode(AArch64ISD::CSEL, dl, VT, FVal, TVal, CCVal, Cmp);
  }

  // Now we know we're dealing with FP values.
  assert(LHS.getValueType() == MVT::f16 || LHS.getValueType() == MVT::f32 ||
         LHS.getValueType() == MVT::f64);

  // If that fails, we'll need to perform an FCMP + CSEL sequence.  Go ahead
  // and do the comparison.
  SDValue Cmp = emitComparison(LHS, RHS, CC, dl, DAG);

  AArch64CC::CondCode CC1, CC2;
  changeFPCCToAArch64CC(CC, CC1, CC2);
  if (CC2 == AArch64CC::AL) {
    changeFPCCToAArch64CC(ISD::getSetCCInverse(CC, false), CC1, CC2);
    SDValue CC1Val = DAG.getConstant(CC1, dl, MVT::i32);

    // Note that we inverted the condition above, so we reverse the order of
    // the true and false operands here.  This will allow the setcc to be
    // matched to a single CSINC instruction.
    return DAG.getNode(AArch64ISD::CSEL, dl, VT, FVal, TVal, CC1Val, Cmp);
  } else {
    // Unfortunately, the mapping of LLVM FP CC's onto AArch64 CC's isn't
    // totally clean.  Some of them require two CSELs to implement.  As is in
    // this case, we emit the first CSEL and then emit a second using the output
    // of the first as the RHS.  We're effectively OR'ing the two CC's together.

    // FIXME: It would be nice if we could match the two CSELs to two CSINCs.
    SDValue CC1Val = DAG.getConstant(CC1, dl, MVT::i32);
    SDValue CS1 =
        DAG.getNode(AArch64ISD::CSEL, dl, VT, TVal, FVal, CC1Val, Cmp);

    SDValue CC2Val = DAG.getConstant(CC2, dl, MVT::i32);
    return DAG.getNode(AArch64ISD::CSEL, dl, VT, TVal, CS1, CC2Val, Cmp);
  }
}

SDValue AArch64TargetLowering::LowerSELECT_CC(ISD::CondCode CC, SDValue LHS,
                                              SDValue RHS, SDValue TVal,
                                              SDValue FVal, const SDLoc &dl,
                                              SelectionDAG &DAG) const {
  // Handle f128 first, because it will result in a comparison of some RTLIB
  // call result against zero.
  if (LHS.getValueType() == MVT::f128) {
    softenSetCCOperands(DAG, MVT::f128, LHS, RHS, CC, dl);

    // If softenSetCCOperands returned a scalar, we need to compare the result
    // against zero to select between true and false values.
    if (!RHS.getNode()) {
      RHS = DAG.getConstant(0, dl, LHS.getValueType());
      CC = ISD::SETNE;
    }
  }

  // Also handle f16, for which we need to do a f32 comparison.
  if (LHS.getValueType() == MVT::f16 && !Subtarget->hasFullFP16()) {
    LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f32, LHS);
    RHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f32, RHS);
  }

  // Next, handle integers.
  if (LHS.getValueType().isInteger()) {
    assert((LHS.getValueType() == RHS.getValueType()) &&
           (LHS.getValueType() == MVT::i32 || LHS.getValueType() == MVT::i64));

    unsigned Opcode = AArch64ISD::CSEL;

    // If both the TVal and the FVal are constants, see if we can swap them in
    // order to for a CSINV or CSINC out of them.
    ConstantSDNode *CFVal = dyn_cast<ConstantSDNode>(FVal);
    ConstantSDNode *CTVal = dyn_cast<ConstantSDNode>(TVal);

    if (CTVal && CFVal && CTVal->isAllOnesValue() && CFVal->isNullValue()) {
      std::swap(TVal, FVal);
      std::swap(CTVal, CFVal);
      CC = ISD::getSetCCInverse(CC, true);
    } else if (CTVal && CFVal && CTVal->isOne() && CFVal->isNullValue()) {
      std::swap(TVal, FVal);
      std::swap(CTVal, CFVal);
      CC = ISD::getSetCCInverse(CC, true);
    } else if (TVal.getOpcode() == ISD::XOR) {
      // If TVal is a NOT we want to swap TVal and FVal so that we can match
      // with a CSINV rather than a CSEL.
      if (isAllOnesConstant(TVal.getOperand(1))) {
        std::swap(TVal, FVal);
        std::swap(CTVal, CFVal);
        CC = ISD::getSetCCInverse(CC, true);
      }
    } else if (TVal.getOpcode() == ISD::SUB) {
      // If TVal is a negation (SUB from 0) we want to swap TVal and FVal so
      // that we can match with a CSNEG rather than a CSEL.
      if (isNullConstant(TVal.getOperand(0))) {
        std::swap(TVal, FVal);
        std::swap(CTVal, CFVal);
        CC = ISD::getSetCCInverse(CC, true);
      }
    } else if (CTVal && CFVal) {
      const int64_t TrueVal = CTVal->getSExtValue();
      const int64_t FalseVal = CFVal->getSExtValue();
      bool Swap = false;

      // If both TVal and FVal are constants, see if FVal is the
      // inverse/negation/increment of TVal and generate a CSINV/CSNEG/CSINC
      // instead of a CSEL in that case.
      if (TrueVal == ~FalseVal) {
        Opcode = AArch64ISD::CSINV;
      } else if (TrueVal == -FalseVal) {
        Opcode = AArch64ISD::CSNEG;
      } else if (TVal.getValueType() == MVT::i32) {
        // If our operands are only 32-bit wide, make sure we use 32-bit
        // arithmetic for the check whether we can use CSINC. This ensures that
        // the addition in the check will wrap around properly in case there is
        // an overflow (which would not be the case if we do the check with
        // 64-bit arithmetic).
        const uint32_t TrueVal32 = CTVal->getZExtValue();
        const uint32_t FalseVal32 = CFVal->getZExtValue();

        if ((TrueVal32 == FalseVal32 + 1) || (TrueVal32 + 1 == FalseVal32)) {
          Opcode = AArch64ISD::CSINC;

          if (TrueVal32 > FalseVal32) {
            Swap = true;
          }
        }
        // 64-bit check whether we can use CSINC.
      } else if ((TrueVal == FalseVal + 1) || (TrueVal + 1 == FalseVal)) {
        Opcode = AArch64ISD::CSINC;

        if (TrueVal > FalseVal) {
          Swap = true;
        }
      }

      // Swap TVal and FVal if necessary.
      if (Swap) {
        std::swap(TVal, FVal);
        std::swap(CTVal, CFVal);
        CC = ISD::getSetCCInverse(CC, true);
      }

      if (Opcode != AArch64ISD::CSEL) {
        // Drop FVal since we can get its value by simply inverting/negating
        // TVal.
        FVal = TVal;
      }
    }

    // Avoid materializing a constant when possible by reusing a known value in
    // a register.  However, don't perform this optimization if the known value
    // is one, zero or negative one in the case of a CSEL.  We can always
    // materialize these values using CSINC, CSEL and CSINV with wzr/xzr as the
    // FVal, respectively.
    ConstantSDNode *RHSVal = dyn_cast<ConstantSDNode>(RHS);
    if (Opcode == AArch64ISD::CSEL && RHSVal && !RHSVal->isOne() &&
        !RHSVal->isNullValue() && !RHSVal->isAllOnesValue()) {
      AArch64CC::CondCode AArch64CC = changeIntCCToAArch64CC(CC);
      // Transform "a == C ? C : x" to "a == C ? a : x" and "a != C ? x : C" to
      // "a != C ? x : a" to avoid materializing C.
      if (CTVal && CTVal == RHSVal && AArch64CC == AArch64CC::EQ)
        TVal = LHS;
      else if (CFVal && CFVal == RHSVal && AArch64CC == AArch64CC::NE)
        FVal = LHS;
    } else if (Opcode == AArch64ISD::CSNEG && RHSVal && RHSVal->isOne()) {
      assert (CTVal && CFVal && "Expected constant operands for CSNEG.");
      // Use a CSINV to transform "a == C ? 1 : -1" to "a == C ? a : -1" to
      // avoid materializing C.
      AArch64CC::CondCode AArch64CC = changeIntCCToAArch64CC(CC);
      if (CTVal == RHSVal && AArch64CC == AArch64CC::EQ) {
        Opcode = AArch64ISD::CSINV;
        TVal = LHS;
        FVal = DAG.getConstant(0, dl, FVal.getValueType());
      }
    }

    SDValue CCVal;
    SDValue Cmp = getAArch64Cmp(LHS, RHS, CC, CCVal, DAG, dl);
    EVT VT = TVal.getValueType();
    return DAG.getNode(Opcode, dl, VT, TVal, FVal, CCVal, Cmp);
  }

  // Now we know we're dealing with FP values.
  assert(LHS.getValueType() == MVT::f16 || LHS.getValueType() == MVT::f32 ||
         LHS.getValueType() == MVT::f64);
  assert(LHS.getValueType() == RHS.getValueType());
  EVT VT = TVal.getValueType();
  SDValue Cmp = emitComparison(LHS, RHS, CC, dl, DAG);

  // Unfortunately, the mapping of LLVM FP CC's onto AArch64 CC's isn't totally
  // clean.  Some of them require two CSELs to implement.
  AArch64CC::CondCode CC1, CC2;
  changeFPCCToAArch64CC(CC, CC1, CC2);

  if (DAG.getTarget().Options.UnsafeFPMath) {
    // Transform "a == 0.0 ? 0.0 : x" to "a == 0.0 ? a : x" and
    // "a != 0.0 ? x : 0.0" to "a != 0.0 ? x : a" to avoid materializing 0.0.
    ConstantFPSDNode *RHSVal = dyn_cast<ConstantFPSDNode>(RHS);
    if (RHSVal && RHSVal->isZero()) {
      ConstantFPSDNode *CFVal = dyn_cast<ConstantFPSDNode>(FVal);
      ConstantFPSDNode *CTVal = dyn_cast<ConstantFPSDNode>(TVal);

      if ((CC == ISD::SETEQ || CC == ISD::SETOEQ || CC == ISD::SETUEQ) &&
          CTVal && CTVal->isZero() && TVal.getValueType() == LHS.getValueType())
        TVal = LHS;
      else if ((CC == ISD::SETNE || CC == ISD::SETONE || CC == ISD::SETUNE) &&
               CFVal && CFVal->isZero() &&
               FVal.getValueType() == LHS.getValueType())
        FVal = LHS;
    }
  }

  // Emit first, and possibly only, CSEL.
  SDValue CC1Val = DAG.getConstant(CC1, dl, MVT::i32);
  SDValue CS1 = DAG.getNode(AArch64ISD::CSEL, dl, VT, TVal, FVal, CC1Val, Cmp);

  // If we need a second CSEL, emit it, using the output of the first as the
  // RHS.  We're effectively OR'ing the two CC's together.
  if (CC2 != AArch64CC::AL) {
    SDValue CC2Val = DAG.getConstant(CC2, dl, MVT::i32);
    return DAG.getNode(AArch64ISD::CSEL, dl, VT, TVal, CS1, CC2Val, Cmp);
  }

  // Otherwise, return the output of the first CSEL.
  return CS1;
}

SDValue AArch64TargetLowering::LowerSELECT_CC(SDValue Op,
                                              SelectionDAG &DAG) const {
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TVal = Op.getOperand(2);
  SDValue FVal = Op.getOperand(3);
  SDLoc DL(Op);
  return LowerSELECT_CC(CC, LHS, RHS, TVal, FVal, DL, DAG);
}

SDValue AArch64TargetLowering::LowerSELECT(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue CCVal = Op->getOperand(0);
  SDValue TVal = Op->getOperand(1);
  SDValue FVal = Op->getOperand(2);
  SDLoc DL(Op);

  // Optimize {s|u}{add|sub|mul}.with.overflow feeding into a select
  // instruction.
  if (isOverflowIntrOpRes(CCVal)) {
    // Only lower legal XALUO ops.
    if (!DAG.getTargetLoweringInfo().isTypeLegal(CCVal->getValueType(0)))
      return SDValue();

    AArch64CC::CondCode OFCC;
    SDValue Value, Overflow;
    std::tie(Value, Overflow) = getAArch64XALUOOp(OFCC, CCVal.getValue(0), DAG);
    SDValue CCVal = DAG.getConstant(OFCC, DL, MVT::i32);

    return DAG.getNode(AArch64ISD::CSEL, DL, Op.getValueType(), TVal, FVal,
                       CCVal, Overflow);
  }

  // Lower it the same way as we would lower a SELECT_CC node.
  ISD::CondCode CC;
  SDValue LHS, RHS;
  if (CCVal.getOpcode() == ISD::SETCC) {
    LHS = CCVal.getOperand(0);
    RHS = CCVal.getOperand(1);
    CC = cast<CondCodeSDNode>(CCVal->getOperand(2))->get();
  } else {
    LHS = CCVal;
    RHS = DAG.getConstant(0, DL, CCVal.getValueType());
    CC = ISD::SETNE;
  }
  return LowerSELECT_CC(CC, LHS, RHS, TVal, FVal, DL, DAG);
}

SDValue AArch64TargetLowering::LowerJumpTable(SDValue Op,
                                              SelectionDAG &DAG) const {
  // Jump table entries as PC relative offsets. No additional tweaking
  // is necessary here. Just get the address of the jump table.
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);

  if (getTargetMachine().getCodeModel() == CodeModel::Large &&
      !Subtarget->isTargetMachO()) {
    return getAddrLarge(JT, DAG);
  } else if (getTargetMachine().getCodeModel() == CodeModel::Tiny) {
    return getAddrTiny(JT, DAG);
  }
  return getAddr(JT, DAG);
}

SDValue AArch64TargetLowering::LowerBR_JT(SDValue Op,
                                          SelectionDAG &DAG) const {
  // Jump table entries as PC relative offsets. No additional tweaking
  // is necessary here. Just get the address of the jump table.
  SDLoc DL(Op);
  SDValue JT = Op.getOperand(1);
  SDValue Entry = Op.getOperand(2);
  int JTI = cast<JumpTableSDNode>(JT.getNode())->getIndex();

  SDNode *Dest =
      DAG.getMachineNode(AArch64::JumpTableDest32, DL, MVT::i64, MVT::i64, JT,
                         Entry, DAG.getTargetJumpTable(JTI, MVT::i32));
  return DAG.getNode(ISD::BRIND, DL, MVT::Other, Op.getOperand(0),
                     SDValue(Dest, 0));
}

SDValue AArch64TargetLowering::LowerConstantPool(SDValue Op,
                                                 SelectionDAG &DAG) const {
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);

  if (getTargetMachine().getCodeModel() == CodeModel::Large) {
    // Use the GOT for the large code model on iOS.
    if (Subtarget->isTargetMachO()) {
      return getGOT(CP, DAG);
    }
    return getAddrLarge(CP, DAG);
  } else if (getTargetMachine().getCodeModel() == CodeModel::Tiny) {
    return getAddrTiny(CP, DAG);
  } else {
    return getAddr(CP, DAG);
  }
}

SDValue AArch64TargetLowering::LowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  BlockAddressSDNode *BA = cast<BlockAddressSDNode>(Op);
  if (getTargetMachine().getCodeModel() == CodeModel::Large &&
      !Subtarget->isTargetMachO()) {
    return getAddrLarge(BA, DAG);
  } else if (getTargetMachine().getCodeModel() == CodeModel::Tiny) {
    return getAddrTiny(BA, DAG);
  }
  return getAddr(BA, DAG);
}

SDValue AArch64TargetLowering::LowerDarwin_VASTART(SDValue Op,
                                                 SelectionDAG &DAG) const {
  AArch64FunctionInfo *FuncInfo =
      DAG.getMachineFunction().getInfo<AArch64FunctionInfo>();

  SDLoc DL(Op);
  SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsStackIndex(),
                                 getPointerTy(DAG.getDataLayout()));
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FR, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue AArch64TargetLowering::LowerWin64_VASTART(SDValue Op,
                                                  SelectionDAG &DAG) const {
  AArch64FunctionInfo *FuncInfo =
      DAG.getMachineFunction().getInfo<AArch64FunctionInfo>();

  SDLoc DL(Op);
  SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsGPRSize() > 0
                                     ? FuncInfo->getVarArgsGPRIndex()
                                     : FuncInfo->getVarArgsStackIndex(),
                                 getPointerTy(DAG.getDataLayout()));
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FR, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue AArch64TargetLowering::LowerAAPCS_VASTART(SDValue Op,
                                                SelectionDAG &DAG) const {
  // The layout of the va_list struct is specified in the AArch64 Procedure Call
  // Standard, section B.3.
  MachineFunction &MF = DAG.getMachineFunction();
  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  auto PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(Op);

  SDValue Chain = Op.getOperand(0);
  SDValue VAList = Op.getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  SmallVector<SDValue, 4> MemOps;

  // void *__stack at offset 0
  SDValue Stack = DAG.getFrameIndex(FuncInfo->getVarArgsStackIndex(), PtrVT);
  MemOps.push_back(DAG.getStore(Chain, DL, Stack, VAList,
                                MachinePointerInfo(SV), /* Alignment = */ 8));

  // void *__gr_top at offset 8
  int GPRSize = FuncInfo->getVarArgsGPRSize();
  if (GPRSize > 0) {
    SDValue GRTop, GRTopAddr;

    GRTopAddr =
        DAG.getNode(ISD::ADD, DL, PtrVT, VAList, DAG.getConstant(8, DL, PtrVT));

    GRTop = DAG.getFrameIndex(FuncInfo->getVarArgsGPRIndex(), PtrVT);
    GRTop = DAG.getNode(ISD::ADD, DL, PtrVT, GRTop,
                        DAG.getConstant(GPRSize, DL, PtrVT));

    MemOps.push_back(DAG.getStore(Chain, DL, GRTop, GRTopAddr,
                                  MachinePointerInfo(SV, 8),
                                  /* Alignment = */ 8));
  }

  // void *__vr_top at offset 16
  int FPRSize = FuncInfo->getVarArgsFPRSize();
  if (FPRSize > 0) {
    SDValue VRTop, VRTopAddr;
    VRTopAddr = DAG.getNode(ISD::ADD, DL, PtrVT, VAList,
                            DAG.getConstant(16, DL, PtrVT));

    VRTop = DAG.getFrameIndex(FuncInfo->getVarArgsFPRIndex(), PtrVT);
    VRTop = DAG.getNode(ISD::ADD, DL, PtrVT, VRTop,
                        DAG.getConstant(FPRSize, DL, PtrVT));

    MemOps.push_back(DAG.getStore(Chain, DL, VRTop, VRTopAddr,
                                  MachinePointerInfo(SV, 16),
                                  /* Alignment = */ 8));
  }

  // int __gr_offs at offset 24
  SDValue GROffsAddr =
      DAG.getNode(ISD::ADD, DL, PtrVT, VAList, DAG.getConstant(24, DL, PtrVT));
  MemOps.push_back(DAG.getStore(
      Chain, DL, DAG.getConstant(-GPRSize, DL, MVT::i32), GROffsAddr,
      MachinePointerInfo(SV, 24), /* Alignment = */ 4));

  // int __vr_offs at offset 28
  SDValue VROffsAddr =
      DAG.getNode(ISD::ADD, DL, PtrVT, VAList, DAG.getConstant(28, DL, PtrVT));
  MemOps.push_back(DAG.getStore(
      Chain, DL, DAG.getConstant(-FPRSize, DL, MVT::i32), VROffsAddr,
      MachinePointerInfo(SV, 28), /* Alignment = */ 4));

  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOps);
}

SDValue AArch64TargetLowering::LowerVASTART(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  if (Subtarget->isCallingConvWin64(MF.getFunction().getCallingConv()))
    return LowerWin64_VASTART(Op, DAG);
  else if (Subtarget->isTargetDarwin())
    return LowerDarwin_VASTART(Op, DAG);
  else
    return LowerAAPCS_VASTART(Op, DAG);
}

SDValue AArch64TargetLowering::LowerVACOPY(SDValue Op,
                                           SelectionDAG &DAG) const {
  // AAPCS has three pointers and two ints (= 32 bytes), Darwin has single
  // pointer.
  SDLoc DL(Op);
  unsigned VaListSize =
      Subtarget->isTargetDarwin() || Subtarget->isTargetWindows() ? 8 : 32;
  const Value *DestSV = cast<SrcValueSDNode>(Op.getOperand(3))->getValue();
  const Value *SrcSV = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();

  return DAG.getMemcpy(Op.getOperand(0), DL, Op.getOperand(1),
                       Op.getOperand(2),
                       DAG.getConstant(VaListSize, DL, MVT::i32),
                       8, false, false, false, MachinePointerInfo(DestSV),
                       MachinePointerInfo(SrcSV));
}

SDValue AArch64TargetLowering::LowerVAARG(SDValue Op, SelectionDAG &DAG) const {
  assert(Subtarget->isTargetDarwin() &&
         "automatic va_arg instruction only works on Darwin");

  const Value *V = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Addr = Op.getOperand(1);
  unsigned Align = Op.getConstantOperandVal(3);
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue VAList = DAG.getLoad(PtrVT, DL, Chain, Addr, MachinePointerInfo(V));
  Chain = VAList.getValue(1);

  if (Align > 8) {
    assert(((Align & (Align - 1)) == 0) && "Expected Align to be a power of 2");
    VAList = DAG.getNode(ISD::ADD, DL, PtrVT, VAList,
                         DAG.getConstant(Align - 1, DL, PtrVT));
    VAList = DAG.getNode(ISD::AND, DL, PtrVT, VAList,
                         DAG.getConstant(-(int64_t)Align, DL, PtrVT));
  }

  Type *ArgTy = VT.getTypeForEVT(*DAG.getContext());
  uint64_t ArgSize = DAG.getDataLayout().getTypeAllocSize(ArgTy);

  // Scalar integer and FP values smaller than 64 bits are implicitly extended
  // up to 64 bits.  At the very least, we have to increase the striding of the
  // vaargs list to match this, and for FP values we need to introduce
  // FP_ROUND nodes as well.
  if (VT.isInteger() && !VT.isVector())
    ArgSize = 8;
  bool NeedFPTrunc = false;
  if (VT.isFloatingPoint() && !VT.isVector() && VT != MVT::f64) {
    ArgSize = 8;
    NeedFPTrunc = true;
  }

  // Increment the pointer, VAList, to the next vaarg
  SDValue VANext = DAG.getNode(ISD::ADD, DL, PtrVT, VAList,
                               DAG.getConstant(ArgSize, DL, PtrVT));
  // Store the incremented VAList to the legalized pointer
  SDValue APStore =
      DAG.getStore(Chain, DL, VANext, Addr, MachinePointerInfo(V));

  // Load the actual argument out of the pointer VAList
  if (NeedFPTrunc) {
    // Load the value as an f64.
    SDValue WideFP =
        DAG.getLoad(MVT::f64, DL, APStore, VAList, MachinePointerInfo());
    // Round the value down to an f32.
    SDValue NarrowFP = DAG.getNode(ISD::FP_ROUND, DL, VT, WideFP.getValue(0),
                                   DAG.getIntPtrConstant(1, DL));
    SDValue Ops[] = { NarrowFP, WideFP.getValue(1) };
    // Merge the rounded value with the chain output of the load.
    return DAG.getMergeValues(Ops, DL);
  }

  return DAG.getLoad(VT, DL, APStore, VAList, MachinePointerInfo());
}

SDValue AArch64TargetLowering::LowerFRAMEADDR(SDValue Op,
                                              SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  SDValue FrameAddr =
      DAG.getCopyFromReg(DAG.getEntryNode(), DL, AArch64::FP, VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, DL, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

SDValue AArch64TargetLowering::LowerSPONENTRY(SDValue Op,
                                              SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();

  EVT VT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(Op);
  int FI = MFI.CreateFixedObject(4, 0, false);
  return DAG.getFrameIndex(FI, VT);
}

// FIXME? Maybe this could be a TableGen attribute on some registers and
// this table could be generated automatically from RegInfo.
unsigned AArch64TargetLowering::getRegisterByName(const char* RegName, EVT VT,
                                                  SelectionDAG &DAG) const {
  unsigned Reg = StringSwitch<unsigned>(RegName)
                       .Case("sp", AArch64::SP)
                       .Case("x1", AArch64::X1)
                       .Case("w1", AArch64::W1)
                       .Case("x2", AArch64::X2)
                       .Case("w2", AArch64::W2)
                       .Case("x3", AArch64::X3)
                       .Case("w3", AArch64::W3)
                       .Case("x4", AArch64::X4)
                       .Case("w4", AArch64::W4)
                       .Case("x5", AArch64::X5)
                       .Case("w5", AArch64::W5)
                       .Case("x6", AArch64::X6)
                       .Case("w6", AArch64::W6)
                       .Case("x7", AArch64::X7)
                       .Case("w7", AArch64::W7)
                       .Case("x18", AArch64::X18)
                       .Case("w18", AArch64::W18)
                       .Case("x20", AArch64::X20)
                       .Case("w20", AArch64::W20)
                       .Default(0);
  if (((Reg == AArch64::X1 || Reg == AArch64::W1) &&
      !Subtarget->isXRegisterReserved(1)) ||
      ((Reg == AArch64::X2 || Reg == AArch64::W2) &&
      !Subtarget->isXRegisterReserved(2)) ||
      ((Reg == AArch64::X3 || Reg == AArch64::W3) &&
      !Subtarget->isXRegisterReserved(3)) ||
      ((Reg == AArch64::X4 || Reg == AArch64::W4) &&
      !Subtarget->isXRegisterReserved(4)) ||
      ((Reg == AArch64::X5 || Reg == AArch64::W5) &&
      !Subtarget->isXRegisterReserved(5)) ||
      ((Reg == AArch64::X6 || Reg == AArch64::W6) &&
      !Subtarget->isXRegisterReserved(6)) ||
      ((Reg == AArch64::X7 || Reg == AArch64::W7) &&
      !Subtarget->isXRegisterReserved(7)) ||
      ((Reg == AArch64::X18 || Reg == AArch64::W18) &&
      !Subtarget->isXRegisterReserved(18)) ||
      ((Reg == AArch64::X20 || Reg == AArch64::W20) &&
      !Subtarget->isXRegisterReserved(20)))
    Reg = 0;
  if (Reg)
    return Reg;
  report_fatal_error(Twine("Invalid register name \""
                              + StringRef(RegName)  + "\"."));
}

SDValue AArch64TargetLowering::LowerADDROFRETURNADDR(SDValue Op,
                                                     SelectionDAG &DAG) const {
  DAG.getMachineFunction().getFrameInfo().setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc DL(Op);

  SDValue FrameAddr =
      DAG.getCopyFromReg(DAG.getEntryNode(), DL, AArch64::FP, VT);
  SDValue Offset = DAG.getConstant(8, DL, getPointerTy(DAG.getDataLayout()));

  return DAG.getNode(ISD::ADD, DL, VT, FrameAddr, Offset);
}

SDValue AArch64TargetLowering::LowerRETURNADDR(SDValue Op,
                                               SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  if (Depth) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset = DAG.getConstant(8, DL, getPointerTy(DAG.getDataLayout()));
    return DAG.getLoad(VT, DL, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, DL, VT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Return LR, which contains the return address. Mark it an implicit live-in.
  unsigned Reg = MF.addLiveIn(AArch64::LR, &AArch64::GPR64RegClass);
  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, VT);
}

/// LowerShiftRightParts - Lower SRA_PARTS, which returns two
/// i64 values and take a 2 x i64 value to shift plus a shift amount.
SDValue AArch64TargetLowering::LowerShiftRightParts(SDValue Op,
                                                    SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt = Op.getOperand(2);
  unsigned Opc = (Op.getOpcode() == ISD::SRA_PARTS) ? ISD::SRA : ISD::SRL;

  assert(Op.getOpcode() == ISD::SRA_PARTS || Op.getOpcode() == ISD::SRL_PARTS);

  SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i64,
                                 DAG.getConstant(VTBits, dl, MVT::i64), ShAmt);
  SDValue HiBitsForLo = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, RevShAmt);

  // Unfortunately, if ShAmt == 0, we just calculated "(SHL ShOpHi, 64)" which
  // is "undef". We wanted 0, so CSEL it directly.
  SDValue Cmp = emitComparison(ShAmt, DAG.getConstant(0, dl, MVT::i64),
                               ISD::SETEQ, dl, DAG);
  SDValue CCVal = DAG.getConstant(AArch64CC::EQ, dl, MVT::i32);
  HiBitsForLo =
      DAG.getNode(AArch64ISD::CSEL, dl, VT, DAG.getConstant(0, dl, MVT::i64),
                  HiBitsForLo, CCVal, Cmp);

  SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i64, ShAmt,
                                   DAG.getConstant(VTBits, dl, MVT::i64));

  SDValue LoBitsForLo = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, ShAmt);
  SDValue LoForNormalShift =
      DAG.getNode(ISD::OR, dl, VT, LoBitsForLo, HiBitsForLo);

  Cmp = emitComparison(ExtraShAmt, DAG.getConstant(0, dl, MVT::i64), ISD::SETGE,
                       dl, DAG);
  CCVal = DAG.getConstant(AArch64CC::GE, dl, MVT::i32);
  SDValue LoForBigShift = DAG.getNode(Opc, dl, VT, ShOpHi, ExtraShAmt);
  SDValue Lo = DAG.getNode(AArch64ISD::CSEL, dl, VT, LoForBigShift,
                           LoForNormalShift, CCVal, Cmp);

  // AArch64 shifts larger than the register width are wrapped rather than
  // clamped, so we can't just emit "hi >> x".
  SDValue HiForNormalShift = DAG.getNode(Opc, dl, VT, ShOpHi, ShAmt);
  SDValue HiForBigShift =
      Opc == ISD::SRA
          ? DAG.getNode(Opc, dl, VT, ShOpHi,
                        DAG.getConstant(VTBits - 1, dl, MVT::i64))
          : DAG.getConstant(0, dl, VT);
  SDValue Hi = DAG.getNode(AArch64ISD::CSEL, dl, VT, HiForBigShift,
                           HiForNormalShift, CCVal, Cmp);

  SDValue Ops[2] = { Lo, Hi };
  return DAG.getMergeValues(Ops, dl);
}

/// LowerShiftLeftParts - Lower SHL_PARTS, which returns two
/// i64 values and take a 2 x i64 value to shift plus a shift amount.
SDValue AArch64TargetLowering::LowerShiftLeftParts(SDValue Op,
                                                   SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt = Op.getOperand(2);

  assert(Op.getOpcode() == ISD::SHL_PARTS);
  SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i64,
                                 DAG.getConstant(VTBits, dl, MVT::i64), ShAmt);
  SDValue LoBitsForHi = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, RevShAmt);

  // Unfortunately, if ShAmt == 0, we just calculated "(SRL ShOpLo, 64)" which
  // is "undef". We wanted 0, so CSEL it directly.
  SDValue Cmp = emitComparison(ShAmt, DAG.getConstant(0, dl, MVT::i64),
                               ISD::SETEQ, dl, DAG);
  SDValue CCVal = DAG.getConstant(AArch64CC::EQ, dl, MVT::i32);
  LoBitsForHi =
      DAG.getNode(AArch64ISD::CSEL, dl, VT, DAG.getConstant(0, dl, MVT::i64),
                  LoBitsForHi, CCVal, Cmp);

  SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i64, ShAmt,
                                   DAG.getConstant(VTBits, dl, MVT::i64));
  SDValue HiBitsForHi = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, ShAmt);
  SDValue HiForNormalShift =
      DAG.getNode(ISD::OR, dl, VT, LoBitsForHi, HiBitsForHi);

  SDValue HiForBigShift = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ExtraShAmt);

  Cmp = emitComparison(ExtraShAmt, DAG.getConstant(0, dl, MVT::i64), ISD::SETGE,
                       dl, DAG);
  CCVal = DAG.getConstant(AArch64CC::GE, dl, MVT::i32);
  SDValue Hi = DAG.getNode(AArch64ISD::CSEL, dl, VT, HiForBigShift,
                           HiForNormalShift, CCVal, Cmp);

  // AArch64 shifts of larger than register sizes are wrapped rather than
  // clamped, so we can't just emit "lo << a" if a is too big.
  SDValue LoForBigShift = DAG.getConstant(0, dl, VT);
  SDValue LoForNormalShift = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ShAmt);
  SDValue Lo = DAG.getNode(AArch64ISD::CSEL, dl, VT, LoForBigShift,
                           LoForNormalShift, CCVal, Cmp);

  SDValue Ops[2] = { Lo, Hi };
  return DAG.getMergeValues(Ops, dl);
}

bool AArch64TargetLowering::isOffsetFoldingLegal(
    const GlobalAddressSDNode *GA) const {
  // Offsets are folded in the DAG combine rather than here so that we can
  // intelligently choose an offset based on the uses.
  return false;
}

bool AArch64TargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT) const {
  // We can materialize #0.0 as fmov $Rd, XZR for 64-bit and 32-bit cases.
  // FIXME: We should be able to handle f128 as well with a clever lowering.
  if (Imm.isPosZero() && (VT == MVT::f64 || VT == MVT::f32 ||
                          (VT == MVT::f16 && Subtarget->hasFullFP16()))) {
    LLVM_DEBUG(dbgs() << "Legal " << VT.getEVTString() << " imm value: 0\n");
    return true;
  }

  bool IsLegal = false;
  SmallString<128> ImmStrVal;
  Imm.toString(ImmStrVal);

  if (VT == MVT::f64)
    IsLegal = AArch64_AM::getFP64Imm(Imm) != -1;
  else if (VT == MVT::f32)
    IsLegal = AArch64_AM::getFP32Imm(Imm) != -1;
  else if (VT == MVT::f16 && Subtarget->hasFullFP16())
    IsLegal = AArch64_AM::getFP16Imm(Imm) != -1;

  if (IsLegal) {
    LLVM_DEBUG(dbgs() << "Legal " << VT.getEVTString()
                      << " imm value: " << ImmStrVal << "\n");
    return true;
  }

  LLVM_DEBUG(dbgs() << "Illegal " << VT.getEVTString()
                    << " imm value: " << ImmStrVal << "\n");
  return false;
}

//===----------------------------------------------------------------------===//
//                          AArch64 Optimization Hooks
//===----------------------------------------------------------------------===//

static SDValue getEstimate(const AArch64Subtarget *ST, unsigned Opcode,
                           SDValue Operand, SelectionDAG &DAG,
                           int &ExtraSteps) {
  EVT VT = Operand.getValueType();
  if (ST->hasNEON() &&
      (VT == MVT::f64 || VT == MVT::v1f64 || VT == MVT::v2f64 ||
       VT == MVT::f32 || VT == MVT::v1f32 ||
       VT == MVT::v2f32 || VT == MVT::v4f32)) {
    if (ExtraSteps == TargetLoweringBase::ReciprocalEstimate::Unspecified)
      // For the reciprocal estimates, convergence is quadratic, so the number
      // of digits is doubled after each iteration.  In ARMv8, the accuracy of
      // the initial estimate is 2^-8.  Thus the number of extra steps to refine
      // the result for float (23 mantissa bits) is 2 and for double (52
      // mantissa bits) is 3.
      ExtraSteps = VT.getScalarType() == MVT::f64 ? 3 : 2;

    return DAG.getNode(Opcode, SDLoc(Operand), VT, Operand);
  }

  return SDValue();
}

SDValue AArch64TargetLowering::getSqrtEstimate(SDValue Operand,
                                               SelectionDAG &DAG, int Enabled,
                                               int &ExtraSteps,
                                               bool &UseOneConst,
                                               bool Reciprocal) const {
  if (Enabled == ReciprocalEstimate::Enabled ||
      (Enabled == ReciprocalEstimate::Unspecified && Subtarget->useRSqrt()))
    if (SDValue Estimate = getEstimate(Subtarget, AArch64ISD::FRSQRTE, Operand,
                                       DAG, ExtraSteps)) {
      SDLoc DL(Operand);
      EVT VT = Operand.getValueType();

      SDNodeFlags Flags;
      Flags.setAllowReassociation(true);

      // Newton reciprocal square root iteration: E * 0.5 * (3 - X * E^2)
      // AArch64 reciprocal square root iteration instruction: 0.5 * (3 - M * N)
      for (int i = ExtraSteps; i > 0; --i) {
        SDValue Step = DAG.getNode(ISD::FMUL, DL, VT, Estimate, Estimate,
                                   Flags);
        Step = DAG.getNode(AArch64ISD::FRSQRTS, DL, VT, Operand, Step, Flags);
        Estimate = DAG.getNode(ISD::FMUL, DL, VT, Estimate, Step, Flags);
      }
      if (!Reciprocal) {
        EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                      VT);
        SDValue FPZero = DAG.getConstantFP(0.0, DL, VT);
        SDValue Eq = DAG.getSetCC(DL, CCVT, Operand, FPZero, ISD::SETEQ);

        Estimate = DAG.getNode(ISD::FMUL, DL, VT, Operand, Estimate, Flags);
        // Correct the result if the operand is 0.0.
        Estimate = DAG.getNode(VT.isVector() ? ISD::VSELECT : ISD::SELECT, DL,
                               VT, Eq, Operand, Estimate);
      }

      ExtraSteps = 0;
      return Estimate;
    }

  return SDValue();
}

SDValue AArch64TargetLowering::getRecipEstimate(SDValue Operand,
                                                SelectionDAG &DAG, int Enabled,
                                                int &ExtraSteps) const {
  if (Enabled == ReciprocalEstimate::Enabled)
    if (SDValue Estimate = getEstimate(Subtarget, AArch64ISD::FRECPE, Operand,
                                       DAG, ExtraSteps)) {
      SDLoc DL(Operand);
      EVT VT = Operand.getValueType();

      SDNodeFlags Flags;
      Flags.setAllowReassociation(true);

      // Newton reciprocal iteration: E * (2 - X * E)
      // AArch64 reciprocal iteration instruction: (2 - M * N)
      for (int i = ExtraSteps; i > 0; --i) {
        SDValue Step = DAG.getNode(AArch64ISD::FRECPS, DL, VT, Operand,
                                   Estimate, Flags);
        Estimate = DAG.getNode(ISD::FMUL, DL, VT, Estimate, Step, Flags);
      }

      ExtraSteps = 0;
      return Estimate;
    }

  return SDValue();
}

//===----------------------------------------------------------------------===//
//                          AArch64 Inline Assembly Support
//===----------------------------------------------------------------------===//

// Table of Constraints
// TODO: This is the current set of constraints supported by ARM for the
// compiler, not all of them may make sense.
//
// r - A general register
// w - An FP/SIMD register of some size in the range v0-v31
// x - An FP/SIMD register of some size in the range v0-v15
// I - Constant that can be used with an ADD instruction
// J - Constant that can be used with a SUB instruction
// K - Constant that can be used with a 32-bit logical instruction
// L - Constant that can be used with a 64-bit logical instruction
// M - Constant that can be used as a 32-bit MOV immediate
// N - Constant that can be used as a 64-bit MOV immediate
// Q - A memory reference with base register and no offset
// S - A symbolic address
// Y - Floating point constant zero
// Z - Integer constant zero
//
//   Note that general register operands will be output using their 64-bit x
// register name, whatever the size of the variable, unless the asm operand
// is prefixed by the %w modifier. Floating-point and SIMD register operands
// will be output with the v prefix unless prefixed by the %b, %h, %s, %d or
// %q modifier.
const char *AArch64TargetLowering::LowerXConstraint(EVT ConstraintVT) const {
  // At this point, we have to lower this constraint to something else, so we
  // lower it to an "r" or "w". However, by doing this we will force the result
  // to be in register, while the X constraint is much more permissive.
  //
  // Although we are correct (we are free to emit anything, without
  // constraints), we might break use cases that would expect us to be more
  // efficient and emit something else.
  if (!Subtarget->hasFPARMv8())
    return "r";

  if (ConstraintVT.isFloatingPoint())
    return "w";

  if (ConstraintVT.isVector() &&
     (ConstraintVT.getSizeInBits() == 64 ||
      ConstraintVT.getSizeInBits() == 128))
    return "w";

  return "r";
}

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
AArch64TargetLowering::ConstraintType
AArch64TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'z':
      return C_Other;
    case 'x':
    case 'w':
      return C_RegisterClass;
    // An address with a single base register. Due to the way we
    // currently handle addresses it is the same as 'r'.
    case 'Q':
      return C_Memory;
    case 'S': // A symbolic address
      return C_Other;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
AArch64TargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &info, const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
  // If we don't have a value, we can't do a match,
  // but allow it at the lowest weight.
  if (!CallOperandVal)
    return CW_Default;
  Type *type = CallOperandVal->getType();
  // Look at the constraint type.
  switch (*constraint) {
  default:
    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;
  case 'x':
  case 'w':
    if (type->isFloatingPointTy() || type->isVectorTy())
      weight = CW_Register;
    break;
  case 'z':
    weight = CW_Constant;
    break;
  }
  return weight;
}

std::pair<unsigned, const TargetRegisterClass *>
AArch64TargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      if (VT.getSizeInBits() == 64)
        return std::make_pair(0U, &AArch64::GPR64commonRegClass);
      return std::make_pair(0U, &AArch64::GPR32commonRegClass);
    case 'w':
      if (!Subtarget->hasFPARMv8())
        break;
      if (VT.getSizeInBits() == 16)
        return std::make_pair(0U, &AArch64::FPR16RegClass);
      if (VT.getSizeInBits() == 32)
        return std::make_pair(0U, &AArch64::FPR32RegClass);
      if (VT.getSizeInBits() == 64)
        return std::make_pair(0U, &AArch64::FPR64RegClass);
      if (VT.getSizeInBits() == 128)
        return std::make_pair(0U, &AArch64::FPR128RegClass);
      break;
    // The instructions that this constraint is designed for can
    // only take 128-bit registers so just use that regclass.
    case 'x':
      if (!Subtarget->hasFPARMv8())
        break;
      if (VT.getSizeInBits() == 128)
        return std::make_pair(0U, &AArch64::FPR128_loRegClass);
      break;
    }
  }
  if (StringRef("{cc}").equals_lower(Constraint))
    return std::make_pair(unsigned(AArch64::NZCV), &AArch64::CCRRegClass);

  // Use the default implementation in TargetLowering to convert the register
  // constraint into a member of a register class.
  std::pair<unsigned, const TargetRegisterClass *> Res;
  Res = TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);

  // Not found as a standard register?
  if (!Res.second) {
    unsigned Size = Constraint.size();
    if ((Size == 4 || Size == 5) && Constraint[0] == '{' &&
        tolower(Constraint[1]) == 'v' && Constraint[Size - 1] == '}') {
      int RegNo;
      bool Failed = Constraint.slice(2, Size - 1).getAsInteger(10, RegNo);
      if (!Failed && RegNo >= 0 && RegNo <= 31) {
        // v0 - v31 are aliases of q0 - q31 or d0 - d31 depending on size.
        // By default we'll emit v0-v31 for this unless there's a modifier where
        // we'll emit the correct register as well.
        if (VT != MVT::Other && VT.getSizeInBits() == 64) {
          Res.first = AArch64::FPR64RegClass.getRegister(RegNo);
          Res.second = &AArch64::FPR64RegClass;
        } else {
          Res.first = AArch64::FPR128RegClass.getRegister(RegNo);
          Res.second = &AArch64::FPR128RegClass;
        }
      }
    }
  }

  if (Res.second && !Subtarget->hasFPARMv8() &&
      !AArch64::GPR32allRegClass.hasSubClassEq(Res.second) &&
      !AArch64::GPR64allRegClass.hasSubClassEq(Res.second))
    return std::make_pair(0U, nullptr);

  return Res;
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void AArch64TargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, std::string &Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  SDValue Result;

  // Currently only support length 1 constraints.
  if (Constraint.length() != 1)
    return;

  char ConstraintLetter = Constraint[0];
  switch (ConstraintLetter) {
  default:
    break;

  // This set of constraints deal with valid constants for various instructions.
  // Validate and return a target constant for them if we can.
  case 'z': {
    // 'z' maps to xzr or wzr so it needs an input of 0.
    if (!isNullConstant(Op))
      return;

    if (Op.getValueType() == MVT::i64)
      Result = DAG.getRegister(AArch64::XZR, MVT::i64);
    else
      Result = DAG.getRegister(AArch64::WZR, MVT::i32);
    break;
  }
  case 'S': {
    // An absolute symbolic address or label reference.
    if (const GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(Op)) {
      Result = DAG.getTargetGlobalAddress(GA->getGlobal(), SDLoc(Op),
                                          GA->getValueType(0));
    } else if (const BlockAddressSDNode *BA =
                   dyn_cast<BlockAddressSDNode>(Op)) {
      Result =
          DAG.getTargetBlockAddress(BA->getBlockAddress(), BA->getValueType(0));
    } else if (const ExternalSymbolSDNode *ES =
                   dyn_cast<ExternalSymbolSDNode>(Op)) {
      Result =
          DAG.getTargetExternalSymbol(ES->getSymbol(), ES->getValueType(0));
    } else
      return;
    break;
  }

  case 'I':
  case 'J':
  case 'K':
  case 'L':
  case 'M':
  case 'N':
    ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op);
    if (!C)
      return;

    // Grab the value and do some validation.
    uint64_t CVal = C->getZExtValue();
    switch (ConstraintLetter) {
    // The I constraint applies only to simple ADD or SUB immediate operands:
    // i.e. 0 to 4095 with optional shift by 12
    // The J constraint applies only to ADD or SUB immediates that would be
    // valid when negated, i.e. if [an add pattern] were to be output as a SUB
    // instruction [or vice versa], in other words -1 to -4095 with optional
    // left shift by 12.
    case 'I':
      if (isUInt<12>(CVal) || isShiftedUInt<12, 12>(CVal))
        break;
      return;
    case 'J': {
      uint64_t NVal = -C->getSExtValue();
      if (isUInt<12>(NVal) || isShiftedUInt<12, 12>(NVal)) {
        CVal = C->getSExtValue();
        break;
      }
      return;
    }
    // The K and L constraints apply *only* to logical immediates, including
    // what used to be the MOVI alias for ORR (though the MOVI alias has now
    // been removed and MOV should be used). So these constraints have to
    // distinguish between bit patterns that are valid 32-bit or 64-bit
    // "bitmask immediates": for example 0xaaaaaaaa is a valid bimm32 (K), but
    // not a valid bimm64 (L) where 0xaaaaaaaaaaaaaaaa would be valid, and vice
    // versa.
    case 'K':
      if (AArch64_AM::isLogicalImmediate(CVal, 32))
        break;
      return;
    case 'L':
      if (AArch64_AM::isLogicalImmediate(CVal, 64))
        break;
      return;
    // The M and N constraints are a superset of K and L respectively, for use
    // with the MOV (immediate) alias. As well as the logical immediates they
    // also match 32 or 64-bit immediates that can be loaded either using a
    // *single* MOVZ or MOVN , such as 32-bit 0x12340000, 0x00001234, 0xffffedca
    // (M) or 64-bit 0x1234000000000000 (N) etc.
    // As a note some of this code is liberally stolen from the asm parser.
    case 'M': {
      if (!isUInt<32>(CVal))
        return;
      if (AArch64_AM::isLogicalImmediate(CVal, 32))
        break;
      if ((CVal & 0xFFFF) == CVal)
        break;
      if ((CVal & 0xFFFF0000ULL) == CVal)
        break;
      uint64_t NCVal = ~(uint32_t)CVal;
      if ((NCVal & 0xFFFFULL) == NCVal)
        break;
      if ((NCVal & 0xFFFF0000ULL) == NCVal)
        break;
      return;
    }
    case 'N': {
      if (AArch64_AM::isLogicalImmediate(CVal, 64))
        break;
      if ((CVal & 0xFFFFULL) == CVal)
        break;
      if ((CVal & 0xFFFF0000ULL) == CVal)
        break;
      if ((CVal & 0xFFFF00000000ULL) == CVal)
        break;
      if ((CVal & 0xFFFF000000000000ULL) == CVal)
        break;
      uint64_t NCVal = ~CVal;
      if ((NCVal & 0xFFFFULL) == NCVal)
        break;
      if ((NCVal & 0xFFFF0000ULL) == NCVal)
        break;
      if ((NCVal & 0xFFFF00000000ULL) == NCVal)
        break;
      if ((NCVal & 0xFFFF000000000000ULL) == NCVal)
        break;
      return;
    }
    default:
      return;
    }

    // All assembler immediates are 64-bit integers.
    Result = DAG.getTargetConstant(CVal, SDLoc(Op), MVT::i64);
    break;
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }

  return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

//===----------------------------------------------------------------------===//
//                     AArch64 Advanced SIMD Support
//===----------------------------------------------------------------------===//

/// WidenVector - Given a value in the V64 register class, produce the
/// equivalent value in the V128 register class.
static SDValue WidenVector(SDValue V64Reg, SelectionDAG &DAG) {
  EVT VT = V64Reg.getValueType();
  unsigned NarrowSize = VT.getVectorNumElements();
  MVT EltTy = VT.getVectorElementType().getSimpleVT();
  MVT WideTy = MVT::getVectorVT(EltTy, 2 * NarrowSize);
  SDLoc DL(V64Reg);

  return DAG.getNode(ISD::INSERT_SUBVECTOR, DL, WideTy, DAG.getUNDEF(WideTy),
                     V64Reg, DAG.getConstant(0, DL, MVT::i32));
}

/// getExtFactor - Determine the adjustment factor for the position when
/// generating an "extract from vector registers" instruction.
static unsigned getExtFactor(SDValue &V) {
  EVT EltType = V.getValueType().getVectorElementType();
  return EltType.getSizeInBits() / 8;
}

/// NarrowVector - Given a value in the V128 register class, produce the
/// equivalent value in the V64 register class.
static SDValue NarrowVector(SDValue V128Reg, SelectionDAG &DAG) {
  EVT VT = V128Reg.getValueType();
  unsigned WideSize = VT.getVectorNumElements();
  MVT EltTy = VT.getVectorElementType().getSimpleVT();
  MVT NarrowTy = MVT::getVectorVT(EltTy, WideSize / 2);
  SDLoc DL(V128Reg);

  return DAG.getTargetExtractSubreg(AArch64::dsub, DL, NarrowTy, V128Reg);
}

// Gather data to see if the operation can be modelled as a
// shuffle in combination with VEXTs.
SDValue AArch64TargetLowering::ReconstructShuffle(SDValue Op,
                                                  SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::BUILD_VECTOR && "Unknown opcode!");
  LLVM_DEBUG(dbgs() << "AArch64TargetLowering::ReconstructShuffle\n");
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  unsigned NumElts = VT.getVectorNumElements();

  struct ShuffleSourceInfo {
    SDValue Vec;
    unsigned MinElt;
    unsigned MaxElt;

    // We may insert some combination of BITCASTs and VEXT nodes to force Vec to
    // be compatible with the shuffle we intend to construct. As a result
    // ShuffleVec will be some sliding window into the original Vec.
    SDValue ShuffleVec;

    // Code should guarantee that element i in Vec starts at element "WindowBase
    // + i * WindowScale in ShuffleVec".
    int WindowBase;
    int WindowScale;

    ShuffleSourceInfo(SDValue Vec)
      : Vec(Vec), MinElt(std::numeric_limits<unsigned>::max()), MaxElt(0),
          ShuffleVec(Vec), WindowBase(0), WindowScale(1) {}

    bool operator ==(SDValue OtherVec) { return Vec == OtherVec; }
  };

  // First gather all vectors used as an immediate source for this BUILD_VECTOR
  // node.
  SmallVector<ShuffleSourceInfo, 2> Sources;
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (V.isUndef())
      continue;
    else if (V.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
             !isa<ConstantSDNode>(V.getOperand(1))) {
      LLVM_DEBUG(
          dbgs() << "Reshuffle failed: "
                    "a shuffle can only come from building a vector from "
                    "various elements of other vectors, provided their "
                    "indices are constant\n");
      return SDValue();
    }

    // Add this element source to the list if it's not already there.
    SDValue SourceVec = V.getOperand(0);
    auto Source = find(Sources, SourceVec);
    if (Source == Sources.end())
      Source = Sources.insert(Sources.end(), ShuffleSourceInfo(SourceVec));

    // Update the minimum and maximum lane number seen.
    unsigned EltNo = cast<ConstantSDNode>(V.getOperand(1))->getZExtValue();
    Source->MinElt = std::min(Source->MinElt, EltNo);
    Source->MaxElt = std::max(Source->MaxElt, EltNo);
  }

  if (Sources.size() > 2) {
    LLVM_DEBUG(
        dbgs() << "Reshuffle failed: currently only do something sane when at "
                  "most two source vectors are involved\n");
    return SDValue();
  }

  // Find out the smallest element size among result and two sources, and use
  // it as element size to build the shuffle_vector.
  EVT SmallestEltTy = VT.getVectorElementType();
  for (auto &Source : Sources) {
    EVT SrcEltTy = Source.Vec.getValueType().getVectorElementType();
    if (SrcEltTy.bitsLT(SmallestEltTy)) {
      SmallestEltTy = SrcEltTy;
    }
  }
  unsigned ResMultiplier =
      VT.getScalarSizeInBits() / SmallestEltTy.getSizeInBits();
  NumElts = VT.getSizeInBits() / SmallestEltTy.getSizeInBits();
  EVT ShuffleVT = EVT::getVectorVT(*DAG.getContext(), SmallestEltTy, NumElts);

  // If the source vector is too wide or too narrow, we may nevertheless be able
  // to construct a compatible shuffle either by concatenating it with UNDEF or
  // extracting a suitable range of elements.
  for (auto &Src : Sources) {
    EVT SrcVT = Src.ShuffleVec.getValueType();

    if (SrcVT.getSizeInBits() == VT.getSizeInBits())
      continue;

    // This stage of the search produces a source with the same element type as
    // the original, but with a total width matching the BUILD_VECTOR output.
    EVT EltVT = SrcVT.getVectorElementType();
    unsigned NumSrcElts = VT.getSizeInBits() / EltVT.getSizeInBits();
    EVT DestVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NumSrcElts);

    if (SrcVT.getSizeInBits() < VT.getSizeInBits()) {
      assert(2 * SrcVT.getSizeInBits() == VT.getSizeInBits());
      // We can pad out the smaller vector for free, so if it's part of a
      // shuffle...
      Src.ShuffleVec =
          DAG.getNode(ISD::CONCAT_VECTORS, dl, DestVT, Src.ShuffleVec,
                      DAG.getUNDEF(Src.ShuffleVec.getValueType()));
      continue;
    }

    assert(SrcVT.getSizeInBits() == 2 * VT.getSizeInBits());

    if (Src.MaxElt - Src.MinElt >= NumSrcElts) {
      LLVM_DEBUG(
          dbgs() << "Reshuffle failed: span too large for a VEXT to cope\n");
      return SDValue();
    }

    if (Src.MinElt >= NumSrcElts) {
      // The extraction can just take the second half
      Src.ShuffleVec =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(NumSrcElts, dl, MVT::i64));
      Src.WindowBase = -NumSrcElts;
    } else if (Src.MaxElt < NumSrcElts) {
      // The extraction can just take the first half
      Src.ShuffleVec =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(0, dl, MVT::i64));
    } else {
      // An actual VEXT is needed
      SDValue VEXTSrc1 =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(0, dl, MVT::i64));
      SDValue VEXTSrc2 =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(NumSrcElts, dl, MVT::i64));
      unsigned Imm = Src.MinElt * getExtFactor(VEXTSrc1);

      Src.ShuffleVec = DAG.getNode(AArch64ISD::EXT, dl, DestVT, VEXTSrc1,
                                   VEXTSrc2,
                                   DAG.getConstant(Imm, dl, MVT::i32));
      Src.WindowBase = -Src.MinElt;
    }
  }

  // Another possible incompatibility occurs from the vector element types. We
  // can fix this by bitcasting the source vectors to the same type we intend
  // for the shuffle.
  for (auto &Src : Sources) {
    EVT SrcEltTy = Src.ShuffleVec.getValueType().getVectorElementType();
    if (SrcEltTy == SmallestEltTy)
      continue;
    assert(ShuffleVT.getVectorElementType() == SmallestEltTy);
    Src.ShuffleVec = DAG.getNode(ISD::BITCAST, dl, ShuffleVT, Src.ShuffleVec);
    Src.WindowScale = SrcEltTy.getSizeInBits() / SmallestEltTy.getSizeInBits();
    Src.WindowBase *= Src.WindowScale;
  }

  // Final sanity check before we try to actually produce a shuffle.
  LLVM_DEBUG(for (auto Src
                  : Sources)
                 assert(Src.ShuffleVec.getValueType() == ShuffleVT););

  // The stars all align, our next step is to produce the mask for the shuffle.
  SmallVector<int, 8> Mask(ShuffleVT.getVectorNumElements(), -1);
  int BitsPerShuffleLane = ShuffleVT.getScalarSizeInBits();
  for (unsigned i = 0; i < VT.getVectorNumElements(); ++i) {
    SDValue Entry = Op.getOperand(i);
    if (Entry.isUndef())
      continue;

    auto Src = find(Sources, Entry.getOperand(0));
    int EltNo = cast<ConstantSDNode>(Entry.getOperand(1))->getSExtValue();

    // EXTRACT_VECTOR_ELT performs an implicit any_ext; BUILD_VECTOR an implicit
    // trunc. So only std::min(SrcBits, DestBits) actually get defined in this
    // segment.
    EVT OrigEltTy = Entry.getOperand(0).getValueType().getVectorElementType();
    int BitsDefined =
        std::min(OrigEltTy.getSizeInBits(), VT.getScalarSizeInBits());
    int LanesDefined = BitsDefined / BitsPerShuffleLane;

    // This source is expected to fill ResMultiplier lanes of the final shuffle,
    // starting at the appropriate offset.
    int *LaneMask = &Mask[i * ResMultiplier];

    int ExtractBase = EltNo * Src->WindowScale + Src->WindowBase;
    ExtractBase += NumElts * (Src - Sources.begin());
    for (int j = 0; j < LanesDefined; ++j)
      LaneMask[j] = ExtractBase + j;
  }

  // Final check before we try to produce nonsense...
  if (!isShuffleMaskLegal(Mask, ShuffleVT)) {
    LLVM_DEBUG(dbgs() << "Reshuffle failed: illegal shuffle mask\n");
    return SDValue();
  }

  SDValue ShuffleOps[] = { DAG.getUNDEF(ShuffleVT), DAG.getUNDEF(ShuffleVT) };
  for (unsigned i = 0; i < Sources.size(); ++i)
    ShuffleOps[i] = Sources[i].ShuffleVec;

  SDValue Shuffle = DAG.getVectorShuffle(ShuffleVT, dl, ShuffleOps[0],
                                         ShuffleOps[1], Mask);
  SDValue V = DAG.getNode(ISD::BITCAST, dl, VT, Shuffle);

  LLVM_DEBUG(dbgs() << "Reshuffle, creating node: "; Shuffle.dump();
             dbgs() << "Reshuffle, creating node: "; V.dump(););

  return V;
}

// check if an EXT instruction can handle the shuffle mask when the
// vector sources of the shuffle are the same.
static bool isSingletonEXTMask(ArrayRef<int> M, EVT VT, unsigned &Imm) {
  unsigned NumElts = VT.getVectorNumElements();

  // Assume that the first shuffle index is not UNDEF.  Fail if it is.
  if (M[0] < 0)
    return false;

  Imm = M[0];

  // If this is a VEXT shuffle, the immediate value is the index of the first
  // element.  The other shuffle indices must be the successive elements after
  // the first one.
  unsigned ExpectedElt = Imm;
  for (unsigned i = 1; i < NumElts; ++i) {
    // Increment the expected index.  If it wraps around, just follow it
    // back to index zero and keep going.
    ++ExpectedElt;
    if (ExpectedElt == NumElts)
      ExpectedElt = 0;

    if (M[i] < 0)
      continue; // ignore UNDEF indices
    if (ExpectedElt != static_cast<unsigned>(M[i]))
      return false;
  }

  return true;
}

// check if an EXT instruction can handle the shuffle mask when the
// vector sources of the shuffle are different.
static bool isEXTMask(ArrayRef<int> M, EVT VT, bool &ReverseEXT,
                      unsigned &Imm) {
  // Look for the first non-undef element.
  const int *FirstRealElt = find_if(M, [](int Elt) { return Elt >= 0; });

  // Benefit form APInt to handle overflow when calculating expected element.
  unsigned NumElts = VT.getVectorNumElements();
  unsigned MaskBits = APInt(32, NumElts * 2).logBase2();
  APInt ExpectedElt = APInt(MaskBits, *FirstRealElt + 1);
  // The following shuffle indices must be the successive elements after the
  // first real element.
  const int *FirstWrongElt = std::find_if(FirstRealElt + 1, M.end(),
      [&](int Elt) {return Elt != ExpectedElt++ && Elt != -1;});
  if (FirstWrongElt != M.end())
    return false;

  // The index of an EXT is the first element if it is not UNDEF.
  // Watch out for the beginning UNDEFs. The EXT index should be the expected
  // value of the first element.  E.g.
  // <-1, -1, 3, ...> is treated as <1, 2, 3, ...>.
  // <-1, -1, 0, 1, ...> is treated as <2*NumElts-2, 2*NumElts-1, 0, 1, ...>.
  // ExpectedElt is the last mask index plus 1.
  Imm = ExpectedElt.getZExtValue();

  // There are two difference cases requiring to reverse input vectors.
  // For example, for vector <4 x i32> we have the following cases,
  // Case 1: shufflevector(<4 x i32>,<4 x i32>,<-1, -1, -1, 0>)
  // Case 2: shufflevector(<4 x i32>,<4 x i32>,<-1, -1, 7, 0>)
  // For both cases, we finally use mask <5, 6, 7, 0>, which requires
  // to reverse two input vectors.
  if (Imm < NumElts)
    ReverseEXT = true;
  else
    Imm -= NumElts;

  return true;
}

/// isREVMask - Check if a vector shuffle corresponds to a REV
/// instruction with the specified blocksize.  (The order of the elements
/// within each block of the vector is reversed.)
static bool isREVMask(ArrayRef<int> M, EVT VT, unsigned BlockSize) {
  assert((BlockSize == 16 || BlockSize == 32 || BlockSize == 64) &&
         "Only possible block sizes for REV are: 16, 32, 64");

  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  unsigned BlockElts = M[0] + 1;
  // If the first shuffle index is UNDEF, be optimistic.
  if (M[0] < 0)
    BlockElts = BlockSize / EltSz;

  if (BlockSize <= EltSz || BlockSize != BlockElts * EltSz)
    return false;

  for (unsigned i = 0; i < NumElts; ++i) {
    if (M[i] < 0)
      continue; // ignore UNDEF indices
    if ((unsigned)M[i] != (i - i % BlockElts) + (BlockElts - 1 - i % BlockElts))
      return false;
  }

  return true;
}

static bool isZIPMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned NumElts = VT.getVectorNumElements();
  WhichResult = (M[0] == 0 ? 0 : 1);
  unsigned Idx = WhichResult * NumElts / 2;
  for (unsigned i = 0; i != NumElts; i += 2) {
    if ((M[i] >= 0 && (unsigned)M[i] != Idx) ||
        (M[i + 1] >= 0 && (unsigned)M[i + 1] != Idx + NumElts))
      return false;
    Idx += 1;
  }

  return true;
}

static bool isUZPMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned NumElts = VT.getVectorNumElements();
  WhichResult = (M[0] == 0 ? 0 : 1);
  for (unsigned i = 0; i != NumElts; ++i) {
    if (M[i] < 0)
      continue; // ignore UNDEF indices
    if ((unsigned)M[i] != 2 * i + WhichResult)
      return false;
  }

  return true;
}

static bool isTRNMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned NumElts = VT.getVectorNumElements();
  WhichResult = (M[0] == 0 ? 0 : 1);
  for (unsigned i = 0; i < NumElts; i += 2) {
    if ((M[i] >= 0 && (unsigned)M[i] != i + WhichResult) ||
        (M[i + 1] >= 0 && (unsigned)M[i + 1] != i + NumElts + WhichResult))
      return false;
  }
  return true;
}

/// isZIP_v_undef_Mask - Special case of isZIPMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 0, 1, 1> instead of <0, 4, 1, 5>.
static bool isZIP_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned NumElts = VT.getVectorNumElements();
  WhichResult = (M[0] == 0 ? 0 : 1);
  unsigned Idx = WhichResult * NumElts / 2;
  for (unsigned i = 0; i != NumElts; i += 2) {
    if ((M[i] >= 0 && (unsigned)M[i] != Idx) ||
        (M[i + 1] >= 0 && (unsigned)M[i + 1] != Idx))
      return false;
    Idx += 1;
  }

  return true;
}

/// isUZP_v_undef_Mask - Special case of isUZPMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 2, 0, 2> instead of <0, 2, 4, 6>,
static bool isUZP_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned Half = VT.getVectorNumElements() / 2;
  WhichResult = (M[0] == 0 ? 0 : 1);
  for (unsigned j = 0; j != 2; ++j) {
    unsigned Idx = WhichResult;
    for (unsigned i = 0; i != Half; ++i) {
      int MIdx = M[i + j * Half];
      if (MIdx >= 0 && (unsigned)MIdx != Idx)
        return false;
      Idx += 2;
    }
  }

  return true;
}

/// isTRN_v_undef_Mask - Special case of isTRNMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 0, 2, 2> instead of <0, 4, 2, 6>.
static bool isTRN_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned NumElts = VT.getVectorNumElements();
  WhichResult = (M[0] == 0 ? 0 : 1);
  for (unsigned i = 0; i < NumElts; i += 2) {
    if ((M[i] >= 0 && (unsigned)M[i] != i + WhichResult) ||
        (M[i + 1] >= 0 && (unsigned)M[i + 1] != i + WhichResult))
      return false;
  }
  return true;
}

static bool isINSMask(ArrayRef<int> M, int NumInputElements,
                      bool &DstIsLeft, int &Anomaly) {
  if (M.size() != static_cast<size_t>(NumInputElements))
    return false;

  int NumLHSMatch = 0, NumRHSMatch = 0;
  int LastLHSMismatch = -1, LastRHSMismatch = -1;

  for (int i = 0; i < NumInputElements; ++i) {
    if (M[i] == -1) {
      ++NumLHSMatch;
      ++NumRHSMatch;
      continue;
    }

    if (M[i] == i)
      ++NumLHSMatch;
    else
      LastLHSMismatch = i;

    if (M[i] == i + NumInputElements)
      ++NumRHSMatch;
    else
      LastRHSMismatch = i;
  }

  if (NumLHSMatch == NumInputElements - 1) {
    DstIsLeft = true;
    Anomaly = LastLHSMismatch;
    return true;
  } else if (NumRHSMatch == NumInputElements - 1) {
    DstIsLeft = false;
    Anomaly = LastRHSMismatch;
    return true;
  }

  return false;
}

static bool isConcatMask(ArrayRef<int> Mask, EVT VT, bool SplitLHS) {
  if (VT.getSizeInBits() != 128)
    return false;

  unsigned NumElts = VT.getVectorNumElements();

  for (int I = 0, E = NumElts / 2; I != E; I++) {
    if (Mask[I] != I)
      return false;
  }

  int Offset = NumElts / 2;
  for (int I = NumElts / 2, E = NumElts; I != E; I++) {
    if (Mask[I] != I + SplitLHS * Offset)
      return false;
  }

  return true;
}

static SDValue tryFormConcatFromShuffle(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue V0 = Op.getOperand(0);
  SDValue V1 = Op.getOperand(1);
  ArrayRef<int> Mask = cast<ShuffleVectorSDNode>(Op)->getMask();

  if (VT.getVectorElementType() != V0.getValueType().getVectorElementType() ||
      VT.getVectorElementType() != V1.getValueType().getVectorElementType())
    return SDValue();

  bool SplitV0 = V0.getValueSizeInBits() == 128;

  if (!isConcatMask(Mask, VT, SplitV0))
    return SDValue();

  EVT CastVT = EVT::getVectorVT(*DAG.getContext(), VT.getVectorElementType(),
                                VT.getVectorNumElements() / 2);
  if (SplitV0) {
    V0 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, CastVT, V0,
                     DAG.getConstant(0, DL, MVT::i64));
  }
  if (V1.getValueSizeInBits() == 128) {
    V1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, CastVT, V1,
                     DAG.getConstant(0, DL, MVT::i64));
  }
  return DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, V0, V1);
}

/// GeneratePerfectShuffle - Given an entry in the perfect-shuffle table, emit
/// the specified operations to build the shuffle.
static SDValue GeneratePerfectShuffle(unsigned PFEntry, SDValue LHS,
                                      SDValue RHS, SelectionDAG &DAG,
                                      const SDLoc &dl) {
  unsigned OpNum = (PFEntry >> 26) & 0x0F;
  unsigned LHSID = (PFEntry >> 13) & ((1 << 13) - 1);
  unsigned RHSID = (PFEntry >> 0) & ((1 << 13) - 1);

  enum {
    OP_COPY = 0, // Copy, used for things like <u,u,u,3> to say it is <0,1,2,3>
    OP_VREV,
    OP_VDUP0,
    OP_VDUP1,
    OP_VDUP2,
    OP_VDUP3,
    OP_VEXT1,
    OP_VEXT2,
    OP_VEXT3,
    OP_VUZPL, // VUZP, left result
    OP_VUZPR, // VUZP, right result
    OP_VZIPL, // VZIP, left result
    OP_VZIPR, // VZIP, right result
    OP_VTRNL, // VTRN, left result
    OP_VTRNR  // VTRN, right result
  };

  if (OpNum == OP_COPY) {
    if (LHSID == (1 * 9 + 2) * 9 + 3)
      return LHS;
    assert(LHSID == ((4 * 9 + 5) * 9 + 6) * 9 + 7 && "Illegal OP_COPY!");
    return RHS;
  }

  SDValue OpLHS, OpRHS;
  OpLHS = GeneratePerfectShuffle(PerfectShuffleTable[LHSID], LHS, RHS, DAG, dl);
  OpRHS = GeneratePerfectShuffle(PerfectShuffleTable[RHSID], LHS, RHS, DAG, dl);
  EVT VT = OpLHS.getValueType();

  switch (OpNum) {
  default:
    llvm_unreachable("Unknown shuffle opcode!");
  case OP_VREV:
    // VREV divides the vector in half and swaps within the half.
    if (VT.getVectorElementType() == MVT::i32 ||
        VT.getVectorElementType() == MVT::f32)
      return DAG.getNode(AArch64ISD::REV64, dl, VT, OpLHS);
    // vrev <4 x i16> -> REV32
    if (VT.getVectorElementType() == MVT::i16 ||
        VT.getVectorElementType() == MVT::f16)
      return DAG.getNode(AArch64ISD::REV32, dl, VT, OpLHS);
    // vrev <4 x i8> -> REV16
    assert(VT.getVectorElementType() == MVT::i8);
    return DAG.getNode(AArch64ISD::REV16, dl, VT, OpLHS);
  case OP_VDUP0:
  case OP_VDUP1:
  case OP_VDUP2:
  case OP_VDUP3: {
    EVT EltTy = VT.getVectorElementType();
    unsigned Opcode;
    if (EltTy == MVT::i8)
      Opcode = AArch64ISD::DUPLANE8;
    else if (EltTy == MVT::i16 || EltTy == MVT::f16)
      Opcode = AArch64ISD::DUPLANE16;
    else if (EltTy == MVT::i32 || EltTy == MVT::f32)
      Opcode = AArch64ISD::DUPLANE32;
    else if (EltTy == MVT::i64 || EltTy == MVT::f64)
      Opcode = AArch64ISD::DUPLANE64;
    else
      llvm_unreachable("Invalid vector element type?");

    if (VT.getSizeInBits() == 64)
      OpLHS = WidenVector(OpLHS, DAG);
    SDValue Lane = DAG.getConstant(OpNum - OP_VDUP0, dl, MVT::i64);
    return DAG.getNode(Opcode, dl, VT, OpLHS, Lane);
  }
  case OP_VEXT1:
  case OP_VEXT2:
  case OP_VEXT3: {
    unsigned Imm = (OpNum - OP_VEXT1 + 1) * getExtFactor(OpLHS);
    return DAG.getNode(AArch64ISD::EXT, dl, VT, OpLHS, OpRHS,
                       DAG.getConstant(Imm, dl, MVT::i32));
  }
  case OP_VUZPL:
    return DAG.getNode(AArch64ISD::UZP1, dl, DAG.getVTList(VT, VT), OpLHS,
                       OpRHS);
  case OP_VUZPR:
    return DAG.getNode(AArch64ISD::UZP2, dl, DAG.getVTList(VT, VT), OpLHS,
                       OpRHS);
  case OP_VZIPL:
    return DAG.getNode(AArch64ISD::ZIP1, dl, DAG.getVTList(VT, VT), OpLHS,
                       OpRHS);
  case OP_VZIPR:
    return DAG.getNode(AArch64ISD::ZIP2, dl, DAG.getVTList(VT, VT), OpLHS,
                       OpRHS);
  case OP_VTRNL:
    return DAG.getNode(AArch64ISD::TRN1, dl, DAG.getVTList(VT, VT), OpLHS,
                       OpRHS);
  case OP_VTRNR:
    return DAG.getNode(AArch64ISD::TRN2, dl, DAG.getVTList(VT, VT), OpLHS,
                       OpRHS);
  }
}

static SDValue GenerateTBL(SDValue Op, ArrayRef<int> ShuffleMask,
                           SelectionDAG &DAG) {
  // Check to see if we can use the TBL instruction.
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc DL(Op);

  EVT EltVT = Op.getValueType().getVectorElementType();
  unsigned BytesPerElt = EltVT.getSizeInBits() / 8;

  SmallVector<SDValue, 8> TBLMask;
  for (int Val : ShuffleMask) {
    for (unsigned Byte = 0; Byte < BytesPerElt; ++Byte) {
      unsigned Offset = Byte + Val * BytesPerElt;
      TBLMask.push_back(DAG.getConstant(Offset, DL, MVT::i32));
    }
  }

  MVT IndexVT = MVT::v8i8;
  unsigned IndexLen = 8;
  if (Op.getValueSizeInBits() == 128) {
    IndexVT = MVT::v16i8;
    IndexLen = 16;
  }

  SDValue V1Cst = DAG.getNode(ISD::BITCAST, DL, IndexVT, V1);
  SDValue V2Cst = DAG.getNode(ISD::BITCAST, DL, IndexVT, V2);

  SDValue Shuffle;
  if (V2.getNode()->isUndef()) {
    if (IndexLen == 8)
      V1Cst = DAG.getNode(ISD::CONCAT_VECTORS, DL, MVT::v16i8, V1Cst, V1Cst);
    Shuffle = DAG.getNode(
        ISD::INTRINSIC_WO_CHAIN, DL, IndexVT,
        DAG.getConstant(Intrinsic::aarch64_neon_tbl1, DL, MVT::i32), V1Cst,
        DAG.getBuildVector(IndexVT, DL,
                           makeArrayRef(TBLMask.data(), IndexLen)));
  } else {
    if (IndexLen == 8) {
      V1Cst = DAG.getNode(ISD::CONCAT_VECTORS, DL, MVT::v16i8, V1Cst, V2Cst);
      Shuffle = DAG.getNode(
          ISD::INTRINSIC_WO_CHAIN, DL, IndexVT,
          DAG.getConstant(Intrinsic::aarch64_neon_tbl1, DL, MVT::i32), V1Cst,
          DAG.getBuildVector(IndexVT, DL,
                             makeArrayRef(TBLMask.data(), IndexLen)));
    } else {
      // FIXME: We cannot, for the moment, emit a TBL2 instruction because we
      // cannot currently represent the register constraints on the input
      // table registers.
      //  Shuffle = DAG.getNode(AArch64ISD::TBL2, DL, IndexVT, V1Cst, V2Cst,
      //                   DAG.getBuildVector(IndexVT, DL, &TBLMask[0],
      //                   IndexLen));
      Shuffle = DAG.getNode(
          ISD::INTRINSIC_WO_CHAIN, DL, IndexVT,
          DAG.getConstant(Intrinsic::aarch64_neon_tbl2, DL, MVT::i32), V1Cst,
          V2Cst, DAG.getBuildVector(IndexVT, DL,
                                    makeArrayRef(TBLMask.data(), IndexLen)));
    }
  }
  return DAG.getNode(ISD::BITCAST, DL, Op.getValueType(), Shuffle);
}

static unsigned getDUPLANEOp(EVT EltType) {
  if (EltType == MVT::i8)
    return AArch64ISD::DUPLANE8;
  if (EltType == MVT::i16 || EltType == MVT::f16)
    return AArch64ISD::DUPLANE16;
  if (EltType == MVT::i32 || EltType == MVT::f32)
    return AArch64ISD::DUPLANE32;
  if (EltType == MVT::i64 || EltType == MVT::f64)
    return AArch64ISD::DUPLANE64;

  llvm_unreachable("Invalid vector element type?");
}

SDValue AArch64TargetLowering::LowerVECTOR_SHUFFLE(SDValue Op,
                                                   SelectionDAG &DAG) const {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();

  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(Op.getNode());

  // Convert shuffles that are directly supported on NEON to target-specific
  // DAG nodes, instead of keeping them as shuffles and matching them again
  // during code selection.  This is more efficient and avoids the possibility
  // of inconsistencies between legalization and selection.
  ArrayRef<int> ShuffleMask = SVN->getMask();

  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);

  if (SVN->isSplat()) {
    int Lane = SVN->getSplatIndex();
    // If this is undef splat, generate it via "just" vdup, if possible.
    if (Lane == -1)
      Lane = 0;

    if (Lane == 0 && V1.getOpcode() == ISD::SCALAR_TO_VECTOR)
      return DAG.getNode(AArch64ISD::DUP, dl, V1.getValueType(),
                         V1.getOperand(0));
    // Test if V1 is a BUILD_VECTOR and the lane being referenced is a non-
    // constant. If so, we can just reference the lane's definition directly.
    if (V1.getOpcode() == ISD::BUILD_VECTOR &&
        !isa<ConstantSDNode>(V1.getOperand(Lane)))
      return DAG.getNode(AArch64ISD::DUP, dl, VT, V1.getOperand(Lane));

    // Otherwise, duplicate from the lane of the input vector.
    unsigned Opcode = getDUPLANEOp(V1.getValueType().getVectorElementType());

    // SelectionDAGBuilder may have "helpfully" already extracted or conatenated
    // to make a vector of the same size as this SHUFFLE. We can ignore the
    // extract entirely, and canonicalise the concat using WidenVector.
    if (V1.getOpcode() == ISD::EXTRACT_SUBVECTOR) {
      Lane += cast<ConstantSDNode>(V1.getOperand(1))->getZExtValue();
      V1 = V1.getOperand(0);
    } else if (V1.getOpcode() == ISD::CONCAT_VECTORS) {
      unsigned Idx = Lane >= (int)VT.getVectorNumElements() / 2;
      Lane -= Idx * VT.getVectorNumElements() / 2;
      V1 = WidenVector(V1.getOperand(Idx), DAG);
    } else if (VT.getSizeInBits() == 64)
      V1 = WidenVector(V1, DAG);

    return DAG.getNode(Opcode, dl, VT, V1, DAG.getConstant(Lane, dl, MVT::i64));
  }

  if (isREVMask(ShuffleMask, VT, 64))
    return DAG.getNode(AArch64ISD::REV64, dl, V1.getValueType(), V1, V2);
  if (isREVMask(ShuffleMask, VT, 32))
    return DAG.getNode(AArch64ISD::REV32, dl, V1.getValueType(), V1, V2);
  if (isREVMask(ShuffleMask, VT, 16))
    return DAG.getNode(AArch64ISD::REV16, dl, V1.getValueType(), V1, V2);

  bool ReverseEXT = false;
  unsigned Imm;
  if (isEXTMask(ShuffleMask, VT, ReverseEXT, Imm)) {
    if (ReverseEXT)
      std::swap(V1, V2);
    Imm *= getExtFactor(V1);
    return DAG.getNode(AArch64ISD::EXT, dl, V1.getValueType(), V1, V2,
                       DAG.getConstant(Imm, dl, MVT::i32));
  } else if (V2->isUndef() && isSingletonEXTMask(ShuffleMask, VT, Imm)) {
    Imm *= getExtFactor(V1);
    return DAG.getNode(AArch64ISD::EXT, dl, V1.getValueType(), V1, V1,
                       DAG.getConstant(Imm, dl, MVT::i32));
  }

  unsigned WhichResult;
  if (isZIPMask(ShuffleMask, VT, WhichResult)) {
    unsigned Opc = (WhichResult == 0) ? AArch64ISD::ZIP1 : AArch64ISD::ZIP2;
    return DAG.getNode(Opc, dl, V1.getValueType(), V1, V2);
  }
  if (isUZPMask(ShuffleMask, VT, WhichResult)) {
    unsigned Opc = (WhichResult == 0) ? AArch64ISD::UZP1 : AArch64ISD::UZP2;
    return DAG.getNode(Opc, dl, V1.getValueType(), V1, V2);
  }
  if (isTRNMask(ShuffleMask, VT, WhichResult)) {
    unsigned Opc = (WhichResult == 0) ? AArch64ISD::TRN1 : AArch64ISD::TRN2;
    return DAG.getNode(Opc, dl, V1.getValueType(), V1, V2);
  }

  if (isZIP_v_undef_Mask(ShuffleMask, VT, WhichResult)) {
    unsigned Opc = (WhichResult == 0) ? AArch64ISD::ZIP1 : AArch64ISD::ZIP2;
    return DAG.getNode(Opc, dl, V1.getValueType(), V1, V1);
  }
  if (isUZP_v_undef_Mask(ShuffleMask, VT, WhichResult)) {
    unsigned Opc = (WhichResult == 0) ? AArch64ISD::UZP1 : AArch64ISD::UZP2;
    return DAG.getNode(Opc, dl, V1.getValueType(), V1, V1);
  }
  if (isTRN_v_undef_Mask(ShuffleMask, VT, WhichResult)) {
    unsigned Opc = (WhichResult == 0) ? AArch64ISD::TRN1 : AArch64ISD::TRN2;
    return DAG.getNode(Opc, dl, V1.getValueType(), V1, V1);
  }

  if (SDValue Concat = tryFormConcatFromShuffle(Op, DAG))
    return Concat;

  bool DstIsLeft;
  int Anomaly;
  int NumInputElements = V1.getValueType().getVectorNumElements();
  if (isINSMask(ShuffleMask, NumInputElements, DstIsLeft, Anomaly)) {
    SDValue DstVec = DstIsLeft ? V1 : V2;
    SDValue DstLaneV = DAG.getConstant(Anomaly, dl, MVT::i64);

    SDValue SrcVec = V1;
    int SrcLane = ShuffleMask[Anomaly];
    if (SrcLane >= NumInputElements) {
      SrcVec = V2;
      SrcLane -= VT.getVectorNumElements();
    }
    SDValue SrcLaneV = DAG.getConstant(SrcLane, dl, MVT::i64);

    EVT ScalarVT = VT.getVectorElementType();

    if (ScalarVT.getSizeInBits() < 32 && ScalarVT.isInteger())
      ScalarVT = MVT::i32;

    return DAG.getNode(
        ISD::INSERT_VECTOR_ELT, dl, VT, DstVec,
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, ScalarVT, SrcVec, SrcLaneV),
        DstLaneV);
  }

  // If the shuffle is not directly supported and it has 4 elements, use
  // the PerfectShuffle-generated table to synthesize it from other shuffles.
  unsigned NumElts = VT.getVectorNumElements();
  if (NumElts == 4) {
    unsigned PFIndexes[4];
    for (unsigned i = 0; i != 4; ++i) {
      if (ShuffleMask[i] < 0)
        PFIndexes[i] = 8;
      else
        PFIndexes[i] = ShuffleMask[i];
    }

    // Compute the index in the perfect shuffle table.
    unsigned PFTableIndex = PFIndexes[0] * 9 * 9 * 9 + PFIndexes[1] * 9 * 9 +
                            PFIndexes[2] * 9 + PFIndexes[3];
    unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
    unsigned Cost = (PFEntry >> 30);

    if (Cost <= 4)
      return GeneratePerfectShuffle(PFEntry, V1, V2, DAG, dl);
  }

  return GenerateTBL(Op, ShuffleMask, DAG);
}

static bool resolveBuildVector(BuildVectorSDNode *BVN, APInt &CnstBits,
                               APInt &UndefBits) {
  EVT VT = BVN->getValueType(0);
  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs)) {
    unsigned NumSplats = VT.getSizeInBits() / SplatBitSize;

    for (unsigned i = 0; i < NumSplats; ++i) {
      CnstBits <<= SplatBitSize;
      UndefBits <<= SplatBitSize;
      CnstBits |= SplatBits.zextOrTrunc(VT.getSizeInBits());
      UndefBits |= (SplatBits ^ SplatUndef).zextOrTrunc(VT.getSizeInBits());
    }

    return true;
  }

  return false;
}

// Try 64-bit splatted SIMD immediate.
static SDValue tryAdvSIMDModImm64(unsigned NewOp, SDValue Op, SelectionDAG &DAG,
                                 const APInt &Bits) {
  if (Bits.getHiBits(64) == Bits.getLoBits(64)) {
    uint64_t Value = Bits.zextOrTrunc(64).getZExtValue();
    EVT VT = Op.getValueType();
    MVT MovTy = (VT.getSizeInBits() == 128) ? MVT::v2i64 : MVT::f64;

    if (AArch64_AM::isAdvSIMDModImmType10(Value)) {
      Value = AArch64_AM::encodeAdvSIMDModImmType10(Value);

      SDLoc dl(Op);
      SDValue Mov = DAG.getNode(NewOp, dl, MovTy,
                                DAG.getConstant(Value, dl, MVT::i32));
      return DAG.getNode(AArch64ISD::NVCAST, dl, VT, Mov);
    }
  }

  return SDValue();
}

// Try 32-bit splatted SIMD immediate.
static SDValue tryAdvSIMDModImm32(unsigned NewOp, SDValue Op, SelectionDAG &DAG,
                                  const APInt &Bits,
                                  const SDValue *LHS = nullptr) {
  if (Bits.getHiBits(64) == Bits.getLoBits(64)) {
    uint64_t Value = Bits.zextOrTrunc(64).getZExtValue();
    EVT VT = Op.getValueType();
    MVT MovTy = (VT.getSizeInBits() == 128) ? MVT::v4i32 : MVT::v2i32;
    bool isAdvSIMDModImm = false;
    uint64_t Shift;

    if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType1(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType1(Value);
      Shift = 0;
    }
    else if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType2(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType2(Value);
      Shift = 8;
    }
    else if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType3(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType3(Value);
      Shift = 16;
    }
    else if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType4(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType4(Value);
      Shift = 24;
    }

    if (isAdvSIMDModImm) {
      SDLoc dl(Op);
      SDValue Mov;

      if (LHS)
        Mov = DAG.getNode(NewOp, dl, MovTy, *LHS,
                          DAG.getConstant(Value, dl, MVT::i32),
                          DAG.getConstant(Shift, dl, MVT::i32));
      else
        Mov = DAG.getNode(NewOp, dl, MovTy,
                          DAG.getConstant(Value, dl, MVT::i32),
                          DAG.getConstant(Shift, dl, MVT::i32));

      return DAG.getNode(AArch64ISD::NVCAST, dl, VT, Mov);
    }
  }

  return SDValue();
}

// Try 16-bit splatted SIMD immediate.
static SDValue tryAdvSIMDModImm16(unsigned NewOp, SDValue Op, SelectionDAG &DAG,
                                  const APInt &Bits,
                                  const SDValue *LHS = nullptr) {
  if (Bits.getHiBits(64) == Bits.getLoBits(64)) {
    uint64_t Value = Bits.zextOrTrunc(64).getZExtValue();
    EVT VT = Op.getValueType();
    MVT MovTy = (VT.getSizeInBits() == 128) ? MVT::v8i16 : MVT::v4i16;
    bool isAdvSIMDModImm = false;
    uint64_t Shift;

    if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType5(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType5(Value);
      Shift = 0;
    }
    else if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType6(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType6(Value);
      Shift = 8;
    }

    if (isAdvSIMDModImm) {
      SDLoc dl(Op);
      SDValue Mov;

      if (LHS)
        Mov = DAG.getNode(NewOp, dl, MovTy, *LHS,
                          DAG.getConstant(Value, dl, MVT::i32),
                          DAG.getConstant(Shift, dl, MVT::i32));
      else
        Mov = DAG.getNode(NewOp, dl, MovTy,
                          DAG.getConstant(Value, dl, MVT::i32),
                          DAG.getConstant(Shift, dl, MVT::i32));

      return DAG.getNode(AArch64ISD::NVCAST, dl, VT, Mov);
    }
  }

  return SDValue();
}

// Try 32-bit splatted SIMD immediate with shifted ones.
static SDValue tryAdvSIMDModImm321s(unsigned NewOp, SDValue Op,
                                    SelectionDAG &DAG, const APInt &Bits) {
  if (Bits.getHiBits(64) == Bits.getLoBits(64)) {
    uint64_t Value = Bits.zextOrTrunc(64).getZExtValue();
    EVT VT = Op.getValueType();
    MVT MovTy = (VT.getSizeInBits() == 128) ? MVT::v4i32 : MVT::v2i32;
    bool isAdvSIMDModImm = false;
    uint64_t Shift;

    if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType7(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType7(Value);
      Shift = 264;
    }
    else if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType8(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType8(Value);
      Shift = 272;
    }

    if (isAdvSIMDModImm) {
      SDLoc dl(Op);
      SDValue Mov = DAG.getNode(NewOp, dl, MovTy,
                                DAG.getConstant(Value, dl, MVT::i32),
                                DAG.getConstant(Shift, dl, MVT::i32));
      return DAG.getNode(AArch64ISD::NVCAST, dl, VT, Mov);
    }
  }

  return SDValue();
}

// Try 8-bit splatted SIMD immediate.
static SDValue tryAdvSIMDModImm8(unsigned NewOp, SDValue Op, SelectionDAG &DAG,
                                 const APInt &Bits) {
  if (Bits.getHiBits(64) == Bits.getLoBits(64)) {
    uint64_t Value = Bits.zextOrTrunc(64).getZExtValue();
    EVT VT = Op.getValueType();
    MVT MovTy = (VT.getSizeInBits() == 128) ? MVT::v16i8 : MVT::v8i8;

    if (AArch64_AM::isAdvSIMDModImmType9(Value)) {
      Value = AArch64_AM::encodeAdvSIMDModImmType9(Value);

      SDLoc dl(Op);
      SDValue Mov = DAG.getNode(NewOp, dl, MovTy,
                                DAG.getConstant(Value, dl, MVT::i32));
      return DAG.getNode(AArch64ISD::NVCAST, dl, VT, Mov);
    }
  }

  return SDValue();
}

// Try FP splatted SIMD immediate.
static SDValue tryAdvSIMDModImmFP(unsigned NewOp, SDValue Op, SelectionDAG &DAG,
                                  const APInt &Bits) {
  if (Bits.getHiBits(64) == Bits.getLoBits(64)) {
    uint64_t Value = Bits.zextOrTrunc(64).getZExtValue();
    EVT VT = Op.getValueType();
    bool isWide = (VT.getSizeInBits() == 128);
    MVT MovTy;
    bool isAdvSIMDModImm = false;

    if ((isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType11(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType11(Value);
      MovTy = isWide ? MVT::v4f32 : MVT::v2f32;
    }
    else if (isWide &&
             (isAdvSIMDModImm = AArch64_AM::isAdvSIMDModImmType12(Value))) {
      Value = AArch64_AM::encodeAdvSIMDModImmType12(Value);
      MovTy = MVT::v2f64;
    }

    if (isAdvSIMDModImm) {
      SDLoc dl(Op);
      SDValue Mov = DAG.getNode(NewOp, dl, MovTy,
                                DAG.getConstant(Value, dl, MVT::i32));
      return DAG.getNode(AArch64ISD::NVCAST, dl, VT, Mov);
    }
  }

  return SDValue();
}

SDValue AArch64TargetLowering::LowerVectorAND(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  EVT VT = Op.getValueType();

  BuildVectorSDNode *BVN =
      dyn_cast<BuildVectorSDNode>(Op.getOperand(1).getNode());
  if (!BVN) {
    // AND commutes, so try swapping the operands.
    LHS = Op.getOperand(1);
    BVN = dyn_cast<BuildVectorSDNode>(Op.getOperand(0).getNode());
  }
  if (!BVN)
    return Op;

  APInt DefBits(VT.getSizeInBits(), 0);
  APInt UndefBits(VT.getSizeInBits(), 0);
  if (resolveBuildVector(BVN, DefBits, UndefBits)) {
    SDValue NewOp;

    // We only have BIC vector immediate instruction, which is and-not.
    DefBits = ~DefBits;
    if ((NewOp = tryAdvSIMDModImm32(AArch64ISD::BICi, Op, DAG,
                                    DefBits, &LHS)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::BICi, Op, DAG,
                                    DefBits, &LHS)))
      return NewOp;

    UndefBits = ~UndefBits;
    if ((NewOp = tryAdvSIMDModImm32(AArch64ISD::BICi, Op, DAG,
                                    UndefBits, &LHS)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::BICi, Op, DAG,
                                    UndefBits, &LHS)))
      return NewOp;
  }

  // We can always fall back to a non-immediate AND.
  return Op;
}

// Specialized code to quickly find if PotentialBVec is a BuildVector that
// consists of only the same constant int value, returned in reference arg
// ConstVal
static bool isAllConstantBuildVector(const SDValue &PotentialBVec,
                                     uint64_t &ConstVal) {
  BuildVectorSDNode *Bvec = dyn_cast<BuildVectorSDNode>(PotentialBVec);
  if (!Bvec)
    return false;
  ConstantSDNode *FirstElt = dyn_cast<ConstantSDNode>(Bvec->getOperand(0));
  if (!FirstElt)
    return false;
  EVT VT = Bvec->getValueType(0);
  unsigned NumElts = VT.getVectorNumElements();
  for (unsigned i = 1; i < NumElts; ++i)
    if (dyn_cast<ConstantSDNode>(Bvec->getOperand(i)) != FirstElt)
      return false;
  ConstVal = FirstElt->getZExtValue();
  return true;
}

static unsigned getIntrinsicID(const SDNode *N) {
  unsigned Opcode = N->getOpcode();
  switch (Opcode) {
  default:
    return Intrinsic::not_intrinsic;
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
    if (IID < Intrinsic::num_intrinsics)
      return IID;
    return Intrinsic::not_intrinsic;
  }
  }
}

// Attempt to form a vector S[LR]I from (or (and X, BvecC1), (lsl Y, C2)),
// to (SLI X, Y, C2), where X and Y have matching vector types, BvecC1 is a
// BUILD_VECTORs with constant element C1, C2 is a constant, and C1 == ~C2.
// Also, logical shift right -> sri, with the same structure.
static SDValue tryLowerToSLI(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);

  if (!VT.isVector())
    return SDValue();

  SDLoc DL(N);

  // Is the first op an AND?
  const SDValue And = N->getOperand(0);
  if (And.getOpcode() != ISD::AND)
    return SDValue();

  // Is the second op an shl or lshr?
  SDValue Shift = N->getOperand(1);
  // This will have been turned into: AArch64ISD::VSHL vector, #shift
  // or AArch64ISD::VLSHR vector, #shift
  unsigned ShiftOpc = Shift.getOpcode();
  if ((ShiftOpc != AArch64ISD::VSHL && ShiftOpc != AArch64ISD::VLSHR))
    return SDValue();
  bool IsShiftRight = ShiftOpc == AArch64ISD::VLSHR;

  // Is the shift amount constant?
  ConstantSDNode *C2node = dyn_cast<ConstantSDNode>(Shift.getOperand(1));
  if (!C2node)
    return SDValue();

  // Is the and mask vector all constant?
  uint64_t C1;
  if (!isAllConstantBuildVector(And.getOperand(1), C1))
    return SDValue();

  // Is C1 == ~C2, taking into account how much one can shift elements of a
  // particular size?
  uint64_t C2 = C2node->getZExtValue();
  unsigned ElemSizeInBits = VT.getScalarSizeInBits();
  if (C2 > ElemSizeInBits)
    return SDValue();
  unsigned ElemMask = (1 << ElemSizeInBits) - 1;
  if ((C1 & ElemMask) != (~C2 & ElemMask))
    return SDValue();

  SDValue X = And.getOperand(0);
  SDValue Y = Shift.getOperand(0);

  unsigned Intrin =
      IsShiftRight ? Intrinsic::aarch64_neon_vsri : Intrinsic::aarch64_neon_vsli;
  SDValue ResultSLI =
      DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VT,
                  DAG.getConstant(Intrin, DL, MVT::i32), X, Y,
                  Shift.getOperand(1));

  LLVM_DEBUG(dbgs() << "aarch64-lower: transformed: \n");
  LLVM_DEBUG(N->dump(&DAG));
  LLVM_DEBUG(dbgs() << "into: \n");
  LLVM_DEBUG(ResultSLI->dump(&DAG));

  ++NumShiftInserts;
  return ResultSLI;
}

SDValue AArch64TargetLowering::LowerVectorOR(SDValue Op,
                                             SelectionDAG &DAG) const {
  // Attempt to form a vector S[LR]I from (or (and X, C1), (lsl Y, C2))
  if (EnableAArch64SlrGeneration) {
    if (SDValue Res = tryLowerToSLI(Op.getNode(), DAG))
      return Res;
  }

  EVT VT = Op.getValueType();

  SDValue LHS = Op.getOperand(0);
  BuildVectorSDNode *BVN =
      dyn_cast<BuildVectorSDNode>(Op.getOperand(1).getNode());
  if (!BVN) {
    // OR commutes, so try swapping the operands.
    LHS = Op.getOperand(1);
    BVN = dyn_cast<BuildVectorSDNode>(Op.getOperand(0).getNode());
  }
  if (!BVN)
    return Op;

  APInt DefBits(VT.getSizeInBits(), 0);
  APInt UndefBits(VT.getSizeInBits(), 0);
  if (resolveBuildVector(BVN, DefBits, UndefBits)) {
    SDValue NewOp;

    if ((NewOp = tryAdvSIMDModImm32(AArch64ISD::ORRi, Op, DAG,
                                    DefBits, &LHS)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::ORRi, Op, DAG,
                                    DefBits, &LHS)))
      return NewOp;

    if ((NewOp = tryAdvSIMDModImm32(AArch64ISD::ORRi, Op, DAG,
                                    UndefBits, &LHS)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::ORRi, Op, DAG,
                                    UndefBits, &LHS)))
      return NewOp;
  }

  // We can always fall back to a non-immediate OR.
  return Op;
}

// Normalize the operands of BUILD_VECTOR. The value of constant operands will
// be truncated to fit element width.
static SDValue NormalizeBuildVector(SDValue Op,
                                    SelectionDAG &DAG) {
  assert(Op.getOpcode() == ISD::BUILD_VECTOR && "Unknown opcode!");
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  EVT EltTy= VT.getVectorElementType();

  if (EltTy.isFloatingPoint() || EltTy.getSizeInBits() > 16)
    return Op;

  SmallVector<SDValue, 16> Ops;
  for (SDValue Lane : Op->ops()) {
    // For integer vectors, type legalization would have promoted the
    // operands already. Otherwise, if Op is a floating-point splat
    // (with operands cast to integers), then the only possibilities
    // are constants and UNDEFs.
    if (auto *CstLane = dyn_cast<ConstantSDNode>(Lane)) {
      APInt LowBits(EltTy.getSizeInBits(),
                    CstLane->getZExtValue());
      Lane = DAG.getConstant(LowBits.getZExtValue(), dl, MVT::i32);
    } else if (Lane.getNode()->isUndef()) {
      Lane = DAG.getUNDEF(MVT::i32);
    } else {
      assert(Lane.getValueType() == MVT::i32 &&
             "Unexpected BUILD_VECTOR operand type");
    }
    Ops.push_back(Lane);
  }
  return DAG.getBuildVector(VT, dl, Ops);
}

static SDValue ConstantBuildVector(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();

  APInt DefBits(VT.getSizeInBits(), 0);
  APInt UndefBits(VT.getSizeInBits(), 0);
  BuildVectorSDNode *BVN = cast<BuildVectorSDNode>(Op.getNode());
  if (resolveBuildVector(BVN, DefBits, UndefBits)) {
    SDValue NewOp;
    if ((NewOp = tryAdvSIMDModImm64(AArch64ISD::MOVIedit, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm32(AArch64ISD::MOVIshift, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm321s(AArch64ISD::MOVImsl, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::MOVIshift, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm8(AArch64ISD::MOVI, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImmFP(AArch64ISD::FMOV, Op, DAG, DefBits)))
      return NewOp;

    DefBits = ~DefBits;
    if ((NewOp = tryAdvSIMDModImm32(AArch64ISD::MVNIshift, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm321s(AArch64ISD::MVNImsl, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::MVNIshift, Op, DAG, DefBits)))
      return NewOp;

    DefBits = UndefBits;
    if ((NewOp = tryAdvSIMDModImm64(AArch64ISD::MOVIedit, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm32(AArch64ISD::MOVIshift, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm321s(AArch64ISD::MOVImsl, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::MOVIshift, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm8(AArch64ISD::MOVI, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImmFP(AArch64ISD::FMOV, Op, DAG, DefBits)))
      return NewOp;

    DefBits = ~UndefBits;
    if ((NewOp = tryAdvSIMDModImm32(AArch64ISD::MVNIshift, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm321s(AArch64ISD::MVNImsl, Op, DAG, DefBits)) ||
        (NewOp = tryAdvSIMDModImm16(AArch64ISD::MVNIshift, Op, DAG, DefBits)))
      return NewOp;
  }

  return SDValue();
}

SDValue AArch64TargetLowering::LowerBUILD_VECTOR(SDValue Op,
                                                 SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();

  // Try to build a simple constant vector.
  Op = NormalizeBuildVector(Op, DAG);
  if (VT.isInteger()) {
    // Certain vector constants, used to express things like logical NOT and
    // arithmetic NEG, are passed through unmodified.  This allows special
    // patterns for these operations to match, which will lower these constants
    // to whatever is proven necessary.
    BuildVectorSDNode *BVN = cast<BuildVectorSDNode>(Op.getNode());
    if (BVN->isConstant())
      if (ConstantSDNode *Const = BVN->getConstantSplatNode()) {
        unsigned BitSize = VT.getVectorElementType().getSizeInBits();
        APInt Val(BitSize,
                  Const->getAPIntValue().zextOrTrunc(BitSize).getZExtValue());
        if (Val.isNullValue() || Val.isAllOnesValue())
          return Op;
      }
  }

  if (SDValue V = ConstantBuildVector(Op, DAG))
    return V;

  // Scan through the operands to find some interesting properties we can
  // exploit:
  //   1) If only one value is used, we can use a DUP, or
  //   2) if only the low element is not undef, we can just insert that, or
  //   3) if only one constant value is used (w/ some non-constant lanes),
  //      we can splat the constant value into the whole vector then fill
  //      in the non-constant lanes.
  //   4) FIXME: If different constant values are used, but we can intelligently
  //             select the values we'll be overwriting for the non-constant
  //             lanes such that we can directly materialize the vector
  //             some other way (MOVI, e.g.), we can be sneaky.
  //   5) if all operands are EXTRACT_VECTOR_ELT, check for VUZP.
  SDLoc dl(Op);
  unsigned NumElts = VT.getVectorNumElements();
  bool isOnlyLowElement = true;
  bool usesOnlyOneValue = true;
  bool usesOnlyOneConstantValue = true;
  bool isConstant = true;
  bool AllLanesExtractElt = true;
  unsigned NumConstantLanes = 0;
  SDValue Value;
  SDValue ConstantValue;
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (V.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
      AllLanesExtractElt = false;
    if (V.isUndef())
      continue;
    if (i > 0)
      isOnlyLowElement = false;
    if (!isa<ConstantFPSDNode>(V) && !isa<ConstantSDNode>(V))
      isConstant = false;

    if (isa<ConstantSDNode>(V) || isa<ConstantFPSDNode>(V)) {
      ++NumConstantLanes;
      if (!ConstantValue.getNode())
        ConstantValue = V;
      else if (ConstantValue != V)
        usesOnlyOneConstantValue = false;
    }

    if (!Value.getNode())
      Value = V;
    else if (V != Value)
      usesOnlyOneValue = false;
  }

  if (!Value.getNode()) {
    LLVM_DEBUG(
        dbgs() << "LowerBUILD_VECTOR: value undefined, creating undef node\n");
    return DAG.getUNDEF(VT);
  }

  // Convert BUILD_VECTOR where all elements but the lowest are undef into
  // SCALAR_TO_VECTOR, except for when we have a single-element constant vector
  // as SimplifyDemandedBits will just turn that back into BUILD_VECTOR.
  if (isOnlyLowElement && !(NumElts == 1 && isa<ConstantSDNode>(Value))) {
    LLVM_DEBUG(dbgs() << "LowerBUILD_VECTOR: only low element used, creating 1 "
                         "SCALAR_TO_VECTOR node\n");
    return DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value);
  }

  if (AllLanesExtractElt) {
    SDNode *Vector = nullptr;
    bool Even = false;
    bool Odd = false;
    // Check whether the extract elements match the Even pattern <0,2,4,...> or
    // the Odd pattern <1,3,5,...>.
    for (unsigned i = 0; i < NumElts; ++i) {
      SDValue V = Op.getOperand(i);
      const SDNode *N = V.getNode();
      if (!isa<ConstantSDNode>(N->getOperand(1)))
        break;
      SDValue N0 = N->getOperand(0);

      // All elements are extracted from the same vector.
      if (!Vector) {
        Vector = N0.getNode();
        // Check that the type of EXTRACT_VECTOR_ELT matches the type of
        // BUILD_VECTOR.
        if (VT.getVectorElementType() !=
            N0.getValueType().getVectorElementType())
          break;
      } else if (Vector != N0.getNode()) {
        Odd = false;
        Even = false;
        break;
      }

      // Extracted values are either at Even indices <0,2,4,...> or at Odd
      // indices <1,3,5,...>.
      uint64_t Val = N->getConstantOperandVal(1);
      if (Val == 2 * i) {
        Even = true;
        continue;
      }
      if (Val - 1 == 2 * i) {
        Odd = true;
        continue;
      }

      // Something does not match: abort.
      Odd = false;
      Even = false;
      break;
    }
    if (Even || Odd) {
      SDValue LHS =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, VT, SDValue(Vector, 0),
                      DAG.getConstant(0, dl, MVT::i64));
      SDValue RHS =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, VT, SDValue(Vector, 0),
                      DAG.getConstant(NumElts, dl, MVT::i64));

      if (Even && !Odd)
        return DAG.getNode(AArch64ISD::UZP1, dl, DAG.getVTList(VT, VT), LHS,
                           RHS);
      if (Odd && !Even)
        return DAG.getNode(AArch64ISD::UZP2, dl, DAG.getVTList(VT, VT), LHS,
                           RHS);
    }
  }

  // Use DUP for non-constant splats. For f32 constant splats, reduce to
  // i32 and try again.
  if (usesOnlyOneValue) {
    if (!isConstant) {
      if (Value.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
          Value.getValueType() != VT) {
        LLVM_DEBUG(
            dbgs() << "LowerBUILD_VECTOR: use DUP for non-constant splats\n");
        return DAG.getNode(AArch64ISD::DUP, dl, VT, Value);
      }

      // This is actually a DUPLANExx operation, which keeps everything vectory.

      SDValue Lane = Value.getOperand(1);
      Value = Value.getOperand(0);
      if (Value.getValueSizeInBits() == 64) {
        LLVM_DEBUG(
            dbgs() << "LowerBUILD_VECTOR: DUPLANE works on 128-bit vectors, "
                      "widening it\n");
        Value = WidenVector(Value, DAG);
      }

      unsigned Opcode = getDUPLANEOp(VT.getVectorElementType());
      return DAG.getNode(Opcode, dl, VT, Value, Lane);
    }

    if (VT.getVectorElementType().isFloatingPoint()) {
      SmallVector<SDValue, 8> Ops;
      EVT EltTy = VT.getVectorElementType();
      assert ((EltTy == MVT::f16 || EltTy == MVT::f32 || EltTy == MVT::f64) &&
              "Unsupported floating-point vector type");
      LLVM_DEBUG(
          dbgs() << "LowerBUILD_VECTOR: float constant splats, creating int "
                    "BITCASTS, and try again\n");
      MVT NewType = MVT::getIntegerVT(EltTy.getSizeInBits());
      for (unsigned i = 0; i < NumElts; ++i)
        Ops.push_back(DAG.getNode(ISD::BITCAST, dl, NewType, Op.getOperand(i)));
      EVT VecVT = EVT::getVectorVT(*DAG.getContext(), NewType, NumElts);
      SDValue Val = DAG.getBuildVector(VecVT, dl, Ops);
      LLVM_DEBUG(dbgs() << "LowerBUILD_VECTOR: trying to lower new vector: ";
                 Val.dump(););
      Val = LowerBUILD_VECTOR(Val, DAG);
      if (Val.getNode())
        return DAG.getNode(ISD::BITCAST, dl, VT, Val);
    }
  }

  // If there was only one constant value used and for more than one lane,
  // start by splatting that value, then replace the non-constant lanes. This
  // is better than the default, which will perform a separate initialization
  // for each lane.
  if (NumConstantLanes > 0 && usesOnlyOneConstantValue) {
    // Firstly, try to materialize the splat constant.
    SDValue Vec = DAG.getSplatBuildVector(VT, dl, ConstantValue),
            Val = ConstantBuildVector(Vec, DAG);
    if (!Val) {
      // Otherwise, materialize the constant and splat it.
      Val = DAG.getNode(AArch64ISD::DUP, dl, VT, ConstantValue);
      DAG.ReplaceAllUsesWith(Vec.getNode(), &Val);
    }

    // Now insert the non-constant lanes.
    for (unsigned i = 0; i < NumElts; ++i) {
      SDValue V = Op.getOperand(i);
      SDValue LaneIdx = DAG.getConstant(i, dl, MVT::i64);
      if (!isa<ConstantSDNode>(V) && !isa<ConstantFPSDNode>(V))
        // Note that type legalization likely mucked about with the VT of the
        // source operand, so we may have to convert it here before inserting.
        Val = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, Val, V, LaneIdx);
    }
    return Val;
  }

  // This will generate a load from the constant pool.
  if (isConstant) {
    LLVM_DEBUG(
        dbgs() << "LowerBUILD_VECTOR: all elements are constant, use default "
                  "expansion\n");
    return SDValue();
  }

  // Empirical tests suggest this is rarely worth it for vectors of length <= 2.
  if (NumElts >= 4) {
    if (SDValue shuffle = ReconstructShuffle(Op, DAG))
      return shuffle;
  }

  // If all else fails, just use a sequence of INSERT_VECTOR_ELT when we
  // know the default expansion would otherwise fall back on something even
  // worse. For a vector with one or two non-undef values, that's
  // scalar_to_vector for the elements followed by a shuffle (provided the
  // shuffle is valid for the target) and materialization element by element
  // on the stack followed by a load for everything else.
  if (!isConstant && !usesOnlyOneValue) {
    LLVM_DEBUG(
        dbgs() << "LowerBUILD_VECTOR: alternatives failed, creating sequence "
                  "of INSERT_VECTOR_ELT\n");

    SDValue Vec = DAG.getUNDEF(VT);
    SDValue Op0 = Op.getOperand(0);
    unsigned i = 0;

    // Use SCALAR_TO_VECTOR for lane zero to
    // a) Avoid a RMW dependency on the full vector register, and
    // b) Allow the register coalescer to fold away the copy if the
    //    value is already in an S or D register, and we're forced to emit an
    //    INSERT_SUBREG that we can't fold anywhere.
    //
    // We also allow types like i8 and i16 which are illegal scalar but legal
    // vector element types. After type-legalization the inserted value is
    // extended (i32) and it is safe to cast them to the vector type by ignoring
    // the upper bits of the lowest lane (e.g. v8i8, v4i16).
    if (!Op0.isUndef()) {
      LLVM_DEBUG(dbgs() << "Creating node for op0, it is not undefined:\n");
      Vec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Op0);
      ++i;
    }
    LLVM_DEBUG(if (i < NumElts) dbgs()
                   << "Creating nodes for the other vector elements:\n";);
    for (; i < NumElts; ++i) {
      SDValue V = Op.getOperand(i);
      if (V.isUndef())
        continue;
      SDValue LaneIdx = DAG.getConstant(i, dl, MVT::i64);
      Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, Vec, V, LaneIdx);
    }
    return Vec;
  }

  LLVM_DEBUG(
      dbgs() << "LowerBUILD_VECTOR: use default expansion, failed to find "
                "better alternative\n");
  return SDValue();
}

SDValue AArch64TargetLowering::LowerINSERT_VECTOR_ELT(SDValue Op,
                                                      SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::INSERT_VECTOR_ELT && "Unknown opcode!");

  // Check for non-constant or out of range lane.
  EVT VT = Op.getOperand(0).getValueType();
  ConstantSDNode *CI = dyn_cast<ConstantSDNode>(Op.getOperand(2));
  if (!CI || CI->getZExtValue() >= VT.getVectorNumElements())
    return SDValue();


  // Insertion/extraction are legal for V128 types.
  if (VT == MVT::v16i8 || VT == MVT::v8i16 || VT == MVT::v4i32 ||
      VT == MVT::v2i64 || VT == MVT::v4f32 || VT == MVT::v2f64 ||
      VT == MVT::v8f16)
    return Op;

  if (VT != MVT::v8i8 && VT != MVT::v4i16 && VT != MVT::v2i32 &&
      VT != MVT::v1i64 && VT != MVT::v2f32 && VT != MVT::v4f16)
    return SDValue();

  // For V64 types, we perform insertion by expanding the value
  // to a V128 type and perform the insertion on that.
  SDLoc DL(Op);
  SDValue WideVec = WidenVector(Op.getOperand(0), DAG);
  EVT WideTy = WideVec.getValueType();

  SDValue Node = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, WideTy, WideVec,
                             Op.getOperand(1), Op.getOperand(2));
  // Re-narrow the resultant vector.
  return NarrowVector(Node, DAG);
}

SDValue
AArch64TargetLowering::LowerEXTRACT_VECTOR_ELT(SDValue Op,
                                               SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::EXTRACT_VECTOR_ELT && "Unknown opcode!");

  // Check for non-constant or out of range lane.
  EVT VT = Op.getOperand(0).getValueType();
  ConstantSDNode *CI = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!CI || CI->getZExtValue() >= VT.getVectorNumElements())
    return SDValue();


  // Insertion/extraction are legal for V128 types.
  if (VT == MVT::v16i8 || VT == MVT::v8i16 || VT == MVT::v4i32 ||
      VT == MVT::v2i64 || VT == MVT::v4f32 || VT == MVT::v2f64 ||
      VT == MVT::v8f16)
    return Op;

  if (VT != MVT::v8i8 && VT != MVT::v4i16 && VT != MVT::v2i32 &&
      VT != MVT::v1i64 && VT != MVT::v2f32 && VT != MVT::v4f16)
    return SDValue();

  // For V64 types, we perform extraction by expanding the value
  // to a V128 type and perform the extraction on that.
  SDLoc DL(Op);
  SDValue WideVec = WidenVector(Op.getOperand(0), DAG);
  EVT WideTy = WideVec.getValueType();

  EVT ExtrTy = WideTy.getVectorElementType();
  if (ExtrTy == MVT::i16 || ExtrTy == MVT::i8)
    ExtrTy = MVT::i32;

  // For extractions, we just return the result directly.
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, ExtrTy, WideVec,
                     Op.getOperand(1));
}

SDValue AArch64TargetLowering::LowerEXTRACT_SUBVECTOR(SDValue Op,
                                                      SelectionDAG &DAG) const {
  EVT VT = Op.getOperand(0).getValueType();
  SDLoc dl(Op);
  // Just in case...
  if (!VT.isVector())
    return SDValue();

  ConstantSDNode *Cst = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!Cst)
    return SDValue();
  unsigned Val = Cst->getZExtValue();

  unsigned Size = Op.getValueSizeInBits();

  // This will get lowered to an appropriate EXTRACT_SUBREG in ISel.
  if (Val == 0)
    return Op;

  // If this is extracting the upper 64-bits of a 128-bit vector, we match
  // that directly.
  if (Size == 64 && Val * VT.getScalarSizeInBits() == 64)
    return Op;

  return SDValue();
}

bool AArch64TargetLowering::isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const {
  if (VT.getVectorNumElements() == 4 &&
      (VT.is128BitVector() || VT.is64BitVector())) {
    unsigned PFIndexes[4];
    for (unsigned i = 0; i != 4; ++i) {
      if (M[i] < 0)
        PFIndexes[i] = 8;
      else
        PFIndexes[i] = M[i];
    }

    // Compute the index in the perfect shuffle table.
    unsigned PFTableIndex = PFIndexes[0] * 9 * 9 * 9 + PFIndexes[1] * 9 * 9 +
                            PFIndexes[2] * 9 + PFIndexes[3];
    unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
    unsigned Cost = (PFEntry >> 30);

    if (Cost <= 4)
      return true;
  }

  bool DummyBool;
  int DummyInt;
  unsigned DummyUnsigned;

  return (ShuffleVectorSDNode::isSplatMask(&M[0], VT) || isREVMask(M, VT, 64) ||
          isREVMask(M, VT, 32) || isREVMask(M, VT, 16) ||
          isEXTMask(M, VT, DummyBool, DummyUnsigned) ||
          // isTBLMask(M, VT) || // FIXME: Port TBL support from ARM.
          isTRNMask(M, VT, DummyUnsigned) || isUZPMask(M, VT, DummyUnsigned) ||
          isZIPMask(M, VT, DummyUnsigned) ||
          isTRN_v_undef_Mask(M, VT, DummyUnsigned) ||
          isUZP_v_undef_Mask(M, VT, DummyUnsigned) ||
          isZIP_v_undef_Mask(M, VT, DummyUnsigned) ||
          isINSMask(M, VT.getVectorNumElements(), DummyBool, DummyInt) ||
          isConcatMask(M, VT, VT.getSizeInBits() == 128));
}

/// getVShiftImm - Check if this is a valid build_vector for the immediate
/// operand of a vector shift operation, where all the elements of the
/// build_vector must have the same constant integer value.
static bool getVShiftImm(SDValue Op, unsigned ElementBits, int64_t &Cnt) {
  // Ignore bit_converts.
  while (Op.getOpcode() == ISD::BITCAST)
    Op = Op.getOperand(0);
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(Op.getNode());
  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (!BVN || !BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize,
                                    HasAnyUndefs, ElementBits) ||
      SplatBitSize > ElementBits)
    return false;
  Cnt = SplatBits.getSExtValue();
  return true;
}

/// isVShiftLImm - Check if this is a valid build_vector for the immediate
/// operand of a vector shift left operation.  That value must be in the range:
///   0 <= Value < ElementBits for a left shift; or
///   0 <= Value <= ElementBits for a long left shift.
static bool isVShiftLImm(SDValue Op, EVT VT, bool isLong, int64_t &Cnt) {
  assert(VT.isVector() && "vector shift count is not a vector type");
  int64_t ElementBits = VT.getScalarSizeInBits();
  if (!getVShiftImm(Op, ElementBits, Cnt))
    return false;
  return (Cnt >= 0 && (isLong ? Cnt - 1 : Cnt) < ElementBits);
}

/// isVShiftRImm - Check if this is a valid build_vector for the immediate
/// operand of a vector shift right operation. The value must be in the range:
///   1 <= Value <= ElementBits for a right shift; or
static bool isVShiftRImm(SDValue Op, EVT VT, bool isNarrow, int64_t &Cnt) {
  assert(VT.isVector() && "vector shift count is not a vector type");
  int64_t ElementBits = VT.getScalarSizeInBits();
  if (!getVShiftImm(Op, ElementBits, Cnt))
    return false;
  return (Cnt >= 1 && Cnt <= (isNarrow ? ElementBits / 2 : ElementBits));
}

SDValue AArch64TargetLowering::LowerVectorSRA_SRL_SHL(SDValue Op,
                                                      SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  int64_t Cnt;

  if (!Op.getOperand(1).getValueType().isVector())
    return Op;
  unsigned EltSize = VT.getScalarSizeInBits();

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("unexpected shift opcode");

  case ISD::SHL:
    if (isVShiftLImm(Op.getOperand(1), VT, false, Cnt) && Cnt < EltSize)
      return DAG.getNode(AArch64ISD::VSHL, DL, VT, Op.getOperand(0),
                         DAG.getConstant(Cnt, DL, MVT::i32));
    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VT,
                       DAG.getConstant(Intrinsic::aarch64_neon_ushl, DL,
                                       MVT::i32),
                       Op.getOperand(0), Op.getOperand(1));
  case ISD::SRA:
  case ISD::SRL:
    // Right shift immediate
    if (isVShiftRImm(Op.getOperand(1), VT, false, Cnt) && Cnt < EltSize) {
      unsigned Opc =
          (Op.getOpcode() == ISD::SRA) ? AArch64ISD::VASHR : AArch64ISD::VLSHR;
      return DAG.getNode(Opc, DL, VT, Op.getOperand(0),
                         DAG.getConstant(Cnt, DL, MVT::i32));
    }

    // Right shift register.  Note, there is not a shift right register
    // instruction, but the shift left register instruction takes a signed
    // value, where negative numbers specify a right shift.
    unsigned Opc = (Op.getOpcode() == ISD::SRA) ? Intrinsic::aarch64_neon_sshl
                                                : Intrinsic::aarch64_neon_ushl;
    // negate the shift amount
    SDValue NegShift = DAG.getNode(AArch64ISD::NEG, DL, VT, Op.getOperand(1));
    SDValue NegShiftLeft =
        DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VT,
                    DAG.getConstant(Opc, DL, MVT::i32), Op.getOperand(0),
                    NegShift);
    return NegShiftLeft;
  }

  return SDValue();
}

static SDValue EmitVectorComparison(SDValue LHS, SDValue RHS,
                                    AArch64CC::CondCode CC, bool NoNans, EVT VT,
                                    const SDLoc &dl, SelectionDAG &DAG) {
  EVT SrcVT = LHS.getValueType();
  assert(VT.getSizeInBits() == SrcVT.getSizeInBits() &&
         "function only supposed to emit natural comparisons");

  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(RHS.getNode());
  APInt CnstBits(VT.getSizeInBits(), 0);
  APInt UndefBits(VT.getSizeInBits(), 0);
  bool IsCnst = BVN && resolveBuildVector(BVN, CnstBits, UndefBits);
  bool IsZero = IsCnst && (CnstBits == 0);

  if (SrcVT.getVectorElementType().isFloatingPoint()) {
    switch (CC) {
    default:
      return SDValue();
    case AArch64CC::NE: {
      SDValue Fcmeq;
      if (IsZero)
        Fcmeq = DAG.getNode(AArch64ISD::FCMEQz, dl, VT, LHS);
      else
        Fcmeq = DAG.getNode(AArch64ISD::FCMEQ, dl, VT, LHS, RHS);
      return DAG.getNode(AArch64ISD::NOT, dl, VT, Fcmeq);
    }
    case AArch64CC::EQ:
      if (IsZero)
        return DAG.getNode(AArch64ISD::FCMEQz, dl, VT, LHS);
      return DAG.getNode(AArch64ISD::FCMEQ, dl, VT, LHS, RHS);
    case AArch64CC::GE:
      if (IsZero)
        return DAG.getNode(AArch64ISD::FCMGEz, dl, VT, LHS);
      return DAG.getNode(AArch64ISD::FCMGE, dl, VT, LHS, RHS);
    case AArch64CC::GT:
      if (IsZero)
        return DAG.getNode(AArch64ISD::FCMGTz, dl, VT, LHS);
      return DAG.getNode(AArch64ISD::FCMGT, dl, VT, LHS, RHS);
    case AArch64CC::LS:
      if (IsZero)
        return DAG.getNode(AArch64ISD::FCMLEz, dl, VT, LHS);
      return DAG.getNode(AArch64ISD::FCMGE, dl, VT, RHS, LHS);
    case AArch64CC::LT:
      if (!NoNans)
        return SDValue();
      // If we ignore NaNs then we can use to the MI implementation.
      LLVM_FALLTHROUGH;
    case AArch64CC::MI:
      if (IsZero)
        return DAG.getNode(AArch64ISD::FCMLTz, dl, VT, LHS);
      return DAG.getNode(AArch64ISD::FCMGT, dl, VT, RHS, LHS);
    }
  }

  switch (CC) {
  default:
    return SDValue();
  case AArch64CC::NE: {
    SDValue Cmeq;
    if (IsZero)
      Cmeq = DAG.getNode(AArch64ISD::CMEQz, dl, VT, LHS);
    else
      Cmeq = DAG.getNode(AArch64ISD::CMEQ, dl, VT, LHS, RHS);
    return DAG.getNode(AArch64ISD::NOT, dl, VT, Cmeq);
  }
  case AArch64CC::EQ:
    if (IsZero)
      return DAG.getNode(AArch64ISD::CMEQz, dl, VT, LHS);
    return DAG.getNode(AArch64ISD::CMEQ, dl, VT, LHS, RHS);
  case AArch64CC::GE:
    if (IsZero)
      return DAG.getNode(AArch64ISD::CMGEz, dl, VT, LHS);
    return DAG.getNode(AArch64ISD::CMGE, dl, VT, LHS, RHS);
  case AArch64CC::GT:
    if (IsZero)
      return DAG.getNode(AArch64ISD::CMGTz, dl, VT, LHS);
    return DAG.getNode(AArch64ISD::CMGT, dl, VT, LHS, RHS);
  case AArch64CC::LE:
    if (IsZero)
      return DAG.getNode(AArch64ISD::CMLEz, dl, VT, LHS);
    return DAG.getNode(AArch64ISD::CMGE, dl, VT, RHS, LHS);
  case AArch64CC::LS:
    return DAG.getNode(AArch64ISD::CMHS, dl, VT, RHS, LHS);
  case AArch64CC::LO:
    return DAG.getNode(AArch64ISD::CMHI, dl, VT, RHS, LHS);
  case AArch64CC::LT:
    if (IsZero)
      return DAG.getNode(AArch64ISD::CMLTz, dl, VT, LHS);
    return DAG.getNode(AArch64ISD::CMGT, dl, VT, RHS, LHS);
  case AArch64CC::HI:
    return DAG.getNode(AArch64ISD::CMHI, dl, VT, LHS, RHS);
  case AArch64CC::HS:
    return DAG.getNode(AArch64ISD::CMHS, dl, VT, LHS, RHS);
  }
}

SDValue AArch64TargetLowering::LowerVSETCC(SDValue Op,
                                           SelectionDAG &DAG) const {
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  EVT CmpVT = LHS.getValueType().changeVectorElementTypeToInteger();
  SDLoc dl(Op);

  if (LHS.getValueType().getVectorElementType().isInteger()) {
    assert(LHS.getValueType() == RHS.getValueType());
    AArch64CC::CondCode AArch64CC = changeIntCCToAArch64CC(CC);
    SDValue Cmp =
        EmitVectorComparison(LHS, RHS, AArch64CC, false, CmpVT, dl, DAG);
    return DAG.getSExtOrTrunc(Cmp, dl, Op.getValueType());
  }

  const bool FullFP16 =
    static_cast<const AArch64Subtarget &>(DAG.getSubtarget()).hasFullFP16();

  // Make v4f16 (only) fcmp operations utilise vector instructions
  // v8f16 support will be a litle more complicated
  if (LHS.getValueType().getVectorElementType() == MVT::f16) {
    if (!FullFP16 && LHS.getValueType().getVectorNumElements() == 4) {
      LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::v4f32, LHS);
      RHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::v4f32, RHS);
      SDValue NewSetcc = DAG.getSetCC(dl, MVT::v4i16, LHS, RHS, CC);
      DAG.ReplaceAllUsesWith(Op, NewSetcc);
      CmpVT = MVT::v4i32;
    } else
      return SDValue();
  }

  assert(LHS.getValueType().getVectorElementType() == MVT::f32 ||
         LHS.getValueType().getVectorElementType() == MVT::f64);

  // Unfortunately, the mapping of LLVM FP CC's onto AArch64 CC's isn't totally
  // clean.  Some of them require two branches to implement.
  AArch64CC::CondCode CC1, CC2;
  bool ShouldInvert;
  changeVectorFPCCToAArch64CC(CC, CC1, CC2, ShouldInvert);

  bool NoNaNs = getTargetMachine().Options.NoNaNsFPMath;
  SDValue Cmp =
      EmitVectorComparison(LHS, RHS, CC1, NoNaNs, CmpVT, dl, DAG);
  if (!Cmp.getNode())
    return SDValue();

  if (CC2 != AArch64CC::AL) {
    SDValue Cmp2 =
        EmitVectorComparison(LHS, RHS, CC2, NoNaNs, CmpVT, dl, DAG);
    if (!Cmp2.getNode())
      return SDValue();

    Cmp = DAG.getNode(ISD::OR, dl, CmpVT, Cmp, Cmp2);
  }

  Cmp = DAG.getSExtOrTrunc(Cmp, dl, Op.getValueType());

  if (ShouldInvert)
    Cmp = DAG.getNOT(dl, Cmp, Cmp.getValueType());

  return Cmp;
}

static SDValue getReductionSDNode(unsigned Op, SDLoc DL, SDValue ScalarOp,
                                  SelectionDAG &DAG) {
  SDValue VecOp = ScalarOp.getOperand(0);
  auto Rdx = DAG.getNode(Op, DL, VecOp.getSimpleValueType(), VecOp);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, ScalarOp.getValueType(), Rdx,
                     DAG.getConstant(0, DL, MVT::i64));
}

SDValue AArch64TargetLowering::LowerVECREDUCE(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc dl(Op);
  switch (Op.getOpcode()) {
  case ISD::VECREDUCE_ADD:
    return getReductionSDNode(AArch64ISD::UADDV, dl, Op, DAG);
  case ISD::VECREDUCE_SMAX:
    return getReductionSDNode(AArch64ISD::SMAXV, dl, Op, DAG);
  case ISD::VECREDUCE_SMIN:
    return getReductionSDNode(AArch64ISD::SMINV, dl, Op, DAG);
  case ISD::VECREDUCE_UMAX:
    return getReductionSDNode(AArch64ISD::UMAXV, dl, Op, DAG);
  case ISD::VECREDUCE_UMIN:
    return getReductionSDNode(AArch64ISD::UMINV, dl, Op, DAG);
  case ISD::VECREDUCE_FMAX: {
    assert(Op->getFlags().hasNoNaNs() && "fmax vector reduction needs NoNaN flag");
    return DAG.getNode(
        ISD::INTRINSIC_WO_CHAIN, dl, Op.getValueType(),
        DAG.getConstant(Intrinsic::aarch64_neon_fmaxnmv, dl, MVT::i32),
        Op.getOperand(0));
  }
  case ISD::VECREDUCE_FMIN: {
    assert(Op->getFlags().hasNoNaNs() && "fmin vector reduction needs NoNaN flag");
    return DAG.getNode(
        ISD::INTRINSIC_WO_CHAIN, dl, Op.getValueType(),
        DAG.getConstant(Intrinsic::aarch64_neon_fminnmv, dl, MVT::i32),
        Op.getOperand(0));
  }
  default:
    llvm_unreachable("Unhandled reduction");
  }
}

SDValue AArch64TargetLowering::LowerATOMIC_LOAD_SUB(SDValue Op,
                                                    SelectionDAG &DAG) const {
  auto &Subtarget = static_cast<const AArch64Subtarget &>(DAG.getSubtarget());
  if (!Subtarget.hasLSE())
    return SDValue();

  // LSE has an atomic load-add instruction, but not a load-sub.
  SDLoc dl(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue RHS = Op.getOperand(2);
  AtomicSDNode *AN = cast<AtomicSDNode>(Op.getNode());
  RHS = DAG.getNode(ISD::SUB, dl, VT, DAG.getConstant(0, dl, VT), RHS);
  return DAG.getAtomic(ISD::ATOMIC_LOAD_ADD, dl, AN->getMemoryVT(),
                       Op.getOperand(0), Op.getOperand(1), RHS,
                       AN->getMemOperand());
}

SDValue AArch64TargetLowering::LowerATOMIC_LOAD_AND(SDValue Op,
                                                    SelectionDAG &DAG) const {
  auto &Subtarget = static_cast<const AArch64Subtarget &>(DAG.getSubtarget());
  if (!Subtarget.hasLSE())
    return SDValue();

  // LSE has an atomic load-clear instruction, but not a load-and.
  SDLoc dl(Op);
  MVT VT = Op.getSimpleValueType();
  SDValue RHS = Op.getOperand(2);
  AtomicSDNode *AN = cast<AtomicSDNode>(Op.getNode());
  RHS = DAG.getNode(ISD::XOR, dl, VT, DAG.getConstant(-1ULL, dl, VT), RHS);
  return DAG.getAtomic(ISD::ATOMIC_LOAD_CLR, dl, AN->getMemoryVT(),
                       Op.getOperand(0), Op.getOperand(1), RHS,
                       AN->getMemOperand());
}

SDValue AArch64TargetLowering::LowerWindowsDYNAMIC_STACKALLOC(
    SDValue Op, SDValue Chain, SDValue &Size, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Callee = DAG.getTargetExternalSymbol("__chkstk", PtrVT, 0);

  const AArch64RegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *Mask = TRI->getWindowsStackProbePreservedMask();
  if (Subtarget->hasCustomCallingConv())
    TRI->UpdateCustomCallPreservedMask(DAG.getMachineFunction(), &Mask);

  Size = DAG.getNode(ISD::SRL, dl, MVT::i64, Size,
                     DAG.getConstant(4, dl, MVT::i64));
  Chain = DAG.getCopyToReg(Chain, dl, AArch64::X15, Size, SDValue());
  Chain =
      DAG.getNode(AArch64ISD::CALL, dl, DAG.getVTList(MVT::Other, MVT::Glue),
                  Chain, Callee, DAG.getRegister(AArch64::X15, MVT::i64),
                  DAG.getRegisterMask(Mask), Chain.getValue(1));
  // To match the actual intent better, we should read the output from X15 here
  // again (instead of potentially spilling it to the stack), but rereading Size
  // from X15 here doesn't work at -O0, since it thinks that X15 is undefined
  // here.

  Size = DAG.getNode(ISD::SHL, dl, MVT::i64, Size,
                     DAG.getConstant(4, dl, MVT::i64));
  return Chain;
}

SDValue
AArch64TargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                               SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() &&
         "Only Windows alloca probing supported");
  SDLoc dl(Op);
  // Get the inputs.
  SDNode *Node = Op.getNode();
  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  unsigned Align = cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue();
  EVT VT = Node->getValueType(0);

  if (DAG.getMachineFunction().getFunction().hasFnAttribute(
          "no-stack-arg-probe")) {
    SDValue SP = DAG.getCopyFromReg(Chain, dl, AArch64::SP, MVT::i64);
    Chain = SP.getValue(1);
    SP = DAG.getNode(ISD::SUB, dl, MVT::i64, SP, Size);
    if (Align)
      SP = DAG.getNode(ISD::AND, dl, VT, SP.getValue(0),
                       DAG.getConstant(-(uint64_t)Align, dl, VT));
    Chain = DAG.getCopyToReg(Chain, dl, AArch64::SP, SP);
    SDValue Ops[2] = {SP, Chain};
    return DAG.getMergeValues(Ops, dl);
  }

  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, dl);

  Chain = LowerWindowsDYNAMIC_STACKALLOC(Op, Chain, Size, DAG);

  SDValue SP = DAG.getCopyFromReg(Chain, dl, AArch64::SP, MVT::i64);
  Chain = SP.getValue(1);
  SP = DAG.getNode(ISD::SUB, dl, MVT::i64, SP, Size);
  if (Align)
    SP = DAG.getNode(ISD::AND, dl, VT, SP.getValue(0),
                     DAG.getConstant(-(uint64_t)Align, dl, VT));
  Chain = DAG.getCopyToReg(Chain, dl, AArch64::SP, SP);

  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(0, dl, true),
                             DAG.getIntPtrConstant(0, dl, true), SDValue(), dl);

  SDValue Ops[2] = {SP, Chain};
  return DAG.getMergeValues(Ops, dl);
}

/// getTgtMemIntrinsic - Represent NEON load and store intrinsics as
/// MemIntrinsicNodes.  The associated MachineMemOperands record the alignment
/// specified in the intrinsic calls.
bool AArch64TargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                               const CallInst &I,
                                               MachineFunction &MF,
                                               unsigned Intrinsic) const {
  auto &DL = I.getModule()->getDataLayout();
  switch (Intrinsic) {
  case Intrinsic::aarch64_neon_ld2:
  case Intrinsic::aarch64_neon_ld3:
  case Intrinsic::aarch64_neon_ld4:
  case Intrinsic::aarch64_neon_ld1x2:
  case Intrinsic::aarch64_neon_ld1x3:
  case Intrinsic::aarch64_neon_ld1x4:
  case Intrinsic::aarch64_neon_ld2lane:
  case Intrinsic::aarch64_neon_ld3lane:
  case Intrinsic::aarch64_neon_ld4lane:
  case Intrinsic::aarch64_neon_ld2r:
  case Intrinsic::aarch64_neon_ld3r:
  case Intrinsic::aarch64_neon_ld4r: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // Conservatively set memVT to the entire set of vectors loaded.
    uint64_t NumElts = DL.getTypeSizeInBits(I.getType()) / 64;
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(I.getNumArgOperands() - 1);
    Info.offset = 0;
    Info.align = 0;
    // volatile loads with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::aarch64_neon_st2:
  case Intrinsic::aarch64_neon_st3:
  case Intrinsic::aarch64_neon_st4:
  case Intrinsic::aarch64_neon_st1x2:
  case Intrinsic::aarch64_neon_st1x3:
  case Intrinsic::aarch64_neon_st1x4:
  case Intrinsic::aarch64_neon_st2lane:
  case Intrinsic::aarch64_neon_st3lane:
  case Intrinsic::aarch64_neon_st4lane: {
    Info.opc = ISD::INTRINSIC_VOID;
    // Conservatively set memVT to the entire set of vectors stored.
    unsigned NumElts = 0;
    for (unsigned ArgI = 0, ArgE = I.getNumArgOperands(); ArgI < ArgE; ++ArgI) {
      Type *ArgTy = I.getArgOperand(ArgI)->getType();
      if (!ArgTy->isVectorTy())
        break;
      NumElts += DL.getTypeSizeInBits(ArgTy) / 64;
    }
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(I.getNumArgOperands() - 1);
    Info.offset = 0;
    Info.align = 0;
    // volatile stores with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::aarch64_ldaxr:
  case Intrinsic::aarch64_ldxr: {
    PointerType *PtrTy = cast<PointerType>(I.getArgOperand(0)->getType());
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(PtrTy->getElementType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = DL.getABITypeAlignment(PtrTy->getElementType());
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::aarch64_stlxr:
  case Intrinsic::aarch64_stxr: {
    PointerType *PtrTy = cast<PointerType>(I.getArgOperand(1)->getType());
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(PtrTy->getElementType());
    Info.ptrVal = I.getArgOperand(1);
    Info.offset = 0;
    Info.align = DL.getABITypeAlignment(PtrTy->getElementType());
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::aarch64_ldaxp:
  case Intrinsic::aarch64_ldxp:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i128;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = 16;
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
    return true;
  case Intrinsic::aarch64_stlxp:
  case Intrinsic::aarch64_stxp:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i128;
    Info.ptrVal = I.getArgOperand(2);
    Info.offset = 0;
    Info.align = 16;
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;
  default:
    break;
  }

  return false;
}

bool AArch64TargetLowering::shouldReduceLoadWidth(SDNode *Load,
                                                  ISD::LoadExtType ExtTy,
                                                  EVT NewVT) const {
  // TODO: This may be worth removing. Check regression tests for diffs.
  if (!TargetLoweringBase::shouldReduceLoadWidth(Load, ExtTy, NewVT))
    return false;

  // If we're reducing the load width in order to avoid having to use an extra
  // instruction to do extension then it's probably a good idea.
  if (ExtTy != ISD::NON_EXTLOAD)
    return true;
  // Don't reduce load width if it would prevent us from combining a shift into
  // the offset.
  MemSDNode *Mem = dyn_cast<MemSDNode>(Load);
  assert(Mem);
  const SDValue &Base = Mem->getBasePtr();
  if (Base.getOpcode() == ISD::ADD &&
      Base.getOperand(1).getOpcode() == ISD::SHL &&
      Base.getOperand(1).hasOneUse() &&
      Base.getOperand(1).getOperand(1).getOpcode() == ISD::Constant) {
    // The shift can be combined if it matches the size of the value being
    // loaded (and so reducing the width would make it not match).
    uint64_t ShiftAmount = Base.getOperand(1).getConstantOperandVal(1);
    uint64_t LoadBytes = Mem->getMemoryVT().getSizeInBits()/8;
    if (ShiftAmount == Log2_32(LoadBytes))
      return false;
  }
  // We have no reason to disallow reducing the load width, so allow it.
  return true;
}

// Truncations from 64-bit GPR to 32-bit GPR is free.
bool AArch64TargetLowering::isTruncateFree(Type *Ty1, Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;
  unsigned NumBits1 = Ty1->getPrimitiveSizeInBits();
  unsigned NumBits2 = Ty2->getPrimitiveSizeInBits();
  return NumBits1 > NumBits2;
}
bool AArch64TargetLowering::isTruncateFree(EVT VT1, EVT VT2) const {
  if (VT1.isVector() || VT2.isVector() || !VT1.isInteger() || !VT2.isInteger())
    return false;
  unsigned NumBits1 = VT1.getSizeInBits();
  unsigned NumBits2 = VT2.getSizeInBits();
  return NumBits1 > NumBits2;
}

/// Check if it is profitable to hoist instruction in then/else to if.
/// Not profitable if I and it's user can form a FMA instruction
/// because we prefer FMSUB/FMADD.
bool AArch64TargetLowering::isProfitableToHoist(Instruction *I) const {
  if (I->getOpcode() != Instruction::FMul)
    return true;

  if (!I->hasOneUse())
    return true;

  Instruction *User = I->user_back();

  if (User &&
      !(User->getOpcode() == Instruction::FSub ||
        User->getOpcode() == Instruction::FAdd))
    return true;

  const TargetOptions &Options = getTargetMachine().Options;
  const DataLayout &DL = I->getModule()->getDataLayout();
  EVT VT = getValueType(DL, User->getOperand(0)->getType());

  return !(isFMAFasterThanFMulAndFAdd(VT) &&
           isOperationLegalOrCustom(ISD::FMA, VT) &&
           (Options.AllowFPOpFusion == FPOpFusion::Fast ||
            Options.UnsafeFPMath));
}

// All 32-bit GPR operations implicitly zero the high-half of the corresponding
// 64-bit GPR.
bool AArch64TargetLowering::isZExtFree(Type *Ty1, Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;
  unsigned NumBits1 = Ty1->getPrimitiveSizeInBits();
  unsigned NumBits2 = Ty2->getPrimitiveSizeInBits();
  return NumBits1 == 32 && NumBits2 == 64;
}
bool AArch64TargetLowering::isZExtFree(EVT VT1, EVT VT2) const {
  if (VT1.isVector() || VT2.isVector() || !VT1.isInteger() || !VT2.isInteger())
    return false;
  unsigned NumBits1 = VT1.getSizeInBits();
  unsigned NumBits2 = VT2.getSizeInBits();
  return NumBits1 == 32 && NumBits2 == 64;
}

bool AArch64TargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  EVT VT1 = Val.getValueType();
  if (isZExtFree(VT1, VT2)) {
    return true;
  }

  if (Val.getOpcode() != ISD::LOAD)
    return false;

  // 8-, 16-, and 32-bit integer loads all implicitly zero-extend.
  return (VT1.isSimple() && !VT1.isVector() && VT1.isInteger() &&
          VT2.isSimple() && !VT2.isVector() && VT2.isInteger() &&
          VT1.getSizeInBits() <= 32);
}

bool AArch64TargetLowering::isExtFreeImpl(const Instruction *Ext) const {
  if (isa<FPExtInst>(Ext))
    return false;

  // Vector types are not free.
  if (Ext->getType()->isVectorTy())
    return false;

  for (const Use &U : Ext->uses()) {
    // The extension is free if we can fold it with a left shift in an
    // addressing mode or an arithmetic operation: add, sub, and cmp.

    // Is there a shift?
    const Instruction *Instr = cast<Instruction>(U.getUser());

    // Is this a constant shift?
    switch (Instr->getOpcode()) {
    case Instruction::Shl:
      if (!isa<ConstantInt>(Instr->getOperand(1)))
        return false;
      break;
    case Instruction::GetElementPtr: {
      gep_type_iterator GTI = gep_type_begin(Instr);
      auto &DL = Ext->getModule()->getDataLayout();
      std::advance(GTI, U.getOperandNo()-1);
      Type *IdxTy = GTI.getIndexedType();
      // This extension will end up with a shift because of the scaling factor.
      // 8-bit sized types have a scaling factor of 1, thus a shift amount of 0.
      // Get the shift amount based on the scaling factor:
      // log2(sizeof(IdxTy)) - log2(8).
      uint64_t ShiftAmt =
          countTrailingZeros(DL.getTypeStoreSizeInBits(IdxTy)) - 3;
      // Is the constant foldable in the shift of the addressing mode?
      // I.e., shift amount is between 1 and 4 inclusive.
      if (ShiftAmt == 0 || ShiftAmt > 4)
        return false;
      break;
    }
    case Instruction::Trunc:
      // Check if this is a noop.
      // trunc(sext ty1 to ty2) to ty1.
      if (Instr->getType() == Ext->getOperand(0)->getType())
        continue;
      LLVM_FALLTHROUGH;
    default:
      return false;
    }

    // At this point we can use the bfm family, so this extension is free
    // for that use.
  }
  return true;
}

bool AArch64TargetLowering::hasPairedLoad(EVT LoadedType,
                                          unsigned &RequiredAligment) const {
  if (!LoadedType.isSimple() ||
      (!LoadedType.isInteger() && !LoadedType.isFloatingPoint()))
    return false;
  // Cyclone supports unaligned accesses.
  RequiredAligment = 0;
  unsigned NumBits = LoadedType.getSizeInBits();
  return NumBits == 32 || NumBits == 64;
}

/// A helper function for determining the number of interleaved accesses we
/// will generate when lowering accesses of the given type.
unsigned
AArch64TargetLowering::getNumInterleavedAccesses(VectorType *VecTy,
                                                 const DataLayout &DL) const {
  return (DL.getTypeSizeInBits(VecTy) + 127) / 128;
}

MachineMemOperand::Flags
AArch64TargetLowering::getMMOFlags(const Instruction &I) const {
  if (Subtarget->getProcFamily() == AArch64Subtarget::Falkor &&
      I.getMetadata(FALKOR_STRIDED_ACCESS_MD) != nullptr)
    return MOStridedAccess;
  return MachineMemOperand::MONone;
}

bool AArch64TargetLowering::isLegalInterleavedAccessType(
    VectorType *VecTy, const DataLayout &DL) const {

  unsigned VecSize = DL.getTypeSizeInBits(VecTy);
  unsigned ElSize = DL.getTypeSizeInBits(VecTy->getElementType());

  // Ensure the number of vector elements is greater than 1.
  if (VecTy->getNumElements() < 2)
    return false;

  // Ensure the element type is legal.
  if (ElSize != 8 && ElSize != 16 && ElSize != 32 && ElSize != 64)
    return false;

  // Ensure the total vector size is 64 or a multiple of 128. Types larger than
  // 128 will be split into multiple interleaved accesses.
  return VecSize == 64 || VecSize % 128 == 0;
}

/// Lower an interleaved load into a ldN intrinsic.
///
/// E.g. Lower an interleaved load (Factor = 2):
///        %wide.vec = load <8 x i32>, <8 x i32>* %ptr
///        %v0 = shuffle %wide.vec, undef, <0, 2, 4, 6>  ; Extract even elements
///        %v1 = shuffle %wide.vec, undef, <1, 3, 5, 7>  ; Extract odd elements
///
///      Into:
///        %ld2 = { <4 x i32>, <4 x i32> } call llvm.aarch64.neon.ld2(%ptr)
///        %vec0 = extractelement { <4 x i32>, <4 x i32> } %ld2, i32 0
///        %vec1 = extractelement { <4 x i32>, <4 x i32> } %ld2, i32 1
bool AArch64TargetLowering::lowerInterleavedLoad(
    LoadInst *LI, ArrayRef<ShuffleVectorInst *> Shuffles,
    ArrayRef<unsigned> Indices, unsigned Factor) const {
  assert(Factor >= 2 && Factor <= getMaxSupportedInterleaveFactor() &&
         "Invalid interleave factor");
  assert(!Shuffles.empty() && "Empty shufflevector input");
  assert(Shuffles.size() == Indices.size() &&
         "Unmatched number of shufflevectors and indices");

  const DataLayout &DL = LI->getModule()->getDataLayout();

  VectorType *VecTy = Shuffles[0]->getType();

  // Skip if we do not have NEON and skip illegal vector types. We can
  // "legalize" wide vector types into multiple interleaved accesses as long as
  // the vector types are divisible by 128.
  if (!Subtarget->hasNEON() || !isLegalInterleavedAccessType(VecTy, DL))
    return false;

  unsigned NumLoads = getNumInterleavedAccesses(VecTy, DL);

  // A pointer vector can not be the return type of the ldN intrinsics. Need to
  // load integer vectors first and then convert to pointer vectors.
  Type *EltTy = VecTy->getVectorElementType();
  if (EltTy->isPointerTy())
    VecTy =
        VectorType::get(DL.getIntPtrType(EltTy), VecTy->getVectorNumElements());

  IRBuilder<> Builder(LI);

  // The base address of the load.
  Value *BaseAddr = LI->getPointerOperand();

  if (NumLoads > 1) {
    // If we're going to generate more than one load, reset the sub-vector type
    // to something legal.
    VecTy = VectorType::get(VecTy->getVectorElementType(),
                            VecTy->getVectorNumElements() / NumLoads);

    // We will compute the pointer operand of each load from the original base
    // address using GEPs. Cast the base address to a pointer to the scalar
    // element type.
    BaseAddr = Builder.CreateBitCast(
        BaseAddr, VecTy->getVectorElementType()->getPointerTo(
                      LI->getPointerAddressSpace()));
  }

  Type *PtrTy = VecTy->getPointerTo(LI->getPointerAddressSpace());
  Type *Tys[2] = {VecTy, PtrTy};
  static const Intrinsic::ID LoadInts[3] = {Intrinsic::aarch64_neon_ld2,
                                            Intrinsic::aarch64_neon_ld3,
                                            Intrinsic::aarch64_neon_ld4};
  Function *LdNFunc =
      Intrinsic::getDeclaration(LI->getModule(), LoadInts[Factor - 2], Tys);

  // Holds sub-vectors extracted from the load intrinsic return values. The
  // sub-vectors are associated with the shufflevector instructions they will
  // replace.
  DenseMap<ShuffleVectorInst *, SmallVector<Value *, 4>> SubVecs;

  for (unsigned LoadCount = 0; LoadCount < NumLoads; ++LoadCount) {

    // If we're generating more than one load, compute the base address of
    // subsequent loads as an offset from the previous.
    if (LoadCount > 0)
      BaseAddr = Builder.CreateConstGEP1_32(
          BaseAddr, VecTy->getVectorNumElements() * Factor);

    CallInst *LdN = Builder.CreateCall(
        LdNFunc, Builder.CreateBitCast(BaseAddr, PtrTy), "ldN");

    // Extract and store the sub-vectors returned by the load intrinsic.
    for (unsigned i = 0; i < Shuffles.size(); i++) {
      ShuffleVectorInst *SVI = Shuffles[i];
      unsigned Index = Indices[i];

      Value *SubVec = Builder.CreateExtractValue(LdN, Index);

      // Convert the integer vector to pointer vector if the element is pointer.
      if (EltTy->isPointerTy())
        SubVec = Builder.CreateIntToPtr(
            SubVec, VectorType::get(SVI->getType()->getVectorElementType(),
                                    VecTy->getVectorNumElements()));
      SubVecs[SVI].push_back(SubVec);
    }
  }

  // Replace uses of the shufflevector instructions with the sub-vectors
  // returned by the load intrinsic. If a shufflevector instruction is
  // associated with more than one sub-vector, those sub-vectors will be
  // concatenated into a single wide vector.
  for (ShuffleVectorInst *SVI : Shuffles) {
    auto &SubVec = SubVecs[SVI];
    auto *WideVec =
        SubVec.size() > 1 ? concatenateVectors(Builder, SubVec) : SubVec[0];
    SVI->replaceAllUsesWith(WideVec);
  }

  return true;
}

/// Lower an interleaved store into a stN intrinsic.
///
/// E.g. Lower an interleaved store (Factor = 3):
///        %i.vec = shuffle <8 x i32> %v0, <8 x i32> %v1,
///                 <0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11>
///        store <12 x i32> %i.vec, <12 x i32>* %ptr
///
///      Into:
///        %sub.v0 = shuffle <8 x i32> %v0, <8 x i32> v1, <0, 1, 2, 3>
///        %sub.v1 = shuffle <8 x i32> %v0, <8 x i32> v1, <4, 5, 6, 7>
///        %sub.v2 = shuffle <8 x i32> %v0, <8 x i32> v1, <8, 9, 10, 11>
///        call void llvm.aarch64.neon.st3(%sub.v0, %sub.v1, %sub.v2, %ptr)
///
/// Note that the new shufflevectors will be removed and we'll only generate one
/// st3 instruction in CodeGen.
///
/// Example for a more general valid mask (Factor 3). Lower:
///        %i.vec = shuffle <32 x i32> %v0, <32 x i32> %v1,
///                 <4, 32, 16, 5, 33, 17, 6, 34, 18, 7, 35, 19>
///        store <12 x i32> %i.vec, <12 x i32>* %ptr
///
///      Into:
///        %sub.v0 = shuffle <32 x i32> %v0, <32 x i32> v1, <4, 5, 6, 7>
///        %sub.v1 = shuffle <32 x i32> %v0, <32 x i32> v1, <32, 33, 34, 35>
///        %sub.v2 = shuffle <32 x i32> %v0, <32 x i32> v1, <16, 17, 18, 19>
///        call void llvm.aarch64.neon.st3(%sub.v0, %sub.v1, %sub.v2, %ptr)
bool AArch64TargetLowering::lowerInterleavedStore(StoreInst *SI,
                                                  ShuffleVectorInst *SVI,
                                                  unsigned Factor) const {
  assert(Factor >= 2 && Factor <= getMaxSupportedInterleaveFactor() &&
         "Invalid interleave factor");

  VectorType *VecTy = SVI->getType();
  assert(VecTy->getVectorNumElements() % Factor == 0 &&
         "Invalid interleaved store");

  unsigned LaneLen = VecTy->getVectorNumElements() / Factor;
  Type *EltTy = VecTy->getVectorElementType();
  VectorType *SubVecTy = VectorType::get(EltTy, LaneLen);

  const DataLayout &DL = SI->getModule()->getDataLayout();

  // Skip if we do not have NEON and skip illegal vector types. We can
  // "legalize" wide vector types into multiple interleaved accesses as long as
  // the vector types are divisible by 128.
  if (!Subtarget->hasNEON() || !isLegalInterleavedAccessType(SubVecTy, DL))
    return false;

  unsigned NumStores = getNumInterleavedAccesses(SubVecTy, DL);

  Value *Op0 = SVI->getOperand(0);
  Value *Op1 = SVI->getOperand(1);
  IRBuilder<> Builder(SI);

  // StN intrinsics don't support pointer vectors as arguments. Convert pointer
  // vectors to integer vectors.
  if (EltTy->isPointerTy()) {
    Type *IntTy = DL.getIntPtrType(EltTy);
    unsigned NumOpElts = Op0->getType()->getVectorNumElements();

    // Convert to the corresponding integer vector.
    Type *IntVecTy = VectorType::get(IntTy, NumOpElts);
    Op0 = Builder.CreatePtrToInt(Op0, IntVecTy);
    Op1 = Builder.CreatePtrToInt(Op1, IntVecTy);

    SubVecTy = VectorType::get(IntTy, LaneLen);
  }

  // The base address of the store.
  Value *BaseAddr = SI->getPointerOperand();

  if (NumStores > 1) {
    // If we're going to generate more than one store, reset the lane length
    // and sub-vector type to something legal.
    LaneLen /= NumStores;
    SubVecTy = VectorType::get(SubVecTy->getVectorElementType(), LaneLen);

    // We will compute the pointer operand of each store from the original base
    // address using GEPs. Cast the base address to a pointer to the scalar
    // element type.
    BaseAddr = Builder.CreateBitCast(
        BaseAddr, SubVecTy->getVectorElementType()->getPointerTo(
                      SI->getPointerAddressSpace()));
  }

  auto Mask = SVI->getShuffleMask();

  Type *PtrTy = SubVecTy->getPointerTo(SI->getPointerAddressSpace());
  Type *Tys[2] = {SubVecTy, PtrTy};
  static const Intrinsic::ID StoreInts[3] = {Intrinsic::aarch64_neon_st2,
                                             Intrinsic::aarch64_neon_st3,
                                             Intrinsic::aarch64_neon_st4};
  Function *StNFunc =
      Intrinsic::getDeclaration(SI->getModule(), StoreInts[Factor - 2], Tys);

  for (unsigned StoreCount = 0; StoreCount < NumStores; ++StoreCount) {

    SmallVector<Value *, 5> Ops;

    // Split the shufflevector operands into sub vectors for the new stN call.
    for (unsigned i = 0; i < Factor; i++) {
      unsigned IdxI = StoreCount * LaneLen * Factor + i;
      if (Mask[IdxI] >= 0) {
        Ops.push_back(Builder.CreateShuffleVector(
            Op0, Op1, createSequentialMask(Builder, Mask[IdxI], LaneLen, 0)));
      } else {
        unsigned StartMask = 0;
        for (unsigned j = 1; j < LaneLen; j++) {
          unsigned IdxJ = StoreCount * LaneLen * Factor + j;
          if (Mask[IdxJ * Factor + IdxI] >= 0) {
            StartMask = Mask[IdxJ * Factor + IdxI] - IdxJ;
            break;
          }
        }
        // Note: Filling undef gaps with random elements is ok, since
        // those elements were being written anyway (with undefs).
        // In the case of all undefs we're defaulting to using elems from 0
        // Note: StartMask cannot be negative, it's checked in
        // isReInterleaveMask
        Ops.push_back(Builder.CreateShuffleVector(
            Op0, Op1, createSequentialMask(Builder, StartMask, LaneLen, 0)));
      }
    }

    // If we generating more than one store, we compute the base address of
    // subsequent stores as an offset from the previous.
    if (StoreCount > 0)
      BaseAddr = Builder.CreateConstGEP1_32(BaseAddr, LaneLen * Factor);

    Ops.push_back(Builder.CreateBitCast(BaseAddr, PtrTy));
    Builder.CreateCall(StNFunc, Ops);
  }
  return true;
}

static bool memOpAlign(unsigned DstAlign, unsigned SrcAlign,
                       unsigned AlignCheck) {
  return ((SrcAlign == 0 || SrcAlign % AlignCheck == 0) &&
          (DstAlign == 0 || DstAlign % AlignCheck == 0));
}

EVT AArch64TargetLowering::getOptimalMemOpType(uint64_t Size, unsigned DstAlign,
                                               unsigned SrcAlign, bool IsMemset,
                                               bool ZeroMemset,
                                               bool MemcpyStrSrc,
                                               MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  bool CanImplicitFloat = !F.hasFnAttribute(Attribute::NoImplicitFloat);
  bool CanUseNEON = Subtarget->hasNEON() && CanImplicitFloat;
  bool CanUseFP = Subtarget->hasFPARMv8() && CanImplicitFloat;
  // Only use AdvSIMD to implement memset of 32-byte and above. It would have
  // taken one instruction to materialize the v2i64 zero and one store (with
  // restrictive addressing mode). Just do i64 stores.
  bool IsSmallMemset = IsMemset && Size < 32;
  auto AlignmentIsAcceptable = [&](EVT VT, unsigned AlignCheck) {
    if (memOpAlign(SrcAlign, DstAlign, AlignCheck))
      return true;
    bool Fast;
    return allowsMisalignedMemoryAccesses(VT, 0, 1, &Fast) && Fast;
  };

  if (CanUseNEON && IsMemset && !IsSmallMemset &&
      AlignmentIsAcceptable(MVT::v2i64, 16))
    return MVT::v2i64;
  if (CanUseFP && !IsSmallMemset && AlignmentIsAcceptable(MVT::f128, 16))
    return MVT::f128;
  if (Size >= 8 && AlignmentIsAcceptable(MVT::i64, 8))
    return MVT::i64;
  if (Size >= 4 && AlignmentIsAcceptable(MVT::i32, 4))
    return MVT::i32;
  return MVT::Other;
}

// 12-bit optionally shifted immediates are legal for adds.
bool AArch64TargetLowering::isLegalAddImmediate(int64_t Immed) const {
  if (Immed == std::numeric_limits<int64_t>::min()) {
    LLVM_DEBUG(dbgs() << "Illegal add imm " << Immed
                      << ": avoid UB for INT64_MIN\n");
    return false;
  }
  // Same encoding for add/sub, just flip the sign.
  Immed = std::abs(Immed);
  bool IsLegal = ((Immed >> 12) == 0 ||
                  ((Immed & 0xfff) == 0 && Immed >> 24 == 0));
  LLVM_DEBUG(dbgs() << "Is " << Immed
                    << " legal add imm: " << (IsLegal ? "yes" : "no") << "\n");
  return IsLegal;
}

// Integer comparisons are implemented with ADDS/SUBS, so the range of valid
// immediates is the same as for an add or a sub.
bool AArch64TargetLowering::isLegalICmpImmediate(int64_t Immed) const {
  return isLegalAddImmediate(Immed);
}

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
bool AArch64TargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                  const AddrMode &AM, Type *Ty,
                                                  unsigned AS, Instruction *I) const {
  // AArch64 has five basic addressing modes:
  //  reg
  //  reg + 9-bit signed offset
  //  reg + SIZE_IN_BYTES * 12-bit unsigned offset
  //  reg1 + reg2
  //  reg + SIZE_IN_BYTES * reg

  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  // No reg+reg+imm addressing.
  if (AM.HasBaseReg && AM.BaseOffs && AM.Scale)
    return false;

  // check reg + imm case:
  // i.e., reg + 0, reg + imm9, reg + SIZE_IN_BYTES * uimm12
  uint64_t NumBytes = 0;
  if (Ty->isSized()) {
    uint64_t NumBits = DL.getTypeSizeInBits(Ty);
    NumBytes = NumBits / 8;
    if (!isPowerOf2_64(NumBits))
      NumBytes = 0;
  }

  if (!AM.Scale) {
    int64_t Offset = AM.BaseOffs;

    // 9-bit signed offset
    if (isInt<9>(Offset))
      return true;

    // 12-bit unsigned offset
    unsigned shift = Log2_64(NumBytes);
    if (NumBytes && Offset > 0 && (Offset / NumBytes) <= (1LL << 12) - 1 &&
        // Must be a multiple of NumBytes (NumBytes is a power of 2)
        (Offset >> shift) << shift == Offset)
      return true;
    return false;
  }

  // Check reg1 + SIZE_IN_BYTES * reg2 and reg1 + reg2

  return AM.Scale == 1 || (AM.Scale > 0 && (uint64_t)AM.Scale == NumBytes);
}

bool AArch64TargetLowering::shouldConsiderGEPOffsetSplit() const {
  // Consider splitting large offset of struct or array.
  return true;
}

int AArch64TargetLowering::getScalingFactorCost(const DataLayout &DL,
                                                const AddrMode &AM, Type *Ty,
                                                unsigned AS) const {
  // Scaling factors are not free at all.
  // Operands                     | Rt Latency
  // -------------------------------------------
  // Rt, [Xn, Xm]                 | 4
  // -------------------------------------------
  // Rt, [Xn, Xm, lsl #imm]       | Rn: 4 Rm: 5
  // Rt, [Xn, Wm, <extend> #imm]  |
  if (isLegalAddressingMode(DL, AM, Ty, AS))
    // Scale represents reg2 * scale, thus account for 1 if
    // it is not equal to 0 or 1.
    return AM.Scale != 0 && AM.Scale != 1;
  return -1;
}

bool AArch64TargetLowering::isFMAFasterThanFMulAndFAdd(EVT VT) const {
  VT = VT.getScalarType();

  if (!VT.isSimple())
    return false;

  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::f32:
  case MVT::f64:
    return true;
  default:
    break;
  }

  return false;
}

const MCPhysReg *
AArch64TargetLowering::getScratchRegisters(CallingConv::ID) const {
  // LR is a callee-save register, but we must treat it as clobbered by any call
  // site. Hence we include LR in the scratch registers, which are in turn added
  // as implicit-defs for stackmaps and patchpoints.
  static const MCPhysReg ScratchRegs[] = {
    AArch64::X16, AArch64::X17, AArch64::LR, 0
  };
  return ScratchRegs;
}

bool
AArch64TargetLowering::isDesirableToCommuteWithShift(const SDNode *N,
                                                     CombineLevel Level) const {
  N = N->getOperand(0).getNode();
  EVT VT = N->getValueType(0);
    // If N is unsigned bit extraction: ((x >> C) & mask), then do not combine
    // it with shift to let it be lowered to UBFX.
  if (N->getOpcode() == ISD::AND && (VT == MVT::i32 || VT == MVT::i64) &&
      isa<ConstantSDNode>(N->getOperand(1))) {
    uint64_t TruncMask = N->getConstantOperandVal(1);
    if (isMask_64(TruncMask) &&
      N->getOperand(0).getOpcode() == ISD::SRL &&
      isa<ConstantSDNode>(N->getOperand(0)->getOperand(1)))
      return false;
  }
  return true;
}

bool AArch64TargetLowering::shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                              Type *Ty) const {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  if (BitSize == 0)
    return false;

  int64_t Val = Imm.getSExtValue();
  if (Val == 0 || AArch64_AM::isLogicalImmediate(Val, BitSize))
    return true;

  if ((int64_t)Val < 0)
    Val = ~Val;
  if (BitSize == 32)
    Val &= (1LL << 32) - 1;

  unsigned LZ = countLeadingZeros((uint64_t)Val);
  unsigned Shift = (63 - LZ) / 16;
  // MOVZ is free so return true for one or fewer MOVK.
  return Shift < 3;
}

bool AArch64TargetLowering::isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                                    unsigned Index) const {
  if (!isOperationLegalOrCustom(ISD::EXTRACT_SUBVECTOR, ResVT))
    return false;

  return (Index == 0 || Index == ResVT.getVectorNumElements());
}

/// Turn vector tests of the signbit in the form of:
///   xor (sra X, elt_size(X)-1), -1
/// into:
///   cmge X, X, #0
static SDValue foldVectorXorShiftIntoCmp(SDNode *N, SelectionDAG &DAG,
                                         const AArch64Subtarget *Subtarget) {
  EVT VT = N->getValueType(0);
  if (!Subtarget->hasNEON() || !VT.isVector())
    return SDValue();

  // There must be a shift right algebraic before the xor, and the xor must be a
  // 'not' operation.
  SDValue Shift = N->getOperand(0);
  SDValue Ones = N->getOperand(1);
  if (Shift.getOpcode() != AArch64ISD::VASHR || !Shift.hasOneUse() ||
      !ISD::isBuildVectorAllOnes(Ones.getNode()))
    return SDValue();

  // The shift should be smearing the sign bit across each vector element.
  auto *ShiftAmt = dyn_cast<ConstantSDNode>(Shift.getOperand(1));
  EVT ShiftEltTy = Shift.getValueType().getVectorElementType();
  if (!ShiftAmt || ShiftAmt->getZExtValue() != ShiftEltTy.getSizeInBits() - 1)
    return SDValue();

  return DAG.getNode(AArch64ISD::CMGEz, SDLoc(N), VT, Shift.getOperand(0));
}

// Generate SUBS and CSEL for integer abs.
static SDValue performIntegerAbsCombine(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDLoc DL(N);

  // Check pattern of XOR(ADD(X,Y), Y) where Y is SRA(X, size(X)-1)
  // and change it to SUB and CSEL.
  if (VT.isInteger() && N->getOpcode() == ISD::XOR &&
      N0.getOpcode() == ISD::ADD && N0.getOperand(1) == N1 &&
      N1.getOpcode() == ISD::SRA && N1.getOperand(0) == N0.getOperand(0))
    if (ConstantSDNode *Y1C = dyn_cast<ConstantSDNode>(N1.getOperand(1)))
      if (Y1C->getAPIntValue() == VT.getSizeInBits() - 1) {
        SDValue Neg = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT),
                                  N0.getOperand(0));
        // Generate SUBS & CSEL.
        SDValue Cmp =
            DAG.getNode(AArch64ISD::SUBS, DL, DAG.getVTList(VT, MVT::i32),
                        N0.getOperand(0), DAG.getConstant(0, DL, VT));
        return DAG.getNode(AArch64ISD::CSEL, DL, VT, N0.getOperand(0), Neg,
                           DAG.getConstant(AArch64CC::PL, DL, MVT::i32),
                           SDValue(Cmp.getNode(), 1));
      }
  return SDValue();
}

static SDValue performXorCombine(SDNode *N, SelectionDAG &DAG,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const AArch64Subtarget *Subtarget) {
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  if (SDValue Cmp = foldVectorXorShiftIntoCmp(N, DAG, Subtarget))
    return Cmp;

  return performIntegerAbsCombine(N, DAG);
}

SDValue
AArch64TargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                     SelectionDAG &DAG,
                                     SmallVectorImpl<SDNode *> &Created) const {
  AttributeList Attr = DAG.getMachineFunction().getFunction().getAttributes();
  if (isIntDivCheap(N->getValueType(0), Attr))
    return SDValue(N,0); // Lower SDIV as SDIV

  // fold (sdiv X, pow2)
  EVT VT = N->getValueType(0);
  if ((VT != MVT::i32 && VT != MVT::i64) ||
      !(Divisor.isPowerOf2() || (-Divisor).isPowerOf2()))
    return SDValue();

  SDLoc DL(N);
  SDValue N0 = N->getOperand(0);
  unsigned Lg2 = Divisor.countTrailingZeros();
  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue Pow2MinusOne = DAG.getConstant((1ULL << Lg2) - 1, DL, VT);

  // Add (N0 < 0) ? Pow2 - 1 : 0;
  SDValue CCVal;
  SDValue Cmp = getAArch64Cmp(N0, Zero, ISD::SETLT, CCVal, DAG, DL);
  SDValue Add = DAG.getNode(ISD::ADD, DL, VT, N0, Pow2MinusOne);
  SDValue CSel = DAG.getNode(AArch64ISD::CSEL, DL, VT, Add, N0, CCVal, Cmp);

  Created.push_back(Cmp.getNode());
  Created.push_back(Add.getNode());
  Created.push_back(CSel.getNode());

  // Divide by pow2.
  SDValue SRA =
      DAG.getNode(ISD::SRA, DL, VT, CSel, DAG.getConstant(Lg2, DL, MVT::i64));

  // If we're dividing by a positive value, we're done.  Otherwise, we must
  // negate the result.
  if (Divisor.isNonNegative())
    return SRA;

  Created.push_back(SRA.getNode());
  return DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), SRA);
}

static SDValue performMulCombine(SDNode *N, SelectionDAG &DAG,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const AArch64Subtarget *Subtarget) {
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  // The below optimizations require a constant RHS.
  if (!isa<ConstantSDNode>(N->getOperand(1)))
    return SDValue();

  ConstantSDNode *C = cast<ConstantSDNode>(N->getOperand(1));
  const APInt &ConstValue = C->getAPIntValue();

  // Multiplication of a power of two plus/minus one can be done more
  // cheaply as as shift+add/sub. For now, this is true unilaterally. If
  // future CPUs have a cheaper MADD instruction, this may need to be
  // gated on a subtarget feature. For Cyclone, 32-bit MADD is 4 cycles and
  // 64-bit is 5 cycles, so this is always a win.
  // More aggressively, some multiplications N0 * C can be lowered to
  // shift+add+shift if the constant C = A * B where A = 2^N + 1 and B = 2^M,
  // e.g. 6=3*2=(2+1)*2.
  // TODO: consider lowering more cases, e.g. C = 14, -6, -14 or even 45
  // which equals to (1+2)*16-(1+2).
  SDValue N0 = N->getOperand(0);
  // TrailingZeroes is used to test if the mul can be lowered to
  // shift+add+shift.
  unsigned TrailingZeroes = ConstValue.countTrailingZeros();
  if (TrailingZeroes) {
    // Conservatively do not lower to shift+add+shift if the mul might be
    // folded into smul or umul.
    if (N0->hasOneUse() && (isSignExtended(N0.getNode(), DAG) ||
                            isZeroExtended(N0.getNode(), DAG)))
      return SDValue();
    // Conservatively do not lower to shift+add+shift if the mul might be
    // folded into madd or msub.
    if (N->hasOneUse() && (N->use_begin()->getOpcode() == ISD::ADD ||
                           N->use_begin()->getOpcode() == ISD::SUB))
      return SDValue();
  }
  // Use ShiftedConstValue instead of ConstValue to support both shift+add/sub
  // and shift+add+shift.
  APInt ShiftedConstValue = ConstValue.ashr(TrailingZeroes);

  unsigned ShiftAmt, AddSubOpc;
  // Is the shifted value the LHS operand of the add/sub?
  bool ShiftValUseIsN0 = true;
  // Do we need to negate the result?
  bool NegateResult = false;

  if (ConstValue.isNonNegative()) {
    // (mul x, 2^N + 1) => (add (shl x, N), x)
    // (mul x, 2^N - 1) => (sub (shl x, N), x)
    // (mul x, (2^N + 1) * 2^M) => (shl (add (shl x, N), x), M)
    APInt SCVMinus1 = ShiftedConstValue - 1;
    APInt CVPlus1 = ConstValue + 1;
    if (SCVMinus1.isPowerOf2()) {
      ShiftAmt = SCVMinus1.logBase2();
      AddSubOpc = ISD::ADD;
    } else if (CVPlus1.isPowerOf2()) {
      ShiftAmt = CVPlus1.logBase2();
      AddSubOpc = ISD::SUB;
    } else
      return SDValue();
  } else {
    // (mul x, -(2^N - 1)) => (sub x, (shl x, N))
    // (mul x, -(2^N + 1)) => - (add (shl x, N), x)
    APInt CVNegPlus1 = -ConstValue + 1;
    APInt CVNegMinus1 = -ConstValue - 1;
    if (CVNegPlus1.isPowerOf2()) {
      ShiftAmt = CVNegPlus1.logBase2();
      AddSubOpc = ISD::SUB;
      ShiftValUseIsN0 = false;
    } else if (CVNegMinus1.isPowerOf2()) {
      ShiftAmt = CVNegMinus1.logBase2();
      AddSubOpc = ISD::ADD;
      NegateResult = true;
    } else
      return SDValue();
  }

  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  SDValue ShiftedVal = DAG.getNode(ISD::SHL, DL, VT, N0,
                                   DAG.getConstant(ShiftAmt, DL, MVT::i64));

  SDValue AddSubN0 = ShiftValUseIsN0 ? ShiftedVal : N0;
  SDValue AddSubN1 = ShiftValUseIsN0 ? N0 : ShiftedVal;
  SDValue Res = DAG.getNode(AddSubOpc, DL, VT, AddSubN0, AddSubN1);
  assert(!(NegateResult && TrailingZeroes) &&
         "NegateResult and TrailingZeroes cannot both be true for now.");
  // Negate the result.
  if (NegateResult)
    return DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), Res);
  // Shift the result.
  if (TrailingZeroes)
    return DAG.getNode(ISD::SHL, DL, VT, Res,
                       DAG.getConstant(TrailingZeroes, DL, MVT::i64));
  return Res;
}

static SDValue performVectorCompareAndMaskUnaryOpCombine(SDNode *N,
                                                         SelectionDAG &DAG) {
  // Take advantage of vector comparisons producing 0 or -1 in each lane to
  // optimize away operation when it's from a constant.
  //
  // The general transformation is:
  //    UNARYOP(AND(VECTOR_CMP(x,y), constant)) -->
  //       AND(VECTOR_CMP(x,y), constant2)
  //    constant2 = UNARYOP(constant)

  // Early exit if this isn't a vector operation, the operand of the
  // unary operation isn't a bitwise AND, or if the sizes of the operations
  // aren't the same.
  EVT VT = N->getValueType(0);
  if (!VT.isVector() || N->getOperand(0)->getOpcode() != ISD::AND ||
      N->getOperand(0)->getOperand(0)->getOpcode() != ISD::SETCC ||
      VT.getSizeInBits() != N->getOperand(0)->getValueType(0).getSizeInBits())
    return SDValue();

  // Now check that the other operand of the AND is a constant. We could
  // make the transformation for non-constant splats as well, but it's unclear
  // that would be a benefit as it would not eliminate any operations, just
  // perform one more step in scalar code before moving to the vector unit.
  if (BuildVectorSDNode *BV =
          dyn_cast<BuildVectorSDNode>(N->getOperand(0)->getOperand(1))) {
    // Bail out if the vector isn't a constant.
    if (!BV->isConstant())
      return SDValue();

    // Everything checks out. Build up the new and improved node.
    SDLoc DL(N);
    EVT IntVT = BV->getValueType(0);
    // Create a new constant of the appropriate type for the transformed
    // DAG.
    SDValue SourceConst = DAG.getNode(N->getOpcode(), DL, VT, SDValue(BV, 0));
    // The AND node needs bitcasts to/from an integer vector type around it.
    SDValue MaskConst = DAG.getNode(ISD::BITCAST, DL, IntVT, SourceConst);
    SDValue NewAnd = DAG.getNode(ISD::AND, DL, IntVT,
                                 N->getOperand(0)->getOperand(0), MaskConst);
    SDValue Res = DAG.getNode(ISD::BITCAST, DL, VT, NewAnd);
    return Res;
  }

  return SDValue();
}

static SDValue performIntToFpCombine(SDNode *N, SelectionDAG &DAG,
                                     const AArch64Subtarget *Subtarget) {
  // First try to optimize away the conversion when it's conditionally from
  // a constant. Vectors only.
  if (SDValue Res = performVectorCompareAndMaskUnaryOpCombine(N, DAG))
    return Res;

  EVT VT = N->getValueType(0);
  if (VT != MVT::f32 && VT != MVT::f64)
    return SDValue();

  // Only optimize when the source and destination types have the same width.
  if (VT.getSizeInBits() != N->getOperand(0).getValueSizeInBits())
    return SDValue();

  // If the result of an integer load is only used by an integer-to-float
  // conversion, use a fp load instead and a AdvSIMD scalar {S|U}CVTF instead.
  // This eliminates an "integer-to-vector-move" UOP and improves throughput.
  SDValue N0 = N->getOperand(0);
  if (Subtarget->hasNEON() && ISD::isNormalLoad(N0.getNode()) && N0.hasOneUse() &&
      // Do not change the width of a volatile load.
      !cast<LoadSDNode>(N0)->isVolatile()) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);
    SDValue Load = DAG.getLoad(VT, SDLoc(N), LN0->getChain(), LN0->getBasePtr(),
                               LN0->getPointerInfo(), LN0->getAlignment(),
                               LN0->getMemOperand()->getFlags());

    // Make sure successors of the original load stay after it by updating them
    // to use the new Chain.
    DAG.ReplaceAllUsesOfValueWith(SDValue(LN0, 1), Load.getValue(1));

    unsigned Opcode =
        (N->getOpcode() == ISD::SINT_TO_FP) ? AArch64ISD::SITOF : AArch64ISD::UITOF;
    return DAG.getNode(Opcode, SDLoc(N), VT, Load);
  }

  return SDValue();
}

/// Fold a floating-point multiply by power of two into floating-point to
/// fixed-point conversion.
static SDValue performFpToIntCombine(SDNode *N, SelectionDAG &DAG,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const AArch64Subtarget *Subtarget) {
  if (!Subtarget->hasNEON())
    return SDValue();

  SDValue Op = N->getOperand(0);
  if (!Op.getValueType().isVector() || !Op.getValueType().isSimple() ||
      Op.getOpcode() != ISD::FMUL)
    return SDValue();

  SDValue ConstVec = Op->getOperand(1);
  if (!isa<BuildVectorSDNode>(ConstVec))
    return SDValue();

  MVT FloatTy = Op.getSimpleValueType().getVectorElementType();
  uint32_t FloatBits = FloatTy.getSizeInBits();
  if (FloatBits != 32 && FloatBits != 64)
    return SDValue();

  MVT IntTy = N->getSimpleValueType(0).getVectorElementType();
  uint32_t IntBits = IntTy.getSizeInBits();
  if (IntBits != 16 && IntBits != 32 && IntBits != 64)
    return SDValue();

  // Avoid conversions where iN is larger than the float (e.g., float -> i64).
  if (IntBits > FloatBits)
    return SDValue();

  BitVector UndefElements;
  BuildVectorSDNode *BV = cast<BuildVectorSDNode>(ConstVec);
  int32_t Bits = IntBits == 64 ? 64 : 32;
  int32_t C = BV->getConstantFPSplatPow2ToLog2Int(&UndefElements, Bits + 1);
  if (C == -1 || C == 0 || C > Bits)
    return SDValue();

  MVT ResTy;
  unsigned NumLanes = Op.getValueType().getVectorNumElements();
  switch (NumLanes) {
  default:
    return SDValue();
  case 2:
    ResTy = FloatBits == 32 ? MVT::v2i32 : MVT::v2i64;
    break;
  case 4:
    ResTy = FloatBits == 32 ? MVT::v4i32 : MVT::v4i64;
    break;
  }

  if (ResTy == MVT::v4i64 && DCI.isBeforeLegalizeOps())
    return SDValue();

  assert((ResTy != MVT::v4i64 || DCI.isBeforeLegalizeOps()) &&
         "Illegal vector type after legalization");

  SDLoc DL(N);
  bool IsSigned = N->getOpcode() == ISD::FP_TO_SINT;
  unsigned IntrinsicOpcode = IsSigned ? Intrinsic::aarch64_neon_vcvtfp2fxs
                                      : Intrinsic::aarch64_neon_vcvtfp2fxu;
  SDValue FixConv =
      DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, ResTy,
                  DAG.getConstant(IntrinsicOpcode, DL, MVT::i32),
                  Op->getOperand(0), DAG.getConstant(C, DL, MVT::i32));
  // We can handle smaller integers by generating an extra trunc.
  if (IntBits < FloatBits)
    FixConv = DAG.getNode(ISD::TRUNCATE, DL, N->getValueType(0), FixConv);

  return FixConv;
}

/// Fold a floating-point divide by power of two into fixed-point to
/// floating-point conversion.
static SDValue performFDivCombine(SDNode *N, SelectionDAG &DAG,
                                  TargetLowering::DAGCombinerInfo &DCI,
                                  const AArch64Subtarget *Subtarget) {
  if (!Subtarget->hasNEON())
    return SDValue();

  SDValue Op = N->getOperand(0);
  unsigned Opc = Op->getOpcode();
  if (!Op.getValueType().isVector() || !Op.getValueType().isSimple() ||
      !Op.getOperand(0).getValueType().isSimple() ||
      (Opc != ISD::SINT_TO_FP && Opc != ISD::UINT_TO_FP))
    return SDValue();

  SDValue ConstVec = N->getOperand(1);
  if (!isa<BuildVectorSDNode>(ConstVec))
    return SDValue();

  MVT IntTy = Op.getOperand(0).getSimpleValueType().getVectorElementType();
  int32_t IntBits = IntTy.getSizeInBits();
  if (IntBits != 16 && IntBits != 32 && IntBits != 64)
    return SDValue();

  MVT FloatTy = N->getSimpleValueType(0).getVectorElementType();
  int32_t FloatBits = FloatTy.getSizeInBits();
  if (FloatBits != 32 && FloatBits != 64)
    return SDValue();

  // Avoid conversions where iN is larger than the float (e.g., i64 -> float).
  if (IntBits > FloatBits)
    return SDValue();

  BitVector UndefElements;
  BuildVectorSDNode *BV = cast<BuildVectorSDNode>(ConstVec);
  int32_t C = BV->getConstantFPSplatPow2ToLog2Int(&UndefElements, FloatBits + 1);
  if (C == -1 || C == 0 || C > FloatBits)
    return SDValue();

  MVT ResTy;
  unsigned NumLanes = Op.getValueType().getVectorNumElements();
  switch (NumLanes) {
  default:
    return SDValue();
  case 2:
    ResTy = FloatBits == 32 ? MVT::v2i32 : MVT::v2i64;
    break;
  case 4:
    ResTy = FloatBits == 32 ? MVT::v4i32 : MVT::v4i64;
    break;
  }

  if (ResTy == MVT::v4i64 && DCI.isBeforeLegalizeOps())
    return SDValue();

  SDLoc DL(N);
  SDValue ConvInput = Op.getOperand(0);
  bool IsSigned = Opc == ISD::SINT_TO_FP;
  if (IntBits < FloatBits)
    ConvInput = DAG.getNode(IsSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, DL,
                            ResTy, ConvInput);

  unsigned IntrinsicOpcode = IsSigned ? Intrinsic::aarch64_neon_vcvtfxs2fp
                                      : Intrinsic::aarch64_neon_vcvtfxu2fp;
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, Op.getValueType(),
                     DAG.getConstant(IntrinsicOpcode, DL, MVT::i32), ConvInput,
                     DAG.getConstant(C, DL, MVT::i32));
}

/// An EXTR instruction is made up of two shifts, ORed together. This helper
/// searches for and classifies those shifts.
static bool findEXTRHalf(SDValue N, SDValue &Src, uint32_t &ShiftAmount,
                         bool &FromHi) {
  if (N.getOpcode() == ISD::SHL)
    FromHi = false;
  else if (N.getOpcode() == ISD::SRL)
    FromHi = true;
  else
    return false;

  if (!isa<ConstantSDNode>(N.getOperand(1)))
    return false;

  ShiftAmount = N->getConstantOperandVal(1);
  Src = N->getOperand(0);
  return true;
}

/// EXTR instruction extracts a contiguous chunk of bits from two existing
/// registers viewed as a high/low pair. This function looks for the pattern:
/// <tt>(or (shl VAL1, \#N), (srl VAL2, \#RegWidth-N))</tt> and replaces it
/// with an EXTR. Can't quite be done in TableGen because the two immediates
/// aren't independent.
static SDValue tryCombineToEXTR(SDNode *N,
                                TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  assert(N->getOpcode() == ISD::OR && "Unexpected root");

  if (VT != MVT::i32 && VT != MVT::i64)
    return SDValue();

  SDValue LHS;
  uint32_t ShiftLHS = 0;
  bool LHSFromHi = false;
  if (!findEXTRHalf(N->getOperand(0), LHS, ShiftLHS, LHSFromHi))
    return SDValue();

  SDValue RHS;
  uint32_t ShiftRHS = 0;
  bool RHSFromHi = false;
  if (!findEXTRHalf(N->getOperand(1), RHS, ShiftRHS, RHSFromHi))
    return SDValue();

  // If they're both trying to come from the high part of the register, they're
  // not really an EXTR.
  if (LHSFromHi == RHSFromHi)
    return SDValue();

  if (ShiftLHS + ShiftRHS != VT.getSizeInBits())
    return SDValue();

  if (LHSFromHi) {
    std::swap(LHS, RHS);
    std::swap(ShiftLHS, ShiftRHS);
  }

  return DAG.getNode(AArch64ISD::EXTR, DL, VT, LHS, RHS,
                     DAG.getConstant(ShiftRHS, DL, MVT::i64));
}

static SDValue tryCombineToBSL(SDNode *N,
                                TargetLowering::DAGCombinerInfo &DCI) {
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  if (!VT.isVector())
    return SDValue();

  SDValue N0 = N->getOperand(0);
  if (N0.getOpcode() != ISD::AND)
    return SDValue();

  SDValue N1 = N->getOperand(1);
  if (N1.getOpcode() != ISD::AND)
    return SDValue();

  // We only have to look for constant vectors here since the general, variable
  // case can be handled in TableGen.
  unsigned Bits = VT.getScalarSizeInBits();
  uint64_t BitMask = Bits == 64 ? -1ULL : ((1ULL << Bits) - 1);
  for (int i = 1; i >= 0; --i)
    for (int j = 1; j >= 0; --j) {
      BuildVectorSDNode *BVN0 = dyn_cast<BuildVectorSDNode>(N0->getOperand(i));
      BuildVectorSDNode *BVN1 = dyn_cast<BuildVectorSDNode>(N1->getOperand(j));
      if (!BVN0 || !BVN1)
        continue;

      bool FoundMatch = true;
      for (unsigned k = 0; k < VT.getVectorNumElements(); ++k) {
        ConstantSDNode *CN0 = dyn_cast<ConstantSDNode>(BVN0->getOperand(k));
        ConstantSDNode *CN1 = dyn_cast<ConstantSDNode>(BVN1->getOperand(k));
        if (!CN0 || !CN1 ||
            CN0->getZExtValue() != (BitMask & ~CN1->getZExtValue())) {
          FoundMatch = false;
          break;
        }
      }

      if (FoundMatch)
        return DAG.getNode(AArch64ISD::BSL, DL, VT, SDValue(BVN0, 0),
                           N0->getOperand(1 - i), N1->getOperand(1 - j));
    }

  return SDValue();
}

static SDValue performORCombine(SDNode *N, TargetLowering::DAGCombinerInfo &DCI,
                                const AArch64Subtarget *Subtarget) {
  // Attempt to form an EXTR from (or (shl VAL1, #N), (srl VAL2, #RegWidth-N))
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);

  if (!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  if (SDValue Res = tryCombineToEXTR(N, DCI))
    return Res;

  if (SDValue Res = tryCombineToBSL(N, DCI))
    return Res;

  return SDValue();
}

static SDValue performSRLCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  if (VT != MVT::i32 && VT != MVT::i64)
    return SDValue();

  // Canonicalize (srl (bswap i32 x), 16) to (rotr (bswap i32 x), 16), if the
  // high 16-bits of x are zero. Similarly, canonicalize (srl (bswap i64 x), 32)
  // to (rotr (bswap i64 x), 32), if the high 32-bits of x are zero.
  SDValue N0 = N->getOperand(0);
  if (N0.getOpcode() == ISD::BSWAP) {
    SDLoc DL(N);
    SDValue N1 = N->getOperand(1);
    SDValue N00 = N0.getOperand(0);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(N1)) {
      uint64_t ShiftAmt = C->getZExtValue();
      if (VT == MVT::i32 && ShiftAmt == 16 &&
          DAG.MaskedValueIsZero(N00, APInt::getHighBitsSet(32, 16)))
        return DAG.getNode(ISD::ROTR, DL, VT, N0, N1);
      if (VT == MVT::i64 && ShiftAmt == 32 &&
          DAG.MaskedValueIsZero(N00, APInt::getHighBitsSet(64, 32)))
        return DAG.getNode(ISD::ROTR, DL, VT, N0, N1);
    }
  }
  return SDValue();
}

static SDValue performBitcastCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     SelectionDAG &DAG) {
  // Wait 'til after everything is legalized to try this. That way we have
  // legal vector types and such.
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  // Remove extraneous bitcasts around an extract_subvector.
  // For example,
  //    (v4i16 (bitconvert
  //             (extract_subvector (v2i64 (bitconvert (v8i16 ...)), (i64 1)))))
  //  becomes
  //    (extract_subvector ((v8i16 ...), (i64 4)))

  // Only interested in 64-bit vectors as the ultimate result.
  EVT VT = N->getValueType(0);
  if (!VT.isVector())
    return SDValue();
  if (VT.getSimpleVT().getSizeInBits() != 64)
    return SDValue();
  // Is the operand an extract_subvector starting at the beginning or halfway
  // point of the vector? A low half may also come through as an
  // EXTRACT_SUBREG, so look for that, too.
  SDValue Op0 = N->getOperand(0);
  if (Op0->getOpcode() != ISD::EXTRACT_SUBVECTOR &&
      !(Op0->isMachineOpcode() &&
        Op0->getMachineOpcode() == AArch64::EXTRACT_SUBREG))
    return SDValue();
  uint64_t idx = cast<ConstantSDNode>(Op0->getOperand(1))->getZExtValue();
  if (Op0->getOpcode() == ISD::EXTRACT_SUBVECTOR) {
    if (Op0->getValueType(0).getVectorNumElements() != idx && idx != 0)
      return SDValue();
  } else if (Op0->getMachineOpcode() == AArch64::EXTRACT_SUBREG) {
    if (idx != AArch64::dsub)
      return SDValue();
    // The dsub reference is equivalent to a lane zero subvector reference.
    idx = 0;
  }
  // Look through the bitcast of the input to the extract.
  if (Op0->getOperand(0)->getOpcode() != ISD::BITCAST)
    return SDValue();
  SDValue Source = Op0->getOperand(0)->getOperand(0);
  // If the source type has twice the number of elements as our destination
  // type, we know this is an extract of the high or low half of the vector.
  EVT SVT = Source->getValueType(0);
  if (!SVT.isVector() ||
      SVT.getVectorNumElements() != VT.getVectorNumElements() * 2)
    return SDValue();

  LLVM_DEBUG(
      dbgs() << "aarch64-lower: bitcast extract_subvector simplification\n");

  // Create the simplified form to just extract the low or high half of the
  // vector directly rather than bothering with the bitcasts.
  SDLoc dl(N);
  unsigned NumElements = VT.getVectorNumElements();
  if (idx) {
    SDValue HalfIdx = DAG.getConstant(NumElements, dl, MVT::i64);
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, VT, Source, HalfIdx);
  } else {
    SDValue SubReg = DAG.getTargetConstant(AArch64::dsub, dl, MVT::i32);
    return SDValue(DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG, dl, VT,
                                      Source, SubReg),
                   0);
  }
}

static SDValue performConcatVectorsCombine(SDNode *N,
                                           TargetLowering::DAGCombinerInfo &DCI,
                                           SelectionDAG &DAG) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0), N1 = N->getOperand(1);

  // Optimize concat_vectors of truncated vectors, where the intermediate
  // type is illegal, to avoid said illegality,  e.g.,
  //   (v4i16 (concat_vectors (v2i16 (truncate (v2i64))),
  //                          (v2i16 (truncate (v2i64)))))
  // ->
  //   (v4i16 (truncate (vector_shuffle (v4i32 (bitcast (v2i64))),
  //                                    (v4i32 (bitcast (v2i64))),
  //                                    <0, 2, 4, 6>)))
  // This isn't really target-specific, but ISD::TRUNCATE legality isn't keyed
  // on both input and result type, so we might generate worse code.
  // On AArch64 we know it's fine for v2i64->v4i16 and v4i32->v8i8.
  if (N->getNumOperands() == 2 &&
      N0->getOpcode() == ISD::TRUNCATE &&
      N1->getOpcode() == ISD::TRUNCATE) {
    SDValue N00 = N0->getOperand(0);
    SDValue N10 = N1->getOperand(0);
    EVT N00VT = N00.getValueType();

    if (N00VT == N10.getValueType() &&
        (N00VT == MVT::v2i64 || N00VT == MVT::v4i32) &&
        N00VT.getScalarSizeInBits() == 4 * VT.getScalarSizeInBits()) {
      MVT MidVT = (N00VT == MVT::v2i64 ? MVT::v4i32 : MVT::v8i16);
      SmallVector<int, 8> Mask(MidVT.getVectorNumElements());
      for (size_t i = 0; i < Mask.size(); ++i)
        Mask[i] = i * 2;
      return DAG.getNode(ISD::TRUNCATE, dl, VT,
                         DAG.getVectorShuffle(
                             MidVT, dl,
                             DAG.getNode(ISD::BITCAST, dl, MidVT, N00),
                             DAG.getNode(ISD::BITCAST, dl, MidVT, N10), Mask));
    }
  }

  // Wait 'til after everything is legalized to try this. That way we have
  // legal vector types and such.
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  // If we see a (concat_vectors (v1x64 A), (v1x64 A)) it's really a vector
  // splat. The indexed instructions are going to be expecting a DUPLANE64, so
  // canonicalise to that.
  if (N0 == N1 && VT.getVectorNumElements() == 2) {
    assert(VT.getScalarSizeInBits() == 64);
    return DAG.getNode(AArch64ISD::DUPLANE64, dl, VT, WidenVector(N0, DAG),
                       DAG.getConstant(0, dl, MVT::i64));
  }

  // Canonicalise concat_vectors so that the right-hand vector has as few
  // bit-casts as possible before its real operation. The primary matching
  // destination for these operations will be the narrowing "2" instructions,
  // which depend on the operation being performed on this right-hand vector.
  // For example,
  //    (concat_vectors LHS,  (v1i64 (bitconvert (v4i16 RHS))))
  // becomes
  //    (bitconvert (concat_vectors (v4i16 (bitconvert LHS)), RHS))

  if (N1->getOpcode() != ISD::BITCAST)
    return SDValue();
  SDValue RHS = N1->getOperand(0);
  MVT RHSTy = RHS.getValueType().getSimpleVT();
  // If the RHS is not a vector, this is not the pattern we're looking for.
  if (!RHSTy.isVector())
    return SDValue();

  LLVM_DEBUG(
      dbgs() << "aarch64-lower: concat_vectors bitcast simplification\n");

  MVT ConcatTy = MVT::getVectorVT(RHSTy.getVectorElementType(),
                                  RHSTy.getVectorNumElements() * 2);
  return DAG.getNode(ISD::BITCAST, dl, VT,
                     DAG.getNode(ISD::CONCAT_VECTORS, dl, ConcatTy,
                                 DAG.getNode(ISD::BITCAST, dl, RHSTy, N0),
                                 RHS));
}

static SDValue tryCombineFixedPointConvert(SDNode *N,
                                           TargetLowering::DAGCombinerInfo &DCI,
                                           SelectionDAG &DAG) {
  // Wait until after everything is legalized to try this. That way we have
  // legal vector types and such.
  if (DCI.isBeforeLegalizeOps())
    return SDValue();
  // Transform a scalar conversion of a value from a lane extract into a
  // lane extract of a vector conversion. E.g., from foo1 to foo2:
  // double foo1(int64x2_t a) { return vcvtd_n_f64_s64(a[1], 9); }
  // double foo2(int64x2_t a) { return vcvtq_n_f64_s64(a, 9)[1]; }
  //
  // The second form interacts better with instruction selection and the
  // register allocator to avoid cross-class register copies that aren't
  // coalescable due to a lane reference.

  // Check the operand and see if it originates from a lane extract.
  SDValue Op1 = N->getOperand(1);
  if (Op1.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
    // Yep, no additional predication needed. Perform the transform.
    SDValue IID = N->getOperand(0);
    SDValue Shift = N->getOperand(2);
    SDValue Vec = Op1.getOperand(0);
    SDValue Lane = Op1.getOperand(1);
    EVT ResTy = N->getValueType(0);
    EVT VecResTy;
    SDLoc DL(N);

    // The vector width should be 128 bits by the time we get here, even
    // if it started as 64 bits (the extract_vector handling will have
    // done so).
    assert(Vec.getValueSizeInBits() == 128 &&
           "unexpected vector size on extract_vector_elt!");
    if (Vec.getValueType() == MVT::v4i32)
      VecResTy = MVT::v4f32;
    else if (Vec.getValueType() == MVT::v2i64)
      VecResTy = MVT::v2f64;
    else
      llvm_unreachable("unexpected vector type!");

    SDValue Convert =
        DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VecResTy, IID, Vec, Shift);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, ResTy, Convert, Lane);
  }
  return SDValue();
}

// AArch64 high-vector "long" operations are formed by performing the non-high
// version on an extract_subvector of each operand which gets the high half:
//
//  (longop2 LHS, RHS) == (longop (extract_high LHS), (extract_high RHS))
//
// However, there are cases which don't have an extract_high explicitly, but
// have another operation that can be made compatible with one for free. For
// example:
//
//  (dupv64 scalar) --> (extract_high (dup128 scalar))
//
// This routine does the actual conversion of such DUPs, once outer routines
// have determined that everything else is in order.
// It also supports immediate DUP-like nodes (MOVI/MVNi), which we can fold
// similarly here.
static SDValue tryExtendDUPToExtractHigh(SDValue N, SelectionDAG &DAG) {
  switch (N.getOpcode()) {
  case AArch64ISD::DUP:
  case AArch64ISD::DUPLANE8:
  case AArch64ISD::DUPLANE16:
  case AArch64ISD::DUPLANE32:
  case AArch64ISD::DUPLANE64:
  case AArch64ISD::MOVI:
  case AArch64ISD::MOVIshift:
  case AArch64ISD::MOVIedit:
  case AArch64ISD::MOVImsl:
  case AArch64ISD::MVNIshift:
  case AArch64ISD::MVNImsl:
    break;
  default:
    // FMOV could be supported, but isn't very useful, as it would only occur
    // if you passed a bitcast' floating point immediate to an eligible long
    // integer op (addl, smull, ...).
    return SDValue();
  }

  MVT NarrowTy = N.getSimpleValueType();
  if (!NarrowTy.is64BitVector())
    return SDValue();

  MVT ElementTy = NarrowTy.getVectorElementType();
  unsigned NumElems = NarrowTy.getVectorNumElements();
  MVT NewVT = MVT::getVectorVT(ElementTy, NumElems * 2);

  SDLoc dl(N);
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, NarrowTy,
                     DAG.getNode(N->getOpcode(), dl, NewVT, N->ops()),
                     DAG.getConstant(NumElems, dl, MVT::i64));
}

static bool isEssentiallyExtractSubvector(SDValue N) {
  if (N.getOpcode() == ISD::EXTRACT_SUBVECTOR)
    return true;

  return N.getOpcode() == ISD::BITCAST &&
         N.getOperand(0).getOpcode() == ISD::EXTRACT_SUBVECTOR;
}

/// Helper structure to keep track of ISD::SET_CC operands.
struct GenericSetCCInfo {
  const SDValue *Opnd0;
  const SDValue *Opnd1;
  ISD::CondCode CC;
};

/// Helper structure to keep track of a SET_CC lowered into AArch64 code.
struct AArch64SetCCInfo {
  const SDValue *Cmp;
  AArch64CC::CondCode CC;
};

/// Helper structure to keep track of SetCC information.
union SetCCInfo {
  GenericSetCCInfo Generic;
  AArch64SetCCInfo AArch64;
};

/// Helper structure to be able to read SetCC information.  If set to
/// true, IsAArch64 field, Info is a AArch64SetCCInfo, otherwise Info is a
/// GenericSetCCInfo.
struct SetCCInfoAndKind {
  SetCCInfo Info;
  bool IsAArch64;
};

/// Check whether or not \p Op is a SET_CC operation, either a generic or
/// an
/// AArch64 lowered one.
/// \p SetCCInfo is filled accordingly.
/// \post SetCCInfo is meanginfull only when this function returns true.
/// \return True when Op is a kind of SET_CC operation.
static bool isSetCC(SDValue Op, SetCCInfoAndKind &SetCCInfo) {
  // If this is a setcc, this is straight forward.
  if (Op.getOpcode() == ISD::SETCC) {
    SetCCInfo.Info.Generic.Opnd0 = &Op.getOperand(0);
    SetCCInfo.Info.Generic.Opnd1 = &Op.getOperand(1);
    SetCCInfo.Info.Generic.CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
    SetCCInfo.IsAArch64 = false;
    return true;
  }
  // Otherwise, check if this is a matching csel instruction.
  // In other words:
  // - csel 1, 0, cc
  // - csel 0, 1, !cc
  if (Op.getOpcode() != AArch64ISD::CSEL)
    return false;
  // Set the information about the operands.
  // TODO: we want the operands of the Cmp not the csel
  SetCCInfo.Info.AArch64.Cmp = &Op.getOperand(3);
  SetCCInfo.IsAArch64 = true;
  SetCCInfo.Info.AArch64.CC = static_cast<AArch64CC::CondCode>(
      cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue());

  // Check that the operands matches the constraints:
  // (1) Both operands must be constants.
  // (2) One must be 1 and the other must be 0.
  ConstantSDNode *TValue = dyn_cast<ConstantSDNode>(Op.getOperand(0));
  ConstantSDNode *FValue = dyn_cast<ConstantSDNode>(Op.getOperand(1));

  // Check (1).
  if (!TValue || !FValue)
    return false;

  // Check (2).
  if (!TValue->isOne()) {
    // Update the comparison when we are interested in !cc.
    std::swap(TValue, FValue);
    SetCCInfo.Info.AArch64.CC =
        AArch64CC::getInvertedCondCode(SetCCInfo.Info.AArch64.CC);
  }
  return TValue->isOne() && FValue->isNullValue();
}

// Returns true if Op is setcc or zext of setcc.
static bool isSetCCOrZExtSetCC(const SDValue& Op, SetCCInfoAndKind &Info) {
  if (isSetCC(Op, Info))
    return true;
  return ((Op.getOpcode() == ISD::ZERO_EXTEND) &&
    isSetCC(Op->getOperand(0), Info));
}

// The folding we want to perform is:
// (add x, [zext] (setcc cc ...) )
//   -->
// (csel x, (add x, 1), !cc ...)
//
// The latter will get matched to a CSINC instruction.
static SDValue performSetccAddFolding(SDNode *Op, SelectionDAG &DAG) {
  assert(Op && Op->getOpcode() == ISD::ADD && "Unexpected operation!");
  SDValue LHS = Op->getOperand(0);
  SDValue RHS = Op->getOperand(1);
  SetCCInfoAndKind InfoAndKind;

  // If neither operand is a SET_CC, give up.
  if (!isSetCCOrZExtSetCC(LHS, InfoAndKind)) {
    std::swap(LHS, RHS);
    if (!isSetCCOrZExtSetCC(LHS, InfoAndKind))
      return SDValue();
  }

  // FIXME: This could be generatized to work for FP comparisons.
  EVT CmpVT = InfoAndKind.IsAArch64
                  ? InfoAndKind.Info.AArch64.Cmp->getOperand(0).getValueType()
                  : InfoAndKind.Info.Generic.Opnd0->getValueType();
  if (CmpVT != MVT::i32 && CmpVT != MVT::i64)
    return SDValue();

  SDValue CCVal;
  SDValue Cmp;
  SDLoc dl(Op);
  if (InfoAndKind.IsAArch64) {
    CCVal = DAG.getConstant(
        AArch64CC::getInvertedCondCode(InfoAndKind.Info.AArch64.CC), dl,
        MVT::i32);
    Cmp = *InfoAndKind.Info.AArch64.Cmp;
  } else
    Cmp = getAArch64Cmp(*InfoAndKind.Info.Generic.Opnd0,
                      *InfoAndKind.Info.Generic.Opnd1,
                      ISD::getSetCCInverse(InfoAndKind.Info.Generic.CC, true),
                      CCVal, DAG, dl);

  EVT VT = Op->getValueType(0);
  LHS = DAG.getNode(ISD::ADD, dl, VT, RHS, DAG.getConstant(1, dl, VT));
  return DAG.getNode(AArch64ISD::CSEL, dl, VT, RHS, LHS, CCVal, Cmp);
}

// The basic add/sub long vector instructions have variants with "2" on the end
// which act on the high-half of their inputs. They are normally matched by
// patterns like:
//
// (add (zeroext (extract_high LHS)),
//      (zeroext (extract_high RHS)))
// -> uaddl2 vD, vN, vM
//
// However, if one of the extracts is something like a duplicate, this
// instruction can still be used profitably. This function puts the DAG into a
// more appropriate form for those patterns to trigger.
static SDValue performAddSubLongCombine(SDNode *N,
                                        TargetLowering::DAGCombinerInfo &DCI,
                                        SelectionDAG &DAG) {
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  MVT VT = N->getSimpleValueType(0);
  if (!VT.is128BitVector()) {
    if (N->getOpcode() == ISD::ADD)
      return performSetccAddFolding(N, DAG);
    return SDValue();
  }

  // Make sure both branches are extended in the same way.
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  if ((LHS.getOpcode() != ISD::ZERO_EXTEND &&
       LHS.getOpcode() != ISD::SIGN_EXTEND) ||
      LHS.getOpcode() != RHS.getOpcode())
    return SDValue();

  unsigned ExtType = LHS.getOpcode();

  // It's not worth doing if at least one of the inputs isn't already an
  // extract, but we don't know which it'll be so we have to try both.
  if (isEssentiallyExtractSubvector(LHS.getOperand(0))) {
    RHS = tryExtendDUPToExtractHigh(RHS.getOperand(0), DAG);
    if (!RHS.getNode())
      return SDValue();

    RHS = DAG.getNode(ExtType, SDLoc(N), VT, RHS);
  } else if (isEssentiallyExtractSubvector(RHS.getOperand(0))) {
    LHS = tryExtendDUPToExtractHigh(LHS.getOperand(0), DAG);
    if (!LHS.getNode())
      return SDValue();

    LHS = DAG.getNode(ExtType, SDLoc(N), VT, LHS);
  }

  return DAG.getNode(N->getOpcode(), SDLoc(N), VT, LHS, RHS);
}

// Massage DAGs which we can use the high-half "long" operations on into
// something isel will recognize better. E.g.
//
// (aarch64_neon_umull (extract_high vec) (dupv64 scalar)) -->
//   (aarch64_neon_umull (extract_high (v2i64 vec)))
//                     (extract_high (v2i64 (dup128 scalar)))))
//
static SDValue tryCombineLongOpWithDup(unsigned IID, SDNode *N,
                                       TargetLowering::DAGCombinerInfo &DCI,
                                       SelectionDAG &DAG) {
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  SDValue LHS = N->getOperand(1);
  SDValue RHS = N->getOperand(2);
  assert(LHS.getValueType().is64BitVector() &&
         RHS.getValueType().is64BitVector() &&
         "unexpected shape for long operation");

  // Either node could be a DUP, but it's not worth doing both of them (you'd
  // just as well use the non-high version) so look for a corresponding extract
  // operation on the other "wing".
  if (isEssentiallyExtractSubvector(LHS)) {
    RHS = tryExtendDUPToExtractHigh(RHS, DAG);
    if (!RHS.getNode())
      return SDValue();
  } else if (isEssentiallyExtractSubvector(RHS)) {
    LHS = tryExtendDUPToExtractHigh(LHS, DAG);
    if (!LHS.getNode())
      return SDValue();
  }

  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SDLoc(N), N->getValueType(0),
                     N->getOperand(0), LHS, RHS);
}

static SDValue tryCombineShiftImm(unsigned IID, SDNode *N, SelectionDAG &DAG) {
  MVT ElemTy = N->getSimpleValueType(0).getScalarType();
  unsigned ElemBits = ElemTy.getSizeInBits();

  int64_t ShiftAmount;
  if (BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(N->getOperand(2))) {
    APInt SplatValue, SplatUndef;
    unsigned SplatBitSize;
    bool HasAnyUndefs;
    if (!BVN->isConstantSplat(SplatValue, SplatUndef, SplatBitSize,
                              HasAnyUndefs, ElemBits) ||
        SplatBitSize != ElemBits)
      return SDValue();

    ShiftAmount = SplatValue.getSExtValue();
  } else if (ConstantSDNode *CVN = dyn_cast<ConstantSDNode>(N->getOperand(2))) {
    ShiftAmount = CVN->getSExtValue();
  } else
    return SDValue();

  unsigned Opcode;
  bool IsRightShift;
  switch (IID) {
  default:
    llvm_unreachable("Unknown shift intrinsic");
  case Intrinsic::aarch64_neon_sqshl:
    Opcode = AArch64ISD::SQSHL_I;
    IsRightShift = false;
    break;
  case Intrinsic::aarch64_neon_uqshl:
    Opcode = AArch64ISD::UQSHL_I;
    IsRightShift = false;
    break;
  case Intrinsic::aarch64_neon_srshl:
    Opcode = AArch64ISD::SRSHR_I;
    IsRightShift = true;
    break;
  case Intrinsic::aarch64_neon_urshl:
    Opcode = AArch64ISD::URSHR_I;
    IsRightShift = true;
    break;
  case Intrinsic::aarch64_neon_sqshlu:
    Opcode = AArch64ISD::SQSHLU_I;
    IsRightShift = false;
    break;
  }

  if (IsRightShift && ShiftAmount <= -1 && ShiftAmount >= -(int)ElemBits) {
    SDLoc dl(N);
    return DAG.getNode(Opcode, dl, N->getValueType(0), N->getOperand(1),
                       DAG.getConstant(-ShiftAmount, dl, MVT::i32));
  } else if (!IsRightShift && ShiftAmount >= 0 && ShiftAmount < ElemBits) {
    SDLoc dl(N);
    return DAG.getNode(Opcode, dl, N->getValueType(0), N->getOperand(1),
                       DAG.getConstant(ShiftAmount, dl, MVT::i32));
  }

  return SDValue();
}

// The CRC32[BH] instructions ignore the high bits of their data operand. Since
// the intrinsics must be legal and take an i32, this means there's almost
// certainly going to be a zext in the DAG which we can eliminate.
static SDValue tryCombineCRC32(unsigned Mask, SDNode *N, SelectionDAG &DAG) {
  SDValue AndN = N->getOperand(2);
  if (AndN.getOpcode() != ISD::AND)
    return SDValue();

  ConstantSDNode *CMask = dyn_cast<ConstantSDNode>(AndN.getOperand(1));
  if (!CMask || CMask->getZExtValue() != Mask)
    return SDValue();

  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SDLoc(N), MVT::i32,
                     N->getOperand(0), N->getOperand(1), AndN.getOperand(0));
}

static SDValue combineAcrossLanesIntrinsic(unsigned Opc, SDNode *N,
                                           SelectionDAG &DAG) {
  SDLoc dl(N);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, N->getValueType(0),
                     DAG.getNode(Opc, dl,
                                 N->getOperand(1).getSimpleValueType(),
                                 N->getOperand(1)),
                     DAG.getConstant(0, dl, MVT::i64));
}

static SDValue performIntrinsicCombine(SDNode *N,
                                       TargetLowering::DAGCombinerInfo &DCI,
                                       const AArch64Subtarget *Subtarget) {
  SelectionDAG &DAG = DCI.DAG;
  unsigned IID = getIntrinsicID(N);
  switch (IID) {
  default:
    break;
  case Intrinsic::aarch64_neon_vcvtfxs2fp:
  case Intrinsic::aarch64_neon_vcvtfxu2fp:
    return tryCombineFixedPointConvert(N, DCI, DAG);
  case Intrinsic::aarch64_neon_saddv:
    return combineAcrossLanesIntrinsic(AArch64ISD::SADDV, N, DAG);
  case Intrinsic::aarch64_neon_uaddv:
    return combineAcrossLanesIntrinsic(AArch64ISD::UADDV, N, DAG);
  case Intrinsic::aarch64_neon_sminv:
    return combineAcrossLanesIntrinsic(AArch64ISD::SMINV, N, DAG);
  case Intrinsic::aarch64_neon_uminv:
    return combineAcrossLanesIntrinsic(AArch64ISD::UMINV, N, DAG);
  case Intrinsic::aarch64_neon_smaxv:
    return combineAcrossLanesIntrinsic(AArch64ISD::SMAXV, N, DAG);
  case Intrinsic::aarch64_neon_umaxv:
    return combineAcrossLanesIntrinsic(AArch64ISD::UMAXV, N, DAG);
  case Intrinsic::aarch64_neon_fmax:
    return DAG.getNode(ISD::FMAXIMUM, SDLoc(N), N->getValueType(0),
                       N->getOperand(1), N->getOperand(2));
  case Intrinsic::aarch64_neon_fmin:
    return DAG.getNode(ISD::FMINIMUM, SDLoc(N), N->getValueType(0),
                       N->getOperand(1), N->getOperand(2));
  case Intrinsic::aarch64_neon_fmaxnm:
    return DAG.getNode(ISD::FMAXNUM, SDLoc(N), N->getValueType(0),
                       N->getOperand(1), N->getOperand(2));
  case Intrinsic::aarch64_neon_fminnm:
    return DAG.getNode(ISD::FMINNUM, SDLoc(N), N->getValueType(0),
                       N->getOperand(1), N->getOperand(2));
  case Intrinsic::aarch64_neon_smull:
  case Intrinsic::aarch64_neon_umull:
  case Intrinsic::aarch64_neon_pmull:
  case Intrinsic::aarch64_neon_sqdmull:
    return tryCombineLongOpWithDup(IID, N, DCI, DAG);
  case Intrinsic::aarch64_neon_sqshl:
  case Intrinsic::aarch64_neon_uqshl:
  case Intrinsic::aarch64_neon_sqshlu:
  case Intrinsic::aarch64_neon_srshl:
  case Intrinsic::aarch64_neon_urshl:
    return tryCombineShiftImm(IID, N, DAG);
  case Intrinsic::aarch64_crc32b:
  case Intrinsic::aarch64_crc32cb:
    return tryCombineCRC32(0xff, N, DAG);
  case Intrinsic::aarch64_crc32h:
  case Intrinsic::aarch64_crc32ch:
    return tryCombineCRC32(0xffff, N, DAG);
  }
  return SDValue();
}

static SDValue performExtendCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI,
                                    SelectionDAG &DAG) {
  // If we see something like (zext (sabd (extract_high ...), (DUP ...))) then
  // we can convert that DUP into another extract_high (of a bigger DUP), which
  // helps the backend to decide that an sabdl2 would be useful, saving a real
  // extract_high operation.
  if (!DCI.isBeforeLegalizeOps() && N->getOpcode() == ISD::ZERO_EXTEND &&
      N->getOperand(0).getOpcode() == ISD::INTRINSIC_WO_CHAIN) {
    SDNode *ABDNode = N->getOperand(0).getNode();
    unsigned IID = getIntrinsicID(ABDNode);
    if (IID == Intrinsic::aarch64_neon_sabd ||
        IID == Intrinsic::aarch64_neon_uabd) {
      SDValue NewABD = tryCombineLongOpWithDup(IID, ABDNode, DCI, DAG);
      if (!NewABD.getNode())
        return SDValue();

      return DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N), N->getValueType(0),
                         NewABD);
    }
  }

  // This is effectively a custom type legalization for AArch64.
  //
  // Type legalization will split an extend of a small, legal, type to a larger
  // illegal type by first splitting the destination type, often creating
  // illegal source types, which then get legalized in isel-confusing ways,
  // leading to really terrible codegen. E.g.,
  //   %result = v8i32 sext v8i8 %value
  // becomes
  //   %losrc = extract_subreg %value, ...
  //   %hisrc = extract_subreg %value, ...
  //   %lo = v4i32 sext v4i8 %losrc
  //   %hi = v4i32 sext v4i8 %hisrc
  // Things go rapidly downhill from there.
  //
  // For AArch64, the [sz]ext vector instructions can only go up one element
  // size, so we can, e.g., extend from i8 to i16, but to go from i8 to i32
  // take two instructions.
  //
  // This implies that the most efficient way to do the extend from v8i8
  // to two v4i32 values is to first extend the v8i8 to v8i16, then do
  // the normal splitting to happen for the v8i16->v8i32.

  // This is pre-legalization to catch some cases where the default
  // type legalization will create ill-tempered code.
  if (!DCI.isBeforeLegalizeOps())
    return SDValue();

  // We're only interested in cleaning things up for non-legal vector types
  // here. If both the source and destination are legal, things will just
  // work naturally without any fiddling.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT ResVT = N->getValueType(0);
  if (!ResVT.isVector() || TLI.isTypeLegal(ResVT))
    return SDValue();
  // If the vector type isn't a simple VT, it's beyond the scope of what
  // we're  worried about here. Let legalization do its thing and hope for
  // the best.
  SDValue Src = N->getOperand(0);
  EVT SrcVT = Src->getValueType(0);
  if (!ResVT.isSimple() || !SrcVT.isSimple())
    return SDValue();

  // If the source VT is a 64-bit vector, we can play games and get the
  // better results we want.
  if (SrcVT.getSizeInBits() != 64)
    return SDValue();

  unsigned SrcEltSize = SrcVT.getScalarSizeInBits();
  unsigned ElementCount = SrcVT.getVectorNumElements();
  SrcVT = MVT::getVectorVT(MVT::getIntegerVT(SrcEltSize * 2), ElementCount);
  SDLoc DL(N);
  Src = DAG.getNode(N->getOpcode(), DL, SrcVT, Src);

  // Now split the rest of the operation into two halves, each with a 64
  // bit source.
  EVT LoVT, HiVT;
  SDValue Lo, Hi;
  unsigned NumElements = ResVT.getVectorNumElements();
  assert(!(NumElements & 1) && "Splitting vector, but not in half!");
  LoVT = HiVT = EVT::getVectorVT(*DAG.getContext(),
                                 ResVT.getVectorElementType(), NumElements / 2);

  EVT InNVT = EVT::getVectorVT(*DAG.getContext(), SrcVT.getVectorElementType(),
                               LoVT.getVectorNumElements());
  Lo = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, InNVT, Src,
                   DAG.getConstant(0, DL, MVT::i64));
  Hi = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, InNVT, Src,
                   DAG.getConstant(InNVT.getVectorNumElements(), DL, MVT::i64));
  Lo = DAG.getNode(N->getOpcode(), DL, LoVT, Lo);
  Hi = DAG.getNode(N->getOpcode(), DL, HiVT, Hi);

  // Now combine the parts back together so we still have a single result
  // like the combiner expects.
  return DAG.getNode(ISD::CONCAT_VECTORS, DL, ResVT, Lo, Hi);
}

static SDValue splitStoreSplat(SelectionDAG &DAG, StoreSDNode &St,
                               SDValue SplatVal, unsigned NumVecElts) {
  assert(!St.isTruncatingStore() && "cannot split truncating vector store");
  unsigned OrigAlignment = St.getAlignment();
  unsigned EltOffset = SplatVal.getValueType().getSizeInBits() / 8;

  // Create scalar stores. This is at least as good as the code sequence for a
  // split unaligned store which is a dup.s, ext.b, and two stores.
  // Most of the time the three stores should be replaced by store pair
  // instructions (stp).
  SDLoc DL(&St);
  SDValue BasePtr = St.getBasePtr();
  uint64_t BaseOffset = 0;

  const MachinePointerInfo &PtrInfo = St.getPointerInfo();
  SDValue NewST1 =
      DAG.getStore(St.getChain(), DL, SplatVal, BasePtr, PtrInfo,
                   OrigAlignment, St.getMemOperand()->getFlags());

  // As this in ISel, we will not merge this add which may degrade results.
  if (BasePtr->getOpcode() == ISD::ADD &&
      isa<ConstantSDNode>(BasePtr->getOperand(1))) {
    BaseOffset = cast<ConstantSDNode>(BasePtr->getOperand(1))->getSExtValue();
    BasePtr = BasePtr->getOperand(0);
  }

  unsigned Offset = EltOffset;
  while (--NumVecElts) {
    unsigned Alignment = MinAlign(OrigAlignment, Offset);
    SDValue OffsetPtr =
        DAG.getNode(ISD::ADD, DL, MVT::i64, BasePtr,
                    DAG.getConstant(BaseOffset + Offset, DL, MVT::i64));
    NewST1 = DAG.getStore(NewST1.getValue(0), DL, SplatVal, OffsetPtr,
                          PtrInfo.getWithOffset(Offset), Alignment,
                          St.getMemOperand()->getFlags());
    Offset += EltOffset;
  }
  return NewST1;
}

/// Replace a splat of zeros to a vector store by scalar stores of WZR/XZR.  The
/// load store optimizer pass will merge them to store pair stores.  This should
/// be better than a movi to create the vector zero followed by a vector store
/// if the zero constant is not re-used, since one instructions and one register
/// live range will be removed.
///
/// For example, the final generated code should be:
///
///   stp xzr, xzr, [x0]
///
/// instead of:
///
///   movi v0.2d, #0
///   str q0, [x0]
///
static SDValue replaceZeroVectorStore(SelectionDAG &DAG, StoreSDNode &St) {
  SDValue StVal = St.getValue();
  EVT VT = StVal.getValueType();

  // It is beneficial to scalarize a zero splat store for 2 or 3 i64 elements or
  // 2, 3 or 4 i32 elements.
  int NumVecElts = VT.getVectorNumElements();
  if (!(((NumVecElts == 2 || NumVecElts == 3) &&
         VT.getVectorElementType().getSizeInBits() == 64) ||
        ((NumVecElts == 2 || NumVecElts == 3 || NumVecElts == 4) &&
         VT.getVectorElementType().getSizeInBits() == 32)))
    return SDValue();

  if (StVal.getOpcode() != ISD::BUILD_VECTOR)
    return SDValue();

  // If the zero constant has more than one use then the vector store could be
  // better since the constant mov will be amortized and stp q instructions
  // should be able to be formed.
  if (!StVal.hasOneUse())
    return SDValue();

  // If the store is truncating then it's going down to i16 or smaller, which
  // means it can be implemented in a single store anyway.
  if (St.isTruncatingStore())
    return SDValue();

  // If the immediate offset of the address operand is too large for the stp
  // instruction, then bail out.
  if (DAG.isBaseWithConstantOffset(St.getBasePtr())) {
    int64_t Offset = St.getBasePtr()->getConstantOperandVal(1);
    if (Offset < -512 || Offset > 504)
      return SDValue();
  }

  for (int I = 0; I < NumVecElts; ++I) {
    SDValue EltVal = StVal.getOperand(I);
    if (!isNullConstant(EltVal) && !isNullFPConstant(EltVal))
      return SDValue();
  }

  // Use a CopyFromReg WZR/XZR here to prevent
  // DAGCombiner::MergeConsecutiveStores from undoing this transformation.
  SDLoc DL(&St);
  unsigned ZeroReg;
  EVT ZeroVT;
  if (VT.getVectorElementType().getSizeInBits() == 32) {
    ZeroReg = AArch64::WZR;
    ZeroVT = MVT::i32;
  } else {
    ZeroReg = AArch64::XZR;
    ZeroVT = MVT::i64;
  }
  SDValue SplatVal =
      DAG.getCopyFromReg(DAG.getEntryNode(), DL, ZeroReg, ZeroVT);
  return splitStoreSplat(DAG, St, SplatVal, NumVecElts);
}

/// Replace a splat of a scalar to a vector store by scalar stores of the scalar
/// value. The load store optimizer pass will merge them to store pair stores.
/// This has better performance than a splat of the scalar followed by a split
/// vector store. Even if the stores are not merged it is four stores vs a dup,
/// followed by an ext.b and two stores.
static SDValue replaceSplatVectorStore(SelectionDAG &DAG, StoreSDNode &St) {
  SDValue StVal = St.getValue();
  EVT VT = StVal.getValueType();

  // Don't replace floating point stores, they possibly won't be transformed to
  // stp because of the store pair suppress pass.
  if (VT.isFloatingPoint())
    return SDValue();

  // We can express a splat as store pair(s) for 2 or 4 elements.
  unsigned NumVecElts = VT.getVectorNumElements();
  if (NumVecElts != 4 && NumVecElts != 2)
    return SDValue();

  // If the store is truncating then it's going down to i16 or smaller, which
  // means it can be implemented in a single store anyway.
  if (St.isTruncatingStore())
    return SDValue();

  // Check that this is a splat.
  // Make sure that each of the relevant vector element locations are inserted
  // to, i.e. 0 and 1 for v2i64 and 0, 1, 2, 3 for v4i32.
  std::bitset<4> IndexNotInserted((1 << NumVecElts) - 1);
  SDValue SplatVal;
  for (unsigned I = 0; I < NumVecElts; ++I) {
    // Check for insert vector elements.
    if (StVal.getOpcode() != ISD::INSERT_VECTOR_ELT)
      return SDValue();

    // Check that same value is inserted at each vector element.
    if (I == 0)
      SplatVal = StVal.getOperand(1);
    else if (StVal.getOperand(1) != SplatVal)
      return SDValue();

    // Check insert element index.
    ConstantSDNode *CIndex = dyn_cast<ConstantSDNode>(StVal.getOperand(2));
    if (!CIndex)
      return SDValue();
    uint64_t IndexVal = CIndex->getZExtValue();
    if (IndexVal >= NumVecElts)
      return SDValue();
    IndexNotInserted.reset(IndexVal);

    StVal = StVal.getOperand(0);
  }
  // Check that all vector element locations were inserted to.
  if (IndexNotInserted.any())
      return SDValue();

  return splitStoreSplat(DAG, St, SplatVal, NumVecElts);
}

static SDValue splitStores(SDNode *N, TargetLowering::DAGCombinerInfo &DCI,
                           SelectionDAG &DAG,
                           const AArch64Subtarget *Subtarget) {

  StoreSDNode *S = cast<StoreSDNode>(N);
  if (S->isVolatile() || S->isIndexed())
    return SDValue();

  SDValue StVal = S->getValue();
  EVT VT = StVal.getValueType();
  if (!VT.isVector())
    return SDValue();

  // If we get a splat of zeros, convert this vector store to a store of
  // scalars. They will be merged into store pairs of xzr thereby removing one
  // instruction and one register.
  if (SDValue ReplacedZeroSplat = replaceZeroVectorStore(DAG, *S))
    return ReplacedZeroSplat;

  // FIXME: The logic for deciding if an unaligned store should be split should
  // be included in TLI.allowsMisalignedMemoryAccesses(), and there should be
  // a call to that function here.

  if (!Subtarget->isMisaligned128StoreSlow())
    return SDValue();

  // Don't split at -Oz.
  if (DAG.getMachineFunction().getFunction().optForMinSize())
    return SDValue();

  // Don't split v2i64 vectors. Memcpy lowering produces those and splitting
  // those up regresses performance on micro-benchmarks and olden/bh.
  if (VT.getVectorNumElements() < 2 || VT == MVT::v2i64)
    return SDValue();

  // Split unaligned 16B stores. They are terrible for performance.
  // Don't split stores with alignment of 1 or 2. Code that uses clang vector
  // extensions can use this to mark that it does not want splitting to happen
  // (by underspecifying alignment to be 1 or 2). Furthermore, the chance of
  // eliminating alignment hazards is only 1 in 8 for alignment of 2.
  if (VT.getSizeInBits() != 128 || S->getAlignment() >= 16 ||
      S->getAlignment() <= 2)
    return SDValue();

  // If we get a splat of a scalar convert this vector store to a store of
  // scalars. They will be merged into store pairs thereby removing two
  // instructions.
  if (SDValue ReplacedSplat = replaceSplatVectorStore(DAG, *S))
    return ReplacedSplat;

  SDLoc DL(S);
  unsigned NumElts = VT.getVectorNumElements() / 2;
  // Split VT into two.
  EVT HalfVT =
      EVT::getVectorVT(*DAG.getContext(), VT.getVectorElementType(), NumElts);
  SDValue SubVector0 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, HalfVT, StVal,
                                   DAG.getConstant(0, DL, MVT::i64));
  SDValue SubVector1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, HalfVT, StVal,
                                   DAG.getConstant(NumElts, DL, MVT::i64));
  SDValue BasePtr = S->getBasePtr();
  SDValue NewST1 =
      DAG.getStore(S->getChain(), DL, SubVector0, BasePtr, S->getPointerInfo(),
                   S->getAlignment(), S->getMemOperand()->getFlags());
  SDValue OffsetPtr = DAG.getNode(ISD::ADD, DL, MVT::i64, BasePtr,
                                  DAG.getConstant(8, DL, MVT::i64));
  return DAG.getStore(NewST1.getValue(0), DL, SubVector1, OffsetPtr,
                      S->getPointerInfo(), S->getAlignment(),
                      S->getMemOperand()->getFlags());
}

/// Target-specific DAG combine function for post-increment LD1 (lane) and
/// post-increment LD1R.
static SDValue performPostLD1Combine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     bool IsLaneOp) {
  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);

  unsigned LoadIdx = IsLaneOp ? 1 : 0;
  SDNode *LD = N->getOperand(LoadIdx).getNode();
  // If it is not LOAD, can not do such combine.
  if (LD->getOpcode() != ISD::LOAD)
    return SDValue();

  // The vector lane must be a constant in the LD1LANE opcode.
  SDValue Lane;
  if (IsLaneOp) {
    Lane = N->getOperand(2);
    auto *LaneC = dyn_cast<ConstantSDNode>(Lane);
    if (!LaneC || LaneC->getZExtValue() >= VT.getVectorNumElements())
      return SDValue();
  }

  LoadSDNode *LoadSDN = cast<LoadSDNode>(LD);
  EVT MemVT = LoadSDN->getMemoryVT();
  // Check if memory operand is the same type as the vector element.
  if (MemVT != VT.getVectorElementType())
    return SDValue();

  // Check if there are other uses. If so, do not combine as it will introduce
  // an extra load.
  for (SDNode::use_iterator UI = LD->use_begin(), UE = LD->use_end(); UI != UE;
       ++UI) {
    if (UI.getUse().getResNo() == 1) // Ignore uses of the chain result.
      continue;
    if (*UI != N)
      return SDValue();
  }

  SDValue Addr = LD->getOperand(1);
  SDValue Vector = N->getOperand(0);
  // Search for a use of the address operand that is an increment.
  for (SDNode::use_iterator UI = Addr.getNode()->use_begin(), UE =
       Addr.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User->getOpcode() != ISD::ADD
        || UI.getUse().getResNo() != Addr.getResNo())
      continue;

    // If the increment is a constant, it must match the memory ref size.
    SDValue Inc = User->getOperand(User->getOperand(0) == Addr ? 1 : 0);
    if (ConstantSDNode *CInc = dyn_cast<ConstantSDNode>(Inc.getNode())) {
      uint32_t IncVal = CInc->getZExtValue();
      unsigned NumBytes = VT.getScalarSizeInBits() / 8;
      if (IncVal != NumBytes)
        continue;
      Inc = DAG.getRegister(AArch64::XZR, MVT::i64);
    }

    // To avoid cycle construction make sure that neither the load nor the add
    // are predecessors to each other or the Vector.
    SmallPtrSet<const SDNode *, 32> Visited;
    SmallVector<const SDNode *, 16> Worklist;
    Visited.insert(N);
    Worklist.push_back(User);
    Worklist.push_back(LD);
    Worklist.push_back(Vector.getNode());
    if (SDNode::hasPredecessorHelper(LD, Visited, Worklist) ||
        SDNode::hasPredecessorHelper(User, Visited, Worklist))
      continue;

    SmallVector<SDValue, 8> Ops;
    Ops.push_back(LD->getOperand(0));  // Chain
    if (IsLaneOp) {
      Ops.push_back(Vector);           // The vector to be inserted
      Ops.push_back(Lane);             // The lane to be inserted in the vector
    }
    Ops.push_back(Addr);
    Ops.push_back(Inc);

    EVT Tys[3] = { VT, MVT::i64, MVT::Other };
    SDVTList SDTys = DAG.getVTList(Tys);
    unsigned NewOp = IsLaneOp ? AArch64ISD::LD1LANEpost : AArch64ISD::LD1DUPpost;
    SDValue UpdN = DAG.getMemIntrinsicNode(NewOp, SDLoc(N), SDTys, Ops,
                                           MemVT,
                                           LoadSDN->getMemOperand());

    // Update the uses.
    SDValue NewResults[] = {
        SDValue(LD, 0),            // The result of load
        SDValue(UpdN.getNode(), 2) // Chain
    };
    DCI.CombineTo(LD, NewResults);
    DCI.CombineTo(N, SDValue(UpdN.getNode(), 0));     // Dup/Inserted Result
    DCI.CombineTo(User, SDValue(UpdN.getNode(), 1));  // Write back register

    break;
  }
  return SDValue();
}

/// Simplify ``Addr`` given that the top byte of it is ignored by HW during
/// address translation.
static bool performTBISimplification(SDValue Addr,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     SelectionDAG &DAG) {
  APInt DemandedMask = APInt::getLowBitsSet(64, 56);
  KnownBits Known;
  TargetLowering::TargetLoweringOpt TLO(DAG, !DCI.isBeforeLegalize(),
                                        !DCI.isBeforeLegalizeOps());
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (TLI.SimplifyDemandedBits(Addr, DemandedMask, Known, TLO)) {
    DCI.CommitTargetLoweringOpt(TLO);
    return true;
  }
  return false;
}

static SDValue performSTORECombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI,
                                   SelectionDAG &DAG,
                                   const AArch64Subtarget *Subtarget) {
  if (SDValue Split = splitStores(N, DCI, DAG, Subtarget))
    return Split;

  if (Subtarget->supportsAddressTopByteIgnored() &&
      performTBISimplification(N->getOperand(2), DCI, DAG))
    return SDValue(N, 0);

  return SDValue();
}


/// Target-specific DAG combine function for NEON load/store intrinsics
/// to merge base address updates.
static SDValue performNEONPostLDSTCombine(SDNode *N,
                                          TargetLowering::DAGCombinerInfo &DCI,
                                          SelectionDAG &DAG) {
  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  unsigned AddrOpIdx = N->getNumOperands() - 1;
  SDValue Addr = N->getOperand(AddrOpIdx);

  // Search for a use of the address operand that is an increment.
  for (SDNode::use_iterator UI = Addr.getNode()->use_begin(),
       UE = Addr.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User->getOpcode() != ISD::ADD ||
        UI.getUse().getResNo() != Addr.getResNo())
      continue;

    // Check that the add is independent of the load/store.  Otherwise, folding
    // it would create a cycle.
    SmallPtrSet<const SDNode *, 32> Visited;
    SmallVector<const SDNode *, 16> Worklist;
    Visited.insert(Addr.getNode());
    Worklist.push_back(N);
    Worklist.push_back(User);
    if (SDNode::hasPredecessorHelper(N, Visited, Worklist) ||
        SDNode::hasPredecessorHelper(User, Visited, Worklist))
      continue;

    // Find the new opcode for the updating load/store.
    bool IsStore = false;
    bool IsLaneOp = false;
    bool IsDupOp = false;
    unsigned NewOpc = 0;
    unsigned NumVecs = 0;
    unsigned IntNo = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
    switch (IntNo) {
    default: llvm_unreachable("unexpected intrinsic for Neon base update");
    case Intrinsic::aarch64_neon_ld2:       NewOpc = AArch64ISD::LD2post;
      NumVecs = 2; break;
    case Intrinsic::aarch64_neon_ld3:       NewOpc = AArch64ISD::LD3post;
      NumVecs = 3; break;
    case Intrinsic::aarch64_neon_ld4:       NewOpc = AArch64ISD::LD4post;
      NumVecs = 4; break;
    case Intrinsic::aarch64_neon_st2:       NewOpc = AArch64ISD::ST2post;
      NumVecs = 2; IsStore = true; break;
    case Intrinsic::aarch64_neon_st3:       NewOpc = AArch64ISD::ST3post;
      NumVecs = 3; IsStore = true; break;
    case Intrinsic::aarch64_neon_st4:       NewOpc = AArch64ISD::ST4post;
      NumVecs = 4; IsStore = true; break;
    case Intrinsic::aarch64_neon_ld1x2:     NewOpc = AArch64ISD::LD1x2post;
      NumVecs = 2; break;
    case Intrinsic::aarch64_neon_ld1x3:     NewOpc = AArch64ISD::LD1x3post;
      NumVecs = 3; break;
    case Intrinsic::aarch64_neon_ld1x4:     NewOpc = AArch64ISD::LD1x4post;
      NumVecs = 4; break;
    case Intrinsic::aarch64_neon_st1x2:     NewOpc = AArch64ISD::ST1x2post;
      NumVecs = 2; IsStore = true; break;
    case Intrinsic::aarch64_neon_st1x3:     NewOpc = AArch64ISD::ST1x3post;
      NumVecs = 3; IsStore = true; break;
    case Intrinsic::aarch64_neon_st1x4:     NewOpc = AArch64ISD::ST1x4post;
      NumVecs = 4; IsStore = true; break;
    case Intrinsic::aarch64_neon_ld2r:      NewOpc = AArch64ISD::LD2DUPpost;
      NumVecs = 2; IsDupOp = true; break;
    case Intrinsic::aarch64_neon_ld3r:      NewOpc = AArch64ISD::LD3DUPpost;
      NumVecs = 3; IsDupOp = true; break;
    case Intrinsic::aarch64_neon_ld4r:      NewOpc = AArch64ISD::LD4DUPpost;
      NumVecs = 4; IsDupOp = true; break;
    case Intrinsic::aarch64_neon_ld2lane:   NewOpc = AArch64ISD::LD2LANEpost;
      NumVecs = 2; IsLaneOp = true; break;
    case Intrinsic::aarch64_neon_ld3lane:   NewOpc = AArch64ISD::LD3LANEpost;
      NumVecs = 3; IsLaneOp = true; break;
    case Intrinsic::aarch64_neon_ld4lane:   NewOpc = AArch64ISD::LD4LANEpost;
      NumVecs = 4; IsLaneOp = true; break;
    case Intrinsic::aarch64_neon_st2lane:   NewOpc = AArch64ISD::ST2LANEpost;
      NumVecs = 2; IsStore = true; IsLaneOp = true; break;
    case Intrinsic::aarch64_neon_st3lane:   NewOpc = AArch64ISD::ST3LANEpost;
      NumVecs = 3; IsStore = true; IsLaneOp = true; break;
    case Intrinsic::aarch64_neon_st4lane:   NewOpc = AArch64ISD::ST4LANEpost;
      NumVecs = 4; IsStore = true; IsLaneOp = true; break;
    }

    EVT VecTy;
    if (IsStore)
      VecTy = N->getOperand(2).getValueType();
    else
      VecTy = N->getValueType(0);

    // If the increment is a constant, it must match the memory ref size.
    SDValue Inc = User->getOperand(User->getOperand(0) == Addr ? 1 : 0);
    if (ConstantSDNode *CInc = dyn_cast<ConstantSDNode>(Inc.getNode())) {
      uint32_t IncVal = CInc->getZExtValue();
      unsigned NumBytes = NumVecs * VecTy.getSizeInBits() / 8;
      if (IsLaneOp || IsDupOp)
        NumBytes /= VecTy.getVectorNumElements();
      if (IncVal != NumBytes)
        continue;
      Inc = DAG.getRegister(AArch64::XZR, MVT::i64);
    }
    SmallVector<SDValue, 8> Ops;
    Ops.push_back(N->getOperand(0)); // Incoming chain
    // Load lane and store have vector list as input.
    if (IsLaneOp || IsStore)
      for (unsigned i = 2; i < AddrOpIdx; ++i)
        Ops.push_back(N->getOperand(i));
    Ops.push_back(Addr); // Base register
    Ops.push_back(Inc);

    // Return Types.
    EVT Tys[6];
    unsigned NumResultVecs = (IsStore ? 0 : NumVecs);
    unsigned n;
    for (n = 0; n < NumResultVecs; ++n)
      Tys[n] = VecTy;
    Tys[n++] = MVT::i64;  // Type of write back register
    Tys[n] = MVT::Other;  // Type of the chain
    SDVTList SDTys = DAG.getVTList(makeArrayRef(Tys, NumResultVecs + 2));

    MemIntrinsicSDNode *MemInt = cast<MemIntrinsicSDNode>(N);
    SDValue UpdN = DAG.getMemIntrinsicNode(NewOpc, SDLoc(N), SDTys, Ops,
                                           MemInt->getMemoryVT(),
                                           MemInt->getMemOperand());

    // Update the uses.
    std::vector<SDValue> NewResults;
    for (unsigned i = 0; i < NumResultVecs; ++i) {
      NewResults.push_back(SDValue(UpdN.getNode(), i));
    }
    NewResults.push_back(SDValue(UpdN.getNode(), NumResultVecs + 1));
    DCI.CombineTo(N, NewResults);
    DCI.CombineTo(User, SDValue(UpdN.getNode(), NumResultVecs));

    break;
  }
  return SDValue();
}

// Checks to see if the value is the prescribed width and returns information
// about its extension mode.
static
bool checkValueWidth(SDValue V, unsigned width, ISD::LoadExtType &ExtType) {
  ExtType = ISD::NON_EXTLOAD;
  switch(V.getNode()->getOpcode()) {
  default:
    return false;
  case ISD::LOAD: {
    LoadSDNode *LoadNode = cast<LoadSDNode>(V.getNode());
    if ((LoadNode->getMemoryVT() == MVT::i8 && width == 8)
       || (LoadNode->getMemoryVT() == MVT::i16 && width == 16)) {
      ExtType = LoadNode->getExtensionType();
      return true;
    }
    return false;
  }
  case ISD::AssertSext: {
    VTSDNode *TypeNode = cast<VTSDNode>(V.getNode()->getOperand(1));
    if ((TypeNode->getVT() == MVT::i8 && width == 8)
       || (TypeNode->getVT() == MVT::i16 && width == 16)) {
      ExtType = ISD::SEXTLOAD;
      return true;
    }
    return false;
  }
  case ISD::AssertZext: {
    VTSDNode *TypeNode = cast<VTSDNode>(V.getNode()->getOperand(1));
    if ((TypeNode->getVT() == MVT::i8 && width == 8)
       || (TypeNode->getVT() == MVT::i16 && width == 16)) {
      ExtType = ISD::ZEXTLOAD;
      return true;
    }
    return false;
  }
  case ISD::Constant:
  case ISD::TargetConstant: {
    return std::abs(cast<ConstantSDNode>(V.getNode())->getSExtValue()) <
           1LL << (width - 1);
  }
  }

  return true;
}

// This function does a whole lot of voodoo to determine if the tests are
// equivalent without and with a mask. Essentially what happens is that given a
// DAG resembling:
//
//  +-------------+ +-------------+ +-------------+ +-------------+
//  |    Input    | | AddConstant | | CompConstant| |     CC      |
//  +-------------+ +-------------+ +-------------+ +-------------+
//           |           |           |               |
//           V           V           |    +----------+
//          +-------------+  +----+  |    |
//          |     ADD     |  |0xff|  |    |
//          +-------------+  +----+  |    |
//                  |           |    |    |
//                  V           V    |    |
//                 +-------------+   |    |
//                 |     AND     |   |    |
//                 +-------------+   |    |
//                      |            |    |
//                      +-----+      |    |
//                            |      |    |
//                            V      V    V
//                           +-------------+
//                           |     CMP     |
//                           +-------------+
//
// The AND node may be safely removed for some combinations of inputs. In
// particular we need to take into account the extension type of the Input,
// the exact values of AddConstant, CompConstant, and CC, along with the nominal
// width of the input (this can work for any width inputs, the above graph is
// specific to 8 bits.
//
// The specific equations were worked out by generating output tables for each
// AArch64CC value in terms of and AddConstant (w1), CompConstant(w2). The
// problem was simplified by working with 4 bit inputs, which means we only
// needed to reason about 24 distinct bit patterns: 8 patterns unique to zero
// extension (8,15), 8 patterns unique to sign extensions (-8,-1), and 8
// patterns present in both extensions (0,7). For every distinct set of
// AddConstant and CompConstants bit patterns we can consider the masked and
// unmasked versions to be equivalent if the result of this function is true for
// all 16 distinct bit patterns of for the current extension type of Input (w0).
//
//   sub      w8, w0, w1
//   and      w10, w8, #0x0f
//   cmp      w8, w2
//   cset     w9, AArch64CC
//   cmp      w10, w2
//   cset     w11, AArch64CC
//   cmp      w9, w11
//   cset     w0, eq
//   ret
//
// Since the above function shows when the outputs are equivalent it defines
// when it is safe to remove the AND. Unfortunately it only runs on AArch64 and
// would be expensive to run during compiles. The equations below were written
// in a test harness that confirmed they gave equivalent outputs to the above
// for all inputs function, so they can be used determine if the removal is
// legal instead.
//
// isEquivalentMaskless() is the code for testing if the AND can be removed
// factored out of the DAG recognition as the DAG can take several forms.

static bool isEquivalentMaskless(unsigned CC, unsigned width,
                                 ISD::LoadExtType ExtType, int AddConstant,
                                 int CompConstant) {
  // By being careful about our equations and only writing the in term
  // symbolic values and well known constants (0, 1, -1, MaxUInt) we can
  // make them generally applicable to all bit widths.
  int MaxUInt = (1 << width);

  // For the purposes of these comparisons sign extending the type is
  // equivalent to zero extending the add and displacing it by half the integer
  // width. Provided we are careful and make sure our equations are valid over
  // the whole range we can just adjust the input and avoid writing equations
  // for sign extended inputs.
  if (ExtType == ISD::SEXTLOAD)
    AddConstant -= (1 << (width-1));

  switch(CC) {
  case AArch64CC::LE:
  case AArch64CC::GT:
    if ((AddConstant == 0) ||
        (CompConstant == MaxUInt - 1 && AddConstant < 0) ||
        (AddConstant >= 0 && CompConstant < 0) ||
        (AddConstant <= 0 && CompConstant <= 0 && CompConstant < AddConstant))
      return true;
    break;
  case AArch64CC::LT:
  case AArch64CC::GE:
    if ((AddConstant == 0) ||
        (AddConstant >= 0 && CompConstant <= 0) ||
        (AddConstant <= 0 && CompConstant <= 0 && CompConstant <= AddConstant))
      return true;
    break;
  case AArch64CC::HI:
  case AArch64CC::LS:
    if ((AddConstant >= 0 && CompConstant < 0) ||
       (AddConstant <= 0 && CompConstant >= -1 &&
        CompConstant < AddConstant + MaxUInt))
      return true;
   break;
  case AArch64CC::PL:
  case AArch64CC::MI:
    if ((AddConstant == 0) ||
        (AddConstant > 0 && CompConstant <= 0) ||
        (AddConstant < 0 && CompConstant <= AddConstant))
      return true;
    break;
  case AArch64CC::LO:
  case AArch64CC::HS:
    if ((AddConstant >= 0 && CompConstant <= 0) ||
        (AddConstant <= 0 && CompConstant >= 0 &&
         CompConstant <= AddConstant + MaxUInt))
      return true;
    break;
  case AArch64CC::EQ:
  case AArch64CC::NE:
    if ((AddConstant > 0 && CompConstant < 0) ||
        (AddConstant < 0 && CompConstant >= 0 &&
         CompConstant < AddConstant + MaxUInt) ||
        (AddConstant >= 0 && CompConstant >= 0 &&
         CompConstant >= AddConstant) ||
        (AddConstant <= 0 && CompConstant < 0 && CompConstant < AddConstant))
      return true;
    break;
  case AArch64CC::VS:
  case AArch64CC::VC:
  case AArch64CC::AL:
  case AArch64CC::NV:
    return true;
  case AArch64CC::Invalid:
    break;
  }

  return false;
}

static
SDValue performCONDCombine(SDNode *N,
                           TargetLowering::DAGCombinerInfo &DCI,
                           SelectionDAG &DAG, unsigned CCIndex,
                           unsigned CmpIndex) {
  unsigned CC = cast<ConstantSDNode>(N->getOperand(CCIndex))->getSExtValue();
  SDNode *SubsNode = N->getOperand(CmpIndex).getNode();
  unsigned CondOpcode = SubsNode->getOpcode();

  if (CondOpcode != AArch64ISD::SUBS)
    return SDValue();

  // There is a SUBS feeding this condition. Is it fed by a mask we can
  // use?

  SDNode *AndNode = SubsNode->getOperand(0).getNode();
  unsigned MaskBits = 0;

  if (AndNode->getOpcode() != ISD::AND)
    return SDValue();

  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(AndNode->getOperand(1))) {
    uint32_t CNV = CN->getZExtValue();
    if (CNV == 255)
      MaskBits = 8;
    else if (CNV == 65535)
      MaskBits = 16;
  }

  if (!MaskBits)
    return SDValue();

  SDValue AddValue = AndNode->getOperand(0);

  if (AddValue.getOpcode() != ISD::ADD)
    return SDValue();

  // The basic dag structure is correct, grab the inputs and validate them.

  SDValue AddInputValue1 = AddValue.getNode()->getOperand(0);
  SDValue AddInputValue2 = AddValue.getNode()->getOperand(1);
  SDValue SubsInputValue = SubsNode->getOperand(1);

  // The mask is present and the provenance of all the values is a smaller type,
  // lets see if the mask is superfluous.

  if (!isa<ConstantSDNode>(AddInputValue2.getNode()) ||
      !isa<ConstantSDNode>(SubsInputValue.getNode()))
    return SDValue();

  ISD::LoadExtType ExtType;

  if (!checkValueWidth(SubsInputValue, MaskBits, ExtType) ||
      !checkValueWidth(AddInputValue2, MaskBits, ExtType) ||
      !checkValueWidth(AddInputValue1, MaskBits, ExtType) )
    return SDValue();

  if(!isEquivalentMaskless(CC, MaskBits, ExtType,
                cast<ConstantSDNode>(AddInputValue2.getNode())->getSExtValue(),
                cast<ConstantSDNode>(SubsInputValue.getNode())->getSExtValue()))
    return SDValue();

  // The AND is not necessary, remove it.

  SDVTList VTs = DAG.getVTList(SubsNode->getValueType(0),
                               SubsNode->getValueType(1));
  SDValue Ops[] = { AddValue, SubsNode->getOperand(1) };

  SDValue NewValue = DAG.getNode(CondOpcode, SDLoc(SubsNode), VTs, Ops);
  DAG.ReplaceAllUsesWith(SubsNode, NewValue.getNode());

  return SDValue(N, 0);
}

// Optimize compare with zero and branch.
static SDValue performBRCONDCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI,
                                    SelectionDAG &DAG) {
  MachineFunction &MF = DAG.getMachineFunction();
  // Speculation tracking/SLH assumes that optimized TB(N)Z/CB(N)Z instructions
  // will not be produced, as they are conditional branch instructions that do
  // not set flags.
  if (MF.getFunction().hasFnAttribute(Attribute::SpeculativeLoadHardening))
    return SDValue();

  if (SDValue NV = performCONDCombine(N, DCI, DAG, 2, 3))
    N = NV.getNode();
  SDValue Chain = N->getOperand(0);
  SDValue Dest = N->getOperand(1);
  SDValue CCVal = N->getOperand(2);
  SDValue Cmp = N->getOperand(3);

  assert(isa<ConstantSDNode>(CCVal) && "Expected a ConstantSDNode here!");
  unsigned CC = cast<ConstantSDNode>(CCVal)->getZExtValue();
  if (CC != AArch64CC::EQ && CC != AArch64CC::NE)
    return SDValue();

  unsigned CmpOpc = Cmp.getOpcode();
  if (CmpOpc != AArch64ISD::ADDS && CmpOpc != AArch64ISD::SUBS)
    return SDValue();

  // Only attempt folding if there is only one use of the flag and no use of the
  // value.
  if (!Cmp->hasNUsesOfValue(0, 0) || !Cmp->hasNUsesOfValue(1, 1))
    return SDValue();

  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);

  assert(LHS.getValueType() == RHS.getValueType() &&
         "Expected the value type to be the same for both operands!");
  if (LHS.getValueType() != MVT::i32 && LHS.getValueType() != MVT::i64)
    return SDValue();

  if (isNullConstant(LHS))
    std::swap(LHS, RHS);

  if (!isNullConstant(RHS))
    return SDValue();

  if (LHS.getOpcode() == ISD::SHL || LHS.getOpcode() == ISD::SRA ||
      LHS.getOpcode() == ISD::SRL)
    return SDValue();

  // Fold the compare into the branch instruction.
  SDValue BR;
  if (CC == AArch64CC::EQ)
    BR = DAG.getNode(AArch64ISD::CBZ, SDLoc(N), MVT::Other, Chain, LHS, Dest);
  else
    BR = DAG.getNode(AArch64ISD::CBNZ, SDLoc(N), MVT::Other, Chain, LHS, Dest);

  // Do not add new nodes to DAG combiner worklist.
  DCI.CombineTo(N, BR, false);

  return SDValue();
}

// Optimize some simple tbz/tbnz cases.  Returns the new operand and bit to test
// as well as whether the test should be inverted.  This code is required to
// catch these cases (as opposed to standard dag combines) because
// AArch64ISD::TBZ is matched during legalization.
static SDValue getTestBitOperand(SDValue Op, unsigned &Bit, bool &Invert,
                                 SelectionDAG &DAG) {

  if (!Op->hasOneUse())
    return Op;

  // We don't handle undef/constant-fold cases below, as they should have
  // already been taken care of (e.g. and of 0, test of undefined shifted bits,
  // etc.)

  // (tbz (trunc x), b) -> (tbz x, b)
  // This case is just here to enable more of the below cases to be caught.
  if (Op->getOpcode() == ISD::TRUNCATE &&
      Bit < Op->getValueType(0).getSizeInBits()) {
    return getTestBitOperand(Op->getOperand(0), Bit, Invert, DAG);
  }

  if (Op->getNumOperands() != 2)
    return Op;

  auto *C = dyn_cast<ConstantSDNode>(Op->getOperand(1));
  if (!C)
    return Op;

  switch (Op->getOpcode()) {
  default:
    return Op;

  // (tbz (and x, m), b) -> (tbz x, b)
  case ISD::AND:
    if ((C->getZExtValue() >> Bit) & 1)
      return getTestBitOperand(Op->getOperand(0), Bit, Invert, DAG);
    return Op;

  // (tbz (shl x, c), b) -> (tbz x, b-c)
  case ISD::SHL:
    if (C->getZExtValue() <= Bit &&
        (Bit - C->getZExtValue()) < Op->getValueType(0).getSizeInBits()) {
      Bit = Bit - C->getZExtValue();
      return getTestBitOperand(Op->getOperand(0), Bit, Invert, DAG);
    }
    return Op;

  // (tbz (sra x, c), b) -> (tbz x, b+c) or (tbz x, msb) if b+c is > # bits in x
  case ISD::SRA:
    Bit = Bit + C->getZExtValue();
    if (Bit >= Op->getValueType(0).getSizeInBits())
      Bit = Op->getValueType(0).getSizeInBits() - 1;
    return getTestBitOperand(Op->getOperand(0), Bit, Invert, DAG);

  // (tbz (srl x, c), b) -> (tbz x, b+c)
  case ISD::SRL:
    if ((Bit + C->getZExtValue()) < Op->getValueType(0).getSizeInBits()) {
      Bit = Bit + C->getZExtValue();
      return getTestBitOperand(Op->getOperand(0), Bit, Invert, DAG);
    }
    return Op;

  // (tbz (xor x, -1), b) -> (tbnz x, b)
  case ISD::XOR:
    if ((C->getZExtValue() >> Bit) & 1)
      Invert = !Invert;
    return getTestBitOperand(Op->getOperand(0), Bit, Invert, DAG);
  }
}

// Optimize test single bit zero/non-zero and branch.
static SDValue performTBZCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 SelectionDAG &DAG) {
  unsigned Bit = cast<ConstantSDNode>(N->getOperand(2))->getZExtValue();
  bool Invert = false;
  SDValue TestSrc = N->getOperand(1);
  SDValue NewTestSrc = getTestBitOperand(TestSrc, Bit, Invert, DAG);

  if (TestSrc == NewTestSrc)
    return SDValue();

  unsigned NewOpc = N->getOpcode();
  if (Invert) {
    if (NewOpc == AArch64ISD::TBZ)
      NewOpc = AArch64ISD::TBNZ;
    else {
      assert(NewOpc == AArch64ISD::TBNZ);
      NewOpc = AArch64ISD::TBZ;
    }
  }

  SDLoc DL(N);
  return DAG.getNode(NewOpc, DL, MVT::Other, N->getOperand(0), NewTestSrc,
                     DAG.getConstant(Bit, DL, MVT::i64), N->getOperand(3));
}

// vselect (v1i1 setcc) ->
//     vselect (v1iXX setcc)  (XX is the size of the compared operand type)
// FIXME: Currently the type legalizer can't handle VSELECT having v1i1 as
// condition. If it can legalize "VSELECT v1i1" correctly, no need to combine
// such VSELECT.
static SDValue performVSelectCombine(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  EVT CCVT = N0.getValueType();

  if (N0.getOpcode() != ISD::SETCC || CCVT.getVectorNumElements() != 1 ||
      CCVT.getVectorElementType() != MVT::i1)
    return SDValue();

  EVT ResVT = N->getValueType(0);
  EVT CmpVT = N0.getOperand(0).getValueType();
  // Only combine when the result type is of the same size as the compared
  // operands.
  if (ResVT.getSizeInBits() != CmpVT.getSizeInBits())
    return SDValue();

  SDValue IfTrue = N->getOperand(1);
  SDValue IfFalse = N->getOperand(2);
  SDValue SetCC =
      DAG.getSetCC(SDLoc(N), CmpVT.changeVectorElementTypeToInteger(),
                   N0.getOperand(0), N0.getOperand(1),
                   cast<CondCodeSDNode>(N0.getOperand(2))->get());
  return DAG.getNode(ISD::VSELECT, SDLoc(N), ResVT, SetCC,
                     IfTrue, IfFalse);
}

/// A vector select: "(select vL, vR, (setcc LHS, RHS))" is best performed with
/// the compare-mask instructions rather than going via NZCV, even if LHS and
/// RHS are really scalar. This replaces any scalar setcc in the above pattern
/// with a vector one followed by a DUP shuffle on the result.
static SDValue performSelectCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);
  EVT ResVT = N->getValueType(0);

  if (N0.getOpcode() != ISD::SETCC)
    return SDValue();

  // Make sure the SETCC result is either i1 (initial DAG), or i32, the lowered
  // scalar SetCCResultType. We also don't expect vectors, because we assume
  // that selects fed by vector SETCCs are canonicalized to VSELECT.
  assert((N0.getValueType() == MVT::i1 || N0.getValueType() == MVT::i32) &&
         "Scalar-SETCC feeding SELECT has unexpected result type!");

  // If NumMaskElts == 0, the comparison is larger than select result. The
  // largest real NEON comparison is 64-bits per lane, which means the result is
  // at most 32-bits and an illegal vector. Just bail out for now.
  EVT SrcVT = N0.getOperand(0).getValueType();

  // Don't try to do this optimization when the setcc itself has i1 operands.
  // There are no legal vectors of i1, so this would be pointless.
  if (SrcVT == MVT::i1)
    return SDValue();

  int NumMaskElts = ResVT.getSizeInBits() / SrcVT.getSizeInBits();
  if (!ResVT.isVector() || NumMaskElts == 0)
    return SDValue();

  SrcVT = EVT::getVectorVT(*DAG.getContext(), SrcVT, NumMaskElts);
  EVT CCVT = SrcVT.changeVectorElementTypeToInteger();

  // Also bail out if the vector CCVT isn't the same size as ResVT.
  // This can happen if the SETCC operand size doesn't divide the ResVT size
  // (e.g., f64 vs v3f32).
  if (CCVT.getSizeInBits() != ResVT.getSizeInBits())
    return SDValue();

  // Make sure we didn't create illegal types, if we're not supposed to.
  assert(DCI.isBeforeLegalize() ||
         DAG.getTargetLoweringInfo().isTypeLegal(SrcVT));

  // First perform a vector comparison, where lane 0 is the one we're interested
  // in.
  SDLoc DL(N0);
  SDValue LHS =
      DAG.getNode(ISD::SCALAR_TO_VECTOR, DL, SrcVT, N0.getOperand(0));
  SDValue RHS =
      DAG.getNode(ISD::SCALAR_TO_VECTOR, DL, SrcVT, N0.getOperand(1));
  SDValue SetCC = DAG.getNode(ISD::SETCC, DL, CCVT, LHS, RHS, N0.getOperand(2));

  // Now duplicate the comparison mask we want across all other lanes.
  SmallVector<int, 8> DUPMask(CCVT.getVectorNumElements(), 0);
  SDValue Mask = DAG.getVectorShuffle(CCVT, DL, SetCC, SetCC, DUPMask);
  Mask = DAG.getNode(ISD::BITCAST, DL,
                     ResVT.changeVectorElementTypeToInteger(), Mask);

  return DAG.getSelect(DL, ResVT, Mask, N->getOperand(1), N->getOperand(2));
}

/// Get rid of unnecessary NVCASTs (that don't change the type).
static SDValue performNVCASTCombine(SDNode *N) {
  if (N->getValueType(0) == N->getOperand(0).getValueType())
    return N->getOperand(0);

  return SDValue();
}

// If all users of the globaladdr are of the form (globaladdr + constant), find
// the smallest constant, fold it into the globaladdr's offset and rewrite the
// globaladdr as (globaladdr + constant) - constant.
static SDValue performGlobalAddressCombine(SDNode *N, SelectionDAG &DAG,
                                           const AArch64Subtarget *Subtarget,
                                           const TargetMachine &TM) {
  auto *GN = cast<GlobalAddressSDNode>(N);
  if (Subtarget->ClassifyGlobalReference(GN->getGlobal(), TM) !=
      AArch64II::MO_NO_FLAG)
    return SDValue();

  uint64_t MinOffset = -1ull;
  for (SDNode *N : GN->uses()) {
    if (N->getOpcode() != ISD::ADD)
      return SDValue();
    auto *C = dyn_cast<ConstantSDNode>(N->getOperand(0));
    if (!C)
      C = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!C)
      return SDValue();
    MinOffset = std::min(MinOffset, C->getZExtValue());
  }
  uint64_t Offset = MinOffset + GN->getOffset();

  // Require that the new offset is larger than the existing one. Otherwise, we
  // can end up oscillating between two possible DAGs, for example,
  // (add (add globaladdr + 10, -1), 1) and (add globaladdr + 9, 1).
  if (Offset <= uint64_t(GN->getOffset()))
    return SDValue();

  // Check whether folding this offset is legal. It must not go out of bounds of
  // the referenced object to avoid violating the code model, and must be
  // smaller than 2^21 because this is the largest offset expressible in all
  // object formats.
  //
  // This check also prevents us from folding negative offsets, which will end
  // up being treated in the same way as large positive ones. They could also
  // cause code model violations, and aren't really common enough to matter.
  if (Offset >= (1 << 21))
    return SDValue();

  const GlobalValue *GV = GN->getGlobal();
  Type *T = GV->getValueType();
  if (!T->isSized() ||
      Offset > GV->getParent()->getDataLayout().getTypeAllocSize(T))
    return SDValue();

  SDLoc DL(GN);
  SDValue Result = DAG.getGlobalAddress(GV, DL, MVT::i64, Offset);
  return DAG.getNode(ISD::SUB, DL, MVT::i64, Result,
                     DAG.getConstant(MinOffset, DL, MVT::i64));
}

SDValue AArch64TargetLowering::PerformDAGCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  switch (N->getOpcode()) {
  default:
    LLVM_DEBUG(dbgs() << "Custom combining: skipping\n");
    break;
  case ISD::ADD:
  case ISD::SUB:
    return performAddSubLongCombine(N, DCI, DAG);
  case ISD::XOR:
    return performXorCombine(N, DAG, DCI, Subtarget);
  case ISD::MUL:
    return performMulCombine(N, DAG, DCI, Subtarget);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    return performIntToFpCombine(N, DAG, Subtarget);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return performFpToIntCombine(N, DAG, DCI, Subtarget);
  case ISD::FDIV:
    return performFDivCombine(N, DAG, DCI, Subtarget);
  case ISD::OR:
    return performORCombine(N, DCI, Subtarget);
  case ISD::SRL:
    return performSRLCombine(N, DCI);
  case ISD::INTRINSIC_WO_CHAIN:
    return performIntrinsicCombine(N, DCI, Subtarget);
  case ISD::ANY_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND:
    return performExtendCombine(N, DCI, DAG);
  case ISD::BITCAST:
    return performBitcastCombine(N, DCI, DAG);
  case ISD::CONCAT_VECTORS:
    return performConcatVectorsCombine(N, DCI, DAG);
  case ISD::SELECT:
    return performSelectCombine(N, DCI);
  case ISD::VSELECT:
    return performVSelectCombine(N, DCI.DAG);
  case ISD::LOAD:
    if (performTBISimplification(N->getOperand(1), DCI, DAG))
      return SDValue(N, 0);
    break;
  case ISD::STORE:
    return performSTORECombine(N, DCI, DAG, Subtarget);
  case AArch64ISD::BRCOND:
    return performBRCONDCombine(N, DCI, DAG);
  case AArch64ISD::TBNZ:
  case AArch64ISD::TBZ:
    return performTBZCombine(N, DCI, DAG);
  case AArch64ISD::CSEL:
    return performCONDCombine(N, DCI, DAG, 2, 3);
  case AArch64ISD::DUP:
    return performPostLD1Combine(N, DCI, false);
  case AArch64ISD::NVCAST:
    return performNVCASTCombine(N);
  case ISD::INSERT_VECTOR_ELT:
    return performPostLD1Combine(N, DCI, true);
  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN:
    switch (cast<ConstantSDNode>(N->getOperand(1))->getZExtValue()) {
    case Intrinsic::aarch64_neon_ld2:
    case Intrinsic::aarch64_neon_ld3:
    case Intrinsic::aarch64_neon_ld4:
    case Intrinsic::aarch64_neon_ld1x2:
    case Intrinsic::aarch64_neon_ld1x3:
    case Intrinsic::aarch64_neon_ld1x4:
    case Intrinsic::aarch64_neon_ld2lane:
    case Intrinsic::aarch64_neon_ld3lane:
    case Intrinsic::aarch64_neon_ld4lane:
    case Intrinsic::aarch64_neon_ld2r:
    case Intrinsic::aarch64_neon_ld3r:
    case Intrinsic::aarch64_neon_ld4r:
    case Intrinsic::aarch64_neon_st2:
    case Intrinsic::aarch64_neon_st3:
    case Intrinsic::aarch64_neon_st4:
    case Intrinsic::aarch64_neon_st1x2:
    case Intrinsic::aarch64_neon_st1x3:
    case Intrinsic::aarch64_neon_st1x4:
    case Intrinsic::aarch64_neon_st2lane:
    case Intrinsic::aarch64_neon_st3lane:
    case Intrinsic::aarch64_neon_st4lane:
      return performNEONPostLDSTCombine(N, DCI, DAG);
    default:
      break;
    }
    break;
  case ISD::GlobalAddress:
    return performGlobalAddressCombine(N, DAG, Subtarget, getTargetMachine());
  }
  return SDValue();
}

// Check if the return value is used as only a return value, as otherwise
// we can't perform a tail-call. In particular, we need to check for
// target ISD nodes that are returns and any other "odd" constructs
// that the generic analysis code won't necessarily catch.
bool AArch64TargetLowering::isUsedByReturnOnly(SDNode *N,
                                               SDValue &Chain) const {
  if (N->getNumValues() != 1)
    return false;
  if (!N->hasNUsesOfValue(1, 0))
    return false;

  SDValue TCChain = Chain;
  SDNode *Copy = *N->use_begin();
  if (Copy->getOpcode() == ISD::CopyToReg) {
    // If the copy has a glue operand, we conservatively assume it isn't safe to
    // perform a tail call.
    if (Copy->getOperand(Copy->getNumOperands() - 1).getValueType() ==
        MVT::Glue)
      return false;
    TCChain = Copy->getOperand(0);
  } else if (Copy->getOpcode() != ISD::FP_EXTEND)
    return false;

  bool HasRet = false;
  for (SDNode *Node : Copy->uses()) {
    if (Node->getOpcode() != AArch64ISD::RET_FLAG)
      return false;
    HasRet = true;
  }

  if (!HasRet)
    return false;

  Chain = TCChain;
  return true;
}

// Return whether the an instruction can potentially be optimized to a tail
// call. This will cause the optimizers to attempt to move, or duplicate,
// return instructions to help enable tail call optimizations for this
// instruction.
bool AArch64TargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  return CI->isTailCall();
}

bool AArch64TargetLowering::getIndexedAddressParts(SDNode *Op, SDValue &Base,
                                                   SDValue &Offset,
                                                   ISD::MemIndexedMode &AM,
                                                   bool &IsInc,
                                                   SelectionDAG &DAG) const {
  if (Op->getOpcode() != ISD::ADD && Op->getOpcode() != ISD::SUB)
    return false;

  Base = Op->getOperand(0);
  // All of the indexed addressing mode instructions take a signed
  // 9 bit immediate offset.
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1))) {
    int64_t RHSC = RHS->getSExtValue();
    if (Op->getOpcode() == ISD::SUB)
      RHSC = -(uint64_t)RHSC;
    if (!isInt<9>(RHSC))
      return false;
    IsInc = (Op->getOpcode() == ISD::ADD);
    Offset = Op->getOperand(1);
    return true;
  }
  return false;
}

bool AArch64TargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                      SDValue &Offset,
                                                      ISD::MemIndexedMode &AM,
                                                      SelectionDAG &DAG) const {
  EVT VT;
  SDValue Ptr;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
  } else
    return false;

  bool IsInc;
  if (!getIndexedAddressParts(Ptr.getNode(), Base, Offset, AM, IsInc, DAG))
    return false;
  AM = IsInc ? ISD::PRE_INC : ISD::PRE_DEC;
  return true;
}

bool AArch64TargetLowering::getPostIndexedAddressParts(
    SDNode *N, SDNode *Op, SDValue &Base, SDValue &Offset,
    ISD::MemIndexedMode &AM, SelectionDAG &DAG) const {
  EVT VT;
  SDValue Ptr;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
  } else
    return false;

  bool IsInc;
  if (!getIndexedAddressParts(Op, Base, Offset, AM, IsInc, DAG))
    return false;
  // Post-indexing updates the base, so it's not a valid transform
  // if that's not the same as the load's pointer.
  if (Ptr != Base)
    return false;
  AM = IsInc ? ISD::POST_INC : ISD::POST_DEC;
  return true;
}

static void ReplaceBITCASTResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                                  SelectionDAG &DAG) {
  SDLoc DL(N);
  SDValue Op = N->getOperand(0);

  if (N->getValueType(0) != MVT::i16 || Op.getValueType() != MVT::f16)
    return;

  Op = SDValue(
      DAG.getMachineNode(TargetOpcode::INSERT_SUBREG, DL, MVT::f32,
                         DAG.getUNDEF(MVT::i32), Op,
                         DAG.getTargetConstant(AArch64::hsub, DL, MVT::i32)),
      0);
  Op = DAG.getNode(ISD::BITCAST, DL, MVT::i32, Op);
  Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Op));
}

static void ReplaceReductionResults(SDNode *N,
                                    SmallVectorImpl<SDValue> &Results,
                                    SelectionDAG &DAG, unsigned InterOp,
                                    unsigned AcrossOp) {
  EVT LoVT, HiVT;
  SDValue Lo, Hi;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  std::tie(Lo, Hi) = DAG.SplitVectorOperand(N, 0);
  SDValue InterVal = DAG.getNode(InterOp, dl, LoVT, Lo, Hi);
  SDValue SplitVal = DAG.getNode(AcrossOp, dl, LoVT, InterVal);
  Results.push_back(SplitVal);
}

static std::pair<SDValue, SDValue> splitInt128(SDValue N, SelectionDAG &DAG) {
  SDLoc DL(N);
  SDValue Lo = DAG.getNode(ISD::TRUNCATE, DL, MVT::i64, N);
  SDValue Hi = DAG.getNode(ISD::TRUNCATE, DL, MVT::i64,
                           DAG.getNode(ISD::SRL, DL, MVT::i128, N,
                                       DAG.getConstant(64, DL, MVT::i64)));
  return std::make_pair(Lo, Hi);
}

// Create an even/odd pair of X registers holding integer value V.
static SDValue createGPRPairNode(SelectionDAG &DAG, SDValue V) {
  SDLoc dl(V.getNode());
  SDValue VLo = DAG.getAnyExtOrTrunc(V, dl, MVT::i64);
  SDValue VHi = DAG.getAnyExtOrTrunc(
      DAG.getNode(ISD::SRL, dl, MVT::i128, V, DAG.getConstant(64, dl, MVT::i64)),
      dl, MVT::i64);
  if (DAG.getDataLayout().isBigEndian())
    std::swap (VLo, VHi);
  SDValue RegClass =
      DAG.getTargetConstant(AArch64::XSeqPairsClassRegClassID, dl, MVT::i32);
  SDValue SubReg0 = DAG.getTargetConstant(AArch64::sube64, dl, MVT::i32);
  SDValue SubReg1 = DAG.getTargetConstant(AArch64::subo64, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, VLo, SubReg0, VHi, SubReg1 };
  return SDValue(
      DAG.getMachineNode(TargetOpcode::REG_SEQUENCE, dl, MVT::Untyped, Ops), 0);
}

static void ReplaceCMP_SWAP_128Results(SDNode *N,
                                       SmallVectorImpl<SDValue> &Results,
                                       SelectionDAG &DAG,
                                       const AArch64Subtarget *Subtarget) {
  assert(N->getValueType(0) == MVT::i128 &&
         "AtomicCmpSwap on types less than 128 should be legal");

  if (Subtarget->hasLSE()) {
    // LSE has a 128-bit compare and swap (CASP), but i128 is not a legal type,
    // so lower it here, wrapped in REG_SEQUENCE and EXTRACT_SUBREG.
    SDValue Ops[] = {
        createGPRPairNode(DAG, N->getOperand(2)), // Compare value
        createGPRPairNode(DAG, N->getOperand(3)), // Store value
        N->getOperand(1), // Ptr
        N->getOperand(0), // Chain in
    };

    MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();

    unsigned Opcode;
    switch (MemOp->getOrdering()) {
    case AtomicOrdering::Monotonic:
      Opcode = AArch64::CASPX;
      break;
    case AtomicOrdering::Acquire:
      Opcode = AArch64::CASPAX;
      break;
    case AtomicOrdering::Release:
      Opcode = AArch64::CASPLX;
      break;
    case AtomicOrdering::AcquireRelease:
    case AtomicOrdering::SequentiallyConsistent:
      Opcode = AArch64::CASPALX;
      break;
    default:
      llvm_unreachable("Unexpected ordering!");
    }

    MachineSDNode *CmpSwap = DAG.getMachineNode(
        Opcode, SDLoc(N), DAG.getVTList(MVT::Untyped, MVT::Other), Ops);
    DAG.setNodeMemRefs(CmpSwap, {MemOp});

    unsigned SubReg1 = AArch64::sube64, SubReg2 = AArch64::subo64;
    if (DAG.getDataLayout().isBigEndian())
      std::swap(SubReg1, SubReg2);
    Results.push_back(DAG.getTargetExtractSubreg(SubReg1, SDLoc(N), MVT::i64,
                                                 SDValue(CmpSwap, 0)));
    Results.push_back(DAG.getTargetExtractSubreg(SubReg2, SDLoc(N), MVT::i64,
                                                 SDValue(CmpSwap, 0)));
    Results.push_back(SDValue(CmpSwap, 1)); // Chain out
    return;
  }

  auto Desired = splitInt128(N->getOperand(2), DAG);
  auto New = splitInt128(N->getOperand(3), DAG);
  SDValue Ops[] = {N->getOperand(1), Desired.first, Desired.second,
                   New.first,        New.second,    N->getOperand(0)};
  SDNode *CmpSwap = DAG.getMachineNode(
      AArch64::CMP_SWAP_128, SDLoc(N),
      DAG.getVTList(MVT::i64, MVT::i64, MVT::i32, MVT::Other), Ops);

  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  DAG.setNodeMemRefs(cast<MachineSDNode>(CmpSwap), {MemOp});

  Results.push_back(SDValue(CmpSwap, 0));
  Results.push_back(SDValue(CmpSwap, 1));
  Results.push_back(SDValue(CmpSwap, 3));
}

void AArch64TargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Don't know how to custom expand this");
  case ISD::BITCAST:
    ReplaceBITCASTResults(N, Results, DAG);
    return;
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
    Results.push_back(LowerVECREDUCE(SDValue(N, 0), DAG));
    return;

  case AArch64ISD::SADDV:
    ReplaceReductionResults(N, Results, DAG, ISD::ADD, AArch64ISD::SADDV);
    return;
  case AArch64ISD::UADDV:
    ReplaceReductionResults(N, Results, DAG, ISD::ADD, AArch64ISD::UADDV);
    return;
  case AArch64ISD::SMINV:
    ReplaceReductionResults(N, Results, DAG, ISD::SMIN, AArch64ISD::SMINV);
    return;
  case AArch64ISD::UMINV:
    ReplaceReductionResults(N, Results, DAG, ISD::UMIN, AArch64ISD::UMINV);
    return;
  case AArch64ISD::SMAXV:
    ReplaceReductionResults(N, Results, DAG, ISD::SMAX, AArch64ISD::SMAXV);
    return;
  case AArch64ISD::UMAXV:
    ReplaceReductionResults(N, Results, DAG, ISD::UMAX, AArch64ISD::UMAXV);
    return;
  case ISD::FP_TO_UINT:
  case ISD::FP_TO_SINT:
    assert(N->getValueType(0) == MVT::i128 && "unexpected illegal conversion");
    // Let normal code take care of it by not adding anything to Results.
    return;
  case ISD::ATOMIC_CMP_SWAP:
    ReplaceCMP_SWAP_128Results(N, Results, DAG, Subtarget);
    return;
  }
}

bool AArch64TargetLowering::useLoadStackGuardNode() const {
  if (Subtarget->isTargetAndroid() || Subtarget->isTargetFuchsia())
    return TargetLowering::useLoadStackGuardNode();
  return true;
}

unsigned AArch64TargetLowering::combineRepeatedFPDivisors() const {
  // Combine multiple FDIVs with the same divisor into multiple FMULs by the
  // reciprocal if there are three or more FDIVs.
  return 3;
}

TargetLoweringBase::LegalizeTypeAction
AArch64TargetLowering::getPreferredVectorAction(MVT VT) const {
  // During type legalization, we prefer to widen v1i8, v1i16, v1i32  to v8i8,
  // v4i16, v2i32 instead of to promote.
  if (VT == MVT::v1i8 || VT == MVT::v1i16 || VT == MVT::v1i32 ||
      VT == MVT::v1f32)
    return TypeWidenVector;

  return TargetLoweringBase::getPreferredVectorAction(VT);
}

// Loads and stores less than 128-bits are already atomic; ones above that
// are doomed anyway, so defer to the default libcall and blame the OS when
// things go wrong.
bool AArch64TargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  unsigned Size = SI->getValueOperand()->getType()->getPrimitiveSizeInBits();
  return Size == 128;
}

// Loads and stores less than 128-bits are already atomic; ones above that
// are doomed anyway, so defer to the default libcall and blame the OS when
// things go wrong.
TargetLowering::AtomicExpansionKind
AArch64TargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  unsigned Size = LI->getType()->getPrimitiveSizeInBits();
  return Size == 128 ? AtomicExpansionKind::LLSC : AtomicExpansionKind::None;
}

// For the real atomic operations, we have ldxr/stxr up to 128 bits,
TargetLowering::AtomicExpansionKind
AArch64TargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  unsigned Size = AI->getType()->getPrimitiveSizeInBits();
  if (Size > 128) return AtomicExpansionKind::None;
  // Nand not supported in LSE.
  if (AI->getOperation() == AtomicRMWInst::Nand) return AtomicExpansionKind::LLSC;
  // Leave 128 bits to LLSC.
  return (Subtarget->hasLSE() && Size < 128) ? AtomicExpansionKind::None : AtomicExpansionKind::LLSC;
}

TargetLowering::AtomicExpansionKind
AArch64TargetLowering::shouldExpandAtomicCmpXchgInIR(
    AtomicCmpXchgInst *AI) const {
  // If subtarget has LSE, leave cmpxchg intact for codegen.
  if (Subtarget->hasLSE())
    return AtomicExpansionKind::None;
  // At -O0, fast-regalloc cannot cope with the live vregs necessary to
  // implement cmpxchg without spilling. If the address being exchanged is also
  // on the stack and close enough to the spill slot, this can lead to a
  // situation where the monitor always gets cleared and the atomic operation
  // can never succeed. So at -O0 we need a late-expanded pseudo-inst instead.
  if (getTargetMachine().getOptLevel() == 0)
    return AtomicExpansionKind::None;
  return AtomicExpansionKind::LLSC;
}

Value *AArch64TargetLowering::emitLoadLinked(IRBuilder<> &Builder, Value *Addr,
                                             AtomicOrdering Ord) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Type *ValTy = cast<PointerType>(Addr->getType())->getElementType();
  bool IsAcquire = isAcquireOrStronger(Ord);

  // Since i128 isn't legal and intrinsics don't get type-lowered, the ldrexd
  // intrinsic must return {i64, i64} and we have to recombine them into a
  // single i128 here.
  if (ValTy->getPrimitiveSizeInBits() == 128) {
    Intrinsic::ID Int =
        IsAcquire ? Intrinsic::aarch64_ldaxp : Intrinsic::aarch64_ldxp;
    Function *Ldxr = Intrinsic::getDeclaration(M, Int);

    Addr = Builder.CreateBitCast(Addr, Type::getInt8PtrTy(M->getContext()));
    Value *LoHi = Builder.CreateCall(Ldxr, Addr, "lohi");

    Value *Lo = Builder.CreateExtractValue(LoHi, 0, "lo");
    Value *Hi = Builder.CreateExtractValue(LoHi, 1, "hi");
    Lo = Builder.CreateZExt(Lo, ValTy, "lo64");
    Hi = Builder.CreateZExt(Hi, ValTy, "hi64");
    return Builder.CreateOr(
        Lo, Builder.CreateShl(Hi, ConstantInt::get(ValTy, 64)), "val64");
  }

  Type *Tys[] = { Addr->getType() };
  Intrinsic::ID Int =
      IsAcquire ? Intrinsic::aarch64_ldaxr : Intrinsic::aarch64_ldxr;
  Function *Ldxr = Intrinsic::getDeclaration(M, Int, Tys);

  return Builder.CreateTruncOrBitCast(
      Builder.CreateCall(Ldxr, Addr),
      cast<PointerType>(Addr->getType())->getElementType());
}

void AArch64TargetLowering::emitAtomicCmpXchgNoStoreLLBalance(
    IRBuilder<> &Builder) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Builder.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::aarch64_clrex));
}

Value *AArch64TargetLowering::emitStoreConditional(IRBuilder<> &Builder,
                                                   Value *Val, Value *Addr,
                                                   AtomicOrdering Ord) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  bool IsRelease = isReleaseOrStronger(Ord);

  // Since the intrinsics must have legal type, the i128 intrinsics take two
  // parameters: "i64, i64". We must marshal Val into the appropriate form
  // before the call.
  if (Val->getType()->getPrimitiveSizeInBits() == 128) {
    Intrinsic::ID Int =
        IsRelease ? Intrinsic::aarch64_stlxp : Intrinsic::aarch64_stxp;
    Function *Stxr = Intrinsic::getDeclaration(M, Int);
    Type *Int64Ty = Type::getInt64Ty(M->getContext());

    Value *Lo = Builder.CreateTrunc(Val, Int64Ty, "lo");
    Value *Hi = Builder.CreateTrunc(Builder.CreateLShr(Val, 64), Int64Ty, "hi");
    Addr = Builder.CreateBitCast(Addr, Type::getInt8PtrTy(M->getContext()));
    return Builder.CreateCall(Stxr, {Lo, Hi, Addr});
  }

  Intrinsic::ID Int =
      IsRelease ? Intrinsic::aarch64_stlxr : Intrinsic::aarch64_stxr;
  Type *Tys[] = { Addr->getType() };
  Function *Stxr = Intrinsic::getDeclaration(M, Int, Tys);

  return Builder.CreateCall(Stxr,
                            {Builder.CreateZExtOrBitCast(
                                 Val, Stxr->getFunctionType()->getParamType(0)),
                             Addr});
}

bool AArch64TargetLowering::functionArgumentNeedsConsecutiveRegisters(
    Type *Ty, CallingConv::ID CallConv, bool isVarArg) const {
  return Ty->isArrayTy();
}

bool AArch64TargetLowering::shouldNormalizeToSelectSequence(LLVMContext &,
                                                            EVT) const {
  return false;
}

static Value *UseTlsOffset(IRBuilder<> &IRB, unsigned Offset) {
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  Function *ThreadPointerFunc =
      Intrinsic::getDeclaration(M, Intrinsic::thread_pointer);
  return IRB.CreatePointerCast(
      IRB.CreateConstGEP1_32(IRB.CreateCall(ThreadPointerFunc), Offset),
      Type::getInt8PtrTy(IRB.getContext())->getPointerTo(0));
}

Value *AArch64TargetLowering::getIRStackGuard(IRBuilder<> &IRB) const {
  // Android provides a fixed TLS slot for the stack cookie. See the definition
  // of TLS_SLOT_STACK_GUARD in
  // https://android.googlesource.com/platform/bionic/+/master/libc/private/bionic_tls.h
  if (Subtarget->isTargetAndroid())
    return UseTlsOffset(IRB, 0x28);

  // Fuchsia is similar.
  // <zircon/tls.h> defines ZX_TLS_STACK_GUARD_OFFSET with this value.
  if (Subtarget->isTargetFuchsia())
    return UseTlsOffset(IRB, -0x10);

  return TargetLowering::getIRStackGuard(IRB);
}

void AArch64TargetLowering::insertSSPDeclarations(Module &M) const {
  // MSVC CRT provides functionalities for stack protection.
  if (Subtarget->getTargetTriple().isWindowsMSVCEnvironment()) {
    // MSVC CRT has a global variable holding security cookie.
    M.getOrInsertGlobal("__security_cookie",
                        Type::getInt8PtrTy(M.getContext()));

    // MSVC CRT has a function to validate security cookie.
    auto *SecurityCheckCookie = cast<Function>(
        M.getOrInsertFunction("__security_check_cookie",
                              Type::getVoidTy(M.getContext()),
                              Type::getInt8PtrTy(M.getContext())));
    SecurityCheckCookie->setCallingConv(CallingConv::Win64);
    SecurityCheckCookie->addAttribute(1, Attribute::AttrKind::InReg);
    return;
  }
  TargetLowering::insertSSPDeclarations(M);
}

Value *AArch64TargetLowering::getSDagStackGuard(const Module &M) const {
  // MSVC CRT has a global variable holding security cookie.
  if (Subtarget->getTargetTriple().isWindowsMSVCEnvironment())
    return M.getGlobalVariable("__security_cookie");
  return TargetLowering::getSDagStackGuard(M);
}

Value *AArch64TargetLowering::getSSPStackGuardCheck(const Module &M) const {
  // MSVC CRT has a function to validate security cookie.
  if (Subtarget->getTargetTriple().isWindowsMSVCEnvironment())
    return M.getFunction("__security_check_cookie");
  return TargetLowering::getSSPStackGuardCheck(M);
}

Value *AArch64TargetLowering::getSafeStackPointerLocation(IRBuilder<> &IRB) const {
  // Android provides a fixed TLS slot for the SafeStack pointer. See the
  // definition of TLS_SLOT_SAFESTACK in
  // https://android.googlesource.com/platform/bionic/+/master/libc/private/bionic_tls.h
  if (Subtarget->isTargetAndroid())
    return UseTlsOffset(IRB, 0x48);

  // Fuchsia is similar.
  // <zircon/tls.h> defines ZX_TLS_UNSAFE_SP_OFFSET with this value.
  if (Subtarget->isTargetFuchsia())
    return UseTlsOffset(IRB, -0x8);

  return TargetLowering::getSafeStackPointerLocation(IRB);
}

bool AArch64TargetLowering::isMaskAndCmp0FoldingBeneficial(
    const Instruction &AndI) const {
  // Only sink 'and' mask to cmp use block if it is masking a single bit, since
  // this is likely to be fold the and/cmp/br into a single tbz instruction.  It
  // may be beneficial to sink in other cases, but we would have to check that
  // the cmp would not get folded into the br to form a cbz for these to be
  // beneficial.
  ConstantInt* Mask = dyn_cast<ConstantInt>(AndI.getOperand(1));
  if (!Mask)
    return false;
  return Mask->getValue().isPowerOf2();
}

void AArch64TargetLowering::initializeSplitCSR(MachineBasicBlock *Entry) const {
  // Update IsSplitCSR in AArch64unctionInfo.
  AArch64FunctionInfo *AFI = Entry->getParent()->getInfo<AArch64FunctionInfo>();
  AFI->setIsSplitCSR(true);
}

void AArch64TargetLowering::insertCopiesSplitCSR(
    MachineBasicBlock *Entry,
    const SmallVectorImpl<MachineBasicBlock *> &Exits) const {
  const AArch64RegisterInfo *TRI = Subtarget->getRegisterInfo();
  const MCPhysReg *IStart = TRI->getCalleeSavedRegsViaCopy(Entry->getParent());
  if (!IStart)
    return;

  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  MachineRegisterInfo *MRI = &Entry->getParent()->getRegInfo();
  MachineBasicBlock::iterator MBBI = Entry->begin();
  for (const MCPhysReg *I = IStart; *I; ++I) {
    const TargetRegisterClass *RC = nullptr;
    if (AArch64::GPR64RegClass.contains(*I))
      RC = &AArch64::GPR64RegClass;
    else if (AArch64::FPR64RegClass.contains(*I))
      RC = &AArch64::FPR64RegClass;
    else
      llvm_unreachable("Unexpected register class in CSRsViaCopy!");

    unsigned NewVR = MRI->createVirtualRegister(RC);
    // Create copy from CSR to a virtual register.
    // FIXME: this currently does not emit CFI pseudo-instructions, it works
    // fine for CXX_FAST_TLS since the C++-style TLS access functions should be
    // nounwind. If we want to generalize this later, we may need to emit
    // CFI pseudo-instructions.
    assert(Entry->getParent()->getFunction().hasFnAttribute(
               Attribute::NoUnwind) &&
           "Function should be nounwind in insertCopiesSplitCSR!");
    Entry->addLiveIn(*I);
    BuildMI(*Entry, MBBI, DebugLoc(), TII->get(TargetOpcode::COPY), NewVR)
        .addReg(*I);

    // Insert the copy-back instructions right before the terminator.
    for (auto *Exit : Exits)
      BuildMI(*Exit, Exit->getFirstTerminator(), DebugLoc(),
              TII->get(TargetOpcode::COPY), *I)
          .addReg(NewVR);
  }
}

bool AArch64TargetLowering::isIntDivCheap(EVT VT, AttributeList Attr) const {
  // Integer division on AArch64 is expensive. However, when aggressively
  // optimizing for code size, we prefer to use a div instruction, as it is
  // usually smaller than the alternative sequence.
  // The exception to this is vector division. Since AArch64 doesn't have vector
  // integer division, leaving the division as-is is a loss even in terms of
  // size, because it will have to be scalarized, while the alternative code
  // sequence can be performed in vector form.
  bool OptSize =
      Attr.hasAttribute(AttributeList::FunctionIndex, Attribute::MinSize);
  return OptSize && !VT.isVector();
}

bool AArch64TargetLowering::enableAggressiveFMAFusion(EVT VT) const {
  return Subtarget->hasAggressiveFMA() && VT.isFloatingPoint();
}

unsigned
AArch64TargetLowering::getVaListSizeInBits(const DataLayout &DL) const {
  if (Subtarget->isTargetDarwin() || Subtarget->isTargetWindows())
    return getPointerTy(DL).getSizeInBits();

  return 3 * getPointerTy(DL).getSizeInBits() + 2 * 32;
}

void AArch64TargetLowering::finalizeLowering(MachineFunction &MF) const {
  MF.getFrameInfo().computeMaxCallFrameSize(MF);
  TargetLoweringBase::finalizeLowering(MF);
}

// Unlike X86, we let frame lowering assign offsets to all catch objects.
bool AArch64TargetLowering::needsFixedCatchObjects() const {
  return false;
}
