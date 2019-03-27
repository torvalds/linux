//===-- LanaiInstrInfo.td - Target Description for Lanai Target -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the Lanai instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

include "LanaiInstrFormats.td"

// -------------------------------------------------- //
// Instruction Operands and Patterns
// -------------------------------------------------- //

//  These are target-independent nodes, but have target-specific formats.
def SDT_LanaiCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>,
                                            SDTCisVT<1, i32>]>;
def SDT_LanaiCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>,
                                          SDTCisVT<1, i32>]>;
def SDT_LanaiCall         : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;
def SDT_LanaiSetFlag      : SDTypeProfile<0,  2, [SDTCisSameAs<0, 1>]>;
def SDT_LanaiSelectCC     : SDTypeProfile<1,  3, [SDTCisSameAs<0, 1>,
                                                  SDTCisSameAs<1, 2>]>;
def SDT_LanaiSetCC        : SDTypeProfile<1,  1, [SDTCisVT<0, i32>,
                                                  SDTCisVT<1, i32>]>;
def SDT_LanaiBrCC         : SDTypeProfile<0,  2, [SDTCisVT<0, OtherVT>,
                                                  SDTCisVT<1, i32>]>;
def SDT_LanaiAdjDynAlloc  : SDTypeProfile<1,  1, [SDTCisVT<0, i32>,
                                                  SDTCisVT<1, i32>]>;

def Call             : SDNode<"LanaiISD::CALL", SDT_LanaiCall,
                              [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                               SDNPVariadic]>;
def RetFlag          : SDNode<"LanaiISD::RET_FLAG", SDTNone,
                              [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def CallSeqStart     : SDNode<"ISD::CALLSEQ_START", SDT_LanaiCallSeqStart,
                              [SDNPHasChain, SDNPOutGlue]>;
def CallSeqEnd       : SDNode<"ISD::CALLSEQ_END", SDT_LanaiCallSeqEnd,
                              [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def LanaiSetFlag     : SDNode<"LanaiISD::SET_FLAG", SDT_LanaiSetFlag,
                              [SDNPOutGlue]>;
def LanaiSubbF       : SDNode<"LanaiISD::SUBBF", SDT_LanaiSetFlag,
                              [SDNPOutGlue, SDNPInGlue]>;
def LanaiBrCC        : SDNode<"LanaiISD::BR_CC", SDT_LanaiBrCC,
                              [SDNPHasChain, SDNPInGlue]>;
def LanaiSelectCC    : SDNode<"LanaiISD::SELECT_CC", SDT_LanaiSelectCC,
                              [SDNPInGlue]>;
def LanaiSetCC       : SDNode<"LanaiISD::SETCC", SDT_LanaiSetCC,
                              [SDNPInGlue]>;
def LanaiHi          : SDNode<"LanaiISD::HI", SDTIntUnaryOp>;
def LanaiLo          : SDNode<"LanaiISD::LO", SDTIntUnaryOp>;
def LanaiSmall       : SDNode<"LanaiISD::SMALL", SDTIntUnaryOp>;
def LanaiAdjDynAlloc : SDNode<"LanaiISD::ADJDYNALLOC", SDT_LanaiAdjDynAlloc>;

// Extract bits 0-15 (low-end) of an immediate value.
def LO16 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((uint64_t)N->getZExtValue() & 0xffff,
                                   SDLoc(N), MVT::i32);
}]>;

// Extract bits 16-31 (high-end) of an immediate value.
// Transformation function: shift the immediate value down into the low bits.
def HI16 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((uint64_t)N->getZExtValue() >> 16, SDLoc(N),
                                   MVT::i32);
}]>;

def NEG : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(-N->getSExtValue(), SDLoc(N), MVT::i32);
}]>;

def LO21 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((uint64_t)N->getZExtValue() & 0x1fffff,
                                   SDLoc(N), MVT::i32);
}]>;

// Branch targets
def BrTargetAsmOperand : AsmOperandClass {
  let Name = "BrTarget";
}
def BrTarget   : Operand<OtherVT> {
  let ParserMatchClass = BrTargetAsmOperand;
  let EncoderMethod = "getBranchTargetOpValue";
  let DecoderMethod = "decodeBranch";
}

def CallTargetAsmOperand : AsmOperandClass {
  let Name = "CallTarget";
}
def CallTarget : Operand<i32> {
  let ParserMatchClass = CallTargetAsmOperand;
  let EncoderMethod = "getBranchTargetOpValue";
  let DecoderMethod = "decodeBranch";
}

def ImmShiftAsmOperand : AsmOperandClass { let Name = "ImmShift"; }
def immShift : Operand<i32>, PatLeaf<(imm), [{
    int Imm = N->getSExtValue();
    return Imm >= -31 && Imm <= 31;}]> {
  let ParserMatchClass = ImmShiftAsmOperand;
  let DecoderMethod = "decodeShiftImm";
}

def Imm10AsmOperand : AsmOperandClass { let Name = "Imm10"; }
def imm10 : Operand<i32>, PatLeaf<(imm), [{
    return isInt<10>(N->getSExtValue()); }]> {
  let ParserMatchClass = Imm10AsmOperand;
}

def LoImm16AsmOperand : AsmOperandClass { let Name = "LoImm16"; }
def i32lo16z : Operand<i32>, PatLeaf<(i32 imm), [{
    // i32lo16 predicate - true if the 32-bit immediate has only rightmost 16
    // bits set.
    return ((N->getZExtValue() & 0xFFFFUL) == N->getZExtValue());}], LO16> {
  let ParserMatchClass = LoImm16AsmOperand;
}
def i32neg16 : Operand<i32>, PatLeaf<(i32 imm), [{
    // i32neg16 predicate - true if the 32-bit immediate is negative and can
    // be represented by a 16 bit integer.
    int Imm = N->getSExtValue();
    return (Imm < 0) && (isInt<16>(Imm));}], LO16> {
  let ParserMatchClass = LoImm16AsmOperand;
}
def i32lo16s : Operand<i32>, PatLeaf<(i32 imm), [{
    // i32lo16 predicate - true if the 32-bit immediate has only rightmost 16
    // bits set.
    return ((int64_t)(N->getSExtValue() & 0xFFFFUL) == N->getSExtValue());}], LO16> {
  let ParserMatchClass = LoImm16AsmOperand;
}

def LoImm16AndAsmOperand : AsmOperandClass { let Name = "LoImm16And"; }
def i32lo16and : Operand<i32>, PatLeaf<(i32 imm), [{
    // i32lo16 predicate - true if the 32-bit immediate has the rightmost 16
    // bits set and the leftmost 16 bits 1's.
    return (N->getZExtValue() >= 0xFFFF0000UL);}], LO16> {
  let ParserMatchClass = LoImm16AndAsmOperand;
  let PrintMethod = "printLo16AndImmOperand";
}

def HiImm16AsmOperand : AsmOperandClass { let Name = "HiImm16"; }
def i32hi16 : Operand<i32>, PatLeaf<(i32 imm), [{
    // i32hi16 predicate - true if the 32-bit immediate has only leftmost 16
    // bits set.
    return ((N->getZExtValue() & 0xFFFF0000UL) == N->getZExtValue());}], HI16> {
  let ParserMatchClass = HiImm16AsmOperand;
  let PrintMethod = "printHi16ImmOperand";
}

def HiImm16AndAsmOperand : AsmOperandClass { let Name = "HiImm16And"; }
def i32hi16and : Operand<i32>, PatLeaf<(i32 imm), [{
    // i32lo16 predicate - true if the 32-bit immediate has the leftmost 16
    // bits set and the rightmost 16 bits 1's.
    return ((N->getZExtValue() & 0xFFFFUL) == 0xFFFFUL);}], HI16> {
  let ParserMatchClass = HiImm16AndAsmOperand;
  let PrintMethod = "printHi16AndImmOperand";
}

def LoImm21AsmOperand : AsmOperandClass { let Name = "LoImm21"; }
def i32lo21 : Operand<i32>, PatLeaf<(i32 imm), [{
    // i32lo21 predicate - true if the 32-bit immediate has only rightmost 21
    // bits set.
    return ((N->getZExtValue() & 0x1FFFFFUL) == N->getZExtValue());}], LO21> {
  let ParserMatchClass = LoImm21AsmOperand;
}

def AluOp : Operand<i32> {
  let PrintMethod = "printAluOperand";
}

// Addressing modes.
def ADDRrr : ComplexPattern<i32, 3, "selectAddrRr", [], []>;
def ADDRri : ComplexPattern<i32, 3, "selectAddrRi", [frameindex], []>;
def ADDRsls : ComplexPattern<i32, 1, "selectAddrSls", [frameindex], []>;
def ADDRspls : ComplexPattern<i32, 3, "selectAddrSpls", [frameindex], []>;

// Address operands
def MemRegImmAsmOperand : AsmOperandClass {
  let Name = "MemRegImm";
  let ParserMethod  = "parseMemoryOperand";
}
def MEMri : Operand<i32> {
  let DecoderMethod = "decodeRiMemoryValue";
  let EncoderMethod = "getRiMemoryOpValue";
  let MIOperandInfo = (ops GPR:$base, i32lo16s:$offset, AluOp:$Opcode);
  let ParserMatchClass = MemRegImmAsmOperand;
  let PrintMethod   = "printMemRiOperand";
}

def MemRegRegAsmOperand : AsmOperandClass {
  let Name = "MemRegReg";
  let ParserMethod  = "parseMemoryOperand";
}
def MEMrr : Operand<i32> {
  let DecoderMethod = "decodeRrMemoryValue";
  let EncoderMethod = "getRrMemoryOpValue";
  let MIOperandInfo = (ops GPR:$Op1, GPR:$Op2, AluOp:$Opcode);
  let ParserMatchClass = MemRegRegAsmOperand;
  let PrintMethod   = "printMemRrOperand";
}

def MemImmAsmOperand : AsmOperandClass {
  let Name = "MemImm";
  let ParserMethod  = "parseMemoryOperand";
}
def MEMi : Operand<i32> {
  let MIOperandInfo = (ops i32lo21:$offset);
  let ParserMatchClass = MemImmAsmOperand;
  let PrintMethod   = "printMemImmOperand";
}

def MemSplsAsmOperand : AsmOperandClass {
  let Name = "MemSpls";
  let ParserMethod  = "parseMemoryOperand";
}
def MEMspls : Operand<i32> {
  let DecoderMethod = "decodeSplsValue";
  let EncoderMethod = "getSplsOpValue";
  let MIOperandInfo = (ops GPR:$base, imm10:$offset, AluOp:$Opcode);
  let ParserMatchClass = MemSplsAsmOperand;
  let PrintMethod   = "printMemSplsOperand";
}

def CCOp : Operand<i32> {
  let PrintMethod = "printCCOperand";
}

// Predicate operand. Default to 0 = true.
def CondCodeOperand : AsmOperandClass { let Name = "CondCode"; }

def pred : PredicateOperand<i32, (ops i32imm), (ops (i32 0))> {
  let PrintMethod = "printPredicateOperand";
  let ParserMatchClass = CondCodeOperand;
  let DecoderMethod = "decodePredicateOperand";
}

let hasSideEffects = 0, Inst = 0x00000001 in
  def NOP : InstLanai<(outs), (ins), "nop", []>;

// Special NOPs to change logging level in vlanai.
let hasSideEffects = 0, Inst = 0x00000002 in
  def LOG0 : InstLanai<(outs), (ins), "log_0", []>;
let hasSideEffects = 0, Inst = 0x00000003 in
  def LOG1 : InstLanai<(outs), (ins), "log_1", []>;
let hasSideEffects = 0, Inst = 0x00000004 in
  def LOG2 : InstLanai<(outs), (ins), "log_2", []>;
let hasSideEffects = 0, Inst = 0x00000005 in
  def LOG3 : InstLanai<(outs), (ins), "log_3", []>;
let hasSideEffects = 0, Inst = 0x00000006 in
  def LOG4 : InstLanai<(outs), (ins), "log_4", []>;

// Map an SPLS instruction onto itself. All other instructions will be mapped
// onto -1. Used to identify SPLS instructions.
def splsIdempotent : InstrMapping {
  let FilterClass = "InstSPLS";
  let RowFields = ["AsmString"];
  let ColFields = ["PostEncoderMethod"];
  let KeyCol = ["adjustPqBitsSpls"];
  let ValueCols = [["adjustPqBitsSpls"]];
}

// -------------------------------------------------- //
// ALU instructions
// -------------------------------------------------- //
multiclass ALUbase<bits<3> subOp, string AsmStr, SDNode OpNode,
                   PatLeaf LoExt, PatLeaf HiExt,
                   list<dag> loPattern, list<dag> hiPattern> {
  // Register Immediate
  let H = 0 in
    def LO : InstRI<subOp, (outs GPR:$Rd), (ins GPR:$Rs1, LoExt:$imm16),
                    !strconcat(AsmStr, "\t$Rs1, $imm16, $Rd"),
                    loPattern>;
  let H = 1 in
    def HI : InstRI<subOp, (outs GPR:$Rd), (ins GPR:$Rs1, HiExt:$imm16),
                    !strconcat(AsmStr, "\t$Rs1, $imm16, $Rd"),
                    hiPattern>;

}

multiclass ALUarith<bits<3> subOp, string AsmStr, SDNode OpNode,
                    PatLeaf LoExt, PatLeaf HiExt> {
  defm I_ : ALUbase<subOp, AsmStr, OpNode, LoExt, HiExt, [], []>;

  // Register Register
  let JJJJJ = 0 in
    def R : InstRR<subOp, (outs GPR:$Rd), (ins GPR:$Rs1, GPR:$Rs2, pred:$DDDI),
                   !strconcat(AsmStr, "$DDDI\t$Rs1, $Rs2, $Rd"),
                   [(set GPR:$Rd, (OpNode GPR:$Rs1, GPR:$Rs2))]>;
}

multiclass ALUlogic<bits<3> subOp, string AsmStr, SDNode OpNode,
                    PatLeaf LoExt, PatLeaf HiExt> {
  defm I_ : ALUbase<subOp, AsmStr, OpNode, LoExt, HiExt,
                    [(set GPR:$Rd, (OpNode GPR:$Rs1, LoExt:$imm16))],
                    [(set GPR:$Rd, (OpNode GPR:$Rs1, HiExt:$imm16))]>;

  // Register Register
  let JJJJJ = 0 in
    def R : InstRR<subOp, (outs GPR:$Rd), (ins GPR:$Rs1, GPR:$Rs2, pred:$DDDI),
                   !strconcat(AsmStr, "$DDDI\t$Rs1, $Rs2, $Rd"),
                   [(set GPR:$Rd, (OpNode GPR:$Rs1, GPR:$Rs2))]>;
}

// Non flag setting ALU operations
let isAsCheapAsAMove = 1, F = 0 in {
  let isCommutable = 1 in {
    defm ADD_ : ALUarith<0b000, "add", add, i32lo16z, i32hi16>;
  }
  defm SUB_ : ALUarith<0b010,   "sub", sub, i32lo16z, i32hi16>;
  let isCommutable = 1 in {
    defm AND_ : ALUlogic<0b100, "and", and, i32lo16and, i32hi16and>;
    defm OR_  : ALUlogic<0b101,  "or",  or, i32lo16z, i32hi16>;
    defm XOR_ : ALUlogic<0b110, "xor", xor, i32lo16z, i32hi16>;
  }
}

def : Pat<(add GPR:$Rs1, i32lo16z:$imm),
          (ADD_I_LO GPR:$Rs1, i32lo16z:$imm)>;

def : Pat<(sub GPR:$Rs1, i32lo16z:$imm),
          (SUB_I_LO GPR:$Rs1, i32lo16z:$imm)>;

def : Pat<(add GPR:$Rs1, i32hi16:$imm),
          (ADD_I_HI GPR:$Rs1, i32hi16:$imm)>;

def : Pat<(sub GPR:$Rs1, i32hi16:$imm),
          (SUB_I_HI GPR:$Rs1, i32hi16:$imm)>;

def : Pat<(i32 i32lo16and:$imm), (AND_I_LO (i32 R1), i32lo16and:$imm)>;
def : Pat<(i32 i32hi16and:$imm), (AND_I_HI (i32 R1), i32hi16and:$imm)>;

// Change add/sub with negative number to sub/add
def : Pat<(add GPR:$Rs1, i32neg16:$imm),
          (SUB_I_LO GPR:$Rs1, (NEG $imm))>;
def : Pat<(sub GPR:$Rs1, i32neg16:$imm),
          (ADD_I_LO GPR:$Rs1, (NEG $imm))>;

// Flag (incl. carry) setting addition and subtraction
let F = 1, Defs = [SR] in {
  defm ADD_F_ : ALUarith<0b000, "add.f", addc, i32lo16z, i32hi16>;
  defm SUB_F_ : ALUarith<0b010, "sub.f", subc, i32lo16z, i32hi16>;
}

def : Pat<(addc GPR:$Rs1, i32lo16z:$imm),
          (ADD_F_I_LO GPR:$Rs1, i32lo16z:$imm)>;

def : Pat<(subc GPR:$Rs1, i32lo16z:$imm),
          (SUB_F_I_LO GPR:$Rs1, i32lo16z:$imm)>;

def : Pat<(addc GPR:$Rs1, i32hi16:$imm),
          (ADD_F_I_HI GPR:$Rs1, i32hi16:$imm)>;

def : Pat<(subc GPR:$Rs1, i32hi16:$imm),
          (SUB_F_I_HI GPR:$Rs1, i32hi16:$imm)>;

// Carry using addition and subtraction
let F = 0, Uses = [SR] in {
  defm ADDC_ : ALUarith<0b001, "addc", adde, i32lo16z, i32hi16>;
  defm SUBB_ : ALUarith<0b011, "subb", sube, i32lo16z, i32hi16>;
}

def : Pat<(adde GPR:$Rs1, i32lo16z:$imm),
          (ADDC_I_LO GPR:$Rs1, i32lo16z:$imm)>;

def : Pat<(sube GPR:$Rs1, i32lo16z:$imm),
          (SUBB_I_LO GPR:$Rs1, i32lo16z:$imm)>;

def : Pat<(adde GPR:$Rs1, i32hi16:$imm),
          (ADDC_I_HI GPR:$Rs1, i32hi16:$imm)>;

def : Pat<(sube GPR:$Rs1, i32hi16:$imm),
          (SUBB_I_HI GPR:$Rs1, i32hi16:$imm)>;

// Flag setting ALU operations
let isAsCheapAsAMove = 1, F = 1, Defs = [SR] in {
  let isCommutable = 1 in {
    defm AND_F_ : ALUlogic<0b100, "and.f",  and, i32lo16and, i32hi16and>;
    defm OR_F_  : ALUlogic<0b101,  "or.f",   or, i32lo16z, i32hi16>;
    defm XOR_F_ : ALUlogic<0b110, "xor.f",  xor, i32lo16z, i32hi16>;
  }
}

let isAsCheapAsAMove = 1, F = 1, Defs = [SR], Uses = [SR] in {
  defm ADDC_F_ : ALUarith<0b001, "addc.f", adde, i32lo16z, i32hi16>;
  defm SUBB_F_ : ALUarith<0b011, "subb.f", sube, i32lo16z, i32hi16>;
}

def : Pat<(LanaiSubbF GPR:$Rs1, GPR:$Rs2),
          (SUBB_F_R GPR:$Rs1, GPR:$Rs2)>;

def : Pat<(LanaiSubbF GPR:$Rs1, i32lo16z:$imm),
          (SUBB_F_I_LO GPR:$Rs1, i32lo16z:$imm)>;

def : Pat<(LanaiSubbF GPR:$Rs1, i32hi16:$imm),
          (SUBB_F_I_HI GPR:$Rs1, i32hi16:$imm)>;

def : InstAlias<"mov $src, $dst", (ADD_R GPR:$dst, GPR:$src, R0, 0)>;

let isAsCheapAsAMove = 1, Rs1 = R0.Num, isCodeGenOnly = 1, H = 1, F = 0,
  isReMaterializable = 1 in
  def MOVHI : InstRI<0b000, (outs GPR:$Rd), (ins i32hi16:$imm16),
                     "mov\t$imm16, $Rd",
                     [(set GPR:$Rd, i32hi16:$imm16)]>;

def : InstAlias<"mov $imm16, $dst", (ADD_I_LO GPR:$dst, R0, i32lo16z:$imm16)>;
def : InstAlias<"mov $imm16, $dst", (ADD_I_HI GPR:$dst, R0, i32hi16:$imm16)>;
def : InstAlias<"mov $imm16, $dst",
                (AND_I_LO GPR:$dst, R1, i32lo16and:$imm16)>;
def : InstAlias<"mov $imm16, $dst",
                (AND_I_HI GPR:$dst, R1, i32hi16and:$imm16)>;

// Shift instructions
class ShiftRI<string AsmStr, list<dag> Pattern>
  : InstRI<0b111, (outs GPR:$Rd), (ins GPR:$Rs1, immShift:$imm16),
           !strconcat(AsmStr, "\t$Rs1, $imm16, $Rd"), Pattern> {
  let isReMaterializable = 1;
}

let F = 0 in {
  let H = 0 in
    def SL_I : ShiftRI<"sh", [(set GPR:$Rd, (shl GPR:$Rs1, immShift:$imm16))]>;
  let H = 1 in
    def SA_I : ShiftRI<"sha", []>;
}
def : Pat<(srl GPR:$Rs1, immShift:$imm), (SL_I GPR:$Rs1, (NEG $imm))>;
def : Pat<(sra GPR:$Rs1, immShift:$imm), (SA_I GPR:$Rs1, (NEG $imm))>;

let F = 1, Defs = [SR] in {
  let H = 0 in
    def SL_F_I : ShiftRI<"sh.f", []>;
  let H = 1 in
    def SA_F_I : ShiftRI<"sha.f", []>;
}

class ShiftRR<string AsmStr, list<dag> Pattern>
  : InstRR<0b111, (outs GPR:$Rd), (ins GPR:$Rs1, GPR:$Rs2, pred:$DDDI), AsmStr,
           Pattern>;

let F = 0 in {
  let JJJJJ = 0b10000 in
    def SHL_R : ShiftRR<"sh$DDDI\t$Rs1, $Rs2, $Rd",
                        [(set GPR:$Rd, (shl GPR:$Rs1, GPR:$Rs2))]>;
  let isCodeGenOnly = 1 in {
    let JJJJJ = 0b10000 in
      def SRL_R : ShiftRR<"sh$DDDI\t$Rs1, $Rs2, $Rd", []>;
  }
  let JJJJJ = 0b11000 in
    def SRA_R : ShiftRR<"sha$DDDI\t$Rs1, $Rs2, $Rd", []>;
}

let F = 1, Defs = [SR] in {
  let JJJJJ = 0b10000 in
    def SHL_F_R : ShiftRR<"sh.f$DDDI\t$Rs1, $Rs2, $Rd", []>;
  let isCodeGenOnly = 1 in {
    let JJJJJ = 0b10000 in
      def SRL_F_R : ShiftRR<"sh.f$DDDI\t$Rs1, $Rs2, $Rd", []>;
  }
  let JJJJJ = 0b11000 in
    def SRA_F_R : ShiftRR<"sha.f$DDDI\t$Rs1, $Rs2, $Rd", []>;
}

// Expand shift-right operations
def : Pat<(srl GPR:$Rs1, GPR:$Rs2),
          (SRL_R GPR:$Rs1, (SUB_R R0, GPR:$Rs2))>;
def : Pat<(sra GPR:$Rs1, GPR:$Rs2),
          (SRA_R GPR:$Rs1, (SUB_R R0, GPR:$Rs2))>;

// -------------------------------------------------- //
// LOAD instructions
// -------------------------------------------------- //

class LoadRR<string OpcString, PatFrag OpNode, ValueType Ty>
  : InstRRM<0b0, (outs GPR:$Rd), (ins MEMrr:$src),
            !strconcat(OpcString, "\t$src, $Rd"),
            [(set (Ty GPR:$Rd), (OpNode ADDRrr:$src))]>,
    Sched<[WriteLD]> {
  bits<20> src;

  let Rs1 = src{19-15};
  let Rs2 = src{14-10};
  let P = src{9};
  let Q = src{8};
  let BBB = src{7-5};
  let JJJJJ = src{4-0};
  let mayLoad = 1;
}

class LoadRI<string OpcString, PatFrag OpNode, ValueType Ty>
  : InstRM<0b0, (outs GPR:$Rd), (ins MEMri:$src),
           !strconcat(OpcString, "\t$src, $Rd"),
           [(set (Ty GPR:$Rd), (OpNode ADDRri:$src))]>,
    Sched<[WriteLD]> {
  bits<23> src;

  let Itinerary = IIC_LD;
  let Rs1 = src{22-18};
  let P = src{17};
  let Q = src{16};
  let imm16 = src{15-0};
  let isReMaterializable = 1;
  let mayLoad = 1;
}

let E = 0 in {
  let YL = 0b01 in {
    // uld is used here and ld in the alias as the alias is printed out first if
    // an alias exist
    def LDW_RI : LoadRI<"uld", load, i32>;
    def LDW_RR : LoadRR<"ld", load, i32>;
  }
}

def : InstAlias<"ld $src, $dst", (LDW_RI GPR:$dst, MEMri:$src)>;

let E = 1 in {
  let YL = 0b01 in {
    def LDWz_RR : LoadRR<"uld", zextloadi32, i32>;
  }
}

let E = 1 in {
  let YL = 0b00 in
    def LDHz_RR : LoadRR<"uld.h", zextloadi16, i32>;
  let YL = 0b10 in
    def LDBz_RR : LoadRR<"uld.b", zextloadi8, i32>;
}

let E = 0 in {
  let YL = 0b00 in
    def LDHs_RR : LoadRR<"ld.h", sextloadi16, i32>;
  let YL = 0b10 in
    def LDBs_RR : LoadRR<"ld.b", sextloadi8, i32>;
}

def LDADDR : InstSLS<0x0, (outs GPR:$Rd), (ins MEMi:$src),
                     "ld\t$src, $Rd",
                     [(set (i32 GPR:$Rd), (load ADDRsls:$src))]>,
    Sched<[WriteLD]> {
  bits<21> src;

  let Itinerary = IIC_LD;
  let msb = src{20-16};
  let lsb = src{15-0};
  let isReMaterializable = 1;
  let mayLoad = 1;
}

class LoadSPLS<string asmstring, PatFrag opNode>
  : InstSPLS<(outs GPR:$Rd), (ins MEMspls:$src),
             !strconcat(asmstring, "\t$src, $Rd"),
             [(set (i32 GPR:$Rd), (opNode ADDRspls:$src))]>,
    Sched<[WriteLDSW]> {
  bits<17> src;
  let Itinerary = IIC_LDSW;
  let Rs1 = src{16-12};
  let P = src{11};
  let Q = src{10};
  let imm10 = src{9-0};
  let mayLoad = 1;
  let isReMaterializable = 1;
}

let Y = 0, S = 0, E = 1 in
  def LDHz_RI : LoadSPLS<"uld.h", zextloadi16>;

let Y = 0, S = 0, E = 0 in
  def LDHs_RI : LoadSPLS<"ld.h", sextloadi16>;

let Y = 1, S = 0, E = 1 in
  def LDBz_RI : LoadSPLS<"uld.b", zextloadi8>;

let Y = 1, S = 0, E = 0 in
  def LDBs_RI : LoadSPLS<"ld.b", sextloadi8>;

def SLI : InstSLI<(outs GPR:$Rd), (ins i32lo21:$imm),
                  "mov\t$imm, $Rd",
                  [(set GPR:$Rd, i32lo21:$imm)]> {
  bits<21> imm;

  let msb = imm{20-16};
  let lsb = imm{15-0};
  let isReMaterializable = 1;
  let isAsCheapAsAMove = 1;
}

// -------------------------------------------------- //
// STORE instructions
// -------------------------------------------------- //

class StoreRR<string OpcString, PatFrag OpNode, ValueType Ty>
  : InstRRM<0b1, (outs), (ins GPR:$Rd, MEMrr:$dst),
            !strconcat(OpcString, "\t$Rd, $dst"),
            [(OpNode (Ty GPR:$Rd), ADDRrr:$dst)]>,
    Sched<[WriteST]> {
  bits<20> dst;

  let Itinerary = IIC_ST;
  let Rs1 = dst{19-15};
  let Rs2 = dst{14-10};
  let P = dst{9};
  let Q = dst{8};
  let BBB = dst{7-5};
  let JJJJJ = dst{4-0};
  let mayStore = 1;
}

class StoreRI<string OpcString, PatFrag OpNode, ValueType Ty>
  : InstRM<0b1, (outs), (ins GPR:$Rd, MEMri:$dst),
           !strconcat(OpcString, "\t$Rd, $dst"),
           [(OpNode (Ty GPR:$Rd), ADDRri:$dst)]>,
    Sched<[WriteST]> {
  bits<23> dst;

  let Itinerary = IIC_ST;
  let Rs1 = dst{22-18};
  let P = dst{17};
  let Q = dst{16};
  let imm16 = dst{15-0};
  let mayStore = 1;
}

let YL = 0b01, E = 0 in {
  def SW_RR : StoreRR<"st", store, i32>;
  def SW_RI : StoreRI<"st", store, i32>;
}

let E = 0 in {
  let YL = 0b00 in
    def STH_RR : StoreRR<"st.h", truncstorei16, i32>;
  let YL = 0b10 in
    def STB_RR : StoreRR<"st.b", truncstorei8, i32>;
}

def STADDR : InstSLS<0x1, (outs), (ins GPR:$Rd, MEMi:$dst),
                     "st\t$Rd, $dst",
                     [(store (i32 GPR:$Rd), ADDRsls:$dst)]>,
    Sched<[WriteST]> {
  bits<21> dst;

  let Itinerary = IIC_ST;
  let msb = dst{20-16};
  let lsb = dst{15-0};
  let mayStore = 1;
}

class StoreSPLS<string asmstring, PatFrag opNode>
  : InstSPLS<(outs), (ins GPR:$Rd, MEMspls:$dst),
             !strconcat(asmstring, "\t$Rd, $dst"),
             [(opNode (i32 GPR:$Rd), ADDRspls:$dst)]>,
    Sched<[WriteSTSW]> {
  bits<17> dst;

  let Itinerary = IIC_STSW;
  let Rs1 = dst{16-12};
  let P = dst{11};
  let Q = dst{10};
  let imm10 = dst{9-0};
  let mayStore = 1;
}

let Y = 0, S = 1, E = 0 in
  def STH_RI : StoreSPLS<"st.h", truncstorei16>;

let Y = 1, S = 1, E = 0 in
  def STB_RI : StoreSPLS<"st.b", truncstorei8>;

// -------------------------------------------------- //
// BRANCH instructions
// -------------------------------------------------- //

let isBranch = 1, isBarrier = 1, isTerminator = 1, hasDelaySlot = 1 in {
  def BT : InstBR<(outs), (ins BrTarget:$addr),
                  "bt\t$addr",
                  [(br bb:$addr)]> {
    let DDDI = 0b0000;
  }
  let Uses = [SR] in
    def BRCC : InstBR<(outs), (ins BrTarget:$addr, CCOp:$DDDI),
                      "b$DDDI\t$addr",
                      [(LanaiBrCC bb:$addr, imm:$DDDI)]>;

  let isIndirectBranch = 1 in {
    def JR : InstRR<0b101, (outs), (ins GPR:$Rs2), "bt\t$Rs2",
                    [(brind GPR:$Rs2)]> {
      let Rs1 = R0.Num;
      let Rd = R2.Num;
      let F = 0;
      let JJJJJ = 0;
      let DDDI = 0;
    }
  }
}

// -------------------------------------------------- //
// Condition/SF instructions
// -------------------------------------------------- //

// Instructions to set flags used in lowering comparisons.
multiclass SF<bits<3> op2Val, string AsmStr> {
  let F = 1, Rd = R0.Num, JJJJJ = 0, Defs = [SR], DDDI = 0 in
    def _RR : InstRR<op2Val, (outs), (ins GPR:$Rs1, GPR:$Rs2),
                     !strconcat(AsmStr, "\t$Rs1, $Rs2, %r0"),
                     [(LanaiSetFlag (i32 GPR:$Rs1), (i32 GPR:$Rs2))]>;
  let F = 1, Rd = R0.Num, H = 0, Defs = [SR] in
    def _RI_LO : InstRI<op2Val, (outs), (ins GPR:$Rs1, i32lo16z:$imm16),
                     !strconcat(AsmStr, "\t$Rs1, $imm16, %r0"),
                     [(LanaiSetFlag (i32 GPR:$Rs1), i32lo16z:$imm16)]>;
  let F = 1, Rd = R0.Num, H = 1, Defs = [SR] in
    def _RI_HI : InstRI<op2Val, (outs), (ins GPR:$Rs1, i32hi16:$imm16),
                     !strconcat(AsmStr, "\t$Rs1, $imm16, %r0"),
                     [(LanaiSetFlag (i32 GPR:$Rs1), i32hi16:$imm16)]>;
}
let isCodeGenOnly = 1, isCompare = 1 in {
  defm SFSUB_F : SF<0b010, "sub.f">;
}

// Jump and link
let isCall = 1, hasDelaySlot = 1, isCodeGenOnly = 1, Uses = [SP],
    Defs = [RCA] in {
  def CALL : Pseudo<(outs), (ins CallTarget:$addr), "", []>;
  def CALLR : Pseudo<(outs), (ins GPR:$Rs1), "", [(Call GPR:$Rs1)]>;
}

let isReturn = 1, isTerminator = 1, hasDelaySlot = 1, isBarrier = 1,
    Uses = [RCA] in {
  def RET : InstRM<0b0, (outs), (ins),
                   "ld\t-4[%fp], %pc ! return",
                   [(RetFlag)]> {
    let Rd = PC.Num;
    let Rs1 = FP.Num;
    let P = 1;
    let Q = 0;
    let imm16 = -4;

    // Post encoding is not needed for RET.
    let PostEncoderMethod = "";
  }
}

// ADJCALLSTACKDOWN/UP implicitly use/def SP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber SP.
let Defs = [SP], Uses = [SP] in {
  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                "#ADJCALLSTACKDOWN $amt1 $amt2",
                                [(CallSeqStart timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP   : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                "#ADJCALLSTACKUP $amt1 $amt2",
                                [(CallSeqEnd timm:$amt1, timm:$amt2)]>;
}

let Defs = [SP], Uses = [SP] in {
  def ADJDYNALLOC : Pseudo<(outs GPR:$dst), (ins GPR:$src),
                           "#ADJDYNALLOC $dst $src",
                           [(set GPR:$dst, (LanaiAdjDynAlloc GPR:$src))]>;
}

let Uses = [SR] in {
  def SCC : InstSCC<(outs GPR:$Rs1), (ins CCOp:$DDDI),
                    "s$DDDI\t$Rs1",
                    [(set (i32 GPR:$Rs1), (LanaiSetCC imm:$DDDI))]>;
}

// Select with hardware support
let Uses = [SR], isSelect = 1 in {
  def SELECT : InstRR<0b111, (outs GPR:$Rd),
                      (ins GPR:$Rs1, GPR:$Rs2, CCOp:$DDDI),
                      "sel.$DDDI $Rs1, $Rs2, $Rd",
                      [(set (i32 GPR:$Rd),
                       (LanaiSelectCC (i32 GPR:$Rs1), (i32 GPR:$Rs2),
                                      (imm:$DDDI)))]> {
    let JJJJJ = 0;
    let F = 0;
  }
}

let isBranch = 1, isBarrier = 1, isTerminator = 1, hasDelaySlot = 1,
    isIndirectBranch = 1, Uses = [SR] in {
  def BRIND_CC : InstRR<0b101, (outs), (ins GPR:$Rs1, CCOp:$DDDI),
                        "b$DDDI\t$Rs1", []> {
    let F = 0;
    let JJJJJ = 0;
    let Rd = PC.Num;
    let Rs2 = R0.Num;
  }

  def BRIND_CCA : InstRR<0b101, (outs), (ins GPR:$Rs1, GPR:$Rs2, CCOp:$DDDI),
                         "b${DDDI}\t$Rs1 add $Rs2", []> {
    let F = 0;
    let Rd = PC.Num;
    let JJJJJ = 0;
  }
}

// TODO: This only considers the case where BROFF is an immediate and not where
// it is a register. Add support for register relative branching.
let isBranch = 1, isBarrier = 1, isTerminator = 1, hasDelaySlot = 1, Rs1 = 0,
    Uses = [SR] in
  def BRR : InstBRR<(outs), (ins i16imm:$imm16, CCOp:$DDDI),
                    "b${DDDI}.r\t$imm16", []>;

let F = 0 in {
// Population Count (POPC)
def POPC: InstSpecial<0b001, (outs GPR:$Rd), (ins GPR:$Rs1),
                      "popc\t$Rs1, $Rd",
                      [(set GPR:$Rd, (ctpop GPR:$Rs1))]>;

// Count Leading Zeros (LEADZ)
def LEADZ: InstSpecial<0b010, (outs GPR:$Rd), (ins GPR:$Rs1),
                       "leadz\t$Rs1, $Rd", [(set GPR:$Rd, (ctlz GPR:$Rs1))]>;

// Count Trailing Zeros (TRAILZ)
def TRAILZ : InstSpecial<0b011, (outs GPR:$Rd), (ins GPR:$Rs1),
                         "trailz\t$Rs1, $Rd",
                         [(set GPR:$Rd, (cttz GPR:$Rs1))]>;
}

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//===----------------------------------------------------------------------===//

// unsigned 16-bit immediate
def : Pat<(i32 i32lo16z:$imm), (OR_I_LO (i32 R0), imm:$imm)>;

// arbitrary immediate
def : Pat<(i32 imm:$imm), (OR_I_LO (MOVHI (HI16 imm:$imm)), (LO16 imm:$imm))>;

// Calls
def : Pat<(Call tglobaladdr:$dst), (CALL tglobaladdr:$dst)>;
def : Pat<(Call texternalsym:$dst), (CALL texternalsym:$dst)>;

// Loads
def : Pat<(extloadi8  ADDRspls:$src), (i32 (LDBz_RI ADDRspls:$src))>;
def : Pat<(extloadi16 ADDRspls:$src), (i32 (LDHz_RI ADDRspls:$src))>;
// Loads up to 32-bits are already atomic.
// TODO: This is a workaround for a particular failing case and should be
// handled more generally.
def : Pat<(atomic_load_8  ADDRspls:$src), (i32 (LDBz_RI ADDRspls:$src))>;

// GlobalAddress, ExternalSymbol, Jumptable, ConstantPool
def : Pat<(LanaiHi tglobaladdr:$dst), (MOVHI tglobaladdr:$dst)>;
def : Pat<(LanaiLo tglobaladdr:$dst), (OR_I_LO (i32 R0), tglobaladdr:$dst)>;
def : Pat<(LanaiSmall tglobaladdr:$dst), (SLI tglobaladdr:$dst)>;
def : Pat<(LanaiHi texternalsym:$dst), (MOVHI texternalsym:$dst)>;
def : Pat<(LanaiLo texternalsym:$dst), (OR_I_LO (i32 R0), texternalsym:$dst)>;
def : Pat<(LanaiSmall texternalsym:$dst), (SLI texternalsym:$dst)>;
def : Pat<(LanaiHi tblockaddress:$dst), (MOVHI tblockaddress:$dst)>;
def : Pat<(LanaiLo tblockaddress:$dst), (OR_I_LO (i32 R0), tblockaddress:$dst)>;
def : Pat<(LanaiSmall tblockaddress:$dst), (SLI tblockaddress:$dst)>;
def : Pat<(LanaiHi tjumptable:$dst), (MOVHI tjumptable:$dst)>;
def : Pat<(LanaiLo tjumptable:$dst), (OR_I_LO (i32 R0), tjumptable:$dst)>;
def : Pat<(LanaiSmall tjumptable:$dst), (SLI tjumptable:$dst)>;
def : Pat<(LanaiHi tconstpool:$dst), (MOVHI tconstpool:$dst)>;
def : Pat<(LanaiLo tconstpool:$dst), (OR_I_LO (i32 R0), tconstpool:$dst)>;
def : Pat<(LanaiSmall tconstpool:$dst), (SLI tconstpool:$dst)>;

def : Pat<(or GPR:$hi, (LanaiLo tglobaladdr:$lo)),
          (OR_I_LO GPR:$hi, tglobaladdr:$lo)>;
def : Pat<(or R0, (LanaiSmall tglobaladdr:$small)),
          (SLI tglobaladdr:$small)>;
def : Pat<(or GPR:$hi, (LanaiLo texternalsym:$lo)),
          (OR_I_LO GPR:$hi, texternalsym:$lo)>;
def : Pat<(or R0, (LanaiSmall texternalsym:$small)),
          (SLI texternalsym:$small)>;
def : Pat<(or GPR:$hi, (LanaiLo tblockaddress:$lo)),
          (OR_I_LO GPR:$hi, tblockaddress:$lo)>;
def : Pat<(or R0, (LanaiSmall tblockaddress:$small)),
          (SLI tblockaddress:$small)>;
def : Pat<(or GPR:$hi, (LanaiLo tjumptable:$lo)),
          (OR_I_LO GPR:$hi, tjumptable:$lo)>;
def : Pat<(or R0, (LanaiSmall tjumptable:$small)),
          (SLI tjumptable:$small)>;
def : Pat<(or GPR:$hi, (LanaiLo tconstpool:$lo)),
          (OR_I_LO GPR:$hi, tconstpool:$lo)>;
def : Pat<(or R0, (LanaiSmall tconstpool:$small)),
          (SLI tconstpool:$small)>;
