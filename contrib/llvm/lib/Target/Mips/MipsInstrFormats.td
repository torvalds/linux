//===-- MipsInstrFormats.td - Mips Instruction Formats -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe MIPS instructions format
//
//  CPU INSTRUCTION FORMATS
//
//  opcode  - operation code.
//  rs      - src reg.
//  rt      - dst reg (on a 2 regs instr) or src reg (on a 3 reg instr).
//  rd      - dst reg, only used on 3 regs instr.
//  shamt   - only used on shift instructions, contains the shift amount.
//  funct   - combined with opcode field give us an operation code.
//
//===----------------------------------------------------------------------===//

// Format specifies the encoding used by the instruction.  This is part of the
// ad-hoc solution used to emit machine instruction encodings by our machine
// code emitter.
class Format<bits<4> val> {
  bits<4> Value = val;
}

def Pseudo    : Format<0>;
def FrmR      : Format<1>;
def FrmI      : Format<2>;
def FrmJ      : Format<3>;
def FrmFR     : Format<4>;
def FrmFI     : Format<5>;
def FrmOther  : Format<6>; // Instruction w/ a custom format

class MMRel;

def Std2MicroMips : InstrMapping {
  let FilterClass = "MMRel";
  // Instructions with the same BaseOpcode and isNVStore values form a row.
  let RowFields = ["BaseOpcode"];
  // Instructions with the same predicate sense form a column.
  let ColFields = ["Arch"];
  // The key column is the unpredicated instructions.
  let KeyCol = ["se"];
  // Value columns are PredSense=true and PredSense=false
  let ValueCols = [["se"], ["micromips"]];
}

class StdMMR6Rel;

def Std2MicroMipsR6 : InstrMapping {
  let FilterClass = "StdMMR6Rel";
  // Instructions with the same BaseOpcode and isNVStore values form a row.
  let RowFields = ["BaseOpcode"];
  // Instructions with the same predicate sense form a column.
  let ColFields = ["Arch"];
  // The key column is the unpredicated instructions.
  let KeyCol = ["se"];
  // Value columns are PredSense=true and PredSense=false
  let ValueCols = [["se"], ["micromipsr6"]];
}

class StdArch {
  string Arch = "se";
}

// Generic Mips Format
class MipsInst<dag outs, dag ins, string asmstr, list<dag> pattern,
               InstrItinClass itin, Format f>: Instruction, PredicateControl
{
  field bits<32> Inst;
  Format Form = f;

  let Namespace = "Mips";

  let Size = 4;

  bits<6> Opcode = 0;

  // Top 6 bits are the 'opcode' field
  let Inst{31-26} = Opcode;

  let OutOperandList = outs;
  let InOperandList  = ins;

  let AsmString   = asmstr;
  let Pattern     = pattern;
  let Itinerary   = itin;

  //
  // Attributes specific to Mips instructions...
  //
  bits<4> FormBits     = Form.Value;
  bit isCTI            = 0; // Any form of Control Transfer Instruction.
                            // Required for MIPSR6
  bit hasForbiddenSlot = 0; // Instruction has a forbidden slot.
  bit IsPCRelativeLoad = 0; // Load instruction with implicit source register
                            // ($pc) and with explicit offset and destination
                            // register
  bit hasFCCRegOperand = 0; // Instruction uses $fcc<X> register and is
                            // present in MIPS-I to MIPS-III.

  // TSFlags layout should be kept in sync with MCTargetDesc/MipsBaseInfo.h.
  let TSFlags{3-0}   = FormBits;
  let TSFlags{4}     = isCTI;
  let TSFlags{5}     = hasForbiddenSlot;
  let TSFlags{6}     = IsPCRelativeLoad;
  let TSFlags{7}     = hasFCCRegOperand;

  let DecoderNamespace = "Mips";

  field bits<32> SoftFail = 0;
}

// Mips32/64 Instruction Format
class InstSE<dag outs, dag ins, string asmstr, list<dag> pattern,
             InstrItinClass itin, Format f, string opstr = ""> :
  MipsInst<outs, ins, asmstr, pattern, itin, f> {
  let EncodingPredicates = [NotInMips16Mode];
  string BaseOpcode = opstr;
  string Arch;
}

// Mips Pseudo Instructions Format
class MipsPseudo<dag outs, dag ins, list<dag> pattern,
                 InstrItinClass itin = IIPseudo> :
  MipsInst<outs, ins, "", pattern, itin, Pseudo> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}

// Mips32/64 Pseudo Instruction Format
class PseudoSE<dag outs, dag ins, list<dag> pattern,
               InstrItinClass itin = IIPseudo> :
  MipsPseudo<outs, ins, pattern, itin> {
  let EncodingPredicates = [NotInMips16Mode];
}

// Pseudo-instructions for alternate assembly syntax (never used by codegen).
// These are aliases that require C++ handling to convert to the target
// instruction, while InstAliases can be handled directly by tblgen.
class MipsAsmPseudoInst<dag outs, dag ins, string asmstr>:
  MipsInst<outs, ins, asmstr, [], IIPseudo, Pseudo> {
  let isPseudo = 1;
  let Pattern = [];
}
//===----------------------------------------------------------------------===//
// Format R instruction class in Mips : <|opcode|rs|rt|rd|shamt|funct|>
//===----------------------------------------------------------------------===//

class FR<bits<6> op, bits<6> _funct, dag outs, dag ins, string asmstr,
         list<dag> pattern, InstrItinClass itin>:
  InstSE<outs, ins, asmstr, pattern, itin, FrmR>
{
  bits<5>  rd;
  bits<5>  rs;
  bits<5>  rt;
  bits<5>  shamt;
  bits<6>  funct;

  let Opcode = op;
  let funct  = _funct;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = shamt;
  let Inst{5-0}   = funct;
}

//===----------------------------------------------------------------------===//
// Format I instruction class in Mips : <|opcode|rs|rt|immediate|>
//===----------------------------------------------------------------------===//

class FI<bits<6> op, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin>: InstSE<outs, ins, asmstr, pattern, itin, FrmI>
{
  bits<5>  rt;
  bits<5>  rs;
  bits<16> imm16;

  let Opcode = op;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-0}  = imm16;
}

class BranchBase<bits<6> op, dag outs, dag ins, string asmstr,
                  list<dag> pattern, InstrItinClass itin>:
  InstSE<outs, ins, asmstr, pattern, itin, FrmI>
{
  bits<5>  rs;
  bits<5>  rt;
  bits<16> imm16;

  let Opcode = op;

  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-0}  = imm16;
}

//===----------------------------------------------------------------------===//
// Format J instruction class in Mips : <|opcode|address|>
//===----------------------------------------------------------------------===//

class FJ<bits<6> op> : StdArch
{
  bits<26> target;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-0}  = target;
}

//===----------------------------------------------------------------------===//
// MFC instruction class in Mips : <|op|mf|rt|rd|gst|0000|sel|>
//===----------------------------------------------------------------------===//
class MFC3OP_FM<bits<6> op, bits<5> mfmt, bits<3> guest> : StdArch {
  bits<5> rt;
  bits<5> rd;
  bits<3> sel;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = mfmt;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-8}  = guest;
  let Inst{7-3}   = 0;
  let Inst{2-0}   = sel;
}

class MFC2OP_FM<bits<6> op, bits<5> mfmt> : StdArch {
  bits<5>  rt;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = mfmt;
  let Inst{20-16} = rt;
  let Inst{15-0}  = imm16;
}

class ADD_FM<bits<6> op, bits<6> funct> : StdArch {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = 0;
  let Inst{5-0}   = funct;
}

class ADDI_FM<bits<6> op> : StdArch {
  bits<5>  rs;
  bits<5>  rt;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-0}  = imm16;
}

class SRA_FM<bits<6> funct, bit rotate> : StdArch {
  bits<5> rd;
  bits<5> rt;
  bits<5> shamt;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-22} = 0;
  let Inst{21}    = rotate;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = shamt;
  let Inst{5-0}   = funct;
}

class SRLV_FM<bits<6> funct, bit rotate> : StdArch {
  bits<5> rd;
  bits<5> rt;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-7}  = 0;
  let Inst{6}     = rotate;
  let Inst{5-0}   = funct;
}

class BEQ_FM<bits<6> op> : StdArch {
  bits<5>  rs;
  bits<5>  rt;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-0}  = offset;
}

class BGEZ_FM<bits<6> op, bits<5> funct> : StdArch {
  bits<5>  rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rs;
  let Inst{20-16} = funct;
  let Inst{15-0}  = offset;
}

class BBIT_FM<bits<6> op> : StdArch {
  bits<5>  rs;
  bits<5>  p;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rs;
  let Inst{20-16} = p;
  let Inst{15-0}  = offset;
}

class SLTI_FM<bits<6> op> : StdArch {
  bits<5> rt;
  bits<5> rs;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-0}  = imm16;
}

class MFLO_FM<bits<6> funct> : StdArch {
  bits<5> rd;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-16} = 0;
  let Inst{15-11} = rd;
  let Inst{10-6}  = 0;
  let Inst{5-0}   = funct;
}

class MTLO_FM<bits<6> funct> : StdArch {
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rs;
  let Inst{20-6}  = 0;
  let Inst{5-0}   = funct;
}

class SEB_FM<bits<5> funct, bits<6> funct2> : StdArch {
  bits<5> rd;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0x1f;
  let Inst{25-21} = 0;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = funct;
  let Inst{5-0}   = funct2;
}

class CLO_FM<bits<6> funct> : StdArch {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0x1c;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = 0;
  let Inst{5-0}   = funct;
  let rt = rd;
}

class LUI_FM : StdArch {
  bits<5> rt;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = 0xf;
  let Inst{25-21} = 0;
  let Inst{20-16} = rt;
  let Inst{15-0}  = imm16;
}

class JALR_FM {
  bits<5> rd;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rs;
  let Inst{20-16} = 0;
  let Inst{15-11} = rd;
  let Inst{10-6}  = 0;
  let Inst{5-0}   = 9;
}

class BGEZAL_FM<bits<5> funct> : StdArch {
  bits<5>  rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 1;
  let Inst{25-21} = rs;
  let Inst{20-16} = funct;
  let Inst{15-0}  = offset;
}

class SYNC_FM : StdArch {
  bits<5> stype;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{10-6}  = stype;
  let Inst{5-0}   = 0xf;
}

class SYNCI_FM : StdArch {
  // Produced by the mem_simm16 address as reg << 16 | imm (see getMemEncoding).
  bits<21> addr;
  bits<5> rs = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = 0b000001;
  let Inst{25-21} = rs;
  let Inst{20-16} = 0b11111;
  let Inst{15-0}  = offset;
}

class MULT_FM<bits<6> op, bits<6> funct> : StdArch {
  bits<5>  rs;
  bits<5>  rt;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-6}  = 0;
  let Inst{5-0}   = funct;
}

class EXT_FM<bits<6> funct> : StdArch {
  bits<5> rt;
  bits<5> rs;
  bits<5> pos;
  bits<5> size;

  bits<32> Inst;

  let Inst{31-26} = 0x1f;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = size;
  let Inst{10-6}  = pos;
  let Inst{5-0}   = funct;
}

class RDHWR_FM : StdArch {
  bits<5> rt;
  bits<5> rd;
  bits<3> sel;

  bits<32> Inst;

  let Inst{31-26} = 0x1f;
  let Inst{25-21} = 0;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-9}  = 0b00;
  let Inst{8-6}   = sel;
  let Inst{5-0}   = 0x3b;
}

class TEQ_FM<bits<6> funct> : StdArch {
  bits<5> rs;
  bits<5> rt;
  bits<10> code_;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-6}  = code_;
  let Inst{5-0}   = funct;
}

class TEQI_FM<bits<5> funct> : StdArch {
  bits<5> rs;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = 1;
  let Inst{25-21} = rs;
  let Inst{20-16}   = funct;
  let Inst{15-0}  = imm16;
}

class WAIT_FM : StdArch {
  bits<32> Inst;

  let Inst{31-26} = 0x10;
  let Inst{25}    = 1;
  let Inst{24-6}  = 0;
  let Inst{5-0}   = 0x20;
}

class EXTS_FM<bits<6> funct> : StdArch {
  bits<5> rt;
  bits<5> rs;
  bits<5> pos;
  bits<5> lenm1;

  bits<32> Inst;

  let Inst{31-26} = 0x1c;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = lenm1;
  let Inst{10-6}  = pos;
  let Inst{5-0}   = funct;
}

class MTMR_FM<bits<6> funct> : StdArch {
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0x1c;
  let Inst{25-21} = rs;
  let Inst{20-6}  = 0;
  let Inst{5-0}   = funct;
}

class POP_FM<bits<6> funct> : StdArch {
  bits<5> rd;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0x1c;
  let Inst{25-21} = rs;
  let Inst{20-16} = 0;
  let Inst{15-11} = rd;
  let Inst{10-6}  = 0;
  let Inst{5-0}   = funct;
}

class SEQ_FM<bits<6> funct> : StdArch {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0x1c;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-6}  = 0;
  let Inst{5-0}   = funct;
}

class SEQI_FM<bits<6> funct> : StdArch {
  bits<5> rs;
  bits<5> rt;
  bits<10> imm10;

  bits<32> Inst;

  let Inst{31-26} = 0x1c;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-6}  = imm10;
  let Inst{5-0}   = funct;
}

//===----------------------------------------------------------------------===//
//  System calls format <op|code_|funct>
//===----------------------------------------------------------------------===//

class SYS_FM<bits<6> funct> : StdArch
{
  bits<20> code_;
  bits<32> Inst;
  let Inst{31-26} = 0x0;
  let Inst{25-6} = code_;
  let Inst{5-0}  = funct;
}

//===----------------------------------------------------------------------===//
//  Break instruction format <op|code_1|funct>
//===----------------------------------------------------------------------===//

class BRK_FM<bits<6> funct> : StdArch
{
  bits<10> code_1;
  bits<10> code_2;
  bits<32> Inst;
  let Inst{31-26} = 0x0;
  let Inst{25-16} = code_1;
  let Inst{15-6}  = code_2;
  let Inst{5-0}   = funct;
}

//===----------------------------------------------------------------------===//
//  Exception return format <Cop0|1|0|funct>
//===----------------------------------------------------------------------===//

class ER_FM<bits<6> funct, bit LLBit> : StdArch
{
  bits<32> Inst;
  let Inst{31-26} = 0x10;
  let Inst{25}    = 1;
  let Inst{24-7}  = 0;
  let Inst{6} = LLBit;
  let Inst{5-0}   = funct;
}

//===----------------------------------------------------------------------===//
//  Enable/disable interrupt instruction format <Cop0|MFMC0|rt|12|0|sc|0|0>
//===----------------------------------------------------------------------===//

class EI_FM<bits<1> sc> : StdArch
{
  bits<32> Inst;
  bits<5> rt;
  let Inst{31-26} = 0x10;
  let Inst{25-21} = 0xb;
  let Inst{20-16} = rt;
  let Inst{15-11} = 0xc;
  let Inst{10-6}  = 0;
  let Inst{5}     = sc;
  let Inst{4-0}   = 0;
}

//===----------------------------------------------------------------------===//
//
//  FLOATING POINT INSTRUCTION FORMATS
//
//  opcode  - operation code.
//  fs      - src reg.
//  ft      - dst reg (on a 2 regs instr) or src reg (on a 3 reg instr).
//  fd      - dst reg, only used on 3 regs instr.
//  fmt     - double or single precision.
//  funct   - combined with opcode field give us an operation code.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Format FI instruction class in Mips : <|opcode|base|ft|immediate|>
//===----------------------------------------------------------------------===//

class FFI<bits<6> op, dag outs, dag ins, string asmstr, list<dag> pattern>:
  InstSE<outs, ins, asmstr, pattern, NoItinerary, FrmFI>
{
  bits<5>  ft;
  bits<5>  base;
  bits<16> imm16;

  let Opcode = op;

  let Inst{25-21} = base;
  let Inst{20-16} = ft;
  let Inst{15-0}  = imm16;
}

class ADDS_FM<bits<6> funct, bits<5> fmt> : StdArch {
  bits<5> fd;
  bits<5> fs;
  bits<5> ft;

  bits<32> Inst;

  let Inst{31-26} = 0x11;
  let Inst{25-21} = fmt;
  let Inst{20-16} = ft;
  let Inst{15-11} = fs;
  let Inst{10-6}  = fd;
  let Inst{5-0}   = funct;
}

class ABSS_FM<bits<6> funct, bits<5> fmt> : StdArch {
  bits<5> fd;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0x11;
  let Inst{25-21} = fmt;
  let Inst{20-16} = 0;
  let Inst{15-11} = fs;
  let Inst{10-6}  = fd;
  let Inst{5-0}   = funct;
}

class MFC1_FM<bits<5> funct> : StdArch {
  bits<5> rt;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0x11;
  let Inst{25-21} = funct;
  let Inst{20-16} = rt;
  let Inst{15-11} = fs;
  let Inst{10-0}  = 0;
}

class LW_FM<bits<6> op> : StdArch {
  bits<5> rt;
  bits<21> addr;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = addr{20-16};
  let Inst{20-16} = rt;
  let Inst{15-0}  = addr{15-0};
}

class MADDS_FM<bits<3> funct, bits<3> fmt> : StdArch {
  bits<5> fd;
  bits<5> fr;
  bits<5> fs;
  bits<5> ft;

  bits<32> Inst;

  let Inst{31-26} = 0x13;
  let Inst{25-21} = fr;
  let Inst{20-16} = ft;
  let Inst{15-11} = fs;
  let Inst{10-6}  = fd;
  let Inst{5-3}   = funct;
  let Inst{2-0}   = fmt;
}

class LWXC1_FM<bits<6> funct> : StdArch {
  bits<5> fd;
  bits<5> base;
  bits<5> index;

  bits<32> Inst;

  let Inst{31-26} = 0x13;
  let Inst{25-21} = base;
  let Inst{20-16} = index;
  let Inst{15-11} = 0;
  let Inst{10-6}  = fd;
  let Inst{5-0}   = funct;
}

class SWXC1_FM<bits<6> funct> : StdArch {
  bits<5> fs;
  bits<5> base;
  bits<5> index;

  bits<32> Inst;

  let Inst{31-26} = 0x13;
  let Inst{25-21} = base;
  let Inst{20-16} = index;
  let Inst{15-11} = fs;
  let Inst{10-6}  = 0;
  let Inst{5-0}   = funct;
}

class BC1F_FM<bit nd, bit tf> : StdArch {
  bits<3>  fcc;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0x11;
  let Inst{25-21} = 0x8;
  let Inst{20-18} = fcc;
  let Inst{17} = nd;
  let Inst{16} = tf;
  let Inst{15-0} = offset;
}

class CEQS_FM<bits<5> fmt> : StdArch {
  bits<5> fs;
  bits<5> ft;
  bits<3> fcc;
  bits<4> cond;

  bits<32> Inst;

  let Inst{31-26} = 0x11;
  let Inst{25-21} = fmt;
  let Inst{20-16} = ft;
  let Inst{15-11} = fs;
  let Inst{10-8} = fcc;
  let Inst{7-4} = 0x3;
  let Inst{3-0} = cond;
}

class C_COND_FM<bits<5> fmt, bits<4> c> : CEQS_FM<fmt> {
  let cond = c;
}

class CMov_I_F_FM<bits<6> funct, bits<5> fmt> : StdArch {
  bits<5> fd;
  bits<5> fs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0x11;
  let Inst{25-21} = fmt;
  let Inst{20-16} = rt;
  let Inst{15-11} = fs;
  let Inst{10-6} = fd;
  let Inst{5-0} = funct;
}

class CMov_F_I_FM<bit tf> : StdArch {
  bits<5> rd;
  bits<5> rs;
  bits<3> fcc;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rs;
  let Inst{20-18} = fcc;
  let Inst{17} = 0;
  let Inst{16} = tf;
  let Inst{15-11} = rd;
  let Inst{10-6} = 0;
  let Inst{5-0} = 1;
}

class CMov_F_F_FM<bits<5> fmt, bit tf> : StdArch {
  bits<5> fd;
  bits<5> fs;
  bits<3> fcc;

  bits<32> Inst;

  let Inst{31-26} = 0x11;
  let Inst{25-21} = fmt;
  let Inst{20-18} = fcc;
  let Inst{17} = 0;
  let Inst{16} = tf;
  let Inst{15-11} = fs;
  let Inst{10-6} = fd;
  let Inst{5-0} = 0x11;
}

class BARRIER_FM<bits<5> op> : StdArch {
  bits<32> Inst;

  let Inst{31-26} = 0; // SPECIAL
  let Inst{25-21} = 0;
  let Inst{20-16} = 0; // rt = 0
  let Inst{15-11} = 0; // rd = 0
  let Inst{10-6} = op; // Operation
  let Inst{5-0} = 0;   // SLL
}

class SDBBP_FM : StdArch {
  bits<20> code_;

  bits<32> Inst;

  let Inst{31-26} = 0b011100; // SPECIAL2
  let Inst{25-6} = code_;
  let Inst{5-0} = 0b111111;   // SDBBP
}

class JR_HB_FM<bits<6> op> : StdArch{
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0; // SPECIAL
  let Inst{25-21} = rs;
  let Inst{20-11} = 0;
  let Inst{10} = 1;
  let Inst{9-6} = 0;
  let Inst{5-0} = op;
}

class JALR_HB_FM<bits<6> op> : StdArch {
  bits<5> rd;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0; // SPECIAL
  let Inst{25-21} = rs;
  let Inst{20-16} = 0;
  let Inst{15-11} = rd;
  let Inst{10} = 1;
  let Inst{9-6} = 0;
  let Inst{5-0} = op;
}

class COP0_TLB_FM<bits<6> op> : StdArch {
  bits<32> Inst;

  let Inst{31-26} = 0x10; // COP0
  let Inst{25} = 1;       // CO
  let Inst{24-6} = 0;
  let Inst{5-0} = op;     // Operation
}

class CACHEOP_FM<bits<6> op> : StdArch {
  bits<21> addr;
  bits<5> hint;
  bits<5> base = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = base;
  let Inst{20-16} = hint;
  let Inst{15-0}  = offset;
}

class HYPCALL_FM<bits<6> op> : StdArch {
  bits<10> code_;

  bits<32> Inst;

  let Inst{31-26} = 0b010000;
  let Inst{25}    = 1;
  let Inst{20-11} = code_;
  let Inst{5-0}   = op;
}
