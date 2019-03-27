//===-- X86InstrCMovSetCC.td - Conditional Move and SetCC --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 conditional move and set on condition
// instructions.
//
//===----------------------------------------------------------------------===//


// CMOV instructions.
multiclass CMOV<bits<8> opc, string Mnemonic, X86FoldableSchedWrite Sched,
                PatLeaf CondNode> {
  let Uses = [EFLAGS], Predicates = [HasCMov], Constraints = "$src1 = $dst",
      isCommutable = 1, SchedRW = [Sched] in {
    def NAME#16rr
      : I<opc, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
          !strconcat(Mnemonic, "{w}\t{$src2, $dst|$dst, $src2}"),
          [(set GR16:$dst,
                (X86cmov GR16:$src1, GR16:$src2, CondNode, EFLAGS))]>,
                TB, OpSize16;
    def NAME#32rr
      : I<opc, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
          !strconcat(Mnemonic, "{l}\t{$src2, $dst|$dst, $src2}"),
          [(set GR32:$dst,
                (X86cmov GR32:$src1, GR32:$src2, CondNode, EFLAGS))]>,
                TB, OpSize32;
    def NAME#64rr
      :RI<opc, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
          !strconcat(Mnemonic, "{q}\t{$src2, $dst|$dst, $src2}"),
          [(set GR64:$dst,
                (X86cmov GR64:$src1, GR64:$src2, CondNode, EFLAGS))]>, TB;
  }

  let Uses = [EFLAGS], Predicates = [HasCMov], Constraints = "$src1 = $dst",
      SchedRW = [Sched.Folded, Sched.ReadAfterFold] in {
    def NAME#16rm
      : I<opc, MRMSrcMem, (outs GR16:$dst), (ins GR16:$src1, i16mem:$src2),
          !strconcat(Mnemonic, "{w}\t{$src2, $dst|$dst, $src2}"),
          [(set GR16:$dst, (X86cmov GR16:$src1, (loadi16 addr:$src2),
                                    CondNode, EFLAGS))]>, TB, OpSize16;
    def NAME#32rm
      : I<opc, MRMSrcMem, (outs GR32:$dst), (ins GR32:$src1, i32mem:$src2),
          !strconcat(Mnemonic, "{l}\t{$src2, $dst|$dst, $src2}"),
          [(set GR32:$dst, (X86cmov GR32:$src1, (loadi32 addr:$src2),
                                    CondNode, EFLAGS))]>, TB, OpSize32;
    def NAME#64rm
      :RI<opc, MRMSrcMem, (outs GR64:$dst), (ins GR64:$src1, i64mem:$src2),
          !strconcat(Mnemonic, "{q}\t{$src2, $dst|$dst, $src2}"),
          [(set GR64:$dst, (X86cmov GR64:$src1, (loadi64 addr:$src2),
                                    CondNode, EFLAGS))]>, TB;
  } // Uses = [EFLAGS], Predicates = [HasCMov], Constraints = "$src1 = $dst"
} // end multiclass


// Conditional Moves.
defm CMOVO  : CMOV<0x40, "cmovo" , WriteCMOV,  X86_COND_O>;
defm CMOVNO : CMOV<0x41, "cmovno", WriteCMOV,  X86_COND_NO>;
defm CMOVB  : CMOV<0x42, "cmovb" , WriteCMOV,  X86_COND_B>;
defm CMOVAE : CMOV<0x43, "cmovae", WriteCMOV,  X86_COND_AE>;
defm CMOVE  : CMOV<0x44, "cmove" , WriteCMOV,  X86_COND_E>;
defm CMOVNE : CMOV<0x45, "cmovne", WriteCMOV,  X86_COND_NE>;
defm CMOVBE : CMOV<0x46, "cmovbe", WriteCMOV2, X86_COND_BE>;
defm CMOVA  : CMOV<0x47, "cmova" , WriteCMOV2, X86_COND_A>;
defm CMOVS  : CMOV<0x48, "cmovs" , WriteCMOV,  X86_COND_S>;
defm CMOVNS : CMOV<0x49, "cmovns", WriteCMOV,  X86_COND_NS>;
defm CMOVP  : CMOV<0x4A, "cmovp" , WriteCMOV,  X86_COND_P>;
defm CMOVNP : CMOV<0x4B, "cmovnp", WriteCMOV,  X86_COND_NP>;
defm CMOVL  : CMOV<0x4C, "cmovl" , WriteCMOV,  X86_COND_L>;
defm CMOVGE : CMOV<0x4D, "cmovge", WriteCMOV,  X86_COND_GE>;
defm CMOVLE : CMOV<0x4E, "cmovle", WriteCMOV,  X86_COND_LE>;
defm CMOVG  : CMOV<0x4F, "cmovg" , WriteCMOV,  X86_COND_G>;


// SetCC instructions.
multiclass SETCC<bits<8> opc, string Mnemonic, PatLeaf OpNode> {
  let Uses = [EFLAGS] in {
    def r    : I<opc, MRMXr,  (outs GR8:$dst), (ins),
                     !strconcat(Mnemonic, "\t$dst"),
                     [(set GR8:$dst, (X86setcc OpNode, EFLAGS))]>,
                     TB, Sched<[WriteSETCC]>;
    def m    : I<opc, MRMXm,  (outs), (ins i8mem:$dst),
                     !strconcat(Mnemonic, "\t$dst"),
                     [(store (X86setcc OpNode, EFLAGS), addr:$dst)]>,
                     TB, Sched<[WriteSETCCStore]>;
  } // Uses = [EFLAGS]
}

defm SETO  : SETCC<0x90, "seto",  X86_COND_O>;   // is overflow bit set
defm SETNO : SETCC<0x91, "setno", X86_COND_NO>;  // is overflow bit not set
defm SETB  : SETCC<0x92, "setb",  X86_COND_B>;   // unsigned less than
defm SETAE : SETCC<0x93, "setae", X86_COND_AE>;  // unsigned greater or equal
defm SETE  : SETCC<0x94, "sete",  X86_COND_E>;   // equal to
defm SETNE : SETCC<0x95, "setne", X86_COND_NE>;  // not equal to
defm SETBE : SETCC<0x96, "setbe", X86_COND_BE>;  // unsigned less than or equal
defm SETA  : SETCC<0x97, "seta",  X86_COND_A>;   // unsigned greater than
defm SETS  : SETCC<0x98, "sets",  X86_COND_S>;   // is signed bit set
defm SETNS : SETCC<0x99, "setns", X86_COND_NS>;  // is not signed
defm SETP  : SETCC<0x9A, "setp",  X86_COND_P>;   // is parity bit set
defm SETNP : SETCC<0x9B, "setnp", X86_COND_NP>;  // is parity bit not set
defm SETL  : SETCC<0x9C, "setl",  X86_COND_L>;   // signed less than
defm SETGE : SETCC<0x9D, "setge", X86_COND_GE>;  // signed greater or equal
defm SETLE : SETCC<0x9E, "setle", X86_COND_LE>;  // signed less than or equal
defm SETG  : SETCC<0x9F, "setg",  X86_COND_G>;   // signed greater than

// SALC is an undocumented instruction. Information for this instruction can be found
// here http://www.rcollins.org/secrets/opcodes/SALC.html
// Set AL if carry. 
let Uses = [EFLAGS], Defs = [AL], SchedRW = [WriteALU] in {
  def SALC : I<0xD6, RawFrm, (outs), (ins), "salc", []>, Requires<[Not64BitMode]>;
}
