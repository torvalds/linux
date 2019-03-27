//===-- BPFInstrInfo.td - Target Description for BPF Target ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the BPF instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "BPFInstrFormats.td"

// Instruction Operands and Patterns

// These are target-independent nodes, but have target-specific formats.
def SDT_BPFCallSeqStart : SDCallSeqStart<[SDTCisVT<0, iPTR>,
                                          SDTCisVT<1, iPTR>]>;
def SDT_BPFCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, iPTR>, SDTCisVT<1, iPTR>]>;
def SDT_BPFCall         : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
def SDT_BPFSetFlag      : SDTypeProfile<0, 3, [SDTCisSameAs<0, 1>]>;
def SDT_BPFSelectCC     : SDTypeProfile<1, 5, [SDTCisSameAs<1, 2>,
                                               SDTCisSameAs<0, 4>,
                                               SDTCisSameAs<4, 5>]>;
def SDT_BPFBrCC         : SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>,
                                               SDTCisVT<3, OtherVT>]>;
def SDT_BPFWrapper      : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                               SDTCisPtrTy<0>]>;
def SDT_BPFMEMCPY       : SDTypeProfile<0, 4, [SDTCisVT<0, i64>,
                                               SDTCisVT<1, i64>,
                                               SDTCisVT<2, i64>,
                                               SDTCisVT<3, i64>]>;

def BPFcall         : SDNode<"BPFISD::CALL", SDT_BPFCall,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                              SDNPVariadic]>;
def BPFretflag      : SDNode<"BPFISD::RET_FLAG", SDTNone,
                             [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def BPFcallseq_start: SDNode<"ISD::CALLSEQ_START", SDT_BPFCallSeqStart,
                             [SDNPHasChain, SDNPOutGlue]>;
def BPFcallseq_end  : SDNode<"ISD::CALLSEQ_END",   SDT_BPFCallSeqEnd,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def BPFbrcc         : SDNode<"BPFISD::BR_CC", SDT_BPFBrCC,
                             [SDNPHasChain, SDNPOutGlue, SDNPInGlue]>;

def BPFselectcc     : SDNode<"BPFISD::SELECT_CC", SDT_BPFSelectCC, [SDNPInGlue]>;
def BPFWrapper      : SDNode<"BPFISD::Wrapper", SDT_BPFWrapper>;
def BPFmemcpy       : SDNode<"BPFISD::MEMCPY", SDT_BPFMEMCPY,
                             [SDNPHasChain, SDNPInGlue, SDNPOutGlue,
                              SDNPMayStore, SDNPMayLoad]>;
def BPFIsLittleEndian : Predicate<"CurDAG->getDataLayout().isLittleEndian()">;
def BPFIsBigEndian    : Predicate<"!CurDAG->getDataLayout().isLittleEndian()">;
def BPFHasALU32 : Predicate<"Subtarget->getHasAlu32()">;
def BPFNoALU32 : Predicate<"!Subtarget->getHasAlu32()">;

def brtarget : Operand<OtherVT> {
  let PrintMethod = "printBrTargetOperand";
}
def calltarget : Operand<i64>;

def u64imm   : Operand<i64> {
  let PrintMethod = "printImm64Operand";
}

def i64immSExt32 : PatLeaf<(i64 imm),
                [{return isInt<32>(N->getSExtValue()); }]>;
def i32immSExt32 : PatLeaf<(i32 imm),
                [{return isInt<32>(N->getSExtValue()); }]>;

// Addressing modes.
def ADDRri : ComplexPattern<i64, 2, "SelectAddr", [], []>;
def FIri : ComplexPattern<i64, 2, "SelectFIAddr", [add, or], []>;

// Address operands
def MEMri : Operand<i64> {
  let PrintMethod = "printMemOperand";
  let EncoderMethod = "getMemoryOpValue";
  let DecoderMethod = "decodeMemoryOpValue";
  let MIOperandInfo = (ops GPR, i16imm);
}

// Conditional code predicates - used for pattern matching for jump instructions
def BPF_CC_EQ  : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETEQ);}]>;
def BPF_CC_NE  : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETNE);}]>;
def BPF_CC_GE  : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETGE);}]>;
def BPF_CC_GT  : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETGT);}]>;
def BPF_CC_GTU : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETUGT);}]>;
def BPF_CC_GEU : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETUGE);}]>;
def BPF_CC_LE  : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETLE);}]>;
def BPF_CC_LT  : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETLT);}]>;
def BPF_CC_LTU : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETULT);}]>;
def BPF_CC_LEU : PatLeaf<(i64 imm),
                         [{return (N->getZExtValue() == ISD::SETULE);}]>;

// For arithmetic and jump instructions the 8-bit 'code'
// field is divided into three parts:
//
//  +----------------+--------+--------------------+
//  |   4 bits       |  1 bit |   3 bits           |
//  | operation code | source | instruction class  |
//  +----------------+--------+--------------------+
//  (MSB)                                      (LSB)
class TYPE_ALU_JMP<bits<4> op, bits<1> srctype,
                   dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstBPF<outs, ins, asmstr, pattern> {

  let Inst{63-60} = op;
  let Inst{59} = srctype;
}

//For load and store instructions the 8-bit 'code' field is divided as:
//
//  +--------+--------+-------------------+
//  | 3 bits | 2 bits |   3 bits          |
//  |  mode  |  size  | instruction class |
//  +--------+--------+-------------------+
//  (MSB)                             (LSB)
class TYPE_LD_ST<bits<3> mode, bits<2> size,
                 dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstBPF<outs, ins, asmstr, pattern> {

  let Inst{63-61} = mode;
  let Inst{60-59} = size;
}

// jump instructions
class JMP_RR<BPFJumpOp Opc, string OpcodeStr, PatLeaf Cond>
    : TYPE_ALU_JMP<Opc.Value, BPF_X.Value,
                   (outs),
                   (ins GPR:$dst, GPR:$src, brtarget:$BrDst),
                   "if $dst "#OpcodeStr#" $src goto $BrDst",
                   [(BPFbrcc i64:$dst, i64:$src, Cond, bb:$BrDst)]> {
  bits<4> dst;
  bits<4> src;
  bits<16> BrDst;

  let Inst{55-52} = src;
  let Inst{51-48} = dst;
  let Inst{47-32} = BrDst;
  let BPFClass = BPF_JMP;
}

class JMP_RI<BPFJumpOp Opc, string OpcodeStr, PatLeaf Cond>
    : TYPE_ALU_JMP<Opc.Value, BPF_K.Value,
                   (outs),
                   (ins GPR:$dst, i64imm:$imm, brtarget:$BrDst),
                   "if $dst "#OpcodeStr#" $imm goto $BrDst",
                   [(BPFbrcc i64:$dst, i64immSExt32:$imm, Cond, bb:$BrDst)]> {
  bits<4> dst;
  bits<16> BrDst;
  bits<32> imm;

  let Inst{51-48} = dst;
  let Inst{47-32} = BrDst;
  let Inst{31-0} = imm;
  let BPFClass = BPF_JMP;
}

multiclass J<BPFJumpOp Opc, string OpcodeStr, PatLeaf Cond> {
  def _rr : JMP_RR<Opc, OpcodeStr, Cond>;
  def _ri : JMP_RI<Opc, OpcodeStr, Cond>;
}

let isBranch = 1, isTerminator = 1, hasDelaySlot=0 in {
// cmp+goto instructions
defm JEQ  : J<BPF_JEQ, "==",  BPF_CC_EQ>;
defm JUGT : J<BPF_JGT, ">", BPF_CC_GTU>;
defm JUGE : J<BPF_JGE, ">=", BPF_CC_GEU>;
defm JNE  : J<BPF_JNE, "!=",  BPF_CC_NE>;
defm JSGT : J<BPF_JSGT, "s>", BPF_CC_GT>;
defm JSGE : J<BPF_JSGE, "s>=", BPF_CC_GE>;
defm JULT : J<BPF_JLT, "<", BPF_CC_LTU>;
defm JULE : J<BPF_JLE, "<=", BPF_CC_LEU>;
defm JSLT : J<BPF_JSLT, "s<", BPF_CC_LT>;
defm JSLE : J<BPF_JSLE, "s<=", BPF_CC_LE>;
}

// ALU instructions
class ALU_RI<BPFOpClass Class, BPFArithOp Opc,
             dag outs, dag ins, string asmstr, list<dag> pattern>
    : TYPE_ALU_JMP<Opc.Value, BPF_K.Value, outs, ins, asmstr, pattern> {
  bits<4> dst;
  bits<32> imm;

  let Inst{51-48} = dst;
  let Inst{31-0} = imm;
  let BPFClass = Class;
}

class ALU_RR<BPFOpClass Class, BPFArithOp Opc,
             dag outs, dag ins, string asmstr, list<dag> pattern>
    : TYPE_ALU_JMP<Opc.Value, BPF_X.Value, outs, ins, asmstr, pattern> {
  bits<4> dst;
  bits<4> src;

  let Inst{55-52} = src;
  let Inst{51-48} = dst;
  let BPFClass = Class;
}

multiclass ALU<BPFArithOp Opc, string OpcodeStr, SDNode OpNode> {
  def _rr : ALU_RR<BPF_ALU64, Opc,
                   (outs GPR:$dst),
                   (ins GPR:$src2, GPR:$src),
                   "$dst "#OpcodeStr#" $src",
                   [(set GPR:$dst, (OpNode i64:$src2, i64:$src))]>;
  def _ri : ALU_RI<BPF_ALU64, Opc,
                   (outs GPR:$dst),
                   (ins GPR:$src2, i64imm:$imm),
                   "$dst "#OpcodeStr#" $imm",
                   [(set GPR:$dst, (OpNode GPR:$src2, i64immSExt32:$imm))]>;
  def _rr_32 : ALU_RR<BPF_ALU, Opc,
                   (outs GPR32:$dst),
                   (ins GPR32:$src2, GPR32:$src),
                   "$dst "#OpcodeStr#" $src",
                   [(set GPR32:$dst, (OpNode i32:$src2, i32:$src))]>;
  def _ri_32 : ALU_RI<BPF_ALU, Opc,
                   (outs GPR32:$dst),
                   (ins GPR32:$src2, i32imm:$imm),
                   "$dst "#OpcodeStr#" $imm",
                   [(set GPR32:$dst, (OpNode GPR32:$src2, i32immSExt32:$imm))]>;
}

let Constraints = "$dst = $src2" in {
let isAsCheapAsAMove = 1 in {
  defm ADD : ALU<BPF_ADD, "+=", add>;
  defm SUB : ALU<BPF_SUB, "-=", sub>;
  defm OR  : ALU<BPF_OR, "|=", or>;
  defm AND : ALU<BPF_AND, "&=", and>;
  defm SLL : ALU<BPF_LSH, "<<=", shl>;
  defm SRL : ALU<BPF_RSH, ">>=", srl>;
  defm XOR : ALU<BPF_XOR, "^=", xor>;
  defm SRA : ALU<BPF_ARSH, "s>>=", sra>;
}
  defm MUL : ALU<BPF_MUL, "*=", mul>;
  defm DIV : ALU<BPF_DIV, "/=", udiv>;
}

class NEG_RR<BPFOpClass Class, BPFArithOp Opc,
             dag outs, dag ins, string asmstr, list<dag> pattern>
    : TYPE_ALU_JMP<Opc.Value, 0, outs, ins, asmstr, pattern> {
  bits<4> dst;

  let Inst{51-48} = dst;
  let BPFClass = Class;
}

let Constraints = "$dst = $src", isAsCheapAsAMove = 1 in {
  def NEG_64: NEG_RR<BPF_ALU64, BPF_NEG, (outs GPR:$dst), (ins GPR:$src),
                     "$dst = -$src",
                     [(set GPR:$dst, (ineg i64:$src))]>;
  def NEG_32: NEG_RR<BPF_ALU, BPF_NEG, (outs GPR32:$dst), (ins GPR32:$src),
                     "$dst = -$src",
                     [(set GPR32:$dst, (ineg i32:$src))]>;
}

class LD_IMM64<bits<4> Pseudo, string OpcodeStr>
    : TYPE_LD_ST<BPF_IMM.Value, BPF_DW.Value,
                 (outs GPR:$dst),
                 (ins u64imm:$imm),
                 "$dst "#OpcodeStr#" ${imm} ll",
                 [(set GPR:$dst, (i64 imm:$imm))]> {

  bits<4> dst;
  bits<64> imm;

  let Inst{51-48} = dst;
  let Inst{55-52} = Pseudo;
  let Inst{47-32} = 0;
  let Inst{31-0} = imm{31-0};
  let BPFClass = BPF_LD;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def LD_imm64 : LD_IMM64<0, "=">;
def MOV_rr : ALU_RR<BPF_ALU64, BPF_MOV,
                    (outs GPR:$dst),
                    (ins GPR:$src),
                    "$dst = $src",
                    []>;
def MOV_ri : ALU_RI<BPF_ALU64, BPF_MOV,
                    (outs GPR:$dst),
                    (ins i64imm:$imm),
                    "$dst = $imm",
                    [(set GPR:$dst, (i64 i64immSExt32:$imm))]>;
def MOV_rr_32 : ALU_RR<BPF_ALU, BPF_MOV,
                    (outs GPR32:$dst),
                    (ins GPR32:$src),
                    "$dst = $src",
                    []>;
def MOV_ri_32 : ALU_RI<BPF_ALU, BPF_MOV,
                    (outs GPR32:$dst),
                    (ins i32imm:$imm),
                    "$dst = $imm",
                    [(set GPR32:$dst, (i32 i32immSExt32:$imm))]>;
}

def FI_ri
    : TYPE_LD_ST<BPF_IMM.Value, BPF_DW.Value,
                 (outs GPR:$dst),
                 (ins MEMri:$addr),
                 "lea\t$dst, $addr",
                 [(set i64:$dst, FIri:$addr)]> {
  // This is a tentative instruction, and will be replaced
  // with MOV_rr and ADD_ri in PEI phase
  let Inst{51-48} = 0;
  let Inst{55-52} = 2;
  let Inst{47-32} = 0;
  let Inst{31-0} = 0;
  let BPFClass = BPF_LD;
}

def LD_pseudo
    : TYPE_LD_ST<BPF_IMM.Value, BPF_DW.Value,
                 (outs GPR:$dst),
                 (ins i64imm:$pseudo, u64imm:$imm),
                 "ld_pseudo\t$dst, $pseudo, $imm",
                 [(set GPR:$dst, (int_bpf_pseudo imm:$pseudo, imm:$imm))]> {

  bits<4> dst;
  bits<64> imm;
  bits<4> pseudo;

  let Inst{51-48} = dst;
  let Inst{55-52} = pseudo;
  let Inst{47-32} = 0;
  let Inst{31-0} = imm{31-0};
  let BPFClass = BPF_LD;
}

// STORE instructions
class STORE<BPFWidthModifer SizeOp, string OpcodeStr, list<dag> Pattern>
    : TYPE_LD_ST<BPF_MEM.Value, SizeOp.Value,
                 (outs),
                 (ins GPR:$src, MEMri:$addr),
                 "*("#OpcodeStr#" *)($addr) = $src",
                 Pattern> {
  bits<4> src;
  bits<20> addr;

  let Inst{51-48} = addr{19-16}; // base reg
  let Inst{55-52} = src;
  let Inst{47-32} = addr{15-0}; // offset
  let BPFClass = BPF_STX;
}

class STOREi64<BPFWidthModifer Opc, string OpcodeStr, PatFrag OpNode>
    : STORE<Opc, OpcodeStr, [(OpNode i64:$src, ADDRri:$addr)]>;

let Predicates = [BPFNoALU32] in {
  def STW : STOREi64<BPF_W, "u32", truncstorei32>;
  def STH : STOREi64<BPF_H, "u16", truncstorei16>;
  def STB : STOREi64<BPF_B, "u8", truncstorei8>;
}
def STD : STOREi64<BPF_DW, "u64", store>;

// LOAD instructions
class LOAD<BPFWidthModifer SizeOp, string OpcodeStr, list<dag> Pattern>
    : TYPE_LD_ST<BPF_MEM.Value, SizeOp.Value,
                 (outs GPR:$dst),
                 (ins MEMri:$addr),
                 "$dst = *("#OpcodeStr#" *)($addr)",
                 Pattern> {
  bits<4> dst;
  bits<20> addr;

  let Inst{51-48} = dst;
  let Inst{55-52} = addr{19-16};
  let Inst{47-32} = addr{15-0};
  let BPFClass = BPF_LDX;
}

class LOADi64<BPFWidthModifer SizeOp, string OpcodeStr, PatFrag OpNode>
    : LOAD<SizeOp, OpcodeStr, [(set i64:$dst, (OpNode ADDRri:$addr))]>;


let Predicates = [BPFNoALU32] in {
  def LDW : LOADi64<BPF_W, "u32", zextloadi32>;
  def LDH : LOADi64<BPF_H, "u16", zextloadi16>;
  def LDB : LOADi64<BPF_B, "u8", zextloadi8>;
}

def LDD : LOADi64<BPF_DW, "u64", load>;

class BRANCH<BPFJumpOp Opc, string OpcodeStr, list<dag> Pattern>
    : TYPE_ALU_JMP<Opc.Value, BPF_K.Value,
                   (outs),
                   (ins brtarget:$BrDst),
                   !strconcat(OpcodeStr, " $BrDst"),
                   Pattern> {
  bits<16> BrDst;

  let Inst{47-32} = BrDst;
  let BPFClass = BPF_JMP;
}

class CALL<string OpcodeStr>
    : TYPE_ALU_JMP<BPF_CALL.Value, BPF_K.Value,
                   (outs),
                   (ins calltarget:$BrDst),
                   !strconcat(OpcodeStr, " $BrDst"),
                   []> {
  bits<32> BrDst;

  let Inst{31-0} = BrDst;
  let BPFClass = BPF_JMP;
}

class CALLX<string OpcodeStr>
    : TYPE_ALU_JMP<BPF_CALL.Value, BPF_X.Value,
                   (outs),
                   (ins calltarget:$BrDst),
                   !strconcat(OpcodeStr, " $BrDst"),
                   []> {
  bits<32> BrDst;

  let Inst{31-0} = BrDst;
  let BPFClass = BPF_JMP;
}

// Jump always
let isBranch = 1, isTerminator = 1, hasDelaySlot=0, isBarrier = 1 in {
  def JMP : BRANCH<BPF_JA, "goto", [(br bb:$BrDst)]>;
}

// Jump and link
let isCall=1, hasDelaySlot=0, Uses = [R11],
    // Potentially clobbered registers
    Defs = [R0, R1, R2, R3, R4, R5] in {
  def JAL  : CALL<"call">;
  def JALX  : CALLX<"callx">;
}

class NOP_I<string OpcodeStr>
    : TYPE_ALU_JMP<BPF_MOV.Value, BPF_X.Value,
                   (outs),
                   (ins i32imm:$imm),
                   !strconcat(OpcodeStr, "\t$imm"),
                   []> {
  // mov r0, r0 == nop
  let Inst{55-52} = 0;
  let Inst{51-48} = 0;
  let BPFClass = BPF_ALU64;
}

let hasSideEffects = 0 in
  def NOP : NOP_I<"nop">;

class RET<string OpcodeStr>
    : TYPE_ALU_JMP<BPF_EXIT.Value, BPF_K.Value,
                   (outs),
                   (ins),
                   !strconcat(OpcodeStr, ""),
                   [(BPFretflag)]> {
  let Inst{31-0} = 0;
  let BPFClass = BPF_JMP;
}

let isReturn = 1, isTerminator = 1, hasDelaySlot=0, isBarrier = 1,
    isNotDuplicable = 1 in {
  def RET : RET<"exit">;
}

// ADJCALLSTACKDOWN/UP pseudo insns
let Defs = [R11], Uses = [R11], isCodeGenOnly = 1 in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                              "#ADJCALLSTACKDOWN $amt1 $amt2",
                              [(BPFcallseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                              "#ADJCALLSTACKUP $amt1 $amt2",
                              [(BPFcallseq_end timm:$amt1, timm:$amt2)]>;
}

let usesCustomInserter = 1, isCodeGenOnly = 1 in {
  def Select : Pseudo<(outs GPR:$dst),
                      (ins GPR:$lhs, GPR:$rhs, i64imm:$imm, GPR:$src, GPR:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i64:$dst,
                       (BPFselectcc i64:$lhs, i64:$rhs, (i64 imm:$imm), i64:$src, i64:$src2))]>;
  def Select_Ri : Pseudo<(outs GPR:$dst),
                      (ins GPR:$lhs, i64imm:$rhs, i64imm:$imm, GPR:$src, GPR:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i64:$dst,
                       (BPFselectcc i64:$lhs, (i64immSExt32:$rhs), (i64 imm:$imm), i64:$src, i64:$src2))]>;
  def Select_64_32 : Pseudo<(outs GPR32:$dst),
                      (ins GPR:$lhs, GPR:$rhs, i64imm:$imm, GPR32:$src, GPR32:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i32:$dst,
                       (BPFselectcc i64:$lhs, i64:$rhs, (i64 imm:$imm), i32:$src, i32:$src2))]>;
  def Select_Ri_64_32 : Pseudo<(outs GPR32:$dst),
                      (ins GPR:$lhs, i64imm:$rhs, i64imm:$imm, GPR32:$src, GPR32:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i32:$dst,
                       (BPFselectcc i64:$lhs, (i64immSExt32:$rhs), (i64 imm:$imm), i32:$src, i32:$src2))]>;
  def Select_32 : Pseudo<(outs GPR32:$dst),
                      (ins GPR32:$lhs, GPR32:$rhs, i32imm:$imm, GPR32:$src, GPR32:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i32:$dst,
                       (BPFselectcc i32:$lhs, i32:$rhs, (i32 imm:$imm), i32:$src, i32:$src2))]>;
  def Select_Ri_32 : Pseudo<(outs GPR32:$dst),
                      (ins GPR32:$lhs, i32imm:$rhs, i32imm:$imm, GPR32:$src, GPR32:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i32:$dst,
                       (BPFselectcc i32:$lhs, (i32immSExt32:$rhs), (i32 imm:$imm), i32:$src, i32:$src2))]>;
  def Select_32_64 : Pseudo<(outs GPR:$dst),
                      (ins GPR32:$lhs, GPR32:$rhs, i32imm:$imm, GPR:$src, GPR:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i64:$dst,
                       (BPFselectcc i32:$lhs, i32:$rhs, (i32 imm:$imm), i64:$src, i64:$src2))]>;
  def Select_Ri_32_64 : Pseudo<(outs GPR:$dst),
                      (ins GPR32:$lhs, i32imm:$rhs, i32imm:$imm, GPR:$src, GPR:$src2),
                      "# Select PSEUDO $dst = $lhs $imm $rhs ? $src : $src2",
                      [(set i64:$dst,
                       (BPFselectcc i32:$lhs, (i32immSExt32:$rhs), (i32 imm:$imm), i64:$src, i64:$src2))]>;
}

// load 64-bit global addr into register
def : Pat<(BPFWrapper tglobaladdr:$in), (LD_imm64 tglobaladdr:$in)>;

// 0xffffFFFF doesn't fit into simm32, optimize common case
def : Pat<(i64 (and (i64 GPR:$src), 0xffffFFFF)),
          (SRL_ri (SLL_ri (i64 GPR:$src), 32), 32)>;

// Calls
def : Pat<(BPFcall tglobaladdr:$dst), (JAL tglobaladdr:$dst)>;
def : Pat<(BPFcall texternalsym:$dst), (JAL texternalsym:$dst)>;
def : Pat<(BPFcall imm:$dst), (JAL imm:$dst)>;
def : Pat<(BPFcall GPR:$dst), (JALX GPR:$dst)>;

// Loads
let Predicates = [BPFNoALU32] in {
  def : Pat<(i64 (extloadi8  ADDRri:$src)), (i64 (LDB ADDRri:$src))>;
  def : Pat<(i64 (extloadi16 ADDRri:$src)), (i64 (LDH ADDRri:$src))>;
  def : Pat<(i64 (extloadi32 ADDRri:$src)), (i64 (LDW ADDRri:$src))>;
}

// Atomics
class XADD<BPFWidthModifer SizeOp, string OpcodeStr, PatFrag OpNode>
    : TYPE_LD_ST<BPF_XADD.Value, SizeOp.Value,
                 (outs GPR:$dst),
                 (ins MEMri:$addr, GPR:$val),
                 "lock *("#OpcodeStr#" *)($addr) += $val",
                 [(set GPR:$dst, (OpNode ADDRri:$addr, GPR:$val))]> {
  bits<4> dst;
  bits<20> addr;

  let Inst{51-48} = addr{19-16}; // base reg
  let Inst{55-52} = dst;
  let Inst{47-32} = addr{15-0}; // offset
  let BPFClass = BPF_STX;
}

let Constraints = "$dst = $val" in {
def XADD32 : XADD<BPF_W, "u32", atomic_load_add_32>;
def XADD64 : XADD<BPF_DW, "u64", atomic_load_add_64>;
// undefined def XADD16 : XADD<1, "xadd16", atomic_load_add_16>;
// undefined def XADD8  : XADD<2, "xadd8", atomic_load_add_8>;
}

// bswap16, bswap32, bswap64
class BSWAP<bits<32> SizeOp, string OpcodeStr, BPFSrcType SrcType, list<dag> Pattern>
    : TYPE_ALU_JMP<BPF_END.Value, SrcType.Value,
                   (outs GPR:$dst),
                   (ins GPR:$src),
                   "$dst = "#OpcodeStr#" $src",
                   Pattern> {
  bits<4> dst;

  let Inst{51-48} = dst;
  let Inst{31-0} = SizeOp;
  let BPFClass = BPF_ALU;
}


let Constraints = "$dst = $src" in {
    let Predicates = [BPFIsLittleEndian] in {
        def BE16 : BSWAP<16, "be16", BPF_TO_BE, [(set GPR:$dst, (srl (bswap GPR:$src), (i64 48)))]>;
        def BE32 : BSWAP<32, "be32", BPF_TO_BE, [(set GPR:$dst, (srl (bswap GPR:$src), (i64 32)))]>;
        def BE64 : BSWAP<64, "be64", BPF_TO_BE, [(set GPR:$dst, (bswap GPR:$src))]>;
    }
    let Predicates = [BPFIsBigEndian] in {
        def LE16 : BSWAP<16, "le16", BPF_TO_LE, [(set GPR:$dst, (srl (bswap GPR:$src), (i64 48)))]>;
        def LE32 : BSWAP<32, "le32", BPF_TO_LE, [(set GPR:$dst, (srl (bswap GPR:$src), (i64 32)))]>;
        def LE64 : BSWAP<64, "le64", BPF_TO_LE, [(set GPR:$dst, (bswap GPR:$src))]>;
    }
}

let Defs = [R0, R1, R2, R3, R4, R5], Uses = [R6], hasSideEffects = 1,
    hasExtraDefRegAllocReq = 1, hasExtraSrcRegAllocReq = 1, mayLoad = 1 in {
class LOAD_ABS<BPFWidthModifer SizeOp, string OpcodeStr, Intrinsic OpNode>
    : TYPE_LD_ST<BPF_ABS.Value, SizeOp.Value,
                 (outs),
                 (ins GPR:$skb, i64imm:$imm),
                 "r0 = *("#OpcodeStr#" *)skb[$imm]",
                 [(set R0, (OpNode GPR:$skb, i64immSExt32:$imm))]> {
  bits<32> imm;

  let Inst{31-0} = imm;
  let BPFClass = BPF_LD;
}

class LOAD_IND<BPFWidthModifer SizeOp, string OpcodeStr, Intrinsic OpNode>
    : TYPE_LD_ST<BPF_IND.Value, SizeOp.Value,
                 (outs),
                 (ins GPR:$skb, GPR:$val),
                 "r0 = *("#OpcodeStr#" *)skb[$val]",
                 [(set R0, (OpNode GPR:$skb, GPR:$val))]> {
  bits<4> val;

  let Inst{55-52} = val;
  let BPFClass = BPF_LD;
}
}

def LD_ABS_B : LOAD_ABS<BPF_B, "u8", int_bpf_load_byte>;
def LD_ABS_H : LOAD_ABS<BPF_H, "u16", int_bpf_load_half>;
def LD_ABS_W : LOAD_ABS<BPF_W, "u32", int_bpf_load_word>;

def LD_IND_B : LOAD_IND<BPF_B, "u8", int_bpf_load_byte>;
def LD_IND_H : LOAD_IND<BPF_H, "u16", int_bpf_load_half>;
def LD_IND_W : LOAD_IND<BPF_W, "u32", int_bpf_load_word>;

let isCodeGenOnly = 1 in {
  def MOV_32_64 : ALU_RR<BPF_ALU, BPF_MOV,
                         (outs GPR:$dst), (ins GPR32:$src),
                         "$dst = $src", []>;
}

def : Pat<(i64 (sext GPR32:$src)),
          (SRA_ri (SLL_ri (MOV_32_64 GPR32:$src), 32), 32)>;

def : Pat<(i64 (zext GPR32:$src)),
          (SRL_ri (SLL_ri (MOV_32_64 GPR32:$src), 32), 32)>;

// For i64 -> i32 truncation, use the 32-bit subregister directly.
def : Pat<(i32 (trunc GPR:$src)),
          (i32 (EXTRACT_SUBREG GPR:$src, sub_32))>;

// For i32 -> i64 anyext, we don't care about the high bits.
def : Pat<(i64 (anyext GPR32:$src)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GPR32:$src, sub_32)>;

class STORE32<BPFWidthModifer SizeOp, string OpcodeStr, list<dag> Pattern>
    : TYPE_LD_ST<BPF_MEM.Value, SizeOp.Value,
                 (outs),
                 (ins GPR32:$src, MEMri:$addr),
                 "*("#OpcodeStr#" *)($addr) = $src",
                 Pattern> {
  bits<4> src;
  bits<20> addr;

  let Inst{51-48} = addr{19-16}; // base reg
  let Inst{55-52} = src;
  let Inst{47-32} = addr{15-0}; // offset
  let BPFClass = BPF_STX;
}

class STOREi32<BPFWidthModifer Opc, string OpcodeStr, PatFrag OpNode>
    : STORE32<Opc, OpcodeStr, [(OpNode i32:$src, ADDRri:$addr)]>;

let Predicates = [BPFHasALU32], DecoderNamespace = "BPFALU32" in {
  def STW32 : STOREi32<BPF_W, "u32", store>;
  def STH32 : STOREi32<BPF_H, "u16", truncstorei16>;
  def STB32 : STOREi32<BPF_B, "u8", truncstorei8>;
}

class LOAD32<BPFWidthModifer SizeOp, string OpcodeStr, list<dag> Pattern>
    : TYPE_LD_ST<BPF_MEM.Value, SizeOp.Value,
                (outs GPR32:$dst),
                (ins MEMri:$addr),
                "$dst = *("#OpcodeStr#" *)($addr)",
                Pattern> {
  bits<4> dst;
  bits<20> addr;

  let Inst{51-48} = dst;
  let Inst{55-52} = addr{19-16};
  let Inst{47-32} = addr{15-0};
  let BPFClass = BPF_LDX;
}

class LOADi32<BPFWidthModifer SizeOp, string OpcodeStr, PatFrag OpNode>
    : LOAD32<SizeOp, OpcodeStr, [(set i32:$dst, (OpNode ADDRri:$addr))]>;

let Predicates = [BPFHasALU32], DecoderNamespace = "BPFALU32" in {
  def LDW32 : LOADi32<BPF_W, "u32", load>;
  def LDH32 : LOADi32<BPF_H, "u16", zextloadi16>;
  def LDB32 : LOADi32<BPF_B, "u8", zextloadi8>;
}

let Predicates = [BPFHasALU32] in {
  def : Pat<(truncstorei8 GPR:$src, ADDRri:$dst),
            (STB32 (EXTRACT_SUBREG GPR:$src, sub_32), ADDRri:$dst)>;
  def : Pat<(truncstorei16 GPR:$src, ADDRri:$dst),
            (STH32 (EXTRACT_SUBREG GPR:$src, sub_32), ADDRri:$dst)>;
  def : Pat<(truncstorei32 GPR:$src, ADDRri:$dst),
            (STW32 (EXTRACT_SUBREG GPR:$src, sub_32), ADDRri:$dst)>;
  def : Pat<(i32 (extloadi8 ADDRri:$src)), (i32 (LDB32 ADDRri:$src))>;
  def : Pat<(i32 (extloadi16 ADDRri:$src)), (i32 (LDH32 ADDRri:$src))>;
  def : Pat<(i64 (zextloadi8  ADDRri:$src)),
            (SUBREG_TO_REG (i64 0), (LDB32 ADDRri:$src), sub_32)>;
  def : Pat<(i64 (zextloadi16 ADDRri:$src)),
            (SUBREG_TO_REG (i64 0), (LDH32 ADDRri:$src), sub_32)>;
  def : Pat<(i64 (zextloadi32 ADDRri:$src)),
            (SUBREG_TO_REG (i64 0), (LDW32 ADDRri:$src), sub_32)>;
  def : Pat<(i64 (extloadi8  ADDRri:$src)),
            (SUBREG_TO_REG (i64 0), (LDB32 ADDRri:$src), sub_32)>;
  def : Pat<(i64 (extloadi16 ADDRri:$src)),
            (SUBREG_TO_REG (i64 0), (LDH32 ADDRri:$src), sub_32)>;
  def : Pat<(i64 (extloadi32 ADDRri:$src)),
            (SUBREG_TO_REG (i64 0), (LDW32 ADDRri:$src), sub_32)>;
}

let usesCustomInserter = 1, isCodeGenOnly = 1 in {
    def MEMCPY : Pseudo<
      (outs),
      (ins GPR:$dst, GPR:$src, i64imm:$len, i64imm:$align, variable_ops),
      "#memcpy dst: $dst, src: $src, len: $len, align: $align",
      [(BPFmemcpy GPR:$dst, GPR:$src, imm:$len, imm:$align)]>;
}
