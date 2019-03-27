//===- SelectionDAGDumper.cpp - Implement SelectionDAG::dump() ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the SelectionDAG::dump method and friends.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/Printable.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "SDNodeDbgValue.h"
#include <cstdint>
#include <iterator>

using namespace llvm;

static cl::opt<bool>
VerboseDAGDumping("dag-dump-verbose", cl::Hidden,
                  cl::desc("Display more information when dumping selection "
                           "DAG nodes."));

std::string SDNode::getOperationName(const SelectionDAG *G) const {
  switch (getOpcode()) {
  default:
    if (getOpcode() < ISD::BUILTIN_OP_END)
      return "<<Unknown DAG Node>>";
    if (isMachineOpcode()) {
      if (G)
        if (const TargetInstrInfo *TII = G->getSubtarget().getInstrInfo())
          if (getMachineOpcode() < TII->getNumOpcodes())
            return TII->getName(getMachineOpcode());
      return "<<Unknown Machine Node #" + utostr(getOpcode()) + ">>";
    }
    if (G) {
      const TargetLowering &TLI = G->getTargetLoweringInfo();
      const char *Name = TLI.getTargetNodeName(getOpcode());
      if (Name) return Name;
      return "<<Unknown Target Node #" + utostr(getOpcode()) + ">>";
    }
    return "<<Unknown Node #" + utostr(getOpcode()) + ">>";

#ifndef NDEBUG
  case ISD::DELETED_NODE:               return "<<Deleted Node!>>";
#endif
  case ISD::PREFETCH:                   return "Prefetch";
  case ISD::ATOMIC_FENCE:               return "AtomicFence";
  case ISD::ATOMIC_CMP_SWAP:            return "AtomicCmpSwap";
  case ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS: return "AtomicCmpSwapWithSuccess";
  case ISD::ATOMIC_SWAP:                return "AtomicSwap";
  case ISD::ATOMIC_LOAD_ADD:            return "AtomicLoadAdd";
  case ISD::ATOMIC_LOAD_SUB:            return "AtomicLoadSub";
  case ISD::ATOMIC_LOAD_AND:            return "AtomicLoadAnd";
  case ISD::ATOMIC_LOAD_CLR:            return "AtomicLoadClr";
  case ISD::ATOMIC_LOAD_OR:             return "AtomicLoadOr";
  case ISD::ATOMIC_LOAD_XOR:            return "AtomicLoadXor";
  case ISD::ATOMIC_LOAD_NAND:           return "AtomicLoadNand";
  case ISD::ATOMIC_LOAD_MIN:            return "AtomicLoadMin";
  case ISD::ATOMIC_LOAD_MAX:            return "AtomicLoadMax";
  case ISD::ATOMIC_LOAD_UMIN:           return "AtomicLoadUMin";
  case ISD::ATOMIC_LOAD_UMAX:           return "AtomicLoadUMax";
  case ISD::ATOMIC_LOAD:                return "AtomicLoad";
  case ISD::ATOMIC_STORE:               return "AtomicStore";
  case ISD::PCMARKER:                   return "PCMarker";
  case ISD::READCYCLECOUNTER:           return "ReadCycleCounter";
  case ISD::SRCVALUE:                   return "SrcValue";
  case ISD::MDNODE_SDNODE:              return "MDNode";
  case ISD::EntryToken:                 return "EntryToken";
  case ISD::TokenFactor:                return "TokenFactor";
  case ISD::AssertSext:                 return "AssertSext";
  case ISD::AssertZext:                 return "AssertZext";

  case ISD::BasicBlock:                 return "BasicBlock";
  case ISD::VALUETYPE:                  return "ValueType";
  case ISD::Register:                   return "Register";
  case ISD::RegisterMask:               return "RegisterMask";
  case ISD::Constant:
    if (cast<ConstantSDNode>(this)->isOpaque())
      return "OpaqueConstant";
    return "Constant";
  case ISD::ConstantFP:                 return "ConstantFP";
  case ISD::GlobalAddress:              return "GlobalAddress";
  case ISD::GlobalTLSAddress:           return "GlobalTLSAddress";
  case ISD::FrameIndex:                 return "FrameIndex";
  case ISD::JumpTable:                  return "JumpTable";
  case ISD::GLOBAL_OFFSET_TABLE:        return "GLOBAL_OFFSET_TABLE";
  case ISD::RETURNADDR:                 return "RETURNADDR";
  case ISD::ADDROFRETURNADDR:           return "ADDROFRETURNADDR";
  case ISD::FRAMEADDR:                  return "FRAMEADDR";
  case ISD::SPONENTRY:                  return "SPONENTRY";
  case ISD::LOCAL_RECOVER:              return "LOCAL_RECOVER";
  case ISD::READ_REGISTER:              return "READ_REGISTER";
  case ISD::WRITE_REGISTER:             return "WRITE_REGISTER";
  case ISD::FRAME_TO_ARGS_OFFSET:       return "FRAME_TO_ARGS_OFFSET";
  case ISD::EH_DWARF_CFA:               return "EH_DWARF_CFA";
  case ISD::EH_RETURN:                  return "EH_RETURN";
  case ISD::EH_SJLJ_SETJMP:             return "EH_SJLJ_SETJMP";
  case ISD::EH_SJLJ_LONGJMP:            return "EH_SJLJ_LONGJMP";
  case ISD::EH_SJLJ_SETUP_DISPATCH:     return "EH_SJLJ_SETUP_DISPATCH";
  case ISD::ConstantPool:               return "ConstantPool";
  case ISD::TargetIndex:                return "TargetIndex";
  case ISD::ExternalSymbol:             return "ExternalSymbol";
  case ISD::BlockAddress:               return "BlockAddress";
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN: {
    unsigned OpNo = getOpcode() == ISD::INTRINSIC_WO_CHAIN ? 0 : 1;
    unsigned IID = cast<ConstantSDNode>(getOperand(OpNo))->getZExtValue();
    if (IID < Intrinsic::num_intrinsics)
      return Intrinsic::getName((Intrinsic::ID)IID, None);
    else if (const TargetIntrinsicInfo *TII = G->getTarget().getIntrinsicInfo())
      return TII->getName(IID);
    llvm_unreachable("Invalid intrinsic ID");
  }

  case ISD::BUILD_VECTOR:               return "BUILD_VECTOR";
  case ISD::TargetConstant:
    if (cast<ConstantSDNode>(this)->isOpaque())
      return "OpaqueTargetConstant";
    return "TargetConstant";
  case ISD::TargetConstantFP:           return "TargetConstantFP";
  case ISD::TargetGlobalAddress:        return "TargetGlobalAddress";
  case ISD::TargetGlobalTLSAddress:     return "TargetGlobalTLSAddress";
  case ISD::TargetFrameIndex:           return "TargetFrameIndex";
  case ISD::TargetJumpTable:            return "TargetJumpTable";
  case ISD::TargetConstantPool:         return "TargetConstantPool";
  case ISD::TargetExternalSymbol:       return "TargetExternalSymbol";
  case ISD::MCSymbol:                   return "MCSymbol";
  case ISD::TargetBlockAddress:         return "TargetBlockAddress";

  case ISD::CopyToReg:                  return "CopyToReg";
  case ISD::CopyFromReg:                return "CopyFromReg";
  case ISD::UNDEF:                      return "undef";
  case ISD::MERGE_VALUES:               return "merge_values";
  case ISD::INLINEASM:                  return "inlineasm";
  case ISD::EH_LABEL:                   return "eh_label";
  case ISD::HANDLENODE:                 return "handlenode";

  // Unary operators
  case ISD::FABS:                       return "fabs";
  case ISD::FMINNUM:                    return "fminnum";
  case ISD::STRICT_FMINNUM:             return "strict_fminnum";
  case ISD::FMAXNUM:                    return "fmaxnum";
  case ISD::STRICT_FMAXNUM:             return "strict_fmaxnum";
  case ISD::FMINNUM_IEEE:               return "fminnum_ieee";
  case ISD::FMAXNUM_IEEE:               return "fmaxnum_ieee";
  case ISD::FMINIMUM:                   return "fminimum";
  case ISD::FMAXIMUM:                   return "fmaximum";
  case ISD::FNEG:                       return "fneg";
  case ISD::FSQRT:                      return "fsqrt";
  case ISD::STRICT_FSQRT:               return "strict_fsqrt";
  case ISD::FCBRT:                      return "fcbrt";
  case ISD::FSIN:                       return "fsin";
  case ISD::STRICT_FSIN:                return "strict_fsin";
  case ISD::FCOS:                       return "fcos";
  case ISD::STRICT_FCOS:                return "strict_fcos";
  case ISD::FSINCOS:                    return "fsincos";
  case ISD::FTRUNC:                     return "ftrunc";
  case ISD::STRICT_FTRUNC:              return "strict_ftrunc";
  case ISD::FFLOOR:                     return "ffloor";
  case ISD::STRICT_FFLOOR:              return "strict_ffloor";
  case ISD::FCEIL:                      return "fceil";
  case ISD::STRICT_FCEIL:               return "strict_fceil";
  case ISD::FRINT:                      return "frint";
  case ISD::STRICT_FRINT:               return "strict_frint";
  case ISD::FNEARBYINT:                 return "fnearbyint";
  case ISD::STRICT_FNEARBYINT:          return "strict_fnearbyint";
  case ISD::FROUND:                     return "fround";
  case ISD::STRICT_FROUND:              return "strict_fround";
  case ISD::FEXP:                       return "fexp";
  case ISD::STRICT_FEXP:                return "strict_fexp";
  case ISD::FEXP2:                      return "fexp2";
  case ISD::STRICT_FEXP2:               return "strict_fexp2";
  case ISD::FLOG:                       return "flog";
  case ISD::STRICT_FLOG:                return "strict_flog";
  case ISD::FLOG2:                      return "flog2";
  case ISD::STRICT_FLOG2:               return "strict_flog2";
  case ISD::FLOG10:                     return "flog10";
  case ISD::STRICT_FLOG10:              return "strict_flog10";

  // Binary operators
  case ISD::ADD:                        return "add";
  case ISD::SUB:                        return "sub";
  case ISD::MUL:                        return "mul";
  case ISD::MULHU:                      return "mulhu";
  case ISD::MULHS:                      return "mulhs";
  case ISD::SDIV:                       return "sdiv";
  case ISD::UDIV:                       return "udiv";
  case ISD::SREM:                       return "srem";
  case ISD::UREM:                       return "urem";
  case ISD::SMUL_LOHI:                  return "smul_lohi";
  case ISD::UMUL_LOHI:                  return "umul_lohi";
  case ISD::SDIVREM:                    return "sdivrem";
  case ISD::UDIVREM:                    return "udivrem";
  case ISD::AND:                        return "and";
  case ISD::OR:                         return "or";
  case ISD::XOR:                        return "xor";
  case ISD::SHL:                        return "shl";
  case ISD::SRA:                        return "sra";
  case ISD::SRL:                        return "srl";
  case ISD::ROTL:                       return "rotl";
  case ISD::ROTR:                       return "rotr";
  case ISD::FSHL:                       return "fshl";
  case ISD::FSHR:                       return "fshr";
  case ISD::FADD:                       return "fadd";
  case ISD::STRICT_FADD:                return "strict_fadd";
  case ISD::FSUB:                       return "fsub";
  case ISD::STRICT_FSUB:                return "strict_fsub";
  case ISD::FMUL:                       return "fmul";
  case ISD::STRICT_FMUL:                return "strict_fmul";
  case ISD::FDIV:                       return "fdiv";
  case ISD::STRICT_FDIV:                return "strict_fdiv";
  case ISD::FMA:                        return "fma";
  case ISD::STRICT_FMA:                 return "strict_fma";
  case ISD::FMAD:                       return "fmad";
  case ISD::FREM:                       return "frem";
  case ISD::STRICT_FREM:                return "strict_frem";
  case ISD::FCOPYSIGN:                  return "fcopysign";
  case ISD::FGETSIGN:                   return "fgetsign";
  case ISD::FCANONICALIZE:              return "fcanonicalize";
  case ISD::FPOW:                       return "fpow";
  case ISD::STRICT_FPOW:                return "strict_fpow";
  case ISD::SMIN:                       return "smin";
  case ISD::SMAX:                       return "smax";
  case ISD::UMIN:                       return "umin";
  case ISD::UMAX:                       return "umax";

  case ISD::FPOWI:                      return "fpowi";
  case ISD::STRICT_FPOWI:               return "strict_fpowi";
  case ISD::SETCC:                      return "setcc";
  case ISD::SETCCCARRY:                 return "setcccarry";
  case ISD::SELECT:                     return "select";
  case ISD::VSELECT:                    return "vselect";
  case ISD::SELECT_CC:                  return "select_cc";
  case ISD::INSERT_VECTOR_ELT:          return "insert_vector_elt";
  case ISD::EXTRACT_VECTOR_ELT:         return "extract_vector_elt";
  case ISD::CONCAT_VECTORS:             return "concat_vectors";
  case ISD::INSERT_SUBVECTOR:           return "insert_subvector";
  case ISD::EXTRACT_SUBVECTOR:          return "extract_subvector";
  case ISD::SCALAR_TO_VECTOR:           return "scalar_to_vector";
  case ISD::VECTOR_SHUFFLE:             return "vector_shuffle";
  case ISD::CARRY_FALSE:                return "carry_false";
  case ISD::ADDC:                       return "addc";
  case ISD::ADDE:                       return "adde";
  case ISD::ADDCARRY:                   return "addcarry";
  case ISD::SADDO:                      return "saddo";
  case ISD::UADDO:                      return "uaddo";
  case ISD::SSUBO:                      return "ssubo";
  case ISD::USUBO:                      return "usubo";
  case ISD::SMULO:                      return "smulo";
  case ISD::UMULO:                      return "umulo";
  case ISD::SUBC:                       return "subc";
  case ISD::SUBE:                       return "sube";
  case ISD::SUBCARRY:                   return "subcarry";
  case ISD::SHL_PARTS:                  return "shl_parts";
  case ISD::SRA_PARTS:                  return "sra_parts";
  case ISD::SRL_PARTS:                  return "srl_parts";

  case ISD::SADDSAT:                    return "saddsat";
  case ISD::UADDSAT:                    return "uaddsat";
  case ISD::SSUBSAT:                    return "ssubsat";
  case ISD::USUBSAT:                    return "usubsat";
  case ISD::SMULFIX:                    return "smulfix";

  // Conversion operators.
  case ISD::SIGN_EXTEND:                return "sign_extend";
  case ISD::ZERO_EXTEND:                return "zero_extend";
  case ISD::ANY_EXTEND:                 return "any_extend";
  case ISD::SIGN_EXTEND_INREG:          return "sign_extend_inreg";
  case ISD::ANY_EXTEND_VECTOR_INREG:    return "any_extend_vector_inreg";
  case ISD::SIGN_EXTEND_VECTOR_INREG:   return "sign_extend_vector_inreg";
  case ISD::ZERO_EXTEND_VECTOR_INREG:   return "zero_extend_vector_inreg";
  case ISD::TRUNCATE:                   return "truncate";
  case ISD::FP_ROUND:                   return "fp_round";
  case ISD::FLT_ROUNDS_:                return "flt_rounds";
  case ISD::FP_ROUND_INREG:             return "fp_round_inreg";
  case ISD::FP_EXTEND:                  return "fp_extend";

  case ISD::SINT_TO_FP:                 return "sint_to_fp";
  case ISD::UINT_TO_FP:                 return "uint_to_fp";
  case ISD::FP_TO_SINT:                 return "fp_to_sint";
  case ISD::FP_TO_UINT:                 return "fp_to_uint";
  case ISD::BITCAST:                    return "bitcast";
  case ISD::ADDRSPACECAST:              return "addrspacecast";
  case ISD::FP16_TO_FP:                 return "fp16_to_fp";
  case ISD::FP_TO_FP16:                 return "fp_to_fp16";

    // Control flow instructions
  case ISD::BR:                         return "br";
  case ISD::BRIND:                      return "brind";
  case ISD::BR_JT:                      return "br_jt";
  case ISD::BRCOND:                     return "brcond";
  case ISD::BR_CC:                      return "br_cc";
  case ISD::CALLSEQ_START:              return "callseq_start";
  case ISD::CALLSEQ_END:                return "callseq_end";

    // EH instructions
  case ISD::CATCHRET:                   return "catchret";
  case ISD::CLEANUPRET:                 return "cleanupret";

    // Other operators
  case ISD::LOAD:                       return "load";
  case ISD::STORE:                      return "store";
  case ISD::MLOAD:                      return "masked_load";
  case ISD::MSTORE:                     return "masked_store";
  case ISD::MGATHER:                    return "masked_gather";
  case ISD::MSCATTER:                   return "masked_scatter";
  case ISD::VAARG:                      return "vaarg";
  case ISD::VACOPY:                     return "vacopy";
  case ISD::VAEND:                      return "vaend";
  case ISD::VASTART:                    return "vastart";
  case ISD::DYNAMIC_STACKALLOC:         return "dynamic_stackalloc";
  case ISD::EXTRACT_ELEMENT:            return "extract_element";
  case ISD::BUILD_PAIR:                 return "build_pair";
  case ISD::STACKSAVE:                  return "stacksave";
  case ISD::STACKRESTORE:               return "stackrestore";
  case ISD::TRAP:                       return "trap";
  case ISD::DEBUGTRAP:                  return "debugtrap";
  case ISD::LIFETIME_START:             return "lifetime.start";
  case ISD::LIFETIME_END:               return "lifetime.end";
  case ISD::GC_TRANSITION_START:        return "gc_transition.start";
  case ISD::GC_TRANSITION_END:          return "gc_transition.end";
  case ISD::GET_DYNAMIC_AREA_OFFSET:    return "get.dynamic.area.offset";

  // Bit manipulation
  case ISD::ABS:                        return "abs";
  case ISD::BITREVERSE:                 return "bitreverse";
  case ISD::BSWAP:                      return "bswap";
  case ISD::CTPOP:                      return "ctpop";
  case ISD::CTTZ:                       return "cttz";
  case ISD::CTTZ_ZERO_UNDEF:            return "cttz_zero_undef";
  case ISD::CTLZ:                       return "ctlz";
  case ISD::CTLZ_ZERO_UNDEF:            return "ctlz_zero_undef";

  // Trampolines
  case ISD::INIT_TRAMPOLINE:            return "init_trampoline";
  case ISD::ADJUST_TRAMPOLINE:          return "adjust_trampoline";

  case ISD::CONDCODE:
    switch (cast<CondCodeSDNode>(this)->get()) {
    default: llvm_unreachable("Unknown setcc condition!");
    case ISD::SETOEQ:                   return "setoeq";
    case ISD::SETOGT:                   return "setogt";
    case ISD::SETOGE:                   return "setoge";
    case ISD::SETOLT:                   return "setolt";
    case ISD::SETOLE:                   return "setole";
    case ISD::SETONE:                   return "setone";

    case ISD::SETO:                     return "seto";
    case ISD::SETUO:                    return "setuo";
    case ISD::SETUEQ:                   return "setueq";
    case ISD::SETUGT:                   return "setugt";
    case ISD::SETUGE:                   return "setuge";
    case ISD::SETULT:                   return "setult";
    case ISD::SETULE:                   return "setule";
    case ISD::SETUNE:                   return "setune";

    case ISD::SETEQ:                    return "seteq";
    case ISD::SETGT:                    return "setgt";
    case ISD::SETGE:                    return "setge";
    case ISD::SETLT:                    return "setlt";
    case ISD::SETLE:                    return "setle";
    case ISD::SETNE:                    return "setne";

    case ISD::SETTRUE:                  return "settrue";
    case ISD::SETTRUE2:                 return "settrue2";
    case ISD::SETFALSE:                 return "setfalse";
    case ISD::SETFALSE2:                return "setfalse2";
    }
  case ISD::VECREDUCE_FADD:             return "vecreduce_fadd";
  case ISD::VECREDUCE_STRICT_FADD:      return "vecreduce_strict_fadd";
  case ISD::VECREDUCE_FMUL:             return "vecreduce_fmul";
  case ISD::VECREDUCE_STRICT_FMUL:      return "vecreduce_strict_fmul";
  case ISD::VECREDUCE_ADD:              return "vecreduce_add";
  case ISD::VECREDUCE_MUL:              return "vecreduce_mul";
  case ISD::VECREDUCE_AND:              return "vecreduce_and";
  case ISD::VECREDUCE_OR:               return "vecreduce_or";
  case ISD::VECREDUCE_XOR:              return "vecreduce_xor";
  case ISD::VECREDUCE_SMAX:             return "vecreduce_smax";
  case ISD::VECREDUCE_SMIN:             return "vecreduce_smin";
  case ISD::VECREDUCE_UMAX:             return "vecreduce_umax";
  case ISD::VECREDUCE_UMIN:             return "vecreduce_umin";
  case ISD::VECREDUCE_FMAX:             return "vecreduce_fmax";
  case ISD::VECREDUCE_FMIN:             return "vecreduce_fmin";
  }
}

const char *SDNode::getIndexedModeName(ISD::MemIndexedMode AM) {
  switch (AM) {
  default:              return "";
  case ISD::PRE_INC:    return "<pre-inc>";
  case ISD::PRE_DEC:    return "<pre-dec>";
  case ISD::POST_INC:   return "<post-inc>";
  case ISD::POST_DEC:   return "<post-dec>";
  }
}

static Printable PrintNodeId(const SDNode &Node) {
  return Printable([&Node](raw_ostream &OS) {
#ifndef NDEBUG
    OS << 't' << Node.PersistentId;
#else
    OS << (const void*)&Node;
#endif
  });
}

// Print the MMO with more information from the SelectionDAG.
static void printMemOperand(raw_ostream &OS, const MachineMemOperand &MMO,
                            const MachineFunction *MF, const Module *M,
                            const MachineFrameInfo *MFI,
                            const TargetInstrInfo *TII, LLVMContext &Ctx) {
  ModuleSlotTracker MST(M);
  if (MF)
    MST.incorporateFunction(MF->getFunction());
  SmallVector<StringRef, 0> SSNs;
  MMO.print(OS, MST, SSNs, Ctx, MFI, TII);
}

static void printMemOperand(raw_ostream &OS, const MachineMemOperand &MMO,
                            const SelectionDAG *G) {
  if (G) {
    const MachineFunction *MF = &G->getMachineFunction();
    return printMemOperand(OS, MMO, MF, MF->getFunction().getParent(),
                           &MF->getFrameInfo(), G->getSubtarget().getInstrInfo(),
                           *G->getContext());
  } else {
    LLVMContext Ctx;
    return printMemOperand(OS, MMO, /*MF=*/nullptr, /*M=*/nullptr,
                           /*MFI=*/nullptr, /*TII=*/nullptr, Ctx);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SDNode::dump() const { dump(nullptr); }

LLVM_DUMP_METHOD void SDNode::dump(const SelectionDAG *G) const {
  print(dbgs(), G);
  dbgs() << '\n';
}
#endif

void SDNode::print_types(raw_ostream &OS, const SelectionDAG *G) const {
  for (unsigned i = 0, e = getNumValues(); i != e; ++i) {
    if (i) OS << ",";
    if (getValueType(i) == MVT::Other)
      OS << "ch";
    else
      OS << getValueType(i).getEVTString();
  }
}

void SDNode::print_details(raw_ostream &OS, const SelectionDAG *G) const {
  if (getFlags().hasNoUnsignedWrap())
    OS << " nuw";

  if (getFlags().hasNoSignedWrap())
    OS << " nsw";

  if (getFlags().hasExact())
    OS << " exact";

  if (getFlags().hasNoNaNs())
    OS << " nnan";

  if (getFlags().hasNoInfs())
    OS << " ninf";

  if (getFlags().hasNoSignedZeros())
    OS << " nsz";

  if (getFlags().hasAllowReciprocal())
    OS << " arcp";

  if (getFlags().hasAllowContract())
    OS << " contract";

  if (getFlags().hasApproximateFuncs())
    OS << " afn";

  if (getFlags().hasAllowReassociation())
    OS << " reassoc";

  if (getFlags().hasVectorReduction())
    OS << " vector-reduction";

  if (const MachineSDNode *MN = dyn_cast<MachineSDNode>(this)) {
    if (!MN->memoperands_empty()) {
      OS << "<";
      OS << "Mem:";
      for (MachineSDNode::mmo_iterator i = MN->memoperands_begin(),
           e = MN->memoperands_end(); i != e; ++i) {
        printMemOperand(OS, **i, G);
        if (std::next(i) != e)
          OS << " ";
      }
      OS << ">";
    }
  } else if (const ShuffleVectorSDNode *SVN =
               dyn_cast<ShuffleVectorSDNode>(this)) {
    OS << "<";
    for (unsigned i = 0, e = ValueList[0].getVectorNumElements(); i != e; ++i) {
      int Idx = SVN->getMaskElt(i);
      if (i) OS << ",";
      if (Idx < 0)
        OS << "u";
      else
        OS << Idx;
    }
    OS << ">";
  } else if (const ConstantSDNode *CSDN = dyn_cast<ConstantSDNode>(this)) {
    OS << '<' << CSDN->getAPIntValue() << '>';
  } else if (const ConstantFPSDNode *CSDN = dyn_cast<ConstantFPSDNode>(this)) {
    if (&CSDN->getValueAPF().getSemantics() == &APFloat::IEEEsingle())
      OS << '<' << CSDN->getValueAPF().convertToFloat() << '>';
    else if (&CSDN->getValueAPF().getSemantics() == &APFloat::IEEEdouble())
      OS << '<' << CSDN->getValueAPF().convertToDouble() << '>';
    else {
      OS << "<APFloat(";
      CSDN->getValueAPF().bitcastToAPInt().print(OS, false);
      OS << ")>";
    }
  } else if (const GlobalAddressSDNode *GADN =
             dyn_cast<GlobalAddressSDNode>(this)) {
    int64_t offset = GADN->getOffset();
    OS << '<';
    GADN->getGlobal()->printAsOperand(OS);
    OS << '>';
    if (offset > 0)
      OS << " + " << offset;
    else
      OS << " " << offset;
    if (unsigned int TF = GADN->getTargetFlags())
      OS << " [TF=" << TF << ']';
  } else if (const FrameIndexSDNode *FIDN = dyn_cast<FrameIndexSDNode>(this)) {
    OS << "<" << FIDN->getIndex() << ">";
  } else if (const JumpTableSDNode *JTDN = dyn_cast<JumpTableSDNode>(this)) {
    OS << "<" << JTDN->getIndex() << ">";
    if (unsigned int TF = JTDN->getTargetFlags())
      OS << " [TF=" << TF << ']';
  } else if (const ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(this)){
    int offset = CP->getOffset();
    if (CP->isMachineConstantPoolEntry())
      OS << "<" << *CP->getMachineCPVal() << ">";
    else
      OS << "<" << *CP->getConstVal() << ">";
    if (offset > 0)
      OS << " + " << offset;
    else
      OS << " " << offset;
    if (unsigned int TF = CP->getTargetFlags())
      OS << " [TF=" << TF << ']';
  } else if (const TargetIndexSDNode *TI = dyn_cast<TargetIndexSDNode>(this)) {
    OS << "<" << TI->getIndex() << '+' << TI->getOffset() << ">";
    if (unsigned TF = TI->getTargetFlags())
      OS << " [TF=" << TF << ']';
  } else if (const BasicBlockSDNode *BBDN = dyn_cast<BasicBlockSDNode>(this)) {
    OS << "<";
    const Value *LBB = (const Value*)BBDN->getBasicBlock()->getBasicBlock();
    if (LBB)
      OS << LBB->getName() << " ";
    OS << (const void*)BBDN->getBasicBlock() << ">";
  } else if (const RegisterSDNode *R = dyn_cast<RegisterSDNode>(this)) {
    OS << ' ' << printReg(R->getReg(),
                          G ? G->getSubtarget().getRegisterInfo() : nullptr);
  } else if (const ExternalSymbolSDNode *ES =
             dyn_cast<ExternalSymbolSDNode>(this)) {
    OS << "'" << ES->getSymbol() << "'";
    if (unsigned int TF = ES->getTargetFlags())
      OS << " [TF=" << TF << ']';
  } else if (const SrcValueSDNode *M = dyn_cast<SrcValueSDNode>(this)) {
    if (M->getValue())
      OS << "<" << M->getValue() << ">";
    else
      OS << "<null>";
  } else if (const MDNodeSDNode *MD = dyn_cast<MDNodeSDNode>(this)) {
    if (MD->getMD())
      OS << "<" << MD->getMD() << ">";
    else
      OS << "<null>";
  } else if (const VTSDNode *N = dyn_cast<VTSDNode>(this)) {
    OS << ":" << N->getVT().getEVTString();
  }
  else if (const LoadSDNode *LD = dyn_cast<LoadSDNode>(this)) {
    OS << "<";

    printMemOperand(OS, *LD->getMemOperand(), G);

    bool doExt = true;
    switch (LD->getExtensionType()) {
    default: doExt = false; break;
    case ISD::EXTLOAD:  OS << ", anyext"; break;
    case ISD::SEXTLOAD: OS << ", sext"; break;
    case ISD::ZEXTLOAD: OS << ", zext"; break;
    }
    if (doExt)
      OS << " from " << LD->getMemoryVT().getEVTString();

    const char *AM = getIndexedModeName(LD->getAddressingMode());
    if (*AM)
      OS << ", " << AM;

    OS << ">";
  } else if (const StoreSDNode *ST = dyn_cast<StoreSDNode>(this)) {
    OS << "<";
    printMemOperand(OS, *ST->getMemOperand(), G);

    if (ST->isTruncatingStore())
      OS << ", trunc to " << ST->getMemoryVT().getEVTString();

    const char *AM = getIndexedModeName(ST->getAddressingMode());
    if (*AM)
      OS << ", " << AM;

    OS << ">";
  } else if (const MemSDNode* M = dyn_cast<MemSDNode>(this)) {
    OS << "<";
    printMemOperand(OS, *M->getMemOperand(), G);
    OS << ">";
  } else if (const BlockAddressSDNode *BA =
               dyn_cast<BlockAddressSDNode>(this)) {
    int64_t offset = BA->getOffset();
    OS << "<";
    BA->getBlockAddress()->getFunction()->printAsOperand(OS, false);
    OS << ", ";
    BA->getBlockAddress()->getBasicBlock()->printAsOperand(OS, false);
    OS << ">";
    if (offset > 0)
      OS << " + " << offset;
    else
      OS << " " << offset;
    if (unsigned int TF = BA->getTargetFlags())
      OS << " [TF=" << TF << ']';
  } else if (const AddrSpaceCastSDNode *ASC =
               dyn_cast<AddrSpaceCastSDNode>(this)) {
    OS << '['
       << ASC->getSrcAddressSpace()
       << " -> "
       << ASC->getDestAddressSpace()
       << ']';
  }

  if (VerboseDAGDumping) {
    if (unsigned Order = getIROrder())
        OS << " [ORD=" << Order << ']';

    if (getNodeId() != -1)
      OS << " [ID=" << getNodeId() << ']';
    if (!(isa<ConstantSDNode>(this) || (isa<ConstantFPSDNode>(this))))
      OS << "# D:" << isDivergent();

    if (!G)
      return;

    DILocation *L = getDebugLoc();
    if (!L)
      return;

    if (auto *Scope = L->getScope())
      OS << Scope->getFilename();
    else
      OS << "<unknown>";
    OS << ':' << L->getLine();
    if (unsigned C = L->getColumn())
      OS << ':' << C;

    for (SDDbgValue *Dbg : G->GetDbgValues(this)) {
      if (Dbg->getKind() != SDDbgValue::SDNODE || Dbg->isInvalidated())
        continue;
      Dbg->dump(OS);
    }
  }
}

LLVM_DUMP_METHOD void SDDbgValue::dump(raw_ostream &OS) const {
 OS << " DbgVal";
 if (kind==SDNODE)
   OS << '(' << u.s.ResNo << ')';
 OS << ":\"" << Var->getName() << '"';
#ifndef NDEBUG
 if (Expr->getNumElements())
   Expr->dump();
#endif
}

/// Return true if this node is so simple that we should just print it inline
/// if it appears as an operand.
static bool shouldPrintInline(const SDNode &Node) {
  if (Node.getOpcode() == ISD::EntryToken)
    return false;
  return Node.getNumOperands() == 0;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
static void DumpNodes(const SDNode *N, unsigned indent, const SelectionDAG *G) {
  for (const SDValue &Op : N->op_values()) {
    if (shouldPrintInline(*Op.getNode()))
      continue;
    if (Op.getNode()->hasOneUse())
      DumpNodes(Op.getNode(), indent+2, G);
  }

  dbgs().indent(indent);
  N->dump(G);
}

LLVM_DUMP_METHOD void SelectionDAG::dump() const {
  dbgs() << "SelectionDAG has " << AllNodes.size() << " nodes:\n";

  for (allnodes_const_iterator I = allnodes_begin(), E = allnodes_end();
       I != E; ++I) {
    const SDNode *N = &*I;
    if (!N->hasOneUse() && N != getRoot().getNode() &&
        (!shouldPrintInline(*N) || N->use_empty()))
      DumpNodes(N, 2, this);
  }

  if (getRoot().getNode()) DumpNodes(getRoot().getNode(), 2, this);
  dbgs() << "\n\n";
}
#endif

void SDNode::printr(raw_ostream &OS, const SelectionDAG *G) const {
  OS << PrintNodeId(*this) << ": ";
  print_types(OS, G);
  OS << " = " << getOperationName(G);
  print_details(OS, G);
}

static bool printOperand(raw_ostream &OS, const SelectionDAG *G,
                         const SDValue Value) {
  if (!Value.getNode()) {
    OS << "<null>";
    return false;
  } else if (shouldPrintInline(*Value.getNode())) {
    OS << Value->getOperationName(G) << ':';
    Value->print_types(OS, G);
    Value->print_details(OS, G);
    return true;
  } else {
    OS << PrintNodeId(*Value.getNode());
    if (unsigned RN = Value.getResNo())
      OS << ':' << RN;
    return false;
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
using VisitedSDNodeSet = SmallPtrSet<const SDNode *, 32>;

static void DumpNodesr(raw_ostream &OS, const SDNode *N, unsigned indent,
                       const SelectionDAG *G, VisitedSDNodeSet &once) {
  if (!once.insert(N).second) // If we've been here before, return now.
    return;

  // Dump the current SDNode, but don't end the line yet.
  OS.indent(indent);
  N->printr(OS, G);

  // Having printed this SDNode, walk the children:
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    if (i) OS << ",";
    OS << " ";

    const SDValue Op = N->getOperand(i);
    bool printedInline = printOperand(OS, G, Op);
    if (printedInline)
      once.insert(Op.getNode());
  }

  OS << "\n";

  // Dump children that have grandchildren on their own line(s).
  for (const SDValue &Op : N->op_values())
    DumpNodesr(OS, Op.getNode(), indent+2, G, once);
}

LLVM_DUMP_METHOD void SDNode::dumpr() const {
  VisitedSDNodeSet once;
  DumpNodesr(dbgs(), this, 0, nullptr, once);
}

LLVM_DUMP_METHOD void SDNode::dumpr(const SelectionDAG *G) const {
  VisitedSDNodeSet once;
  DumpNodesr(dbgs(), this, 0, G, once);
}
#endif

static void printrWithDepthHelper(raw_ostream &OS, const SDNode *N,
                                  const SelectionDAG *G, unsigned depth,
                                  unsigned indent) {
  if (depth == 0)
    return;

  OS.indent(indent);

  N->print(OS, G);

  if (depth < 1)
    return;

  for (const SDValue &Op : N->op_values()) {
    // Don't follow chain operands.
    if (Op.getValueType() == MVT::Other)
      continue;
    OS << '\n';
    printrWithDepthHelper(OS, Op.getNode(), G, depth-1, indent+2);
  }
}

void SDNode::printrWithDepth(raw_ostream &OS, const SelectionDAG *G,
                            unsigned depth) const {
  printrWithDepthHelper(OS, this, G, depth, 0);
}

void SDNode::printrFull(raw_ostream &OS, const SelectionDAG *G) const {
  // Don't print impossibly deep things.
  printrWithDepth(OS, G, 10);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
void SDNode::dumprWithDepth(const SelectionDAG *G, unsigned depth) const {
  printrWithDepth(dbgs(), G, depth);
}

LLVM_DUMP_METHOD void SDNode::dumprFull(const SelectionDAG *G) const {
  // Don't print impossibly deep things.
  dumprWithDepth(G, 10);
}
#endif

void SDNode::print(raw_ostream &OS, const SelectionDAG *G) const {
  printr(OS, G);
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    if (i) OS << ", "; else OS << " ";
    printOperand(OS, G, getOperand(i));
  }
  if (DebugLoc DL = getDebugLoc()) {
    OS << ", ";
    DL.print(OS);
  }
}
