//===-- X86InstrControl.td - Control Flow Instructions -----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the X86 jump, return, call, and related instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Control Flow Instructions.
//

// Return instructions.
//
// The X86retflag return instructions are variadic because we may add ST0 and
// ST1 arguments when returning values on the x87 stack.
let isTerminator = 1, isReturn = 1, isBarrier = 1,
    hasCtrlDep = 1, FPForm = SpecialFP, SchedRW = [WriteJumpLd] in {
  def RETL   : I   <0xC3, RawFrm, (outs), (ins variable_ops),
                    "ret{l}", []>, OpSize32, Requires<[Not64BitMode]>;
  def RETQ   : I   <0xC3, RawFrm, (outs), (ins variable_ops),
                    "ret{q}", []>, OpSize32, Requires<[In64BitMode]>;
  def RETW   : I   <0xC3, RawFrm, (outs), (ins),
                    "ret{w}", []>, OpSize16;
  def RETIL  : Ii16<0xC2, RawFrm, (outs), (ins i16imm:$amt, variable_ops),
                    "ret{l}\t$amt", []>, OpSize32, Requires<[Not64BitMode]>;
  def RETIQ  : Ii16<0xC2, RawFrm, (outs), (ins i16imm:$amt, variable_ops),
                    "ret{q}\t$amt", []>, OpSize32, Requires<[In64BitMode]>;
  def RETIW  : Ii16<0xC2, RawFrm, (outs), (ins i16imm:$amt),
                    "ret{w}\t$amt", []>, OpSize16;
  def LRETL  : I   <0xCB, RawFrm, (outs), (ins),
                    "{l}ret{l|f}", []>, OpSize32;
  def LRETQ  : RI  <0xCB, RawFrm, (outs), (ins),
                    "{l}ret{|f}q", []>, Requires<[In64BitMode]>;
  def LRETW  : I   <0xCB, RawFrm, (outs), (ins),
                    "{l}ret{w|f}", []>, OpSize16;
  def LRETIL : Ii16<0xCA, RawFrm, (outs), (ins i16imm:$amt),
                    "{l}ret{l|f}\t$amt", []>, OpSize32;
  def LRETIQ : RIi16<0xCA, RawFrm, (outs), (ins i16imm:$amt),
                    "{l}ret{|f}q\t$amt", []>, Requires<[In64BitMode]>;
  def LRETIW : Ii16<0xCA, RawFrm, (outs), (ins i16imm:$amt),
                    "{l}ret{w|f}\t$amt", []>, OpSize16;

  // The machine return from interrupt instruction, but sometimes we need to
  // perform a post-epilogue stack adjustment. Codegen emits the pseudo form
  // which expands to include an SP adjustment if necessary.
  def IRET16 : I   <0xcf, RawFrm, (outs), (ins), "iret{w}", []>,
               OpSize16;
  def IRET32 : I   <0xcf, RawFrm, (outs), (ins), "iret{l|d}", []>, OpSize32;
  def IRET64 : RI  <0xcf, RawFrm, (outs), (ins), "iretq", []>, Requires<[In64BitMode]>;
  let isCodeGenOnly = 1 in
  def IRET : PseudoI<(outs), (ins i32imm:$adj), [(X86iret timm:$adj)]>;
  def RET  : PseudoI<(outs), (ins i32imm:$adj, variable_ops), [(X86retflag timm:$adj)]>;
}

// Unconditional branches.
let isBarrier = 1, isBranch = 1, isTerminator = 1, SchedRW = [WriteJump] in {
  def JMP_1 : Ii8PCRel<0xEB, RawFrm, (outs), (ins brtarget8:$dst),
                       "jmp\t$dst", [(br bb:$dst)]>;
  let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in {
    def JMP_2 : Ii16PCRel<0xE9, RawFrm, (outs), (ins brtarget16:$dst),
                          "jmp\t$dst", []>, OpSize16;
    def JMP_4 : Ii32PCRel<0xE9, RawFrm, (outs), (ins brtarget32:$dst),
                          "jmp\t$dst", []>, OpSize32;
  }
}

// Conditional Branches.
let isBranch = 1, isTerminator = 1, Uses = [EFLAGS], SchedRW = [WriteJump] in {
  multiclass ICBr<bits<8> opc1, bits<8> opc4, string asm, PatFrag Cond> {
    def _1 : Ii8PCRel <opc1, RawFrm, (outs), (ins brtarget8:$dst), asm,
                       [(X86brcond bb:$dst, Cond, EFLAGS)]>;
    let hasSideEffects = 0, isCodeGenOnly = 1, ForceDisassemble = 1 in {
      def _2 : Ii16PCRel<opc4, RawFrm, (outs), (ins brtarget16:$dst), asm,
                         []>, OpSize16, TB;
      def _4 : Ii32PCRel<opc4, RawFrm, (outs), (ins brtarget32:$dst), asm,
                         []>, TB, OpSize32;
    }
  }
}

defm JO  : ICBr<0x70, 0x80, "jo\t$dst" , X86_COND_O>;
defm JNO : ICBr<0x71, 0x81, "jno\t$dst", X86_COND_NO>;
defm JB  : ICBr<0x72, 0x82, "jb\t$dst" , X86_COND_B>;
defm JAE : ICBr<0x73, 0x83, "jae\t$dst", X86_COND_AE>;
defm JE  : ICBr<0x74, 0x84, "je\t$dst" , X86_COND_E>;
defm JNE : ICBr<0x75, 0x85, "jne\t$dst", X86_COND_NE>;
defm JBE : ICBr<0x76, 0x86, "jbe\t$dst", X86_COND_BE>;
defm JA  : ICBr<0x77, 0x87, "ja\t$dst" , X86_COND_A>;
defm JS  : ICBr<0x78, 0x88, "js\t$dst" , X86_COND_S>;
defm JNS : ICBr<0x79, 0x89, "jns\t$dst", X86_COND_NS>;
defm JP  : ICBr<0x7A, 0x8A, "jp\t$dst" , X86_COND_P>;
defm JNP : ICBr<0x7B, 0x8B, "jnp\t$dst", X86_COND_NP>;
defm JL  : ICBr<0x7C, 0x8C, "jl\t$dst" , X86_COND_L>;
defm JGE : ICBr<0x7D, 0x8D, "jge\t$dst", X86_COND_GE>;
defm JLE : ICBr<0x7E, 0x8E, "jle\t$dst", X86_COND_LE>;
defm JG  : ICBr<0x7F, 0x8F, "jg\t$dst" , X86_COND_G>;

// jcx/jecx/jrcx instructions.
let isBranch = 1, isTerminator = 1, hasSideEffects = 0, SchedRW = [WriteJump] in {
  // These are the 32-bit versions of this instruction for the asmparser.  In
  // 32-bit mode, the address size prefix is jcxz and the unprefixed version is
  // jecxz.
  let Uses = [CX] in
    def JCXZ : Ii8PCRel<0xE3, RawFrm, (outs), (ins brtarget8:$dst),
                        "jcxz\t$dst", []>, AdSize16, Requires<[Not64BitMode]>;
  let Uses = [ECX] in
    def JECXZ : Ii8PCRel<0xE3, RawFrm, (outs), (ins brtarget8:$dst),
                        "jecxz\t$dst", []>, AdSize32;

  let Uses = [RCX] in
    def JRCXZ : Ii8PCRel<0xE3, RawFrm, (outs), (ins brtarget8:$dst),
                         "jrcxz\t$dst", []>, AdSize64, Requires<[In64BitMode]>;
}

// Indirect branches
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in {
  def JMP16r     : I<0xFF, MRM4r, (outs), (ins GR16:$dst), "jmp{w}\t{*}$dst",
                     [(brind GR16:$dst)]>, Requires<[Not64BitMode]>,
                     OpSize16, Sched<[WriteJump]>;
  def JMP16m     : I<0xFF, MRM4m, (outs), (ins i16mem:$dst), "jmp{w}\t{*}$dst",
                     [(brind (loadi16 addr:$dst))]>, Requires<[Not64BitMode]>,
                     OpSize16, Sched<[WriteJumpLd]>;

  def JMP32r     : I<0xFF, MRM4r, (outs), (ins GR32:$dst), "jmp{l}\t{*}$dst",
                     [(brind GR32:$dst)]>, Requires<[Not64BitMode]>,
                     OpSize32, Sched<[WriteJump]>;
  def JMP32m     : I<0xFF, MRM4m, (outs), (ins i32mem:$dst), "jmp{l}\t{*}$dst",
                     [(brind (loadi32 addr:$dst))]>, Requires<[Not64BitMode]>,
                     OpSize32, Sched<[WriteJumpLd]>;

  def JMP64r     : I<0xFF, MRM4r, (outs), (ins GR64:$dst), "jmp{q}\t{*}$dst",
                     [(brind GR64:$dst)]>, Requires<[In64BitMode]>,
                     Sched<[WriteJump]>;
  def JMP64m     : I<0xFF, MRM4m, (outs), (ins i64mem:$dst), "jmp{q}\t{*}$dst",
                     [(brind (loadi64 addr:$dst))]>, Requires<[In64BitMode]>,
                     Sched<[WriteJumpLd]>;

  // Non-tracking jumps for IBT, use with caution.
  let isCodeGenOnly = 1 in {
    def JMP16r_NT : I<0xFF, MRM4r, (outs), (ins GR16 : $dst), "jmp{w}\t{*}$dst",
                      [(X86NoTrackBrind GR16 : $dst)]>, Requires<[Not64BitMode]>,
                      OpSize16, Sched<[WriteJump]>, NOTRACK;

    def JMP16m_NT : I<0xFF, MRM4m, (outs), (ins i16mem : $dst), "jmp{w}\t{*}$dst",
                      [(X86NoTrackBrind (loadi16 addr : $dst))]>,
                      Requires<[Not64BitMode]>, OpSize16, Sched<[WriteJumpLd]>,
                      NOTRACK;

    def JMP32r_NT : I<0xFF, MRM4r, (outs), (ins GR32 : $dst), "jmp{l}\t{*}$dst",
                      [(X86NoTrackBrind GR32 : $dst)]>, Requires<[Not64BitMode]>,
                      OpSize32, Sched<[WriteJump]>, NOTRACK;
    def JMP32m_NT : I<0xFF, MRM4m, (outs), (ins i32mem : $dst), "jmp{l}\t{*}$dst",
                      [(X86NoTrackBrind (loadi32 addr : $dst))]>,
                      Requires<[Not64BitMode]>, OpSize32, Sched<[WriteJumpLd]>,
                      NOTRACK;

    def JMP64r_NT : I<0xFF, MRM4r, (outs), (ins GR64 : $dst), "jmp{q}\t{*}$dst",
                      [(X86NoTrackBrind GR64 : $dst)]>, Requires<[In64BitMode]>,
                      Sched<[WriteJump]>, NOTRACK;
    def JMP64m_NT : I<0xFF, MRM4m, (outs), (ins i64mem : $dst), "jmp{q}\t{*}$dst",
                      [(X86NoTrackBrind(loadi64 addr : $dst))]>,
                      Requires<[In64BitMode]>, Sched<[WriteJumpLd]>, NOTRACK;
  }

  let Predicates = [Not64BitMode], AsmVariantName = "att" in {
    def FARJMP16i  : Iseg16<0xEA, RawFrmImm16, (outs),
                            (ins i16imm:$off, i16imm:$seg),
                            "ljmp{w}\t$seg, $off", []>,
                            OpSize16, Sched<[WriteJump]>;
    def FARJMP32i  : Iseg32<0xEA, RawFrmImm16, (outs),
                            (ins i32imm:$off, i16imm:$seg),
                            "ljmp{l}\t$seg, $off", []>,
                            OpSize32, Sched<[WriteJump]>;
  }
  def FARJMP64   : RI<0xFF, MRM5m, (outs), (ins opaquemem:$dst),
                      "ljmp{q}\t{*}$dst", []>, Sched<[WriteJump]>, Requires<[In64BitMode]>;

  let AsmVariantName = "att" in
  def FARJMP16m  : I<0xFF, MRM5m, (outs), (ins opaquemem:$dst),
                     "ljmp{w}\t{*}$dst", []>, OpSize16, Sched<[WriteJumpLd]>;
  def FARJMP32m  : I<0xFF, MRM5m, (outs), (ins opaquemem:$dst),
                     "{l}jmp{l}\t{*}$dst", []>, OpSize32, Sched<[WriteJumpLd]>;
}

// Loop instructions
let SchedRW = [WriteJump] in {
def LOOP   : Ii8PCRel<0xE2, RawFrm, (outs), (ins brtarget8:$dst), "loop\t$dst", []>;
def LOOPE  : Ii8PCRel<0xE1, RawFrm, (outs), (ins brtarget8:$dst), "loope\t$dst", []>;
def LOOPNE : Ii8PCRel<0xE0, RawFrm, (outs), (ins brtarget8:$dst), "loopne\t$dst", []>;
}

//===----------------------------------------------------------------------===//
//  Call Instructions...
//
let isCall = 1 in
  // All calls clobber the non-callee saved registers. ESP is marked as
  // a use to prevent stack-pointer assignments that appear immediately
  // before calls from potentially appearing dead. Uses for argument
  // registers are added manually.
  let Uses = [ESP, SSP] in {
    def CALLpcrel32 : Ii32PCRel<0xE8, RawFrm,
                           (outs), (ins i32imm_pcrel:$dst),
                           "call{l}\t$dst", []>, OpSize32,
                      Requires<[Not64BitMode]>, Sched<[WriteJump]>;
    let hasSideEffects = 0 in
      def CALLpcrel16 : Ii16PCRel<0xE8, RawFrm,
                             (outs), (ins i16imm_pcrel:$dst),
                             "call{w}\t$dst", []>, OpSize16,
                        Sched<[WriteJump]>;
    def CALL16r     : I<0xFF, MRM2r, (outs), (ins GR16:$dst),
                        "call{w}\t{*}$dst", [(X86call GR16:$dst)]>,
                      OpSize16, Requires<[Not64BitMode]>, Sched<[WriteJump]>;
    def CALL16m     : I<0xFF, MRM2m, (outs), (ins i16mem:$dst),
                        "call{w}\t{*}$dst", [(X86call (loadi16 addr:$dst))]>,
                        OpSize16, Requires<[Not64BitMode,FavorMemIndirectCall]>,
                        Sched<[WriteJumpLd]>;
    def CALL32r     : I<0xFF, MRM2r, (outs), (ins GR32:$dst),
                        "call{l}\t{*}$dst", [(X86call GR32:$dst)]>, OpSize32,
                        Requires<[Not64BitMode,NotUseRetpolineIndirectCalls]>,
                        Sched<[WriteJump]>;
    def CALL32m     : I<0xFF, MRM2m, (outs), (ins i32mem:$dst),
                        "call{l}\t{*}$dst", [(X86call (loadi32 addr:$dst))]>,
                        OpSize32,
                        Requires<[Not64BitMode,FavorMemIndirectCall,
                                  NotUseRetpolineIndirectCalls]>,
                        Sched<[WriteJumpLd]>;

    // Non-tracking calls for IBT, use with caution.
    let isCodeGenOnly = 1 in {
      def CALL16r_NT : I<0xFF, MRM2r, (outs), (ins GR16 : $dst),
                        "call{w}\t{*}$dst",[(X86NoTrackCall GR16 : $dst)]>,
                        OpSize16, Requires<[Not64BitMode]>, Sched<[WriteJump]>, NOTRACK;
      def CALL16m_NT : I<0xFF, MRM2m, (outs), (ins i16mem : $dst),
                        "call{w}\t{*}$dst",[(X86NoTrackCall(loadi16 addr : $dst))]>,
                        OpSize16, Requires<[Not64BitMode,FavorMemIndirectCall]>,
                        Sched<[WriteJumpLd]>, NOTRACK;
      def CALL32r_NT : I<0xFF, MRM2r, (outs), (ins GR32 : $dst),
                        "call{l}\t{*}$dst",[(X86NoTrackCall GR32 : $dst)]>,
                        OpSize32, Requires<[Not64BitMode]>, Sched<[WriteJump]>, NOTRACK;
      def CALL32m_NT : I<0xFF, MRM2m, (outs), (ins i32mem : $dst),
                        "call{l}\t{*}$dst",[(X86NoTrackCall(loadi32 addr : $dst))]>,
                        OpSize32, Requires<[Not64BitMode,FavorMemIndirectCall]>,
                        Sched<[WriteJumpLd]>, NOTRACK;
    }

    let Predicates = [Not64BitMode], AsmVariantName = "att" in {
      def FARCALL16i  : Iseg16<0x9A, RawFrmImm16, (outs),
                               (ins i16imm:$off, i16imm:$seg),
                               "lcall{w}\t$seg, $off", []>,
                               OpSize16, Sched<[WriteJump]>;
      def FARCALL32i  : Iseg32<0x9A, RawFrmImm16, (outs),
                               (ins i32imm:$off, i16imm:$seg),
                               "lcall{l}\t$seg, $off", []>,
                               OpSize32, Sched<[WriteJump]>;
    }

    def FARCALL16m  : I<0xFF, MRM3m, (outs), (ins opaquemem:$dst),
                        "lcall{w}\t{*}$dst", []>, OpSize16, Sched<[WriteJumpLd]>;
    def FARCALL32m  : I<0xFF, MRM3m, (outs), (ins opaquemem:$dst),
                        "{l}call{l}\t{*}$dst", []>, OpSize32, Sched<[WriteJumpLd]>;
  }


// Tail call stuff.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    isCodeGenOnly = 1, SchedRW = [WriteJumpLd] in
  let Uses = [ESP, SSP] in {
  def TCRETURNdi : PseudoI<(outs),
                     (ins i32imm_pcrel:$dst, i32imm:$offset), []>, NotMemoryFoldable;
  def TCRETURNri : PseudoI<(outs),
                     (ins ptr_rc_tailcall:$dst, i32imm:$offset), []>, NotMemoryFoldable;
  let mayLoad = 1 in
  def TCRETURNmi : PseudoI<(outs),
                     (ins i32mem_TC:$dst, i32imm:$offset), []>;

  // FIXME: The should be pseudo instructions that are lowered when going to
  // mcinst.
  def TAILJMPd : Ii32PCRel<0xE9, RawFrm, (outs),
                           (ins i32imm_pcrel:$dst), "jmp\t$dst", []>;

  def TAILJMPr : I<0xFF, MRM4r, (outs), (ins ptr_rc_tailcall:$dst),
                   "", []>;  // FIXME: Remove encoding when JIT is dead.
  let mayLoad = 1 in
  def TAILJMPm : I<0xFF, MRM4m, (outs), (ins i32mem_TC:$dst),
                   "jmp{l}\t{*}$dst", []>;
}

// Conditional tail calls are similar to the above, but they are branches
// rather than barriers, and they use EFLAGS.
let isCall = 1, isTerminator = 1, isReturn = 1, isBranch = 1,
    isCodeGenOnly = 1, SchedRW = [WriteJumpLd] in
  let Uses = [ESP, EFLAGS, SSP] in {
  def TCRETURNdicc : PseudoI<(outs),
                     (ins i32imm_pcrel:$dst, i32imm:$offset, i32imm:$cond), []>;

  // This gets substituted to a conditional jump instruction in MC lowering.
  def TAILJMPd_CC : Ii32PCRel<0x80, RawFrm, (outs),
                           (ins i32imm_pcrel:$dst, i32imm:$cond), "", []>;
}


//===----------------------------------------------------------------------===//
//  Call Instructions...
//

// RSP is marked as a use to prevent stack-pointer assignments that appear
// immediately before calls from potentially appearing dead. Uses for argument
// registers are added manually.
let isCall = 1, Uses = [RSP, SSP], SchedRW = [WriteJump] in {
  // NOTE: this pattern doesn't match "X86call imm", because we do not know
  // that the offset between an arbitrary immediate and the call will fit in
  // the 32-bit pcrel field that we have.
  def CALL64pcrel32 : Ii32PCRel<0xE8, RawFrm,
                        (outs), (ins i64i32imm_pcrel:$dst),
                        "call{q}\t$dst", []>, OpSize32,
                      Requires<[In64BitMode]>;
  def CALL64r       : I<0xFF, MRM2r, (outs), (ins GR64:$dst),
                        "call{q}\t{*}$dst", [(X86call GR64:$dst)]>,
                      Requires<[In64BitMode,NotUseRetpolineIndirectCalls]>;
  def CALL64m       : I<0xFF, MRM2m, (outs), (ins i64mem:$dst),
                        "call{q}\t{*}$dst", [(X86call (loadi64 addr:$dst))]>,
                      Requires<[In64BitMode,FavorMemIndirectCall,
                                NotUseRetpolineIndirectCalls]>;

  // Non-tracking calls for IBT, use with caution.
  let isCodeGenOnly = 1 in {
    def CALL64r_NT : I<0xFF, MRM2r, (outs), (ins GR64 : $dst),
                      "call{q}\t{*}$dst",[(X86NoTrackCall GR64 : $dst)]>,
                      Requires<[In64BitMode]>, NOTRACK;
    def CALL64m_NT : I<0xFF, MRM2m, (outs), (ins i64mem : $dst),
                       "call{q}\t{*}$dst",
                       [(X86NoTrackCall(loadi64 addr : $dst))]>,
                       Requires<[In64BitMode,FavorMemIndirectCall]>, NOTRACK;
  }

  def FARCALL64   : RI<0xFF, MRM3m, (outs), (ins opaquemem:$dst),
                       "lcall{q}\t{*}$dst", []>;
}

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    isCodeGenOnly = 1, Uses = [RSP, SSP], SchedRW = [WriteJump] in {
  def TCRETURNdi64   : PseudoI<(outs),
                        (ins i64i32imm_pcrel:$dst, i32imm:$offset),
                        []>;
  def TCRETURNri64   : PseudoI<(outs),
                        (ins ptr_rc_tailcall:$dst, i32imm:$offset), []>, NotMemoryFoldable;
  let mayLoad = 1 in
  def TCRETURNmi64   : PseudoI<(outs),
                        (ins i64mem_TC:$dst, i32imm:$offset), []>, NotMemoryFoldable;

  def TAILJMPd64 : Ii32PCRel<0xE9, RawFrm, (outs), (ins i64i32imm_pcrel:$dst),
                   "jmp\t$dst", []>;

  def TAILJMPr64 : I<0xFF, MRM4r, (outs), (ins ptr_rc_tailcall:$dst),
                     "jmp{q}\t{*}$dst", []>;

  let mayLoad = 1 in
  def TAILJMPm64 : I<0xFF, MRM4m, (outs), (ins i64mem_TC:$dst),
                     "jmp{q}\t{*}$dst", []>;

  // Win64 wants indirect jumps leaving the function to have a REX_W prefix.
  let hasREX_WPrefix = 1 in {
    def TAILJMPr64_REX : I<0xFF, MRM4r, (outs), (ins ptr_rc_tailcall:$dst),
                           "rex64 jmp{q}\t{*}$dst", []>;

    let mayLoad = 1 in
    def TAILJMPm64_REX : I<0xFF, MRM4m, (outs), (ins i64mem_TC:$dst),
                           "rex64 jmp{q}\t{*}$dst", []>;
  }
}

let isPseudo = 1, isCall = 1, isCodeGenOnly = 1,
    Uses = [RSP, SSP],
    usesCustomInserter = 1,
    SchedRW = [WriteJump] in {
  def RETPOLINE_CALL32 :
    PseudoI<(outs), (ins GR32:$dst), [(X86call GR32:$dst)]>,
            Requires<[Not64BitMode,UseRetpolineIndirectCalls]>;

  def RETPOLINE_CALL64 :
    PseudoI<(outs), (ins GR64:$dst), [(X86call GR64:$dst)]>,
            Requires<[In64BitMode,UseRetpolineIndirectCalls]>;

  // Retpoline variant of indirect tail calls.
  let isTerminator = 1, isReturn = 1, isBarrier = 1 in {
    def RETPOLINE_TCRETURN64 :
      PseudoI<(outs), (ins GR64:$dst, i32imm:$offset), []>;
    def RETPOLINE_TCRETURN32 :
      PseudoI<(outs), (ins GR32:$dst, i32imm:$offset), []>;
  }
}

// Conditional tail calls are similar to the above, but they are branches
// rather than barriers, and they use EFLAGS.
let isCall = 1, isTerminator = 1, isReturn = 1, isBranch = 1,
    isCodeGenOnly = 1, SchedRW = [WriteJumpLd] in
  let Uses = [RSP, EFLAGS, SSP] in {
  def TCRETURNdi64cc : PseudoI<(outs),
                           (ins i64i32imm_pcrel:$dst, i32imm:$offset,
                            i32imm:$cond), []>;

  // This gets substituted to a conditional jump instruction in MC lowering.
  def TAILJMPd64_CC : Ii32PCRel<0x80, RawFrm, (outs),
                           (ins i64i32imm_pcrel:$dst, i32imm:$cond), "", []>;
}
