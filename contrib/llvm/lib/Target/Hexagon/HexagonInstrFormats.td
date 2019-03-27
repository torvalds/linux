//==- HexagonInstrFormats.td - Hexagon Instruction Formats --*- tablegen -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Addressing modes for load/store instructions
class AddrModeType<bits<3> value> {
  bits<3> Value = value;
}

def NoAddrMode     : AddrModeType<0>;  // No addressing mode
def Absolute       : AddrModeType<1>;  // Absolute addressing mode
def AbsoluteSet    : AddrModeType<2>;  // Absolute set addressing mode
def BaseImmOffset  : AddrModeType<3>;  // Indirect with offset
def BaseLongOffset : AddrModeType<4>;  // Indirect with long offset
def BaseRegOffset  : AddrModeType<5>;  // Indirect with register offset
def PostInc        : AddrModeType<6>;  // Post increment addressing mode

class MemAccessSize<bits<4> value> {
  bits<4> Value = value;
}

// These numbers must match the MemAccessSize enumeration values in
// HexagonBaseInfo.h.
def NoMemAccess      : MemAccessSize<0>;
def ByteAccess       : MemAccessSize<1>;
def HalfWordAccess   : MemAccessSize<2>;
def WordAccess       : MemAccessSize<3>;
def DoubleWordAccess : MemAccessSize<4>;
def HVXVectorAccess  : MemAccessSize<5>;


//===----------------------------------------------------------------------===//
//                         Instruction Class Declaration +
//===----------------------------------------------------------------------===//

class OpcodeHexagon {
  field bits<32> Inst = ?; // Default to an invalid insn.
  bits<4> IClass = 0; // ICLASS
  bits<1> zero = 0;

  let Inst{31-28} = IClass;
}

class InstHexagon<dag outs, dag ins, string asmstr, list<dag> pattern,
                  string cstr, InstrItinClass itin, IType type>
  : Instruction {
  let Namespace = "Hexagon";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Constraints = cstr;
  let Itinerary = itin;
  let Size = 4;

  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<32> SoftFail = 0;

  // *** Must match MCTargetDesc/HexagonBaseInfo.h ***

  // Instruction type according to the ISA.
  IType Type = type;
  let TSFlags{6-0} = Type.Value;

  // Solo instructions, i.e., those that cannot be in a packet with others.
  bits<1> isSolo = 0;
  let TSFlags{7} = isSolo;
  // Packed only with A or X-type instructions.
  bits<1> isSoloAX = 0;
  let TSFlags{8} = isSoloAX;
  // Restricts slot 1 to ALU-only instructions.
  bits<1> isRestrictSlot1AOK = 0;
  let TSFlags{9} = isRestrictSlot1AOK;

  // Predicated instructions.
  bits<1> isPredicated = 0;
  let TSFlags{10} = isPredicated;
  bits<1> isPredicatedFalse = 0;
  let TSFlags{11} = isPredicatedFalse;
  bits<1> isPredicatedNew = 0;
  let TSFlags{12} = isPredicatedNew;
  bits<1> isPredicateLate = 0;
  let TSFlags{13} = isPredicateLate; // Late predicate producer insn.

  // New-value insn helper fields.
  bits<1> isNewValue = 0;
  let TSFlags{14} = isNewValue; // New-value consumer insn.
  bits<1> hasNewValue = 0;
  let TSFlags{15} = hasNewValue; // New-value producer insn.
  bits<3> opNewValue = 0;
  let TSFlags{18-16} = opNewValue; // New-value produced operand.
  bits<1> isNVStorable = 0;
  let TSFlags{19} = isNVStorable; // Store that can become new-value store.
  bits<1> isNVStore = 0;
  let TSFlags{20} = isNVStore; // New-value store insn.
  bits<1> isCVLoadable = 0;
  let TSFlags{21} = isCVLoadable; // Load that can become cur-value load.
  bits<1> isCVLoad = 0;
  let TSFlags{22} = isCVLoad; // Cur-value load insn.

  // Immediate extender helper fields.
  bits<1> isExtendable = 0;
  let TSFlags{23} = isExtendable; // Insn may be extended.
  bits<1> isExtended = 0;
  let TSFlags{24} = isExtended; // Insn must be extended.
  bits<3> opExtendable = 0;
  let TSFlags{27-25} = opExtendable; // Which operand may be extended.
  bits<1> isExtentSigned = 0;
  let TSFlags{28} = isExtentSigned; // Signed or unsigned range.
  bits<5> opExtentBits = 0;
  let TSFlags{33-29} = opExtentBits; //Number of bits of range before extending.
  bits<2> opExtentAlign = 0;
  let TSFlags{35-34} = opExtentAlign; // Alignment exponent before extending.

  bit cofMax1 = 0;
  let TSFlags{36} = cofMax1;
  bit cofRelax1 = 0;
  let TSFlags{37} = cofRelax1;
  bit cofRelax2 = 0;
  let TSFlags{38} = cofRelax2;

  bit isRestrictNoSlot1Store = 0;
  let TSFlags{39} = isRestrictNoSlot1Store;

  // Addressing mode for load/store instructions.
  AddrModeType addrMode = NoAddrMode;
  let TSFlags{44-42} = addrMode.Value;

  // Memory access size for mem access instructions (load/store)
  MemAccessSize accessSize = NoMemAccess;
  let TSFlags{48-45} = accessSize.Value;

  bits<1> isTaken = 0;
  let TSFlags {49} = isTaken; // Branch prediction.

  bits<1> isFP = 0;
  let TSFlags {50} = isFP; // Floating-point.

  bits<1> isSomeOK = 0;
  let TSFlags {51} = isSomeOK; // Relax some grouping constraints.

  bits<1> hasNewValue2 = 0;
  let TSFlags{52} = hasNewValue2; // Second New-value producer insn.
  bits<3> opNewValue2 = 0;
  let TSFlags{55-53} = opNewValue2; // Second New-value produced operand.

  bits<1> isAccumulator = 0;
  let TSFlags{56} = isAccumulator;

  bits<1> prefersSlot3 = 0;
  let TSFlags{57} = prefersSlot3; // Complex XU

  bits<1> hasTmpDst = 0;
  let TSFlags{60} = hasTmpDst;  // v65 : 'fake" register VTMP is set

  bit CVINew = 0;
  let TSFlags{62} = CVINew;

  // Fields used for relation models.
  bit isNonTemporal = 0;
  string isNT = ""; // set to "true" for non-temporal vector stores.
  string BaseOpcode = "";
  string CextOpcode = "";
  string PredSense = "";
  string PNewValue = "";
  string NValueST  = "";    // Set to "true" for new-value stores.
  string InputType = "";    // Input is "imm" or "reg" type.
  string isFloat = "false"; // Set to "true" for the floating-point load/store.
  string isBrTaken = !if(isTaken, "true", "false"); // Set to "true"/"false" for jump instructions

  let PredSense = !if(isPredicated, !if(isPredicatedFalse, "false", "true"),
                                    "");
  let PNewValue = !if(isPredicatedNew, "new", "");
  let NValueST = !if(isNVStore, "true", "false");
  let isNT = !if(isNonTemporal, "true", "false");

  let hasSideEffects = 0;
  // *** Must match MCTargetDesc/HexagonBaseInfo.h ***
}

class HInst<dag outs, dag ins, string asmstr, InstrItinClass itin, IType type> :
      InstHexagon<outs, ins, asmstr, [], "", itin, type>;

//===----------------------------------------------------------------------===//
//                         Instruction Classes Definitions +
//===----------------------------------------------------------------------===//

let mayLoad = 1 in
class LDInst<dag outs, dag ins, string asmstr, list<dag> pattern = [],
             string cstr = "", InstrItinClass itin = LD_tc_ld_SLOT01>
  : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeLD>, OpcodeHexagon;

class CONSTLDInst<dag outs, dag ins, string asmstr, list<dag> pattern = [],
             string cstr = "", InstrItinClass itin = LD_tc_ld_SLOT01>
  : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeLD>, OpcodeHexagon;

let mayStore = 1 in
class STInst<dag outs, dag ins, string asmstr, list<dag> pattern = [],
             string cstr = "", InstrItinClass itin = ST_tc_st_SLOT01>
  : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeST>, OpcodeHexagon;

let isCodeGenOnly = 1, isPseudo = 1 in
class Endloop<dag outs, dag ins, string asmstr, list<dag> pattern = [],
              string cstr = "", InstrItinClass itin = tc_ENDLOOP>
  : InstHexagon<outs, ins, asmstr, pattern, cstr, itin, TypeENDLOOP>,
    OpcodeHexagon;

let isCodeGenOnly = 1, isPseudo = 1 in
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern = [],
             string cstr = "">
  : InstHexagon<outs, ins, asmstr, pattern, cstr, PSEUDO, TypePSEUDO>,
    OpcodeHexagon;

let isCodeGenOnly = 1, isPseudo = 1 in
class PseudoM<dag outs, dag ins, string asmstr, list<dag> pattern = [],
              string cstr="">
  : InstHexagon<outs, ins, asmstr, pattern, cstr, PSEUDOM, TypePSEUDO>,
    OpcodeHexagon;

//===----------------------------------------------------------------------===//
//                         Instruction Classes Definitions -
//===----------------------------------------------------------------------===//

include "HexagonInstrFormatsV5.td"
include "HexagonInstrFormatsV60.td"
include "HexagonInstrFormatsV65.td"
