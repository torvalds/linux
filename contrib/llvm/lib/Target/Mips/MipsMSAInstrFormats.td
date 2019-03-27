//===- MipsMSAInstrFormats.td - Mips Instruction Formats ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class MSAInst : MipsInst<(outs), (ins), "", [], NoItinerary, FrmOther>,
                ASE_MSA {
  let EncodingPredicates = [HasStdEnc];
  let Inst{31-26} = 0b011110;
}

class MSACBranch : MSAInst {
  let Inst{31-26} = 0b010001;
}

class MSASpecial : MSAInst {
  let Inst{31-26} = 0b000000;
}

class MSAPseudo<dag outs, dag ins, list<dag> pattern,
                InstrItinClass itin = IIPseudo>:
  MipsPseudo<outs, ins, pattern, itin> {
  let EncodingPredicates = [HasStdEnc];
  let ASEPredicate = [HasMSA];
}

class MSA_BIT_B_FMT<bits<3> major, bits<6> minor>: MSAInst {
  bits<5> ws;
  bits<5> wd;
  bits<3> m;

  let Inst{25-23} = major;
  let Inst{22-19} = 0b1110;
  let Inst{18-16} = m;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_BIT_H_FMT<bits<3> major, bits<6> minor>: MSAInst {
  bits<5> ws;
  bits<5> wd;
  bits<4> m;

  let Inst{25-23} = major;
  let Inst{22-20} = 0b110;
  let Inst{19-16} = m;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_BIT_W_FMT<bits<3> major, bits<6> minor>: MSAInst {
  bits<5> ws;
  bits<5> wd;
  bits<5> m;

  let Inst{25-23} = major;
  let Inst{22-21} = 0b10;
  let Inst{20-16} = m;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_BIT_D_FMT<bits<3> major, bits<6> minor>: MSAInst {
  bits<5> ws;
  bits<5> wd;
  bits<6> m;

  let Inst{25-23} = major;
  let Inst{22} = 0b0;
  let Inst{21-16} = m;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_2R_FILL_FMT<bits<8> major, bits<2> df, bits<6> minor>: MSAInst {
  bits<5> rs;
  bits<5> wd;

  let Inst{25-18} = major;
  let Inst{17-16} = df;
  let Inst{15-11} = rs;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_2R_FILL_D_FMT<bits<8> major, bits<2> df, bits<6> minor>: MSAInst {
  bits<5> rs;
  bits<5> wd;

  let Inst{25-18} = major;
  let Inst{17-16} = df;
  let Inst{15-11} = rs;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_2R_FMT<bits<8> major, bits<2> df, bits<6> minor>: MSAInst {
  bits<5> ws;
  bits<5> wd;

  let Inst{25-18} = major;
  let Inst{17-16} = df;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_2RF_FMT<bits<9> major, bits<1> df, bits<6> minor>: MSAInst {
  bits<5> ws;
  bits<5> wd;

  let Inst{25-17} = major;
  let Inst{16} = df;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_3R_FMT<bits<3> major, bits<2> df, bits<6> minor>: MSAInst {
  bits<5> wt;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{20-16} = wt;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_3RF_FMT<bits<4> major, bits<1> df, bits<6> minor>: MSAInst {
  bits<5> wt;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21} = df;
  let Inst{20-16} = wt;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_3R_INDEX_FMT<bits<3> major, bits<2> df, bits<6> minor>: MSAInst {
  bits<5> rt;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{20-16} = rt;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_FMT<bits<10> major, bits<6> minor>: MSAInst {
  bits<5> ws;
  bits<5> wd;

  let Inst{25-16} = major;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_CFCMSA_FMT<bits<10> major, bits<6> minor>: MSAInst {
  bits<5> rd;
  bits<5> cs;

  let Inst{25-16} = major;
  let Inst{15-11} = cs;
  let Inst{10-6} = rd;
  let Inst{5-0} = minor;
}

class MSA_ELM_CTCMSA_FMT<bits<10> major, bits<6> minor>: MSAInst {
  bits<5> rs;
  bits<5> cd;

  let Inst{25-16} = major;
  let Inst{15-11} = rs;
  let Inst{10-6} = cd;
  let Inst{5-0} = minor;
}

class MSA_ELM_B_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-20} = 0b00;
  let Inst{19-16} = n{3-0};
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_H_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-19} = 0b100;
  let Inst{18-16} = n{2-0};
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_W_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-18} = 0b1100;
  let Inst{17-16} = n{1-0};
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_D_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-17} = 0b11100;
  let Inst{16} = n{0};
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_COPY_B_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> rd;

  let Inst{25-22} = major;
  let Inst{21-20} = 0b00;
  let Inst{19-16} = n{3-0};
  let Inst{15-11} = ws;
  let Inst{10-6} = rd;
  let Inst{5-0} = minor;
}

class MSA_ELM_COPY_H_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> rd;

  let Inst{25-22} = major;
  let Inst{21-19} = 0b100;
  let Inst{18-16} = n{2-0};
  let Inst{15-11} = ws;
  let Inst{10-6} = rd;
  let Inst{5-0} = minor;
}

class MSA_ELM_COPY_W_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> rd;

  let Inst{25-22} = major;
  let Inst{21-18} = 0b1100;
  let Inst{17-16} = n{1-0};
  let Inst{15-11} = ws;
  let Inst{10-6} = rd;
  let Inst{5-0} = minor;
}

class MSA_ELM_COPY_D_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<4> n;
  bits<5> ws;
  bits<5> rd;

  let Inst{25-22} = major;
  let Inst{21-17} = 0b11100;
  let Inst{16} = n{0};
  let Inst{15-11} = ws;
  let Inst{10-6} = rd;
  let Inst{5-0} = minor;
}

class MSA_ELM_INSERT_B_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<6> n;
  bits<5> rs;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-20} = 0b00;
  let Inst{19-16} = n{3-0};
  let Inst{15-11} = rs;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_INSERT_H_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<6> n;
  bits<5> rs;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-19} = 0b100;
  let Inst{18-16} = n{2-0};
  let Inst{15-11} = rs;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_INSERT_W_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<6> n;
  bits<5> rs;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-18} = 0b1100;
  let Inst{17-16} = n{1-0};
  let Inst{15-11} = rs;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_ELM_INSERT_D_FMT<bits<4> major, bits<6> minor>: MSAInst {
  bits<6> n;
  bits<5> rs;
  bits<5> wd;

  let Inst{25-22} = major;
  let Inst{21-17} = 0b11100;
  let Inst{16} = n{0};
  let Inst{15-11} = rs;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_I5_FMT<bits<3> major, bits<2> df, bits<6> minor>: MSAInst {
  bits<5> imm;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{20-16} = imm;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_I8_FMT<bits<2> major, bits<6> minor>: MSAInst {
  bits<8> u8;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-24} = major;
  let Inst{23-16} = u8;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_I10_FMT<bits<3> major, bits<2> df, bits<6> minor>: MSAInst {
  bits<10> s10;
  bits<5> wd;

  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{20-11} = s10;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_MI10_FMT<bits<2> df, bits<4> minor>: MSAInst {
  bits<21> addr;
  bits<5> wd;

  let Inst{25-16} = addr{9-0};
  let Inst{15-11} = addr{20-16};
  let Inst{10-6} = wd;
  let Inst{5-2} = minor;
  let Inst{1-0} = df;
}

class MSA_VEC_FMT<bits<5> major, bits<6> minor>: MSAInst {
  bits<5> wt;
  bits<5> ws;
  bits<5> wd;

  let Inst{25-21} = major;
  let Inst{20-16} = wt;
  let Inst{15-11} = ws;
  let Inst{10-6} = wd;
  let Inst{5-0} = minor;
}

class MSA_CBRANCH_FMT<bits<3> major, bits<2> df>: MSACBranch {
  bits<16> offset;
  bits<5> wt;

  let Inst{25-23} = major;
  let Inst{22-21} = df;
  let Inst{20-16} = wt;
  let Inst{15-0} = offset;
}

class MSA_CBRANCH_V_FMT<bits<5> major>: MSACBranch {
  bits<16> offset;
  bits<5> wt;

  let Inst{25-21} = major;
  let Inst{20-16} = wt;
  let Inst{15-0} = offset;
}

class SPECIAL_LSA_FMT<bits<6> minor>: MSASpecial {
  bits<5> rs;
  bits<5> rt;
  bits<5> rd;
  bits<2> sa;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-8} = 0b000;
  let Inst{7-6} = sa;
  let Inst{5-0} = minor;
}

class SPECIAL_DLSA_FMT<bits<6> minor>: MSASpecial {
  bits<5> rs;
  bits<5> rt;
  bits<5> rd;
  bits<2> sa;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-8} = 0b000;
  let Inst{7-6} = sa;
  let Inst{5-0} = minor;
}
