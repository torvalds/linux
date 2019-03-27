//===-- ARMInstrThumb.td - Thumb support for ARM -----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the Thumb instruction set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Thumb specific DAG Nodes.
//

def imm_sr_XFORM: SDNodeXForm<imm, [{
  unsigned Imm = N->getZExtValue();
  return CurDAG->getTargetConstant((Imm == 32 ? 0 : Imm), SDLoc(N), MVT::i32);
}]>;
def ThumbSRImmAsmOperand: ImmAsmOperand<1,32> { let Name = "ImmThumbSR"; }
def imm_sr : Operand<i32>, PatLeaf<(imm), [{
  uint64_t Imm = N->getZExtValue();
  return Imm > 0 && Imm <= 32;
}], imm_sr_XFORM> {
  let PrintMethod = "printThumbSRImm";
  let ParserMatchClass = ThumbSRImmAsmOperand;
}

def imm0_7_neg : PatLeaf<(i32 imm), [{
  return (uint32_t)-N->getZExtValue() < 8;
}], imm_neg_XFORM>;

def ThumbModImmNeg1_7AsmOperand : AsmOperandClass { let Name = "ThumbModImmNeg1_7"; }
def mod_imm1_7_neg : Operand<i32>, PatLeaf<(imm), [{
    unsigned Value = -(unsigned)N->getZExtValue();
    return 0 < Value && Value < 8;
  }], imm_neg_XFORM> {
  let ParserMatchClass = ThumbModImmNeg1_7AsmOperand;
}

def ThumbModImmNeg8_255AsmOperand : AsmOperandClass { let Name = "ThumbModImmNeg8_255"; }
def mod_imm8_255_neg : Operand<i32>, PatLeaf<(imm), [{
    unsigned Value = -(unsigned)N->getZExtValue();
    return 7 < Value && Value < 256;
  }], imm_neg_XFORM> {
  let ParserMatchClass = ThumbModImmNeg8_255AsmOperand;
}


def imm0_255_comp : PatLeaf<(i32 imm), [{
  return ~((uint32_t)N->getZExtValue()) < 256;
}]>;

def imm8_255_neg : PatLeaf<(i32 imm), [{
  unsigned Val = -N->getZExtValue();
  return Val >= 8 && Val < 256;
}], imm_neg_XFORM>;

// Break imm's up into two pieces: an immediate + a left shift. This uses
// thumb_immshifted to match and thumb_immshifted_val and thumb_immshifted_shamt
// to get the val/shift pieces.
def thumb_immshifted : PatLeaf<(imm), [{
  return ARM_AM::isThumbImmShiftedVal((unsigned)N->getZExtValue());
}]>;

def thumb_immshifted_val : SDNodeXForm<imm, [{
  unsigned V = ARM_AM::getThumbImmNonShiftedVal((unsigned)N->getZExtValue());
  return CurDAG->getTargetConstant(V, SDLoc(N), MVT::i32);
}]>;

def thumb_immshifted_shamt : SDNodeXForm<imm, [{
  unsigned V = ARM_AM::getThumbImmValShift((unsigned)N->getZExtValue());
  return CurDAG->getTargetConstant(V, SDLoc(N), MVT::i32);
}]>;

def imm256_510 : ImmLeaf<i32, [{
  return Imm >= 256 && Imm < 511;
}]>;

def thumb_imm256_510_addend : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() - 255, SDLoc(N), MVT::i32);
}]>;

// Scaled 4 immediate.
def t_imm0_1020s4_asmoperand: AsmOperandClass { let Name = "Imm0_1020s4"; }
def t_imm0_1020s4 : Operand<i32> {
  let PrintMethod = "printThumbS4ImmOperand";
  let ParserMatchClass = t_imm0_1020s4_asmoperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

def t_imm0_508s4_asmoperand: AsmOperandClass { let Name = "Imm0_508s4"; }
def t_imm0_508s4 : Operand<i32> {
  let PrintMethod = "printThumbS4ImmOperand";
  let ParserMatchClass = t_imm0_508s4_asmoperand;
  let OperandType = "OPERAND_IMMEDIATE";
}
// Alias use only, so no printer is necessary.
def t_imm0_508s4_neg_asmoperand: AsmOperandClass { let Name = "Imm0_508s4Neg"; }
def t_imm0_508s4_neg : Operand<i32> {
  let ParserMatchClass = t_imm0_508s4_neg_asmoperand;
  let OperandType = "OPERAND_IMMEDIATE";
}

// Define Thumb specific addressing modes.

// unsigned 8-bit, 2-scaled memory offset
class OperandUnsignedOffset_b8s2 : AsmOperandClass {
  let Name = "UnsignedOffset_b8s2";
  let PredicateMethod = "isUnsignedOffset<8, 2>";
}

def UnsignedOffset_b8s2 : OperandUnsignedOffset_b8s2;

// thumb style PC relative operand. signed, 8 bits magnitude,
// two bits shift. can be represented as either [pc, #imm], #imm,
// or relocatable expression...
def ThumbMemPC : AsmOperandClass {
  let Name = "ThumbMemPC";
}

let OperandType = "OPERAND_PCREL" in {
def t_brtarget : Operand<OtherVT> {
  let EncoderMethod = "getThumbBRTargetOpValue";
  let DecoderMethod = "DecodeThumbBROperand";
}

// ADR instruction labels.
def t_adrlabel : Operand<i32> {
  let EncoderMethod = "getThumbAdrLabelOpValue";
  let PrintMethod = "printAdrLabelOperand<2>";
  let ParserMatchClass = UnsignedOffset_b8s2;
}


def thumb_br_target : Operand<OtherVT> {
  let ParserMatchClass = ThumbBranchTarget;
  let EncoderMethod = "getThumbBranchTargetOpValue";
  let OperandType = "OPERAND_PCREL";
}

def thumb_bl_target : Operand<i32> {
  let ParserMatchClass = ThumbBranchTarget;
  let EncoderMethod = "getThumbBLTargetOpValue";
  let DecoderMethod = "DecodeThumbBLTargetOperand";
}

// Target for BLX *from* thumb mode.
def thumb_blx_target : Operand<i32> {
  let ParserMatchClass = ARMBranchTarget;
  let EncoderMethod = "getThumbBLXTargetOpValue";
  let DecoderMethod = "DecodeThumbBLXOffset";
}

def thumb_bcc_target : Operand<OtherVT> {
  let ParserMatchClass = ThumbBranchTarget;
  let EncoderMethod = "getThumbBCCTargetOpValue";
  let DecoderMethod = "DecodeThumbBCCTargetOperand";
}

def thumb_cb_target : Operand<OtherVT> {
  let ParserMatchClass = ThumbBranchTarget;
  let EncoderMethod = "getThumbCBTargetOpValue";
  let DecoderMethod = "DecodeThumbCmpBROperand";
}

// t_addrmode_pc := <label> => pc + imm8 * 4
//
def t_addrmode_pc : MemOperand {
  let EncoderMethod = "getAddrModePCOpValue";
  let DecoderMethod = "DecodeThumbAddrModePC";
  let PrintMethod = "printThumbLdrLabelOperand";
  let ParserMatchClass = ThumbMemPC;
}
}

// t_addrmode_rr := reg + reg
//
def t_addrmode_rr_asm_operand : AsmOperandClass { let Name = "MemThumbRR"; }
def t_addrmode_rr : MemOperand,
                    ComplexPattern<i32, 2, "SelectThumbAddrModeRR", []> {
  let EncoderMethod = "getThumbAddrModeRegRegOpValue";
  let PrintMethod = "printThumbAddrModeRROperand";
  let DecoderMethod = "DecodeThumbAddrModeRR";
  let ParserMatchClass = t_addrmode_rr_asm_operand;
  let MIOperandInfo = (ops tGPR:$base, tGPR:$offsreg);
}

// t_addrmode_rrs := reg + reg
//
// We use separate scaled versions because the Select* functions need
// to explicitly check for a matching constant and return false here so that
// the reg+imm forms will match instead. This is a horrible way to do that,
// as it forces tight coupling between the methods, but it's how selectiondag
// currently works.
def t_addrmode_rrs1 : MemOperand,
                      ComplexPattern<i32, 2, "SelectThumbAddrModeRI5S1", []> {
  let EncoderMethod = "getThumbAddrModeRegRegOpValue";
  let PrintMethod = "printThumbAddrModeRROperand";
  let DecoderMethod = "DecodeThumbAddrModeRR";
  let ParserMatchClass = t_addrmode_rr_asm_operand;
  let MIOperandInfo = (ops tGPR:$base, tGPR:$offsreg);
}
def t_addrmode_rrs2 : MemOperand,
                      ComplexPattern<i32, 2, "SelectThumbAddrModeRI5S2", []> {
  let EncoderMethod = "getThumbAddrModeRegRegOpValue";
  let DecoderMethod = "DecodeThumbAddrModeRR";
  let PrintMethod = "printThumbAddrModeRROperand";
  let ParserMatchClass = t_addrmode_rr_asm_operand;
  let MIOperandInfo = (ops tGPR:$base, tGPR:$offsreg);
}
def t_addrmode_rrs4 : MemOperand,
                      ComplexPattern<i32, 2, "SelectThumbAddrModeRI5S4", []> {
  let EncoderMethod = "getThumbAddrModeRegRegOpValue";
  let DecoderMethod = "DecodeThumbAddrModeRR";
  let PrintMethod = "printThumbAddrModeRROperand";
  let ParserMatchClass = t_addrmode_rr_asm_operand;
  let MIOperandInfo = (ops tGPR:$base, tGPR:$offsreg);
}

// t_addrmode_is4 := reg + imm5 * 4
//
def t_addrmode_is4_asm_operand : AsmOperandClass { let Name = "MemThumbRIs4"; }
def t_addrmode_is4 : MemOperand,
                     ComplexPattern<i32, 2, "SelectThumbAddrModeImm5S4", []> {
  let EncoderMethod = "getAddrModeISOpValue";
  let DecoderMethod = "DecodeThumbAddrModeIS";
  let PrintMethod = "printThumbAddrModeImm5S4Operand";
  let ParserMatchClass = t_addrmode_is4_asm_operand;
  let MIOperandInfo = (ops tGPR:$base, i32imm:$offsimm);
}

// t_addrmode_is2 := reg + imm5 * 2
//
def t_addrmode_is2_asm_operand : AsmOperandClass { let Name = "MemThumbRIs2"; }
def t_addrmode_is2 : MemOperand,
                     ComplexPattern<i32, 2, "SelectThumbAddrModeImm5S2", []> {
  let EncoderMethod = "getAddrModeISOpValue";
  let DecoderMethod = "DecodeThumbAddrModeIS";
  let PrintMethod = "printThumbAddrModeImm5S2Operand";
  let ParserMatchClass = t_addrmode_is2_asm_operand;
  let MIOperandInfo = (ops tGPR:$base, i32imm:$offsimm);
}

// t_addrmode_is1 := reg + imm5
//
def t_addrmode_is1_asm_operand : AsmOperandClass { let Name = "MemThumbRIs1"; }
def t_addrmode_is1 : MemOperand,
                     ComplexPattern<i32, 2, "SelectThumbAddrModeImm5S1", []> {
  let EncoderMethod = "getAddrModeISOpValue";
  let DecoderMethod = "DecodeThumbAddrModeIS";
  let PrintMethod = "printThumbAddrModeImm5S1Operand";
  let ParserMatchClass = t_addrmode_is1_asm_operand;
  let MIOperandInfo = (ops tGPR:$base, i32imm:$offsimm);
}

// t_addrmode_sp := sp + imm8 * 4
//
// FIXME: This really shouldn't have an explicit SP operand at all. It should
// be implicit, just like in the instruction encoding itself.
def t_addrmode_sp_asm_operand : AsmOperandClass { let Name = "MemThumbSPI"; }
def t_addrmode_sp : MemOperand,
                    ComplexPattern<i32, 2, "SelectThumbAddrModeSP", []> {
  let EncoderMethod = "getAddrModeThumbSPOpValue";
  let DecoderMethod = "DecodeThumbAddrModeSP";
  let PrintMethod = "printThumbAddrModeSPOperand";
  let ParserMatchClass = t_addrmode_sp_asm_operand;
  let MIOperandInfo = (ops GPR:$base, i32imm:$offsimm);
}

// Inspects parent to determine whether an or instruction can be implemented as
// an add (i.e. whether we know overflow won't occur in the add).
def AddLikeOrOp : ComplexPattern<i32, 1, "SelectAddLikeOr", [],
                                 [SDNPWantParent]>;

// Pattern to exclude immediates from matching
def non_imm32 : PatLeaf<(i32 GPR), [{ return !isa<ConstantSDNode>(N); }]>;

//===----------------------------------------------------------------------===//
//  Miscellaneous Instructions.
//

// FIXME: Marking these as hasSideEffects is necessary to prevent machine DCE
// from removing one half of the matched pairs. That breaks PEI, which assumes
// these will always be in pairs, and asserts if it finds otherwise. Better way?
let Defs = [SP], Uses = [SP], hasSideEffects = 1 in {
def tADJCALLSTACKUP :
  PseudoInst<(outs), (ins i32imm:$amt1, i32imm:$amt2), NoItinerary,
             [(ARMcallseq_end imm:$amt1, imm:$amt2)]>,
            Requires<[IsThumb, IsThumb1Only]>;

def tADJCALLSTACKDOWN :
  PseudoInst<(outs), (ins i32imm:$amt, i32imm:$amt2), NoItinerary,
             [(ARMcallseq_start imm:$amt, imm:$amt2)]>,
            Requires<[IsThumb, IsThumb1Only]>;
}

class T1SystemEncoding<bits<8> opc>
  : T1Encoding<0b101111> {
  let Inst{9-8} = 0b11;
  let Inst{7-0} = opc;
}

def tHINT : T1pI<(outs), (ins imm0_15:$imm), NoItinerary, "hint", "\t$imm",
                 [(int_arm_hint imm0_15:$imm)]>,
            T1SystemEncoding<0x00>,
            Requires<[IsThumb, HasV6M]> {
  bits<4> imm;
  let Inst{7-4} = imm;
}

// Note: When EmitPriority == 1, the alias will be used for printing
class tHintAlias<string Asm, dag Result, bit EmitPriority = 0> : tInstAlias<Asm, Result, EmitPriority> {
  let Predicates = [IsThumb, HasV6M];
}

def : tHintAlias<"nop$p", (tHINT 0, pred:$p), 1>; // A8.6.110
def : tHintAlias<"yield$p", (tHINT 1, pred:$p), 1>; // A8.6.410
def : tHintAlias<"wfe$p", (tHINT 2, pred:$p), 1>; // A8.6.408
def : tHintAlias<"wfi$p", (tHINT 3, pred:$p), 1>; // A8.6.409
def : tHintAlias<"sev$p", (tHINT 4, pred:$p), 1>; // A8.6.157
def : tInstAlias<"sevl$p", (tHINT 5, pred:$p), 1> {
  let Predicates = [IsThumb2, HasV8];
}

// The imm operand $val can be used by a debugger to store more information
// about the breakpoint.
def tBKPT : T1I<(outs), (ins imm0_255:$val), NoItinerary, "bkpt\t$val",
                []>,
           T1Encoding<0b101111> {
  let Inst{9-8} = 0b10;
  // A8.6.22
  bits<8> val;
  let Inst{7-0} = val;
}
// default immediate for breakpoint mnemonic
def : InstAlias<"bkpt", (tBKPT 0), 0>, Requires<[IsThumb]>;

def tHLT : T1I<(outs), (ins imm0_63:$val), NoItinerary, "hlt\t$val",
                []>, T1Encoding<0b101110>, Requires<[IsThumb, HasV8]> {
  let Inst{9-6} = 0b1010;
  bits<6> val;
  let Inst{5-0} = val;
}

def tSETEND : T1I<(outs), (ins setend_op:$end), NoItinerary, "setend\t$end",
                  []>, T1Encoding<0b101101>, Requires<[IsThumb, IsNotMClass]>, Deprecated<HasV8Ops> {
  bits<1> end;
  // A8.6.156
  let Inst{9-5} = 0b10010;
  let Inst{4}   = 1;
  let Inst{3}   = end;
  let Inst{2-0} = 0b000;
}

// Change Processor State is a system instruction -- for disassembly only.
def tCPS : T1I<(outs), (ins imod_op:$imod, iflags_op:$iflags),
                NoItinerary, "cps$imod $iflags", []>,
           T1Misc<0b0110011> {
  // A8.6.38 & B6.1.1
  bit imod;
  bits<3> iflags;

  let Inst{4}   = imod;
  let Inst{3}   = 0;
  let Inst{2-0} = iflags;
  let DecoderMethod = "DecodeThumbCPS";
}

// For both thumb1 and thumb2.
let isNotDuplicable = 1, isCodeGenOnly = 1 in
def tPICADD : TIt<(outs GPR:$dst), (ins GPR:$lhs, pclabel:$cp), IIC_iALUr, "",
                  [(set GPR:$dst, (ARMpic_add GPR:$lhs, imm:$cp))]>,
              T1Special<{0,0,?,?}>, Sched<[WriteALU]> {
  // A8.6.6
  bits<3> dst;
  let Inst{6-3} = 0b1111; // Rm = pc
  let Inst{2-0} = dst;
}

// ADD <Rd>, sp, #<imm8>
// FIXME: This should not be marked as having side effects, and it should be
// rematerializable. Clearing the side effect bit causes miscompilations,
// probably because the instruction can be moved around.
def tADDrSPi : T1pI<(outs tGPR:$dst), (ins GPRsp:$sp, t_imm0_1020s4:$imm),
                    IIC_iALUi, "add", "\t$dst, $sp, $imm", []>,
               T1Encoding<{1,0,1,0,1,?}>, Sched<[WriteALU]> {
  // A6.2 & A8.6.8
  bits<3> dst;
  bits<8> imm;
  let Inst{10-8} = dst;
  let Inst{7-0}  = imm;
  let DecoderMethod = "DecodeThumbAddSpecialReg";
}

// Thumb1 frame lowering is rather fragile, we hope to be able to use
// tADDrSPi, but we may need to insert a sequence that clobbers CPSR.
def tADDframe : PseudoInst<(outs tGPR:$dst), (ins i32imm:$base, i32imm:$offset),
                           NoItinerary, []>,
                Requires<[IsThumb, IsThumb1Only]> {
  let Defs = [CPSR];
}

// ADD sp, sp, #<imm7>
def tADDspi : T1pIt<(outs GPRsp:$Rdn), (ins GPRsp:$Rn, t_imm0_508s4:$imm),
                     IIC_iALUi, "add", "\t$Rdn, $imm", []>,
              T1Misc<{0,0,0,0,0,?,?}>, Sched<[WriteALU]> {
  // A6.2.5 & A8.6.8
  bits<7> imm;
  let Inst{6-0} = imm;
  let DecoderMethod = "DecodeThumbAddSPImm";
}

// SUB sp, sp, #<imm7>
// FIXME: The encoding and the ASM string don't match up.
def tSUBspi : T1pIt<(outs GPRsp:$Rdn), (ins GPRsp:$Rn, t_imm0_508s4:$imm),
                    IIC_iALUi, "sub", "\t$Rdn, $imm", []>,
              T1Misc<{0,0,0,0,1,?,?}>, Sched<[WriteALU]> {
  // A6.2.5 & A8.6.214
  bits<7> imm;
  let Inst{6-0} = imm;
  let DecoderMethod = "DecodeThumbAddSPImm";
}

def : tInstSubst<"add${p} sp, $imm",
                 (tSUBspi SP, t_imm0_508s4_neg:$imm, pred:$p)>;
def : tInstSubst<"add${p} sp, sp, $imm",
                 (tSUBspi SP, t_imm0_508s4_neg:$imm, pred:$p)>;

// Can optionally specify SP as a three operand instruction.
def : tInstAlias<"add${p} sp, sp, $imm",
                 (tADDspi SP, t_imm0_508s4:$imm, pred:$p)>;
def : tInstAlias<"sub${p} sp, sp, $imm",
                 (tSUBspi SP, t_imm0_508s4:$imm, pred:$p)>;

// ADD <Rm>, sp
def tADDrSP : T1pI<(outs GPR:$Rdn), (ins GPRsp:$sp, GPR:$Rn), IIC_iALUr,
                   "add", "\t$Rdn, $sp, $Rn", []>,
              T1Special<{0,0,?,?}>, Sched<[WriteALU]> {
  // A8.6.9 Encoding T1
  bits<4> Rdn;
  let Inst{7}   = Rdn{3};
  let Inst{6-3} = 0b1101;
  let Inst{2-0} = Rdn{2-0};
  let DecoderMethod = "DecodeThumbAddSPReg";
}

// ADD sp, <Rm>
def tADDspr : T1pIt<(outs GPRsp:$Rdn), (ins GPRsp:$Rn, GPR:$Rm), IIC_iALUr,
                  "add", "\t$Rdn, $Rm", []>,
              T1Special<{0,0,?,?}>, Sched<[WriteALU]> {
  // A8.6.9 Encoding T2
  bits<4> Rm;
  let Inst{7} = 1;
  let Inst{6-3} = Rm;
  let Inst{2-0} = 0b101;
  let DecoderMethod = "DecodeThumbAddSPReg";
}

//===----------------------------------------------------------------------===//
//  Control Flow Instructions.
//

// Indirect branches
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in {
  def tBX : TI<(outs), (ins GPR:$Rm, pred:$p), IIC_Br, "bx${p}\t$Rm", []>,
            T1Special<{1,1,0,?}>, Sched<[WriteBr]> {
    // A6.2.3 & A8.6.25
    bits<4> Rm;
    let Inst{6-3} = Rm;
    let Inst{2-0} = 0b000;
    let Unpredictable{2-0} = 0b111;
  }
  def tBXNS : TI<(outs), (ins GPR:$Rm, pred:$p), IIC_Br, "bxns${p}\t$Rm", []>,
              Requires<[IsThumb, Has8MSecExt]>,
              T1Special<{1,1,0,?}>, Sched<[WriteBr]> {
    bits<4> Rm;
    let Inst{6-3} = Rm;
    let Inst{2-0} = 0b100;
    let Unpredictable{1-0} = 0b11;
  }
}

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
  def tBX_RET : tPseudoExpand<(outs), (ins pred:$p), 2, IIC_Br,
                   [(ARMretflag)], (tBX LR, pred:$p)>, Sched<[WriteBr]>;

  // Alternative return instruction used by vararg functions.
  def tBX_RET_vararg : tPseudoExpand<(outs), (ins tGPR:$Rm, pred:$p),
                   2, IIC_Br, [],
                   (tBX GPR:$Rm, pred:$p)>, Sched<[WriteBr]>;
}

// All calls clobber the non-callee saved registers. SP is marked as a use to
// prevent stack-pointer assignments that appear immediately before calls from
// potentially appearing dead.
let isCall = 1,
  Defs = [LR], Uses = [SP] in {
  // Also used for Thumb2
  def tBL  : TIx2<0b11110, 0b11, 1,
                  (outs), (ins pred:$p, thumb_bl_target:$func), IIC_Br,
                  "bl${p}\t$func",
                  [(ARMcall tglobaladdr:$func)]>,
             Requires<[IsThumb]>, Sched<[WriteBrL]> {
    bits<24> func;
    let Inst{26} = func{23};
    let Inst{25-16} = func{20-11};
    let Inst{13} = func{22};
    let Inst{11} = func{21};
    let Inst{10-0} = func{10-0};
  }

  // ARMv5T and above, also used for Thumb2
  def tBLXi : TIx2<0b11110, 0b11, 0,
                 (outs), (ins pred:$p, thumb_blx_target:$func), IIC_Br,
                   "blx${p}\t$func", []>,
              Requires<[IsThumb, HasV5T, IsNotMClass]>, Sched<[WriteBrL]> {
    bits<24> func;
    let Inst{26} = func{23};
    let Inst{25-16} = func{20-11};
    let Inst{13} = func{22};
    let Inst{11} = func{21};
    let Inst{10-1} = func{10-1};
    let Inst{0} = 0; // func{0} is assumed zero
  }

  // Also used for Thumb2
  def tBLXr : TI<(outs), (ins pred:$p, GPR:$func), IIC_Br,
                  "blx${p}\t$func",
                  [(ARMcall GPR:$func)]>,
              Requires<[IsThumb, HasV5T]>,
              T1Special<{1,1,1,?}>, Sched<[WriteBrL]> { // A6.2.3 & A8.6.24;
    bits<4> func;
    let Inst{6-3} = func;
    let Inst{2-0} = 0b000;
  }

  // ARMv8-M Security Extensions
  def tBLXNSr : TI<(outs), (ins pred:$p, GPRnopc:$func), IIC_Br,
                   "blxns${p}\t$func", []>,
                Requires<[IsThumb, Has8MSecExt]>,
                T1Special<{1,1,1,?}>, Sched<[WriteBrL]> {
    bits<4> func;
    let Inst{6-3} = func;
    let Inst{2-0} = 0b100;
    let Unpredictable{1-0} = 0b11;
  }

  // ARMv4T
  def tBX_CALL : tPseudoInst<(outs), (ins tGPR:$func),
                  4, IIC_Br,
                  [(ARMcall_nolink tGPR:$func)]>,
            Requires<[IsThumb, IsThumb1Only]>, Sched<[WriteBr]>;
}

let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
  let isPredicable = 1 in
  def tB   : T1pI<(outs), (ins t_brtarget:$target), IIC_Br,
                 "b", "\t$target", [(br bb:$target)]>,
             T1Encoding<{1,1,1,0,0,?}>, Sched<[WriteBr]> {
    bits<11> target;
    let Inst{10-0} = target;
    let AsmMatchConverter = "cvtThumbBranches";
 }

  // Far jump
  // Just a pseudo for a tBL instruction. Needed to let regalloc know about
  // the clobber of LR.
  let Defs = [LR] in
  def tBfar : tPseudoExpand<(outs), (ins thumb_bl_target:$target, pred:$p),
                          4, IIC_Br, [],
                          (tBL pred:$p, thumb_bl_target:$target)>,
                          Sched<[WriteBrTbl]>;

  def tBR_JTr : tPseudoInst<(outs),
                      (ins tGPR:$target, i32imm:$jt),
                      0, IIC_Br,
                      [(ARMbrjt tGPR:$target, tjumptable:$jt)]>,
                      Sched<[WriteBrTbl]> {
    let Size = 2;
    list<Predicate> Predicates = [IsThumb, IsThumb1Only];
  }
}

// FIXME: should be able to write a pattern for ARMBrcond, but can't use
// a two-value operand where a dag node expects two operands. :(
let isBranch = 1, isTerminator = 1 in
  def tBcc : T1I<(outs), (ins thumb_bcc_target:$target, pred:$p), IIC_Br,
                 "b${p}\t$target",
                 [/*(ARMbrcond bb:$target, imm:$cc)*/]>,
             T1BranchCond<{1,1,0,1}>, Sched<[WriteBr]> {
  bits<4> p;
  bits<8> target;
  let Inst{11-8} = p;
  let Inst{7-0} = target;
  let AsmMatchConverter = "cvtThumbBranches";
}


// Tail calls
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1 in {
  // IOS versions.
  let Uses = [SP] in {
    def tTAILJMPr : tPseudoExpand<(outs), (ins tcGPR:$dst),
                     4, IIC_Br, [],
                     (tBX GPR:$dst, (ops 14, zero_reg))>,
                     Requires<[IsThumb]>, Sched<[WriteBr]>;
  }
  // tTAILJMPd: MachO version uses a Thumb2 branch (no Thumb1 tail calls
  // on MachO), so it's in ARMInstrThumb2.td.
  // Non-MachO version:
  let Uses = [SP] in {
    def tTAILJMPdND : tPseudoExpand<(outs),
                   (ins t_brtarget:$dst, pred:$p),
                   4, IIC_Br, [],
                   (tB t_brtarget:$dst, pred:$p)>,
                 Requires<[IsThumb, IsNotMachO]>, Sched<[WriteBr]>;
  }
}


// A8.6.218 Supervisor Call (Software Interrupt)
// A8.6.16 B: Encoding T1
// If Inst{11-8} == 0b1111 then SEE SVC
let isCall = 1, Uses = [SP] in
def tSVC : T1pI<(outs), (ins imm0_255:$imm), IIC_Br,
                "svc", "\t$imm", []>, Encoding16, Sched<[WriteBr]> {
  bits<8> imm;
  let Inst{15-12} = 0b1101;
  let Inst{11-8}  = 0b1111;
  let Inst{7-0}   = imm;
}

// The assembler uses 0xDEFE for a trap instruction.
let isBarrier = 1, isTerminator = 1 in
def tTRAP : TI<(outs), (ins), IIC_Br,
               "trap", [(trap)]>, Encoding16, Sched<[WriteBr]> {
  let Inst = 0xdefe;
}

//===----------------------------------------------------------------------===//
//  Load Store Instructions.
//

// PC-relative loads need to be matched first as constant pool accesses need to
// always be PC-relative. We do this using AddedComplexity, as the pattern is
// simpler than the patterns of the other load instructions.
let canFoldAsLoad = 1, isReMaterializable = 1, AddedComplexity = 10 in
def tLDRpci : T1pIs<(outs tGPR:$Rt), (ins t_addrmode_pc:$addr), IIC_iLoad_i,
                  "ldr", "\t$Rt, $addr",
                  [(set tGPR:$Rt, (load (ARMWrapper tconstpool:$addr)))]>,
              T1Encoding<{0,1,0,0,1,?}> {
  // A6.2 & A8.6.59
  bits<3> Rt;
  bits<8> addr;
  let Inst{10-8} = Rt;
  let Inst{7-0}  = addr;
}

// SP-relative loads should be matched before standard immediate-offset loads as
// it means we avoid having to move SP to another register.
let canFoldAsLoad = 1 in
def tLDRspi : T1pIs<(outs tGPR:$Rt), (ins t_addrmode_sp:$addr), IIC_iLoad_i,
                    "ldr", "\t$Rt, $addr",
                    [(set tGPR:$Rt, (load t_addrmode_sp:$addr))]>,
              T1LdStSP<{1,?,?}> {
  bits<3> Rt;
  bits<8> addr;
  let Inst{10-8} = Rt;
  let Inst{7-0} = addr;
}

// Loads: reg/reg and reg/imm5
let canFoldAsLoad = 1, isReMaterializable = 1 in
multiclass thumb_ld_rr_ri_enc<bits<3> reg_opc, bits<4> imm_opc,
                              Operand AddrMode_r, Operand AddrMode_i,
                              AddrMode am, InstrItinClass itin_r,
                              InstrItinClass itin_i, string asm,
                              PatFrag opnode> {
  // Immediate-offset loads should be matched before register-offset loads as
  // when the offset is a constant it's simpler to first check if it fits in the
  // immediate offset field then fall back to register-offset if it doesn't.
  def i : // reg/imm5
    T1pILdStEncodeImm<imm_opc, 1 /* Load */,
                      (outs tGPR:$Rt), (ins AddrMode_i:$addr),
                      am, itin_i, asm, "\t$Rt, $addr",
                      [(set tGPR:$Rt, (opnode AddrMode_i:$addr))]>;
  // Register-offset loads are matched last.
  def r : // reg/reg
    T1pILdStEncode<reg_opc,
                   (outs tGPR:$Rt), (ins AddrMode_r:$addr),
                   am, itin_r, asm, "\t$Rt, $addr",
                   [(set tGPR:$Rt, (opnode AddrMode_r:$addr))]>;
}
// Stores: reg/reg and reg/imm5
multiclass thumb_st_rr_ri_enc<bits<3> reg_opc, bits<4> imm_opc,
                              Operand AddrMode_r, Operand AddrMode_i,
                              AddrMode am, InstrItinClass itin_r,
                              InstrItinClass itin_i, string asm,
                              PatFrag opnode> {
  def i : // reg/imm5
    T1pILdStEncodeImm<imm_opc, 0 /* Store */,
                      (outs), (ins tGPR:$Rt, AddrMode_i:$addr),
                      am, itin_i, asm, "\t$Rt, $addr",
                      [(opnode tGPR:$Rt, AddrMode_i:$addr)]>;
  def r : // reg/reg
    T1pILdStEncode<reg_opc,
                   (outs), (ins tGPR:$Rt, AddrMode_r:$addr),
                   am, itin_r, asm, "\t$Rt, $addr",
                   [(opnode tGPR:$Rt, AddrMode_r:$addr)]>;
}

// A8.6.57 & A8.6.60
defm tLDR  : thumb_ld_rr_ri_enc<0b100, 0b0110, t_addrmode_rr,
                                t_addrmode_is4, AddrModeT1_4,
                                IIC_iLoad_r, IIC_iLoad_i, "ldr",
                                load>;

// A8.6.64 & A8.6.61
defm tLDRB : thumb_ld_rr_ri_enc<0b110, 0b0111, t_addrmode_rr,
                                t_addrmode_is1, AddrModeT1_1,
                                IIC_iLoad_bh_r, IIC_iLoad_bh_i, "ldrb",
                                zextloadi8>;

// A8.6.76 & A8.6.73
defm tLDRH : thumb_ld_rr_ri_enc<0b101, 0b1000, t_addrmode_rr,
                                t_addrmode_is2, AddrModeT1_2,
                                IIC_iLoad_bh_r, IIC_iLoad_bh_i, "ldrh",
                                zextloadi16>;

let AddedComplexity = 10 in
def tLDRSB :                    // A8.6.80
  T1pILdStEncode<0b011, (outs tGPR:$Rt), (ins t_addrmode_rr:$addr),
                 AddrModeT1_1, IIC_iLoad_bh_r,
                 "ldrsb", "\t$Rt, $addr",
                 [(set tGPR:$Rt, (sextloadi8 t_addrmode_rr:$addr))]>;

let AddedComplexity = 10 in
def tLDRSH :                    // A8.6.84
  T1pILdStEncode<0b111, (outs tGPR:$Rt), (ins t_addrmode_rr:$addr),
                 AddrModeT1_2, IIC_iLoad_bh_r,
                 "ldrsh", "\t$Rt, $addr",
                 [(set tGPR:$Rt, (sextloadi16 t_addrmode_rr:$addr))]>;


def tSTRspi : T1pIs<(outs), (ins tGPR:$Rt, t_addrmode_sp:$addr), IIC_iStore_i,
                    "str", "\t$Rt, $addr",
                    [(store tGPR:$Rt, t_addrmode_sp:$addr)]>,
              T1LdStSP<{0,?,?}> {
  bits<3> Rt;
  bits<8> addr;
  let Inst{10-8} = Rt;
  let Inst{7-0} = addr;
}

// A8.6.194 & A8.6.192
defm tSTR  : thumb_st_rr_ri_enc<0b000, 0b0110, t_addrmode_rr,
                                t_addrmode_is4, AddrModeT1_4,
                                IIC_iStore_r, IIC_iStore_i, "str",
                                store>;

// A8.6.197 & A8.6.195
defm tSTRB : thumb_st_rr_ri_enc<0b010, 0b0111, t_addrmode_rr,
                                t_addrmode_is1, AddrModeT1_1,
                                IIC_iStore_bh_r, IIC_iStore_bh_i, "strb",
                                truncstorei8>;

// A8.6.207 & A8.6.205
defm tSTRH : thumb_st_rr_ri_enc<0b001, 0b1000, t_addrmode_rr,
                               t_addrmode_is2, AddrModeT1_2,
                               IIC_iStore_bh_r, IIC_iStore_bh_i, "strh",
                               truncstorei16>;


//===----------------------------------------------------------------------===//
//  Load / store multiple Instructions.
//

// These require base address to be written back or one of the loaded regs.
let hasSideEffects = 0 in {

let mayLoad = 1, hasExtraDefRegAllocReq = 1, variadicOpsAreDefs = 1 in
def tLDMIA : T1I<(outs), (ins tGPR:$Rn, pred:$p, reglist:$regs, variable_ops),
        IIC_iLoad_m, "ldm${p}\t$Rn, $regs", []>, T1Encoding<{1,1,0,0,1,?}> {
  bits<3> Rn;
  bits<8> regs;
  let Inst{10-8} = Rn;
  let Inst{7-0}  = regs;
}

// Writeback version is just a pseudo, as there's no encoding difference.
// Writeback happens iff the base register is not in the destination register
// list.
let mayLoad = 1, hasExtraDefRegAllocReq = 1 in
def tLDMIA_UPD :
    InstTemplate<AddrModeNone, 0, IndexModeNone, Pseudo, GenericDomain,
                 "$Rn = $wb", IIC_iLoad_mu>,
    PseudoInstExpansion<(tLDMIA tGPR:$Rn, pred:$p, reglist:$regs)> {
  let Size = 2;
  let OutOperandList = (outs GPR:$wb);
  let InOperandList = (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops);
  let Pattern = [];
  let isCodeGenOnly = 1;
  let isPseudo = 1;
  list<Predicate> Predicates = [IsThumb];
}

// There is no non-writeback version of STM for Thumb.
let mayStore = 1, hasExtraSrcRegAllocReq = 1 in
def tSTMIA_UPD : Thumb1I<(outs GPR:$wb),
                         (ins tGPR:$Rn, pred:$p, reglist:$regs, variable_ops),
                         AddrModeNone, 2, IIC_iStore_mu,
                         "stm${p}\t$Rn!, $regs", "$Rn = $wb", []>,
                     T1Encoding<{1,1,0,0,0,?}> {
  bits<3> Rn;
  bits<8> regs;
  let Inst{10-8} = Rn;
  let Inst{7-0}  = regs;
}

} // hasSideEffects

def : InstAlias<"ldm${p} $Rn!, $regs",
                (tLDMIA tGPR:$Rn, pred:$p, reglist:$regs), 0>,
        Requires<[IsThumb, IsThumb1Only]>;

let mayLoad = 1, Uses = [SP], Defs = [SP], hasExtraDefRegAllocReq = 1,
    variadicOpsAreDefs = 1 in
def tPOP : T1I<(outs), (ins pred:$p, reglist:$regs, variable_ops),
               IIC_iPop,
               "pop${p}\t$regs", []>,
           T1Misc<{1,1,0,?,?,?,?}> {
  bits<16> regs;
  let Inst{8}   = regs{15};
  let Inst{7-0} = regs{7-0};
}

let mayStore = 1, Uses = [SP], Defs = [SP], hasExtraSrcRegAllocReq = 1 in
def tPUSH : T1I<(outs), (ins pred:$p, reglist:$regs, variable_ops),
                IIC_iStore_m,
                "push${p}\t$regs", []>,
            T1Misc<{0,1,0,?,?,?,?}> {
  bits<16> regs;
  let Inst{8}   = regs{14};
  let Inst{7-0} = regs{7-0};
}

//===----------------------------------------------------------------------===//
//  Arithmetic Instructions.
//

// Helper classes for encoding T1pI patterns:
class T1pIDPEncode<bits<4> opA, dag oops, dag iops, InstrItinClass itin,
                   string opc, string asm, list<dag> pattern>
    : T1pI<oops, iops, itin, opc, asm, pattern>,
      T1DataProcessing<opA> {
  bits<3> Rm;
  bits<3> Rn;
  let Inst{5-3} = Rm;
  let Inst{2-0} = Rn;
}
class T1pIMiscEncode<bits<7> opA, dag oops, dag iops, InstrItinClass itin,
                     string opc, string asm, list<dag> pattern>
    : T1pI<oops, iops, itin, opc, asm, pattern>,
      T1Misc<opA> {
  bits<3> Rm;
  bits<3> Rd;
  let Inst{5-3} = Rm;
  let Inst{2-0} = Rd;
}

// Helper classes for encoding T1sI patterns:
class T1sIDPEncode<bits<4> opA, dag oops, dag iops, InstrItinClass itin,
                   string opc, string asm, list<dag> pattern>
    : T1sI<oops, iops, itin, opc, asm, pattern>,
      T1DataProcessing<opA> {
  bits<3> Rd;
  bits<3> Rn;
  let Inst{5-3} = Rn;
  let Inst{2-0} = Rd;
}
class T1sIGenEncode<bits<5> opA, dag oops, dag iops, InstrItinClass itin,
                    string opc, string asm, list<dag> pattern>
    : T1sI<oops, iops, itin, opc, asm, pattern>,
      T1General<opA> {
  bits<3> Rm;
  bits<3> Rn;
  bits<3> Rd;
  let Inst{8-6} = Rm;
  let Inst{5-3} = Rn;
  let Inst{2-0} = Rd;
}
class T1sIGenEncodeImm<bits<5> opA, dag oops, dag iops, InstrItinClass itin,
                       string opc, string asm, list<dag> pattern>
    : T1sI<oops, iops, itin, opc, asm, pattern>,
      T1General<opA> {
  bits<3> Rd;
  bits<3> Rm;
  let Inst{5-3} = Rm;
  let Inst{2-0} = Rd;
}

// Helper classes for encoding T1sIt patterns:
class T1sItDPEncode<bits<4> opA, dag oops, dag iops, InstrItinClass itin,
                    string opc, string asm, list<dag> pattern>
    : T1sIt<oops, iops, itin, opc, asm, pattern>,
      T1DataProcessing<opA> {
  bits<3> Rdn;
  bits<3> Rm;
  let Inst{5-3} = Rm;
  let Inst{2-0} = Rdn;
}
class T1sItGenEncodeImm<bits<5> opA, dag oops, dag iops, InstrItinClass itin,
                        string opc, string asm, list<dag> pattern>
    : T1sIt<oops, iops, itin, opc, asm, pattern>,
      T1General<opA> {
  bits<3> Rdn;
  bits<8> imm8;
  let Inst{10-8} = Rdn;
  let Inst{7-0}  = imm8;
}

let isAdd = 1 in {
  // Add with carry register
  let isCommutable = 1, Uses = [CPSR] in
  def tADC :                      // A8.6.2
    T1sItDPEncode<0b0101, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm), IIC_iALUr,
                  "adc", "\t$Rdn, $Rm",
                  []>, Sched<[WriteALU]>;

  // Add immediate
  def tADDi3 :                    // A8.6.4 T1
    T1sIGenEncodeImm<0b01110, (outs tGPR:$Rd), (ins tGPR:$Rm, imm0_7:$imm3),
                     IIC_iALUi,
                     "add", "\t$Rd, $Rm, $imm3",
                     [(set tGPR:$Rd, (add tGPR:$Rm, imm0_7:$imm3))]>,
                     Sched<[WriteALU]> {
    bits<3> imm3;
    let Inst{8-6} = imm3;
  }

  def tADDi8 :                    // A8.6.4 T2
    T1sItGenEncodeImm<{1,1,0,?,?}, (outs tGPR:$Rdn),
                      (ins tGPR:$Rn, imm0_255:$imm8), IIC_iALUi,
                      "add", "\t$Rdn, $imm8",
                      [(set tGPR:$Rdn, (add tGPR:$Rn, imm8_255:$imm8))]>,
                      Sched<[WriteALU]>;

  // Add register
  let isCommutable = 1 in
  def tADDrr :                    // A8.6.6 T1
    T1sIGenEncode<0b01100, (outs tGPR:$Rd), (ins tGPR:$Rn, tGPR:$Rm),
                  IIC_iALUr,
                  "add", "\t$Rd, $Rn, $Rm",
                  [(set tGPR:$Rd, (add tGPR:$Rn, tGPR:$Rm))]>, Sched<[WriteALU]>;

  /// Similar to the above except these set the 's' bit so the
  /// instruction modifies the CPSR register.
  ///
  /// These opcodes will be converted to the real non-S opcodes by
  /// AdjustInstrPostInstrSelection after giving then an optional CPSR operand.
  let hasPostISelHook = 1, Defs = [CPSR] in {
    let isCommutable = 1, Uses = [CPSR] in
    def tADCS : tPseudoInst<(outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                            2, IIC_iALUr,
                            [(set tGPR:$Rdn, CPSR, (ARMadde tGPR:$Rn, tGPR:$Rm,
                                                            CPSR))]>,
                Requires<[IsThumb1Only]>,
                Sched<[WriteALU]>;

    def tADDSi3 : tPseudoInst<(outs tGPR:$Rd), (ins tGPR:$Rm, imm0_7:$imm3),
                              2, IIC_iALUi,
                              [(set tGPR:$Rd, CPSR, (ARMaddc tGPR:$Rm,
                                                             imm0_7:$imm3))]>,
                  Requires<[IsThumb1Only]>,
                  Sched<[WriteALU]>;

    def tADDSi8 : tPseudoInst<(outs tGPR:$Rdn), (ins tGPR:$Rn, imm0_255:$imm8),
                              2, IIC_iALUi,
                              [(set tGPR:$Rdn, CPSR, (ARMaddc tGPR:$Rn,
                                                      imm8_255:$imm8))]>,
                  Requires<[IsThumb1Only]>,
                  Sched<[WriteALU]>;

    let isCommutable = 1 in
    def tADDSrr : tPseudoInst<(outs tGPR:$Rd), (ins tGPR:$Rn, tGPR:$Rm),
                              2, IIC_iALUr,
                              [(set tGPR:$Rd, CPSR, (ARMaddc tGPR:$Rn,
                                                             tGPR:$Rm))]>,
                  Requires<[IsThumb1Only]>,
                  Sched<[WriteALU]>;
  }

  let hasSideEffects = 0 in
  def tADDhirr : T1pIt<(outs GPR:$Rdn), (ins GPR:$Rn, GPR:$Rm), IIC_iALUr,
                       "add", "\t$Rdn, $Rm", []>,
                 T1Special<{0,0,?,?}>, Sched<[WriteALU]> {
    // A8.6.6 T2
    bits<4> Rdn;
    bits<4> Rm;
    let Inst{7}   = Rdn{3};
    let Inst{6-3} = Rm;
    let Inst{2-0} = Rdn{2-0};
  }
}

// Thumb has more flexible short encodings for ADD than ORR, so use those where
// possible.
def : T1Pat<(or AddLikeOrOp:$Rn, imm0_7:$imm), (tADDi3 $Rn, imm0_7:$imm)>;

def : T1Pat<(or AddLikeOrOp:$Rn, imm8_255:$imm), (tADDi8 $Rn, imm8_255:$imm)>;

def : T1Pat<(or AddLikeOrOp:$Rn, tGPR:$Rm), (tADDrr $Rn, $Rm)>;


def : tInstAlias <"add${s}${p} $Rdn, $Rm",
                 (tADDrr tGPR:$Rdn,s_cc_out:$s, tGPR:$Rdn, tGPR:$Rm, pred:$p)>;

def : tInstSubst<"sub${s}${p} $rd, $rn, $imm",
                 (tADDi3 tGPR:$rd, s_cc_out:$s, tGPR:$rn, mod_imm1_7_neg:$imm, pred:$p)>;
def : tInstSubst<"sub${s}${p} $rdn, $imm",
                 (tADDi8 tGPR:$rdn, s_cc_out:$s, mod_imm8_255_neg:$imm, pred:$p)>;


// AND register
let isCommutable = 1 in
def tAND :                      // A8.6.12
  T1sItDPEncode<0b0000, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iBITr,
                "and", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (and tGPR:$Rn, tGPR:$Rm))]>, Sched<[WriteALU]>;

// ASR immediate
def tASRri :                    // A8.6.14
  T1sIGenEncodeImm<{0,1,0,?,?}, (outs tGPR:$Rd), (ins tGPR:$Rm, imm_sr:$imm5),
                   IIC_iMOVsi,
                   "asr", "\t$Rd, $Rm, $imm5",
                   [(set tGPR:$Rd, (sra tGPR:$Rm, (i32 imm_sr:$imm5)))]>,
                   Sched<[WriteALU]> {
  bits<5> imm5;
  let Inst{10-6} = imm5;
}

// ASR register
def tASRrr :                    // A8.6.15
  T1sItDPEncode<0b0100, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iMOVsr,
                "asr", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (sra tGPR:$Rn, tGPR:$Rm))]>, Sched<[WriteALU]>;

// BIC register
def tBIC :                      // A8.6.20
  T1sItDPEncode<0b1110, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iBITr,
                "bic", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (and tGPR:$Rn, (not tGPR:$Rm)))]>,
                Sched<[WriteALU]>;

// CMN register
let isCompare = 1, Defs = [CPSR] in {
//FIXME: Disable CMN, as CCodes are backwards from compare expectations
//       Compare-to-zero still works out, just not the relationals
//def tCMN :                     // A8.6.33
//  T1pIDPEncode<0b1011, (outs), (ins tGPR:$lhs, tGPR:$rhs),
//               IIC_iCMPr,
//               "cmn", "\t$lhs, $rhs",
//               [(ARMcmp tGPR:$lhs, (ineg tGPR:$rhs))]>;

def tCMNz :                     // A8.6.33
  T1pIDPEncode<0b1011, (outs), (ins tGPR:$Rn, tGPR:$Rm),
               IIC_iCMPr,
               "cmn", "\t$Rn, $Rm",
               [(ARMcmpZ tGPR:$Rn, (ineg tGPR:$Rm))]>, Sched<[WriteCMP]>;

} // isCompare = 1, Defs = [CPSR]

// CMP immediate
let isCompare = 1, Defs = [CPSR] in {
def tCMPi8 : T1pI<(outs), (ins tGPR:$Rn, imm0_255:$imm8), IIC_iCMPi,
                  "cmp", "\t$Rn, $imm8",
                  [(ARMcmp tGPR:$Rn, imm0_255:$imm8)]>,
             T1General<{1,0,1,?,?}>, Sched<[WriteCMP]> {
  // A8.6.35
  bits<3> Rn;
  bits<8> imm8;
  let Inst{10-8} = Rn;
  let Inst{7-0}  = imm8;
}

// CMP register
def tCMPr :                     // A8.6.36 T1
  T1pIDPEncode<0b1010, (outs), (ins tGPR:$Rn, tGPR:$Rm),
               IIC_iCMPr,
               "cmp", "\t$Rn, $Rm",
               [(ARMcmp tGPR:$Rn, tGPR:$Rm)]>, Sched<[WriteCMP]>;

def tCMPhir : T1pI<(outs), (ins GPR:$Rn, GPR:$Rm), IIC_iCMPr,
                   "cmp", "\t$Rn, $Rm", []>,
              T1Special<{0,1,?,?}>, Sched<[WriteCMP]> {
  // A8.6.36 T2
  bits<4> Rm;
  bits<4> Rn;
  let Inst{7}   = Rn{3};
  let Inst{6-3} = Rm;
  let Inst{2-0} = Rn{2-0};
}
} // isCompare = 1, Defs = [CPSR]


// XOR register
let isCommutable = 1 in
def tEOR :                      // A8.6.45
  T1sItDPEncode<0b0001, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iBITr,
                "eor", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (xor tGPR:$Rn, tGPR:$Rm))]>, Sched<[WriteALU]>;

// LSL immediate
def tLSLri :                    // A8.6.88
  T1sIGenEncodeImm<{0,0,0,?,?}, (outs tGPR:$Rd), (ins tGPR:$Rm, imm0_31:$imm5),
                   IIC_iMOVsi,
                   "lsl", "\t$Rd, $Rm, $imm5",
                   [(set tGPR:$Rd, (shl tGPR:$Rm, (i32 imm:$imm5)))]>,
                   Sched<[WriteALU]> {
  bits<5> imm5;
  let Inst{10-6} = imm5;
}

// LSL register
def tLSLrr :                    // A8.6.89
  T1sItDPEncode<0b0010, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iMOVsr,
                "lsl", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (shl tGPR:$Rn, tGPR:$Rm))]>, Sched<[WriteALU]>;

// LSR immediate
def tLSRri :                    // A8.6.90
  T1sIGenEncodeImm<{0,0,1,?,?}, (outs tGPR:$Rd), (ins tGPR:$Rm, imm_sr:$imm5),
                   IIC_iMOVsi,
                   "lsr", "\t$Rd, $Rm, $imm5",
                   [(set tGPR:$Rd, (srl tGPR:$Rm, (i32 imm_sr:$imm5)))]>,
                   Sched<[WriteALU]> {
  bits<5> imm5;
  let Inst{10-6} = imm5;
}

// LSR register
def tLSRrr :                    // A8.6.91
  T1sItDPEncode<0b0011, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iMOVsr,
                "lsr", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (srl tGPR:$Rn, tGPR:$Rm))]>, Sched<[WriteALU]>;

// Move register
let isMoveImm = 1 in
def tMOVi8 : T1sI<(outs tGPR:$Rd), (ins imm0_255:$imm8), IIC_iMOVi,
                  "mov", "\t$Rd, $imm8",
                  [(set tGPR:$Rd, imm0_255:$imm8)]>,
             T1General<{1,0,0,?,?}>, Sched<[WriteALU]> {
  // A8.6.96
  bits<3> Rd;
  bits<8> imm8;
  let Inst{10-8} = Rd;
  let Inst{7-0}  = imm8;
}
// Because we have an explicit tMOVSr below, we need an alias to handle
// the immediate "movs" form here. Blech.
def : tInstAlias <"movs $Rdn, $imm",
                 (tMOVi8 tGPR:$Rdn, CPSR, imm0_255:$imm, 14, 0)>;

// A7-73: MOV(2) - mov setting flag.

let hasSideEffects = 0, isMoveReg = 1 in {
def tMOVr : Thumb1pI<(outs GPR:$Rd), (ins GPR:$Rm), AddrModeNone,
                      2, IIC_iMOVr,
                      "mov", "\t$Rd, $Rm", "", []>,
                  T1Special<{1,0,?,?}>, Sched<[WriteALU]> {
  // A8.6.97
  bits<4> Rd;
  bits<4> Rm;
  let Inst{7}   = Rd{3};
  let Inst{6-3} = Rm;
  let Inst{2-0} = Rd{2-0};
}
let Defs = [CPSR] in
def tMOVSr      : T1I<(outs tGPR:$Rd), (ins tGPR:$Rm), IIC_iMOVr,
                      "movs\t$Rd, $Rm", []>, Encoding16, Sched<[WriteALU]> {
  // A8.6.97
  bits<3> Rd;
  bits<3> Rm;
  let Inst{15-6} = 0b0000000000;
  let Inst{5-3}  = Rm;
  let Inst{2-0}  = Rd;
}
} // hasSideEffects

// Multiply register
let isCommutable = 1 in
def tMUL :                      // A8.6.105 T1
  Thumb1sI<(outs tGPR:$Rd), (ins tGPR:$Rn, tGPR:$Rm), AddrModeNone, 2,
           IIC_iMUL32, "mul", "\t$Rd, $Rn, $Rm", "$Rm = $Rd",
           [(set tGPR:$Rd, (mul tGPR:$Rn, tGPR:$Rm))]>,
      T1DataProcessing<0b1101> {
  bits<3> Rd;
  bits<3> Rn;
  let Inst{5-3} = Rn;
  let Inst{2-0} = Rd;
  let AsmMatchConverter = "cvtThumbMultiply";
}

def :tInstAlias<"mul${s}${p} $Rdm, $Rn", (tMUL tGPR:$Rdm, s_cc_out:$s, tGPR:$Rn,
                                               pred:$p)>;

// Move inverse register
def tMVN :                      // A8.6.107
  T1sIDPEncode<0b1111, (outs tGPR:$Rd), (ins tGPR:$Rn), IIC_iMVNr,
               "mvn", "\t$Rd, $Rn",
               [(set tGPR:$Rd, (not tGPR:$Rn))]>, Sched<[WriteALU]>;

// Bitwise or register
let isCommutable = 1 in
def tORR :                      // A8.6.114
  T1sItDPEncode<0b1100, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iBITr,
                "orr", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (or tGPR:$Rn, tGPR:$Rm))]>, Sched<[WriteALU]>;

// Swaps
def tREV :                      // A8.6.134
  T1pIMiscEncode<{1,0,1,0,0,0,?}, (outs tGPR:$Rd), (ins tGPR:$Rm),
                 IIC_iUNAr,
                 "rev", "\t$Rd, $Rm",
                 [(set tGPR:$Rd, (bswap tGPR:$Rm))]>,
                 Requires<[IsThumb, IsThumb1Only, HasV6]>, Sched<[WriteALU]>;

def tREV16 :                    // A8.6.135
  T1pIMiscEncode<{1,0,1,0,0,1,?}, (outs tGPR:$Rd), (ins tGPR:$Rm),
                 IIC_iUNAr,
                 "rev16", "\t$Rd, $Rm",
             [(set tGPR:$Rd, (rotr (bswap tGPR:$Rm), (i32 16)))]>,
                Requires<[IsThumb, IsThumb1Only, HasV6]>, Sched<[WriteALU]>;

def tREVSH :                    // A8.6.136
  T1pIMiscEncode<{1,0,1,0,1,1,?}, (outs tGPR:$Rd), (ins tGPR:$Rm),
                 IIC_iUNAr,
                 "revsh", "\t$Rd, $Rm",
                 [(set tGPR:$Rd, (sra (bswap tGPR:$Rm), (i32 16)))]>,
                 Requires<[IsThumb, IsThumb1Only, HasV6]>, Sched<[WriteALU]>;

// Rotate right register
def tROR :                      // A8.6.139
  T1sItDPEncode<0b0111, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iMOVsr,
                "ror", "\t$Rdn, $Rm",
                [(set tGPR:$Rdn, (rotr tGPR:$Rn, tGPR:$Rm))]>,
                Sched<[WriteALU]>;

// Negate register
def tRSB :                      // A8.6.141
  T1sIDPEncode<0b1001, (outs tGPR:$Rd), (ins tGPR:$Rn),
               IIC_iALUi,
               "rsb", "\t$Rd, $Rn, #0",
               [(set tGPR:$Rd, (ineg tGPR:$Rn))]>, Sched<[WriteALU]>;

// Subtract with carry register
let Uses = [CPSR] in
def tSBC :                      // A8.6.151
  T1sItDPEncode<0b0110, (outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iALUr,
                "sbc", "\t$Rdn, $Rm",
                []>,
                Sched<[WriteALU]>;

// Subtract immediate
def tSUBi3 :                    // A8.6.210 T1
  T1sIGenEncodeImm<0b01111, (outs tGPR:$Rd), (ins tGPR:$Rm, imm0_7:$imm3),
                   IIC_iALUi,
                   "sub", "\t$Rd, $Rm, $imm3",
                   [(set tGPR:$Rd, (add tGPR:$Rm, imm0_7_neg:$imm3))]>,
                   Sched<[WriteALU]> {
  bits<3> imm3;
  let Inst{8-6} = imm3;
}

def tSUBi8 :                    // A8.6.210 T2
  T1sItGenEncodeImm<{1,1,1,?,?}, (outs tGPR:$Rdn),
                    (ins tGPR:$Rn, imm0_255:$imm8), IIC_iALUi,
                    "sub", "\t$Rdn, $imm8",
                    [(set tGPR:$Rdn, (add tGPR:$Rn, imm8_255_neg:$imm8))]>,
                    Sched<[WriteALU]>;

def : tInstSubst<"add${s}${p} $rd, $rn, $imm",
                 (tSUBi3 tGPR:$rd, s_cc_out:$s, tGPR:$rn, mod_imm1_7_neg:$imm, pred:$p)>;


def : tInstSubst<"add${s}${p} $rdn, $imm",
                 (tSUBi8 tGPR:$rdn, s_cc_out:$s, mod_imm8_255_neg:$imm, pred:$p)>;


// Subtract register
def tSUBrr :                    // A8.6.212
  T1sIGenEncode<0b01101, (outs tGPR:$Rd), (ins tGPR:$Rn, tGPR:$Rm),
                IIC_iALUr,
                "sub", "\t$Rd, $Rn, $Rm",
                [(set tGPR:$Rd, (sub tGPR:$Rn, tGPR:$Rm))]>,
                Sched<[WriteALU]>;

def : tInstAlias <"sub${s}${p} $Rdn, $Rm",
                 (tSUBrr tGPR:$Rdn,s_cc_out:$s, tGPR:$Rdn, tGPR:$Rm, pred:$p)>;

/// Similar to the above except these set the 's' bit so the
/// instruction modifies the CPSR register.
///
/// These opcodes will be converted to the real non-S opcodes by
/// AdjustInstrPostInstrSelection after giving then an optional CPSR operand.
let hasPostISelHook = 1, Defs = [CPSR] in {
  let Uses = [CPSR] in
  def tSBCS : tPseudoInst<(outs tGPR:$Rdn), (ins tGPR:$Rn, tGPR:$Rm),
                          2, IIC_iALUr,
                          [(set tGPR:$Rdn, CPSR, (ARMsube tGPR:$Rn, tGPR:$Rm,
                                                          CPSR))]>,
              Requires<[IsThumb1Only]>,
              Sched<[WriteALU]>;

  def tSUBSi3 : tPseudoInst<(outs tGPR:$Rd), (ins tGPR:$Rm, imm0_7:$imm3),
                            2, IIC_iALUi,
                            [(set tGPR:$Rd, CPSR, (ARMsubc tGPR:$Rm,
                                                           imm0_7:$imm3))]>,
                Requires<[IsThumb1Only]>,
                Sched<[WriteALU]>;

  def tSUBSi8 : tPseudoInst<(outs tGPR:$Rdn), (ins tGPR:$Rn, imm0_255:$imm8),
                            2, IIC_iALUi,
                            [(set tGPR:$Rdn, CPSR, (ARMsubc tGPR:$Rn,
                                                            imm8_255:$imm8))]>,
                Requires<[IsThumb1Only]>,
                Sched<[WriteALU]>;

  def tSUBSrr : tPseudoInst<(outs tGPR:$Rd), (ins tGPR:$Rn, tGPR:$Rm),
                            2, IIC_iALUr,
                            [(set tGPR:$Rd, CPSR, (ARMsubc tGPR:$Rn,
                                                           tGPR:$Rm))]>,
                Requires<[IsThumb1Only]>,
                Sched<[WriteALU]>;

  def tRSBS   : tPseudoInst<(outs tGPR:$Rd), (ins tGPR:$Rn),
                            2, IIC_iALUr,
                            [(set tGPR:$Rd, CPSR, (ARMsubc 0, tGPR:$Rn))]>,
                Requires<[IsThumb1Only]>,
                Sched<[WriteALU]>;
}


def : T1Pat<(ARMsubs tGPR:$Rn, tGPR:$Rm), (tSUBSrr $Rn, $Rm)>;
def : T1Pat<(ARMsubs tGPR:$Rn, imm0_7:$imm3), (tSUBSi3 $Rn, imm0_7:$imm3)>;
def : T1Pat<(ARMsubs tGPR:$Rn, imm0_255:$imm8), (tSUBSi8 $Rn, imm0_255:$imm8)>;


// Sign-extend byte
def tSXTB :                     // A8.6.222
  T1pIMiscEncode<{0,0,1,0,0,1,?}, (outs tGPR:$Rd), (ins tGPR:$Rm),
                 IIC_iUNAr,
                 "sxtb", "\t$Rd, $Rm",
                 [(set tGPR:$Rd, (sext_inreg tGPR:$Rm, i8))]>,
                 Requires<[IsThumb, IsThumb1Only, HasV6]>,
                 Sched<[WriteALU]>;

// Sign-extend short
def tSXTH :                     // A8.6.224
  T1pIMiscEncode<{0,0,1,0,0,0,?}, (outs tGPR:$Rd), (ins tGPR:$Rm),
                 IIC_iUNAr,
                 "sxth", "\t$Rd, $Rm",
                 [(set tGPR:$Rd, (sext_inreg tGPR:$Rm, i16))]>,
                 Requires<[IsThumb, IsThumb1Only, HasV6]>,
                 Sched<[WriteALU]>;

// Test
let isCompare = 1, isCommutable = 1, Defs = [CPSR] in
def tTST :                      // A8.6.230
  T1pIDPEncode<0b1000, (outs), (ins tGPR:$Rn, tGPR:$Rm), IIC_iTSTr,
               "tst", "\t$Rn, $Rm",
               [(ARMcmpZ (and_su tGPR:$Rn, tGPR:$Rm), 0)]>,
               Sched<[WriteALU]>;

// A8.8.247  UDF - Undefined (Encoding T1)
def tUDF : TI<(outs), (ins imm0_255:$imm8), IIC_Br, "udf\t$imm8",
              [(int_arm_undefined imm0_255:$imm8)]>, Encoding16 {
  bits<8> imm8;
  let Inst{15-12} = 0b1101;
  let Inst{11-8} = 0b1110;
  let Inst{7-0} = imm8;
}

def : Pat<(debugtrap), (tBKPT 0)>, Requires<[IsThumb, HasV5T]>;
def : Pat<(debugtrap), (tUDF 254)>, Requires<[IsThumb, NoV5T]>;

def t__brkdiv0 : TI<(outs), (ins), IIC_Br, "__brkdiv0",
                    [(int_arm_undefined 249)]>, Encoding16,
    Requires<[IsThumb, IsWindows]> {
  let Inst = 0xdef9;
  let isTerminator = 1;
}

// Zero-extend byte
def tUXTB :                     // A8.6.262
  T1pIMiscEncode<{0,0,1,0,1,1,?}, (outs tGPR:$Rd), (ins tGPR:$Rm),
                 IIC_iUNAr,
                 "uxtb", "\t$Rd, $Rm",
                 [(set tGPR:$Rd, (and tGPR:$Rm, 0xFF))]>,
                 Requires<[IsThumb, IsThumb1Only, HasV6]>,
                 Sched<[WriteALU]>;

// Zero-extend short
def tUXTH :                     // A8.6.264
  T1pIMiscEncode<{0,0,1,0,1,0,?}, (outs tGPR:$Rd), (ins tGPR:$Rm),
                 IIC_iUNAr,
                 "uxth", "\t$Rd, $Rm",
                 [(set tGPR:$Rd, (and tGPR:$Rm, 0xFFFF))]>,
                 Requires<[IsThumb, IsThumb1Only, HasV6]>, Sched<[WriteALU]>;

// Conditional move tMOVCCr - Used to implement the Thumb SELECT_CC operation.
// Expanded after instruction selection into a branch sequence.
let usesCustomInserter = 1 in  // Expanded after instruction selection.
  def tMOVCCr_pseudo :
  PseudoInst<(outs tGPR:$dst), (ins tGPR:$false, tGPR:$true, cmovpred:$p),
             NoItinerary,
             [(set tGPR:$dst, (ARMcmov tGPR:$false, tGPR:$true, cmovpred:$p))]>;

// tLEApcrel - Load a pc-relative address into a register without offending the
// assembler.

def tADR : T1I<(outs tGPR:$Rd), (ins t_adrlabel:$addr, pred:$p),
               IIC_iALUi, "adr{$p}\t$Rd, $addr", []>,
               T1Encoding<{1,0,1,0,0,?}>, Sched<[WriteALU]> {
  bits<3> Rd;
  bits<8> addr;
  let Inst{10-8} = Rd;
  let Inst{7-0} = addr;
  let DecoderMethod = "DecodeThumbAddSpecialReg";
}

let hasSideEffects = 0, isReMaterializable = 1 in
def tLEApcrel   : tPseudoInst<(outs tGPR:$Rd), (ins i32imm:$label, pred:$p),
                              2, IIC_iALUi, []>, Sched<[WriteALU]>;

let hasSideEffects = 1 in
def tLEApcrelJT : tPseudoInst<(outs tGPR:$Rd),
                              (ins i32imm:$label, pred:$p),
                              2, IIC_iALUi, []>, Sched<[WriteALU]>;

// Thumb-1 doesn't have the TBB or TBH instructions, but we can synthesize them
// and make use of the same compressed jump table format as Thumb-2.
let Size = 2, isBranch = 1, isTerminator = 1, isBarrier = 1,
    isIndirectBranch = 1 in {
def tTBB_JT : tPseudoInst<(outs),
        (ins tGPRwithpc:$base, tGPR:$index, i32imm:$jt, i32imm:$pclbl), 0,
         IIC_Br, []>, Sched<[WriteBr]>;

def tTBH_JT : tPseudoInst<(outs),
        (ins tGPRwithpc:$base, tGPR:$index, i32imm:$jt, i32imm:$pclbl), 0,
         IIC_Br, []>,  Sched<[WriteBr]>;
}

//===----------------------------------------------------------------------===//
// TLS Instructions
//

// __aeabi_read_tp preserves the registers r1-r3.
// This is a pseudo inst so that we can get the encoding right,
// complete with fixup for the aeabi_read_tp function.
let isCall = 1, Defs = [R0, R12, LR, CPSR], Uses = [SP] in
def tTPsoft : tPseudoInst<(outs), (ins), 4, IIC_Br,
                          [(set R0, ARMthread_pointer)]>,
                          Sched<[WriteBr]>;

//===----------------------------------------------------------------------===//
// SJLJ Exception handling intrinsics
//

// eh_sjlj_setjmp() is an instruction sequence to store the return address and
// save #0 in R0 for the non-longjmp case.  Since by its nature we may be coming
// from some other function to get here, and we're using the stack frame for the
// containing function to save/restore registers, we can't keep anything live in
// regs across the eh_sjlj_setjmp(), else it will almost certainly have been
// tromped upon when we get here from a longjmp(). We force everything out of
// registers except for our own input by listing the relevant registers in
// Defs. By doing so, we also cause the prologue/epilogue code to actively
// preserve all of the callee-saved resgisters, which is exactly what we want.
// $val is a scratch register for our use.
let Defs = [ R0,  R1,  R2,  R3,  R4,  R5,  R6,  R7, R12, CPSR ],
    hasSideEffects = 1, isBarrier = 1, isCodeGenOnly = 1,
    usesCustomInserter = 1 in
def tInt_eh_sjlj_setjmp : ThumbXI<(outs),(ins tGPR:$src, tGPR:$val),
                                  AddrModeNone, 0, NoItinerary, "","",
                          [(set R0, (ARMeh_sjlj_setjmp tGPR:$src, tGPR:$val))]>;

// FIXME: Non-IOS version(s)
let isBarrier = 1, hasSideEffects = 1, isTerminator = 1, isCodeGenOnly = 1,
    Defs = [ R7, LR, SP ] in
def tInt_eh_sjlj_longjmp : XI<(outs), (ins GPR:$src, GPR:$scratch),
                              AddrModeNone, 0, IndexModeNone,
                              Pseudo, NoItinerary, "", "",
                              [(ARMeh_sjlj_longjmp GPR:$src, GPR:$scratch)]>,
                             Requires<[IsThumb,IsNotWindows]>;

let isBarrier = 1, hasSideEffects = 1, isTerminator = 1, isCodeGenOnly = 1,
    Defs = [ R11, LR, SP ] in
def tInt_WIN_eh_sjlj_longjmp
  : XI<(outs), (ins GPR:$src, GPR:$scratch), AddrModeNone, 0, IndexModeNone,
       Pseudo, NoItinerary, "", "", [(ARMeh_sjlj_longjmp GPR:$src, GPR:$scratch)]>,
    Requires<[IsThumb,IsWindows]>;

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//

// Comparisons
def : T1Pat<(ARMcmpZ tGPR:$Rn, imm0_255:$imm8),
            (tCMPi8  tGPR:$Rn, imm0_255:$imm8)>;
def : T1Pat<(ARMcmpZ tGPR:$Rn, tGPR:$Rm),
            (tCMPr   tGPR:$Rn, tGPR:$Rm)>;

// Bswap 16 with load/store
def : T1Pat<(srl (bswap (extloadi16 t_addrmode_is2:$addr)), (i32 16)),
            (tREV16 (tLDRHi t_addrmode_is2:$addr))>;
def : T1Pat<(srl (bswap (extloadi16 t_addrmode_rr:$addr)), (i32 16)),
            (tREV16 (tLDRHr t_addrmode_rr:$addr))>;
def : T1Pat<(truncstorei16 (srl (bswap tGPR:$Rn), (i32 16)),
                           t_addrmode_is2:$addr),
            (tSTRHi(tREV16 tGPR:$Rn), t_addrmode_is2:$addr)>;
def : T1Pat<(truncstorei16 (srl (bswap tGPR:$Rn), (i32 16)),
                           t_addrmode_rr:$addr),
            (tSTRHr (tREV16 tGPR:$Rn), t_addrmode_rr:$addr)>;

// ConstantPool
def : T1Pat<(ARMWrapper  tconstpool  :$dst), (tLEApcrel tconstpool  :$dst)>;

// GlobalAddress
def tLDRLIT_ga_pcrel : PseudoInst<(outs tGPR:$dst), (ins i32imm:$addr),
                                  IIC_iLoadiALU,
                                  [(set tGPR:$dst,
                                        (ARMWrapperPIC tglobaladdr:$addr))]>,
                       Requires<[IsThumb, DontUseMovtInPic]>;

def tLDRLIT_ga_abs : PseudoInst<(outs tGPR:$dst), (ins i32imm:$src),
                                IIC_iLoad_i,
                                [(set tGPR:$dst,
                                      (ARMWrapper tglobaladdr:$src))]>,
                     Requires<[IsThumb, DontUseMovt]>;

// TLS globals
def : Pat<(ARMWrapperPIC tglobaltlsaddr:$addr),
          (tLDRLIT_ga_pcrel tglobaltlsaddr:$addr)>,
      Requires<[IsThumb, DontUseMovtInPic]>;
def : Pat<(ARMWrapper tglobaltlsaddr:$addr),
          (tLDRLIT_ga_abs tglobaltlsaddr:$addr)>,
      Requires<[IsThumb, DontUseMovt]>;


// JumpTable
def : T1Pat<(ARMWrapperJT tjumptable:$dst),
            (tLEApcrelJT tjumptable:$dst)>;

// Direct calls
def : T1Pat<(ARMcall texternalsym:$func), (tBL texternalsym:$func)>,
      Requires<[IsThumb]>;

// zextload i1 -> zextload i8
def : T1Pat<(zextloadi1 t_addrmode_is1:$addr),
            (tLDRBi t_addrmode_is1:$addr)>;
def : T1Pat<(zextloadi1 t_addrmode_rr:$addr),
            (tLDRBr t_addrmode_rr:$addr)>;

// extload from the stack -> word load from the stack, as it avoids having to
// materialize the base in a separate register. This only works when a word
// load puts the byte/halfword value in the same place in the register that the
// byte/halfword load would, i.e. when little-endian.
def : T1Pat<(extloadi1  t_addrmode_sp:$addr), (tLDRspi t_addrmode_sp:$addr)>,
      Requires<[IsThumb, IsThumb1Only, IsLE]>;
def : T1Pat<(extloadi8  t_addrmode_sp:$addr), (tLDRspi t_addrmode_sp:$addr)>,
      Requires<[IsThumb, IsThumb1Only, IsLE]>;
def : T1Pat<(extloadi16 t_addrmode_sp:$addr), (tLDRspi t_addrmode_sp:$addr)>,
      Requires<[IsThumb, IsThumb1Only, IsLE]>;

// extload -> zextload
def : T1Pat<(extloadi1  t_addrmode_is1:$addr), (tLDRBi t_addrmode_is1:$addr)>;
def : T1Pat<(extloadi1  t_addrmode_rr:$addr),  (tLDRBr t_addrmode_rr:$addr)>;
def : T1Pat<(extloadi8  t_addrmode_is1:$addr), (tLDRBi t_addrmode_is1:$addr)>;
def : T1Pat<(extloadi8  t_addrmode_rr:$addr),  (tLDRBr t_addrmode_rr:$addr)>;
def : T1Pat<(extloadi16 t_addrmode_is2:$addr), (tLDRHi t_addrmode_is2:$addr)>;
def : T1Pat<(extloadi16 t_addrmode_rr:$addr),  (tLDRHr t_addrmode_rr:$addr)>;

// post-inc loads and stores

// post-inc LDR -> LDM r0!, {r1}. The way operands are layed out in LDMs is
// different to how ISel expects them for a post-inc load, so use a pseudo
// and expand it just after ISel.
let usesCustomInserter = 1, mayLoad =1,
    Constraints = "$Rn = $Rn_wb,@earlyclobber $Rn_wb" in
 def tLDR_postidx: tPseudoInst<(outs rGPR:$Rt, rGPR:$Rn_wb),
                               (ins rGPR:$Rn, pred:$p),
                               4, IIC_iStore_ru,
                               []>;

// post-inc STR -> STM r0!, {r1}. The layout of this (because it doesn't def
// multiple registers) is the same in ISel as MachineInstr, so there's no need
// for a pseudo.
def : T1Pat<(post_store rGPR:$Rt, rGPR:$Rn, 4),
            (tSTMIA_UPD rGPR:$Rn, rGPR:$Rt)>;

// If it's impossible to use [r,r] address mode for sextload, select to
// ldr{b|h} + sxt{b|h} instead.
def : T1Pat<(sextloadi8 t_addrmode_is1:$addr),
            (tSXTB (tLDRBi t_addrmode_is1:$addr))>,
      Requires<[IsThumb, IsThumb1Only, HasV6]>;
def : T1Pat<(sextloadi8 t_addrmode_rr:$addr),
            (tSXTB (tLDRBr t_addrmode_rr:$addr))>,
      Requires<[IsThumb, IsThumb1Only, HasV6]>;
def : T1Pat<(sextloadi16 t_addrmode_is2:$addr),
            (tSXTH (tLDRHi t_addrmode_is2:$addr))>,
      Requires<[IsThumb, IsThumb1Only, HasV6]>;
def : T1Pat<(sextloadi16 t_addrmode_rr:$addr),
            (tSXTH (tLDRHr t_addrmode_rr:$addr))>,
      Requires<[IsThumb, IsThumb1Only, HasV6]>;

def : T1Pat<(sextloadi8 t_addrmode_is1:$addr),
            (tASRri (tLSLri (tLDRBi t_addrmode_is1:$addr), 24), 24)>;
def : T1Pat<(sextloadi8 t_addrmode_rr:$addr),
            (tASRri (tLSLri (tLDRBr t_addrmode_rr:$addr), 24), 24)>;
def : T1Pat<(sextloadi16 t_addrmode_is2:$addr),
            (tASRri (tLSLri (tLDRHi t_addrmode_is2:$addr), 16), 16)>;
def : T1Pat<(sextloadi16 t_addrmode_rr:$addr),
            (tASRri (tLSLri (tLDRHr t_addrmode_rr:$addr), 16), 16)>;

def : T1Pat<(atomic_load_8 t_addrmode_is1:$src),
             (tLDRBi t_addrmode_is1:$src)>;
def : T1Pat<(atomic_load_8 t_addrmode_rr:$src),
             (tLDRBr t_addrmode_rr:$src)>;
def : T1Pat<(atomic_load_16 t_addrmode_is2:$src),
             (tLDRHi t_addrmode_is2:$src)>;
def : T1Pat<(atomic_load_16 t_addrmode_rr:$src),
             (tLDRHr t_addrmode_rr:$src)>;
def : T1Pat<(atomic_load_32 t_addrmode_is4:$src),
             (tLDRi t_addrmode_is4:$src)>;
def : T1Pat<(atomic_load_32 t_addrmode_rr:$src),
             (tLDRr t_addrmode_rr:$src)>;
def : T1Pat<(atomic_store_8 t_addrmode_is1:$ptr, tGPR:$val),
             (tSTRBi tGPR:$val, t_addrmode_is1:$ptr)>;
def : T1Pat<(atomic_store_8 t_addrmode_rr:$ptr, tGPR:$val),
             (tSTRBr tGPR:$val, t_addrmode_rr:$ptr)>;
def : T1Pat<(atomic_store_16 t_addrmode_is2:$ptr, tGPR:$val),
             (tSTRHi tGPR:$val, t_addrmode_is2:$ptr)>;
def : T1Pat<(atomic_store_16 t_addrmode_rr:$ptr, tGPR:$val),
             (tSTRHr tGPR:$val, t_addrmode_rr:$ptr)>;
def : T1Pat<(atomic_store_32 t_addrmode_is4:$ptr, tGPR:$val),
             (tSTRi tGPR:$val, t_addrmode_is4:$ptr)>;
def : T1Pat<(atomic_store_32 t_addrmode_rr:$ptr, tGPR:$val),
             (tSTRr tGPR:$val, t_addrmode_rr:$ptr)>;

// Large immediate handling.

// Two piece imms.
def : T1Pat<(i32 thumb_immshifted:$src),
            (tLSLri (tMOVi8 (thumb_immshifted_val imm:$src)),
                    (thumb_immshifted_shamt imm:$src))>;

def : T1Pat<(i32 imm0_255_comp:$src),
            (tMVN (tMOVi8 (imm_not_XFORM imm:$src)))>;

def : T1Pat<(i32 imm256_510:$src),
            (tADDi8 (tMOVi8 255),
                    (thumb_imm256_510_addend imm:$src))>;

// Pseudo instruction that combines ldr from constpool and add pc. This should
// be expanded into two instructions late to allow if-conversion and
// scheduling.
let isReMaterializable = 1 in
def tLDRpci_pic : PseudoInst<(outs GPR:$dst), (ins i32imm:$addr, pclabel:$cp),
                             NoItinerary,
               [(set GPR:$dst, (ARMpic_add (load (ARMWrapper tconstpool:$addr)),
                                           imm:$cp))]>,
               Requires<[IsThumb, IsThumb1Only]>;

// Pseudo-instruction for merged POP and return.
// FIXME: remove when we have a way to marking a MI with these properties.
let isReturn = 1, isTerminator = 1, isBarrier = 1, mayLoad = 1,
    hasExtraDefRegAllocReq = 1 in
def tPOP_RET : tPseudoExpand<(outs), (ins pred:$p, reglist:$regs, variable_ops),
                           2, IIC_iPop_Br, [],
                           (tPOP pred:$p, reglist:$regs)>, Sched<[WriteBrL]>;

// Indirect branch using "mov pc, $Rm"
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in {
  def tBRIND : tPseudoExpand<(outs), (ins GPR:$Rm, pred:$p),
                  2, IIC_Br, [(brind GPR:$Rm)],
                  (tMOVr PC, GPR:$Rm, pred:$p)>, Sched<[WriteBr]>;
}


// In Thumb1, "nop" is encoded as a "mov r8, r8". Technically, the bf00
// encoding is available on ARMv6K, but we don't differentiate that finely.
def : InstAlias<"nop", (tMOVr R8, R8, 14, 0), 0>, Requires<[IsThumb, IsThumb1Only]>;


// "neg" is and alias for "rsb rd, rn, #0"
def : tInstAlias<"neg${s}${p} $Rd, $Rm",
                 (tRSB tGPR:$Rd, s_cc_out:$s, tGPR:$Rm, pred:$p)>;


// Implied destination operand forms for shifts.
def : tInstAlias<"lsl${s}${p} $Rdm, $imm",
             (tLSLri tGPR:$Rdm, cc_out:$s, tGPR:$Rdm, imm0_31:$imm, pred:$p)>;
def : tInstAlias<"lsr${s}${p} $Rdm, $imm",
             (tLSRri tGPR:$Rdm, cc_out:$s, tGPR:$Rdm, imm_sr:$imm, pred:$p)>;
def : tInstAlias<"asr${s}${p} $Rdm, $imm",
             (tASRri tGPR:$Rdm, cc_out:$s, tGPR:$Rdm, imm_sr:$imm, pred:$p)>;

// Pseudo instruction ldr Rt, =immediate
def tLDRConstPool
  : tAsmPseudo<"ldr${p} $Rt, $immediate",
               (ins tGPR:$Rt, const_pool_asm_imm:$immediate, pred:$p)>;
