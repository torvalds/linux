//===-- RISCVInstrInfoM.td - RISC-V 'M' instructions -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'M', Integer
// Multiplication and Division instruction set extension.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtM] in {
def MUL     : ALU_rr<0b0000001, 0b000, "mul">;
def MULH    : ALU_rr<0b0000001, 0b001, "mulh">;
def MULHSU  : ALU_rr<0b0000001, 0b010, "mulhsu">;
def MULHU   : ALU_rr<0b0000001, 0b011, "mulhu">;
def DIV     : ALU_rr<0b0000001, 0b100, "div">;
def DIVU    : ALU_rr<0b0000001, 0b101, "divu">;
def REM     : ALU_rr<0b0000001, 0b110, "rem">;
def REMU    : ALU_rr<0b0000001, 0b111, "remu">;
} // Predicates = [HasStdExtM]

let Predicates = [HasStdExtM, IsRV64] in {
def MULW    : ALUW_rr<0b0000001, 0b000, "mulw">;
def DIVW    : ALUW_rr<0b0000001, 0b100, "divw">;
def DIVUW   : ALUW_rr<0b0000001, 0b101, "divuw">;
def REMW    : ALUW_rr<0b0000001, 0b110, "remw">;
def REMUW   : ALUW_rr<0b0000001, 0b111, "remuw">;
} // Predicates = [HasStdExtM, IsRV64]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtM] in {
def : PatGprGpr<mul, MUL>;
def : PatGprGpr<mulhs, MULH>;
def : PatGprGpr<mulhu, MULHU>;
// No ISDOpcode for mulhsu
def : PatGprGpr<sdiv, DIV>;
def : PatGprGpr<udiv, DIVU>;
def : PatGprGpr<srem, REM>;
def : PatGprGpr<urem, REMU>;
} // Predicates = [HasStdExtM]

let Predicates = [HasStdExtM, IsRV64] in {
def : Pat<(sext_inreg (mul GPR:$rs1, GPR:$rs2), i32),
          (MULW GPR:$rs1, GPR:$rs2)>;
def : Pat<(sext_inreg (sdiv (sexti32 GPR:$rs1),
                            (sexti32 GPR:$rs2)), i32),
          (DIVW GPR:$rs1, GPR:$rs2)>;
def : Pat<(zexti32 (sdiv (sexti32 GPR:$rs1),
                         (sexti32 GPR:$rs2))),
          (SRLI (SLLI (DIVW GPR:$rs1, GPR:$rs2), 32), 32)>;
def : Pat<(sext_inreg (udiv (zexti32 GPR:$rs1), (zexti32 GPR:$rs2)), i32),
          (DIVUW GPR:$rs1, GPR:$rs2)>;
// It's cheaper to perform a divuw and zero-extend the result than to
// zero-extend both inputs to a udiv.
def : Pat<(udiv (and GPR:$rs1, 0xffffffff), (and GPR:$rs2, 0xffffffff)),
          (SRLI (SLLI (DIVUW GPR:$rs1, GPR:$rs2), 32), 32)>;
// Although the sexti32 operands may not have originated from an i32 srem,
// this pattern is safe as it is impossible for two sign extended inputs to
// produce a result where res[63:32]=0 and res[31]=1.
def : Pat<(srem (sexti32 GPR:$rs1), (sexti32 GPR:$rs2)),
          (REMW GPR:$rs1, GPR:$rs2)>;
def : Pat<(sext_inreg (srem (sexti32 GPR:$rs1),
                            (sexti32 GPR:$rs2)), i32),
          (REMW GPR:$rs1, GPR:$rs2)>;
def : Pat<(sext_inreg (urem (zexti32 GPR:$rs1), (zexti32 GPR:$rs2)), i32),
          (REMUW GPR:$rs1, GPR:$rs2)>;
// It's cheaper to perform a remuw and zero-extend the result than to
// zero-extend both inputs to a urem.
def : Pat<(urem (and GPR:$rs1, 0xffffffff), (and GPR:$rs2, 0xffffffff)),
          (SRLI (SLLI (REMUW GPR:$rs1, GPR:$rs2), 32), 32)>;
} // Predicates = [HasStdExtM, IsRV64]
