//===-- RISCVInstrInfo.td - Target Description for RISCV ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "RISCVInstrFormats.td"

//===----------------------------------------------------------------------===//
// RISC-V specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDT_RISCVCall         : SDTypeProfile<0, -1, [SDTCisVT<0, XLenVT>]>;
def SDT_RISCVCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>,
                                            SDTCisVT<1, i32>]>;
def SDT_RISCVCallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>,
                                          SDTCisVT<1, i32>]>;
def SDT_RISCVSelectCC     : SDTypeProfile<1, 5, [SDTCisSameAs<1, 2>,
                                                 SDTCisSameAs<0, 4>,
                                                 SDTCisSameAs<4, 5>]>;


def Call         : SDNode<"RISCVISD::CALL", SDT_RISCVCall,
                          [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                           SDNPVariadic]>;
def CallSeqStart : SDNode<"ISD::CALLSEQ_START", SDT_RISCVCallSeqStart,
                          [SDNPHasChain, SDNPOutGlue]>;
def CallSeqEnd   : SDNode<"ISD::CALLSEQ_END", SDT_RISCVCallSeqEnd,
                          [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def RetFlag      : SDNode<"RISCVISD::RET_FLAG", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def URetFlag     : SDNode<"RISCVISD::URET_FLAG", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue]>;
def SRetFlag     : SDNode<"RISCVISD::SRET_FLAG", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue]>;
def MRetFlag     : SDNode<"RISCVISD::MRET_FLAG", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue]>;
def SelectCC     : SDNode<"RISCVISD::SELECT_CC", SDT_RISCVSelectCC,
                          [SDNPInGlue]>;
def Tail         : SDNode<"RISCVISD::TAIL", SDT_RISCVCall,
                          [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                           SDNPVariadic]>;

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

class ImmXLenAsmOperand<string prefix, string suffix = ""> : AsmOperandClass {
  let Name = prefix # "ImmXLen" # suffix;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", Name);
}

class ImmAsmOperand<string prefix, int width, string suffix> : AsmOperandClass {
  let Name = prefix # "Imm" # width # suffix;
  let RenderMethod = "addImmOperands";
  let DiagnosticType = !strconcat("Invalid", Name);
}

class SImmAsmOperand<int width, string suffix = "">
    : ImmAsmOperand<"S", width, suffix> {
}

class UImmAsmOperand<int width, string suffix = "">
    : ImmAsmOperand<"U", width, suffix> {
}

def FenceArg : AsmOperandClass {
  let Name = "FenceArg";
  let RenderMethod = "addFenceArgOperands";
  let DiagnosticType = "InvalidFenceArg";
}

def fencearg : Operand<XLenVT> {
  let ParserMatchClass = FenceArg;
  let PrintMethod = "printFenceArg";
  let DecoderMethod = "decodeUImmOperand<4>";
}

def UImmLog2XLenAsmOperand : AsmOperandClass {
  let Name = "UImmLog2XLen";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidUImmLog2XLen";
}

def uimmlog2xlen : Operand<XLenVT>, ImmLeaf<XLenVT, [{
  if (Subtarget->is64Bit())
    return isUInt<6>(Imm);
  return isUInt<5>(Imm);
}]> {
  let ParserMatchClass = UImmLog2XLenAsmOperand;
  // TODO: should ensure invalid shamt is rejected when decoding.
  let DecoderMethod = "decodeUImmOperand<6>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (!MCOp.evaluateAsConstantImm(Imm))
      return false;
    if (STI.getTargetTriple().isArch64Bit())
      return  isUInt<6>(Imm);
    return isUInt<5>(Imm);
  }];
}

def uimm5 : Operand<XLenVT>, ImmLeaf<XLenVT, [{return isUInt<5>(Imm);}]> {
  let ParserMatchClass = UImmAsmOperand<5>;
  let DecoderMethod = "decodeUImmOperand<5>";
}

def simm12 : Operand<XLenVT>, ImmLeaf<XLenVT, [{return isInt<12>(Imm);}]> {
  let ParserMatchClass = SImmAsmOperand<12>;
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeSImmOperand<12>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isInt<12>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

// A 13-bit signed immediate where the least significant bit is zero.
def simm13_lsb0 : Operand<OtherVT> {
  let ParserMatchClass = SImmAsmOperand<13, "Lsb0">;
  let EncoderMethod = "getImmOpValueAsr1";
  let DecoderMethod = "decodeSImmOperandAndLsl1<13>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isShiftedInt<12, 1>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

class UImm20Operand : Operand<XLenVT> {
  let EncoderMethod = "getImmOpValue";
  let DecoderMethod = "decodeUImmOperand<20>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isUInt<20>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

def uimm20_lui : UImm20Operand {
  let ParserMatchClass = UImmAsmOperand<20, "LUI">;
}
def uimm20_auipc : UImm20Operand {
  let ParserMatchClass = UImmAsmOperand<20, "AUIPC">;
}

def Simm21Lsb0JALAsmOperand : SImmAsmOperand<21, "Lsb0JAL"> {
  let ParserMethod = "parseJALOffset";
}

// A 21-bit signed immediate where the least significant bit is zero.
def simm21_lsb0_jal : Operand<OtherVT> {
  let ParserMatchClass = Simm21Lsb0JALAsmOperand;
  let EncoderMethod = "getImmOpValueAsr1";
  let DecoderMethod = "decodeSImmOperandAndLsl1<21>";
  let MCOperandPredicate = [{
    int64_t Imm;
    if (MCOp.evaluateAsConstantImm(Imm))
      return isShiftedInt<20, 1>(Imm);
    return MCOp.isBareSymbolRef();
  }];
}

def BareSymbol : AsmOperandClass {
  let Name = "BareSymbol";
  let RenderMethod = "addImmOperands";
  let DiagnosticType = "InvalidBareSymbol";
  let ParserMethod = "parseBareSymbol";
}

// A bare symbol.
def bare_symbol : Operand<XLenVT> {
  let ParserMatchClass = BareSymbol;
}

def CSRSystemRegister : AsmOperandClass {
  let Name = "CSRSystemRegister";
  let ParserMethod = "parseCSRSystemRegister";
  let DiagnosticType = "InvalidCSRSystemRegister";
}

def csr_sysreg : Operand<XLenVT> {
  let ParserMatchClass = CSRSystemRegister;
  let PrintMethod = "printCSRSystemRegister";
  let DecoderMethod = "decodeUImmOperand<12>";
}

// A parameterized register class alternative to i32imm/i64imm from Target.td.
def ixlenimm : Operand<XLenVT>;

def ixlenimm_li : Operand<XLenVT> {
  let ParserMatchClass = ImmXLenAsmOperand<"", "LI">;
}

// Standalone (codegen-only) immleaf patterns.
def simm32     : ImmLeaf<XLenVT, [{return isInt<32>(Imm);}]>;
def simm32hi20 : ImmLeaf<XLenVT, [{return isShiftedInt<20, 12>(Imm);}]>;
// A mask value that won't affect significant shift bits.
def immbottomxlenset : ImmLeaf<XLenVT, [{
  if (Subtarget->is64Bit())
    return countTrailingOnes<uint64_t>(Imm) >= 6;
  return countTrailingOnes<uint64_t>(Imm) >= 5;
}]>;

// Addressing modes.
// Necessary because a frameindex can't be matched directly in a pattern.
def AddrFI : ComplexPattern<iPTR, 1, "SelectAddrFI", [frameindex], []>;

// Extract least significant 12 bits from an immediate value and sign extend
// them.
def LO12Sext : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(SignExtend64<12>(N->getZExtValue()),
                                   SDLoc(N), N->getValueType(0));
}]>;

// Extract the most significant 20 bits from an immediate value. Add 1 if bit
// 11 is 1, to compensate for the low 12 bits in the matching immediate addi
// or ld/st being negative.
def HI20 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(((N->getZExtValue()+0x800) >> 12) & 0xfffff,
                                   SDLoc(N), N->getValueType(0));
}]>;

//===----------------------------------------------------------------------===//
// Instruction Class Templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class BranchCC_rri<bits<3> funct3, string opcodestr>
    : RVInstB<funct3, OPC_BRANCH, (outs),
              (ins GPR:$rs1, GPR:$rs2, simm13_lsb0:$imm12),
              opcodestr, "$rs1, $rs2, $imm12"> {
  let isBranch = 1;
  let isTerminator = 1;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class Load_ri<bits<3> funct3, string opcodestr>
    : RVInstI<funct3, OPC_LOAD, (outs GPR:$rd), (ins GPR:$rs1, simm12:$imm12),
              opcodestr, "$rd, ${imm12}(${rs1})">;

// Operands for stores are in the order srcreg, base, offset rather than
// reflecting the order these fields are specified in the instruction
// encoding.
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class Store_rri<bits<3> funct3, string opcodestr>
    : RVInstS<funct3, OPC_STORE, (outs),
              (ins GPR:$rs2, GPR:$rs1, simm12:$imm12),
              opcodestr, "$rs2, ${imm12}(${rs1})">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class ALU_ri<bits<3> funct3, string opcodestr>
    : RVInstI<funct3, OPC_OP_IMM, (outs GPR:$rd), (ins GPR:$rs1, simm12:$imm12),
              opcodestr, "$rd, $rs1, $imm12">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class Shift_ri<bit arithshift, bits<3> funct3, string opcodestr>
    : RVInstIShift<arithshift, funct3, OPC_OP_IMM, (outs GPR:$rd),
                   (ins GPR:$rs1, uimmlog2xlen:$shamt), opcodestr,
                   "$rd, $rs1, $shamt">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class ALU_rr<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_OP, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
              opcodestr, "$rd, $rs1, $rs2">;

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class CSR_ir<bits<3> funct3, string opcodestr>
    : RVInstI<funct3, OPC_SYSTEM, (outs GPR:$rd), (ins csr_sysreg:$imm12, GPR:$rs1),
              opcodestr, "$rd, $imm12, $rs1">;

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class CSR_ii<bits<3> funct3, string opcodestr>
    : RVInstI<funct3, OPC_SYSTEM, (outs GPR:$rd),
              (ins csr_sysreg:$imm12, uimm5:$rs1),
              opcodestr, "$rd, $imm12, $rs1">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class ShiftW_ri<bit arithshift, bits<3> funct3, string opcodestr>
    : RVInstIShiftW<arithshift, funct3, OPC_OP_IMM_32, (outs GPR:$rd),
                    (ins GPR:$rs1, uimm5:$shamt), opcodestr,
                    "$rd, $rs1, $shamt">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class ALUW_rr<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_OP_32, (outs GPR:$rd),
              (ins GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2">;

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class Priv<string opcodestr, bits<7> funct7>
    : RVInstR<funct7, 0b000, OPC_SYSTEM, (outs), (ins GPR:$rs1, GPR:$rs2),
              opcodestr, "">;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, isReMaterializable = 1, mayLoad = 0, mayStore = 0 in {
def LUI : RVInstU<OPC_LUI, (outs GPR:$rd), (ins uimm20_lui:$imm20),
                  "lui", "$rd, $imm20">;

def AUIPC : RVInstU<OPC_AUIPC, (outs GPR:$rd), (ins uimm20_auipc:$imm20),
                    "auipc", "$rd, $imm20">;

let isCall = 1 in
def JAL : RVInstJ<OPC_JAL, (outs GPR:$rd), (ins simm21_lsb0_jal:$imm20),
                  "jal", "$rd, $imm20">;

let isCall = 1 in
def JALR : RVInstI<0b000, OPC_JALR, (outs GPR:$rd),
                   (ins GPR:$rs1, simm12:$imm12),
                   "jalr", "$rd, $rs1, $imm12">;
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

def BEQ  : BranchCC_rri<0b000, "beq">;
def BNE  : BranchCC_rri<0b001, "bne">;
def BLT  : BranchCC_rri<0b100, "blt">;
def BGE  : BranchCC_rri<0b101, "bge">;
def BLTU : BranchCC_rri<0b110, "bltu">;
def BGEU : BranchCC_rri<0b111, "bgeu">;

def LB  : Load_ri<0b000, "lb">;
def LH  : Load_ri<0b001, "lh">;
def LW  : Load_ri<0b010, "lw">;
def LBU : Load_ri<0b100, "lbu">;
def LHU : Load_ri<0b101, "lhu">;

def SB : Store_rri<0b000, "sb">;
def SH : Store_rri<0b001, "sh">;
def SW : Store_rri<0b010, "sw">;

// ADDI isn't always rematerializable, but isReMaterializable will be used as
// a hint which is verified in isReallyTriviallyReMaterializable.
let isReMaterializable = 1 in
def ADDI  : ALU_ri<0b000, "addi">;

def SLTI  : ALU_ri<0b010, "slti">;
def SLTIU : ALU_ri<0b011, "sltiu">;
def XORI  : ALU_ri<0b100, "xori">;
def ORI   : ALU_ri<0b110, "ori">;
def ANDI  : ALU_ri<0b111, "andi">;

def SLLI : Shift_ri<0, 0b001, "slli">;
def SRLI : Shift_ri<0, 0b101, "srli">;
def SRAI : Shift_ri<1, 0b101, "srai">;

def ADD  : ALU_rr<0b0000000, 0b000, "add">;
def SUB  : ALU_rr<0b0100000, 0b000, "sub">;
def SLL  : ALU_rr<0b0000000, 0b001, "sll">;
def SLT  : ALU_rr<0b0000000, 0b010, "slt">;
def SLTU : ALU_rr<0b0000000, 0b011, "sltu">;
def XOR  : ALU_rr<0b0000000, 0b100, "xor">;
def SRL  : ALU_rr<0b0000000, 0b101, "srl">;
def SRA  : ALU_rr<0b0100000, 0b101, "sra">;
def OR   : ALU_rr<0b0000000, 0b110, "or">;
def AND  : ALU_rr<0b0000000, 0b111, "and">;

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in {
def FENCE : RVInstI<0b000, OPC_MISC_MEM, (outs),
                    (ins fencearg:$pred, fencearg:$succ),
                    "fence", "$pred, $succ"> {
  bits<4> pred;
  bits<4> succ;

  let rs1 = 0;
  let rd = 0;
  let imm12 = {0b0000,pred,succ};
}

def FENCE_TSO : RVInstI<0b000, OPC_MISC_MEM, (outs), (ins), "fence.tso", ""> {
  let rs1 = 0;
  let rd = 0;
  let imm12 = {0b1000,0b0011,0b0011};
}

def FENCE_I : RVInstI<0b001, OPC_MISC_MEM, (outs), (ins), "fence.i", ""> {
  let rs1 = 0;
  let rd = 0;
  let imm12 = 0;
}

def ECALL : RVInstI<0b000, OPC_SYSTEM, (outs), (ins), "ecall", ""> {
  let rs1 = 0;
  let rd = 0;
  let imm12 = 0;
}

def EBREAK : RVInstI<0b000, OPC_SYSTEM, (outs), (ins), "ebreak", ""> {
  let rs1 = 0;
  let rd = 0;
  let imm12 = 1;
}

// This is a de facto standard (as set by GNU binutils) 32-bit unimplemented
// instruction (i.e., it should always trap, if your implementation has invalid
// instruction traps).
def UNIMP : RVInstI<0b001, OPC_SYSTEM, (outs), (ins), "unimp", ""> {
  let rs1 = 0;
  let rd = 0;
  let imm12 = 0b110000000000;
}
} // hasSideEffects = 1, mayLoad = 0, mayStore = 0

def CSRRW : CSR_ir<0b001, "csrrw">;
def CSRRS : CSR_ir<0b010, "csrrs">;
def CSRRC : CSR_ir<0b011, "csrrc">;

def CSRRWI : CSR_ii<0b101, "csrrwi">;
def CSRRSI : CSR_ii<0b110, "csrrsi">;
def CSRRCI : CSR_ii<0b111, "csrrci">;

/// RV64I instructions

let Predicates = [IsRV64] in {
def LWU   : Load_ri<0b110, "lwu">;
def LD    : Load_ri<0b011, "ld">;
def SD    : Store_rri<0b011, "sd">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
def ADDIW : RVInstI<0b000, OPC_OP_IMM_32, (outs GPR:$rd),
                    (ins GPR:$rs1, simm12:$imm12),
                    "addiw", "$rd, $rs1, $imm12">;

def SLLIW : ShiftW_ri<0, 0b001, "slliw">;
def SRLIW : ShiftW_ri<0, 0b101, "srliw">;
def SRAIW : ShiftW_ri<1, 0b101, "sraiw">;

def ADDW  : ALUW_rr<0b0000000, 0b000, "addw">;
def SUBW  : ALUW_rr<0b0100000, 0b000, "subw">;
def SLLW  : ALUW_rr<0b0000000, 0b001, "sllw">;
def SRLW  : ALUW_rr<0b0000000, 0b101, "srlw">;
def SRAW  : ALUW_rr<0b0100000, 0b101, "sraw">;
} // Predicates = [IsRV64]

//===----------------------------------------------------------------------===//
// Privileged instructions
//===----------------------------------------------------------------------===//

let isBarrier = 1, isReturn = 1, isTerminator = 1 in {
def URET : Priv<"uret", 0b0000000> {
  let rd = 0;
  let rs1 = 0;
  let rs2 = 0b00010;
}

def SRET : Priv<"sret", 0b0001000> {
  let rd = 0;
  let rs1 = 0;
  let rs2 = 0b00010;
}

def MRET : Priv<"mret", 0b0011000> {
  let rd = 0;
  let rs1 = 0;
  let rs2 = 0b00010;
}
} // isBarrier = 1, isReturn = 1, isTerminator = 1

def WFI : Priv<"wfi", 0b0001000> {
  let rd = 0;
  let rs1 = 0;
  let rs2 = 0b00101;
}

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
def SFENCE_VMA : RVInstR<0b0001001, 0b000, OPC_SYSTEM, (outs),
                         (ins GPR:$rs1, GPR:$rs2),
                         "sfence.vma", "$rs1, $rs2"> {
  let rd = 0;
}

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions (User-Level ISA, Version 2.2, Chapter 20)
//===----------------------------------------------------------------------===//

// TODO la
// TODO lb lh lw
// TODO RV64I: ld
// TODO sb sh sw
// TODO RV64I: sd

def : InstAlias<"nop",           (ADDI      X0,      X0,       0)>;

// Note that the size is 32 because up to 8 32-bit instructions are needed to
// generate an arbitrary 64-bit immediate. However, the size does not really
// matter since PseudoLI is currently only used in the AsmParser where it gets
// expanded to real instructions immediately.
let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Size = 32,
    isCodeGenOnly = 0, isAsmParserOnly = 1 in
def PseudoLI : Pseudo<(outs GPR:$rd), (ins ixlenimm_li:$imm), [],
                      "li", "$rd, $imm">;

def : InstAlias<"mv $rd, $rs",   (ADDI GPR:$rd, GPR:$rs,       0)>;
def : InstAlias<"not $rd, $rs",  (XORI GPR:$rd, GPR:$rs,      -1)>;
def : InstAlias<"neg $rd, $rs",  (SUB  GPR:$rd,      X0, GPR:$rs)>;

let Predicates = [IsRV64] in {
def : InstAlias<"negw $rd, $rs",   (SUBW  GPR:$rd,      X0, GPR:$rs)>;
def : InstAlias<"sext.w $rd, $rs", (ADDIW GPR:$rd, GPR:$rs,       0)>;
} // Predicates = [IsRV64]

def : InstAlias<"seqz $rd, $rs", (SLTIU GPR:$rd, GPR:$rs,       1)>;
def : InstAlias<"snez $rd, $rs", (SLTU  GPR:$rd,      X0, GPR:$rs)>;
def : InstAlias<"sltz $rd, $rs", (SLT   GPR:$rd, GPR:$rs,      X0)>;
def : InstAlias<"sgtz $rd, $rs", (SLT   GPR:$rd,      X0, GPR:$rs)>;

// sgt/sgtu are recognised by the GNU assembler but the canonical slt/sltu
// form will always be printed. Therefore, set a zero weight.
def : InstAlias<"sgt $rd, $rs, $rt", (SLT GPR:$rd, GPR:$rt, GPR:$rs), 0>;
def : InstAlias<"sgtu $rd, $rs, $rt", (SLTU GPR:$rd, GPR:$rt, GPR:$rs), 0>;

def : InstAlias<"beqz $rs, $offset",
                (BEQ GPR:$rs,      X0, simm13_lsb0:$offset)>;
def : InstAlias<"bnez $rs, $offset",
                (BNE GPR:$rs,      X0, simm13_lsb0:$offset)>;
def : InstAlias<"blez $rs, $offset",
                (BGE      X0, GPR:$rs, simm13_lsb0:$offset)>;
def : InstAlias<"bgez $rs, $offset",
                (BGE GPR:$rs,      X0, simm13_lsb0:$offset)>;
def : InstAlias<"bltz $rs, $offset",
                (BLT GPR:$rs,      X0, simm13_lsb0:$offset)>;
def : InstAlias<"bgtz $rs, $offset",
                (BLT      X0, GPR:$rs, simm13_lsb0:$offset)>;

// Always output the canonical mnemonic for the pseudo branch instructions.
// The GNU tools emit the canonical mnemonic for the branch pseudo instructions
// as well (e.g. "bgt" will be recognised by the assembler but never printed by
// objdump). Match this behaviour by setting a zero weight.
def : InstAlias<"bgt $rs, $rt, $offset",
                (BLT  GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;
def : InstAlias<"ble $rs, $rt, $offset",
                (BGE  GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;
def : InstAlias<"bgtu $rs, $rt, $offset",
                (BLTU GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;
def : InstAlias<"bleu $rs, $rt, $offset",
                (BGEU GPR:$rt, GPR:$rs, simm13_lsb0:$offset), 0>;

// "ret" has more weight since "ret" and "jr" alias the same "jalr" instruction.
def : InstAlias<"j $offset",   (JAL  X0, simm21_lsb0_jal:$offset)>;
def : InstAlias<"jal $offset", (JAL  X1, simm21_lsb0_jal:$offset)>;
def : InstAlias<"jr $rs",      (JALR X0, GPR:$rs, 0)>;
def : InstAlias<"jalr $rs",    (JALR X1, GPR:$rs, 0)>;
def : InstAlias<"ret",         (JALR X0,      X1, 0), 2>;
// TODO call
// TODO tail

def : InstAlias<"fence", (FENCE 0xF, 0xF)>; // 0xF == iorw

// CSR Addresses: 0xC00 == cycle,  0xC01 == time,  0xC02 == instret
//                0xC80 == cycleh, 0xC81 == timeh, 0xC82 == instreth
def : InstAlias<"rdinstret $rd", (CSRRS GPR:$rd, 0xC02, X0)>;
def : InstAlias<"rdcycle $rd",   (CSRRS GPR:$rd, 0xC00, X0)>;
def : InstAlias<"rdtime $rd",    (CSRRS GPR:$rd, 0xC01, X0)>;

let Predicates = [IsRV32] in {
def : InstAlias<"rdinstreth $rd", (CSRRS GPR:$rd, 0xC82, X0)>;
def : InstAlias<"rdcycleh $rd",   (CSRRS GPR:$rd, 0xC80, X0)>;
def : InstAlias<"rdtimeh $rd",    (CSRRS GPR:$rd, 0xC81, X0)>;
} // Predicates = [IsRV32]

def : InstAlias<"csrr $rd, $csr", (CSRRS GPR:$rd, csr_sysreg:$csr,      X0)>;
def : InstAlias<"csrw $csr, $rs", (CSRRW      X0, csr_sysreg:$csr, GPR:$rs)>;
def : InstAlias<"csrs $csr, $rs", (CSRRS      X0, csr_sysreg:$csr, GPR:$rs)>;
def : InstAlias<"csrc $csr, $rs", (CSRRC      X0, csr_sysreg:$csr, GPR:$rs)>;

def : InstAlias<"csrwi $csr, $imm", (CSRRWI X0, csr_sysreg:$csr, uimm5:$imm)>;
def : InstAlias<"csrsi $csr, $imm", (CSRRSI X0, csr_sysreg:$csr, uimm5:$imm)>;
def : InstAlias<"csrci $csr, $imm", (CSRRCI X0, csr_sysreg:$csr, uimm5:$imm)>;

let EmitPriority = 0 in {
def : InstAlias<"csrw $csr, $imm", (CSRRWI X0, csr_sysreg:$csr, uimm5:$imm)>;
def : InstAlias<"csrs $csr, $imm", (CSRRSI X0, csr_sysreg:$csr, uimm5:$imm)>;
def : InstAlias<"csrc $csr, $imm", (CSRRCI X0, csr_sysreg:$csr, uimm5:$imm)>;

def : InstAlias<"csrrw $rd, $csr, $imm", (CSRRWI GPR:$rd, csr_sysreg:$csr, uimm5:$imm)>;
def : InstAlias<"csrrs $rd, $csr, $imm", (CSRRSI GPR:$rd, csr_sysreg:$csr, uimm5:$imm)>;
def : InstAlias<"csrrc $rd, $csr, $imm", (CSRRCI GPR:$rd, csr_sysreg:$csr, uimm5:$imm)>;
}

def : InstAlias<"sfence.vma",     (SFENCE_VMA      X0, X0)>;
def : InstAlias<"sfence.vma $rs", (SFENCE_VMA GPR:$rs, X0)>;

let EmitPriority = 0 in {
def : InstAlias<"add $rd, $rs1, $imm12",
                (ADDI  GPR:$rd, GPR:$rs1, simm12:$imm12)>;
def : InstAlias<"and $rd, $rs1, $imm12",
                (ANDI  GPR:$rd, GPR:$rs1, simm12:$imm12)>;
def : InstAlias<"xor $rd, $rs1, $imm12",
                (XORI  GPR:$rd, GPR:$rs1, simm12:$imm12)>;
def : InstAlias<"or $rd, $rs1, $imm12",
                (ORI  GPR:$rd, GPR:$rs1, simm12:$imm12)>;
def : InstAlias<"sll $rd, $rs1, $shamt",
                (SLLI  GPR:$rd, GPR:$rs1, uimmlog2xlen:$shamt)>;
def : InstAlias<"srl $rd, $rs1, $shamt",
                (SRLI  GPR:$rd, GPR:$rs1, uimmlog2xlen:$shamt)>;
def : InstAlias<"sra $rd, $rs1, $shamt",
                (SRAI  GPR:$rd, GPR:$rs1, uimmlog2xlen:$shamt)>;
let Predicates = [IsRV64] in {
def : InstAlias<"addw $rd, $rs1, $imm12",
                (ADDIW  GPR:$rd, GPR:$rs1, simm12:$imm12)>;
def : InstAlias<"sllw $rd, $rs1, $shamt",
                (SLLIW  GPR:$rd, GPR:$rs1, uimm5:$shamt)>;
def : InstAlias<"srlw $rd, $rs1, $shamt",
                (SRLIW  GPR:$rd, GPR:$rs1, uimm5:$shamt)>;
def : InstAlias<"sraw $rd, $rs1, $shamt",
                (SRAIW  GPR:$rd, GPR:$rs1, uimm5:$shamt)>;
} // Predicates = [IsRV64]
def : InstAlias<"slt $rd, $rs1, $imm12",
                (SLTI  GPR:$rd, GPR:$rs1, simm12:$imm12)>;
def : InstAlias<"sltu $rd, $rs1, $imm12",
                (SLTIU  GPR:$rd, GPR:$rs1, simm12:$imm12)>;
}

def : MnemonicAlias<"move", "mv">;

// The SCALL and SBREAK instructions wererenamed to ECALL and EBREAK in
// version 2.1 of the user-level ISA. Like the GNU toolchain, we still accept
// the old name for backwards compatibility.
def : MnemonicAlias<"scall", "ecall">;
def : MnemonicAlias<"sbreak", "ebreak">;

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//
// Naming convention: For 'generic' pattern classes, we use the naming
// convention PatTy1Ty2. For pattern classes which offer a more complex
// expension, prefix the class name, e.g. BccPat.
//===----------------------------------------------------------------------===//

/// Generic pattern classes

class PatGprGpr<SDPatternOperator OpNode, RVInst Inst>
    : Pat<(OpNode GPR:$rs1, GPR:$rs2), (Inst GPR:$rs1, GPR:$rs2)>;
class PatGprSimm12<SDPatternOperator OpNode, RVInstI Inst>
    : Pat<(OpNode GPR:$rs1, simm12:$imm12), (Inst GPR:$rs1, simm12:$imm12)>;
class PatGprUimmLog2XLen<SDPatternOperator OpNode, RVInstIShift Inst>
    : Pat<(OpNode GPR:$rs1, uimmlog2xlen:$shamt),
          (Inst GPR:$rs1, uimmlog2xlen:$shamt)>;

/// Predicates

def IsOrAdd: PatFrag<(ops node:$A, node:$B), (or node:$A, node:$B), [{
  return isOrEquivalentToAdd(N);
}]>;
def assertsexti32 : PatFrag<(ops node:$src), (assertsext node:$src), [{
  return cast<VTSDNode>(N->getOperand(1))->getVT() == MVT::i32;
}]>;
def sexti32 : PatFrags<(ops node:$src),
                       [(sext_inreg node:$src, i32),
                        (assertsexti32 node:$src)]>;
def assertzexti32 : PatFrag<(ops node:$src), (assertzext node:$src), [{
  return cast<VTSDNode>(N->getOperand(1))->getVT() == MVT::i32;
}]>;
def assertzexti5 : PatFrag<(ops node:$src), (assertzext node:$src), [{
  return cast<VTSDNode>(N->getOperand(1))->getVT().getSizeInBits() <= 5;
}]>;
def zexti32 : PatFrags<(ops node:$src),
                       [(and node:$src, 0xffffffff),
                        (assertzexti32 node:$src)]>;
// Defines a legal mask for (assertzexti5 (and src, mask)) to be combinable
// with a shiftw operation. The mask mustn't modify the lower 5 bits or the
// upper 32 bits.
def shiftwamt_mask : ImmLeaf<XLenVT, [{
  return countTrailingOnes<uint64_t>(Imm) >= 5 && isUInt<32>(Imm);
}]>;
def shiftwamt : PatFrags<(ops node:$src),
                         [(assertzexti5 (and node:$src, shiftwamt_mask)),
                          (assertzexti5 node:$src)]>;

/// Immediates

def : Pat<(simm12:$imm), (ADDI X0, simm12:$imm)>;
def : Pat<(simm32hi20:$imm), (LUI (HI20 imm:$imm))>;
def : Pat<(simm32:$imm), (ADDI (LUI (HI20 imm:$imm)), (LO12Sext imm:$imm))>,
      Requires<[IsRV32]>;

/// Simple arithmetic operations

def : PatGprGpr<add, ADD>;
def : PatGprSimm12<add, ADDI>;
def : PatGprGpr<sub, SUB>;
def : PatGprGpr<or, OR>;
def : PatGprSimm12<or, ORI>;
def : PatGprGpr<and, AND>;
def : PatGprSimm12<and, ANDI>;
def : PatGprGpr<xor, XOR>;
def : PatGprSimm12<xor, XORI>;
def : PatGprUimmLog2XLen<shl, SLLI>;
def : PatGprUimmLog2XLen<srl, SRLI>;
def : PatGprUimmLog2XLen<sra, SRAI>;

// Match both a plain shift and one where the shift amount is masked (this is
// typically introduced when the legalizer promotes the shift amount and
// zero-extends it). For RISC-V, the mask is unnecessary as shifts in the base
// ISA only read the least significant 5 bits (RV32I) or 6 bits (RV64I).
class shiftop<SDPatternOperator operator>
    : PatFrags<(ops node:$val, node:$count),
               [(operator node:$val, node:$count),
                (operator node:$val, (and node:$count, immbottomxlenset))]>;

def : PatGprGpr<shiftop<shl>, SLL>;
def : PatGprGpr<shiftop<srl>, SRL>;
def : PatGprGpr<shiftop<sra>, SRA>;

/// FrameIndex calculations

def : Pat<(add (i32 AddrFI:$Rs), simm12:$imm12),
          (ADDI (i32 AddrFI:$Rs), simm12:$imm12)>;
def : Pat<(IsOrAdd (i32 AddrFI:$Rs), simm12:$imm12),
          (ADDI (i32 AddrFI:$Rs), simm12:$imm12)>;

/// Setcc

def : PatGprGpr<setlt, SLT>;
def : PatGprSimm12<setlt, SLTI>;
def : PatGprGpr<setult, SLTU>;
def : PatGprSimm12<setult, SLTIU>;

// Define pattern expansions for setcc operations that aren't directly
// handled by a RISC-V instruction.
def : Pat<(seteq GPR:$rs1, 0), (SLTIU GPR:$rs1, 1)>;
def : Pat<(seteq GPR:$rs1, GPR:$rs2), (SLTIU (XOR GPR:$rs1, GPR:$rs2), 1)>;
def : Pat<(setne GPR:$rs1, 0), (SLTU X0, GPR:$rs1)>;
def : Pat<(setne GPR:$rs1, GPR:$rs2), (SLTU X0, (XOR GPR:$rs1, GPR:$rs2))>;
def : Pat<(setugt GPR:$rs1, GPR:$rs2), (SLTU GPR:$rs2, GPR:$rs1)>;
def : Pat<(setuge GPR:$rs1, GPR:$rs2), (XORI (SLTU GPR:$rs1, GPR:$rs2), 1)>;
def : Pat<(setule GPR:$rs1, GPR:$rs2), (XORI (SLTU GPR:$rs2, GPR:$rs1), 1)>;
def : Pat<(setgt GPR:$rs1, GPR:$rs2), (SLT GPR:$rs2, GPR:$rs1)>;
def : Pat<(setge GPR:$rs1, GPR:$rs2), (XORI (SLT GPR:$rs1, GPR:$rs2), 1)>;
def : Pat<(setle GPR:$rs1, GPR:$rs2), (XORI (SLT GPR:$rs2, GPR:$rs1), 1)>;

let usesCustomInserter = 1 in
class SelectCC_rrirr<RegisterClass valty, RegisterClass cmpty>
    : Pseudo<(outs valty:$dst),
             (ins cmpty:$lhs, cmpty:$rhs, ixlenimm:$imm,
              valty:$truev, valty:$falsev),
             [(set valty:$dst, (SelectCC cmpty:$lhs, cmpty:$rhs,
              (XLenVT imm:$imm), valty:$truev, valty:$falsev))]>;

def Select_GPR_Using_CC_GPR : SelectCC_rrirr<GPR, GPR>;

/// Branches and jumps

// Match `(brcond (CondOp ..), ..)` and lower to the appropriate RISC-V branch
// instruction.
class BccPat<PatFrag CondOp, RVInstB Inst>
    : Pat<(brcond (XLenVT (CondOp GPR:$rs1, GPR:$rs2)), bb:$imm12),
          (Inst GPR:$rs1, GPR:$rs2, simm13_lsb0:$imm12)>;

def : BccPat<seteq, BEQ>;
def : BccPat<setne, BNE>;
def : BccPat<setlt, BLT>;
def : BccPat<setge, BGE>;
def : BccPat<setult, BLTU>;
def : BccPat<setuge, BGEU>;

class BccSwapPat<PatFrag CondOp, RVInst InstBcc>
    : Pat<(brcond (XLenVT (CondOp GPR:$rs1, GPR:$rs2)), bb:$imm12),
          (InstBcc GPR:$rs2, GPR:$rs1, bb:$imm12)>;

// Condition codes that don't have matching RISC-V branch instructions, but
// are trivially supported by swapping the two input operands
def : BccSwapPat<setgt, BLT>;
def : BccSwapPat<setle, BGE>;
def : BccSwapPat<setugt, BLTU>;
def : BccSwapPat<setule, BGEU>;

// An extra pattern is needed for a brcond without a setcc (i.e. where the
// condition was calculated elsewhere).
def : Pat<(brcond GPR:$cond, bb:$imm12), (BNE GPR:$cond, X0, bb:$imm12)>;

let isBarrier = 1, isBranch = 1, isTerminator = 1 in
def PseudoBR : Pseudo<(outs), (ins simm21_lsb0_jal:$imm20), [(br bb:$imm20)]>,
               PseudoInstExpansion<(JAL X0, simm21_lsb0_jal:$imm20)>;

let isCall = 1, Defs=[X1] in
let isBarrier = 1, isBranch = 1, isIndirectBranch = 1, isTerminator = 1 in
def PseudoBRIND : Pseudo<(outs), (ins GPR:$rs1, simm12:$imm12), []>,
                  PseudoInstExpansion<(JALR X0, GPR:$rs1, simm12:$imm12)>;

def : Pat<(brind GPR:$rs1), (PseudoBRIND GPR:$rs1, 0)>;
def : Pat<(brind (add GPR:$rs1, simm12:$imm12)),
          (PseudoBRIND GPR:$rs1, simm12:$imm12)>;

// PseudoCALL is a pseudo instruction which will eventually expand to auipc
// and jalr while encoding. This is desirable, as an auipc+jalr pair with
// R_RISCV_CALL and R_RISCV_RELAX relocations can be be relaxed by the linker
// if the offset fits in a signed 21-bit immediate.
// Define AsmString to print "call" when compile with -S flag.
// Define isCodeGenOnly = 0 to support parsing assembly "call" instruction.
let isCall = 1, Defs = [X1], isCodeGenOnly = 0 in
def PseudoCALL : Pseudo<(outs), (ins bare_symbol:$func),
                        [(Call tglobaladdr:$func)]> {
  let AsmString = "call\t$func";
}

def : Pat<(Call texternalsym:$func), (PseudoCALL texternalsym:$func)>;

def : Pat<(URetFlag), (URET X0, X0)>;
def : Pat<(SRetFlag), (SRET X0, X0)>;
def : Pat<(MRetFlag), (MRET X0, X0)>;

let isCall = 1, Defs = [X1] in
def PseudoCALLIndirect : Pseudo<(outs), (ins GPR:$rs1), [(Call GPR:$rs1)]>,
                         PseudoInstExpansion<(JALR X1, GPR:$rs1, 0)>;

let isBarrier = 1, isReturn = 1, isTerminator = 1 in
def PseudoRET : Pseudo<(outs), (ins), [(RetFlag)]>,
                PseudoInstExpansion<(JALR X0, X1, 0)>;

// PseudoTAIL is a pseudo instruction similar to PseudoCALL and will eventually
// expand to auipc and jalr while encoding.
// Define AsmString to print "tail" when compile with -S flag.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [X2],
    isCodeGenOnly = 0 in
def PseudoTAIL : Pseudo<(outs), (ins bare_symbol:$dst), []> {
  let AsmString = "tail\t$dst";
}

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [X2] in
def PseudoTAILIndirect : Pseudo<(outs), (ins GPRTC:$rs1), [(Tail GPRTC:$rs1)]>,
                         PseudoInstExpansion<(JALR X0, GPR:$rs1, 0)>;

def : Pat<(Tail (iPTR tglobaladdr:$dst)),
          (PseudoTAIL texternalsym:$dst)>;
def : Pat<(Tail (iPTR texternalsym:$dst)),
          (PseudoTAIL texternalsym:$dst)>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCodeGenOnly = 0,
    isAsmParserOnly = 1 in
def PseudoLLA : Pseudo<(outs GPR:$dst), (ins bare_symbol:$src), [],
                       "lla", "$dst, $src">;

/// Loads

multiclass LdPat<PatFrag LoadOp, RVInst Inst> {
  def : Pat<(LoadOp GPR:$rs1), (Inst GPR:$rs1, 0)>;
  def : Pat<(LoadOp AddrFI:$rs1), (Inst AddrFI:$rs1, 0)>;
  def : Pat<(LoadOp (add GPR:$rs1, simm12:$imm12)),
            (Inst GPR:$rs1, simm12:$imm12)>;
  def : Pat<(LoadOp (add AddrFI:$rs1, simm12:$imm12)),
            (Inst AddrFI:$rs1, simm12:$imm12)>;
  def : Pat<(LoadOp (IsOrAdd AddrFI:$rs1, simm12:$imm12)),
            (Inst AddrFI:$rs1, simm12:$imm12)>;
}

defm : LdPat<sextloadi8, LB>;
defm : LdPat<extloadi8, LB>;
defm : LdPat<sextloadi16, LH>;
defm : LdPat<extloadi16, LH>;
defm : LdPat<load, LW>, Requires<[IsRV32]>;
defm : LdPat<zextloadi8, LBU>;
defm : LdPat<zextloadi16, LHU>;

/// Stores

multiclass StPat<PatFrag StoreOp, RVInst Inst, RegisterClass StTy> {
  def : Pat<(StoreOp StTy:$rs2, GPR:$rs1), (Inst StTy:$rs2, GPR:$rs1, 0)>;
  def : Pat<(StoreOp StTy:$rs2, AddrFI:$rs1), (Inst StTy:$rs2, AddrFI:$rs1, 0)>;
  def : Pat<(StoreOp StTy:$rs2, (add GPR:$rs1, simm12:$imm12)),
            (Inst StTy:$rs2, GPR:$rs1, simm12:$imm12)>;
  def : Pat<(StoreOp StTy:$rs2, (add AddrFI:$rs1, simm12:$imm12)),
            (Inst StTy:$rs2, AddrFI:$rs1, simm12:$imm12)>;
  def : Pat<(StoreOp StTy:$rs2, (IsOrAdd AddrFI:$rs1, simm12:$imm12)),
            (Inst StTy:$rs2, AddrFI:$rs1, simm12:$imm12)>;
}

defm : StPat<truncstorei8, SB, GPR>;
defm : StPat<truncstorei16, SH, GPR>;
defm : StPat<store, SW, GPR>, Requires<[IsRV32]>;

/// Fences

// Refer to Table A.6 in the version 2.3 draft of the RISC-V Instruction Set
// Manual: Volume I.

// fence acquire -> fence r, rw
def : Pat<(atomic_fence (XLenVT 4), (imm)), (FENCE 0b10, 0b11)>;
// fence release -> fence rw, w
def : Pat<(atomic_fence (XLenVT 5), (imm)), (FENCE 0b11, 0b1)>;
// fence acq_rel -> fence.tso
def : Pat<(atomic_fence (XLenVT 6), (imm)), (FENCE_TSO)>;
// fence seq_cst -> fence rw, rw
def : Pat<(atomic_fence (XLenVT 7), (imm)), (FENCE 0b11, 0b11)>;

// Lowering for atomic load and store is defined in RISCVInstrInfoA.td.
// Although these are lowered to fence+load/store instructions defined in the
// base RV32I/RV64I ISA, this lowering is only used when the A extension is
// present. This is necessary as it isn't valid to mix __atomic_* libcalls
// with inline atomic operations for the same object.

/// Other pseudo-instructions

// Pessimistically assume the stack pointer will be clobbered
let Defs = [X2], Uses = [X2] in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              [(CallSeqStart timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              [(CallSeqEnd timm:$amt1, timm:$amt2)]>;
} // Defs = [X2], Uses = [X2]

/// RV64 patterns

let Predicates = [IsRV64] in {

/// sext and zext

def : Pat<(sext_inreg GPR:$rs1, i32), (ADDIW GPR:$rs1, 0)>;
def : Pat<(and GPR:$rs1, 0xffffffff), (SRLI (SLLI GPR:$rs1, 32), 32)>;

/// ALU operations

def : Pat<(sext_inreg (add GPR:$rs1, GPR:$rs2), i32),
          (ADDW GPR:$rs1, GPR:$rs2)>;
def : Pat<(sext_inreg (add GPR:$rs1, simm12:$imm12), i32),
          (ADDIW GPR:$rs1, simm12:$imm12)>;
def : Pat<(sext_inreg (sub GPR:$rs1, GPR:$rs2), i32),
          (SUBW GPR:$rs1, GPR:$rs2)>;
def : Pat<(sext_inreg (shl GPR:$rs1, uimm5:$shamt), i32),
          (SLLIW GPR:$rs1, uimm5:$shamt)>;
// (srl (zexti32 ...), uimm5:$shamt) is matched with custom code due to the
// need to undo manipulation of the mask value performed by DAGCombine.
def : Pat<(sra (sext_inreg GPR:$rs1, i32), uimm5:$shamt),
          (SRAIW GPR:$rs1, uimm5:$shamt)>;

// For variable-length shifts, we rely on assertzexti5 being inserted during
// lowering (see RISCVTargetLowering::PerformDAGCombine). This enables us to
// guarantee that selecting a 32-bit variable shift is legal (as the variable
// shift is known to be <= 32). We must also be careful not to create
// semantically incorrect patterns. For instance, selecting SRLW for
// (srl (zexti32 GPR:$rs1), (shiftwamt GPR:$rs2)),
// is not guaranteed to be safe, as we don't know whether the upper 32-bits of
// the result are used or not (in the case where rs2=0, this is a
// sign-extension operation).

def : Pat<(sext_inreg (shl GPR:$rs1, (shiftwamt GPR:$rs2)), i32),
          (SLLW GPR:$rs1, GPR:$rs2)>;
def : Pat<(zexti32 (shl GPR:$rs1, (shiftwamt GPR:$rs2))),
          (SRLI (SLLI (SLLW GPR:$rs1, GPR:$rs2), 32), 32)>;

def : Pat<(sext_inreg (srl (zexti32 GPR:$rs1), (shiftwamt GPR:$rs2)), i32),
          (SRLW GPR:$rs1, GPR:$rs2)>;
def : Pat<(zexti32 (srl (zexti32 GPR:$rs1), (shiftwamt GPR:$rs2))),
          (SRLI (SLLI (SRLW GPR:$rs1, GPR:$rs2), 32), 32)>;

def : Pat<(sra (sexti32 GPR:$rs1), (shiftwamt GPR:$rs2)),
          (SRAW GPR:$rs1, GPR:$rs2)>;

/// Loads

defm : LdPat<sextloadi32, LW>;
defm : LdPat<extloadi32, LW>;
defm : LdPat<zextloadi32, LWU>;
defm : LdPat<load, LD>;

/// Stores

defm : StPat<truncstorei32, SW, GPR>;
defm : StPat<store, SD, GPR>;
} // Predicates = [IsRV64]

//===----------------------------------------------------------------------===//
// Standard extensions
//===----------------------------------------------------------------------===//

include "RISCVInstrInfoM.td"
include "RISCVInstrInfoA.td"
include "RISCVInstrInfoF.td"
include "RISCVInstrInfoD.td"
include "RISCVInstrInfoC.td"
