//==- SystemZInstrFormats.td - SystemZ Instruction Formats --*- tablegen -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Basic SystemZ instruction definition
//===----------------------------------------------------------------------===//

class InstSystemZ<int size, dag outs, dag ins, string asmstr,
                  list<dag> pattern> : Instruction {
  let Namespace = "SystemZ";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let Size = size;
  let Pattern = pattern;
  let AsmString = asmstr;

  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;

  // Some instructions come in pairs, one having a 12-bit displacement
  // and the other having a 20-bit displacement.  Both instructions in
  // the pair have the same DispKey and their DispSizes are "12" and "20"
  // respectively.
  string DispKey = "";
  string DispSize = "none";

  // Many register-based <INSN>R instructions have a memory-based <INSN>
  // counterpart.  OpKey uniquely identifies <INSN>R, while OpType is
  // "reg" for <INSN>R and "mem" for <INSN>.
  string OpKey = "";
  string OpType = "none";

  // Many distinct-operands instructions have older 2-operand equivalents.
  // NumOpsKey uniquely identifies one of these 2-operand and 3-operand pairs,
  // with NumOpsValue being "2" or "3" as appropriate.
  string NumOpsKey = "";
  string NumOpsValue = "none";

  // True if this instruction is a simple D(X,B) load of a register
  // (with no sign or zero extension).
  bit SimpleBDXLoad = 0;

  // True if this instruction is a simple D(X,B) store of a register
  // (with no truncation).
  bit SimpleBDXStore = 0;

  // True if this instruction has a 20-bit displacement field.
  bit Has20BitOffset = 0;

  // True if addresses in this instruction have an index register.
  bit HasIndex = 0;

  // True if this is a 128-bit pseudo instruction that combines two 64-bit
  // operations.
  bit Is128Bit = 0;

  // The access size of all memory operands in bytes, or 0 if not known.
  bits<5> AccessBytes = 0;

  // If the instruction sets CC to a useful value, this gives the mask
  // of all possible CC results.  The mask has the same form as
  // SystemZ::CCMASK_*.
  bits<4> CCValues = 0;

  // The subset of CCValues that have the same meaning as they would after
  // a comparison of the first operand against zero.
  bits<4> CompareZeroCCMask = 0;

  // True if the instruction is conditional and if the CC mask operand
  // comes first (as for BRC, etc.).
  bit CCMaskFirst = 0;

  // Similar, but true if the CC mask operand comes last (as for LOC, etc.).
  bit CCMaskLast = 0;

  // True if the instruction is the "logical" rather than "arithmetic" form,
  // in cases where a distinction exists.
  bit IsLogical = 0;

  let TSFlags{0}     = SimpleBDXLoad;
  let TSFlags{1}     = SimpleBDXStore;
  let TSFlags{2}     = Has20BitOffset;
  let TSFlags{3}     = HasIndex;
  let TSFlags{4}     = Is128Bit;
  let TSFlags{9-5}   = AccessBytes;
  let TSFlags{13-10} = CCValues;
  let TSFlags{17-14} = CompareZeroCCMask;
  let TSFlags{18}    = CCMaskFirst;
  let TSFlags{19}    = CCMaskLast;
  let TSFlags{20}    = IsLogical;
}

//===----------------------------------------------------------------------===//
// Mappings between instructions
//===----------------------------------------------------------------------===//

// Return the version of an instruction that has an unsigned 12-bit
// displacement.
def getDisp12Opcode : InstrMapping {
  let FilterClass = "InstSystemZ";
  let RowFields = ["DispKey"];
  let ColFields = ["DispSize"];
  let KeyCol = ["20"];
  let ValueCols = [["12"]];
}

// Return the version of an instruction that has a signed 20-bit displacement.
def getDisp20Opcode : InstrMapping {
  let FilterClass = "InstSystemZ";
  let RowFields = ["DispKey"];
  let ColFields = ["DispSize"];
  let KeyCol = ["12"];
  let ValueCols = [["20"]];
}

// Return the memory form of a register instruction.
def getMemOpcode : InstrMapping {
  let FilterClass = "InstSystemZ";
  let RowFields = ["OpKey"];
  let ColFields = ["OpType"];
  let KeyCol = ["reg"];
  let ValueCols = [["mem"]];
}

// Return the 3-operand form of a 2-operand instruction.
def getThreeOperandOpcode : InstrMapping {
  let FilterClass = "InstSystemZ";
  let RowFields = ["NumOpsKey"];
  let ColFields = ["NumOpsValue"];
  let KeyCol = ["2"];
  let ValueCols = [["3"]];
}

//===----------------------------------------------------------------------===//
// Instruction formats
//===----------------------------------------------------------------------===//
//
// Formats are specified using operand field declarations of the form:
//
//   bits<4> Rn   : register input or output for operand n
//   bits<5> Vn   : vector register input or output for operand n
//   bits<m> In   : immediate value of width m for operand n
//   bits<4> BDn  : address operand n, which has a base and a displacement
//   bits<m> XBDn : address operand n, which has an index, a base and a
//                  displacement
//   bits<m> VBDn : address operand n, which has a vector index, a base and a
//                  displacement
//   bits<4> Xn   : index register for address operand n
//   bits<4> Mn   : mode value for operand n
//
// The operand numbers ("n" in the list above) follow the architecture manual.
// Assembly operands sometimes have a different order; in particular, R3 often
// is often written between operands 1 and 2.
//
//===----------------------------------------------------------------------===//

class InstE<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<2, outs, ins, asmstr, pattern> {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;

  let Inst = op;
}

class InstI<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<2, outs, ins, asmstr, pattern> {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;

  bits<8> I1;

  let Inst{15-8} = op;
  let Inst{7-0}  = I1;
}

class InstIE<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> I1;
  bits<4> I2;

  let Inst{31-16} = op;
  let Inst{15-8}  = 0;
  let Inst{7-4}   = I1;
  let Inst{3-0}   = I2;
}

class InstMII<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> M1;
  bits<12> RI2;
  bits<24> RI3;

  let Inst{47-40} = op;
  let Inst{39-36} = M1;
  let Inst{35-24} = RI2;
  let Inst{23-0}  = RI3;
}

class InstRIa<bits<12> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<16> I2;

  let Inst{31-24} = op{11-4};
  let Inst{23-20} = R1;
  let Inst{19-16} = op{3-0};
  let Inst{15-0}  = I2;
}

class InstRIb<bits<12> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<16> RI2;

  let Inst{31-24} = op{11-4};
  let Inst{23-20} = R1;
  let Inst{19-16} = op{3-0};
  let Inst{15-0}  = RI2;
}

class InstRIc<bits<12> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> M1;
  bits<16> RI2;

  let Inst{31-24} = op{11-4};
  let Inst{23-20} = M1;
  let Inst{19-16} = op{3-0};
  let Inst{15-0}  = RI2;
}

class InstRIEa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<16> I2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = 0;
  let Inst{31-16} = I2;
  let Inst{15-12} = M3;
  let Inst{11-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstRIEb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<4> M3;
  bits<16> RI4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = R2;
  let Inst{31-16} = RI4;
  let Inst{15-12} = M3;
  let Inst{11-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstRIEc<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<8> I2;
  bits<4> M3;
  bits<16> RI4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = M3;
  let Inst{31-16} = RI4;
  let Inst{15-8}  = I2;
  let Inst{7-0}   = op{7-0};
}

class InstRIEd<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> R3;
  bits<16> I2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = R3;
  let Inst{31-16} = I2;
  let Inst{15-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstRIEe<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> R3;
  bits<16> RI2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = R3;
  let Inst{31-16} = RI2;
  let Inst{15-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstRIEf<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<8> I3;
  bits<8> I4;
  bits<8> I5;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = R2;
  let Inst{31-24} = I3;
  let Inst{23-16} = I4;
  let Inst{15-8}  = I5;
  let Inst{7-0}   = op{7-0};
}

class InstRIEg<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> M3;
  bits<16> I2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = M3;
  let Inst{31-16} = I2;
  let Inst{15-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstRILa<bits<12> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<32> I2;

  let Inst{47-40} = op{11-4};
  let Inst{39-36} = R1;
  let Inst{35-32} = op{3-0};
  let Inst{31-0}  = I2;
}

class InstRILb<bits<12> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<32> RI2;

  let Inst{47-40} = op{11-4};
  let Inst{39-36} = R1;
  let Inst{35-32} = op{3-0};
  let Inst{31-0}  = RI2;
}

class InstRILc<bits<12> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> M1;
  bits<32> RI2;

  let Inst{47-40} = op{11-4};
  let Inst{39-36} = M1;
  let Inst{35-32} = op{3-0};
  let Inst{31-0}  = RI2;
}

class InstRIS<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<8> I2;
  bits<4> M3;
  bits<16> BD4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = M3;
  let Inst{31-16} = BD4;
  let Inst{15-8}  = I2;
  let Inst{7-0}   = op{7-0};
}

class InstRR<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<2, outs, ins, asmstr, pattern> {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;

  let Inst{15-8} = op;
  let Inst{7-4}  = R1;
  let Inst{3-0}  = R2;
}

class InstRRD<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R3;
  bits<4> R2;

  let Inst{31-16} = op;
  let Inst{15-12} = R1;
  let Inst{11-8}  = 0;
  let Inst{7-4}   = R3;
  let Inst{3-0}   = R2;
}

class InstRRE<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;

  let Inst{31-16} = op;
  let Inst{15-8}  = 0;
  let Inst{7-4}   = R1;
  let Inst{3-0}   = R2;
}

class InstRRFa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<4> R3;
  bits<4> M4;

  let Inst{31-16} = op;
  let Inst{15-12} = R3;
  let Inst{11-8}  = M4;
  let Inst{7-4}   = R1;
  let Inst{3-0}   = R2;
}

class InstRRFb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<4> R3;
  bits<4> M4;

  let Inst{31-16} = op;
  let Inst{15-12} = R3;
  let Inst{11-8}  = M4;
  let Inst{7-4}   = R1;
  let Inst{3-0}   = R2;
}

class InstRRFc<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<4> M3;

  let Inst{31-16} = op;
  let Inst{15-12} = M3;
  let Inst{11-8}  = 0;
  let Inst{7-4}   = R1;
  let Inst{3-0}   = R2;
}

class InstRRFd<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<4> M4;

  let Inst{31-16} = op;
  let Inst{15-12} = 0;
  let Inst{11-8}  = M4;
  let Inst{7-4}   = R1;
  let Inst{3-0}   = R2;
}

class InstRRFe<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<4> M3;
  bits<4> M4;

  let Inst{31-16} = op;
  let Inst{15-12} = M3;
  let Inst{11-8}  = M4;
  let Inst{7-4}   = R1;
  let Inst{3-0}   = R2;
}

class InstRRS<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> R2;
  bits<4> M3;
  bits<16> BD4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = R2;
  let Inst{31-16} = BD4;
  let Inst{15-12} = M3;
  let Inst{11-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstRXa<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<20> XBD2;

  let Inst{31-24} = op;
  let Inst{23-20} = R1;
  let Inst{19-0}  = XBD2;

  let HasIndex = 1;
}

class InstRXb<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> M1;
  bits<20> XBD2;

  let Inst{31-24} = op;
  let Inst{23-20} = M1;
  let Inst{19-0}  = XBD2;

  let HasIndex = 1;
}

class InstRXE<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<20> XBD2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-16} = XBD2;
  let Inst{15-12} = M3;
  let Inst{11-8}  = 0;
  let Inst{7-0}   = op{7-0};

  let HasIndex = 1;
}

class InstRXF<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> R3;
  bits<20> XBD2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R3;
  let Inst{35-16} = XBD2;
  let Inst{15-12} = R1;
  let Inst{11-8}  = 0;
  let Inst{7-0}   = op{7-0};

  let HasIndex = 1;
}

class InstRXYa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<28> XBD2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-8}  = XBD2;
  let Inst{7-0}   = op{7-0};

  let Has20BitOffset = 1;
  let HasIndex = 1;
}

class InstRXYb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> M1;
  bits<28> XBD2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = M1;
  let Inst{35-8}  = XBD2;
  let Inst{7-0}   = op{7-0};

  let Has20BitOffset = 1;
  let HasIndex = 1;
}

class InstRSa<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R3;
  bits<16> BD2;

  let Inst{31-24} = op;
  let Inst{23-20} = R1;
  let Inst{19-16} = R3;
  let Inst{15-0}  = BD2;
}

class InstRSb<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> M3;
  bits<16> BD2;

  let Inst{31-24} = op;
  let Inst{23-20} = R1;
  let Inst{19-16} = M3;
  let Inst{15-0}  = BD2;
}

class InstRSI<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<4> R1;
  bits<4> R3;
  bits<16> RI2;

  let Inst{31-24} = op;
  let Inst{23-20} = R1;
  let Inst{19-16} = R3;
  let Inst{15-0}  = RI2;
}

class InstRSLa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<20> BDL1;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = BDL1{19-16};
  let Inst{35-32} = 0;
  let Inst{31-16} = BDL1{15-0};
  let Inst{15-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstRSLb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<24> BDL2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-16} = BDL2;
  let Inst{15-12} = R1;
  let Inst{11-8}  = M3;
  let Inst{7-0}   = op{7-0};
}

class InstRSYa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> R3;
  bits<24> BD2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = R3;
  let Inst{31-8}  = BD2;
  let Inst{7-0}   = op{7-0};

  let Has20BitOffset = 1;
}

class InstRSYb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<4> M3;
  bits<24> BD2;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = M3;
  let Inst{31-8}  = BD2;
  let Inst{7-0}   = op{7-0};

  let Has20BitOffset = 1;
}

class InstSI<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<16> BD1;
  bits<8> I2;

  let Inst{31-24} = op;
  let Inst{23-16} = I2;
  let Inst{15-0}  = BD1;
}

class InstSIL<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<16> BD1;
  bits<16> I2;

  let Inst{47-32} = op;
  let Inst{31-16} = BD1;
  let Inst{15-0}  = I2;
}

class InstSIY<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<24> BD1;
  bits<8> I2;

  let Inst{47-40} = op{15-8};
  let Inst{39-32} = I2;
  let Inst{31-8}  = BD1;
  let Inst{7-0}   = op{7-0};

  let Has20BitOffset = 1;
}

class InstSMI<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> M1;
  bits<16> RI2;
  bits<16> BD3;

  let Inst{47-40} = op;
  let Inst{39-36} = M1;
  let Inst{35-32} = 0;
  let Inst{31-16} = BD3;
  let Inst{15-0}  = RI2;
}

class InstSSa<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<24> BDL1;
  bits<16> BD2;

  let Inst{47-40} = op;
  let Inst{39-16} = BDL1;
  let Inst{15-0}  = BD2;
}

class InstSSb<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<20> BDL1;
  bits<20> BDL2;

  let Inst{47-40} = op;
  let Inst{39-36} = BDL1{19-16};
  let Inst{35-32} = BDL2{19-16};
  let Inst{31-16} = BDL1{15-0};
  let Inst{15-0}  = BDL2{15-0};
}

class InstSSc<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<20> BDL1;
  bits<16> BD2;
  bits<4> I3;

  let Inst{47-40} = op;
  let Inst{39-36} = BDL1{19-16};
  let Inst{35-32} = I3;
  let Inst{31-16} = BDL1{15-0};
  let Inst{15-0}  = BD2;
}

class InstSSd<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<20> RBD1;
  bits<16> BD2;
  bits<4> R3;

  let Inst{47-40} = op;
  let Inst{39-36} = RBD1{19-16};
  let Inst{35-32} = R3;
  let Inst{31-16} = RBD1{15-0};
  let Inst{15-0}  = BD2;
}

class InstSSe<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<16> BD2;
  bits<4> R3;
  bits<16> BD4;

  let Inst{47-40} = op;
  let Inst{39-36} = R1;
  let Inst{35-32} = R3;
  let Inst{31-16} = BD2;
  let Inst{15-0}  = BD4;
}

class InstSSf<bits<8> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<16> BD1;
  bits<24> BDL2;

  let Inst{47-40} = op;
  let Inst{39-32} = BDL2{23-16};
  let Inst{31-16} = BD1;
  let Inst{15-0}  = BDL2{15-0};
}

class InstSSE<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<16> BD1;
  bits<16> BD2;

  let Inst{47-32} = op;
  let Inst{31-16} = BD1;
  let Inst{15-0}  = BD2;
}

class InstSSF<bits<12> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<16> BD1;
  bits<16> BD2;
  bits<4>  R3;

  let Inst{47-40} = op{11-4};
  let Inst{39-36} = R3;
  let Inst{35-32} = op{3-0};
  let Inst{31-16} = BD1;
  let Inst{15-0}  = BD2;
}

class InstS<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;

  bits<16> BD2;

  let Inst{31-16} = op;
  let Inst{15-0}  = BD2;
}

class InstVRIa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<16> I2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = 0;
  let Inst{31-16} = I2;
  let Inst{15-12} = M3;
  let Inst{11}    = V1{4};
  let Inst{10-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRIb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<8> I2;
  bits<8> I3;
  bits<4> M4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = 0;
  let Inst{31-24} = I2;
  let Inst{23-16} = I3;
  let Inst{15-12} = M4;
  let Inst{11}    = V1{4};
  let Inst{10-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRIc<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V3;
  bits<16> I2;
  bits<4> M4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V3{3-0};
  let Inst{31-16} = I2;
  let Inst{15-12} = M4;
  let Inst{11}    = V1{4};
  let Inst{10}    = V3{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRId<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<5> V3;
  bits<8> I4;
  bits<4> M5;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-28} = V3{3-0};
  let Inst{27-24} = 0;
  let Inst{23-16} = I4;
  let Inst{15-12} = M5;
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9}     = V3{4};
  let Inst{8}     = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRIe<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<12> I3;
  bits<4> M4;
  bits<4> M5;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-20} = I3;
  let Inst{19-16} = M5;
  let Inst{15-12} = M4;
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRIf<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<5> V3;
  bits<8> I4;
  bits<4> M5;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-28} = V3{3-0};
  let Inst{27-24} = 0;
  let Inst{23-20} = M5;
  let Inst{19-12} = I4;
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9}     = V3{4};
  let Inst{8}     = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRIg<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<8> I3;
  bits<8> I4;
  bits<4> M5;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-24} = I4;
  let Inst{23-20} = M5;
  let Inst{19-12} = I3;
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRIh<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<16> I2;
  bits<4> I3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = 0;
  let Inst{31-16} = I2;
  let Inst{15-12} = I3;
  let Inst{11}    = V1{4};
  let Inst{10-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRIi<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<4> R2;
  bits<8> I3;
  bits<4> M4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = R2;
  let Inst{31-24} = 0;
  let Inst{23-20} = M4;
  let Inst{19-12} = I3;
  let Inst{11}    = V1{4};
  let Inst{10-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

// Depending on the instruction mnemonic, certain bits may be or-ed into
// the M4 value provided as explicit operand.  These are passed as m4or.
class InstVRRa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern,
               bits<4> m4or = 0>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<4> M3;
  bits<4> M4;
  bits<4> M5;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-24} = 0;
  let Inst{23-20} = M5;
  let Inst{19}    = !if (!eq (m4or{3}, 1), 1, M4{3});
  let Inst{18}    = !if (!eq (m4or{2}, 1), 1, M4{2});
  let Inst{17}    = !if (!eq (m4or{1}, 1), 1, M4{1});
  let Inst{16}    = !if (!eq (m4or{0}, 1), 1, M4{0});
  let Inst{15-12} = M3;
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

// Depending on the instruction mnemonic, certain bits may be or-ed into
// the M5 value provided as explicit operand.  These are passed as m5or.
class InstVRRb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern,
               bits<4> m5or = 0>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<5> V3;
  bits<4> M4;
  bits<4> M5;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-28} = V3{3-0};
  let Inst{27-24} = 0;
  let Inst{23}    = !if (!eq (m5or{3}, 1), 1, M5{3});
  let Inst{22}    = !if (!eq (m5or{2}, 1), 1, M5{2});
  let Inst{21}    = !if (!eq (m5or{1}, 1), 1, M5{1});
  let Inst{20}    = !if (!eq (m5or{0}, 1), 1, M5{0});
  let Inst{19-16} = 0;
  let Inst{15-12} = M4;
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9}     = V3{4};
  let Inst{8}     = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRRc<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<5> V3;
  bits<4> M4;
  bits<4> M5;
  bits<4> M6;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-28} = V3{3-0};
  let Inst{27-24} = 0;
  let Inst{23-20} = M6;
  let Inst{19-16} = M5;
  let Inst{15-12} = M4;
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9}     = V3{4};
  let Inst{8}     = 0;
  let Inst{7-0}   = op{7-0};
}

// Depending on the instruction mnemonic, certain bits may be or-ed into
// the M6 value provided as explicit operand.  These are passed as m6or.
class InstVRRd<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern,
               bits<4> m6or = 0>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<5> V3;
  bits<5> V4;
  bits<4> M5;
  bits<4> M6;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-28} = V3{3-0};
  let Inst{27-24} = M5;
  let Inst{23}    = !if (!eq (m6or{3}, 1), 1, M6{3});
  let Inst{22}    = !if (!eq (m6or{2}, 1), 1, M6{2});
  let Inst{21}    = !if (!eq (m6or{1}, 1), 1, M6{1});
  let Inst{20}    = !if (!eq (m6or{0}, 1), 1, M6{0});
  let Inst{19-16} = 0;
  let Inst{15-12} = V4{3-0};
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9}     = V3{4};
  let Inst{8}     = V4{4};
  let Inst{7-0}   = op{7-0};
}

class InstVRRe<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<5> V3;
  bits<5> V4;
  bits<4> M5;
  bits<4> M6;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V2{3-0};
  let Inst{31-28} = V3{3-0};
  let Inst{27-24} = M6;
  let Inst{23-20} = 0;
  let Inst{19-16} = M5;
  let Inst{15-12} = V4{3-0};
  let Inst{11}    = V1{4};
  let Inst{10}    = V2{4};
  let Inst{9}     = V3{4};
  let Inst{8}     = V4{4};
  let Inst{7-0}   = op{7-0};
}

class InstVRRf<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<4> R2;
  bits<4> R3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = R2;
  let Inst{31-28} = R3;
  let Inst{27-12} = 0;
  let Inst{11}    = V1{4};
  let Inst{10-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRRg<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = 0;
  let Inst{35-32} = V1{3-0};
  let Inst{31-12} = 0;
  let Inst{11}    = 0;
  let Inst{10}    = V1{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRRh<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<5> V2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = 0;
  let Inst{35-32} = V1{3-0};
  let Inst{31-28} = V2{3-0};
  let Inst{27-24} = 0;
  let Inst{23-20} = M3;
  let Inst{19-12} = 0;
  let Inst{11}    = 0;
  let Inst{10}    = V1{4};
  let Inst{9}     = V2{4};
  let Inst{8}     = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRRi<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<5> V2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = V2{3-0};
  let Inst{31-24} = 0;
  let Inst{23-20} = M3;
  let Inst{19-12} = 0;
  let Inst{11}    = 0;
  let Inst{10}    = V2{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRSa<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<16> BD2;
  bits<5> V3;
  bits<4> M4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = V3{3-0};
  let Inst{31-16} = BD2;
  let Inst{15-12} = M4;
  let Inst{11}    = V1{4};
  let Inst{10}    = V3{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRSb<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<16> BD2;
  bits<4> R3;
  bits<4> M4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-32} = R3;
  let Inst{31-16} = BD2;
  let Inst{15-12} = M4;
  let Inst{11}    = V1{4};
  let Inst{10-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRSc<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<4> R1;
  bits<16> BD2;
  bits<5> V3;
  bits<4> M4;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = R1;
  let Inst{35-32} = V3{3-0};
  let Inst{31-16} = BD2;
  let Inst{15-12} = M4;
  let Inst{11}    = 0;
  let Inst{10}    = V3{4};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRSd<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<16> BD2;
  bits<4> R3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = 0;
  let Inst{35-32} = R3;
  let Inst{31-16} = BD2;
  let Inst{15-12} = V1{3-0};
  let Inst{11-9}  = 0;
  let Inst{8}     = V1{4};
  let Inst{7-0}   = op{7-0};
}

class InstVRV<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<21> VBD2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-16} = VBD2{19-0};
  let Inst{15-12} = M3;
  let Inst{11}    = V1{4};
  let Inst{10}    = VBD2{20};
  let Inst{9-8}   = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVRX<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<20> XBD2;
  bits<4> M3;

  let Inst{47-40} = op{15-8};
  let Inst{39-36} = V1{3-0};
  let Inst{35-16} = XBD2;
  let Inst{15-12} = M3;
  let Inst{11}    = V1{4};
  let Inst{10-8}  = 0;
  let Inst{7-0}   = op{7-0};
}

class InstVSI<bits<16> op, dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSystemZ<6, outs, ins, asmstr, pattern> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;

  bits<5> V1;
  bits<16> BD2;
  bits<8> I3;

  let Inst{47-40} = op{15-8};
  let Inst{39-32} = I3;
  let Inst{31-16} = BD2;
  let Inst{15-12} = V1{3-0};
  let Inst{11-9}  = 0;
  let Inst{8}     = V1{4};
  let Inst{7-0}   = op{7-0};
}

//===----------------------------------------------------------------------===//
// Instruction classes for .insn directives
//===----------------------------------------------------------------------===//

class DirectiveInsnE<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstE<0, outs, ins, asmstr, pattern> {
  bits<16> enc;

  let Inst = enc;
}

class DirectiveInsnRI<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRIa<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-24} = enc{31-24};
  let Inst{19-16} = enc{19-16};
}

class DirectiveInsnRIE<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRIEd<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnRIL<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRILa<0, outs, ins, asmstr, pattern> {
  bits<48> enc;
  string type;

  let Inst{47-40} = enc{47-40};
  let Inst{35-32} = enc{35-32};
}

class DirectiveInsnRIS<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRIS<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnRR<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRR<0, outs, ins, asmstr, pattern> {
  bits<16> enc;

  let Inst{15-8} = enc{15-8};
}

class DirectiveInsnRRE<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRRE<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-16} = enc{31-16};
}

class DirectiveInsnRRF<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRRFa<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-16} = enc{31-16};
}

class DirectiveInsnRRS<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRRS<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnRS<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRSa<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-24} = enc{31-24};
}

// RSE is like RSY except with a 12 bit displacement (instead of 20).
class DirectiveInsnRSE<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRSYa<6, outs, ins, asmstr, pattern> {
  bits <48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{31-16} = BD2{15-0};
  let Inst{15-8}  = 0;
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnRSI<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRSI<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-24} = enc{31-24};
}

class DirectiveInsnRSY<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRSYa<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnRX<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRXa<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-24} = enc{31-24};
}

class DirectiveInsnRXE<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRXE<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let M3 = 0;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnRXF<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRXF<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnRXY<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstRXYa<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnS<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstS<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-16} = enc{31-16};
}

class DirectiveInsnSI<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSI<0, outs, ins, asmstr, pattern> {
  bits<32> enc;

  let Inst{31-24} = enc{31-24};
}

class DirectiveInsnSIY<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSIY<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{7-0}   = enc{7-0};
}

class DirectiveInsnSIL<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSIL<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-32} = enc{47-32};
}

class DirectiveInsnSS<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSSd<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
}

class DirectiveInsnSSE<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSSE<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-32} = enc{47-32};
}

class DirectiveInsnSSF<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstSSF<0, outs, ins, asmstr, pattern> {
  bits<48> enc;

  let Inst{47-40} = enc{47-40};
  let Inst{35-32} = enc{35-32};
}

//===----------------------------------------------------------------------===//
// Variants of instructions with condition mask
//===----------------------------------------------------------------------===//
//
// For instructions using a condition mask (e.g. conditional branches,
// compare-and-branch instructions, or conditional move instructions),
// we generally need to create multiple instruction patterns:
//
// - One used for code generation, which encodes the condition mask as an
//   MI operand, but writes out an extended mnemonic for better readability.
// - One pattern for the base form of the instruction with an explicit
//   condition mask (encoded as a plain integer MI operand).
// - Specific patterns for each extended mnemonic, where the condition mask
//   is implied by the pattern name and not otherwise encoded at all.
//
// We need the latter primarily for the assembler and disassembler, since the
// assembler parser is not able to decode part of an instruction mnemonic
// into an operand.  Thus we provide separate patterns for each mnemonic.
//
// Note that in some cases there are two different mnemonics for the same
// condition mask.  In this case we cannot have both instructions available
// to the disassembler at the same time since the encodings are not distinct.
// Therefore the alternate forms are marked isAsmParserOnly.
//
// We don't make one of the two names an alias of the other because
// we need the custom parsing routines to select the correct register class.
//
// This section provides helpers for generating the specific forms.
//
//===----------------------------------------------------------------------===//

// A class to describe a variant of an instruction with condition mask.
class CondVariant<bits<4> ccmaskin, string suffixin, bit alternatein> {
  // The fixed condition mask to use.
  bits<4> ccmask = ccmaskin;

  // The suffix to use for the extended assembler mnemonic.
  string suffix = suffixin;

  // Whether this is an alternate that needs to be marked isAsmParserOnly.
  bit alternate = alternatein;
}

// Condition mask 15 means "always true", which is used to define
// unconditional branches as a variant of conditional branches.
def CondAlways : CondVariant<15, "", 0>;

// Condition masks for general instructions that can set all 4 bits.
def CondVariantO   : CondVariant<1,  "o",   0>;
def CondVariantH   : CondVariant<2,  "h",   0>;
def CondVariantP   : CondVariant<2,  "p",   1>;
def CondVariantNLE : CondVariant<3,  "nle", 0>;
def CondVariantL   : CondVariant<4,  "l",   0>;
def CondVariantM   : CondVariant<4,  "m",   1>;
def CondVariantNHE : CondVariant<5,  "nhe", 0>;
def CondVariantLH  : CondVariant<6,  "lh",  0>;
def CondVariantNE  : CondVariant<7,  "ne",  0>;
def CondVariantNZ  : CondVariant<7,  "nz",  1>;
def CondVariantE   : CondVariant<8,  "e",   0>;
def CondVariantZ   : CondVariant<8,  "z",   1>;
def CondVariantNLH : CondVariant<9,  "nlh", 0>;
def CondVariantHE  : CondVariant<10, "he",  0>;
def CondVariantNL  : CondVariant<11, "nl",  0>;
def CondVariantNM  : CondVariant<11, "nm",  1>;
def CondVariantLE  : CondVariant<12, "le",  0>;
def CondVariantNH  : CondVariant<13, "nh",  0>;
def CondVariantNP  : CondVariant<13, "np",  1>;
def CondVariantNO  : CondVariant<14, "no",  0>;

// A helper class to look up one of the above by name.
class CV<string name>
  : CondVariant<!cast<CondVariant>("CondVariant"#name).ccmask,
                !cast<CondVariant>("CondVariant"#name).suffix,
                !cast<CondVariant>("CondVariant"#name).alternate>;

// Condition masks for integer instructions (e.g. compare-and-branch).
// This is like the list above, except that condition 3 is not possible
// and that the low bit of the mask is therefore always 0.  This means
// that each condition has two names.  Conditions "o" and "no" are not used.
def IntCondVariantH   : CondVariant<2,  "h",   0>;
def IntCondVariantNLE : CondVariant<2,  "nle", 1>;
def IntCondVariantL   : CondVariant<4,  "l",   0>;
def IntCondVariantNHE : CondVariant<4,  "nhe", 1>;
def IntCondVariantLH  : CondVariant<6,  "lh",  0>;
def IntCondVariantNE  : CondVariant<6,  "ne",  1>;
def IntCondVariantE   : CondVariant<8,  "e",   0>;
def IntCondVariantNLH : CondVariant<8,  "nlh", 1>;
def IntCondVariantHE  : CondVariant<10, "he",  0>;
def IntCondVariantNL  : CondVariant<10, "nl",  1>;
def IntCondVariantLE  : CondVariant<12, "le",  0>;
def IntCondVariantNH  : CondVariant<12, "nh",  1>;

// A helper class to look up one of the above by name.
class ICV<string name>
  : CondVariant<!cast<CondVariant>("IntCondVariant"#name).ccmask,
                !cast<CondVariant>("IntCondVariant"#name).suffix,
                !cast<CondVariant>("IntCondVariant"#name).alternate>;

//===----------------------------------------------------------------------===//
// Instruction definitions with semantics
//===----------------------------------------------------------------------===//
//
// These classes have the form [Cond]<Category><Format>, where <Format> is one
// of the formats defined above and where <Category> describes the inputs
// and outputs.  "Cond" is used if the instruction is conditional,
// in which case the 4-bit condition-code mask is added as a final operand.
// <Category> can be one of:
//
//   Inherent:
//     One register output operand and no input operands.
//
//   InherentDual:
//     Two register output operands and no input operands.
//
//   StoreInherent:
//     One address operand.  The instruction stores to the address.
//
//   SideEffectInherent:
//     No input or output operands, but causes some side effect.
//
//   Branch:
//     One branch target.  The instruction branches to the target.
//
//   Call:
//     One output operand and one branch target.  The instruction stores
//     the return address to the output operand and branches to the target.
//
//   CmpBranch:
//     Two input operands and one optional branch target.  The instruction
//     compares the two input operands and branches or traps on the result.
//
//   BranchUnary:
//     One register output operand, one register input operand and one branch
//     target.  The instructions stores a modified form of the source register
//     in the destination register and branches on the result.
//
//   BranchBinary:
//     One register output operand, two register input operands and one branch
//     target. The instructions stores a modified form of one of the source
//     registers in the destination register and branches on the result.
//
//   LoadMultiple:
//     One address input operand and two explicit output operands.
//     The instruction loads a range of registers from the address,
//     with the explicit operands giving the first and last register
//     to load.  Other loaded registers are added as implicit definitions.
//
//   StoreMultiple:
//     Two explicit input register operands and an address operand.
//     The instruction stores a range of registers to the address,
//     with the explicit operands giving the first and last register
//     to store.  Other stored registers are added as implicit uses.
//
//   StoreLength:
//     One value operand, one length operand and one address operand.
//     The instruction stores the value operand to the address but
//     doesn't write more than the number of bytes specified by the
//     length operand.
//
//   LoadAddress:
//     One register output operand and one address operand.
//
//   SideEffectAddress:
//     One address operand.  No output operands, but causes some side effect.
//
//   Unary:
//     One register output operand and one input operand.
//
//   Store:
//     One address operand and one other input operand.  The instruction
//     stores to the address.
//
//   SideEffectUnary:
//     One input operand.  No output operands, but causes some side effect.
//
//   Binary:
//     One register output operand and two input operands.
//
//   StoreBinary:
//     One address operand and two other input operands.  The instruction
//     stores to the address.
//
//   SideEffectBinary:
//     Two input operands.  No output operands, but causes some side effect.
//
//   Compare:
//     Two input operands and an implicit CC output operand.
//
//   Test:
//     One or two input operands and an implicit CC output operand.  If
//     present, the second input operand is an "address" operand used as
//     a test class mask.
//
//   Ternary:
//     One register output operand and three input operands.
//
//   SideEffectTernary:
//     Three input operands.  No output operands, but causes some side effect.
//
//   Quaternary:
//     One register output operand and four input operands.
//
//   LoadAndOp:
//     One output operand and two input operands, one of which is an address.
//     The instruction both reads from and writes to the address.
//
//   CmpSwap:
//     One output operand and three input operands, one of which is an address.
//     The instruction both reads from and writes to the address.
//
//   RotateSelect:
//     One output operand and five input operands.  The first two operands
//     are registers and the other three are immediates.
//
//   Prefetch:
//     One 4-bit immediate operand and one address operand.  The immediate
//     operand is 1 for a load prefetch and 2 for a store prefetch.
//
//   BranchPreload:
//     One 4-bit immediate operand and two address operands.
//
// The format determines which input operands are tied to output operands,
// and also determines the shape of any address operand.
//
// Multiclasses of the form <Category><Format>Pair define two instructions,
// one with <Category><Format> and one with <Category><Format>Y.  The name
// of the first instruction has no suffix, the name of the second has
// an extra "y".
//
//===----------------------------------------------------------------------===//

class InherentRRE<string mnemonic, bits<16> opcode, RegisterOperand cls,
                  SDPatternOperator operator>
  : InstRRE<opcode, (outs cls:$R1), (ins),
            mnemonic#"\t$R1",
            [(set cls:$R1, (operator))]> {
  let R2 = 0;
}

class InherentDualRRE<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRRE<opcode, (outs cls:$R1, cls:$R2), (ins),
            mnemonic#"\t$R1, $R2", []>;

class InherentVRIa<string mnemonic, bits<16> opcode, bits<16> value>
  : InstVRIa<opcode, (outs VR128:$V1), (ins), mnemonic#"\t$V1", []> {
  let I2 = value;
  let M3 = 0;
}

class StoreInherentS<string mnemonic, bits<16> opcode,
                     SDPatternOperator operator, bits<5> bytes>
  : InstS<opcode, (outs), (ins bdaddr12only:$BD2),
          mnemonic#"\t$BD2", [(operator bdaddr12only:$BD2)]> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

class SideEffectInherentE<string mnemonic, bits<16>opcode>
  : InstE<opcode, (outs), (ins), mnemonic, []>;

class SideEffectInherentS<string mnemonic, bits<16> opcode,
                          SDPatternOperator operator>
  : InstS<opcode, (outs), (ins), mnemonic, [(operator)]> {
  let BD2 = 0;
}

class SideEffectInherentRRE<string mnemonic, bits<16> opcode>
  : InstRRE<opcode, (outs), (ins), mnemonic, []> {
  let R1 = 0;
  let R2 = 0;
}

// Allow an optional TLS marker symbol to generate TLS call relocations.
class CallRI<string mnemonic, bits<12> opcode>
  : InstRIb<opcode, (outs), (ins GR64:$R1, brtarget16tls:$RI2),
            mnemonic#"\t$R1, $RI2", []>;

// Allow an optional TLS marker symbol to generate TLS call relocations.
class CallRIL<string mnemonic, bits<12> opcode>
  : InstRILb<opcode, (outs), (ins GR64:$R1, brtarget32tls:$RI2),
             mnemonic#"\t$R1, $RI2", []>;

class CallRR<string mnemonic, bits<8> opcode>
  : InstRR<opcode, (outs), (ins GR64:$R1, ADDR64:$R2),
           mnemonic#"\t$R1, $R2", []>;

class CallRX<string mnemonic, bits<8> opcode>
  : InstRXa<opcode, (outs), (ins GR64:$R1, bdxaddr12only:$XBD2),
            mnemonic#"\t$R1, $XBD2", []>;

class CondBranchRI<string mnemonic, bits<12> opcode,
                   SDPatternOperator operator = null_frag>
  : InstRIc<opcode, (outs), (ins cond4:$valid, cond4:$M1, brtarget16:$RI2),
            !subst("#", "${M1}", mnemonic)#"\t$RI2",
            [(operator cond4:$valid, cond4:$M1, bb:$RI2)]> {
  let CCMaskFirst = 1;
}

class AsmCondBranchRI<string mnemonic, bits<12> opcode>
  : InstRIc<opcode, (outs), (ins imm32zx4:$M1, brtarget16:$RI2),
            mnemonic#"\t$M1, $RI2", []>;

class FixedCondBranchRI<CondVariant V, string mnemonic, bits<12> opcode,
                        SDPatternOperator operator = null_frag>
  : InstRIc<opcode, (outs), (ins brtarget16:$RI2),
            !subst("#", V.suffix, mnemonic)#"\t$RI2", [(operator bb:$RI2)]> {
  let isAsmParserOnly = V.alternate;
  let M1 = V.ccmask;
}

class CondBranchRIL<string mnemonic, bits<12> opcode>
  : InstRILc<opcode, (outs), (ins cond4:$valid, cond4:$M1, brtarget32:$RI2),
             !subst("#", "${M1}", mnemonic)#"\t$RI2", []> {
  let CCMaskFirst = 1;
}

class AsmCondBranchRIL<string mnemonic, bits<12> opcode>
  : InstRILc<opcode, (outs), (ins imm32zx4:$M1, brtarget32:$RI2),
             mnemonic#"\t$M1, $RI2", []>;

class FixedCondBranchRIL<CondVariant V, string mnemonic, bits<12> opcode>
  : InstRILc<opcode, (outs), (ins brtarget32:$RI2),
             !subst("#", V.suffix, mnemonic)#"\t$RI2", []> {
  let isAsmParserOnly = V.alternate;
  let M1 = V.ccmask;
}

class CondBranchRR<string mnemonic, bits<8> opcode>
  : InstRR<opcode, (outs), (ins cond4:$valid, cond4:$R1, GR64:$R2),
           !subst("#", "${R1}", mnemonic)#"\t$R2", []> {
  let CCMaskFirst = 1;
}

class AsmCondBranchRR<string mnemonic, bits<8> opcode>
  : InstRR<opcode, (outs), (ins imm32zx4:$R1, GR64:$R2),
           mnemonic#"\t$R1, $R2", []>;

class FixedCondBranchRR<CondVariant V, string mnemonic, bits<8> opcode,
                      SDPatternOperator operator = null_frag>
  : InstRR<opcode, (outs), (ins ADDR64:$R2),
           !subst("#", V.suffix, mnemonic)#"\t$R2", [(operator ADDR64:$R2)]> {
  let isAsmParserOnly = V.alternate;
  let R1 = V.ccmask;
}

class CondBranchRX<string mnemonic, bits<8> opcode>
  : InstRXb<opcode, (outs), (ins cond4:$valid, cond4:$M1, bdxaddr12only:$XBD2),
            !subst("#", "${M1}", mnemonic)#"\t$XBD2", []> {
  let CCMaskFirst = 1;
}

class AsmCondBranchRX<string mnemonic, bits<8> opcode>
  : InstRXb<opcode, (outs), (ins imm32zx4:$M1, bdxaddr12only:$XBD2),
            mnemonic#"\t$M1, $XBD2", []>;

class FixedCondBranchRX<CondVariant V, string mnemonic, bits<8> opcode>
  : InstRXb<opcode, (outs), (ins bdxaddr12only:$XBD2),
            !subst("#", V.suffix, mnemonic)#"\t$XBD2", []> {
  let isAsmParserOnly = V.alternate;
  let M1 = V.ccmask;
}

class CondBranchRXY<string mnemonic, bits<16> opcode>
  : InstRXYb<opcode, (outs), (ins cond4:$valid, cond4:$M1, bdxaddr20only:$XBD2),
             !subst("#", "${M1}", mnemonic)#"\t$XBD2", []> {
  let CCMaskFirst = 1;
  let mayLoad = 1;
}

class AsmCondBranchRXY<string mnemonic, bits<16> opcode>
  : InstRXYb<opcode, (outs), (ins imm32zx4:$M1, bdxaddr20only:$XBD2),
             mnemonic#"\t$M1, $XBD2", []> {
  let mayLoad = 1;
}

class FixedCondBranchRXY<CondVariant V, string mnemonic, bits<16> opcode,
                         SDPatternOperator operator = null_frag>
  : InstRXYb<opcode, (outs), (ins bdxaddr20only:$XBD2),
             !subst("#", V.suffix, mnemonic)#"\t$XBD2",
             [(operator (load bdxaddr20only:$XBD2))]> {
  let isAsmParserOnly = V.alternate;
  let M1 = V.ccmask;
  let mayLoad = 1;
}

class CmpBranchRIEa<string mnemonic, bits<16> opcode,
                    RegisterOperand cls, Immediate imm>
  : InstRIEa<opcode, (outs), (ins cls:$R1, imm:$I2, cond4:$M3),
             mnemonic#"$M3\t$R1, $I2", []>;

class AsmCmpBranchRIEa<string mnemonic, bits<16> opcode,
                       RegisterOperand cls, Immediate imm>
  : InstRIEa<opcode, (outs), (ins cls:$R1, imm:$I2, imm32zx4:$M3),
             mnemonic#"\t$R1, $I2, $M3", []>;

class FixedCmpBranchRIEa<CondVariant V, string mnemonic, bits<16> opcode,
                          RegisterOperand cls, Immediate imm>
  : InstRIEa<opcode, (outs), (ins cls:$R1, imm:$I2),
             mnemonic#V.suffix#"\t$R1, $I2", []> {
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CmpBranchRIEaPair<string mnemonic, bits<16> opcode,
                             RegisterOperand cls, Immediate imm> {
  let isCodeGenOnly = 1 in
    def "" : CmpBranchRIEa<mnemonic, opcode, cls, imm>;
  def Asm : AsmCmpBranchRIEa<mnemonic, opcode, cls, imm>;
}

class CmpBranchRIEb<string mnemonic, bits<16> opcode,
                    RegisterOperand cls>
  : InstRIEb<opcode, (outs),
             (ins cls:$R1, cls:$R2, cond4:$M3, brtarget16:$RI4),
             mnemonic#"$M3\t$R1, $R2, $RI4", []>;

class AsmCmpBranchRIEb<string mnemonic, bits<16> opcode,
                       RegisterOperand cls>
  : InstRIEb<opcode, (outs),
             (ins cls:$R1, cls:$R2, imm32zx4:$M3, brtarget16:$RI4),
             mnemonic#"\t$R1, $R2, $M3, $RI4", []>;

class FixedCmpBranchRIEb<CondVariant V, string mnemonic, bits<16> opcode,
                         RegisterOperand cls>
  : InstRIEb<opcode, (outs), (ins cls:$R1, cls:$R2, brtarget16:$RI4),
             mnemonic#V.suffix#"\t$R1, $R2, $RI4", []> {
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CmpBranchRIEbPair<string mnemonic, bits<16> opcode,
                             RegisterOperand cls> {
  let isCodeGenOnly = 1 in
    def "" : CmpBranchRIEb<mnemonic, opcode, cls>;
  def Asm : AsmCmpBranchRIEb<mnemonic, opcode, cls>;
}

class CmpBranchRIEc<string mnemonic, bits<16> opcode,
                    RegisterOperand cls, Immediate imm>
  : InstRIEc<opcode, (outs),
             (ins cls:$R1, imm:$I2, cond4:$M3, brtarget16:$RI4),
             mnemonic#"$M3\t$R1, $I2, $RI4", []>;

class AsmCmpBranchRIEc<string mnemonic, bits<16> opcode,
                       RegisterOperand cls, Immediate imm>
  : InstRIEc<opcode, (outs),
             (ins cls:$R1, imm:$I2, imm32zx4:$M3, brtarget16:$RI4),
             mnemonic#"\t$R1, $I2, $M3, $RI4", []>;

class FixedCmpBranchRIEc<CondVariant V, string mnemonic, bits<16> opcode,
                         RegisterOperand cls, Immediate imm>
  : InstRIEc<opcode, (outs), (ins cls:$R1, imm:$I2, brtarget16:$RI4),
             mnemonic#V.suffix#"\t$R1, $I2, $RI4", []> {
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CmpBranchRIEcPair<string mnemonic, bits<16> opcode,
                            RegisterOperand cls, Immediate imm> {
  let isCodeGenOnly = 1 in
    def "" : CmpBranchRIEc<mnemonic, opcode, cls, imm>;
  def Asm : AsmCmpBranchRIEc<mnemonic, opcode, cls, imm>;
}

class CmpBranchRRFc<string mnemonic, bits<16> opcode,
                    RegisterOperand cls>
  : InstRRFc<opcode, (outs), (ins cls:$R1, cls:$R2, cond4:$M3),
             mnemonic#"$M3\t$R1, $R2", []>;

class AsmCmpBranchRRFc<string mnemonic, bits<16> opcode,
                       RegisterOperand cls>
  : InstRRFc<opcode, (outs), (ins cls:$R1, cls:$R2, imm32zx4:$M3),
             mnemonic#"\t$R1, $R2, $M3", []>;

multiclass CmpBranchRRFcPair<string mnemonic, bits<16> opcode,
                             RegisterOperand cls> {
  let isCodeGenOnly = 1 in
    def "" : CmpBranchRRFc<mnemonic, opcode, cls>;
  def Asm : AsmCmpBranchRRFc<mnemonic, opcode, cls>;
}

class FixedCmpBranchRRFc<CondVariant V, string mnemonic, bits<16> opcode,
                          RegisterOperand cls>
  : InstRRFc<opcode, (outs), (ins cls:$R1, cls:$R2),
             mnemonic#V.suffix#"\t$R1, $R2", []> {
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

class CmpBranchRRS<string mnemonic, bits<16> opcode,
                   RegisterOperand cls>
  : InstRRS<opcode, (outs),
            (ins cls:$R1, cls:$R2, cond4:$M3, bdaddr12only:$BD4),
            mnemonic#"$M3\t$R1, $R2, $BD4", []>;

class AsmCmpBranchRRS<string mnemonic, bits<16> opcode,
                      RegisterOperand cls>
  : InstRRS<opcode, (outs),
            (ins cls:$R1, cls:$R2, imm32zx4:$M3, bdaddr12only:$BD4),
            mnemonic#"\t$R1, $R2, $M3, $BD4", []>;

class FixedCmpBranchRRS<CondVariant V, string mnemonic, bits<16> opcode,
                        RegisterOperand cls>
  : InstRRS<opcode, (outs), (ins cls:$R1, cls:$R2, bdaddr12only:$BD4),
            mnemonic#V.suffix#"\t$R1, $R2, $BD4", []> {
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CmpBranchRRSPair<string mnemonic, bits<16> opcode,
                            RegisterOperand cls> {
  let isCodeGenOnly = 1 in
    def "" : CmpBranchRRS<mnemonic, opcode, cls>;
  def Asm : AsmCmpBranchRRS<mnemonic, opcode, cls>;
}

class CmpBranchRIS<string mnemonic, bits<16> opcode,
                   RegisterOperand cls, Immediate imm>
  : InstRIS<opcode, (outs),
            (ins cls:$R1, imm:$I2, cond4:$M3, bdaddr12only:$BD4),
            mnemonic#"$M3\t$R1, $I2, $BD4", []>;

class AsmCmpBranchRIS<string mnemonic, bits<16> opcode,
                      RegisterOperand cls, Immediate imm>
  : InstRIS<opcode, (outs),
            (ins cls:$R1, imm:$I2, imm32zx4:$M3, bdaddr12only:$BD4),
            mnemonic#"\t$R1, $I2, $M3, $BD4", []>;

class FixedCmpBranchRIS<CondVariant V, string mnemonic, bits<16> opcode,
                        RegisterOperand cls, Immediate imm>
  : InstRIS<opcode, (outs), (ins cls:$R1, imm:$I2, bdaddr12only:$BD4),
            mnemonic#V.suffix#"\t$R1, $I2, $BD4", []> {
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CmpBranchRISPair<string mnemonic, bits<16> opcode,
                            RegisterOperand cls, Immediate imm> {
  let isCodeGenOnly = 1 in
    def "" : CmpBranchRIS<mnemonic, opcode, cls, imm>;
  def Asm : AsmCmpBranchRIS<mnemonic, opcode, cls, imm>;
}

class CmpBranchRSYb<string mnemonic, bits<16> opcode,
                    RegisterOperand cls>
  : InstRSYb<opcode, (outs), (ins cls:$R1, bdaddr20only:$BD2, cond4:$M3),
             mnemonic#"$M3\t$R1, $BD2", []>;

class AsmCmpBranchRSYb<string mnemonic, bits<16> opcode,
                       RegisterOperand cls>
  : InstRSYb<opcode, (outs), (ins cls:$R1, bdaddr20only:$BD2, imm32zx4:$M3),
             mnemonic#"\t$R1, $M3, $BD2", []>;

multiclass CmpBranchRSYbPair<string mnemonic, bits<16> opcode,
                             RegisterOperand cls> {
  let isCodeGenOnly = 1 in
    def "" : CmpBranchRSYb<mnemonic, opcode, cls>;
  def Asm : AsmCmpBranchRSYb<mnemonic, opcode, cls>;
}

class FixedCmpBranchRSYb<CondVariant V, string mnemonic, bits<16> opcode,
                          RegisterOperand cls>
  : InstRSYb<opcode, (outs), (ins cls:$R1, bdaddr20only:$BD2),
             mnemonic#V.suffix#"\t$R1, $BD2", []> {
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

class BranchUnaryRI<string mnemonic, bits<12> opcode, RegisterOperand cls>
  : InstRIb<opcode, (outs cls:$R1), (ins cls:$R1src, brtarget16:$RI2),
            mnemonic##"\t$R1, $RI2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchUnaryRIL<string mnemonic, bits<12> opcode, RegisterOperand cls>
  : InstRILb<opcode, (outs cls:$R1), (ins cls:$R1src, brtarget32:$RI2),
             mnemonic##"\t$R1, $RI2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchUnaryRR<string mnemonic, bits<8> opcode, RegisterOperand cls>
  : InstRR<opcode, (outs cls:$R1), (ins cls:$R1src, GR64:$R2),
           mnemonic##"\t$R1, $R2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchUnaryRRE<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRRE<opcode, (outs cls:$R1), (ins cls:$R1src, GR64:$R2),
            mnemonic##"\t$R1, $R2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchUnaryRX<string mnemonic, bits<8> opcode, RegisterOperand cls>
  : InstRXa<opcode, (outs cls:$R1), (ins cls:$R1src, bdxaddr12only:$XBD2),
            mnemonic##"\t$R1, $XBD2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchUnaryRXY<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRXYa<opcode, (outs cls:$R1), (ins cls:$R1src, bdxaddr20only:$XBD2),
             mnemonic##"\t$R1, $XBD2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchBinaryRSI<string mnemonic, bits<8> opcode, RegisterOperand cls>
  : InstRSI<opcode, (outs cls:$R1), (ins cls:$R1src, cls:$R3, brtarget16:$RI2),
            mnemonic##"\t$R1, $R3, $RI2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchBinaryRIEe<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRIEe<opcode, (outs cls:$R1),
             (ins cls:$R1src, cls:$R3, brtarget16:$RI2),
             mnemonic##"\t$R1, $R3, $RI2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchBinaryRS<string mnemonic, bits<8> opcode, RegisterOperand cls>
  : InstRSa<opcode, (outs cls:$R1),
            (ins cls:$R1src, cls:$R3, bdaddr12only:$BD2),
            mnemonic##"\t$R1, $R3, $BD2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BranchBinaryRSY<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRSYa<opcode,
             (outs cls:$R1), (ins cls:$R1src, cls:$R3, bdaddr20only:$BD2),
             mnemonic##"\t$R1, $R3, $BD2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class LoadMultipleRS<string mnemonic, bits<8> opcode, RegisterOperand cls,
                     AddressingMode mode = bdaddr12only>
  : InstRSa<opcode, (outs cls:$R1, cls:$R3), (ins mode:$BD2),
            mnemonic#"\t$R1, $R3, $BD2", []> {
  let mayLoad = 1;
}

class LoadMultipleRSY<string mnemonic, bits<16> opcode, RegisterOperand cls,
                      AddressingMode mode = bdaddr20only>
  : InstRSYa<opcode, (outs cls:$R1, cls:$R3), (ins mode:$BD2),
             mnemonic#"\t$R1, $R3, $BD2", []> {
  let mayLoad = 1;
}

multiclass LoadMultipleRSPair<string mnemonic, bits<8> rsOpcode,
                              bits<16> rsyOpcode, RegisterOperand cls> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : LoadMultipleRS<mnemonic, rsOpcode, cls, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : LoadMultipleRSY<mnemonic#"y", rsyOpcode, cls, bdaddr20pair>;
  }
}

class LoadMultipleSSe<string mnemonic, bits<8> opcode, RegisterOperand cls>
  : InstSSe<opcode, (outs cls:$R1, cls:$R3),
            (ins bdaddr12only:$BD2, bdaddr12only:$BD4),
            mnemonic#"\t$R1, $R3, $BD2, $BD4", []> {
  let mayLoad = 1;
}

class LoadMultipleVRSa<string mnemonic, bits<16> opcode>
  : InstVRSa<opcode, (outs VR128:$V1, VR128:$V3), (ins bdaddr12only:$BD2),
             mnemonic#"\t$V1, $V3, $BD2", []> {
  let M4 = 0;
  let mayLoad = 1;
}

class StoreRILPC<string mnemonic, bits<12> opcode, SDPatternOperator operator,
                 RegisterOperand cls>
  : InstRILb<opcode, (outs), (ins cls:$R1, pcrel32:$RI2),
             mnemonic#"\t$R1, $RI2",
             [(operator cls:$R1, pcrel32:$RI2)]> {
  let mayStore = 1;
  // We want PC-relative addresses to be tried ahead of BD and BDX addresses.
  // However, BDXs have two extra operands and are therefore 6 units more
  // complex.
  let AddedComplexity = 7;
}

class StoreRX<string mnemonic, bits<8> opcode, SDPatternOperator operator,
              RegisterOperand cls, bits<5> bytes,
              AddressingMode mode = bdxaddr12only>
  : InstRXa<opcode, (outs), (ins cls:$R1, mode:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(operator cls:$R1, mode:$XBD2)]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let mayStore = 1;
  let AccessBytes = bytes;
}

class StoreRXY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               RegisterOperand cls, bits<5> bytes,
               AddressingMode mode = bdxaddr20only>
  : InstRXYa<opcode, (outs), (ins cls:$R1, mode:$XBD2),
             mnemonic#"\t$R1, $XBD2",
             [(operator cls:$R1, mode:$XBD2)]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let mayStore = 1;
  let AccessBytes = bytes;
}

multiclass StoreRXPair<string mnemonic, bits<8> rxOpcode, bits<16> rxyOpcode,
                       SDPatternOperator operator, RegisterOperand cls,
                       bits<5> bytes> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : StoreRX<mnemonic, rxOpcode, operator, cls, bytes, bdxaddr12pair>;
    let DispSize = "20" in
      def Y  : StoreRXY<mnemonic#"y", rxyOpcode, operator, cls, bytes,
                        bdxaddr20pair>;
  }
}

class StoreVRX<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               TypedReg tr, bits<5> bytes, bits<4> type = 0>
  : InstVRX<opcode, (outs), (ins tr.op:$V1, bdxaddr12only:$XBD2),
            mnemonic#"\t$V1, $XBD2",
            [(set (tr.vt tr.op:$V1), (operator bdxaddr12only:$XBD2))]> {
  let M3 = type;
  let mayStore = 1;
  let AccessBytes = bytes;
}

class StoreLengthVRSb<string mnemonic, bits<16> opcode,
                      SDPatternOperator operator, bits<5> bytes>
  : InstVRSb<opcode, (outs), (ins VR128:$V1, GR32:$R3, bdaddr12only:$BD2),
             mnemonic#"\t$V1, $R3, $BD2",
             [(operator VR128:$V1, GR32:$R3, bdaddr12only:$BD2)]> {
  let M4 = 0;
  let mayStore = 1;
  let AccessBytes = bytes;
}

class StoreLengthVRSd<string mnemonic, bits<16> opcode,
                      SDPatternOperator operator, bits<5> bytes>
  : InstVRSd<opcode, (outs), (ins VR128:$V1, GR32:$R3, bdaddr12only:$BD2),
             mnemonic#"\t$V1, $R3, $BD2",
             [(operator VR128:$V1, GR32:$R3, bdaddr12only:$BD2)]> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

class StoreLengthVSI<string mnemonic, bits<16> opcode,
                     SDPatternOperator operator, bits<5> bytes>
  : InstVSI<opcode, (outs), (ins VR128:$V1, bdaddr12only:$BD2, imm32zx8:$I3),
            mnemonic#"\t$V1, $BD2, $I3",
            [(operator VR128:$V1, imm32zx8:$I3, bdaddr12only:$BD2)]> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

class StoreMultipleRS<string mnemonic, bits<8> opcode, RegisterOperand cls,
                      AddressingMode mode = bdaddr12only>
  : InstRSa<opcode, (outs), (ins cls:$R1, cls:$R3, mode:$BD2),
            mnemonic#"\t$R1, $R3, $BD2", []> {
  let mayStore = 1;
}

class StoreMultipleRSY<string mnemonic, bits<16> opcode, RegisterOperand cls,
                       AddressingMode mode = bdaddr20only>
  : InstRSYa<opcode, (outs), (ins cls:$R1, cls:$R3, mode:$BD2),
             mnemonic#"\t$R1, $R3, $BD2", []> {
  let mayStore = 1;
}

multiclass StoreMultipleRSPair<string mnemonic, bits<8> rsOpcode,
                               bits<16> rsyOpcode, RegisterOperand cls> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : StoreMultipleRS<mnemonic, rsOpcode, cls, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : StoreMultipleRSY<mnemonic#"y", rsyOpcode, cls, bdaddr20pair>;
  }
}

class StoreMultipleVRSa<string mnemonic, bits<16> opcode>
  : InstVRSa<opcode, (outs), (ins VR128:$V1, VR128:$V3, bdaddr12only:$BD2),
             mnemonic#"\t$V1, $V3, $BD2", []> {
  let M4 = 0;
  let mayStore = 1;
}

// StoreSI* instructions are used to store an integer to memory, but the
// addresses are more restricted than for normal stores.  If we are in the
// situation of having to force either the address into a register or the
// constant into a register, it's usually better to do the latter.
// We therefore match the address in the same way as a normal store and
// only use the StoreSI* instruction if the matched address is suitable.
class StoreSI<string mnemonic, bits<8> opcode, SDPatternOperator operator,
              Immediate imm>
  : InstSI<opcode, (outs), (ins mviaddr12pair:$BD1, imm:$I2),
           mnemonic#"\t$BD1, $I2",
           [(operator imm:$I2, mviaddr12pair:$BD1)]> {
  let mayStore = 1;
}

class StoreSIY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               Immediate imm>
  : InstSIY<opcode, (outs), (ins mviaddr20pair:$BD1, imm:$I2),
            mnemonic#"\t$BD1, $I2",
            [(operator imm:$I2, mviaddr20pair:$BD1)]> {
  let mayStore = 1;
}

class StoreSIL<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               Immediate imm>
  : InstSIL<opcode, (outs), (ins mviaddr12pair:$BD1, imm:$I2),
            mnemonic#"\t$BD1, $I2",
            [(operator imm:$I2, mviaddr12pair:$BD1)]> {
  let mayStore = 1;
}

multiclass StoreSIPair<string mnemonic, bits<8> siOpcode, bits<16> siyOpcode,
                       SDPatternOperator operator, Immediate imm> {
  let DispKey = mnemonic in {
    let DispSize = "12" in
      def "" : StoreSI<mnemonic, siOpcode, operator, imm>;
    let DispSize = "20" in
      def Y  : StoreSIY<mnemonic#"y", siyOpcode, operator, imm>;
  }
}

class StoreSSE<string mnemonic, bits<16> opcode>
  : InstSSE<opcode, (outs), (ins bdaddr12only:$BD1, bdaddr12only:$BD2),
            mnemonic#"\t$BD1, $BD2", []> {
  let mayStore = 1;
}

class CondStoreRSY<string mnemonic, bits<16> opcode,
                   RegisterOperand cls, bits<5> bytes,
                   AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs), (ins cls:$R1, mode:$BD2, cond4:$valid, cond4:$M3),
            mnemonic#"$M3\t$R1, $BD2", []> {
  let mayStore = 1;
  let AccessBytes = bytes;
  let CCMaskLast = 1;
}

// Like CondStoreRSY, but used for the raw assembly form.  The condition-code
// mask is the third operand rather than being part of the mnemonic.
class AsmCondStoreRSY<string mnemonic, bits<16> opcode,
                      RegisterOperand cls, bits<5> bytes,
                      AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs), (ins cls:$R1, mode:$BD2, imm32zx4:$M3),
             mnemonic#"\t$R1, $BD2, $M3", []> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

// Like CondStoreRSY, but with a fixed CC mask.
class FixedCondStoreRSY<CondVariant V, string mnemonic, bits<16> opcode,
                        RegisterOperand cls, bits<5> bytes,
                        AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs), (ins cls:$R1, mode:$BD2),
             mnemonic#V.suffix#"\t$R1, $BD2", []> {
  let mayStore = 1;
  let AccessBytes = bytes;
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CondStoreRSYPair<string mnemonic, bits<16> opcode,
                            RegisterOperand cls, bits<5> bytes,
                            AddressingMode mode = bdaddr20only> {
  let isCodeGenOnly = 1 in
    def "" : CondStoreRSY<mnemonic, opcode, cls, bytes, mode>;
  def Asm : AsmCondStoreRSY<mnemonic, opcode, cls, bytes, mode>;
}

class SideEffectUnaryI<string mnemonic, bits<8> opcode, Immediate imm>
  : InstI<opcode, (outs), (ins imm:$I1),
          mnemonic#"\t$I1", []>;

class SideEffectUnaryRR<string mnemonic, bits<8>opcode, RegisterOperand cls>
  : InstRR<opcode, (outs), (ins cls:$R1),
           mnemonic#"\t$R1", []> {
  let R2 = 0;
}

class SideEffectUnaryRRE<string mnemonic, bits<16> opcode, RegisterOperand cls,
                         SDPatternOperator operator>
  : InstRRE<opcode, (outs), (ins cls:$R1),
            mnemonic#"\t$R1", [(operator cls:$R1)]> {
  let R2 = 0;
}

class SideEffectUnaryS<string mnemonic, bits<16> opcode,
                       SDPatternOperator operator, bits<5> bytes,
                       AddressingMode mode = bdaddr12only>
  : InstS<opcode, (outs), (ins mode:$BD2),
          mnemonic#"\t$BD2", [(operator mode:$BD2)]> {
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class SideEffectAddressS<string mnemonic, bits<16> opcode,
                        SDPatternOperator operator,
                        AddressingMode mode = bdaddr12only>
  : InstS<opcode, (outs), (ins mode:$BD2),
          mnemonic#"\t$BD2", [(operator mode:$BD2)]>;

class LoadAddressRX<string mnemonic, bits<8> opcode,
                    SDPatternOperator operator, AddressingMode mode>
  : InstRXa<opcode, (outs GR64:$R1), (ins mode:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set GR64:$R1, (operator mode:$XBD2))]>;

class LoadAddressRXY<string mnemonic, bits<16> opcode,
                     SDPatternOperator operator, AddressingMode mode>
  : InstRXYa<opcode, (outs GR64:$R1), (ins mode:$XBD2),
             mnemonic#"\t$R1, $XBD2",
             [(set GR64:$R1, (operator mode:$XBD2))]>;

multiclass LoadAddressRXPair<string mnemonic, bits<8> rxOpcode,
                             bits<16> rxyOpcode, SDPatternOperator operator> {
  let DispKey = mnemonic in {
    let DispSize = "12" in
      def "" : LoadAddressRX<mnemonic, rxOpcode, operator, laaddr12pair>;
    let DispSize = "20" in
      def Y  : LoadAddressRXY<mnemonic#"y", rxyOpcode, operator, laaddr20pair>;
  }
}

class LoadAddressRIL<string mnemonic, bits<12> opcode,
                     SDPatternOperator operator>
  : InstRILb<opcode, (outs GR64:$R1), (ins pcrel32:$RI2),
             mnemonic#"\t$R1, $RI2",
             [(set GR64:$R1, (operator pcrel32:$RI2))]>;

class UnaryRR<string mnemonic, bits<8> opcode, SDPatternOperator operator,
              RegisterOperand cls1, RegisterOperand cls2>
  : InstRR<opcode, (outs cls1:$R1), (ins cls2:$R2),
           mnemonic#"\t$R1, $R2",
           [(set cls1:$R1, (operator cls2:$R2))]> {
  let OpKey = mnemonic#cls1;
  let OpType = "reg";
}

class UnaryRRE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               RegisterOperand cls1, RegisterOperand cls2>
  : InstRRE<opcode, (outs cls1:$R1), (ins cls2:$R2),
            mnemonic#"\t$R1, $R2",
            [(set cls1:$R1, (operator cls2:$R2))]> {
  let OpKey = mnemonic#cls1;
  let OpType = "reg";
}

class UnaryTiedRRE<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRRE<opcode, (outs cls:$R1), (ins cls:$R1src),
            mnemonic#"\t$R1", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let R2 = 0;
}

class UnaryMemRRFc<string mnemonic, bits<16> opcode,
                   RegisterOperand cls1, RegisterOperand cls2>
  : InstRRFc<opcode, (outs cls2:$R2, cls1:$R1), (ins cls1:$R1src),
            mnemonic#"\t$R1, $R2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let M3 = 0;
}

class UnaryRI<string mnemonic, bits<12> opcode, SDPatternOperator operator,
              RegisterOperand cls, Immediate imm>
  : InstRIa<opcode, (outs cls:$R1), (ins imm:$I2),
            mnemonic#"\t$R1, $I2",
            [(set cls:$R1, (operator imm:$I2))]>;

class UnaryRIL<string mnemonic, bits<12> opcode, SDPatternOperator operator,
               RegisterOperand cls, Immediate imm>
  : InstRILa<opcode, (outs cls:$R1), (ins imm:$I2),
             mnemonic#"\t$R1, $I2",
             [(set cls:$R1, (operator imm:$I2))]>;

class UnaryRILPC<string mnemonic, bits<12> opcode, SDPatternOperator operator,
                 RegisterOperand cls>
  : InstRILb<opcode, (outs cls:$R1), (ins pcrel32:$RI2),
             mnemonic#"\t$R1, $RI2",
             [(set cls:$R1, (operator pcrel32:$RI2))]> {
  let mayLoad = 1;
  // We want PC-relative addresses to be tried ahead of BD and BDX addresses.
  // However, BDXs have two extra operands and are therefore 6 units more
  // complex.
  let AddedComplexity = 7;
}

class CondUnaryRSY<string mnemonic, bits<16> opcode,
                   SDPatternOperator operator, RegisterOperand cls,
                   bits<5> bytes, AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs cls:$R1),
             (ins cls:$R1src, mode:$BD2, cond4:$valid, cond4:$M3),
             mnemonic#"$M3\t$R1, $BD2",
             [(set cls:$R1,
                   (z_select_ccmask (operator bdaddr20only:$BD2), cls:$R1src,
                                    cond4:$valid, cond4:$M3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
  let CCMaskLast = 1;
}

// Like CondUnaryRSY, but used for the raw assembly form.  The condition-code
// mask is the third operand rather than being part of the mnemonic.
class AsmCondUnaryRSY<string mnemonic, bits<16> opcode,
                      RegisterOperand cls, bits<5> bytes,
                      AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs cls:$R1), (ins cls:$R1src, mode:$BD2, imm32zx4:$M3),
             mnemonic#"\t$R1, $BD2, $M3", []> {
  let mayLoad = 1;
  let AccessBytes = bytes;
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

// Like CondUnaryRSY, but with a fixed CC mask.
class FixedCondUnaryRSY<CondVariant V, string mnemonic, bits<16> opcode,
                        RegisterOperand cls, bits<5> bytes,
                        AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs cls:$R1), (ins cls:$R1src, mode:$BD2),
             mnemonic#V.suffix#"\t$R1, $BD2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CondUnaryRSYPair<string mnemonic, bits<16> opcode,
                            SDPatternOperator operator,
                            RegisterOperand cls, bits<5> bytes,
                            AddressingMode mode = bdaddr20only> {
  let isCodeGenOnly = 1 in
    def "" : CondUnaryRSY<mnemonic, opcode, operator, cls, bytes, mode>;
  def Asm : AsmCondUnaryRSY<mnemonic, opcode, cls, bytes, mode>;
}

class UnaryRX<string mnemonic, bits<8> opcode, SDPatternOperator operator,
              RegisterOperand cls, bits<5> bytes,
              AddressingMode mode = bdxaddr12only>
  : InstRXa<opcode, (outs cls:$R1), (ins mode:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set cls:$R1, (operator mode:$XBD2))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class UnaryRXE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               RegisterOperand cls, bits<5> bytes>
  : InstRXE<opcode, (outs cls:$R1), (ins bdxaddr12only:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set cls:$R1, (operator bdxaddr12only:$XBD2))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let mayLoad = 1;
  let AccessBytes = bytes;
  let M3 = 0;
}

class UnaryRXY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               RegisterOperand cls, bits<5> bytes,
               AddressingMode mode = bdxaddr20only>
  : InstRXYa<opcode, (outs cls:$R1), (ins mode:$XBD2),
             mnemonic#"\t$R1, $XBD2",
             [(set cls:$R1, (operator mode:$XBD2))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

multiclass UnaryRXPair<string mnemonic, bits<8> rxOpcode, bits<16> rxyOpcode,
                       SDPatternOperator operator, RegisterOperand cls,
                       bits<5> bytes> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : UnaryRX<mnemonic, rxOpcode, operator, cls, bytes, bdxaddr12pair>;
    let DispSize = "20" in
      def Y  : UnaryRXY<mnemonic#"y", rxyOpcode, operator, cls, bytes,
                        bdxaddr20pair>;
  }
}

class UnaryVRIa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                TypedReg tr, Immediate imm, bits<4> type = 0>
  : InstVRIa<opcode, (outs tr.op:$V1), (ins imm:$I2),
             mnemonic#"\t$V1, $I2",
             [(set (tr.vt tr.op:$V1), (operator imm:$I2))]> {
  let M3 = type;
}

class UnaryVRIaGeneric<string mnemonic, bits<16> opcode, Immediate imm>
  : InstVRIa<opcode, (outs VR128:$V1), (ins imm:$I2, imm32zx4:$M3),
             mnemonic#"\t$V1, $I2, $M3", []>;

class UnaryVRRa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                TypedReg tr1, TypedReg tr2, bits<4> type = 0, bits<4> m4 = 0,
                bits<4> m5 = 0>
  : InstVRRa<opcode, (outs tr1.op:$V1), (ins tr2.op:$V2),
             mnemonic#"\t$V1, $V2",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2)))]> {
  let M3 = type;
  let M4 = m4;
  let M5 = m5;
}

class UnaryVRRaGeneric<string mnemonic, bits<16> opcode, bits<4> m4 = 0,
                       bits<4> m5 = 0>
  : InstVRRa<opcode, (outs VR128:$V1), (ins VR128:$V2, imm32zx4:$M3),
             mnemonic#"\t$V1, $V2, $M3", []> {
  let M4 = m4;
  let M5 = m5;
}

class UnaryVRRaFloatGeneric<string mnemonic, bits<16> opcode, bits<4> m5 = 0>
  : InstVRRa<opcode, (outs VR128:$V1),
             (ins VR128:$V2, imm32zx4:$M3, imm32zx4:$M4),
             mnemonic#"\t$V1, $V2, $M3, $M4", []> {
  let M5 = m5;
}

// Declare a pair of instructions, one which sets CC and one which doesn't.
// The CC-setting form ends with "S" and sets the low bit of M5.
// The form that does not set CC has an extra operand to optionally allow
// specifying arbitrary M5 values in assembler.
multiclass UnaryExtraVRRaSPair<string mnemonic, bits<16> opcode,
                               SDPatternOperator operator,
                               SDPatternOperator operator_cc,
                               TypedReg tr1, TypedReg tr2, bits<4> type> {
  let M3 = type, M4 = 0 in
    def "" : InstVRRa<opcode, (outs tr1.op:$V1),
                      (ins tr2.op:$V2, imm32zx4:$M5),
                      mnemonic#"\t$V1, $V2, $M5", []>;
  def : Pat<(tr1.vt (operator (tr2.vt tr2.op:$V2))),
            (!cast<Instruction>(NAME) tr2.op:$V2, 0)>;
  def : InstAlias<mnemonic#"\t$V1, $V2",
                  (!cast<Instruction>(NAME) tr1.op:$V1, tr2.op:$V2, 0)>;
  let Defs = [CC] in
    def S : UnaryVRRa<mnemonic##"s", opcode, operator_cc, tr1, tr2,
                      type, 0, 1>;
}

multiclass UnaryExtraVRRaSPairGeneric<string mnemonic, bits<16> opcode> {
  let M4 = 0, Defs = [CC] in
    def "" : InstVRRa<opcode, (outs VR128:$V1),
                     (ins VR128:$V2, imm32zx4:$M3, imm32zx4:$M5),
                     mnemonic#"\t$V1, $V2, $M3, $M5", []>;
  def : InstAlias<mnemonic#"\t$V1, $V2, $M3",
                  (!cast<Instruction>(NAME) VR128:$V1, VR128:$V2,
                                            imm32zx4:$M3, 0)>;
}

class UnaryVRX<string mnemonic, bits<16> opcode, SDPatternOperator operator,
               TypedReg tr, bits<5> bytes, bits<4> type = 0>
  : InstVRX<opcode, (outs tr.op:$V1), (ins bdxaddr12only:$XBD2),
            mnemonic#"\t$V1, $XBD2",
            [(set (tr.vt tr.op:$V1), (operator bdxaddr12only:$XBD2))]> {
  let M3 = type;
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class UnaryVRXGeneric<string mnemonic, bits<16> opcode>
  : InstVRX<opcode, (outs VR128:$V1), (ins bdxaddr12only:$XBD2, imm32zx4:$M3),
            mnemonic#"\t$V1, $XBD2, $M3", []> {
  let mayLoad = 1;
}

class SideEffectBinaryRX<string mnemonic, bits<8> opcode,
                         RegisterOperand cls>
  : InstRXa<opcode, (outs), (ins cls:$R1, bdxaddr12only:$XBD2),
            mnemonic##"\t$R1, $XBD2", []>;

class SideEffectBinaryRXY<string mnemonic, bits<16> opcode,
                          RegisterOperand cls>
  : InstRXYa<opcode, (outs), (ins cls:$R1, bdxaddr20only:$XBD2),
             mnemonic##"\t$R1, $XBD2", []>;

class SideEffectBinaryRILPC<string mnemonic, bits<12> opcode,
                            RegisterOperand cls>
  : InstRILb<opcode, (outs), (ins cls:$R1, pcrel32:$RI2),
             mnemonic##"\t$R1, $RI2", []> {
  // We want PC-relative addresses to be tried ahead of BD and BDX addresses.
  // However, BDXs have two extra operands and are therefore 6 units more
  // complex.
  let AddedComplexity = 7;
}

class SideEffectBinaryRRE<string mnemonic, bits<16> opcode,
                          RegisterOperand cls1, RegisterOperand cls2>
  : InstRRE<opcode, (outs), (ins cls1:$R1, cls2:$R2),
            mnemonic#"\t$R1, $R2", []>;

class SideEffectBinaryRRFa<string mnemonic, bits<16> opcode,
                           RegisterOperand cls1, RegisterOperand cls2>
  : InstRRFa<opcode, (outs), (ins cls1:$R1, cls2:$R2),
             mnemonic#"\t$R1, $R2", []> {
  let R3 = 0;
  let M4 = 0;
}

class SideEffectBinaryRRFc<string mnemonic, bits<16> opcode,
                           RegisterOperand cls1, RegisterOperand cls2>
  : InstRRFc<opcode, (outs), (ins cls1:$R1, cls2:$R2),
             mnemonic#"\t$R1, $R2", []> {
  let M3 = 0;
}

class SideEffectBinaryIE<string mnemonic, bits<16> opcode,
                         Immediate imm1, Immediate imm2>
  : InstIE<opcode, (outs), (ins imm1:$I1, imm2:$I2),
           mnemonic#"\t$I1, $I2", []>;

class SideEffectBinarySI<string mnemonic, bits<8> opcode, Operand imm>
  : InstSI<opcode, (outs), (ins bdaddr12only:$BD1, imm:$I2),
           mnemonic#"\t$BD1, $I2", []>;

class SideEffectBinarySIL<string mnemonic, bits<16> opcode,
                          SDPatternOperator operator, Immediate imm>
  : InstSIL<opcode, (outs), (ins bdaddr12only:$BD1, imm:$I2),
            mnemonic#"\t$BD1, $I2", [(operator bdaddr12only:$BD1, imm:$I2)]>;

class SideEffectBinarySSa<string mnemonic, bits<8> opcode>
  : InstSSa<opcode, (outs), (ins bdladdr12onlylen8:$BDL1, bdaddr12only:$BD2),
            mnemonic##"\t$BDL1, $BD2", []>;

class SideEffectBinarySSb<string mnemonic, bits<8> opcode>
  : InstSSb<opcode,
            (outs), (ins bdladdr12onlylen4:$BDL1, bdladdr12onlylen4:$BDL2),
            mnemonic##"\t$BDL1, $BDL2", []>;

class SideEffectBinarySSf<string mnemonic, bits<8> opcode>
  : InstSSf<opcode, (outs), (ins bdaddr12only:$BD1, bdladdr12onlylen8:$BDL2),
            mnemonic##"\t$BD1, $BDL2", []>;

class SideEffectBinarySSE<string mnemonic, bits<16> opcode>
  : InstSSE<opcode, (outs), (ins bdaddr12only:$BD1, bdaddr12only:$BD2),
            mnemonic#"\t$BD1, $BD2", []>;

class SideEffectBinaryMemMemRR<string mnemonic, bits<8> opcode,
                               RegisterOperand cls1, RegisterOperand cls2>
  : InstRR<opcode, (outs cls1:$R1, cls2:$R2), (ins cls1:$R1src, cls2:$R2src),
           mnemonic#"\t$R1, $R2", []> {
    let Constraints = "$R1 = $R1src, $R2 = $R2src";
    let DisableEncoding = "$R1src, $R2src";
}

class SideEffectBinaryMemRRE<string mnemonic, bits<16> opcode,
                             RegisterOperand cls1, RegisterOperand cls2>
  : InstRRE<opcode, (outs cls2:$R2), (ins cls1:$R1, cls2:$R2src),
            mnemonic#"\t$R1, $R2", []> {
  let Constraints = "$R2 = $R2src";
  let DisableEncoding = "$R2src";
}

class SideEffectBinaryMemMemRRE<string mnemonic, bits<16> opcode,
                                RegisterOperand cls1, RegisterOperand cls2>
  : InstRRE<opcode, (outs cls1:$R1, cls2:$R2), (ins cls1:$R1src, cls2:$R2src),
            mnemonic#"\t$R1, $R2", []> {
    let Constraints = "$R1 = $R1src, $R2 = $R2src";
    let DisableEncoding = "$R1src, $R2src";
}

class SideEffectBinaryMemMemRRFc<string mnemonic, bits<16> opcode,
                                 RegisterOperand cls1, RegisterOperand cls2>
  : InstRRFc<opcode, (outs cls1:$R1, cls2:$R2), (ins cls1:$R1src, cls2:$R2src),
             mnemonic#"\t$R1, $R2", []> {
  let Constraints = "$R1 = $R1src, $R2 = $R2src";
  let DisableEncoding = "$R1src, $R2src";
  let M3 = 0;
}

class BinaryRR<string mnemonic, bits<8> opcode, SDPatternOperator operator,
               RegisterOperand cls1, RegisterOperand cls2>
  : InstRR<opcode, (outs cls1:$R1), (ins cls1:$R1src, cls2:$R2),
           mnemonic#"\t$R1, $R2",
           [(set cls1:$R1, (operator cls1:$R1src, cls2:$R2))]> {
  let OpKey = mnemonic#cls1;
  let OpType = "reg";
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BinaryRRE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                RegisterOperand cls1, RegisterOperand cls2>
  : InstRRE<opcode, (outs cls1:$R1), (ins cls1:$R1src, cls2:$R2),
            mnemonic#"\t$R1, $R2",
            [(set cls1:$R1, (operator cls1:$R1src, cls2:$R2))]> {
  let OpKey = mnemonic#cls1;
  let OpType = "reg";
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BinaryRRD<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                RegisterOperand cls1, RegisterOperand cls2>
  : InstRRD<opcode, (outs cls1:$R1), (ins cls2:$R3, cls2:$R2),
            mnemonic#"\t$R1, $R3, $R2",
            [(set cls1:$R1, (operator cls2:$R3, cls2:$R2))]> {
  let OpKey = mnemonic#cls;
  let OpType = "reg";
}

class BinaryRRFa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls1, RegisterOperand cls2,
                 RegisterOperand cls3>
  : InstRRFa<opcode, (outs cls1:$R1), (ins cls2:$R2, cls3:$R3),
             mnemonic#"\t$R1, $R2, $R3",
             [(set cls1:$R1, (operator cls2:$R2, cls3:$R3))]> {
  let M4 = 0;
}

multiclass BinaryRRAndK<string mnemonic, bits<8> opcode1, bits<16> opcode2,
                        SDPatternOperator operator, RegisterOperand cls1,
                        RegisterOperand cls2> {
  let NumOpsKey = mnemonic in {
    let NumOpsValue = "3" in
      def K : BinaryRRFa<mnemonic#"k", opcode2, null_frag, cls1, cls1, cls2>,
              Requires<[FeatureDistinctOps]>;
    let NumOpsValue = "2", isConvertibleToThreeAddress = 1 in
      def "" : BinaryRR<mnemonic, opcode1, operator, cls1, cls2>;
  }
}

multiclass BinaryRREAndK<string mnemonic, bits<16> opcode1, bits<16> opcode2,
                         SDPatternOperator operator, RegisterOperand cls1,
                         RegisterOperand cls2> {
  let NumOpsKey = mnemonic in {
    let NumOpsValue = "3" in
      def K : BinaryRRFa<mnemonic#"k", opcode2, null_frag, cls1, cls1, cls2>,
              Requires<[FeatureDistinctOps]>;
    let NumOpsValue = "2", isConvertibleToThreeAddress = 1 in
      def "" : BinaryRRE<mnemonic, opcode1, operator, cls1, cls2>;
  }
}

class BinaryRRFb<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls1, RegisterOperand cls2,
                 RegisterOperand cls3>
  : InstRRFb<opcode, (outs cls1:$R1), (ins cls2:$R2, cls3:$R3),
             mnemonic#"\t$R1, $R3, $R2",
             [(set cls1:$R1, (operator cls2:$R2, cls3:$R3))]> {
  let M4 = 0;
}

class BinaryMemRRFc<string mnemonic, bits<16> opcode,
                    RegisterOperand cls1, RegisterOperand cls2, Immediate imm>
  : InstRRFc<opcode, (outs cls2:$R2, cls1:$R1), (ins cls1:$R1src, imm:$M3),
            mnemonic#"\t$R1, $R2, $M3", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

multiclass BinaryMemRRFcOpt<string mnemonic, bits<16> opcode,
                            RegisterOperand cls1, RegisterOperand cls2> {
  def "" : BinaryMemRRFc<mnemonic, opcode, cls1, cls2, imm32zx4>;
  def Opt : UnaryMemRRFc<mnemonic, opcode, cls1, cls2>;
}

class BinaryRRFd<string mnemonic, bits<16> opcode, RegisterOperand cls1,
                RegisterOperand cls2>
  : InstRRFd<opcode, (outs cls1:$R1), (ins cls2:$R2, imm32zx4:$M4),
             mnemonic#"\t$R1, $R2, $M4", []>;

class BinaryRRFe<string mnemonic, bits<16> opcode, RegisterOperand cls1,
                RegisterOperand cls2>
  : InstRRFe<opcode, (outs cls1:$R1), (ins imm32zx4:$M3, cls2:$R2),
             mnemonic#"\t$R1, $M3, $R2", []> {
  let M4 = 0;
}

class CondBinaryRRF<string mnemonic, bits<16> opcode, RegisterOperand cls1,
                   RegisterOperand cls2>
  : InstRRFc<opcode, (outs cls1:$R1),
             (ins cls1:$R1src, cls2:$R2, cond4:$valid, cond4:$M3),
             mnemonic#"$M3\t$R1, $R2",
             [(set cls1:$R1, (z_select_ccmask cls2:$R2, cls1:$R1src,
                                              cond4:$valid, cond4:$M3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let CCMaskLast = 1;
}

// Like CondBinaryRRF, but used for the raw assembly form.  The condition-code
// mask is the third operand rather than being part of the mnemonic.
class AsmCondBinaryRRF<string mnemonic, bits<16> opcode, RegisterOperand cls1,
                       RegisterOperand cls2>
  : InstRRFc<opcode, (outs cls1:$R1),
             (ins cls1:$R1src, cls2:$R2, imm32zx4:$M3),
             mnemonic#"\t$R1, $R2, $M3", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

// Like CondBinaryRRF, but with a fixed CC mask.
class FixedCondBinaryRRF<CondVariant V, string mnemonic, bits<16> opcode,
                         RegisterOperand cls1, RegisterOperand cls2>
  : InstRRFc<opcode, (outs cls1:$R1), (ins cls1:$R1src, cls2:$R2),
             mnemonic#V.suffix#"\t$R1, $R2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CondBinaryRRFPair<string mnemonic, bits<16> opcode,
                             RegisterOperand cls1, RegisterOperand cls2> {
  let isCodeGenOnly = 1 in
    def "" : CondBinaryRRF<mnemonic, opcode, cls1, cls2>;
  def Asm : AsmCondBinaryRRF<mnemonic, opcode, cls1, cls2>;
}

class BinaryRI<string mnemonic, bits<12> opcode, SDPatternOperator operator,
               RegisterOperand cls, Immediate imm>
  : InstRIa<opcode, (outs cls:$R1), (ins cls:$R1src, imm:$I2),
            mnemonic#"\t$R1, $I2",
            [(set cls:$R1, (operator cls:$R1src, imm:$I2))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BinaryRIE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                RegisterOperand cls, Immediate imm>
  : InstRIEd<opcode, (outs cls:$R1), (ins cls:$R3, imm:$I2),
             mnemonic#"\t$R1, $R3, $I2",
             [(set cls:$R1, (operator cls:$R3, imm:$I2))]>;

multiclass BinaryRIAndK<string mnemonic, bits<12> opcode1, bits<16> opcode2,
                        SDPatternOperator operator, RegisterOperand cls,
                        Immediate imm> {
  let NumOpsKey = mnemonic in {
    let NumOpsValue = "3" in
      def K : BinaryRIE<mnemonic##"k", opcode2, null_frag, cls, imm>,
              Requires<[FeatureDistinctOps]>;
    let NumOpsValue = "2", isConvertibleToThreeAddress = 1 in
      def "" : BinaryRI<mnemonic, opcode1, operator, cls, imm>;
  }
}

class CondBinaryRIE<string mnemonic, bits<16> opcode, RegisterOperand cls,
                    Immediate imm>
  : InstRIEg<opcode, (outs cls:$R1),
             (ins cls:$R1src, imm:$I2, cond4:$valid, cond4:$M3),
             mnemonic#"$M3\t$R1, $I2",
             [(set cls:$R1, (z_select_ccmask imm:$I2, cls:$R1src,
                                             cond4:$valid, cond4:$M3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let CCMaskLast = 1;
}

// Like CondBinaryRIE, but used for the raw assembly form.  The condition-code
// mask is the third operand rather than being part of the mnemonic.
class AsmCondBinaryRIE<string mnemonic, bits<16> opcode, RegisterOperand cls,
                       Immediate imm>
  : InstRIEg<opcode, (outs cls:$R1),
             (ins cls:$R1src, imm:$I2, imm32zx4:$M3),
             mnemonic#"\t$R1, $I2, $M3", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

// Like CondBinaryRIE, but with a fixed CC mask.
class FixedCondBinaryRIE<CondVariant V, string mnemonic, bits<16> opcode,
                         RegisterOperand cls, Immediate imm>
  : InstRIEg<opcode, (outs cls:$R1), (ins cls:$R1src, imm:$I2),
             mnemonic#V.suffix#"\t$R1, $I2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let isAsmParserOnly = V.alternate;
  let M3 = V.ccmask;
}

multiclass CondBinaryRIEPair<string mnemonic, bits<16> opcode,
                             RegisterOperand cls, Immediate imm> {
  let isCodeGenOnly = 1 in
    def "" : CondBinaryRIE<mnemonic, opcode, cls, imm>;
  def Asm : AsmCondBinaryRIE<mnemonic, opcode, cls, imm>;
}

class BinaryRIL<string mnemonic, bits<12> opcode, SDPatternOperator operator,
                RegisterOperand cls, Immediate imm>
  : InstRILa<opcode, (outs cls:$R1), (ins cls:$R1src, imm:$I2),
             mnemonic#"\t$R1, $I2",
             [(set cls:$R1, (operator cls:$R1src, imm:$I2))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BinaryRS<string mnemonic, bits<8> opcode, SDPatternOperator operator,
               RegisterOperand cls>
  : InstRSa<opcode, (outs cls:$R1), (ins cls:$R1src, shift12only:$BD2),
            mnemonic#"\t$R1, $BD2",
            [(set cls:$R1, (operator cls:$R1src, shift12only:$BD2))]> {
  let R3 = 0;
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class BinaryRSY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                RegisterOperand cls>
  : InstRSYa<opcode, (outs cls:$R1), (ins cls:$R3, shift20only:$BD2),
             mnemonic#"\t$R1, $R3, $BD2",
             [(set cls:$R1, (operator cls:$R3, shift20only:$BD2))]>;

multiclass BinaryRSAndK<string mnemonic, bits<8> opcode1, bits<16> opcode2,
                        SDPatternOperator operator, RegisterOperand cls> {
  let NumOpsKey = mnemonic in {
    let NumOpsValue = "3" in
      def K  : BinaryRSY<mnemonic##"k", opcode2, null_frag, cls>,
               Requires<[FeatureDistinctOps]>;
    let NumOpsValue = "2", isConvertibleToThreeAddress = 1 in
      def "" : BinaryRS<mnemonic, opcode1, operator, cls>;
  }
}

class BinaryRSL<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRSLb<opcode, (outs cls:$R1),
             (ins bdladdr12onlylen8:$BDL2, imm32zx4:$M3),
             mnemonic#"\t$R1, $BDL2, $M3", []> {
  let mayLoad = 1;
}

class BinaryRX<string mnemonic, bits<8> opcode, SDPatternOperator operator,
               RegisterOperand cls, SDPatternOperator load, bits<5> bytes,
               AddressingMode mode = bdxaddr12only>
  : InstRXa<opcode, (outs cls:$R1), (ins cls:$R1src, mode:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set cls:$R1, (operator cls:$R1src, (load mode:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class BinaryRXE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  RegisterOperand cls, SDPatternOperator load, bits<5> bytes>
  : InstRXE<opcode, (outs cls:$R1), (ins cls:$R1src, bdxaddr12only:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set cls:$R1, (operator cls:$R1src,
                                     (load bdxaddr12only:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
  let M3 = 0;
}

class BinaryRXF<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                RegisterOperand cls1, RegisterOperand cls2,
                SDPatternOperator load, bits<5> bytes>
  : InstRXF<opcode, (outs cls1:$R1), (ins cls2:$R3, bdxaddr12only:$XBD2),
            mnemonic#"\t$R1, $R3, $XBD2",
            [(set cls1:$R1, (operator cls2:$R3, (load bdxaddr12only:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class BinaryRXY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                RegisterOperand cls, SDPatternOperator load, bits<5> bytes,
                AddressingMode mode = bdxaddr20only>
  : InstRXYa<opcode, (outs cls:$R1), (ins cls:$R1src, mode:$XBD2),
             mnemonic#"\t$R1, $XBD2",
             [(set cls:$R1, (operator cls:$R1src, (load mode:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

multiclass BinaryRXPair<string mnemonic, bits<8> rxOpcode, bits<16> rxyOpcode,
                        SDPatternOperator operator, RegisterOperand cls,
                        SDPatternOperator load, bits<5> bytes> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : BinaryRX<mnemonic, rxOpcode, operator, cls, load, bytes,
                        bdxaddr12pair>;
    let DispSize = "20" in
      def Y  : BinaryRXY<mnemonic#"y", rxyOpcode, operator, cls, load, bytes,
                         bdxaddr20pair>;
  }
}

class BinarySI<string mnemonic, bits<8> opcode, SDPatternOperator operator,
               Operand imm, AddressingMode mode = bdaddr12only>
  : InstSI<opcode, (outs), (ins mode:$BD1, imm:$I2),
           mnemonic#"\t$BD1, $I2",
           [(store (operator (load mode:$BD1), imm:$I2), mode:$BD1)]> {
  let mayLoad = 1;
  let mayStore = 1;
}

class BinarySIY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                Operand imm, AddressingMode mode = bdaddr20only>
  : InstSIY<opcode, (outs), (ins mode:$BD1, imm:$I2),
            mnemonic#"\t$BD1, $I2",
            [(store (operator (load mode:$BD1), imm:$I2), mode:$BD1)]> {
  let mayLoad = 1;
  let mayStore = 1;
}

multiclass BinarySIPair<string mnemonic, bits<8> siOpcode,
                        bits<16> siyOpcode, SDPatternOperator operator,
                        Operand imm> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : BinarySI<mnemonic, siOpcode, operator, imm, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : BinarySIY<mnemonic#"y", siyOpcode, operator, imm, bdaddr20pair>;
  }
}

class BinarySSF<string mnemonic, bits<12> opcode, RegisterOperand cls>
  : InstSSF<opcode, (outs cls:$R3), (ins bdaddr12pair:$BD1, bdaddr12pair:$BD2),
            mnemonic#"\t$R3, $BD1, $BD2", []> {
  let mayLoad = 1;
}

class BinaryVRIb<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr, bits<4> type>
  : InstVRIb<opcode, (outs tr.op:$V1), (ins imm32zx8:$I2, imm32zx8:$I3),
             mnemonic#"\t$V1, $I2, $I3",
             [(set (tr.vt tr.op:$V1), (operator imm32zx8:$I2, imm32zx8:$I3))]> {
  let M4 = type;
}

class BinaryVRIbGeneric<string mnemonic, bits<16> opcode>
  : InstVRIb<opcode, (outs VR128:$V1),
             (ins imm32zx8:$I2, imm32zx8:$I3, imm32zx4:$M4),
             mnemonic#"\t$V1, $I2, $I3, $M4", []>;

class BinaryVRIc<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr1, TypedReg tr2, bits<4> type>
  : InstVRIc<opcode, (outs tr1.op:$V1), (ins tr2.op:$V3, imm32zx16:$I2),
             mnemonic#"\t$V1, $V3, $I2",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V3),
                                                  imm32zx16:$I2))]> {
  let M4 = type;
}

class BinaryVRIcGeneric<string mnemonic, bits<16> opcode>
  : InstVRIc<opcode, (outs VR128:$V1),
             (ins VR128:$V3, imm32zx16:$I2, imm32zx4:$M4),
             mnemonic#"\t$V1, $V3, $I2, $M4", []>;

class BinaryVRIe<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr1, TypedReg tr2, bits<4> type, bits<4> m5>
  : InstVRIe<opcode, (outs tr1.op:$V1), (ins tr2.op:$V2, imm32zx12:$I3),
             mnemonic#"\t$V1, $V2, $I3",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  imm32zx12:$I3))]> {
  let M4 = type;
  let M5 = m5;
}

class BinaryVRIeFloatGeneric<string mnemonic, bits<16> opcode>
  : InstVRIe<opcode, (outs VR128:$V1),
             (ins VR128:$V2, imm32zx12:$I3, imm32zx4:$M4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $I3, $M4, $M5", []>;

class BinaryVRIh<string mnemonic, bits<16> opcode>
  : InstVRIh<opcode, (outs VR128:$V1),
             (ins imm32zx16:$I2, imm32zx4:$I3),
             mnemonic#"\t$V1, $I2, $I3", []>;

class BinaryVRRa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr1, TypedReg tr2, bits<4> type = 0, bits<4> m4 = 0>
  : InstVRRa<opcode, (outs tr1.op:$V1), (ins tr2.op:$V2, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $M5",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  imm32zx12:$M5))]> {
  let M3 = type;
  let M4 = m4;
}

class BinaryVRRaFloatGeneric<string mnemonic, bits<16> opcode>
  : InstVRRa<opcode, (outs VR128:$V1),
             (ins VR128:$V2, imm32zx4:$M3, imm32zx4:$M4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $M3, $M4, $M5", []>;

class BinaryVRRb<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr1, TypedReg tr2, bits<4> type = 0,
                 bits<4> modifier = 0>
  : InstVRRb<opcode, (outs tr1.op:$V1), (ins tr2.op:$V2, tr2.op:$V3),
             mnemonic#"\t$V1, $V2, $V3",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3)))]> {
  let M4 = type;
  let M5 = modifier;
}

// Declare a pair of instructions, one which sets CC and one which doesn't.
// The CC-setting form ends with "S" and sets the low bit of M5.
multiclass BinaryVRRbSPair<string mnemonic, bits<16> opcode,
                           SDPatternOperator operator,
                           SDPatternOperator operator_cc, TypedReg tr1,
                           TypedReg tr2, bits<4> type, bits<4> modifier = 0> {
  def "" : BinaryVRRb<mnemonic, opcode, operator, tr1, tr2, type,
                      !and (modifier, 14)>;
  let Defs = [CC] in
    def S : BinaryVRRb<mnemonic##"s", opcode, operator_cc, tr1, tr2, type,
                       !add (!and (modifier, 14), 1)>;
}

class BinaryVRRbSPairGeneric<string mnemonic, bits<16> opcode>
  : InstVRRb<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, imm32zx4:$M4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $V3, $M4, $M5", []> {
  let Defs = [CC];
}

// Declare a pair of instructions, one which sets CC and one which doesn't.
// The CC-setting form ends with "S" and sets the low bit of M5.
// The form that does not set CC has an extra operand to optionally allow
// specifying arbitrary M5 values in assembler.
multiclass BinaryExtraVRRbSPair<string mnemonic, bits<16> opcode,
                                SDPatternOperator operator,
                                SDPatternOperator operator_cc,
                                TypedReg tr1, TypedReg tr2, bits<4> type> {
  let M4 = type in
    def "" : InstVRRb<opcode, (outs tr1.op:$V1),
                      (ins tr2.op:$V2, tr2.op:$V3, imm32zx4:$M5),
                      mnemonic#"\t$V1, $V2, $V3, $M5", []>;
  def : Pat<(tr1.vt (operator (tr2.vt tr2.op:$V2), (tr2.vt tr2.op:$V3))),
            (!cast<Instruction>(NAME) tr2.op:$V2, tr2.op:$V3, 0)>;
  def : InstAlias<mnemonic#"\t$V1, $V2, $V3",
                  (!cast<Instruction>(NAME) tr1.op:$V1, tr2.op:$V2,
                                            tr2.op:$V3, 0)>;
  let Defs = [CC] in
    def S : BinaryVRRb<mnemonic##"s", opcode, operator_cc, tr1, tr2, type, 1>;
}

multiclass BinaryExtraVRRbSPairGeneric<string mnemonic, bits<16> opcode> {
  let Defs = [CC] in
    def "" : InstVRRb<opcode, (outs VR128:$V1),
                     (ins VR128:$V2, VR128:$V3, imm32zx4:$M4, imm32zx4:$M5),
                     mnemonic#"\t$V1, $V2, $V3, $M4, $M5", []>;
  def : InstAlias<mnemonic#"\t$V1, $V2, $V3, $M4",
                  (!cast<Instruction>(NAME) VR128:$V1, VR128:$V2, VR128:$V3,
                                            imm32zx4:$M4, 0)>;
}

class BinaryVRRc<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr1, TypedReg tr2, bits<4> type = 0, bits<4> m5 = 0,
                 bits<4> m6 = 0>
  : InstVRRc<opcode, (outs tr1.op:$V1), (ins tr2.op:$V2, tr2.op:$V3),
             mnemonic#"\t$V1, $V2, $V3",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3)))]> {
  let M4 = type;
  let M5 = m5;
  let M6 = m6;
}

class BinaryVRRcGeneric<string mnemonic, bits<16> opcode, bits<4> m5 = 0,
                        bits<4> m6 = 0>
  : InstVRRc<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, imm32zx4:$M4),
             mnemonic#"\t$V1, $V2, $V3, $M4", []> {
  let M5 = m5;
  let M6 = m6;
}

class BinaryVRRcFloatGeneric<string mnemonic, bits<16> opcode, bits<4> m6 = 0>
  : InstVRRc<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, imm32zx4:$M4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $V3, $M4, $M5", []> {
  let M6 = m6;
}

// Declare a pair of instructions, one which sets CC and one which doesn't.
// The CC-setting form ends with "S" and sets the low bit of M5.
multiclass BinaryVRRcSPair<string mnemonic, bits<16> opcode,
                           SDPatternOperator operator,
                           SDPatternOperator operator_cc, TypedReg tr1,
                           TypedReg tr2, bits<4> type, bits<4> m5,
                           bits<4> modifier = 0> {
  def "" : BinaryVRRc<mnemonic, opcode, operator, tr1, tr2, type,
                      m5, !and (modifier, 14)>;
  let Defs = [CC] in
    def S : BinaryVRRc<mnemonic##"s", opcode, operator_cc, tr1, tr2, type,
                       m5, !add (!and (modifier, 14), 1)>;
}

class BinaryVRRcSPairFloatGeneric<string mnemonic, bits<16> opcode>
  : InstVRRc<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, imm32zx4:$M4, imm32zx4:$M5,
                  imm32zx4:$M6),
             mnemonic#"\t$V1, $V2, $V3, $M4, $M5, $M6", []>;

class BinaryVRRf<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr>
  : InstVRRf<opcode, (outs tr.op:$V1), (ins GR64:$R2, GR64:$R3),
             mnemonic#"\t$V1, $R2, $R3",
             [(set (tr.vt tr.op:$V1), (operator GR64:$R2, GR64:$R3))]>;

class BinaryVRRi<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstVRRi<opcode, (outs cls:$R1), (ins VR128:$V2, imm32zx4:$M3),
             mnemonic#"\t$R1, $V2, $M3", []>;

class BinaryVRSa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr1, TypedReg tr2, bits<4> type>
  : InstVRSa<opcode, (outs tr1.op:$V1), (ins tr2.op:$V3, shift12only:$BD2),
             mnemonic#"\t$V1, $V3, $BD2",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V3),
                                                  shift12only:$BD2))]> {
  let M4 = type;
}

class BinaryVRSaGeneric<string mnemonic, bits<16> opcode>
  : InstVRSa<opcode, (outs VR128:$V1),
             (ins VR128:$V3, shift12only:$BD2, imm32zx4:$M4),
             mnemonic#"\t$V1, $V3, $BD2, $M4", []>;

class BinaryVRSb<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 bits<5> bytes>
  : InstVRSb<opcode, (outs VR128:$V1), (ins GR32:$R3, bdaddr12only:$BD2),
             mnemonic#"\t$V1, $R3, $BD2",
             [(set VR128:$V1, (operator GR32:$R3, bdaddr12only:$BD2))]> {
  let M4 = 0;
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class BinaryVRSc<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr, bits<4> type>
  : InstVRSc<opcode, (outs GR64:$R1), (ins tr.op:$V3, shift12only:$BD2),
           mnemonic#"\t$R1, $V3, $BD2",
           [(set GR64:$R1, (operator (tr.vt tr.op:$V3), shift12only:$BD2))]> {
  let M4 = type;
}

class BinaryVRScGeneric<string mnemonic, bits<16> opcode>
  : InstVRSc<opcode, (outs GR64:$R1),
             (ins VR128:$V3, shift12only:$BD2, imm32zx4: $M4),
             mnemonic#"\t$R1, $V3, $BD2, $M4", []>;

class BinaryVRSd<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 bits<5> bytes>
  : InstVRSd<opcode, (outs VR128:$V1), (ins GR32:$R3, bdaddr12only:$BD2),
             mnemonic#"\t$V1, $R3, $BD2",
             [(set VR128:$V1, (operator GR32:$R3, bdaddr12only:$BD2))]> {
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class BinaryVRX<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                TypedReg tr, bits<5> bytes>
  : InstVRX<opcode, (outs VR128:$V1), (ins bdxaddr12only:$XBD2, imm32zx4:$M3),
            mnemonic#"\t$V1, $XBD2, $M3",
            [(set (tr.vt tr.op:$V1), (operator bdxaddr12only:$XBD2,
                                               imm32zx4:$M3))]> {
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class StoreBinaryRS<string mnemonic, bits<8> opcode, RegisterOperand cls,
                    bits<5> bytes, AddressingMode mode = bdaddr12only>
  : InstRSb<opcode, (outs), (ins cls:$R1, imm32zx4:$M3, mode:$BD2),
            mnemonic#"\t$R1, $M3, $BD2", []> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

class StoreBinaryRSY<string mnemonic, bits<16> opcode, RegisterOperand cls,
                     bits<5> bytes, AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs), (ins cls:$R1, imm32zx4:$M3, mode:$BD2),
             mnemonic#"\t$R1, $M3, $BD2", []> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

multiclass StoreBinaryRSPair<string mnemonic, bits<8> rsOpcode,
                             bits<16> rsyOpcode, RegisterOperand cls,
                             bits<5> bytes> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : StoreBinaryRS<mnemonic, rsOpcode, cls, bytes, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : StoreBinaryRSY<mnemonic#"y", rsyOpcode, cls, bytes,
                              bdaddr20pair>;
  }
}

class StoreBinaryRSL<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstRSLb<opcode, (outs),
             (ins cls:$R1, bdladdr12onlylen8:$BDL2, imm32zx4:$M3),
             mnemonic#"\t$R1, $BDL2, $M3", []> {
  let mayStore = 1;
}

class BinaryVSI<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                bits<5> bytes>
  : InstVSI<opcode, (outs VR128:$V1), (ins bdaddr12only:$BD2, imm32zx8:$I3),
            mnemonic#"\t$V1, $BD2, $I3",
            [(set VR128:$V1, (operator imm32zx8:$I3, bdaddr12only:$BD2))]> {
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class StoreBinaryVRV<string mnemonic, bits<16> opcode, bits<5> bytes,
                     Immediate index>
  : InstVRV<opcode, (outs), (ins VR128:$V1, bdvaddr12only:$VBD2, index:$M3),
            mnemonic#"\t$V1, $VBD2, $M3", []> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

class StoreBinaryVRX<string mnemonic, bits<16> opcode,
                     SDPatternOperator operator, TypedReg tr, bits<5> bytes,
                     Immediate index>
  : InstVRX<opcode, (outs), (ins tr.op:$V1, bdxaddr12only:$XBD2, index:$M3),
            mnemonic#"\t$V1, $XBD2, $M3",
            [(operator (tr.vt tr.op:$V1), bdxaddr12only:$XBD2, index:$M3)]> {
  let mayStore = 1;
  let AccessBytes = bytes;
}

class MemoryBinarySSd<string mnemonic, bits<8> opcode,
                      RegisterOperand cls>
  : InstSSd<opcode, (outs),
            (ins bdraddr12only:$RBD1, bdaddr12only:$BD2, cls:$R3),
            mnemonic#"\t$RBD1, $BD2, $R3", []>;

class CompareRR<string mnemonic, bits<8> opcode, SDPatternOperator operator,
                RegisterOperand cls1, RegisterOperand cls2>
  : InstRR<opcode, (outs), (ins cls1:$R1, cls2:$R2),
           mnemonic#"\t$R1, $R2",
           [(set CC, (operator cls1:$R1, cls2:$R2))]> {
  let OpKey = mnemonic#cls1;
  let OpType = "reg";
  let isCompare = 1;
}

class CompareRRE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls1, RegisterOperand cls2>
  : InstRRE<opcode, (outs), (ins cls1:$R1, cls2:$R2),
            mnemonic#"\t$R1, $R2",
            [(set CC, (operator cls1:$R1, cls2:$R2))]> {
  let OpKey = mnemonic#cls1;
  let OpType = "reg";
  let isCompare = 1;
}

class CompareRI<string mnemonic, bits<12> opcode, SDPatternOperator operator,
                RegisterOperand cls, Immediate imm>
  : InstRIa<opcode, (outs), (ins cls:$R1, imm:$I2),
            mnemonic#"\t$R1, $I2",
            [(set CC, (operator cls:$R1, imm:$I2))]> {
  let isCompare = 1;
}

class CompareRIL<string mnemonic, bits<12> opcode, SDPatternOperator operator,
                 RegisterOperand cls, Immediate imm>
  : InstRILa<opcode, (outs), (ins cls:$R1, imm:$I2),
             mnemonic#"\t$R1, $I2",
             [(set CC, (operator cls:$R1, imm:$I2))]> {
  let isCompare = 1;
}

class CompareRILPC<string mnemonic, bits<12> opcode, SDPatternOperator operator,
                   RegisterOperand cls, SDPatternOperator load>
  : InstRILb<opcode, (outs), (ins cls:$R1, pcrel32:$RI2),
             mnemonic#"\t$R1, $RI2",
             [(set CC, (operator cls:$R1, (load pcrel32:$RI2)))]> {
  let isCompare = 1;
  let mayLoad = 1;
  // We want PC-relative addresses to be tried ahead of BD and BDX addresses.
  // However, BDXs have two extra operands and are therefore 6 units more
  // complex.
  let AddedComplexity = 7;
}

class CompareRX<string mnemonic, bits<8> opcode, SDPatternOperator operator,
                RegisterOperand cls, SDPatternOperator load, bits<5> bytes,
                AddressingMode mode = bdxaddr12only>
  : InstRXa<opcode, (outs), (ins cls:$R1, mode:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set CC, (operator cls:$R1, (load mode:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let isCompare = 1;
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class CompareRXE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls, SDPatternOperator load, bits<5> bytes>
  : InstRXE<opcode, (outs), (ins cls:$R1, bdxaddr12only:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set CC, (operator cls:$R1, (load bdxaddr12only:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let isCompare = 1;
  let mayLoad = 1;
  let AccessBytes = bytes;
  let M3 = 0;
}

class CompareRXY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls, SDPatternOperator load, bits<5> bytes,
                 AddressingMode mode = bdxaddr20only>
  : InstRXYa<opcode, (outs), (ins cls:$R1, mode:$XBD2),
             mnemonic#"\t$R1, $XBD2",
             [(set CC, (operator cls:$R1, (load mode:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let isCompare = 1;
  let mayLoad = 1;
  let AccessBytes = bytes;
}

multiclass CompareRXPair<string mnemonic, bits<8> rxOpcode, bits<16> rxyOpcode,
                         SDPatternOperator operator, RegisterOperand cls,
                         SDPatternOperator load, bits<5> bytes> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : CompareRX<mnemonic, rxOpcode, operator, cls,
                         load, bytes, bdxaddr12pair>;
    let DispSize = "20" in
      def Y  : CompareRXY<mnemonic#"y", rxyOpcode, operator, cls,
                          load, bytes, bdxaddr20pair>;
  }
}

class CompareRS<string mnemonic, bits<8> opcode, RegisterOperand cls,
                bits<5> bytes, AddressingMode mode = bdaddr12only>
  : InstRSb<opcode, (outs), (ins cls:$R1, imm32zx4:$M3, mode:$BD2),
            mnemonic#"\t$R1, $M3, $BD2", []> {
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class CompareRSY<string mnemonic, bits<16> opcode, RegisterOperand cls,
                 bits<5> bytes, AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs), (ins cls:$R1, imm32zx4:$M3, mode:$BD2),
             mnemonic#"\t$R1, $M3, $BD2", []> {
  let mayLoad = 1;
  let AccessBytes = bytes;
}

multiclass CompareRSPair<string mnemonic, bits<8> rsOpcode, bits<16> rsyOpcode,
                         RegisterOperand cls, bits<5> bytes> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : CompareRS<mnemonic, rsOpcode, cls, bytes, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : CompareRSY<mnemonic#"y", rsyOpcode, cls, bytes, bdaddr20pair>;
  }
}

class CompareSSb<string mnemonic, bits<8> opcode>
  : InstSSb<opcode,
            (outs), (ins bdladdr12onlylen4:$BDL1, bdladdr12onlylen4:$BDL2),
            mnemonic##"\t$BDL1, $BDL2", []> {
  let isCompare = 1;
  let mayLoad = 1;
}

class CompareSI<string mnemonic, bits<8> opcode, SDPatternOperator operator,
                SDPatternOperator load, Immediate imm,
                AddressingMode mode = bdaddr12only>
  : InstSI<opcode, (outs), (ins mode:$BD1, imm:$I2),
           mnemonic#"\t$BD1, $I2",
           [(set CC, (operator (load mode:$BD1), imm:$I2))]> {
  let isCompare = 1;
  let mayLoad = 1;
}

class CompareSIL<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 SDPatternOperator load, Immediate imm>
  : InstSIL<opcode, (outs), (ins bdaddr12only:$BD1, imm:$I2),
            mnemonic#"\t$BD1, $I2",
            [(set CC, (operator (load bdaddr12only:$BD1), imm:$I2))]> {
  let isCompare = 1;
  let mayLoad = 1;
}

class CompareSIY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 SDPatternOperator load, Immediate imm,
                 AddressingMode mode = bdaddr20only>
  : InstSIY<opcode, (outs), (ins mode:$BD1, imm:$I2),
            mnemonic#"\t$BD1, $I2",
            [(set CC, (operator (load mode:$BD1), imm:$I2))]> {
  let isCompare = 1;
  let mayLoad = 1;
}

multiclass CompareSIPair<string mnemonic, bits<8> siOpcode, bits<16> siyOpcode,
                         SDPatternOperator operator, SDPatternOperator load,
                         Immediate imm> {
  let DispKey = mnemonic in {
    let DispSize = "12" in
      def "" : CompareSI<mnemonic, siOpcode, operator, load, imm, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : CompareSIY<mnemonic#"y", siyOpcode, operator, load, imm,
                          bdaddr20pair>;
  }
}

class CompareVRRa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr, bits<4> type>
  : InstVRRa<opcode, (outs), (ins tr.op:$V1, tr.op:$V2),
             mnemonic#"\t$V1, $V2",
             [(set CC, (operator (tr.vt tr.op:$V1), (tr.vt tr.op:$V2)))]> {
  let isCompare = 1;
  let M3 = type;
  let M4 = 0;
  let M5 = 0;
}

class CompareVRRaGeneric<string mnemonic, bits<16> opcode>
  : InstVRRa<opcode, (outs), (ins VR128:$V1, VR128:$V2, imm32zx4:$M3),
             mnemonic#"\t$V1, $V2, $M3", []> {
  let isCompare = 1;
  let M4 = 0;
  let M5 = 0;
}

class CompareVRRaFloatGeneric<string mnemonic, bits<16> opcode>
  : InstVRRa<opcode, (outs),
             (ins VR64:$V1, VR64:$V2, imm32zx4:$M3, imm32zx4:$M4),
             mnemonic#"\t$V1, $V2, $M3, $M4", []> {
  let isCompare = 1;
  let M5 = 0;
}

class CompareVRRh<string mnemonic, bits<16> opcode>
  : InstVRRh<opcode, (outs), (ins VR128:$V1, VR128:$V2, imm32zx4:$M3),
             mnemonic#"\t$V1, $V2, $M3", []> {
  let isCompare = 1;
}

class TestInherentS<string mnemonic, bits<16> opcode,
                    SDPatternOperator operator>
  : InstS<opcode, (outs), (ins), mnemonic, [(set CC, (operator))]> {
  let BD2 = 0;
}

class TestRXE<string mnemonic, bits<16> opcode, SDPatternOperator operator,
              RegisterOperand cls>
  : InstRXE<opcode, (outs), (ins cls:$R1, bdxaddr12only:$XBD2),
            mnemonic#"\t$R1, $XBD2",
            [(set CC, (operator cls:$R1, bdxaddr12only:$XBD2))]> {
  let M3 = 0;
}

class TestBinarySIL<string mnemonic, bits<16> opcode,
                    SDPatternOperator operator, Immediate imm>
  : InstSIL<opcode, (outs), (ins bdaddr12only:$BD1, imm:$I2),
            mnemonic#"\t$BD1, $I2",
            [(set CC, (operator bdaddr12only:$BD1, imm:$I2))]>;

class TestRSL<string mnemonic, bits<16> opcode>
  : InstRSLa<opcode, (outs), (ins bdladdr12onlylen4:$BDL1),
             mnemonic#"\t$BDL1", []> {
  let mayLoad = 1;
}

class TestVRRg<string mnemonic, bits<16> opcode>
  : InstVRRg<opcode, (outs), (ins VR128:$V1),
             mnemonic#"\t$V1", []>;

class SideEffectTernarySSc<string mnemonic, bits<8> opcode>
  : InstSSc<opcode, (outs), (ins bdladdr12onlylen4:$BDL1,
                                 shift12only:$BD2, imm32zx4:$I3),
            mnemonic##"\t$BDL1, $BD2, $I3", []>;

class SideEffectTernaryRRFa<string mnemonic, bits<16> opcode,
                            RegisterOperand cls1, RegisterOperand cls2,
                            RegisterOperand cls3>
  : InstRRFa<opcode, (outs), (ins cls1:$R1, cls2:$R2, cls3:$R3),
             mnemonic#"\t$R1, $R2, $R3", []> {
  let M4 = 0;
}

class SideEffectTernaryRRFb<string mnemonic, bits<16> opcode,
                            RegisterOperand cls1, RegisterOperand cls2,
                            RegisterOperand cls3>
  : InstRRFb<opcode, (outs), (ins cls1:$R1, cls2:$R2, cls3:$R3),
             mnemonic#"\t$R1, $R3, $R2", []> {
  let M4 = 0;
}

class SideEffectTernaryMemMemMemRRFb<string mnemonic, bits<16> opcode,
                                     RegisterOperand cls1,
                                     RegisterOperand cls2,
                                     RegisterOperand cls3>
  : InstRRFb<opcode, (outs cls1:$R1, cls2:$R2, cls3:$R3),
             (ins cls1:$R1src, cls2:$R2src, cls3:$R3src),
             mnemonic#"\t$R1, $R3, $R2", []> {
  let Constraints = "$R1 = $R1src, $R2 = $R2src, $R3 = $R3src";
  let DisableEncoding = "$R1src, $R2src, $R3src";
  let M4 = 0;
}

class SideEffectTernaryRRFc<string mnemonic, bits<16> opcode,
                            RegisterOperand cls1, RegisterOperand cls2,
                            Immediate imm>
  : InstRRFc<opcode, (outs), (ins cls1:$R1, cls2:$R2, imm:$M3),
             mnemonic#"\t$R1, $R2, $M3", []>;

multiclass SideEffectTernaryRRFcOpt<string mnemonic, bits<16> opcode,
                                    RegisterOperand cls1,
                                    RegisterOperand cls2> {
  def "" : SideEffectTernaryRRFc<mnemonic, opcode, cls1, cls2, imm32zx4>;
  def Opt : SideEffectBinaryRRFc<mnemonic, opcode, cls1, cls2>;
}

class SideEffectTernaryMemMemRRFc<string mnemonic, bits<16> opcode,
                                  RegisterOperand cls1, RegisterOperand cls2,
                                  Immediate imm>
  : InstRRFc<opcode, (outs cls1:$R1, cls2:$R2),
             (ins cls1:$R1src, cls2:$R2src, imm:$M3),
             mnemonic#"\t$R1, $R2, $M3", []> {
  let Constraints = "$R1 = $R1src, $R2 = $R2src";
  let DisableEncoding = "$R1src, $R2src";
}

multiclass SideEffectTernaryMemMemRRFcOpt<string mnemonic, bits<16> opcode,
                                          RegisterOperand cls1,
                                          RegisterOperand cls2> {
  def "" : SideEffectTernaryMemMemRRFc<mnemonic, opcode, cls1, cls2, imm32zx4>;
  def Opt : SideEffectBinaryMemMemRRFc<mnemonic, opcode, cls1, cls2>;
}

class SideEffectTernarySSF<string mnemonic, bits<12> opcode,
                           RegisterOperand cls>
  : InstSSF<opcode, (outs),
            (ins bdaddr12only:$BD1, bdaddr12only:$BD2, cls:$R3),
            mnemonic#"\t$BD1, $BD2, $R3", []>;

class TernaryRRFa<string mnemonic, bits<16> opcode,
                 RegisterOperand cls1, RegisterOperand cls2,
                 RegisterOperand cls3>
  : InstRRFa<opcode, (outs cls1:$R1), (ins cls2:$R2, cls3:$R3, imm32zx4:$M4),
             mnemonic#"\t$R1, $R2, $R3, $M4", []>;

class TernaryRRFb<string mnemonic, bits<16> opcode,
                  RegisterOperand cls1, RegisterOperand cls2,
                  RegisterOperand cls3>
  : InstRRFb<opcode, (outs cls1:$R1, cls3:$R3),
             (ins cls1:$R1src, cls2:$R2, imm32zx4:$M4),
             mnemonic#"\t$R1, $R3, $R2, $M4", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class TernaryRRFe<string mnemonic, bits<16> opcode, RegisterOperand cls1,
                  RegisterOperand cls2>
  : InstRRFe<opcode, (outs cls1:$R1),
             (ins imm32zx4:$M3, cls2:$R2, imm32zx4:$M4),
             mnemonic#"\t$R1, $M3, $R2, $M4", []>;

class TernaryRRD<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls1, RegisterOperand cls2>
  : InstRRD<opcode, (outs cls1:$R1), (ins cls2:$R1src, cls2:$R3, cls2:$R2),
            mnemonic#"\t$R1, $R3, $R2",
            [(set cls1:$R1, (operator cls2:$R1src, cls2:$R3, cls2:$R2))]> {
  let OpKey = mnemonic#cls;
  let OpType = "reg";
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class TernaryRS<string mnemonic, bits<8> opcode, RegisterOperand cls,
                bits<5> bytes, AddressingMode mode = bdaddr12only>
  : InstRSb<opcode, (outs cls:$R1),
            (ins cls:$R1src, imm32zx4:$M3, mode:$BD2),
            mnemonic#"\t$R1, $M3, $BD2", []> {

  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class TernaryRSY<string mnemonic, bits<16> opcode, RegisterOperand cls,
                bits<5> bytes, AddressingMode mode = bdaddr20only>
  : InstRSYb<opcode, (outs cls:$R1),
             (ins cls:$R1src, imm32zx4:$M3, mode:$BD2),
             mnemonic#"\t$R1, $M3, $BD2", []> {

  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

multiclass TernaryRSPair<string mnemonic, bits<8> rsOpcode, bits<16> rsyOpcode,
                         RegisterOperand cls, bits<5> bytes> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : TernaryRS<mnemonic, rsOpcode, cls, bytes, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : TernaryRSY<mnemonic#"y", rsyOpcode, cls, bytes, bdaddr20pair>;
  }
}

class SideEffectTernaryRS<string mnemonic, bits<8> opcode,
                          RegisterOperand cls1, RegisterOperand cls2>
  : InstRSa<opcode, (outs),
            (ins cls1:$R1, cls2:$R3, bdaddr12only:$BD2),
            mnemonic#"\t$R1, $R3, $BD2", []>;

class SideEffectTernaryRSY<string mnemonic, bits<16> opcode,
                           RegisterOperand cls1, RegisterOperand cls2>
  : InstRSYa<opcode, (outs),
             (ins cls1:$R1, cls2:$R3, bdaddr20only:$BD2),
             mnemonic#"\t$R1, $R3, $BD2", []>;

class SideEffectTernaryMemMemRS<string mnemonic, bits<8> opcode,
                                RegisterOperand cls1, RegisterOperand cls2>
  : InstRSa<opcode, (outs cls1:$R1, cls2:$R3),
            (ins cls1:$R1src, cls2:$R3src, shift12only:$BD2),
            mnemonic#"\t$R1, $R3, $BD2", []> {
    let Constraints = "$R1 = $R1src, $R3 = $R3src";
    let DisableEncoding = "$R1src, $R3src";
}

class SideEffectTernaryMemMemRSY<string mnemonic, bits<16> opcode,
                                 RegisterOperand cls1, RegisterOperand cls2>
  : InstRSYa<opcode, (outs cls1:$R1, cls2:$R3),
             (ins cls1:$R1src, cls2:$R3src, shift20only:$BD2),
             mnemonic#"\t$R1, $R3, $BD2", []> {
    let Constraints = "$R1 = $R1src, $R3 = $R3src";
    let DisableEncoding = "$R1src, $R3src";
}

class TernaryRXF<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls1, RegisterOperand cls2,
                 SDPatternOperator load, bits<5> bytes>
  : InstRXF<opcode, (outs cls1:$R1),
            (ins cls2:$R1src, cls2:$R3, bdxaddr12only:$XBD2),
            mnemonic#"\t$R1, $R3, $XBD2",
            [(set cls1:$R1, (operator cls2:$R1src, cls2:$R3,
                                      (load bdxaddr12only:$XBD2)))]> {
  let OpKey = mnemonic#"r"#cls;
  let OpType = "mem";
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class TernaryVRIa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2, Immediate imm, Immediate index>
  : InstVRIa<opcode, (outs tr1.op:$V1), (ins tr2.op:$V1src, imm:$I2, index:$M3),
             mnemonic#"\t$V1, $I2, $M3",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V1src),
                                                  imm:$I2, index:$M3))]> {
  let Constraints = "$V1 = $V1src";
  let DisableEncoding = "$V1src";
}

class TernaryVRId<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2, bits<4> type>
  : InstVRId<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, tr2.op:$V3, imm32zx8:$I4),
             mnemonic#"\t$V1, $V2, $V3, $I4",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3),
                                                  imm32zx8:$I4))]> {
  let M5 = type;
}

class TernaryVRIi<string mnemonic, bits<16> opcode, RegisterOperand cls>
  : InstVRIi<opcode, (outs VR128:$V1),
             (ins cls:$R2, imm32zx8:$I3, imm32zx4:$M4),
             mnemonic#"\t$V1, $R2, $I3, $M4", []>;

class TernaryVRRa<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2, bits<4> type, bits<4> m4or>
  : InstVRRa<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, imm32zx4:$M4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $M4, $M5",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  imm32zx4:$M4,
                                                  imm32zx4:$M5))],
             m4or> {
  let M3 = type;
}

class TernaryVRRaFloatGeneric<string mnemonic, bits<16> opcode>
  : InstVRRa<opcode, (outs VR128:$V1),
             (ins VR128:$V2, imm32zx4:$M3, imm32zx4:$M4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $M3, $M4, $M5", []>;

class TernaryVRRb<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2, bits<4> type,
                  SDPatternOperator m5mask, bits<4> m5or>
  : InstVRRb<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, tr2.op:$V3, m5mask:$M5),
             mnemonic#"\t$V1, $V2, $V3, $M5",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3),
                                                  m5mask:$M5))],
             m5or> {
  let M4 = type;
}

// Declare a pair of instructions, one which sets CC and one which doesn't.
// The CC-setting form ends with "S" and sets the low bit of M5.
// Also create aliases to make use of M5 operand optional in assembler.
multiclass TernaryOptVRRbSPair<string mnemonic, bits<16> opcode,
                               SDPatternOperator operator,
                               SDPatternOperator operator_cc,
                               TypedReg tr1, TypedReg tr2, bits<4> type,
                               bits<4> modifier = 0> {
  def "" : TernaryVRRb<mnemonic, opcode, operator, tr1, tr2, type,
                       imm32zx4even, !and (modifier, 14)>;
  def : InstAlias<mnemonic#"\t$V1, $V2, $V3",
                  (!cast<Instruction>(NAME) tr1.op:$V1, tr2.op:$V2,
                                            tr2.op:$V3, 0)>;
  let Defs = [CC] in
    def S : TernaryVRRb<mnemonic##"s", opcode, operator_cc, tr1, tr2, type,
                        imm32zx4even, !add(!and (modifier, 14), 1)>;
  def : InstAlias<mnemonic#"s\t$V1, $V2, $V3",
                  (!cast<Instruction>(NAME#"S") tr1.op:$V1, tr2.op:$V2,
                                                tr2.op:$V3, 0)>;
}

multiclass TernaryOptVRRbSPairGeneric<string mnemonic, bits<16> opcode> {
  let Defs = [CC] in
    def "" : InstVRRb<opcode, (outs VR128:$V1),
                     (ins VR128:$V2, VR128:$V3, imm32zx4:$M4, imm32zx4:$M5),
                     mnemonic#"\t$V1, $V2, $V3, $M4, $M5", []>;
  def : InstAlias<mnemonic#"\t$V1, $V2, $V3, $M4",
                  (!cast<Instruction>(NAME) VR128:$V1, VR128:$V2, VR128:$V3,
                                            imm32zx4:$M4, 0)>;
}

class TernaryVRRc<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2>
  : InstVRRc<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, tr2.op:$V3, imm32zx4:$M4),
             mnemonic#"\t$V1, $V2, $V3, $M4",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3),
                                                  imm32zx4:$M4))]> {
  let M5 = 0;
  let M6 = 0;
}

class TernaryVRRcFloat<string mnemonic, bits<16> opcode,
                       SDPatternOperator operator, TypedReg tr1, TypedReg tr2,
                       bits<4> type = 0, bits<4> m5 = 0>
  : InstVRRc<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, tr2.op:$V3, imm32zx4:$M6),
             mnemonic#"\t$V1, $V2, $V3, $M6",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3),
                                                  imm32zx4:$M6))]> {
  let M4 = type;
  let M5 = m5;
}

class TernaryVRRcFloatGeneric<string mnemonic, bits<16> opcode>
  : InstVRRc<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, imm32zx4:$M4, imm32zx4:$M5,
                  imm32zx4:$M6),
             mnemonic#"\t$V1, $V2, $V3, $M4, $M5, $M6", []>;

class TernaryVRRd<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2, bits<4> type = 0>
  : InstVRRd<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, tr2.op:$V3, tr1.op:$V4),
             mnemonic#"\t$V1, $V2, $V3, $V4",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3),
                                                  (tr1.vt tr1.op:$V4)))]> {
  let M5 = type;
  let M6 = 0;
}

class TernaryVRRdGeneric<string mnemonic, bits<16> opcode>
  : InstVRRd<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, VR128:$V4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $V3, $V4, $M5", []> {
  let M6 = 0;
}

class TernaryVRRe<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2, bits<4> m5 = 0, bits<4> type = 0>
  : InstVRRe<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, tr2.op:$V3, tr1.op:$V4),
             mnemonic#"\t$V1, $V2, $V3, $V4",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3),
                                                  (tr1.vt tr1.op:$V4)))]> {
  let M5 = m5;
  let M6 = type;
}

class TernaryVRReFloatGeneric<string mnemonic, bits<16> opcode>
  : InstVRRe<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, VR128:$V4, imm32zx4:$M5, imm32zx4:$M6),
             mnemonic#"\t$V1, $V2, $V3, $V4, $M5, $M6", []>;

class TernaryVRSb<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  TypedReg tr1, TypedReg tr2, RegisterOperand cls, bits<4> type>
  : InstVRSb<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V1src, cls:$R3, shift12only:$BD2),
             mnemonic#"\t$V1, $R3, $BD2",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V1src),
                                                  cls:$R3,
                                                  shift12only:$BD2))]> {
  let Constraints = "$V1 = $V1src";
  let DisableEncoding = "$V1src";
  let M4 = type;
}

class TernaryVRSbGeneric<string mnemonic, bits<16> opcode>
  : InstVRSb<opcode, (outs VR128:$V1),
             (ins VR128:$V1src, GR64:$R3, shift12only:$BD2, imm32zx4:$M4),
             mnemonic#"\t$V1, $R3, $BD2, $M4", []> {
  let Constraints = "$V1 = $V1src";
  let DisableEncoding = "$V1src";
}

class TernaryVRV<string mnemonic, bits<16> opcode, bits<5> bytes,
                 Immediate index>
  : InstVRV<opcode, (outs VR128:$V1),
           (ins VR128:$V1src, bdvaddr12only:$VBD2, index:$M3),
           mnemonic#"\t$V1, $VBD2, $M3", []> {
  let Constraints = "$V1 = $V1src";
  let DisableEncoding = "$V1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class TernaryVRX<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 TypedReg tr1, TypedReg tr2, bits<5> bytes, Immediate index>
  : InstVRX<opcode, (outs tr1.op:$V1),
           (ins tr2.op:$V1src, bdxaddr12only:$XBD2, index:$M3),
           mnemonic#"\t$V1, $XBD2, $M3",
           [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V1src),
                                                bdxaddr12only:$XBD2,
                                                index:$M3))]> {
  let Constraints = "$V1 = $V1src";
  let DisableEncoding = "$V1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
}

class QuaternaryVRId<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                     TypedReg tr1, TypedReg tr2, bits<4> type>
  : InstVRId<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V1src, tr2.op:$V2, tr2.op:$V3, imm32zx8:$I4),
             mnemonic#"\t$V1, $V2, $V3, $I4",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V1src),
                                                  (tr2.vt tr2.op:$V2),
                                                  (tr2.vt tr2.op:$V3),
                                                  imm32zx8:$I4))]> {
  let Constraints = "$V1 = $V1src";
  let DisableEncoding = "$V1src";
  let M5 = type;
}

class QuaternaryVRIdGeneric<string mnemonic, bits<16> opcode>
  : InstVRId<opcode, (outs VR128:$V1),
             (ins VR128:$V1src, VR128:$V2, VR128:$V3,
                  imm32zx8:$I4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $V3, $I4, $M5", []> {
  let Constraints = "$V1 = $V1src";
  let DisableEncoding = "$V1src";
}

class QuaternaryVRIf<string mnemonic, bits<16> opcode>
  : InstVRIf<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3,
                  imm32zx8:$I4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $V3, $I4, $M5", []>;

class QuaternaryVRIg<string mnemonic, bits<16> opcode>
  : InstVRIg<opcode, (outs VR128:$V1),
             (ins VR128:$V2, imm32zx8:$I3,
                  imm32zx8:$I4, imm32zx4:$M5),
             mnemonic#"\t$V1, $V2, $I3, $I4, $M5", []>;

class QuaternaryVRRd<string mnemonic, bits<16> opcode,
                     SDPatternOperator operator, TypedReg tr1, TypedReg tr2,
                     TypedReg tr3, TypedReg tr4, bits<4> type,
                     SDPatternOperator m6mask = imm32zx4, bits<4> m6or = 0>
  : InstVRRd<opcode, (outs tr1.op:$V1),
             (ins tr2.op:$V2, tr3.op:$V3, tr4.op:$V4, m6mask:$M6),
             mnemonic#"\t$V1, $V2, $V3, $V4, $M6",
             [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2),
                                                  (tr3.vt tr3.op:$V3),
                                                  (tr4.vt tr4.op:$V4),
                                                  m6mask:$M6))],
             m6or> {
  let M5 = type;
}

class QuaternaryVRRdGeneric<string mnemonic, bits<16> opcode>
  : InstVRRd<opcode, (outs VR128:$V1),
             (ins VR128:$V2, VR128:$V3, VR128:$V4, imm32zx4:$M5, imm32zx4:$M6),
             mnemonic#"\t$V1, $V2, $V3, $V4, $M5, $M6", []>;

// Declare a pair of instructions, one which sets CC and one which doesn't.
// The CC-setting form ends with "S" and sets the low bit of M6.
// Also create aliases to make use of M6 operand optional in assembler.
multiclass QuaternaryOptVRRdSPair<string mnemonic, bits<16> opcode,
                                  SDPatternOperator operator,
                                SDPatternOperator operator_cc,
                                TypedReg tr1, TypedReg tr2, bits<4> type,
                                bits<4> modifier = 0> {
  def "" : QuaternaryVRRd<mnemonic, opcode, operator,
                          tr1, tr2, tr2, tr2, type,
                          imm32zx4even, !and (modifier, 14)>;
  def : InstAlias<mnemonic#"\t$V1, $V2, $V3, $V4",
                  (!cast<Instruction>(NAME) tr1.op:$V1, tr2.op:$V2,
                                            tr2.op:$V3, tr2.op:$V4, 0)>;
  let Defs = [CC] in
    def S : QuaternaryVRRd<mnemonic##"s", opcode, operator_cc,
                           tr1, tr2, tr2, tr2, type,
                           imm32zx4even, !add (!and (modifier, 14), 1)>;
  def : InstAlias<mnemonic#"s\t$V1, $V2, $V3, $V4",
                  (!cast<Instruction>(NAME#"S") tr1.op:$V1, tr2.op:$V2,
                                                tr2.op:$V3, tr2.op:$V4, 0)>;
}

multiclass QuaternaryOptVRRdSPairGeneric<string mnemonic, bits<16> opcode> {
  let Defs = [CC] in
    def "" : QuaternaryVRRdGeneric<mnemonic, opcode>;
  def : InstAlias<mnemonic#"\t$V1, $V2, $V3, $V4, $M5",
                  (!cast<Instruction>(NAME) VR128:$V1, VR128:$V2, VR128:$V3,
                                            VR128:$V4, imm32zx4:$M5, 0)>;
}

class SideEffectQuaternaryRRFa<string mnemonic, bits<16> opcode,
                               RegisterOperand cls1, RegisterOperand cls2,
                               RegisterOperand cls3>
  : InstRRFa<opcode, (outs), (ins cls1:$R1, cls2:$R2, cls3:$R3, imm32zx4:$M4),
             mnemonic#"\t$R1, $R2, $R3, $M4", []>;

multiclass SideEffectQuaternaryRRFaOptOpt<string mnemonic, bits<16> opcode,
                                          RegisterOperand cls1,
                                          RegisterOperand cls2,
                                          RegisterOperand cls3> {
  def "" : SideEffectQuaternaryRRFa<mnemonic, opcode, cls1, cls2, cls3>;
  def Opt : SideEffectTernaryRRFa<mnemonic, opcode, cls1, cls2, cls3>;
  def OptOpt : SideEffectBinaryRRFa<mnemonic, opcode, cls1, cls2>;
}

class SideEffectQuaternaryRRFb<string mnemonic, bits<16> opcode,
                               RegisterOperand cls1, RegisterOperand cls2,
                               RegisterOperand cls3>
  : InstRRFb<opcode, (outs), (ins cls1:$R1, cls2:$R2, cls3:$R3, imm32zx4:$M4),
             mnemonic#"\t$R1, $R3, $R2, $M4", []>;

multiclass SideEffectQuaternaryRRFbOpt<string mnemonic, bits<16> opcode,
                                       RegisterOperand cls1,
                                       RegisterOperand cls2,
                                       RegisterOperand cls3> {
  def "" : SideEffectQuaternaryRRFb<mnemonic, opcode, cls1, cls2, cls3>;
  def Opt : SideEffectTernaryRRFb<mnemonic, opcode, cls1, cls2, cls3>;
}

class SideEffectQuaternarySSe<string mnemonic, bits<8> opcode,
                              RegisterOperand cls>
  : InstSSe<opcode, (outs),
            (ins cls:$R1, bdaddr12only:$BD2, cls:$R3, bdaddr12only:$BD4),
            mnemonic#"\t$R1, $BD2, $R3, $BD4", []>;

class LoadAndOpRSY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                  RegisterOperand cls, AddressingMode mode = bdaddr20only>
  : InstRSYa<opcode, (outs cls:$R1), (ins cls:$R3, mode:$BD2),
             mnemonic#"\t$R1, $R3, $BD2",
             [(set cls:$R1, (operator mode:$BD2, cls:$R3))]> {
  let mayLoad = 1;
  let mayStore = 1;
}

class CmpSwapRRE<string mnemonic, bits<16> opcode,
                 RegisterOperand cls1, RegisterOperand cls2>
  : InstRRE<opcode, (outs cls1:$R1), (ins cls1:$R1src, cls2:$R2),
            mnemonic#"\t$R1, $R2", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let mayStore = 1;
}

class CmpSwapRS<string mnemonic, bits<8> opcode, SDPatternOperator operator,
                RegisterOperand cls, AddressingMode mode = bdaddr12only>
  : InstRSa<opcode, (outs cls:$R1), (ins cls:$R1src, cls:$R3, mode:$BD2),
            mnemonic#"\t$R1, $R3, $BD2",
            [(set cls:$R1, (operator mode:$BD2, cls:$R1src, cls:$R3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let mayStore = 1;
}

class CmpSwapRSY<string mnemonic, bits<16> opcode, SDPatternOperator operator,
                 RegisterOperand cls, AddressingMode mode = bdaddr20only>
  : InstRSYa<opcode, (outs cls:$R1), (ins cls:$R1src, cls:$R3, mode:$BD2),
             mnemonic#"\t$R1, $R3, $BD2",
             [(set cls:$R1, (operator mode:$BD2, cls:$R1src, cls:$R3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let mayStore = 1;
}

multiclass CmpSwapRSPair<string mnemonic, bits<8> rsOpcode, bits<16> rsyOpcode,
                         SDPatternOperator operator, RegisterOperand cls> {
  let DispKey = mnemonic ## #cls in {
    let DispSize = "12" in
      def "" : CmpSwapRS<mnemonic, rsOpcode, operator, cls, bdaddr12pair>;
    let DispSize = "20" in
      def Y  : CmpSwapRSY<mnemonic#"y", rsyOpcode, operator, cls, bdaddr20pair>;
  }
}

class RotateSelectRIEf<string mnemonic, bits<16> opcode, RegisterOperand cls1,
                       RegisterOperand cls2>
  : InstRIEf<opcode, (outs cls1:$R1),
             (ins cls1:$R1src, cls2:$R2, imm32zx8:$I3, imm32zx8:$I4,
                  imm32zx6:$I5),
             mnemonic#"\t$R1, $R2, $I3, $I4, $I5", []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

class PrefetchRXY<string mnemonic, bits<16> opcode, SDPatternOperator operator>
  : InstRXYb<opcode, (outs), (ins imm32zx4:$M1, bdxaddr20only:$XBD2),
             mnemonic##"\t$M1, $XBD2",
             [(operator imm32zx4:$M1, bdxaddr20only:$XBD2)]>;

class PrefetchRILPC<string mnemonic, bits<12> opcode,
                    SDPatternOperator operator>
  : InstRILc<opcode, (outs), (ins imm32zx4:$M1, pcrel32:$RI2),
             mnemonic##"\t$M1, $RI2",
             [(operator imm32zx4:$M1, pcrel32:$RI2)]> {
  // We want PC-relative addresses to be tried ahead of BD and BDX addresses.
  // However, BDXs have two extra operands and are therefore 6 units more
  // complex.
  let AddedComplexity = 7;
}

class BranchPreloadSMI<string mnemonic, bits<8> opcode>
  : InstSMI<opcode, (outs),
            (ins imm32zx4:$M1, brtarget16bpp:$RI2, bdxaddr12only:$BD3),
            mnemonic#"\t$M1, $RI2, $BD3", []>;

class BranchPreloadMII<string mnemonic, bits<8> opcode>
  : InstMII<opcode, (outs),
            (ins imm32zx4:$M1, brtarget12bpp:$RI2, brtarget24bpp:$RI3),
            mnemonic#"\t$M1, $RI2, $RI3", []>;

// A floating-point load-and test operation.  Create both a normal unary
// operation and one that acts as a comparison against zero.
// Note that the comparison against zero operation is not available if we
// have vector support, since load-and-test instructions will partially
// clobber the target (vector) register.
multiclass LoadAndTestRRE<string mnemonic, bits<16> opcode,
                          RegisterOperand cls> {
  def "" : UnaryRRE<mnemonic, opcode, null_frag, cls, cls>;
  let isCodeGenOnly = 1, Predicates = [FeatureNoVector] in
    def Compare : CompareRRE<mnemonic, opcode, null_frag, cls, cls>;
}

//===----------------------------------------------------------------------===//
// Pseudo instructions
//===----------------------------------------------------------------------===//
//
// Convenience instructions that get lowered to real instructions
// by either SystemZTargetLowering::EmitInstrWithCustomInserter()
// or SystemZInstrInfo::expandPostRAPseudo().
//
//===----------------------------------------------------------------------===//

class Pseudo<dag outs, dag ins, list<dag> pattern>
  : InstSystemZ<0, outs, ins, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

// Like UnaryRI, but expanded after RA depending on the choice of register.
class UnaryRIPseudo<SDPatternOperator operator, RegisterOperand cls,
                    Immediate imm>
  : Pseudo<(outs cls:$R1), (ins imm:$I2),
           [(set cls:$R1, (operator imm:$I2))]>;

// Like UnaryRXY, but expanded after RA depending on the choice of register.
class UnaryRXYPseudo<string key, SDPatternOperator operator,
                     RegisterOperand cls, bits<5> bytes,
                     AddressingMode mode = bdxaddr20only>
  : Pseudo<(outs cls:$R1), (ins mode:$XBD2),
           [(set cls:$R1, (operator mode:$XBD2))]> {
  let OpKey = key#"r"#cls;
  let OpType = "mem";
  let mayLoad = 1;
  let Has20BitOffset = 1;
  let HasIndex = 1;
  let AccessBytes = bytes;
}

// Like UnaryRR, but expanded after RA depending on the choice of registers.
class UnaryRRPseudo<string key, SDPatternOperator operator,
                    RegisterOperand cls1, RegisterOperand cls2>
  : Pseudo<(outs cls1:$R1), (ins cls2:$R2),
           [(set cls1:$R1, (operator cls2:$R2))]> {
  let OpKey = key#cls1;
  let OpType = "reg";
}

// Like BinaryRI, but expanded after RA depending on the choice of register.
class BinaryRIPseudo<SDPatternOperator operator, RegisterOperand cls,
                     Immediate imm>
  : Pseudo<(outs cls:$R1), (ins cls:$R1src, imm:$I2),
           [(set cls:$R1, (operator cls:$R1src, imm:$I2))]> {
  let Constraints = "$R1 = $R1src";
}

// Like BinaryRIE, but expanded after RA depending on the choice of register.
class BinaryRIEPseudo<SDPatternOperator operator, RegisterOperand cls,
                      Immediate imm>
  : Pseudo<(outs cls:$R1), (ins cls:$R3, imm:$I2),
           [(set cls:$R1, (operator cls:$R3, imm:$I2))]>;

// Like BinaryRIAndK, but expanded after RA depending on the choice of register.
multiclass BinaryRIAndKPseudo<string key, SDPatternOperator operator,
                              RegisterOperand cls, Immediate imm> {
  let NumOpsKey = key in {
    let NumOpsValue = "3" in
      def K : BinaryRIEPseudo<null_frag, cls, imm>,
              Requires<[FeatureHighWord, FeatureDistinctOps]>;
    let NumOpsValue = "2", isConvertibleToThreeAddress = 1 in
      def "" : BinaryRIPseudo<operator, cls, imm>,
               Requires<[FeatureHighWord]>;
  }
}

// Like CompareRI, but expanded after RA depending on the choice of register.
class CompareRIPseudo<SDPatternOperator operator, RegisterOperand cls,
                      Immediate imm>
  : Pseudo<(outs), (ins cls:$R1, imm:$I2),
           [(set CC, (operator cls:$R1, imm:$I2))]> {
  let isCompare = 1;
}

// Like CompareRXY, but expanded after RA depending on the choice of register.
class CompareRXYPseudo<SDPatternOperator operator, RegisterOperand cls,
                       SDPatternOperator load, bits<5> bytes,
                       AddressingMode mode = bdxaddr20only>
  : Pseudo<(outs), (ins cls:$R1, mode:$XBD2),
           [(set CC, (operator cls:$R1, (load mode:$XBD2)))]> {
  let mayLoad = 1;
  let Has20BitOffset = 1;
  let HasIndex = 1;
  let AccessBytes = bytes;
}

// Like TestBinarySIL, but expanded later.
class TestBinarySILPseudo<SDPatternOperator operator, Immediate imm>
  : Pseudo<(outs), (ins bdaddr12only:$BD1, imm:$I2),
           [(set CC, (operator bdaddr12only:$BD1, imm:$I2))]>;

// Like CondBinaryRRF, but expanded after RA depending on the choice of
// register.
class CondBinaryRRFPseudo<RegisterOperand cls1, RegisterOperand cls2>
  : Pseudo<(outs cls1:$R1),
           (ins cls1:$R1src, cls2:$R2, cond4:$valid, cond4:$M3),
           [(set cls1:$R1, (z_select_ccmask cls2:$R2, cls1:$R1src,
                                            cond4:$valid, cond4:$M3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let CCMaskLast = 1;
}

// Like CondBinaryRIE, but expanded after RA depending on the choice of
// register.
class CondBinaryRIEPseudo<RegisterOperand cls, Immediate imm>
  : Pseudo<(outs cls:$R1),
           (ins cls:$R1src, imm:$I2, cond4:$valid, cond4:$M3),
           [(set cls:$R1, (z_select_ccmask imm:$I2, cls:$R1src,
                                           cond4:$valid, cond4:$M3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let CCMaskLast = 1;
}

// Like CondUnaryRSY, but expanded after RA depending on the choice of
// register.
class CondUnaryRSYPseudo<SDPatternOperator operator, RegisterOperand cls,
                         bits<5> bytes, AddressingMode mode = bdaddr20only>
  : Pseudo<(outs cls:$R1),
           (ins cls:$R1src, mode:$BD2, cond4:$valid, cond4:$R3),
           [(set cls:$R1,
                 (z_select_ccmask (operator mode:$BD2), cls:$R1src,
                                  cond4:$valid, cond4:$R3))]> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
  let mayLoad = 1;
  let AccessBytes = bytes;
  let CCMaskLast = 1;
}

// Like CondStoreRSY, but expanded after RA depending on the choice of
// register.
class CondStoreRSYPseudo<RegisterOperand cls, bits<5> bytes,
                         AddressingMode mode = bdaddr20only>
  : Pseudo<(outs), (ins cls:$R1, mode:$BD2, cond4:$valid, cond4:$R3), []> {
  let mayStore = 1;
  let AccessBytes = bytes;
  let CCMaskLast = 1;
}

// Like StoreRXY, but expanded after RA depending on the choice of register.
class StoreRXYPseudo<SDPatternOperator operator, RegisterOperand cls,
                     bits<5> bytes, AddressingMode mode = bdxaddr20only>
  : Pseudo<(outs), (ins cls:$R1, mode:$XBD2),
           [(operator cls:$R1, mode:$XBD2)]> {
  let mayStore = 1;
  let Has20BitOffset = 1;
  let HasIndex = 1;
  let AccessBytes = bytes;
}

// Like RotateSelectRIEf, but expanded after RA depending on the choice
// of registers.
class RotateSelectRIEfPseudo<RegisterOperand cls1, RegisterOperand cls2>
  : Pseudo<(outs cls1:$R1),
           (ins cls1:$R1src, cls2:$R2, imm32zx8:$I3, imm32zx8:$I4,
                imm32zx6:$I5),
           []> {
  let Constraints = "$R1 = $R1src";
  let DisableEncoding = "$R1src";
}

// Implements "$dst = $cc & (8 >> CC) ? $src1 : $src2", where CC is
// the value of the PSW's 2-bit condition code field.
class SelectWrapper<ValueType vt, RegisterOperand cls>
  : Pseudo<(outs cls:$dst),
           (ins cls:$src1, cls:$src2, imm32zx4:$valid, imm32zx4:$cc),
           [(set (vt cls:$dst), (z_select_ccmask cls:$src1, cls:$src2,
                                            imm32zx4:$valid, imm32zx4:$cc))]> {
  let usesCustomInserter = 1;
  let hasNoSchedulingInfo = 1;
  let Uses = [CC];
}

// Stores $new to $addr if $cc is true ("" case) or false (Inv case).
multiclass CondStores<RegisterOperand cls, SDPatternOperator store,
                      SDPatternOperator load, AddressingMode mode> {
  let Uses = [CC], usesCustomInserter = 1, hasNoSchedulingInfo = 1,
      mayLoad = 1, mayStore = 1 in {
    def "" : Pseudo<(outs),
                    (ins cls:$new, mode:$addr, imm32zx4:$valid, imm32zx4:$cc),
                    [(store (z_select_ccmask cls:$new, (load mode:$addr),
                                             imm32zx4:$valid, imm32zx4:$cc),
                            mode:$addr)]>;
    def Inv : Pseudo<(outs),
                     (ins cls:$new, mode:$addr, imm32zx4:$valid, imm32zx4:$cc),
                     [(store (z_select_ccmask (load mode:$addr), cls:$new,
                                              imm32zx4:$valid, imm32zx4:$cc),
                              mode:$addr)]>;
  }
}

// OPERATOR is ATOMIC_SWAP or an ATOMIC_LOAD_* operation.  PAT and OPERAND
// describe the second (non-memory) operand.
class AtomicLoadBinary<SDPatternOperator operator, RegisterOperand cls,
                       dag pat, DAGOperand operand>
  : Pseudo<(outs cls:$dst), (ins bdaddr20only:$ptr, operand:$src2),
           [(set cls:$dst, (operator bdaddr20only:$ptr, pat))]> {
  let Defs = [CC];
  let Has20BitOffset = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let usesCustomInserter = 1;
  let hasNoSchedulingInfo = 1;
}

// Specializations of AtomicLoadWBinary.
class AtomicLoadBinaryReg32<SDPatternOperator operator>
  : AtomicLoadBinary<operator, GR32, (i32 GR32:$src2), GR32>;
class AtomicLoadBinaryImm32<SDPatternOperator operator, Immediate imm>
  : AtomicLoadBinary<operator, GR32, (i32 imm:$src2), imm>;
class AtomicLoadBinaryReg64<SDPatternOperator operator>
  : AtomicLoadBinary<operator, GR64, (i64 GR64:$src2), GR64>;
class AtomicLoadBinaryImm64<SDPatternOperator operator, Immediate imm>
  : AtomicLoadBinary<operator, GR64, (i64 imm:$src2), imm>;

// OPERATOR is ATOMIC_SWAPW or an ATOMIC_LOADW_* operation.  PAT and OPERAND
// describe the second (non-memory) operand.
class AtomicLoadWBinary<SDPatternOperator operator, dag pat,
                        DAGOperand operand>
  : Pseudo<(outs GR32:$dst),
           (ins bdaddr20only:$ptr, operand:$src2, ADDR32:$bitshift,
                ADDR32:$negbitshift, uimm32:$bitsize),
           [(set GR32:$dst, (operator bdaddr20only:$ptr, pat, ADDR32:$bitshift,
                                      ADDR32:$negbitshift, uimm32:$bitsize))]> {
  let Defs = [CC];
  let Has20BitOffset = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let usesCustomInserter = 1;
  let hasNoSchedulingInfo = 1;
}

// Specializations of AtomicLoadWBinary.
class AtomicLoadWBinaryReg<SDPatternOperator operator>
  : AtomicLoadWBinary<operator, (i32 GR32:$src2), GR32>;
class AtomicLoadWBinaryImm<SDPatternOperator operator, Immediate imm>
  : AtomicLoadWBinary<operator, (i32 imm:$src2), imm>;

// Define an instruction that operates on two fixed-length blocks of memory,
// and associated pseudo instructions for operating on blocks of any size.
// The Sequence form uses a straight-line sequence of instructions and
// the Loop form uses a loop of length-256 instructions followed by
// another instruction to handle the excess.
multiclass MemorySS<string mnemonic, bits<8> opcode,
                    SDPatternOperator sequence, SDPatternOperator loop> {
  def "" : SideEffectBinarySSa<mnemonic, opcode>;
  let usesCustomInserter = 1, hasNoSchedulingInfo = 1, Defs = [CC] in {
    def Sequence : Pseudo<(outs), (ins bdaddr12only:$dest, bdaddr12only:$src,
                                       imm64:$length),
                           [(sequence bdaddr12only:$dest, bdaddr12only:$src,
                                      imm64:$length)]>;
    def Loop : Pseudo<(outs), (ins bdaddr12only:$dest, bdaddr12only:$src,
                                   imm64:$length, GR64:$count256),
                      [(loop bdaddr12only:$dest, bdaddr12only:$src,
                             imm64:$length, GR64:$count256)]>;
  }
}

// The same, but setting a CC result as comparion operator.
multiclass CompareMemorySS<string mnemonic, bits<8> opcode,
                          SDPatternOperator sequence, SDPatternOperator loop> {
  def "" : SideEffectBinarySSa<mnemonic, opcode>;
  let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
    def Sequence : Pseudo<(outs), (ins bdaddr12only:$dest, bdaddr12only:$src,
                                       imm64:$length),
                           [(set CC, (sequence bdaddr12only:$dest, bdaddr12only:$src,
                                               imm64:$length))]>;
    def Loop : Pseudo<(outs), (ins bdaddr12only:$dest, bdaddr12only:$src,
                                   imm64:$length, GR64:$count256),
                      [(set CC, (loop bdaddr12only:$dest, bdaddr12only:$src,
                                      imm64:$length, GR64:$count256))]>;
  }
}

// Define an instruction that operates on two strings, both terminated
// by the character in R0.  The instruction processes a CPU-determinated
// number of bytes at a time and sets CC to 3 if the instruction needs
// to be repeated.  Also define a pseudo instruction that represents
// the full loop (the main instruction plus the branch on CC==3).
multiclass StringRRE<string mnemonic, bits<16> opcode,
                     SDPatternOperator operator> {
  let Uses = [R0L] in
    def "" : SideEffectBinaryMemMemRRE<mnemonic, opcode, GR64, GR64>;
  let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in
    def Loop : Pseudo<(outs GR64:$end),
                      (ins GR64:$start1, GR64:$start2, GR32:$char),
                      [(set GR64:$end, (operator GR64:$start1, GR64:$start2,
                                                 GR32:$char))]>;
}

// A pseudo instruction that is a direct alias of a real instruction.
// These aliases are used in cases where a particular register operand is
// fixed or where the same instruction is used with different register sizes.
// The size parameter is the size in bytes of the associated real instruction.
class Alias<int size, dag outs, dag ins, list<dag> pattern>
  : InstSystemZ<size, outs, ins, "", pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

class UnaryAliasVRS<RegisterOperand cls1, RegisterOperand cls2>
 : Alias<6, (outs cls1:$src1), (ins cls2:$src2), []>;

// An alias of a UnaryVRR*, but with different register sizes.
class UnaryAliasVRR<SDPatternOperator operator, TypedReg tr1, TypedReg tr2>
  : Alias<6, (outs tr1.op:$V1), (ins tr2.op:$V2),
          [(set (tr1.vt tr1.op:$V1), (operator (tr2.vt tr2.op:$V2)))]>;

// An alias of a UnaryVRX, but with different register sizes.
class UnaryAliasVRX<SDPatternOperator operator, TypedReg tr,
                    AddressingMode mode = bdxaddr12only>
  : Alias<6, (outs tr.op:$V1), (ins mode:$XBD2),
          [(set (tr.vt tr.op:$V1), (operator mode:$XBD2))]>;

// An alias of a StoreVRX, but with different register sizes.
class StoreAliasVRX<SDPatternOperator operator, TypedReg tr,
                    AddressingMode mode = bdxaddr12only>
  : Alias<6, (outs), (ins tr.op:$V1, mode:$XBD2),
          [(operator (tr.vt tr.op:$V1), mode:$XBD2)]>;

// An alias of a BinaryRI, but with different register sizes.
class BinaryAliasRI<SDPatternOperator operator, RegisterOperand cls,
                    Immediate imm>
  : Alias<4, (outs cls:$R1), (ins cls:$R1src, imm:$I2),
          [(set cls:$R1, (operator cls:$R1src, imm:$I2))]> {
  let Constraints = "$R1 = $R1src";
}

// An alias of a BinaryRIL, but with different register sizes.
class BinaryAliasRIL<SDPatternOperator operator, RegisterOperand cls,
                     Immediate imm>
  : Alias<6, (outs cls:$R1), (ins cls:$R1src, imm:$I2),
          [(set cls:$R1, (operator cls:$R1src, imm:$I2))]> {
  let Constraints = "$R1 = $R1src";
}

// An alias of a BinaryVRRf, but with different register sizes.
class BinaryAliasVRRf<RegisterOperand cls>
  : Alias<6, (outs VR128:$V1), (ins cls:$R2, cls:$R3), []>;

// An alias of a CompareRI, but with different register sizes.
class CompareAliasRI<SDPatternOperator operator, RegisterOperand cls,
                     Immediate imm>
  : Alias<4, (outs), (ins cls:$R1, imm:$I2),
          [(set CC, (operator cls:$R1, imm:$I2))]> {
  let isCompare = 1;
}

// An alias of a RotateSelectRIEf, but with different register sizes.
class RotateSelectAliasRIEf<RegisterOperand cls1, RegisterOperand cls2>
  : Alias<6, (outs cls1:$R1),
          (ins cls1:$R1src, cls2:$R2, imm32zx8:$I3, imm32zx8:$I4,
               imm32zx6:$I5), []> {
  let Constraints = "$R1 = $R1src";
}
