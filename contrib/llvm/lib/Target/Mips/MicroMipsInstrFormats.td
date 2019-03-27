//===-- MicroMipsInstrFormats.td - microMIPS Inst Formats -*- tablegen -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This files descributes the formats of the microMIPS instruction set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MicroMIPS Base Classes
//===----------------------------------------------------------------------===//

//
// Base class for MicroMips instructions.
// This class does not depend on the instruction size.
//
class MicroMipsInstBase<dag outs, dag ins, string asmstr, list<dag> pattern,
                        InstrItinClass itin, Format f> : Instruction,
                        PredicateControl {
  let Namespace = "Mips";
  let DecoderNamespace = "MicroMips";

  let OutOperandList = outs;
  let InOperandList  = ins;

  let AsmString   = asmstr;
  let Pattern     = pattern;
  let Itinerary   = itin;

  let EncodingPredicates = [InMicroMips];

  Format Form = f;
}

//
// Base class for MicroMIPS 16-bit instructions.
//
class MicroMipsInst16<dag outs, dag ins, string asmstr, list<dag> pattern,
               InstrItinClass itin, Format f> :
  MicroMipsInstBase<outs, ins, asmstr, pattern, itin, f>
{
  let Size = 2;
  field bits<16> Inst;
  field bits<16> SoftFail = 0;
  bits<6> Opcode = 0x0;
}

//===----------------------------------------------------------------------===//
// MicroMIPS 16-bit Instruction Formats
//===----------------------------------------------------------------------===//

class ARITH_FM_MM16<bit funct> {
  bits<3> rd;
  bits<3> rt;
  bits<3> rs;

  bits<16> Inst;

  let Inst{15-10} = 0x01;
  let Inst{9-7}   = rd;
  let Inst{6-4}   = rt;
  let Inst{3-1}   = rs;
  let Inst{0}     = funct;
}

class ANDI_FM_MM16<bits<6> funct> {
  bits<3> rd;
  bits<3> rs;
  bits<4> imm;

  bits<16> Inst;

  let Inst{15-10} = funct;
  let Inst{9-7}   = rd;
  let Inst{6-4}   = rs;
  let Inst{3-0}   = imm;
}

class LOGIC_FM_MM16<bits<4> funct> {
  bits<3> rt;
  bits<3> rs;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-6}   = funct;
  let Inst{5-3}   = rt;
  let Inst{2-0}   = rs;
}

class SHIFT_FM_MM16<bits<1> funct> {
  bits<3> rd;
  bits<3> rt;
  bits<3> shamt;

  bits<16> Inst;

  let Inst{15-10} = 0x09;
  let Inst{9-7}   = rd;
  let Inst{6-4}   = rt;
  let Inst{3-1}   = shamt;
  let Inst{0}     = funct;
}

class ADDIUR2_FM_MM16 {
  bits<3> rd;
  bits<3> rs;
  bits<3> imm;

  bits<16> Inst;

  let Inst{15-10} = 0x1b;
  let Inst{9-7}   = rd;
  let Inst{6-4}   = rs;
  let Inst{3-1}   = imm;
  let Inst{0}     = 0;
}

class LOAD_STORE_FM_MM16<bits<6> op> {
  bits<3> rt;
  bits<7> addr;

  bits<16> Inst;

  let Inst{15-10} = op;
  let Inst{9-7}   = rt;
  let Inst{6-4}   = addr{6-4};
  let Inst{3-0}   = addr{3-0};
}

class LOAD_STORE_SP_FM_MM16<bits<6> op> {
  bits<5> rt;
  bits<5> offset;

  bits<16> Inst;

  let Inst{15-10} = op;
  let Inst{9-5}   = rt;
  let Inst{4-0}   = offset;
}

class LOAD_GP_FM_MM16<bits<6> op> {
  bits<3> rt;
  bits<7> offset;

  bits<16> Inst;

  let Inst{15-10} = op;
  let Inst{9-7} = rt;
  let Inst{6-0} = offset;
}

class ADDIUS5_FM_MM16 {
  bits<5> rd;
  bits<4> imm;

  bits<16> Inst;

  let Inst{15-10} = 0x13;
  let Inst{9-5}   = rd;
  let Inst{4-1}   = imm;
  let Inst{0}     = 0;
}

class ADDIUSP_FM_MM16 {
  bits<9> imm;

  bits<16> Inst;

  let Inst{15-10} = 0x13;
  let Inst{9-1}   = imm;
  let Inst{0}     = 1;
}

class MOVE_FM_MM16<bits<6> funct> {
  bits<5> rs;
  bits<5> rd;

  bits<16> Inst;

  let Inst{15-10} = funct;
  let Inst{9-5}   = rd;
  let Inst{4-0}   = rs;
}

class LI_FM_MM16 {
  bits<3> rd;
  bits<7> imm;

  bits<16> Inst;

  let Inst{15-10} = 0x3b;
  let Inst{9-7}   = rd;
  let Inst{6-0}   = imm;
}

class JALR_FM_MM16<bits<5> op> {
  bits<5> rs;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-5}   = op;
  let Inst{4-0}   = rs;
}

class MFHILO_FM_MM16<bits<5> funct> {
  bits<5> rd;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-5}   = funct;
  let Inst{4-0}   = rd;
}

class JRADDIUSP_FM_MM16<bits<5> op> {
  bits<5> rs;
  bits<5> imm;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-5}   = op;
  let Inst{4-0}   = imm;
}

class ADDIUR1SP_FM_MM16 {
  bits<3> rd;
  bits<6> imm;

  bits<16> Inst;

  let Inst{15-10} = 0x1b;
  let Inst{9-7}   = rd;
  let Inst{6-1}   = imm;
  let Inst{0}     = 1;
}

class BRKSDBBP16_FM_MM<bits<6> op> {
  bits<4> code_;
  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-4}   = op;
  let Inst{3-0}   = code_;
}

class BEQNEZ_FM_MM16<bits<6> op> {
  bits<3> rs;
  bits<7> offset;

  bits<16> Inst;

  let Inst{15-10} = op;
  let Inst{9-7}   = rs;
  let Inst{6-0}   = offset;
}

class B16_FM {
  bits<10> offset;

  bits<16> Inst;

  let Inst{15-10} = 0x33;
  let Inst{9-0}   = offset;
}

class MOVEP_FM_MM16 {
  bits<3> dst_regs;
  bits<3> rt;
  bits<3> rs;

  bits<16> Inst;

  let Inst{15-10} = 0x21;
  let Inst{9-7}   = dst_regs;
  let Inst{6-4}   = rt;
  let Inst{3-1}   = rs;
  let Inst{0}     = 0;
}

//===----------------------------------------------------------------------===//
// MicroMIPS 32-bit Instruction Formats
//===----------------------------------------------------------------------===//

class MMArch {
  string Arch = "micromips";
}

class ADD_FM_MM<bits<6> op, bits<10> funct> : MMArch {
  bits<5> rt;
  bits<5> rs;
  bits<5> rd;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10}    = 0;
  let Inst{9-0}   = funct;
}

class ADDI_FM_MM<bits<6> op> : MMArch {
  bits<5>  rs;
  bits<5>  rt;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0}  = imm16;
}

class SLTI_FM_MM<bits<6> op> : MMArch {
  bits<5> rt;
  bits<5> rs;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0}  = imm16;
}

class LUI_FM_MM : MMArch {
  bits<5> rt;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = 0x10;
  let Inst{25-21} = 0xd;
  let Inst{20-16} = rt;
  let Inst{15-0}  = imm16;
}

class MULT_FM_MM<bits<10> funct> : MMArch {
  bits<5>  rs;
  bits<5>  rt;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class SRA_FM_MM<bits<10> funct, bit rotate> : MMArch {
  bits<5> rd;
  bits<5> rt;
  bits<5> shamt;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rd;
  let Inst{20-16} = rt;
  let Inst{15-11} = shamt;
  let Inst{10}    = rotate;
  let Inst{9-0}   = funct;
}

class SRLV_FM_MM<bits<10> funct, bit rotate> : MMArch {
  bits<5> rd;
  bits<5> rt;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10}    = rotate;
  let Inst{9-0}   = funct;
}

class LW_FM_MM<bits<6> op> : MMArch {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-0}  = offset;
}

class POOL32C_LHUE_FM_MM<bits<6> op, bits<4> fmt, bits<3> funct> : MMArch {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<9> offset = addr{8-0};

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-12} = fmt;
  let Inst{11-9} = funct;
  let Inst{8-0}  = offset;
}

class LWL_FM_MM<bits<4> funct> : MMArch {
  bits<5> rt;
  bits<21> addr;

  bits<32> Inst;

  let Inst{31-26} = 0x18;
  let Inst{25-21} = rt;
  let Inst{20-16} = addr{20-16};
  let Inst{15-12} = funct;
  let Inst{11-0}  = addr{11-0};
}

class POOL32C_STEVA_LDEVA_FM_MM<bits<4> type, bits<3> funct> : MMArch {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<9> offset = addr{8-0};

  bits<32> Inst;

  let Inst{31-26} = 0x18;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-12} = type;
  let Inst{11-9} = funct;
  let Inst{8-0}  = offset;
}

class CMov_F_I_FM_MM<bits<7> func> : MMArch {
  bits<5> rd;
  bits<5> rs;
  bits<3> fcc;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = rd;
  let Inst{20-16} = rs;
  let Inst{15-13} = fcc;
  let Inst{12-6}  = func;
  let Inst{5-0}   = 0x3b;
}

class MTLO_FM_MM<bits<10> funct> : MMArch {
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = 0x00;
  let Inst{20-16} = rs;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class MFLO_FM_MM<bits<10> funct> : MMArch {
  bits<5> rd;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = 0x00;
  let Inst{20-16} = rd;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class CLO_FM_MM<bits<10> funct> : MMArch {
  bits<5> rd;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = rd;
  let Inst{20-16} = rs;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class SEB_FM_MM<bits<10> funct> : MMArch {
  bits<5> rd;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = rd;
  let Inst{20-16} = rt;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class EXT_FM_MM<bits<6> funct> : MMArch {
  bits<5> rt;
  bits<5> rs;
  bits<5> pos;
  bits<5> size;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = size;
  let Inst{10-6}  = pos;
  let Inst{5-0}   = funct;
}

class J_FM_MM<bits<6> op> : MMArch {
  bits<26> target;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-0}  = target;
}

class JR_FM_MM<bits<8> funct> : MMArch {
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-21} = 0x00;
  let Inst{20-16} = rs;
  let Inst{15-14} = 0x0;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class JALR_FM_MM<bits<10> funct> {
  bits<5> rs;
  bits<5> rd;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = rd;
  let Inst{20-16} = rs;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class BEQ_FM_MM<bits<6> op> : MMArch {
  bits<5>  rs;
  bits<5>  rt;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0}  = offset;
}

class BGEZ_FM_MM<bits<5> funct> : MMArch {
  bits<5>  rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0x10;
  let Inst{25-21} = funct;
  let Inst{20-16} = rs;
  let Inst{15-0}  = offset;
}

class BGEZAL_FM_MM<bits<5> funct> : MMArch {
  bits<5>  rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0x10;
  let Inst{25-21} = funct;
  let Inst{20-16} = rs;
  let Inst{15-0}  = offset;
}

class SYNC_FM_MM : MMArch {
  bits<5> stype;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = 0x0;
  let Inst{20-16} = stype;
  let Inst{15-6}  = 0x1ad;
  let Inst{5-0}   = 0x3c;
}

class SYNCI_FM_MM : MMArch {
  bits<21> addr;
  bits<5> rs = addr{20-16};
  bits<16> offset = addr{15-0};
  bits<32> Inst;

  let Inst{31-26} = 0b010000;
  let Inst{25-21} = 0b10000;
  let Inst{20-16} = rs;
  let Inst{15-0}  = offset;
}

class BRK_FM_MM : MMArch {
  bits<10> code_1;
  bits<10> code_2;
  bits<32> Inst;
  let Inst{31-26} = 0x0;
  let Inst{25-16} = code_1;
  let Inst{15-6}  = code_2;
  let Inst{5-0}   = 0x07;
}

class SYS_FM_MM : MMArch {
  bits<10> code_;
  bits<32> Inst;
  let Inst{31-26} = 0x0;
  let Inst{25-16} = code_;
  let Inst{15-6}  = 0x22d;
  let Inst{5-0}   = 0x3c;
}

class WAIT_FM_MM : MMArch {
  bits<10> code_;
  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-16} = code_;
  let Inst{15-6}  = 0x24d;
  let Inst{5-0}   = 0x3c;
}

class ER_FM_MM<bits<10> funct> : MMArch {
  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-16} = 0x00;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class EI_FM_MM<bits<10> funct> : MMArch {
  bits<32> Inst;
  bits<5> rt;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = 0x00;
  let Inst{20-16} = rt;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class TEQ_FM_MM<bits<6> funct> : MMArch {
  bits<5> rs;
  bits<5> rt;
  bits<4> code_;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-12} = code_;
  let Inst{11-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class TEQI_FM_MM<bits<5> funct> : MMArch {
  bits<5> rs;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = 0x10;
  let Inst{25-21} = funct;
  let Inst{20-16} = rs;
  let Inst{15-0}  = imm16;
}

class LL_FM_MM<bits<4> funct> : MMArch {
  bits<5> rt;
  bits<21> addr;

  bits<32> Inst;

  let Inst{31-26} = 0x18;
  let Inst{25-21} = rt;
  let Inst{20-16} = addr{20-16};
  let Inst{15-12} = funct;
  let Inst{11-0}  = addr{11-0};
}

class LLE_FM_MM<bits<4> funct> : MMArch {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<9> offset = addr{8-0};

  bits<32> Inst;

  let Inst{31-26} = 0x18;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-12} = funct;
  let Inst{11-9} = 0x6;
  let Inst{8-0} = offset;
}

class ADDS_FM_MM<bits<2> fmt, bits<8> funct> : MMArch {
  bits<5> ft;
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{10}    = 0;
  let Inst{9-8}   = fmt;
  let Inst{7-0}   = funct;

}

class LWXC1_FM_MM<bits<9> funct> : MMArch {
  bits<5> fd;
  bits<5> base;
  bits<5> index;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = index;
  let Inst{20-16} = base;
  let Inst{15-11} = fd;
  let Inst{10-9}  = 0x0;
  let Inst{8-0}   = funct;
}

class SWXC1_FM_MM<bits<9> funct> : MMArch {
  bits<5> fs;
  bits<5> base;
  bits<5> index;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = index;
  let Inst{20-16} = base;
  let Inst{15-11} = fs;
  let Inst{10-9}  = 0x0;
  let Inst{8-0}   = funct;
}

class CEQS_FM_MM<bits<2> fmt> : MMArch {
  bits<5> fs;
  bits<5> ft;
  bits<3> fcc;
  bits<4> cond;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-13} = fcc;
  let Inst{12}    = 0;
  let Inst{11-10} = fmt;
  let Inst{9-6}   = cond;
  let Inst{5-0}   = 0x3c;
}

class C_COND_FM_MM<bits <2> fmt, bits<4> c> : CEQS_FM_MM<fmt> {
  let cond = c;
}

class BC1F_FM_MM<bits<5> tf> : MMArch {
  bits<3> fcc;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0x10;
  let Inst{25-21} = tf;
  let Inst{20-18} = fcc; // cc
  let Inst{17-16} = 0x0;
  let Inst{15-0}  = offset;
}

class ROUND_W_FM_MM<bits<1> fmt, bits<8> funct> : MMArch {
  bits<5> fd;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = fd;
  let Inst{20-16} = fs;
  let Inst{15}    = 0;
  let Inst{14}    = fmt;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0x3b;
}

class ABS_FM_MM<bits<2> fmt, bits<7> funct> : MMArch {
  bits<5> fd;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = fd;
  let Inst{20-16} = fs;
  let Inst{15}    = 0;
  let Inst{14-13} = fmt;
  let Inst{12-6}  = funct;
  let Inst{5-0}   = 0x3b;
}

class CMov_F_F_FM_MM<bits<9> func, bits<2> fmt> : MMArch {
  bits<5> fd;
  bits<5> fs;
  bits<3> fcc;
  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = fd;
  let Inst{20-16} = fs;
  let Inst{15-13} = fcc; //cc
  let Inst{12-11} = 0x0;
  let Inst{10-9}  = fmt;
  let Inst{8-0}   = func;
}

class CMov_I_F_FM_MM<bits<8> funct, bits<2> fmt> : MMArch {
  bits<5> fd;
  bits<5> fs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = rt;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{9-8}   = fmt;
  let Inst{7-0}   = funct;
}

class MFC1_FM_MM<bits<8> funct> : MMArch {
  bits<5> rt;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = rt;
  let Inst{20-16} = fs;
  let Inst{15-14} = 0x0;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0x3b;
}

class MADDS_FM_MM<bits<6> funct>: MMArch {
  bits<5> ft;
  bits<5> fs;
  bits<5> fd;
  bits<5> fr;

  bits<32> Inst;

  let Inst{31-26} = 0x15;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{10-6}  = fr;
  let Inst{5-0}   = funct;
}

class COMPACT_BRANCH_FM_MM<bits<5> funct> {
  bits<5>  rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0x10;
  let Inst{25-21} = funct;
  let Inst{20-16} = rs;
  let Inst{15-0}  = offset;
}

class COP0_TLB_FM_MM<bits<10> op> : MMArch {
  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-16} = 0x0;
  let Inst{15-6}  = op;
  let Inst{5-0}   = 0x3c;
}

class SDBBP_FM_MM : MMArch {
  bits<10> code_;

  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-16} = code_;
  let Inst{15-6}  = 0x36d;
  let Inst{5-0}   = 0x3c;
}

class SIGRIE_FM_MM : MMArch {
  bits<16> code_;

  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-22} = 0x0;
  let Inst{21-6} = code_;
  let Inst{5-0} = 0b111111;
}

class RDHWR_FM_MM : MMArch {
  bits<5> rt;
  bits<5> rd;

  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rd;
  let Inst{15-6}  = 0x1ac;
  let Inst{5-0}   = 0x3c;
}

class LWXS_FM_MM<bits<10> funct> {
  bits<5> rd;
  bits<5> base;
  bits<5> index;

  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-21} = index;
  let Inst{20-16} = base;
  let Inst{15-11} = rd;
  let Inst{10}    = 0;
  let Inst{9-0}   = funct;
}

class LWM_FM_MM<bits<4> funct> : MMArch {
  bits<5> rt;
  bits<21> addr;

  bits<32> Inst;

  let Inst{31-26} = 0x8;
  let Inst{25-21} = rt;
  let Inst{20-16} = addr{20-16};
  let Inst{15-12} = funct;
  let Inst{11-0}  = addr{11-0};
}

class LWM_FM_MM16<bits<4> funct> : MMArch {
  bits<2> rt;
  bits<4> addr;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-6}   = funct;
  let Inst{5-4}   = rt;
  let Inst{3-0}   = addr;
}

class CACHE_PREF_FM_MM<bits<6> op, bits<4> funct> : MMArch {
  bits<21> addr;
  bits<5> hint;
  bits<5> base = addr{20-16};
  bits<12> offset = addr{11-0};

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = hint;
  let Inst{20-16} = base;
  let Inst{15-12} = funct;
  let Inst{11-0}  = offset;
}

class CACHE_PREFE_FM_MM<bits<6> op, bits<3> funct> : MMArch {
  bits<21> addr;
  bits<5> hint;
  bits<5> base = addr{20-16};
  bits<9> offset = addr{8-0};

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = hint;
  let Inst{20-16} = base;
  let Inst{15-12} = 0xA;
  let Inst{11-9} = funct;
  let Inst{8-0}  = offset;
}

class POOL32F_PREFX_FM_MM<bits<6> op, bits<9> funct> : MMArch {
  bits<5> index;
  bits<5> base;
  bits<5> hint;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = index;
  let Inst{20-16} = base;
  let Inst{15-11} = hint;
  let Inst{10-9}  = 0x0;
  let Inst{8-0}   = funct;
}

class BARRIER_FM_MM<bits<5> op> : MMArch {
  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-21} = 0x0;
  let Inst{20-16} = 0x0;
  let Inst{15-11} = op;
  let Inst{10-6}  = 0x0;
  let Inst{5-0}   = 0x0;
}

class ADDIUPC_FM_MM {
  bits<3> rs;
  bits<23> imm;

  bits<32> Inst;

  let Inst{31-26} = 0x1e;
  let Inst{25-23} = rs;
  let Inst{22-0} = imm;
}

class POOL32A_CFTC2_FM_MM<bits<10> funct> : MMArch {
  bits<5> rt;
  bits<5> impl;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = impl;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_TLBINV_FM_MM<bits<10> funct> : MMArch {
  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-16} = 0x0;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_MFTC0_FM_MM<bits<5> funct, bits<6> opcode> : MMArch {
  bits<5> rt;
  bits<5> rs;
  bits<3> sel;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-14} = 0;
  let Inst{13-11} = sel;
  let Inst{10-6}  = funct;
  let Inst{5-0}   = opcode;
}

class POOL32A_HYPCALL_FM_MM : MMArch {
  bits<32> Inst;

  bits<10> code_;

  let Inst{31-26} = 0x0;
  let Inst{25-16} = code_;
  let Inst{15-6}  = 0b1100001101;
  let Inst{5-0}   = 0b111100;
}
