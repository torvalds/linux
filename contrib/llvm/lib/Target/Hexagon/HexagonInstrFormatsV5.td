//==- HexagonInstrFormatsV5.td - Hexagon Instruction Formats --*- tablegen -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the Hexagon V5 instruction classes in TableGen format.
//
//===----------------------------------------------------------------------===//

//                      Duplex Instruction Class Declaration
//===----------------------------------------------------------------------===//

class OpcodeDuplex {
  field bits<32> Inst = ?; // Default to an invalid insn.
  bits<4> IClass = 0; // ICLASS
  bits<13> ISubHi = 0; // Low sub-insn
  bits<13> ISubLo = 0; // High sub-insn

  let Inst{31-29} = IClass{3-1};
  let Inst{13}    = IClass{0};
  let Inst{15-14} = 0;
  let Inst{28-16} = ISubHi;
  let Inst{12-0}  = ISubLo;
}

class InstDuplex<bits<4> iClass, list<dag> pattern = [],
                 string cstr = "">
  : Instruction, OpcodeDuplex {
  let Namespace = "Hexagon";
  IType Type = TypeDUPLEX;  // uses slot 0,1
  let isCodeGenOnly = 1;
  let hasSideEffects = 0;
  dag OutOperandList = (outs);
  dag InOperandList = (ins);
  let IClass = iClass;
  let Constraints = cstr;
  let Itinerary = DUPLEX;
  let Size = 4;

  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<32> SoftFail = 0;

  // *** Must match MCTargetDesc/HexagonBaseInfo.h ***

  let TSFlags{6-0} = Type.Value;

  // Predicated instructions.
  bits<1> isPredicated = 0;
  let TSFlags{7} = isPredicated;
  bits<1> isPredicatedFalse = 0;
  let TSFlags{8} = isPredicatedFalse;
  bits<1> isPredicatedNew = 0;
  let TSFlags{9} = isPredicatedNew;

  // New-value insn helper fields.
  bits<1> isNewValue = 0;
  let TSFlags{10} = isNewValue; // New-value consumer insn.
  bits<1> hasNewValue = 0;
  let TSFlags{11} = hasNewValue; // New-value producer insn.
  bits<3> opNewValue = 0;
  let TSFlags{14-12} = opNewValue; // New-value produced operand.
  bits<1> isNVStorable = 0;
  let TSFlags{15} = isNVStorable; // Store that can become new-value store.
  bits<1> isNVStore = 0;
  let TSFlags{16} = isNVStore; // New-value store insn.

  // Immediate extender helper fields.
  bits<1> isExtendable = 0;
  let TSFlags{17} = isExtendable; // Insn may be extended.
  bits<1> isExtended = 0;
  let TSFlags{18} = isExtended; // Insn must be extended.
  bits<3> opExtendable = 0;
  let TSFlags{21-19} = opExtendable; // Which operand may be extended.
  bits<1> isExtentSigned = 0;
  let TSFlags{22} = isExtentSigned; // Signed or unsigned range.
  bits<5> opExtentBits = 0;
  let TSFlags{27-23} = opExtentBits; //Number of bits of range before extending.
  bits<2> opExtentAlign = 0;
  let TSFlags{29-28} = opExtentAlign; // Alignment exponent before extending.
}
