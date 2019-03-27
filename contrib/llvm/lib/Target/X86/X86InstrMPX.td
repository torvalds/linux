//===-- X86InstrMPX.td - MPX Instruction Set ---------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 MPX instruction set, defining the
// instructions, and properties of the instructions which are needed for code
// generation, machine code emission, and analysis.
//
//===----------------------------------------------------------------------===//

// FIXME: Investigate a better scheduler class once MPX is used inside LLVM.
let SchedRW = [WriteSystem] in {

multiclass mpx_bound_make<bits<8> opc, string OpcodeStr> {
  def 32rm: I<opc, MRMSrcMem, (outs BNDR:$dst), (ins anymem:$src),
              OpcodeStr#"\t{$src, $dst|$dst, $src}", []>,
              Requires<[HasMPX, Not64BitMode]>;
  def 64rm: I<opc, MRMSrcMem, (outs BNDR:$dst), (ins anymem:$src),
              OpcodeStr#"\t{$src, $dst|$dst, $src}", []>,
              Requires<[HasMPX, In64BitMode]>;
}

defm BNDMK : mpx_bound_make<0x1B, "bndmk">, XS;

multiclass mpx_bound_check<bits<8> opc, string OpcodeStr> {
  def 32rm: I<opc, MRMSrcMem, (outs), (ins  BNDR:$src1, anymem:$src2),
              OpcodeStr#"\t{$src2, $src1|$src1, $src2}", []>,
              Requires<[HasMPX, Not64BitMode]>;
  def 64rm: I<opc, MRMSrcMem, (outs), (ins  BNDR:$src1, anymem:$src2),
              OpcodeStr#"\t{$src2, $src1|$src1, $src2}", []>,
              Requires<[HasMPX, In64BitMode]>;

  def 32rr: I<opc, MRMSrcReg, (outs), (ins  BNDR:$src1, GR32:$src2),
              OpcodeStr#"\t{$src2, $src1|$src1, $src2}", []>,
              Requires<[HasMPX, Not64BitMode]>;
  def 64rr: I<opc, MRMSrcReg, (outs), (ins  BNDR:$src1, GR64:$src2),
              OpcodeStr#"\t{$src2, $src1|$src1, $src2}", []>,
              Requires<[HasMPX, In64BitMode]>;
}
defm BNDCL : mpx_bound_check<0x1A, "bndcl">, XS, NotMemoryFoldable;
defm BNDCU : mpx_bound_check<0x1A, "bndcu">, XD, NotMemoryFoldable;
defm BNDCN : mpx_bound_check<0x1B, "bndcn">, XD, NotMemoryFoldable;

def BNDMOVrr   : I<0x1A, MRMSrcReg, (outs BNDR:$dst), (ins BNDR:$src),
                  "bndmov\t{$src, $dst|$dst, $src}", []>, PD,
                  Requires<[HasMPX]>, NotMemoryFoldable;
let mayLoad = 1 in {
def BNDMOV32rm : I<0x1A, MRMSrcMem, (outs BNDR:$dst), (ins i64mem:$src),
                  "bndmov\t{$src, $dst|$dst, $src}", []>, PD,
                  Requires<[HasMPX, Not64BitMode]>, NotMemoryFoldable;
def BNDMOV64rm : I<0x1A, MRMSrcMem, (outs BNDR:$dst), (ins i128mem:$src),
                  "bndmov\t{$src, $dst|$dst, $src}", []>, PD,
                  Requires<[HasMPX, In64BitMode]>, NotMemoryFoldable;
}
let isCodeGenOnly = 1, ForceDisassemble = 1 in
def BNDMOVrr_REV   : I<0x1B, MRMDestReg, (outs BNDR:$dst), (ins BNDR:$src),
                       "bndmov\t{$src, $dst|$dst, $src}", []>, PD,
                       Requires<[HasMPX]>, NotMemoryFoldable;
let mayStore = 1 in {
def BNDMOV32mr : I<0x1B, MRMDestMem, (outs), (ins i64mem:$dst, BNDR:$src),
                  "bndmov\t{$src, $dst|$dst, $src}", []>, PD,
                  Requires<[HasMPX, Not64BitMode]>, NotMemoryFoldable;
def BNDMOV64mr : I<0x1B, MRMDestMem, (outs), (ins i128mem:$dst, BNDR:$src),
                  "bndmov\t{$src, $dst|$dst, $src}", []>, PD,
                  Requires<[HasMPX, In64BitMode]>, NotMemoryFoldable;

def BNDSTXmr:      I<0x1B, MRMDestMem, (outs), (ins anymem:$dst, BNDR:$src),
                    "bndstx\t{$src, $dst|$dst, $src}", []>, PS,
                    Requires<[HasMPX]>;
}
let mayLoad = 1 in
def BNDLDXrm:      I<0x1A, MRMSrcMem, (outs BNDR:$dst), (ins anymem:$src),
                    "bndldx\t{$src, $dst|$dst, $src}", []>, PS,
                    Requires<[HasMPX]>;
} // SchedRW
