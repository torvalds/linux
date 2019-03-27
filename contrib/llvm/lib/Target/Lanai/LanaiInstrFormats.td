//===- LanaiInstrFormats.td - Lanai Instruction Formats ----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class InstLanai<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;
  let Size = 4;

  let Namespace = "Lanai";
  let DecoderNamespace = "Lanai";

  bits<4> Opcode;
  let Inst{31 - 28} = Opcode;

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
}

//------------------------------------------------------------------------------
// Register Immediate (RI)
//------------------------------------------------------------------------------
// Encoding:
//           -----------------------------------------------------------------
//           |0.A.A.A| . . . . | . . . . |F.H| . . . . . . . . . . . . . . . |
//           -----------------------------------------------------------------
//            opcode     Rd        Rs1                constant (16)
//
// Action:
//           Rd <- Rs1 op constant
//
// Except for shift instructions, `H' determines whether the constant
// is in the high (1) or low (0) word.  The other halfword is 0x0000,
// except for the `AND' instruction (`AAA' = 100), for which the other
// halfword is 0xFFFF, and shifts (`AAA' = 111), for which the constant is
// sign extended.
//
// `F' determines whether the instruction modifies (1) or does not
// modify (0) the program flags.
//
// `AAA' specifies the operation: `add' (000), `addc' (001), `sub'
// (010), `subb' (011), `and' (100), `or' (101), `xor' (110), or `shift'
// (111).  For the shift, `H' specifies a logical (0) or arithmetic (1)
// shift.  The amount and direction of the shift are determined by the
// sign extended constant interpreted as a two's complement number.  The
// shift operation is defined only for the range of:
//      31 ... 0 -1 ... -31
//      \      / \        /
//        left     right
//        shift    shift
//
// If and only if the `F' bit is 1, RI instructions modify the
// condition bits, `Z' (Zero), `N' (Negative), `V' (oVerflow), and `C'
// (Carry), according to the result.  If the flags are updated, they are
// updated as follows:
// `Z'
//      is set if the result is zero and cleared otherwise.
//
// `N'
//      is set to the most significant bit of the result.
//
// `V'
//      For arithmetic instructions (`add', `addc', `sub', `subb') `V' is
//      set if the sign (most significant) bits of the input operands are
//      the same but different from the sign bit of the result and cleared
//      otherwise.  For other RI instructions, `V' is cleared.
//
// `C'
//      For arithmetic instructions, `C' is set/cleared if there is/is_not
//      a carry generated out of the most significant when performing the
//      twos-complement addition (`sub(a,b) == a + ~b + 1', `subb(a,b) ==
//      a + ~b + `C'').  For left shifts, `C' is set to the least
//      significant bit discarded by the shift operation.  For all other
//      operations, `C' is cleared.
//
// A Jump is accomplished by `Rd' being `pc', and it has one shadow.
//
// The all-0s word is the instruction `R0 <- R0 + 0', which is a no-op.
class InstRI<bits<3> op, dag outs, dag ins, string asmstr,
             list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern>, Sched<[WriteALU]> {
  let Itinerary = IIC_ALU;
  bits<5> Rd;
  bits<5> Rs1;
  bit F;
  bit H;
  bits<16> imm16;

  let Opcode{3} = 0;
  let Opcode{2 - 0} = op;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = Rs1;
  let Inst{17} = F;
  let Inst{16} = H;
  let Inst{15 - 0} = imm16;
}

//------------------------------------------------------------------------------
// Register Register (RR)
//------------------------------------------------------------------------------
// Encoding:
//           -----------------------------------------------------------------
//           |1.1.0.0| . . . . | . . . . |F.I| . . . . |B.B.B|J.J.J.J.J|D.D.D|
//           -----------------------------------------------------------------
//            opcode     Rd        Rs1           Rs2   \       operation     /
//
// Action:
//           `Rd <- Rs1 op Rs2' iff condition DDDI is true.
//
// `DDDI' is as described for the BR instruction.
//
// `F' determines whether the instruction modifies (1) or does not
// modify (0) the program flags.
//
// `BBB' determines the operation: `add' (000), `addc' (001), `sub'
// (010), `subb' (011), `and' (100), `or' (101), `xor' (110), or "special"
// (111).  The `JJJJJ' field is irrelevant except for special.
//
// `JJJJJ' determines which special operation is performed.  `10---'
// is a logical shift, and `11---' is an arithmetic shift, and ‘00000` is
// the SELECT operation.  The amount and direction of the shift are
// determined by the contents of `Rs2' interpreted as a two's complement
// number (in the same way as shifts in the Register-Immediate
// instructions in *Note RI::).  For the SELECT operation, Rd gets Rs1 if
// condition DDDI is true, Rs2 otherwise. All other `JJJJJ' combinations
// are reserved for instructions that may be defined in the future.
//
// If the `F' bit is 1, RR instructions modify the condition bits, `Z'
// (Zero), `N' (Negative), `V' (oVerflow), and `C' (Carry), according to
// the result.  All RR instructions modify the `Z', `N', and `V' flags.
// Except for arithmetic instructions (`add', `addc', `sub', `subb'), `V'
// is cleared.  Only arithmetic instructions and shifts modify `C'. Right
// shifts clear C.
//
// DDDI is as described in the table for the BR instruction and only used for
// the select instruction.
//
// A Jump is accomplished by `Rd' being `pc', and it has one shadow.
class InstRR<bits<3> op, dag outs, dag ins, string asmstr,
             list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern>, Sched<[WriteALU]> {
  let Itinerary = IIC_ALU;
  bits<5> Rd;
  bits<5> Rs1;
  bits<5> Rs2;
  bit F;
  bits<4> DDDI;
  bits<5> JJJJJ;

  let Opcode = 0b1100;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = Rs1;
  let Inst{17} = F;
  let Inst{16} = DDDI{0};
  let Inst{15 - 11} = Rs2;
  let Inst{10 - 8} = op;
  let Inst{7 - 3} = JJJJJ;
  let Inst{2 - 0} = DDDI{3 - 1};
}

//------------------------------------------------------------------------------
// Register Memory (RM)
//------------------------------------------------------------------------------
// Encoding:
//          -----------------------------------------------------------------
//          |1.0.0.S| . . . . | . . . . |P.Q| . . . . . . . . . . . . . . . |
//          -----------------------------------------------------------------
//           opcode     Rd        Rs1                 constant (16)
//
// Action:
//        Rd <- Memory(ea)      (Load)    see below for the
//        Memory(ea) <- Rd      (Store)   definition of ea.
//
// `S' determines whether the instruction is a Load (0) or a Store (1).
// Loads appear in Rd one cycle after this instruction executes.  If the
// following instruction reads Rd, that instruction will be delayed by 1
// clock cycle.
//
//   PQ      operation
//   --      ------------------------------------------
//   00      ea = Rs1
//   01      ea = Rs1,             Rs1 <- Rs1 + constant
//   10      ea = Rs1 + constant
//   11      ea = Rs1 + constant,  Rs1 <- Rs1 + constant
//
// The constant is sign-extended for this instruction.
//
// A Jump is accomplished by `Rd' being `pc', and it has *two* delay slots.
class InstRM<bit S, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  bits<5> Rd;
  bits<5> Rs1;
  bit P;
  bit Q;
  bits<16> imm16;
  // Dummy variables to allow multiclass definition of RM and RRM
  bits<2> YL;
  bit E;

  let Opcode{3 - 1} = 0b100;
  let Opcode{0} = S;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = Rs1;
  let Inst{17} = P;
  let Inst{16} = Q;
  let Inst{15 - 0} = imm16;

  let PostEncoderMethod = "adjustPqBitsRmAndRrm";
}

//------------------------------------------------------------------------------
// Register Register Memory (RRM)
//------------------------------------------------------------------------------
// Encoding:
//           -----------------------------------------------------------------
//           |1.0.1.S| . . . . | . . . . |P.Q| . . . . |B.B.B|J.J.J.J.J|Y.L.E|
//           -----------------------------------------------------------------
//            opcode     Rd        Rs1           Rs2   \       operation     /
//
// Action:
//           Rd <- Memory(ea)      (Load)    see below for the
//           Memory(ea) <- Rd      (Store)   definition of ea.
//
// The RRM instruction is identical to the RM (*note RM::.) instruction
// except that:
//
// 1. `Rs1 + constant' is replaced with `Rs1 op Rs2', where `op' is
//    determined in the same way as in the RR instruction (*note RR::.)
//    and
//
// 2. part-word memory accesses are allowed as specified below.
//
//    If `BBB' != 111 (i.e.: For all but shift operations):
//        If `YLE' = 01- => fuLl-word memory access
//        If `YLE' = 00- => half-word memory access
//        If `YLE' = 10- => bYte memory access
//        If `YLE' = --1 => loads are zEro extended
//        If `YLE' = --0 => loads are sign extended
//
//    If `BBB' = 111 (For shift operations):
//        fullword memory access are performed.
//
// All part-word loads write the least significant part of the
// destination register with the higher-order bits zero- or sign-extended.
// All part-word stores store the least significant part-word of the
// source register in the destination memory location.
//
// A Jump is accomplished by `Rd' being `pc', and it has *two* delay slots.
class InstRRM<bit S, dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  bits<5> Rd;
  bits<5> Rs1;
  bits<5> Rs2;
  bit P;
  bit Q;
  bits<3> BBB;
  bits<5> JJJJJ;
  bits<2> YL;
  bit E;

  let Opcode{3 - 1} = 0b101;
  let Opcode{0} = S;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = Rs1;
  let Inst{17} = P;
  let Inst{16} = Q;
  let Inst{15 - 11} = Rs2;
  let Inst{10 - 8} = BBB;
  let Inst{7 - 3} = JJJJJ;
  let Inst{2 - 1} = YL;
  let Inst{0} = E;

  let PostEncoderMethod = "adjustPqBitsRmAndRrm";
}

//------------------------------------------------------------------------------
// Conditional Branch (BR)
//------------------------------------------------------------------------------
// Encoding:
//           -----------------------------------------------------------------
//           |1.1.1.0|D.D.D| . . . . . . . . . . . . . . . . . . . . . . |0.I|
//           -----------------------------------------------------------------
//            opcode condition                   constant (23)
//
// Action:
//            if (condition) { `pc' <- 4*(zero-extended constant) }
//
// The BR instruction is an absolute branch.
// The constant is scaled as shown by its position in the instruction word such
// that it specifies word-aligned addresses in the range [0,2^25-4]
//
// The `DDDI' field selects the condition that causes the branch to be taken.
// (the `I' (Invert sense) bit inverts the sense of the condition):
//
//   DDDI  logical function                        [code, used for...]
//   ----  --------------------------------------  ------------------------
//   0000  1                                       [T, true]
//   0001  0                                       [F, false]
//   0010  C AND Z'                                [HI, high]
//   0011  C' OR Z                                 [LS, low or same]
//   0100  C'                                      [CC, carry cleared]
//   0101  C                                       [CS, carry set]
//   0110  Z'                                      [NE, not equal]
//   0111  Z                                       [EQ, equal]
//   1000  V'                                      [VC, oVerflow cleared]
//   1001  V                                       [VS, oVerflow set]
//   1010  N'                                      [PL, plus]
//   1011  N                                       [MI, minus]
//   1100  (N AND V) OR (N' AND V')                [GE, greater than or equal]
//   1101  (N AND V') OR (N' AND V)                [LT, less than]
//   1110  (N AND V AND Z') OR (N' AND V' AND Z')  [GT, greater than]
//   1111  (Z) OR (N AND V') OR (N' AND V)         [LE, less than or equal]
//
// If the branch is not taken, the BR instruction is a no-op.  If the branch is
// taken, the processor starts executing instructions at the branch target
// address *after* the processor has executed one more instruction.  That is,
// the branch has one “branch delay slot”.  Be very careful if you find yourself
// wanting to put a branch in a branch delays slot!
class InstBR<dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  let Itinerary = IIC_ALU;
  bits<25> addr;
  bits<4> DDDI;

  let Opcode = 0b1110;
  let Inst{27 - 25} = DDDI{3 - 1};
  let Inst{24 - 0} = addr;
  // These instructions overwrite the last two address bits (which are assumed
  // and ensured to be 0).
  let Inst{1} = 0;
  let Inst{0} = DDDI{0};
}

//------------------------------------------------------------------------------
// Conditional Branch Relative (BRR)
//------------------------------------------------------------------------------
// Encoding:
//           -----------------------------------------------------------------
//           |1.1.1.0|D.D.D|1|-| . . . . |-.-| . . . . . . . . . . . . . |1.I|
//           -----------------------------------------------------------------
//            opcode condition     Rs1           constant (14)
// Action:
//           if (condition) { ‘pc’ <- Rs1 + 4*sign-extended constant) }
//
// BRR behaves like BR, except the branch target address is a 16-bit PC relative
// offset.
class InstBRR<dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  bits<4> DDDI;
  bits<5> Rs1;
  bits<16> imm16;

  let Opcode = 0b1110;
  let Inst{27 - 25} = DDDI{3 - 1};
  let Inst{24} = 1;
  let Inst{22 - 18} = Rs1;
  let Inst{17 - 16} = 0;
  let Inst{15 - 0} = imm16;
  // Overwrite last two bits which have to be zero
  let Inst{1} = 1;
  let Inst{0} = DDDI{0};

  // Set don't cares to zero
  let Inst{23} = 0;
}

//------------------------------------------------------------------------------
// Conditional Set (SCC)
//------------------------------------------------------------------------------
// Encoding:
//           -----------------------------------------------------------------
//           |1.1.1.0|D.D.D|0.-| . . . . |-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-|1.I|
//           -----------------------------------------------------------------
//            opcode condition     Rs1
//
// Action:
//       Rs1 <- logical function result
//
// SCC sets dst_reg to the boolean result of computing the logical function
// specified by DDDI, as described in the table for the BR instruction.
class InstSCC<dag outs, dag ins, string asmstr,
              list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  let Itinerary = IIC_ALU;
  bits<5> Rs1; // dst_reg in documentation
  bits<4> DDDI;

  let Opcode = 0b1110;
  let Inst{27 - 25} = DDDI{3 - 1};
  let Inst{24} = 0;
  let Inst{22 - 18} = Rs1;
  let Inst{1} = 1;
  let Inst{0} = DDDI{0};

  // Set don't cares to zero
  let Inst{23} = 0;
  let Inst{17 - 2} = 0;
}

//------------------------------------------------------------------------------
// Special Load/Store (SLS)
//------------------------------------------------------------------------------
//
// Encoding:
//           -----------------------------------------------------------------
//           |1.1.1.1| . . . . | . . . . |0.S| . . . . . . . . . . . . . . . |
//           -----------------------------------------------------------------
//            opcode     Rd    addr 5msb's            address 16 lsb's
//
// Action:
//           If S = 0 (LOAD):   Rd <- Memory(address);
//           If S = 1 (STORE):  Memory(address) <- Rd
//
// The timing is the same as for RM (*note RM::.) and RRM (*note
// RRM::.) instructions.  The two low-order bits of the 21-bit address are
// ignored.  The address is zero extended.  Fullword memory accesses are
// performed.
class InstSLS<bit S, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  bits<5> Rd;
  bits<5> msb;
  bits<16> lsb;

  let Opcode = 0b1111;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = msb;
  let Inst{17} = 0;
  let Inst{16} = S;
  let Inst{15 - 0} = lsb;
}

//------------------------------------------------------------------------------
// Special Load Immediate (SLI)
//------------------------------------------------------------------------------
// Encoding:
//           -----------------------------------------------------------------
//           |1.1.1.1| . . . . | . . . . |1.0| . . . . . . . . . . . . . . . |
//           -----------------------------------------------------------------
//            opcode     Rd    const 5msb's          constant 16 lsb's
//
// Action:
//           Rd <- constant
//
// The 21-bit constant is zero-extended.  The timing is the same as the
// RM instruction (*note RM::.).
class InstSLI<dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  bits<5> Rd;
  bits<5> msb;
  bits<16> lsb;

  let Opcode = 0b1111;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = msb;
  let Inst{17} = 1;
  let Inst{16} = 0;
  let Inst{15 - 0} = lsb;
}

//------------------------------------------------------------------------------
// Special Part-Word Load/Store (SPLS)
//------------------------------------------------------------------------------
// Encoding:
//        -----------------------------------------------------------------
//        |1.1.1.1| . . . . | . . . . |1.1.0.Y.S.E.P.Q| . . . . . . . . . |
//        -----------------------------------------------------------------
//         opcode     Rd        Rs1                       constant (10)
//
// Action:
//        If `YS' = 11  (bYte     Store):
//             Memory(ea) <- (least significant byte of Rr)
//        If `YS' = 01  (halfword Store):
//             Memory(ea) <- (least significant half-word of Rr)
//        If `YS' = 10  (bYte     load):  Rr <- Memory(ea)
//        If `YS' = 00  (halfword load):  Rr <- Memory(ea)
//             [Note: here ea is determined as in the RM instruction. ]
//        If `SE' = 01 then the value is zEro extended
//             before being loaded into Rd.
//        If `SE' = 00 then the value is sign extended
//             before being loaded into Rd.
//
// `P' and `Q' are used to determine `ea' as in the RM instruction. The
// constant is sign extended.  The timing is the same as the RM and RRM
// instructions.  *Note RM:: and *Note RRM::.
//
// All part-word loads write the part-word into the least significant
// part of the destination register, with the higher-order bits zero- or
// sign-extended.  All part-word stores store the least significant
// part-word of the source register into the destination memory location.
class InstSPLS<dag outs, dag ins, string asmstr,
               list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  bits<5> Rd;
  bits<5> Rs1;
  bits<5> msb;
  bit Y;
  bit S;
  bit E;
  bit P;
  bit Q;
  bits<10> imm10;

  let Opcode = 0b1111;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = Rs1;
  let Inst{17 - 15} = 0b110;
  let Inst{14} = Y;
  let Inst{13} = S;
  let Inst{12} = E;
  let Inst{11} = P;
  let Inst{10} = Q;
  let Inst{9 - 0} = imm10;

  let PostEncoderMethod = "adjustPqBitsSpls";
}

//------------------------------------------------------------------------------
// Special instructions (popc, leadz, trailz)
//------------------------------------------------------------------------------
// Encoding:
//         -----------------------------------------------------------------
//         |1.1.0.1|    Rd   |   Rs1   |F.-| . . . . | . . | . . . . | OP  |
//         -----------------------------------------------------------------
//          opcode      Rd       Rs1
// Action:
//         Rd <- Perform action encoded in OP on Rs1
//   OP is one of:
//      0b001 POPC   Population count;
//      0b010 LEADZ  Count number of leading zeros;
//      0b011 TRAILZ Count number of trailing zeros;
class InstSpecial<bits<3> op, dag outs, dag ins, string asmstr,
                  list<dag> pattern> : InstLanai<outs, ins, asmstr,
                  pattern>, Sched<[WriteALU]> {
  let Itinerary = IIC_ALU;
  bit F;
  bits<5> Rd;
  bits<5> Rs1;

  let Opcode = 0b1101;
  let Inst{27 - 23} = Rd;
  let Inst{22 - 18} = Rs1;
  let Inst{17} = F;
  let Inst{16 - 3} = 0;
  let Inst{2 - 0} = op;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstLanai<outs, ins, asmstr, pattern> {
  let Inst{15 - 0} = 0;
  let isPseudo = 1;
}
