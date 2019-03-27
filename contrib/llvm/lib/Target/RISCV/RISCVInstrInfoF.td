//===-- RISCVInstrInfoF.td - RISC-V 'F' instructions -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'F',
// Single-Precision Floating-Point instruction set extension.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

// Floating-point rounding mode

def FRMArg : AsmOperandClass {
  let Name = "FRMArg";
  let RenderMethod = "addFRMArgOperands";
  let DiagnosticType = "InvalidFRMArg";
}

def frmarg : Operand<XLenVT> {
  let ParserMatchClass = FRMArg;
  let PrintMethod = "printFRMArg";
  let DecoderMethod = "decodeFRMArg";
}

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class FPFMAS_rrr_frm<RISCVOpcode opcode, string opcodestr>
    : RVInstR4<0b00, opcode, (outs FPR32:$rd),
               (ins FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, frmarg:$funct3),
                opcodestr, "$rd, $rs1, $rs2, $rs3, $funct3">;

class FPFMASDynFrmAlias<FPFMAS_rrr_frm Inst, string OpcodeStr>
    : InstAlias<OpcodeStr#" $rd, $rs1, $rs2, $rs3",
                (Inst FPR32:$rd, FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, 0b111)>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class FPALUS_rr<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_OP_FP, (outs FPR32:$rd),
              (ins FPR32:$rs1, FPR32:$rs2), opcodestr, "$rd, $rs1, $rs2">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class FPALUS_rr_frm<bits<7> funct7, string opcodestr>
    : RVInstRFrm<funct7, OPC_OP_FP, (outs FPR32:$rd),
                 (ins FPR32:$rs1, FPR32:$rs2, frmarg:$funct3), opcodestr,
                  "$rd, $rs1, $rs2, $funct3">;

class FPALUSDynFrmAlias<FPALUS_rr_frm Inst, string OpcodeStr>
    : InstAlias<OpcodeStr#" $rd, $rs1, $rs2",
                (Inst FPR32:$rd, FPR32:$rs1, FPR32:$rs2, 0b111)>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class FPUnaryOp_r<bits<7> funct7, bits<3> funct3, RegisterClass rdty,
                RegisterClass rs1ty, string opcodestr>
    : RVInstR<funct7, funct3, OPC_OP_FP, (outs rdty:$rd), (ins rs1ty:$rs1),
              opcodestr, "$rd, $rs1">;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class FPUnaryOp_r_frm<bits<7> funct7, RegisterClass rdty, RegisterClass rs1ty,
                      string opcodestr>
    : RVInstRFrm<funct7, OPC_OP_FP, (outs rdty:$rd),
                 (ins rs1ty:$rs1, frmarg:$funct3), opcodestr,
                  "$rd, $rs1, $funct3">;

class FPUnaryOpDynFrmAlias<FPUnaryOp_r_frm Inst, string OpcodeStr,
                           RegisterClass rdty, RegisterClass rs1ty>
    : InstAlias<OpcodeStr#" $rd, $rs1",
                (Inst rdty:$rd, rs1ty:$rs1, 0b111)>;

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class FPCmpS_rr<bits<3> funct3, string opcodestr>
    : RVInstR<0b1010000, funct3, OPC_OP_FP, (outs GPR:$rd),
              (ins FPR32:$rs1, FPR32:$rs2), opcodestr, "$rd, $rs1, $rs2">;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtF] in {
let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def FLW : RVInstI<0b010, OPC_LOAD_FP, (outs FPR32:$rd),
                  (ins GPR:$rs1, simm12:$imm12),
                   "flw", "$rd, ${imm12}(${rs1})">;

// Operands for stores are in the order srcreg, base, offset rather than
// reflecting the order these fields are specified in the instruction
// encoding.
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def FSW : RVInstS<0b010, OPC_STORE_FP, (outs),
                  (ins FPR32:$rs2, GPR:$rs1, simm12:$imm12),
                   "fsw", "$rs2, ${imm12}(${rs1})">;

def FMADD_S  : FPFMAS_rrr_frm<OPC_MADD, "fmadd.s">;
def          : FPFMASDynFrmAlias<FMADD_S, "fmadd.s">;
def FMSUB_S  : FPFMAS_rrr_frm<OPC_MSUB, "fmsub.s">;
def          : FPFMASDynFrmAlias<FMSUB_S, "fmsub.s">;
def FNMSUB_S : FPFMAS_rrr_frm<OPC_NMSUB, "fnmsub.s">;
def          : FPFMASDynFrmAlias<FNMSUB_S, "fnmsub.s">;
def FNMADD_S : FPFMAS_rrr_frm<OPC_NMADD, "fnmadd.s">;
def          : FPFMASDynFrmAlias<FNMADD_S, "fnmadd.s">;

def FADD_S : FPALUS_rr_frm<0b0000000, "fadd.s">;
def        : FPALUSDynFrmAlias<FADD_S, "fadd.s">;
def FSUB_S : FPALUS_rr_frm<0b0000100, "fsub.s">;
def        : FPALUSDynFrmAlias<FSUB_S, "fsub.s">;
def FMUL_S : FPALUS_rr_frm<0b0001000, "fmul.s">;
def        : FPALUSDynFrmAlias<FMUL_S, "fmul.s">;
def FDIV_S : FPALUS_rr_frm<0b0001100, "fdiv.s">;
def        : FPALUSDynFrmAlias<FDIV_S, "fdiv.s">;

def FSQRT_S : FPUnaryOp_r_frm<0b0101100, FPR32, FPR32, "fsqrt.s"> {
  let rs2 = 0b00000;
}
def         : FPUnaryOpDynFrmAlias<FSQRT_S, "fsqrt.s", FPR32, FPR32>;

def FSGNJ_S  : FPALUS_rr<0b0010000, 0b000, "fsgnj.s">;
def FSGNJN_S : FPALUS_rr<0b0010000, 0b001, "fsgnjn.s">;
def FSGNJX_S : FPALUS_rr<0b0010000, 0b010, "fsgnjx.s">;
def FMIN_S   : FPALUS_rr<0b0010100, 0b000, "fmin.s">;
def FMAX_S   : FPALUS_rr<0b0010100, 0b001, "fmax.s">;

def FCVT_W_S : FPUnaryOp_r_frm<0b1100000, GPR, FPR32, "fcvt.w.s"> {
  let rs2 = 0b00000;
}
def          : FPUnaryOpDynFrmAlias<FCVT_W_S, "fcvt.w.s", GPR, FPR32>;

def FCVT_WU_S : FPUnaryOp_r_frm<0b1100000, GPR, FPR32, "fcvt.wu.s"> {
  let rs2 = 0b00001;
}
def           : FPUnaryOpDynFrmAlias<FCVT_WU_S, "fcvt.wu.s", GPR, FPR32>;

def FMV_X_W : FPUnaryOp_r<0b1110000, 0b000, GPR, FPR32, "fmv.x.w"> {
  let rs2 = 0b00000;
}

def FEQ_S : FPCmpS_rr<0b010, "feq.s">;
def FLT_S : FPCmpS_rr<0b001, "flt.s">;
def FLE_S : FPCmpS_rr<0b000, "fle.s">;

def FCLASS_S : FPUnaryOp_r<0b1110000, 0b001, GPR, FPR32, "fclass.s"> {
  let rs2 = 0b00000;
}

def FCVT_S_W : FPUnaryOp_r_frm<0b1101000, FPR32, GPR, "fcvt.s.w"> {
  let rs2 = 0b00000;
}
def          : FPUnaryOpDynFrmAlias<FCVT_S_W, "fcvt.s.w", FPR32, GPR>;

def FCVT_S_WU : FPUnaryOp_r_frm<0b1101000, FPR32, GPR, "fcvt.s.wu"> {
  let rs2 = 0b00001;
}
def           : FPUnaryOpDynFrmAlias<FCVT_S_WU, "fcvt.s.wu", FPR32, GPR>;

def FMV_W_X : FPUnaryOp_r<0b1111000, 0b000, FPR32, GPR, "fmv.w.x"> {
  let rs2 = 0b00000;
}
} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtF, IsRV64] in {
def FCVT_L_S  : FPUnaryOp_r_frm<0b1100000, GPR, FPR32, "fcvt.l.s"> {
  let rs2 = 0b00010;
}
def           : FPUnaryOpDynFrmAlias<FCVT_L_S, "fcvt.l.s", GPR, FPR32>;

def FCVT_LU_S  : FPUnaryOp_r_frm<0b1100000, GPR, FPR32, "fcvt.lu.s"> {
  let rs2 = 0b00011;
}
def            : FPUnaryOpDynFrmAlias<FCVT_LU_S, "fcvt.lu.s", GPR, FPR32>;

def FCVT_S_L : FPUnaryOp_r_frm<0b1101000, FPR32, GPR, "fcvt.s.l"> {
  let rs2 = 0b00010;
}
def          : FPUnaryOpDynFrmAlias<FCVT_S_L, "fcvt.s.l", FPR32, GPR>;

def FCVT_S_LU : FPUnaryOp_r_frm<0b1101000, FPR32, GPR, "fcvt.s.lu"> {
  let rs2 = 0b00011;
}
def           : FPUnaryOpDynFrmAlias<FCVT_S_LU, "fcvt.s.lu", FPR32, GPR>;
} // Predicates = [HasStdExtF, IsRV64]

//===----------------------------------------------------------------------===//
// Assembler Pseudo Instructions (User-Level ISA, Version 2.2, Chapter 20)
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtF] in {
// TODO flw
// TODO fsw

def : InstAlias<"fmv.s $rd, $rs",  (FSGNJ_S  FPR32:$rd, FPR32:$rs, FPR32:$rs)>;
def : InstAlias<"fabs.s $rd, $rs", (FSGNJX_S FPR32:$rd, FPR32:$rs, FPR32:$rs)>;
def : InstAlias<"fneg.s $rd, $rs", (FSGNJN_S FPR32:$rd, FPR32:$rs, FPR32:$rs)>;

// fgt.s/fge.s are recognised by the GNU assembler but the canonical
// flt.s/fle.s forms will always be printed. Therefore, set a zero weight.
def : InstAlias<"fgt.s $rd, $rs, $rt",
                (FLT_S GPR:$rd, FPR32:$rt, FPR32:$rs), 0>;
def : InstAlias<"fge.s $rd, $rs, $rt",
                (FLE_S GPR:$rd, FPR32:$rt, FPR32:$rs), 0>;

// The following csr instructions actually alias instructions from the base ISA.
// However, it only makes sense to support them when the F extension is enabled.
// CSR Addresses: 0x003 == fcsr, 0x002 == frm, 0x001 == fflags
// NOTE: "frcsr", "frrm", and "frflags" are more specialized version of "csrr".
def : InstAlias<"frcsr $rd",      (CSRRS GPR:$rd, 0x003, X0), 2>;
def : InstAlias<"fscsr $rd, $rs", (CSRRW GPR:$rd, 0x003, GPR:$rs)>;
def : InstAlias<"fscsr $rs",      (CSRRW      X0, 0x003, GPR:$rs), 2>;

def : InstAlias<"frrm $rd",        (CSRRS  GPR:$rd, 0x002, X0), 2>;
def : InstAlias<"fsrm $rd, $rs",   (CSRRW  GPR:$rd, 0x002, GPR:$rs)>;
def : InstAlias<"fsrm $rs",        (CSRRW       X0, 0x002, GPR:$rs), 2>;
def : InstAlias<"fsrmi $rd, $imm", (CSRRWI GPR:$rd, 0x002, uimm5:$imm)>;
def : InstAlias<"fsrmi $imm",      (CSRRWI      X0, 0x002, uimm5:$imm), 2>;

def : InstAlias<"frflags $rd",        (CSRRS  GPR:$rd, 0x001, X0), 2>;
def : InstAlias<"fsflags $rd, $rs",   (CSRRW  GPR:$rd, 0x001, GPR:$rs)>;
def : InstAlias<"fsflags $rs",        (CSRRW       X0, 0x001, GPR:$rs), 2>;
def : InstAlias<"fsflagsi $rd, $imm", (CSRRWI GPR:$rd, 0x001, uimm5:$imm)>;
def : InstAlias<"fsflagsi $imm",      (CSRRWI      X0, 0x001, uimm5:$imm), 2>;

// fmv.w.x and fmv.x.w were previously known as fmv.s.x and fmv.x.s. Both
// spellings should be supported by standard tools.
def : MnemonicAlias<"fmv.s.x", "fmv.w.x">;
def : MnemonicAlias<"fmv.x.s", "fmv.x.w">;
} // Predicates = [HasStdExtF]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

/// Generic pattern classes
class PatFpr32Fpr32<SDPatternOperator OpNode, RVInstR Inst>
    : Pat<(OpNode FPR32:$rs1, FPR32:$rs2), (Inst $rs1, $rs2)>;

class PatFpr32Fpr32DynFrm<SDPatternOperator OpNode, RVInstRFrm Inst>
    : Pat<(OpNode FPR32:$rs1, FPR32:$rs2), (Inst $rs1, $rs2, 0b111)>;

let Predicates = [HasStdExtF] in {

/// Float conversion operations

// Moves (no conversion)
def : Pat<(bitconvert GPR:$rs1), (FMV_W_X GPR:$rs1)>;
def : Pat<(bitconvert FPR32:$rs1), (FMV_X_W FPR32:$rs1)>;

// [u]int32<->float conversion patterns must be gated on IsRV32 or IsRV64, so
// are defined later.

/// Float arithmetic operations

def : PatFpr32Fpr32DynFrm<fadd, FADD_S>;
def : PatFpr32Fpr32DynFrm<fsub, FSUB_S>;
def : PatFpr32Fpr32DynFrm<fmul, FMUL_S>;
def : PatFpr32Fpr32DynFrm<fdiv, FDIV_S>;

def : Pat<(fsqrt FPR32:$rs1), (FSQRT_S FPR32:$rs1, 0b111)>;

def : Pat<(fneg FPR32:$rs1), (FSGNJN_S $rs1, $rs1)>;
def : Pat<(fabs FPR32:$rs1), (FSGNJX_S $rs1, $rs1)>;

def : PatFpr32Fpr32<fcopysign, FSGNJ_S>;
def : Pat<(fcopysign FPR32:$rs1, (fneg FPR32:$rs2)), (FSGNJN_S $rs1, $rs2)>;

// fmadd: rs1 * rs2 + rs3
def : Pat<(fma FPR32:$rs1, FPR32:$rs2, FPR32:$rs3),
          (FMADD_S $rs1, $rs2, $rs3, 0b111)>;

// fmsub: rs1 * rs2 - rs3
def : Pat<(fma FPR32:$rs1, FPR32:$rs2, (fneg FPR32:$rs3)),
          (FMSUB_S FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, 0b111)>;

// fnmsub: -rs1 * rs2 + rs3
def : Pat<(fma (fneg FPR32:$rs1), FPR32:$rs2, FPR32:$rs3),
          (FNMSUB_S FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, 0b111)>;

// fnmadd: -rs1 * rs2 - rs3
def : Pat<(fma (fneg FPR32:$rs1), FPR32:$rs2, (fneg FPR32:$rs3)),
          (FNMADD_S FPR32:$rs1, FPR32:$rs2, FPR32:$rs3, 0b111)>;

// The RISC-V 2.2 user-level ISA spec defines fmin and fmax as returning the
// canonical NaN when given a signaling NaN. This doesn't match the LLVM
// behaviour (see https://bugs.llvm.org/show_bug.cgi?id=27363). However, the
// draft 2.3 ISA spec changes the definition of fmin and fmax in a way that
// matches LLVM's fminnum and fmaxnum
// <https://github.com/riscv/riscv-isa-manual/commit/cd20cee7efd9bac7c5aa127ec3b451749d2b3cce>.
def : PatFpr32Fpr32<fminnum, FMIN_S>;
def : PatFpr32Fpr32<fmaxnum, FMAX_S>;

/// Setcc

def : PatFpr32Fpr32<seteq, FEQ_S>;
def : PatFpr32Fpr32<setoeq, FEQ_S>;
def : PatFpr32Fpr32<setlt, FLT_S>;
def : PatFpr32Fpr32<setolt, FLT_S>;
def : PatFpr32Fpr32<setle, FLE_S>;
def : PatFpr32Fpr32<setole, FLE_S>;

// Define pattern expansions for setcc operations which aren't directly
// handled by a RISC-V instruction and aren't expanded in the SelectionDAG
// Legalizer.

def : Pat<(setuo FPR32:$rs1, FPR32:$rs2),
          (SLTIU (AND (FEQ_S FPR32:$rs1, FPR32:$rs1),
                      (FEQ_S FPR32:$rs2, FPR32:$rs2)),
                 1)>;

def Select_FPR32_Using_CC_GPR : SelectCC_rrirr<FPR32, GPR>;

/// Loads

defm : LdPat<load, FLW>;

/// Stores

defm : StPat<store, FSW, FPR32>;

} // Predicates = [HasStdExtF]

let Predicates = [HasStdExtF, IsRV32] in {
// float->[u]int. Round-to-zero must be used.
def : Pat<(fp_to_sint FPR32:$rs1), (FCVT_W_S $rs1, 0b001)>;
def : Pat<(fp_to_uint FPR32:$rs1), (FCVT_WU_S $rs1, 0b001)>;

// [u]int->float. Match GCC and default to using dynamic rounding mode.
def : Pat<(sint_to_fp GPR:$rs1), (FCVT_S_W $rs1, 0b111)>;
def : Pat<(uint_to_fp GPR:$rs1), (FCVT_S_WU $rs1, 0b111)>;
} // Predicates = [HasStdExtF, IsRV32]
