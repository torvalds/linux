//===-- MicroMipsDSPInstrFormats.td - Instruction Formats --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class MMDSPInst<string opstr = "">
    : MipsInst<(outs), (ins), "", [], NoItinerary, FrmOther> {
  let ASEPredicate = [HasDSP];
  let EncodingPredicates = [InMicroMips];
  string BaseOpcode = opstr;
  string Arch = "mmdsp";
  let DecoderNamespace = "MicroMips";
}

class MMDSPInstAlias<string Asm, dag Result, bit Emit = 0b1>
    : InstAlias<Asm, Result, Emit>, PredicateControl {
  let ASEPredicate = [HasDSP];
  let AdditionalPredicates = [InMicroMips];
}

class POOL32A_3R_FMT<string opstr, bits<11> op> : MMDSPInst<opstr> {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10-0}  = op;
}

class POOL32A_2R_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-6}  = op;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_2RAC_FMT<string opstr, bits<8> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<2> ac;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-14} = ac;
  let Inst{13-6}  = op;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_3RB0_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10}    = 0b0;
  let Inst{9-0}   = op;
}

class POOL32A_2RSA4_FMT<string opstr, bits<12> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<4> sa;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-12} = sa;
  let Inst{11-0}  = op;
}

class POOL32A_2RSA3_FMT<string opstr, bits<7> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<3> sa;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-13} = sa;
  let Inst{12-6}  = op;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_2RSA5B0_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<5> sa;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = sa;
  let Inst{10}    = 0b0;
  let Inst{9-0}   = op;
}

class POOL32A_2RSA4B0_FMT<string opstr, bits<11> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<4> sa;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-12} = sa;
  let Inst{11}    = 0b0;
  let Inst{10-0}  = op;
}

class POOL32A_2RSA4OP6_FMT<string opstr, bits<6> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<4> sa;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-12} = sa;
  let Inst{11-6}  = op;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_1RIMM5AC_FMT<string opstr, bits<8> funct> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> imm;
  bits<2> ac;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = imm;
  let Inst{15-14} = ac;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_2RSA5_FMT<string opstr, bits<11> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<5> sa;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = sa;
  let Inst{10-0}  = op;
}

class POOL32A_1RMEMB0_FMT<string opstr, bits<10> funct> : MMDSPInst<opstr> {
  bits<5> index;
  bits<5> base;
  bits<5> rd;

  let Inst{31-26} = 0;
  let Inst{25-21} = index;
  let Inst{20-16} = base;
  let Inst{15-11} = rd;
  let Inst{10}    = 0b0;
  let Inst{9-0}   = funct;
}

class POOL32A_1RAC_FMT<string instr_asm, bits<8> funct> : MMDSPInst<instr_asm> {
  bits<5> rs;
  bits<2> ac;

  let Inst{31-26} = 0;
  let Inst{25-21} = 0;
  let Inst{20-16} = rs;
  let Inst{15-14} = ac;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_1RMASK7_FMT<string opstr, bits<8> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<7> mask;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-14} = mask;
  let Inst{13-6}  = op;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_1RIMM10_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<5> rd;
  bits<10> imm;

  let Inst{31-26} = 0;
  let Inst{25-16} = imm;
  let Inst{15-11} = rd;
  let Inst{10}    = 0;
  let Inst{9-0}   = op;
}

class POOL32A_1RIMM8_FMT<string opstr, bits<6> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<8> imm;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-13} = imm;
  let Inst{12}    = 0;
  let Inst{11-6}  = op;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_4B0SHIFT6AC4B0_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<6> shift;
  bits<2> ac;

  let Inst{31-26} = 0b000000;
  let Inst{25-22} = 0b0000;
  let Inst{21-16} = shift;
  let Inst{15-14} = ac;
  let Inst{13-10} = 0b0000;
  let Inst{9-0}   = op;
}

class POOL32A_5B01RAC_FMT<string opstr, bits<8> op> : MMDSPInst<opstr> {
  bits<5> rs;
  bits<2> ac;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = 0b00000;
  let Inst{20-16} = rs;
  let Inst{15-14} = ac;
  let Inst{13-6}  = op;
  let Inst{5-0}   = 0b111100;
}

class POOL32I_IMMB0_FMT<string opstr, bits<5> op> : MMDSPInst<opstr> {
  bits<16> offset;

  let Inst{31-26} = 0b010000;
  let Inst{25-21} = op;
  let Inst{20-16} = 0;
  let Inst{15-0}  = offset;
}

class POOL32A_2RBP_FMT<string opstr> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<2> bp;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-14} = bp;
  let Inst{13-6}  = 0b00100010;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_2RB0_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-10} = 0;
  let Inst{9-0}   = op;
}

class POOL32S_3RB0_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<5> rd;

  let Inst{31-26} = 0b010110;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10}    = 0b0;
  let Inst{9-0}   = op;
}

class POOL32A_2R2B0_FMT<string opstr, bits<10> op> : MMDSPInst<opstr> {
  bits<5> rt;
  bits<5> rs;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = 0;
  let Inst{10}    = 0;
  let Inst{9-0}   = op;
}
