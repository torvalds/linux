//===- ARMISelLowering.cpp - ARM DAG Lowering Implementation --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that ARM uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "ARMISelLowering.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMCallingConv.h"
#include "ARMConstantPoolValue.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMPerfectShuffle.h"
#include "ARMRegisterInfo.h"
#include "ARMSelectionDAGInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
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
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "arm-isel"

STATISTIC(NumTailCalls, "Number of tail calls");
STATISTIC(NumMovwMovt, "Number of GAs materialized with movw + movt");
STATISTIC(NumLoopByVals, "Number of loops generated for byval arguments");
STATISTIC(NumConstpoolPromoted,
  "Number of constants with their storage promoted into constant pools");

static cl::opt<bool>
ARMInterworking("arm-interworking", cl::Hidden,
  cl::desc("Enable / disable ARM interworking (for debugging only)"),
  cl::init(true));

static cl::opt<bool> EnableConstpoolPromotion(
    "arm-promote-constant", cl::Hidden,
    cl::desc("Enable / disable promotion of unnamed_addr constants into "
             "constant pools"),
    cl::init(false)); // FIXME: set to true by default once PR32780 is fixed
static cl::opt<unsigned> ConstpoolPromotionMaxSize(
    "arm-promote-constant-max-size", cl::Hidden,
    cl::desc("Maximum size of constant to promote into a constant pool"),
    cl::init(64));
static cl::opt<unsigned> ConstpoolPromotionMaxTotal(
    "arm-promote-constant-max-total", cl::Hidden,
    cl::desc("Maximum size of ALL constants to promote into a constant pool"),
    cl::init(128));

// The APCS parameter registers.
static const MCPhysReg GPRArgRegs[] = {
  ARM::R0, ARM::R1, ARM::R2, ARM::R3
};

void ARMTargetLowering::addTypeForNEON(MVT VT, MVT PromotedLdStVT,
                                       MVT PromotedBitwiseVT) {
  if (VT != PromotedLdStVT) {
    setOperationAction(ISD::LOAD, VT, Promote);
    AddPromotedToType (ISD::LOAD, VT, PromotedLdStVT);

    setOperationAction(ISD::STORE, VT, Promote);
    AddPromotedToType (ISD::STORE, VT, PromotedLdStVT);
  }

  MVT ElemTy = VT.getVectorElementType();
  if (ElemTy != MVT::f64)
    setOperationAction(ISD::SETCC, VT, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Custom);
  if (ElemTy == MVT::i32) {
    setOperationAction(ISD::SINT_TO_FP, VT, Custom);
    setOperationAction(ISD::UINT_TO_FP, VT, Custom);
    setOperationAction(ISD::FP_TO_SINT, VT, Custom);
    setOperationAction(ISD::FP_TO_UINT, VT, Custom);
  } else {
    setOperationAction(ISD::SINT_TO_FP, VT, Expand);
    setOperationAction(ISD::UINT_TO_FP, VT, Expand);
    setOperationAction(ISD::FP_TO_SINT, VT, Expand);
    setOperationAction(ISD::FP_TO_UINT, VT, Expand);
  }
  setOperationAction(ISD::BUILD_VECTOR,      VT, Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE,    VT, Custom);
  setOperationAction(ISD::CONCAT_VECTORS,    VT, Legal);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, VT, Legal);
  setOperationAction(ISD::SELECT,            VT, Expand);
  setOperationAction(ISD::SELECT_CC,         VT, Expand);
  setOperationAction(ISD::VSELECT,           VT, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);
  if (VT.isInteger()) {
    setOperationAction(ISD::SHL, VT, Custom);
    setOperationAction(ISD::SRA, VT, Custom);
    setOperationAction(ISD::SRL, VT, Custom);
  }

  // Promote all bit-wise operations.
  if (VT.isInteger() && VT != PromotedBitwiseVT) {
    setOperationAction(ISD::AND, VT, Promote);
    AddPromotedToType (ISD::AND, VT, PromotedBitwiseVT);
    setOperationAction(ISD::OR,  VT, Promote);
    AddPromotedToType (ISD::OR,  VT, PromotedBitwiseVT);
    setOperationAction(ISD::XOR, VT, Promote);
    AddPromotedToType (ISD::XOR, VT, PromotedBitwiseVT);
  }

  // Neon does not support vector divide/remainder operations.
  setOperationAction(ISD::SDIV, VT, Expand);
  setOperationAction(ISD::UDIV, VT, Expand);
  setOperationAction(ISD::FDIV, VT, Expand);
  setOperationAction(ISD::SREM, VT, Expand);
  setOperationAction(ISD::UREM, VT, Expand);
  setOperationAction(ISD::FREM, VT, Expand);

  if (!VT.isFloatingPoint() &&
      VT != MVT::v2i64 && VT != MVT::v1i64)
    for (auto Opcode : {ISD::ABS, ISD::SMIN, ISD::SMAX, ISD::UMIN, ISD::UMAX})
      setOperationAction(Opcode, VT, Legal);
}

void ARMTargetLowering::addDRTypeForNEON(MVT VT) {
  addRegisterClass(VT, &ARM::DPRRegClass);
  addTypeForNEON(VT, MVT::f64, MVT::v2i32);
}

void ARMTargetLowering::addQRTypeForNEON(MVT VT) {
  addRegisterClass(VT, &ARM::DPairRegClass);
  addTypeForNEON(VT, MVT::v2f64, MVT::v4i32);
}

ARMTargetLowering::ARMTargetLowering(const TargetMachine &TM,
                                     const ARMSubtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {
  RegInfo = Subtarget->getRegisterInfo();
  Itins = Subtarget->getInstrItineraryData();

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  if (!Subtarget->isTargetDarwin() && !Subtarget->isTargetIOS() &&
      !Subtarget->isTargetWatchOS()) {
    bool IsHFTarget = TM.Options.FloatABIType == FloatABI::Hard;
    for (int LCID = 0; LCID < RTLIB::UNKNOWN_LIBCALL; ++LCID)
      setLibcallCallingConv(static_cast<RTLIB::Libcall>(LCID),
                            IsHFTarget ? CallingConv::ARM_AAPCS_VFP
                                       : CallingConv::ARM_AAPCS);
  }

  if (Subtarget->isTargetMachO()) {
    // Uses VFP for Thumb libfuncs if available.
    if (Subtarget->isThumb() && Subtarget->hasVFP2() &&
        Subtarget->hasARMOps() && !Subtarget->useSoftFloat()) {
      static const struct {
        const RTLIB::Libcall Op;
        const char * const Name;
        const ISD::CondCode Cond;
      } LibraryCalls[] = {
        // Single-precision floating-point arithmetic.
        { RTLIB::ADD_F32, "__addsf3vfp", ISD::SETCC_INVALID },
        { RTLIB::SUB_F32, "__subsf3vfp", ISD::SETCC_INVALID },
        { RTLIB::MUL_F32, "__mulsf3vfp", ISD::SETCC_INVALID },
        { RTLIB::DIV_F32, "__divsf3vfp", ISD::SETCC_INVALID },

        // Double-precision floating-point arithmetic.
        { RTLIB::ADD_F64, "__adddf3vfp", ISD::SETCC_INVALID },
        { RTLIB::SUB_F64, "__subdf3vfp", ISD::SETCC_INVALID },
        { RTLIB::MUL_F64, "__muldf3vfp", ISD::SETCC_INVALID },
        { RTLIB::DIV_F64, "__divdf3vfp", ISD::SETCC_INVALID },

        // Single-precision comparisons.
        { RTLIB::OEQ_F32, "__eqsf2vfp",    ISD::SETNE },
        { RTLIB::UNE_F32, "__nesf2vfp",    ISD::SETNE },
        { RTLIB::OLT_F32, "__ltsf2vfp",    ISD::SETNE },
        { RTLIB::OLE_F32, "__lesf2vfp",    ISD::SETNE },
        { RTLIB::OGE_F32, "__gesf2vfp",    ISD::SETNE },
        { RTLIB::OGT_F32, "__gtsf2vfp",    ISD::SETNE },
        { RTLIB::UO_F32,  "__unordsf2vfp", ISD::SETNE },
        { RTLIB::O_F32,   "__unordsf2vfp", ISD::SETEQ },

        // Double-precision comparisons.
        { RTLIB::OEQ_F64, "__eqdf2vfp",    ISD::SETNE },
        { RTLIB::UNE_F64, "__nedf2vfp",    ISD::SETNE },
        { RTLIB::OLT_F64, "__ltdf2vfp",    ISD::SETNE },
        { RTLIB::OLE_F64, "__ledf2vfp",    ISD::SETNE },
        { RTLIB::OGE_F64, "__gedf2vfp",    ISD::SETNE },
        { RTLIB::OGT_F64, "__gtdf2vfp",    ISD::SETNE },
        { RTLIB::UO_F64,  "__unorddf2vfp", ISD::SETNE },
        { RTLIB::O_F64,   "__unorddf2vfp", ISD::SETEQ },

        // Floating-point to integer conversions.
        // i64 conversions are done via library routines even when generating VFP
        // instructions, so use the same ones.
        { RTLIB::FPTOSINT_F64_I32, "__fixdfsivfp",    ISD::SETCC_INVALID },
        { RTLIB::FPTOUINT_F64_I32, "__fixunsdfsivfp", ISD::SETCC_INVALID },
        { RTLIB::FPTOSINT_F32_I32, "__fixsfsivfp",    ISD::SETCC_INVALID },
        { RTLIB::FPTOUINT_F32_I32, "__fixunssfsivfp", ISD::SETCC_INVALID },

        // Conversions between floating types.
        { RTLIB::FPROUND_F64_F32, "__truncdfsf2vfp",  ISD::SETCC_INVALID },
        { RTLIB::FPEXT_F32_F64,   "__extendsfdf2vfp", ISD::SETCC_INVALID },

        // Integer to floating-point conversions.
        // i64 conversions are done via library routines even when generating VFP
        // instructions, so use the same ones.
        // FIXME: There appears to be some naming inconsistency in ARM libgcc:
        // e.g., __floatunsidf vs. __floatunssidfvfp.
        { RTLIB::SINTTOFP_I32_F64, "__floatsidfvfp",    ISD::SETCC_INVALID },
        { RTLIB::UINTTOFP_I32_F64, "__floatunssidfvfp", ISD::SETCC_INVALID },
        { RTLIB::SINTTOFP_I32_F32, "__floatsisfvfp",    ISD::SETCC_INVALID },
        { RTLIB::UINTTOFP_I32_F32, "__floatunssisfvfp", ISD::SETCC_INVALID },
      };

      for (const auto &LC : LibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        if (LC.Cond != ISD::SETCC_INVALID)
          setCmpLibcallCC(LC.Op, LC.Cond);
      }
    }
  }

  // These libcalls are not available in 32-bit.
  setLibcallName(RTLIB::SHL_I128, nullptr);
  setLibcallName(RTLIB::SRL_I128, nullptr);
  setLibcallName(RTLIB::SRA_I128, nullptr);

  // RTLIB
  if (Subtarget->isAAPCS_ABI() &&
      (Subtarget->isTargetAEABI() || Subtarget->isTargetGNUAEABI() ||
       Subtarget->isTargetMuslAEABI() || Subtarget->isTargetAndroid())) {
    static const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
      const CallingConv::ID CC;
      const ISD::CondCode Cond;
    } LibraryCalls[] = {
      // Double-precision floating-point arithmetic helper functions
      // RTABI chapter 4.1.2, Table 2
      { RTLIB::ADD_F64, "__aeabi_dadd", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::DIV_F64, "__aeabi_ddiv", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::MUL_F64, "__aeabi_dmul", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SUB_F64, "__aeabi_dsub", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Double-precision floating-point comparison helper functions
      // RTABI chapter 4.1.2, Table 3
      { RTLIB::OEQ_F64, "__aeabi_dcmpeq", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UNE_F64, "__aeabi_dcmpeq", CallingConv::ARM_AAPCS, ISD::SETEQ },
      { RTLIB::OLT_F64, "__aeabi_dcmplt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OLE_F64, "__aeabi_dcmple", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGE_F64, "__aeabi_dcmpge", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGT_F64, "__aeabi_dcmpgt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UO_F64,  "__aeabi_dcmpun", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::O_F64,   "__aeabi_dcmpun", CallingConv::ARM_AAPCS, ISD::SETEQ },

      // Single-precision floating-point arithmetic helper functions
      // RTABI chapter 4.1.2, Table 4
      { RTLIB::ADD_F32, "__aeabi_fadd", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::DIV_F32, "__aeabi_fdiv", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::MUL_F32, "__aeabi_fmul", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SUB_F32, "__aeabi_fsub", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Single-precision floating-point comparison helper functions
      // RTABI chapter 4.1.2, Table 5
      { RTLIB::OEQ_F32, "__aeabi_fcmpeq", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UNE_F32, "__aeabi_fcmpeq", CallingConv::ARM_AAPCS, ISD::SETEQ },
      { RTLIB::OLT_F32, "__aeabi_fcmplt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OLE_F32, "__aeabi_fcmple", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGE_F32, "__aeabi_fcmpge", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGT_F32, "__aeabi_fcmpgt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UO_F32,  "__aeabi_fcmpun", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::O_F32,   "__aeabi_fcmpun", CallingConv::ARM_AAPCS, ISD::SETEQ },

      // Floating-point to integer conversions.
      // RTABI chapter 4.1.2, Table 6
      { RTLIB::FPTOSINT_F64_I32, "__aeabi_d2iz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F64_I32, "__aeabi_d2uiz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOSINT_F64_I64, "__aeabi_d2lz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F64_I64, "__aeabi_d2ulz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOSINT_F32_I32, "__aeabi_f2iz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F32_I32, "__aeabi_f2uiz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOSINT_F32_I64, "__aeabi_f2lz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F32_I64, "__aeabi_f2ulz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Conversions between floating types.
      // RTABI chapter 4.1.2, Table 7
      { RTLIB::FPROUND_F64_F32, "__aeabi_d2f", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPROUND_F64_F16, "__aeabi_d2h", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPEXT_F32_F64,   "__aeabi_f2d", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Integer to floating-point conversions.
      // RTABI chapter 4.1.2, Table 8
      { RTLIB::SINTTOFP_I32_F64, "__aeabi_i2d",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I32_F64, "__aeabi_ui2d", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SINTTOFP_I64_F64, "__aeabi_l2d",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I64_F64, "__aeabi_ul2d", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SINTTOFP_I32_F32, "__aeabi_i2f",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I32_F32, "__aeabi_ui2f", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SINTTOFP_I64_F32, "__aeabi_l2f",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I64_F32, "__aeabi_ul2f", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Long long helper functions
      // RTABI chapter 4.2, Table 9
      { RTLIB::MUL_I64, "__aeabi_lmul", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SHL_I64, "__aeabi_llsl", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SRL_I64, "__aeabi_llsr", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SRA_I64, "__aeabi_lasr", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Integer division functions
      // RTABI chapter 4.3.1
      { RTLIB::SDIV_I8,  "__aeabi_idiv",     CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SDIV_I16, "__aeabi_idiv",     CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SDIV_I32, "__aeabi_idiv",     CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SDIV_I64, "__aeabi_ldivmod",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I8,  "__aeabi_uidiv",    CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I16, "__aeabi_uidiv",    CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I32, "__aeabi_uidiv",    CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I64, "__aeabi_uldivmod", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
    };

    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
      setLibcallCallingConv(LC.Op, LC.CC);
      if (LC.Cond != ISD::SETCC_INVALID)
        setCmpLibcallCC(LC.Op, LC.Cond);
    }

    // EABI dependent RTLIB
    if (TM.Options.EABIVersion == EABI::EABI4 ||
        TM.Options.EABIVersion == EABI::EABI5) {
      static const struct {
        const RTLIB::Libcall Op;
        const char *const Name;
        const CallingConv::ID CC;
        const ISD::CondCode Cond;
      } MemOpsLibraryCalls[] = {
        // Memory operations
        // RTABI chapter 4.3.4
        { RTLIB::MEMCPY,  "__aeabi_memcpy",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
        { RTLIB::MEMMOVE, "__aeabi_memmove", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
        { RTLIB::MEMSET,  "__aeabi_memset",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      };

      for (const auto &LC : MemOpsLibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        setLibcallCallingConv(LC.Op, LC.CC);
        if (LC.Cond != ISD::SETCC_INVALID)
          setCmpLibcallCC(LC.Op, LC.Cond);
      }
    }
  }

  if (Subtarget->isTargetWindows()) {
    static const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
      const CallingConv::ID CC;
    } LibraryCalls[] = {
      { RTLIB::FPTOSINT_F32_I64, "__stoi64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::FPTOSINT_F64_I64, "__dtoi64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::FPTOUINT_F32_I64, "__stou64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::FPTOUINT_F64_I64, "__dtou64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::SINTTOFP_I64_F32, "__i64tos", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::SINTTOFP_I64_F64, "__i64tod", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::UINTTOFP_I64_F32, "__u64tos", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::UINTTOFP_I64_F64, "__u64tod", CallingConv::ARM_AAPCS_VFP },
    };

    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
      setLibcallCallingConv(LC.Op, LC.CC);
    }
  }

  // Use divmod compiler-rt calls for iOS 5.0 and later.
  if (Subtarget->isTargetMachO() &&
      !(Subtarget->isTargetIOS() &&
        Subtarget->getTargetTriple().isOSVersionLT(5, 0))) {
    setLibcallName(RTLIB::SDIVREM_I32, "__divmodsi4");
    setLibcallName(RTLIB::UDIVREM_I32, "__udivmodsi4");
  }

  // The half <-> float conversion functions are always soft-float on
  // non-watchos platforms, but are needed for some targets which use a
  // hard-float calling convention by default.
  if (!Subtarget->isTargetWatchABI()) {
    if (Subtarget->isAAPCS_ABI()) {
      setLibcallCallingConv(RTLIB::FPROUND_F32_F16, CallingConv::ARM_AAPCS);
      setLibcallCallingConv(RTLIB::FPROUND_F64_F16, CallingConv::ARM_AAPCS);
      setLibcallCallingConv(RTLIB::FPEXT_F16_F32, CallingConv::ARM_AAPCS);
    } else {
      setLibcallCallingConv(RTLIB::FPROUND_F32_F16, CallingConv::ARM_APCS);
      setLibcallCallingConv(RTLIB::FPROUND_F64_F16, CallingConv::ARM_APCS);
      setLibcallCallingConv(RTLIB::FPEXT_F16_F32, CallingConv::ARM_APCS);
    }
  }

  // In EABI, these functions have an __aeabi_ prefix, but in GNUEABI they have
  // a __gnu_ prefix (which is the default).
  if (Subtarget->isTargetAEABI()) {
    static const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
      const CallingConv::ID CC;
    } LibraryCalls[] = {
      { RTLIB::FPROUND_F32_F16, "__aeabi_f2h", CallingConv::ARM_AAPCS },
      { RTLIB::FPROUND_F64_F16, "__aeabi_d2h", CallingConv::ARM_AAPCS },
      { RTLIB::FPEXT_F16_F32, "__aeabi_h2f", CallingConv::ARM_AAPCS },
    };

    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
      setLibcallCallingConv(LC.Op, LC.CC);
    }
  }

  if (Subtarget->isThumb1Only())
    addRegisterClass(MVT::i32, &ARM::tGPRRegClass);
  else
    addRegisterClass(MVT::i32, &ARM::GPRRegClass);

  if (!Subtarget->useSoftFloat() && Subtarget->hasVFP2() &&
      !Subtarget->isThumb1Only()) {
    addRegisterClass(MVT::f32, &ARM::SPRRegClass);
    addRegisterClass(MVT::f64, &ARM::DPRRegClass);
  }

  if (Subtarget->hasFullFP16()) {
    addRegisterClass(MVT::f16, &ARM::HPRRegClass);
    setOperationAction(ISD::BITCAST, MVT::i16, Custom);
    setOperationAction(ISD::BITCAST, MVT::i32, Custom);
    setOperationAction(ISD::BITCAST, MVT::f16, Custom);

    setOperationAction(ISD::FMINNUM, MVT::f16, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::f16, Legal);
  }

  for (MVT VT : MVT::vector_valuetypes()) {
    for (MVT InnerVT : MVT::vector_valuetypes()) {
      setTruncStoreAction(VT, InnerVT, Expand);
      setLoadExtAction(ISD::SEXTLOAD, VT, InnerVT, Expand);
      setLoadExtAction(ISD::ZEXTLOAD, VT, InnerVT, Expand);
      setLoadExtAction(ISD::EXTLOAD, VT, InnerVT, Expand);
    }

    setOperationAction(ISD::MULHS, VT, Expand);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::MULHU, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);

    setOperationAction(ISD::BSWAP, VT, Expand);
  }

  setOperationAction(ISD::ConstantFP, MVT::f32, Custom);
  setOperationAction(ISD::ConstantFP, MVT::f64, Custom);

  setOperationAction(ISD::READ_REGISTER, MVT::i64, Custom);
  setOperationAction(ISD::WRITE_REGISTER, MVT::i64, Custom);

  if (Subtarget->hasNEON()) {
    addDRTypeForNEON(MVT::v2f32);
    addDRTypeForNEON(MVT::v8i8);
    addDRTypeForNEON(MVT::v4i16);
    addDRTypeForNEON(MVT::v2i32);
    addDRTypeForNEON(MVT::v1i64);

    addQRTypeForNEON(MVT::v4f32);
    addQRTypeForNEON(MVT::v2f64);
    addQRTypeForNEON(MVT::v16i8);
    addQRTypeForNEON(MVT::v8i16);
    addQRTypeForNEON(MVT::v4i32);
    addQRTypeForNEON(MVT::v2i64);

    if (Subtarget->hasFullFP16()) {
      addQRTypeForNEON(MVT::v8f16);
      addDRTypeForNEON(MVT::v4f16);
    }

    // v2f64 is legal so that QR subregs can be extracted as f64 elements, but
    // neither Neon nor VFP support any arithmetic operations on it.
    // The same with v4f32. But keep in mind that vadd, vsub, vmul are natively
    // supported for v4f32.
    setOperationAction(ISD::FADD, MVT::v2f64, Expand);
    setOperationAction(ISD::FSUB, MVT::v2f64, Expand);
    setOperationAction(ISD::FMUL, MVT::v2f64, Expand);
    // FIXME: Code duplication: FDIV and FREM are expanded always, see
    // ARMTargetLowering::addTypeForNEON method for details.
    setOperationAction(ISD::FDIV, MVT::v2f64, Expand);
    setOperationAction(ISD::FREM, MVT::v2f64, Expand);
    // FIXME: Create unittest.
    // In another words, find a way when "copysign" appears in DAG with vector
    // operands.
    setOperationAction(ISD::FCOPYSIGN, MVT::v2f64, Expand);
    // FIXME: Code duplication: SETCC has custom operation action, see
    // ARMTargetLowering::addTypeForNEON method for details.
    setOperationAction(ISD::SETCC, MVT::v2f64, Expand);
    // FIXME: Create unittest for FNEG and for FABS.
    setOperationAction(ISD::FNEG, MVT::v2f64, Expand);
    setOperationAction(ISD::FABS, MVT::v2f64, Expand);
    setOperationAction(ISD::FSQRT, MVT::v2f64, Expand);
    setOperationAction(ISD::FSIN, MVT::v2f64, Expand);
    setOperationAction(ISD::FCOS, MVT::v2f64, Expand);
    setOperationAction(ISD::FPOW, MVT::v2f64, Expand);
    setOperationAction(ISD::FLOG, MVT::v2f64, Expand);
    setOperationAction(ISD::FLOG2, MVT::v2f64, Expand);
    setOperationAction(ISD::FLOG10, MVT::v2f64, Expand);
    setOperationAction(ISD::FEXP, MVT::v2f64, Expand);
    setOperationAction(ISD::FEXP2, MVT::v2f64, Expand);
    // FIXME: Create unittest for FCEIL, FTRUNC, FRINT, FNEARBYINT, FFLOOR.
    setOperationAction(ISD::FCEIL, MVT::v2f64, Expand);
    setOperationAction(ISD::FTRUNC, MVT::v2f64, Expand);
    setOperationAction(ISD::FRINT, MVT::v2f64, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v2f64, Expand);
    setOperationAction(ISD::FFLOOR, MVT::v2f64, Expand);
    setOperationAction(ISD::FMA, MVT::v2f64, Expand);

    setOperationAction(ISD::FSQRT, MVT::v4f32, Expand);
    setOperationAction(ISD::FSIN, MVT::v4f32, Expand);
    setOperationAction(ISD::FCOS, MVT::v4f32, Expand);
    setOperationAction(ISD::FPOW, MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG, MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG2, MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG10, MVT::v4f32, Expand);
    setOperationAction(ISD::FEXP, MVT::v4f32, Expand);
    setOperationAction(ISD::FEXP2, MVT::v4f32, Expand);
    setOperationAction(ISD::FCEIL, MVT::v4f32, Expand);
    setOperationAction(ISD::FTRUNC, MVT::v4f32, Expand);
    setOperationAction(ISD::FRINT, MVT::v4f32, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v4f32, Expand);
    setOperationAction(ISD::FFLOOR, MVT::v4f32, Expand);

    // Mark v2f32 intrinsics.
    setOperationAction(ISD::FSQRT, MVT::v2f32, Expand);
    setOperationAction(ISD::FSIN, MVT::v2f32, Expand);
    setOperationAction(ISD::FCOS, MVT::v2f32, Expand);
    setOperationAction(ISD::FPOW, MVT::v2f32, Expand);
    setOperationAction(ISD::FLOG, MVT::v2f32, Expand);
    setOperationAction(ISD::FLOG2, MVT::v2f32, Expand);
    setOperationAction(ISD::FLOG10, MVT::v2f32, Expand);
    setOperationAction(ISD::FEXP, MVT::v2f32, Expand);
    setOperationAction(ISD::FEXP2, MVT::v2f32, Expand);
    setOperationAction(ISD::FCEIL, MVT::v2f32, Expand);
    setOperationAction(ISD::FTRUNC, MVT::v2f32, Expand);
    setOperationAction(ISD::FRINT, MVT::v2f32, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v2f32, Expand);
    setOperationAction(ISD::FFLOOR, MVT::v2f32, Expand);

    // Neon does not support some operations on v1i64 and v2i64 types.
    setOperationAction(ISD::MUL, MVT::v1i64, Expand);
    // Custom handling for some quad-vector types to detect VMULL.
    setOperationAction(ISD::MUL, MVT::v8i16, Custom);
    setOperationAction(ISD::MUL, MVT::v4i32, Custom);
    setOperationAction(ISD::MUL, MVT::v2i64, Custom);
    // Custom handling for some vector types to avoid expensive expansions
    setOperationAction(ISD::SDIV, MVT::v4i16, Custom);
    setOperationAction(ISD::SDIV, MVT::v8i8, Custom);
    setOperationAction(ISD::UDIV, MVT::v4i16, Custom);
    setOperationAction(ISD::UDIV, MVT::v8i8, Custom);
    // Neon does not have single instruction SINT_TO_FP and UINT_TO_FP with
    // a destination type that is wider than the source, and nor does
    // it have a FP_TO_[SU]INT instruction with a narrower destination than
    // source.
    setOperationAction(ISD::SINT_TO_FP, MVT::v4i16, Custom);
    setOperationAction(ISD::SINT_TO_FP, MVT::v8i16, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v4i16, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v8i16, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::v4i16, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::v8i16, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::v4i16, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::v8i16, Custom);

    setOperationAction(ISD::FP_ROUND,   MVT::v2f32, Expand);
    setOperationAction(ISD::FP_EXTEND,  MVT::v2f64, Expand);

    // NEON does not have single instruction CTPOP for vectors with element
    // types wider than 8-bits.  However, custom lowering can leverage the
    // v8i8/v16i8 vcnt instruction.
    setOperationAction(ISD::CTPOP,      MVT::v2i32, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v4i32, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v4i16, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v8i16, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v1i64, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v2i64, Custom);

    setOperationAction(ISD::CTLZ,       MVT::v1i64, Expand);
    setOperationAction(ISD::CTLZ,       MVT::v2i64, Expand);

    // NEON does not have single instruction CTTZ for vectors.
    setOperationAction(ISD::CTTZ, MVT::v8i8, Custom);
    setOperationAction(ISD::CTTZ, MVT::v4i16, Custom);
    setOperationAction(ISD::CTTZ, MVT::v2i32, Custom);
    setOperationAction(ISD::CTTZ, MVT::v1i64, Custom);

    setOperationAction(ISD::CTTZ, MVT::v16i8, Custom);
    setOperationAction(ISD::CTTZ, MVT::v8i16, Custom);
    setOperationAction(ISD::CTTZ, MVT::v4i32, Custom);
    setOperationAction(ISD::CTTZ, MVT::v2i64, Custom);

    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v8i8, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v4i16, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v2i32, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v1i64, Custom);

    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v16i8, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v8i16, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v4i32, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v2i64, Custom);

    // NEON only has FMA instructions as of VFP4.
    if (!Subtarget->hasVFP4()) {
      setOperationAction(ISD::FMA, MVT::v2f32, Expand);
      setOperationAction(ISD::FMA, MVT::v4f32, Expand);
    }

    setTargetDAGCombine(ISD::INTRINSIC_VOID);
    setTargetDAGCombine(ISD::INTRINSIC_W_CHAIN);
    setTargetDAGCombine(ISD::INTRINSIC_WO_CHAIN);
    setTargetDAGCombine(ISD::SHL);
    setTargetDAGCombine(ISD::SRL);
    setTargetDAGCombine(ISD::SRA);
    setTargetDAGCombine(ISD::SIGN_EXTEND);
    setTargetDAGCombine(ISD::ZERO_EXTEND);
    setTargetDAGCombine(ISD::ANY_EXTEND);
    setTargetDAGCombine(ISD::BUILD_VECTOR);
    setTargetDAGCombine(ISD::VECTOR_SHUFFLE);
    setTargetDAGCombine(ISD::INSERT_VECTOR_ELT);
    setTargetDAGCombine(ISD::STORE);
    setTargetDAGCombine(ISD::FP_TO_SINT);
    setTargetDAGCombine(ISD::FP_TO_UINT);
    setTargetDAGCombine(ISD::FDIV);
    setTargetDAGCombine(ISD::LOAD);

    // It is legal to extload from v4i8 to v4i16 or v4i32.
    for (MVT Ty : {MVT::v8i8, MVT::v4i8, MVT::v2i8, MVT::v4i16, MVT::v2i16,
                   MVT::v2i32}) {
      for (MVT VT : MVT::integer_vector_valuetypes()) {
        setLoadExtAction(ISD::EXTLOAD, VT, Ty, Legal);
        setLoadExtAction(ISD::ZEXTLOAD, VT, Ty, Legal);
        setLoadExtAction(ISD::SEXTLOAD, VT, Ty, Legal);
      }
    }
  }

  if (Subtarget->isFPOnlySP()) {
    // When targeting a floating-point unit with only single-precision
    // operations, f64 is legal for the few double-precision instructions which
    // are present However, no double-precision operations other than moves,
    // loads and stores are provided by the hardware.
    setOperationAction(ISD::FADD,       MVT::f64, Expand);
    setOperationAction(ISD::FSUB,       MVT::f64, Expand);
    setOperationAction(ISD::FMUL,       MVT::f64, Expand);
    setOperationAction(ISD::FMA,        MVT::f64, Expand);
    setOperationAction(ISD::FDIV,       MVT::f64, Expand);
    setOperationAction(ISD::FREM,       MVT::f64, Expand);
    setOperationAction(ISD::FCOPYSIGN,  MVT::f64, Expand);
    setOperationAction(ISD::FGETSIGN,   MVT::f64, Expand);
    setOperationAction(ISD::FNEG,       MVT::f64, Expand);
    setOperationAction(ISD::FABS,       MVT::f64, Expand);
    setOperationAction(ISD::FSQRT,      MVT::f64, Expand);
    setOperationAction(ISD::FSIN,       MVT::f64, Expand);
    setOperationAction(ISD::FCOS,       MVT::f64, Expand);
    setOperationAction(ISD::FPOW,       MVT::f64, Expand);
    setOperationAction(ISD::FLOG,       MVT::f64, Expand);
    setOperationAction(ISD::FLOG2,      MVT::f64, Expand);
    setOperationAction(ISD::FLOG10,     MVT::f64, Expand);
    setOperationAction(ISD::FEXP,       MVT::f64, Expand);
    setOperationAction(ISD::FEXP2,      MVT::f64, Expand);
    setOperationAction(ISD::FCEIL,      MVT::f64, Expand);
    setOperationAction(ISD::FTRUNC,     MVT::f64, Expand);
    setOperationAction(ISD::FRINT,      MVT::f64, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::f64, Expand);
    setOperationAction(ISD::FFLOOR,     MVT::f64, Expand);
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::f64, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::f64, Custom);
    setOperationAction(ISD::FP_ROUND,   MVT::f32, Custom);
    setOperationAction(ISD::FP_EXTEND,  MVT::f64, Custom);
  }

  computeRegisterProperties(Subtarget->getRegisterInfo());

  // ARM does not have floating-point extending loads.
  for (MVT VT : MVT::fp_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f32, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f16, Expand);
  }

  // ... or truncating stores
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f16, Expand);

  // ARM does not have i1 sign extending load.
  for (MVT VT : MVT::integer_valuetypes())
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);

  // ARM supports all 4 flavors of integer indexed load / store.
  if (!Subtarget->isThumb1Only()) {
    for (unsigned im = (unsigned)ISD::PRE_INC;
         im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
      setIndexedLoadAction(im,  MVT::i1,  Legal);
      setIndexedLoadAction(im,  MVT::i8,  Legal);
      setIndexedLoadAction(im,  MVT::i16, Legal);
      setIndexedLoadAction(im,  MVT::i32, Legal);
      setIndexedStoreAction(im, MVT::i1,  Legal);
      setIndexedStoreAction(im, MVT::i8,  Legal);
      setIndexedStoreAction(im, MVT::i16, Legal);
      setIndexedStoreAction(im, MVT::i32, Legal);
    }
  } else {
    // Thumb-1 has limited post-inc load/store support - LDM r0!, {r1}.
    setIndexedLoadAction(ISD::POST_INC, MVT::i32,  Legal);
    setIndexedStoreAction(ISD::POST_INC, MVT::i32,  Legal);
  }

  setOperationAction(ISD::SADDO, MVT::i32, Custom);
  setOperationAction(ISD::UADDO, MVT::i32, Custom);
  setOperationAction(ISD::SSUBO, MVT::i32, Custom);
  setOperationAction(ISD::USUBO, MVT::i32, Custom);

  setOperationAction(ISD::ADDCARRY, MVT::i32, Custom);
  setOperationAction(ISD::SUBCARRY, MVT::i32, Custom);

  // i64 operation support.
  setOperationAction(ISD::MUL,     MVT::i64, Expand);
  setOperationAction(ISD::MULHU,   MVT::i32, Expand);
  if (Subtarget->isThumb1Only()) {
    setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
    setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  }
  if (Subtarget->isThumb1Only() || !Subtarget->hasV6Ops()
      || (Subtarget->isThumb2() && !Subtarget->hasDSP()))
    setOperationAction(ISD::MULHS, MVT::i32, Expand);

  setOperationAction(ISD::SHL_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRL,       MVT::i64, Custom);
  setOperationAction(ISD::SRA,       MVT::i64, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::i64, Custom);

  // Expand to __aeabi_l{lsl,lsr,asr} calls for Thumb1.
  if (Subtarget->isThumb1Only()) {
    setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
    setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
    setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);
  }

  if (!Subtarget->isThumb1Only() && Subtarget->hasV6T2Ops())
    setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);

  // ARM does not have ROTL.
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  for (MVT VT : MVT::vector_valuetypes()) {
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
  }
  setOperationAction(ISD::CTTZ,  MVT::i32, Custom);
  setOperationAction(ISD::CTPOP, MVT::i32, Expand);
  if (!Subtarget->hasV5TOps() || Subtarget->isThumb1Only()) {
    setOperationAction(ISD::CTLZ, MVT::i32, Expand);
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, LibCall);
  }

  // @llvm.readcyclecounter requires the Performance Monitors extension.
  // Default to the 0 expansion on unsupported platforms.
  // FIXME: Technically there are older ARM CPUs that have
  // implementation-specific ways of obtaining this information.
  if (Subtarget->hasPerfMon())
    setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Custom);

  // Only ARMv6 has BSWAP.
  if (!Subtarget->hasV6Ops())
    setOperationAction(ISD::BSWAP, MVT::i32, Expand);

  bool hasDivide = Subtarget->isThumb() ? Subtarget->hasDivideInThumbMode()
                                        : Subtarget->hasDivideInARMMode();
  if (!hasDivide) {
    // These are expanded into libcalls if the cpu doesn't have HW divider.
    setOperationAction(ISD::SDIV,  MVT::i32, LibCall);
    setOperationAction(ISD::UDIV,  MVT::i32, LibCall);
  }

  if (Subtarget->isTargetWindows() && !Subtarget->hasDivideInThumbMode()) {
    setOperationAction(ISD::SDIV, MVT::i32, Custom);
    setOperationAction(ISD::UDIV, MVT::i32, Custom);

    setOperationAction(ISD::SDIV, MVT::i64, Custom);
    setOperationAction(ISD::UDIV, MVT::i64, Custom);
  }

  setOperationAction(ISD::SREM,  MVT::i32, Expand);
  setOperationAction(ISD::UREM,  MVT::i32, Expand);

  // Register based DivRem for AEABI (RTABI 4.2)
  if (Subtarget->isTargetAEABI() || Subtarget->isTargetAndroid() ||
      Subtarget->isTargetGNUAEABI() || Subtarget->isTargetMuslAEABI() ||
      Subtarget->isTargetWindows()) {
    setOperationAction(ISD::SREM, MVT::i64, Custom);
    setOperationAction(ISD::UREM, MVT::i64, Custom);
    HasStandaloneRem = false;

    if (Subtarget->isTargetWindows()) {
      const struct {
        const RTLIB::Libcall Op;
        const char * const Name;
        const CallingConv::ID CC;
      } LibraryCalls[] = {
        { RTLIB::SDIVREM_I8, "__rt_sdiv", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I16, "__rt_sdiv", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I32, "__rt_sdiv", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I64, "__rt_sdiv64", CallingConv::ARM_AAPCS },

        { RTLIB::UDIVREM_I8, "__rt_udiv", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I16, "__rt_udiv", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I32, "__rt_udiv", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I64, "__rt_udiv64", CallingConv::ARM_AAPCS },
      };

      for (const auto &LC : LibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        setLibcallCallingConv(LC.Op, LC.CC);
      }
    } else {
      const struct {
        const RTLIB::Libcall Op;
        const char * const Name;
        const CallingConv::ID CC;
      } LibraryCalls[] = {
        { RTLIB::SDIVREM_I8, "__aeabi_idivmod", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I16, "__aeabi_idivmod", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I32, "__aeabi_idivmod", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I64, "__aeabi_ldivmod", CallingConv::ARM_AAPCS },

        { RTLIB::UDIVREM_I8, "__aeabi_uidivmod", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I16, "__aeabi_uidivmod", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I32, "__aeabi_uidivmod", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I64, "__aeabi_uldivmod", CallingConv::ARM_AAPCS },
      };

      for (const auto &LC : LibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        setLibcallCallingConv(LC.Op, LC.CC);
      }
    }

    setOperationAction(ISD::SDIVREM, MVT::i32, Custom);
    setOperationAction(ISD::UDIVREM, MVT::i32, Custom);
    setOperationAction(ISD::SDIVREM, MVT::i64, Custom);
    setOperationAction(ISD::UDIVREM, MVT::i64, Custom);
  } else {
    setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
    setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  }

  if (Subtarget->isTargetWindows() && Subtarget->getTargetTriple().isOSMSVCRT())
    for (auto &VT : {MVT::f32, MVT::f64})
      setOperationAction(ISD::FPOWI, VT, Custom);

  setOperationAction(ISD::GlobalAddress, MVT::i32,   Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i32,   Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);

  setOperationAction(ISD::TRAP, MVT::Other, Legal);
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Legal);

  // Use the default implementation.
  setOperationAction(ISD::VASTART,            MVT::Other, Custom);
  setOperationAction(ISD::VAARG,              MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,             MVT::Other, Expand);
  setOperationAction(ISD::VAEND,              MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE,          MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE,       MVT::Other, Expand);

  if (Subtarget->isTargetWindows())
    setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);
  else
    setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);

  // ARMv6 Thumb1 (except for CPUs that support dmb / dsb) and earlier use
  // the default expansion.
  InsertFencesForAtomic = false;
  if (Subtarget->hasAnyDataBarrier() &&
      (!Subtarget->isThumb() || Subtarget->hasV8MBaselineOps())) {
    // ATOMIC_FENCE needs custom lowering; the others should have been expanded
    // to ldrex/strex loops already.
    setOperationAction(ISD::ATOMIC_FENCE,     MVT::Other, Custom);
    if (!Subtarget->isThumb() || !Subtarget->isMClass())
      setOperationAction(ISD::ATOMIC_CMP_SWAP,  MVT::i64, Custom);

    // On v8, we have particularly efficient implementations of atomic fences
    // if they can be combined with nearby atomic loads and stores.
    if (!Subtarget->hasAcquireRelease() ||
        getTargetMachine().getOptLevel() == 0) {
      // Automatically insert fences (dmb ish) around ATOMIC_SWAP etc.
      InsertFencesForAtomic = true;
    }
  } else {
    // If there's anything we can use as a barrier, go through custom lowering
    // for ATOMIC_FENCE.
    // If target has DMB in thumb, Fences can be inserted.
    if (Subtarget->hasDataBarrier())
      InsertFencesForAtomic = true;

    setOperationAction(ISD::ATOMIC_FENCE,   MVT::Other,
                       Subtarget->hasAnyDataBarrier() ? Custom : Expand);

    // Set them all for expansion, which will force libcalls.
    setOperationAction(ISD::ATOMIC_CMP_SWAP,  MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_SWAP,      MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_ADD,  MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_SUB,  MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_AND,  MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_OR,   MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_XOR,  MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_NAND, MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_MIN, MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_MAX, MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_UMIN, MVT::i32, Expand);
    setOperationAction(ISD::ATOMIC_LOAD_UMAX, MVT::i32, Expand);
    // Mark ATOMIC_LOAD and ATOMIC_STORE custom so we can handle the
    // Unordered/Monotonic case.
    if (!InsertFencesForAtomic) {
      setOperationAction(ISD::ATOMIC_LOAD, MVT::i32, Custom);
      setOperationAction(ISD::ATOMIC_STORE, MVT::i32, Custom);
    }
  }

  setOperationAction(ISD::PREFETCH,         MVT::Other, Custom);

  // Requires SXTB/SXTH, available on v6 and up in both ARM and Thumb modes.
  if (!Subtarget->hasV6Ops()) {
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8,  Expand);
  }
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  if (!Subtarget->useSoftFloat() && Subtarget->hasVFP2() &&
      !Subtarget->isThumb1Only()) {
    // Turn f64->i64 into VMOVRRD, i64 -> f64 to VMOVDRR
    // iff target supports vfp2.
    setOperationAction(ISD::BITCAST, MVT::i64, Custom);
    setOperationAction(ISD::FLT_ROUNDS_, MVT::i32, Custom);
  }

  // We want to custom lower some of our intrinsics.
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction(ISD::EH_SJLJ_LONGJMP, MVT::Other, Custom);
  setOperationAction(ISD::EH_SJLJ_SETUP_DISPATCH, MVT::Other, Custom);
  if (Subtarget->useSjLjEH())
    setLibcallName(RTLIB::UNWIND_RESUME, "_Unwind_SjLj_Resume");

  setOperationAction(ISD::SETCC,     MVT::i32, Expand);
  setOperationAction(ISD::SETCC,     MVT::f32, Expand);
  setOperationAction(ISD::SETCC,     MVT::f64, Expand);
  setOperationAction(ISD::SELECT,    MVT::i32, Custom);
  setOperationAction(ISD::SELECT,    MVT::f32, Custom);
  setOperationAction(ISD::SELECT,    MVT::f64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Custom);
  if (Subtarget->hasFullFP16()) {
    setOperationAction(ISD::SETCC,     MVT::f16, Expand);
    setOperationAction(ISD::SELECT,    MVT::f16, Custom);
    setOperationAction(ISD::SELECT_CC, MVT::f16, Custom);
  }

  setOperationAction(ISD::SETCCCARRY, MVT::i32, Custom);

  setOperationAction(ISD::BRCOND,    MVT::Other, Custom);
  setOperationAction(ISD::BR_CC,     MVT::i32,   Custom);
  if (Subtarget->hasFullFP16())
      setOperationAction(ISD::BR_CC, MVT::f16,   Custom);
  setOperationAction(ISD::BR_CC,     MVT::f32,   Custom);
  setOperationAction(ISD::BR_CC,     MVT::f64,   Custom);
  setOperationAction(ISD::BR_JT,     MVT::Other, Custom);

  // We don't support sin/cos/fmod/copysign/pow
  setOperationAction(ISD::FSIN,      MVT::f64, Expand);
  setOperationAction(ISD::FSIN,      MVT::f32, Expand);
  setOperationAction(ISD::FCOS,      MVT::f32, Expand);
  setOperationAction(ISD::FCOS,      MVT::f64, Expand);
  setOperationAction(ISD::FSINCOS,   MVT::f64, Expand);
  setOperationAction(ISD::FSINCOS,   MVT::f32, Expand);
  setOperationAction(ISD::FREM,      MVT::f64, Expand);
  setOperationAction(ISD::FREM,      MVT::f32, Expand);
  if (!Subtarget->useSoftFloat() && Subtarget->hasVFP2() &&
      !Subtarget->isThumb1Only()) {
    setOperationAction(ISD::FCOPYSIGN, MVT::f64, Custom);
    setOperationAction(ISD::FCOPYSIGN, MVT::f32, Custom);
  }
  setOperationAction(ISD::FPOW,      MVT::f64, Expand);
  setOperationAction(ISD::FPOW,      MVT::f32, Expand);

  if (!Subtarget->hasVFP4()) {
    setOperationAction(ISD::FMA, MVT::f64, Expand);
    setOperationAction(ISD::FMA, MVT::f32, Expand);
  }

  // Various VFP goodness
  if (!Subtarget->useSoftFloat() && !Subtarget->isThumb1Only()) {
    // FP-ARMv8 adds f64 <-> f16 conversion. Before that it should be expanded.
    if (!Subtarget->hasFPARMv8() || Subtarget->isFPOnlySP()) {
      setOperationAction(ISD::FP16_TO_FP, MVT::f64, Expand);
      setOperationAction(ISD::FP_TO_FP16, MVT::f64, Expand);
    }

    // fp16 is a special v7 extension that adds f16 <-> f32 conversions.
    if (!Subtarget->hasFP16()) {
      setOperationAction(ISD::FP16_TO_FP, MVT::f32, Expand);
      setOperationAction(ISD::FP_TO_FP16, MVT::f32, Expand);
    }
  }

  // Use __sincos_stret if available.
  if (getLibcallName(RTLIB::SINCOS_STRET_F32) != nullptr &&
      getLibcallName(RTLIB::SINCOS_STRET_F64) != nullptr) {
    setOperationAction(ISD::FSINCOS, MVT::f64, Custom);
    setOperationAction(ISD::FSINCOS, MVT::f32, Custom);
  }

  // FP-ARMv8 implements a lot of rounding-like FP operations.
  if (Subtarget->hasFPARMv8()) {
    setOperationAction(ISD::FFLOOR, MVT::f32, Legal);
    setOperationAction(ISD::FCEIL, MVT::f32, Legal);
    setOperationAction(ISD::FROUND, MVT::f32, Legal);
    setOperationAction(ISD::FTRUNC, MVT::f32, Legal);
    setOperationAction(ISD::FNEARBYINT, MVT::f32, Legal);
    setOperationAction(ISD::FRINT, MVT::f32, Legal);
    setOperationAction(ISD::FMINNUM, MVT::f32, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::f32, Legal);
    setOperationAction(ISD::FMINNUM, MVT::v2f32, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::v2f32, Legal);
    setOperationAction(ISD::FMINNUM, MVT::v4f32, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::v4f32, Legal);

    if (!Subtarget->isFPOnlySP()) {
      setOperationAction(ISD::FFLOOR, MVT::f64, Legal);
      setOperationAction(ISD::FCEIL, MVT::f64, Legal);
      setOperationAction(ISD::FROUND, MVT::f64, Legal);
      setOperationAction(ISD::FTRUNC, MVT::f64, Legal);
      setOperationAction(ISD::FNEARBYINT, MVT::f64, Legal);
      setOperationAction(ISD::FRINT, MVT::f64, Legal);
      setOperationAction(ISD::FMINNUM, MVT::f64, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::f64, Legal);
    }
  }

  if (Subtarget->hasNEON()) {
    // vmin and vmax aren't available in a scalar form, so we use
    // a NEON instruction with an undef lane instead.
    setOperationAction(ISD::FMINIMUM, MVT::f16, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::f16, Legal);
    setOperationAction(ISD::FMINIMUM, MVT::f32, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::f32, Legal);
    setOperationAction(ISD::FMINIMUM, MVT::v2f32, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::v2f32, Legal);
    setOperationAction(ISD::FMINIMUM, MVT::v4f32, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::v4f32, Legal);

    if (Subtarget->hasFullFP16()) {
      setOperationAction(ISD::FMINNUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMINNUM, MVT::v8f16, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::v8f16, Legal);

      setOperationAction(ISD::FMINIMUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMAXIMUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMINIMUM, MVT::v8f16, Legal);
      setOperationAction(ISD::FMAXIMUM, MVT::v8f16, Legal);
    }
  }

  // We have target-specific dag combine patterns for the following nodes:
  // ARMISD::VMOVRRD  - No need to call setTargetDAGCombine
  setTargetDAGCombine(ISD::ADD);
  setTargetDAGCombine(ISD::SUB);
  setTargetDAGCombine(ISD::MUL);
  setTargetDAGCombine(ISD::AND);
  setTargetDAGCombine(ISD::OR);
  setTargetDAGCombine(ISD::XOR);

  if (Subtarget->hasV6Ops())
    setTargetDAGCombine(ISD::SRL);

  setStackPointerRegisterToSaveRestore(ARM::SP);

  if (Subtarget->useSoftFloat() || Subtarget->isThumb1Only() ||
      !Subtarget->hasVFP2())
    setSchedulingPreference(Sched::RegPressure);
  else
    setSchedulingPreference(Sched::Hybrid);

  //// temporary - rewrite interface to use type
  MaxStoresPerMemset = 8;
  MaxStoresPerMemsetOptSize = 4;
  MaxStoresPerMemcpy = 4; // For @llvm.memcpy -> sequence of stores
  MaxStoresPerMemcpyOptSize = 2;
  MaxStoresPerMemmove = 4; // For @llvm.memmove -> sequence of stores
  MaxStoresPerMemmoveOptSize = 2;

  // On ARM arguments smaller than 4 bytes are extended, so all arguments
  // are at least 4 bytes aligned.
  setMinStackArgumentAlignment(4);

  // Prefer likely predicted branches to selects on out-of-order cores.
  PredictableSelectIsExpensive = Subtarget->getSchedModel().isOutOfOrder();

  setPrefLoopAlignment(Subtarget->getPrefLoopAlignment());

  setMinFunctionAlignment(Subtarget->isThumb() ? 1 : 2);
}

bool ARMTargetLowering::useSoftFloat() const {
  return Subtarget->useSoftFloat();
}

// FIXME: It might make sense to define the representative register class as the
// nearest super-register that has a non-null superset. For example, DPR_VFP2 is
// a super-register of SPR, and DPR is a superset if DPR_VFP2. Consequently,
// SPR's representative would be DPR_VFP2. This should work well if register
// pressure tracking were modified such that a register use would increment the
// pressure of the register class's representative and all of it's super
// classes' representatives transitively. We have not implemented this because
// of the difficulty prior to coalescing of modeling operand register classes
// due to the common occurrence of cross class copies and subregister insertions
// and extractions.
std::pair<const TargetRegisterClass *, uint8_t>
ARMTargetLowering::findRepresentativeClass(const TargetRegisterInfo *TRI,
                                           MVT VT) const {
  const TargetRegisterClass *RRC = nullptr;
  uint8_t Cost = 1;
  switch (VT.SimpleTy) {
  default:
    return TargetLowering::findRepresentativeClass(TRI, VT);
  // Use DPR as representative register class for all floating point
  // and vector types. Since there are 32 SPR registers and 32 DPR registers so
  // the cost is 1 for both f32 and f64.
  case MVT::f32: case MVT::f64: case MVT::v8i8: case MVT::v4i16:
  case MVT::v2i32: case MVT::v1i64: case MVT::v2f32:
    RRC = &ARM::DPRRegClass;
    // When NEON is used for SP, only half of the register file is available
    // because operations that define both SP and DP results will be constrained
    // to the VFP2 class (D0-D15). We currently model this constraint prior to
    // coalescing by double-counting the SP regs. See the FIXME above.
    if (Subtarget->useNEONForSinglePrecisionFP())
      Cost = 2;
    break;
  case MVT::v16i8: case MVT::v8i16: case MVT::v4i32: case MVT::v2i64:
  case MVT::v4f32: case MVT::v2f64:
    RRC = &ARM::DPRRegClass;
    Cost = 2;
    break;
  case MVT::v4i64:
    RRC = &ARM::DPRRegClass;
    Cost = 4;
    break;
  case MVT::v8i64:
    RRC = &ARM::DPRRegClass;
    Cost = 8;
    break;
  }
  return std::make_pair(RRC, Cost);
}

const char *ARMTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((ARMISD::NodeType)Opcode) {
  case ARMISD::FIRST_NUMBER:  break;
  case ARMISD::Wrapper:       return "ARMISD::Wrapper";
  case ARMISD::WrapperPIC:    return "ARMISD::WrapperPIC";
  case ARMISD::WrapperJT:     return "ARMISD::WrapperJT";
  case ARMISD::COPY_STRUCT_BYVAL: return "ARMISD::COPY_STRUCT_BYVAL";
  case ARMISD::CALL:          return "ARMISD::CALL";
  case ARMISD::CALL_PRED:     return "ARMISD::CALL_PRED";
  case ARMISD::CALL_NOLINK:   return "ARMISD::CALL_NOLINK";
  case ARMISD::BRCOND:        return "ARMISD::BRCOND";
  case ARMISD::BR_JT:         return "ARMISD::BR_JT";
  case ARMISD::BR2_JT:        return "ARMISD::BR2_JT";
  case ARMISD::RET_FLAG:      return "ARMISD::RET_FLAG";
  case ARMISD::INTRET_FLAG:   return "ARMISD::INTRET_FLAG";
  case ARMISD::PIC_ADD:       return "ARMISD::PIC_ADD";
  case ARMISD::CMP:           return "ARMISD::CMP";
  case ARMISD::CMN:           return "ARMISD::CMN";
  case ARMISD::CMPZ:          return "ARMISD::CMPZ";
  case ARMISD::CMPFP:         return "ARMISD::CMPFP";
  case ARMISD::CMPFPw0:       return "ARMISD::CMPFPw0";
  case ARMISD::BCC_i64:       return "ARMISD::BCC_i64";
  case ARMISD::FMSTAT:        return "ARMISD::FMSTAT";

  case ARMISD::CMOV:          return "ARMISD::CMOV";
  case ARMISD::SUBS:          return "ARMISD::SUBS";

  case ARMISD::SSAT:          return "ARMISD::SSAT";
  case ARMISD::USAT:          return "ARMISD::USAT";

  case ARMISD::SRL_FLAG:      return "ARMISD::SRL_FLAG";
  case ARMISD::SRA_FLAG:      return "ARMISD::SRA_FLAG";
  case ARMISD::RRX:           return "ARMISD::RRX";

  case ARMISD::ADDC:          return "ARMISD::ADDC";
  case ARMISD::ADDE:          return "ARMISD::ADDE";
  case ARMISD::SUBC:          return "ARMISD::SUBC";
  case ARMISD::SUBE:          return "ARMISD::SUBE";

  case ARMISD::VMOVRRD:       return "ARMISD::VMOVRRD";
  case ARMISD::VMOVDRR:       return "ARMISD::VMOVDRR";
  case ARMISD::VMOVhr:        return "ARMISD::VMOVhr";
  case ARMISD::VMOVrh:        return "ARMISD::VMOVrh";
  case ARMISD::VMOVSR:        return "ARMISD::VMOVSR";

  case ARMISD::EH_SJLJ_SETJMP: return "ARMISD::EH_SJLJ_SETJMP";
  case ARMISD::EH_SJLJ_LONGJMP: return "ARMISD::EH_SJLJ_LONGJMP";
  case ARMISD::EH_SJLJ_SETUP_DISPATCH: return "ARMISD::EH_SJLJ_SETUP_DISPATCH";

  case ARMISD::TC_RETURN:     return "ARMISD::TC_RETURN";

  case ARMISD::THREAD_POINTER:return "ARMISD::THREAD_POINTER";

  case ARMISD::DYN_ALLOC:     return "ARMISD::DYN_ALLOC";

  case ARMISD::MEMBARRIER_MCR: return "ARMISD::MEMBARRIER_MCR";

  case ARMISD::PRELOAD:       return "ARMISD::PRELOAD";

  case ARMISD::WIN__CHKSTK:   return "ARMISD::WIN__CHKSTK";
  case ARMISD::WIN__DBZCHK:   return "ARMISD::WIN__DBZCHK";

  case ARMISD::VCEQ:          return "ARMISD::VCEQ";
  case ARMISD::VCEQZ:         return "ARMISD::VCEQZ";
  case ARMISD::VCGE:          return "ARMISD::VCGE";
  case ARMISD::VCGEZ:         return "ARMISD::VCGEZ";
  case ARMISD::VCLEZ:         return "ARMISD::VCLEZ";
  case ARMISD::VCGEU:         return "ARMISD::VCGEU";
  case ARMISD::VCGT:          return "ARMISD::VCGT";
  case ARMISD::VCGTZ:         return "ARMISD::VCGTZ";
  case ARMISD::VCLTZ:         return "ARMISD::VCLTZ";
  case ARMISD::VCGTU:         return "ARMISD::VCGTU";
  case ARMISD::VTST:          return "ARMISD::VTST";

  case ARMISD::VSHL:          return "ARMISD::VSHL";
  case ARMISD::VSHRs:         return "ARMISD::VSHRs";
  case ARMISD::VSHRu:         return "ARMISD::VSHRu";
  case ARMISD::VRSHRs:        return "ARMISD::VRSHRs";
  case ARMISD::VRSHRu:        return "ARMISD::VRSHRu";
  case ARMISD::VRSHRN:        return "ARMISD::VRSHRN";
  case ARMISD::VQSHLs:        return "ARMISD::VQSHLs";
  case ARMISD::VQSHLu:        return "ARMISD::VQSHLu";
  case ARMISD::VQSHLsu:       return "ARMISD::VQSHLsu";
  case ARMISD::VQSHRNs:       return "ARMISD::VQSHRNs";
  case ARMISD::VQSHRNu:       return "ARMISD::VQSHRNu";
  case ARMISD::VQSHRNsu:      return "ARMISD::VQSHRNsu";
  case ARMISD::VQRSHRNs:      return "ARMISD::VQRSHRNs";
  case ARMISD::VQRSHRNu:      return "ARMISD::VQRSHRNu";
  case ARMISD::VQRSHRNsu:     return "ARMISD::VQRSHRNsu";
  case ARMISD::VSLI:          return "ARMISD::VSLI";
  case ARMISD::VSRI:          return "ARMISD::VSRI";
  case ARMISD::VGETLANEu:     return "ARMISD::VGETLANEu";
  case ARMISD::VGETLANEs:     return "ARMISD::VGETLANEs";
  case ARMISD::VMOVIMM:       return "ARMISD::VMOVIMM";
  case ARMISD::VMVNIMM:       return "ARMISD::VMVNIMM";
  case ARMISD::VMOVFPIMM:     return "ARMISD::VMOVFPIMM";
  case ARMISD::VDUP:          return "ARMISD::VDUP";
  case ARMISD::VDUPLANE:      return "ARMISD::VDUPLANE";
  case ARMISD::VEXT:          return "ARMISD::VEXT";
  case ARMISD::VREV64:        return "ARMISD::VREV64";
  case ARMISD::VREV32:        return "ARMISD::VREV32";
  case ARMISD::VREV16:        return "ARMISD::VREV16";
  case ARMISD::VZIP:          return "ARMISD::VZIP";
  case ARMISD::VUZP:          return "ARMISD::VUZP";
  case ARMISD::VTRN:          return "ARMISD::VTRN";
  case ARMISD::VTBL1:         return "ARMISD::VTBL1";
  case ARMISD::VTBL2:         return "ARMISD::VTBL2";
  case ARMISD::VMULLs:        return "ARMISD::VMULLs";
  case ARMISD::VMULLu:        return "ARMISD::VMULLu";
  case ARMISD::UMAAL:         return "ARMISD::UMAAL";
  case ARMISD::UMLAL:         return "ARMISD::UMLAL";
  case ARMISD::SMLAL:         return "ARMISD::SMLAL";
  case ARMISD::SMLALBB:       return "ARMISD::SMLALBB";
  case ARMISD::SMLALBT:       return "ARMISD::SMLALBT";
  case ARMISD::SMLALTB:       return "ARMISD::SMLALTB";
  case ARMISD::SMLALTT:       return "ARMISD::SMLALTT";
  case ARMISD::SMULWB:        return "ARMISD::SMULWB";
  case ARMISD::SMULWT:        return "ARMISD::SMULWT";
  case ARMISD::SMLALD:        return "ARMISD::SMLALD";
  case ARMISD::SMLALDX:       return "ARMISD::SMLALDX";
  case ARMISD::SMLSLD:        return "ARMISD::SMLSLD";
  case ARMISD::SMLSLDX:       return "ARMISD::SMLSLDX";
  case ARMISD::SMMLAR:        return "ARMISD::SMMLAR";
  case ARMISD::SMMLSR:        return "ARMISD::SMMLSR";
  case ARMISD::BUILD_VECTOR:  return "ARMISD::BUILD_VECTOR";
  case ARMISD::BFI:           return "ARMISD::BFI";
  case ARMISD::VORRIMM:       return "ARMISD::VORRIMM";
  case ARMISD::VBICIMM:       return "ARMISD::VBICIMM";
  case ARMISD::VBSL:          return "ARMISD::VBSL";
  case ARMISD::MEMCPY:        return "ARMISD::MEMCPY";
  case ARMISD::VLD1DUP:       return "ARMISD::VLD1DUP";
  case ARMISD::VLD2DUP:       return "ARMISD::VLD2DUP";
  case ARMISD::VLD3DUP:       return "ARMISD::VLD3DUP";
  case ARMISD::VLD4DUP:       return "ARMISD::VLD4DUP";
  case ARMISD::VLD1_UPD:      return "ARMISD::VLD1_UPD";
  case ARMISD::VLD2_UPD:      return "ARMISD::VLD2_UPD";
  case ARMISD::VLD3_UPD:      return "ARMISD::VLD3_UPD";
  case ARMISD::VLD4_UPD:      return "ARMISD::VLD4_UPD";
  case ARMISD::VLD2LN_UPD:    return "ARMISD::VLD2LN_UPD";
  case ARMISD::VLD3LN_UPD:    return "ARMISD::VLD3LN_UPD";
  case ARMISD::VLD4LN_UPD:    return "ARMISD::VLD4LN_UPD";
  case ARMISD::VLD1DUP_UPD:   return "ARMISD::VLD1DUP_UPD";
  case ARMISD::VLD2DUP_UPD:   return "ARMISD::VLD2DUP_UPD";
  case ARMISD::VLD3DUP_UPD:   return "ARMISD::VLD3DUP_UPD";
  case ARMISD::VLD4DUP_UPD:   return "ARMISD::VLD4DUP_UPD";
  case ARMISD::VST1_UPD:      return "ARMISD::VST1_UPD";
  case ARMISD::VST2_UPD:      return "ARMISD::VST2_UPD";
  case ARMISD::VST3_UPD:      return "ARMISD::VST3_UPD";
  case ARMISD::VST4_UPD:      return "ARMISD::VST4_UPD";
  case ARMISD::VST2LN_UPD:    return "ARMISD::VST2LN_UPD";
  case ARMISD::VST3LN_UPD:    return "ARMISD::VST3LN_UPD";
  case ARMISD::VST4LN_UPD:    return "ARMISD::VST4LN_UPD";
  }
  return nullptr;
}

EVT ARMTargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &,
                                          EVT VT) const {
  if (!VT.isVector())
    return getPointerTy(DL);
  return VT.changeVectorElementTypeToInteger();
}

/// getRegClassFor - Return the register class that should be used for the
/// specified value type.
const TargetRegisterClass *ARMTargetLowering::getRegClassFor(MVT VT) const {
  // Map v4i64 to QQ registers but do not make the type legal. Similarly map
  // v8i64 to QQQQ registers. v4i64 and v8i64 are only used for REG_SEQUENCE to
  // load / store 4 to 8 consecutive D registers.
  if (Subtarget->hasNEON()) {
    if (VT == MVT::v4i64)
      return &ARM::QQPRRegClass;
    if (VT == MVT::v8i64)
      return &ARM::QQQQPRRegClass;
  }
  return TargetLowering::getRegClassFor(VT);
}

// memcpy, and other memory intrinsics, typically tries to use LDM/STM if the
// source/dest is aligned and the copy size is large enough. We therefore want
// to align such objects passed to memory intrinsics.
bool ARMTargetLowering::shouldAlignPointerArgs(CallInst *CI, unsigned &MinSize,
                                               unsigned &PrefAlign) const {
  if (!isa<MemIntrinsic>(CI))
    return false;
  MinSize = 8;
  // On ARM11 onwards (excluding M class) 8-byte aligned LDM is typically 1
  // cycle faster than 4-byte aligned LDM.
  PrefAlign = (Subtarget->hasV6Ops() && !Subtarget->isMClass() ? 8 : 4);
  return true;
}

// Create a fast isel object.
FastISel *
ARMTargetLowering::createFastISel(FunctionLoweringInfo &funcInfo,
                                  const TargetLibraryInfo *libInfo) const {
  return ARM::createFastISel(funcInfo, libInfo);
}

Sched::Preference ARMTargetLowering::getSchedulingPreference(SDNode *N) const {
  unsigned NumVals = N->getNumValues();
  if (!NumVals)
    return Sched::RegPressure;

  for (unsigned i = 0; i != NumVals; ++i) {
    EVT VT = N->getValueType(i);
    if (VT == MVT::Glue || VT == MVT::Other)
      continue;
    if (VT.isFloatingPoint() || VT.isVector())
      return Sched::ILP;
  }

  if (!N->isMachineOpcode())
    return Sched::RegPressure;

  // Load are scheduled for latency even if there instruction itinerary
  // is not available.
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());

  if (MCID.getNumDefs() == 0)
    return Sched::RegPressure;
  if (!Itins->isEmpty() &&
      Itins->getOperandCycle(MCID.getSchedClass(), 0) > 2)
    return Sched::ILP;

  return Sched::RegPressure;
}

//===----------------------------------------------------------------------===//
// Lowering Code
//===----------------------------------------------------------------------===//

static bool isSRL16(const SDValue &Op) {
  if (Op.getOpcode() != ISD::SRL)
    return false;
  if (auto Const = dyn_cast<ConstantSDNode>(Op.getOperand(1)))
    return Const->getZExtValue() == 16;
  return false;
}

static bool isSRA16(const SDValue &Op) {
  if (Op.getOpcode() != ISD::SRA)
    return false;
  if (auto Const = dyn_cast<ConstantSDNode>(Op.getOperand(1)))
    return Const->getZExtValue() == 16;
  return false;
}

static bool isSHL16(const SDValue &Op) {
  if (Op.getOpcode() != ISD::SHL)
    return false;
  if (auto Const = dyn_cast<ConstantSDNode>(Op.getOperand(1)))
    return Const->getZExtValue() == 16;
  return false;
}

// Check for a signed 16-bit value. We special case SRA because it makes it
// more simple when also looking for SRAs that aren't sign extending a
// smaller value. Without the check, we'd need to take extra care with
// checking order for some operations.
static bool isS16(const SDValue &Op, SelectionDAG &DAG) {
  if (isSRA16(Op))
    return isSHL16(Op.getOperand(0));
  return DAG.ComputeNumSignBits(Op) == 17;
}

/// IntCCToARMCC - Convert a DAG integer condition code to an ARM CC
static ARMCC::CondCodes IntCCToARMCC(ISD::CondCode CC) {
  switch (CC) {
  default: llvm_unreachable("Unknown condition code!");
  case ISD::SETNE:  return ARMCC::NE;
  case ISD::SETEQ:  return ARMCC::EQ;
  case ISD::SETGT:  return ARMCC::GT;
  case ISD::SETGE:  return ARMCC::GE;
  case ISD::SETLT:  return ARMCC::LT;
  case ISD::SETLE:  return ARMCC::LE;
  case ISD::SETUGT: return ARMCC::HI;
  case ISD::SETUGE: return ARMCC::HS;
  case ISD::SETULT: return ARMCC::LO;
  case ISD::SETULE: return ARMCC::LS;
  }
}

/// FPCCToARMCC - Convert a DAG fp condition code to an ARM CC.
static void FPCCToARMCC(ISD::CondCode CC, ARMCC::CondCodes &CondCode,
                        ARMCC::CondCodes &CondCode2, bool &InvalidOnQNaN) {
  CondCode2 = ARMCC::AL;
  InvalidOnQNaN = true;
  switch (CC) {
  default: llvm_unreachable("Unknown FP condition!");
  case ISD::SETEQ:
  case ISD::SETOEQ:
    CondCode = ARMCC::EQ;
    InvalidOnQNaN = false;
    break;
  case ISD::SETGT:
  case ISD::SETOGT: CondCode = ARMCC::GT; break;
  case ISD::SETGE:
  case ISD::SETOGE: CondCode = ARMCC::GE; break;
  case ISD::SETOLT: CondCode = ARMCC::MI; break;
  case ISD::SETOLE: CondCode = ARMCC::LS; break;
  case ISD::SETONE:
    CondCode = ARMCC::MI;
    CondCode2 = ARMCC::GT;
    InvalidOnQNaN = false;
    break;
  case ISD::SETO:   CondCode = ARMCC::VC; break;
  case ISD::SETUO:  CondCode = ARMCC::VS; break;
  case ISD::SETUEQ:
    CondCode = ARMCC::EQ;
    CondCode2 = ARMCC::VS;
    InvalidOnQNaN = false;
    break;
  case ISD::SETUGT: CondCode = ARMCC::HI; break;
  case ISD::SETUGE: CondCode = ARMCC::PL; break;
  case ISD::SETLT:
  case ISD::SETULT: CondCode = ARMCC::LT; break;
  case ISD::SETLE:
  case ISD::SETULE: CondCode = ARMCC::LE; break;
  case ISD::SETNE:
  case ISD::SETUNE:
    CondCode = ARMCC::NE;
    InvalidOnQNaN = false;
    break;
  }
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "ARMGenCallingConv.inc"

/// getEffectiveCallingConv - Get the effective calling convention, taking into
/// account presence of floating point hardware and calling convention
/// limitations, such as support for variadic functions.
CallingConv::ID
ARMTargetLowering::getEffectiveCallingConv(CallingConv::ID CC,
                                           bool isVarArg) const {
  switch (CC) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::ARM_AAPCS:
  case CallingConv::ARM_APCS:
  case CallingConv::GHC:
    return CC;
  case CallingConv::PreserveMost:
    return CallingConv::PreserveMost;
  case CallingConv::ARM_AAPCS_VFP:
  case CallingConv::Swift:
    return isVarArg ? CallingConv::ARM_AAPCS : CallingConv::ARM_AAPCS_VFP;
  case CallingConv::C:
    if (!Subtarget->isAAPCS_ABI())
      return CallingConv::ARM_APCS;
    else if (Subtarget->hasVFP2() && !Subtarget->isThumb1Only() &&
             getTargetMachine().Options.FloatABIType == FloatABI::Hard &&
             !isVarArg)
      return CallingConv::ARM_AAPCS_VFP;
    else
      return CallingConv::ARM_AAPCS;
  case CallingConv::Fast:
  case CallingConv::CXX_FAST_TLS:
    if (!Subtarget->isAAPCS_ABI()) {
      if (Subtarget->hasVFP2() && !Subtarget->isThumb1Only() && !isVarArg)
        return CallingConv::Fast;
      return CallingConv::ARM_APCS;
    } else if (Subtarget->hasVFP2() && !Subtarget->isThumb1Only() && !isVarArg)
      return CallingConv::ARM_AAPCS_VFP;
    else
      return CallingConv::ARM_AAPCS;
  }
}

CCAssignFn *ARMTargetLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                 bool isVarArg) const {
  return CCAssignFnForNode(CC, false, isVarArg);
}

CCAssignFn *ARMTargetLowering::CCAssignFnForReturn(CallingConv::ID CC,
                                                   bool isVarArg) const {
  return CCAssignFnForNode(CC, true, isVarArg);
}

/// CCAssignFnForNode - Selects the correct CCAssignFn for the given
/// CallingConvention.
CCAssignFn *ARMTargetLowering::CCAssignFnForNode(CallingConv::ID CC,
                                                 bool Return,
                                                 bool isVarArg) const {
  switch (getEffectiveCallingConv(CC, isVarArg)) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::ARM_APCS:
    return (Return ? RetCC_ARM_APCS : CC_ARM_APCS);
  case CallingConv::ARM_AAPCS:
    return (Return ? RetCC_ARM_AAPCS : CC_ARM_AAPCS);
  case CallingConv::ARM_AAPCS_VFP:
    return (Return ? RetCC_ARM_AAPCS_VFP : CC_ARM_AAPCS_VFP);
  case CallingConv::Fast:
    return (Return ? RetFastCC_ARM_APCS : FastCC_ARM_APCS);
  case CallingConv::GHC:
    return (Return ? RetCC_ARM_APCS : CC_ARM_APCS_GHC);
  case CallingConv::PreserveMost:
    return (Return ? RetCC_ARM_AAPCS : CC_ARM_AAPCS);
  }
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
SDValue ARMTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InFlag, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals, bool isThisReturn,
    SDValue ThisVal) const {
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, CCAssignFnForReturn(CallConv, isVarArg));

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign VA = RVLocs[i];

    // Pass 'this' value directly from the argument to return value, to avoid
    // reg unit interference
    if (i == 0 && isThisReturn) {
      assert(!VA.needsCustom() && VA.getLocVT() == MVT::i32 &&
             "unexpected return calling convention register assignment");
      InVals.push_back(ThisVal);
      continue;
    }

    SDValue Val;
    if (VA.needsCustom()) {
      // Handle f64 or half of a v2f64.
      SDValue Lo = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InFlag);
      Chain = Lo.getValue(1);
      InFlag = Lo.getValue(2);
      VA = RVLocs[++i]; // skip ahead to next loc
      SDValue Hi = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InFlag);
      Chain = Hi.getValue(1);
      InFlag = Hi.getValue(2);
      if (!Subtarget->isLittle())
        std::swap (Lo, Hi);
      Val = DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi);

      if (VA.getLocVT() == MVT::v2f64) {
        SDValue Vec = DAG.getNode(ISD::UNDEF, dl, MVT::v2f64);
        Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Vec, Val,
                          DAG.getConstant(0, dl, MVT::i32));

        VA = RVLocs[++i]; // skip ahead to next loc
        Lo = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32, InFlag);
        Chain = Lo.getValue(1);
        InFlag = Lo.getValue(2);
        VA = RVLocs[++i]; // skip ahead to next loc
        Hi = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32, InFlag);
        Chain = Hi.getValue(1);
        InFlag = Hi.getValue(2);
        if (!Subtarget->isLittle())
          std::swap (Lo, Hi);
        Val = DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi);
        Val = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Vec, Val,
                          DAG.getConstant(1, dl, MVT::i32));
      }
    } else {
      Val = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), VA.getLocVT(),
                               InFlag);
      Chain = Val.getValue(1);
      InFlag = Val.getValue(2);
    }

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::BCvt:
      Val = DAG.getNode(ISD::BITCAST, dl, VA.getValVT(), Val);
      break;
    }

    InVals.push_back(Val);
  }

  return Chain;
}

/// LowerMemOpCallTo - Store the argument to the stack.
SDValue ARMTargetLowering::LowerMemOpCallTo(SDValue Chain, SDValue StackPtr,
                                            SDValue Arg, const SDLoc &dl,
                                            SelectionDAG &DAG,
                                            const CCValAssign &VA,
                                            ISD::ArgFlagsTy Flags) const {
  unsigned LocMemOffset = VA.getLocMemOffset();
  SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset, dl);
  PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(DAG.getDataLayout()),
                       StackPtr, PtrOff);
  return DAG.getStore(
      Chain, dl, Arg, PtrOff,
      MachinePointerInfo::getStack(DAG.getMachineFunction(), LocMemOffset));
}

void ARMTargetLowering::PassF64ArgInRegs(const SDLoc &dl, SelectionDAG &DAG,
                                         SDValue Chain, SDValue &Arg,
                                         RegsToPassVector &RegsToPass,
                                         CCValAssign &VA, CCValAssign &NextVA,
                                         SDValue &StackPtr,
                                         SmallVectorImpl<SDValue> &MemOpChains,
                                         ISD::ArgFlagsTy Flags) const {
  SDValue fmrrd = DAG.getNode(ARMISD::VMOVRRD, dl,
                              DAG.getVTList(MVT::i32, MVT::i32), Arg);
  unsigned id = Subtarget->isLittle() ? 0 : 1;
  RegsToPass.push_back(std::make_pair(VA.getLocReg(), fmrrd.getValue(id)));

  if (NextVA.isRegLoc())
    RegsToPass.push_back(std::make_pair(NextVA.getLocReg(), fmrrd.getValue(1-id)));
  else {
    assert(NextVA.isMemLoc());
    if (!StackPtr.getNode())
      StackPtr = DAG.getCopyFromReg(Chain, dl, ARM::SP,
                                    getPointerTy(DAG.getDataLayout()));

    MemOpChains.push_back(LowerMemOpCallTo(Chain, StackPtr, fmrrd.getValue(1-id),
                                           dl, DAG, NextVA,
                                           Flags));
  }
}

/// LowerCall - Lowering a call into a callseq_start <-
/// ARMISD:CALL <- callseq_end chain. Also add input and output parameter
/// nodes.
SDValue
ARMTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                             SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals     = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool doesNotRet                       = CLI.DoesNotReturn;
  bool isVarArg                         = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();
  bool isStructRet    = (Outs.empty()) ? false : Outs[0].Flags.isSRet();
  bool isThisReturn   = false;
  bool isSibCall      = false;
  auto Attr = MF.getFunction().getFnAttribute("disable-tail-calls");

  // Disable tail calls if they're not supported.
  if (!Subtarget->supportsTailCall() || Attr.getValueAsString() == "true")
    isTailCall = false;

  if (isTailCall) {
    // Check if it's really possible to do a tail call.
    isTailCall = IsEligibleForTailCallOptimization(Callee, CallConv,
                    isVarArg, isStructRet, MF.getFunction().hasStructRetAttr(),
                                                   Outs, OutVals, Ins, DAG);
    if (!isTailCall && CLI.CS && CLI.CS.isMustTailCall())
      report_fatal_error("failed to perform tail call elimination on a call "
                         "site marked musttail");
    // We don't support GuaranteedTailCallOpt for ARM, only automatically
    // detected sibcalls.
    if (isTailCall) {
      ++NumTailCalls;
      isSibCall = true;
    }
  }

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CCAssignFnForCall(CallConv, isVarArg));

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getNextStackOffset();

  // For tail calls, memory operands are available in our caller's stack.
  if (isSibCall)
    NumBytes = 0;

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  if (!isSibCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  SDValue StackPtr =
      DAG.getCopyFromReg(Chain, dl, ARM::SP, getPointerTy(DAG.getDataLayout()));

  RegsToPassVector RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  // Walk the register/memloc assignments, inserting copies/loads.  In the case
  // of tail call optimization, arguments are handled later.
  for (unsigned i = 0, realArgIdx = 0, e = ArgLocs.size();
       i != e;
       ++i, ++realArgIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[realArgIdx];
    ISD::ArgFlagsTy Flags = Outs[realArgIdx].Flags;
    bool isByVal = Flags.isByVal();

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      break;
    }

    // f64 and v2f64 might be passed in i32 pairs and must be split into pieces
    if (VA.needsCustom()) {
      if (VA.getLocVT() == MVT::v2f64) {
        SDValue Op0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                                  DAG.getConstant(0, dl, MVT::i32));
        SDValue Op1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                                  DAG.getConstant(1, dl, MVT::i32));

        PassF64ArgInRegs(dl, DAG, Chain, Op0, RegsToPass,
                         VA, ArgLocs[++i], StackPtr, MemOpChains, Flags);

        VA = ArgLocs[++i]; // skip ahead to next loc
        if (VA.isRegLoc()) {
          PassF64ArgInRegs(dl, DAG, Chain, Op1, RegsToPass,
                           VA, ArgLocs[++i], StackPtr, MemOpChains, Flags);
        } else {
          assert(VA.isMemLoc());

          MemOpChains.push_back(LowerMemOpCallTo(Chain, StackPtr, Op1,
                                                 dl, DAG, VA, Flags));
        }
      } else {
        PassF64ArgInRegs(dl, DAG, Chain, Arg, RegsToPass, VA, ArgLocs[++i],
                         StackPtr, MemOpChains, Flags);
      }
    } else if (VA.isRegLoc()) {
      if (realArgIdx == 0 && Flags.isReturned() && !Flags.isSwiftSelf() &&
          Outs[0].VT == MVT::i32) {
        assert(VA.getLocVT() == MVT::i32 &&
               "unexpected calling convention register assignment");
        assert(!Ins.empty() && Ins[0].VT == MVT::i32 &&
               "unexpected use of 'returned'");
        isThisReturn = true;
      }
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else if (isByVal) {
      assert(VA.isMemLoc());
      unsigned offset = 0;

      // True if this byval aggregate will be split between registers
      // and memory.
      unsigned ByValArgsCount = CCInfo.getInRegsParamsCount();
      unsigned CurByValIdx = CCInfo.getInRegsParamsProcessed();

      if (CurByValIdx < ByValArgsCount) {

        unsigned RegBegin, RegEnd;
        CCInfo.getInRegsParamInfo(CurByValIdx, RegBegin, RegEnd);

        EVT PtrVT =
            DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
        unsigned int i, j;
        for (i = 0, j = RegBegin; j < RegEnd; i++, j++) {
          SDValue Const = DAG.getConstant(4*i, dl, MVT::i32);
          SDValue AddArg = DAG.getNode(ISD::ADD, dl, PtrVT, Arg, Const);
          SDValue Load = DAG.getLoad(PtrVT, dl, Chain, AddArg,
                                     MachinePointerInfo(),
                                     DAG.InferPtrAlignment(AddArg));
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(j, Load));
        }

        // If parameter size outsides register area, "offset" value
        // helps us to calculate stack slot for remained part properly.
        offset = RegEnd - RegBegin;

        CCInfo.nextInRegsParam();
      }

      if (Flags.getByValSize() > 4*offset) {
        auto PtrVT = getPointerTy(DAG.getDataLayout());
        unsigned LocMemOffset = VA.getLocMemOffset();
        SDValue StkPtrOff = DAG.getIntPtrConstant(LocMemOffset, dl);
        SDValue Dst = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, StkPtrOff);
        SDValue SrcOffset = DAG.getIntPtrConstant(4*offset, dl);
        SDValue Src = DAG.getNode(ISD::ADD, dl, PtrVT, Arg, SrcOffset);
        SDValue SizeNode = DAG.getConstant(Flags.getByValSize() - 4*offset, dl,
                                           MVT::i32);
        SDValue AlignNode = DAG.getConstant(Flags.getByValAlign(), dl,
                                            MVT::i32);

        SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
        SDValue Ops[] = { Chain, Dst, Src, SizeNode, AlignNode};
        MemOpChains.push_back(DAG.getNode(ARMISD::COPY_STRUCT_BYVAL, dl, VTs,
                                          Ops));
      }
    } else if (!isSibCall) {
      assert(VA.isMemLoc());

      MemOpChains.push_back(LowerMemOpCallTo(Chain, StackPtr, Arg,
                                             dl, DAG, VA, Flags));
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InFlag;
  // Tail call byval lowering might overwrite argument registers so in case of
  // tail call optimization the copies to registers are lowered later.
  if (!isTailCall)
    for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
      Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                               RegsToPass[i].second, InFlag);
      InFlag = Chain.getValue(1);
    }

  // For tail calls lower the arguments to the 'real' stack slot.
  if (isTailCall) {
    // Force all the incoming stack arguments to be loaded from the stack
    // before any new outgoing arguments are stored to the stack, because the
    // outgoing stack slots may alias the incoming argument stack slots, and
    // the alias isn't otherwise explicit. This is slightly more conservative
    // than necessary, because it means that each store effectively depends
    // on every argument instead of just those arguments it would clobber.

    // Do not flag preceding copytoreg stuff together with the following stuff.
    InFlag = SDValue();
    for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
      Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                               RegsToPass[i].second, InFlag);
      InFlag = Chain.getValue(1);
    }
    InFlag = SDValue();
  }

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  bool isDirect = false;

  const TargetMachine &TM = getTargetMachine();
  const Module *Mod = MF.getFunction().getParent();
  const GlobalValue *GV = nullptr;
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    GV = G->getGlobal();
  bool isStub =
      !TM.shouldAssumeDSOLocal(*Mod, GV) && Subtarget->isTargetMachO();

  bool isARMFunc = !Subtarget->isThumb() || (isStub && !Subtarget->isMClass());
  bool isLocalARMFunc = false;
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  auto PtrVt = getPointerTy(DAG.getDataLayout());

  if (Subtarget->genLongCalls()) {
    assert((!isPositionIndependent() || Subtarget->isTargetWindows()) &&
           "long-calls codegen is not position independent!");
    // Handle a global address or an external symbol. If it's not one of
    // those, the target's already in a register, so we don't need to do
    // anything extra.
    if (isa<GlobalAddressSDNode>(Callee)) {
      // Create a constant pool entry for the callee address
      unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
      ARMConstantPoolValue *CPV =
        ARMConstantPoolConstant::Create(GV, ARMPCLabelIndex, ARMCP::CPValue, 0);

      // Get the address of the callee into a register
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVt, 4);
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      Callee = DAG.getLoad(
          PtrVt, dl, DAG.getEntryNode(), CPAddr,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
    } else if (ExternalSymbolSDNode *S=dyn_cast<ExternalSymbolSDNode>(Callee)) {
      const char *Sym = S->getSymbol();

      // Create a constant pool entry for the callee address
      unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
      ARMConstantPoolValue *CPV =
        ARMConstantPoolSymbol::Create(*DAG.getContext(), Sym,
                                      ARMPCLabelIndex, 0);
      // Get the address of the callee into a register
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVt, 4);
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      Callee = DAG.getLoad(
          PtrVt, dl, DAG.getEntryNode(), CPAddr,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
    }
  } else if (isa<GlobalAddressSDNode>(Callee)) {
    // If we're optimizing for minimum size and the function is called three or
    // more times in this block, we can improve codesize by calling indirectly
    // as BLXr has a 16-bit encoding.
    auto *GV = cast<GlobalAddressSDNode>(Callee)->getGlobal();
    auto *BB = CLI.CS.getParent();
    bool PreferIndirect =
        Subtarget->isThumb() && MF.getFunction().optForMinSize() &&
        count_if(GV->users(), [&BB](const User *U) {
          return isa<Instruction>(U) && cast<Instruction>(U)->getParent() == BB;
        }) > 2;

    if (!PreferIndirect) {
      isDirect = true;
      bool isDef = GV->isStrongDefinitionForLinker();

      // ARM call to a local ARM function is predicable.
      isLocalARMFunc = !Subtarget->isThumb() && (isDef || !ARMInterworking);
      // tBX takes a register source operand.
      if (isStub && Subtarget->isThumb1Only() && !Subtarget->hasV5TOps()) {
        assert(Subtarget->isTargetMachO() && "WrapperPIC use on non-MachO?");
        Callee = DAG.getNode(
            ARMISD::WrapperPIC, dl, PtrVt,
            DAG.getTargetGlobalAddress(GV, dl, PtrVt, 0, ARMII::MO_NONLAZY));
        Callee = DAG.getLoad(
            PtrVt, dl, DAG.getEntryNode(), Callee,
            MachinePointerInfo::getGOT(DAG.getMachineFunction()),
            /* Alignment = */ 0, MachineMemOperand::MODereferenceable |
                                     MachineMemOperand::MOInvariant);
      } else if (Subtarget->isTargetCOFF()) {
        assert(Subtarget->isTargetWindows() &&
               "Windows is the only supported COFF target");
        unsigned TargetFlags = GV->hasDLLImportStorageClass()
                                   ? ARMII::MO_DLLIMPORT
                                   : ARMII::MO_NO_FLAG;
        Callee = DAG.getTargetGlobalAddress(GV, dl, PtrVt, /*Offset=*/0,
                                            TargetFlags);
        if (GV->hasDLLImportStorageClass())
          Callee =
              DAG.getLoad(PtrVt, dl, DAG.getEntryNode(),
                          DAG.getNode(ARMISD::Wrapper, dl, PtrVt, Callee),
                          MachinePointerInfo::getGOT(DAG.getMachineFunction()));
      } else {
        Callee = DAG.getTargetGlobalAddress(GV, dl, PtrVt, 0, 0);
      }
    }
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    isDirect = true;
    // tBX takes a register source operand.
    const char *Sym = S->getSymbol();
    if (isARMFunc && Subtarget->isThumb1Only() && !Subtarget->hasV5TOps()) {
      unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
      ARMConstantPoolValue *CPV =
        ARMConstantPoolSymbol::Create(*DAG.getContext(), Sym,
                                      ARMPCLabelIndex, 4);
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVt, 4);
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      Callee = DAG.getLoad(
          PtrVt, dl, DAG.getEntryNode(), CPAddr,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
      SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
      Callee = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVt, Callee, PICLabel);
    } else {
      Callee = DAG.getTargetExternalSymbol(Sym, PtrVt, 0);
    }
  }

  // FIXME: handle tail calls differently.
  unsigned CallOpc;
  if (Subtarget->isThumb()) {
    if ((!isDirect || isARMFunc) && !Subtarget->hasV5TOps())
      CallOpc = ARMISD::CALL_NOLINK;
    else
      CallOpc = ARMISD::CALL;
  } else {
    if (!isDirect && !Subtarget->hasV5TOps())
      CallOpc = ARMISD::CALL_NOLINK;
    else if (doesNotRet && isDirect && Subtarget->hasRetAddrStack() &&
             // Emit regular call when code size is the priority
             !MF.getFunction().optForMinSize())
      // "mov lr, pc; b _foo" to avoid confusing the RSP
      CallOpc = ARMISD::CALL_NOLINK;
    else
      CallOpc = isLocalARMFunc ? ARMISD::CALL_PRED : ARMISD::CALL;
  }

  std::vector<SDValue> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  if (!isTailCall) {
    const uint32_t *Mask;
    const ARMBaseRegisterInfo *ARI = Subtarget->getRegisterInfo();
    if (isThisReturn) {
      // For 'this' returns, use the R0-preserving mask if applicable
      Mask = ARI->getThisReturnPreservedMask(MF, CallConv);
      if (!Mask) {
        // Set isThisReturn to false if the calling convention is not one that
        // allows 'returned' to be modeled in this way, so LowerCallResult does
        // not try to pass 'this' straight through
        isThisReturn = false;
        Mask = ARI->getCallPreservedMask(MF, CallConv);
      }
    } else
      Mask = ARI->getCallPreservedMask(MF, CallConv);

    assert(Mask && "Missing call preserved mask for calling convention");
    Ops.push_back(DAG.getRegisterMask(Mask));
  }

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  if (isTailCall) {
    MF.getFrameInfo().setHasTailCall();
    return DAG.getNode(ARMISD::TC_RETURN, dl, NodeTys, Ops);
  }

  // Returns a chain and a flag for retval copy to use.
  Chain = DAG.getNode(CallOpc, dl, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(NumBytes, dl, true),
                             DAG.getIntPtrConstant(0, dl, true), InFlag, dl);
  if (!Ins.empty())
    InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, isVarArg, Ins, dl, DAG,
                         InVals, isThisReturn,
                         isThisReturn ? OutVals[0] : SDValue());
}

/// HandleByVal - Every parameter *after* a byval parameter is passed
/// on the stack.  Remember the next parameter register to allocate,
/// and then confiscate the rest of the parameter registers to insure
/// this.
void ARMTargetLowering::HandleByVal(CCState *State, unsigned &Size,
                                    unsigned Align) const {
  // Byval (as with any stack) slots are always at least 4 byte aligned.
  Align = std::max(Align, 4U);

  unsigned Reg = State->AllocateReg(GPRArgRegs);
  if (!Reg)
    return;

  unsigned AlignInRegs = Align / 4;
  unsigned Waste = (ARM::R4 - Reg) % AlignInRegs;
  for (unsigned i = 0; i < Waste; ++i)
    Reg = State->AllocateReg(GPRArgRegs);

  if (!Reg)
    return;

  unsigned Excess = 4 * (ARM::R4 - Reg);

  // Special case when NSAA != SP and parameter size greater than size of
  // all remained GPR regs. In that case we can't split parameter, we must
  // send it to stack. We also must set NCRN to R4, so waste all
  // remained registers.
  const unsigned NSAAOffset = State->getNextStackOffset();
  if (NSAAOffset != 0 && Size > Excess) {
    while (State->AllocateReg(GPRArgRegs))
      ;
    return;
  }

  // First register for byval parameter is the first register that wasn't
  // allocated before this method call, so it would be "reg".
  // If parameter is small enough to be saved in range [reg, r4), then
  // the end (first after last) register would be reg + param-size-in-regs,
  // else parameter would be splitted between registers and stack,
  // end register would be r4 in this case.
  unsigned ByValRegBegin = Reg;
  unsigned ByValRegEnd = std::min<unsigned>(Reg + Size / 4, ARM::R4);
  State->addInRegsParamInfo(ByValRegBegin, ByValRegEnd);
  // Note, first register is allocated in the beginning of function already,
  // allocate remained amount of registers we need.
  for (unsigned i = Reg + 1; i != ByValRegEnd; ++i)
    State->AllocateReg(GPRArgRegs);
  // A byval parameter that is split between registers and memory needs its
  // size truncated here.
  // In the case where the entire structure fits in registers, we set the
  // size in memory to zero.
  Size = std::max<int>(Size - Excess, 0);
}

/// MatchingStackOffset - Return true if the given stack call argument is
/// already available in the same position (relatively) of the caller's
/// incoming argument stack.
static
bool MatchingStackOffset(SDValue Arg, unsigned Offset, ISD::ArgFlagsTy Flags,
                         MachineFrameInfo &MFI, const MachineRegisterInfo *MRI,
                         const TargetInstrInfo *TII) {
  unsigned Bytes = Arg.getValueSizeInBits() / 8;
  int FI = std::numeric_limits<int>::max();
  if (Arg.getOpcode() == ISD::CopyFromReg) {
    unsigned VR = cast<RegisterSDNode>(Arg.getOperand(1))->getReg();
    if (!TargetRegisterInfo::isVirtualRegister(VR))
      return false;
    MachineInstr *Def = MRI->getVRegDef(VR);
    if (!Def)
      return false;
    if (!Flags.isByVal()) {
      if (!TII->isLoadFromStackSlot(*Def, FI))
        return false;
    } else {
      return false;
    }
  } else if (LoadSDNode *Ld = dyn_cast<LoadSDNode>(Arg)) {
    if (Flags.isByVal())
      // ByVal argument is passed in as a pointer but it's now being
      // dereferenced. e.g.
      // define @foo(%struct.X* %A) {
      //   tail call @bar(%struct.X* byval %A)
      // }
      return false;
    SDValue Ptr = Ld->getBasePtr();
    FrameIndexSDNode *FINode = dyn_cast<FrameIndexSDNode>(Ptr);
    if (!FINode)
      return false;
    FI = FINode->getIndex();
  } else
    return false;

  assert(FI != std::numeric_limits<int>::max());
  if (!MFI.isFixedObjectIndex(FI))
    return false;
  return Offset == MFI.getObjectOffset(FI) && Bytes == MFI.getObjectSize(FI);
}

/// IsEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization. Targets which want to do tail call
/// optimization should implement this function.
bool
ARMTargetLowering::IsEligibleForTailCallOptimization(SDValue Callee,
                                                     CallingConv::ID CalleeCC,
                                                     bool isVarArg,
                                                     bool isCalleeStructRet,
                                                     bool isCallerStructRet,
                                    const SmallVectorImpl<ISD::OutputArg> &Outs,
                                    const SmallVectorImpl<SDValue> &OutVals,
                                    const SmallVectorImpl<ISD::InputArg> &Ins,
                                                     SelectionDAG& DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const Function &CallerF = MF.getFunction();
  CallingConv::ID CallerCC = CallerF.getCallingConv();

  assert(Subtarget->supportsTailCall());

  // Tail calls to function pointers cannot be optimized for Thumb1 if the args
  // to the call take up r0-r3. The reason is that there are no legal registers
  // left to hold the pointer to the function to be called.
  if (Subtarget->isThumb1Only() && Outs.size() >= 4 &&
      !isa<GlobalAddressSDNode>(Callee.getNode()))
      return false;

  // Look for obvious safe cases to perform tail call optimization that do not
  // require ABI changes. This is what gcc calls sibcall.

  // Exception-handling functions need a special set of instructions to indicate
  // a return to the hardware. Tail-calling another function would probably
  // break this.
  if (CallerF.hasFnAttribute("interrupt"))
    return false;

  // Also avoid sibcall optimization if either caller or callee uses struct
  // return semantics.
  if (isCalleeStructRet || isCallerStructRet)
    return false;

  // Externally-defined functions with weak linkage should not be
  // tail-called on ARM when the OS does not support dynamic
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

  // Check that the call results are passed in the same way.
  LLVMContext &C = *DAG.getContext();
  if (!CCState::resultsCompatible(CalleeCC, CallerCC, MF, C, Ins,
                                  CCAssignFnForReturn(CalleeCC, isVarArg),
                                  CCAssignFnForReturn(CallerCC, isVarArg)))
    return false;
  // The callee has to preserve all registers the caller needs to preserve.
  const ARMBaseRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  if (CalleeCC != CallerCC) {
    const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
    if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
      return false;
  }

  // If Caller's vararg or byval argument has been split between registers and
  // stack, do not perform tail call, since part of the argument is in caller's
  // local frame.
  const ARMFunctionInfo *AFI_Caller = MF.getInfo<ARMFunctionInfo>();
  if (AFI_Caller->getArgRegsSaveSize())
    return false;

  // If the callee takes no arguments then go on to check the results of the
  // call.
  if (!Outs.empty()) {
    // Check if stack adjustment is needed. For now, do not do this if any
    // argument is passed on the stack.
    SmallVector<CCValAssign, 16> ArgLocs;
    CCState CCInfo(CalleeCC, isVarArg, MF, ArgLocs, C);
    CCInfo.AnalyzeCallOperands(Outs, CCAssignFnForCall(CalleeCC, isVarArg));
    if (CCInfo.getNextStackOffset()) {
      // Check if the arguments are already laid out in the right way as
      // the caller's fixed stack objects.
      MachineFrameInfo &MFI = MF.getFrameInfo();
      const MachineRegisterInfo *MRI = &MF.getRegInfo();
      const TargetInstrInfo *TII = Subtarget->getInstrInfo();
      for (unsigned i = 0, realArgIdx = 0, e = ArgLocs.size();
           i != e;
           ++i, ++realArgIdx) {
        CCValAssign &VA = ArgLocs[i];
        EVT RegVT = VA.getLocVT();
        SDValue Arg = OutVals[realArgIdx];
        ISD::ArgFlagsTy Flags = Outs[realArgIdx].Flags;
        if (VA.getLocInfo() == CCValAssign::Indirect)
          return false;
        if (VA.needsCustom()) {
          // f64 and vector types are split into multiple registers or
          // register/stack-slot combinations.  The types will not match
          // the registers; give up on memory f64 refs until we figure
          // out what to do about this.
          if (!VA.isRegLoc())
            return false;
          if (!ArgLocs[++i].isRegLoc())
            return false;
          if (RegVT == MVT::v2f64) {
            if (!ArgLocs[++i].isRegLoc())
              return false;
            if (!ArgLocs[++i].isRegLoc())
              return false;
          }
        } else if (!VA.isRegLoc()) {
          if (!MatchingStackOffset(Arg, VA.getLocMemOffset(), Flags,
                                   MFI, MRI, TII))
            return false;
        }
      }
    }

    const MachineRegisterInfo &MRI = MF.getRegInfo();
    if (!parametersInCSRMatch(MRI, CallerPreserved, ArgLocs, OutVals))
      return false;
  }

  return true;
}

bool
ARMTargetLowering::CanLowerReturn(CallingConv::ID CallConv,
                                  MachineFunction &MF, bool isVarArg,
                                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                                  LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, CCAssignFnForReturn(CallConv, isVarArg));
}

static SDValue LowerInterruptReturn(SmallVectorImpl<SDValue> &RetOps,
                                    const SDLoc &DL, SelectionDAG &DAG) {
  const MachineFunction &MF = DAG.getMachineFunction();
  const Function &F = MF.getFunction();

  StringRef IntKind = F.getFnAttribute("interrupt").getValueAsString();

  // See ARM ARM v7 B1.8.3. On exception entry LR is set to a possibly offset
  // version of the "preferred return address". These offsets affect the return
  // instruction if this is a return from PL1 without hypervisor extensions.
  //    IRQ/FIQ: +4     "subs pc, lr, #4"
  //    SWI:     0      "subs pc, lr, #0"
  //    ABORT:   +4     "subs pc, lr, #4"
  //    UNDEF:   +4/+2  "subs pc, lr, #0"
  // UNDEF varies depending on where the exception came from ARM or Thumb
  // mode. Alongside GCC, we throw our hands up in disgust and pretend it's 0.

  int64_t LROffset;
  if (IntKind == "" || IntKind == "IRQ" || IntKind == "FIQ" ||
      IntKind == "ABORT")
    LROffset = 4;
  else if (IntKind == "SWI" || IntKind == "UNDEF")
    LROffset = 0;
  else
    report_fatal_error("Unsupported interrupt attribute. If present, value "
                       "must be one of: IRQ, FIQ, SWI, ABORT or UNDEF");

  RetOps.insert(RetOps.begin() + 1,
                DAG.getConstant(LROffset, DL, MVT::i32, false));

  return DAG.getNode(ARMISD::INTRET_FLAG, DL, MVT::Other, RetOps);
}

SDValue
ARMTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &dl, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of the return value to a location.
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slots.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analyze outgoing return values.
  CCInfo.AnalyzeReturn(Outs, CCAssignFnForReturn(CallConv, isVarArg));

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps;
  RetOps.push_back(Chain); // Operand #0 = Chain (updated below)
  bool isLittleEndian = Subtarget->isLittle();

  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  AFI->setReturnRegsCount(RVLocs.size());

  // Copy the result values into the output registers.
  for (unsigned i = 0, realRVLocIdx = 0;
       i != RVLocs.size();
       ++i, ++realRVLocIdx) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    SDValue Arg = OutVals[realRVLocIdx];
    bool ReturnF16 = false;

    if (Subtarget->hasFullFP16() && Subtarget->isTargetHardFloat()) {
      // Half-precision return values can be returned like this:
      //
      // t11 f16 = fadd ...
      // t12: i16 = bitcast t11
      //   t13: i32 = zero_extend t12
      // t14: f32 = bitcast t13  <~~~~~~~ Arg
      //
      // to avoid code generation for bitcasts, we simply set Arg to the node
      // that produces the f16 value, t11 in this case.
      //
      if (Arg.getValueType() == MVT::f32 && Arg.getOpcode() == ISD::BITCAST) {
        SDValue ZE = Arg.getOperand(0);
        if (ZE.getOpcode() == ISD::ZERO_EXTEND && ZE.getValueType() == MVT::i32) {
          SDValue BC = ZE.getOperand(0);
          if (BC.getOpcode() == ISD::BITCAST && BC.getValueType() == MVT::i16) {
            Arg = BC.getOperand(0);
            ReturnF16 = true;
          }
        }
      }
    }

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::BCvt:
      if (!ReturnF16)
        Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      break;
    }

    if (VA.needsCustom()) {
      if (VA.getLocVT() == MVT::v2f64) {
        // Extract the first half and return it in two registers.
        SDValue Half = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                                   DAG.getConstant(0, dl, MVT::i32));
        SDValue HalfGPRs = DAG.getNode(ARMISD::VMOVRRD, dl,
                                       DAG.getVTList(MVT::i32, MVT::i32), Half);

        Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                                 HalfGPRs.getValue(isLittleEndian ? 0 : 1),
                                 Flag);
        Flag = Chain.getValue(1);
        RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
        VA = RVLocs[++i]; // skip ahead to next loc
        Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                                 HalfGPRs.getValue(isLittleEndian ? 1 : 0),
                                 Flag);
        Flag = Chain.getValue(1);
        RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
        VA = RVLocs[++i]; // skip ahead to next loc

        // Extract the 2nd half and fall through to handle it as an f64 value.
        Arg = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                          DAG.getConstant(1, dl, MVT::i32));
      }
      // Legalize ret f64 -> ret 2 x i32.  We always have fmrrd if f64 is
      // available.
      SDValue fmrrd = DAG.getNode(ARMISD::VMOVRRD, dl,
                                  DAG.getVTList(MVT::i32, MVT::i32), Arg);
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                               fmrrd.getValue(isLittleEndian ? 0 : 1),
                               Flag);
      Flag = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
      VA = RVLocs[++i]; // skip ahead to next loc
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                               fmrrd.getValue(isLittleEndian ? 1 : 0),
                               Flag);
    } else
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Arg, Flag);

    // Guarantee that all emitted copies are
    // stuck together, avoiding something bad.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(),
                                     ReturnF16 ? MVT::f16 : VA.getLocVT()));
  }
  const ARMBaseRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const MCPhysReg *I =
      TRI->getCalleeSavedRegsViaCopy(&DAG.getMachineFunction());
  if (I) {
    for (; *I; ++I) {
      if (ARM::GPRRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::i32));
      else if (ARM::DPRRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::getFloatingPointVT(64)));
      else
        llvm_unreachable("Unexpected register class in CSRsViaCopy!");
    }
  }

  // Update chain and glue.
  RetOps[0] = Chain;
  if (Flag.getNode())
    RetOps.push_back(Flag);

  // CPUs which aren't M-class use a special sequence to return from
  // exceptions (roughly, any instruction setting pc and cpsr simultaneously,
  // though we use "subs pc, lr, #N").
  //
  // M-class CPUs actually use a normal return sequence with a special
  // (hardware-provided) value in LR, so the normal code path works.
  if (DAG.getMachineFunction().getFunction().hasFnAttribute("interrupt") &&
      !Subtarget->isMClass()) {
    if (Subtarget->isThumb1Only())
      report_fatal_error("interrupt attribute is not supported in Thumb1");
    return LowerInterruptReturn(RetOps, dl, DAG);
  }

  return DAG.getNode(ARMISD::RET_FLAG, dl, MVT::Other, RetOps);
}

bool ARMTargetLowering::isUsedByReturnOnly(SDNode *N, SDValue &Chain) const {
  if (N->getNumValues() != 1)
    return false;
  if (!N->hasNUsesOfValue(1, 0))
    return false;

  SDValue TCChain = Chain;
  SDNode *Copy = *N->use_begin();
  if (Copy->getOpcode() == ISD::CopyToReg) {
    // If the copy has a glue operand, we conservatively assume it isn't safe to
    // perform a tail call.
    if (Copy->getOperand(Copy->getNumOperands()-1).getValueType() == MVT::Glue)
      return false;
    TCChain = Copy->getOperand(0);
  } else if (Copy->getOpcode() == ARMISD::VMOVRRD) {
    SDNode *VMov = Copy;
    // f64 returned in a pair of GPRs.
    SmallPtrSet<SDNode*, 2> Copies;
    for (SDNode::use_iterator UI = VMov->use_begin(), UE = VMov->use_end();
         UI != UE; ++UI) {
      if (UI->getOpcode() != ISD::CopyToReg)
        return false;
      Copies.insert(*UI);
    }
    if (Copies.size() > 2)
      return false;

    for (SDNode::use_iterator UI = VMov->use_begin(), UE = VMov->use_end();
         UI != UE; ++UI) {
      SDValue UseChain = UI->getOperand(0);
      if (Copies.count(UseChain.getNode()))
        // Second CopyToReg
        Copy = *UI;
      else {
        // We are at the top of this chain.
        // If the copy has a glue operand, we conservatively assume it
        // isn't safe to perform a tail call.
        if (UI->getOperand(UI->getNumOperands()-1).getValueType() == MVT::Glue)
          return false;
        // First CopyToReg
        TCChain = UseChain;
      }
    }
  } else if (Copy->getOpcode() == ISD::BITCAST) {
    // f32 returned in a single GPR.
    if (!Copy->hasOneUse())
      return false;
    Copy = *Copy->use_begin();
    if (Copy->getOpcode() != ISD::CopyToReg || !Copy->hasNUsesOfValue(1, 0))
      return false;
    // If the copy has a glue operand, we conservatively assume it isn't safe to
    // perform a tail call.
    if (Copy->getOperand(Copy->getNumOperands()-1).getValueType() == MVT::Glue)
      return false;
    TCChain = Copy->getOperand(0);
  } else {
    return false;
  }

  bool HasRet = false;
  for (SDNode::use_iterator UI = Copy->use_begin(), UE = Copy->use_end();
       UI != UE; ++UI) {
    if (UI->getOpcode() != ARMISD::RET_FLAG &&
        UI->getOpcode() != ARMISD::INTRET_FLAG)
      return false;
    HasRet = true;
  }

  if (!HasRet)
    return false;

  Chain = TCChain;
  return true;
}

bool ARMTargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  if (!Subtarget->supportsTailCall())
    return false;

  auto Attr =
      CI->getParent()->getParent()->getFnAttribute("disable-tail-calls");
  if (!CI->isTailCall() || Attr.getValueAsString() == "true")
    return false;

  return true;
}

// Trying to write a 64 bit value so need to split into two 32 bit values first,
// and pass the lower and high parts through.
static SDValue LowerWRITE_REGISTER(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  SDValue WriteValue = Op->getOperand(2);

  // This function is only supposed to be called for i64 type argument.
  assert(WriteValue.getValueType() == MVT::i64
          && "LowerWRITE_REGISTER called for non-i64 type argument.");

  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, WriteValue,
                           DAG.getConstant(0, DL, MVT::i32));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, WriteValue,
                           DAG.getConstant(1, DL, MVT::i32));
  SDValue Ops[] = { Op->getOperand(0), Op->getOperand(1), Lo, Hi };
  return DAG.getNode(ISD::WRITE_REGISTER, DL, MVT::Other, Ops);
}

// ConstantPool, JumpTable, GlobalAddress, and ExternalSymbol are lowered as
// their target counterpart wrapped in the ARMISD::Wrapper node. Suppose N is
// one of the above mentioned nodes. It has to be wrapped because otherwise
// Select(N) returns N. So the raw TargetGlobalAddress nodes, etc. can only
// be used to form addressing mode. These wrapped nodes will be selected
// into MOVi.
SDValue ARMTargetLowering::LowerConstantPool(SDValue Op,
                                             SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  // FIXME there is no actual debug info here
  SDLoc dl(Op);
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
  SDValue Res;

  // When generating execute-only code Constant Pools must be promoted to the
  // global data section. It's a bit ugly that we can't share them across basic
  // blocks, but this way we guarantee that execute-only behaves correct with
  // position-independent addressing modes.
  if (Subtarget->genExecuteOnly()) {
    auto AFI = DAG.getMachineFunction().getInfo<ARMFunctionInfo>();
    auto T = const_cast<Type*>(CP->getType());
    auto C = const_cast<Constant*>(CP->getConstVal());
    auto M = const_cast<Module*>(DAG.getMachineFunction().
                                 getFunction().getParent());
    auto GV = new GlobalVariable(
                    *M, T, /*isConst=*/true, GlobalVariable::InternalLinkage, C,
                    Twine(DAG.getDataLayout().getPrivateGlobalPrefix()) + "CP" +
                    Twine(DAG.getMachineFunction().getFunctionNumber()) + "_" +
                    Twine(AFI->createPICLabelUId())
                  );
    SDValue GA = DAG.getTargetGlobalAddress(dyn_cast<GlobalValue>(GV),
                                            dl, PtrVT);
    return LowerGlobalAddress(GA, DAG);
  }

  if (CP->isMachineConstantPoolEntry())
    Res = DAG.getTargetConstantPool(CP->getMachineCPVal(), PtrVT,
                                    CP->getAlignment());
  else
    Res = DAG.getTargetConstantPool(CP->getConstVal(), PtrVT,
                                    CP->getAlignment());
  return DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Res);
}

unsigned ARMTargetLowering::getJumpTableEncoding() const {
  return MachineJumpTableInfo::EK_Inline;
}

SDValue ARMTargetLowering::LowerBlockAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  unsigned ARMPCLabelIndex = 0;
  SDLoc DL(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  SDValue CPAddr;
  bool IsPositionIndependent = isPositionIndependent() || Subtarget->isROPI();
  if (!IsPositionIndependent) {
    CPAddr = DAG.getTargetConstantPool(BA, PtrVT, 4);
  } else {
    unsigned PCAdj = Subtarget->isThumb() ? 4 : 8;
    ARMPCLabelIndex = AFI->createPICLabelUId();
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(BA, ARMPCLabelIndex,
                                      ARMCP::CPBlockAddress, PCAdj);
    CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, 4);
  }
  CPAddr = DAG.getNode(ARMISD::Wrapper, DL, PtrVT, CPAddr);
  SDValue Result = DAG.getLoad(
      PtrVT, DL, DAG.getEntryNode(), CPAddr,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  if (!IsPositionIndependent)
    return Result;
  SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, DL, MVT::i32);
  return DAG.getNode(ARMISD::PIC_ADD, DL, PtrVT, Result, PICLabel);
}

/// Convert a TLS address reference into the correct sequence of loads
/// and calls to compute the variable's address for Darwin, and return an
/// SDValue containing the final node.

/// Darwin only has one TLS scheme which must be capable of dealing with the
/// fully general situation, in the worst case. This means:
///     + "extern __thread" declaration.
///     + Defined in a possibly unknown dynamic library.
///
/// The general system is that each __thread variable has a [3 x i32] descriptor
/// which contains information used by the runtime to calculate the address. The
/// only part of this the compiler needs to know about is the first word, which
/// contains a function pointer that must be called with the address of the
/// entire descriptor in "r0".
///
/// Since this descriptor may be in a different unit, in general access must
/// proceed along the usual ARM rules. A common sequence to produce is:
///
///     movw rT1, :lower16:_var$non_lazy_ptr
///     movt rT1, :upper16:_var$non_lazy_ptr
///     ldr r0, [rT1]
///     ldr rT2, [r0]
///     blx rT2
///     [...address now in r0...]
SDValue
ARMTargetLowering::LowerGlobalTLSAddressDarwin(SDValue Op,
                                               SelectionDAG &DAG) const {
  assert(Subtarget->isTargetDarwin() &&
         "This function expects a Darwin target");
  SDLoc DL(Op);

  // First step is to get the address of the actua global symbol. This is where
  // the TLS descriptor lives.
  SDValue DescAddr = LowerGlobalAddressDarwin(Op, DAG);

  // The first entry in the descriptor is a function pointer that we must call
  // to obtain the address of the variable.
  SDValue Chain = DAG.getEntryNode();
  SDValue FuncTLVGet = DAG.getLoad(
      MVT::i32, DL, Chain, DescAddr,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()),
      /* Alignment = */ 4,
      MachineMemOperand::MONonTemporal | MachineMemOperand::MODereferenceable |
          MachineMemOperand::MOInvariant);
  Chain = FuncTLVGet.getValue(1);

  MachineFunction &F = DAG.getMachineFunction();
  MachineFrameInfo &MFI = F.getFrameInfo();
  MFI.setAdjustsStack(true);

  // TLS calls preserve all registers except those that absolutely must be
  // trashed: R0 (it takes an argument), LR (it's a call) and CPSR (let's not be
  // silly).
  auto TRI =
      getTargetMachine().getSubtargetImpl(F.getFunction())->getRegisterInfo();
  auto ARI = static_cast<const ARMRegisterInfo *>(TRI);
  const uint32_t *Mask = ARI->getTLSCallPreservedMask(DAG.getMachineFunction());

  // Finally, we can make the call. This is just a degenerate version of a
  // normal AArch64 call node: r0 takes the address of the descriptor, and
  // returns the address of the variable in this thread.
  Chain = DAG.getCopyToReg(Chain, DL, ARM::R0, DescAddr, SDValue());
  Chain =
      DAG.getNode(ARMISD::CALL, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                  Chain, FuncTLVGet, DAG.getRegister(ARM::R0, MVT::i32),
                  DAG.getRegisterMask(Mask), Chain.getValue(1));
  return DAG.getCopyFromReg(Chain, DL, ARM::R0, MVT::i32, Chain.getValue(1));
}

SDValue
ARMTargetLowering::LowerGlobalTLSAddressWindows(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() && "Windows specific TLS lowering");

  SDValue Chain = DAG.getEntryNode();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(Op);

  // Load the current TEB (thread environment block)
  SDValue Ops[] = {Chain,
                   DAG.getConstant(Intrinsic::arm_mrc, DL, MVT::i32),
                   DAG.getConstant(15, DL, MVT::i32),
                   DAG.getConstant(0, DL, MVT::i32),
                   DAG.getConstant(13, DL, MVT::i32),
                   DAG.getConstant(0, DL, MVT::i32),
                   DAG.getConstant(2, DL, MVT::i32)};
  SDValue CurrentTEB = DAG.getNode(ISD::INTRINSIC_W_CHAIN, DL,
                                   DAG.getVTList(MVT::i32, MVT::Other), Ops);

  SDValue TEB = CurrentTEB.getValue(0);
  Chain = CurrentTEB.getValue(1);

  // Load the ThreadLocalStoragePointer from the TEB
  // A pointer to the TLS array is located at offset 0x2c from the TEB.
  SDValue TLSArray =
      DAG.getNode(ISD::ADD, DL, PtrVT, TEB, DAG.getIntPtrConstant(0x2c, DL));
  TLSArray = DAG.getLoad(PtrVT, DL, Chain, TLSArray, MachinePointerInfo());

  // The pointer to the thread's TLS data area is at the TLS Index scaled by 4
  // offset into the TLSArray.

  // Load the TLS index from the C runtime
  SDValue TLSIndex =
      DAG.getTargetExternalSymbol("_tls_index", PtrVT, ARMII::MO_NO_FLAG);
  TLSIndex = DAG.getNode(ARMISD::Wrapper, DL, PtrVT, TLSIndex);
  TLSIndex = DAG.getLoad(PtrVT, DL, Chain, TLSIndex, MachinePointerInfo());

  SDValue Slot = DAG.getNode(ISD::SHL, DL, PtrVT, TLSIndex,
                              DAG.getConstant(2, DL, MVT::i32));
  SDValue TLS = DAG.getLoad(PtrVT, DL, Chain,
                            DAG.getNode(ISD::ADD, DL, PtrVT, TLSArray, Slot),
                            MachinePointerInfo());

  // Get the offset of the start of the .tls section (section base)
  const auto *GA = cast<GlobalAddressSDNode>(Op);
  auto *CPV = ARMConstantPoolConstant::Create(GA->getGlobal(), ARMCP::SECREL);
  SDValue Offset = DAG.getLoad(
      PtrVT, DL, Chain, DAG.getNode(ARMISD::Wrapper, DL, MVT::i32,
                                    DAG.getTargetConstantPool(CPV, PtrVT, 4)),
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));

  return DAG.getNode(ISD::ADD, DL, PtrVT, TLS, Offset);
}

// Lower ISD::GlobalTLSAddress using the "general dynamic" model
SDValue
ARMTargetLowering::LowerToTLSGeneralDynamicModel(GlobalAddressSDNode *GA,
                                                 SelectionDAG &DAG) const {
  SDLoc dl(GA);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  unsigned char PCAdj = Subtarget->isThumb() ? 4 : 8;
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
  ARMConstantPoolValue *CPV =
    ARMConstantPoolConstant::Create(GA->getGlobal(), ARMPCLabelIndex,
                                    ARMCP::CPValue, PCAdj, ARMCP::TLSGD, true);
  SDValue Argument = DAG.getTargetConstantPool(CPV, PtrVT, 4);
  Argument = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Argument);
  Argument = DAG.getLoad(
      PtrVT, dl, DAG.getEntryNode(), Argument,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  SDValue Chain = Argument.getValue(1);

  SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
  Argument = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Argument, PICLabel);

  // call __tls_get_addr.
  ArgListTy Args;
  ArgListEntry Entry;
  Entry.Node = Argument;
  Entry.Ty = (Type *) Type::getInt32Ty(*DAG.getContext());
  Args.push_back(Entry);

  // FIXME: is there useful debug info available here?
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(Chain).setLibCallee(
      CallingConv::C, Type::getInt32Ty(*DAG.getContext()),
      DAG.getExternalSymbol("__tls_get_addr", PtrVT), std::move(Args));

  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);
  return CallResult.first;
}

// Lower ISD::GlobalTLSAddress using the "initial exec" or
// "local exec" model.
SDValue
ARMTargetLowering::LowerToTLSExecModels(GlobalAddressSDNode *GA,
                                        SelectionDAG &DAG,
                                        TLSModel::Model model) const {
  const GlobalValue *GV = GA->getGlobal();
  SDLoc dl(GA);
  SDValue Offset;
  SDValue Chain = DAG.getEntryNode();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  // Get the Thread Pointer
  SDValue ThreadPointer = DAG.getNode(ARMISD::THREAD_POINTER, dl, PtrVT);

  if (model == TLSModel::InitialExec) {
    MachineFunction &MF = DAG.getMachineFunction();
    ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
    unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
    // Initial exec model.
    unsigned char PCAdj = Subtarget->isThumb() ? 4 : 8;
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(GA->getGlobal(), ARMPCLabelIndex,
                                      ARMCP::CPValue, PCAdj, ARMCP::GOTTPOFF,
                                      true);
    Offset = DAG.getTargetConstantPool(CPV, PtrVT, 4);
    Offset = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Offset);
    Offset = DAG.getLoad(
        PtrVT, dl, Chain, Offset,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
    Chain = Offset.getValue(1);

    SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
    Offset = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Offset, PICLabel);

    Offset = DAG.getLoad(
        PtrVT, dl, Chain, Offset,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  } else {
    // local exec model
    assert(model == TLSModel::LocalExec);
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(GV, ARMCP::TPOFF);
    Offset = DAG.getTargetConstantPool(CPV, PtrVT, 4);
    Offset = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Offset);
    Offset = DAG.getLoad(
        PtrVT, dl, Chain, Offset,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  }

  // The address of the thread local variable is the add of the thread
  // pointer with the offset of the variable.
  return DAG.getNode(ISD::ADD, dl, PtrVT, ThreadPointer, Offset);
}

SDValue
ARMTargetLowering::LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const {
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(GA, DAG);

  if (Subtarget->isTargetDarwin())
    return LowerGlobalTLSAddressDarwin(Op, DAG);

  if (Subtarget->isTargetWindows())
    return LowerGlobalTLSAddressWindows(Op, DAG);

  // TODO: implement the "local dynamic" model
  assert(Subtarget->isTargetELF() && "Only ELF implemented here");
  TLSModel::Model model = getTargetMachine().getTLSModel(GA->getGlobal());

  switch (model) {
    case TLSModel::GeneralDynamic:
    case TLSModel::LocalDynamic:
      return LowerToTLSGeneralDynamicModel(GA, DAG);
    case TLSModel::InitialExec:
    case TLSModel::LocalExec:
      return LowerToTLSExecModels(GA, DAG, model);
  }
  llvm_unreachable("bogus TLS model");
}

/// Return true if all users of V are within function F, looking through
/// ConstantExprs.
static bool allUsersAreInFunction(const Value *V, const Function *F) {
  SmallVector<const User*,4> Worklist;
  for (auto *U : V->users())
    Worklist.push_back(U);
  while (!Worklist.empty()) {
    auto *U = Worklist.pop_back_val();
    if (isa<ConstantExpr>(U)) {
      for (auto *UU : U->users())
        Worklist.push_back(UU);
      continue;
    }

    auto *I = dyn_cast<Instruction>(U);
    if (!I || I->getParent()->getParent() != F)
      return false;
  }
  return true;
}

static SDValue promoteToConstantPool(const ARMTargetLowering *TLI,
                                     const GlobalValue *GV, SelectionDAG &DAG,
                                     EVT PtrVT, const SDLoc &dl) {
  // If we're creating a pool entry for a constant global with unnamed address,
  // and the global is small enough, we can emit it inline into the constant pool
  // to save ourselves an indirection.
  //
  // This is a win if the constant is only used in one function (so it doesn't
  // need to be duplicated) or duplicating the constant wouldn't increase code
  // size (implying the constant is no larger than 4 bytes).
  const Function &F = DAG.getMachineFunction().getFunction();

  // We rely on this decision to inline being idemopotent and unrelated to the
  // use-site. We know that if we inline a variable at one use site, we'll
  // inline it elsewhere too (and reuse the constant pool entry). Fast-isel
  // doesn't know about this optimization, so bail out if it's enabled else
  // we could decide to inline here (and thus never emit the GV) but require
  // the GV from fast-isel generated code.
  if (!EnableConstpoolPromotion ||
      DAG.getMachineFunction().getTarget().Options.EnableFastISel)
      return SDValue();

  auto *GVar = dyn_cast<GlobalVariable>(GV);
  if (!GVar || !GVar->hasInitializer() ||
      !GVar->isConstant() || !GVar->hasGlobalUnnamedAddr() ||
      !GVar->hasLocalLinkage())
    return SDValue();

  // If we inline a value that contains relocations, we move the relocations
  // from .data to .text. This is not allowed in position-independent code.
  auto *Init = GVar->getInitializer();
  if ((TLI->isPositionIndependent() || TLI->getSubtarget()->isROPI()) &&
      Init->needsRelocation())
    return SDValue();

  // The constant islands pass can only really deal with alignment requests
  // <= 4 bytes and cannot pad constants itself. Therefore we cannot promote
  // any type wanting greater alignment requirements than 4 bytes. We also
  // can only promote constants that are multiples of 4 bytes in size or
  // are paddable to a multiple of 4. Currently we only try and pad constants
  // that are strings for simplicity.
  auto *CDAInit = dyn_cast<ConstantDataArray>(Init);
  unsigned Size = DAG.getDataLayout().getTypeAllocSize(Init->getType());
  unsigned Align = DAG.getDataLayout().getPreferredAlignment(GVar);
  unsigned RequiredPadding = 4 - (Size % 4);
  bool PaddingPossible =
    RequiredPadding == 4 || (CDAInit && CDAInit->isString());
  if (!PaddingPossible || Align > 4 || Size > ConstpoolPromotionMaxSize ||
      Size == 0)
    return SDValue();

  unsigned PaddedSize = Size + ((RequiredPadding == 4) ? 0 : RequiredPadding);
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // We can't bloat the constant pool too much, else the ConstantIslands pass
  // may fail to converge. If we haven't promoted this global yet (it may have
  // multiple uses), and promoting it would increase the constant pool size (Sz
  // > 4), ensure we have space to do so up to MaxTotal.
  if (!AFI->getGlobalsPromotedToConstantPool().count(GVar) && Size > 4)
    if (AFI->getPromotedConstpoolIncrease() + PaddedSize - 4 >=
        ConstpoolPromotionMaxTotal)
      return SDValue();

  // This is only valid if all users are in a single function; we can't clone
  // the constant in general. The LLVM IR unnamed_addr allows merging
  // constants, but not cloning them.
  //
  // We could potentially allow cloning if we could prove all uses of the
  // constant in the current function don't care about the address, like
  // printf format strings. But that isn't implemented for now.
  if (!allUsersAreInFunction(GVar, &F))
    return SDValue();

  // We're going to inline this global. Pad it out if needed.
  if (RequiredPadding != 4) {
    StringRef S = CDAInit->getAsString();

    SmallVector<uint8_t,16> V(S.size());
    std::copy(S.bytes_begin(), S.bytes_end(), V.begin());
    while (RequiredPadding--)
      V.push_back(0);
    Init = ConstantDataArray::get(*DAG.getContext(), V);
  }

  auto CPVal = ARMConstantPoolConstant::Create(GVar, Init);
  SDValue CPAddr =
    DAG.getTargetConstantPool(CPVal, PtrVT, /*Align=*/4);
  if (!AFI->getGlobalsPromotedToConstantPool().count(GVar)) {
    AFI->markGlobalAsPromotedToConstantPool(GVar);
    AFI->setPromotedConstpoolIncrease(AFI->getPromotedConstpoolIncrease() +
                                      PaddedSize - 4);
  }
  ++NumConstpoolPromoted;
  return DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
}

bool ARMTargetLowering::isReadOnly(const GlobalValue *GV) const {
  if (const GlobalAlias *GA = dyn_cast<GlobalAlias>(GV))
    if (!(GV = GA->getBaseObject()))
      return false;
  if (const auto *V = dyn_cast<GlobalVariable>(GV))
    return V->isConstant();
  return isa<Function>(GV);
}

SDValue ARMTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  switch (Subtarget->getTargetTriple().getObjectFormat()) {
  default: llvm_unreachable("unknown object format");
  case Triple::COFF:
    return LowerGlobalAddressWindows(Op, DAG);
  case Triple::ELF:
    return LowerGlobalAddressELF(Op, DAG);
  case Triple::MachO:
    return LowerGlobalAddressDarwin(Op, DAG);
  }
}

SDValue ARMTargetLowering::LowerGlobalAddressELF(SDValue Op,
                                                 SelectionDAG &DAG) const {
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc dl(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  const TargetMachine &TM = getTargetMachine();
  bool IsRO = isReadOnly(GV);

  // promoteToConstantPool only if not generating XO text section
  if (TM.shouldAssumeDSOLocal(*GV->getParent(), GV) && !Subtarget->genExecuteOnly())
    if (SDValue V = promoteToConstantPool(this, GV, DAG, PtrVT, dl))
      return V;

  if (isPositionIndependent()) {
    bool UseGOT_PREL = !TM.shouldAssumeDSOLocal(*GV->getParent(), GV);
    SDValue G = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                           UseGOT_PREL ? ARMII::MO_GOT : 0);
    SDValue Result = DAG.getNode(ARMISD::WrapperPIC, dl, PtrVT, G);
    if (UseGOT_PREL)
      Result =
          DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), Result,
                      MachinePointerInfo::getGOT(DAG.getMachineFunction()));
    return Result;
  } else if (Subtarget->isROPI() && IsRO) {
    // PC-relative.
    SDValue G = DAG.getTargetGlobalAddress(GV, dl, PtrVT);
    SDValue Result = DAG.getNode(ARMISD::WrapperPIC, dl, PtrVT, G);
    return Result;
  } else if (Subtarget->isRWPI() && !IsRO) {
    // SB-relative.
    SDValue RelAddr;
    if (Subtarget->useMovt(DAG.getMachineFunction())) {
      ++NumMovwMovt;
      SDValue G = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, ARMII::MO_SBREL);
      RelAddr = DAG.getNode(ARMISD::Wrapper, dl, PtrVT, G);
    } else { // use literal pool for address constant
      ARMConstantPoolValue *CPV =
        ARMConstantPoolConstant::Create(GV, ARMCP::SBREL);
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, 4);
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      RelAddr = DAG.getLoad(
          PtrVT, dl, DAG.getEntryNode(), CPAddr,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
    }
    SDValue SB = DAG.getCopyFromReg(DAG.getEntryNode(), dl, ARM::R9, PtrVT);
    SDValue Result = DAG.getNode(ISD::ADD, dl, PtrVT, SB, RelAddr);
    return Result;
  }

  // If we have T2 ops, we can materialize the address directly via movt/movw
  // pair. This is always cheaper.
  if (Subtarget->useMovt(DAG.getMachineFunction())) {
    ++NumMovwMovt;
    // FIXME: Once remat is capable of dealing with instructions with register
    // operands, expand this into two nodes.
    return DAG.getNode(ARMISD::Wrapper, dl, PtrVT,
                       DAG.getTargetGlobalAddress(GV, dl, PtrVT));
  } else {
    SDValue CPAddr = DAG.getTargetConstantPool(GV, PtrVT, 4);
    CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
    return DAG.getLoad(
        PtrVT, dl, DAG.getEntryNode(), CPAddr,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  }
}

SDValue ARMTargetLowering::LowerGlobalAddressDarwin(SDValue Op,
                                                    SelectionDAG &DAG) const {
  assert(!Subtarget->isROPI() && !Subtarget->isRWPI() &&
         "ROPI/RWPI not currently supported for Darwin");
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc dl(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();

  if (Subtarget->useMovt(DAG.getMachineFunction()))
    ++NumMovwMovt;

  // FIXME: Once remat is capable of dealing with instructions with register
  // operands, expand this into multiple nodes
  unsigned Wrapper =
      isPositionIndependent() ? ARMISD::WrapperPIC : ARMISD::Wrapper;

  SDValue G = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, ARMII::MO_NONLAZY);
  SDValue Result = DAG.getNode(Wrapper, dl, PtrVT, G);

  if (Subtarget->isGVIndirectSymbol(GV))
    Result = DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  return Result;
}

SDValue ARMTargetLowering::LowerGlobalAddressWindows(SDValue Op,
                                                     SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() && "non-Windows COFF is not supported");
  assert(Subtarget->useMovt(DAG.getMachineFunction()) &&
         "Windows on ARM expects to use movw/movt");
  assert(!Subtarget->isROPI() && !Subtarget->isRWPI() &&
         "ROPI/RWPI not currently supported for Windows");

  const TargetMachine &TM = getTargetMachine();
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  ARMII::TOF TargetFlags = ARMII::MO_NO_FLAG;
  if (GV->hasDLLImportStorageClass())
    TargetFlags = ARMII::MO_DLLIMPORT;
  else if (!TM.shouldAssumeDSOLocal(*GV->getParent(), GV))
    TargetFlags = ARMII::MO_COFFSTUB;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Result;
  SDLoc DL(Op);

  ++NumMovwMovt;

  // FIXME: Once remat is capable of dealing with instructions with register
  // operands, expand this into two nodes.
  Result = DAG.getNode(ARMISD::Wrapper, DL, PtrVT,
                       DAG.getTargetGlobalAddress(GV, DL, PtrVT, /*Offset=*/0,
                                                  TargetFlags));
  if (TargetFlags & (ARMII::MO_DLLIMPORT | ARMII::MO_COFFSTUB))
    Result = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  return Result;
}

SDValue
ARMTargetLowering::LowerEH_SJLJ_SETJMP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  SDValue Val = DAG.getConstant(0, dl, MVT::i32);
  return DAG.getNode(ARMISD::EH_SJLJ_SETJMP, dl,
                     DAG.getVTList(MVT::i32, MVT::Other), Op.getOperand(0),
                     Op.getOperand(1), Val);
}

SDValue
ARMTargetLowering::LowerEH_SJLJ_LONGJMP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  return DAG.getNode(ARMISD::EH_SJLJ_LONGJMP, dl, MVT::Other, Op.getOperand(0),
                     Op.getOperand(1), DAG.getConstant(0, dl, MVT::i32));
}

SDValue ARMTargetLowering::LowerEH_SJLJ_SETUP_DISPATCH(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDLoc dl(Op);
  return DAG.getNode(ARMISD::EH_SJLJ_SETUP_DISPATCH, dl, MVT::Other,
                     Op.getOperand(0));
}

SDValue
ARMTargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG,
                                          const ARMSubtarget *Subtarget) const {
  unsigned IntNo = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  SDLoc dl(Op);
  switch (IntNo) {
  default: return SDValue();    // Don't custom lower most intrinsics.
  case Intrinsic::thread_pointer: {
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    return DAG.getNode(ARMISD::THREAD_POINTER, dl, PtrVT);
  }
  case Intrinsic::eh_sjlj_lsda: {
    MachineFunction &MF = DAG.getMachineFunction();
    ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
    unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    SDValue CPAddr;
    bool IsPositionIndependent = isPositionIndependent();
    unsigned PCAdj = IsPositionIndependent ? (Subtarget->isThumb() ? 4 : 8) : 0;
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(&MF.getFunction(), ARMPCLabelIndex,
                                      ARMCP::CPLSDA, PCAdj);
    CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, 4);
    CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
    SDValue Result = DAG.getLoad(
        PtrVT, dl, DAG.getEntryNode(), CPAddr,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));

    if (IsPositionIndependent) {
      SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
      Result = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Result, PICLabel);
    }
    return Result;
  }
  case Intrinsic::arm_neon_vabs:
    return DAG.getNode(ISD::ABS, SDLoc(Op), Op.getValueType(),
                        Op.getOperand(1));
  case Intrinsic::arm_neon_vmulls:
  case Intrinsic::arm_neon_vmullu: {
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vmulls)
      ? ARMISD::VMULLs : ARMISD::VMULLu;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vminnm:
  case Intrinsic::arm_neon_vmaxnm: {
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vminnm)
      ? ISD::FMINNUM : ISD::FMAXNUM;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vminu:
  case Intrinsic::arm_neon_vmaxu: {
    if (Op.getValueType().isFloatingPoint())
      return SDValue();
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vminu)
      ? ISD::UMIN : ISD::UMAX;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                         Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vmins:
  case Intrinsic::arm_neon_vmaxs: {
    // v{min,max}s is overloaded between signed integers and floats.
    if (!Op.getValueType().isFloatingPoint()) {
      unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vmins)
        ? ISD::SMIN : ISD::SMAX;
      return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                         Op.getOperand(1), Op.getOperand(2));
    }
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vmins)
      ? ISD::FMINIMUM : ISD::FMAXIMUM;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vtbl1:
    return DAG.getNode(ARMISD::VTBL1, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::arm_neon_vtbl2:
    return DAG.getNode(ARMISD::VTBL2, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  }
}

static SDValue LowerATOMIC_FENCE(SDValue Op, SelectionDAG &DAG,
                                 const ARMSubtarget *Subtarget) {
  SDLoc dl(Op);
  ConstantSDNode *SSIDNode = cast<ConstantSDNode>(Op.getOperand(2));
  auto SSID = static_cast<SyncScope::ID>(SSIDNode->getZExtValue());
  if (SSID == SyncScope::SingleThread)
    return Op;

  if (!Subtarget->hasDataBarrier()) {
    // Some ARMv6 cpus can support data barriers with an mcr instruction.
    // Thumb1 and pre-v6 ARM mode use a libcall instead and should never get
    // here.
    assert(Subtarget->hasV6Ops() && !Subtarget->isThumb() &&
           "Unexpected ISD::ATOMIC_FENCE encountered. Should be libcall!");
    return DAG.getNode(ARMISD::MEMBARRIER_MCR, dl, MVT::Other, Op.getOperand(0),
                       DAG.getConstant(0, dl, MVT::i32));
  }

  ConstantSDNode *OrdN = cast<ConstantSDNode>(Op.getOperand(1));
  AtomicOrdering Ord = static_cast<AtomicOrdering>(OrdN->getZExtValue());
  ARM_MB::MemBOpt Domain = ARM_MB::ISH;
  if (Subtarget->isMClass()) {
    // Only a full system barrier exists in the M-class architectures.
    Domain = ARM_MB::SY;
  } else if (Subtarget->preferISHSTBarriers() &&
             Ord == AtomicOrdering::Release) {
    // Swift happens to implement ISHST barriers in a way that's compatible with
    // Release semantics but weaker than ISH so we'd be fools not to use
    // it. Beware: other processors probably don't!
    Domain = ARM_MB::ISHST;
  }

  return DAG.getNode(ISD::INTRINSIC_VOID, dl, MVT::Other, Op.getOperand(0),
                     DAG.getConstant(Intrinsic::arm_dmb, dl, MVT::i32),
                     DAG.getConstant(Domain, dl, MVT::i32));
}

static SDValue LowerPREFETCH(SDValue Op, SelectionDAG &DAG,
                             const ARMSubtarget *Subtarget) {
  // ARM pre v5TE and Thumb1 does not have preload instructions.
  if (!(Subtarget->isThumb2() ||
        (!Subtarget->isThumb1Only() && Subtarget->hasV5TEOps())))
    // Just preserve the chain.
    return Op.getOperand(0);

  SDLoc dl(Op);
  unsigned isRead = ~cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue() & 1;
  if (!isRead &&
      (!Subtarget->hasV7Ops() || !Subtarget->hasMPExtension()))
    // ARMv7 with MP extension has PLDW.
    return Op.getOperand(0);

  unsigned isData = cast<ConstantSDNode>(Op.getOperand(4))->getZExtValue();
  if (Subtarget->isThumb()) {
    // Invert the bits.
    isRead = ~isRead & 1;
    isData = ~isData & 1;
  }

  return DAG.getNode(ARMISD::PRELOAD, dl, MVT::Other, Op.getOperand(0),
                     Op.getOperand(1), DAG.getConstant(isRead, dl, MVT::i32),
                     DAG.getConstant(isData, dl, MVT::i32));
}

static SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *FuncInfo = MF.getInfo<ARMFunctionInfo>();

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  SDLoc dl(Op);
  EVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), dl, FR, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue ARMTargetLowering::GetF64FormalArgument(CCValAssign &VA,
                                                CCValAssign &NextVA,
                                                SDValue &Root,
                                                SelectionDAG &DAG,
                                                const SDLoc &dl) const {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  const TargetRegisterClass *RC;
  if (AFI->isThumb1OnlyFunction())
    RC = &ARM::tGPRRegClass;
  else
    RC = &ARM::GPRRegClass;

  // Transform the arguments stored in physical registers into virtual ones.
  unsigned Reg = MF.addLiveIn(VA.getLocReg(), RC);
  SDValue ArgValue = DAG.getCopyFromReg(Root, dl, Reg, MVT::i32);

  SDValue ArgValue2;
  if (NextVA.isMemLoc()) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int FI = MFI.CreateFixedObject(4, NextVA.getLocMemOffset(), true);

    // Create load node to retrieve arguments from the stack.
    SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    ArgValue2 = DAG.getLoad(
        MVT::i32, dl, Root, FIN,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));
  } else {
    Reg = MF.addLiveIn(NextVA.getLocReg(), RC);
    ArgValue2 = DAG.getCopyFromReg(Root, dl, Reg, MVT::i32);
  }
  if (!Subtarget->isLittle())
    std::swap (ArgValue, ArgValue2);
  return DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, ArgValue, ArgValue2);
}

// The remaining GPRs hold either the beginning of variable-argument
// data, or the beginning of an aggregate passed by value (usually
// byval).  Either way, we allocate stack slots adjacent to the data
// provided by our caller, and store the unallocated registers there.
// If this is a variadic function, the va_list pointer will begin with
// these values; otherwise, this reassembles a (byval) structure that
// was split between registers and memory.
// Return: The frame index registers were stored into.
int ARMTargetLowering::StoreByValRegs(CCState &CCInfo, SelectionDAG &DAG,
                                      const SDLoc &dl, SDValue &Chain,
                                      const Value *OrigArg,
                                      unsigned InRegsParamRecordIdx,
                                      int ArgOffset, unsigned ArgSize) const {
  // Currently, two use-cases possible:
  // Case #1. Non-var-args function, and we meet first byval parameter.
  //          Setup first unallocated register as first byval register;
  //          eat all remained registers
  //          (these two actions are performed by HandleByVal method).
  //          Then, here, we initialize stack frame with
  //          "store-reg" instructions.
  // Case #2. Var-args function, that doesn't contain byval parameters.
  //          The same: eat all remained unallocated registers,
  //          initialize stack frame.

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  unsigned RBegin, REnd;
  if (InRegsParamRecordIdx < CCInfo.getInRegsParamsCount()) {
    CCInfo.getInRegsParamInfo(InRegsParamRecordIdx, RBegin, REnd);
  } else {
    unsigned RBeginIdx = CCInfo.getFirstUnallocated(GPRArgRegs);
    RBegin = RBeginIdx == 4 ? (unsigned)ARM::R4 : GPRArgRegs[RBeginIdx];
    REnd = ARM::R4;
  }

  if (REnd != RBegin)
    ArgOffset = -4 * (ARM::R4 - RBegin);

  auto PtrVT = getPointerTy(DAG.getDataLayout());
  int FrameIndex = MFI.CreateFixedObject(ArgSize, ArgOffset, false);
  SDValue FIN = DAG.getFrameIndex(FrameIndex, PtrVT);

  SmallVector<SDValue, 4> MemOps;
  const TargetRegisterClass *RC =
      AFI->isThumb1OnlyFunction() ? &ARM::tGPRRegClass : &ARM::GPRRegClass;

  for (unsigned Reg = RBegin, i = 0; Reg < REnd; ++Reg, ++i) {
    unsigned VReg = MF.addLiveIn(Reg, RC);
    SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i32);
    SDValue Store = DAG.getStore(Val.getValue(1), dl, Val, FIN,
                                 MachinePointerInfo(OrigArg, 4 * i));
    MemOps.push_back(Store);
    FIN = DAG.getNode(ISD::ADD, dl, PtrVT, FIN, DAG.getConstant(4, dl, PtrVT));
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);
  return FrameIndex;
}

// Setup stack frame, the va_list pointer will start from.
void ARMTargetLowering::VarArgStyleRegisters(CCState &CCInfo, SelectionDAG &DAG,
                                             const SDLoc &dl, SDValue &Chain,
                                             unsigned ArgOffset,
                                             unsigned TotalArgRegsSaveSize,
                                             bool ForceMutable) const {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // Try to store any remaining integer argument regs
  // to their spots on the stack so that they may be loaded by dereferencing
  // the result of va_next.
  // If there is no regs to be stored, just point address after last
  // argument passed via stack.
  int FrameIndex = StoreByValRegs(CCInfo, DAG, dl, Chain, nullptr,
                                  CCInfo.getInRegsParamsCount(),
                                  CCInfo.getNextStackOffset(), 4);
  AFI->setVarArgsFrameIndex(FrameIndex);
}

SDValue ARMTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CCAssignFnForCall(CallConv, isVarArg));

  SmallVector<SDValue, 16> ArgValues;
  SDValue ArgValue;
  Function::const_arg_iterator CurOrigArg = MF.getFunction().arg_begin();
  unsigned CurArgIdx = 0;

  // Initially ArgRegsSaveSize is zero.
  // Then we increase this value each time we meet byval parameter.
  // We also increase this value in case of varargs function.
  AFI->setArgRegsSaveSize(0);

  // Calculate the amount of stack space that we need to allocate to store
  // byval and variadic arguments that are passed in registers.
  // We need to know this before we allocate the first byval or variadic
  // argument, as they will be allocated a stack slot below the CFA (Canonical
  // Frame Address, the stack pointer at entry to the function).
  unsigned ArgRegBegin = ARM::R4;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    if (CCInfo.getInRegsParamsProcessed() >= CCInfo.getInRegsParamsCount())
      break;

    CCValAssign &VA = ArgLocs[i];
    unsigned Index = VA.getValNo();
    ISD::ArgFlagsTy Flags = Ins[Index].Flags;
    if (!Flags.isByVal())
      continue;

    assert(VA.isMemLoc() && "unexpected byval pointer in reg");
    unsigned RBegin, REnd;
    CCInfo.getInRegsParamInfo(CCInfo.getInRegsParamsProcessed(), RBegin, REnd);
    ArgRegBegin = std::min(ArgRegBegin, RBegin);

    CCInfo.nextInRegsParam();
  }
  CCInfo.rewindByValRegsInfo();

  int lastInsIndex = -1;
  if (isVarArg && MFI.hasVAStart()) {
    unsigned RegIdx = CCInfo.getFirstUnallocated(GPRArgRegs);
    if (RegIdx != array_lengthof(GPRArgRegs))
      ArgRegBegin = std::min(ArgRegBegin, (unsigned)GPRArgRegs[RegIdx]);
  }

  unsigned TotalArgRegsSaveSize = 4 * (ARM::R4 - ArgRegBegin);
  AFI->setArgRegsSaveSize(TotalArgRegsSaveSize);
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    if (Ins[VA.getValNo()].isOrigArg()) {
      std::advance(CurOrigArg,
                   Ins[VA.getValNo()].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[VA.getValNo()].getOrigArgIndex();
    }
    // Arguments stored in registers.
    if (VA.isRegLoc()) {
      EVT RegVT = VA.getLocVT();

      if (VA.needsCustom()) {
        // f64 and vector types are split up into multiple registers or
        // combinations of registers and stack slots.
        if (VA.getLocVT() == MVT::v2f64) {
          SDValue ArgValue1 = GetF64FormalArgument(VA, ArgLocs[++i],
                                                   Chain, DAG, dl);
          VA = ArgLocs[++i]; // skip ahead to next loc
          SDValue ArgValue2;
          if (VA.isMemLoc()) {
            int FI = MFI.CreateFixedObject(8, VA.getLocMemOffset(), true);
            SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
            ArgValue2 = DAG.getLoad(MVT::f64, dl, Chain, FIN,
                                    MachinePointerInfo::getFixedStack(
                                        DAG.getMachineFunction(), FI));
          } else {
            ArgValue2 = GetF64FormalArgument(VA, ArgLocs[++i],
                                             Chain, DAG, dl);
          }
          ArgValue = DAG.getNode(ISD::UNDEF, dl, MVT::v2f64);
          ArgValue = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64,
                                 ArgValue, ArgValue1,
                                 DAG.getIntPtrConstant(0, dl));
          ArgValue = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64,
                                 ArgValue, ArgValue2,
                                 DAG.getIntPtrConstant(1, dl));
        } else
          ArgValue = GetF64FormalArgument(VA, ArgLocs[++i], Chain, DAG, dl);
      } else {
        const TargetRegisterClass *RC;


        if (RegVT == MVT::f16)
          RC = &ARM::HPRRegClass;
        else if (RegVT == MVT::f32)
          RC = &ARM::SPRRegClass;
        else if (RegVT == MVT::f64 || RegVT == MVT::v4f16)
          RC = &ARM::DPRRegClass;
        else if (RegVT == MVT::v2f64 || RegVT == MVT::v8f16)
          RC = &ARM::QPRRegClass;
        else if (RegVT == MVT::i32)
          RC = AFI->isThumb1OnlyFunction() ? &ARM::tGPRRegClass
                                           : &ARM::GPRRegClass;
        else
          llvm_unreachable("RegVT not supported by FORMAL_ARGUMENTS Lowering");

        // Transform the arguments in physical registers into virtual ones.
        unsigned Reg = MF.addLiveIn(VA.getLocReg(), RC);
        ArgValue = DAG.getCopyFromReg(Chain, dl, Reg, RegVT);
      }

      // If this is an 8 or 16-bit value, it is really passed promoted
      // to 32 bits.  Insert an assert[sz]ext to capture this, then
      // truncate to the right size.
      switch (VA.getLocInfo()) {
      default: llvm_unreachable("Unknown loc info!");
      case CCValAssign::Full: break;
      case CCValAssign::BCvt:
        ArgValue = DAG.getNode(ISD::BITCAST, dl, VA.getValVT(), ArgValue);
        break;
      case CCValAssign::SExt:
        ArgValue = DAG.getNode(ISD::AssertSext, dl, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), ArgValue);
        break;
      case CCValAssign::ZExt:
        ArgValue = DAG.getNode(ISD::AssertZext, dl, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), ArgValue);
        break;
      }

      InVals.push_back(ArgValue);
    } else { // VA.isRegLoc()
      // sanity check
      assert(VA.isMemLoc());
      assert(VA.getValVT() != MVT::i64 && "i64 should already be lowered");

      int index = VA.getValNo();

      // Some Ins[] entries become multiple ArgLoc[] entries.
      // Process them only once.
      if (index != lastInsIndex)
        {
          ISD::ArgFlagsTy Flags = Ins[index].Flags;
          // FIXME: For now, all byval parameter objects are marked mutable.
          // This can be changed with more analysis.
          // In case of tail call optimization mark all arguments mutable.
          // Since they could be overwritten by lowering of arguments in case of
          // a tail call.
          if (Flags.isByVal()) {
            assert(Ins[index].isOrigArg() &&
                   "Byval arguments cannot be implicit");
            unsigned CurByValIndex = CCInfo.getInRegsParamsProcessed();

            int FrameIndex = StoreByValRegs(
                CCInfo, DAG, dl, Chain, &*CurOrigArg, CurByValIndex,
                VA.getLocMemOffset(), Flags.getByValSize());
            InVals.push_back(DAG.getFrameIndex(FrameIndex, PtrVT));
            CCInfo.nextInRegsParam();
          } else {
            unsigned FIOffset = VA.getLocMemOffset();
            int FI = MFI.CreateFixedObject(VA.getLocVT().getSizeInBits()/8,
                                           FIOffset, true);

            // Create load nodes to retrieve arguments from the stack.
            SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
            InVals.push_back(DAG.getLoad(VA.getValVT(), dl, Chain, FIN,
                                         MachinePointerInfo::getFixedStack(
                                             DAG.getMachineFunction(), FI)));
          }
          lastInsIndex = index;
        }
    }
  }

  // varargs
  if (isVarArg && MFI.hasVAStart())
    VarArgStyleRegisters(CCInfo, DAG, dl, Chain,
                         CCInfo.getNextStackOffset(),
                         TotalArgRegsSaveSize);

  AFI->setArgumentStackSize(CCInfo.getNextStackOffset());

  return Chain;
}

/// isFloatingPointZero - Return true if this is +0.0.
static bool isFloatingPointZero(SDValue Op) {
  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(Op))
    return CFP->getValueAPF().isPosZero();
  else if (ISD::isEXTLoad(Op.getNode()) || ISD::isNON_EXTLoad(Op.getNode())) {
    // Maybe this has already been legalized into the constant pool?
    if (Op.getOperand(1).getOpcode() == ARMISD::Wrapper) {
      SDValue WrapperOp = Op.getOperand(1).getOperand(0);
      if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(WrapperOp))
        if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CP->getConstVal()))
          return CFP->getValueAPF().isPosZero();
    }
  } else if (Op->getOpcode() == ISD::BITCAST &&
             Op->getValueType(0) == MVT::f64) {
    // Handle (ISD::BITCAST (ARMISD::VMOVIMM (ISD::TargetConstant 0)) MVT::f64)
    // created by LowerConstantFP().
    SDValue BitcastOp = Op->getOperand(0);
    if (BitcastOp->getOpcode() == ARMISD::VMOVIMM &&
        isNullConstant(BitcastOp->getOperand(0)))
      return true;
  }
  return false;
}

/// Returns appropriate ARM CMP (cmp) and corresponding condition code for
/// the given operands.
SDValue ARMTargetLowering::getARMCmp(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                                     SDValue &ARMcc, SelectionDAG &DAG,
                                     const SDLoc &dl) const {
  if (ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS.getNode())) {
    unsigned C = RHSC->getZExtValue();
    if (!isLegalICmpImmediate((int32_t)C)) {
      // Constant does not fit, try adjusting it by one.
      switch (CC) {
      default: break;
      case ISD::SETLT:
      case ISD::SETGE:
        if (C != 0x80000000 && isLegalICmpImmediate(C-1)) {
          CC = (CC == ISD::SETLT) ? ISD::SETLE : ISD::SETGT;
          RHS = DAG.getConstant(C - 1, dl, MVT::i32);
        }
        break;
      case ISD::SETULT:
      case ISD::SETUGE:
        if (C != 0 && isLegalICmpImmediate(C-1)) {
          CC = (CC == ISD::SETULT) ? ISD::SETULE : ISD::SETUGT;
          RHS = DAG.getConstant(C - 1, dl, MVT::i32);
        }
        break;
      case ISD::SETLE:
      case ISD::SETGT:
        if (C != 0x7fffffff && isLegalICmpImmediate(C+1)) {
          CC = (CC == ISD::SETLE) ? ISD::SETLT : ISD::SETGE;
          RHS = DAG.getConstant(C + 1, dl, MVT::i32);
        }
        break;
      case ISD::SETULE:
      case ISD::SETUGT:
        if (C != 0xffffffff && isLegalICmpImmediate(C+1)) {
          CC = (CC == ISD::SETULE) ? ISD::SETULT : ISD::SETUGE;
          RHS = DAG.getConstant(C + 1, dl, MVT::i32);
        }
        break;
      }
    }
  } else if ((ARM_AM::getShiftOpcForNode(LHS.getOpcode()) != ARM_AM::no_shift) &&
             (ARM_AM::getShiftOpcForNode(RHS.getOpcode()) == ARM_AM::no_shift)) {
    // In ARM and Thumb-2, the compare instructions can shift their second
    // operand.
    CC = ISD::getSetCCSwappedOperands(CC);
    std::swap(LHS, RHS);
  }

  ARMCC::CondCodes CondCode = IntCCToARMCC(CC);
  ARMISD::NodeType CompareType;
  switch (CondCode) {
  default:
    CompareType = ARMISD::CMP;
    break;
  case ARMCC::EQ:
  case ARMCC::NE:
    // Uses only Z Flag
    CompareType = ARMISD::CMPZ;
    break;
  }
  ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
  return DAG.getNode(CompareType, dl, MVT::Glue, LHS, RHS);
}

/// Returns a appropriate VFP CMP (fcmp{s|d}+fmstat) for the given operands.
SDValue ARMTargetLowering::getVFPCmp(SDValue LHS, SDValue RHS,
                                     SelectionDAG &DAG, const SDLoc &dl,
                                     bool InvalidOnQNaN) const {
  assert(!Subtarget->isFPOnlySP() || RHS.getValueType() != MVT::f64);
  SDValue Cmp;
  SDValue C = DAG.getConstant(InvalidOnQNaN, dl, MVT::i32);
  if (!isFloatingPointZero(RHS))
    Cmp = DAG.getNode(ARMISD::CMPFP, dl, MVT::Glue, LHS, RHS, C);
  else
    Cmp = DAG.getNode(ARMISD::CMPFPw0, dl, MVT::Glue, LHS, C);
  return DAG.getNode(ARMISD::FMSTAT, dl, MVT::Glue, Cmp);
}

/// duplicateCmp - Glue values can have only one use, so this function
/// duplicates a comparison node.
SDValue
ARMTargetLowering::duplicateCmp(SDValue Cmp, SelectionDAG &DAG) const {
  unsigned Opc = Cmp.getOpcode();
  SDLoc DL(Cmp);
  if (Opc == ARMISD::CMP || Opc == ARMISD::CMPZ)
    return DAG.getNode(Opc, DL, MVT::Glue, Cmp.getOperand(0),Cmp.getOperand(1));

  assert(Opc == ARMISD::FMSTAT && "unexpected comparison operation");
  Cmp = Cmp.getOperand(0);
  Opc = Cmp.getOpcode();
  if (Opc == ARMISD::CMPFP)
    Cmp = DAG.getNode(Opc, DL, MVT::Glue, Cmp.getOperand(0),
                      Cmp.getOperand(1), Cmp.getOperand(2));
  else {
    assert(Opc == ARMISD::CMPFPw0 && "unexpected operand of FMSTAT");
    Cmp = DAG.getNode(Opc, DL, MVT::Glue, Cmp.getOperand(0),
                      Cmp.getOperand(1));
  }
  return DAG.getNode(ARMISD::FMSTAT, DL, MVT::Glue, Cmp);
}

// This function returns three things: the arithmetic computation itself
// (Value), a comparison (OverflowCmp), and a condition code (ARMcc).  The
// comparison and the condition code define the case in which the arithmetic
// computation *does not* overflow.
std::pair<SDValue, SDValue>
ARMTargetLowering::getARMXALUOOp(SDValue Op, SelectionDAG &DAG,
                                 SDValue &ARMcc) const {
  assert(Op.getValueType() == MVT::i32 &&  "Unsupported value type");

  SDValue Value, OverflowCmp;
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDLoc dl(Op);

  // FIXME: We are currently always generating CMPs because we don't support
  // generating CMN through the backend. This is not as good as the natural
  // CMP case because it causes a register dependency and cannot be folded
  // later.

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unknown overflow instruction!");
  case ISD::SADDO:
    ARMcc = DAG.getConstant(ARMCC::VC, dl, MVT::i32);
    Value = DAG.getNode(ISD::ADD, dl, Op.getValueType(), LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value, LHS);
    break;
  case ISD::UADDO:
    ARMcc = DAG.getConstant(ARMCC::HS, dl, MVT::i32);
    // We use ADDC here to correspond to its use in LowerUnsignedALUO.
    // We do not use it in the USUBO case as Value may not be used.
    Value = DAG.getNode(ARMISD::ADDC, dl,
                        DAG.getVTList(Op.getValueType(), MVT::i32), LHS, RHS)
                .getValue(0);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value, LHS);
    break;
  case ISD::SSUBO:
    ARMcc = DAG.getConstant(ARMCC::VC, dl, MVT::i32);
    Value = DAG.getNode(ISD::SUB, dl, Op.getValueType(), LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, LHS, RHS);
    break;
  case ISD::USUBO:
    ARMcc = DAG.getConstant(ARMCC::HS, dl, MVT::i32);
    Value = DAG.getNode(ISD::SUB, dl, Op.getValueType(), LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, LHS, RHS);
    break;
  case ISD::UMULO:
    // We generate a UMUL_LOHI and then check if the high word is 0.
    ARMcc = DAG.getConstant(ARMCC::EQ, dl, MVT::i32);
    Value = DAG.getNode(ISD::UMUL_LOHI, dl,
                        DAG.getVTList(Op.getValueType(), Op.getValueType()),
                        LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value.getValue(1),
                              DAG.getConstant(0, dl, MVT::i32));
    Value = Value.getValue(0); // We only want the low 32 bits for the result.
    break;
  case ISD::SMULO:
    // We generate a SMUL_LOHI and then check if all the bits of the high word
    // are the same as the sign bit of the low word.
    ARMcc = DAG.getConstant(ARMCC::EQ, dl, MVT::i32);
    Value = DAG.getNode(ISD::SMUL_LOHI, dl,
                        DAG.getVTList(Op.getValueType(), Op.getValueType()),
                        LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value.getValue(1),
                              DAG.getNode(ISD::SRA, dl, Op.getValueType(),
                                          Value.getValue(0),
                                          DAG.getConstant(31, dl, MVT::i32)));
    Value = Value.getValue(0); // We only want the low 32 bits for the result.
    break;
  } // switch (...)

  return std::make_pair(Value, OverflowCmp);
}

SDValue
ARMTargetLowering::LowerSignedALUO(SDValue Op, SelectionDAG &DAG) const {
  // Let legalize expand this if it isn't a legal type yet.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(Op.getValueType()))
    return SDValue();

  SDValue Value, OverflowCmp;
  SDValue ARMcc;
  std::tie(Value, OverflowCmp) = getARMXALUOOp(Op, DAG, ARMcc);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDLoc dl(Op);
  // We use 0 and 1 as false and true values.
  SDValue TVal = DAG.getConstant(1, dl, MVT::i32);
  SDValue FVal = DAG.getConstant(0, dl, MVT::i32);
  EVT VT = Op.getValueType();

  SDValue Overflow = DAG.getNode(ARMISD::CMOV, dl, VT, TVal, FVal,
                                 ARMcc, CCR, OverflowCmp);

  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::i32);
  return DAG.getNode(ISD::MERGE_VALUES, dl, VTs, Value, Overflow);
}

static SDValue ConvertBooleanCarryToCarryFlag(SDValue BoolCarry,
                                              SelectionDAG &DAG) {
  SDLoc DL(BoolCarry);
  EVT CarryVT = BoolCarry.getValueType();

  // This converts the boolean value carry into the carry flag by doing
  // ARMISD::SUBC Carry, 1
  SDValue Carry = DAG.getNode(ARMISD::SUBC, DL,
                              DAG.getVTList(CarryVT, MVT::i32),
                              BoolCarry, DAG.getConstant(1, DL, CarryVT));
  return Carry.getValue(1);
}

static SDValue ConvertCarryFlagToBooleanCarry(SDValue Flags, EVT VT,
                                              SelectionDAG &DAG) {
  SDLoc DL(Flags);

  // Now convert the carry flag into a boolean carry. We do this
  // using ARMISD:ADDE 0, 0, Carry
  return DAG.getNode(ARMISD::ADDE, DL, DAG.getVTList(VT, MVT::i32),
                     DAG.getConstant(0, DL, MVT::i32),
                     DAG.getConstant(0, DL, MVT::i32), Flags);
}

SDValue ARMTargetLowering::LowerUnsignedALUO(SDValue Op,
                                             SelectionDAG &DAG) const {
  // Let legalize expand this if it isn't a legal type yet.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(Op.getValueType()))
    return SDValue();

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDLoc dl(Op);

  EVT VT = Op.getValueType();
  SDVTList VTs = DAG.getVTList(VT, MVT::i32);
  SDValue Value;
  SDValue Overflow;
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unknown overflow instruction!");
  case ISD::UADDO:
    Value = DAG.getNode(ARMISD::ADDC, dl, VTs, LHS, RHS);
    // Convert the carry flag into a boolean value.
    Overflow = ConvertCarryFlagToBooleanCarry(Value.getValue(1), VT, DAG);
    break;
  case ISD::USUBO: {
    Value = DAG.getNode(ARMISD::SUBC, dl, VTs, LHS, RHS);
    // Convert the carry flag into a boolean value.
    Overflow = ConvertCarryFlagToBooleanCarry(Value.getValue(1), VT, DAG);
    // ARMISD::SUBC returns 0 when we have to borrow, so make it an overflow
    // value. So compute 1 - C.
    Overflow = DAG.getNode(ISD::SUB, dl, MVT::i32,
                           DAG.getConstant(1, dl, MVT::i32), Overflow);
    break;
  }
  }

  return DAG.getNode(ISD::MERGE_VALUES, dl, VTs, Value, Overflow);
}

SDValue ARMTargetLowering::LowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDValue Cond = Op.getOperand(0);
  SDValue SelectTrue = Op.getOperand(1);
  SDValue SelectFalse = Op.getOperand(2);
  SDLoc dl(Op);
  unsigned Opc = Cond.getOpcode();

  if (Cond.getResNo() == 1 &&
      (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
       Opc == ISD::USUBO)) {
    if (!DAG.getTargetLoweringInfo().isTypeLegal(Cond->getValueType(0)))
      return SDValue();

    SDValue Value, OverflowCmp;
    SDValue ARMcc;
    std::tie(Value, OverflowCmp) = getARMXALUOOp(Cond, DAG, ARMcc);
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    EVT VT = Op.getValueType();

    return getCMOV(dl, VT, SelectTrue, SelectFalse, ARMcc, CCR,
                   OverflowCmp, DAG);
  }

  // Convert:
  //
  //   (select (cmov 1, 0, cond), t, f) -> (cmov t, f, cond)
  //   (select (cmov 0, 1, cond), t, f) -> (cmov f, t, cond)
  //
  if (Cond.getOpcode() == ARMISD::CMOV && Cond.hasOneUse()) {
    const ConstantSDNode *CMOVTrue =
      dyn_cast<ConstantSDNode>(Cond.getOperand(0));
    const ConstantSDNode *CMOVFalse =
      dyn_cast<ConstantSDNode>(Cond.getOperand(1));

    if (CMOVTrue && CMOVFalse) {
      unsigned CMOVTrueVal = CMOVTrue->getZExtValue();
      unsigned CMOVFalseVal = CMOVFalse->getZExtValue();

      SDValue True;
      SDValue False;
      if (CMOVTrueVal == 1 && CMOVFalseVal == 0) {
        True = SelectTrue;
        False = SelectFalse;
      } else if (CMOVTrueVal == 0 && CMOVFalseVal == 1) {
        True = SelectFalse;
        False = SelectTrue;
      }

      if (True.getNode() && False.getNode()) {
        EVT VT = Op.getValueType();
        SDValue ARMcc = Cond.getOperand(2);
        SDValue CCR = Cond.getOperand(3);
        SDValue Cmp = duplicateCmp(Cond.getOperand(4), DAG);
        assert(True.getValueType() == VT);
        return getCMOV(dl, VT, True, False, ARMcc, CCR, Cmp, DAG);
      }
    }
  }

  // ARM's BooleanContents value is UndefinedBooleanContent. Mask out the
  // undefined bits before doing a full-word comparison with zero.
  Cond = DAG.getNode(ISD::AND, dl, Cond.getValueType(), Cond,
                     DAG.getConstant(1, dl, Cond.getValueType()));

  return DAG.getSelectCC(dl, Cond,
                         DAG.getConstant(0, dl, Cond.getValueType()),
                         SelectTrue, SelectFalse, ISD::SETNE);
}

static void checkVSELConstraints(ISD::CondCode CC, ARMCC::CondCodes &CondCode,
                                 bool &swpCmpOps, bool &swpVselOps) {
  // Start by selecting the GE condition code for opcodes that return true for
  // 'equality'
  if (CC == ISD::SETUGE || CC == ISD::SETOGE || CC == ISD::SETOLE ||
      CC == ISD::SETULE)
    CondCode = ARMCC::GE;

  // and GT for opcodes that return false for 'equality'.
  else if (CC == ISD::SETUGT || CC == ISD::SETOGT || CC == ISD::SETOLT ||
           CC == ISD::SETULT)
    CondCode = ARMCC::GT;

  // Since we are constrained to GE/GT, if the opcode contains 'less', we need
  // to swap the compare operands.
  if (CC == ISD::SETOLE || CC == ISD::SETULE || CC == ISD::SETOLT ||
      CC == ISD::SETULT)
    swpCmpOps = true;

  // Both GT and GE are ordered comparisons, and return false for 'unordered'.
  // If we have an unordered opcode, we need to swap the operands to the VSEL
  // instruction (effectively negating the condition).
  //
  // This also has the effect of swapping which one of 'less' or 'greater'
  // returns true, so we also swap the compare operands. It also switches
  // whether we return true for 'equality', so we compensate by picking the
  // opposite condition code to our original choice.
  if (CC == ISD::SETULE || CC == ISD::SETULT || CC == ISD::SETUGE ||
      CC == ISD::SETUGT) {
    swpCmpOps = !swpCmpOps;
    swpVselOps = !swpVselOps;
    CondCode = CondCode == ARMCC::GT ? ARMCC::GE : ARMCC::GT;
  }

  // 'ordered' is 'anything but unordered', so use the VS condition code and
  // swap the VSEL operands.
  if (CC == ISD::SETO) {
    CondCode = ARMCC::VS;
    swpVselOps = true;
  }

  // 'unordered or not equal' is 'anything but equal', so use the EQ condition
  // code and swap the VSEL operands.
  if (CC == ISD::SETUNE) {
    CondCode = ARMCC::EQ;
    swpVselOps = true;
  }
}

SDValue ARMTargetLowering::getCMOV(const SDLoc &dl, EVT VT, SDValue FalseVal,
                                   SDValue TrueVal, SDValue ARMcc, SDValue CCR,
                                   SDValue Cmp, SelectionDAG &DAG) const {
  if (Subtarget->isFPOnlySP() && VT == MVT::f64) {
    FalseVal = DAG.getNode(ARMISD::VMOVRRD, dl,
                           DAG.getVTList(MVT::i32, MVT::i32), FalseVal);
    TrueVal = DAG.getNode(ARMISD::VMOVRRD, dl,
                          DAG.getVTList(MVT::i32, MVT::i32), TrueVal);

    SDValue TrueLow = TrueVal.getValue(0);
    SDValue TrueHigh = TrueVal.getValue(1);
    SDValue FalseLow = FalseVal.getValue(0);
    SDValue FalseHigh = FalseVal.getValue(1);

    SDValue Low = DAG.getNode(ARMISD::CMOV, dl, MVT::i32, FalseLow, TrueLow,
                              ARMcc, CCR, Cmp);
    SDValue High = DAG.getNode(ARMISD::CMOV, dl, MVT::i32, FalseHigh, TrueHigh,
                               ARMcc, CCR, duplicateCmp(Cmp, DAG));

    return DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Low, High);
  } else {
    return DAG.getNode(ARMISD::CMOV, dl, VT, FalseVal, TrueVal, ARMcc, CCR,
                       Cmp);
  }
}

static bool isGTorGE(ISD::CondCode CC) {
  return CC == ISD::SETGT || CC == ISD::SETGE;
}

static bool isLTorLE(ISD::CondCode CC) {
  return CC == ISD::SETLT || CC == ISD::SETLE;
}

// See if a conditional (LHS CC RHS ? TrueVal : FalseVal) is lower-saturating.
// All of these conditions (and their <= and >= counterparts) will do:
//          x < k ? k : x
//          x > k ? x : k
//          k < x ? x : k
//          k > x ? k : x
static bool isLowerSaturate(const SDValue LHS, const SDValue RHS,
                            const SDValue TrueVal, const SDValue FalseVal,
                            const ISD::CondCode CC, const SDValue K) {
  return (isGTorGE(CC) &&
          ((K == LHS && K == TrueVal) || (K == RHS && K == FalseVal))) ||
         (isLTorLE(CC) &&
          ((K == RHS && K == TrueVal) || (K == LHS && K == FalseVal)));
}

// Similar to isLowerSaturate(), but checks for upper-saturating conditions.
static bool isUpperSaturate(const SDValue LHS, const SDValue RHS,
                            const SDValue TrueVal, const SDValue FalseVal,
                            const ISD::CondCode CC, const SDValue K) {
  return (isGTorGE(CC) &&
          ((K == RHS && K == TrueVal) || (K == LHS && K == FalseVal))) ||
         (isLTorLE(CC) &&
          ((K == LHS && K == TrueVal) || (K == RHS && K == FalseVal)));
}

// Check if two chained conditionals could be converted into SSAT or USAT.
//
// SSAT can replace a set of two conditional selectors that bound a number to an
// interval of type [k, ~k] when k + 1 is a power of 2. Here are some examples:
//
//     x < -k ? -k : (x > k ? k : x)
//     x < -k ? -k : (x < k ? x : k)
//     x > -k ? (x > k ? k : x) : -k
//     x < k ? (x < -k ? -k : x) : k
//     etc.
//
// USAT works similarily to SSAT but bounds on the interval [0, k] where k + 1 is
// a power of 2.
//
// It returns true if the conversion can be done, false otherwise.
// Additionally, the variable is returned in parameter V, the constant in K and
// usat is set to true if the conditional represents an unsigned saturation
static bool isSaturatingConditional(const SDValue &Op, SDValue &V,
                                    uint64_t &K, bool &usat) {
  SDValue LHS1 = Op.getOperand(0);
  SDValue RHS1 = Op.getOperand(1);
  SDValue TrueVal1 = Op.getOperand(2);
  SDValue FalseVal1 = Op.getOperand(3);
  ISD::CondCode CC1 = cast<CondCodeSDNode>(Op.getOperand(4))->get();

  const SDValue Op2 = isa<ConstantSDNode>(TrueVal1) ? FalseVal1 : TrueVal1;
  if (Op2.getOpcode() != ISD::SELECT_CC)
    return false;

  SDValue LHS2 = Op2.getOperand(0);
  SDValue RHS2 = Op2.getOperand(1);
  SDValue TrueVal2 = Op2.getOperand(2);
  SDValue FalseVal2 = Op2.getOperand(3);
  ISD::CondCode CC2 = cast<CondCodeSDNode>(Op2.getOperand(4))->get();

  // Find out which are the constants and which are the variables
  // in each conditional
  SDValue *K1 = isa<ConstantSDNode>(LHS1) ? &LHS1 : isa<ConstantSDNode>(RHS1)
                                                        ? &RHS1
                                                        : nullptr;
  SDValue *K2 = isa<ConstantSDNode>(LHS2) ? &LHS2 : isa<ConstantSDNode>(RHS2)
                                                        ? &RHS2
                                                        : nullptr;
  SDValue K2Tmp = isa<ConstantSDNode>(TrueVal2) ? TrueVal2 : FalseVal2;
  SDValue V1Tmp = (K1 && *K1 == LHS1) ? RHS1 : LHS1;
  SDValue V2Tmp = (K2 && *K2 == LHS2) ? RHS2 : LHS2;
  SDValue V2 = (K2Tmp == TrueVal2) ? FalseVal2 : TrueVal2;

  // We must detect cases where the original operations worked with 16- or
  // 8-bit values. In such case, V2Tmp != V2 because the comparison operations
  // must work with sign-extended values but the select operations return
  // the original non-extended value.
  SDValue V2TmpReg = V2Tmp;
  if (V2Tmp->getOpcode() == ISD::SIGN_EXTEND_INREG)
    V2TmpReg = V2Tmp->getOperand(0);

  // Check that the registers and the constants have the correct values
  // in both conditionals
  if (!K1 || !K2 || *K1 == Op2 || *K2 != K2Tmp || V1Tmp != V2Tmp ||
      V2TmpReg != V2)
    return false;

  // Figure out which conditional is saturating the lower/upper bound.
  const SDValue *LowerCheckOp =
      isLowerSaturate(LHS1, RHS1, TrueVal1, FalseVal1, CC1, *K1)
          ? &Op
          : isLowerSaturate(LHS2, RHS2, TrueVal2, FalseVal2, CC2, *K2)
                ? &Op2
                : nullptr;
  const SDValue *UpperCheckOp =
      isUpperSaturate(LHS1, RHS1, TrueVal1, FalseVal1, CC1, *K1)
          ? &Op
          : isUpperSaturate(LHS2, RHS2, TrueVal2, FalseVal2, CC2, *K2)
                ? &Op2
                : nullptr;

  if (!UpperCheckOp || !LowerCheckOp || LowerCheckOp == UpperCheckOp)
    return false;

  // Check that the constant in the lower-bound check is
  // the opposite of the constant in the upper-bound check
  // in 1's complement.
  int64_t Val1 = cast<ConstantSDNode>(*K1)->getSExtValue();
  int64_t Val2 = cast<ConstantSDNode>(*K2)->getSExtValue();
  int64_t PosVal = std::max(Val1, Val2);
  int64_t NegVal = std::min(Val1, Val2);

  if (((Val1 > Val2 && UpperCheckOp == &Op) ||
       (Val1 < Val2 && UpperCheckOp == &Op2)) &&
      isPowerOf2_64(PosVal + 1)) {

    // Handle the difference between USAT (unsigned) and SSAT (signed) saturation
    if (Val1 == ~Val2)
      usat = false;
    else if (NegVal == 0)
      usat = true;
    else
      return false;

    V = V2;
    K = (uint64_t)PosVal; // At this point, PosVal is guaranteed to be positive

    return true;
  }

  return false;
}

// Check if a condition of the type x < k ? k : x can be converted into a
// bit operation instead of conditional moves.
// Currently this is allowed given:
// - The conditions and values match up
// - k is 0 or -1 (all ones)
// This function will not check the last condition, thats up to the caller
// It returns true if the transformation can be made, and in such case
// returns x in V, and k in SatK.
static bool isLowerSaturatingConditional(const SDValue &Op, SDValue &V,
                                         SDValue &SatK)
{
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue TrueVal = Op.getOperand(2);
  SDValue FalseVal = Op.getOperand(3);

  SDValue *K = isa<ConstantSDNode>(LHS) ? &LHS : isa<ConstantSDNode>(RHS)
                                               ? &RHS
                                               : nullptr;

  // No constant operation in comparison, early out
  if (!K)
    return false;

  SDValue KTmp = isa<ConstantSDNode>(TrueVal) ? TrueVal : FalseVal;
  V = (KTmp == TrueVal) ? FalseVal : TrueVal;
  SDValue VTmp = (K && *K == LHS) ? RHS : LHS;

  // If the constant on left and right side, or variable on left and right,
  // does not match, early out
  if (*K != KTmp || V != VTmp)
    return false;

  if (isLowerSaturate(LHS, RHS, TrueVal, FalseVal, CC, *K)) {
    SatK = *K;
    return true;
  }

  return false;
}

SDValue ARMTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);

  // Try to convert two saturating conditional selects into a single SSAT
  SDValue SatValue;
  uint64_t SatConstant;
  bool SatUSat;
  if (((!Subtarget->isThumb() && Subtarget->hasV6Ops()) || Subtarget->isThumb2()) &&
      isSaturatingConditional(Op, SatValue, SatConstant, SatUSat)) {
    if (SatUSat)
      return DAG.getNode(ARMISD::USAT, dl, VT, SatValue,
                         DAG.getConstant(countTrailingOnes(SatConstant), dl, VT));
    else
      return DAG.getNode(ARMISD::SSAT, dl, VT, SatValue,
                         DAG.getConstant(countTrailingOnes(SatConstant), dl, VT));
  }

  // Try to convert expressions of the form x < k ? k : x (and similar forms)
  // into more efficient bit operations, which is possible when k is 0 or -1
  // On ARM and Thumb-2 which have flexible operand 2 this will result in
  // single instructions. On Thumb the shift and the bit operation will be two
  // instructions.
  // Only allow this transformation on full-width (32-bit) operations
  SDValue LowerSatConstant;
  if (VT == MVT::i32 &&
      isLowerSaturatingConditional(Op, SatValue, LowerSatConstant)) {
    SDValue ShiftV = DAG.getNode(ISD::SRA, dl, VT, SatValue,
                                 DAG.getConstant(31, dl, VT));
    if (isNullConstant(LowerSatConstant)) {
      SDValue NotShiftV = DAG.getNode(ISD::XOR, dl, VT, ShiftV,
                                      DAG.getAllOnesConstant(dl, VT));
      return DAG.getNode(ISD::AND, dl, VT, SatValue, NotShiftV);
    } else if (isAllOnesConstant(LowerSatConstant))
      return DAG.getNode(ISD::OR, dl, VT, SatValue, ShiftV);
  }

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue TrueVal = Op.getOperand(2);
  SDValue FalseVal = Op.getOperand(3);

  if (Subtarget->isFPOnlySP() && LHS.getValueType() == MVT::f64) {
    DAG.getTargetLoweringInfo().softenSetCCOperands(DAG, MVT::f64, LHS, RHS, CC,
                                                    dl);

    // If softenSetCCOperands only returned one value, we should compare it to
    // zero.
    if (!RHS.getNode()) {
      RHS = DAG.getConstant(0, dl, LHS.getValueType());
      CC = ISD::SETNE;
    }
  }

  if (LHS.getValueType() == MVT::i32) {
    // Try to generate VSEL on ARMv8.
    // The VSEL instruction can't use all the usual ARM condition
    // codes: it only has two bits to select the condition code, so it's
    // constrained to use only GE, GT, VS and EQ.
    //
    // To implement all the various ISD::SETXXX opcodes, we sometimes need to
    // swap the operands of the previous compare instruction (effectively
    // inverting the compare condition, swapping 'less' and 'greater') and
    // sometimes need to swap the operands to the VSEL (which inverts the
    // condition in the sense of firing whenever the previous condition didn't)
    if (Subtarget->hasFPARMv8() && (TrueVal.getValueType() == MVT::f32 ||
                                    TrueVal.getValueType() == MVT::f64)) {
      ARMCC::CondCodes CondCode = IntCCToARMCC(CC);
      if (CondCode == ARMCC::LT || CondCode == ARMCC::LE ||
          CondCode == ARMCC::VC || CondCode == ARMCC::NE) {
        CC = ISD::getSetCCInverse(CC, true);
        std::swap(TrueVal, FalseVal);
      }
    }

    SDValue ARMcc;
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMcc, DAG, dl);
    return getCMOV(dl, VT, FalseVal, TrueVal, ARMcc, CCR, Cmp, DAG);
  }

  ARMCC::CondCodes CondCode, CondCode2;
  bool InvalidOnQNaN;
  FPCCToARMCC(CC, CondCode, CondCode2, InvalidOnQNaN);

  // Normalize the fp compare. If RHS is zero we keep it there so we match
  // CMPFPw0 instead of CMPFP.
  if (Subtarget->hasFPARMv8() && !isFloatingPointZero(RHS) &&
     (TrueVal.getValueType() == MVT::f16 ||
      TrueVal.getValueType() == MVT::f32 ||
      TrueVal.getValueType() == MVT::f64)) {
    bool swpCmpOps = false;
    bool swpVselOps = false;
    checkVSELConstraints(CC, CondCode, swpCmpOps, swpVselOps);

    if (CondCode == ARMCC::GT || CondCode == ARMCC::GE ||
        CondCode == ARMCC::VS || CondCode == ARMCC::EQ) {
      if (swpCmpOps)
        std::swap(LHS, RHS);
      if (swpVselOps)
        std::swap(TrueVal, FalseVal);
    }
  }

  SDValue ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
  SDValue Cmp = getVFPCmp(LHS, RHS, DAG, dl, InvalidOnQNaN);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDValue Result = getCMOV(dl, VT, FalseVal, TrueVal, ARMcc, CCR, Cmp, DAG);
  if (CondCode2 != ARMCC::AL) {
    SDValue ARMcc2 = DAG.getConstant(CondCode2, dl, MVT::i32);
    // FIXME: Needs another CMP because flag can have but one use.
    SDValue Cmp2 = getVFPCmp(LHS, RHS, DAG, dl, InvalidOnQNaN);
    Result = getCMOV(dl, VT, Result, TrueVal, ARMcc2, CCR, Cmp2, DAG);
  }
  return Result;
}

/// canChangeToInt - Given the fp compare operand, return true if it is suitable
/// to morph to an integer compare sequence.
static bool canChangeToInt(SDValue Op, bool &SeenZero,
                           const ARMSubtarget *Subtarget) {
  SDNode *N = Op.getNode();
  if (!N->hasOneUse())
    // Otherwise it requires moving the value from fp to integer registers.
    return false;
  if (!N->getNumValues())
    return false;
  EVT VT = Op.getValueType();
  if (VT != MVT::f32 && !Subtarget->isFPBrccSlow())
    // f32 case is generally profitable. f64 case only makes sense when vcmpe +
    // vmrs are very slow, e.g. cortex-a8.
    return false;

  if (isFloatingPointZero(Op)) {
    SeenZero = true;
    return true;
  }
  return ISD::isNormalLoad(N);
}

static SDValue bitcastf32Toi32(SDValue Op, SelectionDAG &DAG) {
  if (isFloatingPointZero(Op))
    return DAG.getConstant(0, SDLoc(Op), MVT::i32);

  if (LoadSDNode *Ld = dyn_cast<LoadSDNode>(Op))
    return DAG.getLoad(MVT::i32, SDLoc(Op), Ld->getChain(), Ld->getBasePtr(),
                       Ld->getPointerInfo(), Ld->getAlignment(),
                       Ld->getMemOperand()->getFlags());

  llvm_unreachable("Unknown VFP cmp argument!");
}

static void expandf64Toi32(SDValue Op, SelectionDAG &DAG,
                           SDValue &RetVal1, SDValue &RetVal2) {
  SDLoc dl(Op);

  if (isFloatingPointZero(Op)) {
    RetVal1 = DAG.getConstant(0, dl, MVT::i32);
    RetVal2 = DAG.getConstant(0, dl, MVT::i32);
    return;
  }

  if (LoadSDNode *Ld = dyn_cast<LoadSDNode>(Op)) {
    SDValue Ptr = Ld->getBasePtr();
    RetVal1 =
        DAG.getLoad(MVT::i32, dl, Ld->getChain(), Ptr, Ld->getPointerInfo(),
                    Ld->getAlignment(), Ld->getMemOperand()->getFlags());

    EVT PtrType = Ptr.getValueType();
    unsigned NewAlign = MinAlign(Ld->getAlignment(), 4);
    SDValue NewPtr = DAG.getNode(ISD::ADD, dl,
                                 PtrType, Ptr, DAG.getConstant(4, dl, PtrType));
    RetVal2 = DAG.getLoad(MVT::i32, dl, Ld->getChain(), NewPtr,
                          Ld->getPointerInfo().getWithOffset(4), NewAlign,
                          Ld->getMemOperand()->getFlags());
    return;
  }

  llvm_unreachable("Unknown VFP cmp argument!");
}

/// OptimizeVFPBrcond - With -enable-unsafe-fp-math, it's legal to optimize some
/// f32 and even f64 comparisons to integer ones.
SDValue
ARMTargetLowering::OptimizeVFPBrcond(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);

  bool LHSSeenZero = false;
  bool LHSOk = canChangeToInt(LHS, LHSSeenZero, Subtarget);
  bool RHSSeenZero = false;
  bool RHSOk = canChangeToInt(RHS, RHSSeenZero, Subtarget);
  if (LHSOk && RHSOk && (LHSSeenZero || RHSSeenZero)) {
    // If unsafe fp math optimization is enabled and there are no other uses of
    // the CMP operands, and the condition code is EQ or NE, we can optimize it
    // to an integer comparison.
    if (CC == ISD::SETOEQ)
      CC = ISD::SETEQ;
    else if (CC == ISD::SETUNE)
      CC = ISD::SETNE;

    SDValue Mask = DAG.getConstant(0x7fffffff, dl, MVT::i32);
    SDValue ARMcc;
    if (LHS.getValueType() == MVT::f32) {
      LHS = DAG.getNode(ISD::AND, dl, MVT::i32,
                        bitcastf32Toi32(LHS, DAG), Mask);
      RHS = DAG.getNode(ISD::AND, dl, MVT::i32,
                        bitcastf32Toi32(RHS, DAG), Mask);
      SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMcc, DAG, dl);
      SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
      return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other,
                         Chain, Dest, ARMcc, CCR, Cmp);
    }

    SDValue LHS1, LHS2;
    SDValue RHS1, RHS2;
    expandf64Toi32(LHS, DAG, LHS1, LHS2);
    expandf64Toi32(RHS, DAG, RHS1, RHS2);
    LHS2 = DAG.getNode(ISD::AND, dl, MVT::i32, LHS2, Mask);
    RHS2 = DAG.getNode(ISD::AND, dl, MVT::i32, RHS2, Mask);
    ARMCC::CondCodes CondCode = IntCCToARMCC(CC);
    ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
    SDVTList VTList = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, ARMcc, LHS1, LHS2, RHS1, RHS2, Dest };
    return DAG.getNode(ARMISD::BCC_i64, dl, VTList, Ops);
  }

  return SDValue();
}

SDValue ARMTargetLowering::LowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);
  SDLoc dl(Op);

  // Optimize {s|u}{add|sub|mul}.with.overflow feeding into a branch
  // instruction.
  unsigned Opc = Cond.getOpcode();
  bool OptimizeMul = (Opc == ISD::SMULO || Opc == ISD::UMULO) &&
                      !Subtarget->isThumb1Only();
  if (Cond.getResNo() == 1 &&
      (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
       Opc == ISD::USUBO || OptimizeMul)) {
    // Only lower legal XALUO ops.
    if (!DAG.getTargetLoweringInfo().isTypeLegal(Cond->getValueType(0)))
      return SDValue();

    // The actual operation with overflow check.
    SDValue Value, OverflowCmp;
    SDValue ARMcc;
    std::tie(Value, OverflowCmp) = getARMXALUOOp(Cond, DAG, ARMcc);

    // Reverse the condition code.
    ARMCC::CondCodes CondCode =
        (ARMCC::CondCodes)cast<const ConstantSDNode>(ARMcc)->getZExtValue();
    CondCode = ARMCC::getOppositeCondition(CondCode);
    ARMcc = DAG.getConstant(CondCode, SDLoc(ARMcc), MVT::i32);
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);

    return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other, Chain, Dest, ARMcc, CCR,
                       OverflowCmp);
  }

  return SDValue();
}

SDValue ARMTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);

  if (Subtarget->isFPOnlySP() && LHS.getValueType() == MVT::f64) {
    DAG.getTargetLoweringInfo().softenSetCCOperands(DAG, MVT::f64, LHS, RHS, CC,
                                                    dl);

    // If softenSetCCOperands only returned one value, we should compare it to
    // zero.
    if (!RHS.getNode()) {
      RHS = DAG.getConstant(0, dl, LHS.getValueType());
      CC = ISD::SETNE;
    }
  }

  // Optimize {s|u}{add|sub|mul}.with.overflow feeding into a branch
  // instruction.
  unsigned Opc = LHS.getOpcode();
  bool OptimizeMul = (Opc == ISD::SMULO || Opc == ISD::UMULO) &&
                      !Subtarget->isThumb1Only();
  if (LHS.getResNo() == 1 && (isOneConstant(RHS) || isNullConstant(RHS)) &&
      (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
       Opc == ISD::USUBO || OptimizeMul) &&
      (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    // Only lower legal XALUO ops.
    if (!DAG.getTargetLoweringInfo().isTypeLegal(LHS->getValueType(0)))
      return SDValue();

    // The actual operation with overflow check.
    SDValue Value, OverflowCmp;
    SDValue ARMcc;
    std::tie(Value, OverflowCmp) = getARMXALUOOp(LHS.getValue(0), DAG, ARMcc);

    if ((CC == ISD::SETNE) != isOneConstant(RHS)) {
      // Reverse the condition code.
      ARMCC::CondCodes CondCode =
          (ARMCC::CondCodes)cast<const ConstantSDNode>(ARMcc)->getZExtValue();
      CondCode = ARMCC::getOppositeCondition(CondCode);
      ARMcc = DAG.getConstant(CondCode, SDLoc(ARMcc), MVT::i32);
    }
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);

    return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other, Chain, Dest, ARMcc, CCR,
                       OverflowCmp);
  }

  if (LHS.getValueType() == MVT::i32) {
    SDValue ARMcc;
    SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMcc, DAG, dl);
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other,
                       Chain, Dest, ARMcc, CCR, Cmp);
  }

  if (getTargetMachine().Options.UnsafeFPMath &&
      (CC == ISD::SETEQ || CC == ISD::SETOEQ ||
       CC == ISD::SETNE || CC == ISD::SETUNE)) {
    if (SDValue Result = OptimizeVFPBrcond(Op, DAG))
      return Result;
  }

  ARMCC::CondCodes CondCode, CondCode2;
  bool InvalidOnQNaN;
  FPCCToARMCC(CC, CondCode, CondCode2, InvalidOnQNaN);

  SDValue ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
  SDValue Cmp = getVFPCmp(LHS, RHS, DAG, dl, InvalidOnQNaN);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDVTList VTList = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Ops[] = { Chain, Dest, ARMcc, CCR, Cmp };
  SDValue Res = DAG.getNode(ARMISD::BRCOND, dl, VTList, Ops);
  if (CondCode2 != ARMCC::AL) {
    ARMcc = DAG.getConstant(CondCode2, dl, MVT::i32);
    SDValue Ops[] = { Res, Dest, ARMcc, CCR, Res.getValue(1) };
    Res = DAG.getNode(ARMISD::BRCOND, dl, VTList, Ops);
  }
  return Res;
}

SDValue ARMTargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Table = Op.getOperand(1);
  SDValue Index = Op.getOperand(2);
  SDLoc dl(Op);

  EVT PTy = getPointerTy(DAG.getDataLayout());
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Table);
  SDValue JTI = DAG.getTargetJumpTable(JT->getIndex(), PTy);
  Table = DAG.getNode(ARMISD::WrapperJT, dl, MVT::i32, JTI);
  Index = DAG.getNode(ISD::MUL, dl, PTy, Index, DAG.getConstant(4, dl, PTy));
  SDValue Addr = DAG.getNode(ISD::ADD, dl, PTy, Table, Index);
  if (Subtarget->isThumb2() || (Subtarget->hasV8MBaselineOps() && Subtarget->isThumb())) {
    // Thumb2 and ARMv8-M use a two-level jump. That is, it jumps into the jump table
    // which does another jump to the destination. This also makes it easier
    // to translate it to TBB / TBH later (Thumb2 only).
    // FIXME: This might not work if the function is extremely large.
    return DAG.getNode(ARMISD::BR2_JT, dl, MVT::Other, Chain,
                       Addr, Op.getOperand(2), JTI);
  }
  if (isPositionIndependent() || Subtarget->isROPI()) {
    Addr =
        DAG.getLoad((EVT)MVT::i32, dl, Chain, Addr,
                    MachinePointerInfo::getJumpTable(DAG.getMachineFunction()));
    Chain = Addr.getValue(1);
    Addr = DAG.getNode(ISD::ADD, dl, PTy, Table, Addr);
    return DAG.getNode(ARMISD::BR_JT, dl, MVT::Other, Chain, Addr, JTI);
  } else {
    Addr =
        DAG.getLoad(PTy, dl, Chain, Addr,
                    MachinePointerInfo::getJumpTable(DAG.getMachineFunction()));
    Chain = Addr.getValue(1);
    return DAG.getNode(ARMISD::BR_JT, dl, MVT::Other, Chain, Addr, JTI);
  }
}

static SDValue LowerVectorFP_TO_INT(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);

  if (Op.getValueType().getVectorElementType() == MVT::i32) {
    if (Op.getOperand(0).getValueType().getVectorElementType() == MVT::f32)
      return Op;
    return DAG.UnrollVectorOp(Op.getNode());
  }

  const bool HasFullFP16 =
    static_cast<const ARMSubtarget&>(DAG.getSubtarget()).hasFullFP16();

  EVT NewTy;
  const EVT OpTy = Op.getOperand(0).getValueType();
  if (OpTy == MVT::v4f32)
    NewTy = MVT::v4i32;
  else if (OpTy == MVT::v4f16 && HasFullFP16)
    NewTy = MVT::v4i16;
  else if (OpTy == MVT::v8f16 && HasFullFP16)
    NewTy = MVT::v8i16;
  else
    llvm_unreachable("Invalid type for custom lowering!");

  if (VT != MVT::v4i16 && VT != MVT::v8i16)
    return DAG.UnrollVectorOp(Op.getNode());

  Op = DAG.getNode(Op.getOpcode(), dl, NewTy, Op.getOperand(0));
  return DAG.getNode(ISD::TRUNCATE, dl, VT, Op);
}

SDValue ARMTargetLowering::LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  if (VT.isVector())
    return LowerVectorFP_TO_INT(Op, DAG);
  if (Subtarget->isFPOnlySP() && Op.getOperand(0).getValueType() == MVT::f64) {
    RTLIB::Libcall LC;
    if (Op.getOpcode() == ISD::FP_TO_SINT)
      LC = RTLIB::getFPTOSINT(Op.getOperand(0).getValueType(),
                              Op.getValueType());
    else
      LC = RTLIB::getFPTOUINT(Op.getOperand(0).getValueType(),
                              Op.getValueType());
    return makeLibCall(DAG, LC, Op.getValueType(), Op.getOperand(0),
                       /*isSigned*/ false, SDLoc(Op)).first;
  }

  return Op;
}

static SDValue LowerVectorINT_TO_FP(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);

  if (Op.getOperand(0).getValueType().getVectorElementType() == MVT::i32) {
    if (VT.getVectorElementType() == MVT::f32)
      return Op;
    return DAG.UnrollVectorOp(Op.getNode());
  }

  assert((Op.getOperand(0).getValueType() == MVT::v4i16 ||
          Op.getOperand(0).getValueType() == MVT::v8i16) &&
         "Invalid type for custom lowering!");

  const bool HasFullFP16 =
    static_cast<const ARMSubtarget&>(DAG.getSubtarget()).hasFullFP16();

  EVT DestVecType;
  if (VT == MVT::v4f32)
    DestVecType = MVT::v4i32;
  else if (VT == MVT::v4f16 && HasFullFP16)
    DestVecType = MVT::v4i16;
  else if (VT == MVT::v8f16 && HasFullFP16)
    DestVecType = MVT::v8i16;
  else
    return DAG.UnrollVectorOp(Op.getNode());

  unsigned CastOpc;
  unsigned Opc;
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Invalid opcode!");
  case ISD::SINT_TO_FP:
    CastOpc = ISD::SIGN_EXTEND;
    Opc = ISD::SINT_TO_FP;
    break;
  case ISD::UINT_TO_FP:
    CastOpc = ISD::ZERO_EXTEND;
    Opc = ISD::UINT_TO_FP;
    break;
  }

  Op = DAG.getNode(CastOpc, dl, DestVecType, Op.getOperand(0));
  return DAG.getNode(Opc, dl, VT, Op);
}

SDValue ARMTargetLowering::LowerINT_TO_FP(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  if (VT.isVector())
    return LowerVectorINT_TO_FP(Op, DAG);
  if (Subtarget->isFPOnlySP() && Op.getValueType() == MVT::f64) {
    RTLIB::Libcall LC;
    if (Op.getOpcode() == ISD::SINT_TO_FP)
      LC = RTLIB::getSINTTOFP(Op.getOperand(0).getValueType(),
                              Op.getValueType());
    else
      LC = RTLIB::getUINTTOFP(Op.getOperand(0).getValueType(),
                              Op.getValueType());
    return makeLibCall(DAG, LC, Op.getValueType(), Op.getOperand(0),
                       /*isSigned*/ false, SDLoc(Op)).first;
  }

  return Op;
}

SDValue ARMTargetLowering::LowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) const {
  // Implement fcopysign with a fabs and a conditional fneg.
  SDValue Tmp0 = Op.getOperand(0);
  SDValue Tmp1 = Op.getOperand(1);
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  EVT SrcVT = Tmp1.getValueType();
  bool InGPR = Tmp0.getOpcode() == ISD::BITCAST ||
    Tmp0.getOpcode() == ARMISD::VMOVDRR;
  bool UseNEON = !InGPR && Subtarget->hasNEON();

  if (UseNEON) {
    // Use VBSL to copy the sign bit.
    unsigned EncodedVal = ARM_AM::createNEONModImm(0x6, 0x80);
    SDValue Mask = DAG.getNode(ARMISD::VMOVIMM, dl, MVT::v2i32,
                               DAG.getTargetConstant(EncodedVal, dl, MVT::i32));
    EVT OpVT = (VT == MVT::f32) ? MVT::v2i32 : MVT::v1i64;
    if (VT == MVT::f64)
      Mask = DAG.getNode(ARMISD::VSHL, dl, OpVT,
                         DAG.getNode(ISD::BITCAST, dl, OpVT, Mask),
                         DAG.getConstant(32, dl, MVT::i32));
    else /*if (VT == MVT::f32)*/
      Tmp0 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, MVT::v2f32, Tmp0);
    if (SrcVT == MVT::f32) {
      Tmp1 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, MVT::v2f32, Tmp1);
      if (VT == MVT::f64)
        Tmp1 = DAG.getNode(ARMISD::VSHL, dl, OpVT,
                           DAG.getNode(ISD::BITCAST, dl, OpVT, Tmp1),
                           DAG.getConstant(32, dl, MVT::i32));
    } else if (VT == MVT::f32)
      Tmp1 = DAG.getNode(ARMISD::VSHRu, dl, MVT::v1i64,
                         DAG.getNode(ISD::BITCAST, dl, MVT::v1i64, Tmp1),
                         DAG.getConstant(32, dl, MVT::i32));
    Tmp0 = DAG.getNode(ISD::BITCAST, dl, OpVT, Tmp0);
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, OpVT, Tmp1);

    SDValue AllOnes = DAG.getTargetConstant(ARM_AM::createNEONModImm(0xe, 0xff),
                                            dl, MVT::i32);
    AllOnes = DAG.getNode(ARMISD::VMOVIMM, dl, MVT::v8i8, AllOnes);
    SDValue MaskNot = DAG.getNode(ISD::XOR, dl, OpVT, Mask,
                                  DAG.getNode(ISD::BITCAST, dl, OpVT, AllOnes));

    SDValue Res = DAG.getNode(ISD::OR, dl, OpVT,
                              DAG.getNode(ISD::AND, dl, OpVT, Tmp1, Mask),
                              DAG.getNode(ISD::AND, dl, OpVT, Tmp0, MaskNot));
    if (VT == MVT::f32) {
      Res = DAG.getNode(ISD::BITCAST, dl, MVT::v2f32, Res);
      Res = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f32, Res,
                        DAG.getConstant(0, dl, MVT::i32));
    } else {
      Res = DAG.getNode(ISD::BITCAST, dl, MVT::f64, Res);
    }

    return Res;
  }

  // Bitcast operand 1 to i32.
  if (SrcVT == MVT::f64)
    Tmp1 = DAG.getNode(ARMISD::VMOVRRD, dl, DAG.getVTList(MVT::i32, MVT::i32),
                       Tmp1).getValue(1);
  Tmp1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Tmp1);

  // Or in the signbit with integer operations.
  SDValue Mask1 = DAG.getConstant(0x80000000, dl, MVT::i32);
  SDValue Mask2 = DAG.getConstant(0x7fffffff, dl, MVT::i32);
  Tmp1 = DAG.getNode(ISD::AND, dl, MVT::i32, Tmp1, Mask1);
  if (VT == MVT::f32) {
    Tmp0 = DAG.getNode(ISD::AND, dl, MVT::i32,
                       DAG.getNode(ISD::BITCAST, dl, MVT::i32, Tmp0), Mask2);
    return DAG.getNode(ISD::BITCAST, dl, MVT::f32,
                       DAG.getNode(ISD::OR, dl, MVT::i32, Tmp0, Tmp1));
  }

  // f64: Or the high part with signbit and then combine two parts.
  Tmp0 = DAG.getNode(ARMISD::VMOVRRD, dl, DAG.getVTList(MVT::i32, MVT::i32),
                     Tmp0);
  SDValue Lo = Tmp0.getValue(0);
  SDValue Hi = DAG.getNode(ISD::AND, dl, MVT::i32, Tmp0.getValue(1), Mask2);
  Hi = DAG.getNode(ISD::OR, dl, MVT::i32, Hi, Tmp1);
  return DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi);
}

SDValue ARMTargetLowering::LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const{
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  if (Depth) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset = DAG.getConstant(4, dl, MVT::i32);
    return DAG.getLoad(VT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, VT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Return LR, which contains the return address. Mark it an implicit live-in.
  unsigned Reg = MF.addLiveIn(ARM::LR, getRegClassFor(MVT::i32));
  return DAG.getCopyFromReg(DAG.getEntryNode(), dl, Reg, VT);
}

SDValue ARMTargetLowering::LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const {
  const ARMBaseRegisterInfo &ARI =
    *static_cast<const ARMBaseRegisterInfo*>(RegInfo);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc dl(Op);  // FIXME probably not meaningful
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  unsigned FrameReg = ARI.getFrameRegister(MF);
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl, FrameReg, VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, dl, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

// FIXME? Maybe this could be a TableGen attribute on some registers and
// this table could be generated automatically from RegInfo.
unsigned ARMTargetLowering::getRegisterByName(const char* RegName, EVT VT,
                                              SelectionDAG &DAG) const {
  unsigned Reg = StringSwitch<unsigned>(RegName)
                       .Case("sp", ARM::SP)
                       .Default(0);
  if (Reg)
    return Reg;
  report_fatal_error(Twine("Invalid register name \""
                              + StringRef(RegName)  + "\"."));
}

// Result is 64 bit value so split into two 32 bit values and return as a
// pair of values.
static void ExpandREAD_REGISTER(SDNode *N, SmallVectorImpl<SDValue> &Results,
                                SelectionDAG &DAG) {
  SDLoc DL(N);

  // This function is only supposed to be called for i64 type destination.
  assert(N->getValueType(0) == MVT::i64
          && "ExpandREAD_REGISTER called for non-i64 type result.");

  SDValue Read = DAG.getNode(ISD::READ_REGISTER, DL,
                             DAG.getVTList(MVT::i32, MVT::i32, MVT::Other),
                             N->getOperand(0),
                             N->getOperand(1));

  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Read.getValue(0),
                    Read.getValue(1)));
  Results.push_back(Read.getOperand(0));
}

/// \p BC is a bitcast that is about to be turned into a VMOVDRR.
/// When \p DstVT, the destination type of \p BC, is on the vector
/// register bank and the source of bitcast, \p Op, operates on the same bank,
/// it might be possible to combine them, such that everything stays on the
/// vector register bank.
/// \p return The node that would replace \p BT, if the combine
/// is possible.
static SDValue CombineVMOVDRRCandidateWithVecOp(const SDNode *BC,
                                                SelectionDAG &DAG) {
  SDValue Op = BC->getOperand(0);
  EVT DstVT = BC->getValueType(0);

  // The only vector instruction that can produce a scalar (remember,
  // since the bitcast was about to be turned into VMOVDRR, the source
  // type is i64) from a vector is EXTRACT_VECTOR_ELT.
  // Moreover, we can do this combine only if there is one use.
  // Finally, if the destination type is not a vector, there is not
  // much point on forcing everything on the vector bank.
  if (!DstVT.isVector() || Op.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
      !Op.hasOneUse())
    return SDValue();

  // If the index is not constant, we will introduce an additional
  // multiply that will stick.
  // Give up in that case.
  ConstantSDNode *Index = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!Index)
    return SDValue();
  unsigned DstNumElt = DstVT.getVectorNumElements();

  // Compute the new index.
  const APInt &APIntIndex = Index->getAPIntValue();
  APInt NewIndex(APIntIndex.getBitWidth(), DstNumElt);
  NewIndex *= APIntIndex;
  // Check if the new constant index fits into i32.
  if (NewIndex.getBitWidth() > 32)
    return SDValue();

  // vMTy bitcast(i64 extractelt vNi64 src, i32 index) ->
  // vMTy extractsubvector vNxMTy (bitcast vNi64 src), i32 index*M)
  SDLoc dl(Op);
  SDValue ExtractSrc = Op.getOperand(0);
  EVT VecVT = EVT::getVectorVT(
      *DAG.getContext(), DstVT.getScalarType(),
      ExtractSrc.getValueType().getVectorNumElements() * DstNumElt);
  SDValue BitCast = DAG.getNode(ISD::BITCAST, dl, VecVT, ExtractSrc);
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DstVT, BitCast,
                     DAG.getConstant(NewIndex.getZExtValue(), dl, MVT::i32));
}

/// ExpandBITCAST - If the target supports VFP, this function is called to
/// expand a bit convert where either the source or destination type is i64 to
/// use a VMOVDRR or VMOVRRD node.  This should not be done when the non-i64
/// operand type is illegal (e.g., v2f32 for a target that doesn't support
/// vectors), since the legalizer won't know what to do with that.
static SDValue ExpandBITCAST(SDNode *N, SelectionDAG &DAG,
                             const ARMSubtarget *Subtarget) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc dl(N);
  SDValue Op = N->getOperand(0);

  // This function is only supposed to be called for i64 types, either as the
  // source or destination of the bit convert.
  EVT SrcVT = Op.getValueType();
  EVT DstVT = N->getValueType(0);
  const bool HasFullFP16 = Subtarget->hasFullFP16();

  if (SrcVT == MVT::f32 && DstVT == MVT::i32) {
     // FullFP16: half values are passed in S-registers, and we don't
     // need any of the bitcast and moves:
     //
     // t2: f32,ch = CopyFromReg t0, Register:f32 %0
     //   t5: i32 = bitcast t2
     // t18: f16 = ARMISD::VMOVhr t5
     if (Op.getOpcode() != ISD::CopyFromReg ||
         Op.getValueType() != MVT::f32)
       return SDValue();

     auto Move = N->use_begin();
     if (Move->getOpcode() != ARMISD::VMOVhr)
       return SDValue();

     SDValue Ops[] = { Op.getOperand(0), Op.getOperand(1) };
     SDValue Copy = DAG.getNode(ISD::CopyFromReg, SDLoc(Op), MVT::f16, Ops);
     DAG.ReplaceAllUsesWith(*Move, &Copy);
     return Copy;
  }

  if (SrcVT == MVT::i16 && DstVT == MVT::f16) {
    if (!HasFullFP16)
      return SDValue();
    // SoftFP: read half-precision arguments:
    //
    // t2: i32,ch = ...
    //        t7: i16 = truncate t2 <~~~~ Op
    //      t8: f16 = bitcast t7    <~~~~ N
    //
    if (Op.getOperand(0).getValueType() == MVT::i32)
      return DAG.getNode(ARMISD::VMOVhr, SDLoc(Op),
                         MVT::f16, Op.getOperand(0));

    return SDValue();
  }

  // Half-precision return values
  if (SrcVT == MVT::f16 && DstVT == MVT::i16) {
    if (!HasFullFP16)
      return SDValue();
    //
    //          t11: f16 = fadd t8, t10
    //        t12: i16 = bitcast t11       <~~~ SDNode N
    //      t13: i32 = zero_extend t12
    //    t16: ch,glue = CopyToReg t0, Register:i32 %r0, t13
    //  t17: ch = ARMISD::RET_FLAG t16, Register:i32 %r0, t16:1
    //
    // transform this into:
    //
    //    t20: i32 = ARMISD::VMOVrh t11
    //  t16: ch,glue = CopyToReg t0, Register:i32 %r0, t20
    //
    auto ZeroExtend = N->use_begin();
    if (N->use_size() != 1 || ZeroExtend->getOpcode() != ISD::ZERO_EXTEND ||
        ZeroExtend->getValueType(0) != MVT::i32)
      return SDValue();

    auto Copy = ZeroExtend->use_begin();
    if (Copy->getOpcode() == ISD::CopyToReg &&
        Copy->use_begin()->getOpcode() == ARMISD::RET_FLAG) {
      SDValue Cvt = DAG.getNode(ARMISD::VMOVrh, SDLoc(Op), MVT::i32, Op);
      DAG.ReplaceAllUsesWith(*ZeroExtend, &Cvt);
      return Cvt;
    }
    return SDValue();
  }

  if (!(SrcVT == MVT::i64 || DstVT == MVT::i64))
    return SDValue();

  // Turn i64->f64 into VMOVDRR.
  if (SrcVT == MVT::i64 && TLI.isTypeLegal(DstVT)) {
    // Do not force values to GPRs (this is what VMOVDRR does for the inputs)
    // if we can combine the bitcast with its source.
    if (SDValue Val = CombineVMOVDRRCandidateWithVecOp(N, DAG))
      return Val;

    SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, Op,
                             DAG.getConstant(0, dl, MVT::i32));
    SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, Op,
                             DAG.getConstant(1, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, DstVT,
                       DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi));
  }

  // Turn f64->i64 into VMOVRRD.
  if (DstVT == MVT::i64 && TLI.isTypeLegal(SrcVT)) {
    SDValue Cvt;
    if (DAG.getDataLayout().isBigEndian() && SrcVT.isVector() &&
        SrcVT.getVectorNumElements() > 1)
      Cvt = DAG.getNode(ARMISD::VMOVRRD, dl,
                        DAG.getVTList(MVT::i32, MVT::i32),
                        DAG.getNode(ARMISD::VREV64, dl, SrcVT, Op));
    else
      Cvt = DAG.getNode(ARMISD::VMOVRRD, dl,
                        DAG.getVTList(MVT::i32, MVT::i32), Op);
    // Merge the pieces into a single i64 value.
    return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Cvt, Cvt.getValue(1));
  }

  return SDValue();
}

/// getZeroVector - Returns a vector of specified type with all zero elements.
/// Zero vectors are used to represent vector negation and in those cases
/// will be implemented with the NEON VNEG instruction.  However, VNEG does
/// not support i64 elements, so sometimes the zero vectors will need to be
/// explicitly constructed.  Regardless, use a canonical VMOV to create the
/// zero vector.
static SDValue getZeroVector(EVT VT, SelectionDAG &DAG, const SDLoc &dl) {
  assert(VT.isVector() && "Expected a vector type");
  // The canonical modified immediate encoding of a zero vector is....0!
  SDValue EncodedVal = DAG.getTargetConstant(0, dl, MVT::i32);
  EVT VmovVT = VT.is128BitVector() ? MVT::v4i32 : MVT::v2i32;
  SDValue Vmov = DAG.getNode(ARMISD::VMOVIMM, dl, VmovVT, EncodedVal);
  return DAG.getNode(ISD::BITCAST, dl, VT, Vmov);
}

/// LowerShiftRightParts - Lower SRA_PARTS, which returns two
/// i32 values and take a 2 x i32 value to shift plus a shift amount.
SDValue ARMTargetLowering::LowerShiftRightParts(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);
  SDValue ARMcc;
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  unsigned Opc = (Op.getOpcode() == ISD::SRA_PARTS) ? ISD::SRA : ISD::SRL;

  assert(Op.getOpcode() == ISD::SRA_PARTS || Op.getOpcode() == ISD::SRL_PARTS);

  SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                 DAG.getConstant(VTBits, dl, MVT::i32), ShAmt);
  SDValue Tmp1 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, ShAmt);
  SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                   DAG.getConstant(VTBits, dl, MVT::i32));
  SDValue Tmp2 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, RevShAmt);
  SDValue LoSmallShift = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
  SDValue LoBigShift = DAG.getNode(Opc, dl, VT, ShOpHi, ExtraShAmt);
  SDValue CmpLo = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                            ISD::SETGE, ARMcc, DAG, dl);
  SDValue Lo = DAG.getNode(ARMISD::CMOV, dl, VT, LoSmallShift, LoBigShift,
                           ARMcc, CCR, CmpLo);

  SDValue HiSmallShift = DAG.getNode(Opc, dl, VT, ShOpHi, ShAmt);
  SDValue HiBigShift = Opc == ISD::SRA
                           ? DAG.getNode(Opc, dl, VT, ShOpHi,
                                         DAG.getConstant(VTBits - 1, dl, VT))
                           : DAG.getConstant(0, dl, VT);
  SDValue CmpHi = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                            ISD::SETGE, ARMcc, DAG, dl);
  SDValue Hi = DAG.getNode(ARMISD::CMOV, dl, VT, HiSmallShift, HiBigShift,
                           ARMcc, CCR, CmpHi);

  SDValue Ops[2] = { Lo, Hi };
  return DAG.getMergeValues(Ops, dl);
}

/// LowerShiftLeftParts - Lower SHL_PARTS, which returns two
/// i32 values and take a 2 x i32 value to shift plus a shift amount.
SDValue ARMTargetLowering::LowerShiftLeftParts(SDValue Op,
                                               SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);
  SDValue ARMcc;
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);

  assert(Op.getOpcode() == ISD::SHL_PARTS);
  SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                 DAG.getConstant(VTBits, dl, MVT::i32), ShAmt);
  SDValue Tmp1 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, RevShAmt);
  SDValue Tmp2 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, ShAmt);
  SDValue HiSmallShift = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);

  SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                   DAG.getConstant(VTBits, dl, MVT::i32));
  SDValue HiBigShift = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ExtraShAmt);
  SDValue CmpHi = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                            ISD::SETGE, ARMcc, DAG, dl);
  SDValue Hi = DAG.getNode(ARMISD::CMOV, dl, VT, HiSmallShift, HiBigShift,
                           ARMcc, CCR, CmpHi);

  SDValue CmpLo = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                          ISD::SETGE, ARMcc, DAG, dl);
  SDValue LoSmallShift = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ShAmt);
  SDValue Lo = DAG.getNode(ARMISD::CMOV, dl, VT, LoSmallShift,
                           DAG.getConstant(0, dl, VT), ARMcc, CCR, CmpLo);

  SDValue Ops[2] = { Lo, Hi };
  return DAG.getMergeValues(Ops, dl);
}

SDValue ARMTargetLowering::LowerFLT_ROUNDS_(SDValue Op,
                                            SelectionDAG &DAG) const {
  // The rounding mode is in bits 23:22 of the FPSCR.
  // The ARM rounding mode value to FLT_ROUNDS mapping is 0->1, 1->2, 2->3, 3->0
  // The formula we use to implement this is (((FPSCR + 1 << 22) >> 22) & 3)
  // so that the shift + and get folded into a bitfield extract.
  SDLoc dl(Op);
  SDValue Ops[] = { DAG.getEntryNode(),
                    DAG.getConstant(Intrinsic::arm_get_fpscr, dl, MVT::i32) };

  SDValue FPSCR = DAG.getNode(ISD::INTRINSIC_W_CHAIN, dl, MVT::i32, Ops);
  SDValue FltRounds = DAG.getNode(ISD::ADD, dl, MVT::i32, FPSCR,
                                  DAG.getConstant(1U << 22, dl, MVT::i32));
  SDValue RMODE = DAG.getNode(ISD::SRL, dl, MVT::i32, FltRounds,
                              DAG.getConstant(22, dl, MVT::i32));
  return DAG.getNode(ISD::AND, dl, MVT::i32, RMODE,
                     DAG.getConstant(3, dl, MVT::i32));
}

static SDValue LowerCTTZ(SDNode *N, SelectionDAG &DAG,
                         const ARMSubtarget *ST) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  if (VT.isVector()) {
    assert(ST->hasNEON());

    // Compute the least significant set bit: LSB = X & -X
    SDValue X = N->getOperand(0);
    SDValue NX = DAG.getNode(ISD::SUB, dl, VT, getZeroVector(VT, DAG, dl), X);
    SDValue LSB = DAG.getNode(ISD::AND, dl, VT, X, NX);

    EVT ElemTy = VT.getVectorElementType();

    if (ElemTy == MVT::i8) {
      // Compute with: cttz(x) = ctpop(lsb - 1)
      SDValue One = DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                                DAG.getTargetConstant(1, dl, ElemTy));
      SDValue Bits = DAG.getNode(ISD::SUB, dl, VT, LSB, One);
      return DAG.getNode(ISD::CTPOP, dl, VT, Bits);
    }

    if ((ElemTy == MVT::i16 || ElemTy == MVT::i32) &&
        (N->getOpcode() == ISD::CTTZ_ZERO_UNDEF)) {
      // Compute with: cttz(x) = (width - 1) - ctlz(lsb), if x != 0
      unsigned NumBits = ElemTy.getSizeInBits();
      SDValue WidthMinus1 =
          DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                      DAG.getTargetConstant(NumBits - 1, dl, ElemTy));
      SDValue CTLZ = DAG.getNode(ISD::CTLZ, dl, VT, LSB);
      return DAG.getNode(ISD::SUB, dl, VT, WidthMinus1, CTLZ);
    }

    // Compute with: cttz(x) = ctpop(lsb - 1)

    // Compute LSB - 1.
    SDValue Bits;
    if (ElemTy == MVT::i64) {
      // Load constant 0xffff'ffff'ffff'ffff to register.
      SDValue FF = DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                               DAG.getTargetConstant(0x1eff, dl, MVT::i32));
      Bits = DAG.getNode(ISD::ADD, dl, VT, LSB, FF);
    } else {
      SDValue One = DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                                DAG.getTargetConstant(1, dl, ElemTy));
      Bits = DAG.getNode(ISD::SUB, dl, VT, LSB, One);
    }
    return DAG.getNode(ISD::CTPOP, dl, VT, Bits);
  }

  if (!ST->hasV6T2Ops())
    return SDValue();

  SDValue rbit = DAG.getNode(ISD::BITREVERSE, dl, VT, N->getOperand(0));
  return DAG.getNode(ISD::CTLZ, dl, VT, rbit);
}

static SDValue LowerCTPOP(SDNode *N, SelectionDAG &DAG,
                          const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  assert(ST->hasNEON() && "Custom ctpop lowering requires NEON.");
  assert((VT == MVT::v1i64 || VT == MVT::v2i64 || VT == MVT::v2i32 ||
          VT == MVT::v4i32 || VT == MVT::v4i16 || VT == MVT::v8i16) &&
         "Unexpected type for custom ctpop lowering");

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT8Bit = VT.is64BitVector() ? MVT::v8i8 : MVT::v16i8;
  SDValue Res = DAG.getBitcast(VT8Bit, N->getOperand(0));
  Res = DAG.getNode(ISD::CTPOP, DL, VT8Bit, Res);

  // Widen v8i8/v16i8 CTPOP result to VT by repeatedly widening pairwise adds.
  unsigned EltSize = 8;
  unsigned NumElts = VT.is64BitVector() ? 8 : 16;
  while (EltSize != VT.getScalarSizeInBits()) {
    SmallVector<SDValue, 8> Ops;
    Ops.push_back(DAG.getConstant(Intrinsic::arm_neon_vpaddlu, DL,
                                  TLI.getPointerTy(DAG.getDataLayout())));
    Ops.push_back(Res);

    EltSize *= 2;
    NumElts /= 2;
    MVT WidenVT = MVT::getVectorVT(MVT::getIntegerVT(EltSize), NumElts);
    Res = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, WidenVT, Ops);
  }

  return Res;
}

static SDValue LowerShift(SDNode *N, SelectionDAG &DAG,
                          const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  if (!VT.isVector())
    return SDValue();

  // Lower vector shifts on NEON to use VSHL.
  assert(ST->hasNEON() && "unexpected vector shift");

  // Left shifts translate directly to the vshiftu intrinsic.
  if (N->getOpcode() == ISD::SHL)
    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT,
                       DAG.getConstant(Intrinsic::arm_neon_vshiftu, dl,
                                       MVT::i32),
                       N->getOperand(0), N->getOperand(1));

  assert((N->getOpcode() == ISD::SRA ||
          N->getOpcode() == ISD::SRL) && "unexpected vector shift opcode");

  // NEON uses the same intrinsics for both left and right shifts.  For
  // right shifts, the shift amounts are negative, so negate the vector of
  // shift amounts.
  EVT ShiftVT = N->getOperand(1).getValueType();
  SDValue NegatedCount = DAG.getNode(ISD::SUB, dl, ShiftVT,
                                     getZeroVector(ShiftVT, DAG, dl),
                                     N->getOperand(1));
  Intrinsic::ID vshiftInt = (N->getOpcode() == ISD::SRA ?
                             Intrinsic::arm_neon_vshifts :
                             Intrinsic::arm_neon_vshiftu);
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT,
                     DAG.getConstant(vshiftInt, dl, MVT::i32),
                     N->getOperand(0), NegatedCount);
}

static SDValue Expand64BitShift(SDNode *N, SelectionDAG &DAG,
                                const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  // We can get here for a node like i32 = ISD::SHL i32, i64
  if (VT != MVT::i64)
    return SDValue();

  assert((N->getOpcode() == ISD::SRL || N->getOpcode() == ISD::SRA) &&
         "Unknown shift to lower!");

  // We only lower SRA, SRL of 1 here, all others use generic lowering.
  if (!isOneConstant(N->getOperand(1)))
    return SDValue();

  // If we are in thumb mode, we don't have RRX.
  if (ST->isThumb1Only()) return SDValue();

  // Okay, we have a 64-bit SRA or SRL of 1.  Lower this to an RRX expr.
  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, N->getOperand(0),
                           DAG.getConstant(0, dl, MVT::i32));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32, N->getOperand(0),
                           DAG.getConstant(1, dl, MVT::i32));

  // First, build a SRA_FLAG/SRL_FLAG op, which shifts the top part by one and
  // captures the result into a carry flag.
  unsigned Opc = N->getOpcode() == ISD::SRL ? ARMISD::SRL_FLAG:ARMISD::SRA_FLAG;
  Hi = DAG.getNode(Opc, dl, DAG.getVTList(MVT::i32, MVT::Glue), Hi);

  // The low part is an ARMISD::RRX operand, which shifts the carry in.
  Lo = DAG.getNode(ARMISD::RRX, dl, MVT::i32, Lo, Hi.getValue(1));

  // Merge the pieces into a single i64 value.
 return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);
}

static SDValue LowerVSETCC(SDValue Op, SelectionDAG &DAG) {
  SDValue TmpOp0, TmpOp1;
  bool Invert = false;
  bool Swap = false;
  unsigned Opc = 0;

  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  SDValue CC = Op.getOperand(2);
  EVT CmpVT = Op0.getValueType().changeVectorElementTypeToInteger();
  EVT VT = Op.getValueType();
  ISD::CondCode SetCCOpcode = cast<CondCodeSDNode>(CC)->get();
  SDLoc dl(Op);

  if (Op0.getValueType().getVectorElementType() == MVT::i64 &&
      (SetCCOpcode == ISD::SETEQ || SetCCOpcode == ISD::SETNE)) {
    // Special-case integer 64-bit equality comparisons. They aren't legal,
    // but they can be lowered with a few vector instructions.
    unsigned CmpElements = CmpVT.getVectorNumElements() * 2;
    EVT SplitVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32, CmpElements);
    SDValue CastOp0 = DAG.getNode(ISD::BITCAST, dl, SplitVT, Op0);
    SDValue CastOp1 = DAG.getNode(ISD::BITCAST, dl, SplitVT, Op1);
    SDValue Cmp = DAG.getNode(ISD::SETCC, dl, SplitVT, CastOp0, CastOp1,
                              DAG.getCondCode(ISD::SETEQ));
    SDValue Reversed = DAG.getNode(ARMISD::VREV64, dl, SplitVT, Cmp);
    SDValue Merged = DAG.getNode(ISD::AND, dl, SplitVT, Cmp, Reversed);
    Merged = DAG.getNode(ISD::BITCAST, dl, CmpVT, Merged);
    if (SetCCOpcode == ISD::SETNE)
      Merged = DAG.getNOT(dl, Merged, CmpVT);
    Merged = DAG.getSExtOrTrunc(Merged, dl, VT);
    return Merged;
  }

  if (CmpVT.getVectorElementType() == MVT::i64)
    // 64-bit comparisons are not legal in general.
    return SDValue();

  if (Op1.getValueType().isFloatingPoint()) {
    switch (SetCCOpcode) {
    default: llvm_unreachable("Illegal FP comparison");
    case ISD::SETUNE:
    case ISD::SETNE:  Invert = true; LLVM_FALLTHROUGH;
    case ISD::SETOEQ:
    case ISD::SETEQ:  Opc = ARMISD::VCEQ; break;
    case ISD::SETOLT:
    case ISD::SETLT: Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETOGT:
    case ISD::SETGT:  Opc = ARMISD::VCGT; break;
    case ISD::SETOLE:
    case ISD::SETLE:  Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETOGE:
    case ISD::SETGE: Opc = ARMISD::VCGE; break;
    case ISD::SETUGE: Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETULE: Invert = true; Opc = ARMISD::VCGT; break;
    case ISD::SETUGT: Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETULT: Invert = true; Opc = ARMISD::VCGE; break;
    case ISD::SETUEQ: Invert = true; LLVM_FALLTHROUGH;
    case ISD::SETONE:
      // Expand this to (OLT | OGT).
      TmpOp0 = Op0;
      TmpOp1 = Op1;
      Opc = ISD::OR;
      Op0 = DAG.getNode(ARMISD::VCGT, dl, CmpVT, TmpOp1, TmpOp0);
      Op1 = DAG.getNode(ARMISD::VCGT, dl, CmpVT, TmpOp0, TmpOp1);
      break;
    case ISD::SETUO:
      Invert = true;
      LLVM_FALLTHROUGH;
    case ISD::SETO:
      // Expand this to (OLT | OGE).
      TmpOp0 = Op0;
      TmpOp1 = Op1;
      Opc = ISD::OR;
      Op0 = DAG.getNode(ARMISD::VCGT, dl, CmpVT, TmpOp1, TmpOp0);
      Op1 = DAG.getNode(ARMISD::VCGE, dl, CmpVT, TmpOp0, TmpOp1);
      break;
    }
  } else {
    // Integer comparisons.
    switch (SetCCOpcode) {
    default: llvm_unreachable("Illegal integer comparison");
    case ISD::SETNE:  Invert = true; LLVM_FALLTHROUGH;
    case ISD::SETEQ:  Opc = ARMISD::VCEQ; break;
    case ISD::SETLT:  Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETGT:  Opc = ARMISD::VCGT; break;
    case ISD::SETLE:  Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETGE:  Opc = ARMISD::VCGE; break;
    case ISD::SETULT: Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETUGT: Opc = ARMISD::VCGTU; break;
    case ISD::SETULE: Swap = true; LLVM_FALLTHROUGH;
    case ISD::SETUGE: Opc = ARMISD::VCGEU; break;
    }

    // Detect VTST (Vector Test Bits) = icmp ne (and (op0, op1), zero).
    if (Opc == ARMISD::VCEQ) {
      SDValue AndOp;
      if (ISD::isBuildVectorAllZeros(Op1.getNode()))
        AndOp = Op0;
      else if (ISD::isBuildVectorAllZeros(Op0.getNode()))
        AndOp = Op1;

      // Ignore bitconvert.
      if (AndOp.getNode() && AndOp.getOpcode() == ISD::BITCAST)
        AndOp = AndOp.getOperand(0);

      if (AndOp.getNode() && AndOp.getOpcode() == ISD::AND) {
        Opc = ARMISD::VTST;
        Op0 = DAG.getNode(ISD::BITCAST, dl, CmpVT, AndOp.getOperand(0));
        Op1 = DAG.getNode(ISD::BITCAST, dl, CmpVT, AndOp.getOperand(1));
        Invert = !Invert;
      }
    }
  }

  if (Swap)
    std::swap(Op0, Op1);

  // If one of the operands is a constant vector zero, attempt to fold the
  // comparison to a specialized compare-against-zero form.
  SDValue SingleOp;
  if (ISD::isBuildVectorAllZeros(Op1.getNode()))
    SingleOp = Op0;
  else if (ISD::isBuildVectorAllZeros(Op0.getNode())) {
    if (Opc == ARMISD::VCGE)
      Opc = ARMISD::VCLEZ;
    else if (Opc == ARMISD::VCGT)
      Opc = ARMISD::VCLTZ;
    SingleOp = Op1;
  }

  SDValue Result;
  if (SingleOp.getNode()) {
    switch (Opc) {
    case ARMISD::VCEQ:
      Result = DAG.getNode(ARMISD::VCEQZ, dl, CmpVT, SingleOp); break;
    case ARMISD::VCGE:
      Result = DAG.getNode(ARMISD::VCGEZ, dl, CmpVT, SingleOp); break;
    case ARMISD::VCLEZ:
      Result = DAG.getNode(ARMISD::VCLEZ, dl, CmpVT, SingleOp); break;
    case ARMISD::VCGT:
      Result = DAG.getNode(ARMISD::VCGTZ, dl, CmpVT, SingleOp); break;
    case ARMISD::VCLTZ:
      Result = DAG.getNode(ARMISD::VCLTZ, dl, CmpVT, SingleOp); break;
    default:
      Result = DAG.getNode(Opc, dl, CmpVT, Op0, Op1);
    }
  } else {
     Result = DAG.getNode(Opc, dl, CmpVT, Op0, Op1);
  }

  Result = DAG.getSExtOrTrunc(Result, dl, VT);

  if (Invert)
    Result = DAG.getNOT(dl, Result, VT);

  return Result;
}

static SDValue LowerSETCCCARRY(SDValue Op, SelectionDAG &DAG) {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue Carry = Op.getOperand(2);
  SDValue Cond = Op.getOperand(3);
  SDLoc DL(Op);

  assert(LHS.getSimpleValueType().isInteger() && "SETCCCARRY is integer only.");

  // ARMISD::SUBE expects a carry not a borrow like ISD::SUBCARRY so we
  // have to invert the carry first.
  Carry = DAG.getNode(ISD::SUB, DL, MVT::i32,
                      DAG.getConstant(1, DL, MVT::i32), Carry);
  // This converts the boolean value carry into the carry flag.
  Carry = ConvertBooleanCarryToCarryFlag(Carry, DAG);

  SDVTList VTs = DAG.getVTList(LHS.getValueType(), MVT::i32);
  SDValue Cmp = DAG.getNode(ARMISD::SUBE, DL, VTs, LHS, RHS, Carry);

  SDValue FVal = DAG.getConstant(0, DL, MVT::i32);
  SDValue TVal = DAG.getConstant(1, DL, MVT::i32);
  SDValue ARMcc = DAG.getConstant(
      IntCCToARMCC(cast<CondCodeSDNode>(Cond)->get()), DL, MVT::i32);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDValue Chain = DAG.getCopyToReg(DAG.getEntryNode(), DL, ARM::CPSR,
                                   Cmp.getValue(1), SDValue());
  return DAG.getNode(ARMISD::CMOV, DL, Op.getValueType(), FVal, TVal, ARMcc,
                     CCR, Chain.getValue(1));
}

/// isNEONModifiedImm - Check if the specified splat value corresponds to a
/// valid vector constant for a NEON instruction with a "modified immediate"
/// operand (e.g., VMOV).  If so, return the encoded value.
static SDValue isNEONModifiedImm(uint64_t SplatBits, uint64_t SplatUndef,
                                 unsigned SplatBitSize, SelectionDAG &DAG,
                                 const SDLoc &dl, EVT &VT, bool is128Bits,
                                 NEONModImmType type) {
  unsigned OpCmode, Imm;

  // SplatBitSize is set to the smallest size that splats the vector, so a
  // zero vector will always have SplatBitSize == 8.  However, NEON modified
  // immediate instructions others than VMOV do not support the 8-bit encoding
  // of a zero vector, and the default encoding of zero is supposed to be the
  // 32-bit version.
  if (SplatBits == 0)
    SplatBitSize = 32;

  switch (SplatBitSize) {
  case 8:
    if (type != VMOVModImm)
      return SDValue();
    // Any 1-byte value is OK.  Op=0, Cmode=1110.
    assert((SplatBits & ~0xff) == 0 && "one byte splat value is too big");
    OpCmode = 0xe;
    Imm = SplatBits;
    VT = is128Bits ? MVT::v16i8 : MVT::v8i8;
    break;

  case 16:
    // NEON's 16-bit VMOV supports splat values where only one byte is nonzero.
    VT = is128Bits ? MVT::v8i16 : MVT::v4i16;
    if ((SplatBits & ~0xff) == 0) {
      // Value = 0x00nn: Op=x, Cmode=100x.
      OpCmode = 0x8;
      Imm = SplatBits;
      break;
    }
    if ((SplatBits & ~0xff00) == 0) {
      // Value = 0xnn00: Op=x, Cmode=101x.
      OpCmode = 0xa;
      Imm = SplatBits >> 8;
      break;
    }
    return SDValue();

  case 32:
    // NEON's 32-bit VMOV supports splat values where:
    // * only one byte is nonzero, or
    // * the least significant byte is 0xff and the second byte is nonzero, or
    // * the least significant 2 bytes are 0xff and the third is nonzero.
    VT = is128Bits ? MVT::v4i32 : MVT::v2i32;
    if ((SplatBits & ~0xff) == 0) {
      // Value = 0x000000nn: Op=x, Cmode=000x.
      OpCmode = 0;
      Imm = SplatBits;
      break;
    }
    if ((SplatBits & ~0xff00) == 0) {
      // Value = 0x0000nn00: Op=x, Cmode=001x.
      OpCmode = 0x2;
      Imm = SplatBits >> 8;
      break;
    }
    if ((SplatBits & ~0xff0000) == 0) {
      // Value = 0x00nn0000: Op=x, Cmode=010x.
      OpCmode = 0x4;
      Imm = SplatBits >> 16;
      break;
    }
    if ((SplatBits & ~0xff000000) == 0) {
      // Value = 0xnn000000: Op=x, Cmode=011x.
      OpCmode = 0x6;
      Imm = SplatBits >> 24;
      break;
    }

    // cmode == 0b1100 and cmode == 0b1101 are not supported for VORR or VBIC
    if (type == OtherModImm) return SDValue();

    if ((SplatBits & ~0xffff) == 0 &&
        ((SplatBits | SplatUndef) & 0xff) == 0xff) {
      // Value = 0x0000nnff: Op=x, Cmode=1100.
      OpCmode = 0xc;
      Imm = SplatBits >> 8;
      break;
    }

    if ((SplatBits & ~0xffffff) == 0 &&
        ((SplatBits | SplatUndef) & 0xffff) == 0xffff) {
      // Value = 0x00nnffff: Op=x, Cmode=1101.
      OpCmode = 0xd;
      Imm = SplatBits >> 16;
      break;
    }

    // Note: there are a few 32-bit splat values (specifically: 00ffff00,
    // ff000000, ff0000ff, and ffff00ff) that are valid for VMOV.I64 but not
    // VMOV.I32.  A (very) minor optimization would be to replicate the value
    // and fall through here to test for a valid 64-bit splat.  But, then the
    // caller would also need to check and handle the change in size.
    return SDValue();

  case 64: {
    if (type != VMOVModImm)
      return SDValue();
    // NEON has a 64-bit VMOV splat where each byte is either 0 or 0xff.
    uint64_t BitMask = 0xff;
    uint64_t Val = 0;
    unsigned ImmMask = 1;
    Imm = 0;
    for (int ByteNum = 0; ByteNum < 8; ++ByteNum) {
      if (((SplatBits | SplatUndef) & BitMask) == BitMask) {
        Val |= BitMask;
        Imm |= ImmMask;
      } else if ((SplatBits & BitMask) != 0) {
        return SDValue();
      }
      BitMask <<= 8;
      ImmMask <<= 1;
    }

    if (DAG.getDataLayout().isBigEndian())
      // swap higher and lower 32 bit word
      Imm = ((Imm & 0xf) << 4) | ((Imm & 0xf0) >> 4);

    // Op=1, Cmode=1110.
    OpCmode = 0x1e;
    VT = is128Bits ? MVT::v2i64 : MVT::v1i64;
    break;
  }

  default:
    llvm_unreachable("unexpected size for isNEONModifiedImm");
  }

  unsigned EncodedVal = ARM_AM::createNEONModImm(OpCmode, Imm);
  return DAG.getTargetConstant(EncodedVal, dl, MVT::i32);
}

SDValue ARMTargetLowering::LowerConstantFP(SDValue Op, SelectionDAG &DAG,
                                           const ARMSubtarget *ST) const {
  EVT VT = Op.getValueType();
  bool IsDouble = (VT == MVT::f64);
  ConstantFPSDNode *CFP = cast<ConstantFPSDNode>(Op);
  const APFloat &FPVal = CFP->getValueAPF();

  // Prevent floating-point constants from using literal loads
  // when execute-only is enabled.
  if (ST->genExecuteOnly()) {
    // If we can represent the constant as an immediate, don't lower it
    if (isFPImmLegal(FPVal, VT))
      return Op;
    // Otherwise, construct as integer, and move to float register
    APInt INTVal = FPVal.bitcastToAPInt();
    SDLoc DL(CFP);
    switch (VT.getSimpleVT().SimpleTy) {
      default:
        llvm_unreachable("Unknown floating point type!");
        break;
      case MVT::f64: {
        SDValue Lo = DAG.getConstant(INTVal.trunc(32), DL, MVT::i32);
        SDValue Hi = DAG.getConstant(INTVal.lshr(32).trunc(32), DL, MVT::i32);
        if (!ST->isLittle())
          std::swap(Lo, Hi);
        return DAG.getNode(ARMISD::VMOVDRR, DL, MVT::f64, Lo, Hi);
      }
      case MVT::f32:
          return DAG.getNode(ARMISD::VMOVSR, DL, VT,
              DAG.getConstant(INTVal, DL, MVT::i32));
    }
  }

  if (!ST->hasVFP3())
    return SDValue();

  // Use the default (constant pool) lowering for double constants when we have
  // an SP-only FPU
  if (IsDouble && Subtarget->isFPOnlySP())
    return SDValue();

  // Try splatting with a VMOV.f32...
  int ImmVal = IsDouble ? ARM_AM::getFP64Imm(FPVal) : ARM_AM::getFP32Imm(FPVal);

  if (ImmVal != -1) {
    if (IsDouble || !ST->useNEONForSinglePrecisionFP()) {
      // We have code in place to select a valid ConstantFP already, no need to
      // do any mangling.
      return Op;
    }

    // It's a float and we are trying to use NEON operations where
    // possible. Lower it to a splat followed by an extract.
    SDLoc DL(Op);
    SDValue NewVal = DAG.getTargetConstant(ImmVal, DL, MVT::i32);
    SDValue VecConstant = DAG.getNode(ARMISD::VMOVFPIMM, DL, MVT::v2f32,
                                      NewVal);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, VecConstant,
                       DAG.getConstant(0, DL, MVT::i32));
  }

  // The rest of our options are NEON only, make sure that's allowed before
  // proceeding..
  if (!ST->hasNEON() || (!IsDouble && !ST->useNEONForSinglePrecisionFP()))
    return SDValue();

  EVT VMovVT;
  uint64_t iVal = FPVal.bitcastToAPInt().getZExtValue();

  // It wouldn't really be worth bothering for doubles except for one very
  // important value, which does happen to match: 0.0. So make sure we don't do
  // anything stupid.
  if (IsDouble && (iVal & 0xffffffff) != (iVal >> 32))
    return SDValue();

  // Try a VMOV.i32 (FIXME: i8, i16, or i64 could work too).
  SDValue NewVal = isNEONModifiedImm(iVal & 0xffffffffU, 0, 32, DAG, SDLoc(Op),
                                     VMovVT, false, VMOVModImm);
  if (NewVal != SDValue()) {
    SDLoc DL(Op);
    SDValue VecConstant = DAG.getNode(ARMISD::VMOVIMM, DL, VMovVT,
                                      NewVal);
    if (IsDouble)
      return DAG.getNode(ISD::BITCAST, DL, MVT::f64, VecConstant);

    // It's a float: cast and extract a vector element.
    SDValue VecFConstant = DAG.getNode(ISD::BITCAST, DL, MVT::v2f32,
                                       VecConstant);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, VecFConstant,
                       DAG.getConstant(0, DL, MVT::i32));
  }

  // Finally, try a VMVN.i32
  NewVal = isNEONModifiedImm(~iVal & 0xffffffffU, 0, 32, DAG, SDLoc(Op), VMovVT,
                             false, VMVNModImm);
  if (NewVal != SDValue()) {
    SDLoc DL(Op);
    SDValue VecConstant = DAG.getNode(ARMISD::VMVNIMM, DL, VMovVT, NewVal);

    if (IsDouble)
      return DAG.getNode(ISD::BITCAST, DL, MVT::f64, VecConstant);

    // It's a float: cast and extract a vector element.
    SDValue VecFConstant = DAG.getNode(ISD::BITCAST, DL, MVT::v2f32,
                                       VecConstant);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, VecFConstant,
                       DAG.getConstant(0, DL, MVT::i32));
  }

  return SDValue();
}

// check if an VEXT instruction can handle the shuffle mask when the
// vector sources of the shuffle are the same.
static bool isSingletonVEXTMask(ArrayRef<int> M, EVT VT, unsigned &Imm) {
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

    if (M[i] < 0) continue; // ignore UNDEF indices
    if (ExpectedElt != static_cast<unsigned>(M[i]))
      return false;
  }

  return true;
}

static bool isVEXTMask(ArrayRef<int> M, EVT VT,
                       bool &ReverseVEXT, unsigned &Imm) {
  unsigned NumElts = VT.getVectorNumElements();
  ReverseVEXT = false;

  // Assume that the first shuffle index is not UNDEF.  Fail if it is.
  if (M[0] < 0)
    return false;

  Imm = M[0];

  // If this is a VEXT shuffle, the immediate value is the index of the first
  // element.  The other shuffle indices must be the successive elements after
  // the first one.
  unsigned ExpectedElt = Imm;
  for (unsigned i = 1; i < NumElts; ++i) {
    // Increment the expected index.  If it wraps around, it may still be
    // a VEXT but the source vectors must be swapped.
    ExpectedElt += 1;
    if (ExpectedElt == NumElts * 2) {
      ExpectedElt = 0;
      ReverseVEXT = true;
    }

    if (M[i] < 0) continue; // ignore UNDEF indices
    if (ExpectedElt != static_cast<unsigned>(M[i]))
      return false;
  }

  // Adjust the index value if the source operands will be swapped.
  if (ReverseVEXT)
    Imm -= NumElts;

  return true;
}

/// isVREVMask - Check if a vector shuffle corresponds to a VREV
/// instruction with the specified blocksize.  (The order of the elements
/// within each block of the vector is reversed.)
static bool isVREVMask(ArrayRef<int> M, EVT VT, unsigned BlockSize) {
  assert((BlockSize==16 || BlockSize==32 || BlockSize==64) &&
         "Only possible block sizes for VREV are: 16, 32, 64");

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
    if (M[i] < 0) continue; // ignore UNDEF indices
    if ((unsigned) M[i] != (i - i%BlockElts) + (BlockElts - 1 - i%BlockElts))
      return false;
  }

  return true;
}

static bool isVTBLMask(ArrayRef<int> M, EVT VT) {
  // We can handle <8 x i8> vector shuffles. If the index in the mask is out of
  // range, then 0 is placed into the resulting vector. So pretty much any mask
  // of 8 elements can work here.
  return VT == MVT::v8i8 && M.size() == 8;
}

static unsigned SelectPairHalf(unsigned Elements, ArrayRef<int> Mask,
                               unsigned Index) {
  if (Mask.size() == Elements * 2)
    return Index / Elements;
  return Mask[Index] == 0 ? 0 : 1;
}

// Checks whether the shuffle mask represents a vector transpose (VTRN) by
// checking that pairs of elements in the shuffle mask represent the same index
// in each vector, incrementing the expected index by 2 at each step.
// e.g. For v1,v2 of type v4i32 a valid shuffle mask is: [0, 4, 2, 6]
//  v1={a,b,c,d} => x=shufflevector v1, v2 shufflemask => x={a,e,c,g}
//  v2={e,f,g,h}
// WhichResult gives the offset for each element in the mask based on which
// of the two results it belongs to.
//
// The transpose can be represented either as:
// result1 = shufflevector v1, v2, result1_shuffle_mask
// result2 = shufflevector v1, v2, result2_shuffle_mask
// where v1/v2 and the shuffle masks have the same number of elements
// (here WhichResult (see below) indicates which result is being checked)
//
// or as:
// results = shufflevector v1, v2, shuffle_mask
// where both results are returned in one vector and the shuffle mask has twice
// as many elements as v1/v2 (here WhichResult will always be 0 if true) here we
// want to check the low half and high half of the shuffle mask as if it were
// the other case
static bool isVTRNMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  // If the mask is twice as long as the input vector then we need to check the
  // upper and lower parts of the mask with a matching value for WhichResult
  // FIXME: A mask with only even values will be rejected in case the first
  // element is undefined, e.g. [-1, 4, 2, 6] will be rejected, because only
  // M[0] is used to determine WhichResult
  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != j + WhichResult) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != j + NumElts + WhichResult))
        return false;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  return true;
}

/// isVTRN_v_undef_Mask - Special case of isVTRNMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 0, 2, 2> instead of <0, 4, 2, 6>.
static bool isVTRN_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult){
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != j + WhichResult) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != j + WhichResult))
        return false;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  return true;
}

// Checks whether the shuffle mask represents a vector unzip (VUZP) by checking
// that the mask elements are either all even and in steps of size 2 or all odd
// and in steps of size 2.
// e.g. For v1,v2 of type v4i32 a valid shuffle mask is: [0, 2, 4, 6]
//  v1={a,b,c,d} => x=shufflevector v1, v2 shufflemask => x={a,c,e,g}
//  v2={e,f,g,h}
// Requires similar checks to that of isVTRNMask with
// respect the how results are returned.
static bool isVUZPMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; ++j) {
      if (M[i+j] >= 0 && (unsigned) M[i+j] != 2 * j + WhichResult)
        return false;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VUZP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

/// isVUZP_v_undef_Mask - Special case of isVUZPMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 2, 0, 2> instead of <0, 2, 4, 6>,
static bool isVUZP_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult){
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  unsigned Half = NumElts / 2;
  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; j += Half) {
      unsigned Idx = WhichResult;
      for (unsigned k = 0; k < Half; ++k) {
        int MIdx = M[i + j + k];
        if (MIdx >= 0 && (unsigned) MIdx != Idx)
          return false;
        Idx += 2;
      }
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VUZP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

// Checks whether the shuffle mask represents a vector zip (VZIP) by checking
// that pairs of elements of the shufflemask represent the same index in each
// vector incrementing sequentially through the vectors.
// e.g. For v1,v2 of type v4i32 a valid shuffle mask is: [0, 4, 1, 5]
//  v1={a,b,c,d} => x=shufflevector v1, v2 shufflemask => x={a,e,b,f}
//  v2={e,f,g,h}
// Requires similar checks to that of isVTRNMask with respect the how results
// are returned.
static bool isVZIPMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    unsigned Idx = WhichResult * NumElts / 2;
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != Idx) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != Idx + NumElts))
        return false;
      Idx += 1;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VZIP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

/// isVZIP_v_undef_Mask - Special case of isVZIPMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 0, 1, 1> instead of <0, 4, 1, 5>.
static bool isVZIP_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult){
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    unsigned Idx = WhichResult * NumElts / 2;
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != Idx) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != Idx))
        return false;
      Idx += 1;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VZIP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

/// Check if \p ShuffleMask is a NEON two-result shuffle (VZIP, VUZP, VTRN),
/// and return the corresponding ARMISD opcode if it is, or 0 if it isn't.
static unsigned isNEONTwoResultShuffleMask(ArrayRef<int> ShuffleMask, EVT VT,
                                           unsigned &WhichResult,
                                           bool &isV_UNDEF) {
  isV_UNDEF = false;
  if (isVTRNMask(ShuffleMask, VT, WhichResult))
    return ARMISD::VTRN;
  if (isVUZPMask(ShuffleMask, VT, WhichResult))
    return ARMISD::VUZP;
  if (isVZIPMask(ShuffleMask, VT, WhichResult))
    return ARMISD::VZIP;

  isV_UNDEF = true;
  if (isVTRN_v_undef_Mask(ShuffleMask, VT, WhichResult))
    return ARMISD::VTRN;
  if (isVUZP_v_undef_Mask(ShuffleMask, VT, WhichResult))
    return ARMISD::VUZP;
  if (isVZIP_v_undef_Mask(ShuffleMask, VT, WhichResult))
    return ARMISD::VZIP;

  return 0;
}

/// \return true if this is a reverse operation on an vector.
static bool isReverseMask(ArrayRef<int> M, EVT VT) {
  unsigned NumElts = VT.getVectorNumElements();
  // Make sure the mask has the right size.
  if (NumElts != M.size())
      return false;

  // Look for <15, ..., 3, -1, 1, 0>.
  for (unsigned i = 0; i != NumElts; ++i)
    if (M[i] >= 0 && M[i] != (int) (NumElts - 1 - i))
      return false;

  return true;
}

// If N is an integer constant that can be moved into a register in one
// instruction, return an SDValue of such a constant (will become a MOV
// instruction).  Otherwise return null.
static SDValue IsSingleInstrConstant(SDValue N, SelectionDAG &DAG,
                                     const ARMSubtarget *ST, const SDLoc &dl) {
  uint64_t Val;
  if (!isa<ConstantSDNode>(N))
    return SDValue();
  Val = cast<ConstantSDNode>(N)->getZExtValue();

  if (ST->isThumb1Only()) {
    if (Val <= 255 || ~Val <= 255)
      return DAG.getConstant(Val, dl, MVT::i32);
  } else {
    if (ARM_AM::getSOImmVal(Val) != -1 || ARM_AM::getSOImmVal(~Val) != -1)
      return DAG.getConstant(Val, dl, MVT::i32);
  }
  return SDValue();
}

// If this is a case we can't handle, return null and let the default
// expansion code take care of it.
SDValue ARMTargetLowering::LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG,
                                             const ARMSubtarget *ST) const {
  BuildVectorSDNode *BVN = cast<BuildVectorSDNode>(Op.getNode());
  SDLoc dl(Op);
  EVT VT = Op.getValueType();

  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs)) {
    if (SplatUndef.isAllOnesValue())
      return DAG.getUNDEF(VT);

    if (SplatBitSize <= 64) {
      // Check if an immediate VMOV works.
      EVT VmovVT;
      SDValue Val = isNEONModifiedImm(SplatBits.getZExtValue(),
                                      SplatUndef.getZExtValue(), SplatBitSize,
                                      DAG, dl, VmovVT, VT.is128BitVector(),
                                      VMOVModImm);
      if (Val.getNode()) {
        SDValue Vmov = DAG.getNode(ARMISD::VMOVIMM, dl, VmovVT, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vmov);
      }

      // Try an immediate VMVN.
      uint64_t NegatedImm = (~SplatBits).getZExtValue();
      Val = isNEONModifiedImm(NegatedImm,
                                      SplatUndef.getZExtValue(), SplatBitSize,
                                      DAG, dl, VmovVT, VT.is128BitVector(),
                                      VMVNModImm);
      if (Val.getNode()) {
        SDValue Vmov = DAG.getNode(ARMISD::VMVNIMM, dl, VmovVT, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vmov);
      }

      // Use vmov.f32 to materialize other v2f32 and v4f32 splats.
      if ((VT == MVT::v2f32 || VT == MVT::v4f32) && SplatBitSize == 32) {
        int ImmVal = ARM_AM::getFP32Imm(SplatBits);
        if (ImmVal != -1) {
          SDValue Val = DAG.getTargetConstant(ImmVal, dl, MVT::i32);
          return DAG.getNode(ARMISD::VMOVFPIMM, dl, VT, Val);
        }
      }
    }
  }

  // Scan through the operands to see if only one value is used.
  //
  // As an optimisation, even if more than one value is used it may be more
  // profitable to splat with one value then change some lanes.
  //
  // Heuristically we decide to do this if the vector has a "dominant" value,
  // defined as splatted to more than half of the lanes.
  unsigned NumElts = VT.getVectorNumElements();
  bool isOnlyLowElement = true;
  bool usesOnlyOneValue = true;
  bool hasDominantValue = false;
  bool isConstant = true;

  // Map of the number of times a particular SDValue appears in the
  // element list.
  DenseMap<SDValue, unsigned> ValueCounts;
  SDValue Value;
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (V.isUndef())
      continue;
    if (i > 0)
      isOnlyLowElement = false;
    if (!isa<ConstantFPSDNode>(V) && !isa<ConstantSDNode>(V))
      isConstant = false;

    ValueCounts.insert(std::make_pair(V, 0));
    unsigned &Count = ValueCounts[V];

    // Is this value dominant? (takes up more than half of the lanes)
    if (++Count > (NumElts / 2)) {
      hasDominantValue = true;
      Value = V;
    }
  }
  if (ValueCounts.size() != 1)
    usesOnlyOneValue = false;
  if (!Value.getNode() && !ValueCounts.empty())
    Value = ValueCounts.begin()->first;

  if (ValueCounts.empty())
    return DAG.getUNDEF(VT);

  // Loads are better lowered with insert_vector_elt/ARMISD::BUILD_VECTOR.
  // Keep going if we are hitting this case.
  if (isOnlyLowElement && !ISD::isNormalLoad(Value.getNode()))
    return DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value);

  unsigned EltSize = VT.getScalarSizeInBits();

  // Use VDUP for non-constant splats.  For f32 constant splats, reduce to
  // i32 and try again.
  if (hasDominantValue && EltSize <= 32) {
    if (!isConstant) {
      SDValue N;

      // If we are VDUPing a value that comes directly from a vector, that will
      // cause an unnecessary move to and from a GPR, where instead we could
      // just use VDUPLANE. We can only do this if the lane being extracted
      // is at a constant index, as the VDUP from lane instructions only have
      // constant-index forms.
      ConstantSDNode *constIndex;
      if (Value->getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
          (constIndex = dyn_cast<ConstantSDNode>(Value->getOperand(1)))) {
        // We need to create a new undef vector to use for the VDUPLANE if the
        // size of the vector from which we get the value is different than the
        // size of the vector that we need to create. We will insert the element
        // such that the register coalescer will remove unnecessary copies.
        if (VT != Value->getOperand(0).getValueType()) {
          unsigned index = constIndex->getAPIntValue().getLimitedValue() %
                             VT.getVectorNumElements();
          N =  DAG.getNode(ARMISD::VDUPLANE, dl, VT,
                 DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, DAG.getUNDEF(VT),
                        Value, DAG.getConstant(index, dl, MVT::i32)),
                           DAG.getConstant(index, dl, MVT::i32));
        } else
          N = DAG.getNode(ARMISD::VDUPLANE, dl, VT,
                        Value->getOperand(0), Value->getOperand(1));
      } else
        N = DAG.getNode(ARMISD::VDUP, dl, VT, Value);

      if (!usesOnlyOneValue) {
        // The dominant value was splatted as 'N', but we now have to insert
        // all differing elements.
        for (unsigned I = 0; I < NumElts; ++I) {
          if (Op.getOperand(I) == Value)
            continue;
          SmallVector<SDValue, 3> Ops;
          Ops.push_back(N);
          Ops.push_back(Op.getOperand(I));
          Ops.push_back(DAG.getConstant(I, dl, MVT::i32));
          N = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, Ops);
        }
      }
      return N;
    }
    if (VT.getVectorElementType().isFloatingPoint()) {
      SmallVector<SDValue, 8> Ops;
      for (unsigned i = 0; i < NumElts; ++i)
        Ops.push_back(DAG.getNode(ISD::BITCAST, dl, MVT::i32,
                                  Op.getOperand(i)));
      EVT VecVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32, NumElts);
      SDValue Val = DAG.getBuildVector(VecVT, dl, Ops);
      Val = LowerBUILD_VECTOR(Val, DAG, ST);
      if (Val.getNode())
        return DAG.getNode(ISD::BITCAST, dl, VT, Val);
    }
    if (usesOnlyOneValue) {
      SDValue Val = IsSingleInstrConstant(Value, DAG, ST, dl);
      if (isConstant && Val.getNode())
        return DAG.getNode(ARMISD::VDUP, dl, VT, Val);
    }
  }

  // If all elements are constants and the case above didn't get hit, fall back
  // to the default expansion, which will generate a load from the constant
  // pool.
  if (isConstant)
    return SDValue();

  // Empirical tests suggest this is rarely worth it for vectors of length <= 2.
  if (NumElts >= 4) {
    SDValue shuffle = ReconstructShuffle(Op, DAG);
    if (shuffle != SDValue())
      return shuffle;
  }

  if (VT.is128BitVector() && VT != MVT::v2f64 && VT != MVT::v4f32) {
    // If we haven't found an efficient lowering, try splitting a 128-bit vector
    // into two 64-bit vectors; we might discover a better way to lower it.
    SmallVector<SDValue, 64> Ops(Op->op_begin(), Op->op_begin() + NumElts);
    EVT ExtVT = VT.getVectorElementType();
    EVT HVT = EVT::getVectorVT(*DAG.getContext(), ExtVT, NumElts / 2);
    SDValue Lower =
        DAG.getBuildVector(HVT, dl, makeArrayRef(&Ops[0], NumElts / 2));
    if (Lower.getOpcode() == ISD::BUILD_VECTOR)
      Lower = LowerBUILD_VECTOR(Lower, DAG, ST);
    SDValue Upper = DAG.getBuildVector(
        HVT, dl, makeArrayRef(&Ops[NumElts / 2], NumElts / 2));
    if (Upper.getOpcode() == ISD::BUILD_VECTOR)
      Upper = LowerBUILD_VECTOR(Upper, DAG, ST);
    if (Lower && Upper)
      return DAG.getNode(ISD::CONCAT_VECTORS, dl, VT, Lower, Upper);
  }

  // Vectors with 32- or 64-bit elements can be built by directly assigning
  // the subregisters.  Lower it to an ARMISD::BUILD_VECTOR so the operands
  // will be legalized.
  if (EltSize >= 32) {
    // Do the expansion with floating-point types, since that is what the VFP
    // registers are defined to use, and since i64 is not legal.
    EVT EltVT = EVT::getFloatingPointVT(EltSize);
    EVT VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NumElts);
    SmallVector<SDValue, 8> Ops;
    for (unsigned i = 0; i < NumElts; ++i)
      Ops.push_back(DAG.getNode(ISD::BITCAST, dl, EltVT, Op.getOperand(i)));
    SDValue Val = DAG.getNode(ARMISD::BUILD_VECTOR, dl, VecVT, Ops);
    return DAG.getNode(ISD::BITCAST, dl, VT, Val);
  }

  // If all else fails, just use a sequence of INSERT_VECTOR_ELT when we
  // know the default expansion would otherwise fall back on something even
  // worse. For a vector with one or two non-undef values, that's
  // scalar_to_vector for the elements followed by a shuffle (provided the
  // shuffle is valid for the target) and materialization element by element
  // on the stack followed by a load for everything else.
  if (!isConstant && !usesOnlyOneValue) {
    SDValue Vec = DAG.getUNDEF(VT);
    for (unsigned i = 0 ; i < NumElts; ++i) {
      SDValue V = Op.getOperand(i);
      if (V.isUndef())
        continue;
      SDValue LaneIdx = DAG.getConstant(i, dl, MVT::i32);
      Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, Vec, V, LaneIdx);
    }
    return Vec;
  }

  return SDValue();
}

// Gather data to see if the operation can be modelled as a
// shuffle in combination with VEXTs.
SDValue ARMTargetLowering::ReconstructShuffle(SDValue Op,
                                              SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::BUILD_VECTOR && "Unknown opcode!");
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  unsigned NumElts = VT.getVectorNumElements();

  struct ShuffleSourceInfo {
    SDValue Vec;
    unsigned MinElt = std::numeric_limits<unsigned>::max();
    unsigned MaxElt = 0;

    // We may insert some combination of BITCASTs and VEXT nodes to force Vec to
    // be compatible with the shuffle we intend to construct. As a result
    // ShuffleVec will be some sliding window into the original Vec.
    SDValue ShuffleVec;

    // Code should guarantee that element i in Vec starts at element "WindowBase
    // + i * WindowScale in ShuffleVec".
    int WindowBase = 0;
    int WindowScale = 1;

    ShuffleSourceInfo(SDValue Vec) : Vec(Vec), ShuffleVec(Vec) {}

    bool operator ==(SDValue OtherVec) { return Vec == OtherVec; }
  };

  // First gather all vectors used as an immediate source for this BUILD_VECTOR
  // node.
  SmallVector<ShuffleSourceInfo, 2> Sources;
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (V.isUndef())
      continue;
    else if (V.getOpcode() != ISD::EXTRACT_VECTOR_ELT) {
      // A shuffle can only come from building a vector from various
      // elements of other vectors.
      return SDValue();
    } else if (!isa<ConstantSDNode>(V.getOperand(1))) {
      // Furthermore, shuffles require a constant mask, whereas extractelts
      // accept variable indices.
      return SDValue();
    }

    // Add this element source to the list if it's not already there.
    SDValue SourceVec = V.getOperand(0);
    auto Source = llvm::find(Sources, SourceVec);
    if (Source == Sources.end())
      Source = Sources.insert(Sources.end(), ShuffleSourceInfo(SourceVec));

    // Update the minimum and maximum lane number seen.
    unsigned EltNo = cast<ConstantSDNode>(V.getOperand(1))->getZExtValue();
    Source->MinElt = std::min(Source->MinElt, EltNo);
    Source->MaxElt = std::max(Source->MaxElt, EltNo);
  }

  // Currently only do something sane when at most two source vectors
  // are involved.
  if (Sources.size() > 2)
    return SDValue();

  // Find out the smallest element size among result and two sources, and use
  // it as element size to build the shuffle_vector.
  EVT SmallestEltTy = VT.getVectorElementType();
  for (auto &Source : Sources) {
    EVT SrcEltTy = Source.Vec.getValueType().getVectorElementType();
    if (SrcEltTy.bitsLT(SmallestEltTy))
      SmallestEltTy = SrcEltTy;
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
      if (2 * SrcVT.getSizeInBits() != VT.getSizeInBits())
        return SDValue();
      // We can pad out the smaller vector for free, so if it's part of a
      // shuffle...
      Src.ShuffleVec =
          DAG.getNode(ISD::CONCAT_VECTORS, dl, DestVT, Src.ShuffleVec,
                      DAG.getUNDEF(Src.ShuffleVec.getValueType()));
      continue;
    }

    if (SrcVT.getSizeInBits() != 2 * VT.getSizeInBits())
      return SDValue();

    if (Src.MaxElt - Src.MinElt >= NumSrcElts) {
      // Span too large for a VEXT to cope
      return SDValue();
    }

    if (Src.MinElt >= NumSrcElts) {
      // The extraction can just take the second half
      Src.ShuffleVec =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(NumSrcElts, dl, MVT::i32));
      Src.WindowBase = -NumSrcElts;
    } else if (Src.MaxElt < NumSrcElts) {
      // The extraction can just take the first half
      Src.ShuffleVec =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(0, dl, MVT::i32));
    } else {
      // An actual VEXT is needed
      SDValue VEXTSrc1 =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(0, dl, MVT::i32));
      SDValue VEXTSrc2 =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(NumSrcElts, dl, MVT::i32));

      Src.ShuffleVec = DAG.getNode(ARMISD::VEXT, dl, DestVT, VEXTSrc1,
                                   VEXTSrc2,
                                   DAG.getConstant(Src.MinElt, dl, MVT::i32));
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

    auto Src = llvm::find(Sources, Entry.getOperand(0));
    int EltNo = cast<ConstantSDNode>(Entry.getOperand(1))->getSExtValue();

    // EXTRACT_VECTOR_ELT performs an implicit any_ext; BUILD_VECTOR an implicit
    // trunc. So only std::min(SrcBits, DestBits) actually get defined in this
    // segment.
    EVT OrigEltTy = Entry.getOperand(0).getValueType().getVectorElementType();
    int BitsDefined = std::min(OrigEltTy.getSizeInBits(),
                               VT.getScalarSizeInBits());
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
  if (!isShuffleMaskLegal(Mask, ShuffleVT))
    return SDValue();

  // We can't handle more than two sources. This should have already
  // been checked before this point.
  assert(Sources.size() <= 2 && "Too many sources!");

  SDValue ShuffleOps[] = { DAG.getUNDEF(ShuffleVT), DAG.getUNDEF(ShuffleVT) };
  for (unsigned i = 0; i < Sources.size(); ++i)
    ShuffleOps[i] = Sources[i].ShuffleVec;

  SDValue Shuffle = DAG.getVectorShuffle(ShuffleVT, dl, ShuffleOps[0],
                                         ShuffleOps[1], Mask);
  return DAG.getNode(ISD::BITCAST, dl, VT, Shuffle);
}

/// isShuffleMaskLegal - Targets can use this to indicate that they only
/// support *some* VECTOR_SHUFFLE operations, those with specific masks.
/// By default, if a target supports the VECTOR_SHUFFLE node, all mask values
/// are assumed to be legal.
bool ARMTargetLowering::isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const {
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
    unsigned PFTableIndex =
      PFIndexes[0]*9*9*9+PFIndexes[1]*9*9+PFIndexes[2]*9+PFIndexes[3];
    unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
    unsigned Cost = (PFEntry >> 30);

    if (Cost <= 4)
      return true;
  }

  bool ReverseVEXT, isV_UNDEF;
  unsigned Imm, WhichResult;

  unsigned EltSize = VT.getScalarSizeInBits();
  return (EltSize >= 32 ||
          ShuffleVectorSDNode::isSplatMask(&M[0], VT) ||
          isVREVMask(M, VT, 64) ||
          isVREVMask(M, VT, 32) ||
          isVREVMask(M, VT, 16) ||
          isVEXTMask(M, VT, ReverseVEXT, Imm) ||
          isVTBLMask(M, VT) ||
          isNEONTwoResultShuffleMask(M, VT, WhichResult, isV_UNDEF) ||
          ((VT == MVT::v8i16 || VT == MVT::v16i8) && isReverseMask(M, VT)));
}

/// GeneratePerfectShuffle - Given an entry in the perfect-shuffle table, emit
/// the specified operations to build the shuffle.
static SDValue GeneratePerfectShuffle(unsigned PFEntry, SDValue LHS,
                                      SDValue RHS, SelectionDAG &DAG,
                                      const SDLoc &dl) {
  unsigned OpNum = (PFEntry >> 26) & 0x0F;
  unsigned LHSID = (PFEntry >> 13) & ((1 << 13)-1);
  unsigned RHSID = (PFEntry >>  0) & ((1 << 13)-1);

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
    if (LHSID == (1*9+2)*9+3) return LHS;
    assert(LHSID == ((4*9+5)*9+6)*9+7 && "Illegal OP_COPY!");
    return RHS;
  }

  SDValue OpLHS, OpRHS;
  OpLHS = GeneratePerfectShuffle(PerfectShuffleTable[LHSID], LHS, RHS, DAG, dl);
  OpRHS = GeneratePerfectShuffle(PerfectShuffleTable[RHSID], LHS, RHS, DAG, dl);
  EVT VT = OpLHS.getValueType();

  switch (OpNum) {
  default: llvm_unreachable("Unknown shuffle opcode!");
  case OP_VREV:
    // VREV divides the vector in half and swaps within the half.
    if (VT.getVectorElementType() == MVT::i32 ||
        VT.getVectorElementType() == MVT::f32)
      return DAG.getNode(ARMISD::VREV64, dl, VT, OpLHS);
    // vrev <4 x i16> -> VREV32
    if (VT.getVectorElementType() == MVT::i16)
      return DAG.getNode(ARMISD::VREV32, dl, VT, OpLHS);
    // vrev <4 x i8> -> VREV16
    assert(VT.getVectorElementType() == MVT::i8);
    return DAG.getNode(ARMISD::VREV16, dl, VT, OpLHS);
  case OP_VDUP0:
  case OP_VDUP1:
  case OP_VDUP2:
  case OP_VDUP3:
    return DAG.getNode(ARMISD::VDUPLANE, dl, VT,
                       OpLHS, DAG.getConstant(OpNum-OP_VDUP0, dl, MVT::i32));
  case OP_VEXT1:
  case OP_VEXT2:
  case OP_VEXT3:
    return DAG.getNode(ARMISD::VEXT, dl, VT,
                       OpLHS, OpRHS,
                       DAG.getConstant(OpNum - OP_VEXT1 + 1, dl, MVT::i32));
  case OP_VUZPL:
  case OP_VUZPR:
    return DAG.getNode(ARMISD::VUZP, dl, DAG.getVTList(VT, VT),
                       OpLHS, OpRHS).getValue(OpNum-OP_VUZPL);
  case OP_VZIPL:
  case OP_VZIPR:
    return DAG.getNode(ARMISD::VZIP, dl, DAG.getVTList(VT, VT),
                       OpLHS, OpRHS).getValue(OpNum-OP_VZIPL);
  case OP_VTRNL:
  case OP_VTRNR:
    return DAG.getNode(ARMISD::VTRN, dl, DAG.getVTList(VT, VT),
                       OpLHS, OpRHS).getValue(OpNum-OP_VTRNL);
  }
}

static SDValue LowerVECTOR_SHUFFLEv8i8(SDValue Op,
                                       ArrayRef<int> ShuffleMask,
                                       SelectionDAG &DAG) {
  // Check to see if we can use the VTBL instruction.
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc DL(Op);

  SmallVector<SDValue, 8> VTBLMask;
  for (ArrayRef<int>::iterator
         I = ShuffleMask.begin(), E = ShuffleMask.end(); I != E; ++I)
    VTBLMask.push_back(DAG.getConstant(*I, DL, MVT::i32));

  if (V2.getNode()->isUndef())
    return DAG.getNode(ARMISD::VTBL1, DL, MVT::v8i8, V1,
                       DAG.getBuildVector(MVT::v8i8, DL, VTBLMask));

  return DAG.getNode(ARMISD::VTBL2, DL, MVT::v8i8, V1, V2,
                     DAG.getBuildVector(MVT::v8i8, DL, VTBLMask));
}

static SDValue LowerReverse_VECTOR_SHUFFLEv16i8_v8i16(SDValue Op,
                                                      SelectionDAG &DAG) {
  SDLoc DL(Op);
  SDValue OpLHS = Op.getOperand(0);
  EVT VT = OpLHS.getValueType();

  assert((VT == MVT::v8i16 || VT == MVT::v16i8) &&
         "Expect an v8i16/v16i8 type");
  OpLHS = DAG.getNode(ARMISD::VREV64, DL, VT, OpLHS);
  // For a v16i8 type: After the VREV, we have got <8, ...15, 8, ..., 0>. Now,
  // extract the first 8 bytes into the top double word and the last 8 bytes
  // into the bottom double word. The v8i16 case is similar.
  unsigned ExtractNum = (VT == MVT::v16i8) ? 8 : 4;
  return DAG.getNode(ARMISD::VEXT, DL, VT, OpLHS, OpLHS,
                     DAG.getConstant(ExtractNum, DL, MVT::i32));
}

static SDValue LowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) {
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(Op.getNode());

  // Convert shuffles that are directly supported on NEON to target-specific
  // DAG nodes, instead of keeping them as shuffles and matching them again
  // during code selection.  This is more efficient and avoids the possibility
  // of inconsistencies between legalization and selection.
  // FIXME: floating-point vectors should be canonicalized to integer vectors
  // of the same time so that they get CSEd properly.
  ArrayRef<int> ShuffleMask = SVN->getMask();

  unsigned EltSize = VT.getScalarSizeInBits();
  if (EltSize <= 32) {
    if (SVN->isSplat()) {
      int Lane = SVN->getSplatIndex();
      // If this is undef splat, generate it via "just" vdup, if possible.
      if (Lane == -1) Lane = 0;

      // Test if V1 is a SCALAR_TO_VECTOR.
      if (Lane == 0 && V1.getOpcode() == ISD::SCALAR_TO_VECTOR) {
        return DAG.getNode(ARMISD::VDUP, dl, VT, V1.getOperand(0));
      }
      // Test if V1 is a BUILD_VECTOR which is equivalent to a SCALAR_TO_VECTOR
      // (and probably will turn into a SCALAR_TO_VECTOR once legalization
      // reaches it).
      if (Lane == 0 && V1.getOpcode() == ISD::BUILD_VECTOR &&
          !isa<ConstantSDNode>(V1.getOperand(0))) {
        bool IsScalarToVector = true;
        for (unsigned i = 1, e = V1.getNumOperands(); i != e; ++i)
          if (!V1.getOperand(i).isUndef()) {
            IsScalarToVector = false;
            break;
          }
        if (IsScalarToVector)
          return DAG.getNode(ARMISD::VDUP, dl, VT, V1.getOperand(0));
      }
      return DAG.getNode(ARMISD::VDUPLANE, dl, VT, V1,
                         DAG.getConstant(Lane, dl, MVT::i32));
    }

    bool ReverseVEXT;
    unsigned Imm;
    if (isVEXTMask(ShuffleMask, VT, ReverseVEXT, Imm)) {
      if (ReverseVEXT)
        std::swap(V1, V2);
      return DAG.getNode(ARMISD::VEXT, dl, VT, V1, V2,
                         DAG.getConstant(Imm, dl, MVT::i32));
    }

    if (isVREVMask(ShuffleMask, VT, 64))
      return DAG.getNode(ARMISD::VREV64, dl, VT, V1);
    if (isVREVMask(ShuffleMask, VT, 32))
      return DAG.getNode(ARMISD::VREV32, dl, VT, V1);
    if (isVREVMask(ShuffleMask, VT, 16))
      return DAG.getNode(ARMISD::VREV16, dl, VT, V1);

    if (V2->isUndef() && isSingletonVEXTMask(ShuffleMask, VT, Imm)) {
      return DAG.getNode(ARMISD::VEXT, dl, VT, V1, V1,
                         DAG.getConstant(Imm, dl, MVT::i32));
    }

    // Check for Neon shuffles that modify both input vectors in place.
    // If both results are used, i.e., if there are two shuffles with the same
    // source operands and with masks corresponding to both results of one of
    // these operations, DAG memoization will ensure that a single node is
    // used for both shuffles.
    unsigned WhichResult;
    bool isV_UNDEF;
    if (unsigned ShuffleOpc = isNEONTwoResultShuffleMask(
            ShuffleMask, VT, WhichResult, isV_UNDEF)) {
      if (isV_UNDEF)
        V2 = V1;
      return DAG.getNode(ShuffleOpc, dl, DAG.getVTList(VT, VT), V1, V2)
          .getValue(WhichResult);
    }

    // Also check for these shuffles through CONCAT_VECTORS: we canonicalize
    // shuffles that produce a result larger than their operands with:
    //   shuffle(concat(v1, undef), concat(v2, undef))
    // ->
    //   shuffle(concat(v1, v2), undef)
    // because we can access quad vectors (see PerformVECTOR_SHUFFLECombine).
    //
    // This is useful in the general case, but there are special cases where
    // native shuffles produce larger results: the two-result ops.
    //
    // Look through the concat when lowering them:
    //   shuffle(concat(v1, v2), undef)
    // ->
    //   concat(VZIP(v1, v2):0, :1)
    //
    if (V1->getOpcode() == ISD::CONCAT_VECTORS && V2->isUndef()) {
      SDValue SubV1 = V1->getOperand(0);
      SDValue SubV2 = V1->getOperand(1);
      EVT SubVT = SubV1.getValueType();

      // We expect these to have been canonicalized to -1.
      assert(llvm::all_of(ShuffleMask, [&](int i) {
        return i < (int)VT.getVectorNumElements();
      }) && "Unexpected shuffle index into UNDEF operand!");

      if (unsigned ShuffleOpc = isNEONTwoResultShuffleMask(
              ShuffleMask, SubVT, WhichResult, isV_UNDEF)) {
        if (isV_UNDEF)
          SubV2 = SubV1;
        assert((WhichResult == 0) &&
               "In-place shuffle of concat can only have one result!");
        SDValue Res = DAG.getNode(ShuffleOpc, dl, DAG.getVTList(SubVT, SubVT),
                                  SubV1, SubV2);
        return DAG.getNode(ISD::CONCAT_VECTORS, dl, VT, Res.getValue(0),
                           Res.getValue(1));
      }
    }
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
    unsigned PFTableIndex =
      PFIndexes[0]*9*9*9+PFIndexes[1]*9*9+PFIndexes[2]*9+PFIndexes[3];
    unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
    unsigned Cost = (PFEntry >> 30);

    if (Cost <= 4)
      return GeneratePerfectShuffle(PFEntry, V1, V2, DAG, dl);
  }

  // Implement shuffles with 32- or 64-bit elements as ARMISD::BUILD_VECTORs.
  if (EltSize >= 32) {
    // Do the expansion with floating-point types, since that is what the VFP
    // registers are defined to use, and since i64 is not legal.
    EVT EltVT = EVT::getFloatingPointVT(EltSize);
    EVT VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NumElts);
    V1 = DAG.getNode(ISD::BITCAST, dl, VecVT, V1);
    V2 = DAG.getNode(ISD::BITCAST, dl, VecVT, V2);
    SmallVector<SDValue, 8> Ops;
    for (unsigned i = 0; i < NumElts; ++i) {
      if (ShuffleMask[i] < 0)
        Ops.push_back(DAG.getUNDEF(EltVT));
      else
        Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT,
                                  ShuffleMask[i] < (int)NumElts ? V1 : V2,
                                  DAG.getConstant(ShuffleMask[i] & (NumElts-1),
                                                  dl, MVT::i32)));
    }
    SDValue Val = DAG.getNode(ARMISD::BUILD_VECTOR, dl, VecVT, Ops);
    return DAG.getNode(ISD::BITCAST, dl, VT, Val);
  }

  if ((VT == MVT::v8i16 || VT == MVT::v16i8) && isReverseMask(ShuffleMask, VT))
    return LowerReverse_VECTOR_SHUFFLEv16i8_v8i16(Op, DAG);

  if (VT == MVT::v8i8)
    if (SDValue NewOp = LowerVECTOR_SHUFFLEv8i8(Op, ShuffleMask, DAG))
      return NewOp;

  return SDValue();
}

static SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) {
  // INSERT_VECTOR_ELT is legal only for immediate indexes.
  SDValue Lane = Op.getOperand(2);
  if (!isa<ConstantSDNode>(Lane))
    return SDValue();

  return Op;
}

static SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) {
  // EXTRACT_VECTOR_ELT is legal only for immediate indexes.
  SDValue Lane = Op.getOperand(1);
  if (!isa<ConstantSDNode>(Lane))
    return SDValue();

  SDValue Vec = Op.getOperand(0);
  if (Op.getValueType() == MVT::i32 && Vec.getScalarValueSizeInBits() < 32) {
    SDLoc dl(Op);
    return DAG.getNode(ARMISD::VGETLANEu, dl, MVT::i32, Vec, Lane);
  }

  return Op;
}

static SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) {
  // The only time a CONCAT_VECTORS operation can have legal types is when
  // two 64-bit vectors are concatenated to a 128-bit vector.
  assert(Op.getValueType().is128BitVector() && Op.getNumOperands() == 2 &&
         "unexpected CONCAT_VECTORS");
  SDLoc dl(Op);
  SDValue Val = DAG.getUNDEF(MVT::v2f64);
  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  if (!Op0.isUndef())
    Val = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Val,
                      DAG.getNode(ISD::BITCAST, dl, MVT::f64, Op0),
                      DAG.getIntPtrConstant(0, dl));
  if (!Op1.isUndef())
    Val = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Val,
                      DAG.getNode(ISD::BITCAST, dl, MVT::f64, Op1),
                      DAG.getIntPtrConstant(1, dl));
  return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Val);
}

/// isExtendedBUILD_VECTOR - Check if N is a constant BUILD_VECTOR where each
/// element has been zero/sign-extended, depending on the isSigned parameter,
/// from an integer type half its size.
static bool isExtendedBUILD_VECTOR(SDNode *N, SelectionDAG &DAG,
                                   bool isSigned) {
  // A v2i64 BUILD_VECTOR will have been legalized to a BITCAST from v4i32.
  EVT VT = N->getValueType(0);
  if (VT == MVT::v2i64 && N->getOpcode() == ISD::BITCAST) {
    SDNode *BVN = N->getOperand(0).getNode();
    if (BVN->getValueType(0) != MVT::v4i32 ||
        BVN->getOpcode() != ISD::BUILD_VECTOR)
      return false;
    unsigned LoElt = DAG.getDataLayout().isBigEndian() ? 1 : 0;
    unsigned HiElt = 1 - LoElt;
    ConstantSDNode *Lo0 = dyn_cast<ConstantSDNode>(BVN->getOperand(LoElt));
    ConstantSDNode *Hi0 = dyn_cast<ConstantSDNode>(BVN->getOperand(HiElt));
    ConstantSDNode *Lo1 = dyn_cast<ConstantSDNode>(BVN->getOperand(LoElt+2));
    ConstantSDNode *Hi1 = dyn_cast<ConstantSDNode>(BVN->getOperand(HiElt+2));
    if (!Lo0 || !Hi0 || !Lo1 || !Hi1)
      return false;
    if (isSigned) {
      if (Hi0->getSExtValue() == Lo0->getSExtValue() >> 32 &&
          Hi1->getSExtValue() == Lo1->getSExtValue() >> 32)
        return true;
    } else {
      if (Hi0->isNullValue() && Hi1->isNullValue())
        return true;
    }
    return false;
  }

  if (N->getOpcode() != ISD::BUILD_VECTOR)
    return false;

  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    SDNode *Elt = N->getOperand(i).getNode();
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

/// isSignExtended - Check if a node is a vector value that is sign-extended
/// or a constant BUILD_VECTOR with sign-extended elements.
static bool isSignExtended(SDNode *N, SelectionDAG &DAG) {
  if (N->getOpcode() == ISD::SIGN_EXTEND || ISD::isSEXTLoad(N))
    return true;
  if (isExtendedBUILD_VECTOR(N, DAG, true))
    return true;
  return false;
}

/// isZeroExtended - Check if a node is a vector value that is zero-extended
/// or a constant BUILD_VECTOR with zero-extended elements.
static bool isZeroExtended(SDNode *N, SelectionDAG &DAG) {
  if (N->getOpcode() == ISD::ZERO_EXTEND || ISD::isZEXTLoad(N))
    return true;
  if (isExtendedBUILD_VECTOR(N, DAG, false))
    return true;
  return false;
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

/// AddRequiredExtensionForVMULL - Add a sign/zero extension to extend the total
/// value size to 64 bits. We need a 64-bit D register as an operand to VMULL.
/// We insert the required extension here to get the vector to fill a D register.
static SDValue AddRequiredExtensionForVMULL(SDValue N, SelectionDAG &DAG,
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

/// SkipLoadExtensionForVMULL - return a load of the original vector size that
/// does not do any sign/zero extension. If the original vector is less
/// than 64 bits, an appropriate extension will be added after the load to
/// reach a total size of 64 bits. We have to add the extension separately
/// because ARM does not have a sign/zero extending load for vectors.
static SDValue SkipLoadExtensionForVMULL(LoadSDNode *LD, SelectionDAG& DAG) {
  EVT ExtendedTy = getExtensionTo64Bits(LD->getMemoryVT());

  // The load already has the right type.
  if (ExtendedTy == LD->getMemoryVT())
    return DAG.getLoad(LD->getMemoryVT(), SDLoc(LD), LD->getChain(),
                       LD->getBasePtr(), LD->getPointerInfo(),
                       LD->getAlignment(), LD->getMemOperand()->getFlags());

  // We need to create a zextload/sextload. We cannot just create a load
  // followed by a zext/zext node because LowerMUL is also run during normal
  // operation legalization where we can't create illegal types.
  return DAG.getExtLoad(LD->getExtensionType(), SDLoc(LD), ExtendedTy,
                        LD->getChain(), LD->getBasePtr(), LD->getPointerInfo(),
                        LD->getMemoryVT(), LD->getAlignment(),
                        LD->getMemOperand()->getFlags());
}

/// SkipExtensionForVMULL - For a node that is a SIGN_EXTEND, ZERO_EXTEND,
/// extending load, or BUILD_VECTOR with extended elements, return the
/// unextended value. The unextended vector should be 64 bits so that it can
/// be used as an operand to a VMULL instruction. If the original vector size
/// before extension is less than 64 bits we add a an extension to resize
/// the vector to 64 bits.
static SDValue SkipExtensionForVMULL(SDNode *N, SelectionDAG &DAG) {
  if (N->getOpcode() == ISD::SIGN_EXTEND || N->getOpcode() == ISD::ZERO_EXTEND)
    return AddRequiredExtensionForVMULL(N->getOperand(0), DAG,
                                        N->getOperand(0)->getValueType(0),
                                        N->getValueType(0),
                                        N->getOpcode());

  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    assert((ISD::isSEXTLoad(LD) || ISD::isZEXTLoad(LD)) &&
           "Expected extending load");

    SDValue newLoad = SkipLoadExtensionForVMULL(LD, DAG);
    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), newLoad.getValue(1));
    unsigned Opcode = ISD::isSEXTLoad(LD) ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    SDValue extLoad =
        DAG.getNode(Opcode, SDLoc(newLoad), LD->getValueType(0), newLoad);
    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 0), extLoad);

    return newLoad;
  }

  // Otherwise, the value must be a BUILD_VECTOR.  For v2i64, it will
  // have been legalized as a BITCAST from v4i32.
  if (N->getOpcode() == ISD::BITCAST) {
    SDNode *BVN = N->getOperand(0).getNode();
    assert(BVN->getOpcode() == ISD::BUILD_VECTOR &&
           BVN->getValueType(0) == MVT::v4i32 && "expected v4i32 BUILD_VECTOR");
    unsigned LowElt = DAG.getDataLayout().isBigEndian() ? 1 : 0;
    return DAG.getBuildVector(
        MVT::v2i32, SDLoc(N),
        {BVN->getOperand(LowElt), BVN->getOperand(LowElt + 2)});
  }
  // Construct a new BUILD_VECTOR with elements truncated to half the size.
  assert(N->getOpcode() == ISD::BUILD_VECTOR && "expected BUILD_VECTOR");
  EVT VT = N->getValueType(0);
  unsigned EltSize = VT.getScalarSizeInBits() / 2;
  unsigned NumElts = VT.getVectorNumElements();
  MVT TruncVT = MVT::getIntegerVT(EltSize);
  SmallVector<SDValue, 8> Ops;
  SDLoc dl(N);
  for (unsigned i = 0; i != NumElts; ++i) {
    ConstantSDNode *C = cast<ConstantSDNode>(N->getOperand(i));
    const APInt &CInt = C->getAPIntValue();
    // Element types smaller than 32 bits are not legal, so use i32 elements.
    // The values are implicitly truncated so sext vs. zext doesn't matter.
    Ops.push_back(DAG.getConstant(CInt.zextOrTrunc(32), dl, MVT::i32));
  }
  return DAG.getBuildVector(MVT::getVectorVT(TruncVT, NumElts), dl, Ops);
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
    NewOpc = ARMISD::VMULLs;
  else {
    bool isN0ZExt = isZeroExtended(N0, DAG);
    bool isN1ZExt = isZeroExtended(N1, DAG);
    if (isN0ZExt && isN1ZExt)
      NewOpc = ARMISD::VMULLu;
    else if (isN1SExt || isN1ZExt) {
      // Look for (s/zext A + s/zext B) * (s/zext C). We want to turn these
      // into (s/zext A * s/zext C) + (s/zext B * s/zext C)
      if (isN1SExt && isAddSubSExt(N0, DAG)) {
        NewOpc = ARMISD::VMULLs;
        isMLA = true;
      } else if (isN1ZExt && isAddSubZExt(N0, DAG)) {
        NewOpc = ARMISD::VMULLu;
        isMLA = true;
      } else if (isN0ZExt && isAddSubZExt(N1, DAG)) {
        std::swap(N0, N1);
        NewOpc = ARMISD::VMULLu;
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

  // Legalize to a VMULL instruction.
  SDLoc DL(Op);
  SDValue Op0;
  SDValue Op1 = SkipExtensionForVMULL(N1, DAG);
  if (!isMLA) {
    Op0 = SkipExtensionForVMULL(N0, DAG);
    assert(Op0.getValueType().is64BitVector() &&
           Op1.getValueType().is64BitVector() &&
           "unexpected types for extended operands to VMULL");
    return DAG.getNode(NewOpc, DL, VT, Op0, Op1);
  }

  // Optimizing (zext A + zext B) * C, to (VMULL A, C) + (VMULL B, C) during
  // isel lowering to take advantage of no-stall back to back vmul + vmla.
  //   vmull q0, d4, d6
  //   vmlal q0, d5, d6
  // is faster than
  //   vaddl q0, d4, d5
  //   vmovl q1, d6
  //   vmul  q0, q0, q1
  SDValue N00 = SkipExtensionForVMULL(N0->getOperand(0).getNode(), DAG);
  SDValue N01 = SkipExtensionForVMULL(N0->getOperand(1).getNode(), DAG);
  EVT Op1VT = Op1.getValueType();
  return DAG.getNode(N0->getOpcode(), DL, VT,
                     DAG.getNode(NewOpc, DL, VT,
                               DAG.getNode(ISD::BITCAST, DL, Op1VT, N00), Op1),
                     DAG.getNode(NewOpc, DL, VT,
                               DAG.getNode(ISD::BITCAST, DL, Op1VT, N01), Op1));
}

static SDValue LowerSDIV_v4i8(SDValue X, SDValue Y, const SDLoc &dl,
                              SelectionDAG &DAG) {
  // TODO: Should this propagate fast-math-flags?

  // Convert to float
  // float4 xf = vcvt_f32_s32(vmovl_s16(a.lo));
  // float4 yf = vcvt_f32_s32(vmovl_s16(b.lo));
  X = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, X);
  Y = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, Y);
  X = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, X);
  Y = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, Y);
  // Get reciprocal estimate.
  // float4 recip = vrecpeq_f32(yf);
  Y = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecpe, dl, MVT::i32),
                   Y);
  // Because char has a smaller range than uchar, we can actually get away
  // without any newton steps.  This requires that we use a weird bias
  // of 0xb000, however (again, this has been exhaustively tested).
  // float4 result = as_float4(as_int4(xf*recip) + 0xb000);
  X = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, X, Y);
  X = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, X);
  Y = DAG.getConstant(0xb000, dl, MVT::v4i32);
  X = DAG.getNode(ISD::ADD, dl, MVT::v4i32, X, Y);
  X = DAG.getNode(ISD::BITCAST, dl, MVT::v4f32, X);
  // Convert back to short.
  X = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::v4i32, X);
  X = DAG.getNode(ISD::TRUNCATE, dl, MVT::v4i16, X);
  return X;
}

static SDValue LowerSDIV_v4i16(SDValue N0, SDValue N1, const SDLoc &dl,
                               SelectionDAG &DAG) {
  // TODO: Should this propagate fast-math-flags?

  SDValue N2;
  // Convert to float.
  // float4 yf = vcvt_f32_s32(vmovl_s16(y));
  // float4 xf = vcvt_f32_s32(vmovl_s16(x));
  N0 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, N0);
  N1 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, N1);
  N0 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N0);
  N1 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N1);

  // Use reciprocal estimate and one refinement step.
  // float4 recip = vrecpeq_f32(yf);
  // recip *= vrecpsq_f32(yf, recip);
  N2 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecpe, dl, MVT::i32),
                   N1);
  N1 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecps, dl, MVT::i32),
                   N1, N2);
  N2 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N1, N2);
  // Because short has a smaller range than ushort, we can actually get away
  // with only a single newton step.  This requires that we use a weird bias
  // of 89, however (again, this has been exhaustively tested).
  // float4 result = as_float4(as_int4(xf*recip) + 0x89);
  N0 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N0, N2);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, N0);
  N1 = DAG.getConstant(0x89, dl, MVT::v4i32);
  N0 = DAG.getNode(ISD::ADD, dl, MVT::v4i32, N0, N1);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4f32, N0);
  // Convert back to integer and return.
  // return vmovn_s32(vcvt_s32_f32(result));
  N0 = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::v4i32, N0);
  N0 = DAG.getNode(ISD::TRUNCATE, dl, MVT::v4i16, N0);
  return N0;
}

static SDValue LowerSDIV(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  assert((VT == MVT::v4i16 || VT == MVT::v8i8) &&
         "unexpected type for custom-lowering ISD::SDIV");

  SDLoc dl(Op);
  SDValue N0 = Op.getOperand(0);
  SDValue N1 = Op.getOperand(1);
  SDValue N2, N3;

  if (VT == MVT::v8i8) {
    N0 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v8i16, N0);
    N1 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v8i16, N1);

    N2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(4, dl));
    N3 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(4, dl));
    N0 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(0, dl));
    N1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(0, dl));

    N0 = LowerSDIV_v4i8(N0, N1, dl, DAG); // v4i16
    N2 = LowerSDIV_v4i8(N2, N3, dl, DAG); // v4i16

    N0 = DAG.getNode(ISD::CONCAT_VECTORS, dl, MVT::v8i16, N0, N2);
    N0 = LowerCONCAT_VECTORS(N0, DAG);

    N0 = DAG.getNode(ISD::TRUNCATE, dl, MVT::v8i8, N0);
    return N0;
  }
  return LowerSDIV_v4i16(N0, N1, dl, DAG);
}

static SDValue LowerUDIV(SDValue Op, SelectionDAG &DAG) {
  // TODO: Should this propagate fast-math-flags?
  EVT VT = Op.getValueType();
  assert((VT == MVT::v4i16 || VT == MVT::v8i8) &&
         "unexpected type for custom-lowering ISD::UDIV");

  SDLoc dl(Op);
  SDValue N0 = Op.getOperand(0);
  SDValue N1 = Op.getOperand(1);
  SDValue N2, N3;

  if (VT == MVT::v8i8) {
    N0 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v8i16, N0);
    N1 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v8i16, N1);

    N2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(4, dl));
    N3 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(4, dl));
    N0 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(0, dl));
    N1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(0, dl));

    N0 = LowerSDIV_v4i16(N0, N1, dl, DAG); // v4i16
    N2 = LowerSDIV_v4i16(N2, N3, dl, DAG); // v4i16

    N0 = DAG.getNode(ISD::CONCAT_VECTORS, dl, MVT::v8i16, N0, N2);
    N0 = LowerCONCAT_VECTORS(N0, DAG);

    N0 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v8i8,
                     DAG.getConstant(Intrinsic::arm_neon_vqmovnsu, dl,
                                     MVT::i32),
                     N0);
    return N0;
  }

  // v4i16 sdiv ... Convert to float.
  // float4 yf = vcvt_f32_s32(vmovl_u16(y));
  // float4 xf = vcvt_f32_s32(vmovl_u16(x));
  N0 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v4i32, N0);
  N1 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v4i32, N1);
  N0 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N0);
  SDValue BN1 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N1);

  // Use reciprocal estimate and two refinement steps.
  // float4 recip = vrecpeq_f32(yf);
  // recip *= vrecpsq_f32(yf, recip);
  // recip *= vrecpsq_f32(yf, recip);
  N2 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecpe, dl, MVT::i32),
                   BN1);
  N1 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecps, dl, MVT::i32),
                   BN1, N2);
  N2 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N1, N2);
  N1 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecps, dl, MVT::i32),
                   BN1, N2);
  N2 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N1, N2);
  // Simply multiplying by the reciprocal estimate can leave us a few ulps
  // too low, so we add 2 ulps (exhaustive testing shows that this is enough,
  // and that it will never cause us to return an answer too large).
  // float4 result = as_float4(as_int4(xf*recip) + 2);
  N0 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N0, N2);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, N0);
  N1 = DAG.getConstant(2, dl, MVT::v4i32);
  N0 = DAG.getNode(ISD::ADD, dl, MVT::v4i32, N0, N1);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4f32, N0);
  // Convert back to integer and return.
  // return vmovn_u32(vcvt_s32_f32(result));
  N0 = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::v4i32, N0);
  N0 = DAG.getNode(ISD::TRUNCATE, dl, MVT::v4i16, N0);
  return N0;
}

static SDValue LowerADDSUBCARRY(SDValue Op, SelectionDAG &DAG) {
  SDNode *N = Op.getNode();
  EVT VT = N->getValueType(0);
  SDVTList VTs = DAG.getVTList(VT, MVT::i32);

  SDValue Carry = Op.getOperand(2);

  SDLoc DL(Op);

  SDValue Result;
  if (Op.getOpcode() == ISD::ADDCARRY) {
    // This converts the boolean value carry into the carry flag.
    Carry = ConvertBooleanCarryToCarryFlag(Carry, DAG);

    // Do the addition proper using the carry flag we wanted.
    Result = DAG.getNode(ARMISD::ADDE, DL, VTs, Op.getOperand(0),
                         Op.getOperand(1), Carry);

    // Now convert the carry flag into a boolean value.
    Carry = ConvertCarryFlagToBooleanCarry(Result.getValue(1), VT, DAG);
  } else {
    // ARMISD::SUBE expects a carry not a borrow like ISD::SUBCARRY so we
    // have to invert the carry first.
    Carry = DAG.getNode(ISD::SUB, DL, MVT::i32,
                        DAG.getConstant(1, DL, MVT::i32), Carry);
    // This converts the boolean value carry into the carry flag.
    Carry = ConvertBooleanCarryToCarryFlag(Carry, DAG);

    // Do the subtraction proper using the carry flag we wanted.
    Result = DAG.getNode(ARMISD::SUBE, DL, VTs, Op.getOperand(0),
                         Op.getOperand(1), Carry);

    // Now convert the carry flag into a boolean value.
    Carry = ConvertCarryFlagToBooleanCarry(Result.getValue(1), VT, DAG);
    // But the carry returned by ARMISD::SUBE is not a borrow as expected
    // by ISD::SUBCARRY, so compute 1 - C.
    Carry = DAG.getNode(ISD::SUB, DL, MVT::i32,
                        DAG.getConstant(1, DL, MVT::i32), Carry);
  }

  // Return both values.
  return DAG.getNode(ISD::MERGE_VALUES, DL, N->getVTList(), Result, Carry);
}

SDValue ARMTargetLowering::LowerFSINCOS(SDValue Op, SelectionDAG &DAG) const {
  assert(Subtarget->isTargetDarwin());

  // For iOS, we want to call an alternative entry point: __sincos_stret,
  // return values are passed via sret.
  SDLoc dl(Op);
  SDValue Arg = Op.getOperand(0);
  EVT ArgVT = Arg.getValueType();
  Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // Pair of floats / doubles used to pass the result.
  Type *RetTy = StructType::get(ArgTy, ArgTy);
  auto &DL = DAG.getDataLayout();

  ArgListTy Args;
  bool ShouldUseSRet = Subtarget->isAPCS_ABI();
  SDValue SRet;
  if (ShouldUseSRet) {
    // Create stack object for sret.
    const uint64_t ByteSize = DL.getTypeAllocSize(RetTy);
    const unsigned StackAlign = DL.getPrefTypeAlignment(RetTy);
    int FrameIdx = MFI.CreateStackObject(ByteSize, StackAlign, false);
    SRet = DAG.getFrameIndex(FrameIdx, TLI.getPointerTy(DL));

    ArgListEntry Entry;
    Entry.Node = SRet;
    Entry.Ty = RetTy->getPointerTo();
    Entry.IsSExt = false;
    Entry.IsZExt = false;
    Entry.IsSRet = true;
    Args.push_back(Entry);
    RetTy = Type::getVoidTy(*DAG.getContext());
  }

  ArgListEntry Entry;
  Entry.Node = Arg;
  Entry.Ty = ArgTy;
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  RTLIB::Libcall LC =
      (ArgVT == MVT::f64) ? RTLIB::SINCOS_STRET_F64 : RTLIB::SINCOS_STRET_F32;
  const char *LibcallName = getLibcallName(LC);
  CallingConv::ID CC = getLibcallCallingConv(LC);
  SDValue Callee = DAG.getExternalSymbol(LibcallName, getPointerTy(DL));

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(DAG.getEntryNode())
      .setCallee(CC, RetTy, Callee, std::move(Args))
      .setDiscardResult(ShouldUseSRet);
  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);

  if (!ShouldUseSRet)
    return CallResult.first;

  SDValue LoadSin =
      DAG.getLoad(ArgVT, dl, CallResult.second, SRet, MachinePointerInfo());

  // Address of cos field.
  SDValue Add = DAG.getNode(ISD::ADD, dl, PtrVT, SRet,
                            DAG.getIntPtrConstant(ArgVT.getStoreSize(), dl));
  SDValue LoadCos =
      DAG.getLoad(ArgVT, dl, LoadSin.getValue(1), Add, MachinePointerInfo());

  SDVTList Tys = DAG.getVTList(ArgVT, ArgVT);
  return DAG.getNode(ISD::MERGE_VALUES, dl, Tys,
                     LoadSin.getValue(0), LoadCos.getValue(0));
}

SDValue ARMTargetLowering::LowerWindowsDIVLibCall(SDValue Op, SelectionDAG &DAG,
                                                  bool Signed,
                                                  SDValue &Chain) const {
  EVT VT = Op.getValueType();
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "unexpected type for custom lowering DIV");
  SDLoc dl(Op);

  const auto &DL = DAG.getDataLayout();
  const auto &TLI = DAG.getTargetLoweringInfo();

  const char *Name = nullptr;
  if (Signed)
    Name = (VT == MVT::i32) ? "__rt_sdiv" : "__rt_sdiv64";
  else
    Name = (VT == MVT::i32) ? "__rt_udiv" : "__rt_udiv64";

  SDValue ES = DAG.getExternalSymbol(Name, TLI.getPointerTy(DL));

  ARMTargetLowering::ArgListTy Args;

  for (auto AI : {1, 0}) {
    ArgListEntry Arg;
    Arg.Node = Op.getOperand(AI);
    Arg.Ty = Arg.Node.getValueType().getTypeForEVT(*DAG.getContext());
    Args.push_back(Arg);
  }

  CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
    .setChain(Chain)
    .setCallee(CallingConv::ARM_AAPCS_VFP, VT.getTypeForEVT(*DAG.getContext()),
               ES, std::move(Args));

  return LowerCallTo(CLI).first;
}

// This is a code size optimisation: return the original SDIV node to
// DAGCombiner when we don't want to expand SDIV into a sequence of
// instructions, and an empty node otherwise which will cause the
// SDIV to be expanded in DAGCombine.
SDValue
ARMTargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                 SelectionDAG &DAG,
                                 SmallVectorImpl<SDNode *> &Created) const {
  // TODO: Support SREM
  if (N->getOpcode() != ISD::SDIV)
    return SDValue();

  const auto &ST = static_cast<const ARMSubtarget&>(DAG.getSubtarget());
  const auto &MF = DAG.getMachineFunction();
  const bool MinSize = MF.getFunction().optForMinSize();
  const bool HasDivide = ST.isThumb() ? ST.hasDivideInThumbMode()
                                      : ST.hasDivideInARMMode();

  // Don't touch vector types; rewriting this may lead to scalarizing
  // the int divs.
  if (N->getOperand(0).getValueType().isVector())
    return SDValue();

  // Bail if MinSize is not set, and also for both ARM and Thumb mode we need
  // hwdiv support for this to be really profitable.
  if (!(MinSize && HasDivide))
    return SDValue();

  // ARM mode is a bit simpler than Thumb: we can handle large power
  // of 2 immediates with 1 mov instruction; no further checks required,
  // just return the sdiv node.
  if (!ST.isThumb())
    return SDValue(N, 0);

  // In Thumb mode, immediates larger than 128 need a wide 4-byte MOV,
  // and thus lose the code size benefits of a MOVS that requires only 2.
  // TargetTransformInfo and 'getIntImmCodeSizeCost' could be helpful here,
  // but as it's doing exactly this, it's not worth the trouble to get TTI.
  if (Divisor.sgt(128))
    return SDValue();

  return SDValue(N, 0);
}

SDValue ARMTargetLowering::LowerDIV_Windows(SDValue Op, SelectionDAG &DAG,
                                            bool Signed) const {
  assert(Op.getValueType() == MVT::i32 &&
         "unexpected type for custom lowering DIV");
  SDLoc dl(Op);

  SDValue DBZCHK = DAG.getNode(ARMISD::WIN__DBZCHK, dl, MVT::Other,
                               DAG.getEntryNode(), Op.getOperand(1));

  return LowerWindowsDIVLibCall(Op, DAG, Signed, DBZCHK);
}

static SDValue WinDBZCheckDenominator(SelectionDAG &DAG, SDNode *N, SDValue InChain) {
  SDLoc DL(N);
  SDValue Op = N->getOperand(1);
  if (N->getValueType(0) == MVT::i32)
    return DAG.getNode(ARMISD::WIN__DBZCHK, DL, MVT::Other, InChain, Op);
  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, Op,
                           DAG.getConstant(0, DL, MVT::i32));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, Op,
                           DAG.getConstant(1, DL, MVT::i32));
  return DAG.getNode(ARMISD::WIN__DBZCHK, DL, MVT::Other, InChain,
                     DAG.getNode(ISD::OR, DL, MVT::i32, Lo, Hi));
}

void ARMTargetLowering::ExpandDIV_Windows(
    SDValue Op, SelectionDAG &DAG, bool Signed,
    SmallVectorImpl<SDValue> &Results) const {
  const auto &DL = DAG.getDataLayout();
  const auto &TLI = DAG.getTargetLoweringInfo();

  assert(Op.getValueType() == MVT::i64 &&
         "unexpected type for custom lowering DIV");
  SDLoc dl(Op);

  SDValue DBZCHK = WinDBZCheckDenominator(DAG, Op.getNode(), DAG.getEntryNode());

  SDValue Result = LowerWindowsDIVLibCall(Op, DAG, Signed, DBZCHK);

  SDValue Lower = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Result);
  SDValue Upper = DAG.getNode(ISD::SRL, dl, MVT::i64, Result,
                              DAG.getConstant(32, dl, TLI.getPointerTy(DL)));
  Upper = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Upper);

  Results.push_back(Lower);
  Results.push_back(Upper);
}

static SDValue LowerAtomicLoadStore(SDValue Op, SelectionDAG &DAG) {
  if (isStrongerThanMonotonic(cast<AtomicSDNode>(Op)->getOrdering()))
    // Acquire/Release load/store is not legal for targets without a dmb or
    // equivalent available.
    return SDValue();

  // Monotonic load/store is legal for all targets.
  return Op;
}

static void ReplaceREADCYCLECOUNTER(SDNode *N,
                                    SmallVectorImpl<SDValue> &Results,
                                    SelectionDAG &DAG,
                                    const ARMSubtarget *Subtarget) {
  SDLoc DL(N);
  // Under Power Management extensions, the cycle-count is:
  //    mrc p15, #0, <Rt>, c9, c13, #0
  SDValue Ops[] = { N->getOperand(0), // Chain
                    DAG.getConstant(Intrinsic::arm_mrc, DL, MVT::i32),
                    DAG.getConstant(15, DL, MVT::i32),
                    DAG.getConstant(0, DL, MVT::i32),
                    DAG.getConstant(9, DL, MVT::i32),
                    DAG.getConstant(13, DL, MVT::i32),
                    DAG.getConstant(0, DL, MVT::i32)
  };

  SDValue Cycles32 = DAG.getNode(ISD::INTRINSIC_W_CHAIN, DL,
                                 DAG.getVTList(MVT::i32, MVT::Other), Ops);
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Cycles32,
                                DAG.getConstant(0, DL, MVT::i32)));
  Results.push_back(Cycles32.getValue(1));
}

static SDValue createGPRPairNode(SelectionDAG &DAG, SDValue V) {
  SDLoc dl(V.getNode());
  SDValue VLo = DAG.getAnyExtOrTrunc(V, dl, MVT::i32);
  SDValue VHi = DAG.getAnyExtOrTrunc(
      DAG.getNode(ISD::SRL, dl, MVT::i64, V, DAG.getConstant(32, dl, MVT::i32)),
      dl, MVT::i32);
  bool isBigEndian = DAG.getDataLayout().isBigEndian();
  if (isBigEndian)
    std::swap (VLo, VHi);
  SDValue RegClass =
      DAG.getTargetConstant(ARM::GPRPairRegClassID, dl, MVT::i32);
  SDValue SubReg0 = DAG.getTargetConstant(ARM::gsub_0, dl, MVT::i32);
  SDValue SubReg1 = DAG.getTargetConstant(ARM::gsub_1, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, VLo, SubReg0, VHi, SubReg1 };
  return SDValue(
      DAG.getMachineNode(TargetOpcode::REG_SEQUENCE, dl, MVT::Untyped, Ops), 0);
}

static void ReplaceCMP_SWAP_64Results(SDNode *N,
                                       SmallVectorImpl<SDValue> & Results,
                                       SelectionDAG &DAG) {
  assert(N->getValueType(0) == MVT::i64 &&
         "AtomicCmpSwap on types less than 64 should be legal");
  SDValue Ops[] = {N->getOperand(1),
                   createGPRPairNode(DAG, N->getOperand(2)),
                   createGPRPairNode(DAG, N->getOperand(3)),
                   N->getOperand(0)};
  SDNode *CmpSwap = DAG.getMachineNode(
      ARM::CMP_SWAP_64, SDLoc(N),
      DAG.getVTList(MVT::Untyped, MVT::i32, MVT::Other), Ops);

  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  DAG.setNodeMemRefs(cast<MachineSDNode>(CmpSwap), {MemOp});

  bool isBigEndian = DAG.getDataLayout().isBigEndian();

  Results.push_back(
      DAG.getTargetExtractSubreg(isBigEndian ? ARM::gsub_1 : ARM::gsub_0,
                                 SDLoc(N), MVT::i32, SDValue(CmpSwap, 0)));
  Results.push_back(
      DAG.getTargetExtractSubreg(isBigEndian ? ARM::gsub_0 : ARM::gsub_1,
                                 SDLoc(N), MVT::i32, SDValue(CmpSwap, 0)));
  Results.push_back(SDValue(CmpSwap, 2));
}

static SDValue LowerFPOWI(SDValue Op, const ARMSubtarget &Subtarget,
                          SelectionDAG &DAG) {
  const auto &TLI = DAG.getTargetLoweringInfo();

  assert(Subtarget.getTargetTriple().isOSMSVCRT() &&
         "Custom lowering is MSVCRT specific!");

  SDLoc dl(Op);
  SDValue Val = Op.getOperand(0);
  MVT Ty = Val->getSimpleValueType(0);
  SDValue Exponent = DAG.getNode(ISD::SINT_TO_FP, dl, Ty, Op.getOperand(1));
  SDValue Callee = DAG.getExternalSymbol(Ty == MVT::f32 ? "powf" : "pow",
                                         TLI.getPointerTy(DAG.getDataLayout()));

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Entry.Node = Val;
  Entry.Ty = Val.getValueType().getTypeForEVT(*DAG.getContext());
  Entry.IsZExt = true;
  Args.push_back(Entry);

  Entry.Node = Exponent;
  Entry.Ty = Exponent.getValueType().getTypeForEVT(*DAG.getContext());
  Entry.IsZExt = true;
  Args.push_back(Entry);

  Type *LCRTy = Val.getValueType().getTypeForEVT(*DAG.getContext());

  // In the in-chain to the call is the entry node  If we are emitting a
  // tailcall, the chain will be mutated if the node has a non-entry input
  // chain.
  SDValue InChain = DAG.getEntryNode();
  SDValue TCChain = InChain;

  const Function &F = DAG.getMachineFunction().getFunction();
  bool IsTC = TLI.isInTailCallPosition(DAG, Op.getNode(), TCChain) &&
              F.getReturnType() == LCRTy;
  if (IsTC)
    InChain = TCChain;

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(InChain)
      .setCallee(CallingConv::ARM_AAPCS_VFP, LCRTy, Callee, std::move(Args))
      .setTailCall(IsTC);
  std::pair<SDValue, SDValue> CI = TLI.LowerCallTo(CLI);

  // Return the chain (the DAG root) if it is a tail call
  return !CI.second.getNode() ? DAG.getRoot() : CI.first;
}

SDValue ARMTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  LLVM_DEBUG(dbgs() << "Lowering node: "; Op.dump());
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Don't know how to custom lower this!");
  case ISD::WRITE_REGISTER: return LowerWRITE_REGISTER(Op, DAG);
  case ISD::ConstantPool: return LowerConstantPool(Op, DAG);
  case ISD::BlockAddress:  return LowerBlockAddress(Op, DAG);
  case ISD::GlobalAddress: return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress: return LowerGlobalTLSAddress(Op, DAG);
  case ISD::SELECT:        return LowerSELECT(Op, DAG);
  case ISD::SELECT_CC:     return LowerSELECT_CC(Op, DAG);
  case ISD::BRCOND:        return LowerBRCOND(Op, DAG);
  case ISD::BR_CC:         return LowerBR_CC(Op, DAG);
  case ISD::BR_JT:         return LowerBR_JT(Op, DAG);
  case ISD::VASTART:       return LowerVASTART(Op, DAG);
  case ISD::ATOMIC_FENCE:  return LowerATOMIC_FENCE(Op, DAG, Subtarget);
  case ISD::PREFETCH:      return LowerPREFETCH(Op, DAG, Subtarget);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:    return LowerINT_TO_FP(Op, DAG);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:    return LowerFP_TO_INT(Op, DAG);
  case ISD::FCOPYSIGN:     return LowerFCOPYSIGN(Op, DAG);
  case ISD::RETURNADDR:    return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:     return LowerFRAMEADDR(Op, DAG);
  case ISD::EH_SJLJ_SETJMP: return LowerEH_SJLJ_SETJMP(Op, DAG);
  case ISD::EH_SJLJ_LONGJMP: return LowerEH_SJLJ_LONGJMP(Op, DAG);
  case ISD::EH_SJLJ_SETUP_DISPATCH: return LowerEH_SJLJ_SETUP_DISPATCH(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN: return LowerINTRINSIC_WO_CHAIN(Op, DAG,
                                                               Subtarget);
  case ISD::BITCAST:       return ExpandBITCAST(Op.getNode(), DAG, Subtarget);
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:           return LowerShift(Op.getNode(), DAG, Subtarget);
  case ISD::SREM:          return LowerREM(Op.getNode(), DAG);
  case ISD::UREM:          return LowerREM(Op.getNode(), DAG);
  case ISD::SHL_PARTS:     return LowerShiftLeftParts(Op, DAG);
  case ISD::SRL_PARTS:
  case ISD::SRA_PARTS:     return LowerShiftRightParts(Op, DAG);
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF: return LowerCTTZ(Op.getNode(), DAG, Subtarget);
  case ISD::CTPOP:         return LowerCTPOP(Op.getNode(), DAG, Subtarget);
  case ISD::SETCC:         return LowerVSETCC(Op, DAG);
  case ISD::SETCCCARRY:    return LowerSETCCCARRY(Op, DAG);
  case ISD::ConstantFP:    return LowerConstantFP(Op, DAG, Subtarget);
  case ISD::BUILD_VECTOR:  return LowerBUILD_VECTOR(Op, DAG, Subtarget);
  case ISD::VECTOR_SHUFFLE: return LowerVECTOR_SHUFFLE(Op, DAG);
  case ISD::INSERT_VECTOR_ELT: return LowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT: return LowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::CONCAT_VECTORS: return LowerCONCAT_VECTORS(Op, DAG);
  case ISD::FLT_ROUNDS_:   return LowerFLT_ROUNDS_(Op, DAG);
  case ISD::MUL:           return LowerMUL(Op, DAG);
  case ISD::SDIV:
    if (Subtarget->isTargetWindows() && !Op.getValueType().isVector())
      return LowerDIV_Windows(Op, DAG, /* Signed */ true);
    return LowerSDIV(Op, DAG);
  case ISD::UDIV:
    if (Subtarget->isTargetWindows() && !Op.getValueType().isVector())
      return LowerDIV_Windows(Op, DAG, /* Signed */ false);
    return LowerUDIV(Op, DAG);
  case ISD::ADDCARRY:
  case ISD::SUBCARRY:      return LowerADDSUBCARRY(Op, DAG);
  case ISD::SADDO:
  case ISD::SSUBO:
    return LowerSignedALUO(Op, DAG);
  case ISD::UADDO:
  case ISD::USUBO:
    return LowerUnsignedALUO(Op, DAG);
  case ISD::ATOMIC_LOAD:
  case ISD::ATOMIC_STORE:  return LowerAtomicLoadStore(Op, DAG);
  case ISD::FSINCOS:       return LowerFSINCOS(Op, DAG);
  case ISD::SDIVREM:
  case ISD::UDIVREM:       return LowerDivRem(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    if (Subtarget->isTargetWindows())
      return LowerDYNAMIC_STACKALLOC(Op, DAG);
    llvm_unreachable("Don't know how to custom lower this!");
  case ISD::FP_ROUND: return LowerFP_ROUND(Op, DAG);
  case ISD::FP_EXTEND: return LowerFP_EXTEND(Op, DAG);
  case ISD::FPOWI: return LowerFPOWI(Op, *Subtarget, DAG);
  case ARMISD::WIN__DBZCHK: return SDValue();
  }
}

static void ReplaceLongIntrinsic(SDNode *N, SmallVectorImpl<SDValue> &Results,
                                 SelectionDAG &DAG) {
  unsigned IntNo = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
  unsigned Opc = 0;
  if (IntNo == Intrinsic::arm_smlald)
    Opc = ARMISD::SMLALD;
  else if (IntNo == Intrinsic::arm_smlaldx)
    Opc = ARMISD::SMLALDX;
  else if (IntNo == Intrinsic::arm_smlsld)
    Opc = ARMISD::SMLSLD;
  else if (IntNo == Intrinsic::arm_smlsldx)
    Opc = ARMISD::SMLSLDX;
  else
    return;

  SDLoc dl(N);
  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32,
                           N->getOperand(3),
                           DAG.getConstant(0, dl, MVT::i32));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, MVT::i32,
                           N->getOperand(3),
                           DAG.getConstant(1, dl, MVT::i32));

  SDValue LongMul = DAG.getNode(Opc, dl,
                                DAG.getVTList(MVT::i32, MVT::i32),
                                N->getOperand(1), N->getOperand(2),
                                Lo, Hi);
  Results.push_back(LongMul.getValue(0));
  Results.push_back(LongMul.getValue(1));
}

/// ReplaceNodeResults - Replace the results of node with an illegal result
/// type with new values built out of custom code.
void ARMTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  SDValue Res;
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Don't know how to custom expand this!");
  case ISD::READ_REGISTER:
    ExpandREAD_REGISTER(N, Results, DAG);
    break;
  case ISD::BITCAST:
    Res = ExpandBITCAST(N, DAG, Subtarget);
    break;
  case ISD::SRL:
  case ISD::SRA:
    Res = Expand64BitShift(N, DAG, Subtarget);
    break;
  case ISD::SREM:
  case ISD::UREM:
    Res = LowerREM(N, DAG);
    break;
  case ISD::SDIVREM:
  case ISD::UDIVREM:
    Res = LowerDivRem(SDValue(N, 0), DAG);
    assert(Res.getNumOperands() == 2 && "DivRem needs two values");
    Results.push_back(Res.getValue(0));
    Results.push_back(Res.getValue(1));
    return;
  case ISD::READCYCLECOUNTER:
    ReplaceREADCYCLECOUNTER(N, Results, DAG, Subtarget);
    return;
  case ISD::UDIV:
  case ISD::SDIV:
    assert(Subtarget->isTargetWindows() && "can only expand DIV on Windows");
    return ExpandDIV_Windows(SDValue(N, 0), DAG, N->getOpcode() == ISD::SDIV,
                             Results);
  case ISD::ATOMIC_CMP_SWAP:
    ReplaceCMP_SWAP_64Results(N, Results, DAG);
    return;
  case ISD::INTRINSIC_WO_CHAIN:
    return ReplaceLongIntrinsic(N, Results, DAG);
  }
  if (Res.getNode())
    Results.push_back(Res);
}

//===----------------------------------------------------------------------===//
//                           ARM Scheduler Hooks
//===----------------------------------------------------------------------===//

/// SetupEntryBlockForSjLj - Insert code into the entry block that creates and
/// registers the function context.
void ARMTargetLowering::SetupEntryBlockForSjLj(MachineInstr &MI,
                                               MachineBasicBlock *MBB,
                                               MachineBasicBlock *DispatchBB,
                                               int FI) const {
  assert(!Subtarget->isROPI() && !Subtarget->isRWPI() &&
         "ROPI/RWPI not currently supported with SjLj");
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  MachineConstantPool *MCP = MF->getConstantPool();
  ARMFunctionInfo *AFI = MF->getInfo<ARMFunctionInfo>();
  const Function &F = MF->getFunction();

  bool isThumb = Subtarget->isThumb();
  bool isThumb2 = Subtarget->isThumb2();

  unsigned PCLabelId = AFI->createPICLabelUId();
  unsigned PCAdj = (isThumb || isThumb2) ? 4 : 8;
  ARMConstantPoolValue *CPV =
    ARMConstantPoolMBB::Create(F.getContext(), DispatchBB, PCLabelId, PCAdj);
  unsigned CPI = MCP->getConstantPoolIndex(CPV, 4);

  const TargetRegisterClass *TRC = isThumb ? &ARM::tGPRRegClass
                                           : &ARM::GPRRegClass;

  // Grab constant pool and fixed stack memory operands.
  MachineMemOperand *CPMMO =
      MF->getMachineMemOperand(MachinePointerInfo::getConstantPool(*MF),
                               MachineMemOperand::MOLoad, 4, 4);

  MachineMemOperand *FIMMOSt =
      MF->getMachineMemOperand(MachinePointerInfo::getFixedStack(*MF, FI),
                               MachineMemOperand::MOStore, 4, 4);

  // Load the address of the dispatch MBB into the jump buffer.
  if (isThumb2) {
    // Incoming value: jbuf
    //   ldr.n  r5, LCPI1_1
    //   orr    r5, r5, #1
    //   add    r5, pc
    //   str    r5, [$jbuf, #+4] ; &jbuf[1]
    unsigned NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::t2LDRpci), NewVReg1)
        .addConstantPoolIndex(CPI)
        .addMemOperand(CPMMO)
        .add(predOps(ARMCC::AL));
    // Set the low bit because of thumb mode.
    unsigned NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::t2ORRri), NewVReg2)
        .addReg(NewVReg1, RegState::Kill)
        .addImm(0x01)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    unsigned NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tPICADD), NewVReg3)
      .addReg(NewVReg2, RegState::Kill)
      .addImm(PCLabelId);
    BuildMI(*MBB, MI, dl, TII->get(ARM::t2STRi12))
        .addReg(NewVReg3, RegState::Kill)
        .addFrameIndex(FI)
        .addImm(36) // &jbuf[1] :: pc
        .addMemOperand(FIMMOSt)
        .add(predOps(ARMCC::AL));
  } else if (isThumb) {
    // Incoming value: jbuf
    //   ldr.n  r1, LCPI1_4
    //   add    r1, pc
    //   mov    r2, #1
    //   orrs   r1, r2
    //   add    r2, $jbuf, #+4 ; &jbuf[1]
    //   str    r1, [r2]
    unsigned NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tLDRpci), NewVReg1)
        .addConstantPoolIndex(CPI)
        .addMemOperand(CPMMO)
        .add(predOps(ARMCC::AL));
    unsigned NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tPICADD), NewVReg2)
      .addReg(NewVReg1, RegState::Kill)
      .addImm(PCLabelId);
    // Set the low bit because of thumb mode.
    unsigned NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tMOVi8), NewVReg3)
        .addReg(ARM::CPSR, RegState::Define)
        .addImm(1)
        .add(predOps(ARMCC::AL));
    unsigned NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tORR), NewVReg4)
        .addReg(ARM::CPSR, RegState::Define)
        .addReg(NewVReg2, RegState::Kill)
        .addReg(NewVReg3, RegState::Kill)
        .add(predOps(ARMCC::AL));
    unsigned NewVReg5 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tADDframe), NewVReg5)
            .addFrameIndex(FI)
            .addImm(36); // &jbuf[1] :: pc
    BuildMI(*MBB, MI, dl, TII->get(ARM::tSTRi))
        .addReg(NewVReg4, RegState::Kill)
        .addReg(NewVReg5, RegState::Kill)
        .addImm(0)
        .addMemOperand(FIMMOSt)
        .add(predOps(ARMCC::AL));
  } else {
    // Incoming value: jbuf
    //   ldr  r1, LCPI1_1
    //   add  r1, pc, r1
    //   str  r1, [$jbuf, #+4] ; &jbuf[1]
    unsigned NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::LDRi12), NewVReg1)
        .addConstantPoolIndex(CPI)
        .addImm(0)
        .addMemOperand(CPMMO)
        .add(predOps(ARMCC::AL));
    unsigned NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::PICADD), NewVReg2)
        .addReg(NewVReg1, RegState::Kill)
        .addImm(PCLabelId)
        .add(predOps(ARMCC::AL));
    BuildMI(*MBB, MI, dl, TII->get(ARM::STRi12))
        .addReg(NewVReg2, RegState::Kill)
        .addFrameIndex(FI)
        .addImm(36) // &jbuf[1] :: pc
        .addMemOperand(FIMMOSt)
        .add(predOps(ARMCC::AL));
  }
}

void ARMTargetLowering::EmitSjLjDispatchBlock(MachineInstr &MI,
                                              MachineBasicBlock *MBB) const {
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  MachineFrameInfo &MFI = MF->getFrameInfo();
  int FI = MFI.getFunctionContextIndex();

  const TargetRegisterClass *TRC = Subtarget->isThumb() ? &ARM::tGPRRegClass
                                                        : &ARM::GPRnopcRegClass;

  // Get a mapping of the call site numbers to all of the landing pads they're
  // associated with.
  DenseMap<unsigned, SmallVector<MachineBasicBlock*, 2>> CallSiteNumToLPad;
  unsigned MaxCSNum = 0;
  for (MachineFunction::iterator BB = MF->begin(), E = MF->end(); BB != E;
       ++BB) {
    if (!BB->isEHPad()) continue;

    // FIXME: We should assert that the EH_LABEL is the first MI in the landing
    // pad.
    for (MachineBasicBlock::iterator
           II = BB->begin(), IE = BB->end(); II != IE; ++II) {
      if (!II->isEHLabel()) continue;

      MCSymbol *Sym = II->getOperand(0).getMCSymbol();
      if (!MF->hasCallSiteLandingPad(Sym)) continue;

      SmallVectorImpl<unsigned> &CallSiteIdxs = MF->getCallSiteLandingPad(Sym);
      for (SmallVectorImpl<unsigned>::iterator
             CSI = CallSiteIdxs.begin(), CSE = CallSiteIdxs.end();
           CSI != CSE; ++CSI) {
        CallSiteNumToLPad[*CSI].push_back(&*BB);
        MaxCSNum = std::max(MaxCSNum, *CSI);
      }
      break;
    }
  }

  // Get an ordered list of the machine basic blocks for the jump table.
  std::vector<MachineBasicBlock*> LPadList;
  SmallPtrSet<MachineBasicBlock*, 32> InvokeBBs;
  LPadList.reserve(CallSiteNumToLPad.size());
  for (unsigned I = 1; I <= MaxCSNum; ++I) {
    SmallVectorImpl<MachineBasicBlock*> &MBBList = CallSiteNumToLPad[I];
    for (SmallVectorImpl<MachineBasicBlock*>::iterator
           II = MBBList.begin(), IE = MBBList.end(); II != IE; ++II) {
      LPadList.push_back(*II);
      InvokeBBs.insert((*II)->pred_begin(), (*II)->pred_end());
    }
  }

  assert(!LPadList.empty() &&
         "No landing pad destinations for the dispatch jump table!");

  // Create the jump table and associated information.
  MachineJumpTableInfo *JTI =
    MF->getOrCreateJumpTableInfo(MachineJumpTableInfo::EK_Inline);
  unsigned MJTI = JTI->createJumpTableIndex(LPadList);

  // Create the MBBs for the dispatch code.

  // Shove the dispatch's address into the return slot in the function context.
  MachineBasicBlock *DispatchBB = MF->CreateMachineBasicBlock();
  DispatchBB->setIsEHPad();

  MachineBasicBlock *TrapBB = MF->CreateMachineBasicBlock();
  unsigned trap_opcode;
  if (Subtarget->isThumb())
    trap_opcode = ARM::tTRAP;
  else
    trap_opcode = Subtarget->useNaClTrap() ? ARM::TRAPNaCl : ARM::TRAP;

  BuildMI(TrapBB, dl, TII->get(trap_opcode));
  DispatchBB->addSuccessor(TrapBB);

  MachineBasicBlock *DispContBB = MF->CreateMachineBasicBlock();
  DispatchBB->addSuccessor(DispContBB);

  // Insert and MBBs.
  MF->insert(MF->end(), DispatchBB);
  MF->insert(MF->end(), DispContBB);
  MF->insert(MF->end(), TrapBB);

  // Insert code into the entry block that creates and registers the function
  // context.
  SetupEntryBlockForSjLj(MI, MBB, DispatchBB, FI);

  MachineMemOperand *FIMMOLd = MF->getMachineMemOperand(
      MachinePointerInfo::getFixedStack(*MF, FI),
      MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile, 4, 4);

  MachineInstrBuilder MIB;
  MIB = BuildMI(DispatchBB, dl, TII->get(ARM::Int_eh_sjlj_dispatchsetup));

  const ARMBaseInstrInfo *AII = static_cast<const ARMBaseInstrInfo*>(TII);
  const ARMBaseRegisterInfo &RI = AII->getRegisterInfo();

  // Add a register mask with no preserved registers.  This results in all
  // registers being marked as clobbered. This can't work if the dispatch block
  // is in a Thumb1 function and is linked with ARM code which uses the FP
  // registers, as there is no way to preserve the FP registers in Thumb1 mode.
  MIB.addRegMask(RI.getSjLjDispatchPreservedMask(*MF));

  bool IsPositionIndependent = isPositionIndependent();
  unsigned NumLPads = LPadList.size();
  if (Subtarget->isThumb2()) {
    unsigned NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(DispatchBB, dl, TII->get(ARM::t2LDRi12), NewVReg1)
        .addFrameIndex(FI)
        .addImm(4)
        .addMemOperand(FIMMOLd)
        .add(predOps(ARMCC::AL));

    if (NumLPads < 256) {
      BuildMI(DispatchBB, dl, TII->get(ARM::t2CMPri))
          .addReg(NewVReg1)
          .addImm(LPadList.size())
          .add(predOps(ARMCC::AL));
    } else {
      unsigned VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::t2MOVi16), VReg1)
          .addImm(NumLPads & 0xFFFF)
          .add(predOps(ARMCC::AL));

      unsigned VReg2 = VReg1;
      if ((NumLPads & 0xFFFF0000) != 0) {
        VReg2 = MRI->createVirtualRegister(TRC);
        BuildMI(DispatchBB, dl, TII->get(ARM::t2MOVTi16), VReg2)
            .addReg(VReg1)
            .addImm(NumLPads >> 16)
            .add(predOps(ARMCC::AL));
      }

      BuildMI(DispatchBB, dl, TII->get(ARM::t2CMPrr))
          .addReg(NewVReg1)
          .addReg(VReg2)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispatchBB, dl, TII->get(ARM::t2Bcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::HI)
      .addReg(ARM::CPSR);

    unsigned NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::t2LEApcrelJT), NewVReg3)
        .addJumpTableIndex(MJTI)
        .add(predOps(ARMCC::AL));

    unsigned NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::t2ADDrs), NewVReg4)
        .addReg(NewVReg3, RegState::Kill)
        .addReg(NewVReg1)
        .addImm(ARM_AM::getSORegOpc(ARM_AM::lsl, 2))
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());

    BuildMI(DispContBB, dl, TII->get(ARM::t2BR_JT))
      .addReg(NewVReg4, RegState::Kill)
      .addReg(NewVReg1)
      .addJumpTableIndex(MJTI);
  } else if (Subtarget->isThumb()) {
    unsigned NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(DispatchBB, dl, TII->get(ARM::tLDRspi), NewVReg1)
        .addFrameIndex(FI)
        .addImm(1)
        .addMemOperand(FIMMOLd)
        .add(predOps(ARMCC::AL));

    if (NumLPads < 256) {
      BuildMI(DispatchBB, dl, TII->get(ARM::tCMPi8))
          .addReg(NewVReg1)
          .addImm(NumLPads)
          .add(predOps(ARMCC::AL));
    } else {
      MachineConstantPool *ConstantPool = MF->getConstantPool();
      Type *Int32Ty = Type::getInt32Ty(MF->getFunction().getContext());
      const Constant *C = ConstantInt::get(Int32Ty, NumLPads);

      // MachineConstantPool wants an explicit alignment.
      unsigned Align = MF->getDataLayout().getPrefTypeAlignment(Int32Ty);
      if (Align == 0)
        Align = MF->getDataLayout().getTypeAllocSize(C->getType());
      unsigned Idx = ConstantPool->getConstantPoolIndex(C, Align);

      unsigned VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::tLDRpci))
          .addReg(VReg1, RegState::Define)
          .addConstantPoolIndex(Idx)
          .add(predOps(ARMCC::AL));
      BuildMI(DispatchBB, dl, TII->get(ARM::tCMPr))
          .addReg(NewVReg1)
          .addReg(VReg1)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispatchBB, dl, TII->get(ARM::tBcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::HI)
      .addReg(ARM::CPSR);

    unsigned NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tLSLri), NewVReg2)
        .addReg(ARM::CPSR, RegState::Define)
        .addReg(NewVReg1)
        .addImm(2)
        .add(predOps(ARMCC::AL));

    unsigned NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tLEApcrelJT), NewVReg3)
        .addJumpTableIndex(MJTI)
        .add(predOps(ARMCC::AL));

    unsigned NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tADDrr), NewVReg4)
        .addReg(ARM::CPSR, RegState::Define)
        .addReg(NewVReg2, RegState::Kill)
        .addReg(NewVReg3)
        .add(predOps(ARMCC::AL));

    MachineMemOperand *JTMMOLd = MF->getMachineMemOperand(
        MachinePointerInfo::getJumpTable(*MF), MachineMemOperand::MOLoad, 4, 4);

    unsigned NewVReg5 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tLDRi), NewVReg5)
        .addReg(NewVReg4, RegState::Kill)
        .addImm(0)
        .addMemOperand(JTMMOLd)
        .add(predOps(ARMCC::AL));

    unsigned NewVReg6 = NewVReg5;
    if (IsPositionIndependent) {
      NewVReg6 = MRI->createVirtualRegister(TRC);
      BuildMI(DispContBB, dl, TII->get(ARM::tADDrr), NewVReg6)
          .addReg(ARM::CPSR, RegState::Define)
          .addReg(NewVReg5, RegState::Kill)
          .addReg(NewVReg3)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispContBB, dl, TII->get(ARM::tBR_JTr))
      .addReg(NewVReg6, RegState::Kill)
      .addJumpTableIndex(MJTI);
  } else {
    unsigned NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(DispatchBB, dl, TII->get(ARM::LDRi12), NewVReg1)
        .addFrameIndex(FI)
        .addImm(4)
        .addMemOperand(FIMMOLd)
        .add(predOps(ARMCC::AL));

    if (NumLPads < 256) {
      BuildMI(DispatchBB, dl, TII->get(ARM::CMPri))
          .addReg(NewVReg1)
          .addImm(NumLPads)
          .add(predOps(ARMCC::AL));
    } else if (Subtarget->hasV6T2Ops() && isUInt<16>(NumLPads)) {
      unsigned VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::MOVi16), VReg1)
          .addImm(NumLPads & 0xFFFF)
          .add(predOps(ARMCC::AL));

      unsigned VReg2 = VReg1;
      if ((NumLPads & 0xFFFF0000) != 0) {
        VReg2 = MRI->createVirtualRegister(TRC);
        BuildMI(DispatchBB, dl, TII->get(ARM::MOVTi16), VReg2)
            .addReg(VReg1)
            .addImm(NumLPads >> 16)
            .add(predOps(ARMCC::AL));
      }

      BuildMI(DispatchBB, dl, TII->get(ARM::CMPrr))
          .addReg(NewVReg1)
          .addReg(VReg2)
          .add(predOps(ARMCC::AL));
    } else {
      MachineConstantPool *ConstantPool = MF->getConstantPool();
      Type *Int32Ty = Type::getInt32Ty(MF->getFunction().getContext());
      const Constant *C = ConstantInt::get(Int32Ty, NumLPads);

      // MachineConstantPool wants an explicit alignment.
      unsigned Align = MF->getDataLayout().getPrefTypeAlignment(Int32Ty);
      if (Align == 0)
        Align = MF->getDataLayout().getTypeAllocSize(C->getType());
      unsigned Idx = ConstantPool->getConstantPoolIndex(C, Align);

      unsigned VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::LDRcp))
          .addReg(VReg1, RegState::Define)
          .addConstantPoolIndex(Idx)
          .addImm(0)
          .add(predOps(ARMCC::AL));
      BuildMI(DispatchBB, dl, TII->get(ARM::CMPrr))
          .addReg(NewVReg1)
          .addReg(VReg1, RegState::Kill)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispatchBB, dl, TII->get(ARM::Bcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::HI)
      .addReg(ARM::CPSR);

    unsigned NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::MOVsi), NewVReg3)
        .addReg(NewVReg1)
        .addImm(ARM_AM::getSORegOpc(ARM_AM::lsl, 2))
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    unsigned NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::LEApcrelJT), NewVReg4)
        .addJumpTableIndex(MJTI)
        .add(predOps(ARMCC::AL));

    MachineMemOperand *JTMMOLd = MF->getMachineMemOperand(
        MachinePointerInfo::getJumpTable(*MF), MachineMemOperand::MOLoad, 4, 4);
    unsigned NewVReg5 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::LDRrs), NewVReg5)
        .addReg(NewVReg3, RegState::Kill)
        .addReg(NewVReg4)
        .addImm(0)
        .addMemOperand(JTMMOLd)
        .add(predOps(ARMCC::AL));

    if (IsPositionIndependent) {
      BuildMI(DispContBB, dl, TII->get(ARM::BR_JTadd))
        .addReg(NewVReg5, RegState::Kill)
        .addReg(NewVReg4)
        .addJumpTableIndex(MJTI);
    } else {
      BuildMI(DispContBB, dl, TII->get(ARM::BR_JTr))
        .addReg(NewVReg5, RegState::Kill)
        .addJumpTableIndex(MJTI);
    }
  }

  // Add the jump table entries as successors to the MBB.
  SmallPtrSet<MachineBasicBlock*, 8> SeenMBBs;
  for (std::vector<MachineBasicBlock*>::iterator
         I = LPadList.begin(), E = LPadList.end(); I != E; ++I) {
    MachineBasicBlock *CurMBB = *I;
    if (SeenMBBs.insert(CurMBB).second)
      DispContBB->addSuccessor(CurMBB);
  }

  // N.B. the order the invoke BBs are processed in doesn't matter here.
  const MCPhysReg *SavedRegs = RI.getCalleeSavedRegs(MF);
  SmallVector<MachineBasicBlock*, 64> MBBLPads;
  for (MachineBasicBlock *BB : InvokeBBs) {

    // Remove the landing pad successor from the invoke block and replace it
    // with the new dispatch block.
    SmallVector<MachineBasicBlock*, 4> Successors(BB->succ_begin(),
                                                  BB->succ_end());
    while (!Successors.empty()) {
      MachineBasicBlock *SMBB = Successors.pop_back_val();
      if (SMBB->isEHPad()) {
        BB->removeSuccessor(SMBB);
        MBBLPads.push_back(SMBB);
      }
    }

    BB->addSuccessor(DispatchBB, BranchProbability::getZero());
    BB->normalizeSuccProbs();

    // Find the invoke call and mark all of the callee-saved registers as
    // 'implicit defined' so that they're spilled. This prevents code from
    // moving instructions to before the EH block, where they will never be
    // executed.
    for (MachineBasicBlock::reverse_iterator
           II = BB->rbegin(), IE = BB->rend(); II != IE; ++II) {
      if (!II->isCall()) continue;

      DenseMap<unsigned, bool> DefRegs;
      for (MachineInstr::mop_iterator
             OI = II->operands_begin(), OE = II->operands_end();
           OI != OE; ++OI) {
        if (!OI->isReg()) continue;
        DefRegs[OI->getReg()] = true;
      }

      MachineInstrBuilder MIB(*MF, &*II);

      for (unsigned i = 0; SavedRegs[i] != 0; ++i) {
        unsigned Reg = SavedRegs[i];
        if (Subtarget->isThumb2() &&
            !ARM::tGPRRegClass.contains(Reg) &&
            !ARM::hGPRRegClass.contains(Reg))
          continue;
        if (Subtarget->isThumb1Only() && !ARM::tGPRRegClass.contains(Reg))
          continue;
        if (!Subtarget->isThumb() && !ARM::GPRRegClass.contains(Reg))
          continue;
        if (!DefRegs[Reg])
          MIB.addReg(Reg, RegState::ImplicitDefine | RegState::Dead);
      }

      break;
    }
  }

  // Mark all former landing pads as non-landing pads. The dispatch is the only
  // landing pad now.
  for (SmallVectorImpl<MachineBasicBlock*>::iterator
         I = MBBLPads.begin(), E = MBBLPads.end(); I != E; ++I)
    (*I)->setIsEHPad(false);

  // The instruction is gone now.
  MI.eraseFromParent();
}

static
MachineBasicBlock *OtherSucc(MachineBasicBlock *MBB, MachineBasicBlock *Succ) {
  for (MachineBasicBlock::succ_iterator I = MBB->succ_begin(),
       E = MBB->succ_end(); I != E; ++I)
    if (*I != Succ)
      return *I;
  llvm_unreachable("Expecting a BB with two successors!");
}

/// Return the load opcode for a given load size. If load size >= 8,
/// neon opcode will be returned.
static unsigned getLdOpcode(unsigned LdSize, bool IsThumb1, bool IsThumb2) {
  if (LdSize >= 8)
    return LdSize == 16 ? ARM::VLD1q32wb_fixed
                        : LdSize == 8 ? ARM::VLD1d32wb_fixed : 0;
  if (IsThumb1)
    return LdSize == 4 ? ARM::tLDRi
                       : LdSize == 2 ? ARM::tLDRHi
                                     : LdSize == 1 ? ARM::tLDRBi : 0;
  if (IsThumb2)
    return LdSize == 4 ? ARM::t2LDR_POST
                       : LdSize == 2 ? ARM::t2LDRH_POST
                                     : LdSize == 1 ? ARM::t2LDRB_POST : 0;
  return LdSize == 4 ? ARM::LDR_POST_IMM
                     : LdSize == 2 ? ARM::LDRH_POST
                                   : LdSize == 1 ? ARM::LDRB_POST_IMM : 0;
}

/// Return the store opcode for a given store size. If store size >= 8,
/// neon opcode will be returned.
static unsigned getStOpcode(unsigned StSize, bool IsThumb1, bool IsThumb2) {
  if (StSize >= 8)
    return StSize == 16 ? ARM::VST1q32wb_fixed
                        : StSize == 8 ? ARM::VST1d32wb_fixed : 0;
  if (IsThumb1)
    return StSize == 4 ? ARM::tSTRi
                       : StSize == 2 ? ARM::tSTRHi
                                     : StSize == 1 ? ARM::tSTRBi : 0;
  if (IsThumb2)
    return StSize == 4 ? ARM::t2STR_POST
                       : StSize == 2 ? ARM::t2STRH_POST
                                     : StSize == 1 ? ARM::t2STRB_POST : 0;
  return StSize == 4 ? ARM::STR_POST_IMM
                     : StSize == 2 ? ARM::STRH_POST
                                   : StSize == 1 ? ARM::STRB_POST_IMM : 0;
}

/// Emit a post-increment load operation with given size. The instructions
/// will be added to BB at Pos.
static void emitPostLd(MachineBasicBlock *BB, MachineBasicBlock::iterator Pos,
                       const TargetInstrInfo *TII, const DebugLoc &dl,
                       unsigned LdSize, unsigned Data, unsigned AddrIn,
                       unsigned AddrOut, bool IsThumb1, bool IsThumb2) {
  unsigned LdOpc = getLdOpcode(LdSize, IsThumb1, IsThumb2);
  assert(LdOpc != 0 && "Should have a load opcode");
  if (LdSize >= 8) {
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrOut, RegState::Define)
        .addReg(AddrIn)
        .addImm(0)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb1) {
    // load + update AddrIn
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrIn)
        .addImm(0)
        .add(predOps(ARMCC::AL));
    BuildMI(*BB, Pos, dl, TII->get(ARM::tADDi8), AddrOut)
        .add(t1CondCodeOp())
        .addReg(AddrIn)
        .addImm(LdSize)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb2) {
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrOut, RegState::Define)
        .addReg(AddrIn)
        .addImm(LdSize)
        .add(predOps(ARMCC::AL));
  } else { // arm
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrOut, RegState::Define)
        .addReg(AddrIn)
        .addReg(0)
        .addImm(LdSize)
        .add(predOps(ARMCC::AL));
  }
}

/// Emit a post-increment store operation with given size. The instructions
/// will be added to BB at Pos.
static void emitPostSt(MachineBasicBlock *BB, MachineBasicBlock::iterator Pos,
                       const TargetInstrInfo *TII, const DebugLoc &dl,
                       unsigned StSize, unsigned Data, unsigned AddrIn,
                       unsigned AddrOut, bool IsThumb1, bool IsThumb2) {
  unsigned StOpc = getStOpcode(StSize, IsThumb1, IsThumb2);
  assert(StOpc != 0 && "Should have a store opcode");
  if (StSize >= 8) {
    BuildMI(*BB, Pos, dl, TII->get(StOpc), AddrOut)
        .addReg(AddrIn)
        .addImm(0)
        .addReg(Data)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb1) {
    // store + update AddrIn
    BuildMI(*BB, Pos, dl, TII->get(StOpc))
        .addReg(Data)
        .addReg(AddrIn)
        .addImm(0)
        .add(predOps(ARMCC::AL));
    BuildMI(*BB, Pos, dl, TII->get(ARM::tADDi8), AddrOut)
        .add(t1CondCodeOp())
        .addReg(AddrIn)
        .addImm(StSize)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb2) {
    BuildMI(*BB, Pos, dl, TII->get(StOpc), AddrOut)
        .addReg(Data)
        .addReg(AddrIn)
        .addImm(StSize)
        .add(predOps(ARMCC::AL));
  } else { // arm
    BuildMI(*BB, Pos, dl, TII->get(StOpc), AddrOut)
        .addReg(Data)
        .addReg(AddrIn)
        .addReg(0)
        .addImm(StSize)
        .add(predOps(ARMCC::AL));
  }
}

MachineBasicBlock *
ARMTargetLowering::EmitStructByval(MachineInstr &MI,
                                   MachineBasicBlock *BB) const {
  // This pseudo instruction has 3 operands: dst, src, size
  // We expand it to a loop if size > Subtarget->getMaxInlineSizeThreshold().
  // Otherwise, we will generate unrolled scalar copies.
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  unsigned dest = MI.getOperand(0).getReg();
  unsigned src = MI.getOperand(1).getReg();
  unsigned SizeVal = MI.getOperand(2).getImm();
  unsigned Align = MI.getOperand(3).getImm();
  DebugLoc dl = MI.getDebugLoc();

  MachineFunction *MF = BB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  unsigned UnitSize = 0;
  const TargetRegisterClass *TRC = nullptr;
  const TargetRegisterClass *VecTRC = nullptr;

  bool IsThumb1 = Subtarget->isThumb1Only();
  bool IsThumb2 = Subtarget->isThumb2();
  bool IsThumb = Subtarget->isThumb();

  if (Align & 1) {
    UnitSize = 1;
  } else if (Align & 2) {
    UnitSize = 2;
  } else {
    // Check whether we can use NEON instructions.
    if (!MF->getFunction().hasFnAttribute(Attribute::NoImplicitFloat) &&
        Subtarget->hasNEON()) {
      if ((Align % 16 == 0) && SizeVal >= 16)
        UnitSize = 16;
      else if ((Align % 8 == 0) && SizeVal >= 8)
        UnitSize = 8;
    }
    // Can't use NEON instructions.
    if (UnitSize == 0)
      UnitSize = 4;
  }

  // Select the correct opcode and register class for unit size load/store
  bool IsNeon = UnitSize >= 8;
  TRC = IsThumb ? &ARM::tGPRRegClass : &ARM::GPRRegClass;
  if (IsNeon)
    VecTRC = UnitSize == 16 ? &ARM::DPairRegClass
                            : UnitSize == 8 ? &ARM::DPRRegClass
                                            : nullptr;

  unsigned BytesLeft = SizeVal % UnitSize;
  unsigned LoopSize = SizeVal - BytesLeft;

  if (SizeVal <= Subtarget->getMaxInlineSizeThreshold()) {
    // Use LDR and STR to copy.
    // [scratch, srcOut] = LDR_POST(srcIn, UnitSize)
    // [destOut] = STR_POST(scratch, destIn, UnitSize)
    unsigned srcIn = src;
    unsigned destIn = dest;
    for (unsigned i = 0; i < LoopSize; i+=UnitSize) {
      unsigned srcOut = MRI.createVirtualRegister(TRC);
      unsigned destOut = MRI.createVirtualRegister(TRC);
      unsigned scratch = MRI.createVirtualRegister(IsNeon ? VecTRC : TRC);
      emitPostLd(BB, MI, TII, dl, UnitSize, scratch, srcIn, srcOut,
                 IsThumb1, IsThumb2);
      emitPostSt(BB, MI, TII, dl, UnitSize, scratch, destIn, destOut,
                 IsThumb1, IsThumb2);
      srcIn = srcOut;
      destIn = destOut;
    }

    // Handle the leftover bytes with LDRB and STRB.
    // [scratch, srcOut] = LDRB_POST(srcIn, 1)
    // [destOut] = STRB_POST(scratch, destIn, 1)
    for (unsigned i = 0; i < BytesLeft; i++) {
      unsigned srcOut = MRI.createVirtualRegister(TRC);
      unsigned destOut = MRI.createVirtualRegister(TRC);
      unsigned scratch = MRI.createVirtualRegister(TRC);
      emitPostLd(BB, MI, TII, dl, 1, scratch, srcIn, srcOut,
                 IsThumb1, IsThumb2);
      emitPostSt(BB, MI, TII, dl, 1, scratch, destIn, destOut,
                 IsThumb1, IsThumb2);
      srcIn = srcOut;
      destIn = destOut;
    }
    MI.eraseFromParent(); // The instruction is gone now.
    return BB;
  }

  // Expand the pseudo op to a loop.
  // thisMBB:
  //   ...
  //   movw varEnd, # --> with thumb2
  //   movt varEnd, #
  //   ldrcp varEnd, idx --> without thumb2
  //   fallthrough --> loopMBB
  // loopMBB:
  //   PHI varPhi, varEnd, varLoop
  //   PHI srcPhi, src, srcLoop
  //   PHI destPhi, dst, destLoop
  //   [scratch, srcLoop] = LDR_POST(srcPhi, UnitSize)
  //   [destLoop] = STR_POST(scratch, destPhi, UnitSize)
  //   subs varLoop, varPhi, #UnitSize
  //   bne loopMBB
  //   fallthrough --> exitMBB
  // exitMBB:
  //   epilogue to handle left-over bytes
  //   [scratch, srcOut] = LDRB_POST(srcLoop, 1)
  //   [destOut] = STRB_POST(scratch, destLoop, 1)
  MachineBasicBlock *loopMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *exitMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(It, loopMBB);
  MF->insert(It, exitMBB);

  // Transfer the remainder of BB and its successor edges to exitMBB.
  exitMBB->splice(exitMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  exitMBB->transferSuccessorsAndUpdatePHIs(BB);

  // Load an immediate to varEnd.
  unsigned varEnd = MRI.createVirtualRegister(TRC);
  if (Subtarget->useMovt(*MF)) {
    unsigned Vtmp = varEnd;
    if ((LoopSize & 0xFFFF0000) != 0)
      Vtmp = MRI.createVirtualRegister(TRC);
    BuildMI(BB, dl, TII->get(IsThumb ? ARM::t2MOVi16 : ARM::MOVi16), Vtmp)
        .addImm(LoopSize & 0xFFFF)
        .add(predOps(ARMCC::AL));

    if ((LoopSize & 0xFFFF0000) != 0)
      BuildMI(BB, dl, TII->get(IsThumb ? ARM::t2MOVTi16 : ARM::MOVTi16), varEnd)
          .addReg(Vtmp)
          .addImm(LoopSize >> 16)
          .add(predOps(ARMCC::AL));
  } else {
    MachineConstantPool *ConstantPool = MF->getConstantPool();
    Type *Int32Ty = Type::getInt32Ty(MF->getFunction().getContext());
    const Constant *C = ConstantInt::get(Int32Ty, LoopSize);

    // MachineConstantPool wants an explicit alignment.
    unsigned Align = MF->getDataLayout().getPrefTypeAlignment(Int32Ty);
    if (Align == 0)
      Align = MF->getDataLayout().getTypeAllocSize(C->getType());
    unsigned Idx = ConstantPool->getConstantPoolIndex(C, Align);

    if (IsThumb)
      BuildMI(*BB, MI, dl, TII->get(ARM::tLDRpci))
          .addReg(varEnd, RegState::Define)
          .addConstantPoolIndex(Idx)
          .add(predOps(ARMCC::AL));
    else
      BuildMI(*BB, MI, dl, TII->get(ARM::LDRcp))
          .addReg(varEnd, RegState::Define)
          .addConstantPoolIndex(Idx)
          .addImm(0)
          .add(predOps(ARMCC::AL));
  }
  BB->addSuccessor(loopMBB);

  // Generate the loop body:
  //   varPhi = PHI(varLoop, varEnd)
  //   srcPhi = PHI(srcLoop, src)
  //   destPhi = PHI(destLoop, dst)
  MachineBasicBlock *entryBB = BB;
  BB = loopMBB;
  unsigned varLoop = MRI.createVirtualRegister(TRC);
  unsigned varPhi = MRI.createVirtualRegister(TRC);
  unsigned srcLoop = MRI.createVirtualRegister(TRC);
  unsigned srcPhi = MRI.createVirtualRegister(TRC);
  unsigned destLoop = MRI.createVirtualRegister(TRC);
  unsigned destPhi = MRI.createVirtualRegister(TRC);

  BuildMI(*BB, BB->begin(), dl, TII->get(ARM::PHI), varPhi)
    .addReg(varLoop).addMBB(loopMBB)
    .addReg(varEnd).addMBB(entryBB);
  BuildMI(BB, dl, TII->get(ARM::PHI), srcPhi)
    .addReg(srcLoop).addMBB(loopMBB)
    .addReg(src).addMBB(entryBB);
  BuildMI(BB, dl, TII->get(ARM::PHI), destPhi)
    .addReg(destLoop).addMBB(loopMBB)
    .addReg(dest).addMBB(entryBB);

  //   [scratch, srcLoop] = LDR_POST(srcPhi, UnitSize)
  //   [destLoop] = STR_POST(scratch, destPhi, UnitSiz)
  unsigned scratch = MRI.createVirtualRegister(IsNeon ? VecTRC : TRC);
  emitPostLd(BB, BB->end(), TII, dl, UnitSize, scratch, srcPhi, srcLoop,
             IsThumb1, IsThumb2);
  emitPostSt(BB, BB->end(), TII, dl, UnitSize, scratch, destPhi, destLoop,
             IsThumb1, IsThumb2);

  // Decrement loop variable by UnitSize.
  if (IsThumb1) {
    BuildMI(*BB, BB->end(), dl, TII->get(ARM::tSUBi8), varLoop)
        .add(t1CondCodeOp())
        .addReg(varPhi)
        .addImm(UnitSize)
        .add(predOps(ARMCC::AL));
  } else {
    MachineInstrBuilder MIB =
        BuildMI(*BB, BB->end(), dl,
                TII->get(IsThumb2 ? ARM::t2SUBri : ARM::SUBri), varLoop);
    MIB.addReg(varPhi)
        .addImm(UnitSize)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    MIB->getOperand(5).setReg(ARM::CPSR);
    MIB->getOperand(5).setIsDef(true);
  }
  BuildMI(*BB, BB->end(), dl,
          TII->get(IsThumb1 ? ARM::tBcc : IsThumb2 ? ARM::t2Bcc : ARM::Bcc))
      .addMBB(loopMBB).addImm(ARMCC::NE).addReg(ARM::CPSR);

  // loopMBB can loop back to loopMBB or fall through to exitMBB.
  BB->addSuccessor(loopMBB);
  BB->addSuccessor(exitMBB);

  // Add epilogue to handle BytesLeft.
  BB = exitMBB;
  auto StartOfExit = exitMBB->begin();

  //   [scratch, srcOut] = LDRB_POST(srcLoop, 1)
  //   [destOut] = STRB_POST(scratch, destLoop, 1)
  unsigned srcIn = srcLoop;
  unsigned destIn = destLoop;
  for (unsigned i = 0; i < BytesLeft; i++) {
    unsigned srcOut = MRI.createVirtualRegister(TRC);
    unsigned destOut = MRI.createVirtualRegister(TRC);
    unsigned scratch = MRI.createVirtualRegister(TRC);
    emitPostLd(BB, StartOfExit, TII, dl, 1, scratch, srcIn, srcOut,
               IsThumb1, IsThumb2);
    emitPostSt(BB, StartOfExit, TII, dl, 1, scratch, destIn, destOut,
               IsThumb1, IsThumb2);
    srcIn = srcOut;
    destIn = destOut;
  }

  MI.eraseFromParent(); // The instruction is gone now.
  return BB;
}

MachineBasicBlock *
ARMTargetLowering::EmitLowered__chkstk(MachineInstr &MI,
                                       MachineBasicBlock *MBB) const {
  const TargetMachine &TM = getTargetMachine();
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  assert(Subtarget->isTargetWindows() &&
         "__chkstk is only supported on Windows");
  assert(Subtarget->isThumb2() && "Windows on ARM requires Thumb-2 mode");

  // __chkstk takes the number of words to allocate on the stack in R4, and
  // returns the stack adjustment in number of bytes in R4.  This will not
  // clober any other registers (other than the obvious lr).
  //
  // Although, technically, IP should be considered a register which may be
  // clobbered, the call itself will not touch it.  Windows on ARM is a pure
  // thumb-2 environment, so there is no interworking required.  As a result, we
  // do not expect a veneer to be emitted by the linker, clobbering IP.
  //
  // Each module receives its own copy of __chkstk, so no import thunk is
  // required, again, ensuring that IP is not clobbered.
  //
  // Finally, although some linkers may theoretically provide a trampoline for
  // out of range calls (which is quite common due to a 32M range limitation of
  // branches for Thumb), we can generate the long-call version via
  // -mcmodel=large, alleviating the need for the trampoline which may clobber
  // IP.

  switch (TM.getCodeModel()) {
  case CodeModel::Tiny:
    llvm_unreachable("Tiny code model not available on ARM.");
  case CodeModel::Small:
  case CodeModel::Medium:
  case CodeModel::Kernel:
    BuildMI(*MBB, MI, DL, TII.get(ARM::tBL))
        .add(predOps(ARMCC::AL))
        .addExternalSymbol("__chkstk")
        .addReg(ARM::R4, RegState::Implicit | RegState::Kill)
        .addReg(ARM::R4, RegState::Implicit | RegState::Define)
        .addReg(ARM::R12,
                RegState::Implicit | RegState::Define | RegState::Dead)
        .addReg(ARM::CPSR,
                RegState::Implicit | RegState::Define | RegState::Dead);
    break;
  case CodeModel::Large: {
    MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
    unsigned Reg = MRI.createVirtualRegister(&ARM::rGPRRegClass);

    BuildMI(*MBB, MI, DL, TII.get(ARM::t2MOVi32imm), Reg)
      .addExternalSymbol("__chkstk");
    BuildMI(*MBB, MI, DL, TII.get(ARM::tBLXr))
        .add(predOps(ARMCC::AL))
        .addReg(Reg, RegState::Kill)
        .addReg(ARM::R4, RegState::Implicit | RegState::Kill)
        .addReg(ARM::R4, RegState::Implicit | RegState::Define)
        .addReg(ARM::R12,
                RegState::Implicit | RegState::Define | RegState::Dead)
        .addReg(ARM::CPSR,
                RegState::Implicit | RegState::Define | RegState::Dead);
    break;
  }
  }

  BuildMI(*MBB, MI, DL, TII.get(ARM::t2SUBrr), ARM::SP)
      .addReg(ARM::SP, RegState::Kill)
      .addReg(ARM::R4, RegState::Kill)
      .setMIFlags(MachineInstr::FrameSetup)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());

  MI.eraseFromParent();
  return MBB;
}

MachineBasicBlock *
ARMTargetLowering::EmitLowered__dbzchk(MachineInstr &MI,
                                       MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();

  MachineBasicBlock *ContBB = MF->CreateMachineBasicBlock();
  MF->insert(++MBB->getIterator(), ContBB);
  ContBB->splice(ContBB->begin(), MBB,
                 std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  ContBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(ContBB);

  MachineBasicBlock *TrapBB = MF->CreateMachineBasicBlock();
  BuildMI(TrapBB, DL, TII->get(ARM::t__brkdiv0));
  MF->push_back(TrapBB);
  MBB->addSuccessor(TrapBB);

  BuildMI(*MBB, MI, DL, TII->get(ARM::tCMPi8))
      .addReg(MI.getOperand(0).getReg())
      .addImm(0)
      .add(predOps(ARMCC::AL));
  BuildMI(*MBB, MI, DL, TII->get(ARM::t2Bcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::EQ)
      .addReg(ARM::CPSR);

  MI.eraseFromParent();
  return ContBB;
}

// The CPSR operand of SelectItr might be missing a kill marker
// because there were multiple uses of CPSR, and ISel didn't know
// which to mark. Figure out whether SelectItr should have had a
// kill marker, and set it if it should. Returns the correct kill
// marker value.
static bool checkAndUpdateCPSRKill(MachineBasicBlock::iterator SelectItr,
                                   MachineBasicBlock* BB,
                                   const TargetRegisterInfo* TRI) {
  // Scan forward through BB for a use/def of CPSR.
  MachineBasicBlock::iterator miI(std::next(SelectItr));
  for (MachineBasicBlock::iterator miE = BB->end(); miI != miE; ++miI) {
    const MachineInstr& mi = *miI;
    if (mi.readsRegister(ARM::CPSR))
      return false;
    if (mi.definesRegister(ARM::CPSR))
      break; // Should have kill-flag - update below.
  }

  // If we hit the end of the block, check whether CPSR is live into a
  // successor.
  if (miI == BB->end()) {
    for (MachineBasicBlock::succ_iterator sItr = BB->succ_begin(),
                                          sEnd = BB->succ_end();
         sItr != sEnd; ++sItr) {
      MachineBasicBlock* succ = *sItr;
      if (succ->isLiveIn(ARM::CPSR))
        return false;
    }
  }

  // We found a def, or hit the end of the basic block and CPSR wasn't live
  // out. SelectMI should have a kill flag on CPSR.
  SelectItr->addRegisterKilled(ARM::CPSR, TRI);
  return true;
}

MachineBasicBlock *
ARMTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  bool isThumb2 = Subtarget->isThumb2();
  switch (MI.getOpcode()) {
  default: {
    MI.print(errs());
    llvm_unreachable("Unexpected instr type to insert");
  }

  // Thumb1 post-indexed loads are really just single-register LDMs.
  case ARM::tLDR_postidx: {
    MachineOperand Def(MI.getOperand(1));
    BuildMI(*BB, MI, dl, TII->get(ARM::tLDMIA_UPD))
        .add(Def)  // Rn_wb
        .add(MI.getOperand(2))  // Rn
        .add(MI.getOperand(3))  // PredImm
        .add(MI.getOperand(4))  // PredReg
        .add(MI.getOperand(0)); // Rt
    MI.eraseFromParent();
    return BB;
  }

  // The Thumb2 pre-indexed stores have the same MI operands, they just
  // define them differently in the .td files from the isel patterns, so
  // they need pseudos.
  case ARM::t2STR_preidx:
    MI.setDesc(TII->get(ARM::t2STR_PRE));
    return BB;
  case ARM::t2STRB_preidx:
    MI.setDesc(TII->get(ARM::t2STRB_PRE));
    return BB;
  case ARM::t2STRH_preidx:
    MI.setDesc(TII->get(ARM::t2STRH_PRE));
    return BB;

  case ARM::STRi_preidx:
  case ARM::STRBi_preidx: {
    unsigned NewOpc = MI.getOpcode() == ARM::STRi_preidx ? ARM::STR_PRE_IMM
                                                         : ARM::STRB_PRE_IMM;
    // Decode the offset.
    unsigned Offset = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(Offset) == ARM_AM::sub;
    Offset = ARM_AM::getAM2Offset(Offset);
    if (isSub)
      Offset = -Offset;

    MachineMemOperand *MMO = *MI.memoperands_begin();
    BuildMI(*BB, MI, dl, TII->get(NewOpc))
        .add(MI.getOperand(0)) // Rn_wb
        .add(MI.getOperand(1)) // Rt
        .add(MI.getOperand(2)) // Rn
        .addImm(Offset)        // offset (skip GPR==zero_reg)
        .add(MI.getOperand(5)) // pred
        .add(MI.getOperand(6))
        .addMemOperand(MMO);
    MI.eraseFromParent();
    return BB;
  }
  case ARM::STRr_preidx:
  case ARM::STRBr_preidx:
  case ARM::STRH_preidx: {
    unsigned NewOpc;
    switch (MI.getOpcode()) {
    default: llvm_unreachable("unexpected opcode!");
    case ARM::STRr_preidx: NewOpc = ARM::STR_PRE_REG; break;
    case ARM::STRBr_preidx: NewOpc = ARM::STRB_PRE_REG; break;
    case ARM::STRH_preidx: NewOpc = ARM::STRH_PRE; break;
    }
    MachineInstrBuilder MIB = BuildMI(*BB, MI, dl, TII->get(NewOpc));
    for (unsigned i = 0; i < MI.getNumOperands(); ++i)
      MIB.add(MI.getOperand(i));
    MI.eraseFromParent();
    return BB;
  }

  case ARM::tMOVCCr_pseudo: {
    // To "insert" a SELECT_CC instruction, we actually have to insert the
    // diamond control-flow pattern.  The incoming instruction knows the
    // destination vreg to set, the condition code register to branch on, the
    // true/false values to select between, and a branch opcode to use.
    const BasicBlock *LLVM_BB = BB->getBasicBlock();
    MachineFunction::iterator It = ++BB->getIterator();

    //  thisMBB:
    //  ...
    //   TrueVal = ...
    //   cmpTY ccX, r1, r2
    //   bCC copy1MBB
    //   fallthrough --> copy0MBB
    MachineBasicBlock *thisMBB  = BB;
    MachineFunction *F = BB->getParent();
    MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *sinkMBB  = F->CreateMachineBasicBlock(LLVM_BB);
    F->insert(It, copy0MBB);
    F->insert(It, sinkMBB);

    // Check whether CPSR is live past the tMOVCCr_pseudo.
    const TargetRegisterInfo *TRI = Subtarget->getRegisterInfo();
    if (!MI.killsRegister(ARM::CPSR) &&
        !checkAndUpdateCPSRKill(MI, thisMBB, TRI)) {
      copy0MBB->addLiveIn(ARM::CPSR);
      sinkMBB->addLiveIn(ARM::CPSR);
    }

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    sinkMBB->splice(sinkMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

    BB->addSuccessor(copy0MBB);
    BB->addSuccessor(sinkMBB);

    BuildMI(BB, dl, TII->get(ARM::tBcc))
        .addMBB(sinkMBB)
        .addImm(MI.getOperand(3).getImm())
        .addReg(MI.getOperand(4).getReg());

    //  copy0MBB:
    //   %FalseValue = ...
    //   # fallthrough to sinkMBB
    BB = copy0MBB;

    // Update machine-CFG edges
    BB->addSuccessor(sinkMBB);

    //  sinkMBB:
    //   %Result = phi [ %FalseValue, copy0MBB ], [ %TrueValue, thisMBB ]
    //  ...
    BB = sinkMBB;
    BuildMI(*BB, BB->begin(), dl, TII->get(ARM::PHI), MI.getOperand(0).getReg())
        .addReg(MI.getOperand(1).getReg())
        .addMBB(copy0MBB)
        .addReg(MI.getOperand(2).getReg())
        .addMBB(thisMBB);

    MI.eraseFromParent(); // The pseudo instruction is gone now.
    return BB;
  }

  case ARM::BCCi64:
  case ARM::BCCZi64: {
    // If there is an unconditional branch to the other successor, remove it.
    BB->erase(std::next(MachineBasicBlock::iterator(MI)), BB->end());

    // Compare both parts that make up the double comparison separately for
    // equality.
    bool RHSisZero = MI.getOpcode() == ARM::BCCZi64;

    unsigned LHS1 = MI.getOperand(1).getReg();
    unsigned LHS2 = MI.getOperand(2).getReg();
    if (RHSisZero) {
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPri : ARM::CMPri))
          .addReg(LHS1)
          .addImm(0)
          .add(predOps(ARMCC::AL));
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPri : ARM::CMPri))
        .addReg(LHS2).addImm(0)
        .addImm(ARMCC::EQ).addReg(ARM::CPSR);
    } else {
      unsigned RHS1 = MI.getOperand(3).getReg();
      unsigned RHS2 = MI.getOperand(4).getReg();
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPrr : ARM::CMPrr))
          .addReg(LHS1)
          .addReg(RHS1)
          .add(predOps(ARMCC::AL));
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPrr : ARM::CMPrr))
        .addReg(LHS2).addReg(RHS2)
        .addImm(ARMCC::EQ).addReg(ARM::CPSR);
    }

    MachineBasicBlock *destMBB = MI.getOperand(RHSisZero ? 3 : 5).getMBB();
    MachineBasicBlock *exitMBB = OtherSucc(BB, destMBB);
    if (MI.getOperand(0).getImm() == ARMCC::NE)
      std::swap(destMBB, exitMBB);

    BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2Bcc : ARM::Bcc))
      .addMBB(destMBB).addImm(ARMCC::EQ).addReg(ARM::CPSR);
    if (isThumb2)
      BuildMI(BB, dl, TII->get(ARM::t2B))
          .addMBB(exitMBB)
          .add(predOps(ARMCC::AL));
    else
      BuildMI(BB, dl, TII->get(ARM::B)) .addMBB(exitMBB);

    MI.eraseFromParent(); // The pseudo instruction is gone now.
    return BB;
  }

  case ARM::Int_eh_sjlj_setjmp:
  case ARM::Int_eh_sjlj_setjmp_nofp:
  case ARM::tInt_eh_sjlj_setjmp:
  case ARM::t2Int_eh_sjlj_setjmp:
  case ARM::t2Int_eh_sjlj_setjmp_nofp:
    return BB;

  case ARM::Int_eh_sjlj_setup_dispatch:
    EmitSjLjDispatchBlock(MI, BB);
    return BB;

  case ARM::ABS:
  case ARM::t2ABS: {
    // To insert an ABS instruction, we have to insert the
    // diamond control-flow pattern.  The incoming instruction knows the
    // source vreg to test against 0, the destination vreg to set,
    // the condition code register to branch on, the
    // true/false values to select between, and a branch opcode to use.
    // It transforms
    //     V1 = ABS V0
    // into
    //     V2 = MOVS V0
    //     BCC                      (branch to SinkBB if V0 >= 0)
    //     RSBBB: V3 = RSBri V2, 0  (compute ABS if V2 < 0)
    //     SinkBB: V1 = PHI(V2, V3)
    const BasicBlock *LLVM_BB = BB->getBasicBlock();
    MachineFunction::iterator BBI = ++BB->getIterator();
    MachineFunction *Fn = BB->getParent();
    MachineBasicBlock *RSBBB = Fn->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *SinkBB  = Fn->CreateMachineBasicBlock(LLVM_BB);
    Fn->insert(BBI, RSBBB);
    Fn->insert(BBI, SinkBB);

    unsigned int ABSSrcReg = MI.getOperand(1).getReg();
    unsigned int ABSDstReg = MI.getOperand(0).getReg();
    bool ABSSrcKIll = MI.getOperand(1).isKill();
    bool isThumb2 = Subtarget->isThumb2();
    MachineRegisterInfo &MRI = Fn->getRegInfo();
    // In Thumb mode S must not be specified if source register is the SP or
    // PC and if destination register is the SP, so restrict register class
    unsigned NewRsbDstReg =
      MRI.createVirtualRegister(isThumb2 ? &ARM::rGPRRegClass : &ARM::GPRRegClass);

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    SinkBB->splice(SinkBB->begin(), BB,
                   std::next(MachineBasicBlock::iterator(MI)), BB->end());
    SinkBB->transferSuccessorsAndUpdatePHIs(BB);

    BB->addSuccessor(RSBBB);
    BB->addSuccessor(SinkBB);

    // fall through to SinkMBB
    RSBBB->addSuccessor(SinkBB);

    // insert a cmp at the end of BB
    BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPri : ARM::CMPri))
        .addReg(ABSSrcReg)
        .addImm(0)
        .add(predOps(ARMCC::AL));

    // insert a bcc with opposite CC to ARMCC::MI at the end of BB
    BuildMI(BB, dl,
      TII->get(isThumb2 ? ARM::t2Bcc : ARM::Bcc)).addMBB(SinkBB)
      .addImm(ARMCC::getOppositeCondition(ARMCC::MI)).addReg(ARM::CPSR);

    // insert rsbri in RSBBB
    // Note: BCC and rsbri will be converted into predicated rsbmi
    // by if-conversion pass
    BuildMI(*RSBBB, RSBBB->begin(), dl,
            TII->get(isThumb2 ? ARM::t2RSBri : ARM::RSBri), NewRsbDstReg)
        .addReg(ABSSrcReg, ABSSrcKIll ? RegState::Kill : 0)
        .addImm(0)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());

    // insert PHI in SinkBB,
    // reuse ABSDstReg to not change uses of ABS instruction
    BuildMI(*SinkBB, SinkBB->begin(), dl,
      TII->get(ARM::PHI), ABSDstReg)
      .addReg(NewRsbDstReg).addMBB(RSBBB)
      .addReg(ABSSrcReg).addMBB(BB);

    // remove ABS instruction
    MI.eraseFromParent();

    // return last added BB
    return SinkBB;
  }
  case ARM::COPY_STRUCT_BYVAL_I32:
    ++NumLoopByVals;
    return EmitStructByval(MI, BB);
  case ARM::WIN__CHKSTK:
    return EmitLowered__chkstk(MI, BB);
  case ARM::WIN__DBZCHK:
    return EmitLowered__dbzchk(MI, BB);
  }
}

/// Attaches vregs to MEMCPY that it will use as scratch registers
/// when it is expanded into LDM/STM. This is done as a post-isel lowering
/// instead of as a custom inserter because we need the use list from the SDNode.
static void attachMEMCPYScratchRegs(const ARMSubtarget *Subtarget,
                                    MachineInstr &MI, const SDNode *Node) {
  bool isThumb1 = Subtarget->isThumb1Only();

  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  MachineInstrBuilder MIB(*MF, MI);

  // If the new dst/src is unused mark it as dead.
  if (!Node->hasAnyUseOfValue(0)) {
    MI.getOperand(0).setIsDead(true);
  }
  if (!Node->hasAnyUseOfValue(1)) {
    MI.getOperand(1).setIsDead(true);
  }

  // The MEMCPY both defines and kills the scratch registers.
  for (unsigned I = 0; I != MI.getOperand(4).getImm(); ++I) {
    unsigned TmpReg = MRI.createVirtualRegister(isThumb1 ? &ARM::tGPRRegClass
                                                         : &ARM::GPRRegClass);
    MIB.addReg(TmpReg, RegState::Define|RegState::Dead);
  }
}

void ARMTargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                      SDNode *Node) const {
  if (MI.getOpcode() == ARM::MEMCPY) {
    attachMEMCPYScratchRegs(Subtarget, MI, Node);
    return;
  }

  const MCInstrDesc *MCID = &MI.getDesc();
  // Adjust potentially 's' setting instructions after isel, i.e. ADC, SBC, RSB,
  // RSC. Coming out of isel, they have an implicit CPSR def, but the optional
  // operand is still set to noreg. If needed, set the optional operand's
  // register to CPSR, and remove the redundant implicit def.
  //
  // e.g. ADCS (..., implicit-def CPSR) -> ADC (... opt:def CPSR).

  // Rename pseudo opcodes.
  unsigned NewOpc = convertAddSubFlagsOpcode(MI.getOpcode());
  unsigned ccOutIdx;
  if (NewOpc) {
    const ARMBaseInstrInfo *TII = Subtarget->getInstrInfo();
    MCID = &TII->get(NewOpc);

    assert(MCID->getNumOperands() ==
           MI.getDesc().getNumOperands() + 5 - MI.getDesc().getSize()
        && "converted opcode should be the same except for cc_out"
           " (and, on Thumb1, pred)");

    MI.setDesc(*MCID);

    // Add the optional cc_out operand
    MI.addOperand(MachineOperand::CreateReg(0, /*isDef=*/true));

    // On Thumb1, move all input operands to the end, then add the predicate
    if (Subtarget->isThumb1Only()) {
      for (unsigned c = MCID->getNumOperands() - 4; c--;) {
        MI.addOperand(MI.getOperand(1));
        MI.RemoveOperand(1);
      }

      // Restore the ties
      for (unsigned i = MI.getNumOperands(); i--;) {
        const MachineOperand& op = MI.getOperand(i);
        if (op.isReg() && op.isUse()) {
          int DefIdx = MCID->getOperandConstraint(i, MCOI::TIED_TO);
          if (DefIdx != -1)
            MI.tieOperands(DefIdx, i);
        }
      }

      MI.addOperand(MachineOperand::CreateImm(ARMCC::AL));
      MI.addOperand(MachineOperand::CreateReg(0, /*isDef=*/false));
      ccOutIdx = 1;
    } else
      ccOutIdx = MCID->getNumOperands() - 1;
  } else
    ccOutIdx = MCID->getNumOperands() - 1;

  // Any ARM instruction that sets the 's' bit should specify an optional
  // "cc_out" operand in the last operand position.
  if (!MI.hasOptionalDef() || !MCID->OpInfo[ccOutIdx].isOptionalDef()) {
    assert(!NewOpc && "Optional cc_out operand required");
    return;
  }
  // Look for an implicit def of CPSR added by MachineInstr ctor. Remove it
  // since we already have an optional CPSR def.
  bool definesCPSR = false;
  bool deadCPSR = false;
  for (unsigned i = MCID->getNumOperands(), e = MI.getNumOperands(); i != e;
       ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg() && MO.isDef() && MO.getReg() == ARM::CPSR) {
      definesCPSR = true;
      if (MO.isDead())
        deadCPSR = true;
      MI.RemoveOperand(i);
      break;
    }
  }
  if (!definesCPSR) {
    assert(!NewOpc && "Optional cc_out operand required");
    return;
  }
  assert(deadCPSR == !Node->hasAnyUseOfValue(1) && "inconsistent dead flag");
  if (deadCPSR) {
    assert(!MI.getOperand(ccOutIdx).getReg() &&
           "expect uninitialized optional cc_out operand");
    // Thumb1 instructions must have the S bit even if the CPSR is dead.
    if (!Subtarget->isThumb1Only())
      return;
  }

  // If this instruction was defined with an optional CPSR def and its dag node
  // had a live implicit CPSR def, then activate the optional CPSR def.
  MachineOperand &MO = MI.getOperand(ccOutIdx);
  MO.setReg(ARM::CPSR);
  MO.setIsDef(true);
}

//===----------------------------------------------------------------------===//
//                           ARM Optimization Hooks
//===----------------------------------------------------------------------===//

// Helper function that checks if N is a null or all ones constant.
static inline bool isZeroOrAllOnes(SDValue N, bool AllOnes) {
  return AllOnes ? isAllOnesConstant(N) : isNullConstant(N);
}

// Return true if N is conditionally 0 or all ones.
// Detects these expressions where cc is an i1 value:
//
//   (select cc 0, y)   [AllOnes=0]
//   (select cc y, 0)   [AllOnes=0]
//   (zext cc)          [AllOnes=0]
//   (sext cc)          [AllOnes=0/1]
//   (select cc -1, y)  [AllOnes=1]
//   (select cc y, -1)  [AllOnes=1]
//
// Invert is set when N is the null/all ones constant when CC is false.
// OtherOp is set to the alternative value of N.
static bool isConditionalZeroOrAllOnes(SDNode *N, bool AllOnes,
                                       SDValue &CC, bool &Invert,
                                       SDValue &OtherOp,
                                       SelectionDAG &DAG) {
  switch (N->getOpcode()) {
  default: return false;
  case ISD::SELECT: {
    CC = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    SDValue N2 = N->getOperand(2);
    if (isZeroOrAllOnes(N1, AllOnes)) {
      Invert = false;
      OtherOp = N2;
      return true;
    }
    if (isZeroOrAllOnes(N2, AllOnes)) {
      Invert = true;
      OtherOp = N1;
      return true;
    }
    return false;
  }
  case ISD::ZERO_EXTEND:
    // (zext cc) can never be the all ones value.
    if (AllOnes)
      return false;
    LLVM_FALLTHROUGH;
  case ISD::SIGN_EXTEND: {
    SDLoc dl(N);
    EVT VT = N->getValueType(0);
    CC = N->getOperand(0);
    if (CC.getValueType() != MVT::i1 || CC.getOpcode() != ISD::SETCC)
      return false;
    Invert = !AllOnes;
    if (AllOnes)
      // When looking for an AllOnes constant, N is an sext, and the 'other'
      // value is 0.
      OtherOp = DAG.getConstant(0, dl, VT);
    else if (N->getOpcode() == ISD::ZERO_EXTEND)
      // When looking for a 0 constant, N can be zext or sext.
      OtherOp = DAG.getConstant(1, dl, VT);
    else
      OtherOp = DAG.getConstant(APInt::getAllOnesValue(VT.getSizeInBits()), dl,
                                VT);
    return true;
  }
  }
}

// Combine a constant select operand into its use:
//
//   (add (select cc, 0, c), x)  -> (select cc, x, (add, x, c))
//   (sub x, (select cc, 0, c))  -> (select cc, x, (sub, x, c))
//   (and (select cc, -1, c), x) -> (select cc, x, (and, x, c))  [AllOnes=1]
//   (or  (select cc, 0, c), x)  -> (select cc, x, (or, x, c))
//   (xor (select cc, 0, c), x)  -> (select cc, x, (xor, x, c))
//
// The transform is rejected if the select doesn't have a constant operand that
// is null, or all ones when AllOnes is set.
//
// Also recognize sext/zext from i1:
//
//   (add (zext cc), x) -> (select cc (add x, 1), x)
//   (add (sext cc), x) -> (select cc (add x, -1), x)
//
// These transformations eventually create predicated instructions.
//
// @param N       The node to transform.
// @param Slct    The N operand that is a select.
// @param OtherOp The other N operand (x above).
// @param DCI     Context.
// @param AllOnes Require the select constant to be all ones instead of null.
// @returns The new node, or SDValue() on failure.
static
SDValue combineSelectAndUse(SDNode *N, SDValue Slct, SDValue OtherOp,
                            TargetLowering::DAGCombinerInfo &DCI,
                            bool AllOnes = false) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDValue NonConstantVal;
  SDValue CCOp;
  bool SwapSelectOps;
  if (!isConditionalZeroOrAllOnes(Slct.getNode(), AllOnes, CCOp, SwapSelectOps,
                                  NonConstantVal, DAG))
    return SDValue();

  // Slct is now know to be the desired identity constant when CC is true.
  SDValue TrueVal = OtherOp;
  SDValue FalseVal = DAG.getNode(N->getOpcode(), SDLoc(N), VT,
                                 OtherOp, NonConstantVal);
  // Unless SwapSelectOps says CC should be false.
  if (SwapSelectOps)
    std::swap(TrueVal, FalseVal);

  return DAG.getNode(ISD::SELECT, SDLoc(N), VT,
                     CCOp, TrueVal, FalseVal);
}

// Attempt combineSelectAndUse on each operand of a commutative operator N.
static
SDValue combineSelectAndUseCommutative(SDNode *N, bool AllOnes,
                                       TargetLowering::DAGCombinerInfo &DCI) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (N0.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N0, N1, DCI, AllOnes))
      return Result;
  if (N1.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N1, N0, DCI, AllOnes))
      return Result;
  return SDValue();
}

static bool IsVUZPShuffleNode(SDNode *N) {
  // VUZP shuffle node.
  if (N->getOpcode() == ARMISD::VUZP)
    return true;

  // "VUZP" on i32 is an alias for VTRN.
  if (N->getOpcode() == ARMISD::VTRN && N->getValueType(0) == MVT::v2i32)
    return true;

  return false;
}

static SDValue AddCombineToVPADD(SDNode *N, SDValue N0, SDValue N1,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  // Look for ADD(VUZP.0, VUZP.1).
  if (!IsVUZPShuffleNode(N0.getNode()) || N0.getNode() != N1.getNode() ||
      N0 == N1)
   return SDValue();

  // Make sure the ADD is a 64-bit add; there is no 128-bit VPADD.
  if (!N->getValueType(0).is64BitVector())
    return SDValue();

  // Generate vpadd.
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc dl(N);
  SDNode *Unzip = N0.getNode();
  EVT VT = N->getValueType(0);

  SmallVector<SDValue, 8> Ops;
  Ops.push_back(DAG.getConstant(Intrinsic::arm_neon_vpadd, dl,
                                TLI.getPointerTy(DAG.getDataLayout())));
  Ops.push_back(Unzip->getOperand(0));
  Ops.push_back(Unzip->getOperand(1));

  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT, Ops);
}

static SDValue AddCombineVUZPToVPADDL(SDNode *N, SDValue N0, SDValue N1,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  // Check for two extended operands.
  if (!(N0.getOpcode() == ISD::SIGN_EXTEND &&
        N1.getOpcode() == ISD::SIGN_EXTEND) &&
      !(N0.getOpcode() == ISD::ZERO_EXTEND &&
        N1.getOpcode() == ISD::ZERO_EXTEND))
    return SDValue();

  SDValue N00 = N0.getOperand(0);
  SDValue N10 = N1.getOperand(0);

  // Look for ADD(SEXT(VUZP.0), SEXT(VUZP.1))
  if (!IsVUZPShuffleNode(N00.getNode()) || N00.getNode() != N10.getNode() ||
      N00 == N10)
    return SDValue();

  // We only recognize Q register paddl here; this can't be reached until
  // after type legalization.
  if (!N00.getValueType().is64BitVector() ||
      !N0.getValueType().is128BitVector())
    return SDValue();

  // Generate vpaddl.
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc dl(N);
  EVT VT = N->getValueType(0);

  SmallVector<SDValue, 8> Ops;
  // Form vpaddl.sN or vpaddl.uN depending on the kind of extension.
  unsigned Opcode;
  if (N0.getOpcode() == ISD::SIGN_EXTEND)
    Opcode = Intrinsic::arm_neon_vpaddls;
  else
    Opcode = Intrinsic::arm_neon_vpaddlu;
  Ops.push_back(DAG.getConstant(Opcode, dl,
                                TLI.getPointerTy(DAG.getDataLayout())));
  EVT ElemTy = N00.getValueType().getVectorElementType();
  unsigned NumElts = VT.getVectorNumElements();
  EVT ConcatVT = EVT::getVectorVT(*DAG.getContext(), ElemTy, NumElts * 2);
  SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), ConcatVT,
                               N00.getOperand(0), N00.getOperand(1));
  Ops.push_back(Concat);

  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT, Ops);
}

// FIXME: This function shouldn't be necessary; if we lower BUILD_VECTOR in
// an appropriate manner, we end up with ADD(VUZP(ZEXT(N))), which is
// much easier to match.
static SDValue
AddCombineBUILD_VECTORToVPADDL(SDNode *N, SDValue N0, SDValue N1,
                               TargetLowering::DAGCombinerInfo &DCI,
                               const ARMSubtarget *Subtarget) {
  // Only perform optimization if after legalize, and if NEON is available. We
  // also expected both operands to be BUILD_VECTORs.
  if (DCI.isBeforeLegalize() || !Subtarget->hasNEON()
      || N0.getOpcode() != ISD::BUILD_VECTOR
      || N1.getOpcode() != ISD::BUILD_VECTOR)
    return SDValue();

  // Check output type since VPADDL operand elements can only be 8, 16, or 32.
  EVT VT = N->getValueType(0);
  if (!VT.isInteger() || VT.getVectorElementType() == MVT::i64)
    return SDValue();

  // Check that the vector operands are of the right form.
  // N0 and N1 are BUILD_VECTOR nodes with N number of EXTRACT_VECTOR
  // operands, where N is the size of the formed vector.
  // Each EXTRACT_VECTOR should have the same input vector and odd or even
  // index such that we have a pair wise add pattern.

  // Grab the vector that all EXTRACT_VECTOR nodes should be referencing.
  if (N0->getOperand(0)->getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    return SDValue();
  SDValue Vec = N0->getOperand(0)->getOperand(0);
  SDNode *V = Vec.getNode();
  unsigned nextIndex = 0;

  // For each operands to the ADD which are BUILD_VECTORs,
  // check to see if each of their operands are an EXTRACT_VECTOR with
  // the same vector and appropriate index.
  for (unsigned i = 0, e = N0->getNumOperands(); i != e; ++i) {
    if (N0->getOperand(i)->getOpcode() == ISD::EXTRACT_VECTOR_ELT
        && N1->getOperand(i)->getOpcode() == ISD::EXTRACT_VECTOR_ELT) {

      SDValue ExtVec0 = N0->getOperand(i);
      SDValue ExtVec1 = N1->getOperand(i);

      // First operand is the vector, verify its the same.
      if (V != ExtVec0->getOperand(0).getNode() ||
          V != ExtVec1->getOperand(0).getNode())
        return SDValue();

      // Second is the constant, verify its correct.
      ConstantSDNode *C0 = dyn_cast<ConstantSDNode>(ExtVec0->getOperand(1));
      ConstantSDNode *C1 = dyn_cast<ConstantSDNode>(ExtVec1->getOperand(1));

      // For the constant, we want to see all the even or all the odd.
      if (!C0 || !C1 || C0->getZExtValue() != nextIndex
          || C1->getZExtValue() != nextIndex+1)
        return SDValue();

      // Increment index.
      nextIndex+=2;
    } else
      return SDValue();
  }

  // Don't generate vpaddl+vmovn; we'll match it to vpadd later. Also make sure
  // we're using the entire input vector, otherwise there's a size/legality
  // mismatch somewhere.
  if (nextIndex != Vec.getValueType().getVectorNumElements() ||
      Vec.getValueType().getVectorElementType() == VT.getVectorElementType())
    return SDValue();

  // Create VPADDL node.
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  SDLoc dl(N);

  // Build operand list.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(DAG.getConstant(Intrinsic::arm_neon_vpaddls, dl,
                                TLI.getPointerTy(DAG.getDataLayout())));

  // Input is the vector.
  Ops.push_back(Vec);

  // Get widened type and narrowed type.
  MVT widenType;
  unsigned numElem = VT.getVectorNumElements();

  EVT inputLaneType = Vec.getValueType().getVectorElementType();
  switch (inputLaneType.getSimpleVT().SimpleTy) {
    case MVT::i8: widenType = MVT::getVectorVT(MVT::i16, numElem); break;
    case MVT::i16: widenType = MVT::getVectorVT(MVT::i32, numElem); break;
    case MVT::i32: widenType = MVT::getVectorVT(MVT::i64, numElem); break;
    default:
      llvm_unreachable("Invalid vector element type for padd optimization.");
  }

  SDValue tmp = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, widenType, Ops);
  unsigned ExtOp = VT.bitsGT(tmp.getValueType()) ? ISD::ANY_EXTEND : ISD::TRUNCATE;
  return DAG.getNode(ExtOp, dl, VT, tmp);
}

static SDValue findMUL_LOHI(SDValue V) {
  if (V->getOpcode() == ISD::UMUL_LOHI ||
      V->getOpcode() == ISD::SMUL_LOHI)
    return V;
  return SDValue();
}

static SDValue AddCombineTo64BitSMLAL16(SDNode *AddcNode, SDNode *AddeNode,
                                        TargetLowering::DAGCombinerInfo &DCI,
                                        const ARMSubtarget *Subtarget) {
  if (Subtarget->isThumb()) {
    if (!Subtarget->hasDSP())
      return SDValue();
  } else if (!Subtarget->hasV5TEOps())
    return SDValue();

  // SMLALBB, SMLALBT, SMLALTB, SMLALTT multiply two 16-bit values and
  // accumulates the product into a 64-bit value. The 16-bit values will
  // be sign extended somehow or SRA'd into 32-bit values
  // (addc (adde (mul 16bit, 16bit), lo), hi)
  SDValue Mul = AddcNode->getOperand(0);
  SDValue Lo = AddcNode->getOperand(1);
  if (Mul.getOpcode() != ISD::MUL) {
    Lo = AddcNode->getOperand(0);
    Mul = AddcNode->getOperand(1);
    if (Mul.getOpcode() != ISD::MUL)
      return SDValue();
  }

  SDValue SRA = AddeNode->getOperand(0);
  SDValue Hi = AddeNode->getOperand(1);
  if (SRA.getOpcode() != ISD::SRA) {
    SRA = AddeNode->getOperand(1);
    Hi = AddeNode->getOperand(0);
    if (SRA.getOpcode() != ISD::SRA)
      return SDValue();
  }
  if (auto Const = dyn_cast<ConstantSDNode>(SRA.getOperand(1))) {
    if (Const->getZExtValue() != 31)
      return SDValue();
  } else
    return SDValue();

  if (SRA.getOperand(0) != Mul)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(AddcNode);
  unsigned Opcode = 0;
  SDValue Op0;
  SDValue Op1;

  if (isS16(Mul.getOperand(0), DAG) && isS16(Mul.getOperand(1), DAG)) {
    Opcode = ARMISD::SMLALBB;
    Op0 = Mul.getOperand(0);
    Op1 = Mul.getOperand(1);
  } else if (isS16(Mul.getOperand(0), DAG) && isSRA16(Mul.getOperand(1))) {
    Opcode = ARMISD::SMLALBT;
    Op0 = Mul.getOperand(0);
    Op1 = Mul.getOperand(1).getOperand(0);
  } else if (isSRA16(Mul.getOperand(0)) && isS16(Mul.getOperand(1), DAG)) {
    Opcode = ARMISD::SMLALTB;
    Op0 = Mul.getOperand(0).getOperand(0);
    Op1 = Mul.getOperand(1);
  } else if (isSRA16(Mul.getOperand(0)) && isSRA16(Mul.getOperand(1))) {
    Opcode = ARMISD::SMLALTT;
    Op0 = Mul->getOperand(0).getOperand(0);
    Op1 = Mul->getOperand(1).getOperand(0);
  }

  if (!Op0 || !Op1)
    return SDValue();

  SDValue SMLAL = DAG.getNode(Opcode, dl, DAG.getVTList(MVT::i32, MVT::i32),
                              Op0, Op1, Lo, Hi);
  // Replace the ADDs' nodes uses by the MLA node's values.
  SDValue HiMLALResult(SMLAL.getNode(), 1);
  SDValue LoMLALResult(SMLAL.getNode(), 0);

  DAG.ReplaceAllUsesOfValueWith(SDValue(AddcNode, 0), LoMLALResult);
  DAG.ReplaceAllUsesOfValueWith(SDValue(AddeNode, 0), HiMLALResult);

  // Return original node to notify the driver to stop replacing.
  SDValue resNode(AddcNode, 0);
  return resNode;
}

static SDValue AddCombineTo64bitMLAL(SDNode *AddeSubeNode,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *Subtarget) {
  // Look for multiply add opportunities.
  // The pattern is a ISD::UMUL_LOHI followed by two add nodes, where
  // each add nodes consumes a value from ISD::UMUL_LOHI and there is
  // a glue link from the first add to the second add.
  // If we find this pattern, we can replace the U/SMUL_LOHI, ADDC, and ADDE by
  // a S/UMLAL instruction.
  //                  UMUL_LOHI
  //                 / :lo    \ :hi
  //                V          \          [no multiline comment]
  //    loAdd ->  ADDC         |
  //                 \ :carry /
  //                  V      V
  //                    ADDE   <- hiAdd
  //
  // In the special case where only the higher part of a signed result is used
  // and the add to the low part of the result of ISD::UMUL_LOHI adds or subtracts
  // a constant with the exact value of 0x80000000, we recognize we are dealing
  // with a "rounded multiply and add" (or subtract) and transform it into
  // either a ARMISD::SMMLAR or ARMISD::SMMLSR respectively.

  assert((AddeSubeNode->getOpcode() == ARMISD::ADDE ||
          AddeSubeNode->getOpcode() == ARMISD::SUBE) &&
         "Expect an ADDE or SUBE");

  assert(AddeSubeNode->getNumOperands() == 3 &&
         AddeSubeNode->getOperand(2).getValueType() == MVT::i32 &&
         "ADDE node has the wrong inputs");

  // Check that we are chained to the right ADDC or SUBC node.
  SDNode *AddcSubcNode = AddeSubeNode->getOperand(2).getNode();
  if ((AddeSubeNode->getOpcode() == ARMISD::ADDE &&
       AddcSubcNode->getOpcode() != ARMISD::ADDC) ||
      (AddeSubeNode->getOpcode() == ARMISD::SUBE &&
       AddcSubcNode->getOpcode() != ARMISD::SUBC))
    return SDValue();

  SDValue AddcSubcOp0 = AddcSubcNode->getOperand(0);
  SDValue AddcSubcOp1 = AddcSubcNode->getOperand(1);

  // Check if the two operands are from the same mul_lohi node.
  if (AddcSubcOp0.getNode() == AddcSubcOp1.getNode())
    return SDValue();

  assert(AddcSubcNode->getNumValues() == 2 &&
         AddcSubcNode->getValueType(0) == MVT::i32 &&
         "Expect ADDC with two result values. First: i32");

  // Check that the ADDC adds the low result of the S/UMUL_LOHI. If not, it
  // maybe a SMLAL which multiplies two 16-bit values.
  if (AddeSubeNode->getOpcode() == ARMISD::ADDE &&
      AddcSubcOp0->getOpcode() != ISD::UMUL_LOHI &&
      AddcSubcOp0->getOpcode() != ISD::SMUL_LOHI &&
      AddcSubcOp1->getOpcode() != ISD::UMUL_LOHI &&
      AddcSubcOp1->getOpcode() != ISD::SMUL_LOHI)
    return AddCombineTo64BitSMLAL16(AddcSubcNode, AddeSubeNode, DCI, Subtarget);

  // Check for the triangle shape.
  SDValue AddeSubeOp0 = AddeSubeNode->getOperand(0);
  SDValue AddeSubeOp1 = AddeSubeNode->getOperand(1);

  // Make sure that the ADDE/SUBE operands are not coming from the same node.
  if (AddeSubeOp0.getNode() == AddeSubeOp1.getNode())
    return SDValue();

  // Find the MUL_LOHI node walking up ADDE/SUBE's operands.
  bool IsLeftOperandMUL = false;
  SDValue MULOp = findMUL_LOHI(AddeSubeOp0);
  if (MULOp == SDValue())
    MULOp = findMUL_LOHI(AddeSubeOp1);
  else
    IsLeftOperandMUL = true;
  if (MULOp == SDValue())
    return SDValue();

  // Figure out the right opcode.
  unsigned Opc = MULOp->getOpcode();
  unsigned FinalOpc = (Opc == ISD::SMUL_LOHI) ? ARMISD::SMLAL : ARMISD::UMLAL;

  // Figure out the high and low input values to the MLAL node.
  SDValue *HiAddSub = nullptr;
  SDValue *LoMul = nullptr;
  SDValue *LowAddSub = nullptr;

  // Ensure that ADDE/SUBE is from high result of ISD::xMUL_LOHI.
  if ((AddeSubeOp0 != MULOp.getValue(1)) && (AddeSubeOp1 != MULOp.getValue(1)))
    return SDValue();

  if (IsLeftOperandMUL)
    HiAddSub = &AddeSubeOp1;
  else
    HiAddSub = &AddeSubeOp0;

  // Ensure that LoMul and LowAddSub are taken from correct ISD::SMUL_LOHI node
  // whose low result is fed to the ADDC/SUBC we are checking.

  if (AddcSubcOp0 == MULOp.getValue(0)) {
    LoMul = &AddcSubcOp0;
    LowAddSub = &AddcSubcOp1;
  }
  if (AddcSubcOp1 == MULOp.getValue(0)) {
    LoMul = &AddcSubcOp1;
    LowAddSub = &AddcSubcOp0;
  }

  if (!LoMul)
    return SDValue();

  // If HiAddSub is the same node as ADDC/SUBC or is a predecessor of ADDC/SUBC
  // the replacement below will create a cycle.
  if (AddcSubcNode == HiAddSub->getNode() ||
      AddcSubcNode->isPredecessorOf(HiAddSub->getNode()))
    return SDValue();

  // Create the merged node.
  SelectionDAG &DAG = DCI.DAG;

  // Start building operand list.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(LoMul->getOperand(0));
  Ops.push_back(LoMul->getOperand(1));

  // Check whether we can use SMMLAR, SMMLSR or SMMULR instead.  For this to be
  // the case, we must be doing signed multiplication and only use the higher
  // part of the result of the MLAL, furthermore the LowAddSub must be a constant
  // addition or subtraction with the value of 0x800000.
  if (Subtarget->hasV6Ops() && Subtarget->hasDSP() && Subtarget->useMulOps() &&
      FinalOpc == ARMISD::SMLAL && !AddeSubeNode->hasAnyUseOfValue(1) &&
      LowAddSub->getNode()->getOpcode() == ISD::Constant &&
      static_cast<ConstantSDNode *>(LowAddSub->getNode())->getZExtValue() ==
          0x80000000) {
    Ops.push_back(*HiAddSub);
    if (AddcSubcNode->getOpcode() == ARMISD::SUBC) {
      FinalOpc = ARMISD::SMMLSR;
    } else {
      FinalOpc = ARMISD::SMMLAR;
    }
    SDValue NewNode = DAG.getNode(FinalOpc, SDLoc(AddcSubcNode), MVT::i32, Ops);
    DAG.ReplaceAllUsesOfValueWith(SDValue(AddeSubeNode, 0), NewNode);

    return SDValue(AddeSubeNode, 0);
  } else if (AddcSubcNode->getOpcode() == ARMISD::SUBC)
    // SMMLS is generated during instruction selection and the rest of this
    // function can not handle the case where AddcSubcNode is a SUBC.
    return SDValue();

  // Finish building the operand list for {U/S}MLAL
  Ops.push_back(*LowAddSub);
  Ops.push_back(*HiAddSub);

  SDValue MLALNode = DAG.getNode(FinalOpc, SDLoc(AddcSubcNode),
                                 DAG.getVTList(MVT::i32, MVT::i32), Ops);

  // Replace the ADDs' nodes uses by the MLA node's values.
  SDValue HiMLALResult(MLALNode.getNode(), 1);
  DAG.ReplaceAllUsesOfValueWith(SDValue(AddeSubeNode, 0), HiMLALResult);

  SDValue LoMLALResult(MLALNode.getNode(), 0);
  DAG.ReplaceAllUsesOfValueWith(SDValue(AddcSubcNode, 0), LoMLALResult);

  // Return original node to notify the driver to stop replacing.
  return SDValue(AddeSubeNode, 0);
}

static SDValue AddCombineTo64bitUMAAL(SDNode *AddeNode,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  // UMAAL is similar to UMLAL except that it adds two unsigned values.
  // While trying to combine for the other MLAL nodes, first search for the
  // chance to use UMAAL. Check if Addc uses a node which has already
  // been combined into a UMLAL. The other pattern is UMLAL using Addc/Adde
  // as the addend, and it's handled in PerformUMLALCombine.

  if (!Subtarget->hasV6Ops() || !Subtarget->hasDSP())
    return AddCombineTo64bitMLAL(AddeNode, DCI, Subtarget);

  // Check that we have a glued ADDC node.
  SDNode* AddcNode = AddeNode->getOperand(2).getNode();
  if (AddcNode->getOpcode() != ARMISD::ADDC)
    return SDValue();

  // Find the converted UMAAL or quit if it doesn't exist.
  SDNode *UmlalNode = nullptr;
  SDValue AddHi;
  if (AddcNode->getOperand(0).getOpcode() == ARMISD::UMLAL) {
    UmlalNode = AddcNode->getOperand(0).getNode();
    AddHi = AddcNode->getOperand(1);
  } else if (AddcNode->getOperand(1).getOpcode() == ARMISD::UMLAL) {
    UmlalNode = AddcNode->getOperand(1).getNode();
    AddHi = AddcNode->getOperand(0);
  } else {
    return AddCombineTo64bitMLAL(AddeNode, DCI, Subtarget);
  }

  // The ADDC should be glued to an ADDE node, which uses the same UMLAL as
  // the ADDC as well as Zero.
  if (!isNullConstant(UmlalNode->getOperand(3)))
    return SDValue();

  if ((isNullConstant(AddeNode->getOperand(0)) &&
       AddeNode->getOperand(1).getNode() == UmlalNode) ||
      (AddeNode->getOperand(0).getNode() == UmlalNode &&
       isNullConstant(AddeNode->getOperand(1)))) {
    SelectionDAG &DAG = DCI.DAG;
    SDValue Ops[] = { UmlalNode->getOperand(0), UmlalNode->getOperand(1),
                      UmlalNode->getOperand(2), AddHi };
    SDValue UMAAL =  DAG.getNode(ARMISD::UMAAL, SDLoc(AddcNode),
                                 DAG.getVTList(MVT::i32, MVT::i32), Ops);

    // Replace the ADDs' nodes uses by the UMAAL node's values.
    DAG.ReplaceAllUsesOfValueWith(SDValue(AddeNode, 0), SDValue(UMAAL.getNode(), 1));
    DAG.ReplaceAllUsesOfValueWith(SDValue(AddcNode, 0), SDValue(UMAAL.getNode(), 0));

    // Return original node to notify the driver to stop replacing.
    return SDValue(AddeNode, 0);
  }
  return SDValue();
}

static SDValue PerformUMLALCombine(SDNode *N, SelectionDAG &DAG,
                                   const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasV6Ops() || !Subtarget->hasDSP())
    return SDValue();

  // Check that we have a pair of ADDC and ADDE as operands.
  // Both addends of the ADDE must be zero.
  SDNode* AddcNode = N->getOperand(2).getNode();
  SDNode* AddeNode = N->getOperand(3).getNode();
  if ((AddcNode->getOpcode() == ARMISD::ADDC) &&
      (AddeNode->getOpcode() == ARMISD::ADDE) &&
      isNullConstant(AddeNode->getOperand(0)) &&
      isNullConstant(AddeNode->getOperand(1)) &&
      (AddeNode->getOperand(2).getNode() == AddcNode))
    return DAG.getNode(ARMISD::UMAAL, SDLoc(N),
                       DAG.getVTList(MVT::i32, MVT::i32),
                       {N->getOperand(0), N->getOperand(1),
                        AddcNode->getOperand(0), AddcNode->getOperand(1)});
  else
    return SDValue();
}

static SDValue PerformAddcSubcCombine(SDNode *N,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  SelectionDAG &DAG(DCI.DAG);

  if (N->getOpcode() == ARMISD::SUBC) {
    // (SUBC (ADDE 0, 0, C), 1) -> C
    SDValue LHS = N->getOperand(0);
    SDValue RHS = N->getOperand(1);
    if (LHS->getOpcode() == ARMISD::ADDE &&
        isNullConstant(LHS->getOperand(0)) &&
        isNullConstant(LHS->getOperand(1)) && isOneConstant(RHS)) {
      return DCI.CombineTo(N, SDValue(N, 0), LHS->getOperand(2));
    }
  }

  if (Subtarget->isThumb1Only()) {
    SDValue RHS = N->getOperand(1);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS)) {
      int32_t imm = C->getSExtValue();
      if (imm < 0 && imm > std::numeric_limits<int>::min()) {
        SDLoc DL(N);
        RHS = DAG.getConstant(-imm, DL, MVT::i32);
        unsigned Opcode = (N->getOpcode() == ARMISD::ADDC) ? ARMISD::SUBC
                                                           : ARMISD::ADDC;
        return DAG.getNode(Opcode, DL, N->getVTList(), N->getOperand(0), RHS);
      }
    }
  }

  return SDValue();
}

static SDValue PerformAddeSubeCombine(SDNode *N,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  if (Subtarget->isThumb1Only()) {
    SelectionDAG &DAG = DCI.DAG;
    SDValue RHS = N->getOperand(1);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS)) {
      int64_t imm = C->getSExtValue();
      if (imm < 0) {
        SDLoc DL(N);

        // The with-carry-in form matches bitwise not instead of the negation.
        // Effectively, the inverse interpretation of the carry flag already
        // accounts for part of the negation.
        RHS = DAG.getConstant(~imm, DL, MVT::i32);

        unsigned Opcode = (N->getOpcode() == ARMISD::ADDE) ? ARMISD::SUBE
                                                           : ARMISD::ADDE;
        return DAG.getNode(Opcode, DL, N->getVTList(),
                           N->getOperand(0), RHS, N->getOperand(2));
      }
    }
  } else if (N->getOperand(1)->getOpcode() == ISD::SMUL_LOHI) {
    return AddCombineTo64bitMLAL(N, DCI, Subtarget);
  }
  return SDValue();
}

/// PerformADDECombine - Target-specific dag combine transform from
/// ARMISD::ADDC, ARMISD::ADDE, and ISD::MUL_LOHI to MLAL or
/// ARMISD::ADDC, ARMISD::ADDE and ARMISD::UMLAL to ARMISD::UMAAL
static SDValue PerformADDECombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI,
                                  const ARMSubtarget *Subtarget) {
  // Only ARM and Thumb2 support UMLAL/SMLAL.
  if (Subtarget->isThumb1Only())
    return PerformAddeSubeCombine(N, DCI, Subtarget);

  // Only perform the checks after legalize when the pattern is available.
  if (DCI.isBeforeLegalize()) return SDValue();

  return AddCombineTo64bitUMAAL(N, DCI, Subtarget);
}

/// PerformADDCombineWithOperands - Try DAG combinations for an ADD with
/// operands N0 and N1.  This is a helper for PerformADDCombine that is
/// called with the default operands, and if that fails, with commuted
/// operands.
static SDValue PerformADDCombineWithOperands(SDNode *N, SDValue N0, SDValue N1,
                                          TargetLowering::DAGCombinerInfo &DCI,
                                          const ARMSubtarget *Subtarget){
  // Attempt to create vpadd for this add.
  if (SDValue Result = AddCombineToVPADD(N, N0, N1, DCI, Subtarget))
    return Result;

  // Attempt to create vpaddl for this add.
  if (SDValue Result = AddCombineVUZPToVPADDL(N, N0, N1, DCI, Subtarget))
    return Result;
  if (SDValue Result = AddCombineBUILD_VECTORToVPADDL(N, N0, N1, DCI,
                                                      Subtarget))
    return Result;

  // fold (add (select cc, 0, c), x) -> (select cc, x, (add, x, c))
  if (N0.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N0, N1, DCI))
      return Result;
  return SDValue();
}

bool
ARMTargetLowering::isDesirableToCommuteWithShift(const SDNode *N,
                                                 CombineLevel Level) const {
  if (Level == BeforeLegalizeTypes)
    return true;

  if (Subtarget->isThumb() && Subtarget->isThumb1Only())
    return true;

  if (N->getOpcode() != ISD::SHL)
    return true;

  // Turn off commute-with-shift transform after legalization, so it doesn't
  // conflict with PerformSHLSimplify.  (We could try to detect when
  // PerformSHLSimplify would trigger more precisely, but it isn't
  // really necessary.)
  return false;
}

bool
ARMTargetLowering::shouldFoldShiftPairToMask(const SDNode *N,
                                             CombineLevel Level) const {
  if (!Subtarget->isThumb1Only())
    return true;

  if (Level == BeforeLegalizeTypes)
    return true;

  return false;
}

static SDValue PerformSHLSimplify(SDNode *N,
                                TargetLowering::DAGCombinerInfo &DCI,
                                const ARMSubtarget *ST) {
  // Allow the generic combiner to identify potential bswaps.
  if (DCI.isBeforeLegalize())
    return SDValue();

  // DAG combiner will fold:
  // (shl (add x, c1), c2) -> (add (shl x, c2), c1 << c2)
  // (shl (or x, c1), c2) -> (or (shl x, c2), c1 << c2
  // Other code patterns that can be also be modified have the following form:
  // b + ((a << 1) | 510)
  // b + ((a << 1) & 510)
  // b + ((a << 1) ^ 510)
  // b + ((a << 1) + 510)

  // Many instructions can  perform the shift for free, but it requires both
  // the operands to be registers. If c1 << c2 is too large, a mov immediate
  // instruction will needed. So, unfold back to the original pattern if:
  // - if c1 and c2 are small enough that they don't require mov imms.
  // - the user(s) of the node can perform an shl

  // No shifted operands for 16-bit instructions.
  if (ST->isThumb() && ST->isThumb1Only())
    return SDValue();

  // Check that all the users could perform the shl themselves.
  for (auto U : N->uses()) {
    switch(U->getOpcode()) {
    default:
      return SDValue();
    case ISD::SUB:
    case ISD::ADD:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SETCC:
    case ARMISD::CMP:
      // Check that the user isn't already using a constant because there
      // aren't any instructions that support an immediate operand and a
      // shifted operand.
      if (isa<ConstantSDNode>(U->getOperand(0)) ||
          isa<ConstantSDNode>(U->getOperand(1)))
        return SDValue();

      // Check that it's not already using a shift.
      if (U->getOperand(0).getOpcode() == ISD::SHL ||
          U->getOperand(1).getOpcode() == ISD::SHL)
        return SDValue();
      break;
    }
  }

  if (N->getOpcode() != ISD::ADD && N->getOpcode() != ISD::OR &&
      N->getOpcode() != ISD::XOR && N->getOpcode() != ISD::AND)
    return SDValue();

  if (N->getOperand(0).getOpcode() != ISD::SHL)
    return SDValue();

  SDValue SHL = N->getOperand(0);

  auto *C1ShlC2 = dyn_cast<ConstantSDNode>(N->getOperand(1));
  auto *C2 = dyn_cast<ConstantSDNode>(SHL.getOperand(1));
  if (!C1ShlC2 || !C2)
    return SDValue();

  APInt C2Int = C2->getAPIntValue();
  APInt C1Int = C1ShlC2->getAPIntValue();

  // Check that performing a lshr will not lose any information.
  APInt Mask = APInt::getHighBitsSet(C2Int.getBitWidth(),
                                     C2Int.getBitWidth() - C2->getZExtValue());
  if ((C1Int & Mask) != C1Int)
    return SDValue();

  // Shift the first constant.
  C1Int.lshrInPlace(C2Int);

  // The immediates are encoded as an 8-bit value that can be rotated.
  auto LargeImm = [](const APInt &Imm) {
    unsigned Zeros = Imm.countLeadingZeros() + Imm.countTrailingZeros();
    return Imm.getBitWidth() - Zeros > 8;
  };

  if (LargeImm(C1Int) || LargeImm(C2Int))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue X = SHL.getOperand(0);
  SDValue BinOp = DAG.getNode(N->getOpcode(), dl, MVT::i32, X,
                              DAG.getConstant(C1Int, dl, MVT::i32));
  // Shift left to compensate for the lshr of C1Int.
  SDValue Res = DAG.getNode(ISD::SHL, dl, MVT::i32, BinOp, SHL.getOperand(1));

  LLVM_DEBUG(dbgs() << "Simplify shl use:\n"; SHL.getOperand(0).dump();
             SHL.dump(); N->dump());
  LLVM_DEBUG(dbgs() << "Into:\n"; X.dump(); BinOp.dump(); Res.dump());
  return Res;
}


/// PerformADDCombine - Target-specific dag combine xforms for ISD::ADD.
///
static SDValue PerformADDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Only works one way, because it needs an immediate operand.
  if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
    return Result;

  // First try with the default operand order.
  if (SDValue Result = PerformADDCombineWithOperands(N, N0, N1, DCI, Subtarget))
    return Result;

  // If that didn't work, try again with the operands commuted.
  return PerformADDCombineWithOperands(N, N1, N0, DCI, Subtarget);
}

/// PerformSUBCombine - Target-specific dag combine xforms for ISD::SUB.
///
static SDValue PerformSUBCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // fold (sub x, (select cc, 0, c)) -> (select cc, x, (sub, x, c))
  if (N1.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N1, N0, DCI))
      return Result;

  return SDValue();
}

/// PerformVMULCombine
/// Distribute (A + B) * C to (A * C) + (B * C) to take advantage of the
/// special multiplier accumulator forwarding.
///   vmul d3, d0, d2
///   vmla d3, d1, d2
/// is faster than
///   vadd d3, d0, d1
///   vmul d3, d3, d2
//  However, for (A + B) * (A + B),
//    vadd d2, d0, d1
//    vmul d3, d0, d2
//    vmla d3, d1, d2
//  is slower than
//    vadd d2, d0, d1
//    vmul d3, d2, d2
static SDValue PerformVMULCombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI,
                                  const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasVMLxForwarding())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  unsigned Opcode = N0.getOpcode();
  if (Opcode != ISD::ADD && Opcode != ISD::SUB &&
      Opcode != ISD::FADD && Opcode != ISD::FSUB) {
    Opcode = N1.getOpcode();
    if (Opcode != ISD::ADD && Opcode != ISD::SUB &&
        Opcode != ISD::FADD && Opcode != ISD::FSUB)
      return SDValue();
    std::swap(N0, N1);
  }

  if (N0 == N1)
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  SDValue N00 = N0->getOperand(0);
  SDValue N01 = N0->getOperand(1);
  return DAG.getNode(Opcode, DL, VT,
                     DAG.getNode(ISD::MUL, DL, VT, N00, N1),
                     DAG.getNode(ISD::MUL, DL, VT, N01, N1));
}

static SDValue PerformMULCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  SelectionDAG &DAG = DCI.DAG;

  if (Subtarget->isThumb1Only())
    return SDValue();

  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  EVT VT = N->getValueType(0);
  if (VT.is64BitVector() || VT.is128BitVector())
    return PerformVMULCombine(N, DCI, Subtarget);
  if (VT != MVT::i32)
    return SDValue();

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!C)
    return SDValue();

  int64_t MulAmt = C->getSExtValue();
  unsigned ShiftAmt = countTrailingZeros<uint64_t>(MulAmt);

  ShiftAmt = ShiftAmt & (32 - 1);
  SDValue V = N->getOperand(0);
  SDLoc DL(N);

  SDValue Res;
  MulAmt >>= ShiftAmt;

  if (MulAmt >= 0) {
    if (isPowerOf2_32(MulAmt - 1)) {
      // (mul x, 2^N + 1) => (add (shl x, N), x)
      Res = DAG.getNode(ISD::ADD, DL, VT,
                        V,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmt - 1), DL,
                                                    MVT::i32)));
    } else if (isPowerOf2_32(MulAmt + 1)) {
      // (mul x, 2^N - 1) => (sub (shl x, N), x)
      Res = DAG.getNode(ISD::SUB, DL, VT,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmt + 1), DL,
                                                    MVT::i32)),
                        V);
    } else
      return SDValue();
  } else {
    uint64_t MulAmtAbs = -MulAmt;
    if (isPowerOf2_32(MulAmtAbs + 1)) {
      // (mul x, -(2^N - 1)) => (sub x, (shl x, N))
      Res = DAG.getNode(ISD::SUB, DL, VT,
                        V,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmtAbs + 1), DL,
                                                    MVT::i32)));
    } else if (isPowerOf2_32(MulAmtAbs - 1)) {
      // (mul x, -(2^N + 1)) => - (add (shl x, N), x)
      Res = DAG.getNode(ISD::ADD, DL, VT,
                        V,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmtAbs - 1), DL,
                                                    MVT::i32)));
      Res = DAG.getNode(ISD::SUB, DL, VT,
                        DAG.getConstant(0, DL, MVT::i32), Res);
    } else
      return SDValue();
  }

  if (ShiftAmt != 0)
    Res = DAG.getNode(ISD::SHL, DL, VT,
                      Res, DAG.getConstant(ShiftAmt, DL, MVT::i32));

  // Do not add new nodes to DAG combiner worklist.
  DCI.CombineTo(N, Res, false);
  return SDValue();
}

static SDValue CombineANDShift(SDNode *N,
                               TargetLowering::DAGCombinerInfo &DCI,
                               const ARMSubtarget *Subtarget) {
  // Allow DAGCombine to pattern-match before we touch the canonical form.
  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  if (N->getValueType(0) != MVT::i32)
    return SDValue();

  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!N1C)
    return SDValue();

  uint32_t C1 = (uint32_t)N1C->getZExtValue();
  // Don't transform uxtb/uxth.
  if (C1 == 255 || C1 == 65535)
    return SDValue();

  SDNode *N0 = N->getOperand(0).getNode();
  if (!N0->hasOneUse())
    return SDValue();

  if (N0->getOpcode() != ISD::SHL && N0->getOpcode() != ISD::SRL)
    return SDValue();

  bool LeftShift = N0->getOpcode() == ISD::SHL;

  ConstantSDNode *N01C = dyn_cast<ConstantSDNode>(N0->getOperand(1));
  if (!N01C)
    return SDValue();

  uint32_t C2 = (uint32_t)N01C->getZExtValue();
  if (!C2 || C2 >= 32)
    return SDValue();

  // Clear irrelevant bits in the mask.
  if (LeftShift)
    C1 &= (-1U << C2);
  else
    C1 &= (-1U >> C2);

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // We have a pattern of the form "(and (shl x, c2) c1)" or
  // "(and (srl x, c2) c1)", where c1 is a shifted mask. Try to
  // transform to a pair of shifts, to save materializing c1.

  // First pattern: right shift, then mask off leading bits.
  // FIXME: Use demanded bits?
  if (!LeftShift && isMask_32(C1)) {
    uint32_t C3 = countLeadingZeros(C1);
    if (C2 < C3) {
      SDValue SHL = DAG.getNode(ISD::SHL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C3 - C2, DL, MVT::i32));
      return DAG.getNode(ISD::SRL, DL, MVT::i32, SHL,
                         DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // First pattern, reversed: left shift, then mask off trailing bits.
  if (LeftShift && isMask_32(~C1)) {
    uint32_t C3 = countTrailingZeros(C1);
    if (C2 < C3) {
      SDValue SHL = DAG.getNode(ISD::SRL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C3 - C2, DL, MVT::i32));
      return DAG.getNode(ISD::SHL, DL, MVT::i32, SHL,
                         DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // Second pattern: left shift, then mask off leading bits.
  // FIXME: Use demanded bits?
  if (LeftShift && isShiftedMask_32(C1)) {
    uint32_t Trailing = countTrailingZeros(C1);
    uint32_t C3 = countLeadingZeros(C1);
    if (Trailing == C2 && C2 + C3 < 32) {
      SDValue SHL = DAG.getNode(ISD::SHL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C2 + C3, DL, MVT::i32));
      return DAG.getNode(ISD::SRL, DL, MVT::i32, SHL,
                        DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // Second pattern, reversed: right shift, then mask off trailing bits.
  // FIXME: Handle other patterns of known/demanded bits.
  if (!LeftShift && isShiftedMask_32(C1)) {
    uint32_t Leading = countLeadingZeros(C1);
    uint32_t C3 = countTrailingZeros(C1);
    if (Leading == C2 && C2 + C3 < 32) {
      SDValue SHL = DAG.getNode(ISD::SRL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C2 + C3, DL, MVT::i32));
      return DAG.getNode(ISD::SHL, DL, MVT::i32, SHL,
                         DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // FIXME: Transform "(and (shl x, c2) c1)" ->
  // "(shl (and x, c1>>c2), c2)" if "c1 >> c2" is a cheaper immediate than
  // c1.
  return SDValue();
}

static SDValue PerformANDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  // Attempt to use immediate-form VBIC
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(N->getOperand(1));
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;

  if(!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (BVN &&
      BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs)) {
    if (SplatBitSize <= 64) {
      EVT VbicVT;
      SDValue Val = isNEONModifiedImm((~SplatBits).getZExtValue(),
                                      SplatUndef.getZExtValue(), SplatBitSize,
                                      DAG, dl, VbicVT, VT.is128BitVector(),
                                      OtherModImm);
      if (Val.getNode()) {
        SDValue Input =
          DAG.getNode(ISD::BITCAST, dl, VbicVT, N->getOperand(0));
        SDValue Vbic = DAG.getNode(ARMISD::VBICIMM, dl, VbicVT, Input, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vbic);
      }
    }
  }

  if (!Subtarget->isThumb1Only()) {
    // fold (and (select cc, -1, c), x) -> (select cc, x, (and, x, c))
    if (SDValue Result = combineSelectAndUseCommutative(N, true, DCI))
      return Result;

    if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
      return Result;
  }

  if (Subtarget->isThumb1Only())
    if (SDValue Result = CombineANDShift(N, DCI, Subtarget))
      return Result;

  return SDValue();
}

// Try combining OR nodes to SMULWB, SMULWT.
static SDValue PerformORCombineToSMULWBT(SDNode *OR,
                                         TargetLowering::DAGCombinerInfo &DCI,
                                         const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasV6Ops() ||
      (Subtarget->isThumb() &&
       (!Subtarget->hasThumb2() || !Subtarget->hasDSP())))
    return SDValue();

  SDValue SRL = OR->getOperand(0);
  SDValue SHL = OR->getOperand(1);

  if (SRL.getOpcode() != ISD::SRL || SHL.getOpcode() != ISD::SHL) {
    SRL = OR->getOperand(1);
    SHL = OR->getOperand(0);
  }
  if (!isSRL16(SRL) || !isSHL16(SHL))
    return SDValue();

  // The first operands to the shifts need to be the two results from the
  // same smul_lohi node.
  if ((SRL.getOperand(0).getNode() != SHL.getOperand(0).getNode()) ||
       SRL.getOperand(0).getOpcode() != ISD::SMUL_LOHI)
    return SDValue();

  SDNode *SMULLOHI = SRL.getOperand(0).getNode();
  if (SRL.getOperand(0) != SDValue(SMULLOHI, 0) ||
      SHL.getOperand(0) != SDValue(SMULLOHI, 1))
    return SDValue();

  // Now we have:
  // (or (srl (smul_lohi ?, ?), 16), (shl (smul_lohi ?, ?), 16)))
  // For SMUL[B|T] smul_lohi will take a 32-bit and a 16-bit arguments.
  // For SMUWB the 16-bit value will signed extended somehow.
  // For SMULWT only the SRA is required.
  // Check both sides of SMUL_LOHI
  SDValue OpS16 = SMULLOHI->getOperand(0);
  SDValue OpS32 = SMULLOHI->getOperand(1);

  SelectionDAG &DAG = DCI.DAG;
  if (!isS16(OpS16, DAG) && !isSRA16(OpS16)) {
    OpS16 = OpS32;
    OpS32 = SMULLOHI->getOperand(0);
  }

  SDLoc dl(OR);
  unsigned Opcode = 0;
  if (isS16(OpS16, DAG))
    Opcode = ARMISD::SMULWB;
  else if (isSRA16(OpS16)) {
    Opcode = ARMISD::SMULWT;
    OpS16 = OpS16->getOperand(0);
  }
  else
    return SDValue();

  SDValue Res = DAG.getNode(Opcode, dl, MVT::i32, OpS32, OpS16);
  DAG.ReplaceAllUsesOfValueWith(SDValue(OR, 0), Res);
  return SDValue(OR, 0);
}

static SDValue PerformORCombineToBFI(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *Subtarget) {
  // BFI is only available on V6T2+
  if (Subtarget->isThumb1Only() || !Subtarget->hasV6T2Ops())
    return SDValue();

  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  // 1) or (and A, mask), val => ARMbfi A, val, mask
  //      iff (val & mask) == val
  //
  // 2) or (and A, mask), (and B, mask2) => ARMbfi A, (lsr B, amt), mask
  //  2a) iff isBitFieldInvertedMask(mask) && isBitFieldInvertedMask(~mask2)
  //          && mask == ~mask2
  //  2b) iff isBitFieldInvertedMask(~mask) && isBitFieldInvertedMask(mask2)
  //          && ~mask == mask2
  //  (i.e., copy a bitfield value into another bitfield of the same width)

  if (VT != MVT::i32)
    return SDValue();

  SDValue N00 = N0.getOperand(0);

  // The value and the mask need to be constants so we can verify this is
  // actually a bitfield set. If the mask is 0xffff, we can do better
  // via a movt instruction, so don't use BFI in that case.
  SDValue MaskOp = N0.getOperand(1);
  ConstantSDNode *MaskC = dyn_cast<ConstantSDNode>(MaskOp);
  if (!MaskC)
    return SDValue();
  unsigned Mask = MaskC->getZExtValue();
  if (Mask == 0xffff)
    return SDValue();
  SDValue Res;
  // Case (1): or (and A, mask), val => ARMbfi A, val, mask
  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
  if (N1C) {
    unsigned Val = N1C->getZExtValue();
    if ((Val & ~Mask) != Val)
      return SDValue();

    if (ARM::isBitFieldInvertedMask(Mask)) {
      Val >>= countTrailingZeros(~Mask);

      Res = DAG.getNode(ARMISD::BFI, DL, VT, N00,
                        DAG.getConstant(Val, DL, MVT::i32),
                        DAG.getConstant(Mask, DL, MVT::i32));

      DCI.CombineTo(N, Res, false);
      // Return value from the original node to inform the combiner than N is
      // now dead.
      return SDValue(N, 0);
    }
  } else if (N1.getOpcode() == ISD::AND) {
    // case (2) or (and A, mask), (and B, mask2) => ARMbfi A, (lsr B, amt), mask
    ConstantSDNode *N11C = dyn_cast<ConstantSDNode>(N1.getOperand(1));
    if (!N11C)
      return SDValue();
    unsigned Mask2 = N11C->getZExtValue();

    // Mask and ~Mask2 (or reverse) must be equivalent for the BFI pattern
    // as is to match.
    if (ARM::isBitFieldInvertedMask(Mask) &&
        (Mask == ~Mask2)) {
      // The pack halfword instruction works better for masks that fit it,
      // so use that when it's available.
      if (Subtarget->hasDSP() &&
          (Mask == 0xffff || Mask == 0xffff0000))
        return SDValue();
      // 2a
      unsigned amt = countTrailingZeros(Mask2);
      Res = DAG.getNode(ISD::SRL, DL, VT, N1.getOperand(0),
                        DAG.getConstant(amt, DL, MVT::i32));
      Res = DAG.getNode(ARMISD::BFI, DL, VT, N00, Res,
                        DAG.getConstant(Mask, DL, MVT::i32));
      DCI.CombineTo(N, Res, false);
      // Return value from the original node to inform the combiner than N is
      // now dead.
      return SDValue(N, 0);
    } else if (ARM::isBitFieldInvertedMask(~Mask) &&
               (~Mask == Mask2)) {
      // The pack halfword instruction works better for masks that fit it,
      // so use that when it's available.
      if (Subtarget->hasDSP() &&
          (Mask2 == 0xffff || Mask2 == 0xffff0000))
        return SDValue();
      // 2b
      unsigned lsb = countTrailingZeros(Mask);
      Res = DAG.getNode(ISD::SRL, DL, VT, N00,
                        DAG.getConstant(lsb, DL, MVT::i32));
      Res = DAG.getNode(ARMISD::BFI, DL, VT, N1.getOperand(0), Res,
                        DAG.getConstant(Mask2, DL, MVT::i32));
      DCI.CombineTo(N, Res, false);
      // Return value from the original node to inform the combiner than N is
      // now dead.
      return SDValue(N, 0);
    }
  }

  if (DAG.MaskedValueIsZero(N1, MaskC->getAPIntValue()) &&
      N00.getOpcode() == ISD::SHL && isa<ConstantSDNode>(N00.getOperand(1)) &&
      ARM::isBitFieldInvertedMask(~Mask)) {
    // Case (3): or (and (shl A, #shamt), mask), B => ARMbfi B, A, ~mask
    // where lsb(mask) == #shamt and masked bits of B are known zero.
    SDValue ShAmt = N00.getOperand(1);
    unsigned ShAmtC = cast<ConstantSDNode>(ShAmt)->getZExtValue();
    unsigned LSB = countTrailingZeros(Mask);
    if (ShAmtC != LSB)
      return SDValue();

    Res = DAG.getNode(ARMISD::BFI, DL, VT, N1, N00.getOperand(0),
                      DAG.getConstant(~Mask, DL, MVT::i32));

    DCI.CombineTo(N, Res, false);
    // Return value from the original node to inform the combiner than N is
    // now dead.
    return SDValue(N, 0);
  }

  return SDValue();
}

/// PerformORCombine - Target-specific dag combine xforms for ISD::OR
static SDValue PerformORCombine(SDNode *N,
                                TargetLowering::DAGCombinerInfo &DCI,
                                const ARMSubtarget *Subtarget) {
  // Attempt to use immediate-form VORR
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(N->getOperand(1));
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;

  if(!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (BVN && Subtarget->hasNEON() &&
      BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs)) {
    if (SplatBitSize <= 64) {
      EVT VorrVT;
      SDValue Val = isNEONModifiedImm(SplatBits.getZExtValue(),
                                      SplatUndef.getZExtValue(), SplatBitSize,
                                      DAG, dl, VorrVT, VT.is128BitVector(),
                                      OtherModImm);
      if (Val.getNode()) {
        SDValue Input =
          DAG.getNode(ISD::BITCAST, dl, VorrVT, N->getOperand(0));
        SDValue Vorr = DAG.getNode(ARMISD::VORRIMM, dl, VorrVT, Input, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vorr);
      }
    }
  }

  if (!Subtarget->isThumb1Only()) {
    // fold (or (select cc, 0, c), x) -> (select cc, x, (or, x, c))
    if (SDValue Result = combineSelectAndUseCommutative(N, false, DCI))
      return Result;
    if (SDValue Result = PerformORCombineToSMULWBT(N, DCI, Subtarget))
      return Result;
  }

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // (or (and B, A), (and C, ~A)) => (VBSL A, B, C) when A is a constant.
  if (Subtarget->hasNEON() && N1.getOpcode() == ISD::AND && VT.isVector() &&
      DAG.getTargetLoweringInfo().isTypeLegal(VT)) {

    // The code below optimizes (or (and X, Y), Z).
    // The AND operand needs to have a single user to make these optimizations
    // profitable.
    if (N0.getOpcode() != ISD::AND || !N0.hasOneUse())
      return SDValue();

    APInt SplatUndef;
    unsigned SplatBitSize;
    bool HasAnyUndefs;

    APInt SplatBits0, SplatBits1;
    BuildVectorSDNode *BVN0 = dyn_cast<BuildVectorSDNode>(N0->getOperand(1));
    BuildVectorSDNode *BVN1 = dyn_cast<BuildVectorSDNode>(N1->getOperand(1));
    // Ensure that the second operand of both ands are constants
    if (BVN0 && BVN0->isConstantSplat(SplatBits0, SplatUndef, SplatBitSize,
                                      HasAnyUndefs) && !HasAnyUndefs) {
        if (BVN1 && BVN1->isConstantSplat(SplatBits1, SplatUndef, SplatBitSize,
                                          HasAnyUndefs) && !HasAnyUndefs) {
            // Ensure that the bit width of the constants are the same and that
            // the splat arguments are logical inverses as per the pattern we
            // are trying to simplify.
            if (SplatBits0.getBitWidth() == SplatBits1.getBitWidth() &&
                SplatBits0 == ~SplatBits1) {
                // Canonicalize the vector type to make instruction selection
                // simpler.
                EVT CanonicalVT = VT.is128BitVector() ? MVT::v4i32 : MVT::v2i32;
                SDValue Result = DAG.getNode(ARMISD::VBSL, dl, CanonicalVT,
                                             N0->getOperand(1),
                                             N0->getOperand(0),
                                             N1->getOperand(0));
                return DAG.getNode(ISD::BITCAST, dl, VT, Result);
            }
        }
    }
  }

  // Try to use the ARM/Thumb2 BFI (bitfield insert) instruction when
  // reasonable.
  if (N0.getOpcode() == ISD::AND && N0.hasOneUse()) {
    if (SDValue Res = PerformORCombineToBFI(N, DCI, Subtarget))
      return Res;
  }

  if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
    return Result;

  return SDValue();
}

static SDValue PerformXORCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;

  if(!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  if (!Subtarget->isThumb1Only()) {
    // fold (xor (select cc, 0, c), x) -> (select cc, x, (xor, x, c))
    if (SDValue Result = combineSelectAndUseCommutative(N, false, DCI))
      return Result;

    if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
      return Result;
  }

  return SDValue();
}

// ParseBFI - given a BFI instruction in N, extract the "from" value (Rn) and return it,
// and fill in FromMask and ToMask with (consecutive) bits in "from" to be extracted and
// their position in "to" (Rd).
static SDValue ParseBFI(SDNode *N, APInt &ToMask, APInt &FromMask) {
  assert(N->getOpcode() == ARMISD::BFI);

  SDValue From = N->getOperand(1);
  ToMask = ~cast<ConstantSDNode>(N->getOperand(2))->getAPIntValue();
  FromMask = APInt::getLowBitsSet(ToMask.getBitWidth(), ToMask.countPopulation());

  // If the Base came from a SHR #C, we can deduce that it is really testing bit
  // #C in the base of the SHR.
  if (From->getOpcode() == ISD::SRL &&
      isa<ConstantSDNode>(From->getOperand(1))) {
    APInt Shift = cast<ConstantSDNode>(From->getOperand(1))->getAPIntValue();
    assert(Shift.getLimitedValue() < 32 && "Shift too large!");
    FromMask <<= Shift.getLimitedValue(31);
    From = From->getOperand(0);
  }

  return From;
}

// If A and B contain one contiguous set of bits, does A | B == A . B?
//
// Neither A nor B must be zero.
static bool BitsProperlyConcatenate(const APInt &A, const APInt &B) {
  unsigned LastActiveBitInA =  A.countTrailingZeros();
  unsigned FirstActiveBitInB = B.getBitWidth() - B.countLeadingZeros() - 1;
  return LastActiveBitInA - 1 == FirstActiveBitInB;
}

static SDValue FindBFIToCombineWith(SDNode *N) {
  // We have a BFI in N. Follow a possible chain of BFIs and find a BFI it can combine with,
  // if one exists.
  APInt ToMask, FromMask;
  SDValue From = ParseBFI(N, ToMask, FromMask);
  SDValue To = N->getOperand(0);

  // Now check for a compatible BFI to merge with. We can pass through BFIs that
  // aren't compatible, but not if they set the same bit in their destination as
  // we do (or that of any BFI we're going to combine with).
  SDValue V = To;
  APInt CombinedToMask = ToMask;
  while (V.getOpcode() == ARMISD::BFI) {
    APInt NewToMask, NewFromMask;
    SDValue NewFrom = ParseBFI(V.getNode(), NewToMask, NewFromMask);
    if (NewFrom != From) {
      // This BFI has a different base. Keep going.
      CombinedToMask |= NewToMask;
      V = V.getOperand(0);
      continue;
    }

    // Do the written bits conflict with any we've seen so far?
    if ((NewToMask & CombinedToMask).getBoolValue())
      // Conflicting bits - bail out because going further is unsafe.
      return SDValue();

    // Are the new bits contiguous when combined with the old bits?
    if (BitsProperlyConcatenate(ToMask, NewToMask) &&
        BitsProperlyConcatenate(FromMask, NewFromMask))
      return V;
    if (BitsProperlyConcatenate(NewToMask, ToMask) &&
        BitsProperlyConcatenate(NewFromMask, FromMask))
      return V;

    // We've seen a write to some bits, so track it.
    CombinedToMask |= NewToMask;
    // Keep going...
    V = V.getOperand(0);
  }

  return SDValue();
}

static SDValue PerformBFICombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  SDValue N1 = N->getOperand(1);
  if (N1.getOpcode() == ISD::AND) {
    // (bfi A, (and B, Mask1), Mask2) -> (bfi A, B, Mask2) iff
    // the bits being cleared by the AND are not demanded by the BFI.
    ConstantSDNode *N11C = dyn_cast<ConstantSDNode>(N1.getOperand(1));
    if (!N11C)
      return SDValue();
    unsigned InvMask = cast<ConstantSDNode>(N->getOperand(2))->getZExtValue();
    unsigned LSB = countTrailingZeros(~InvMask);
    unsigned Width = (32 - countLeadingZeros(~InvMask)) - LSB;
    assert(Width <
               static_cast<unsigned>(std::numeric_limits<unsigned>::digits) &&
           "undefined behavior");
    unsigned Mask = (1u << Width) - 1;
    unsigned Mask2 = N11C->getZExtValue();
    if ((Mask & (~Mask2)) == 0)
      return DCI.DAG.getNode(ARMISD::BFI, SDLoc(N), N->getValueType(0),
                             N->getOperand(0), N1.getOperand(0),
                             N->getOperand(2));
  } else if (N->getOperand(0).getOpcode() == ARMISD::BFI) {
    // We have a BFI of a BFI. Walk up the BFI chain to see how long it goes.
    // Keep track of any consecutive bits set that all come from the same base
    // value. We can combine these together into a single BFI.
    SDValue CombineBFI = FindBFIToCombineWith(N);
    if (CombineBFI == SDValue())
      return SDValue();

    // We've found a BFI.
    APInt ToMask1, FromMask1;
    SDValue From1 = ParseBFI(N, ToMask1, FromMask1);

    APInt ToMask2, FromMask2;
    SDValue From2 = ParseBFI(CombineBFI.getNode(), ToMask2, FromMask2);
    assert(From1 == From2);
    (void)From2;

    // First, unlink CombineBFI.
    DCI.DAG.ReplaceAllUsesWith(CombineBFI, CombineBFI.getOperand(0));
    // Then create a new BFI, combining the two together.
    APInt NewFromMask = FromMask1 | FromMask2;
    APInt NewToMask = ToMask1 | ToMask2;

    EVT VT = N->getValueType(0);
    SDLoc dl(N);

    if (NewFromMask[0] == 0)
      From1 = DCI.DAG.getNode(
        ISD::SRL, dl, VT, From1,
        DCI.DAG.getConstant(NewFromMask.countTrailingZeros(), dl, VT));
    return DCI.DAG.getNode(ARMISD::BFI, dl, VT, N->getOperand(0), From1,
                           DCI.DAG.getConstant(~NewToMask, dl, VT));
  }
  return SDValue();
}

/// PerformVMOVRRDCombine - Target-specific dag combine xforms for
/// ARMISD::VMOVRRD.
static SDValue PerformVMOVRRDCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *Subtarget) {
  // vmovrrd(vmovdrr x, y) -> x,y
  SDValue InDouble = N->getOperand(0);
  if (InDouble.getOpcode() == ARMISD::VMOVDRR && !Subtarget->isFPOnlySP())
    return DCI.CombineTo(N, InDouble.getOperand(0), InDouble.getOperand(1));

  // vmovrrd(load f64) -> (load i32), (load i32)
  SDNode *InNode = InDouble.getNode();
  if (ISD::isNormalLoad(InNode) && InNode->hasOneUse() &&
      InNode->getValueType(0) == MVT::f64 &&
      InNode->getOperand(1).getOpcode() == ISD::FrameIndex &&
      !cast<LoadSDNode>(InNode)->isVolatile()) {
    // TODO: Should this be done for non-FrameIndex operands?
    LoadSDNode *LD = cast<LoadSDNode>(InNode);

    SelectionDAG &DAG = DCI.DAG;
    SDLoc DL(LD);
    SDValue BasePtr = LD->getBasePtr();
    SDValue NewLD1 =
        DAG.getLoad(MVT::i32, DL, LD->getChain(), BasePtr, LD->getPointerInfo(),
                    LD->getAlignment(), LD->getMemOperand()->getFlags());

    SDValue OffsetPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, BasePtr,
                                    DAG.getConstant(4, DL, MVT::i32));
    SDValue NewLD2 = DAG.getLoad(
        MVT::i32, DL, NewLD1.getValue(1), OffsetPtr, LD->getPointerInfo(),
        std::min(4U, LD->getAlignment() / 2), LD->getMemOperand()->getFlags());

    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), NewLD2.getValue(1));
    if (DCI.DAG.getDataLayout().isBigEndian())
      std::swap (NewLD1, NewLD2);
    SDValue Result = DCI.CombineTo(N, NewLD1, NewLD2);
    return Result;
  }

  return SDValue();
}

/// PerformVMOVDRRCombine - Target-specific dag combine xforms for
/// ARMISD::VMOVDRR.  This is also used for BUILD_VECTORs with 2 operands.
static SDValue PerformVMOVDRRCombine(SDNode *N, SelectionDAG &DAG) {
  // N=vmovrrd(X); vmovdrr(N:0, N:1) -> bit_convert(X)
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  if (Op0.getOpcode() == ISD::BITCAST)
    Op0 = Op0.getOperand(0);
  if (Op1.getOpcode() == ISD::BITCAST)
    Op1 = Op1.getOperand(0);
  if (Op0.getOpcode() == ARMISD::VMOVRRD &&
      Op0.getNode() == Op1.getNode() &&
      Op0.getResNo() == 0 && Op1.getResNo() == 1)
    return DAG.getNode(ISD::BITCAST, SDLoc(N),
                       N->getValueType(0), Op0.getOperand(0));
  return SDValue();
}

/// hasNormalLoadOperand - Check if any of the operands of a BUILD_VECTOR node
/// are normal, non-volatile loads.  If so, it is profitable to bitcast an
/// i64 vector to have f64 elements, since the value can then be loaded
/// directly into a VFP register.
static bool hasNormalLoadOperand(SDNode *N) {
  unsigned NumElts = N->getValueType(0).getVectorNumElements();
  for (unsigned i = 0; i < NumElts; ++i) {
    SDNode *Elt = N->getOperand(i).getNode();
    if (ISD::isNormalLoad(Elt) && !cast<LoadSDNode>(Elt)->isVolatile())
      return true;
  }
  return false;
}

/// PerformBUILD_VECTORCombine - Target-specific dag combine xforms for
/// ISD::BUILD_VECTOR.
static SDValue PerformBUILD_VECTORCombine(SDNode *N,
                                          TargetLowering::DAGCombinerInfo &DCI,
                                          const ARMSubtarget *Subtarget) {
  // build_vector(N=ARMISD::VMOVRRD(X), N:1) -> bit_convert(X):
  // VMOVRRD is introduced when legalizing i64 types.  It forces the i64 value
  // into a pair of GPRs, which is fine when the value is used as a scalar,
  // but if the i64 value is converted to a vector, we need to undo the VMOVRRD.
  SelectionDAG &DAG = DCI.DAG;
  if (N->getNumOperands() == 2)
    if (SDValue RV = PerformVMOVDRRCombine(N, DAG))
      return RV;

  // Load i64 elements as f64 values so that type legalization does not split
  // them up into i32 values.
  EVT VT = N->getValueType(0);
  if (VT.getVectorElementType() != MVT::i64 || !hasNormalLoadOperand(N))
    return SDValue();
  SDLoc dl(N);
  SmallVector<SDValue, 8> Ops;
  unsigned NumElts = VT.getVectorNumElements();
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = DAG.getNode(ISD::BITCAST, dl, MVT::f64, N->getOperand(i));
    Ops.push_back(V);
    // Make the DAGCombiner fold the bitcast.
    DCI.AddToWorklist(V.getNode());
  }
  EVT FloatVT = EVT::getVectorVT(*DAG.getContext(), MVT::f64, NumElts);
  SDValue BV = DAG.getBuildVector(FloatVT, dl, Ops);
  return DAG.getNode(ISD::BITCAST, dl, VT, BV);
}

/// Target-specific dag combine xforms for ARMISD::BUILD_VECTOR.
static SDValue
PerformARMBUILD_VECTORCombine(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  // ARMISD::BUILD_VECTOR is introduced when legalizing ISD::BUILD_VECTOR.
  // At that time, we may have inserted bitcasts from integer to float.
  // If these bitcasts have survived DAGCombine, change the lowering of this
  // BUILD_VECTOR in something more vector friendly, i.e., that does not
  // force to use floating point types.

  // Make sure we can change the type of the vector.
  // This is possible iff:
  // 1. The vector is only used in a bitcast to a integer type. I.e.,
  //    1.1. Vector is used only once.
  //    1.2. Use is a bit convert to an integer type.
  // 2. The size of its operands are 32-bits (64-bits are not legal).
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();

  // Check 1.1. and 2.
  if (EltVT.getSizeInBits() != 32 || !N->hasOneUse())
    return SDValue();

  // By construction, the input type must be float.
  assert(EltVT == MVT::f32 && "Unexpected type!");

  // Check 1.2.
  SDNode *Use = *N->use_begin();
  if (Use->getOpcode() != ISD::BITCAST ||
      Use->getValueType(0).isFloatingPoint())
    return SDValue();

  // Check profitability.
  // Model is, if more than half of the relevant operands are bitcast from
  // i32, turn the build_vector into a sequence of insert_vector_elt.
  // Relevant operands are everything that is not statically
  // (i.e., at compile time) bitcasted.
  unsigned NumOfBitCastedElts = 0;
  unsigned NumElts = VT.getVectorNumElements();
  unsigned NumOfRelevantElts = NumElts;
  for (unsigned Idx = 0; Idx < NumElts; ++Idx) {
    SDValue Elt = N->getOperand(Idx);
    if (Elt->getOpcode() == ISD::BITCAST) {
      // Assume only bit cast to i32 will go away.
      if (Elt->getOperand(0).getValueType() == MVT::i32)
        ++NumOfBitCastedElts;
    } else if (Elt.isUndef() || isa<ConstantSDNode>(Elt))
      // Constants are statically casted, thus do not count them as
      // relevant operands.
      --NumOfRelevantElts;
  }

  // Check if more than half of the elements require a non-free bitcast.
  if (NumOfBitCastedElts <= NumOfRelevantElts / 2)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  // Create the new vector type.
  EVT VecVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32, NumElts);
  // Check if the type is legal.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!TLI.isTypeLegal(VecVT))
    return SDValue();

  // Combine:
  // ARMISD::BUILD_VECTOR E1, E2, ..., EN.
  // => BITCAST INSERT_VECTOR_ELT
  //                      (INSERT_VECTOR_ELT (...), (BITCAST EN-1), N-1),
  //                      (BITCAST EN), N.
  SDValue Vec = DAG.getUNDEF(VecVT);
  SDLoc dl(N);
  for (unsigned Idx = 0 ; Idx < NumElts; ++Idx) {
    SDValue V = N->getOperand(Idx);
    if (V.isUndef())
      continue;
    if (V.getOpcode() == ISD::BITCAST &&
        V->getOperand(0).getValueType() == MVT::i32)
      // Fold obvious case.
      V = V.getOperand(0);
    else {
      V = DAG.getNode(ISD::BITCAST, SDLoc(V), MVT::i32, V);
      // Make the DAGCombiner fold the bitcasts.
      DCI.AddToWorklist(V.getNode());
    }
    SDValue LaneIdx = DAG.getConstant(Idx, dl, MVT::i32);
    Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VecVT, Vec, V, LaneIdx);
  }
  Vec = DAG.getNode(ISD::BITCAST, dl, VT, Vec);
  // Make the DAGCombiner fold the bitcasts.
  DCI.AddToWorklist(Vec.getNode());
  return Vec;
}

/// PerformInsertEltCombine - Target-specific dag combine xforms for
/// ISD::INSERT_VECTOR_ELT.
static SDValue PerformInsertEltCombine(SDNode *N,
                                       TargetLowering::DAGCombinerInfo &DCI) {
  // Bitcast an i64 load inserted into a vector to f64.
  // Otherwise, the i64 value will be legalized to a pair of i32 values.
  EVT VT = N->getValueType(0);
  SDNode *Elt = N->getOperand(1).getNode();
  if (VT.getVectorElementType() != MVT::i64 ||
      !ISD::isNormalLoad(Elt) || cast<LoadSDNode>(Elt)->isVolatile())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  EVT FloatVT = EVT::getVectorVT(*DAG.getContext(), MVT::f64,
                                 VT.getVectorNumElements());
  SDValue Vec = DAG.getNode(ISD::BITCAST, dl, FloatVT, N->getOperand(0));
  SDValue V = DAG.getNode(ISD::BITCAST, dl, MVT::f64, N->getOperand(1));
  // Make the DAGCombiner fold the bitcasts.
  DCI.AddToWorklist(Vec.getNode());
  DCI.AddToWorklist(V.getNode());
  SDValue InsElt = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, FloatVT,
                               Vec, V, N->getOperand(2));
  return DAG.getNode(ISD::BITCAST, dl, VT, InsElt);
}

/// PerformVECTOR_SHUFFLECombine - Target-specific dag combine xforms for
/// ISD::VECTOR_SHUFFLE.
static SDValue PerformVECTOR_SHUFFLECombine(SDNode *N, SelectionDAG &DAG) {
  // The LLVM shufflevector instruction does not require the shuffle mask
  // length to match the operand vector length, but ISD::VECTOR_SHUFFLE does
  // have that requirement.  When translating to ISD::VECTOR_SHUFFLE, if the
  // operands do not match the mask length, they are extended by concatenating
  // them with undef vectors.  That is probably the right thing for other
  // targets, but for NEON it is better to concatenate two double-register
  // size vector operands into a single quad-register size vector.  Do that
  // transformation here:
  //   shuffle(concat(v1, undef), concat(v2, undef)) ->
  //   shuffle(concat(v1, v2), undef)
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  if (Op0.getOpcode() != ISD::CONCAT_VECTORS ||
      Op1.getOpcode() != ISD::CONCAT_VECTORS ||
      Op0.getNumOperands() != 2 ||
      Op1.getNumOperands() != 2)
    return SDValue();
  SDValue Concat0Op1 = Op0.getOperand(1);
  SDValue Concat1Op1 = Op1.getOperand(1);
  if (!Concat0Op1.isUndef() || !Concat1Op1.isUndef())
    return SDValue();
  // Skip the transformation if any of the types are illegal.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = N->getValueType(0);
  if (!TLI.isTypeLegal(VT) ||
      !TLI.isTypeLegal(Concat0Op1.getValueType()) ||
      !TLI.isTypeLegal(Concat1Op1.getValueType()))
    return SDValue();

  SDValue NewConcat = DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), VT,
                                  Op0.getOperand(0), Op1.getOperand(0));
  // Translate the shuffle mask.
  SmallVector<int, 16> NewMask;
  unsigned NumElts = VT.getVectorNumElements();
  unsigned HalfElts = NumElts/2;
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(N);
  for (unsigned n = 0; n < NumElts; ++n) {
    int MaskElt = SVN->getMaskElt(n);
    int NewElt = -1;
    if (MaskElt < (int)HalfElts)
      NewElt = MaskElt;
    else if (MaskElt >= (int)NumElts && MaskElt < (int)(NumElts + HalfElts))
      NewElt = HalfElts + MaskElt - NumElts;
    NewMask.push_back(NewElt);
  }
  return DAG.getVectorShuffle(VT, SDLoc(N), NewConcat,
                              DAG.getUNDEF(VT), NewMask);
}

/// CombineBaseUpdate - Target-specific DAG combine function for VLDDUP,
/// NEON load/store intrinsics, and generic vector load/stores, to merge
/// base address updates.
/// For generic load/stores, the memory type is assumed to be a vector.
/// The caller is assumed to have checked legality.
static SDValue CombineBaseUpdate(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  const bool isIntrinsic = (N->getOpcode() == ISD::INTRINSIC_VOID ||
                            N->getOpcode() == ISD::INTRINSIC_W_CHAIN);
  const bool isStore = N->getOpcode() == ISD::STORE;
  const unsigned AddrOpIdx = ((isIntrinsic || isStore) ? 2 : 1);
  SDValue Addr = N->getOperand(AddrOpIdx);
  MemSDNode *MemN = cast<MemSDNode>(N);
  SDLoc dl(N);

  // Search for a use of the address operand that is an increment.
  for (SDNode::use_iterator UI = Addr.getNode()->use_begin(),
         UE = Addr.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User->getOpcode() != ISD::ADD ||
        UI.getUse().getResNo() != Addr.getResNo())
      continue;

    // Check that the add is independent of the load/store.  Otherwise, folding
    // it would create a cycle. We can avoid searching through Addr as it's a
    // predecessor to both.
    SmallPtrSet<const SDNode *, 32> Visited;
    SmallVector<const SDNode *, 16> Worklist;
    Visited.insert(Addr.getNode());
    Worklist.push_back(N);
    Worklist.push_back(User);
    if (SDNode::hasPredecessorHelper(N, Visited, Worklist) ||
        SDNode::hasPredecessorHelper(User, Visited, Worklist))
      continue;

    // Find the new opcode for the updating load/store.
    bool isLoadOp = true;
    bool isLaneOp = false;
    unsigned NewOpc = 0;
    unsigned NumVecs = 0;
    if (isIntrinsic) {
      unsigned IntNo = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
      switch (IntNo) {
      default: llvm_unreachable("unexpected intrinsic for Neon base update");
      case Intrinsic::arm_neon_vld1:     NewOpc = ARMISD::VLD1_UPD;
        NumVecs = 1; break;
      case Intrinsic::arm_neon_vld2:     NewOpc = ARMISD::VLD2_UPD;
        NumVecs = 2; break;
      case Intrinsic::arm_neon_vld3:     NewOpc = ARMISD::VLD3_UPD;
        NumVecs = 3; break;
      case Intrinsic::arm_neon_vld4:     NewOpc = ARMISD::VLD4_UPD;
        NumVecs = 4; break;
      case Intrinsic::arm_neon_vld2dup:
      case Intrinsic::arm_neon_vld3dup:
      case Intrinsic::arm_neon_vld4dup:
        // TODO: Support updating VLDxDUP nodes. For now, we just skip
        // combining base updates for such intrinsics.
        continue;
      case Intrinsic::arm_neon_vld2lane: NewOpc = ARMISD::VLD2LN_UPD;
        NumVecs = 2; isLaneOp = true; break;
      case Intrinsic::arm_neon_vld3lane: NewOpc = ARMISD::VLD3LN_UPD;
        NumVecs = 3; isLaneOp = true; break;
      case Intrinsic::arm_neon_vld4lane: NewOpc = ARMISD::VLD4LN_UPD;
        NumVecs = 4; isLaneOp = true; break;
      case Intrinsic::arm_neon_vst1:     NewOpc = ARMISD::VST1_UPD;
        NumVecs = 1; isLoadOp = false; break;
      case Intrinsic::arm_neon_vst2:     NewOpc = ARMISD::VST2_UPD;
        NumVecs = 2; isLoadOp = false; break;
      case Intrinsic::arm_neon_vst3:     NewOpc = ARMISD::VST3_UPD;
        NumVecs = 3; isLoadOp = false; break;
      case Intrinsic::arm_neon_vst4:     NewOpc = ARMISD::VST4_UPD;
        NumVecs = 4; isLoadOp = false; break;
      case Intrinsic::arm_neon_vst2lane: NewOpc = ARMISD::VST2LN_UPD;
        NumVecs = 2; isLoadOp = false; isLaneOp = true; break;
      case Intrinsic::arm_neon_vst3lane: NewOpc = ARMISD::VST3LN_UPD;
        NumVecs = 3; isLoadOp = false; isLaneOp = true; break;
      case Intrinsic::arm_neon_vst4lane: NewOpc = ARMISD::VST4LN_UPD;
        NumVecs = 4; isLoadOp = false; isLaneOp = true; break;
      }
    } else {
      isLaneOp = true;
      switch (N->getOpcode()) {
      default: llvm_unreachable("unexpected opcode for Neon base update");
      case ARMISD::VLD1DUP: NewOpc = ARMISD::VLD1DUP_UPD; NumVecs = 1; break;
      case ARMISD::VLD2DUP: NewOpc = ARMISD::VLD2DUP_UPD; NumVecs = 2; break;
      case ARMISD::VLD3DUP: NewOpc = ARMISD::VLD3DUP_UPD; NumVecs = 3; break;
      case ARMISD::VLD4DUP: NewOpc = ARMISD::VLD4DUP_UPD; NumVecs = 4; break;
      case ISD::LOAD:       NewOpc = ARMISD::VLD1_UPD;
        NumVecs = 1; isLaneOp = false; break;
      case ISD::STORE:      NewOpc = ARMISD::VST1_UPD;
        NumVecs = 1; isLaneOp = false; isLoadOp = false; break;
      }
    }

    // Find the size of memory referenced by the load/store.
    EVT VecTy;
    if (isLoadOp) {
      VecTy = N->getValueType(0);
    } else if (isIntrinsic) {
      VecTy = N->getOperand(AddrOpIdx+1).getValueType();
    } else {
      assert(isStore && "Node has to be a load, a store, or an intrinsic!");
      VecTy = N->getOperand(1).getValueType();
    }

    unsigned NumBytes = NumVecs * VecTy.getSizeInBits() / 8;
    if (isLaneOp)
      NumBytes /= VecTy.getVectorNumElements();

    // If the increment is a constant, it must match the memory ref size.
    SDValue Inc = User->getOperand(User->getOperand(0) == Addr ? 1 : 0);
    ConstantSDNode *CInc = dyn_cast<ConstantSDNode>(Inc.getNode());
    if (NumBytes >= 3 * 16 && (!CInc || CInc->getZExtValue() != NumBytes)) {
      // VLD3/4 and VST3/4 for 128-bit vectors are implemented with two
      // separate instructions that make it harder to use a non-constant update.
      continue;
    }

    // OK, we found an ADD we can fold into the base update.
    // Now, create a _UPD node, taking care of not breaking alignment.

    EVT AlignedVecTy = VecTy;
    unsigned Alignment = MemN->getAlignment();

    // If this is a less-than-standard-aligned load/store, change the type to
    // match the standard alignment.
    // The alignment is overlooked when selecting _UPD variants; and it's
    // easier to introduce bitcasts here than fix that.
    // There are 3 ways to get to this base-update combine:
    // - intrinsics: they are assumed to be properly aligned (to the standard
    //   alignment of the memory type), so we don't need to do anything.
    // - ARMISD::VLDx nodes: they are only generated from the aforementioned
    //   intrinsics, so, likewise, there's nothing to do.
    // - generic load/store instructions: the alignment is specified as an
    //   explicit operand, rather than implicitly as the standard alignment
    //   of the memory type (like the intrisics).  We need to change the
    //   memory type to match the explicit alignment.  That way, we don't
    //   generate non-standard-aligned ARMISD::VLDx nodes.
    if (isa<LSBaseSDNode>(N)) {
      if (Alignment == 0)
        Alignment = 1;
      if (Alignment < VecTy.getScalarSizeInBits() / 8) {
        MVT EltTy = MVT::getIntegerVT(Alignment * 8);
        assert(NumVecs == 1 && "Unexpected multi-element generic load/store.");
        assert(!isLaneOp && "Unexpected generic load/store lane.");
        unsigned NumElts = NumBytes / (EltTy.getSizeInBits() / 8);
        AlignedVecTy = MVT::getVectorVT(EltTy, NumElts);
      }
      // Don't set an explicit alignment on regular load/stores that we want
      // to transform to VLD/VST 1_UPD nodes.
      // This matches the behavior of regular load/stores, which only get an
      // explicit alignment if the MMO alignment is larger than the standard
      // alignment of the memory type.
      // Intrinsics, however, always get an explicit alignment, set to the
      // alignment of the MMO.
      Alignment = 1;
    }

    // Create the new updating load/store node.
    // First, create an SDVTList for the new updating node's results.
    EVT Tys[6];
    unsigned NumResultVecs = (isLoadOp ? NumVecs : 0);
    unsigned n;
    for (n = 0; n < NumResultVecs; ++n)
      Tys[n] = AlignedVecTy;
    Tys[n++] = MVT::i32;
    Tys[n] = MVT::Other;
    SDVTList SDTys = DAG.getVTList(makeArrayRef(Tys, NumResultVecs+2));

    // Then, gather the new node's operands.
    SmallVector<SDValue, 8> Ops;
    Ops.push_back(N->getOperand(0)); // incoming chain
    Ops.push_back(N->getOperand(AddrOpIdx));
    Ops.push_back(Inc);

    if (StoreSDNode *StN = dyn_cast<StoreSDNode>(N)) {
      // Try to match the intrinsic's signature
      Ops.push_back(StN->getValue());
    } else {
      // Loads (and of course intrinsics) match the intrinsics' signature,
      // so just add all but the alignment operand.
      for (unsigned i = AddrOpIdx + 1; i < N->getNumOperands() - 1; ++i)
        Ops.push_back(N->getOperand(i));
    }

    // For all node types, the alignment operand is always the last one.
    Ops.push_back(DAG.getConstant(Alignment, dl, MVT::i32));

    // If this is a non-standard-aligned STORE, the penultimate operand is the
    // stored value.  Bitcast it to the aligned type.
    if (AlignedVecTy != VecTy && N->getOpcode() == ISD::STORE) {
      SDValue &StVal = Ops[Ops.size()-2];
      StVal = DAG.getNode(ISD::BITCAST, dl, AlignedVecTy, StVal);
    }

    EVT LoadVT = isLaneOp ? VecTy.getVectorElementType() : AlignedVecTy;
    SDValue UpdN = DAG.getMemIntrinsicNode(NewOpc, dl, SDTys, Ops, LoadVT,
                                           MemN->getMemOperand());

    // Update the uses.
    SmallVector<SDValue, 5> NewResults;
    for (unsigned i = 0; i < NumResultVecs; ++i)
      NewResults.push_back(SDValue(UpdN.getNode(), i));

    // If this is an non-standard-aligned LOAD, the first result is the loaded
    // value.  Bitcast it to the expected result type.
    if (AlignedVecTy != VecTy && N->getOpcode() == ISD::LOAD) {
      SDValue &LdVal = NewResults[0];
      LdVal = DAG.getNode(ISD::BITCAST, dl, VecTy, LdVal);
    }

    NewResults.push_back(SDValue(UpdN.getNode(), NumResultVecs+1)); // chain
    DCI.CombineTo(N, NewResults);
    DCI.CombineTo(User, SDValue(UpdN.getNode(), NumResultVecs));

    break;
  }
  return SDValue();
}

static SDValue PerformVLDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  return CombineBaseUpdate(N, DCI);
}

/// CombineVLDDUP - For a VDUPLANE node N, check if its source operand is a
/// vldN-lane (N > 1) intrinsic, and if all the other uses of that intrinsic
/// are also VDUPLANEs.  If so, combine them to a vldN-dup operation and
/// return true.
static bool CombineVLDDUP(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  // vldN-dup instructions only support 64-bit vectors for N > 1.
  if (!VT.is64BitVector())
    return false;

  // Check if the VDUPLANE operand is a vldN-dup intrinsic.
  SDNode *VLD = N->getOperand(0).getNode();
  if (VLD->getOpcode() != ISD::INTRINSIC_W_CHAIN)
    return false;
  unsigned NumVecs = 0;
  unsigned NewOpc = 0;
  unsigned IntNo = cast<ConstantSDNode>(VLD->getOperand(1))->getZExtValue();
  if (IntNo == Intrinsic::arm_neon_vld2lane) {
    NumVecs = 2;
    NewOpc = ARMISD::VLD2DUP;
  } else if (IntNo == Intrinsic::arm_neon_vld3lane) {
    NumVecs = 3;
    NewOpc = ARMISD::VLD3DUP;
  } else if (IntNo == Intrinsic::arm_neon_vld4lane) {
    NumVecs = 4;
    NewOpc = ARMISD::VLD4DUP;
  } else {
    return false;
  }

  // First check that all the vldN-lane uses are VDUPLANEs and that the lane
  // numbers match the load.
  unsigned VLDLaneNo =
    cast<ConstantSDNode>(VLD->getOperand(NumVecs+3))->getZExtValue();
  for (SDNode::use_iterator UI = VLD->use_begin(), UE = VLD->use_end();
       UI != UE; ++UI) {
    // Ignore uses of the chain result.
    if (UI.getUse().getResNo() == NumVecs)
      continue;
    SDNode *User = *UI;
    if (User->getOpcode() != ARMISD::VDUPLANE ||
        VLDLaneNo != cast<ConstantSDNode>(User->getOperand(1))->getZExtValue())
      return false;
  }

  // Create the vldN-dup node.
  EVT Tys[5];
  unsigned n;
  for (n = 0; n < NumVecs; ++n)
    Tys[n] = VT;
  Tys[n] = MVT::Other;
  SDVTList SDTys = DAG.getVTList(makeArrayRef(Tys, NumVecs+1));
  SDValue Ops[] = { VLD->getOperand(0), VLD->getOperand(2) };
  MemIntrinsicSDNode *VLDMemInt = cast<MemIntrinsicSDNode>(VLD);
  SDValue VLDDup = DAG.getMemIntrinsicNode(NewOpc, SDLoc(VLD), SDTys,
                                           Ops, VLDMemInt->getMemoryVT(),
                                           VLDMemInt->getMemOperand());

  // Update the uses.
  for (SDNode::use_iterator UI = VLD->use_begin(), UE = VLD->use_end();
       UI != UE; ++UI) {
    unsigned ResNo = UI.getUse().getResNo();
    // Ignore uses of the chain result.
    if (ResNo == NumVecs)
      continue;
    SDNode *User = *UI;
    DCI.CombineTo(User, SDValue(VLDDup.getNode(), ResNo));
  }

  // Now the vldN-lane intrinsic is dead except for its chain result.
  // Update uses of the chain.
  std::vector<SDValue> VLDDupResults;
  for (unsigned n = 0; n < NumVecs; ++n)
    VLDDupResults.push_back(SDValue(VLDDup.getNode(), n));
  VLDDupResults.push_back(SDValue(VLDDup.getNode(), NumVecs));
  DCI.CombineTo(VLD, VLDDupResults);

  return true;
}

/// PerformVDUPLANECombine - Target-specific dag combine xforms for
/// ARMISD::VDUPLANE.
static SDValue PerformVDUPLANECombine(SDNode *N,
                                      TargetLowering::DAGCombinerInfo &DCI) {
  SDValue Op = N->getOperand(0);

  // If the source is a vldN-lane (N > 1) intrinsic, and all the other uses
  // of that intrinsic are also VDUPLANEs, combine them to a vldN-dup operation.
  if (CombineVLDDUP(N, DCI))
    return SDValue(N, 0);

  // If the source is already a VMOVIMM or VMVNIMM splat, the VDUPLANE is
  // redundant.  Ignore bit_converts for now; element sizes are checked below.
  while (Op.getOpcode() == ISD::BITCAST)
    Op = Op.getOperand(0);
  if (Op.getOpcode() != ARMISD::VMOVIMM && Op.getOpcode() != ARMISD::VMVNIMM)
    return SDValue();

  // Make sure the VMOV element size is not bigger than the VDUPLANE elements.
  unsigned EltSize = Op.getScalarValueSizeInBits();
  // The canonical VMOV for a zero vector uses a 32-bit element size.
  unsigned Imm = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  unsigned EltBits;
  if (ARM_AM::decodeNEONModImm(Imm, EltBits) == 0)
    EltSize = 8;
  EVT VT = N->getValueType(0);
  if (EltSize > VT.getScalarSizeInBits())
    return SDValue();

  return DCI.DAG.getNode(ISD::BITCAST, SDLoc(N), VT, Op);
}

/// PerformVDUPCombine - Target-specific dag combine xforms for ARMISD::VDUP.
static SDValue PerformVDUPCombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  SDValue Op = N->getOperand(0);

  // Match VDUP(LOAD) -> VLD1DUP.
  // We match this pattern here rather than waiting for isel because the
  // transform is only legal for unindexed loads.
  LoadSDNode *LD = dyn_cast<LoadSDNode>(Op.getNode());
  if (LD && Op.hasOneUse() && LD->isUnindexed() &&
      LD->getMemoryVT() == N->getValueType(0).getVectorElementType()) {
    SDValue Ops[] = { LD->getOperand(0), LD->getOperand(1),
                      DAG.getConstant(LD->getAlignment(), SDLoc(N), MVT::i32) };
    SDVTList SDTys = DAG.getVTList(N->getValueType(0), MVT::Other);
    SDValue VLDDup = DAG.getMemIntrinsicNode(ARMISD::VLD1DUP, SDLoc(N), SDTys,
                                             Ops, LD->getMemoryVT(),
                                             LD->getMemOperand());
    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), VLDDup.getValue(1));
    return VLDDup;
  }

  return SDValue();
}

static SDValue PerformLOADCombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI) {
  EVT VT = N->getValueType(0);

  // If this is a legal vector load, try to combine it into a VLD1_UPD.
  if (ISD::isNormalLoad(N) && VT.isVector() &&
      DCI.DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return CombineBaseUpdate(N, DCI);

  return SDValue();
}

/// PerformSTORECombine - Target-specific dag combine xforms for
/// ISD::STORE.
static SDValue PerformSTORECombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI) {
  StoreSDNode *St = cast<StoreSDNode>(N);
  if (St->isVolatile())
    return SDValue();

  // Optimize trunc store (of multiple scalars) to shuffle and store.  First,
  // pack all of the elements in one place.  Next, store to memory in fewer
  // chunks.
  SDValue StVal = St->getValue();
  EVT VT = StVal.getValueType();
  if (St->isTruncatingStore() && VT.isVector()) {
    SelectionDAG &DAG = DCI.DAG;
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();
    EVT StVT = St->getMemoryVT();
    unsigned NumElems = VT.getVectorNumElements();
    assert(StVT != VT && "Cannot truncate to the same type");
    unsigned FromEltSz = VT.getScalarSizeInBits();
    unsigned ToEltSz = StVT.getScalarSizeInBits();

    // From, To sizes and ElemCount must be pow of two
    if (!isPowerOf2_32(NumElems * FromEltSz * ToEltSz)) return SDValue();

    // We are going to use the original vector elt for storing.
    // Accumulated smaller vector elements must be a multiple of the store size.
    if (0 != (NumElems * FromEltSz) % ToEltSz) return SDValue();

    unsigned SizeRatio  = FromEltSz / ToEltSz;
    assert(SizeRatio * NumElems * ToEltSz == VT.getSizeInBits());

    // Create a type on which we perform the shuffle.
    EVT WideVecVT = EVT::getVectorVT(*DAG.getContext(), StVT.getScalarType(),
                                     NumElems*SizeRatio);
    assert(WideVecVT.getSizeInBits() == VT.getSizeInBits());

    SDLoc DL(St);
    SDValue WideVec = DAG.getNode(ISD::BITCAST, DL, WideVecVT, StVal);
    SmallVector<int, 8> ShuffleVec(NumElems * SizeRatio, -1);
    for (unsigned i = 0; i < NumElems; ++i)
      ShuffleVec[i] = DAG.getDataLayout().isBigEndian()
                          ? (i + 1) * SizeRatio - 1
                          : i * SizeRatio;

    // Can't shuffle using an illegal type.
    if (!TLI.isTypeLegal(WideVecVT)) return SDValue();

    SDValue Shuff = DAG.getVectorShuffle(WideVecVT, DL, WideVec,
                                DAG.getUNDEF(WideVec.getValueType()),
                                ShuffleVec);
    // At this point all of the data is stored at the bottom of the
    // register. We now need to save it to mem.

    // Find the largest store unit
    MVT StoreType = MVT::i8;
    for (MVT Tp : MVT::integer_valuetypes()) {
      if (TLI.isTypeLegal(Tp) && Tp.getSizeInBits() <= NumElems * ToEltSz)
        StoreType = Tp;
    }
    // Didn't find a legal store type.
    if (!TLI.isTypeLegal(StoreType))
      return SDValue();

    // Bitcast the original vector into a vector of store-size units
    EVT StoreVecVT = EVT::getVectorVT(*DAG.getContext(),
            StoreType, VT.getSizeInBits()/EVT(StoreType).getSizeInBits());
    assert(StoreVecVT.getSizeInBits() == VT.getSizeInBits());
    SDValue ShuffWide = DAG.getNode(ISD::BITCAST, DL, StoreVecVT, Shuff);
    SmallVector<SDValue, 8> Chains;
    SDValue Increment = DAG.getConstant(StoreType.getSizeInBits() / 8, DL,
                                        TLI.getPointerTy(DAG.getDataLayout()));
    SDValue BasePtr = St->getBasePtr();

    // Perform one or more big stores into memory.
    unsigned E = (ToEltSz*NumElems)/StoreType.getSizeInBits();
    for (unsigned I = 0; I < E; I++) {
      SDValue SubVec = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL,
                                   StoreType, ShuffWide,
                                   DAG.getIntPtrConstant(I, DL));
      SDValue Ch = DAG.getStore(St->getChain(), DL, SubVec, BasePtr,
                                St->getPointerInfo(), St->getAlignment(),
                                St->getMemOperand()->getFlags());
      BasePtr = DAG.getNode(ISD::ADD, DL, BasePtr.getValueType(), BasePtr,
                            Increment);
      Chains.push_back(Ch);
    }
    return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
  }

  if (!ISD::isNormalStore(St))
    return SDValue();

  // Split a store of a VMOVDRR into two integer stores to avoid mixing NEON and
  // ARM stores of arguments in the same cache line.
  if (StVal.getNode()->getOpcode() == ARMISD::VMOVDRR &&
      StVal.getNode()->hasOneUse()) {
    SelectionDAG  &DAG = DCI.DAG;
    bool isBigEndian = DAG.getDataLayout().isBigEndian();
    SDLoc DL(St);
    SDValue BasePtr = St->getBasePtr();
    SDValue NewST1 = DAG.getStore(
        St->getChain(), DL, StVal.getNode()->getOperand(isBigEndian ? 1 : 0),
        BasePtr, St->getPointerInfo(), St->getAlignment(),
        St->getMemOperand()->getFlags());

    SDValue OffsetPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, BasePtr,
                                    DAG.getConstant(4, DL, MVT::i32));
    return DAG.getStore(NewST1.getValue(0), DL,
                        StVal.getNode()->getOperand(isBigEndian ? 0 : 1),
                        OffsetPtr, St->getPointerInfo(),
                        std::min(4U, St->getAlignment() / 2),
                        St->getMemOperand()->getFlags());
  }

  if (StVal.getValueType() == MVT::i64 &&
      StVal.getNode()->getOpcode() == ISD::EXTRACT_VECTOR_ELT) {

    // Bitcast an i64 store extracted from a vector to f64.
    // Otherwise, the i64 value will be legalized to a pair of i32 values.
    SelectionDAG &DAG = DCI.DAG;
    SDLoc dl(StVal);
    SDValue IntVec = StVal.getOperand(0);
    EVT FloatVT = EVT::getVectorVT(*DAG.getContext(), MVT::f64,
                                   IntVec.getValueType().getVectorNumElements());
    SDValue Vec = DAG.getNode(ISD::BITCAST, dl, FloatVT, IntVec);
    SDValue ExtElt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64,
                                 Vec, StVal.getOperand(1));
    dl = SDLoc(N);
    SDValue V = DAG.getNode(ISD::BITCAST, dl, MVT::i64, ExtElt);
    // Make the DAGCombiner fold the bitcasts.
    DCI.AddToWorklist(Vec.getNode());
    DCI.AddToWorklist(ExtElt.getNode());
    DCI.AddToWorklist(V.getNode());
    return DAG.getStore(St->getChain(), dl, V, St->getBasePtr(),
                        St->getPointerInfo(), St->getAlignment(),
                        St->getMemOperand()->getFlags(), St->getAAInfo());
  }

  // If this is a legal vector store, try to combine it into a VST1_UPD.
  if (ISD::isNormalStore(N) && VT.isVector() &&
      DCI.DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return CombineBaseUpdate(N, DCI);

  return SDValue();
}

/// PerformVCVTCombine - VCVT (floating-point to fixed-point, Advanced SIMD)
/// can replace combinations of VMUL and VCVT (floating-point to integer)
/// when the VMUL has a constant operand that is a power of 2.
///
/// Example (assume d17 = <float 8.000000e+00, float 8.000000e+00>):
///  vmul.f32        d16, d17, d16
///  vcvt.s32.f32    d16, d16
/// becomes:
///  vcvt.s32.f32    d16, d16, #3
static SDValue PerformVCVTCombine(SDNode *N, SelectionDAG &DAG,
                                  const ARMSubtarget *Subtarget) {
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
  MVT IntTy = N->getSimpleValueType(0).getVectorElementType();
  uint32_t IntBits = IntTy.getSizeInBits();
  unsigned NumLanes = Op.getValueType().getVectorNumElements();
  if (FloatBits != 32 || IntBits > 32 || NumLanes > 4) {
    // These instructions only exist converting from f32 to i32. We can handle
    // smaller integers by generating an extra truncate, but larger ones would
    // be lossy. We also can't handle more then 4 lanes, since these intructions
    // only support v2i32/v4i32 types.
    return SDValue();
  }

  BitVector UndefElements;
  BuildVectorSDNode *BV = cast<BuildVectorSDNode>(ConstVec);
  int32_t C = BV->getConstantFPSplatPow2ToLog2Int(&UndefElements, 33);
  if (C == -1 || C == 0 || C > 32)
    return SDValue();

  SDLoc dl(N);
  bool isSigned = N->getOpcode() == ISD::FP_TO_SINT;
  unsigned IntrinsicOpcode = isSigned ? Intrinsic::arm_neon_vcvtfp2fxs :
    Intrinsic::arm_neon_vcvtfp2fxu;
  SDValue FixConv = DAG.getNode(
      ISD::INTRINSIC_WO_CHAIN, dl, NumLanes == 2 ? MVT::v2i32 : MVT::v4i32,
      DAG.getConstant(IntrinsicOpcode, dl, MVT::i32), Op->getOperand(0),
      DAG.getConstant(C, dl, MVT::i32));

  if (IntBits < FloatBits)
    FixConv = DAG.getNode(ISD::TRUNCATE, dl, N->getValueType(0), FixConv);

  return FixConv;
}

/// PerformVDIVCombine - VCVT (fixed-point to floating-point, Advanced SIMD)
/// can replace combinations of VCVT (integer to floating-point) and VDIV
/// when the VDIV has a constant operand that is a power of 2.
///
/// Example (assume d17 = <float 8.000000e+00, float 8.000000e+00>):
///  vcvt.f32.s32    d16, d16
///  vdiv.f32        d16, d17, d16
/// becomes:
///  vcvt.f32.s32    d16, d16, #3
static SDValue PerformVDIVCombine(SDNode *N, SelectionDAG &DAG,
                                  const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasNEON())
    return SDValue();

  SDValue Op = N->getOperand(0);
  unsigned OpOpcode = Op.getNode()->getOpcode();
  if (!N->getValueType(0).isVector() || !N->getValueType(0).isSimple() ||
      (OpOpcode != ISD::SINT_TO_FP && OpOpcode != ISD::UINT_TO_FP))
    return SDValue();

  SDValue ConstVec = N->getOperand(1);
  if (!isa<BuildVectorSDNode>(ConstVec))
    return SDValue();

  MVT FloatTy = N->getSimpleValueType(0).getVectorElementType();
  uint32_t FloatBits = FloatTy.getSizeInBits();
  MVT IntTy = Op.getOperand(0).getSimpleValueType().getVectorElementType();
  uint32_t IntBits = IntTy.getSizeInBits();
  unsigned NumLanes = Op.getValueType().getVectorNumElements();
  if (FloatBits != 32 || IntBits > 32 || NumLanes > 4) {
    // These instructions only exist converting from i32 to f32. We can handle
    // smaller integers by generating an extra extend, but larger ones would
    // be lossy. We also can't handle more then 4 lanes, since these intructions
    // only support v2i32/v4i32 types.
    return SDValue();
  }

  BitVector UndefElements;
  BuildVectorSDNode *BV = cast<BuildVectorSDNode>(ConstVec);
  int32_t C = BV->getConstantFPSplatPow2ToLog2Int(&UndefElements, 33);
  if (C == -1 || C == 0 || C > 32)
    return SDValue();

  SDLoc dl(N);
  bool isSigned = OpOpcode == ISD::SINT_TO_FP;
  SDValue ConvInput = Op.getOperand(0);
  if (IntBits < FloatBits)
    ConvInput = DAG.getNode(isSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                            dl, NumLanes == 2 ? MVT::v2i32 : MVT::v4i32,
                            ConvInput);

  unsigned IntrinsicOpcode = isSigned ? Intrinsic::arm_neon_vcvtfxs2fp :
    Intrinsic::arm_neon_vcvtfxu2fp;
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl,
                     Op.getValueType(),
                     DAG.getConstant(IntrinsicOpcode, dl, MVT::i32),
                     ConvInput, DAG.getConstant(C, dl, MVT::i32));
}

/// Getvshiftimm - Check if this is a valid build_vector for the immediate
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
  if (! BVN || ! BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize,
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
  if (! getVShiftImm(Op, ElementBits, Cnt))
    return false;
  return (Cnt >= 0 && (isLong ? Cnt-1 : Cnt) < ElementBits);
}

/// isVShiftRImm - Check if this is a valid build_vector for the immediate
/// operand of a vector shift right operation.  For a shift opcode, the value
/// is positive, but for an intrinsic the value count must be negative. The
/// absolute value must be in the range:
///   1 <= |Value| <= ElementBits for a right shift; or
///   1 <= |Value| <= ElementBits/2 for a narrow right shift.
static bool isVShiftRImm(SDValue Op, EVT VT, bool isNarrow, bool isIntrinsic,
                         int64_t &Cnt) {
  assert(VT.isVector() && "vector shift count is not a vector type");
  int64_t ElementBits = VT.getScalarSizeInBits();
  if (! getVShiftImm(Op, ElementBits, Cnt))
    return false;
  if (!isIntrinsic)
    return (Cnt >= 1 && Cnt <= (isNarrow ? ElementBits/2 : ElementBits));
  if (Cnt >= -(isNarrow ? ElementBits/2 : ElementBits) && Cnt <= -1) {
    Cnt = -Cnt;
    return true;
  }
  return false;
}

/// PerformIntrinsicCombine - ARM-specific DAG combining for intrinsics.
static SDValue PerformIntrinsicCombine(SDNode *N, SelectionDAG &DAG) {
  unsigned IntNo = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
  switch (IntNo) {
  default:
    // Don't do anything for most intrinsics.
    break;

  // Vector shifts: check for immediate versions and lower them.
  // Note: This is done during DAG combining instead of DAG legalizing because
  // the build_vectors for 64-bit vector element shift counts are generally
  // not legal, and it is hard to see their values after they get legalized to
  // loads from a constant pool.
  case Intrinsic::arm_neon_vshifts:
  case Intrinsic::arm_neon_vshiftu:
  case Intrinsic::arm_neon_vrshifts:
  case Intrinsic::arm_neon_vrshiftu:
  case Intrinsic::arm_neon_vrshiftn:
  case Intrinsic::arm_neon_vqshifts:
  case Intrinsic::arm_neon_vqshiftu:
  case Intrinsic::arm_neon_vqshiftsu:
  case Intrinsic::arm_neon_vqshiftns:
  case Intrinsic::arm_neon_vqshiftnu:
  case Intrinsic::arm_neon_vqshiftnsu:
  case Intrinsic::arm_neon_vqrshiftns:
  case Intrinsic::arm_neon_vqrshiftnu:
  case Intrinsic::arm_neon_vqrshiftnsu: {
    EVT VT = N->getOperand(1).getValueType();
    int64_t Cnt;
    unsigned VShiftOpc = 0;

    switch (IntNo) {
    case Intrinsic::arm_neon_vshifts:
    case Intrinsic::arm_neon_vshiftu:
      if (isVShiftLImm(N->getOperand(2), VT, false, Cnt)) {
        VShiftOpc = ARMISD::VSHL;
        break;
      }
      if (isVShiftRImm(N->getOperand(2), VT, false, true, Cnt)) {
        VShiftOpc = (IntNo == Intrinsic::arm_neon_vshifts ?
                     ARMISD::VSHRs : ARMISD::VSHRu);
        break;
      }
      return SDValue();

    case Intrinsic::arm_neon_vrshifts:
    case Intrinsic::arm_neon_vrshiftu:
      if (isVShiftRImm(N->getOperand(2), VT, false, true, Cnt))
        break;
      return SDValue();

    case Intrinsic::arm_neon_vqshifts:
    case Intrinsic::arm_neon_vqshiftu:
      if (isVShiftLImm(N->getOperand(2), VT, false, Cnt))
        break;
      return SDValue();

    case Intrinsic::arm_neon_vqshiftsu:
      if (isVShiftLImm(N->getOperand(2), VT, false, Cnt))
        break;
      llvm_unreachable("invalid shift count for vqshlu intrinsic");

    case Intrinsic::arm_neon_vrshiftn:
    case Intrinsic::arm_neon_vqshiftns:
    case Intrinsic::arm_neon_vqshiftnu:
    case Intrinsic::arm_neon_vqshiftnsu:
    case Intrinsic::arm_neon_vqrshiftns:
    case Intrinsic::arm_neon_vqrshiftnu:
    case Intrinsic::arm_neon_vqrshiftnsu:
      // Narrowing shifts require an immediate right shift.
      if (isVShiftRImm(N->getOperand(2), VT, true, true, Cnt))
        break;
      llvm_unreachable("invalid shift count for narrowing vector shift "
                       "intrinsic");

    default:
      llvm_unreachable("unhandled vector shift");
    }

    switch (IntNo) {
    case Intrinsic::arm_neon_vshifts:
    case Intrinsic::arm_neon_vshiftu:
      // Opcode already set above.
      break;
    case Intrinsic::arm_neon_vrshifts:
      VShiftOpc = ARMISD::VRSHRs; break;
    case Intrinsic::arm_neon_vrshiftu:
      VShiftOpc = ARMISD::VRSHRu; break;
    case Intrinsic::arm_neon_vrshiftn:
      VShiftOpc = ARMISD::VRSHRN; break;
    case Intrinsic::arm_neon_vqshifts:
      VShiftOpc = ARMISD::VQSHLs; break;
    case Intrinsic::arm_neon_vqshiftu:
      VShiftOpc = ARMISD::VQSHLu; break;
    case Intrinsic::arm_neon_vqshiftsu:
      VShiftOpc = ARMISD::VQSHLsu; break;
    case Intrinsic::arm_neon_vqshiftns:
      VShiftOpc = ARMISD::VQSHRNs; break;
    case Intrinsic::arm_neon_vqshiftnu:
      VShiftOpc = ARMISD::VQSHRNu; break;
    case Intrinsic::arm_neon_vqshiftnsu:
      VShiftOpc = ARMISD::VQSHRNsu; break;
    case Intrinsic::arm_neon_vqrshiftns:
      VShiftOpc = ARMISD::VQRSHRNs; break;
    case Intrinsic::arm_neon_vqrshiftnu:
      VShiftOpc = ARMISD::VQRSHRNu; break;
    case Intrinsic::arm_neon_vqrshiftnsu:
      VShiftOpc = ARMISD::VQRSHRNsu; break;
    }

    SDLoc dl(N);
    return DAG.getNode(VShiftOpc, dl, N->getValueType(0),
                       N->getOperand(1), DAG.getConstant(Cnt, dl, MVT::i32));
  }

  case Intrinsic::arm_neon_vshiftins: {
    EVT VT = N->getOperand(1).getValueType();
    int64_t Cnt;
    unsigned VShiftOpc = 0;

    if (isVShiftLImm(N->getOperand(3), VT, false, Cnt))
      VShiftOpc = ARMISD::VSLI;
    else if (isVShiftRImm(N->getOperand(3), VT, false, true, Cnt))
      VShiftOpc = ARMISD::VSRI;
    else {
      llvm_unreachable("invalid shift count for vsli/vsri intrinsic");
    }

    SDLoc dl(N);
    return DAG.getNode(VShiftOpc, dl, N->getValueType(0),
                       N->getOperand(1), N->getOperand(2),
                       DAG.getConstant(Cnt, dl, MVT::i32));
  }

  case Intrinsic::arm_neon_vqrshifts:
  case Intrinsic::arm_neon_vqrshiftu:
    // No immediate versions of these to check for.
    break;
  }

  return SDValue();
}

/// PerformShiftCombine - Checks for immediate versions of vector shifts and
/// lowers them.  As with the vector shift intrinsics, this is done during DAG
/// combining instead of DAG legalizing because the build_vectors for 64-bit
/// vector element shift counts are generally not legal, and it is hard to see
/// their values after they get legalized to loads from a constant pool.
static SDValue PerformShiftCombine(SDNode *N, SelectionDAG &DAG,
                                   const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  if (N->getOpcode() == ISD::SRL && VT == MVT::i32 && ST->hasV6Ops()) {
    // Canonicalize (srl (bswap x), 16) to (rotr (bswap x), 16) if the high
    // 16-bits of x is zero. This optimizes rev + lsr 16 to rev16.
    SDValue N1 = N->getOperand(1);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(N1)) {
      SDValue N0 = N->getOperand(0);
      if (C->getZExtValue() == 16 && N0.getOpcode() == ISD::BSWAP &&
          DAG.MaskedValueIsZero(N0.getOperand(0),
                                APInt::getHighBitsSet(32, 16)))
        return DAG.getNode(ISD::ROTR, SDLoc(N), VT, N0, N1);
    }
  }

  // Nothing to be done for scalar shifts.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!VT.isVector() || !TLI.isTypeLegal(VT))
    return SDValue();

  assert(ST->hasNEON() && "unexpected vector shift");
  int64_t Cnt;

  switch (N->getOpcode()) {
  default: llvm_unreachable("unexpected shift opcode");

  case ISD::SHL:
    if (isVShiftLImm(N->getOperand(1), VT, false, Cnt)) {
      SDLoc dl(N);
      return DAG.getNode(ARMISD::VSHL, dl, VT, N->getOperand(0),
                         DAG.getConstant(Cnt, dl, MVT::i32));
    }
    break;

  case ISD::SRA:
  case ISD::SRL:
    if (isVShiftRImm(N->getOperand(1), VT, false, false, Cnt)) {
      unsigned VShiftOpc = (N->getOpcode() == ISD::SRA ?
                            ARMISD::VSHRs : ARMISD::VSHRu);
      SDLoc dl(N);
      return DAG.getNode(VShiftOpc, dl, VT, N->getOperand(0),
                         DAG.getConstant(Cnt, dl, MVT::i32));
    }
  }
  return SDValue();
}

/// PerformExtendCombine - Target-specific DAG combining for ISD::SIGN_EXTEND,
/// ISD::ZERO_EXTEND, and ISD::ANY_EXTEND.
static SDValue PerformExtendCombine(SDNode *N, SelectionDAG &DAG,
                                    const ARMSubtarget *ST) {
  SDValue N0 = N->getOperand(0);

  // Check for sign- and zero-extensions of vector extract operations of 8-
  // and 16-bit vector elements.  NEON supports these directly.  They are
  // handled during DAG combining because type legalization will promote them
  // to 32-bit types and it is messy to recognize the operations after that.
  if (ST->hasNEON() && N0.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
    SDValue Vec = N0.getOperand(0);
    SDValue Lane = N0.getOperand(1);
    EVT VT = N->getValueType(0);
    EVT EltVT = N0.getValueType();
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();

    if (VT == MVT::i32 &&
        (EltVT == MVT::i8 || EltVT == MVT::i16) &&
        TLI.isTypeLegal(Vec.getValueType()) &&
        isa<ConstantSDNode>(Lane)) {

      unsigned Opc = 0;
      switch (N->getOpcode()) {
      default: llvm_unreachable("unexpected opcode");
      case ISD::SIGN_EXTEND:
        Opc = ARMISD::VGETLANEs;
        break;
      case ISD::ZERO_EXTEND:
      case ISD::ANY_EXTEND:
        Opc = ARMISD::VGETLANEu;
        break;
      }
      return DAG.getNode(Opc, SDLoc(N), VT, Vec, Lane);
    }
  }

  return SDValue();
}

static const APInt *isPowerOf2Constant(SDValue V) {
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(V);
  if (!C)
    return nullptr;
  const APInt *CV = &C->getAPIntValue();
  return CV->isPowerOf2() ? CV : nullptr;
}

SDValue ARMTargetLowering::PerformCMOVToBFICombine(SDNode *CMOV, SelectionDAG &DAG) const {
  // If we have a CMOV, OR and AND combination such as:
  //   if (x & CN)
  //     y |= CM;
  //
  // And:
  //   * CN is a single bit;
  //   * All bits covered by CM are known zero in y
  //
  // Then we can convert this into a sequence of BFI instructions. This will
  // always be a win if CM is a single bit, will always be no worse than the
  // TST&OR sequence if CM is two bits, and for thumb will be no worse if CM is
  // three bits (due to the extra IT instruction).

  SDValue Op0 = CMOV->getOperand(0);
  SDValue Op1 = CMOV->getOperand(1);
  auto CCNode = cast<ConstantSDNode>(CMOV->getOperand(2));
  auto CC = CCNode->getAPIntValue().getLimitedValue();
  SDValue CmpZ = CMOV->getOperand(4);

  // The compare must be against zero.
  if (!isNullConstant(CmpZ->getOperand(1)))
    return SDValue();

  assert(CmpZ->getOpcode() == ARMISD::CMPZ);
  SDValue And = CmpZ->getOperand(0);
  if (And->getOpcode() != ISD::AND)
    return SDValue();
  const APInt *AndC = isPowerOf2Constant(And->getOperand(1));
  if (!AndC)
    return SDValue();
  SDValue X = And->getOperand(0);

  if (CC == ARMCC::EQ) {
    // We're performing an "equal to zero" compare. Swap the operands so we
    // canonicalize on a "not equal to zero" compare.
    std::swap(Op0, Op1);
  } else {
    assert(CC == ARMCC::NE && "How can a CMPZ node not be EQ or NE?");
  }

  if (Op1->getOpcode() != ISD::OR)
    return SDValue();

  ConstantSDNode *OrC = dyn_cast<ConstantSDNode>(Op1->getOperand(1));
  if (!OrC)
    return SDValue();
  SDValue Y = Op1->getOperand(0);

  if (Op0 != Y)
    return SDValue();

  // Now, is it profitable to continue?
  APInt OrCI = OrC->getAPIntValue();
  unsigned Heuristic = Subtarget->isThumb() ? 3 : 2;
  if (OrCI.countPopulation() > Heuristic)
    return SDValue();

  // Lastly, can we determine that the bits defined by OrCI
  // are zero in Y?
  KnownBits Known = DAG.computeKnownBits(Y);
  if ((OrCI & Known.Zero) != OrCI)
    return SDValue();

  // OK, we can do the combine.
  SDValue V = Y;
  SDLoc dl(X);
  EVT VT = X.getValueType();
  unsigned BitInX = AndC->logBase2();

  if (BitInX != 0) {
    // We must shift X first.
    X = DAG.getNode(ISD::SRL, dl, VT, X,
                    DAG.getConstant(BitInX, dl, VT));
  }

  for (unsigned BitInY = 0, NumActiveBits = OrCI.getActiveBits();
       BitInY < NumActiveBits; ++BitInY) {
    if (OrCI[BitInY] == 0)
      continue;
    APInt Mask(VT.getSizeInBits(), 0);
    Mask.setBit(BitInY);
    V = DAG.getNode(ARMISD::BFI, dl, VT, V, X,
                    // Confusingly, the operand is an *inverted* mask.
                    DAG.getConstant(~Mask, dl, VT));
  }

  return V;
}

/// PerformBRCONDCombine - Target-specific DAG combining for ARMISD::BRCOND.
SDValue
ARMTargetLowering::PerformBRCONDCombine(SDNode *N, SelectionDAG &DAG) const {
  SDValue Cmp = N->getOperand(4);
  if (Cmp.getOpcode() != ARMISD::CMPZ)
    // Only looking at NE cases.
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  SDValue Chain = N->getOperand(0);
  SDValue BB = N->getOperand(1);
  SDValue ARMcc = N->getOperand(2);
  ARMCC::CondCodes CC =
    (ARMCC::CondCodes)cast<ConstantSDNode>(ARMcc)->getZExtValue();

  // (brcond Chain BB ne CPSR (cmpz (and (cmov 0 1 CC CPSR Cmp) 1) 0))
  // -> (brcond Chain BB CC CPSR Cmp)
  if (CC == ARMCC::NE && LHS.getOpcode() == ISD::AND && LHS->hasOneUse() &&
      LHS->getOperand(0)->getOpcode() == ARMISD::CMOV &&
      LHS->getOperand(0)->hasOneUse()) {
    auto *LHS00C = dyn_cast<ConstantSDNode>(LHS->getOperand(0)->getOperand(0));
    auto *LHS01C = dyn_cast<ConstantSDNode>(LHS->getOperand(0)->getOperand(1));
    auto *LHS1C = dyn_cast<ConstantSDNode>(LHS->getOperand(1));
    auto *RHSC = dyn_cast<ConstantSDNode>(RHS);
    if ((LHS00C && LHS00C->getZExtValue() == 0) &&
        (LHS01C && LHS01C->getZExtValue() == 1) &&
        (LHS1C && LHS1C->getZExtValue() == 1) &&
        (RHSC && RHSC->getZExtValue() == 0)) {
      return DAG.getNode(
          ARMISD::BRCOND, dl, VT, Chain, BB, LHS->getOperand(0)->getOperand(2),
          LHS->getOperand(0)->getOperand(3), LHS->getOperand(0)->getOperand(4));
    }
  }

  return SDValue();
}

/// PerformCMOVCombine - Target-specific DAG combining for ARMISD::CMOV.
SDValue
ARMTargetLowering::PerformCMOVCombine(SDNode *N, SelectionDAG &DAG) const {
  SDValue Cmp = N->getOperand(4);
  if (Cmp.getOpcode() != ARMISD::CMPZ)
    // Only looking at EQ and NE cases.
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  SDValue FalseVal = N->getOperand(0);
  SDValue TrueVal = N->getOperand(1);
  SDValue ARMcc = N->getOperand(2);
  ARMCC::CondCodes CC =
    (ARMCC::CondCodes)cast<ConstantSDNode>(ARMcc)->getZExtValue();

  // BFI is only available on V6T2+.
  if (!Subtarget->isThumb1Only() && Subtarget->hasV6T2Ops()) {
    SDValue R = PerformCMOVToBFICombine(N, DAG);
    if (R)
      return R;
  }

  // Simplify
  //   mov     r1, r0
  //   cmp     r1, x
  //   mov     r0, y
  //   moveq   r0, x
  // to
  //   cmp     r0, x
  //   movne   r0, y
  //
  //   mov     r1, r0
  //   cmp     r1, x
  //   mov     r0, x
  //   movne   r0, y
  // to
  //   cmp     r0, x
  //   movne   r0, y
  /// FIXME: Turn this into a target neutral optimization?
  SDValue Res;
  if (CC == ARMCC::NE && FalseVal == RHS && FalseVal != LHS) {
    Res = DAG.getNode(ARMISD::CMOV, dl, VT, LHS, TrueVal, ARMcc,
                      N->getOperand(3), Cmp);
  } else if (CC == ARMCC::EQ && TrueVal == RHS) {
    SDValue ARMcc;
    SDValue NewCmp = getARMCmp(LHS, RHS, ISD::SETNE, ARMcc, DAG, dl);
    Res = DAG.getNode(ARMISD::CMOV, dl, VT, LHS, FalseVal, ARMcc,
                      N->getOperand(3), NewCmp);
  }

  // (cmov F T ne CPSR (cmpz (cmov 0 1 CC CPSR Cmp) 0))
  // -> (cmov F T CC CPSR Cmp)
  if (CC == ARMCC::NE && LHS.getOpcode() == ARMISD::CMOV && LHS->hasOneUse()) {
    auto *LHS0C = dyn_cast<ConstantSDNode>(LHS->getOperand(0));
    auto *LHS1C = dyn_cast<ConstantSDNode>(LHS->getOperand(1));
    auto *RHSC = dyn_cast<ConstantSDNode>(RHS);
    if ((LHS0C && LHS0C->getZExtValue() == 0) &&
        (LHS1C && LHS1C->getZExtValue() == 1) &&
        (RHSC && RHSC->getZExtValue() == 0)) {
      return DAG.getNode(ARMISD::CMOV, dl, VT, FalseVal, TrueVal,
                         LHS->getOperand(2), LHS->getOperand(3),
                         LHS->getOperand(4));
    }
  }

  if (!VT.isInteger())
      return SDValue();

  // Materialize a boolean comparison for integers so we can avoid branching.
  if (isNullConstant(FalseVal)) {
    if (CC == ARMCC::EQ && isOneConstant(TrueVal)) {
      if (!Subtarget->isThumb1Only() && Subtarget->hasV5TOps()) {
        // If x == y then x - y == 0 and ARM's CLZ will return 32, shifting it
        // right 5 bits will make that 32 be 1, otherwise it will be 0.
        // CMOV 0, 1, ==, (CMPZ x, y) -> SRL (CTLZ (SUB x, y)), 5
        SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, LHS, RHS);
        Res = DAG.getNode(ISD::SRL, dl, VT, DAG.getNode(ISD::CTLZ, dl, VT, Sub),
                          DAG.getConstant(5, dl, MVT::i32));
      } else {
        // CMOV 0, 1, ==, (CMPZ x, y) ->
        //     (ADDCARRY (SUB x, y), t:0, t:1)
        // where t = (SUBCARRY 0, (SUB x, y), 0)
        //
        // The SUBCARRY computes 0 - (x - y) and this will give a borrow when
        // x != y. In other words, a carry C == 1 when x == y, C == 0
        // otherwise.
        // The final ADDCARRY computes
        //     x - y + (0 - (x - y)) + C == C
        SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, LHS, RHS);
        SDVTList VTs = DAG.getVTList(VT, MVT::i32);
        SDValue Neg = DAG.getNode(ISD::USUBO, dl, VTs, FalseVal, Sub);
        // ISD::SUBCARRY returns a borrow but we want the carry here
        // actually.
        SDValue Carry =
            DAG.getNode(ISD::SUB, dl, MVT::i32,
                        DAG.getConstant(1, dl, MVT::i32), Neg.getValue(1));
        Res = DAG.getNode(ISD::ADDCARRY, dl, VTs, Sub, Neg, Carry);
      }
    } else if (CC == ARMCC::NE && !isNullConstant(RHS) &&
               (!Subtarget->isThumb1Only() || isPowerOf2Constant(TrueVal))) {
      // This seems pointless but will allow us to combine it further below.
      // CMOV 0, z, !=, (CMPZ x, y) -> CMOV (SUBS x, y), z, !=, (SUBS x, y):1
      SDValue Sub =
          DAG.getNode(ARMISD::SUBS, dl, DAG.getVTList(VT, MVT::i32), LHS, RHS);
      SDValue CPSRGlue = DAG.getCopyToReg(DAG.getEntryNode(), dl, ARM::CPSR,
                                          Sub.getValue(1), SDValue());
      Res = DAG.getNode(ARMISD::CMOV, dl, VT, Sub, TrueVal, ARMcc,
                        N->getOperand(3), CPSRGlue.getValue(1));
      FalseVal = Sub;
    }
  } else if (isNullConstant(TrueVal)) {
    if (CC == ARMCC::EQ && !isNullConstant(RHS) &&
        (!Subtarget->isThumb1Only() || isPowerOf2Constant(FalseVal))) {
      // This seems pointless but will allow us to combine it further below
      // Note that we change == for != as this is the dual for the case above.
      // CMOV z, 0, ==, (CMPZ x, y) -> CMOV (SUBS x, y), z, !=, (SUBS x, y):1
      SDValue Sub =
          DAG.getNode(ARMISD::SUBS, dl, DAG.getVTList(VT, MVT::i32), LHS, RHS);
      SDValue CPSRGlue = DAG.getCopyToReg(DAG.getEntryNode(), dl, ARM::CPSR,
                                          Sub.getValue(1), SDValue());
      Res = DAG.getNode(ARMISD::CMOV, dl, VT, Sub, FalseVal,
                        DAG.getConstant(ARMCC::NE, dl, MVT::i32),
                        N->getOperand(3), CPSRGlue.getValue(1));
      FalseVal = Sub;
    }
  }

  // On Thumb1, the DAG above may be further combined if z is a power of 2
  // (z == 2 ^ K).
  // CMOV (SUBS x, y), z, !=, (SUBS x, y):1 ->
  //       merge t3, t4
  // where t1 = (SUBCARRY (SUB x, y), z, 0)
  //       t2 = (SUBCARRY (SUB x, y), t1:0, t1:1)
  //       t3 = if K != 0 then (SHL t2:0, K) else t2:0
  //       t4 = (SUB 1, t2:1)   [ we want a carry, not a borrow ]
  const APInt *TrueConst;
  if (Subtarget->isThumb1Only() && CC == ARMCC::NE &&
      (FalseVal.getOpcode() == ARMISD::SUBS) &&
      (FalseVal.getOperand(0) == LHS) && (FalseVal.getOperand(1) == RHS) &&
      (TrueConst = isPowerOf2Constant(TrueVal))) {
    SDVTList VTs = DAG.getVTList(VT, MVT::i32);
    unsigned ShiftAmount = TrueConst->logBase2();
    if (ShiftAmount)
      TrueVal = DAG.getConstant(1, dl, VT);
    SDValue Subc = DAG.getNode(ISD::USUBO, dl, VTs, FalseVal, TrueVal);
    Res = DAG.getNode(ISD::SUBCARRY, dl, VTs, FalseVal, Subc, Subc.getValue(1));
    // Make it a carry, not a borrow.
    SDValue Carry = DAG.getNode(
        ISD::SUB, dl, VT, DAG.getConstant(1, dl, MVT::i32), Res.getValue(1));
    Res = DAG.getNode(ISD::MERGE_VALUES, dl, VTs, Res, Carry);

    if (ShiftAmount)
      Res = DAG.getNode(ISD::SHL, dl, VT, Res,
                        DAG.getConstant(ShiftAmount, dl, MVT::i32));
  }

  if (Res.getNode()) {
    KnownBits Known = DAG.computeKnownBits(SDValue(N,0));
    // Capture demanded bits information that would be otherwise lost.
    if (Known.Zero == 0xfffffffe)
      Res = DAG.getNode(ISD::AssertZext, dl, MVT::i32, Res,
                        DAG.getValueType(MVT::i1));
    else if (Known.Zero == 0xffffff00)
      Res = DAG.getNode(ISD::AssertZext, dl, MVT::i32, Res,
                        DAG.getValueType(MVT::i8));
    else if (Known.Zero == 0xffff0000)
      Res = DAG.getNode(ISD::AssertZext, dl, MVT::i32, Res,
                        DAG.getValueType(MVT::i16));
  }

  return Res;
}

SDValue ARMTargetLowering::PerformDAGCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  default: break;
  case ARMISD::ADDE:    return PerformADDECombine(N, DCI, Subtarget);
  case ARMISD::UMLAL:   return PerformUMLALCombine(N, DCI.DAG, Subtarget);
  case ISD::ADD:        return PerformADDCombine(N, DCI, Subtarget);
  case ISD::SUB:        return PerformSUBCombine(N, DCI);
  case ISD::MUL:        return PerformMULCombine(N, DCI, Subtarget);
  case ISD::OR:         return PerformORCombine(N, DCI, Subtarget);
  case ISD::XOR:        return PerformXORCombine(N, DCI, Subtarget);
  case ISD::AND:        return PerformANDCombine(N, DCI, Subtarget);
  case ARMISD::ADDC:
  case ARMISD::SUBC:    return PerformAddcSubcCombine(N, DCI, Subtarget);
  case ARMISD::SUBE:    return PerformAddeSubeCombine(N, DCI, Subtarget);
  case ARMISD::BFI:     return PerformBFICombine(N, DCI);
  case ARMISD::VMOVRRD: return PerformVMOVRRDCombine(N, DCI, Subtarget);
  case ARMISD::VMOVDRR: return PerformVMOVDRRCombine(N, DCI.DAG);
  case ISD::STORE:      return PerformSTORECombine(N, DCI);
  case ISD::BUILD_VECTOR: return PerformBUILD_VECTORCombine(N, DCI, Subtarget);
  case ISD::INSERT_VECTOR_ELT: return PerformInsertEltCombine(N, DCI);
  case ISD::VECTOR_SHUFFLE: return PerformVECTOR_SHUFFLECombine(N, DCI.DAG);
  case ARMISD::VDUPLANE: return PerformVDUPLANECombine(N, DCI);
  case ARMISD::VDUP: return PerformVDUPCombine(N, DCI);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return PerformVCVTCombine(N, DCI.DAG, Subtarget);
  case ISD::FDIV:
    return PerformVDIVCombine(N, DCI.DAG, Subtarget);
  case ISD::INTRINSIC_WO_CHAIN: return PerformIntrinsicCombine(N, DCI.DAG);
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:        return PerformShiftCombine(N, DCI.DAG, Subtarget);
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND: return PerformExtendCombine(N, DCI.DAG, Subtarget);
  case ARMISD::CMOV: return PerformCMOVCombine(N, DCI.DAG);
  case ARMISD::BRCOND: return PerformBRCONDCombine(N, DCI.DAG);
  case ISD::LOAD:       return PerformLOADCombine(N, DCI);
  case ARMISD::VLD1DUP:
  case ARMISD::VLD2DUP:
  case ARMISD::VLD3DUP:
  case ARMISD::VLD4DUP:
    return PerformVLDCombine(N, DCI);
  case ARMISD::BUILD_VECTOR:
    return PerformARMBUILD_VECTORCombine(N, DCI);
  case ARMISD::SMULWB: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getLowBitsSet(BitWidth, 16);
    if (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI))
      return SDValue();
    break;
  }
  case ARMISD::SMULWT: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getHighBitsSet(BitWidth, 16);
    if (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI))
      return SDValue();
    break;
  }
  case ARMISD::SMLALBB: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getLowBitsSet(BitWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), DemandedMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI)))
      return SDValue();
    break;
  }
  case ARMISD::SMLALBT: {
    unsigned LowWidth = N->getOperand(0).getValueType().getSizeInBits();
    APInt LowMask = APInt::getLowBitsSet(LowWidth, 16);
    unsigned HighWidth = N->getOperand(1).getValueType().getSizeInBits();
    APInt HighMask = APInt::getHighBitsSet(HighWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), LowMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), HighMask, DCI)))
      return SDValue();
    break;
  }
  case ARMISD::SMLALTB: {
    unsigned HighWidth = N->getOperand(0).getValueType().getSizeInBits();
    APInt HighMask = APInt::getHighBitsSet(HighWidth, 16);
    unsigned LowWidth = N->getOperand(1).getValueType().getSizeInBits();
    APInt LowMask = APInt::getLowBitsSet(LowWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), HighMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), LowMask, DCI)))
      return SDValue();
    break;
  }
  case ARMISD::SMLALTT: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getHighBitsSet(BitWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), DemandedMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI)))
      return SDValue();
    break;
  }
  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN:
    switch (cast<ConstantSDNode>(N->getOperand(1))->getZExtValue()) {
    case Intrinsic::arm_neon_vld1:
    case Intrinsic::arm_neon_vld1x2:
    case Intrinsic::arm_neon_vld1x3:
    case Intrinsic::arm_neon_vld1x4:
    case Intrinsic::arm_neon_vld2:
    case Intrinsic::arm_neon_vld3:
    case Intrinsic::arm_neon_vld4:
    case Intrinsic::arm_neon_vld2lane:
    case Intrinsic::arm_neon_vld3lane:
    case Intrinsic::arm_neon_vld4lane:
    case Intrinsic::arm_neon_vld2dup:
    case Intrinsic::arm_neon_vld3dup:
    case Intrinsic::arm_neon_vld4dup:
    case Intrinsic::arm_neon_vst1:
    case Intrinsic::arm_neon_vst1x2:
    case Intrinsic::arm_neon_vst1x3:
    case Intrinsic::arm_neon_vst1x4:
    case Intrinsic::arm_neon_vst2:
    case Intrinsic::arm_neon_vst3:
    case Intrinsic::arm_neon_vst4:
    case Intrinsic::arm_neon_vst2lane:
    case Intrinsic::arm_neon_vst3lane:
    case Intrinsic::arm_neon_vst4lane:
      return PerformVLDCombine(N, DCI);
    default: break;
    }
    break;
  }
  return SDValue();
}

bool ARMTargetLowering::isDesirableToTransformToIntegerOp(unsigned Opc,
                                                          EVT VT) const {
  return (VT == MVT::f32) && (Opc == ISD::LOAD || Opc == ISD::STORE);
}

bool ARMTargetLowering::allowsMisalignedMemoryAccesses(EVT VT,
                                                       unsigned,
                                                       unsigned,
                                                       bool *Fast) const {
  // Depends what it gets converted into if the type is weird.
  if (!VT.isSimple())
    return false;

  // The AllowsUnaliged flag models the SCTLR.A setting in ARM cpus
  bool AllowsUnaligned = Subtarget->allowsUnalignedMem();

  switch (VT.getSimpleVT().SimpleTy) {
  default:
    return false;
  case MVT::i8:
  case MVT::i16:
  case MVT::i32: {
    // Unaligned access can use (for example) LRDB, LRDH, LDR
    if (AllowsUnaligned) {
      if (Fast)
        *Fast = Subtarget->hasV7Ops();
      return true;
    }
    return false;
  }
  case MVT::f64:
  case MVT::v2f64: {
    // For any little-endian targets with neon, we can support unaligned ld/st
    // of D and Q (e.g. {D0,D1}) registers by using vld1.i8/vst1.i8.
    // A big-endian target may also explicitly support unaligned accesses
    if (Subtarget->hasNEON() && (AllowsUnaligned || Subtarget->isLittle())) {
      if (Fast)
        *Fast = true;
      return true;
    }
    return false;
  }
  }
}

static bool memOpAlign(unsigned DstAlign, unsigned SrcAlign,
                       unsigned AlignCheck) {
  return ((SrcAlign == 0 || SrcAlign % AlignCheck == 0) &&
          (DstAlign == 0 || DstAlign % AlignCheck == 0));
}

EVT ARMTargetLowering::getOptimalMemOpType(uint64_t Size,
                                           unsigned DstAlign, unsigned SrcAlign,
                                           bool IsMemset, bool ZeroMemset,
                                           bool MemcpyStrSrc,
                                           MachineFunction &MF) const {
  const Function &F = MF.getFunction();

  // See if we can use NEON instructions for this...
  if ((!IsMemset || ZeroMemset) && Subtarget->hasNEON() &&
      !F.hasFnAttribute(Attribute::NoImplicitFloat)) {
    bool Fast;
    if (Size >= 16 &&
        (memOpAlign(SrcAlign, DstAlign, 16) ||
         (allowsMisalignedMemoryAccesses(MVT::v2f64, 0, 1, &Fast) && Fast))) {
      return MVT::v2f64;
    } else if (Size >= 8 &&
               (memOpAlign(SrcAlign, DstAlign, 8) ||
                (allowsMisalignedMemoryAccesses(MVT::f64, 0, 1, &Fast) &&
                 Fast))) {
      return MVT::f64;
    }
  }

  // Let the target-independent logic figure it out.
  return MVT::Other;
}

// 64-bit integers are split into their high and low parts and held in two
// different registers, so the trunc is free since the low register can just
// be used.
bool ARMTargetLowering::isTruncateFree(Type *SrcTy, Type *DstTy) const {
  if (!SrcTy->isIntegerTy() || !DstTy->isIntegerTy())
    return false;
  unsigned SrcBits = SrcTy->getPrimitiveSizeInBits();
  unsigned DestBits = DstTy->getPrimitiveSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool ARMTargetLowering::isTruncateFree(EVT SrcVT, EVT DstVT) const {
  if (SrcVT.isVector() || DstVT.isVector() || !SrcVT.isInteger() ||
      !DstVT.isInteger())
    return false;
  unsigned SrcBits = SrcVT.getSizeInBits();
  unsigned DestBits = DstVT.getSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool ARMTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  if (Val.getOpcode() != ISD::LOAD)
    return false;

  EVT VT1 = Val.getValueType();
  if (!VT1.isSimple() || !VT1.isInteger() ||
      !VT2.isSimple() || !VT2.isInteger())
    return false;

  switch (VT1.getSimpleVT().SimpleTy) {
  default: break;
  case MVT::i1:
  case MVT::i8:
  case MVT::i16:
    // 8-bit and 16-bit loads implicitly zero-extend to 32-bits.
    return true;
  }

  return false;
}

bool ARMTargetLowering::isFNegFree(EVT VT) const {
  if (!VT.isSimple())
    return false;

  // There are quite a few FP16 instructions (e.g. VNMLA, VNMLS, etc.) that
  // negate values directly (fneg is free). So, we don't want to let the DAG
  // combiner rewrite fneg into xors and some other instructions.  For f16 and
  // FullFP16 argument passing, some bitcast nodes may be introduced,
  // triggering this DAG combine rewrite, so we are avoiding that with this.
  switch (VT.getSimpleVT().SimpleTy) {
  default: break;
  case MVT::f16:
    return Subtarget->hasFullFP16();
  }

  return false;
}

bool ARMTargetLowering::isVectorLoadExtDesirable(SDValue ExtVal) const {
  EVT VT = ExtVal.getValueType();

  if (!isTypeLegal(VT))
    return false;

  // Don't create a loadext if we can fold the extension into a wide/long
  // instruction.
  // If there's more than one user instruction, the loadext is desirable no
  // matter what.  There can be two uses by the same instruction.
  if (ExtVal->use_empty() ||
      !ExtVal->use_begin()->isOnlyUserOf(ExtVal.getNode()))
    return true;

  SDNode *U = *ExtVal->use_begin();
  if ((U->getOpcode() == ISD::ADD || U->getOpcode() == ISD::SUB ||
       U->getOpcode() == ISD::SHL || U->getOpcode() == ARMISD::VSHL))
    return false;

  return true;
}

bool ARMTargetLowering::allowTruncateForTailCall(Type *Ty1, Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;

  if (!isTypeLegal(EVT::getEVT(Ty1)))
    return false;

  assert(Ty1->getPrimitiveSizeInBits() <= 64 && "i128 is probably not a noop");

  // Assuming the caller doesn't have a zeroext or signext return parameter,
  // truncation all the way down to i1 is valid.
  return true;
}

int ARMTargetLowering::getScalingFactorCost(const DataLayout &DL,
                                                const AddrMode &AM, Type *Ty,
                                                unsigned AS) const {
  if (isLegalAddressingMode(DL, AM, Ty, AS)) {
    if (Subtarget->hasFPAO())
      return AM.Scale < 0 ? 1 : 0; // positive offsets execute faster
    return 0;
  }
  return -1;
}

static bool isLegalT1AddressImmediate(int64_t V, EVT VT) {
  if (V < 0)
    return false;

  unsigned Scale = 1;
  switch (VT.getSimpleVT().SimpleTy) {
  default: return false;
  case MVT::i1:
  case MVT::i8:
    // Scale == 1;
    break;
  case MVT::i16:
    // Scale == 2;
    Scale = 2;
    break;
  case MVT::i32:
    // Scale == 4;
    Scale = 4;
    break;
  }

  if ((V & (Scale - 1)) != 0)
    return false;
  V /= Scale;
  return V == (V & ((1LL << 5) - 1));
}

static bool isLegalT2AddressImmediate(int64_t V, EVT VT,
                                      const ARMSubtarget *Subtarget) {
  bool isNeg = false;
  if (V < 0) {
    isNeg = true;
    V = - V;
  }

  switch (VT.getSimpleVT().SimpleTy) {
  default: return false;
  case MVT::i1:
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
    // + imm12 or - imm8
    if (isNeg)
      return V == (V & ((1LL << 8) - 1));
    return V == (V & ((1LL << 12) - 1));
  case MVT::f32:
  case MVT::f64:
    // Same as ARM mode. FIXME: NEON?
    if (!Subtarget->hasVFP2())
      return false;
    if ((V & 3) != 0)
      return false;
    V >>= 2;
    return V == (V & ((1LL << 8) - 1));
  }
}

/// isLegalAddressImmediate - Return true if the integer value can be used
/// as the offset of the target addressing mode for load / store of the
/// given type.
static bool isLegalAddressImmediate(int64_t V, EVT VT,
                                    const ARMSubtarget *Subtarget) {
  if (V == 0)
    return true;

  if (!VT.isSimple())
    return false;

  if (Subtarget->isThumb1Only())
    return isLegalT1AddressImmediate(V, VT);
  else if (Subtarget->isThumb2())
    return isLegalT2AddressImmediate(V, VT, Subtarget);

  // ARM mode.
  if (V < 0)
    V = - V;
  switch (VT.getSimpleVT().SimpleTy) {
  default: return false;
  case MVT::i1:
  case MVT::i8:
  case MVT::i32:
    // +- imm12
    return V == (V & ((1LL << 12) - 1));
  case MVT::i16:
    // +- imm8
    return V == (V & ((1LL << 8) - 1));
  case MVT::f32:
  case MVT::f64:
    if (!Subtarget->hasVFP2()) // FIXME: NEON?
      return false;
    if ((V & 3) != 0)
      return false;
    V >>= 2;
    return V == (V & ((1LL << 8) - 1));
  }
}

bool ARMTargetLowering::isLegalT2ScaledAddressingMode(const AddrMode &AM,
                                                      EVT VT) const {
  int Scale = AM.Scale;
  if (Scale < 0)
    return false;

  switch (VT.getSimpleVT().SimpleTy) {
  default: return false;
  case MVT::i1:
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
    if (Scale == 1)
      return true;
    // r + r << imm
    Scale = Scale & ~1;
    return Scale == 2 || Scale == 4 || Scale == 8;
  case MVT::i64:
    // FIXME: What are we trying to model here? ldrd doesn't have an r + r
    // version in Thumb mode.
    // r + r
    if (Scale == 1)
      return true;
    // r * 2 (this can be lowered to r + r).
    if (!AM.HasBaseReg && Scale == 2)
      return true;
    return false;
  case MVT::isVoid:
    // Note, we allow "void" uses (basically, uses that aren't loads or
    // stores), because arm allows folding a scale into many arithmetic
    // operations.  This should be made more precise and revisited later.

    // Allow r << imm, but the imm has to be a multiple of two.
    if (Scale & 1) return false;
    return isPowerOf2_32(Scale);
  }
}

bool ARMTargetLowering::isLegalT1ScaledAddressingMode(const AddrMode &AM,
                                                      EVT VT) const {
  const int Scale = AM.Scale;

  // Negative scales are not supported in Thumb1.
  if (Scale < 0)
    return false;

  // Thumb1 addressing modes do not support register scaling excepting the
  // following cases:
  // 1. Scale == 1 means no scaling.
  // 2. Scale == 2 this can be lowered to r + r if there is no base register.
  return (Scale == 1) || (!AM.HasBaseReg && Scale == 2);
}

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
bool ARMTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                              const AddrMode &AM, Type *Ty,
                                              unsigned AS, Instruction *I) const {
  EVT VT = getValueType(DL, Ty, true);
  if (!isLegalAddressImmediate(AM.BaseOffs, VT, Subtarget))
    return false;

  // Can never fold addr of global into load/store.
  if (AM.BaseGV)
    return false;

  switch (AM.Scale) {
  case 0:  // no scale reg, must be "r+i" or "r", or "i".
    break;
  default:
    // ARM doesn't support any R+R*scale+imm addr modes.
    if (AM.BaseOffs)
      return false;

    if (!VT.isSimple())
      return false;

    if (Subtarget->isThumb1Only())
      return isLegalT1ScaledAddressingMode(AM, VT);

    if (Subtarget->isThumb2())
      return isLegalT2ScaledAddressingMode(AM, VT);

    int Scale = AM.Scale;
    switch (VT.getSimpleVT().SimpleTy) {
    default: return false;
    case MVT::i1:
    case MVT::i8:
    case MVT::i32:
      if (Scale < 0) Scale = -Scale;
      if (Scale == 1)
        return true;
      // r + r << imm
      return isPowerOf2_32(Scale & ~1);
    case MVT::i16:
    case MVT::i64:
      // r +/- r
      if (Scale == 1 || (AM.HasBaseReg && Scale == -1))
        return true;
      // r * 2 (this can be lowered to r + r).
      if (!AM.HasBaseReg && Scale == 2)
        return true;
      return false;

    case MVT::isVoid:
      // Note, we allow "void" uses (basically, uses that aren't loads or
      // stores), because arm allows folding a scale into many arithmetic
      // operations.  This should be made more precise and revisited later.

      // Allow r << imm, but the imm has to be a multiple of two.
      if (Scale & 1) return false;
      return isPowerOf2_32(Scale);
    }
  }
  return true;
}

/// isLegalICmpImmediate - Return true if the specified immediate is legal
/// icmp immediate, that is the target has icmp instructions which can compare
/// a register against the immediate without having to materialize the
/// immediate into a register.
bool ARMTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  // Thumb2 and ARM modes can use cmn for negative immediates.
  if (!Subtarget->isThumb())
    return ARM_AM::getSOImmVal((uint32_t)Imm) != -1 ||
           ARM_AM::getSOImmVal(-(uint32_t)Imm) != -1;
  if (Subtarget->isThumb2())
    return ARM_AM::getT2SOImmVal((uint32_t)Imm) != -1 ||
           ARM_AM::getT2SOImmVal(-(uint32_t)Imm) != -1;
  // Thumb1 doesn't have cmn, and only 8-bit immediates.
  return Imm >= 0 && Imm <= 255;
}

/// isLegalAddImmediate - Return true if the specified immediate is a legal add
/// *or sub* immediate, that is the target has add or sub instructions which can
/// add a register with the immediate without having to materialize the
/// immediate into a register.
bool ARMTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  // Same encoding for add/sub, just flip the sign.
  int64_t AbsImm = std::abs(Imm);
  if (!Subtarget->isThumb())
    return ARM_AM::getSOImmVal(AbsImm) != -1;
  if (Subtarget->isThumb2())
    return ARM_AM::getT2SOImmVal(AbsImm) != -1;
  // Thumb1 only has 8-bit unsigned immediate.
  return AbsImm >= 0 && AbsImm <= 255;
}

static bool getARMIndexedAddressParts(SDNode *Ptr, EVT VT,
                                      bool isSEXTLoad, SDValue &Base,
                                      SDValue &Offset, bool &isInc,
                                      SelectionDAG &DAG) {
  if (Ptr->getOpcode() != ISD::ADD && Ptr->getOpcode() != ISD::SUB)
    return false;

  if (VT == MVT::i16 || ((VT == MVT::i8 || VT == MVT::i1) && isSEXTLoad)) {
    // AddressingMode 3
    Base = Ptr->getOperand(0);
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC < 0 && RHSC > -256) {
        assert(Ptr->getOpcode() == ISD::ADD);
        isInc = false;
        Offset = DAG.getConstant(-RHSC, SDLoc(Ptr), RHS->getValueType(0));
        return true;
      }
    }
    isInc = (Ptr->getOpcode() == ISD::ADD);
    Offset = Ptr->getOperand(1);
    return true;
  } else if (VT == MVT::i32 || VT == MVT::i8 || VT == MVT::i1) {
    // AddressingMode 2
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC < 0 && RHSC > -0x1000) {
        assert(Ptr->getOpcode() == ISD::ADD);
        isInc = false;
        Offset = DAG.getConstant(-RHSC, SDLoc(Ptr), RHS->getValueType(0));
        Base = Ptr->getOperand(0);
        return true;
      }
    }

    if (Ptr->getOpcode() == ISD::ADD) {
      isInc = true;
      ARM_AM::ShiftOpc ShOpcVal=
        ARM_AM::getShiftOpcForNode(Ptr->getOperand(0).getOpcode());
      if (ShOpcVal != ARM_AM::no_shift) {
        Base = Ptr->getOperand(1);
        Offset = Ptr->getOperand(0);
      } else {
        Base = Ptr->getOperand(0);
        Offset = Ptr->getOperand(1);
      }
      return true;
    }

    isInc = (Ptr->getOpcode() == ISD::ADD);
    Base = Ptr->getOperand(0);
    Offset = Ptr->getOperand(1);
    return true;
  }

  // FIXME: Use VLDM / VSTM to emulate indexed FP load / store.
  return false;
}

static bool getT2IndexedAddressParts(SDNode *Ptr, EVT VT,
                                     bool isSEXTLoad, SDValue &Base,
                                     SDValue &Offset, bool &isInc,
                                     SelectionDAG &DAG) {
  if (Ptr->getOpcode() != ISD::ADD && Ptr->getOpcode() != ISD::SUB)
    return false;

  Base = Ptr->getOperand(0);
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
    int RHSC = (int)RHS->getZExtValue();
    if (RHSC < 0 && RHSC > -0x100) { // 8 bits.
      assert(Ptr->getOpcode() == ISD::ADD);
      isInc = false;
      Offset = DAG.getConstant(-RHSC, SDLoc(Ptr), RHS->getValueType(0));
      return true;
    } else if (RHSC > 0 && RHSC < 0x100) { // 8 bit, no zero.
      isInc = Ptr->getOpcode() == ISD::ADD;
      Offset = DAG.getConstant(RHSC, SDLoc(Ptr), RHS->getValueType(0));
      return true;
    }
  }

  return false;
}

/// getPreIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if the node's address
/// can be legally represented as pre-indexed load / store address.
bool
ARMTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                             SDValue &Offset,
                                             ISD::MemIndexedMode &AM,
                                             SelectionDAG &DAG) const {
  if (Subtarget->isThumb1Only())
    return false;

  EVT VT;
  SDValue Ptr;
  bool isSEXTLoad = false;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    Ptr = LD->getBasePtr();
    VT  = LD->getMemoryVT();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    Ptr = ST->getBasePtr();
    VT  = ST->getMemoryVT();
  } else
    return false;

  bool isInc;
  bool isLegal = false;
  if (Subtarget->isThumb2())
    isLegal = getT2IndexedAddressParts(Ptr.getNode(), VT, isSEXTLoad, Base,
                                       Offset, isInc, DAG);
  else
    isLegal = getARMIndexedAddressParts(Ptr.getNode(), VT, isSEXTLoad, Base,
                                        Offset, isInc, DAG);
  if (!isLegal)
    return false;

  AM = isInc ? ISD::PRE_INC : ISD::PRE_DEC;
  return true;
}

/// getPostIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if this node can be
/// combined with a load / store to form a post-indexed load / store.
bool ARMTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                   SDValue &Base,
                                                   SDValue &Offset,
                                                   ISD::MemIndexedMode &AM,
                                                   SelectionDAG &DAG) const {
  EVT VT;
  SDValue Ptr;
  bool isSEXTLoad = false, isNonExt;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT  = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
    isNonExt = LD->getExtensionType() == ISD::NON_EXTLOAD;
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT  = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
    isNonExt = !ST->isTruncatingStore();
  } else
    return false;

  if (Subtarget->isThumb1Only()) {
    // Thumb-1 can do a limited post-inc load or store as an updating LDM. It
    // must be non-extending/truncating, i32, with an offset of 4.
    assert(Op->getValueType(0) == MVT::i32 && "Non-i32 post-inc op?!");
    if (Op->getOpcode() != ISD::ADD || !isNonExt)
      return false;
    auto *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!RHS || RHS->getZExtValue() != 4)
      return false;

    Offset = Op->getOperand(1);
    Base = Op->getOperand(0);
    AM = ISD::POST_INC;
    return true;
  }

  bool isInc;
  bool isLegal = false;
  if (Subtarget->isThumb2())
    isLegal = getT2IndexedAddressParts(Op, VT, isSEXTLoad, Base, Offset,
                                       isInc, DAG);
  else
    isLegal = getARMIndexedAddressParts(Op, VT, isSEXTLoad, Base, Offset,
                                        isInc, DAG);
  if (!isLegal)
    return false;

  if (Ptr != Base) {
    // Swap base ptr and offset to catch more post-index load / store when
    // it's legal. In Thumb2 mode, offset must be an immediate.
    if (Ptr == Offset && Op->getOpcode() == ISD::ADD &&
        !Subtarget->isThumb2())
      std::swap(Base, Offset);

    // Post-indexed load / store update the base pointer.
    if (Ptr != Base)
      return false;
  }

  AM = isInc ? ISD::POST_INC : ISD::POST_DEC;
  return true;
}

void ARMTargetLowering::computeKnownBitsForTargetNode(const SDValue Op,
                                                      KnownBits &Known,
                                                      const APInt &DemandedElts,
                                                      const SelectionDAG &DAG,
                                                      unsigned Depth) const {
  unsigned BitWidth = Known.getBitWidth();
  Known.resetAll();
  switch (Op.getOpcode()) {
  default: break;
  case ARMISD::ADDC:
  case ARMISD::ADDE:
  case ARMISD::SUBC:
  case ARMISD::SUBE:
    // Special cases when we convert a carry to a boolean.
    if (Op.getResNo() == 0) {
      SDValue LHS = Op.getOperand(0);
      SDValue RHS = Op.getOperand(1);
      // (ADDE 0, 0, C) will give us a single bit.
      if (Op->getOpcode() == ARMISD::ADDE && isNullConstant(LHS) &&
          isNullConstant(RHS)) {
        Known.Zero |= APInt::getHighBitsSet(BitWidth, BitWidth - 1);
        return;
      }
    }
    break;
  case ARMISD::CMOV: {
    // Bits are known zero/one if known on the LHS and RHS.
    Known = DAG.computeKnownBits(Op.getOperand(0), Depth+1);
    if (Known.isUnknown())
      return;

    KnownBits KnownRHS = DAG.computeKnownBits(Op.getOperand(1), Depth+1);
    Known.Zero &= KnownRHS.Zero;
    Known.One  &= KnownRHS.One;
    return;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    ConstantSDNode *CN = cast<ConstantSDNode>(Op->getOperand(1));
    Intrinsic::ID IntID = static_cast<Intrinsic::ID>(CN->getZExtValue());
    switch (IntID) {
    default: return;
    case Intrinsic::arm_ldaex:
    case Intrinsic::arm_ldrex: {
      EVT VT = cast<MemIntrinsicSDNode>(Op)->getMemoryVT();
      unsigned MemBits = VT.getScalarSizeInBits();
      Known.Zero |= APInt::getHighBitsSet(BitWidth, BitWidth - MemBits);
      return;
    }
    }
  }
  case ARMISD::BFI: {
    // Conservatively, we can recurse down the first operand
    // and just mask out all affected bits.
    Known = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);

    // The operand to BFI is already a mask suitable for removing the bits it
    // sets.
    ConstantSDNode *CI = cast<ConstantSDNode>(Op.getOperand(2));
    const APInt &Mask = CI->getAPIntValue();
    Known.Zero &= Mask;
    Known.One &= Mask;
    return;
  }
  case ARMISD::VGETLANEs:
  case ARMISD::VGETLANEu: {
    const SDValue &SrcSV = Op.getOperand(0);
    EVT VecVT = SrcSV.getValueType();
    assert(VecVT.isVector() && "VGETLANE expected a vector type");
    const unsigned NumSrcElts = VecVT.getVectorNumElements();
    ConstantSDNode *Pos = cast<ConstantSDNode>(Op.getOperand(1).getNode());
    assert(Pos->getAPIntValue().ult(NumSrcElts) &&
           "VGETLANE index out of bounds");
    unsigned Idx = Pos->getZExtValue();
    APInt DemandedElt = APInt::getOneBitSet(NumSrcElts, Idx);
    Known = DAG.computeKnownBits(SrcSV, DemandedElt, Depth + 1);

    EVT VT = Op.getValueType();
    const unsigned DstSz = VT.getScalarSizeInBits();
    const unsigned SrcSz = VecVT.getVectorElementType().getSizeInBits();
    assert(SrcSz == Known.getBitWidth());
    assert(DstSz > SrcSz);
    if (Op.getOpcode() == ARMISD::VGETLANEs)
      Known = Known.sext(DstSz);
    else {
      Known = Known.zext(DstSz);
      Known.Zero.setBitsFrom(SrcSz);
    }
    assert(DstSz == Known.getBitWidth());
    break;
  }
  }
}

bool
ARMTargetLowering::targetShrinkDemandedConstant(SDValue Op,
                                                const APInt &DemandedAPInt,
                                                TargetLoweringOpt &TLO) const {
  // Delay optimization, so we don't have to deal with illegal types, or block
  // optimizations.
  if (!TLO.LegalOps)
    return false;

  // Only optimize AND for now.
  if (Op.getOpcode() != ISD::AND)
    return false;

  EVT VT = Op.getValueType();

  // Ignore vectors.
  if (VT.isVector())
    return false;

  assert(VT == MVT::i32 && "Unexpected integer type");

  // Make sure the RHS really is a constant.
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!C)
    return false;

  unsigned Mask = C->getZExtValue();

  unsigned Demanded = DemandedAPInt.getZExtValue();
  unsigned ShrunkMask = Mask & Demanded;
  unsigned ExpandedMask = Mask | ~Demanded;

  // If the mask is all zeros, let the target-independent code replace the
  // result with zero.
  if (ShrunkMask == 0)
    return false;

  // If the mask is all ones, erase the AND. (Currently, the target-independent
  // code won't do this, so we have to do it explicitly to avoid an infinite
  // loop in obscure cases.)
  if (ExpandedMask == ~0U)
    return TLO.CombineTo(Op, Op.getOperand(0));

  auto IsLegalMask = [ShrunkMask, ExpandedMask](unsigned Mask) -> bool {
    return (ShrunkMask & Mask) == ShrunkMask && (~ExpandedMask & Mask) == 0;
  };
  auto UseMask = [Mask, Op, VT, &TLO](unsigned NewMask) -> bool {
    if (NewMask == Mask)
      return true;
    SDLoc DL(Op);
    SDValue NewC = TLO.DAG.getConstant(NewMask, DL, VT);
    SDValue NewOp = TLO.DAG.getNode(ISD::AND, DL, VT, Op.getOperand(0), NewC);
    return TLO.CombineTo(Op, NewOp);
  };

  // Prefer uxtb mask.
  if (IsLegalMask(0xFF))
    return UseMask(0xFF);

  // Prefer uxth mask.
  if (IsLegalMask(0xFFFF))
    return UseMask(0xFFFF);

  // [1, 255] is Thumb1 movs+ands, legal immediate for ARM/Thumb2.
  // FIXME: Prefer a contiguous sequence of bits for other optimizations.
  if (ShrunkMask < 256)
    return UseMask(ShrunkMask);

  // [-256, -2] is Thumb1 movs+bics, legal immediate for ARM/Thumb2.
  // FIXME: Prefer a contiguous sequence of bits for other optimizations.
  if ((int)ExpandedMask <= -2 && (int)ExpandedMask >= -256)
    return UseMask(ExpandedMask);

  // Potential improvements:
  //
  // We could try to recognize lsls+lsrs or lsrs+lsls pairs here.
  // We could try to prefer Thumb1 immediates which can be lowered to a
  // two-instruction sequence.
  // We could try to recognize more legal ARM/Thumb2 immediates here.

  return false;
}


//===----------------------------------------------------------------------===//
//                           ARM Inline Assembly Support
//===----------------------------------------------------------------------===//

bool ARMTargetLowering::ExpandInlineAsm(CallInst *CI) const {
  // Looking for "rev" which is V6+.
  if (!Subtarget->hasV6Ops())
    return false;

  InlineAsm *IA = cast<InlineAsm>(CI->getCalledValue());
  std::string AsmStr = IA->getAsmString();
  SmallVector<StringRef, 4> AsmPieces;
  SplitString(AsmStr, AsmPieces, ";\n");

  switch (AsmPieces.size()) {
  default: return false;
  case 1:
    AsmStr = AsmPieces[0];
    AsmPieces.clear();
    SplitString(AsmStr, AsmPieces, " \t,");

    // rev $0, $1
    if (AsmPieces.size() == 3 &&
        AsmPieces[0] == "rev" && AsmPieces[1] == "$0" && AsmPieces[2] == "$1" &&
        IA->getConstraintString().compare(0, 4, "=l,l") == 0) {
      IntegerType *Ty = dyn_cast<IntegerType>(CI->getType());
      if (Ty && Ty->getBitWidth() == 32)
        return IntrinsicLowering::LowerToByteSwap(CI);
    }
    break;
  }

  return false;
}

const char *ARMTargetLowering::LowerXConstraint(EVT ConstraintVT) const {
  // At this point, we have to lower this constraint to something else, so we
  // lower it to an "r" or "w". However, by doing this we will force the result
  // to be in register, while the X constraint is much more permissive.
  //
  // Although we are correct (we are free to emit anything, without
  // constraints), we might break use cases that would expect us to be more
  // efficient and emit something else.
  if (!Subtarget->hasVFP2())
    return "r";
  if (ConstraintVT.isFloatingPoint())
    return "w";
  if (ConstraintVT.isVector() && Subtarget->hasNEON() &&
     (ConstraintVT.getSizeInBits() == 64 ||
      ConstraintVT.getSizeInBits() == 128))
    return "w";

  return "r";
}

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
ARMTargetLowering::ConstraintType
ARMTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:  break;
    case 'l': return C_RegisterClass;
    case 'w': return C_RegisterClass;
    case 'h': return C_RegisterClass;
    case 'x': return C_RegisterClass;
    case 't': return C_RegisterClass;
    case 'j': return C_Other; // Constant for movw.
      // An address with a single base register. Due to the way we
      // currently handle addresses it is the same as an 'r' memory constraint.
    case 'Q': return C_Memory;
    }
  } else if (Constraint.size() == 2) {
    switch (Constraint[0]) {
    default: break;
    // All 'U+' constraints are addresses.
    case 'U': return C_Memory;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
ARMTargetLowering::getSingleConstraintMatchWeight(
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
  case 'l':
    if (type->isIntegerTy()) {
      if (Subtarget->isThumb())
        weight = CW_SpecificReg;
      else
        weight = CW_Register;
    }
    break;
  case 'w':
    if (type->isFloatingPointTy())
      weight = CW_Register;
    break;
  }
  return weight;
}

using RCPair = std::pair<unsigned, const TargetRegisterClass *>;

RCPair ARMTargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint.size() == 1) {
    // GCC ARM Constraint Letters
    switch (Constraint[0]) {
    case 'l': // Low regs or general regs.
      if (Subtarget->isThumb())
        return RCPair(0U, &ARM::tGPRRegClass);
      return RCPair(0U, &ARM::GPRRegClass);
    case 'h': // High regs or no regs.
      if (Subtarget->isThumb())
        return RCPair(0U, &ARM::hGPRRegClass);
      break;
    case 'r':
      if (Subtarget->isThumb1Only())
        return RCPair(0U, &ARM::tGPRRegClass);
      return RCPair(0U, &ARM::GPRRegClass);
    case 'w':
      if (VT == MVT::Other)
        break;
      if (VT == MVT::f32)
        return RCPair(0U, &ARM::SPRRegClass);
      if (VT.getSizeInBits() == 64)
        return RCPair(0U, &ARM::DPRRegClass);
      if (VT.getSizeInBits() == 128)
        return RCPair(0U, &ARM::QPRRegClass);
      break;
    case 'x':
      if (VT == MVT::Other)
        break;
      if (VT == MVT::f32)
        return RCPair(0U, &ARM::SPR_8RegClass);
      if (VT.getSizeInBits() == 64)
        return RCPair(0U, &ARM::DPR_8RegClass);
      if (VT.getSizeInBits() == 128)
        return RCPair(0U, &ARM::QPR_8RegClass);
      break;
    case 't':
      if (VT == MVT::Other)
        break;
      if (VT == MVT::f32 || VT == MVT::i32)
        return RCPair(0U, &ARM::SPRRegClass);
      if (VT.getSizeInBits() == 64)
        return RCPair(0U, &ARM::DPR_VFP2RegClass);
      if (VT.getSizeInBits() == 128)
        return RCPair(0U, &ARM::QPR_VFP2RegClass);
      break;
    }
  }
  if (StringRef("{cc}").equals_lower(Constraint))
    return std::make_pair(unsigned(ARM::CPSR), &ARM::CCRRegClass);

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void ARMTargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                     std::string &Constraint,
                                                     std::vector<SDValue>&Ops,
                                                     SelectionDAG &DAG) const {
  SDValue Result;

  // Currently only support length 1 constraints.
  if (Constraint.length() != 1) return;

  char ConstraintLetter = Constraint[0];
  switch (ConstraintLetter) {
  default: break;
  case 'j':
  case 'I': case 'J': case 'K': case 'L':
  case 'M': case 'N': case 'O':
    ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op);
    if (!C)
      return;

    int64_t CVal64 = C->getSExtValue();
    int CVal = (int) CVal64;
    // None of these constraints allow values larger than 32 bits.  Check
    // that the value fits in an int.
    if (CVal != CVal64)
      return;

    switch (ConstraintLetter) {
      case 'j':
        // Constant suitable for movw, must be between 0 and
        // 65535.
        if (Subtarget->hasV6T2Ops())
          if (CVal >= 0 && CVal <= 65535)
            break;
        return;
      case 'I':
        if (Subtarget->isThumb1Only()) {
          // This must be a constant between 0 and 255, for ADD
          // immediates.
          if (CVal >= 0 && CVal <= 255)
            break;
        } else if (Subtarget->isThumb2()) {
          // A constant that can be used as an immediate value in a
          // data-processing instruction.
          if (ARM_AM::getT2SOImmVal(CVal) != -1)
            break;
        } else {
          // A constant that can be used as an immediate value in a
          // data-processing instruction.
          if (ARM_AM::getSOImmVal(CVal) != -1)
            break;
        }
        return;

      case 'J':
        if (Subtarget->isThumb1Only()) {
          // This must be a constant between -255 and -1, for negated ADD
          // immediates. This can be used in GCC with an "n" modifier that
          // prints the negated value, for use with SUB instructions. It is
          // not useful otherwise but is implemented for compatibility.
          if (CVal >= -255 && CVal <= -1)
            break;
        } else {
          // This must be a constant between -4095 and 4095. It is not clear
          // what this constraint is intended for. Implemented for
          // compatibility with GCC.
          if (CVal >= -4095 && CVal <= 4095)
            break;
        }
        return;

      case 'K':
        if (Subtarget->isThumb1Only()) {
          // A 32-bit value where only one byte has a nonzero value. Exclude
          // zero to match GCC. This constraint is used by GCC internally for
          // constants that can be loaded with a move/shift combination.
          // It is not useful otherwise but is implemented for compatibility.
          if (CVal != 0 && ARM_AM::isThumbImmShiftedVal(CVal))
            break;
        } else if (Subtarget->isThumb2()) {
          // A constant whose bitwise inverse can be used as an immediate
          // value in a data-processing instruction. This can be used in GCC
          // with a "B" modifier that prints the inverted value, for use with
          // BIC and MVN instructions. It is not useful otherwise but is
          // implemented for compatibility.
          if (ARM_AM::getT2SOImmVal(~CVal) != -1)
            break;
        } else {
          // A constant whose bitwise inverse can be used as an immediate
          // value in a data-processing instruction. This can be used in GCC
          // with a "B" modifier that prints the inverted value, for use with
          // BIC and MVN instructions. It is not useful otherwise but is
          // implemented for compatibility.
          if (ARM_AM::getSOImmVal(~CVal) != -1)
            break;
        }
        return;

      case 'L':
        if (Subtarget->isThumb1Only()) {
          // This must be a constant between -7 and 7,
          // for 3-operand ADD/SUB immediate instructions.
          if (CVal >= -7 && CVal < 7)
            break;
        } else if (Subtarget->isThumb2()) {
          // A constant whose negation can be used as an immediate value in a
          // data-processing instruction. This can be used in GCC with an "n"
          // modifier that prints the negated value, for use with SUB
          // instructions. It is not useful otherwise but is implemented for
          // compatibility.
          if (ARM_AM::getT2SOImmVal(-CVal) != -1)
            break;
        } else {
          // A constant whose negation can be used as an immediate value in a
          // data-processing instruction. This can be used in GCC with an "n"
          // modifier that prints the negated value, for use with SUB
          // instructions. It is not useful otherwise but is implemented for
          // compatibility.
          if (ARM_AM::getSOImmVal(-CVal) != -1)
            break;
        }
        return;

      case 'M':
        if (Subtarget->isThumb1Only()) {
          // This must be a multiple of 4 between 0 and 1020, for
          // ADD sp + immediate.
          if ((CVal >= 0 && CVal <= 1020) && ((CVal & 3) == 0))
            break;
        } else {
          // A power of two or a constant between 0 and 32.  This is used in
          // GCC for the shift amount on shifted register operands, but it is
          // useful in general for any shift amounts.
          if ((CVal >= 0 && CVal <= 32) || ((CVal & (CVal - 1)) == 0))
            break;
        }
        return;

      case 'N':
        if (Subtarget->isThumb()) {  // FIXME thumb2
          // This must be a constant between 0 and 31, for shift amounts.
          if (CVal >= 0 && CVal <= 31)
            break;
        }
        return;

      case 'O':
        if (Subtarget->isThumb()) {  // FIXME thumb2
          // This must be a multiple of 4 between -508 and 508, for
          // ADD/SUB sp = sp + immediate.
          if ((CVal >= -508 && CVal <= 508) && ((CVal & 3) == 0))
            break;
        }
        return;
    }
    Result = DAG.getTargetConstant(CVal, SDLoc(Op), Op.getValueType());
    break;
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }
  return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

static RTLIB::Libcall getDivRemLibcall(
    const SDNode *N, MVT::SimpleValueType SVT) {
  assert((N->getOpcode() == ISD::SDIVREM || N->getOpcode() == ISD::UDIVREM ||
          N->getOpcode() == ISD::SREM    || N->getOpcode() == ISD::UREM) &&
         "Unhandled Opcode in getDivRemLibcall");
  bool isSigned = N->getOpcode() == ISD::SDIVREM ||
                  N->getOpcode() == ISD::SREM;
  RTLIB::Libcall LC;
  switch (SVT) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:  LC = isSigned ? RTLIB::SDIVREM_I8  : RTLIB::UDIVREM_I8;  break;
  case MVT::i16: LC = isSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16; break;
  case MVT::i32: LC = isSigned ? RTLIB::SDIVREM_I32 : RTLIB::UDIVREM_I32; break;
  case MVT::i64: LC = isSigned ? RTLIB::SDIVREM_I64 : RTLIB::UDIVREM_I64; break;
  }
  return LC;
}

static TargetLowering::ArgListTy getDivRemArgList(
    const SDNode *N, LLVMContext *Context, const ARMSubtarget *Subtarget) {
  assert((N->getOpcode() == ISD::SDIVREM || N->getOpcode() == ISD::UDIVREM ||
          N->getOpcode() == ISD::SREM    || N->getOpcode() == ISD::UREM) &&
         "Unhandled Opcode in getDivRemArgList");
  bool isSigned = N->getOpcode() == ISD::SDIVREM ||
                  N->getOpcode() == ISD::SREM;
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    EVT ArgVT = N->getOperand(i).getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*Context);
    Entry.Node = N->getOperand(i);
    Entry.Ty = ArgTy;
    Entry.IsSExt = isSigned;
    Entry.IsZExt = !isSigned;
    Args.push_back(Entry);
  }
  if (Subtarget->isTargetWindows() && Args.size() >= 2)
    std::swap(Args[0], Args[1]);
  return Args;
}

SDValue ARMTargetLowering::LowerDivRem(SDValue Op, SelectionDAG &DAG) const {
  assert((Subtarget->isTargetAEABI() || Subtarget->isTargetAndroid() ||
          Subtarget->isTargetGNUAEABI() || Subtarget->isTargetMuslAEABI() ||
          Subtarget->isTargetWindows()) &&
         "Register-based DivRem lowering only");
  unsigned Opcode = Op->getOpcode();
  assert((Opcode == ISD::SDIVREM || Opcode == ISD::UDIVREM) &&
         "Invalid opcode for Div/Rem lowering");
  bool isSigned = (Opcode == ISD::SDIVREM);
  EVT VT = Op->getValueType(0);
  Type *Ty = VT.getTypeForEVT(*DAG.getContext());
  SDLoc dl(Op);

  // If the target has hardware divide, use divide + multiply + subtract:
  //     div = a / b
  //     rem = a - b * div
  //     return {div, rem}
  // This should be lowered into UDIV/SDIV + MLS later on.
  bool hasDivide = Subtarget->isThumb() ? Subtarget->hasDivideInThumbMode()
                                        : Subtarget->hasDivideInARMMode();
  if (hasDivide && Op->getValueType(0).isSimple() &&
      Op->getSimpleValueType(0) == MVT::i32) {
    unsigned DivOpcode = isSigned ? ISD::SDIV : ISD::UDIV;
    const SDValue Dividend = Op->getOperand(0);
    const SDValue Divisor = Op->getOperand(1);
    SDValue Div = DAG.getNode(DivOpcode, dl, VT, Dividend, Divisor);
    SDValue Mul = DAG.getNode(ISD::MUL, dl, VT, Div, Divisor);
    SDValue Rem = DAG.getNode(ISD::SUB, dl, VT, Dividend, Mul);

    SDValue Values[2] = {Div, Rem};
    return DAG.getNode(ISD::MERGE_VALUES, dl, DAG.getVTList(VT, VT), Values);
  }

  RTLIB::Libcall LC = getDivRemLibcall(Op.getNode(),
                                       VT.getSimpleVT().SimpleTy);
  SDValue InChain = DAG.getEntryNode();

  TargetLowering::ArgListTy Args = getDivRemArgList(Op.getNode(),
                                                    DAG.getContext(),
                                                    Subtarget);

  SDValue Callee = DAG.getExternalSymbol(getLibcallName(LC),
                                         getPointerTy(DAG.getDataLayout()));

  Type *RetTy = StructType::get(Ty, Ty);

  if (Subtarget->isTargetWindows())
    InChain = WinDBZCheckDenominator(DAG, Op.getNode(), InChain);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(InChain)
    .setCallee(getLibcallCallingConv(LC), RetTy, Callee, std::move(Args))
    .setInRegister().setSExtResult(isSigned).setZExtResult(!isSigned);

  std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);
  return CallInfo.first;
}

// Lowers REM using divmod helpers
// see RTABI section 4.2/4.3
SDValue ARMTargetLowering::LowerREM(SDNode *N, SelectionDAG &DAG) const {
  // Build return types (div and rem)
  std::vector<Type*> RetTyParams;
  Type *RetTyElement;

  switch (N->getValueType(0).getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   RetTyElement = Type::getInt8Ty(*DAG.getContext());  break;
  case MVT::i16:  RetTyElement = Type::getInt16Ty(*DAG.getContext()); break;
  case MVT::i32:  RetTyElement = Type::getInt32Ty(*DAG.getContext()); break;
  case MVT::i64:  RetTyElement = Type::getInt64Ty(*DAG.getContext()); break;
  }

  RetTyParams.push_back(RetTyElement);
  RetTyParams.push_back(RetTyElement);
  ArrayRef<Type*> ret = ArrayRef<Type*>(RetTyParams);
  Type *RetTy = StructType::get(*DAG.getContext(), ret);

  RTLIB::Libcall LC = getDivRemLibcall(N, N->getValueType(0).getSimpleVT().
                                                             SimpleTy);
  SDValue InChain = DAG.getEntryNode();
  TargetLowering::ArgListTy Args = getDivRemArgList(N, DAG.getContext(),
                                                    Subtarget);
  bool isSigned = N->getOpcode() == ISD::SREM;
  SDValue Callee = DAG.getExternalSymbol(getLibcallName(LC),
                                         getPointerTy(DAG.getDataLayout()));

  if (Subtarget->isTargetWindows())
    InChain = WinDBZCheckDenominator(DAG, N, InChain);

  // Lower call
  CallLoweringInfo CLI(DAG);
  CLI.setChain(InChain)
     .setCallee(CallingConv::ARM_AAPCS, RetTy, Callee, std::move(Args))
     .setSExtResult(isSigned).setZExtResult(!isSigned).setDebugLoc(SDLoc(N));
  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);

  // Return second (rem) result operand (first contains div)
  SDNode *ResNode = CallResult.first.getNode();
  assert(ResNode->getNumOperands() == 2 && "divmod should return two operands");
  return ResNode->getOperand(1);
}

SDValue
ARMTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() && "unsupported target platform");
  SDLoc DL(Op);

  // Get the inputs.
  SDValue Chain = Op.getOperand(0);
  SDValue Size  = Op.getOperand(1);

  if (DAG.getMachineFunction().getFunction().hasFnAttribute(
          "no-stack-arg-probe")) {
    unsigned Align = cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue();
    SDValue SP = DAG.getCopyFromReg(Chain, DL, ARM::SP, MVT::i32);
    Chain = SP.getValue(1);
    SP = DAG.getNode(ISD::SUB, DL, MVT::i32, SP, Size);
    if (Align)
      SP = DAG.getNode(ISD::AND, DL, MVT::i32, SP.getValue(0),
                       DAG.getConstant(-(uint64_t)Align, DL, MVT::i32));
    Chain = DAG.getCopyToReg(Chain, DL, ARM::SP, SP);
    SDValue Ops[2] = { SP, Chain };
    return DAG.getMergeValues(Ops, DL);
  }

  SDValue Words = DAG.getNode(ISD::SRL, DL, MVT::i32, Size,
                              DAG.getConstant(2, DL, MVT::i32));

  SDValue Flag;
  Chain = DAG.getCopyToReg(Chain, DL, ARM::R4, Words, Flag);
  Flag = Chain.getValue(1);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(ARMISD::WIN__CHKSTK, DL, NodeTys, Chain, Flag);

  SDValue NewSP = DAG.getCopyFromReg(Chain, DL, ARM::SP, MVT::i32);
  Chain = NewSP.getValue(1);

  SDValue Ops[2] = { NewSP, Chain };
  return DAG.getMergeValues(Ops, DL);
}

SDValue ARMTargetLowering::LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getValueType() == MVT::f64 && Subtarget->isFPOnlySP() &&
         "Unexpected type for custom-lowering FP_EXTEND");

  RTLIB::Libcall LC;
  LC = RTLIB::getFPEXT(Op.getOperand(0).getValueType(), Op.getValueType());

  SDValue SrcVal = Op.getOperand(0);
  return makeLibCall(DAG, LC, Op.getValueType(), SrcVal, /*isSigned*/ false,
                     SDLoc(Op)).first;
}

SDValue ARMTargetLowering::LowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getOperand(0).getValueType() == MVT::f64 &&
         Subtarget->isFPOnlySP() &&
         "Unexpected type for custom-lowering FP_ROUND");

  RTLIB::Libcall LC;
  LC = RTLIB::getFPROUND(Op.getOperand(0).getValueType(), Op.getValueType());

  SDValue SrcVal = Op.getOperand(0);
  return makeLibCall(DAG, LC, Op.getValueType(), SrcVal, /*isSigned*/ false,
                     SDLoc(Op)).first;
}

bool
ARMTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The ARM target isn't yet aware of offsets.
  return false;
}

bool ARM::isBitFieldInvertedMask(unsigned v) {
  if (v == 0xffffffff)
    return false;

  // there can be 1's on either or both "outsides", all the "inside"
  // bits must be 0's
  return isShiftedMask_32(~v);
}

/// isFPImmLegal - Returns true if the target can instruction select the
/// specified FP immediate natively. If false, the legalizer will
/// materialize the FP immediate as a load from a constant pool.
bool ARMTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT) const {
  if (!Subtarget->hasVFP3())
    return false;
  if (VT == MVT::f16 && Subtarget->hasFullFP16())
    return ARM_AM::getFP16Imm(Imm) != -1;
  if (VT == MVT::f32)
    return ARM_AM::getFP32Imm(Imm) != -1;
  if (VT == MVT::f64 && !Subtarget->isFPOnlySP())
    return ARM_AM::getFP64Imm(Imm) != -1;
  return false;
}

/// getTgtMemIntrinsic - Represent NEON load and store intrinsics as
/// MemIntrinsicNodes.  The associated MachineMemOperands record the alignment
/// specified in the intrinsic calls.
bool ARMTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                           const CallInst &I,
                                           MachineFunction &MF,
                                           unsigned Intrinsic) const {
  switch (Intrinsic) {
  case Intrinsic::arm_neon_vld1:
  case Intrinsic::arm_neon_vld2:
  case Intrinsic::arm_neon_vld3:
  case Intrinsic::arm_neon_vld4:
  case Intrinsic::arm_neon_vld2lane:
  case Intrinsic::arm_neon_vld3lane:
  case Intrinsic::arm_neon_vld4lane:
  case Intrinsic::arm_neon_vld2dup:
  case Intrinsic::arm_neon_vld3dup:
  case Intrinsic::arm_neon_vld4dup: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // Conservatively set memVT to the entire set of vectors loaded.
    auto &DL = I.getCalledFunction()->getParent()->getDataLayout();
    uint64_t NumElts = DL.getTypeSizeInBits(I.getType()) / 64;
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Value *AlignArg = I.getArgOperand(I.getNumArgOperands() - 1);
    Info.align = cast<ConstantInt>(AlignArg)->getZExtValue();
    // volatile loads with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_neon_vld1x2:
  case Intrinsic::arm_neon_vld1x3:
  case Intrinsic::arm_neon_vld1x4: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // Conservatively set memVT to the entire set of vectors loaded.
    auto &DL = I.getCalledFunction()->getParent()->getDataLayout();
    uint64_t NumElts = DL.getTypeSizeInBits(I.getType()) / 64;
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(I.getNumArgOperands() - 1);
    Info.offset = 0;
    Info.align = 0;
    // volatile loads with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_neon_vst1:
  case Intrinsic::arm_neon_vst2:
  case Intrinsic::arm_neon_vst3:
  case Intrinsic::arm_neon_vst4:
  case Intrinsic::arm_neon_vst2lane:
  case Intrinsic::arm_neon_vst3lane:
  case Intrinsic::arm_neon_vst4lane: {
    Info.opc = ISD::INTRINSIC_VOID;
    // Conservatively set memVT to the entire set of vectors stored.
    auto &DL = I.getCalledFunction()->getParent()->getDataLayout();
    unsigned NumElts = 0;
    for (unsigned ArgI = 1, ArgE = I.getNumArgOperands(); ArgI < ArgE; ++ArgI) {
      Type *ArgTy = I.getArgOperand(ArgI)->getType();
      if (!ArgTy->isVectorTy())
        break;
      NumElts += DL.getTypeSizeInBits(ArgTy) / 64;
    }
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Value *AlignArg = I.getArgOperand(I.getNumArgOperands() - 1);
    Info.align = cast<ConstantInt>(AlignArg)->getZExtValue();
    // volatile stores with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_neon_vst1x2:
  case Intrinsic::arm_neon_vst1x3:
  case Intrinsic::arm_neon_vst1x4: {
    Info.opc = ISD::INTRINSIC_VOID;
    // Conservatively set memVT to the entire set of vectors stored.
    auto &DL = I.getCalledFunction()->getParent()->getDataLayout();
    unsigned NumElts = 0;
    for (unsigned ArgI = 1, ArgE = I.getNumArgOperands(); ArgI < ArgE; ++ArgI) {
      Type *ArgTy = I.getArgOperand(ArgI)->getType();
      if (!ArgTy->isVectorTy())
        break;
      NumElts += DL.getTypeSizeInBits(ArgTy) / 64;
    }
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = 0;
    // volatile stores with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_ldaex:
  case Intrinsic::arm_ldrex: {
    auto &DL = I.getCalledFunction()->getParent()->getDataLayout();
    PointerType *PtrTy = cast<PointerType>(I.getArgOperand(0)->getType());
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(PtrTy->getElementType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = DL.getABITypeAlignment(PtrTy->getElementType());
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::arm_stlex:
  case Intrinsic::arm_strex: {
    auto &DL = I.getCalledFunction()->getParent()->getDataLayout();
    PointerType *PtrTy = cast<PointerType>(I.getArgOperand(1)->getType());
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(PtrTy->getElementType());
    Info.ptrVal = I.getArgOperand(1);
    Info.offset = 0;
    Info.align = DL.getABITypeAlignment(PtrTy->getElementType());
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::arm_stlexd:
  case Intrinsic::arm_strexd:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i64;
    Info.ptrVal = I.getArgOperand(2);
    Info.offset = 0;
    Info.align = 8;
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;

  case Intrinsic::arm_ldaexd:
  case Intrinsic::arm_ldrexd:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i64;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = 8;
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
    return true;

  default:
    break;
  }

  return false;
}

/// Returns true if it is beneficial to convert a load of a constant
/// to just the constant itself.
bool ARMTargetLowering::shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                          Type *Ty) const {
  assert(Ty->isIntegerTy());

  unsigned Bits = Ty->getPrimitiveSizeInBits();
  if (Bits == 0 || Bits > 32)
    return false;
  return true;
}

bool ARMTargetLowering::isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                                unsigned Index) const {
  if (!isOperationLegalOrCustom(ISD::EXTRACT_SUBVECTOR, ResVT))
    return false;

  return (Index == 0 || Index == ResVT.getVectorNumElements());
}

Instruction* ARMTargetLowering::makeDMB(IRBuilder<> &Builder,
                                        ARM_MB::MemBOpt Domain) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();

  // First, if the target has no DMB, see what fallback we can use.
  if (!Subtarget->hasDataBarrier()) {
    // Some ARMv6 cpus can support data barriers with an mcr instruction.
    // Thumb1 and pre-v6 ARM mode use a libcall instead and should never get
    // here.
    if (Subtarget->hasV6Ops() && !Subtarget->isThumb()) {
      Function *MCR = Intrinsic::getDeclaration(M, Intrinsic::arm_mcr);
      Value* args[6] = {Builder.getInt32(15), Builder.getInt32(0),
                        Builder.getInt32(0), Builder.getInt32(7),
                        Builder.getInt32(10), Builder.getInt32(5)};
      return Builder.CreateCall(MCR, args);
    } else {
      // Instead of using barriers, atomic accesses on these subtargets use
      // libcalls.
      llvm_unreachable("makeDMB on a target so old that it has no barriers");
    }
  } else {
    Function *DMB = Intrinsic::getDeclaration(M, Intrinsic::arm_dmb);
    // Only a full system barrier exists in the M-class architectures.
    Domain = Subtarget->isMClass() ? ARM_MB::SY : Domain;
    Constant *CDomain = Builder.getInt32(Domain);
    return Builder.CreateCall(DMB, CDomain);
  }
}

// Based on http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
Instruction *ARMTargetLowering::emitLeadingFence(IRBuilder<> &Builder,
                                                 Instruction *Inst,
                                                 AtomicOrdering Ord) const {
  switch (Ord) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
    llvm_unreachable("Invalid fence: unordered/non-atomic");
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Acquire:
    return nullptr; // Nothing to do
  case AtomicOrdering::SequentiallyConsistent:
    if (!Inst->hasAtomicStore())
      return nullptr; // Nothing to do
    LLVM_FALLTHROUGH;
  case AtomicOrdering::Release:
  case AtomicOrdering::AcquireRelease:
    if (Subtarget->preferISHSTBarriers())
      return makeDMB(Builder, ARM_MB::ISHST);
    // FIXME: add a comment with a link to documentation justifying this.
    else
      return makeDMB(Builder, ARM_MB::ISH);
  }
  llvm_unreachable("Unknown fence ordering in emitLeadingFence");
}

Instruction *ARMTargetLowering::emitTrailingFence(IRBuilder<> &Builder,
                                                  Instruction *Inst,
                                                  AtomicOrdering Ord) const {
  switch (Ord) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
    llvm_unreachable("Invalid fence: unordered/not-atomic");
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Release:
    return nullptr; // Nothing to do
  case AtomicOrdering::Acquire:
  case AtomicOrdering::AcquireRelease:
  case AtomicOrdering::SequentiallyConsistent:
    return makeDMB(Builder, ARM_MB::ISH);
  }
  llvm_unreachable("Unknown fence ordering in emitTrailingFence");
}

// Loads and stores less than 64-bits are already atomic; ones above that
// are doomed anyway, so defer to the default libcall and blame the OS when
// things go wrong. Cortex M doesn't have ldrexd/strexd though, so don't emit
// anything for those.
bool ARMTargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  unsigned Size = SI->getValueOperand()->getType()->getPrimitiveSizeInBits();
  return (Size == 64) && !Subtarget->isMClass();
}

// Loads and stores less than 64-bits are already atomic; ones above that
// are doomed anyway, so defer to the default libcall and blame the OS when
// things go wrong. Cortex M doesn't have ldrexd/strexd though, so don't emit
// anything for those.
// FIXME: ldrd and strd are atomic if the CPU has LPAE (e.g. A15 has that
// guarantee, see DDI0406C ARM architecture reference manual,
// sections A8.8.72-74 LDRD)
TargetLowering::AtomicExpansionKind
ARMTargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  unsigned Size = LI->getType()->getPrimitiveSizeInBits();
  return ((Size == 64) && !Subtarget->isMClass()) ? AtomicExpansionKind::LLOnly
                                                  : AtomicExpansionKind::None;
}

// For the real atomic operations, we have ldrex/strex up to 32 bits,
// and up to 64 bits on the non-M profiles
TargetLowering::AtomicExpansionKind
ARMTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  unsigned Size = AI->getType()->getPrimitiveSizeInBits();
  bool hasAtomicRMW = !Subtarget->isThumb() || Subtarget->hasV8MBaselineOps();
  return (Size <= (Subtarget->isMClass() ? 32U : 64U) && hasAtomicRMW)
             ? AtomicExpansionKind::LLSC
             : AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
ARMTargetLowering::shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const {
  // At -O0, fast-regalloc cannot cope with the live vregs necessary to
  // implement cmpxchg without spilling. If the address being exchanged is also
  // on the stack and close enough to the spill slot, this can lead to a
  // situation where the monitor always gets cleared and the atomic operation
  // can never succeed. So at -O0 we need a late-expanded pseudo-inst instead.
  bool HasAtomicCmpXchg =
      !Subtarget->isThumb() || Subtarget->hasV8MBaselineOps();
  if (getTargetMachine().getOptLevel() != 0 && HasAtomicCmpXchg)
    return AtomicExpansionKind::LLSC;
  return AtomicExpansionKind::None;
}

bool ARMTargetLowering::shouldInsertFencesForAtomic(
    const Instruction *I) const {
  return InsertFencesForAtomic;
}

// This has so far only been implemented for MachO.
bool ARMTargetLowering::useLoadStackGuardNode() const {
  return Subtarget->isTargetMachO();
}

bool ARMTargetLowering::canCombineStoreAndExtract(Type *VectorTy, Value *Idx,
                                                  unsigned &Cost) const {
  // If we do not have NEON, vector types are not natively supported.
  if (!Subtarget->hasNEON())
    return false;

  // Floating point values and vector values map to the same register file.
  // Therefore, although we could do a store extract of a vector type, this is
  // better to leave at float as we have more freedom in the addressing mode for
  // those.
  if (VectorTy->isFPOrFPVectorTy())
    return false;

  // If the index is unknown at compile time, this is very expensive to lower
  // and it is not possible to combine the store with the extract.
  if (!isa<ConstantInt>(Idx))
    return false;

  assert(VectorTy->isVectorTy() && "VectorTy is not a vector type");
  unsigned BitWidth = cast<VectorType>(VectorTy)->getBitWidth();
  // We can do a store + vector extract on any vector that fits perfectly in a D
  // or Q register.
  if (BitWidth == 64 || BitWidth == 128) {
    Cost = 0;
    return true;
  }
  return false;
}

bool ARMTargetLowering::isCheapToSpeculateCttz() const {
  return Subtarget->hasV6T2Ops();
}

bool ARMTargetLowering::isCheapToSpeculateCtlz() const {
  return Subtarget->hasV6T2Ops();
}

Value *ARMTargetLowering::emitLoadLinked(IRBuilder<> &Builder, Value *Addr,
                                         AtomicOrdering Ord) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Type *ValTy = cast<PointerType>(Addr->getType())->getElementType();
  bool IsAcquire = isAcquireOrStronger(Ord);

  // Since i64 isn't legal and intrinsics don't get type-lowered, the ldrexd
  // intrinsic must return {i32, i32} and we have to recombine them into a
  // single i64 here.
  if (ValTy->getPrimitiveSizeInBits() == 64) {
    Intrinsic::ID Int =
        IsAcquire ? Intrinsic::arm_ldaexd : Intrinsic::arm_ldrexd;
    Function *Ldrex = Intrinsic::getDeclaration(M, Int);

    Addr = Builder.CreateBitCast(Addr, Type::getInt8PtrTy(M->getContext()));
    Value *LoHi = Builder.CreateCall(Ldrex, Addr, "lohi");

    Value *Lo = Builder.CreateExtractValue(LoHi, 0, "lo");
    Value *Hi = Builder.CreateExtractValue(LoHi, 1, "hi");
    if (!Subtarget->isLittle())
      std::swap (Lo, Hi);
    Lo = Builder.CreateZExt(Lo, ValTy, "lo64");
    Hi = Builder.CreateZExt(Hi, ValTy, "hi64");
    return Builder.CreateOr(
        Lo, Builder.CreateShl(Hi, ConstantInt::get(ValTy, 32)), "val64");
  }

  Type *Tys[] = { Addr->getType() };
  Intrinsic::ID Int = IsAcquire ? Intrinsic::arm_ldaex : Intrinsic::arm_ldrex;
  Function *Ldrex = Intrinsic::getDeclaration(M, Int, Tys);

  return Builder.CreateTruncOrBitCast(
      Builder.CreateCall(Ldrex, Addr),
      cast<PointerType>(Addr->getType())->getElementType());
}

void ARMTargetLowering::emitAtomicCmpXchgNoStoreLLBalance(
    IRBuilder<> &Builder) const {
  if (!Subtarget->hasV7Ops())
    return;
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Builder.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::arm_clrex));
}

Value *ARMTargetLowering::emitStoreConditional(IRBuilder<> &Builder, Value *Val,
                                               Value *Addr,
                                               AtomicOrdering Ord) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  bool IsRelease = isReleaseOrStronger(Ord);

  // Since the intrinsics must have legal type, the i64 intrinsics take two
  // parameters: "i32, i32". We must marshal Val into the appropriate form
  // before the call.
  if (Val->getType()->getPrimitiveSizeInBits() == 64) {
    Intrinsic::ID Int =
        IsRelease ? Intrinsic::arm_stlexd : Intrinsic::arm_strexd;
    Function *Strex = Intrinsic::getDeclaration(M, Int);
    Type *Int32Ty = Type::getInt32Ty(M->getContext());

    Value *Lo = Builder.CreateTrunc(Val, Int32Ty, "lo");
    Value *Hi = Builder.CreateTrunc(Builder.CreateLShr(Val, 32), Int32Ty, "hi");
    if (!Subtarget->isLittle())
      std::swap(Lo, Hi);
    Addr = Builder.CreateBitCast(Addr, Type::getInt8PtrTy(M->getContext()));
    return Builder.CreateCall(Strex, {Lo, Hi, Addr});
  }

  Intrinsic::ID Int = IsRelease ? Intrinsic::arm_stlex : Intrinsic::arm_strex;
  Type *Tys[] = { Addr->getType() };
  Function *Strex = Intrinsic::getDeclaration(M, Int, Tys);

  return Builder.CreateCall(
      Strex, {Builder.CreateZExtOrBitCast(
                  Val, Strex->getFunctionType()->getParamType(0)),
              Addr});
}


bool ARMTargetLowering::alignLoopsWithOptSize() const {
  return Subtarget->isMClass();
}

/// A helper function for determining the number of interleaved accesses we
/// will generate when lowering accesses of the given type.
unsigned
ARMTargetLowering::getNumInterleavedAccesses(VectorType *VecTy,
                                             const DataLayout &DL) const {
  return (DL.getTypeSizeInBits(VecTy) + 127) / 128;
}

bool ARMTargetLowering::isLegalInterleavedAccessType(
    VectorType *VecTy, const DataLayout &DL) const {

  unsigned VecSize = DL.getTypeSizeInBits(VecTy);
  unsigned ElSize = DL.getTypeSizeInBits(VecTy->getElementType());

  // Ensure the vector doesn't have f16 elements. Even though we could do an
  // i16 vldN, we can't hold the f16 vectors and will end up converting via
  // f32.
  if (VecTy->getElementType()->isHalfTy())
    return false;

  // Ensure the number of vector elements is greater than 1.
  if (VecTy->getNumElements() < 2)
    return false;

  // Ensure the element type is legal.
  if (ElSize != 8 && ElSize != 16 && ElSize != 32)
    return false;

  // Ensure the total vector size is 64 or a multiple of 128. Types larger than
  // 128 will be split into multiple interleaved accesses.
  return VecSize == 64 || VecSize % 128 == 0;
}

/// Lower an interleaved load into a vldN intrinsic.
///
/// E.g. Lower an interleaved load (Factor = 2):
///        %wide.vec = load <8 x i32>, <8 x i32>* %ptr, align 4
///        %v0 = shuffle %wide.vec, undef, <0, 2, 4, 6>  ; Extract even elements
///        %v1 = shuffle %wide.vec, undef, <1, 3, 5, 7>  ; Extract odd elements
///
///      Into:
///        %vld2 = { <4 x i32>, <4 x i32> } call llvm.arm.neon.vld2(%ptr, 4)
///        %vec0 = extractelement { <4 x i32>, <4 x i32> } %vld2, i32 0
///        %vec1 = extractelement { <4 x i32>, <4 x i32> } %vld2, i32 1
bool ARMTargetLowering::lowerInterleavedLoad(
    LoadInst *LI, ArrayRef<ShuffleVectorInst *> Shuffles,
    ArrayRef<unsigned> Indices, unsigned Factor) const {
  assert(Factor >= 2 && Factor <= getMaxSupportedInterleaveFactor() &&
         "Invalid interleave factor");
  assert(!Shuffles.empty() && "Empty shufflevector input");
  assert(Shuffles.size() == Indices.size() &&
         "Unmatched number of shufflevectors and indices");

  VectorType *VecTy = Shuffles[0]->getType();
  Type *EltTy = VecTy->getVectorElementType();

  const DataLayout &DL = LI->getModule()->getDataLayout();

  // Skip if we do not have NEON and skip illegal vector types. We can
  // "legalize" wide vector types into multiple interleaved accesses as long as
  // the vector types are divisible by 128.
  if (!Subtarget->hasNEON() || !isLegalInterleavedAccessType(VecTy, DL))
    return false;

  unsigned NumLoads = getNumInterleavedAccesses(VecTy, DL);

  // A pointer vector can not be the return type of the ldN intrinsics. Need to
  // load integer vectors first and then convert to pointer vectors.
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

  assert(isTypeLegal(EVT::getEVT(VecTy)) && "Illegal vldN vector type!");

  Type *Int8Ptr = Builder.getInt8PtrTy(LI->getPointerAddressSpace());
  Type *Tys[] = {VecTy, Int8Ptr};
  static const Intrinsic::ID LoadInts[3] = {Intrinsic::arm_neon_vld2,
                                            Intrinsic::arm_neon_vld3,
                                            Intrinsic::arm_neon_vld4};
  Function *VldnFunc =
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

    SmallVector<Value *, 2> Ops;
    Ops.push_back(Builder.CreateBitCast(BaseAddr, Int8Ptr));
    Ops.push_back(Builder.getInt32(LI->getAlignment()));

    CallInst *VldN = Builder.CreateCall(VldnFunc, Ops, "vldN");

    // Replace uses of each shufflevector with the corresponding vector loaded
    // by ldN.
    for (unsigned i = 0; i < Shuffles.size(); i++) {
      ShuffleVectorInst *SV = Shuffles[i];
      unsigned Index = Indices[i];

      Value *SubVec = Builder.CreateExtractValue(VldN, Index);

      // Convert the integer vector to pointer vector if the element is pointer.
      if (EltTy->isPointerTy())
        SubVec = Builder.CreateIntToPtr(
            SubVec, VectorType::get(SV->getType()->getVectorElementType(),
                                    VecTy->getVectorNumElements()));

      SubVecs[SV].push_back(SubVec);
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

/// Lower an interleaved store into a vstN intrinsic.
///
/// E.g. Lower an interleaved store (Factor = 3):
///        %i.vec = shuffle <8 x i32> %v0, <8 x i32> %v1,
///                                  <0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11>
///        store <12 x i32> %i.vec, <12 x i32>* %ptr, align 4
///
///      Into:
///        %sub.v0 = shuffle <8 x i32> %v0, <8 x i32> v1, <0, 1, 2, 3>
///        %sub.v1 = shuffle <8 x i32> %v0, <8 x i32> v1, <4, 5, 6, 7>
///        %sub.v2 = shuffle <8 x i32> %v0, <8 x i32> v1, <8, 9, 10, 11>
///        call void llvm.arm.neon.vst3(%ptr, %sub.v0, %sub.v1, %sub.v2, 4)
///
/// Note that the new shufflevectors will be removed and we'll only generate one
/// vst3 instruction in CodeGen.
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
///        call void llvm.arm.neon.vst3(%ptr, %sub.v0, %sub.v1, %sub.v2, 4)
bool ARMTargetLowering::lowerInterleavedStore(StoreInst *SI,
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

    // Convert to the corresponding integer vector.
    Type *IntVecTy =
        VectorType::get(IntTy, Op0->getType()->getVectorNumElements());
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

  assert(isTypeLegal(EVT::getEVT(SubVecTy)) && "Illegal vstN vector type!");

  auto Mask = SVI->getShuffleMask();

  Type *Int8Ptr = Builder.getInt8PtrTy(SI->getPointerAddressSpace());
  Type *Tys[] = {Int8Ptr, SubVecTy};
  static const Intrinsic::ID StoreInts[3] = {Intrinsic::arm_neon_vst2,
                                             Intrinsic::arm_neon_vst3,
                                             Intrinsic::arm_neon_vst4};

  for (unsigned StoreCount = 0; StoreCount < NumStores; ++StoreCount) {
    // If we generating more than one store, we compute the base address of
    // subsequent stores as an offset from the previous.
    if (StoreCount > 0)
      BaseAddr = Builder.CreateConstGEP1_32(BaseAddr, LaneLen * Factor);

    SmallVector<Value *, 6> Ops;
    Ops.push_back(Builder.CreateBitCast(BaseAddr, Int8Ptr));

    Function *VstNFunc =
        Intrinsic::getDeclaration(SI->getModule(), StoreInts[Factor - 2], Tys);

    // Split the shufflevector operands into sub vectors for the new vstN call.
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
        // Note: If all elements in a chunk are undefs, StartMask=0!
        // Note: Filling undef gaps with random elements is ok, since
        // those elements were being written anyway (with undefs).
        // In the case of all undefs we're defaulting to using elems from 0
        // Note: StartMask cannot be negative, it's checked in
        // isReInterleaveMask
        Ops.push_back(Builder.CreateShuffleVector(
            Op0, Op1, createSequentialMask(Builder, StartMask, LaneLen, 0)));
      }
    }

    Ops.push_back(Builder.getInt32(SI->getAlignment()));
    Builder.CreateCall(VstNFunc, Ops);
  }
  return true;
}

enum HABaseType {
  HA_UNKNOWN = 0,
  HA_FLOAT,
  HA_DOUBLE,
  HA_VECT64,
  HA_VECT128
};

static bool isHomogeneousAggregate(Type *Ty, HABaseType &Base,
                                   uint64_t &Members) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    for (unsigned i = 0; i < ST->getNumElements(); ++i) {
      uint64_t SubMembers = 0;
      if (!isHomogeneousAggregate(ST->getElementType(i), Base, SubMembers))
        return false;
      Members += SubMembers;
    }
  } else if (auto *AT = dyn_cast<ArrayType>(Ty)) {
    uint64_t SubMembers = 0;
    if (!isHomogeneousAggregate(AT->getElementType(), Base, SubMembers))
      return false;
    Members += SubMembers * AT->getNumElements();
  } else if (Ty->isFloatTy()) {
    if (Base != HA_UNKNOWN && Base != HA_FLOAT)
      return false;
    Members = 1;
    Base = HA_FLOAT;
  } else if (Ty->isDoubleTy()) {
    if (Base != HA_UNKNOWN && Base != HA_DOUBLE)
      return false;
    Members = 1;
    Base = HA_DOUBLE;
  } else if (auto *VT = dyn_cast<VectorType>(Ty)) {
    Members = 1;
    switch (Base) {
    case HA_FLOAT:
    case HA_DOUBLE:
      return false;
    case HA_VECT64:
      return VT->getBitWidth() == 64;
    case HA_VECT128:
      return VT->getBitWidth() == 128;
    case HA_UNKNOWN:
      switch (VT->getBitWidth()) {
      case 64:
        Base = HA_VECT64;
        return true;
      case 128:
        Base = HA_VECT128;
        return true;
      default:
        return false;
      }
    }
  }

  return (Members > 0 && Members <= 4);
}

/// Return the correct alignment for the current calling convention.
unsigned
ARMTargetLowering::getABIAlignmentForCallingConv(Type *ArgTy,
                                                 DataLayout DL) const {
  if (!ArgTy->isVectorTy())
    return DL.getABITypeAlignment(ArgTy);

  // Avoid over-aligning vector parameters. It would require realigning the
  // stack and waste space for no real benefit.
  return std::min(DL.getABITypeAlignment(ArgTy), DL.getStackAlignment());
}

/// Return true if a type is an AAPCS-VFP homogeneous aggregate or one of
/// [N x i32] or [N x i64]. This allows front-ends to skip emitting padding when
/// passing according to AAPCS rules.
bool ARMTargetLowering::functionArgumentNeedsConsecutiveRegisters(
    Type *Ty, CallingConv::ID CallConv, bool isVarArg) const {
  if (getEffectiveCallingConv(CallConv, isVarArg) !=
      CallingConv::ARM_AAPCS_VFP)
    return false;

  HABaseType Base = HA_UNKNOWN;
  uint64_t Members = 0;
  bool IsHA = isHomogeneousAggregate(Ty, Base, Members);
  LLVM_DEBUG(dbgs() << "isHA: " << IsHA << " "; Ty->dump());

  bool IsIntArray = Ty->isArrayTy() && Ty->getArrayElementType()->isIntegerTy();
  return IsHA || IsIntArray;
}

unsigned ARMTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  // Platforms which do not use SjLj EH may return values in these registers
  // via the personality function.
  return Subtarget->useSjLjEH() ? ARM::NoRegister : ARM::R0;
}

unsigned ARMTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  // Platforms which do not use SjLj EH may return values in these registers
  // via the personality function.
  return Subtarget->useSjLjEH() ? ARM::NoRegister : ARM::R1;
}

void ARMTargetLowering::initializeSplitCSR(MachineBasicBlock *Entry) const {
  // Update IsSplitCSR in ARMFunctionInfo.
  ARMFunctionInfo *AFI = Entry->getParent()->getInfo<ARMFunctionInfo>();
  AFI->setIsSplitCSR(true);
}

void ARMTargetLowering::insertCopiesSplitCSR(
    MachineBasicBlock *Entry,
    const SmallVectorImpl<MachineBasicBlock *> &Exits) const {
  const ARMBaseRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const MCPhysReg *IStart = TRI->getCalleeSavedRegsViaCopy(Entry->getParent());
  if (!IStart)
    return;

  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  MachineRegisterInfo *MRI = &Entry->getParent()->getRegInfo();
  MachineBasicBlock::iterator MBBI = Entry->begin();
  for (const MCPhysReg *I = IStart; *I; ++I) {
    const TargetRegisterClass *RC = nullptr;
    if (ARM::GPRRegClass.contains(*I))
      RC = &ARM::GPRRegClass;
    else if (ARM::DPRRegClass.contains(*I))
      RC = &ARM::DPRRegClass;
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

void ARMTargetLowering::finalizeLowering(MachineFunction &MF) const {
  MF.getFrameInfo().computeMaxCallFrameSize(MF);
  TargetLoweringBase::finalizeLowering(MF);
}
