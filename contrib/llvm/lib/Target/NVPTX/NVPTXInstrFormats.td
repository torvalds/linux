//===- NVPTXInstrFormats.td - NVPTX Instruction Formats-------*- tblgen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe NVPTX instructions format
//
//===----------------------------------------------------------------------===//

// Vector instruction type enum
class VecInstTypeEnum<bits<4> val> {
  bits<4> Value=val;
}
def VecNOP : VecInstTypeEnum<0>;

// Generic NVPTX Format

class NVPTXInst<dag outs, dag ins, string asmstr, list<dag> pattern>
  : Instruction {
  field bits<14> Inst;

  let Namespace = "NVPTX";
  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;

  // TSFlagFields
  bits<4> VecInstType = VecNOP.Value;
  bit IsSimpleMove = 0;
  bit IsLoad = 0;
  bit IsStore = 0;

  bit IsTex = 0;
  bit IsSust = 0;
  bit IsSurfTexQuery = 0;
  bit IsTexModeUnified = 0;

  // The following field is encoded as log2 of the vector size minus one,
  // with 0 meaning the operation is not a surface instruction.  For example,
  // if IsSuld == 2, then the instruction is a suld instruction with vector size
  // 2**(2-1) = 2.
  bits<2> IsSuld = 0;

  let TSFlags{3-0}   = VecInstType;
  let TSFlags{4-4}   = IsSimpleMove;
  let TSFlags{5-5}   = IsLoad;
  let TSFlags{6-6}   = IsStore;
  let TSFlags{7}     = IsTex;
  let TSFlags{9-8}   = IsSuld;
  let TSFlags{10}    = IsSust;
  let TSFlags{11}    = IsSurfTexQuery;
  let TSFlags{12}    = IsTexModeUnified;
}
