//===- MipsEVAInstrFormats.td - Mips Instruction Formats ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes Mips32r6 instruction formats.
//
//===----------------------------------------------------------------------===//

class MipsEVAInst : MipsInst<(outs), (ins), "", [], NoItinerary, FrmOther>,
                    StdArch {
  let DecoderNamespace = "Mips";
  let EncodingPredicates = [HasStdEnc];
}

//===----------------------------------------------------------------------===//
//
// Field Values
//
//===----------------------------------------------------------------------===//

// Memory Load/Store EVA
def OPCODE6_LBE        : OPCODE6<0b101100>;
def OPCODE6_LBuE       : OPCODE6<0b101000>;
def OPCODE6_LHE        : OPCODE6<0b101101>;
def OPCODE6_LHuE       : OPCODE6<0b101001>;
def OPCODE6_LWE        : OPCODE6<0b101111>;

def OPCODE6_SBE        : OPCODE6<0b011100>;
def OPCODE6_SHE        : OPCODE6<0b011101>;
def OPCODE6_SWE        : OPCODE6<0b011111>;

// load/store left/right EVA
def OPCODE6_LWLE       : OPCODE6<0b011001>;
def OPCODE6_LWRE       : OPCODE6<0b011010>;
def OPCODE6_SWLE       : OPCODE6<0b100001>;
def OPCODE6_SWRE       : OPCODE6<0b100010>;

// Load-linked EVA, Store-conditional EVA
def OPCODE6_LLE        : OPCODE6<0b101110>;
def OPCODE6_SCE        : OPCODE6<0b011110>;

def OPCODE6_TLBINV     : OPCODE6<0b000011>;
def OPCODE6_TLBINVF    : OPCODE6<0b000100>;

def OPCODE6_CACHEE     : OPCODE6<0b011011>;
def OPCODE6_PREFE      : OPCODE6<0b100011>;

def OPGROUP_COP0_TLB   : OPGROUP<0b010000>;

//===----------------------------------------------------------------------===//
//
// Encoding Formats
//
//===----------------------------------------------------------------------===//

class SPECIAL3_EVA_LOAD_STORE_FM<OPCODE6 Operation> : MipsEVAInst {
  bits<21> addr;
  bits<5> hint;
  bits<5> base = addr{20-16};
  bits<9> offset = addr{8-0};

  bits<32> Inst;

  let Inst{31-26} = OPGROUP_SPECIAL3.Value;
  let Inst{25-21} = base;
  let Inst{20-16} = hint;
  let Inst{15-7}  = offset;
  let Inst{6}     = 0;
  let Inst{5-0}   = Operation.Value;
}

class TLB_FM<OPCODE6 Operation> : MipsEVAInst {
  bits<32> Inst;

  let Inst{31-26} = OPGROUP_COP0_TLB.Value;
  let Inst{25} = 1;       // CO
  let Inst{24-6} = 0;
  let Inst{5-0} = Operation.Value;
}
