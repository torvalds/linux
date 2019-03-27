//===-- RISCVInstrFormatsC.td - RISCV C Instruction Formats --*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file describes the RISC-V C extension instruction formats.
//
//===----------------------------------------------------------------------===//

class RVInst16<dag outs, dag ins, string opcodestr, string argstr,
               list<dag> pattern, InstFormat format>
    : Instruction {
  field bits<16> Inst;
  // SoftFail is a field the disassembler can use to provide a way for
  // instructions to not match without killing the whole decode process. It is
  // mainly used for ARM, but Tablegen expects this field to exist or it fails
  // to build the decode table.
  field bits<16> SoftFail = 0;
  let Size = 2;

  bits<2> Opcode = 0;

  let Namespace = "RISCV";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = opcodestr # "\t" # argstr;
  let Pattern = pattern;

  let TSFlags{4-0} = format.Value;
}

class RVInst16CR<bits<4> funct4, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCR> {
  bits<5> rs1;
  bits<5> rs2;

  let Inst{15-12} = funct4;
  let Inst{11-7} = rs1;
  let Inst{6-2} = rs2;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{6-2} must be set for each instruction.
class RVInst16CI<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCI> {
  bits<10> imm;
  bits<5> rd;
  bits<5> rs1;

  let Inst{15-13} = funct3;
  let Inst{12} = imm{5};
  let Inst{11-7} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{12-7} must be set for each instruction.
class RVInst16CSS<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCSS> {
  bits<10> imm;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{15-13} = funct3;
  let Inst{6-2} = rs2;
  let Inst{1-0} = opcode;
}

class RVInst16CIW<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                  string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCIW> {
  bits<10> imm;
  bits<3> rd;

  let Inst{15-13} = funct3;
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{12-10} and Inst{6-5} must be set for each instruction.
class RVInst16CL<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCL> {
  bits<3> rd;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{9-7} = rs1;
  let Inst{4-2} = rd;
  let Inst{1-0} = opcode;
}

// The immediate value encoding differs for each instruction, so each subclass
// is responsible for setting the appropriate bits in the Inst field.
// The bits Inst{12-10} and Inst{6-5} must be set for each instruction.
class RVInst16CS<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCS> {
  bits<3> rs2;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{9-7} = rs1;
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;
}

class RVInst16CA<bits<6> funct6, bits<2> funct2, bits<2> opcode, dag outs,
                 dag ins, string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCA> {
  bits<3> rs2;
  bits<3> rs1;

  let Inst{15-10} = funct6;
  let Inst{9-7} = rs1;
  let Inst{6-5} = funct2;
  let Inst{4-2} = rs2;
  let Inst{1-0} = opcode;
}

class RVInst16CB<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCB> {
  bits<9> imm;
  bits<3> rs1;

  let Inst{15-13} = funct3;
  let Inst{9-7} = rs1;
  let Inst{1-0} = opcode;
}

class RVInst16CJ<bits<3> funct3, bits<2> opcode, dag outs, dag ins,
                 string opcodestr, string argstr>
    : RVInst16<outs, ins, opcodestr, argstr, [], InstFormatCJ> {
  bits<11> offset;

  let Inst{15-13} = funct3;
  let Inst{12} = offset{10};
  let Inst{11} = offset{3};
  let Inst{10-9} = offset{8-7};
  let Inst{8} = offset{9};
  let Inst{7} = offset{5};
  let Inst{6} = offset{6};
  let Inst{5-3} = offset{2-0};
  let Inst{2} = offset{4};
  let Inst{1-0} = opcode;
}
