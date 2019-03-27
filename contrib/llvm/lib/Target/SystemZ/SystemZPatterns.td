//===-- SystemZPatterns.td - SystemZ-specific pattern rules ---*- tblgen-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Record that INSN performs a 64-bit version of unary operator OPERATOR
// in which the operand is sign-extended from 32 to 64 bits.
multiclass SXU<SDPatternOperator operator, Instruction insn> {
  def : Pat<(operator (sext (i32 GR32:$src))),
            (insn GR32:$src)>;
  def : Pat<(operator (sext_inreg GR64:$src, i32)),
            (insn (EXTRACT_SUBREG GR64:$src, subreg_l32))>;
}

// Record that INSN performs a 64-bit version of binary operator OPERATOR
// in which the first operand has class CLS and which the second operand
// is sign-extended from a 32-bit register.
multiclass SXB<SDPatternOperator operator, RegisterOperand cls,
               Instruction insn> {
  def : Pat<(operator cls:$src1, (sext GR32:$src2)),
            (insn cls:$src1, GR32:$src2)>;
  def : Pat<(operator cls:$src1, (sext_inreg GR64:$src2, i32)),
            (insn cls:$src1, (EXTRACT_SUBREG GR64:$src2, subreg_l32))>;
}

// Like SXB, but for zero extension.
multiclass ZXB<SDPatternOperator operator, RegisterOperand cls,
               Instruction insn> {
  def : Pat<(operator cls:$src1, (zext GR32:$src2)),
            (insn cls:$src1, GR32:$src2)>;
  def : Pat<(operator cls:$src1, (and GR64:$src2, 0xffffffff)),
            (insn cls:$src1, (EXTRACT_SUBREG GR64:$src2, subreg_l32))>;
}

// Record that INSN performs a binary read-modify-write operation,
// with LOAD, OPERATOR and STORE being the read, modify and write
// respectively.  MODE is the addressing mode and IMM is the type
// of the second operand.
class RMWI<SDPatternOperator load, SDPatternOperator operator,
           SDPatternOperator store, AddressingMode mode,
           PatFrag imm, Instruction insn>
  : Pat<(store (operator (load mode:$addr), imm:$src), mode:$addr),
        (insn mode:$addr, (UIMM8 imm:$src))>;

// Record that INSN performs binary operation OPERATION on a byte
// memory location.  IMM is the type of the second operand.
multiclass RMWIByte<SDPatternOperator operator, AddressingMode mode,
                    Instruction insn> {
  def : RMWI<anyextloadi8, operator, truncstorei8, mode, imm32, insn>;
  def : RMWI<anyextloadi8, operator, truncstorei8, mode, imm64, insn>;
}

// Record that INSN performs insertion TYPE into a register of class CLS.
// The inserted operand is loaded using LOAD from an address of mode MODE.
multiclass InsertMem<string type, Instruction insn, RegisterOperand cls,
                     SDPatternOperator load, AddressingMode mode> {
  def : Pat<(!cast<SDPatternOperator>("or_as_"##type)
              cls:$src1, (load mode:$src2)),
            (insn cls:$src1, mode:$src2)>;
  def : Pat<(!cast<SDPatternOperator>("or_as_rev"##type)
              (load mode:$src2), cls:$src1),
            (insn cls:$src1, mode:$src2)>;
}

// INSN stores the low 32 bits of a GPR to a memory with addressing mode MODE.
// Record that it is equivalent to using OPERATOR to store a GR64.
class StoreGR64<Instruction insn, SDPatternOperator operator,
                AddressingMode mode>
  : Pat<(operator GR64:$R1, mode:$XBD2),
        (insn (EXTRACT_SUBREG GR64:$R1, subreg_l32), mode:$XBD2)>;

// INSN and INSNY are an RX/RXY pair of instructions that store the low
// 32 bits of a GPR to memory.  Record that they are equivalent to using
// OPERATOR to store a GR64.
multiclass StoreGR64Pair<Instruction insn, Instruction insny,
                         SDPatternOperator operator> {
  def : StoreGR64<insn, operator, bdxaddr12pair>;
  def : StoreGR64<insny, operator, bdxaddr20pair>;
}

// INSN stores the low 32 bits of a GPR using PC-relative addressing.
// Record that it is equivalent to using OPERATOR to store a GR64.
class StoreGR64PC<Instruction insn, SDPatternOperator operator>
  : Pat<(operator GR64:$R1, pcrel32:$XBD2),
        (insn (EXTRACT_SUBREG GR64:$R1, subreg_l32), pcrel32:$XBD2)> {
  // We want PC-relative addresses to be tried ahead of BD and BDX addresses.
  // However, BDXs have two extra operands and are therefore 6 units more
  // complex.
  let AddedComplexity = 7;
}

// INSN and INSNINV conditionally store the low 32 bits of a GPR to memory,
// with INSN storing when the condition is true and INSNINV storing when the
// condition is false.  Record that they are equivalent to a LOAD/select/STORE
// sequence for GR64s.
multiclass CondStores64<Instruction insn, Instruction insninv,
                        SDPatternOperator store, SDPatternOperator load,
                        AddressingMode mode> {
  def : Pat<(store (z_select_ccmask GR64:$new, (load mode:$addr),
                                    imm32zx4:$valid, imm32zx4:$cc),
                   mode:$addr),
            (insn (EXTRACT_SUBREG GR64:$new, subreg_l32), mode:$addr,
                  imm32zx4:$valid, imm32zx4:$cc)>;
  def : Pat<(store (z_select_ccmask (load mode:$addr), GR64:$new,
                                    imm32zx4:$valid, imm32zx4:$cc),
                   mode:$addr),
            (insninv (EXTRACT_SUBREG GR64:$new, subreg_l32), mode:$addr,
                     imm32zx4:$valid, imm32zx4:$cc)>;
}

// Try to use MVC instruction INSN for a load of type LOAD followed by a store
// of the same size.  VT is the type of the intermediate (legalized) value and
// LENGTH is the number of bytes loaded by LOAD.
multiclass MVCLoadStore<SDPatternOperator load, ValueType vt, Instruction insn,
                        bits<5> length> {
  def : Pat<(mvc_store (vt (load bdaddr12only:$src)), bdaddr12only:$dest),
            (insn bdaddr12only:$dest, bdaddr12only:$src, length)>;
}

// Use NC-like instruction INSN for block_op operation OPERATOR.
// The other operand is a load of type LOAD, which accesses LENGTH bytes.
// VT is the intermediate legalized type in which the binary operation
// is actually done.
multiclass BinaryLoadStore<SDPatternOperator operator, SDPatternOperator load,
                           ValueType vt, Instruction insn, bits<5> length> {
  def : Pat<(operator (vt (load bdaddr12only:$src)), bdaddr12only:$dest),
            (insn bdaddr12only:$dest, bdaddr12only:$src, length)>;
}

// A convenient way of generating all block peepholes for a particular
// LOAD/VT/LENGTH combination.
multiclass BlockLoadStore<SDPatternOperator load, ValueType vt,
                          Instruction mvc, Instruction nc, Instruction oc,
                          Instruction xc, bits<5> length> {
  defm : MVCLoadStore<load, vt, mvc, length>;
  defm : BinaryLoadStore<block_and1, load, vt, nc, length>;
  defm : BinaryLoadStore<block_and2, load, vt, nc, length>;
  defm : BinaryLoadStore<block_or1,  load, vt, oc, length>;
  defm : BinaryLoadStore<block_or2,  load, vt, oc, length>;
  defm : BinaryLoadStore<block_xor1, load, vt, xc, length>;
  defm : BinaryLoadStore<block_xor2, load, vt, xc, length>;
}

// Record that INSN is a LOAD AND TEST that can be used to compare
// registers in CLS against zero.  The instruction has separate R1 and R2
// operands, but they must be the same when the instruction is used like this.
multiclass CompareZeroFP<Instruction insn, RegisterOperand cls> {
  def : Pat<(z_fcmp cls:$reg, (fpimm0)), (insn cls:$reg, cls:$reg)>;
  // The sign of the zero makes no difference.
  def : Pat<(z_fcmp cls:$reg, (fpimmneg0)), (insn cls:$reg, cls:$reg)>;
}

// Use INSN for performing binary operation OPERATION of type VT
// on registers of class CLS.
class BinaryRRWithType<Instruction insn, RegisterOperand cls,
                       SDPatternOperator operator, ValueType vt>
  : Pat<(vt (operator cls:$x, cls:$y)), (insn cls:$x, cls:$y)>;

// Use INSN to perform conversion operation OPERATOR, with the input being
// TR2 and the output being TR1.  SUPPRESS is 4 to suppress inexact conditions
// and 0 to allow them.  MODE is the rounding mode to use.
class FPConversion<Instruction insn, SDPatternOperator operator, TypedReg tr1,
                   TypedReg tr2, bits<3> suppress, bits<4> mode>
  : Pat<(tr1.vt (operator (tr2.vt tr2.op:$vec))),
        (insn tr2.op:$vec, suppress, mode)>;

// Use INSN to perform mininum/maximum operation OPERATOR on type TR.
// FUNCTION is the type of minimum/maximum function to perform.
class FPMinMax<Instruction insn, SDPatternOperator operator, TypedReg tr,
               bits<4> function>
  : Pat<(tr.vt (operator (tr.vt tr.op:$vec1), (tr.vt tr.op:$vec2))),
        (insn tr.op:$vec1, tr.op:$vec2, function)>;
