//===- X86InstrCompiler.td - Compiler Pseudos and Patterns -*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the various pseudo instructions used by the compiler,
// as well as Pat patterns used during instruction selection.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Pattern Matching Support

def GetLo32XForm : SDNodeXForm<imm, [{
  // Transformation function: get the low 32 bits.
  return getI32Imm((uint32_t)N->getZExtValue(), SDLoc(N));
}]>;

def GetLo8XForm : SDNodeXForm<imm, [{
  // Transformation function: get the low 8 bits.
  return getI8Imm((uint8_t)N->getZExtValue(), SDLoc(N));
}]>;


//===----------------------------------------------------------------------===//
// Random Pseudo Instructions.

// PIC base construction.  This expands to code that looks like this:
//     call  $next_inst
//     popl %destreg"
let hasSideEffects = 0, isNotDuplicable = 1, Uses = [ESP, SSP],
    SchedRW = [WriteJump] in
  def MOVPC32r : Ii32<0xE8, Pseudo, (outs GR32:$reg), (ins i32imm:$label),
                      "", []>;

// ADJCALLSTACKDOWN/UP implicitly use/def ESP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber EFLAGS.
let Defs = [ESP, EFLAGS, SSP], Uses = [ESP, SSP], SchedRW = [WriteALU] in {
def ADJCALLSTACKDOWN32 : I<0, Pseudo, (outs),
                           (ins i32imm:$amt1, i32imm:$amt2, i32imm:$amt3),
                           "#ADJCALLSTACKDOWN", []>, Requires<[NotLP64]>;
def ADJCALLSTACKUP32   : I<0, Pseudo, (outs), (ins i32imm:$amt1, i32imm:$amt2),
                           "#ADJCALLSTACKUP",
                           [(X86callseq_end timm:$amt1, timm:$amt2)]>,
                           Requires<[NotLP64]>;
}
def : Pat<(X86callseq_start timm:$amt1, timm:$amt2),
       (ADJCALLSTACKDOWN32 i32imm:$amt1, i32imm:$amt2, 0)>, Requires<[NotLP64]>;


// ADJCALLSTACKDOWN/UP implicitly use/def RSP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber EFLAGS.
let Defs = [RSP, EFLAGS, SSP], Uses = [RSP, SSP], SchedRW = [WriteALU] in {
def ADJCALLSTACKDOWN64 : I<0, Pseudo, (outs),
                           (ins i32imm:$amt1, i32imm:$amt2, i32imm:$amt3),
                           "#ADJCALLSTACKDOWN", []>, Requires<[IsLP64]>;
def ADJCALLSTACKUP64   : I<0, Pseudo, (outs), (ins i32imm:$amt1, i32imm:$amt2),
                           "#ADJCALLSTACKUP",
                           [(X86callseq_end timm:$amt1, timm:$amt2)]>,
                           Requires<[IsLP64]>;
}
def : Pat<(X86callseq_start timm:$amt1, timm:$amt2),
        (ADJCALLSTACKDOWN64 i32imm:$amt1, i32imm:$amt2, 0)>, Requires<[IsLP64]>;

let SchedRW = [WriteSystem] in {

// x86-64 va_start lowering magic.
let usesCustomInserter = 1, Defs = [EFLAGS] in {
def VASTART_SAVE_XMM_REGS : I<0, Pseudo,
                              (outs),
                              (ins GR8:$al,
                                   i64imm:$regsavefi, i64imm:$offset,
                                   variable_ops),
                              "#VASTART_SAVE_XMM_REGS $al, $regsavefi, $offset",
                              [(X86vastart_save_xmm_regs GR8:$al,
                                                         imm:$regsavefi,
                                                         imm:$offset),
                               (implicit EFLAGS)]>;

// The VAARG_64 pseudo-instruction takes the address of the va_list,
// and places the address of the next argument into a register.
let Defs = [EFLAGS] in
def VAARG_64 : I<0, Pseudo,
                 (outs GR64:$dst),
                 (ins i8mem:$ap, i32imm:$size, i8imm:$mode, i32imm:$align),
                 "#VAARG_64 $dst, $ap, $size, $mode, $align",
                 [(set GR64:$dst,
                    (X86vaarg64 addr:$ap, imm:$size, imm:$mode, imm:$align)),
                  (implicit EFLAGS)]>;


// When using segmented stacks these are lowered into instructions which first
// check if the current stacklet has enough free memory. If it does, memory is
// allocated by bumping the stack pointer. Otherwise memory is allocated from
// the heap.

let Defs = [EAX, ESP, EFLAGS], Uses = [ESP] in
def SEG_ALLOCA_32 : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$size),
                      "# variable sized alloca for segmented stacks",
                      [(set GR32:$dst,
                         (X86SegAlloca GR32:$size))]>,
                    Requires<[NotLP64]>;

let Defs = [RAX, RSP, EFLAGS], Uses = [RSP] in
def SEG_ALLOCA_64 : I<0, Pseudo, (outs GR64:$dst), (ins GR64:$size),
                      "# variable sized alloca for segmented stacks",
                      [(set GR64:$dst,
                         (X86SegAlloca GR64:$size))]>,
                    Requires<[In64BitMode]>;
}

// Dynamic stack allocation yields a _chkstk or _alloca call for all Windows
// targets.  These calls are needed to probe the stack when allocating more than
// 4k bytes in one go. Touching the stack at 4K increments is necessary to
// ensure that the guard pages used by the OS virtual memory manager are
// allocated in correct sequence.
// The main point of having separate instruction are extra unmodelled effects
// (compared to ordinary calls) like stack pointer change.

let Defs = [EAX, ESP, EFLAGS], Uses = [ESP] in
def WIN_ALLOCA_32 : I<0, Pseudo, (outs), (ins GR32:$size),
                     "# dynamic stack allocation",
                     [(X86WinAlloca GR32:$size)]>,
                     Requires<[NotLP64]>;

let Defs = [RAX, RSP, EFLAGS], Uses = [RSP] in
def WIN_ALLOCA_64 : I<0, Pseudo, (outs), (ins GR64:$size),
                     "# dynamic stack allocation",
                     [(X86WinAlloca GR64:$size)]>,
                     Requires<[In64BitMode]>;
} // SchedRW

// These instructions XOR the frame pointer into a GPR. They are used in some
// stack protection schemes. These are post-RA pseudos because we only know the
// frame register after register allocation.
let Constraints = "$src = $dst", isMoveImm = 1, isPseudo = 1, Defs = [EFLAGS] in {
  def XOR32_FP : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$src),
                  "xorl\t$$FP, $src", []>,
                  Requires<[NotLP64]>, Sched<[WriteALU]>;
  def XOR64_FP : I<0, Pseudo, (outs GR64:$dst), (ins GR64:$src),
                  "xorq\t$$FP $src", []>,
                  Requires<[In64BitMode]>, Sched<[WriteALU]>;
}

//===----------------------------------------------------------------------===//
// EH Pseudo Instructions
//
let SchedRW = [WriteSystem] in {
let isTerminator = 1, isReturn = 1, isBarrier = 1,
    hasCtrlDep = 1, isCodeGenOnly = 1 in {
def EH_RETURN   : I<0xC3, RawFrm, (outs), (ins GR32:$addr),
                    "ret\t#eh_return, addr: $addr",
                    [(X86ehret GR32:$addr)]>, Sched<[WriteJumpLd]>;

}

let isTerminator = 1, isReturn = 1, isBarrier = 1,
    hasCtrlDep = 1, isCodeGenOnly = 1 in {
def EH_RETURN64   : I<0xC3, RawFrm, (outs), (ins GR64:$addr),
                     "ret\t#eh_return, addr: $addr",
                     [(X86ehret GR64:$addr)]>, Sched<[WriteJumpLd]>;

}

let isTerminator = 1, hasSideEffects = 1, isBarrier = 1, hasCtrlDep = 1,
    isCodeGenOnly = 1, isReturn = 1, isEHScopeReturn = 1 in {
  def CLEANUPRET : I<0, Pseudo, (outs), (ins), "# CLEANUPRET", [(cleanupret)]>;

  // CATCHRET needs a custom inserter for SEH.
  let usesCustomInserter = 1 in
    def CATCHRET : I<0, Pseudo, (outs), (ins brtarget32:$dst, brtarget32:$from),
                     "# CATCHRET",
                     [(catchret bb:$dst, bb:$from)]>;
}

let hasSideEffects = 1, hasCtrlDep = 1, isCodeGenOnly = 1,
    usesCustomInserter = 1 in
def CATCHPAD : I<0, Pseudo, (outs), (ins), "# CATCHPAD", [(catchpad)]>;

// This instruction is responsible for re-establishing stack pointers after an
// exception has been caught and we are rejoining normal control flow in the
// parent function or funclet. It generally sets ESP and EBP, and optionally
// ESI. It is only needed for 32-bit WinEH, as the runtime restores CSRs for us
// elsewhere.
let hasSideEffects = 1, hasCtrlDep = 1, isCodeGenOnly = 1 in
def EH_RESTORE : I<0, Pseudo, (outs), (ins), "# EH_RESTORE", []>;

let hasSideEffects = 1, isBarrier = 1, isCodeGenOnly = 1,
    usesCustomInserter = 1 in {
  def EH_SjLj_SetJmp32  : I<0, Pseudo, (outs GR32:$dst), (ins i32mem:$buf),
                            "#EH_SJLJ_SETJMP32",
                            [(set GR32:$dst, (X86eh_sjlj_setjmp addr:$buf))]>,
                          Requires<[Not64BitMode]>;
  def EH_SjLj_SetJmp64  : I<0, Pseudo, (outs GR32:$dst), (ins i64mem:$buf),
                            "#EH_SJLJ_SETJMP64",
                            [(set GR32:$dst, (X86eh_sjlj_setjmp addr:$buf))]>,
                          Requires<[In64BitMode]>;
  let isTerminator = 1 in {
  def EH_SjLj_LongJmp32 : I<0, Pseudo, (outs), (ins i32mem:$buf),
                            "#EH_SJLJ_LONGJMP32",
                            [(X86eh_sjlj_longjmp addr:$buf)]>,
                          Requires<[Not64BitMode]>;
  def EH_SjLj_LongJmp64 : I<0, Pseudo, (outs), (ins i64mem:$buf),
                            "#EH_SJLJ_LONGJMP64",
                            [(X86eh_sjlj_longjmp addr:$buf)]>,
                          Requires<[In64BitMode]>;
  }
}

let isBranch = 1, isTerminator = 1, isCodeGenOnly = 1 in {
  def EH_SjLj_Setup : I<0, Pseudo, (outs), (ins brtarget:$dst),
                        "#EH_SjLj_Setup\t$dst", []>;
}
} // SchedRW

//===----------------------------------------------------------------------===//
// Pseudo instructions used by unwind info.
//
let isPseudo = 1, SchedRW = [WriteSystem] in {
  def SEH_PushReg : I<0, Pseudo, (outs), (ins i32imm:$reg),
                            "#SEH_PushReg $reg", []>;
  def SEH_SaveReg : I<0, Pseudo, (outs), (ins i32imm:$reg, i32imm:$dst),
                            "#SEH_SaveReg $reg, $dst", []>;
  def SEH_SaveXMM : I<0, Pseudo, (outs), (ins i32imm:$reg, i32imm:$dst),
                            "#SEH_SaveXMM $reg, $dst", []>;
  def SEH_StackAlloc : I<0, Pseudo, (outs), (ins i32imm:$size),
                            "#SEH_StackAlloc $size", []>;
  def SEH_StackAlign : I<0, Pseudo, (outs), (ins i32imm:$align),
                            "#SEH_StackAlign $align", []>;
  def SEH_SetFrame : I<0, Pseudo, (outs), (ins i32imm:$reg, i32imm:$offset),
                            "#SEH_SetFrame $reg, $offset", []>;
  def SEH_PushFrame : I<0, Pseudo, (outs), (ins i1imm:$mode),
                            "#SEH_PushFrame $mode", []>;
  def SEH_EndPrologue : I<0, Pseudo, (outs), (ins),
                            "#SEH_EndPrologue", []>;
  def SEH_Epilogue : I<0, Pseudo, (outs), (ins),
                            "#SEH_Epilogue", []>;
}

//===----------------------------------------------------------------------===//
// Pseudo instructions used by segmented stacks.
//

// This is lowered into a RET instruction by MCInstLower.  We need
// this so that we don't have to have a MachineBasicBlock which ends
// with a RET and also has successors.
let isPseudo = 1, SchedRW = [WriteJumpLd] in {
def MORESTACK_RET: I<0, Pseudo, (outs), (ins), "", []>;

// This instruction is lowered to a RET followed by a MOV.  The two
// instructions are not generated on a higher level since then the
// verifier sees a MachineBasicBlock ending with a non-terminator.
def MORESTACK_RET_RESTORE_R10 : I<0, Pseudo, (outs), (ins), "", []>;
}

//===----------------------------------------------------------------------===//
// Alias Instructions
//===----------------------------------------------------------------------===//

// Alias instruction mapping movr0 to xor.
// FIXME: remove when we can teach regalloc that xor reg, reg is ok.
let Defs = [EFLAGS], isReMaterializable = 1, isAsCheapAsAMove = 1,
    isPseudo = 1, isMoveImm = 1, AddedComplexity = 10 in
def MOV32r0  : I<0, Pseudo, (outs GR32:$dst), (ins), "",
                 [(set GR32:$dst, 0)]>, Sched<[WriteZero]>;

// Other widths can also make use of the 32-bit xor, which may have a smaller
// encoding and avoid partial register updates.
let AddedComplexity = 10 in {
def : Pat<(i8 0), (EXTRACT_SUBREG (MOV32r0), sub_8bit)>;
def : Pat<(i16 0), (EXTRACT_SUBREG (MOV32r0), sub_16bit)>;
def : Pat<(i64 0), (SUBREG_TO_REG (i64 0), (MOV32r0), sub_32bit)>;
}

let Predicates = [OptForSize, Not64BitMode],
    AddedComplexity = 10 in {
  let SchedRW = [WriteALU] in {
  // Pseudo instructions for materializing 1 and -1 using XOR+INC/DEC,
  // which only require 3 bytes compared to MOV32ri which requires 5.
  let Defs = [EFLAGS], isReMaterializable = 1, isPseudo = 1 in {
    def MOV32r1 : I<0, Pseudo, (outs GR32:$dst), (ins), "",
                        [(set GR32:$dst, 1)]>;
    def MOV32r_1 : I<0, Pseudo, (outs GR32:$dst), (ins), "",
                        [(set GR32:$dst, -1)]>;
  }
  } // SchedRW

  // MOV16ri is 4 bytes, so the instructions above are smaller.
  def : Pat<(i16 1), (EXTRACT_SUBREG (MOV32r1), sub_16bit)>;
  def : Pat<(i16 -1), (EXTRACT_SUBREG (MOV32r_1), sub_16bit)>;
}

let isReMaterializable = 1, isPseudo = 1, AddedComplexity = 5,
    SchedRW = [WriteALU] in {
// AddedComplexity higher than MOV64ri but lower than MOV32r0 and MOV32r1.
def MOV32ImmSExti8 : I<0, Pseudo, (outs GR32:$dst), (ins i32i8imm:$src), "",
                       [(set GR32:$dst, i32immSExt8:$src)]>,
                       Requires<[OptForMinSize, NotWin64WithoutFP]>;
def MOV64ImmSExti8 : I<0, Pseudo, (outs GR64:$dst), (ins i64i8imm:$src), "",
                       [(set GR64:$dst, i64immSExt8:$src)]>,
                       Requires<[OptForMinSize, NotWin64WithoutFP]>;
}

// Materialize i64 constant where top 32-bits are zero. This could theoretically
// use MOV32ri with a SUBREG_TO_REG to represent the zero-extension, however
// that would make it more difficult to rematerialize.
let isReMaterializable = 1, isAsCheapAsAMove = 1,
    isPseudo = 1, hasSideEffects = 0, SchedRW = [WriteMove] in
def MOV32ri64 : I<0, Pseudo, (outs GR64:$dst), (ins i64i32imm:$src), "", []>;

// This 64-bit pseudo-move can be used for both a 64-bit constant that is
// actually the zero-extension of a 32-bit constant and for labels in the
// x86-64 small code model.
def mov64imm32 : ComplexPattern<i64, 1, "selectMOV64Imm32", [imm, X86Wrapper]>;

def : Pat<(i64 mov64imm32:$src), (MOV32ri64 mov64imm32:$src)>;

// Use sbb to materialize carry bit.
let Uses = [EFLAGS], Defs = [EFLAGS], isPseudo = 1, SchedRW = [WriteALU] in {
// FIXME: These are pseudo ops that should be replaced with Pat<> patterns.
// However, Pat<> can't replicate the destination reg into the inputs of the
// result.
def SETB_C8r : I<0, Pseudo, (outs GR8:$dst), (ins), "",
                 [(set GR8:$dst, (X86setcc_c X86_COND_B, EFLAGS))]>;
def SETB_C16r : I<0, Pseudo, (outs GR16:$dst), (ins), "",
                 [(set GR16:$dst, (X86setcc_c X86_COND_B, EFLAGS))]>;
def SETB_C32r : I<0, Pseudo, (outs GR32:$dst), (ins), "",
                 [(set GR32:$dst, (X86setcc_c X86_COND_B, EFLAGS))]>;
def SETB_C64r : I<0, Pseudo, (outs GR64:$dst), (ins), "",
                 [(set GR64:$dst, (X86setcc_c X86_COND_B, EFLAGS))]>;
} // isCodeGenOnly


def : Pat<(i16 (anyext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C16r)>;
def : Pat<(i32 (anyext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C32r)>;
def : Pat<(i64 (anyext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C64r)>;

def : Pat<(i16 (sext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C16r)>;
def : Pat<(i32 (sext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C32r)>;
def : Pat<(i64 (sext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C64r)>;

// We canonicalize 'setb' to "(and (sbb reg,reg), 1)" on the hope that the and
// will be eliminated and that the sbb can be extended up to a wider type.  When
// this happens, it is great.  However, if we are left with an 8-bit sbb and an
// and, we might as well just match it as a setb.
def : Pat<(and (i8 (X86setcc_c X86_COND_B, EFLAGS)), 1),
          (SETBr)>;

// Patterns to give priority when both inputs are zero so that we don't use
// an immediate for the RHS.
// TODO: Should we use a 32-bit sbb for 8/16 to push the extract_subreg out?
def : Pat<(X86sbb_flag (i8 0), (i8 0), EFLAGS),
          (SBB8rr (EXTRACT_SUBREG (MOV32r0), sub_8bit),
                  (EXTRACT_SUBREG (MOV32r0), sub_8bit))>;
def : Pat<(X86sbb_flag (i16 0), (i16 0), EFLAGS),
          (SBB16rr (EXTRACT_SUBREG (MOV32r0), sub_16bit),
                   (EXTRACT_SUBREG (MOV32r0), sub_16bit))>;
def : Pat<(X86sbb_flag (i32 0), (i32 0), EFLAGS),
          (SBB32rr (MOV32r0), (MOV32r0))>;
def : Pat<(X86sbb_flag (i64 0), (i64 0), EFLAGS),
          (SBB64rr (SUBREG_TO_REG (i64 0), (MOV32r0), sub_32bit),
                   (SUBREG_TO_REG (i64 0), (MOV32r0), sub_32bit))>;

//===----------------------------------------------------------------------===//
// String Pseudo Instructions
//
let SchedRW = [WriteMicrocoded] in {
let Defs = [ECX,EDI,ESI], Uses = [ECX,EDI,ESI], isCodeGenOnly = 1 in {
def REP_MOVSB_32 : I<0xA4, RawFrm, (outs), (ins),
                    "{rep;movsb (%esi), %es:(%edi)|rep movsb es:[edi], [esi]}",
                    [(X86rep_movs i8)]>, REP, AdSize32,
                   Requires<[NotLP64]>;
def REP_MOVSW_32 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsw (%esi), %es:(%edi)|rep movsw es:[edi], [esi]}",
                    [(X86rep_movs i16)]>, REP, AdSize32, OpSize16,
                   Requires<[NotLP64]>;
def REP_MOVSD_32 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsl (%esi), %es:(%edi)|rep movsd es:[edi], [esi]}",
                    [(X86rep_movs i32)]>, REP, AdSize32, OpSize32,
                   Requires<[NotLP64]>;
def REP_MOVSQ_32 : RI<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsq (%esi), %es:(%edi)|rep movsq es:[edi], [esi]}",
                    [(X86rep_movs i64)]>, REP, AdSize32,
                   Requires<[NotLP64, In64BitMode]>;
}

let Defs = [RCX,RDI,RSI], Uses = [RCX,RDI,RSI], isCodeGenOnly = 1 in {
def REP_MOVSB_64 : I<0xA4, RawFrm, (outs), (ins),
                    "{rep;movsb (%rsi), %es:(%rdi)|rep movsb es:[rdi], [rsi]}",
                    [(X86rep_movs i8)]>, REP, AdSize64,
                   Requires<[IsLP64]>;
def REP_MOVSW_64 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsw (%rsi), %es:(%rdi)|rep movsw es:[rdi], [rsi]}",
                    [(X86rep_movs i16)]>, REP, AdSize64, OpSize16,
                   Requires<[IsLP64]>;
def REP_MOVSD_64 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsl (%rsi), %es:(%rdi)|rep movsdi es:[rdi], [rsi]}",
                    [(X86rep_movs i32)]>, REP, AdSize64, OpSize32,
                   Requires<[IsLP64]>;
def REP_MOVSQ_64 : RI<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsq (%rsi), %es:(%rdi)|rep movsq es:[rdi], [rsi]}",
                    [(X86rep_movs i64)]>, REP, AdSize64,
                   Requires<[IsLP64]>;
}

// FIXME: Should use "(X86rep_stos AL)" as the pattern.
let Defs = [ECX,EDI], isCodeGenOnly = 1 in {
  let Uses = [AL,ECX,EDI] in
  def REP_STOSB_32 : I<0xAA, RawFrm, (outs), (ins),
                       "{rep;stosb %al, %es:(%edi)|rep stosb es:[edi], al}",
                      [(X86rep_stos i8)]>, REP, AdSize32,
                     Requires<[NotLP64]>;
  let Uses = [AX,ECX,EDI] in
  def REP_STOSW_32 : I<0xAB, RawFrm, (outs), (ins),
                      "{rep;stosw %ax, %es:(%edi)|rep stosw es:[edi], ax}",
                      [(X86rep_stos i16)]>, REP, AdSize32, OpSize16,
                     Requires<[NotLP64]>;
  let Uses = [EAX,ECX,EDI] in
  def REP_STOSD_32 : I<0xAB, RawFrm, (outs), (ins),
                      "{rep;stosl %eax, %es:(%edi)|rep stosd es:[edi], eax}",
                      [(X86rep_stos i32)]>, REP, AdSize32, OpSize32,
                     Requires<[NotLP64]>;
  let Uses = [RAX,RCX,RDI] in
  def REP_STOSQ_32 : RI<0xAB, RawFrm, (outs), (ins),
                        "{rep;stosq %rax, %es:(%edi)|rep stosq es:[edi], rax}",
                        [(X86rep_stos i64)]>, REP, AdSize32,
                        Requires<[NotLP64, In64BitMode]>;
}

let Defs = [RCX,RDI], isCodeGenOnly = 1 in {
  let Uses = [AL,RCX,RDI] in
  def REP_STOSB_64 : I<0xAA, RawFrm, (outs), (ins),
                       "{rep;stosb %al, %es:(%rdi)|rep stosb es:[rdi], al}",
                       [(X86rep_stos i8)]>, REP, AdSize64,
                       Requires<[IsLP64]>;
  let Uses = [AX,RCX,RDI] in
  def REP_STOSW_64 : I<0xAB, RawFrm, (outs), (ins),
                       "{rep;stosw %ax, %es:(%rdi)|rep stosw es:[rdi], ax}",
                       [(X86rep_stos i16)]>, REP, AdSize64, OpSize16,
                       Requires<[IsLP64]>;
  let Uses = [RAX,RCX,RDI] in
  def REP_STOSD_64 : I<0xAB, RawFrm, (outs), (ins),
                      "{rep;stosl %eax, %es:(%rdi)|rep stosd es:[rdi], eax}",
                       [(X86rep_stos i32)]>, REP, AdSize64, OpSize32,
                       Requires<[IsLP64]>;

  let Uses = [RAX,RCX,RDI] in
  def REP_STOSQ_64 : RI<0xAB, RawFrm, (outs), (ins),
                        "{rep;stosq %rax, %es:(%rdi)|rep stosq es:[rdi], rax}",
                        [(X86rep_stos i64)]>, REP, AdSize64,
                        Requires<[IsLP64]>;
}
} // SchedRW

//===----------------------------------------------------------------------===//
// Thread Local Storage Instructions
//
let SchedRW = [WriteSystem] in {

// ELF TLS Support
// All calls clobber the non-callee saved registers. ESP is marked as
// a use to prevent stack-pointer assignments that appear immediately
// before calls from potentially appearing dead.
let Defs = [EAX, ECX, EDX, FP0, FP1, FP2, FP3, FP4, FP5, FP6, FP7,
            ST0, ST1, ST2, ST3, ST4, ST5, ST6, ST7,
            MM0, MM1, MM2, MM3, MM4, MM5, MM6, MM7,
            XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
            XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, EFLAGS, DF],
    usesCustomInserter = 1, Uses = [ESP, SSP] in {
def TLS_addr32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                  "# TLS_addr32",
                  [(X86tlsaddr tls32addr:$sym)]>,
                  Requires<[Not64BitMode]>;
def TLS_base_addr32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                  "# TLS_base_addr32",
                  [(X86tlsbaseaddr tls32baseaddr:$sym)]>,
                  Requires<[Not64BitMode]>;
}

// All calls clobber the non-callee saved registers. RSP is marked as
// a use to prevent stack-pointer assignments that appear immediately
// before calls from potentially appearing dead.
let Defs = [RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11,
            FP0, FP1, FP2, FP3, FP4, FP5, FP6, FP7,
            ST0, ST1, ST2, ST3, ST4, ST5, ST6, ST7,
            MM0, MM1, MM2, MM3, MM4, MM5, MM6, MM7,
            XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
            XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, EFLAGS, DF],
    usesCustomInserter = 1, Uses = [RSP, SSP] in {
def TLS_addr64 : I<0, Pseudo, (outs), (ins i64mem:$sym),
                   "# TLS_addr64",
                  [(X86tlsaddr tls64addr:$sym)]>,
                  Requires<[In64BitMode]>;
def TLS_base_addr64 : I<0, Pseudo, (outs), (ins i64mem:$sym),
                   "# TLS_base_addr64",
                  [(X86tlsbaseaddr tls64baseaddr:$sym)]>,
                  Requires<[In64BitMode]>;
}

// Darwin TLS Support
// For i386, the address of the thunk is passed on the stack, on return the
// address of the variable is in %eax.  %ecx is trashed during the function
// call.  All other registers are preserved.
let Defs = [EAX, ECX, EFLAGS, DF],
    Uses = [ESP, SSP],
    usesCustomInserter = 1 in
def TLSCall_32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                "# TLSCall_32",
                [(X86TLSCall addr:$sym)]>,
                Requires<[Not64BitMode]>;

// For x86_64, the address of the thunk is passed in %rdi, but the
// pseudo directly use the symbol, so do not add an implicit use of
// %rdi. The lowering will do the right thing with RDI.
// On return the address of the variable is in %rax.  All other
// registers are preserved.
let Defs = [RAX, EFLAGS, DF],
    Uses = [RSP, SSP],
    usesCustomInserter = 1 in
def TLSCall_64 : I<0, Pseudo, (outs), (ins i64mem:$sym),
                  "# TLSCall_64",
                  [(X86TLSCall addr:$sym)]>,
                  Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Conditional Move Pseudo Instructions

// CMOV* - Used to implement the SELECT DAG operation.  Expanded after
// instruction selection into a branch sequence.
multiclass CMOVrr_PSEUDO<RegisterClass RC, ValueType VT> {
  def CMOV#NAME  : I<0, Pseudo,
                    (outs RC:$dst), (ins RC:$t, RC:$f, i8imm:$cond),
                    "#CMOV_"#NAME#" PSEUDO!",
                    [(set RC:$dst, (VT (X86cmov RC:$t, RC:$f, imm:$cond,
                                                EFLAGS)))]>;
}

let usesCustomInserter = 1, hasNoSchedulingInfo = 1, Uses = [EFLAGS] in {
  // X86 doesn't have 8-bit conditional moves. Use a customInserter to
  // emit control flow. An alternative to this is to mark i8 SELECT as Promote,
  // however that requires promoting the operands, and can induce additional
  // i8 register pressure.
  defm _GR8 : CMOVrr_PSEUDO<GR8, i8>;

  let Predicates = [NoCMov] in {
    defm _GR32 : CMOVrr_PSEUDO<GR32, i32>;
    defm _GR16 : CMOVrr_PSEUDO<GR16, i16>;
  } // Predicates = [NoCMov]

  // fcmov doesn't handle all possible EFLAGS, provide a fallback if there is no
  // SSE1/SSE2.
  let Predicates = [FPStackf32] in
    defm _RFP32 : CMOVrr_PSEUDO<RFP32, f32>;

  let Predicates = [FPStackf64] in
    defm _RFP64 : CMOVrr_PSEUDO<RFP64, f64>;

  defm _RFP80 : CMOVrr_PSEUDO<RFP80, f80>;

  defm _FR32   : CMOVrr_PSEUDO<FR32, f32>;
  defm _FR64   : CMOVrr_PSEUDO<FR64, f64>;
  let Predicates = [NoVLX] in {
    defm _VR128  : CMOVrr_PSEUDO<VR128, v2i64>;
    defm _VR256  : CMOVrr_PSEUDO<VR256, v4i64>;
  }
  let Predicates = [HasVLX] in {
    defm _VR128X : CMOVrr_PSEUDO<VR128X, v2i64>;
    defm _VR256X : CMOVrr_PSEUDO<VR256X, v4i64>;
  }
  defm _VR512  : CMOVrr_PSEUDO<VR512, v8i64>;
  defm _VK2    : CMOVrr_PSEUDO<VK2,  v2i1>;
  defm _VK4    : CMOVrr_PSEUDO<VK4,  v4i1>;
  defm _VK8    : CMOVrr_PSEUDO<VK8,  v8i1>;
  defm _VK16   : CMOVrr_PSEUDO<VK16, v16i1>;
  defm _VK32   : CMOVrr_PSEUDO<VK32, v32i1>;
  defm _VK64   : CMOVrr_PSEUDO<VK64, v64i1>;
} // usesCustomInserter = 1, hasNoSchedulingInfo = 1, Uses = [EFLAGS]

def : Pat<(f128 (X86cmov VR128:$t, VR128:$f, imm:$cond, EFLAGS)),
          (CMOV_VR128 VR128:$t, VR128:$f, imm:$cond)>;

let Predicates = [NoVLX] in {
  def : Pat<(v16i8 (X86cmov VR128:$t, VR128:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, imm:$cond)>;
  def : Pat<(v8i16 (X86cmov VR128:$t, VR128:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, imm:$cond)>;
  def : Pat<(v4i32 (X86cmov VR128:$t, VR128:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, imm:$cond)>;
  def : Pat<(v4f32 (X86cmov VR128:$t, VR128:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, imm:$cond)>;
  def : Pat<(v2f64 (X86cmov VR128:$t, VR128:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, imm:$cond)>;

  def : Pat<(v32i8 (X86cmov VR256:$t, VR256:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, imm:$cond)>;
  def : Pat<(v16i16 (X86cmov VR256:$t, VR256:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, imm:$cond)>;
  def : Pat<(v8i32 (X86cmov VR256:$t, VR256:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, imm:$cond)>;
  def : Pat<(v8f32 (X86cmov VR256:$t, VR256:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, imm:$cond)>;
  def : Pat<(v4f64 (X86cmov VR256:$t, VR256:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, imm:$cond)>;
}
let Predicates = [HasVLX] in {
  def : Pat<(v16i8 (X86cmov VR128X:$t, VR128X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, imm:$cond)>;
  def : Pat<(v8i16 (X86cmov VR128X:$t, VR128X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, imm:$cond)>;
  def : Pat<(v4i32 (X86cmov VR128X:$t, VR128X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, imm:$cond)>;
  def : Pat<(v4f32 (X86cmov VR128X:$t, VR128X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, imm:$cond)>;
  def : Pat<(v2f64 (X86cmov VR128X:$t, VR128X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, imm:$cond)>;

  def : Pat<(v32i8 (X86cmov VR256X:$t, VR256X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, imm:$cond)>;
  def : Pat<(v16i16 (X86cmov VR256X:$t, VR256X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, imm:$cond)>;
  def : Pat<(v8i32 (X86cmov VR256X:$t, VR256X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, imm:$cond)>;
  def : Pat<(v8f32 (X86cmov VR256X:$t, VR256X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, imm:$cond)>;
  def : Pat<(v4f64 (X86cmov VR256X:$t, VR256X:$f, imm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, imm:$cond)>;
}

def : Pat<(v64i8 (X86cmov VR512:$t, VR512:$f, imm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, imm:$cond)>;
def : Pat<(v32i16 (X86cmov VR512:$t, VR512:$f, imm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, imm:$cond)>;
def : Pat<(v16i32 (X86cmov VR512:$t, VR512:$f, imm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, imm:$cond)>;
def : Pat<(v16f32 (X86cmov VR512:$t, VR512:$f, imm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, imm:$cond)>;
def : Pat<(v8f64 (X86cmov VR512:$t, VR512:$f, imm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, imm:$cond)>;

//===----------------------------------------------------------------------===//
// Normal-Instructions-With-Lock-Prefix Pseudo Instructions
//===----------------------------------------------------------------------===//

// FIXME: Use normal instructions and add lock prefix dynamically.

// Memory barriers

let isCodeGenOnly = 1, Defs = [EFLAGS] in
def OR32mi8Locked  : Ii8<0x83, MRM1m, (outs), (ins i32mem:$dst, i32i8imm:$zero),
                         "or{l}\t{$zero, $dst|$dst, $zero}", []>,
                         Requires<[Not64BitMode]>, OpSize32, LOCK,
                         Sched<[WriteALURMW]>;

let hasSideEffects = 1 in
def Int_MemBarrier : I<0, Pseudo, (outs), (ins),
                     "#MEMBARRIER",
                     [(X86MemBarrier)]>, Sched<[WriteLoad]>;

// RegOpc corresponds to the mr version of the instruction
// ImmOpc corresponds to the mi version of the instruction
// ImmOpc8 corresponds to the mi8 version of the instruction
// ImmMod corresponds to the instruction format of the mi and mi8 versions
multiclass LOCK_ArithBinOp<bits<8> RegOpc, bits<8> ImmOpc, bits<8> ImmOpc8,
                           Format ImmMod, SDNode Op, string mnemonic> {
let Defs = [EFLAGS], mayLoad = 1, mayStore = 1, isCodeGenOnly = 1,
    SchedRW = [WriteALURMW] in {

def NAME#8mr : I<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                  RegOpc{3}, RegOpc{2}, RegOpc{1}, 0 },
                  MRMDestMem, (outs), (ins i8mem:$dst, GR8:$src2),
                  !strconcat(mnemonic, "{b}\t",
                             "{$src2, $dst|$dst, $src2}"),
                  [(set EFLAGS, (Op addr:$dst, GR8:$src2))]>, LOCK;

def NAME#16mr : I<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                   RegOpc{3}, RegOpc{2}, RegOpc{1}, 1 },
                   MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src2),
                   !strconcat(mnemonic, "{w}\t",
                              "{$src2, $dst|$dst, $src2}"),
                   [(set EFLAGS, (Op addr:$dst, GR16:$src2))]>,
                   OpSize16, LOCK;

def NAME#32mr : I<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                   RegOpc{3}, RegOpc{2}, RegOpc{1}, 1 },
                   MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src2),
                   !strconcat(mnemonic, "{l}\t",
                              "{$src2, $dst|$dst, $src2}"),
                   [(set EFLAGS, (Op addr:$dst, GR32:$src2))]>,
                   OpSize32, LOCK;

def NAME#64mr : RI<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                    RegOpc{3}, RegOpc{2}, RegOpc{1}, 1 },
                    MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src2),
                    !strconcat(mnemonic, "{q}\t",
                               "{$src2, $dst|$dst, $src2}"),
                    [(set EFLAGS, (Op addr:$dst, GR64:$src2))]>, LOCK;

def NAME#8mi : Ii8<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                    ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 0 },
                    ImmMod, (outs), (ins i8mem :$dst, i8imm :$src2),
                    !strconcat(mnemonic, "{b}\t",
                               "{$src2, $dst|$dst, $src2}"),
                    [(set EFLAGS, (Op addr:$dst, (i8 imm:$src2)))]>, LOCK;

def NAME#16mi : Ii16<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                      ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 1 },
                      ImmMod, (outs), (ins i16mem :$dst, i16imm :$src2),
                      !strconcat(mnemonic, "{w}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, (i16 imm:$src2)))]>,
                      OpSize16, LOCK;

def NAME#32mi : Ii32<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                      ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 1 },
                      ImmMod, (outs), (ins i32mem :$dst, i32imm :$src2),
                      !strconcat(mnemonic, "{l}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, (i32 imm:$src2)))]>,
                      OpSize32, LOCK;

def NAME#64mi32 : RIi32S<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                          ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 1 },
                          ImmMod, (outs), (ins i64mem :$dst, i64i32imm :$src2),
                          !strconcat(mnemonic, "{q}\t",
                                     "{$src2, $dst|$dst, $src2}"),
                          [(set EFLAGS, (Op addr:$dst, i64immSExt32:$src2))]>,
                          LOCK;

def NAME#16mi8 : Ii8<{ImmOpc8{7}, ImmOpc8{6}, ImmOpc8{5}, ImmOpc8{4},
                      ImmOpc8{3}, ImmOpc8{2}, ImmOpc8{1}, 1 },
                      ImmMod, (outs), (ins i16mem :$dst, i16i8imm :$src2),
                      !strconcat(mnemonic, "{w}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, i16immSExt8:$src2))]>,
                      OpSize16, LOCK;

def NAME#32mi8 : Ii8<{ImmOpc8{7}, ImmOpc8{6}, ImmOpc8{5}, ImmOpc8{4},
                      ImmOpc8{3}, ImmOpc8{2}, ImmOpc8{1}, 1 },
                      ImmMod, (outs), (ins i32mem :$dst, i32i8imm :$src2),
                      !strconcat(mnemonic, "{l}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, i32immSExt8:$src2))]>,
                      OpSize32, LOCK;

def NAME#64mi8 : RIi8<{ImmOpc8{7}, ImmOpc8{6}, ImmOpc8{5}, ImmOpc8{4},
                       ImmOpc8{3}, ImmOpc8{2}, ImmOpc8{1}, 1 },
                       ImmMod, (outs), (ins i64mem :$dst, i64i8imm :$src2),
                       !strconcat(mnemonic, "{q}\t",
                                  "{$src2, $dst|$dst, $src2}"),
                       [(set EFLAGS, (Op addr:$dst, i64immSExt8:$src2))]>,
                       LOCK;
}

}

defm LOCK_ADD : LOCK_ArithBinOp<0x00, 0x80, 0x83, MRM0m, X86lock_add, "add">;
defm LOCK_SUB : LOCK_ArithBinOp<0x28, 0x80, 0x83, MRM5m, X86lock_sub, "sub">;
defm LOCK_OR  : LOCK_ArithBinOp<0x08, 0x80, 0x83, MRM1m, X86lock_or , "or">;
defm LOCK_AND : LOCK_ArithBinOp<0x20, 0x80, 0x83, MRM4m, X86lock_and, "and">;
defm LOCK_XOR : LOCK_ArithBinOp<0x30, 0x80, 0x83, MRM6m, X86lock_xor, "xor">;

def X86lock_add_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86lock_add node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 0));
}]>;

def X86lock_sub_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86lock_sub node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 0));
}]>;

let Predicates = [UseIncDec] in {
  let Defs = [EFLAGS], mayLoad = 1, mayStore = 1, isCodeGenOnly = 1,
      SchedRW = [WriteALURMW]  in {
    def LOCK_INC8m  : I<0xFE, MRM0m, (outs), (ins i8mem :$dst),
                        "inc{b}\t$dst",
                        [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i8 1)))]>,
                        LOCK;
    def LOCK_INC16m : I<0xFF, MRM0m, (outs), (ins i16mem:$dst),
                        "inc{w}\t$dst",
                        [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i16 1)))]>,
                        OpSize16, LOCK;
    def LOCK_INC32m : I<0xFF, MRM0m, (outs), (ins i32mem:$dst),
                        "inc{l}\t$dst",
                        [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i32 1)))]>,
                        OpSize32, LOCK;
    def LOCK_INC64m : RI<0xFF, MRM0m, (outs), (ins i64mem:$dst),
                         "inc{q}\t$dst",
                         [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i64 1)))]>,
                         LOCK;

    def LOCK_DEC8m  : I<0xFE, MRM1m, (outs), (ins i8mem :$dst),
                        "dec{b}\t$dst",
                        [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i8 1)))]>,
                        LOCK;
    def LOCK_DEC16m : I<0xFF, MRM1m, (outs), (ins i16mem:$dst),
                        "dec{w}\t$dst",
                        [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i16 1)))]>,
                        OpSize16, LOCK;
    def LOCK_DEC32m : I<0xFF, MRM1m, (outs), (ins i32mem:$dst),
                        "dec{l}\t$dst",
                        [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i32 1)))]>,
                        OpSize32, LOCK;
    def LOCK_DEC64m : RI<0xFF, MRM1m, (outs), (ins i64mem:$dst),
                         "dec{q}\t$dst",
                         [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i64 1)))]>,
                         LOCK;
  }

  // Additional patterns for -1 constant.
  def : Pat<(X86lock_add addr:$dst, (i8  -1)), (LOCK_DEC8m  addr:$dst)>;
  def : Pat<(X86lock_add addr:$dst, (i16 -1)), (LOCK_DEC16m addr:$dst)>;
  def : Pat<(X86lock_add addr:$dst, (i32 -1)), (LOCK_DEC32m addr:$dst)>;
  def : Pat<(X86lock_add addr:$dst, (i64 -1)), (LOCK_DEC64m addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i8  -1)), (LOCK_INC8m  addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i16 -1)), (LOCK_INC16m addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i32 -1)), (LOCK_INC32m addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i64 -1)), (LOCK_INC64m addr:$dst)>;
}

// Atomic compare and swap.
multiclass LCMPXCHG_UnOp<bits<8> Opc, Format Form, string mnemonic,
                         SDPatternOperator frag, X86MemOperand x86memop> {
let isCodeGenOnly = 1, usesCustomInserter = 1 in {
  def NAME : I<Opc, Form, (outs), (ins x86memop:$ptr),
               !strconcat(mnemonic, "\t$ptr"),
               [(frag addr:$ptr)]>, TB, LOCK;
}
}

multiclass LCMPXCHG_BinOp<bits<8> Opc8, bits<8> Opc, Format Form,
                          string mnemonic, SDPatternOperator frag> {
let isCodeGenOnly = 1, SchedRW = [WriteCMPXCHGRMW] in {
  let Defs = [AL, EFLAGS], Uses = [AL] in
  def NAME#8  : I<Opc8, Form, (outs), (ins i8mem:$ptr, GR8:$swap),
                  !strconcat(mnemonic, "{b}\t{$swap, $ptr|$ptr, $swap}"),
                  [(frag addr:$ptr, GR8:$swap, 1)]>, TB, LOCK;
  let Defs = [AX, EFLAGS], Uses = [AX] in
  def NAME#16 : I<Opc, Form, (outs), (ins i16mem:$ptr, GR16:$swap),
                  !strconcat(mnemonic, "{w}\t{$swap, $ptr|$ptr, $swap}"),
                  [(frag addr:$ptr, GR16:$swap, 2)]>, TB, OpSize16, LOCK;
  let Defs = [EAX, EFLAGS], Uses = [EAX] in
  def NAME#32 : I<Opc, Form, (outs), (ins i32mem:$ptr, GR32:$swap),
                  !strconcat(mnemonic, "{l}\t{$swap, $ptr|$ptr, $swap}"),
                  [(frag addr:$ptr, GR32:$swap, 4)]>, TB, OpSize32, LOCK;
  let Defs = [RAX, EFLAGS], Uses = [RAX] in
  def NAME#64 : RI<Opc, Form, (outs), (ins i64mem:$ptr, GR64:$swap),
                   !strconcat(mnemonic, "{q}\t{$swap, $ptr|$ptr, $swap}"),
                   [(frag addr:$ptr, GR64:$swap, 8)]>, TB, LOCK;
}
}

let Defs = [EAX, EDX, EFLAGS], Uses = [EAX, EBX, ECX, EDX],
    SchedRW = [WriteCMPXCHGRMW] in {
defm LCMPXCHG8B : LCMPXCHG_UnOp<0xC7, MRM1m, "cmpxchg8b", X86cas8, i64mem>;
}

// This pseudo must be used when the frame uses RBX as
// the base pointer. Indeed, in such situation RBX is a reserved
// register and the register allocator will ignore any use/def of
// it. In other words, the register will not fix the clobbering of
// RBX that will happen when setting the arguments for the instrucion.
//
// Unlike the actual related instuction, we mark that this one
// defines EBX (instead of using EBX).
// The rationale is that we will define RBX during the expansion of
// the pseudo. The argument feeding EBX is ebx_input.
//
// The additional argument, $ebx_save, is a temporary register used to
// save the value of RBX across the actual instruction.
//
// To make sure the register assigned to $ebx_save does not interfere with
// the definition of the actual instruction, we use a definition $dst which
// is tied to $rbx_save. That way, the live-range of $rbx_save spans across
// the instruction and we are sure we will have a valid register to restore
// the value of RBX.
let Defs = [EAX, EDX, EBX, EFLAGS], Uses = [EAX, ECX, EDX],
    SchedRW = [WriteCMPXCHGRMW], isCodeGenOnly = 1, isPseudo = 1,
    Constraints = "$ebx_save = $dst", usesCustomInserter = 1 in {
def LCMPXCHG8B_SAVE_EBX :
    I<0, Pseudo, (outs GR32:$dst),
      (ins i64mem:$ptr, GR32:$ebx_input, GR32:$ebx_save),
      !strconcat("cmpxchg8b", "\t$ptr"),
      [(set GR32:$dst, (X86cas8save_ebx addr:$ptr, GR32:$ebx_input,
                                        GR32:$ebx_save))]>;
}


let Defs = [RAX, RDX, EFLAGS], Uses = [RAX, RBX, RCX, RDX],
    Predicates = [HasCmpxchg16b], SchedRW = [WriteCMPXCHGRMW] in {
defm LCMPXCHG16B : LCMPXCHG_UnOp<0xC7, MRM1m, "cmpxchg16b",
                                 X86cas16, i128mem>, REX_W;
}

// Same as LCMPXCHG8B_SAVE_RBX but for the 16 Bytes variant.
let Defs = [RAX, RDX, RBX, EFLAGS], Uses = [RAX, RCX, RDX],
    Predicates = [HasCmpxchg16b], SchedRW = [WriteCMPXCHGRMW],
    isCodeGenOnly = 1, isPseudo = 1, Constraints = "$rbx_save = $dst",
    usesCustomInserter = 1 in {
def LCMPXCHG16B_SAVE_RBX :
    I<0, Pseudo, (outs GR64:$dst),
      (ins i128mem:$ptr, GR64:$rbx_input, GR64:$rbx_save),
      !strconcat("cmpxchg16b", "\t$ptr"),
      [(set GR64:$dst, (X86cas16save_rbx addr:$ptr, GR64:$rbx_input,
                                                    GR64:$rbx_save))]>;
}

defm LCMPXCHG : LCMPXCHG_BinOp<0xB0, 0xB1, MRMDestMem, "cmpxchg", X86cas>;

// Atomic exchange and add
multiclass ATOMIC_LOAD_BINOP<bits<8> opc8, bits<8> opc, string mnemonic,
                             string frag> {
  let Constraints = "$val = $dst", Defs = [EFLAGS], isCodeGenOnly = 1,
      SchedRW = [WriteALURMW] in {
    def NAME#8  : I<opc8, MRMSrcMem, (outs GR8:$dst),
                    (ins GR8:$val, i8mem:$ptr),
                    !strconcat(mnemonic, "{b}\t{$val, $ptr|$ptr, $val}"),
                    [(set GR8:$dst,
                          (!cast<PatFrag>(frag # "_8") addr:$ptr, GR8:$val))]>;
    def NAME#16 : I<opc, MRMSrcMem, (outs GR16:$dst),
                    (ins GR16:$val, i16mem:$ptr),
                    !strconcat(mnemonic, "{w}\t{$val, $ptr|$ptr, $val}"),
                    [(set
                       GR16:$dst,
                       (!cast<PatFrag>(frag # "_16") addr:$ptr, GR16:$val))]>,
                    OpSize16;
    def NAME#32 : I<opc, MRMSrcMem, (outs GR32:$dst),
                    (ins GR32:$val, i32mem:$ptr),
                    !strconcat(mnemonic, "{l}\t{$val, $ptr|$ptr, $val}"),
                    [(set
                       GR32:$dst,
                       (!cast<PatFrag>(frag # "_32") addr:$ptr, GR32:$val))]>, 
                    OpSize32;
    def NAME#64 : RI<opc, MRMSrcMem, (outs GR64:$dst),
                     (ins GR64:$val, i64mem:$ptr),
                     !strconcat(mnemonic, "{q}\t{$val, $ptr|$ptr, $val}"),
                     [(set
                        GR64:$dst,
                        (!cast<PatFrag>(frag # "_64") addr:$ptr, GR64:$val))]>;
  }
}

defm LXADD : ATOMIC_LOAD_BINOP<0xc0, 0xc1, "xadd", "atomic_load_add">, TB, LOCK;

/* The following multiclass tries to make sure that in code like
 *    x.store (immediate op x.load(acquire), release)
 * and
 *    x.store (register op x.load(acquire), release)
 * an operation directly on memory is generated instead of wasting a register.
 * It is not automatic as atomic_store/load are only lowered to MOV instructions
 * extremely late to prevent them from being accidentally reordered in the backend
 * (see below the RELEASE_MOV* / ACQUIRE_MOV* pseudo-instructions)
 */
multiclass RELEASE_BINOP_MI<string Name, SDNode op> {
  def : Pat<(atomic_store_8 addr:$dst,
             (op (atomic_load_8 addr:$dst), (i8 imm:$src))),
            (!cast<Instruction>(Name#"8mi") addr:$dst, imm:$src)>;
  def : Pat<(atomic_store_16 addr:$dst,
             (op (atomic_load_16 addr:$dst), (i16 imm:$src))),
            (!cast<Instruction>(Name#"16mi") addr:$dst, imm:$src)>;
  def : Pat<(atomic_store_32 addr:$dst,
             (op (atomic_load_32 addr:$dst), (i32 imm:$src))),
            (!cast<Instruction>(Name#"32mi") addr:$dst, imm:$src)>;
  def : Pat<(atomic_store_64 addr:$dst,
             (op (atomic_load_64 addr:$dst), (i64immSExt32:$src))),
            (!cast<Instruction>(Name#"64mi32") addr:$dst, (i64immSExt32:$src))>;

  def : Pat<(atomic_store_8 addr:$dst,
             (op (atomic_load_8 addr:$dst), (i8 GR8:$src))),
            (!cast<Instruction>(Name#"8mr") addr:$dst, GR8:$src)>;
  def : Pat<(atomic_store_16 addr:$dst,
             (op (atomic_load_16 addr:$dst), (i16 GR16:$src))),
            (!cast<Instruction>(Name#"16mr") addr:$dst, GR16:$src)>;
  def : Pat<(atomic_store_32 addr:$dst,
             (op (atomic_load_32 addr:$dst), (i32 GR32:$src))),
            (!cast<Instruction>(Name#"32mr") addr:$dst, GR32:$src)>;
  def : Pat<(atomic_store_64 addr:$dst,
             (op (atomic_load_64 addr:$dst), (i64 GR64:$src))),
            (!cast<Instruction>(Name#"64mr") addr:$dst, GR64:$src)>;
}
defm : RELEASE_BINOP_MI<"ADD", add>;
defm : RELEASE_BINOP_MI<"AND", and>;
defm : RELEASE_BINOP_MI<"OR",  or>;
defm : RELEASE_BINOP_MI<"XOR", xor>;
defm : RELEASE_BINOP_MI<"SUB", sub>;

// Same as above, but for floating-point.
// FIXME: imm version.
// FIXME: Version that doesn't clobber $src, using AVX's VADDSS.
// FIXME: This could also handle SIMD operations with *ps and *pd instructions.
let usesCustomInserter = 1, SchedRW = [WriteMicrocoded] in {
multiclass RELEASE_FP_BINOP_MI<SDNode op> {
    def NAME#32mr : I<0, Pseudo, (outs), (ins i32mem:$dst, FR32:$src),
        "#BINOP "#NAME#"32mr PSEUDO!",
        [(atomic_store_32 addr:$dst,
           (i32 (bitconvert (op
             (f32 (bitconvert (i32 (atomic_load_32 addr:$dst)))),
          FR32:$src))))]>, Requires<[HasSSE1]>;
    def NAME#64mr : I<0, Pseudo, (outs), (ins i64mem:$dst, FR64:$src),
        "#BINOP "#NAME#"64mr PSEUDO!",
        [(atomic_store_64 addr:$dst,
           (i64 (bitconvert (op
             (f64 (bitconvert (i64 (atomic_load_64 addr:$dst)))),
          FR64:$src))))]>, Requires<[HasSSE2]>;
}
defm RELEASE_FADD : RELEASE_FP_BINOP_MI<fadd>;
// FIXME: Add fsub, fmul, fdiv, ...
}

multiclass RELEASE_UNOP<string Name, dag dag8, dag dag16, dag dag32,
                        dag dag64> {
  def : Pat<(atomic_store_8 addr:$dst, dag8),
            (!cast<Instruction>(Name#8m) addr:$dst)>;
  def : Pat<(atomic_store_16 addr:$dst, dag16),
            (!cast<Instruction>(Name#16m) addr:$dst)>;
  def : Pat<(atomic_store_32 addr:$dst, dag32),
            (!cast<Instruction>(Name#32m) addr:$dst)>;
  def : Pat<(atomic_store_64 addr:$dst, dag64),
            (!cast<Instruction>(Name#64m) addr:$dst)>;
}

let Predicates = [UseIncDec] in {
  defm : RELEASE_UNOP<"INC",
      (add (atomic_load_8  addr:$dst), (i8 1)),
      (add (atomic_load_16 addr:$dst), (i16 1)),
      (add (atomic_load_32 addr:$dst), (i32 1)),
      (add (atomic_load_64 addr:$dst), (i64 1))>;
  defm : RELEASE_UNOP<"DEC",
      (add (atomic_load_8  addr:$dst), (i8 -1)),
      (add (atomic_load_16 addr:$dst), (i16 -1)),
      (add (atomic_load_32 addr:$dst), (i32 -1)),
      (add (atomic_load_64 addr:$dst), (i64 -1))>;
}

defm : RELEASE_UNOP<"NEG",
    (ineg (i8 (atomic_load_8  addr:$dst))),
    (ineg (i16 (atomic_load_16 addr:$dst))),
    (ineg (i32 (atomic_load_32 addr:$dst))),
    (ineg (i64 (atomic_load_64 addr:$dst)))>;
defm : RELEASE_UNOP<"NOT",
    (not (i8 (atomic_load_8  addr:$dst))),
    (not (i16 (atomic_load_16 addr:$dst))),
    (not (i32 (atomic_load_32 addr:$dst))),
    (not (i64 (atomic_load_64 addr:$dst)))>;

def : Pat<(atomic_store_8 addr:$dst, (i8 imm:$src)),
          (MOV8mi addr:$dst, imm:$src)>;
def : Pat<(atomic_store_16 addr:$dst, (i16 imm:$src)),
          (MOV16mi addr:$dst, imm:$src)>;
def : Pat<(atomic_store_32 addr:$dst, (i32 imm:$src)),
          (MOV32mi addr:$dst, imm:$src)>;
def : Pat<(atomic_store_64 addr:$dst, (i64immSExt32:$src)),
          (MOV64mi32 addr:$dst, i64immSExt32:$src)>;

def : Pat<(atomic_store_8 addr:$dst, GR8:$src),
          (MOV8mr addr:$dst, GR8:$src)>;
def : Pat<(atomic_store_16 addr:$dst, GR16:$src),
          (MOV16mr addr:$dst, GR16:$src)>;
def : Pat<(atomic_store_32 addr:$dst, GR32:$src),
          (MOV32mr addr:$dst, GR32:$src)>;
def : Pat<(atomic_store_64 addr:$dst, GR64:$src),
          (MOV64mr addr:$dst, GR64:$src)>;

def : Pat<(i8  (atomic_load_8 addr:$src)),  (MOV8rm addr:$src)>;
def : Pat<(i16 (atomic_load_16 addr:$src)), (MOV16rm addr:$src)>;
def : Pat<(i32 (atomic_load_32 addr:$src)), (MOV32rm addr:$src)>;
def : Pat<(i64 (atomic_load_64 addr:$src)), (MOV64rm addr:$src)>;

//===----------------------------------------------------------------------===//
// DAG Pattern Matching Rules
//===----------------------------------------------------------------------===//

// Use AND/OR to store 0/-1 in memory when optimizing for minsize. This saves
// binary size compared to a regular MOV, but it introduces an unnecessary
// load, so is not suitable for regular or optsize functions.
let Predicates = [OptForMinSize] in {
def : Pat<(nonvolatile_store (i16 0), addr:$dst), (AND16mi8 addr:$dst, 0)>;
def : Pat<(nonvolatile_store (i32 0), addr:$dst), (AND32mi8 addr:$dst, 0)>;
def : Pat<(nonvolatile_store (i64 0), addr:$dst), (AND64mi8 addr:$dst, 0)>;
def : Pat<(nonvolatile_store (i16 -1), addr:$dst), (OR16mi8 addr:$dst, -1)>;
def : Pat<(nonvolatile_store (i32 -1), addr:$dst), (OR32mi8 addr:$dst, -1)>;
def : Pat<(nonvolatile_store (i64 -1), addr:$dst), (OR64mi8 addr:$dst, -1)>;
}

// In kernel code model, we can get the address of a label
// into a register with 'movq'.  FIXME: This is a hack, the 'imm' predicate of
// the MOV64ri32 should accept these.
def : Pat<(i64 (X86Wrapper tconstpool  :$dst)),
          (MOV64ri32 tconstpool  :$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper tjumptable  :$dst)),
          (MOV64ri32 tjumptable  :$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper tglobaladdr :$dst)),
          (MOV64ri32 tglobaladdr :$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper texternalsym:$dst)),
          (MOV64ri32 texternalsym:$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper mcsym:$dst)),
          (MOV64ri32 mcsym:$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper tblockaddress:$dst)),
          (MOV64ri32 tblockaddress:$dst)>, Requires<[KernelCode]>;

// If we have small model and -static mode, it is safe to store global addresses
// directly as immediates.  FIXME: This is really a hack, the 'imm' predicate
// for MOV64mi32 should handle this sort of thing.
def : Pat<(store (i64 (X86Wrapper tconstpool:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tconstpool:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper tjumptable:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tjumptable:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper tglobaladdr:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tglobaladdr:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper texternalsym:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, texternalsym:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper mcsym:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, mcsym:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper tblockaddress:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tblockaddress:$src)>,
          Requires<[NearData, IsNotPIC]>;

def : Pat<(i32 (X86RecoverFrameAlloc mcsym:$dst)), (MOV32ri mcsym:$dst)>;
def : Pat<(i64 (X86RecoverFrameAlloc mcsym:$dst)), (MOV64ri mcsym:$dst)>;

// Calls

// tls has some funny stuff here...
// This corresponds to movabs $foo@tpoff, %rax
def : Pat<(i64 (X86Wrapper tglobaltlsaddr :$dst)),
          (MOV64ri32 tglobaltlsaddr :$dst)>;
// This corresponds to add $foo@tpoff, %rax
def : Pat<(add GR64:$src1, (X86Wrapper tglobaltlsaddr :$dst)),
          (ADD64ri32 GR64:$src1, tglobaltlsaddr :$dst)>;


// Direct PC relative function call for small code model. 32-bit displacement
// sign extended to 64-bit.
def : Pat<(X86call (i64 tglobaladdr:$dst)),
          (CALL64pcrel32 tglobaladdr:$dst)>;
def : Pat<(X86call (i64 texternalsym:$dst)),
          (CALL64pcrel32 texternalsym:$dst)>;

// Tailcall stuff. The TCRETURN instructions execute after the epilog, so they
// can never use callee-saved registers. That is the purpose of the GR64_TC
// register classes.
//
// The only volatile register that is never used by the calling convention is
// %r11. This happens when calling a vararg function with 6 arguments.
//
// Match an X86tcret that uses less than 7 volatile registers.
def X86tcret_6regs : PatFrag<(ops node:$ptr, node:$off),
                             (X86tcret node:$ptr, node:$off), [{
  // X86tcret args: (*chain, ptr, imm, regs..., glue)
  unsigned NumRegs = 0;
  for (unsigned i = 3, e = N->getNumOperands(); i != e; ++i)
    if (isa<RegisterSDNode>(N->getOperand(i)) && ++NumRegs > 6)
      return false;
  return true;
}]>;

def : Pat<(X86tcret ptr_rc_tailcall:$dst, imm:$off),
          (TCRETURNri ptr_rc_tailcall:$dst, imm:$off)>,
          Requires<[Not64BitMode, NotUseRetpolineIndirectCalls]>;

// FIXME: This is disabled for 32-bit PIC mode because the global base
// register which is part of the address mode may be assigned a
// callee-saved register.
def : Pat<(X86tcret (load addr:$dst), imm:$off),
          (TCRETURNmi addr:$dst, imm:$off)>,
          Requires<[Not64BitMode, IsNotPIC, NotUseRetpolineIndirectCalls]>;

def : Pat<(X86tcret (i32 tglobaladdr:$dst), imm:$off),
          (TCRETURNdi tglobaladdr:$dst, imm:$off)>,
          Requires<[NotLP64]>;

def : Pat<(X86tcret (i32 texternalsym:$dst), imm:$off),
          (TCRETURNdi texternalsym:$dst, imm:$off)>,
          Requires<[NotLP64]>;

def : Pat<(X86tcret ptr_rc_tailcall:$dst, imm:$off),
          (TCRETURNri64 ptr_rc_tailcall:$dst, imm:$off)>,
          Requires<[In64BitMode, NotUseRetpolineIndirectCalls]>;

// Don't fold loads into X86tcret requiring more than 6 regs.
// There wouldn't be enough scratch registers for base+index.
def : Pat<(X86tcret_6regs (load addr:$dst), imm:$off),
          (TCRETURNmi64 addr:$dst, imm:$off)>,
          Requires<[In64BitMode, NotUseRetpolineIndirectCalls]>;

def : Pat<(X86tcret ptr_rc_tailcall:$dst, imm:$off),
          (RETPOLINE_TCRETURN64 ptr_rc_tailcall:$dst, imm:$off)>,
          Requires<[In64BitMode, UseRetpolineIndirectCalls]>;

def : Pat<(X86tcret ptr_rc_tailcall:$dst, imm:$off),
          (RETPOLINE_TCRETURN32 ptr_rc_tailcall:$dst, imm:$off)>,
          Requires<[Not64BitMode, UseRetpolineIndirectCalls]>;

def : Pat<(X86tcret (i64 tglobaladdr:$dst), imm:$off),
          (TCRETURNdi64 tglobaladdr:$dst, imm:$off)>,
          Requires<[IsLP64]>;

def : Pat<(X86tcret (i64 texternalsym:$dst), imm:$off),
          (TCRETURNdi64 texternalsym:$dst, imm:$off)>,
          Requires<[IsLP64]>;

// Normal calls, with various flavors of addresses.
def : Pat<(X86call (i32 tglobaladdr:$dst)),
          (CALLpcrel32 tglobaladdr:$dst)>;
def : Pat<(X86call (i32 texternalsym:$dst)),
          (CALLpcrel32 texternalsym:$dst)>;
def : Pat<(X86call (i32 imm:$dst)),
          (CALLpcrel32 imm:$dst)>, Requires<[CallImmAddr]>;

// Comparisons.

// TEST R,R is smaller than CMP R,0
def : Pat<(X86cmp GR8:$src1, 0),
          (TEST8rr GR8:$src1, GR8:$src1)>;
def : Pat<(X86cmp GR16:$src1, 0),
          (TEST16rr GR16:$src1, GR16:$src1)>;
def : Pat<(X86cmp GR32:$src1, 0),
          (TEST32rr GR32:$src1, GR32:$src1)>;
def : Pat<(X86cmp GR64:$src1, 0),
          (TEST64rr GR64:$src1, GR64:$src1)>;

// Conditional moves with folded loads with operands swapped and conditions
// inverted.
multiclass CMOVmr<PatLeaf InvertedCond, Instruction Inst16, Instruction Inst32,
                  Instruction Inst64> {
  let Predicates = [HasCMov] in {
    def : Pat<(X86cmov (loadi16 addr:$src1), GR16:$src2, InvertedCond, EFLAGS),
              (Inst16 GR16:$src2, addr:$src1)>;
    def : Pat<(X86cmov (loadi32 addr:$src1), GR32:$src2, InvertedCond, EFLAGS),
              (Inst32 GR32:$src2, addr:$src1)>;
    def : Pat<(X86cmov (loadi64 addr:$src1), GR64:$src2, InvertedCond, EFLAGS),
              (Inst64 GR64:$src2, addr:$src1)>;
  }
}

defm : CMOVmr<X86_COND_B , CMOVAE16rm, CMOVAE32rm, CMOVAE64rm>;
defm : CMOVmr<X86_COND_AE, CMOVB16rm , CMOVB32rm , CMOVB64rm>;
defm : CMOVmr<X86_COND_E , CMOVNE16rm, CMOVNE32rm, CMOVNE64rm>;
defm : CMOVmr<X86_COND_NE, CMOVE16rm , CMOVE32rm , CMOVE64rm>;
defm : CMOVmr<X86_COND_BE, CMOVA16rm , CMOVA32rm , CMOVA64rm>;
defm : CMOVmr<X86_COND_A , CMOVBE16rm, CMOVBE32rm, CMOVBE64rm>;
defm : CMOVmr<X86_COND_L , CMOVGE16rm, CMOVGE32rm, CMOVGE64rm>;
defm : CMOVmr<X86_COND_GE, CMOVL16rm , CMOVL32rm , CMOVL64rm>;
defm : CMOVmr<X86_COND_LE, CMOVG16rm , CMOVG32rm , CMOVG64rm>;
defm : CMOVmr<X86_COND_G , CMOVLE16rm, CMOVLE32rm, CMOVLE64rm>;
defm : CMOVmr<X86_COND_P , CMOVNP16rm, CMOVNP32rm, CMOVNP64rm>;
defm : CMOVmr<X86_COND_NP, CMOVP16rm , CMOVP32rm , CMOVP64rm>;
defm : CMOVmr<X86_COND_S , CMOVNS16rm, CMOVNS32rm, CMOVNS64rm>;
defm : CMOVmr<X86_COND_NS, CMOVS16rm , CMOVS32rm , CMOVS64rm>;
defm : CMOVmr<X86_COND_O , CMOVNO16rm, CMOVNO32rm, CMOVNO64rm>;
defm : CMOVmr<X86_COND_NO, CMOVO16rm , CMOVO32rm , CMOVO64rm>;

// zextload bool -> zextload byte
// i1 stored in one byte in zero-extended form.
// Upper bits cleanup should be executed before Store.
def : Pat<(zextloadi8i1  addr:$src), (MOV8rm addr:$src)>;
def : Pat<(zextloadi16i1 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;
def : Pat<(zextloadi32i1 addr:$src), (MOVZX32rm8 addr:$src)>;
def : Pat<(zextloadi64i1 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm8 addr:$src), sub_32bit)>;

// extload bool -> extload byte
// When extloading from 16-bit and smaller memory locations into 64-bit
// registers, use zero-extending loads so that the entire 64-bit register is
// defined, avoiding partial-register updates.

def : Pat<(extloadi8i1 addr:$src),   (MOV8rm      addr:$src)>;
def : Pat<(extloadi16i1 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;
def : Pat<(extloadi32i1 addr:$src),  (MOVZX32rm8  addr:$src)>;
def : Pat<(extloadi16i8 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;
def : Pat<(extloadi32i8 addr:$src),  (MOVZX32rm8  addr:$src)>;
def : Pat<(extloadi32i16 addr:$src), (MOVZX32rm16 addr:$src)>;

// For other extloads, use subregs, since the high contents of the register are
// defined after an extload.
def : Pat<(extloadi64i1 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm8 addr:$src), sub_32bit)>;
def : Pat<(extloadi64i8 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm8 addr:$src), sub_32bit)>;
def : Pat<(extloadi64i16 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm16 addr:$src), sub_32bit)>;
def : Pat<(extloadi64i32 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOV32rm addr:$src), sub_32bit)>;

// anyext. Define these to do an explicit zero-extend to
// avoid partial-register updates.
def : Pat<(i16 (anyext GR8 :$src)), (EXTRACT_SUBREG
                                     (MOVZX32rr8 GR8 :$src), sub_16bit)>;
def : Pat<(i32 (anyext GR8 :$src)), (MOVZX32rr8  GR8 :$src)>;

// Except for i16 -> i32 since isel expect i16 ops to be promoted to i32.
def : Pat<(i32 (anyext GR16:$src)),
          (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR16:$src, sub_16bit)>;

def : Pat<(i64 (anyext GR8 :$src)),
          (SUBREG_TO_REG (i64 0), (MOVZX32rr8  GR8  :$src), sub_32bit)>;
def : Pat<(i64 (anyext GR16:$src)),
          (SUBREG_TO_REG (i64 0), (MOVZX32rr16 GR16 :$src), sub_32bit)>;
def : Pat<(i64 (anyext GR32:$src)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR32:$src, sub_32bit)>;

// If this is an anyext of the remainder of an 8-bit sdivrem, use a MOVSX
// instead of a MOVZX. The sdivrem lowering will emit emit a MOVSX to move
// %ah to the lower byte of a register. By using a MOVSX here we allow a
// post-isel peephole to merge the two MOVSX instructions into one.
def anyext_sdiv : PatFrag<(ops node:$lhs), (anyext node:$lhs),[{
  return (N->getOperand(0).getOpcode() == ISD::SDIVREM &&
          N->getOperand(0).getResNo() == 1);
}]>;
def : Pat<(i32 (anyext_sdiv GR8:$src)), (MOVSX32rr8 GR8:$src)>;

// Any instruction that defines a 32-bit result leaves the high half of the
// register. Truncate can be lowered to EXTRACT_SUBREG. CopyFromReg may
// be copying from a truncate. Any other 32-bit operation will zero-extend
// up to 64 bits. AssertSext/AssertZext aren't saying anything about the upper
// 32 bits, they're probably just qualifying a CopyFromReg.
def def32 : PatLeaf<(i32 GR32:$src), [{
  return N->getOpcode() != ISD::TRUNCATE &&
         N->getOpcode() != TargetOpcode::EXTRACT_SUBREG &&
         N->getOpcode() != ISD::CopyFromReg &&
         N->getOpcode() != ISD::AssertSext &&
         N->getOpcode() != ISD::AssertZext;
}]>;

// In the case of a 32-bit def that is known to implicitly zero-extend,
// we can use a SUBREG_TO_REG.
def : Pat<(i64 (zext def32:$src)),
          (SUBREG_TO_REG (i64 0), GR32:$src, sub_32bit)>;

//===----------------------------------------------------------------------===//
// Pattern match OR as ADD
//===----------------------------------------------------------------------===//

// If safe, we prefer to pattern match OR as ADD at isel time. ADD can be
// 3-addressified into an LEA instruction to avoid copies.  However, we also
// want to finally emit these instructions as an or at the end of the code
// generator to make the generated code easier to read.  To do this, we select
// into "disjoint bits" pseudo ops.

// Treat an 'or' node is as an 'add' if the or'ed bits are known to be zero.
def or_is_add : PatFrag<(ops node:$lhs, node:$rhs), (or node:$lhs, node:$rhs),[{
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N->getOperand(1)))
    return CurDAG->MaskedValueIsZero(N->getOperand(0), CN->getAPIntValue());

  KnownBits Known0 = CurDAG->computeKnownBits(N->getOperand(0), 0);
  KnownBits Known1 = CurDAG->computeKnownBits(N->getOperand(1), 0);
  return (~Known0.Zero & ~Known1.Zero) == 0;
}]>;


// (or x1, x2) -> (add x1, x2) if two operands are known not to share bits.
// Try this before the selecting to OR.
let SchedRW = [WriteALU] in {

let isConvertibleToThreeAddress = 1,
    Constraints = "$src1 = $dst", Defs = [EFLAGS] in {
let isCommutable = 1 in {
def ADD16rr_DB  : I<0, Pseudo, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                    "", // orw/addw REG, REG
                    [(set GR16:$dst, (or_is_add GR16:$src1, GR16:$src2))]>;
def ADD32rr_DB  : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                    "", // orl/addl REG, REG
                    [(set GR32:$dst, (or_is_add GR32:$src1, GR32:$src2))]>;
def ADD64rr_DB  : I<0, Pseudo, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
                    "", // orq/addq REG, REG
                    [(set GR64:$dst, (or_is_add GR64:$src1, GR64:$src2))]>;
} // isCommutable

// NOTE: These are order specific, we want the ri8 forms to be listed
// first so that they are slightly preferred to the ri forms.

def ADD16ri8_DB : I<0, Pseudo,
                    (outs GR16:$dst), (ins GR16:$src1, i16i8imm:$src2),
                    "", // orw/addw REG, imm8
                    [(set GR16:$dst,(or_is_add GR16:$src1,i16immSExt8:$src2))]>;
def ADD16ri_DB  : I<0, Pseudo, (outs GR16:$dst), (ins GR16:$src1, i16imm:$src2),
                    "", // orw/addw REG, imm
                    [(set GR16:$dst, (or_is_add GR16:$src1, imm:$src2))]>;

def ADD32ri8_DB : I<0, Pseudo,
                    (outs GR32:$dst), (ins GR32:$src1, i32i8imm:$src2),
                    "", // orl/addl REG, imm8
                    [(set GR32:$dst,(or_is_add GR32:$src1,i32immSExt8:$src2))]>;
def ADD32ri_DB  : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$src1, i32imm:$src2),
                    "", // orl/addl REG, imm
                    [(set GR32:$dst, (or_is_add GR32:$src1, imm:$src2))]>;


def ADD64ri8_DB : I<0, Pseudo,
                    (outs GR64:$dst), (ins GR64:$src1, i64i8imm:$src2),
                    "", // orq/addq REG, imm8
                    [(set GR64:$dst, (or_is_add GR64:$src1,
                                                i64immSExt8:$src2))]>;
def ADD64ri32_DB : I<0, Pseudo,
                     (outs GR64:$dst), (ins GR64:$src1, i64i32imm:$src2),
                     "", // orq/addq REG, imm
                     [(set GR64:$dst, (or_is_add GR64:$src1,
                                                 i64immSExt32:$src2))]>;
}
} // AddedComplexity, SchedRW

//===----------------------------------------------------------------------===//
// Pattern match SUB as XOR
//===----------------------------------------------------------------------===//

// An immediate in the LHS of a subtract can't be encoded in the instruction.
// If there is no possibility of a borrow we can use an XOR instead of a SUB
// to enable the immediate to be folded.
// TODO: Move this to a DAG combine?

def sub_is_xor : PatFrag<(ops node:$lhs, node:$rhs), (sub node:$lhs, node:$rhs),[{
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N->getOperand(0))) {
    KnownBits Known = CurDAG->computeKnownBits(N->getOperand(1));

    // If all possible ones in the RHS are set in the LHS then there can't be
    // a borrow and we can use xor.
    return (~Known.Zero).isSubsetOf(CN->getAPIntValue());
  }

  return false;
}]>;

let AddedComplexity = 5 in {
def : Pat<(sub_is_xor imm:$src2, GR8:$src1),
          (XOR8ri GR8:$src1, imm:$src2)>;
def : Pat<(sub_is_xor i16immSExt8:$src2, GR16:$src1),
          (XOR16ri8 GR16:$src1, i16immSExt8:$src2)>;
def : Pat<(sub_is_xor imm:$src2, GR16:$src1),
          (XOR16ri GR16:$src1, imm:$src2)>;
def : Pat<(sub_is_xor i32immSExt8:$src2, GR32:$src1),
          (XOR32ri8 GR32:$src1, i32immSExt8:$src2)>;
def : Pat<(sub_is_xor imm:$src2, GR32:$src1),
          (XOR32ri GR32:$src1, imm:$src2)>;
def : Pat<(sub_is_xor i64immSExt8:$src2, GR64:$src1),
          (XOR64ri8 GR64:$src1, i64immSExt8:$src2)>;
def : Pat<(sub_is_xor i64immSExt32:$src2, GR64:$src1),
          (XOR64ri32 GR64:$src1, i64immSExt32:$src2)>;
}

//===----------------------------------------------------------------------===//
// Some peepholes
//===----------------------------------------------------------------------===//

// Odd encoding trick: -128 fits into an 8-bit immediate field while
// +128 doesn't, so in this special case use a sub instead of an add.
def : Pat<(add GR16:$src1, 128),
          (SUB16ri8 GR16:$src1, -128)>;
def : Pat<(store (add (loadi16 addr:$dst), 128), addr:$dst),
          (SUB16mi8 addr:$dst, -128)>;

def : Pat<(add GR32:$src1, 128),
          (SUB32ri8 GR32:$src1, -128)>;
def : Pat<(store (add (loadi32 addr:$dst), 128), addr:$dst),
          (SUB32mi8 addr:$dst, -128)>;

def : Pat<(add GR64:$src1, 128),
          (SUB64ri8 GR64:$src1, -128)>;
def : Pat<(store (add (loadi64 addr:$dst), 128), addr:$dst),
          (SUB64mi8 addr:$dst, -128)>;

// The same trick applies for 32-bit immediate fields in 64-bit
// instructions.
def : Pat<(add GR64:$src1, 0x0000000080000000),
          (SUB64ri32 GR64:$src1, 0xffffffff80000000)>;
def : Pat<(store (add (loadi64 addr:$dst), 0x0000000080000000), addr:$dst),
          (SUB64mi32 addr:$dst, 0xffffffff80000000)>;

// To avoid needing to materialize an immediate in a register, use a 32-bit and
// with implicit zero-extension instead of a 64-bit and if the immediate has at
// least 32 bits of leading zeros. If in addition the last 32 bits can be
// represented with a sign extension of a 8 bit constant, use that.
// This can also reduce instruction size by eliminating the need for the REX
// prefix.

// AddedComplexity is needed to give priority over i64immSExt8 and i64immSExt32.
let AddedComplexity = 1 in {
def : Pat<(and GR64:$src, i64immZExt32SExt8:$imm),
          (SUBREG_TO_REG
            (i64 0),
            (AND32ri8
              (EXTRACT_SUBREG GR64:$src, sub_32bit),
              (i32 (GetLo8XForm imm:$imm))),
            sub_32bit)>;

def : Pat<(and GR64:$src, i64immZExt32:$imm),
          (SUBREG_TO_REG
            (i64 0),
            (AND32ri
              (EXTRACT_SUBREG GR64:$src, sub_32bit),
              (i32 (GetLo32XForm imm:$imm))),
            sub_32bit)>;
} // AddedComplexity = 1


// AddedComplexity is needed due to the increased complexity on the
// i64immZExt32SExt8 and i64immZExt32 patterns above. Applying this to all
// the MOVZX patterns keeps thems together in DAGIsel tables.
let AddedComplexity = 1 in {
// r & (2^16-1) ==> movz
def : Pat<(and GR32:$src1, 0xffff),
          (MOVZX32rr16 (EXTRACT_SUBREG GR32:$src1, sub_16bit))>;
// r & (2^8-1) ==> movz
def : Pat<(and GR32:$src1, 0xff),
          (MOVZX32rr8 (EXTRACT_SUBREG GR32:$src1, sub_8bit))>;
// r & (2^8-1) ==> movz
def : Pat<(and GR16:$src1, 0xff),
           (EXTRACT_SUBREG (MOVZX32rr8 (EXTRACT_SUBREG GR16:$src1, sub_8bit)),
             sub_16bit)>;

// r & (2^32-1) ==> movz
def : Pat<(and GR64:$src, 0x00000000FFFFFFFF),
          (SUBREG_TO_REG (i64 0),
                         (MOV32rr (EXTRACT_SUBREG GR64:$src, sub_32bit)),
                         sub_32bit)>;
// r & (2^16-1) ==> movz
def : Pat<(and GR64:$src, 0xffff),
          (SUBREG_TO_REG (i64 0),
                      (MOVZX32rr16 (i16 (EXTRACT_SUBREG GR64:$src, sub_16bit))),
                      sub_32bit)>;
// r & (2^8-1) ==> movz
def : Pat<(and GR64:$src, 0xff),
          (SUBREG_TO_REG (i64 0),
                         (MOVZX32rr8 (i8 (EXTRACT_SUBREG GR64:$src, sub_8bit))),
                         sub_32bit)>;
} // AddedComplexity = 1


// Try to use BTS/BTR/BTC for single bit operations on the upper 32-bits.

def BTRXForm : SDNodeXForm<imm, [{
  // Transformation function: Find the lowest 0.
  return getI64Imm((uint8_t)N->getAPIntValue().countTrailingOnes(), SDLoc(N));
}]>;

def BTCBTSXForm : SDNodeXForm<imm, [{
  // Transformation function: Find the lowest 1.
  return getI64Imm((uint8_t)N->getAPIntValue().countTrailingZeros(), SDLoc(N));
}]>;

def BTRMask64 : ImmLeaf<i64, [{
  return !isUInt<32>(Imm) && !isInt<32>(Imm) && isPowerOf2_64(~Imm);
}]>;

def BTCBTSMask64 : ImmLeaf<i64, [{
  return !isInt<32>(Imm) && isPowerOf2_64(Imm);
}]>;

// For now only do this for optsize.
let AddedComplexity = 1, Predicates=[OptForSize] in {
  def : Pat<(and GR64:$src1, BTRMask64:$mask),
            (BTR64ri8 GR64:$src1, (BTRXForm imm:$mask))>;
  def : Pat<(or GR64:$src1, BTCBTSMask64:$mask),
            (BTS64ri8 GR64:$src1, (BTCBTSXForm imm:$mask))>;
  def : Pat<(xor GR64:$src1, BTCBTSMask64:$mask),
            (BTC64ri8 GR64:$src1, (BTCBTSXForm imm:$mask))>;
}


// sext_inreg patterns
def : Pat<(sext_inreg GR32:$src, i16),
          (MOVSX32rr16 (EXTRACT_SUBREG GR32:$src, sub_16bit))>;
def : Pat<(sext_inreg GR32:$src, i8),
          (MOVSX32rr8 (EXTRACT_SUBREG GR32:$src, sub_8bit))>;

def : Pat<(sext_inreg GR16:$src, i8),
           (EXTRACT_SUBREG (MOVSX32rr8 (EXTRACT_SUBREG GR16:$src, sub_8bit)),
             sub_16bit)>;

def : Pat<(sext_inreg GR64:$src, i32),
          (MOVSX64rr32 (EXTRACT_SUBREG GR64:$src, sub_32bit))>;
def : Pat<(sext_inreg GR64:$src, i16),
          (MOVSX64rr16 (EXTRACT_SUBREG GR64:$src, sub_16bit))>;
def : Pat<(sext_inreg GR64:$src, i8),
          (MOVSX64rr8 (EXTRACT_SUBREG GR64:$src, sub_8bit))>;

// sext, sext_load, zext, zext_load
def: Pat<(i16 (sext GR8:$src)),
          (EXTRACT_SUBREG (MOVSX32rr8 GR8:$src), sub_16bit)>;
def: Pat<(sextloadi16i8 addr:$src),
          (EXTRACT_SUBREG (MOVSX32rm8 addr:$src), sub_16bit)>;
def: Pat<(i16 (zext GR8:$src)),
          (EXTRACT_SUBREG (MOVZX32rr8 GR8:$src), sub_16bit)>;
def: Pat<(zextloadi16i8 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;

// trunc patterns
def : Pat<(i16 (trunc GR32:$src)),
          (EXTRACT_SUBREG GR32:$src, sub_16bit)>;
def : Pat<(i8 (trunc GR32:$src)),
          (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS GR32:$src, GR32_ABCD)),
                          sub_8bit)>,
      Requires<[Not64BitMode]>;
def : Pat<(i8 (trunc GR16:$src)),
          (EXTRACT_SUBREG (i16 (COPY_TO_REGCLASS GR16:$src, GR16_ABCD)),
                          sub_8bit)>,
      Requires<[Not64BitMode]>;
def : Pat<(i32 (trunc GR64:$src)),
          (EXTRACT_SUBREG GR64:$src, sub_32bit)>;
def : Pat<(i16 (trunc GR64:$src)),
          (EXTRACT_SUBREG GR64:$src, sub_16bit)>;
def : Pat<(i8 (trunc GR64:$src)),
          (EXTRACT_SUBREG GR64:$src, sub_8bit)>;
def : Pat<(i8 (trunc GR32:$src)),
          (EXTRACT_SUBREG GR32:$src, sub_8bit)>,
      Requires<[In64BitMode]>;
def : Pat<(i8 (trunc GR16:$src)),
          (EXTRACT_SUBREG GR16:$src, sub_8bit)>,
      Requires<[In64BitMode]>;

def immff00_ffff  : ImmLeaf<i32, [{
  return Imm >= 0xff00 && Imm <= 0xffff;
}]>;

// h-register tricks
def : Pat<(i8 (trunc (srl_su GR16:$src, (i8 8)))),
          (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)>,
      Requires<[Not64BitMode]>;
def : Pat<(i8 (trunc (srl_su (i32 (anyext GR16:$src)), (i8 8)))),
          (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)>,
      Requires<[Not64BitMode]>;
def : Pat<(i8 (trunc (srl_su GR32:$src, (i8 8)))),
          (EXTRACT_SUBREG GR32:$src, sub_8bit_hi)>,
      Requires<[Not64BitMode]>;
def : Pat<(srl GR16:$src, (i8 8)),
          (EXTRACT_SUBREG
            (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)),
            sub_16bit)>;
def : Pat<(i32 (zext (srl_su GR16:$src, (i8 8)))),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR16:$src, sub_8bit_hi))>;
def : Pat<(i32 (anyext (srl_su GR16:$src, (i8 8)))),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR16:$src, sub_8bit_hi))>;
def : Pat<(and (srl_su GR32:$src, (i8 8)), (i32 255)),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR32:$src, sub_8bit_hi))>;
def : Pat<(srl (and_su GR32:$src, immff00_ffff), (i8 8)),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR32:$src, sub_8bit_hi))>;

// h-register tricks.
// For now, be conservative on x86-64 and use an h-register extract only if the
// value is immediately zero-extended or stored, which are somewhat common
// cases. This uses a bunch of code to prevent a register requiring a REX prefix
// from being allocated in the same instruction as the h register, as there's
// currently no way to describe this requirement to the register allocator.

// h-register extract and zero-extend.
def : Pat<(and (srl_su GR64:$src, (i8 8)), (i64 255)),
          (SUBREG_TO_REG
            (i64 0),
            (MOVZX32rr8_NOREX
              (EXTRACT_SUBREG GR64:$src, sub_8bit_hi)),
            sub_32bit)>;
def : Pat<(i64 (zext (srl_su GR16:$src, (i8 8)))),
          (SUBREG_TO_REG
            (i64 0),
            (MOVZX32rr8_NOREX
              (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)),
            sub_32bit)>;
def : Pat<(i64 (anyext (srl_su GR16:$src, (i8 8)))),
          (SUBREG_TO_REG
            (i64 0),
            (MOVZX32rr8_NOREX
              (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)),
            sub_32bit)>;

// h-register extract and store.
def : Pat<(store (i8 (trunc_su (srl_su GR64:$src, (i8 8)))), addr:$dst),
          (MOV8mr_NOREX
            addr:$dst,
            (EXTRACT_SUBREG GR64:$src, sub_8bit_hi))>;
def : Pat<(store (i8 (trunc_su (srl_su GR32:$src, (i8 8)))), addr:$dst),
          (MOV8mr_NOREX
            addr:$dst,
            (EXTRACT_SUBREG GR32:$src, sub_8bit_hi))>,
      Requires<[In64BitMode]>;
def : Pat<(store (i8 (trunc_su (srl_su GR16:$src, (i8 8)))), addr:$dst),
          (MOV8mr_NOREX
            addr:$dst,
            (EXTRACT_SUBREG GR16:$src, sub_8bit_hi))>,
      Requires<[In64BitMode]>;


// (shl x, 1) ==> (add x, x)
// Note that if x is undef (immediate or otherwise), we could theoretically
// end up with the two uses of x getting different values, producing a result
// where the least significant bit is not 0. However, the probability of this
// happening is considered low enough that this is officially not a
// "real problem".
def : Pat<(shl GR8 :$src1, (i8 1)), (ADD8rr  GR8 :$src1, GR8 :$src1)>;
def : Pat<(shl GR16:$src1, (i8 1)), (ADD16rr GR16:$src1, GR16:$src1)>;
def : Pat<(shl GR32:$src1, (i8 1)), (ADD32rr GR32:$src1, GR32:$src1)>;
def : Pat<(shl GR64:$src1, (i8 1)), (ADD64rr GR64:$src1, GR64:$src1)>;

// Helper imms to check if a mask doesn't change significant shift/rotate bits.
def immShift8 : ImmLeaf<i8, [{
  return countTrailingOnes<uint64_t>(Imm) >= 3;
}]>;
def immShift16 : ImmLeaf<i8, [{
  return countTrailingOnes<uint64_t>(Imm) >= 4;
}]>;
def immShift32 : ImmLeaf<i8, [{
  return countTrailingOnes<uint64_t>(Imm) >= 5;
}]>;
def immShift64 : ImmLeaf<i8, [{
  return countTrailingOnes<uint64_t>(Imm) >= 6;
}]>;

// Shift amount is implicitly masked.
multiclass MaskedShiftAmountPats<SDNode frag, string name> {
  // (shift x (and y, 31)) ==> (shift x, y)
  def : Pat<(frag GR8:$src1, (and CL, immShift32)),
            (!cast<Instruction>(name # "8rCL") GR8:$src1)>;
  def : Pat<(frag GR16:$src1, (and CL, immShift32)),
            (!cast<Instruction>(name # "16rCL") GR16:$src1)>;
  def : Pat<(frag GR32:$src1, (and CL, immShift32)),
            (!cast<Instruction>(name # "32rCL") GR32:$src1)>;
  def : Pat<(store (frag (loadi8 addr:$dst), (and CL, immShift32)), addr:$dst),
            (!cast<Instruction>(name # "8mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi16 addr:$dst), (and CL, immShift32)), addr:$dst),
            (!cast<Instruction>(name # "16mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi32 addr:$dst), (and CL, immShift32)), addr:$dst),
            (!cast<Instruction>(name # "32mCL") addr:$dst)>;

  // (shift x (and y, 63)) ==> (shift x, y)
  def : Pat<(frag GR64:$src1, (and CL, immShift64)),
            (!cast<Instruction>(name # "64rCL") GR64:$src1)>;
  def : Pat<(store (frag (loadi64 addr:$dst), (and CL, immShift64)), addr:$dst),
            (!cast<Instruction>(name # "64mCL") addr:$dst)>;
}

defm : MaskedShiftAmountPats<shl, "SHL">;
defm : MaskedShiftAmountPats<srl, "SHR">;
defm : MaskedShiftAmountPats<sra, "SAR">;

// ROL/ROR instructions allow a stronger mask optimization than shift for 8- and
// 16-bit. We can remove a mask of any (bitwidth - 1) on the rotation amount
// because over-rotating produces the same result. This is noted in the Intel
// docs with: "tempCOUNT <- (COUNT & COUNTMASK) MOD SIZE". Masking the rotation
// amount could affect EFLAGS results, but that does not matter because we are
// not tracking flags for these nodes.
multiclass MaskedRotateAmountPats<SDNode frag, string name> {
  // (rot x (and y, BitWidth - 1)) ==> (rot x, y)
  def : Pat<(frag GR8:$src1, (and CL, immShift8)),
  (!cast<Instruction>(name # "8rCL") GR8:$src1)>;
  def : Pat<(frag GR16:$src1, (and CL, immShift16)),
  (!cast<Instruction>(name # "16rCL") GR16:$src1)>;
  def : Pat<(frag GR32:$src1, (and CL, immShift32)),
  (!cast<Instruction>(name # "32rCL") GR32:$src1)>;
  def : Pat<(store (frag (loadi8 addr:$dst), (and CL, immShift8)), addr:$dst),
  (!cast<Instruction>(name # "8mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi16 addr:$dst), (and CL, immShift16)), addr:$dst),
  (!cast<Instruction>(name # "16mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi32 addr:$dst), (and CL, immShift32)), addr:$dst),
  (!cast<Instruction>(name # "32mCL") addr:$dst)>;

  // (rot x (and y, 63)) ==> (rot x, y)
  def : Pat<(frag GR64:$src1, (and CL, immShift64)),
  (!cast<Instruction>(name # "64rCL") GR64:$src1)>;
  def : Pat<(store (frag (loadi64 addr:$dst), (and CL, immShift64)), addr:$dst),
  (!cast<Instruction>(name # "64mCL") addr:$dst)>;
}


defm : MaskedRotateAmountPats<rotl, "ROL">;
defm : MaskedRotateAmountPats<rotr, "ROR">;

// Double shift amount is implicitly masked.
multiclass MaskedDoubleShiftAmountPats<SDNode frag, string name> {
  // (shift x (and y, 31)) ==> (shift x, y)
  def : Pat<(frag GR16:$src1, GR16:$src2, (and CL, immShift32)),
            (!cast<Instruction>(name # "16rrCL") GR16:$src1, GR16:$src2)>;
  def : Pat<(frag GR32:$src1, GR32:$src2, (and CL, immShift32)),
            (!cast<Instruction>(name # "32rrCL") GR32:$src1, GR32:$src2)>;

  // (shift x (and y, 63)) ==> (shift x, y)
  def : Pat<(frag GR64:$src1, GR64:$src2, (and CL, immShift64)),
            (!cast<Instruction>(name # "64rrCL") GR64:$src1, GR64:$src2)>;
}

defm : MaskedDoubleShiftAmountPats<X86shld, "SHLD">;
defm : MaskedDoubleShiftAmountPats<X86shrd, "SHRD">;

let Predicates = [HasBMI2] in {
  let AddedComplexity = 1 in {
    def : Pat<(sra GR32:$src1, (and GR8:$src2, immShift32)),
              (SARX32rr GR32:$src1,
                        (INSERT_SUBREG
                          (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
    def : Pat<(sra GR64:$src1, (and GR8:$src2, immShift64)),
              (SARX64rr GR64:$src1,
                        (INSERT_SUBREG
                          (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

    def : Pat<(srl GR32:$src1, (and GR8:$src2, immShift32)),
              (SHRX32rr GR32:$src1,
                        (INSERT_SUBREG
                          (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
    def : Pat<(srl GR64:$src1, (and GR8:$src2, immShift64)),
              (SHRX64rr GR64:$src1,
                        (INSERT_SUBREG
                          (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

    def : Pat<(shl GR32:$src1, (and GR8:$src2, immShift32)),
              (SHLX32rr GR32:$src1,
                        (INSERT_SUBREG
                          (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
    def : Pat<(shl GR64:$src1, (and GR8:$src2, immShift64)),
              (SHLX64rr GR64:$src1,
                        (INSERT_SUBREG
                          (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  }

  def : Pat<(sra (loadi32 addr:$src1), (and GR8:$src2, immShift32)),
            (SARX32rm addr:$src1,
                      (INSERT_SUBREG
                        (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(sra (loadi64 addr:$src1), (and GR8:$src2, immShift64)),
            (SARX64rm addr:$src1,
                      (INSERT_SUBREG
                        (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

  def : Pat<(srl (loadi32 addr:$src1), (and GR8:$src2, immShift32)),
            (SHRX32rm addr:$src1,
                      (INSERT_SUBREG
                        (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(srl (loadi64 addr:$src1), (and GR8:$src2, immShift64)),
            (SHRX64rm addr:$src1,
                      (INSERT_SUBREG
                        (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

  def : Pat<(shl (loadi32 addr:$src1), (and GR8:$src2, immShift32)),
            (SHLX32rm addr:$src1,
                      (INSERT_SUBREG
                        (i32 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(shl (loadi64 addr:$src1), (and GR8:$src2, immShift64)),
            (SHLX64rm addr:$src1,
                      (INSERT_SUBREG
                        (i64 (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
}

// Use BTR/BTS/BTC for clearing/setting/toggling a bit in a variable location.
multiclass one_bit_patterns<RegisterClass RC, ValueType VT, Instruction BTR,
                            Instruction BTS, Instruction BTC,
                            ImmLeaf ImmShift> {
  def : Pat<(and RC:$src1, (rotl -2, GR8:$src2)),
            (BTR RC:$src1,
                 (INSERT_SUBREG (VT (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(or RC:$src1, (shl 1, GR8:$src2)),
            (BTS RC:$src1,
                 (INSERT_SUBREG (VT (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(xor RC:$src1, (shl 1, GR8:$src2)),
            (BTC RC:$src1,
                 (INSERT_SUBREG (VT (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

  // Similar to above, but removing unneeded masking of the shift amount.
  def : Pat<(and RC:$src1, (rotl -2, (and GR8:$src2, ImmShift))),
            (BTR RC:$src1,
                 (INSERT_SUBREG (VT (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(or RC:$src1, (shl 1, (and GR8:$src2, ImmShift))),
            (BTS RC:$src1,
                (INSERT_SUBREG (VT (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(xor RC:$src1, (shl 1, (and GR8:$src2, ImmShift))),
            (BTC RC:$src1,
                (INSERT_SUBREG (VT (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
}

defm : one_bit_patterns<GR16, i16, BTR16rr, BTS16rr, BTC16rr, immShift16>;
defm : one_bit_patterns<GR32, i32, BTR32rr, BTS32rr, BTC32rr, immShift32>;
defm : one_bit_patterns<GR64, i64, BTR64rr, BTS64rr, BTC64rr, immShift64>;


// (anyext (setcc_carry)) -> (setcc_carry)
def : Pat<(i16 (anyext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C16r)>;
def : Pat<(i32 (anyext (i8 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C32r)>;
def : Pat<(i32 (anyext (i16 (X86setcc_c X86_COND_B, EFLAGS)))),
          (SETB_C32r)>;

//===----------------------------------------------------------------------===//
// EFLAGS-defining Patterns
//===----------------------------------------------------------------------===//

// add reg, reg
def : Pat<(add GR8 :$src1, GR8 :$src2), (ADD8rr  GR8 :$src1, GR8 :$src2)>;
def : Pat<(add GR16:$src1, GR16:$src2), (ADD16rr GR16:$src1, GR16:$src2)>;
def : Pat<(add GR32:$src1, GR32:$src2), (ADD32rr GR32:$src1, GR32:$src2)>;
def : Pat<(add GR64:$src1, GR64:$src2), (ADD64rr GR64:$src1, GR64:$src2)>;

// add reg, mem
def : Pat<(add GR8:$src1, (loadi8 addr:$src2)),
          (ADD8rm GR8:$src1, addr:$src2)>;
def : Pat<(add GR16:$src1, (loadi16 addr:$src2)),
          (ADD16rm GR16:$src1, addr:$src2)>;
def : Pat<(add GR32:$src1, (loadi32 addr:$src2)),
          (ADD32rm GR32:$src1, addr:$src2)>;
def : Pat<(add GR64:$src1, (loadi64 addr:$src2)),
          (ADD64rm GR64:$src1, addr:$src2)>;

// add reg, imm
def : Pat<(add GR8 :$src1, imm:$src2), (ADD8ri  GR8:$src1 , imm:$src2)>;
def : Pat<(add GR16:$src1, imm:$src2), (ADD16ri GR16:$src1, imm:$src2)>;
def : Pat<(add GR32:$src1, imm:$src2), (ADD32ri GR32:$src1, imm:$src2)>;
def : Pat<(add GR16:$src1, i16immSExt8:$src2),
          (ADD16ri8 GR16:$src1, i16immSExt8:$src2)>;
def : Pat<(add GR32:$src1, i32immSExt8:$src2),
          (ADD32ri8 GR32:$src1, i32immSExt8:$src2)>;
def : Pat<(add GR64:$src1, i64immSExt8:$src2),
          (ADD64ri8 GR64:$src1, i64immSExt8:$src2)>;
def : Pat<(add GR64:$src1, i64immSExt32:$src2),
          (ADD64ri32 GR64:$src1, i64immSExt32:$src2)>;

// sub reg, reg
def : Pat<(sub GR8 :$src1, GR8 :$src2), (SUB8rr  GR8 :$src1, GR8 :$src2)>;
def : Pat<(sub GR16:$src1, GR16:$src2), (SUB16rr GR16:$src1, GR16:$src2)>;
def : Pat<(sub GR32:$src1, GR32:$src2), (SUB32rr GR32:$src1, GR32:$src2)>;
def : Pat<(sub GR64:$src1, GR64:$src2), (SUB64rr GR64:$src1, GR64:$src2)>;

// sub reg, mem
def : Pat<(sub GR8:$src1, (loadi8 addr:$src2)),
          (SUB8rm GR8:$src1, addr:$src2)>;
def : Pat<(sub GR16:$src1, (loadi16 addr:$src2)),
          (SUB16rm GR16:$src1, addr:$src2)>;
def : Pat<(sub GR32:$src1, (loadi32 addr:$src2)),
          (SUB32rm GR32:$src1, addr:$src2)>;
def : Pat<(sub GR64:$src1, (loadi64 addr:$src2)),
          (SUB64rm GR64:$src1, addr:$src2)>;

// sub reg, imm
def : Pat<(sub GR8:$src1, imm:$src2),
          (SUB8ri GR8:$src1, imm:$src2)>;
def : Pat<(sub GR16:$src1, imm:$src2),
          (SUB16ri GR16:$src1, imm:$src2)>;
def : Pat<(sub GR32:$src1, imm:$src2),
          (SUB32ri GR32:$src1, imm:$src2)>;
def : Pat<(sub GR16:$src1, i16immSExt8:$src2),
          (SUB16ri8 GR16:$src1, i16immSExt8:$src2)>;
def : Pat<(sub GR32:$src1, i32immSExt8:$src2),
          (SUB32ri8 GR32:$src1, i32immSExt8:$src2)>;
def : Pat<(sub GR64:$src1, i64immSExt8:$src2),
          (SUB64ri8 GR64:$src1, i64immSExt8:$src2)>;
def : Pat<(sub GR64:$src1, i64immSExt32:$src2),
          (SUB64ri32 GR64:$src1, i64immSExt32:$src2)>;

// sub 0, reg
def : Pat<(X86sub_flag 0, GR8 :$src), (NEG8r  GR8 :$src)>;
def : Pat<(X86sub_flag 0, GR16:$src), (NEG16r GR16:$src)>;
def : Pat<(X86sub_flag 0, GR32:$src), (NEG32r GR32:$src)>;
def : Pat<(X86sub_flag 0, GR64:$src), (NEG64r GR64:$src)>;

// sub reg, relocImm
def : Pat<(X86sub_flag GR64:$src1, i64relocImmSExt8_su:$src2),
          (SUB64ri8 GR64:$src1, i64relocImmSExt8_su:$src2)>;
def : Pat<(X86sub_flag GR64:$src1, i64relocImmSExt32_su:$src2),
          (SUB64ri32 GR64:$src1, i64relocImmSExt32_su:$src2)>;

// mul reg, reg
def : Pat<(mul GR16:$src1, GR16:$src2),
          (IMUL16rr GR16:$src1, GR16:$src2)>;
def : Pat<(mul GR32:$src1, GR32:$src2),
          (IMUL32rr GR32:$src1, GR32:$src2)>;
def : Pat<(mul GR64:$src1, GR64:$src2),
          (IMUL64rr GR64:$src1, GR64:$src2)>;

// mul reg, mem
def : Pat<(mul GR16:$src1, (loadi16 addr:$src2)),
          (IMUL16rm GR16:$src1, addr:$src2)>;
def : Pat<(mul GR32:$src1, (loadi32 addr:$src2)),
          (IMUL32rm GR32:$src1, addr:$src2)>;
def : Pat<(mul GR64:$src1, (loadi64 addr:$src2)),
          (IMUL64rm GR64:$src1, addr:$src2)>;

// mul reg, imm
def : Pat<(mul GR16:$src1, imm:$src2),
          (IMUL16rri GR16:$src1, imm:$src2)>;
def : Pat<(mul GR32:$src1, imm:$src2),
          (IMUL32rri GR32:$src1, imm:$src2)>;
def : Pat<(mul GR16:$src1, i16immSExt8:$src2),
          (IMUL16rri8 GR16:$src1, i16immSExt8:$src2)>;
def : Pat<(mul GR32:$src1, i32immSExt8:$src2),
          (IMUL32rri8 GR32:$src1, i32immSExt8:$src2)>;
def : Pat<(mul GR64:$src1, i64immSExt8:$src2),
          (IMUL64rri8 GR64:$src1, i64immSExt8:$src2)>;
def : Pat<(mul GR64:$src1, i64immSExt32:$src2),
          (IMUL64rri32 GR64:$src1, i64immSExt32:$src2)>;

// reg = mul mem, imm
def : Pat<(mul (loadi16 addr:$src1), imm:$src2),
          (IMUL16rmi addr:$src1, imm:$src2)>;
def : Pat<(mul (loadi32 addr:$src1), imm:$src2),
          (IMUL32rmi addr:$src1, imm:$src2)>;
def : Pat<(mul (loadi16 addr:$src1), i16immSExt8:$src2),
          (IMUL16rmi8 addr:$src1, i16immSExt8:$src2)>;
def : Pat<(mul (loadi32 addr:$src1), i32immSExt8:$src2),
          (IMUL32rmi8 addr:$src1, i32immSExt8:$src2)>;
def : Pat<(mul (loadi64 addr:$src1), i64immSExt8:$src2),
          (IMUL64rmi8 addr:$src1, i64immSExt8:$src2)>;
def : Pat<(mul (loadi64 addr:$src1), i64immSExt32:$src2),
          (IMUL64rmi32 addr:$src1, i64immSExt32:$src2)>;

// Increment/Decrement reg.
// Do not make INC/DEC if it is slow
let Predicates = [UseIncDec] in {
  def : Pat<(add GR8:$src, 1),   (INC8r GR8:$src)>;
  def : Pat<(add GR16:$src, 1),  (INC16r GR16:$src)>;
  def : Pat<(add GR32:$src, 1),  (INC32r GR32:$src)>;
  def : Pat<(add GR64:$src, 1),  (INC64r GR64:$src)>;
  def : Pat<(add GR8:$src, -1),  (DEC8r GR8:$src)>;
  def : Pat<(add GR16:$src, -1), (DEC16r GR16:$src)>;
  def : Pat<(add GR32:$src, -1), (DEC32r GR32:$src)>;
  def : Pat<(add GR64:$src, -1), (DEC64r GR64:$src)>;

  def : Pat<(X86add_flag_nocf GR8:$src, -1),  (DEC8r GR8:$src)>;
  def : Pat<(X86add_flag_nocf GR16:$src, -1), (DEC16r GR16:$src)>;
  def : Pat<(X86add_flag_nocf GR32:$src, -1), (DEC32r GR32:$src)>;
  def : Pat<(X86add_flag_nocf GR64:$src, -1), (DEC64r GR64:$src)>;
  def : Pat<(X86sub_flag_nocf GR8:$src, -1),  (INC8r GR8:$src)>;
  def : Pat<(X86sub_flag_nocf GR16:$src, -1), (INC16r GR16:$src)>;
  def : Pat<(X86sub_flag_nocf GR32:$src, -1), (INC32r GR32:$src)>;
  def : Pat<(X86sub_flag_nocf GR64:$src, -1), (INC64r GR64:$src)>;
}

// or reg/reg.
def : Pat<(or GR8 :$src1, GR8 :$src2), (OR8rr  GR8 :$src1, GR8 :$src2)>;
def : Pat<(or GR16:$src1, GR16:$src2), (OR16rr GR16:$src1, GR16:$src2)>;
def : Pat<(or GR32:$src1, GR32:$src2), (OR32rr GR32:$src1, GR32:$src2)>;
def : Pat<(or GR64:$src1, GR64:$src2), (OR64rr GR64:$src1, GR64:$src2)>;

// or reg/mem
def : Pat<(or GR8:$src1, (loadi8 addr:$src2)),
          (OR8rm GR8:$src1, addr:$src2)>;
def : Pat<(or GR16:$src1, (loadi16 addr:$src2)),
          (OR16rm GR16:$src1, addr:$src2)>;
def : Pat<(or GR32:$src1, (loadi32 addr:$src2)),
          (OR32rm GR32:$src1, addr:$src2)>;
def : Pat<(or GR64:$src1, (loadi64 addr:$src2)),
          (OR64rm GR64:$src1, addr:$src2)>;

// or reg/imm
def : Pat<(or GR8:$src1 , imm:$src2), (OR8ri  GR8 :$src1, imm:$src2)>;
def : Pat<(or GR16:$src1, imm:$src2), (OR16ri GR16:$src1, imm:$src2)>;
def : Pat<(or GR32:$src1, imm:$src2), (OR32ri GR32:$src1, imm:$src2)>;
def : Pat<(or GR16:$src1, i16immSExt8:$src2),
          (OR16ri8 GR16:$src1, i16immSExt8:$src2)>;
def : Pat<(or GR32:$src1, i32immSExt8:$src2),
          (OR32ri8 GR32:$src1, i32immSExt8:$src2)>;
def : Pat<(or GR64:$src1, i64immSExt8:$src2),
          (OR64ri8 GR64:$src1, i64immSExt8:$src2)>;
def : Pat<(or GR64:$src1, i64immSExt32:$src2),
          (OR64ri32 GR64:$src1, i64immSExt32:$src2)>;

// xor reg/reg
def : Pat<(xor GR8 :$src1, GR8 :$src2), (XOR8rr  GR8 :$src1, GR8 :$src2)>;
def : Pat<(xor GR16:$src1, GR16:$src2), (XOR16rr GR16:$src1, GR16:$src2)>;
def : Pat<(xor GR32:$src1, GR32:$src2), (XOR32rr GR32:$src1, GR32:$src2)>;
def : Pat<(xor GR64:$src1, GR64:$src2), (XOR64rr GR64:$src1, GR64:$src2)>;

// xor reg/mem
def : Pat<(xor GR8:$src1, (loadi8 addr:$src2)),
          (XOR8rm GR8:$src1, addr:$src2)>;
def : Pat<(xor GR16:$src1, (loadi16 addr:$src2)),
          (XOR16rm GR16:$src1, addr:$src2)>;
def : Pat<(xor GR32:$src1, (loadi32 addr:$src2)),
          (XOR32rm GR32:$src1, addr:$src2)>;
def : Pat<(xor GR64:$src1, (loadi64 addr:$src2)),
          (XOR64rm GR64:$src1, addr:$src2)>;

// xor reg/imm
def : Pat<(xor GR8:$src1, imm:$src2),
          (XOR8ri GR8:$src1, imm:$src2)>;
def : Pat<(xor GR16:$src1, imm:$src2),
          (XOR16ri GR16:$src1, imm:$src2)>;
def : Pat<(xor GR32:$src1, imm:$src2),
          (XOR32ri GR32:$src1, imm:$src2)>;
def : Pat<(xor GR16:$src1, i16immSExt8:$src2),
          (XOR16ri8 GR16:$src1, i16immSExt8:$src2)>;
def : Pat<(xor GR32:$src1, i32immSExt8:$src2),
          (XOR32ri8 GR32:$src1, i32immSExt8:$src2)>;
def : Pat<(xor GR64:$src1, i64immSExt8:$src2),
          (XOR64ri8 GR64:$src1, i64immSExt8:$src2)>;
def : Pat<(xor GR64:$src1, i64immSExt32:$src2),
          (XOR64ri32 GR64:$src1, i64immSExt32:$src2)>;

// and reg/reg
def : Pat<(and GR8 :$src1, GR8 :$src2), (AND8rr  GR8 :$src1, GR8 :$src2)>;
def : Pat<(and GR16:$src1, GR16:$src2), (AND16rr GR16:$src1, GR16:$src2)>;
def : Pat<(and GR32:$src1, GR32:$src2), (AND32rr GR32:$src1, GR32:$src2)>;
def : Pat<(and GR64:$src1, GR64:$src2), (AND64rr GR64:$src1, GR64:$src2)>;

// and reg/mem
def : Pat<(and GR8:$src1, (loadi8 addr:$src2)),
          (AND8rm GR8:$src1, addr:$src2)>;
def : Pat<(and GR16:$src1, (loadi16 addr:$src2)),
          (AND16rm GR16:$src1, addr:$src2)>;
def : Pat<(and GR32:$src1, (loadi32 addr:$src2)),
          (AND32rm GR32:$src1, addr:$src2)>;
def : Pat<(and GR64:$src1, (loadi64 addr:$src2)),
          (AND64rm GR64:$src1, addr:$src2)>;

// and reg/imm
def : Pat<(and GR8:$src1, imm:$src2),
          (AND8ri GR8:$src1, imm:$src2)>;
def : Pat<(and GR16:$src1, imm:$src2),
          (AND16ri GR16:$src1, imm:$src2)>;
def : Pat<(and GR32:$src1, imm:$src2),
          (AND32ri GR32:$src1, imm:$src2)>;
def : Pat<(and GR16:$src1, i16immSExt8:$src2),
          (AND16ri8 GR16:$src1, i16immSExt8:$src2)>;
def : Pat<(and GR32:$src1, i32immSExt8:$src2),
          (AND32ri8 GR32:$src1, i32immSExt8:$src2)>;
def : Pat<(and GR64:$src1, i64immSExt8:$src2),
          (AND64ri8 GR64:$src1, i64immSExt8:$src2)>;
def : Pat<(and GR64:$src1, i64immSExt32:$src2),
          (AND64ri32 GR64:$src1, i64immSExt32:$src2)>;

// Bit scan instruction patterns to match explicit zero-undef behavior.
def : Pat<(cttz_zero_undef GR16:$src), (BSF16rr GR16:$src)>;
def : Pat<(cttz_zero_undef GR32:$src), (BSF32rr GR32:$src)>;
def : Pat<(cttz_zero_undef GR64:$src), (BSF64rr GR64:$src)>;
def : Pat<(cttz_zero_undef (loadi16 addr:$src)), (BSF16rm addr:$src)>;
def : Pat<(cttz_zero_undef (loadi32 addr:$src)), (BSF32rm addr:$src)>;
def : Pat<(cttz_zero_undef (loadi64 addr:$src)), (BSF64rm addr:$src)>;

// When HasMOVBE is enabled it is possible to get a non-legalized
// register-register 16 bit bswap. This maps it to a ROL instruction.
let Predicates = [HasMOVBE] in {
 def : Pat<(bswap GR16:$src), (ROL16ri GR16:$src, (i8 8))>;
}
