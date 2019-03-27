//===- MipsDSPInstrFormats.td - Mips Instruction Formats ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

class DspMMRel;

def Dsp2MicroMips : InstrMapping {
  let FilterClass = "DspMMRel";
  // Instructions with the same BaseOpcode and isNVStore values form a row.
  let RowFields = ["BaseOpcode"];
  // Instructions with the same predicate sense form a column.
  let ColFields = ["Arch"];
  // The key column is the unpredicated instructions.
  let KeyCol = ["dsp"];
  // Value columns are PredSense=true and PredSense=false
  let ValueCols = [["dsp"], ["mmdsp"]];
}

def HasDSP : Predicate<"Subtarget->hasDSP()">,
             AssemblerPredicate<"FeatureDSP">;
def HasDSPR2 : Predicate<"Subtarget->hasDSPR2()">,
               AssemblerPredicate<"FeatureDSPR2">;
def HasDSPR3 : Predicate<"Subtarget->hasDSPR3()">,
               AssemblerPredicate<"FeatureDSPR3">;

class ISA_DSPR2 {
  list<Predicate> ASEPredicate = [HasDSPR2];
}

class ISA_DSPR3 {
  list<Predicate> ASEPredicate = [HasDSPR3];
}

// Fields.
class Field6<bits<6> val> {
  bits<6> V = val;
}

def SPECIAL3_OPCODE : Field6<0b011111>;
def REGIMM_OPCODE : Field6<0b000001>;

class DSPInst<string opstr = "">
    : MipsInst<(outs), (ins), "", [], NoItinerary, FrmOther> {
  let ASEPredicate = [HasDSP];
  string BaseOpcode = opstr;
  string Arch = "dsp";
}

class PseudoDSP<dag outs, dag ins, list<dag> pattern,
                InstrItinClass itin = IIPseudo>
    : MipsPseudo<outs, ins, pattern, itin> {
  let ASEPredicate = [HasDSP];
}

class DSPInstAlias<string Asm, dag Result, bit Emit = 0b1>
    : InstAlias<Asm, Result, Emit>, PredicateControl {
  let ASEPredicate = [HasDSP];
}

// ADDU.QB sub-class format.
class ADDU_QB_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010000;
}

class RADDU_W_QB_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<5> rs;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = 0;
  let Inst{15-11} = rd;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010000;
}

// CMPU.EQ.QB sub-class format.
class CMP_EQ_QB_R2_FMT<bits<5> op> : DSPInst {
  bits<5> rs;
  bits<5> rt;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = 0;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010001;
}

class CMP_EQ_QB_R3_FMT<bits<5> op> : DSPInst {
  bits<5> rs;
  bits<5> rt;
  bits<5> rd;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010001;
}

class PRECR_SRA_PH_W_FMT<bits<5> op> : DSPInst {
  bits<5> rs;
  bits<5> rt;
  bits<5> sa;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = sa;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010001;
}

// ABSQ_S.PH sub-class format.
class ABSQ_S_PH_R2_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<5> rt;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = 0;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010010;
}


class REPL_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<10> imm;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-16} = imm;
  let Inst{15-11} = rd;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010010;
}

// SHLL.QB sub-class format.
class SHLL_QB_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<5> rt;
  bits<5> rs_sa;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs_sa;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = op;
  let Inst{5-0}   = 0b010011;
}

// LX sub-class format.
class LX_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<5> base;
  bits<5> index;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = base;
  let Inst{20-16} = index;
  let Inst{15-11} = rd;
  let Inst{10-6}  = op;
  let Inst{5-0} = 0b001010;
}

// ADDUH.QB sub-class format.
class ADDUH_QB_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6} = op;
  let Inst{5-0} = 0b011000;
}

// APPEND sub-class format.
class APPEND_FMT<bits<5> op> : DSPInst {
  bits<5> rt;
  bits<5> rs;
  bits<5> sa;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = sa;
  let Inst{10-6} = op;
  let Inst{5-0} = 0b110001;
}

// DPA.W.PH sub-class format.
class DPA_W_PH_FMT<bits<5> op> : DSPInst {
  bits<2> ac;
  bits<5> rs;
  bits<5> rt;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-13} = 0;
  let Inst{12-11} = ac;
  let Inst{10-6}  = op;
  let Inst{5-0} = 0b110000;
}

// MULT sub-class format.
class MULT_FMT<bits<6> opcode, bits<6> funct> : DSPInst {
  bits<2> ac;
  bits<5> rs;
  bits<5> rt;

  let Opcode = opcode;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-13} = 0;
  let Inst{12-11} = ac;
  let Inst{10-6}  = 0;
  let Inst{5-0} = funct;
}

// MFHI sub-class format.
class MFHI_FMT<bits<6> funct> : DSPInst {
  bits<5> rd;
  bits<2> ac;

  let Inst{31-26} = 0;
  let Inst{25-23} = 0;
  let Inst{22-21} = ac;
  let Inst{20-16} = 0;
  let Inst{15-11} = rd;
  let Inst{10-6} = 0;
  let Inst{5-0} = funct;
}

// MTHI sub-class format.
class MTHI_FMT<bits<6> funct> : DSPInst {
  bits<5> rs;
  bits<2> ac;

  let Inst{31-26} = 0;
  let Inst{25-21} = rs;
  let Inst{20-13} = 0;
  let Inst{12-11} = ac;
  let Inst{10-6} = 0;
  let Inst{5-0} = funct;
}

// EXTR.W sub-class format (type 1).
class EXTR_W_TY1_FMT<bits<5> op> : DSPInst {
  bits<5> rt;
  bits<2> ac;
  bits<5> shift_rs;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = shift_rs;
  let Inst{20-16} = rt;
  let Inst{15-13} = 0;
  let Inst{12-11} = ac;
  let Inst{10-6} = op;
  let Inst{5-0} = 0b111000;
}

// SHILO sub-class format.
class SHILO_R1_FMT<bits<5> op> : DSPInst {
  bits<2> ac;
  bits<6> shift;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-20} = shift;
  let Inst{19-13} = 0;
  let Inst{12-11} = ac;
  let Inst{10-6} = op;
  let Inst{5-0} = 0b111000;
}

class SHILO_R2_FMT<bits<5> op> : DSPInst {
  bits<2> ac;
  bits<5> rs;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-13} = 0;
  let Inst{12-11} = ac;
  let Inst{10-6} = op;
  let Inst{5-0} = 0b111000;
}

class RDDSP_FMT<bits<5> op> : DSPInst {
  bits<5> rd;
  bits<10> mask;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-16} = mask;
  let Inst{15-11} = rd;
  let Inst{10-6} = op;
  let Inst{5-0} = 0b111000;
}

class WRDSP_FMT<bits<5> op> : DSPInst {
  bits<5> rs;
  bits<10> mask;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-11} = mask;
  let Inst{10-6} = op;
  let Inst{5-0} = 0b111000;
}

class BPOSGE32_FMT<bits<5> op> : DSPInst {
  bits<16> offset;

  let Opcode = REGIMM_OPCODE.V;

  let Inst{25-21} = 0;
  let Inst{20-16} = op;
  let Inst{15-0} = offset;
}

// INSV sub-class format.
class INSV_FMT<bits<6> op> : DSPInst {
  bits<5> rt;
  bits<5> rs;

  let Opcode = SPECIAL3_OPCODE.V;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-6} = 0;
  let Inst{5-0} = op;
}
