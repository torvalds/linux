//===-- XCoreInstrFormats.td - XCore Instruction Formats ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//
class InstXCore<int sz, dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  field bits<32> Inst;

  let Namespace = "XCore";
  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString   = asmstr;
  let Pattern = pattern;
  let Size = sz;
  field bits<32> SoftFail = 0;
}

// XCore pseudo instructions format
class PseudoInstXCore<dag outs, dag ins, string asmstr, list<dag> pattern>
   : InstXCore<0, outs, ins, asmstr, pattern> {
  let isPseudo = 1;
}

//===----------------------------------------------------------------------===//
// Instruction formats
//===----------------------------------------------------------------------===//

class _F3R<bits<5> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  let Inst{15-11} = opc;
  let DecoderMethod = "Decode3RInstruction";
}

// 3R with first operand as an immediate. Used for TSETR where the first
// operand is treated as an immediate since it refers to a register number in
// another thread.
class _F3RImm<bits<5> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : _F3R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "Decode3RImmInstruction";
}

class _FL3R<bits<9> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  let Inst{31-27} = opc{8-4};
  let Inst{26-20} = 0b1111110;
  let Inst{19-16} = opc{3-0};

  let Inst{15-11} = 0b11111;
  let DecoderMethod = "DecodeL3RInstruction";
}

// L3R with first operand as both a source and a destination.
class _FL3RSrcDst<bits<9> opc, dag outs, dag ins, string asmstr,
                  list<dag> pattern> : _FL3R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeL3RSrcDstInstruction";
}

class _F2RUS<bits<5> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  let Inst{15-11} = opc;
  let DecoderMethod = "Decode2RUSInstruction";
}

// 2RUS with bitp operand
class _F2RUSBitp<bits<5> opc, dag outs, dag ins, string asmstr,
                 list<dag> pattern>
    : _F2RUS<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "Decode2RUSBitpInstruction";
}

class _FL2RUS<bits<9> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  let Inst{31-27} = opc{8-4};
  let Inst{26-20} = 0b1111110;
  let Inst{19-16} = opc{3-0};

  let Inst{15-11} = 0b11111;
  let DecoderMethod = "DecodeL2RUSInstruction";
}

// L2RUS with bitp operand
class _FL2RUSBitp<bits<9> opc, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : _FL2RUS<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeL2RUSBitpInstruction";
}

class _FRU6<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  bits<4> a;
  bits<6> b;

  let Inst{15-10} = opc;
  let Inst{9-6} = a;
  let Inst{5-0} = b;
}

class _FLRU6<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  bits<4> a;
  bits<16> b;

  let Inst{31-26} = opc;
  let Inst{25-22} = a;
  let Inst{21-16} = b{5-0};
  let Inst{15-10} = 0b111100;
  let Inst{9-0} = b{15-6};
}

class _FU6<bits<10> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  bits<6> a;

  let Inst{15-6} = opc;
  let Inst{5-0} = a;
}

class _FLU6<bits<10> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  bits<16> a;

  let Inst{31-22} = opc;
  let Inst{21-16} = a{5-0};
  let Inst{15-10} = 0b111100;
  let Inst{9-0} = a{15-6};
}

class _FU10<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  bits<10> a;

  let Inst{15-10} = opc;
  let Inst{9-0} = a;
}

class _FLU10<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  bits<20> a;

  let Inst{31-26} = opc;
  let Inst{25-16} = a{9-0};
  let Inst{15-10} = 0b111100;
  let Inst{9-0} = a{19-10};
}

class _F2R<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  let Inst{15-11} = opc{5-1};
  let Inst{4} = opc{0};
  let DecoderMethod = "Decode2RInstruction";
}

// 2R with first operand as an immediate. Used for TSETMR where the first
// operand is treated as an immediate since it refers to a register number in
// another thread.
class _F2RImm<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : _F2R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "Decode2RImmInstruction";
}

// 2R with first operand as both a source and a destination.
class _F2RSrcDst<bits<6> opc, dag outs, dag ins, string asmstr,
                 list<dag> pattern> : _F2R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "Decode2RSrcDstInstruction";
}

// Same as 2R with last two operands swapped
class _FR2R<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : _F2R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeR2RInstruction";
}

class _FRUS<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  let Inst{15-11} = opc{5-1};
  let Inst{4} = opc{0};
  let DecoderMethod = "DecodeRUSInstruction";
}

// RUS with bitp operand
class _FRUSBitp<bits<6> opc, dag outs, dag ins, string asmstr,
                list<dag> pattern>
    : _FRUS<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeRUSBitpInstruction";
}

// RUS with first operand as both a source and a destination and a bitp second
// operand
class _FRUSSrcDstBitp<bits<6> opc, dag outs, dag ins, string asmstr,
                      list<dag> pattern>
    : _FRUS<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeRUSSrcDstBitpInstruction";
}

class _FL2R<bits<10> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  let Inst{31-27} = opc{9-5};
  let Inst{26-20} = 0b1111110;
  let Inst{19-16} = opc{4-1};

  let Inst{15-11} = 0b11111;
  let Inst{4} = opc{0};
  let DecoderMethod = "DecodeL2RInstruction";
}

// Same as L2R with last two operands swapped
class _FLR2R<bits<10> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : _FL2R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeLR2RInstruction";
}

class _F1R<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  bits<4> a;

  let Inst{15-11} = opc{5-1};
  let Inst{10-5} = 0b111111;
  let Inst{4} = opc{0};
  let Inst{3-0} = a;
}

class _F0R<bits<10> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<2, outs, ins, asmstr, pattern> {
  let Inst{15-11} = opc{9-5};
  let Inst{10-5} = 0b111111;
  let Inst{4-0} = opc{4-0};
}

class _FL4R<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  bits<4> d;

  let Inst{31-27} = opc{5-1};
  let Inst{26-21} = 0b111111;
  let Inst{20} = opc{0};
  let Inst{19-16} = d;
  let Inst{15-11} = 0b11111;
}

// L4R with 4th operand as both a source and a destination.
class _FL4RSrcDst<bits<6> opc, dag outs, dag ins, string asmstr,
                  list<dag> pattern>
    : _FL4R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeL4RSrcDstInstruction";
}

// L4R with 1st and 4th operand as both a source and a destination.
class _FL4RSrcDstSrcDst<bits<6> opc, dag outs, dag ins, string asmstr,
                        list<dag> pattern>
    : _FL4R<opc, outs, ins, asmstr, pattern> {
  let DecoderMethod = "DecodeL4RSrcDstSrcDstInstruction";
}

class _FL5R<bits<6> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  let Inst{31-27} = opc{5-1};
  let Inst{20} = opc{0};
  let Inst{15-11} = 0b11111;

  let DecoderMethod = "DecodeL5RInstruction";
}

class _FL6R<bits<5> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : InstXCore<4, outs, ins, asmstr, pattern> {
  let Inst{31-27} = opc;
  let Inst{15-11} = 0b11111;

  let DecoderMethod = "DecodeL6RInstruction";
}
