//===- Mips16InstrInfo.td - Target Description for Mips16  -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes Mips16 instructions.
//
//===----------------------------------------------------------------------===//
//
//
// Mips Address
//
def addr16 : ComplexPattern<iPTR, 2, "selectAddr16", [frameindex]>;
def addr16sp : ComplexPattern<iPTR, 2, "selectAddr16SP", [frameindex]>;

//
// Address operand
def mem16 : Operand<i32> {
  let PrintMethod = "printMemOperand";
  let MIOperandInfo = (ops CPU16Regs, simm16);
  let EncoderMethod = "getMemEncoding";
}

def mem16sp : Operand<i32> {
  let PrintMethod = "printMemOperand";
  // This should be CPUSPReg but the MIPS16 subtarget isn't good enough at
  // keeping the sp-relative load and the other varieties separate at the
  // moment. This lie fixes the problem sufficiently well to fix the errors
  // emitted by -verify-machineinstrs and the output ends up correct as long
  // as we use an external assembler (which is already a requirement for MIPS16
  // for several other reasons).
  let MIOperandInfo = (ops CPU16RegsPlusSP, simm16);
  let EncoderMethod = "getMemEncoding";
}

def mem16_ea : Operand<i32> {
  let PrintMethod = "printMemOperandEA";
  let MIOperandInfo = (ops CPU16RegsPlusSP, simm16);
  let EncoderMethod = "getMemEncoding";
}

def pcrel16 : Operand<i32>;

//
// I-type instruction format
//
// this is only used by bimm. the actual assembly value is a 12 bit signed
// number
//
class FI16_ins<bits<5> op, string asmstr, InstrItinClass itin>:
  FI16<op, (outs), (ins brtarget:$imm16),
            !strconcat(asmstr, "\t$imm16 # 16 bit inst"), [], itin>;

//
//
// I8 instruction format
//

class FI816_ins_base<bits<3> _func, string asmstr,
                     string asmstr2, InstrItinClass itin>:
  FI816<_func, (outs), (ins simm16:$imm), !strconcat(asmstr, asmstr2),
        [], itin>;

class FI816_ins<bits<3> _func, string asmstr,
                InstrItinClass itin>:
  FI816_ins_base<_func, asmstr, "\t$imm  # 16 bit inst", itin>;
 
class FI816_SP_ins<bits<3> _func, string asmstr,
                   InstrItinClass itin>:
  FI816_ins_base<_func, asmstr, "\t$$sp, $imm # 16 bit inst", itin>;

//
// RI instruction format
//


class FRI16_ins_base<bits<5> op, string asmstr, string asmstr2,
                     InstrItinClass itin>:
  FRI16<op, (outs CPU16Regs:$rx), (ins simm16:$imm),
        !strconcat(asmstr, asmstr2), [], itin>;

class FRI16_ins<bits<5> op, string asmstr,
                InstrItinClass itin>:
  FRI16_ins_base<op, asmstr, "\t$rx, $imm \t# 16 bit inst", itin>;

class FRI16_TCP_ins<bits<5> _op, string asmstr,
                    InstrItinClass itin>:
  FRI16<_op, (outs CPU16Regs:$rx), (ins pcrel16:$imm, i32imm:$size),
            !strconcat(asmstr, "\t$rx, $imm\t# 16 bit inst"), [], itin>;
            
class FRI16R_ins_base<bits<5> op, string asmstr, string asmstr2,
                     InstrItinClass itin>:
  FRI16<op, (outs), (ins CPU16Regs:$rx, simm16:$imm),
        !strconcat(asmstr, asmstr2), [], itin>;

class FRI16R_ins<bits<5> op, string asmstr,
                InstrItinClass itin>:
  FRI16R_ins_base<op, asmstr, "\t$rx, $imm \t# 16 bit inst", itin>;

class F2RI16_ins<bits<5> _op, string asmstr,
                     InstrItinClass itin>:
  FRI16<_op, (outs CPU16Regs:$rx), (ins CPU16Regs:$rx_, simm16:$imm),
        !strconcat(asmstr, "\t$rx, $imm\t# 16 bit inst"), [], itin> {
  let Constraints = "$rx_ = $rx";
}

class FRI16_B_ins<bits<5> _op, string asmstr,
                  InstrItinClass itin>:
  FRI16<_op, (outs), (ins  CPU16Regs:$rx, brtarget:$imm),
        !strconcat(asmstr, "\t$rx, $imm  # 16 bit inst"), [], itin>;
//
// Compare a register and immediate and place result in CC
// Implicit use of T8
//
// EXT-CCRR Instruction format
//
class FEXT_CCRXI16_ins<string asmstr>:
  MipsPseudo16<(outs CPU16Regs:$cc), (ins CPU16Regs:$rx, simm16:$imm),
               !strconcat(asmstr, "\t$rx, $imm\n\tmove\t$cc, $$t8"), []> {
  let isCodeGenOnly=1;
  let usesCustomInserter = 1;
}

// JAL and JALX instruction format
//
class FJAL16_ins<bits<1> _X, string asmstr,
                 InstrItinClass itin>:
  FJAL16<_X, (outs), (ins uimm26:$imm),
         !strconcat(asmstr, "\t$imm\n\tnop"),[],
         itin>  {
  let isCodeGenOnly=1;
  let Size=6;
}

class FJALB16_ins<bits<1> _X, string asmstr,
                 InstrItinClass itin>:
  FJAL16<_X, (outs), (ins uimm26:$imm),
         !strconcat(asmstr, "\t$imm\t# branch\n\tnop"),[],
         itin>  {
  let isCodeGenOnly=1;
  let Size=6;
}

//
// EXT-I instruction format
//
class FEXT_I16_ins<bits<5> eop, string asmstr, InstrItinClass itin> :
  FEXT_I16<eop, (outs), (ins brtarget:$imm16),
           !strconcat(asmstr, "\t$imm16"),[], itin>;

//
// EXT-I8 instruction format
//

class FEXT_I816_ins_base<bits<3> _func, string asmstr,
                         string asmstr2, InstrItinClass itin>:
  FEXT_I816<_func, (outs), (ins simm16:$imm), !strconcat(asmstr, asmstr2),
            [], itin>;

class FEXT_I816_ins<bits<3> _func, string asmstr,
                    InstrItinClass itin>:
  FEXT_I816_ins_base<_func, asmstr, "\t$imm", itin>;

class FEXT_I816_SP_ins<bits<3> _func, string asmstr,
                       InstrItinClass itin>:
      FEXT_I816_ins_base<_func, asmstr, "\t$$sp, $imm", itin>;

//
// Assembler formats in alphabetical order.
// Natural and pseudos are mixed together.
//
// Compare two registers and place result in CC
// Implicit use of T8
//
// CC-RR Instruction format
//
class FCCRR16_ins<string asmstr> :
  MipsPseudo16<(outs CPU16Regs:$cc), (ins CPU16Regs:$rx, CPU16Regs:$ry),
               !strconcat(asmstr, "\t$rx, $ry\n\tmove\t$cc, $$t8"), []> {
  let isCodeGenOnly=1;
  let usesCustomInserter = 1;
}

//
// EXT-RI instruction format
//

class FEXT_RI16_ins_base<bits<5> _op, string asmstr, string asmstr2,
                         InstrItinClass itin>:
  FEXT_RI16<_op, (outs CPU16Regs:$rx), (ins simm16:$imm),
                  !strconcat(asmstr, asmstr2), [], itin>;

class FEXT_RI16_ins<bits<5> _op, string asmstr,
                    InstrItinClass itin>:
  FEXT_RI16_ins_base<_op, asmstr, "\t$rx, $imm", itin>;

class FEXT_RI16R_ins_base<bits<5> _op, string asmstr, string asmstr2,
                         InstrItinClass itin>:
  FEXT_RI16<_op, (outs ), (ins CPU16Regs:$rx, simm16:$imm),
                  !strconcat(asmstr, asmstr2), [], itin>;

class FEXT_RI16R_ins<bits<5> _op, string asmstr,
                    InstrItinClass itin>:
  FEXT_RI16R_ins_base<_op, asmstr, "\t$rx, $imm", itin>;

class FEXT_RI16_PC_ins<bits<5> _op, string asmstr, InstrItinClass itin>:
  FEXT_RI16_ins_base<_op, asmstr, "\t$rx, $$pc, $imm", itin>;

class FEXT_RI16_B_ins<bits<5> _op, string asmstr,
                      InstrItinClass itin>:
  FEXT_RI16<_op, (outs), (ins  CPU16Regs:$rx, brtarget:$imm),
            !strconcat(asmstr, "\t$rx, $imm"), [], itin>;

class FEXT_RI16_TCP_ins<bits<5> _op, string asmstr,
                        InstrItinClass itin>:
  FEXT_RI16<_op, (outs CPU16Regs:$rx), (ins pcrel16:$imm, i32imm:$size),
            !strconcat(asmstr, "\t$rx, $imm"), [], itin>;

class FEXT_2RI16_ins<bits<5> _op, string asmstr,
                     InstrItinClass itin>:
  FEXT_RI16<_op, (outs CPU16Regs:$rx), (ins CPU16Regs:$rx_, simm16:$imm),
            !strconcat(asmstr, "\t$rx, $imm"), [], itin> {
  let Constraints = "$rx_ = $rx";
}

//
// EXT-RRI instruction format
//

class FEXT_RRI16_mem_ins<bits<5> op, string asmstr, Operand MemOpnd,
                         InstrItinClass itin>:
  FEXT_RRI16<op, (outs CPU16Regs:$ry), (ins  MemOpnd:$addr),
             !strconcat(asmstr, "\t$ry, $addr"), [], itin>;

class FEXT_RRI16_mem2_ins<bits<5> op, string asmstr, Operand MemOpnd,
                          InstrItinClass itin>:
  FEXT_RRI16<op, (outs ), (ins  CPU16Regs:$ry, MemOpnd:$addr),
             !strconcat(asmstr, "\t$ry, $addr"), [], itin>;

//
//
// EXT-RRI-A instruction format
//

class FEXT_RRI_A16_mem_ins<bits<1> op, string asmstr, Operand MemOpnd,
                           InstrItinClass itin>:
  FEXT_RRI_A16<op, (outs CPU16Regs:$ry), (ins  MemOpnd:$addr),
               !strconcat(asmstr, "\t$ry, $addr"), [], itin>;

//
// EXT-SHIFT instruction format
//
class FEXT_SHIFT16_ins<bits<2> _f, string asmstr, InstrItinClass itin>:
  FEXT_SHIFT16<_f, (outs CPU16Regs:$rx), (ins CPU16Regs:$ry, uimm5:$sa),
               !strconcat(asmstr, "\t$rx, $ry, $sa"), [], itin>;

//
// EXT-T8I8
//
class FEXT_T8I816_ins<string asmstr, string asmstr2>:
  MipsPseudo16<(outs),
               (ins CPU16Regs:$rx, CPU16Regs:$ry, brtarget:$imm),
               !strconcat(asmstr2, !strconcat("\t$rx, $ry\n\t",
               !strconcat(asmstr, "\t$imm"))),[]> {
  let isCodeGenOnly=1;
  let usesCustomInserter = 1;
}

//
// EXT-T8I8I
//
class FEXT_T8I8I16_ins<string asmstr, string asmstr2>:
  MipsPseudo16<(outs),
               (ins CPU16Regs:$rx, simm16:$imm, brtarget:$targ),
               !strconcat(asmstr2, !strconcat("\t$rx, $imm\n\t",
               !strconcat(asmstr, "\t$targ"))), []> {
  let isCodeGenOnly=1;
  let usesCustomInserter = 1;
}
//


//
// I8_MOVR32 instruction format (used only by the MOVR32 instructio
//
class FI8_MOVR3216_ins<string asmstr, InstrItinClass itin>:
       FI8_MOVR3216<(outs CPU16Regs:$rz), (ins GPR32:$r32),
       !strconcat(asmstr,  "\t$rz, $r32"), [], itin>;

//
// I8_MOV32R instruction format (used only by MOV32R instruction)
//

class FI8_MOV32R16_ins<string asmstr, InstrItinClass itin>:
  FI8_MOV32R16<(outs GPR32:$r32), (ins CPU16Regs:$rz),
               !strconcat(asmstr,  "\t$r32, $rz"), [], itin>;

//
// This are pseudo formats for multiply
// This first one can be changed to non-pseudo now.
//
// MULT
//
class FMULT16_ins<string asmstr, InstrItinClass itin> :
  MipsPseudo16<(outs), (ins CPU16Regs:$rx, CPU16Regs:$ry),
               !strconcat(asmstr, "\t$rx, $ry"), []>;

//
// MULT-LO
//
class FMULT16_LO_ins<string asmstr, InstrItinClass itin> :
  MipsPseudo16<(outs CPU16Regs:$rz), (ins CPU16Regs:$rx, CPU16Regs:$ry),
               !strconcat(asmstr, "\t$rx, $ry\n\tmflo\t$rz"), []> {
  let isCodeGenOnly=1;
}

//
// RR-type instruction format
//

class FRR16_ins<bits<5> f, string asmstr, InstrItinClass itin> :
  FRR16<f, (outs CPU16Regs:$rx), (ins CPU16Regs:$ry),
        !strconcat(asmstr, "\t$rx, $ry"), [], itin> {
}

class FRRBreakNull16_ins<string asmstr, InstrItinClass itin> :
  FRRBreak16<(outs), (ins), asmstr, [], itin> {
  let Code=0;
}

class FRR16R_ins<bits<5> f, string asmstr, InstrItinClass itin> :
  FRR16<f, (outs), (ins  CPU16Regs:$rx, CPU16Regs:$ry),
        !strconcat(asmstr, "\t$rx, $ry"), [], itin> {
}

class FRRTR16_ins<string asmstr> :
  MipsPseudo16<(outs CPU16Regs:$rz), (ins CPU16Regs:$rx, CPU16Regs:$ry),
               !strconcat(asmstr, "\t$rx, $ry\n\tmove\t$rz, $$t8"), []> ;

//
// maybe refactor but need a $zero as a dummy first parameter
//
class FRR16_div_ins<bits<5> f, string asmstr, InstrItinClass itin> :
  FRR16<f, (outs ), (ins CPU16Regs:$rx, CPU16Regs:$ry),
        !strconcat(asmstr, "\t$$zero, $rx, $ry"), [], itin> ;

class FUnaryRR16_ins<bits<5> f, string asmstr, InstrItinClass itin> :
  FRR16<f, (outs CPU16Regs:$rx), (ins CPU16Regs:$ry),
        !strconcat(asmstr, "\t$rx, $ry"), [], itin> ;


class FRR16_M_ins<bits<5> f, string asmstr,
                  InstrItinClass itin> :
  FRR16<f, (outs CPU16Regs:$rx), (ins),
        !strconcat(asmstr, "\t$rx"), [], itin>;

class FRxRxRy16_ins<bits<5> f, string asmstr,
                    InstrItinClass itin> :
  FRR16<f, (outs CPU16Regs:$rz), (ins CPU16Regs:$rx, CPU16Regs:$ry),
            !strconcat(asmstr, "\t$rz, $ry"),
            [], itin> {
  let Constraints = "$rx = $rz";
}

let rx=0 in
class FRR16_JALRC_RA_only_ins<bits<1> nd_, bits<1> l_,
                              string asmstr, InstrItinClass itin>:
  FRR16_JALRC<nd_, l_, 1, (outs), (ins), !strconcat(asmstr, "\t$$ra"),
              [], itin> ;


class FRR16_JALRC_ins<bits<1> nd, bits<1> l, bits<1> ra,
                      string asmstr, InstrItinClass itin>:
  FRR16_JALRC<nd, l, ra, (outs), (ins CPU16Regs:$rx),
              !strconcat(asmstr, "\t$rx"), [], itin> ;

class FRR_SF16_ins
  <bits<5> _funct, bits<3> _subfunc,
    string asmstr, InstrItinClass itin>:
  FRR_SF16<_funct, _subfunc, (outs CPU16Regs:$rx), (ins CPU16Regs:$rx_),
           !strconcat(asmstr, "\t $rx"),
           [], itin> {
  let Constraints = "$rx_ = $rx";
  }
//
// RRR-type instruction format
//

class FRRR16_ins<bits<2> _f, string asmstr,  InstrItinClass itin> :
  FRRR16<_f, (outs CPU16Regs:$rz), (ins CPU16Regs:$rx, CPU16Regs:$ry),
         !strconcat(asmstr, "\t$rz, $rx, $ry"), [], itin>;

//
// These Sel patterns support the generation of conditional move
// pseudo instructions.
//
// The nomenclature uses the components making up the pseudo and may
// be a bit counter intuitive when compared with the end result we seek.
// For example using a bqez in the example directly below results in the
// conditional move being done if the tested register is not zero.
// I considered in easier to check by keeping the pseudo consistent with
// it's components but it could have been done differently.
//
// The simplest case is when can test and operand directly and do the
// conditional move based on a simple mips16 conditional
//  branch instruction.
// for example:
// if $op == beqz or bnez:
//
// $op1 $rt, .+4
// move $rd, $rs
//
// if $op == beqz, then if $rt != 0, then the conditional assignment
// $rd = $rs is done.

// if $op == bnez, then if $rt == 0, then the conditional assignment
// $rd = $rs is done.
//
// So this pseudo class only has one operand, i.e. op
//
class Sel<string op>:
  MipsPseudo16<(outs CPU16Regs:$rd_), (ins CPU16Regs:$rd, CPU16Regs:$rs,
               CPU16Regs:$rt),
               !strconcat(op, "\t$rt, .+4\n\t\n\tmove $rd, $rs"), []> {
  //let isCodeGenOnly=1;
  let Constraints = "$rd = $rd_";
  let usesCustomInserter = 1;
}

//
// The next two instruction classes allow for an operand which tests
// two operands and returns a value in register T8 and
//then does a conditional branch based on the value of T8
//

// op2 can be cmpi or slti/sltiu
// op1 can bteqz or btnez
// the operands for op2 are a register and a signed constant
//
// $op2 $t, $imm  ;test register t and branch conditionally
// $op1 .+4       ;op1 is a conditional branch
// move $rd, $rs
//
//
class SeliT<string op1, string op2>:
  MipsPseudo16<(outs CPU16Regs:$rd_), (ins CPU16Regs:$rd, CPU16Regs:$rs,
                                       CPU16Regs:$rl, simm16:$imm),
               !strconcat(op2,
               !strconcat("\t$rl, $imm\n\t",
               !strconcat(op1, "\t.+4\n\tmove $rd, $rs"))), []> {
  let isCodeGenOnly=1;
  let Constraints = "$rd = $rd_";
  let usesCustomInserter = 1;
}

//
// op2 can be cmp or slt/sltu
// op1 can be bteqz or btnez
// the operands for op2 are two registers
// op1 is a conditional branch
//
//
// $op2 $rl, $rr  ;test registers rl,rr
// $op1 .+4       ;op2 is a conditional branch
// move $rd, $rs
//
//
class SelT<string op1, string op2>:
  MipsPseudo16<(outs CPU16Regs:$rd_),
               (ins CPU16Regs:$rd, CPU16Regs:$rs,
                CPU16Regs:$rl, CPU16Regs:$rr),
               !strconcat(op2,
               !strconcat("\t$rl, $rr\n\t",
               !strconcat(op1, "\t.+4\n\tmove $rd, $rs"))), []> {
  let isCodeGenOnly=1;
  let Constraints = "$rd = $rd_";
  let usesCustomInserter = 1;
}

//
// 32 bit constant
//
def Constant32:
  MipsPseudo16<(outs), (ins simm32:$imm), "\t.word $imm", []>;

def LwConstant32:
  MipsPseudo16<(outs CPU16Regs:$rx), (ins simm32:$imm, simm32:$constid),
    "lw\t$rx, 1f\n\tb\t2f\n\t.align\t2\n1: \t.word\t$imm\n2:", []>;


//
// Some general instruction class info
//
//

class ArithLogic16Defs<bit isCom=0> {
  bits<5> shamt = 0;
  bit isCommutable = isCom;
  bit isReMaterializable = 1;
  bit hasSideEffects = 0;
}

class branch16 {
  bit isBranch = 1;
  bit isTerminator = 1;
  bit isBarrier = 1;
}

class cbranch16 {
  bit isBranch = 1;
  bit isTerminator = 1;
}

class MayLoad {
  bit mayLoad = 1;
}

class MayStore {
  bit mayStore = 1;
}
//


// Format: ADDIU rx, immediate MIPS16e
// Purpose: Add Immediate Unsigned Word (2-Operand, Extended)
// To add a constant to a 32-bit integer.
//
def AddiuRxImmX16: FEXT_RI16_ins<0b01001, "addiu", IIM16Alu>;

def AddiuRxRxImm16: F2RI16_ins<0b01001, "addiu", IIM16Alu>,
  ArithLogic16Defs<0> {
  let AddedComplexity = 5;
}
def AddiuRxRxImmX16: FEXT_2RI16_ins<0b01001, "addiu", IIM16Alu>,
  ArithLogic16Defs<0> {
  let isCodeGenOnly = 1;
}

def AddiuRxRyOffMemX16:
  FEXT_RRI_A16_mem_ins<0, "addiu", mem16_ea, IIM16Alu>;

//

// Format: ADDIU rx, pc, immediate MIPS16e
// Purpose: Add Immediate Unsigned Word (3-Operand, PC-Relative, Extended)
// To add a constant to the program counter.
//
def AddiuRxPcImmX16: FEXT_RI16_PC_ins<0b00001, "addiu", IIM16Alu>;

//
// Format: ADDIU sp, immediate MIPS16e
// Purpose: Add Immediate Unsigned Word (2-Operand, SP-Relative, Extended)
// To add a constant to the stack pointer.
//
def AddiuSpImm16
  : FI816_SP_ins<0b011, "addiu", IIM16Alu> {
  let Defs = [SP];
  let Uses = [SP];
  let AddedComplexity = 5;
}

def AddiuSpImmX16
  : FEXT_I816_SP_ins<0b011, "addiu", IIM16Alu> {
  let Defs = [SP];
  let Uses = [SP];
}

//
// Format: ADDU rz, rx, ry MIPS16e
// Purpose: Add Unsigned Word (3-Operand)
// To add 32-bit integers.
//

def AdduRxRyRz16: FRRR16_ins<01, "addu", IIM16Alu>, ArithLogic16Defs<1>;

//
// Format: AND rx, ry MIPS16e
// Purpose: AND
// To do a bitwise logical AND.

def AndRxRxRy16: FRxRxRy16_ins<0b01100, "and", IIM16Alu>, ArithLogic16Defs<1>;


//
// Format: BEQZ rx, offset MIPS16e
// Purpose: Branch on Equal to Zero
// To test a GPR then do a PC-relative conditional branch.
//
def BeqzRxImm16: FRI16_B_ins<0b00100, "beqz", IIM16Alu>, cbranch16;


//
// Format: BEQZ rx, offset MIPS16e
// Purpose: Branch on Equal to Zero (Extended)
// To test a GPR then do a PC-relative conditional branch.
//
def BeqzRxImmX16: FEXT_RI16_B_ins<0b00100, "beqz", IIM16Alu>, cbranch16;

//
// Format: B offset MIPS16e
// Purpose: Unconditional Branch (Extended)
// To do an unconditional PC-relative branch.
//

def Bimm16: FI16_ins<0b00010, "b", IIM16Alu>, branch16;

// Format: B offset MIPS16e
// Purpose: Unconditional Branch
// To do an unconditional PC-relative branch.
//
def BimmX16: FEXT_I16_ins<0b00010, "b", IIM16Alu>, branch16;

//
// Format: BNEZ rx, offset MIPS16e
// Purpose: Branch on Not Equal to Zero
// To test a GPR then do a PC-relative conditional branch.
//
def BnezRxImm16: FRI16_B_ins<0b00101, "bnez", IIM16Alu>, cbranch16;

//
// Format: BNEZ rx, offset MIPS16e
// Purpose: Branch on Not Equal to Zero (Extended)
// To test a GPR then do a PC-relative conditional branch.
//
def BnezRxImmX16: FEXT_RI16_B_ins<0b00101, "bnez", IIM16Alu>, cbranch16;


//
//Format: BREAK immediate
// Purpose: Breakpoint
// To cause a Breakpoint exception.

def Break16: FRRBreakNull16_ins<"break 0", IIM16Alu>;
//
// Format: BTEQZ offset MIPS16e
// Purpose: Branch on T Equal to Zero (Extended)
// To test special register T then do a PC-relative conditional branch.
//
def Bteqz16: FI816_ins<0b000, "bteqz", IIM16Alu>, cbranch16 {
  let Uses = [T8];
}

def BteqzX16: FEXT_I816_ins<0b000, "bteqz", IIM16Alu>, cbranch16 {
  let Uses = [T8];
}

def BteqzT8CmpX16: FEXT_T8I816_ins<"bteqz", "cmp">, cbranch16;

def BteqzT8CmpiX16: FEXT_T8I8I16_ins<"bteqz", "cmpi">,
  cbranch16;

def BteqzT8SltX16: FEXT_T8I816_ins<"bteqz", "slt">, cbranch16;

def BteqzT8SltuX16: FEXT_T8I816_ins<"bteqz", "sltu">, cbranch16;

def BteqzT8SltiX16: FEXT_T8I8I16_ins<"bteqz", "slti">, cbranch16;

def BteqzT8SltiuX16: FEXT_T8I8I16_ins<"bteqz", "sltiu">,
  cbranch16;

//
// Format: BTNEZ offset MIPS16e
// Purpose: Branch on T Not Equal to Zero (Extended)
// To test special register T then do a PC-relative conditional branch.
//

def Btnez16: FI816_ins<0b001, "btnez", IIM16Alu>, cbranch16 {
  let Uses = [T8];
}

def BtnezX16: FEXT_I816_ins<0b001, "btnez", IIM16Alu> ,cbranch16 {
  let Uses = [T8];
}

def BtnezT8CmpX16: FEXT_T8I816_ins<"btnez", "cmp">, cbranch16;

def BtnezT8CmpiX16: FEXT_T8I8I16_ins<"btnez", "cmpi">, cbranch16;

def BtnezT8SltX16: FEXT_T8I816_ins<"btnez", "slt">, cbranch16;

def BtnezT8SltuX16: FEXT_T8I816_ins<"btnez", "sltu">, cbranch16;

def BtnezT8SltiX16: FEXT_T8I8I16_ins<"btnez", "slti">, cbranch16;

def BtnezT8SltiuX16: FEXT_T8I8I16_ins<"btnez", "sltiu">,
  cbranch16;

//
// Format: CMP rx, ry MIPS16e
// Purpose: Compare
// To compare the contents of two GPRs.
//
def CmpRxRy16: FRR16R_ins<0b01010, "cmp", IIM16Alu> {
  let Defs = [T8];
}

//
// Format: CMPI rx, immediate MIPS16e
// Purpose: Compare Immediate
// To compare a constant with the contents of a GPR.
//
def CmpiRxImm16: FRI16R_ins<0b01110, "cmpi", IIM16Alu> {
  let Defs = [T8];
}

//
// Format: CMPI rx, immediate MIPS16e
// Purpose: Compare Immediate (Extended)
// To compare a constant with the contents of a GPR.
//
def CmpiRxImmX16: FEXT_RI16R_ins<0b01110, "cmpi", IIM16Alu> {
  let Defs = [T8];
}


//
// Format: DIV rx, ry MIPS16e
// Purpose: Divide Word
// To divide 32-bit signed integers.
//
def DivRxRy16: FRR16_div_ins<0b11010, "div", IIM16Alu> {
  let Defs = [HI0, LO0];
}

//
// Format: DIVU rx, ry MIPS16e
// Purpose: Divide Unsigned Word
// To divide 32-bit unsigned integers.
//
def DivuRxRy16: FRR16_div_ins<0b11011, "divu", IIM16Alu> {
  let Defs = [HI0, LO0];
}
//
// Format: JAL target MIPS16e
// Purpose: Jump and Link
// To execute a procedure call within the current 256 MB-aligned
// region and preserve the current ISA.
//

def Jal16 : FJAL16_ins<0b0, "jal", IIM16Alu> {
  let hasDelaySlot = 0;  // not true, but we add the nop for now
  let isCall=1;
  let Defs = [RA];
}

def JalB16 : FJALB16_ins<0b0, "jal", IIM16Alu>, branch16 {
  let hasDelaySlot = 0;  // not true, but we add the nop for now
  let isBranch=1;
  let Defs = [RA];
}

//
// Format: JR ra MIPS16e
// Purpose: Jump Register Through Register ra
// To execute a branch to the instruction address in the return
// address register.
//

def JrRa16: FRR16_JALRC_RA_only_ins<0, 0, "jr", IIM16Alu> {
  let isBranch = 1;
  let isIndirectBranch = 1;
  let hasDelaySlot = 1;
  let isTerminator=1;
  let isBarrier=1;
  let isReturn=1;
}

def JrcRa16: FRR16_JALRC_RA_only_ins<1, 1, "jrc", IIM16Alu> {
  let isBranch = 1;
  let isIndirectBranch = 1;
  let isTerminator=1;
  let isBarrier=1;
  let isReturn=1;
}

def JrcRx16: FRR16_JALRC_ins<1, 1, 0, "jrc", IIM16Alu> {
  let isBranch = 1;
  let isIndirectBranch = 1;
  let isTerminator=1;
  let isBarrier=1;
}
//
// Format: LB ry, offset(rx) MIPS16e
// Purpose: Load Byte (Extended)
// To load a byte from memory as a signed value.
//
def LbRxRyOffMemX16: FEXT_RRI16_mem_ins<0b10011, "lb", mem16, II_LB>, MayLoad{
  let isCodeGenOnly = 1;
}

//
// Format: LBU ry, offset(rx) MIPS16e
// Purpose: Load Byte Unsigned (Extended)
// To load a byte from memory as a unsigned value.
//
def LbuRxRyOffMemX16:
  FEXT_RRI16_mem_ins<0b10100, "lbu", mem16, II_LBU>, MayLoad {
  let isCodeGenOnly = 1;
}

//
// Format: LH ry, offset(rx) MIPS16e
// Purpose: Load Halfword signed (Extended)
// To load a halfword from memory as a signed value.
//
def LhRxRyOffMemX16: FEXT_RRI16_mem_ins<0b10100, "lh", mem16, II_LH>, MayLoad{
  let isCodeGenOnly = 1;
}

//
// Format: LHU ry, offset(rx) MIPS16e
// Purpose: Load Halfword unsigned (Extended)
// To load a halfword from memory as an unsigned value.
//
def LhuRxRyOffMemX16:
  FEXT_RRI16_mem_ins<0b10100, "lhu", mem16, II_LHU>, MayLoad {
  let isCodeGenOnly = 1;
}

//
// Format: LI rx, immediate MIPS16e
// Purpose: Load Immediate
// To load a constant into a GPR.
//
def LiRxImm16: FRI16_ins<0b01101, "li", IIM16Alu>;

//
// Format: LI rx, immediate MIPS16e
// Purpose: Load Immediate (Extended)
// To load a constant into a GPR.
//
def LiRxImmX16: FEXT_RI16_ins<0b01101, "li", IIM16Alu>;

def LiRxImmAlignX16: FEXT_RI16_ins<0b01101, ".align 2\n\tli", IIM16Alu> {
  let isCodeGenOnly = 1;
}

//
// Format: LW ry, offset(rx) MIPS16e
// Purpose: Load Word (Extended)
// To load a word from memory as a signed value.
//
def LwRxRyOffMemX16: FEXT_RRI16_mem_ins<0b10011, "lw", mem16, II_LW>, MayLoad{
  let isCodeGenOnly = 1;
}

// Format: LW rx, offset(sp) MIPS16e
// Purpose: Load Word (SP-Relative, Extended)
// To load an SP-relative word from memory as a signed value.
//
def LwRxSpImmX16: FEXT_RRI16_mem_ins<0b10010, "lw", mem16sp, II_LW>, MayLoad;

def LwRxPcTcp16: FRI16_TCP_ins<0b10110, "lw", II_LW>, MayLoad;

def LwRxPcTcpX16: FEXT_RI16_TCP_ins<0b10110, "lw", II_LW>, MayLoad;
//
// Format: MOVE r32, rz MIPS16e
// Purpose: Move
// To move the contents of a GPR to a GPR.
//
def Move32R16: FI8_MOV32R16_ins<"move", IIM16Alu>;

//
// Format: MOVE ry, r32 MIPS16e
//Purpose: Move
// To move the contents of a GPR to a GPR.
//
def MoveR3216: FI8_MOVR3216_ins<"move", IIM16Alu> {
  let isMoveReg = 1;
}

//
// Format: MFHI rx MIPS16e
// Purpose: Move From HI Register
// To copy the special purpose HI register to a GPR.
//
def Mfhi16: FRR16_M_ins<0b10000, "mfhi", IIM16Alu> {
  let Uses = [HI0];
  let hasSideEffects = 0;
  let isMoveReg = 1;
}

//
// Format: MFLO rx MIPS16e
// Purpose: Move From LO Register
// To copy the special purpose LO register to a GPR.
//
def Mflo16: FRR16_M_ins<0b10010, "mflo", IIM16Alu> {
  let Uses = [LO0];
  let hasSideEffects = 0;
  let isMoveReg = 0;
}

//
// Pseudo Instruction for mult
//
def MultRxRy16:  FMULT16_ins<"mult",  IIM16Alu> {
  let isCommutable = 1;
  let hasSideEffects = 0;
  let Defs = [HI0, LO0];
}

def MultuRxRy16: FMULT16_ins<"multu", IIM16Alu> {
  let isCommutable = 1;
  let hasSideEffects = 0;
  let Defs = [HI0, LO0];
}

//
// Format: MULT rx, ry MIPS16e
// Purpose: Multiply Word
// To multiply 32-bit signed integers.
//
def MultRxRyRz16: FMULT16_LO_ins<"mult", IIM16Alu> {
  let isCommutable = 1;
  let hasSideEffects = 0;
  let Defs = [HI0, LO0];
}

//
// Format: MULTU rx, ry MIPS16e
// Purpose: Multiply Unsigned Word
// To multiply 32-bit unsigned integers.
//
def MultuRxRyRz16: FMULT16_LO_ins<"multu", IIM16Alu> {
  let isCommutable = 1;
  let hasSideEffects = 0;
  let Defs = [HI0, LO0];
}

//
// Format: NEG rx, ry MIPS16e
// Purpose: Negate
// To negate an integer value.
//
def NegRxRy16: FUnaryRR16_ins<0b11101, "neg", IIM16Alu>;

//
// Format: NOT rx, ry MIPS16e
// Purpose: Not
// To complement an integer value
//
def NotRxRy16: FUnaryRR16_ins<0b01111, "not", IIM16Alu>;

//
// Format: OR rx, ry MIPS16e
// Purpose: Or
// To do a bitwise logical OR.
//
def OrRxRxRy16: FRxRxRy16_ins<0b01101, "or", IIM16Alu>, ArithLogic16Defs<1>;

//
// Format: RESTORE {ra,}{s0/s1/s0-1,}{framesize}
// (All args are optional) MIPS16e
// Purpose: Restore Registers and Deallocate Stack Frame
// To deallocate a stack frame before exit from a subroutine,
// restoring return address and static registers, and adjusting
// stack
//

def Restore16:
  FI8_SVRS16<0b1, (outs), (ins variable_ops),
             "", [], II_RESTORE >, MayLoad {
  let isCodeGenOnly = 1;
  let Defs = [SP];
  let Uses = [SP];
}


def RestoreX16:
  FI8_SVRS16<0b1, (outs), (ins variable_ops),
             "", [], II_RESTORE >, MayLoad {
  let isCodeGenOnly = 1;
  let Defs = [SP];
  let Uses = [SP];
}

//
// Format: SAVE {ra,}{s0/s1/s0-1,}{framesize} (All arguments are optional)
// MIPS16e
// Purpose: Save Registers and Set Up Stack Frame
// To set up a stack frame on entry to a subroutine,
// saving return address and static registers, and adjusting stack
//
def Save16: 
  FI8_SVRS16<0b1, (outs), (ins variable_ops),
             "", [], II_SAVE >, MayStore {
  let isCodeGenOnly = 1;
  let Uses = [SP];
  let Defs = [SP];
}

def SaveX16:
  FI8_SVRS16<0b1, (outs), (ins variable_ops),
             "", [], II_SAVE >, MayStore {
  let isCodeGenOnly = 1;
  let Uses = [SP];
  let Defs = [SP];
}
//
// Format: SB ry, offset(rx) MIPS16e
// Purpose: Store Byte (Extended)
// To store a byte to memory.
//
def SbRxRyOffMemX16:
  FEXT_RRI16_mem2_ins<0b11000, "sb", mem16, II_SB>, MayStore;

//
// Format: SEB rx MIPS16e
// Purpose: Sign-Extend Byte
// Sign-extend least significant byte in register rx.
//
def SebRx16
  : FRR_SF16_ins<0b10001, 0b100, "seb", IIM16Alu>;

//
// Format: SEH rx MIPS16e
// Purpose: Sign-Extend Halfword
// Sign-extend least significant word in register rx.
//
def SehRx16
  : FRR_SF16_ins<0b10001, 0b101, "seh", IIM16Alu>;

//
// The Sel(T) instructions are pseudos
// T means that they use T8 implicitly.
//
//
// Format: SelBeqZ rd, rs, rt
// Purpose: if rt==0, do nothing
//          else rs = rt
//
def SelBeqZ: Sel<"beqz">;

//
// Format:  SelTBteqZCmp rd, rs, rl, rr
// Purpose: b = Cmp rl, rr.
//          If b==0 then do nothing.
//          if b!=0 then rd = rs
//
def SelTBteqZCmp: SelT<"bteqz", "cmp">;

//
// Format:  SelTBteqZCmpi rd, rs, rl, rr
// Purpose: b = Cmpi rl, imm.
//          If b==0 then do nothing.
//          if b!=0 then rd = rs
//
def SelTBteqZCmpi: SeliT<"bteqz", "cmpi">;

//
// Format:  SelTBteqZSlt rd, rs, rl, rr
// Purpose: b = Slt rl, rr.
//          If b==0 then do nothing.
//          if b!=0 then rd = rs
//
def SelTBteqZSlt: SelT<"bteqz", "slt">;

//
// Format:  SelTBteqZSlti rd, rs, rl, rr
// Purpose: b = Slti rl, imm.
//          If b==0 then do nothing.
//          if b!=0 then rd = rs
//
def SelTBteqZSlti: SeliT<"bteqz", "slti">;

//
// Format:  SelTBteqZSltu rd, rs, rl, rr
// Purpose: b = Sltu rl, rr.
//          If b==0 then do nothing.
//          if b!=0 then rd = rs
//
def SelTBteqZSltu: SelT<"bteqz", "sltu">;

//
// Format:  SelTBteqZSltiu rd, rs, rl, rr
// Purpose: b = Sltiu rl, imm.
//          If b==0 then do nothing.
//          if b!=0 then rd = rs
//
def SelTBteqZSltiu: SeliT<"bteqz", "sltiu">;

//
// Format: SelBnez rd, rs, rt
// Purpose: if rt!=0, do nothing
//          else rs = rt
//
def SelBneZ: Sel<"bnez">;

//
// Format:  SelTBtneZCmp rd, rs, rl, rr
// Purpose: b = Cmp rl, rr.
//          If b!=0 then do nothing.
//          if b0=0 then rd = rs
//
def SelTBtneZCmp: SelT<"btnez", "cmp">;

//
// Format:  SelTBtnezCmpi rd, rs, rl, rr
// Purpose: b = Cmpi rl, imm.
//          If b!=0 then do nothing.
//          if b==0 then rd = rs
//
def SelTBtneZCmpi: SeliT<"btnez", "cmpi">;

//
// Format:  SelTBtneZSlt rd, rs, rl, rr
// Purpose: b = Slt rl, rr.
//          If b!=0 then do nothing.
//          if b==0 then rd = rs
//
def SelTBtneZSlt: SelT<"btnez", "slt">;

//
// Format:  SelTBtneZSlti rd, rs, rl, rr
// Purpose: b = Slti rl, imm.
//          If b!=0 then do nothing.
//          if b==0 then rd = rs
//
def SelTBtneZSlti: SeliT<"btnez", "slti">;

//
// Format:  SelTBtneZSltu rd, rs, rl, rr
// Purpose: b = Sltu rl, rr.
//          If b!=0 then do nothing.
//          if b==0 then rd = rs
//
def SelTBtneZSltu: SelT<"btnez", "sltu">;

//
// Format:  SelTBtneZSltiu rd, rs, rl, rr
// Purpose: b = Slti rl, imm.
//          If b!=0 then do nothing.
//          if b==0 then rd = rs
//
def SelTBtneZSltiu: SeliT<"btnez", "sltiu">;
//
//
// Format: SH ry, offset(rx) MIPS16e
// Purpose: Store Halfword (Extended)
// To store a halfword to memory.
//
def ShRxRyOffMemX16:
  FEXT_RRI16_mem2_ins<0b11001, "sh", mem16, II_SH>, MayStore;

//
// Format: SLL rx, ry, sa MIPS16e
// Purpose: Shift Word Left Logical (Extended)
// To execute a left-shift of a word by a fixed number of bits-0 to 31 bits.
//
def SllX16: FEXT_SHIFT16_ins<0b00, "sll", IIM16Alu>;

//
// Format: SLLV ry, rx MIPS16e
// Purpose: Shift Word Left Logical Variable
// To execute a left-shift of a word by a variable number of bits.
//
def SllvRxRy16 : FRxRxRy16_ins<0b00100, "sllv", IIM16Alu>;

// Format: SLTI rx, immediate MIPS16e
// Purpose: Set on Less Than Immediate
// To record the result of a less-than comparison with a constant.
//
//
def SltiRxImm16: FRI16R_ins<0b01010, "slti", IIM16Alu> {
  let Defs = [T8];
}

//
// Format: SLTI rx, immediate MIPS16e
// Purpose: Set on Less Than Immediate (Extended)
// To record the result of a less-than comparison with a constant.
//
//
def SltiRxImmX16: FEXT_RI16R_ins<0b01010, "slti", IIM16Alu> {
  let Defs = [T8];
}

def SltiCCRxImmX16: FEXT_CCRXI16_ins<"slti">;

// Format: SLTIU rx, immediate MIPS16e
// Purpose: Set on Less Than Immediate Unsigned
// To record the result of a less-than comparison with a constant.
//
//
def SltiuRxImm16: FRI16R_ins<0b01011, "sltiu", IIM16Alu> {
  let Defs = [T8];
}

//
// Format: SLTI rx, immediate MIPS16e
// Purpose: Set on Less Than Immediate Unsigned (Extended)
// To record the result of a less-than comparison with a constant.
//
//
def SltiuRxImmX16: FEXT_RI16R_ins<0b01011, "sltiu", IIM16Alu> {
  let Defs = [T8];
}
//
// Format: SLTIU rx, immediate MIPS16e
// Purpose: Set on Less Than Immediate Unsigned (Extended)
// To record the result of a less-than comparison with a constant.
//
def SltiuCCRxImmX16: FEXT_CCRXI16_ins<"sltiu">;

//
// Format: SLT rx, ry MIPS16e
// Purpose: Set on Less Than
// To record the result of a less-than comparison.
//
def SltRxRy16: FRR16R_ins<0b00010, "slt", IIM16Alu>{
  let Defs = [T8];
}

def SltCCRxRy16: FCCRR16_ins<"slt">;

// Format: SLTU rx, ry MIPS16e
// Purpose: Set on Less Than Unsigned
// To record the result of an unsigned less-than comparison.
//
def SltuRxRy16: FRR16R_ins<0b00011, "sltu", IIM16Alu>{
  let Defs = [T8];
}

def SltuRxRyRz16: FRRTR16_ins<"sltu"> {
  let isCodeGenOnly=1;
  let Defs = [T8];
}


def SltuCCRxRy16: FCCRR16_ins<"sltu">;
//
// Format: SRAV ry, rx MIPS16e
// Purpose: Shift Word Right Arithmetic Variable
// To execute an arithmetic right-shift of a word by a variable
// number of bits.
//
def SravRxRy16: FRxRxRy16_ins<0b00111, "srav", IIM16Alu>;


//
// Format: SRA rx, ry, sa MIPS16e
// Purpose: Shift Word Right Arithmetic (Extended)
// To execute an arithmetic right-shift of a word by a fixed
// number of bits-1 to 8 bits.
//
def SraX16: FEXT_SHIFT16_ins<0b11, "sra", IIM16Alu>;


//
// Format: SRLV ry, rx MIPS16e
// Purpose: Shift Word Right Logical Variable
// To execute a logical right-shift of a word by a variable
// number of bits.
//
def SrlvRxRy16: FRxRxRy16_ins<0b00110, "srlv", IIM16Alu>;


//
// Format: SRL rx, ry, sa MIPS16e
// Purpose: Shift Word Right Logical (Extended)
// To execute a logical right-shift of a word by a fixed
// number of bits-1 to 31 bits.
//
def SrlX16: FEXT_SHIFT16_ins<0b10, "srl", IIM16Alu>;

//
// Format: SUBU rz, rx, ry MIPS16e
// Purpose: Subtract Unsigned Word
// To subtract 32-bit integers
//
def SubuRxRyRz16: FRRR16_ins<0b11, "subu", IIM16Alu>, ArithLogic16Defs<0>;

//
// Format: SW ry, offset(rx) MIPS16e
// Purpose: Store Word (Extended)
// To store a word to memory.
//
def SwRxRyOffMemX16: FEXT_RRI16_mem2_ins<0b11011, "sw", mem16, II_SW>, MayStore;

//
// Format: SW rx, offset(sp) MIPS16e
// Purpose: Store Word rx (SP-Relative)
// To store an SP-relative word to memory.
//
def SwRxSpImmX16: FEXT_RRI16_mem2_ins<0b11010, "sw", mem16sp, II_SW>, MayStore;

//
//
// Format: XOR rx, ry MIPS16e
// Purpose: Xor
// To do a bitwise logical XOR.
//
def XorRxRxRy16: FRxRxRy16_ins<0b01110, "xor", IIM16Alu>, ArithLogic16Defs<1>;

class Mips16Pat<dag pattern, dag result> : Pat<pattern, result> {
  let Predicates = [InMips16Mode];
}

// Unary Arith/Logic
//
class ArithLogicU_pat<PatFrag OpNode, Instruction I> :
  Mips16Pat<(OpNode CPU16Regs:$r),
            (I CPU16Regs:$r)>;

def: ArithLogicU_pat<not, NotRxRy16>;
def: ArithLogicU_pat<ineg, NegRxRy16>;

class ArithLogic16_pat<SDNode OpNode, Instruction I> :
  Mips16Pat<(OpNode CPU16Regs:$l, CPU16Regs:$r),
            (I CPU16Regs:$l, CPU16Regs:$r)>;

def: ArithLogic16_pat<add, AdduRxRyRz16>;
def: ArithLogic16_pat<and, AndRxRxRy16>;
def: ArithLogic16_pat<mul, MultRxRyRz16>;
def: ArithLogic16_pat<or, OrRxRxRy16>;
def: ArithLogic16_pat<sub, SubuRxRyRz16>;
def: ArithLogic16_pat<xor, XorRxRxRy16>;

// Arithmetic and logical instructions with 2 register operands.

class ArithLogicI16_pat<SDNode OpNode, PatFrag imm_type, Instruction I> :
  Mips16Pat<(OpNode CPU16Regs:$in, imm_type:$imm),
            (I CPU16Regs:$in, imm_type:$imm)>;

def: ArithLogicI16_pat<add, immSExt8, AddiuRxRxImm16>;
def: ArithLogicI16_pat<add, immSExt16, AddiuRxRxImmX16>;
def: ArithLogicI16_pat<shl, immZExt5, SllX16>;
def: ArithLogicI16_pat<srl, immZExt5, SrlX16>;
def: ArithLogicI16_pat<sra, immZExt5, SraX16>;

class shift_rotate_reg16_pat<SDNode OpNode, Instruction I> :
  Mips16Pat<(OpNode CPU16Regs:$r, CPU16Regs:$ra),
            (I CPU16Regs:$r, CPU16Regs:$ra)>;

def: shift_rotate_reg16_pat<shl, SllvRxRy16>;
def: shift_rotate_reg16_pat<sra, SravRxRy16>;
def: shift_rotate_reg16_pat<srl, SrlvRxRy16>;

class LoadM16_pat<PatFrag OpNode, Instruction I, ComplexPattern Addr> :
  Mips16Pat<(OpNode Addr:$addr), (I Addr:$addr)>;

def: LoadM16_pat<sextloadi8, LbRxRyOffMemX16, addr16>;
def: LoadM16_pat<zextloadi8, LbuRxRyOffMemX16, addr16>;
def: LoadM16_pat<sextloadi16, LhRxRyOffMemX16, addr16>;
def: LoadM16_pat<zextloadi16, LhuRxRyOffMemX16, addr16>;
def: LoadM16_pat<load, LwRxSpImmX16, addr16sp>;

class StoreM16_pat<PatFrag OpNode, Instruction I, ComplexPattern Addr> :
  Mips16Pat<(OpNode CPU16Regs:$r, Addr:$addr), (I CPU16Regs:$r, Addr:$addr)>;

def: StoreM16_pat<truncstorei8, SbRxRyOffMemX16, addr16>;
def: StoreM16_pat<truncstorei16, ShRxRyOffMemX16, addr16>;
def: StoreM16_pat<store, SwRxSpImmX16, addr16sp>;

// Unconditional branch
class UncondBranch16_pat<SDNode OpNode, Instruction I>:
  Mips16Pat<(OpNode bb:$imm16), (I bb:$imm16)> {
    let Predicates = [InMips16Mode];
  }

def : Mips16Pat<(MipsJmpLink (i32 tglobaladdr:$dst)),
                (Jal16 tglobaladdr:$dst)>;

def : Mips16Pat<(MipsJmpLink (i32 texternalsym:$dst)),
                (Jal16 texternalsym:$dst)>;

// Indirect branch
def: Mips16Pat<(brind CPU16Regs:$rs), (JrcRx16 CPU16Regs:$rs)> {
  // Ensure that the addition of MIPS32r6/MIPS64r6 support does not change
  // MIPS16's behaviour.
  let AddedComplexity = 1;
}

// Jump and Link (Call)
let isCall=1, hasDelaySlot=0 in
def JumpLinkReg16:
  FRR16_JALRC<0, 0, 0, (outs), (ins CPU16Regs:$rs),
              "jalrc\t$rs", [(MipsJmpLink CPU16Regs:$rs)], II_JALRC> {
  let Defs = [RA];
}

// Mips16 pseudos
let isReturn=1, isTerminator=1, hasDelaySlot=1, isBarrier=1, hasCtrlDep=1,
  hasExtraSrcRegAllocReq = 1 in
def RetRA16 : MipsPseudo16<(outs), (ins), "", [(MipsRet)]>;


// setcc patterns

class SetCC_R16<PatFrag cond_op, Instruction I>:
  Mips16Pat<(cond_op CPU16Regs:$rx, CPU16Regs:$ry),
            (I CPU16Regs:$rx, CPU16Regs:$ry)>;

class SetCC_I16<PatFrag cond_op, PatLeaf imm_type, Instruction I>:
  Mips16Pat<(cond_op CPU16Regs:$rx, imm_type:$imm16),
            (I CPU16Regs:$rx, imm_type:$imm16)>;


def: Mips16Pat<(i32 addr16sp:$addr), (AddiuRxRyOffMemX16 addr16sp:$addr)>;


// Large (>16 bit) immediate loads
def : Mips16Pat<(i32 imm:$imm), (LwConstant32 imm:$imm, -1)>;

//
// Some branch conditional patterns are not generated by llvm at this time.
// Some are for seemingly arbitrary reasons not used: i.e. with signed number
// comparison they are used and for unsigned a different pattern is used.
// I am pushing upstream from the full mips16 port and it seemed that I needed
// these earlier and the mips32 port has these but now I cannot create test
// cases that use these patterns. While I sort this all out I will leave these
// extra patterns commented out and if I can be sure they are really not used,
// I will delete the code. I don't want to check the code in uncommented without
// a valid test case. In some cases, the compiler is generating patterns with
// setcc instead and earlier I had implemented setcc first so may have masked
// the problem. The setcc variants are suboptimal for mips16 so I may wantto
// figure out how to enable the brcond patterns or else possibly new
// combinations of brcond and setcc.
//
//
// bcond-seteq
//
def: Mips16Pat
  <(brcond (i32 (seteq CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
   (BteqzT8CmpX16 CPU16Regs:$rx, CPU16Regs:$ry,  bb:$imm16)
  >;


def: Mips16Pat
  <(brcond (i32 (seteq CPU16Regs:$rx, immZExt16:$imm)), bb:$targ16),
   (BteqzT8CmpiX16 CPU16Regs:$rx, immSExt16:$imm,  bb:$targ16)
  >;

def: Mips16Pat
  <(brcond (i32 (seteq CPU16Regs:$rx, 0)), bb:$targ16),
   (BeqzRxImm16 CPU16Regs:$rx, bb:$targ16)
  >;

//
// bcond-setgt (do we need to have this pair of setlt, setgt??)
//
def: Mips16Pat
  <(brcond (i32 (setgt CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
   (BtnezT8SltX16 CPU16Regs:$ry, CPU16Regs:$rx,  bb:$imm16)
  >;

//
// bcond-setge
//
def: Mips16Pat
  <(brcond (i32 (setge CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
   (BteqzT8SltX16 CPU16Regs:$rx, CPU16Regs:$ry,  bb:$imm16)
  >;

//
// never called because compiler transforms a >= k to a > (k-1)
def: Mips16Pat
  <(brcond (i32 (setge CPU16Regs:$rx, immSExt16:$imm)), bb:$imm16),
   (BteqzT8SltiX16 CPU16Regs:$rx, immSExt16:$imm,  bb:$imm16)
  >;

//
// bcond-setlt
//
def: Mips16Pat
  <(brcond (i32 (setlt CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
   (BtnezT8SltX16 CPU16Regs:$rx, CPU16Regs:$ry,  bb:$imm16)
  >;

def: Mips16Pat
  <(brcond (i32 (setlt CPU16Regs:$rx, immSExt16:$imm)), bb:$imm16),
   (BtnezT8SltiX16 CPU16Regs:$rx, immSExt16:$imm,  bb:$imm16)
  >;

//
// bcond-setle
//
def: Mips16Pat
  <(brcond (i32 (setle CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
   (BteqzT8SltX16 CPU16Regs:$ry, CPU16Regs:$rx,  bb:$imm16)
  >;

//
// bcond-setne
//
def: Mips16Pat
  <(brcond (i32 (setne CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
   (BtnezT8CmpX16 CPU16Regs:$rx, CPU16Regs:$ry,  bb:$imm16)
  >;

def: Mips16Pat
  <(brcond (i32 (setne CPU16Regs:$rx, immZExt16:$imm)), bb:$targ16),
   (BtnezT8CmpiX16 CPU16Regs:$rx, immSExt16:$imm,  bb:$targ16)
  >;

def: Mips16Pat
  <(brcond (i32 (setne CPU16Regs:$rx, 0)), bb:$targ16),
   (BnezRxImm16 CPU16Regs:$rx, bb:$targ16)
  >;

//
// This needs to be there but I forget which code will generate it
//
def: Mips16Pat
  <(brcond CPU16Regs:$rx, bb:$targ16),
   (BnezRxImm16 CPU16Regs:$rx, bb:$targ16)
  >;

//

//
// bcond-setugt
//
//def: Mips16Pat
//  <(brcond (i32 (setugt CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
//   (BtnezT8SltuX16 CPU16Regs:$ry, CPU16Regs:$rx,  bb:$imm16)
//  >;

//
// bcond-setuge
//
//def: Mips16Pat
//  <(brcond (i32 (setuge CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
//   (BteqzT8SltuX16 CPU16Regs:$rx, CPU16Regs:$ry,  bb:$imm16)
//  >;


//
// bcond-setult
//
//def: Mips16Pat
//  <(brcond (i32 (setult CPU16Regs:$rx, CPU16Regs:$ry)), bb:$imm16),
//   (BtnezT8SltuX16 CPU16Regs:$rx, CPU16Regs:$ry,  bb:$imm16)
//  >;

def: UncondBranch16_pat<br, Bimm16>;

// Small immediates
def: Mips16Pat<(i32 immSExt16:$in),
               (AddiuRxRxImmX16 (MoveR3216 ZERO), immSExt16:$in)>;

def: Mips16Pat<(i32 immZExt16:$in), (LiRxImmX16 immZExt16:$in)>;

//
// MipsDivRem
//
def: Mips16Pat
  <(MipsDivRem16 CPU16Regs:$rx, CPU16Regs:$ry),
   (DivRxRy16 CPU16Regs:$rx, CPU16Regs:$ry)>;

//
// MipsDivRemU
//
def: Mips16Pat
  <(MipsDivRemU16 CPU16Regs:$rx, CPU16Regs:$ry),
   (DivuRxRy16 CPU16Regs:$rx, CPU16Regs:$ry)>;

//  signed a,b
//  x = (a>=b)?x:y
//
//  if !(a < b) x = y
//
def : Mips16Pat<(select (i32 (setge CPU16Regs:$a, CPU16Regs:$b)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBteqZSlt CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$a, CPU16Regs:$b)>;

//  signed a,b
//  x = (a>b)?x:y
//
//  if  (b < a) x = y
//
def : Mips16Pat<(select (i32 (setgt CPU16Regs:$a, CPU16Regs:$b)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBtneZSlt CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$b, CPU16Regs:$a)>;

// unsigned a,b
// x = (a>=b)?x:y
//
// if !(a < b) x = y;
//
def : Mips16Pat<
  (select (i32 (setuge CPU16Regs:$a, CPU16Regs:$b)),
   CPU16Regs:$x, CPU16Regs:$y),
  (SelTBteqZSltu CPU16Regs:$x, CPU16Regs:$y,
   CPU16Regs:$a, CPU16Regs:$b)>;

//  unsigned a,b
//  x = (a>b)?x:y
//
//  if (b < a) x = y
//
def : Mips16Pat<(select (i32 (setugt CPU16Regs:$a, CPU16Regs:$b)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBtneZSltu CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$b, CPU16Regs:$a)>;

// signed
// x = (a >= k)?x:y
// due to an llvm optimization, i don't think that this will ever
// be used. This is transformed into x = (a > k-1)?x:y
//
//

//def : Mips16Pat<
//  (select (i32 (setge CPU16Regs:$lhs, immSExt16:$rhs)),
//   CPU16Regs:$T, CPU16Regs:$F),
//  (SelTBteqZSlti CPU16Regs:$T, CPU16Regs:$F,
//   CPU16Regs:$lhs, immSExt16:$rhs)>;

//def : Mips16Pat<
//  (select (i32 (setuge CPU16Regs:$lhs, immSExt16:$rhs)),
//   CPU16Regs:$T, CPU16Regs:$F),
//  (SelTBteqZSltiu CPU16Regs:$T, CPU16Regs:$F,
//   CPU16Regs:$lhs, immSExt16:$rhs)>;

// signed
// x = (a < k)?x:y
//
// if !(a < k) x = y;
//
def : Mips16Pat<
  (select (i32 (setlt CPU16Regs:$a, immSExt16:$b)),
   CPU16Regs:$x, CPU16Regs:$y),
  (SelTBtneZSlti CPU16Regs:$x, CPU16Regs:$y,
   CPU16Regs:$a, immSExt16:$b)>;


//
//
// signed
// x = (a <= b)? x : y
//
// if  (b < a) x = y
//
def : Mips16Pat<(select (i32 (setle CPU16Regs:$a, CPU16Regs:$b)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBteqZSlt CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$b, CPU16Regs:$a)>;

//
// unnsigned
// x = (a <= b)? x : y
//
// if  (b < a) x = y
//
def : Mips16Pat<(select (i32 (setule CPU16Regs:$a, CPU16Regs:$b)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBteqZSltu CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$b, CPU16Regs:$a)>;

//
// signed/unsigned
// x = (a == b)? x : y
//
// if (a != b) x = y
//
def : Mips16Pat<(select (i32 (seteq CPU16Regs:$a, CPU16Regs:$b)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBteqZCmp CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$b, CPU16Regs:$a)>;

//
// signed/unsigned
// x = (a == 0)? x : y
//
// if (a != 0) x = y
//
def : Mips16Pat<(select (i32 (seteq CPU16Regs:$a, 0)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelBeqZ CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$a)>;


//
// signed/unsigned
// x = (a == k)? x : y
//
// if (a != k) x = y
//
def : Mips16Pat<(select (i32 (seteq CPU16Regs:$a, immZExt16:$k)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBteqZCmpi CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$a, immZExt16:$k)>;


//
// signed/unsigned
// x = (a != b)? x : y
//
// if (a == b) x = y
//
//
def : Mips16Pat<(select (i32 (setne CPU16Regs:$a, CPU16Regs:$b)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBtneZCmp CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$b, CPU16Regs:$a)>;

//
// signed/unsigned
// x = (a != 0)? x : y
//
// if (a == 0) x = y
//
def : Mips16Pat<(select (i32 (setne CPU16Regs:$a, 0)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelBneZ CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$a)>;

// signed/unsigned
// x = (a)? x : y
//
// if (!a) x = y
//
def : Mips16Pat<(select  CPU16Regs:$a,
                 CPU16Regs:$x, CPU16Regs:$y),
      (SelBneZ CPU16Regs:$x, CPU16Regs:$y,
       CPU16Regs:$a)>;


//
// signed/unsigned
// x = (a != k)? x : y
//
// if (a == k) x = y
//
def : Mips16Pat<(select (i32 (setne CPU16Regs:$a, immZExt16:$k)),
                 CPU16Regs:$x, CPU16Regs:$y),
                (SelTBtneZCmpi CPU16Regs:$x, CPU16Regs:$y,
                 CPU16Regs:$a, immZExt16:$k)>;

//
// When writing C code to test setxx these patterns,
// some will be transformed into
// other things. So we test using C code but using -O3 and -O0
//
// seteq
//
def : Mips16Pat
  <(seteq CPU16Regs:$lhs,CPU16Regs:$rhs),
   (SltiuCCRxImmX16 (XorRxRxRy16 CPU16Regs:$lhs, CPU16Regs:$rhs), 1)>;

def : Mips16Pat
  <(seteq CPU16Regs:$lhs, 0),
   (SltiuCCRxImmX16 CPU16Regs:$lhs, 1)>;


//
// setge
//

def: Mips16Pat
  <(setge CPU16Regs:$lhs, CPU16Regs:$rhs),
   (XorRxRxRy16 (SltCCRxRy16 CPU16Regs:$lhs, CPU16Regs:$rhs),
   (LiRxImmX16 1))>;

//
// For constants, llvm transforms this to:
// x > (k - 1) and then reverses the operands to use setlt. So this pattern
// is not used now by the compiler. (Presumably checking that k-1 does not
// overflow). The compiler never uses this at the current time, due to
// other optimizations.
//
//def: Mips16Pat
//  <(setge CPU16Regs:$lhs, immSExt16:$rhs),
//   (XorRxRxRy16 (SltiCCRxImmX16 CPU16Regs:$lhs, immSExt16:$rhs),
//   (LiRxImmX16 1))>;

// This catches the x >= -32768 case by transforming it to  x > -32769
//
def: Mips16Pat
  <(setgt CPU16Regs:$lhs, -32769),
   (XorRxRxRy16 (SltiCCRxImmX16 CPU16Regs:$lhs, -32768),
   (LiRxImmX16 1))>;

//
// setgt
//
//

def: Mips16Pat
  <(setgt CPU16Regs:$lhs, CPU16Regs:$rhs),
   (SltCCRxRy16 CPU16Regs:$rhs, CPU16Regs:$lhs)>;

//
// setle
//
def: Mips16Pat
  <(setle CPU16Regs:$lhs, CPU16Regs:$rhs),
   (XorRxRxRy16 (SltCCRxRy16 CPU16Regs:$rhs, CPU16Regs:$lhs), (LiRxImm16 1))>;

//
// setlt
//
def: SetCC_R16<setlt, SltCCRxRy16>;

def: SetCC_I16<setlt, immSExt16, SltiCCRxImmX16>;

//
// setne
//
def : Mips16Pat
  <(setne CPU16Regs:$lhs,CPU16Regs:$rhs),
   (SltuCCRxRy16 (LiRxImmX16 0),
   (XorRxRxRy16 CPU16Regs:$lhs, CPU16Regs:$rhs))>;


//
// setuge
//
def: Mips16Pat
  <(setuge CPU16Regs:$lhs, CPU16Regs:$rhs),
   (XorRxRxRy16 (SltuCCRxRy16 CPU16Regs:$lhs, CPU16Regs:$rhs),
   (LiRxImmX16 1))>;

// this pattern will never be used because the compiler will transform
// x >= k to x > (k - 1) and then use SLT
//
//def: Mips16Pat
//  <(setuge CPU16Regs:$lhs, immZExt16:$rhs),
//   (XorRxRxRy16 (SltiuCCRxImmX16 CPU16Regs:$lhs, immZExt16:$rhs),
//   (LiRxImmX16 1))>;

//
// setugt
//
def: Mips16Pat
  <(setugt CPU16Regs:$lhs, CPU16Regs:$rhs),
   (SltuCCRxRy16 CPU16Regs:$rhs, CPU16Regs:$lhs)>;

//
// setule
//
def: Mips16Pat
  <(setule CPU16Regs:$lhs, CPU16Regs:$rhs),
   (XorRxRxRy16 (SltuCCRxRy16 CPU16Regs:$rhs, CPU16Regs:$lhs), (LiRxImmX16 1))>;

//
// setult
//
def: SetCC_R16<setult, SltuCCRxRy16>;

def: SetCC_I16<setult, immSExt16, SltiuCCRxImmX16>;

def: Mips16Pat<(add CPU16Regs:$hi, (MipsLo tglobaladdr:$lo)),
               (AddiuRxRxImmX16 CPU16Regs:$hi, tglobaladdr:$lo)>;

// hi/lo relocs
def : Mips16Pat<(MipsHi tblockaddress:$in),
                (SllX16 (LiRxImmX16 tblockaddress:$in), 16)>;
def : Mips16Pat<(MipsHi tglobaladdr:$in),
                (SllX16 (LiRxImmX16 tglobaladdr:$in), 16)>;
def : Mips16Pat<(MipsHi tjumptable:$in),
                (SllX16 (LiRxImmX16 tjumptable:$in), 16)>;

def : Mips16Pat<(MipsLo tblockaddress:$in), (LiRxImmX16 tblockaddress:$in)>;

def : Mips16Pat<(MipsTlsHi tglobaltlsaddr:$in),
                (SllX16 (LiRxImmX16 tglobaltlsaddr:$in), 16)>;

// wrapper_pic
class Wrapper16Pat<SDNode node, Instruction ADDiuOp, RegisterClass RC>:
  Mips16Pat<(MipsWrapper RC:$gp, node:$in),
            (ADDiuOp RC:$gp, node:$in)>;


def : Wrapper16Pat<tglobaladdr, AddiuRxRxImmX16, CPU16Regs>;
def : Wrapper16Pat<tglobaltlsaddr, AddiuRxRxImmX16, CPU16Regs>;

def : Mips16Pat<(i32 (extloadi8   addr16:$src)),
                (LbuRxRyOffMemX16  addr16:$src)>;
def : Mips16Pat<(i32 (extloadi16  addr16:$src)),
                (LhuRxRyOffMemX16  addr16:$src)>;

def: Mips16Pat<(trap), (Break16)>;

def : Mips16Pat<(sext_inreg CPU16Regs:$val, i8),
                (SebRx16 CPU16Regs:$val)>;

def : Mips16Pat<(sext_inreg CPU16Regs:$val, i16),
                (SehRx16 CPU16Regs:$val)>;

def GotPrologue16:   
  MipsPseudo16<
    (outs CPU16Regs:$rh, CPU16Regs:$rl),
    (ins simm16:$immHi, simm16:$immLo),
    "li\t$rh, $immHi\n\taddiu\t$rl, $$pc, $immLo\n ",[]> ;

// An operand for the CONSTPOOL_ENTRY pseudo-instruction.
def cpinst_operand : Operand<i32> {
  // let PrintMethod = "printCPInstOperand";
}

// CONSTPOOL_ENTRY - This instruction represents a floating constant pool in
// the function.  The first operand is the ID# for this instruction, the second
// is the index into the MachineConstantPool that this is, the third is the
// size in bytes of this constant pool entry.
//
let hasSideEffects = 0, isNotDuplicable = 1 in
def CONSTPOOL_ENTRY :
MipsPseudo16<(outs), (ins cpinst_operand:$instid, cpinst_operand:$cpidx,
                      i32imm:$size), "foo", []>;

// Instruction Aliases

let EncodingPredicates = [InMips16Mode] in
def : MipsInstAlias<"nop", (Move32R16 ZERO, S0)>;
