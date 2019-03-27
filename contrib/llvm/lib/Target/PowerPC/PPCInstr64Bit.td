//===-- PPCInstr64Bit.td - The PowerPC 64-bit Support ------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the PowerPC 64-bit instructions.  These patterns are used
// both when in ppc64 mode and when in "use 64-bit extensions in 32-bit" mode.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 64-bit operands.
//
def s16imm64 : Operand<i64> {
  let PrintMethod = "printS16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCS16ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<16>";
}
def u16imm64 : Operand<i64> {
  let PrintMethod = "printU16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCU16ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<16>";
}
def s17imm64 : Operand<i64> {
  // This operand type is used for addis/lis to allow the assembler parser
  // to accept immediates in the range -65536..65535 for compatibility with
  // the GNU assembler.  The operand is treated as 16-bit otherwise.
  let PrintMethod = "printS16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCS17ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<16>";
}
def tocentry : Operand<iPTR> {
  let MIOperandInfo = (ops i64imm:$imm);
}
def tlsreg : Operand<i64> {
  let EncoderMethod = "getTLSRegEncoding";
  let ParserMatchClass = PPCTLSRegOperand;
}
def tlsgd : Operand<i64> {}
def tlscall : Operand<i64> {
  let PrintMethod = "printTLSCall";
  let MIOperandInfo = (ops calltarget:$func, tlsgd:$sym);
  let EncoderMethod = "getTLSCallEncoding";
}

//===----------------------------------------------------------------------===//
// 64-bit transformation functions.
//

def SHL64 : SDNodeXForm<imm, [{
  // Transformation function: 63 - imm
  return getI32Imm(63 - N->getZExtValue(), SDLoc(N));
}]>;

def SRL64 : SDNodeXForm<imm, [{
  // Transformation function: 64 - imm
  return N->getZExtValue() ? getI32Imm(64 - N->getZExtValue(), SDLoc(N))
                           : getI32Imm(0, SDLoc(N));
}]>;


//===----------------------------------------------------------------------===//
// Calls.
//

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
let isTerminator = 1, isBarrier = 1, PPC970_Unit = 7 in {
  let isReturn = 1, Uses = [LR8, RM] in
    def BLR8 : XLForm_2_ext<19, 16, 20, 0, 0, (outs), (ins), "blr", IIC_BrB,
                            [(retflag)]>, Requires<[In64BitMode]>;
  let isBranch = 1, isIndirectBranch = 1, Uses = [CTR8] in {
    def BCTR8 : XLForm_2_ext<19, 528, 20, 0, 0, (outs), (ins), "bctr", IIC_BrB,
                             []>,
        Requires<[In64BitMode]>;
    def BCCCTR8 : XLForm_2_br<19, 528, 0, (outs), (ins pred:$cond),
                              "b${cond:cc}ctr${cond:pm} ${cond:reg}", IIC_BrB,
                              []>,
        Requires<[In64BitMode]>;

    def BCCTR8  : XLForm_2_br2<19, 528, 12, 0, (outs), (ins crbitrc:$bi),
                               "bcctr 12, $bi, 0", IIC_BrB, []>,
        Requires<[In64BitMode]>;
    def BCCTR8n : XLForm_2_br2<19, 528, 4, 0, (outs), (ins crbitrc:$bi),
                               "bcctr 4, $bi, 0", IIC_BrB, []>,
        Requires<[In64BitMode]>;
  }
}

let Defs = [LR8] in
  def MovePCtoLR8 : PPCEmitTimePseudo<(outs), (ins), "#MovePCtoLR8", []>,
                    PPC970_Unit_BRU;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7 in {
  let Defs = [CTR8], Uses = [CTR8] in {
    def BDZ8  : BForm_1<16, 18, 0, 0, (outs), (ins condbrtarget:$dst),
                        "bdz $dst">;
    def BDNZ8 : BForm_1<16, 16, 0, 0, (outs), (ins condbrtarget:$dst),
                        "bdnz $dst">;
  }

  let isReturn = 1, Defs = [CTR8], Uses = [CTR8, LR8, RM] in {
    def BDZLR8  : XLForm_2_ext<19, 16, 18, 0, 0, (outs), (ins),
                              "bdzlr", IIC_BrB, []>;
    def BDNZLR8 : XLForm_2_ext<19, 16, 16, 0, 0, (outs), (ins),
                              "bdnzlr", IIC_BrB, []>;
  }
}



let isCall = 1, PPC970_Unit = 7, Defs = [LR8] in {
  // Convenient aliases for call instructions
  let Uses = [RM] in {
    def BL8  : IForm<18, 0, 1, (outs), (ins calltarget:$func),
                     "bl $func", IIC_BrB, []>;  // See Pat patterns below.

    def BL8_TLS  : IForm<18, 0, 1, (outs), (ins tlscall:$func),
                         "bl $func", IIC_BrB, []>;

    def BLA8 : IForm<18, 1, 1, (outs), (ins abscalltarget:$func),
                     "bla $func", IIC_BrB, [(PPCcall (i64 imm:$func))]>;
  }
  let Uses = [RM], isCodeGenOnly = 1 in {
    def BL8_NOP  : IForm_and_DForm_4_zero<18, 0, 1, 24,
                             (outs), (ins calltarget:$func),
                             "bl $func\n\tnop", IIC_BrB, []>;

    def BL8_NOP_TLS : IForm_and_DForm_4_zero<18, 0, 1, 24,
                                  (outs), (ins tlscall:$func),
                                  "bl $func\n\tnop", IIC_BrB, []>;

    def BLA8_NOP : IForm_and_DForm_4_zero<18, 1, 1, 24,
                             (outs), (ins abscalltarget:$func),
                             "bla $func\n\tnop", IIC_BrB,
                             [(PPCcall_nop (i64 imm:$func))]>;
  }
  let Uses = [CTR8, RM] in {
    def BCTRL8 : XLForm_2_ext<19, 528, 20, 0, 1, (outs), (ins),
                              "bctrl", IIC_BrB, [(PPCbctrl)]>,
                 Requires<[In64BitMode]>;

    let isCodeGenOnly = 1 in {
      def BCCCTRL8 : XLForm_2_br<19, 528, 1, (outs), (ins pred:$cond),
                                 "b${cond:cc}ctrl${cond:pm} ${cond:reg}", IIC_BrB,
                                 []>,
          Requires<[In64BitMode]>;

      def BCCTRL8  : XLForm_2_br2<19, 528, 12, 1, (outs), (ins crbitrc:$bi),
                                  "bcctrl 12, $bi, 0", IIC_BrB, []>,
          Requires<[In64BitMode]>;
      def BCCTRL8n : XLForm_2_br2<19, 528, 4, 1, (outs), (ins crbitrc:$bi),
                                  "bcctrl 4, $bi, 0", IIC_BrB, []>,
          Requires<[In64BitMode]>;
    }
  }
}

let isCall = 1, PPC970_Unit = 7, isCodeGenOnly = 1,
    Defs = [LR8, X2], Uses = [CTR8, RM], RST = 2 in {
  def BCTRL8_LDinto_toc :
    XLForm_2_ext_and_DSForm_1<19, 528, 20, 0, 1, 58, 0, (outs),
                              (ins memrix:$src),
                              "bctrl\n\tld 2, $src", IIC_BrB,
                              [(PPCbctrl_load_toc ixaddr:$src)]>,
    Requires<[In64BitMode]>;
}

} // Interpretation64Bit

// FIXME: Duplicating this for the asm parser should be unnecessary, but the
// previous definition must be marked as CodeGen only to prevent decoding
// conflicts.
let Interpretation64Bit = 1, isAsmParserOnly = 1 in
let isCall = 1, PPC970_Unit = 7, Defs = [LR8], Uses = [RM] in
def BL8_TLS_ : IForm<18, 0, 1, (outs), (ins tlscall:$func),
                     "bl $func", IIC_BrB, []>;

// Calls
def : Pat<(PPCcall (i64 tglobaladdr:$dst)),
          (BL8 tglobaladdr:$dst)>;
def : Pat<(PPCcall_nop (i64 tglobaladdr:$dst)),
          (BL8_NOP tglobaladdr:$dst)>;

def : Pat<(PPCcall (i64 texternalsym:$dst)),
          (BL8 texternalsym:$dst)>;
def : Pat<(PPCcall_nop (i64 texternalsym:$dst)),
          (BL8_NOP texternalsym:$dst)>;

// Atomic operations
// FIXME: some of these might be used with constant operands. This will result
// in constant materialization instructions that may be redundant. We currently
// clean this up in PPCMIPeephole with calls to
// PPCInstrInfo::convertToImmediateForm() but we should probably not emit them
// in the first place.
let Defs = [CR0] in {
  def ATOMIC_LOAD_ADD_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_ADD_I64",
    [(set i64:$dst, (atomic_load_add_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_SUB_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_SUB_I64",
    [(set i64:$dst, (atomic_load_sub_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_OR_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_OR_I64",
    [(set i64:$dst, (atomic_load_or_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_XOR_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_XOR_I64",
    [(set i64:$dst, (atomic_load_xor_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_AND_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_AND_i64",
    [(set i64:$dst, (atomic_load_and_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_NAND_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_NAND_I64",
    [(set i64:$dst, (atomic_load_nand_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_MIN_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_MIN_I64",
    [(set i64:$dst, (atomic_load_min_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_MAX_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_MAX_I64",
    [(set i64:$dst, (atomic_load_max_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_UMIN_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_UMIN_I64",
    [(set i64:$dst, (atomic_load_umin_64 xoaddr:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_UMAX_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_UMAX_I64",
    [(set i64:$dst, (atomic_load_umax_64 xoaddr:$ptr, i64:$incr))]>;

  def ATOMIC_CMP_SWAP_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$old, g8rc:$new), "#ATOMIC_CMP_SWAP_I64",
    [(set i64:$dst, (atomic_cmp_swap_64 xoaddr:$ptr, i64:$old, i64:$new))]>;

  def ATOMIC_SWAP_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$new), "#ATOMIC_SWAP_I64",
    [(set i64:$dst, (atomic_swap_64 xoaddr:$ptr, i64:$new))]>;
}

// Instructions to support atomic operations
let mayLoad = 1, hasSideEffects = 0 in {
def LDARX : XForm_1_memOp<31,  84, (outs g8rc:$rD), (ins memrr:$ptr),
                          "ldarx $rD, $ptr", IIC_LdStLDARX, []>;

// Instruction to support lock versions of atomics
// (EH=1 - see Power ISA 2.07 Book II 4.4.2)
def LDARXL : XForm_1<31,  84, (outs g8rc:$rD), (ins memrr:$ptr),
                     "ldarx $rD, $ptr, 1", IIC_LdStLDARX, []>, isDOT;

let hasExtraDefRegAllocReq = 1 in
def LDAT : X_RD5_RS5_IM5<31, 614, (outs g8rc:$rD), (ins g8rc:$rA, u5imm:$FC),
                         "ldat $rD, $rA, $FC", IIC_LdStLoad>, isPPC64,
           Requires<[IsISA3_0]>;
}

let Defs = [CR0], mayStore = 1, mayLoad = 0, hasSideEffects = 0 in
def STDCX : XForm_1_memOp<31, 214, (outs), (ins g8rc:$rS, memrr:$dst),
                          "stdcx. $rS, $dst", IIC_LdStSTDCX, []>, isDOT;

let mayStore = 1, mayLoad = 0, hasSideEffects = 0 in
def STDAT : X_RD5_RS5_IM5<31, 742, (outs), (ins g8rc:$rS, g8rc:$rA, u5imm:$FC),
                          "stdat $rS, $rA, $FC", IIC_LdStStore>, isPPC64,
            Requires<[IsISA3_0]>;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNdi8 :PPCEmitTimePseudo< (outs),
                        (ins calltarget:$dst, i32imm:$offset),
                 "#TC_RETURNd8 $dst $offset",
                 []>;

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNai8 :PPCEmitTimePseudo<(outs), (ins abscalltarget:$func, i32imm:$offset),
                 "#TC_RETURNa8 $func $offset",
                 [(PPCtc_return (i64 imm:$func), imm:$offset)]>;

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNri8 : PPCEmitTimePseudo<(outs), (ins CTRRC8:$dst, i32imm:$offset),
                 "#TC_RETURNr8 $dst $offset",
                 []>;

let isTerminator = 1, isBarrier = 1, PPC970_Unit = 7, isBranch = 1,
    isIndirectBranch = 1, isCall = 1, isReturn = 1, Uses = [CTR8, RM] in
def TAILBCTR8 : XLForm_2_ext<19, 528, 20, 0, 0, (outs), (ins), "bctr", IIC_BrB,
                             []>,
    Requires<[In64BitMode]>;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7,
    isBarrier = 1, isCall = 1, isReturn = 1, Uses = [RM] in
def TAILB8   : IForm<18, 0, 0, (outs), (ins calltarget:$dst),
                  "b $dst", IIC_BrB,
                  []>;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7,
    isBarrier = 1, isCall = 1, isReturn = 1, Uses = [RM] in
def TAILBA8   : IForm<18, 0, 0, (outs), (ins abscalltarget:$dst),
                  "ba $dst", IIC_BrB,
                  []>;
} // Interpretation64Bit

def : Pat<(PPCtc_return (i64 tglobaladdr:$dst),  imm:$imm),
          (TCRETURNdi8 tglobaladdr:$dst, imm:$imm)>;

def : Pat<(PPCtc_return (i64 texternalsym:$dst), imm:$imm),
          (TCRETURNdi8 texternalsym:$dst, imm:$imm)>;

def : Pat<(PPCtc_return CTRRC8:$dst, imm:$imm),
          (TCRETURNri8 CTRRC8:$dst, imm:$imm)>;


// 64-bit CR instructions
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
let hasSideEffects = 0 in {
// mtocrf's input needs to be prepared by shifting by an amount dependent
// on the cr register selected. Thus, post-ra anti-dep breaking must not
// later change that register assignment.
let hasExtraDefRegAllocReq = 1 in {
def MTOCRF8: XFXForm_5a<31, 144, (outs crbitm:$FXM), (ins g8rc:$ST),
                        "mtocrf $FXM, $ST", IIC_BrMCRX>,
            PPC970_DGroup_First, PPC970_Unit_CRU;

// Similarly to mtocrf, the mask for mtcrf must be prepared in a way that
// is dependent on the cr fields being set.
def MTCRF8 : XFXForm_5<31, 144, (outs), (ins i32imm:$FXM, g8rc:$rS),
                      "mtcrf $FXM, $rS", IIC_BrMCRX>,
            PPC970_MicroCode, PPC970_Unit_CRU;
} // hasExtraDefRegAllocReq = 1

// mfocrf's input needs to be prepared by shifting by an amount dependent
// on the cr register selected. Thus, post-ra anti-dep breaking must not
// later change that register assignment.
let hasExtraSrcRegAllocReq = 1 in {
def MFOCRF8: XFXForm_5a<31, 19, (outs g8rc:$rT), (ins crbitm:$FXM),
                        "mfocrf $rT, $FXM", IIC_SprMFCRF>,
             PPC970_DGroup_First, PPC970_Unit_CRU;

// Similarly to mfocrf, the mask for mfcrf must be prepared in a way that
// is dependent on the cr fields being copied.
def MFCR8 : XFXForm_3<31, 19, (outs g8rc:$rT), (ins),
                     "mfcr $rT", IIC_SprMFCR>,
                     PPC970_MicroCode, PPC970_Unit_CRU;
} // hasExtraSrcRegAllocReq = 1
} // hasSideEffects = 0

// While longjmp is a control-flow barrier (fallthrough isn't allowed), setjmp
// is not.
let hasSideEffects = 1 in {
  let Defs = [CTR8] in
  def EH_SjLj_SetJmp64  : PPCCustomInserterPseudo<(outs gprc:$dst), (ins memr:$buf),
                            "#EH_SJLJ_SETJMP64",
                            [(set i32:$dst, (PPCeh_sjlj_setjmp addr:$buf))]>,
                          Requires<[In64BitMode]>;
}

let hasSideEffects = 1, isBarrier = 1 in {
  let isTerminator = 1 in
  def EH_SjLj_LongJmp64 : PPCCustomInserterPseudo<(outs), (ins memr:$buf),
                            "#EH_SJLJ_LONGJMP64",
                            [(PPCeh_sjlj_longjmp addr:$buf)]>,
                          Requires<[In64BitMode]>;
}

def MFSPR8 : XFXForm_1<31, 339, (outs g8rc:$RT), (ins i32imm:$SPR),
                       "mfspr $RT, $SPR", IIC_SprMFSPR>;
def MTSPR8 : XFXForm_1<31, 467, (outs), (ins i32imm:$SPR, g8rc:$RT),
                       "mtspr $SPR, $RT", IIC_SprMTSPR>;


//===----------------------------------------------------------------------===//
// 64-bit SPR manipulation instrs.

let Uses = [CTR8] in {
def MFCTR8 : XFXForm_1_ext<31, 339, 9, (outs g8rc:$rT), (ins),
                           "mfctr $rT", IIC_SprMFSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
let Pattern = [(PPCmtctr i64:$rS)], Defs = [CTR8] in {
def MTCTR8 : XFXForm_7_ext<31, 467, 9, (outs), (ins g8rc:$rS),
                           "mtctr $rS", IIC_SprMTSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
let hasSideEffects = 1, Defs = [CTR8] in {
let Pattern = [(int_ppc_mtctr i64:$rS)] in
def MTCTR8loop : XFXForm_7_ext<31, 467, 9, (outs), (ins g8rc:$rS),
                               "mtctr $rS", IIC_SprMTSPR>,
                 PPC970_DGroup_First, PPC970_Unit_FXU;
}

let Pattern = [(set i64:$rT, readcyclecounter)] in
def MFTB8 : XFXForm_1_ext<31, 339, 268, (outs g8rc:$rT), (ins),
                          "mfspr $rT, 268", IIC_SprMFTB>,
            PPC970_DGroup_First, PPC970_Unit_FXU;
// Note that encoding mftb using mfspr is now the preferred form,
// and has been since at least ISA v2.03. The mftb instruction has
// now been phased out. Using mfspr, however, is known not to work on
// the POWER3.

let Defs = [X1], Uses = [X1] in
def DYNALLOC8 : PPCEmitTimePseudo<(outs g8rc:$result), (ins g8rc:$negsize, memri:$fpsi),"#DYNALLOC8",
                       [(set i64:$result,
                             (PPCdynalloc i64:$negsize, iaddr:$fpsi))]>;
def DYNAREAOFFSET8 : PPCEmitTimePseudo<(outs i64imm:$result), (ins memri:$fpsi), "#DYNAREAOFFSET8",
                       [(set i64:$result, (PPCdynareaoffset iaddr:$fpsi))]>;

let Defs = [LR8] in {
def MTLR8  : XFXForm_7_ext<31, 467, 8, (outs), (ins g8rc:$rS),
                           "mtlr $rS", IIC_SprMTSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
let Uses = [LR8] in {
def MFLR8  : XFXForm_1_ext<31, 339, 8, (outs g8rc:$rT), (ins),
                           "mflr $rT", IIC_SprMFSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
} // Interpretation64Bit

//===----------------------------------------------------------------------===//
// Fixed point instructions.
//

let PPC970_Unit = 1 in {  // FXU Operations.
let Interpretation64Bit = 1 in {
let hasSideEffects = 0 in {
let isCodeGenOnly = 1 in {

let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
def LI8  : DForm_2_r0<14, (outs g8rc:$rD), (ins s16imm64:$imm),
                      "li $rD, $imm", IIC_IntSimple,
                      [(set i64:$rD, imm64SExt16:$imm)]>;
def LIS8 : DForm_2_r0<15, (outs g8rc:$rD), (ins s17imm64:$imm),
                      "lis $rD, $imm", IIC_IntSimple,
                      [(set i64:$rD, imm16ShiftedSExt:$imm)]>;
}

// Logical ops.
let isCommutable = 1 in {
defm NAND8: XForm_6r<31, 476, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "nand", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (not (and i64:$rS, i64:$rB)))]>;
defm AND8 : XForm_6r<31,  28, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "and", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (and i64:$rS, i64:$rB))]>;
} // isCommutable
defm ANDC8: XForm_6r<31,  60, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "andc", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (and i64:$rS, (not i64:$rB)))]>;
let isCommutable = 1 in {
defm OR8  : XForm_6r<31, 444, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "or", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (or i64:$rS, i64:$rB))]>;
defm NOR8 : XForm_6r<31, 124, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "nor", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (not (or i64:$rS, i64:$rB)))]>;
} // isCommutable
defm ORC8 : XForm_6r<31, 412, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "orc", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (or i64:$rS, (not i64:$rB)))]>;
let isCommutable = 1 in {
defm EQV8 : XForm_6r<31, 284, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "eqv", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (not (xor i64:$rS, i64:$rB)))]>;
defm XOR8 : XForm_6r<31, 316, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "xor", "$rA, $rS, $rB", IIC_IntSimple,
                     [(set i64:$rA, (xor i64:$rS, i64:$rB))]>;
} // let isCommutable = 1

// Logical ops with immediate.
let Defs = [CR0] in {
def ANDIo8  : DForm_4<28, (outs g8rc:$dst), (ins g8rc:$src1, u16imm64:$src2),
                      "andi. $dst, $src1, $src2", IIC_IntGeneral,
                      [(set i64:$dst, (and i64:$src1, immZExt16:$src2))]>,
                      isDOT;
def ANDISo8 : DForm_4<29, (outs g8rc:$dst), (ins g8rc:$src1, u16imm64:$src2),
                     "andis. $dst, $src1, $src2", IIC_IntGeneral,
                    [(set i64:$dst, (and i64:$src1, imm16ShiftedZExt:$src2))]>,
                     isDOT;
}
def ORI8    : DForm_4<24, (outs g8rc:$dst), (ins g8rc:$src1, u16imm64:$src2),
                      "ori $dst, $src1, $src2", IIC_IntSimple,
                      [(set i64:$dst, (or i64:$src1, immZExt16:$src2))]>;
def ORIS8   : DForm_4<25, (outs g8rc:$dst), (ins g8rc:$src1, u16imm64:$src2),
                      "oris $dst, $src1, $src2", IIC_IntSimple,
                    [(set i64:$dst, (or i64:$src1, imm16ShiftedZExt:$src2))]>;
def XORI8   : DForm_4<26, (outs g8rc:$dst), (ins g8rc:$src1, u16imm64:$src2),
                      "xori $dst, $src1, $src2", IIC_IntSimple,
                      [(set i64:$dst, (xor i64:$src1, immZExt16:$src2))]>;
def XORIS8  : DForm_4<27, (outs g8rc:$dst), (ins g8rc:$src1, u16imm64:$src2),
                      "xoris $dst, $src1, $src2", IIC_IntSimple,
                   [(set i64:$dst, (xor i64:$src1, imm16ShiftedZExt:$src2))]>;

let isCommutable = 1 in
defm ADD8  : XOForm_1r<31, 266, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                       "add", "$rT, $rA, $rB", IIC_IntSimple,
                       [(set i64:$rT, (add i64:$rA, i64:$rB))]>;
// ADD8 has a special form: reg = ADD8(reg, sym@tls) for use by the
// initial-exec thread-local storage model.  We need to forbid r0 here -
// while it works for add just fine, the linker can relax this to local-exec
// addi, which won't work for r0.
def ADD8TLS  : XOForm_1<31, 266, 0, (outs g8rc:$rT), (ins g8rc_nox0:$rA, tlsreg:$rB),
                        "add $rT, $rA, $rB", IIC_IntSimple,
                        [(set i64:$rT, (add i64:$rA, tglobaltlsaddr:$rB))]>;
let mayLoad = 1 in {
def LBZXTLS : XForm_1<31,  87, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "lbzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LHZXTLS : XForm_1<31, 279, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "lhzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LWZXTLS : XForm_1<31,  23, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "lwzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LDXTLS  : XForm_1<31,  21, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "ldx $rD, $rA, $rB", IIC_LdStLD, []>, isPPC64;
def LBZXTLS_32 : XForm_1<31,  87, (outs gprc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                         "lbzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LHZXTLS_32 : XForm_1<31, 279, (outs gprc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                         "lhzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LWZXTLS_32 : XForm_1<31,  23, (outs gprc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                         "lwzx $rD, $rA, $rB", IIC_LdStLoad, []>;

}

let mayStore = 1 in {
def STBXTLS : XForm_8<31, 215, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                      "stbx $rS, $rA, $rB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STHXTLS : XForm_8<31, 407, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                      "sthx $rS, $rA, $rB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STWXTLS : XForm_8<31, 151, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                      "stwx $rS, $rA, $rB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STDXTLS  : XForm_8<31, 149, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                       "stdx $rS, $rA, $rB", IIC_LdStSTD, []>, isPPC64,
                       PPC970_DGroup_Cracked;
def STBXTLS_32 : XForm_8<31, 215, (outs), (ins gprc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                         "stbx $rS, $rA, $rB", IIC_LdStStore, []>,
                         PPC970_DGroup_Cracked;
def STHXTLS_32 : XForm_8<31, 407, (outs), (ins gprc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                         "sthx $rS, $rA, $rB", IIC_LdStStore, []>,
                         PPC970_DGroup_Cracked;
def STWXTLS_32 : XForm_8<31, 151, (outs), (ins gprc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                         "stwx $rS, $rA, $rB", IIC_LdStStore, []>,
                         PPC970_DGroup_Cracked;

}

let isCommutable = 1 in
defm ADDC8 : XOForm_1rc<31, 10, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                        "addc", "$rT, $rA, $rB", IIC_IntGeneral,
                        [(set i64:$rT, (addc i64:$rA, i64:$rB))]>,
                        PPC970_DGroup_Cracked;

let Defs = [CARRY] in
def ADDIC8 : DForm_2<12, (outs g8rc:$rD), (ins g8rc:$rA, s16imm64:$imm),
                     "addic $rD, $rA, $imm", IIC_IntGeneral,
                     [(set i64:$rD, (addc i64:$rA, imm64SExt16:$imm))]>;
def ADDI8  : DForm_2<14, (outs g8rc:$rD), (ins g8rc_nox0:$rA, s16imm64:$imm),
                     "addi $rD, $rA, $imm", IIC_IntSimple,
                     [(set i64:$rD, (add i64:$rA, imm64SExt16:$imm))]>;
def ADDIS8 : DForm_2<15, (outs g8rc:$rD), (ins g8rc_nox0:$rA, s17imm64:$imm),
                     "addis $rD, $rA, $imm", IIC_IntSimple,
                     [(set i64:$rD, (add i64:$rA, imm16ShiftedSExt:$imm))]>;

let Defs = [CARRY] in {
def SUBFIC8: DForm_2< 8, (outs g8rc:$rD), (ins g8rc:$rA, s16imm64:$imm),
                     "subfic $rD, $rA, $imm", IIC_IntGeneral,
                     [(set i64:$rD, (subc imm64SExt16:$imm, i64:$rA))]>;
}
defm SUBFC8 : XOForm_1rc<31, 8, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                        "subfc", "$rT, $rA, $rB", IIC_IntGeneral,
                        [(set i64:$rT, (subc i64:$rB, i64:$rA))]>,
                        PPC970_DGroup_Cracked;
defm SUBF8 : XOForm_1r<31, 40, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                       "subf", "$rT, $rA, $rB", IIC_IntGeneral,
                       [(set i64:$rT, (sub i64:$rB, i64:$rA))]>;
defm NEG8    : XOForm_3r<31, 104, 0, (outs g8rc:$rT), (ins g8rc:$rA),
                        "neg", "$rT, $rA", IIC_IntSimple,
                        [(set i64:$rT, (ineg i64:$rA))]>;
let Uses = [CARRY] in {
let isCommutable = 1 in
defm ADDE8   : XOForm_1rc<31, 138, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                          "adde", "$rT, $rA, $rB", IIC_IntGeneral,
                          [(set i64:$rT, (adde i64:$rA, i64:$rB))]>;
defm ADDME8  : XOForm_3rc<31, 234, 0, (outs g8rc:$rT), (ins g8rc:$rA),
                          "addme", "$rT, $rA", IIC_IntGeneral,
                          [(set i64:$rT, (adde i64:$rA, -1))]>;
defm ADDZE8  : XOForm_3rc<31, 202, 0, (outs g8rc:$rT), (ins g8rc:$rA),
                          "addze", "$rT, $rA", IIC_IntGeneral,
                          [(set i64:$rT, (adde i64:$rA, 0))]>;
defm SUBFE8  : XOForm_1rc<31, 136, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                          "subfe", "$rT, $rA, $rB", IIC_IntGeneral,
                          [(set i64:$rT, (sube i64:$rB, i64:$rA))]>;
defm SUBFME8 : XOForm_3rc<31, 232, 0, (outs g8rc:$rT), (ins g8rc:$rA),
                          "subfme", "$rT, $rA", IIC_IntGeneral,
                          [(set i64:$rT, (sube -1, i64:$rA))]>;
defm SUBFZE8 : XOForm_3rc<31, 200, 0, (outs g8rc:$rT), (ins g8rc:$rA),
                          "subfze", "$rT, $rA", IIC_IntGeneral,
                          [(set i64:$rT, (sube 0, i64:$rA))]>;
}
} // isCodeGenOnly

// FIXME: Duplicating this for the asm parser should be unnecessary, but the
// previous definition must be marked as CodeGen only to prevent decoding
// conflicts.
let isAsmParserOnly = 1 in {
def ADD8TLS_ : XOForm_1<31, 266, 0, (outs g8rc:$rT), (ins g8rc:$rA, tlsreg:$rB),
                        "add $rT, $rA, $rB", IIC_IntSimple, []>;

let mayLoad = 1 in {
def LBZXTLS_ : XForm_1<31,  87, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "lbzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LHZXTLS_ : XForm_1<31, 279, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "lhzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LWZXTLS_ : XForm_1<31,  23, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "lwzx $rD, $rA, $rB", IIC_LdStLoad, []>;
def LDXTLS_  : XForm_1<31,  21, (outs g8rc:$rD), (ins ptr_rc_nor0:$rA, tlsreg:$rB),
                      "ldx $rD, $rA, $rB", IIC_LdStLD, []>, isPPC64;
}

let mayStore = 1 in {
def STBXTLS_ : XForm_8<31, 215, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                      "stbx $rS, $rA, $rB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STHXTLS_ : XForm_8<31, 407, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                      "sthx $rS, $rA, $rB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STWXTLS_ : XForm_8<31, 151, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                      "stwx $rS, $rA, $rB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STDXTLS_  : XForm_8<31, 149, (outs), (ins g8rc:$rS, ptr_rc_nor0:$rA, tlsreg:$rB),
                       "stdx $rS, $rA, $rB", IIC_LdStSTD, []>, isPPC64,
                       PPC970_DGroup_Cracked;
}
}

let isCommutable = 1 in {
defm MULHD : XOForm_1r<31, 73, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                       "mulhd", "$rT, $rA, $rB", IIC_IntMulHW,
                       [(set i64:$rT, (mulhs i64:$rA, i64:$rB))]>;
defm MULHDU : XOForm_1r<31, 9, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                       "mulhdu", "$rT, $rA, $rB", IIC_IntMulHWU,
                       [(set i64:$rT, (mulhu i64:$rA, i64:$rB))]>;
} // isCommutable
}
} // Interpretation64Bit

let isCompare = 1, hasSideEffects = 0 in {
  def CMPD   : XForm_16_ext<31, 0, (outs crrc:$crD), (ins g8rc:$rA, g8rc:$rB),
                            "cmpd $crD, $rA, $rB", IIC_IntCompare>, isPPC64;
  def CMPLD  : XForm_16_ext<31, 32, (outs crrc:$crD), (ins g8rc:$rA, g8rc:$rB),
                            "cmpld $crD, $rA, $rB", IIC_IntCompare>, isPPC64;
  def CMPDI  : DForm_5_ext<11, (outs crrc:$crD), (ins g8rc:$rA, s16imm64:$imm),
                           "cmpdi $crD, $rA, $imm", IIC_IntCompare>, isPPC64;
  def CMPLDI : DForm_6_ext<10, (outs crrc:$dst), (ins g8rc:$src1, u16imm64:$src2),
                           "cmpldi $dst, $src1, $src2",
                           IIC_IntCompare>, isPPC64;
  let Interpretation64Bit = 1, isCodeGenOnly = 1 in
  def CMPRB8 : X_BF3_L1_RS5_RS5<31, 192, (outs crbitrc:$BF),
                                (ins u1imm:$L, g8rc:$rA, g8rc:$rB),
                                "cmprb $BF, $L, $rA, $rB", IIC_IntCompare, []>,
               Requires<[IsISA3_0]>;
  def CMPEQB : X_BF3_RS5_RS5<31, 224, (outs crbitrc:$BF),
                             (ins g8rc:$rA, g8rc:$rB), "cmpeqb $BF, $rA, $rB",
                             IIC_IntCompare, []>, Requires<[IsISA3_0]>;
}

let hasSideEffects = 0 in {
defm SLD  : XForm_6r<31,  27, (outs g8rc:$rA), (ins g8rc:$rS, gprc:$rB),
                     "sld", "$rA, $rS, $rB", IIC_IntRotateD,
                     [(set i64:$rA, (PPCshl i64:$rS, i32:$rB))]>, isPPC64;
defm SRD  : XForm_6r<31, 539, (outs g8rc:$rA), (ins g8rc:$rS, gprc:$rB),
                     "srd", "$rA, $rS, $rB", IIC_IntRotateD,
                     [(set i64:$rA, (PPCsrl i64:$rS, i32:$rB))]>, isPPC64;
defm SRAD : XForm_6rc<31, 794, (outs g8rc:$rA), (ins g8rc:$rS, gprc:$rB),
                      "srad", "$rA, $rS, $rB", IIC_IntRotateD,
                      [(set i64:$rA, (PPCsra i64:$rS, i32:$rB))]>, isPPC64;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
defm CNTLZW8 : XForm_11r<31,  26, (outs g8rc:$rA), (ins g8rc:$rS),
                        "cntlzw", "$rA, $rS", IIC_IntGeneral, []>;
defm CNTTZW8 : XForm_11r<31, 538, (outs g8rc:$rA), (ins g8rc:$rS),
                        "cnttzw", "$rA, $rS", IIC_IntGeneral, []>,
               Requires<[IsISA3_0]>;

defm EXTSB8 : XForm_11r<31, 954, (outs g8rc:$rA), (ins g8rc:$rS),
                        "extsb", "$rA, $rS", IIC_IntSimple,
                        [(set i64:$rA, (sext_inreg i64:$rS, i8))]>;
defm EXTSH8 : XForm_11r<31, 922, (outs g8rc:$rA), (ins g8rc:$rS),
                        "extsh", "$rA, $rS", IIC_IntSimple,
                        [(set i64:$rA, (sext_inreg i64:$rS, i16))]>;

defm SLW8  : XForm_6r<31,  24, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                      "slw", "$rA, $rS, $rB", IIC_IntGeneral, []>;
defm SRW8  : XForm_6r<31, 536, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                      "srw", "$rA, $rS, $rB", IIC_IntGeneral, []>;
} // Interpretation64Bit

// For fast-isel:
let isCodeGenOnly = 1 in {
def EXTSB8_32_64 : XForm_11<31, 954, (outs g8rc:$rA), (ins gprc:$rS),
                           "extsb $rA, $rS", IIC_IntSimple, []>, isPPC64;
def EXTSH8_32_64 : XForm_11<31, 922, (outs g8rc:$rA), (ins gprc:$rS),
                           "extsh $rA, $rS", IIC_IntSimple, []>, isPPC64;
} // isCodeGenOnly for fast-isel

defm EXTSW  : XForm_11r<31, 986, (outs g8rc:$rA), (ins g8rc:$rS),
                        "extsw", "$rA, $rS", IIC_IntSimple,
                        [(set i64:$rA, (sext_inreg i64:$rS, i32))]>, isPPC64;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm EXTSW_32_64 : XForm_11r<31, 986, (outs g8rc:$rA), (ins gprc:$rS),
                             "extsw", "$rA, $rS", IIC_IntSimple,
                             [(set i64:$rA, (sext i32:$rS))]>, isPPC64;
let isCodeGenOnly = 1 in
def EXTSW_32 : XForm_11<31, 986, (outs gprc:$rA), (ins gprc:$rS),
                        "extsw $rA, $rS", IIC_IntSimple,
                        []>, isPPC64;

defm SRADI  : XSForm_1rc<31, 413, (outs g8rc:$rA), (ins g8rc:$rS, u6imm:$SH),
                         "sradi", "$rA, $rS, $SH", IIC_IntRotateDI,
                         [(set i64:$rA, (sra i64:$rS, (i32 imm:$SH)))]>, isPPC64;

defm EXTSWSLI : XSForm_1r<31, 445, (outs g8rc:$rA), (ins gprc:$rS, u6imm:$SH),
                          "extswsli", "$rA, $rS, $SH", IIC_IntRotateDI,
                          [(set i64:$rA, (PPCextswsli i32:$rS, (i32 imm:$SH)))]>,
                          isPPC64, Requires<[IsISA3_0]>;

// For fast-isel:
let isCodeGenOnly = 1, Defs = [CARRY] in
def SRADI_32  : XSForm_1<31, 413, (outs gprc:$rA), (ins gprc:$rS, u6imm:$SH),
                         "sradi $rA, $rS, $SH", IIC_IntRotateDI, []>, isPPC64;

defm CNTLZD : XForm_11r<31,  58, (outs g8rc:$rA), (ins g8rc:$rS),
                        "cntlzd", "$rA, $rS", IIC_IntGeneral,
                        [(set i64:$rA, (ctlz i64:$rS))]>;
defm CNTTZD : XForm_11r<31, 570, (outs g8rc:$rA), (ins g8rc:$rS),
                        "cnttzd", "$rA, $rS", IIC_IntGeneral,
                        [(set i64:$rA, (cttz i64:$rS))]>, Requires<[IsISA3_0]>;
def POPCNTD : XForm_11<31, 506, (outs g8rc:$rA), (ins g8rc:$rS),
                       "popcntd $rA, $rS", IIC_IntGeneral,
                       [(set i64:$rA, (ctpop i64:$rS))]>;
def BPERMD : XForm_6<31, 252, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                     "bpermd $rA, $rS, $rB", IIC_IntGeneral,
                     [(set i64:$rA, (int_ppc_bpermd g8rc:$rS, g8rc:$rB))]>,
                     isPPC64, Requires<[HasBPERMD]>;

let isCodeGenOnly = 1, isCommutable = 1 in
def CMPB8 : XForm_6<31, 508, (outs g8rc:$rA), (ins g8rc:$rS, g8rc:$rB),
                    "cmpb $rA, $rS, $rB", IIC_IntGeneral,
                    [(set i64:$rA, (PPCcmpb i64:$rS, i64:$rB))]>;

// popcntw also does a population count on the high 32 bits (storing the
// results in the high 32-bits of the output). We'll ignore that here (which is
// safe because we never separately use the high part of the 64-bit registers).
def POPCNTW : XForm_11<31, 378, (outs gprc:$rA), (ins gprc:$rS),
                       "popcntw $rA, $rS", IIC_IntGeneral,
                       [(set i32:$rA, (ctpop i32:$rS))]>;

def POPCNTB : XForm_11<31, 122, (outs gprc:$rA), (ins gprc:$rS),
                       "popcntb $rA, $rS", IIC_IntGeneral, []>;

defm DIVD  : XOForm_1rcr<31, 489, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                          "divd", "$rT, $rA, $rB", IIC_IntDivD,
                          [(set i64:$rT, (sdiv i64:$rA, i64:$rB))]>, isPPC64;
defm DIVDU : XOForm_1rcr<31, 457, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                          "divdu", "$rT, $rA, $rB", IIC_IntDivD,
                          [(set i64:$rT, (udiv i64:$rA, i64:$rB))]>, isPPC64;
def DIVDE : XOForm_1<31, 425, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                     "divde $rT, $rA, $rB", IIC_IntDivD,
                     [(set i64:$rT, (int_ppc_divde g8rc:$rA, g8rc:$rB))]>,
                     isPPC64, Requires<[HasExtDiv]>;

let Predicates = [IsISA3_0] in {
def MADDHD : VAForm_1a<48, (outs g8rc :$RT), (ins g8rc:$RA, g8rc:$RB, g8rc:$RC),
                       "maddhd $RT, $RA, $RB, $RC", IIC_IntMulHD, []>, isPPC64;
def MADDHDU : VAForm_1a<49, (outs g8rc :$RT), (ins g8rc:$RA, g8rc:$RB, g8rc:$RC),
                       "maddhdu $RT, $RA, $RB, $RC", IIC_IntMulHD, []>, isPPC64;
def MADDLD : VAForm_1a<51, (outs g8rc :$RT), (ins g8rc:$RA, g8rc:$RB, g8rc:$RC),
                       "maddld $RT, $RA, $RB, $RC", IIC_IntMulHD, []>, isPPC64;
def SETB : XForm_44<31, 128, (outs gprc:$RT), (ins crrc:$BFA),
                       "setb $RT, $BFA", IIC_IntGeneral>, isPPC64;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
  def SETB8 : XForm_44<31, 128, (outs g8rc:$RT), (ins crrc:$BFA),
                       "setb $RT, $BFA", IIC_IntGeneral>, isPPC64;
}
def DARN : XForm_45<31, 755, (outs g8rc:$RT), (ins i32imm:$L),
                     "darn $RT, $L", IIC_LdStLD>, isPPC64;
def ADDPCIS : DXForm<19, 2, (outs g8rc:$RT), (ins i32imm:$D),
                     "addpcis $RT, $D", IIC_BrB, []>, isPPC64;
def MODSD : XForm_8<31, 777, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                        "modsd $rT, $rA, $rB", IIC_IntDivW,
                        [(set i64:$rT, (srem i64:$rA, i64:$rB))]>;
def MODUD : XForm_8<31, 265, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                        "modud $rT, $rA, $rB", IIC_IntDivW,
                        [(set i64:$rT, (urem i64:$rA, i64:$rB))]>;
}

let Defs = [CR0] in
def DIVDEo : XOForm_1<31, 425, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                      "divde. $rT, $rA, $rB", IIC_IntDivD,
                      []>, isDOT, PPC970_DGroup_Cracked, PPC970_DGroup_First,
                      isPPC64, Requires<[HasExtDiv]>;
def DIVDEU : XOForm_1<31, 393, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                      "divdeu $rT, $rA, $rB", IIC_IntDivD,
                      [(set i64:$rT, (int_ppc_divdeu g8rc:$rA, g8rc:$rB))]>,
                      isPPC64, Requires<[HasExtDiv]>;
let Defs = [CR0] in
def DIVDEUo : XOForm_1<31, 393, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                       "divdeu. $rT, $rA, $rB", IIC_IntDivD,
                       []>, isDOT, PPC970_DGroup_Cracked, PPC970_DGroup_First,
                        isPPC64, Requires<[HasExtDiv]>;
let isCommutable = 1 in
defm MULLD : XOForm_1r<31, 233, 0, (outs g8rc:$rT), (ins g8rc:$rA, g8rc:$rB),
                       "mulld", "$rT, $rA, $rB", IIC_IntMulHD,
                       [(set i64:$rT, (mul i64:$rA, i64:$rB))]>, isPPC64;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def MULLI8 : DForm_2<7, (outs g8rc:$rD), (ins g8rc:$rA, s16imm64:$imm),
                       "mulli $rD, $rA, $imm", IIC_IntMulLI,
                       [(set i64:$rD, (mul i64:$rA, imm64SExt16:$imm))]>;
}

let hasSideEffects = 0 in {
defm RLDIMI : MDForm_1r<30, 3, (outs g8rc:$rA),
                        (ins g8rc:$rSi, g8rc:$rS, u6imm:$SH, u6imm:$MBE),
                        "rldimi", "$rA, $rS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64, RegConstraint<"$rSi = $rA">,
                        NoEncode<"$rSi">;

// Rotate instructions.
defm RLDCL  : MDSForm_1r<30, 8,
                        (outs g8rc:$rA), (ins g8rc:$rS, gprc:$rB, u6imm:$MBE),
                        "rldcl", "$rA, $rS, $rB, $MBE", IIC_IntRotateD,
                        []>, isPPC64;
defm RLDCR  : MDSForm_1r<30, 9,
                        (outs g8rc:$rA), (ins g8rc:$rS, gprc:$rB, u6imm:$MBE),
                        "rldcr", "$rA, $rS, $rB, $MBE", IIC_IntRotateD,
                        []>, isPPC64;
defm RLDICL : MDForm_1r<30, 0,
                        (outs g8rc:$rA), (ins g8rc:$rS, u6imm:$SH, u6imm:$MBE),
                        "rldicl", "$rA, $rS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64;
// For fast-isel:
let isCodeGenOnly = 1 in
def RLDICL_32_64 : MDForm_1<30, 0,
                            (outs g8rc:$rA),
                            (ins gprc:$rS, u6imm:$SH, u6imm:$MBE),
                            "rldicl $rA, $rS, $SH, $MBE", IIC_IntRotateDI,
                            []>, isPPC64;
// End fast-isel.
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm RLDICL_32 : MDForm_1r<30, 0,
                           (outs gprc:$rA),
                           (ins gprc:$rS, u6imm:$SH, u6imm:$MBE),
                           "rldicl", "$rA, $rS, $SH, $MBE", IIC_IntRotateDI,
                           []>, isPPC64;
defm RLDICR : MDForm_1r<30, 1,
                        (outs g8rc:$rA), (ins g8rc:$rS, u6imm:$SH, u6imm:$MBE),
                        "rldicr", "$rA, $rS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64;
let isCodeGenOnly = 1 in
def RLDICR_32 : MDForm_1<30, 1,
                         (outs gprc:$rA), (ins gprc:$rS, u6imm:$SH, u6imm:$MBE),
                         "rldicr $rA, $rS, $SH, $MBE", IIC_IntRotateDI,
                         []>, isPPC64;
defm RLDIC  : MDForm_1r<30, 2,
                        (outs g8rc:$rA), (ins g8rc:$rS, u6imm:$SH, u6imm:$MBE),
                        "rldic", "$rA, $rS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
defm RLWINM8 : MForm_2r<21, (outs g8rc:$rA),
                        (ins g8rc:$rS, u5imm:$SH, u5imm:$MB, u5imm:$ME),
                        "rlwinm", "$rA, $rS, $SH, $MB, $ME", IIC_IntGeneral,
                        []>;

defm RLWNM8  : MForm_2r<23, (outs g8rc:$rA),
                        (ins g8rc:$rS, g8rc:$rB, u5imm:$MB, u5imm:$ME),
                        "rlwnm", "$rA, $rS, $rB, $MB, $ME", IIC_IntGeneral,
                        []>;

// RLWIMI can be commuted if the rotate amount is zero.
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm RLWIMI8 : MForm_2r<20, (outs g8rc:$rA),
                        (ins g8rc:$rSi, g8rc:$rS, u5imm:$SH, u5imm:$MB,
                        u5imm:$ME), "rlwimi", "$rA, $rS, $SH, $MB, $ME",
                        IIC_IntRotate, []>, PPC970_DGroup_Cracked,
                        RegConstraint<"$rSi = $rA">, NoEncode<"$rSi">;

let isSelect = 1 in
def ISEL8   : AForm_4<31, 15,
                     (outs g8rc:$rT), (ins g8rc_nox0:$rA, g8rc:$rB, crbitrc:$cond),
                     "isel $rT, $rA, $rB, $cond", IIC_IntISEL,
                     []>;
}  // Interpretation64Bit
}  // hasSideEffects = 0
}  // End FXU Operations.


//===----------------------------------------------------------------------===//
// Load/Store instructions.
//


// Sign extending loads.
let PPC970_Unit = 2 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHA8: DForm_1<42, (outs g8rc:$rD), (ins memri:$src),
                  "lha $rD, $src", IIC_LdStLHA,
                  [(set i64:$rD, (sextloadi16 iaddr:$src))]>,
                  PPC970_DGroup_Cracked;
def LWA  : DSForm_1<58, 2, (outs g8rc:$rD), (ins memrix:$src),
                    "lwa $rD, $src", IIC_LdStLWA,
                    [(set i64:$rD,
                          (aligned4sextloadi32 ixaddr:$src))]>, isPPC64,
                    PPC970_DGroup_Cracked;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHAX8: XForm_1_memOp<31, 343, (outs g8rc:$rD), (ins memrr:$src),
                        "lhax $rD, $src", IIC_LdStLHA,
                        [(set i64:$rD, (sextloadi16 xaddr:$src))]>,
                        PPC970_DGroup_Cracked;
def LWAX : XForm_1_memOp<31, 341, (outs g8rc:$rD), (ins memrr:$src),
                        "lwax $rD, $src", IIC_LdStLHA,
                        [(set i64:$rD, (sextloadi32 xaddr:$src))]>, isPPC64,
                        PPC970_DGroup_Cracked;
// For fast-isel:
let isCodeGenOnly = 1, mayLoad = 1 in {
def LWA_32  : DSForm_1<58, 2, (outs gprc:$rD), (ins memrix:$src),
                      "lwa $rD, $src", IIC_LdStLWA, []>, isPPC64,
                      PPC970_DGroup_Cracked;
def LWAX_32 : XForm_1_memOp<31, 341, (outs gprc:$rD), (ins memrr:$src),
                            "lwax $rD, $src", IIC_LdStLHA, []>, isPPC64,
                            PPC970_DGroup_Cracked;
} // end fast-isel isCodeGenOnly

// Update forms.
let mayLoad = 1, hasSideEffects = 0 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHAU8 : DForm_1<43, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                    (ins memri:$addr),
                    "lhau $rD, $addr", IIC_LdStLHAU,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;
// NO LWAU!

let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHAUX8 : XForm_1_memOp<31, 375, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                          (ins memrr:$addr),
                          "lhaux $rD, $addr", IIC_LdStLHAUX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
def LWAUX : XForm_1_memOp<31, 373, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                          (ins memrr:$addr),
                          "lwaux $rD, $addr", IIC_LdStLHAUX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">, isPPC64;
}
}

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
// Zero extending loads.
let PPC970_Unit = 2 in {
def LBZ8 : DForm_1<34, (outs g8rc:$rD), (ins memri:$src),
                  "lbz $rD, $src", IIC_LdStLoad,
                  [(set i64:$rD, (zextloadi8 iaddr:$src))]>;
def LHZ8 : DForm_1<40, (outs g8rc:$rD), (ins memri:$src),
                  "lhz $rD, $src", IIC_LdStLoad,
                  [(set i64:$rD, (zextloadi16 iaddr:$src))]>;
def LWZ8 : DForm_1<32, (outs g8rc:$rD), (ins memri:$src),
                  "lwz $rD, $src", IIC_LdStLoad,
                  [(set i64:$rD, (zextloadi32 iaddr:$src))]>, isPPC64;

def LBZX8 : XForm_1_memOp<31,  87, (outs g8rc:$rD), (ins memrr:$src),
                          "lbzx $rD, $src", IIC_LdStLoad,
                          [(set i64:$rD, (zextloadi8 xaddr:$src))]>;
def LHZX8 : XForm_1_memOp<31, 279, (outs g8rc:$rD), (ins memrr:$src),
                          "lhzx $rD, $src", IIC_LdStLoad,
                          [(set i64:$rD, (zextloadi16 xaddr:$src))]>;
def LWZX8 : XForm_1_memOp<31,  23, (outs g8rc:$rD), (ins memrr:$src),
                          "lwzx $rD, $src", IIC_LdStLoad,
                          [(set i64:$rD, (zextloadi32 xaddr:$src))]>;
                   
                   
// Update forms.
let mayLoad = 1, hasSideEffects = 0 in {
def LBZU8 : DForm_1<35, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                    (ins memri:$addr),
                    "lbzu $rD, $addr", IIC_LdStLoadUpd,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;
def LHZU8 : DForm_1<41, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                    (ins memri:$addr),
                    "lhzu $rD, $addr", IIC_LdStLoadUpd,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;
def LWZU8 : DForm_1<33, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                    (ins memri:$addr),
                    "lwzu $rD, $addr", IIC_LdStLoadUpd,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;

def LBZUX8 : XForm_1_memOp<31, 119, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                          (ins memrr:$addr),
                          "lbzux $rD, $addr", IIC_LdStLoadUpdX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
def LHZUX8 : XForm_1_memOp<31, 311, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                          (ins memrr:$addr),
                          "lhzux $rD, $addr", IIC_LdStLoadUpdX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
def LWZUX8 : XForm_1_memOp<31, 55, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                          (ins memrr:$addr),
                          "lwzux $rD, $addr", IIC_LdStLoadUpdX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
}
}
} // Interpretation64Bit


// Full 8-byte loads.
let PPC970_Unit = 2 in {
def LD   : DSForm_1<58, 0, (outs g8rc:$rD), (ins memrix:$src),
                    "ld $rD, $src", IIC_LdStLD,
                    [(set i64:$rD, (aligned4load ixaddr:$src))]>, isPPC64;
// The following four definitions are selected for small code model only.
// Otherwise, we need to create two instructions to form a 32-bit offset,
// so we have a custom matcher for TOC_ENTRY in PPCDAGToDAGIsel::Select().
def LDtoc: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtoc",
                  [(set i64:$rD,
                     (PPCtoc_entry tglobaladdr:$disp, i64:$reg))]>, isPPC64;
def LDtocJTI: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtocJTI",
                  [(set i64:$rD,
                     (PPCtoc_entry tjumptable:$disp, i64:$reg))]>, isPPC64;
def LDtocCPT: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtocCPT",
                  [(set i64:$rD,
                     (PPCtoc_entry tconstpool:$disp, i64:$reg))]>, isPPC64;
def LDtocBA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtocCPT",
                  [(set i64:$rD,
                     (PPCtoc_entry tblockaddress:$disp, i64:$reg))]>, isPPC64;

def LDX  : XForm_1_memOp<31,  21, (outs g8rc:$rD), (ins memrr:$src),
                        "ldx $rD, $src", IIC_LdStLD,
                        [(set i64:$rD, (load xaddr:$src))]>, isPPC64;
def LDBRX : XForm_1_memOp<31,  532, (outs g8rc:$rD), (ins memrr:$src),
                          "ldbrx $rD, $src", IIC_LdStLoad,
                          [(set i64:$rD, (PPClbrx xoaddr:$src, i64))]>, isPPC64;

let mayLoad = 1, hasSideEffects = 0, isCodeGenOnly = 1 in {
def LHBRX8 : XForm_1_memOp<31, 790, (outs g8rc:$rD), (ins memrr:$src),
                          "lhbrx $rD, $src", IIC_LdStLoad, []>;
def LWBRX8 : XForm_1_memOp<31,  534, (outs g8rc:$rD), (ins memrr:$src),
                          "lwbrx $rD, $src", IIC_LdStLoad, []>;
}

let mayLoad = 1, hasSideEffects = 0 in {
def LDU  : DSForm_1<58, 1, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                    (ins memrix:$addr),
                    "ldu $rD, $addr", IIC_LdStLDU,
                    []>, RegConstraint<"$addr.reg = $ea_result">, isPPC64,
                    NoEncode<"$ea_result">;

def LDUX : XForm_1_memOp<31, 53, (outs g8rc:$rD, ptr_rc_nor0:$ea_result),
                        (ins memrr:$addr),
                        "ldux $rD, $addr", IIC_LdStLDUX,
                        []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                        NoEncode<"$ea_result">, isPPC64;

def LDMX : XForm_1<31, 309, (outs g8rc:$rD), (ins memrr:$src),
                   "ldmx $rD, $src", IIC_LdStLD, []>, isPPC64,
                   Requires<[IsISA3_0]>;
}
}

// Support for medium and large code model.
let hasSideEffects = 0 in {
let isReMaterializable = 1 in {
def ADDIStocHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, tocentry:$disp),
                       "#ADDIStocHA", []>, isPPC64;
def ADDItocL: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, tocentry:$disp),
                     "#ADDItocL", []>, isPPC64;
}
let mayLoad = 1 in
def LDtocL: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc_nox0:$reg),
                   "#LDtocL", []>, isPPC64;
}

// Support for thread-local storage.
def ADDISgotTprelHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDISgotTprelHA",
                         [(set i64:$rD,
                           (PPCaddisGotTprelHA i64:$reg,
                                               tglobaltlsaddr:$disp))]>,
                  isPPC64;
def LDgotTprelL: PPCEmitTimePseudo<(outs g8rc:$rD), (ins s16imm64:$disp, g8rc_nox0:$reg),
                        "#LDgotTprelL",
                        [(set i64:$rD,
                          (PPCldGotTprelL tglobaltlsaddr:$disp, i64:$reg))]>,
                 isPPC64;

let Defs = [CR7], Itinerary = IIC_LdStSync in
def CFENCE8 : PPCPostRAExpPseudo<(outs), (ins g8rc:$cr), "#CFENCE8", []>;

def : Pat<(PPCaddTls i64:$in, tglobaltlsaddr:$g),
          (ADD8TLS $in, tglobaltlsaddr:$g)>;
def ADDIStlsgdHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDIStlsgdHA",
                         [(set i64:$rD,
                           (PPCaddisTlsgdHA i64:$reg, tglobaltlsaddr:$disp))]>,
                  isPPC64;
def ADDItlsgdL : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                       "#ADDItlsgdL",
                       [(set i64:$rD,
                         (PPCaddiTlsgdL i64:$reg, tglobaltlsaddr:$disp))]>,
                 isPPC64;
// LR8 is a true define, while the rest of the Defs are clobbers.  X3 is
// explicitly defined when this op is created, so not mentioned here.
// This is lowered to BL8_NOP_TLS by the assembly printer, so the size must be
// correct because the branch select pass is relying on it.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1, Size = 8,
    Defs = [X0,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7] in
def GETtlsADDR : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc:$reg, tlsgd:$sym),
                        "#GETtlsADDR",
                        [(set i64:$rD,
                          (PPCgetTlsAddr i64:$reg, tglobaltlsaddr:$sym))]>,
                 isPPC64;
// Combined op for ADDItlsgdL and GETtlsADDR, late expanded.  X3 and LR8
// are true defines while the rest of the Defs are clobbers.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [X0,X3,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7]
    in
def ADDItlsgdLADDR : PPCEmitTimePseudo<(outs g8rc:$rD),
                            (ins g8rc_nox0:$reg, s16imm64:$disp, tlsgd:$sym),
                            "#ADDItlsgdLADDR",
                            [(set i64:$rD,
                              (PPCaddiTlsgdLAddr i64:$reg,
                                                 tglobaltlsaddr:$disp,
                                                 tglobaltlsaddr:$sym))]>,
                     isPPC64;
def ADDIStlsldHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDIStlsldHA",
                         [(set i64:$rD,
                           (PPCaddisTlsldHA i64:$reg, tglobaltlsaddr:$disp))]>,
                  isPPC64;
def ADDItlsldL : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                       "#ADDItlsldL",
                       [(set i64:$rD,
                         (PPCaddiTlsldL i64:$reg, tglobaltlsaddr:$disp))]>,
                 isPPC64;
// LR8 is a true define, while the rest of the Defs are clobbers.  X3 is
// explicitly defined when this op is created, so not mentioned here.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [X0,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7] in
def GETtlsldADDR : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc:$reg, tlsgd:$sym),
                          "#GETtlsldADDR",
                          [(set i64:$rD,
                            (PPCgetTlsldAddr i64:$reg, tglobaltlsaddr:$sym))]>,
                   isPPC64;
// Combined op for ADDItlsldL and GETtlsADDR, late expanded.  X3 and LR8
// are true defines, while the rest of the Defs are clobbers.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [X0,X3,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7]
    in
def ADDItlsldLADDR : PPCEmitTimePseudo<(outs g8rc:$rD),
                            (ins g8rc_nox0:$reg, s16imm64:$disp, tlsgd:$sym),
                            "#ADDItlsldLADDR",
                            [(set i64:$rD,
                              (PPCaddiTlsldLAddr i64:$reg,
                                                 tglobaltlsaddr:$disp,
                                                 tglobaltlsaddr:$sym))]>,
                     isPPC64;
def ADDISdtprelHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                          "#ADDISdtprelHA",
                          [(set i64:$rD,
                            (PPCaddisDtprelHA i64:$reg,
                                              tglobaltlsaddr:$disp))]>,
                   isPPC64;
def ADDIdtprelL : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDIdtprelL",
                         [(set i64:$rD,
                           (PPCaddiDtprelL i64:$reg, tglobaltlsaddr:$disp))]>,
                  isPPC64;

let PPC970_Unit = 2 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
// Truncating stores.                       
def STB8 : DForm_1<38, (outs), (ins g8rc:$rS, memri:$src),
                   "stb $rS, $src", IIC_LdStStore,
                   [(truncstorei8 i64:$rS, iaddr:$src)]>;
def STH8 : DForm_1<44, (outs), (ins g8rc:$rS, memri:$src),
                   "sth $rS, $src", IIC_LdStStore,
                   [(truncstorei16 i64:$rS, iaddr:$src)]>;
def STW8 : DForm_1<36, (outs), (ins g8rc:$rS, memri:$src),
                   "stw $rS, $src", IIC_LdStStore,
                   [(truncstorei32 i64:$rS, iaddr:$src)]>;
def STBX8 : XForm_8_memOp<31, 215, (outs), (ins g8rc:$rS, memrr:$dst),
                          "stbx $rS, $dst", IIC_LdStStore,
                          [(truncstorei8 i64:$rS, xaddr:$dst)]>,
                          PPC970_DGroup_Cracked;
def STHX8 : XForm_8_memOp<31, 407, (outs), (ins g8rc:$rS, memrr:$dst),
                          "sthx $rS, $dst", IIC_LdStStore,
                          [(truncstorei16 i64:$rS, xaddr:$dst)]>,
                          PPC970_DGroup_Cracked;
def STWX8 : XForm_8_memOp<31, 151, (outs), (ins g8rc:$rS, memrr:$dst),
                          "stwx $rS, $dst", IIC_LdStStore,
                          [(truncstorei32 i64:$rS, xaddr:$dst)]>,
                          PPC970_DGroup_Cracked;
} // Interpretation64Bit

// Normal 8-byte stores.
def STD  : DSForm_1<62, 0, (outs), (ins g8rc:$rS, memrix:$dst),
                    "std $rS, $dst", IIC_LdStSTD,
                    [(aligned4store i64:$rS, ixaddr:$dst)]>, isPPC64;
def STDX  : XForm_8_memOp<31, 149, (outs), (ins g8rc:$rS, memrr:$dst),
                          "stdx $rS, $dst", IIC_LdStSTD,
                          [(store i64:$rS, xaddr:$dst)]>, isPPC64,
                          PPC970_DGroup_Cracked;
def STDBRX: XForm_8_memOp<31, 660, (outs), (ins g8rc:$rS, memrr:$dst),
                          "stdbrx $rS, $dst", IIC_LdStStore,
                          [(PPCstbrx i64:$rS, xoaddr:$dst, i64)]>, isPPC64,
                          PPC970_DGroup_Cracked;
}

// Stores with Update (pre-inc).
let PPC970_Unit = 2, mayStore = 1, mayLoad = 0 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
def STBU8 : DForm_1<39, (outs ptr_rc_nor0:$ea_res), (ins g8rc:$rS, memri:$dst),
                   "stbu $rS, $dst", IIC_LdStSTU, []>,
                   RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;
def STHU8 : DForm_1<45, (outs ptr_rc_nor0:$ea_res), (ins g8rc:$rS, memri:$dst),
                   "sthu $rS, $dst", IIC_LdStSTU, []>,
                   RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;
def STWU8 : DForm_1<37, (outs ptr_rc_nor0:$ea_res), (ins g8rc:$rS, memri:$dst),
                   "stwu $rS, $dst", IIC_LdStSTU, []>,
                   RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;

def STBUX8: XForm_8_memOp<31, 247, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$rS, memrr:$dst),
                          "stbux $rS, $dst", IIC_LdStSTUX, []>,
                          RegConstraint<"$dst.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
def STHUX8: XForm_8_memOp<31, 439, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$rS, memrr:$dst),
                          "sthux $rS, $dst", IIC_LdStSTUX, []>,
                          RegConstraint<"$dst.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
def STWUX8: XForm_8_memOp<31, 183, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$rS, memrr:$dst),
                          "stwux $rS, $dst", IIC_LdStSTUX, []>,
                          RegConstraint<"$dst.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
} // Interpretation64Bit

def STDU : DSForm_1<62, 1, (outs ptr_rc_nor0:$ea_res),
                   (ins g8rc:$rS, memrix:$dst),
                   "stdu $rS, $dst", IIC_LdStSTU, []>,
                   RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">,
                   isPPC64;

def STDUX : XForm_8_memOp<31, 181, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$rS, memrr:$dst),
                          "stdux $rS, $dst", IIC_LdStSTUX, []>,
                          RegConstraint<"$dst.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked, isPPC64;
}

// Patterns to match the pre-inc stores.  We can't put the patterns on
// the instruction definitions directly as ISel wants the address base
// and offset to be separate operands, not a single complex operand.
def : Pat<(pre_truncsti8 i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STBU8 $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_truncsti16 i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STHU8 $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_truncsti32 i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STWU8 $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(aligned4pre_store i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STDU $rS, iaddroff:$ptroff, $ptrreg)>;

def : Pat<(pre_truncsti8 i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STBUX8 $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_truncsti16 i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STHUX8 $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_truncsti32 i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STWUX8 $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_store i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STDUX $rS, $ptrreg, $ptroff)>;


//===----------------------------------------------------------------------===//
// Floating point instructions.
//


let PPC970_Unit = 3, hasSideEffects = 0,
    Uses = [RM] in {  // FPU Operations.
defm FCFID  : XForm_26r<63, 846, (outs f8rc:$frD), (ins f8rc:$frB),
                        "fcfid", "$frD, $frB", IIC_FPGeneral,
                        [(set f64:$frD, (PPCfcfid f64:$frB))]>, isPPC64;
defm FCTID  : XForm_26r<63, 814, (outs f8rc:$frD), (ins f8rc:$frB),
                        "fctid", "$frD, $frB", IIC_FPGeneral,
                        []>, isPPC64;
defm FCTIDU : XForm_26r<63, 942, (outs f8rc:$frD), (ins f8rc:$frB),
                        "fctidu", "$frD, $frB", IIC_FPGeneral,
                        []>, isPPC64;
defm FCTIDZ : XForm_26r<63, 815, (outs f8rc:$frD), (ins f8rc:$frB),
                        "fctidz", "$frD, $frB", IIC_FPGeneral,
                        [(set f64:$frD, (PPCfctidz f64:$frB))]>, isPPC64;

defm FCFIDU  : XForm_26r<63, 974, (outs f8rc:$frD), (ins f8rc:$frB),
                        "fcfidu", "$frD, $frB", IIC_FPGeneral,
                        [(set f64:$frD, (PPCfcfidu f64:$frB))]>, isPPC64;
defm FCFIDS  : XForm_26r<59, 846, (outs f4rc:$frD), (ins f8rc:$frB),
                        "fcfids", "$frD, $frB", IIC_FPGeneral,
                        [(set f32:$frD, (PPCfcfids f64:$frB))]>, isPPC64;
defm FCFIDUS : XForm_26r<59, 974, (outs f4rc:$frD), (ins f8rc:$frB),
                        "fcfidus", "$frD, $frB", IIC_FPGeneral,
                        [(set f32:$frD, (PPCfcfidus f64:$frB))]>, isPPC64;
defm FCTIDUZ : XForm_26r<63, 943, (outs f8rc:$frD), (ins f8rc:$frB),
                        "fctiduz", "$frD, $frB", IIC_FPGeneral,
                        [(set f64:$frD, (PPCfctiduz f64:$frB))]>, isPPC64;
defm FCTIWUZ : XForm_26r<63, 143, (outs f8rc:$frD), (ins f8rc:$frB),
                        "fctiwuz", "$frD, $frB", IIC_FPGeneral,
                        [(set f64:$frD, (PPCfctiwuz f64:$frB))]>, isPPC64;
}


//===----------------------------------------------------------------------===//
// Instruction Patterns
//

// Extensions and truncates to/from 32-bit regs.
def : Pat<(i64 (zext i32:$in)),
          (RLDICL (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $in, sub_32),
                  0, 32)>;
def : Pat<(i64 (anyext i32:$in)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $in, sub_32)>;
def : Pat<(i32 (trunc i64:$in)),
          (EXTRACT_SUBREG $in, sub_32)>;

// Implement the 'not' operation with the NOR instruction.
// (we could use the default xori pattern, but nor has lower latency on some
// cores (such as the A2)).
def i64not : OutPatFrag<(ops node:$in),
                        (NOR8 $in, $in)>;
def        : Pat<(not i64:$in),
                 (i64not $in)>;

// Extending loads with i64 targets.
def : Pat<(zextloadi1 iaddr:$src),
          (LBZ8 iaddr:$src)>;
def : Pat<(zextloadi1 xaddr:$src),
          (LBZX8 xaddr:$src)>;
def : Pat<(extloadi1 iaddr:$src),
          (LBZ8 iaddr:$src)>;
def : Pat<(extloadi1 xaddr:$src),
          (LBZX8 xaddr:$src)>;
def : Pat<(extloadi8 iaddr:$src),
          (LBZ8 iaddr:$src)>;
def : Pat<(extloadi8 xaddr:$src),
          (LBZX8 xaddr:$src)>;
def : Pat<(extloadi16 iaddr:$src),
          (LHZ8 iaddr:$src)>;
def : Pat<(extloadi16 xaddr:$src),
          (LHZX8 xaddr:$src)>;
def : Pat<(extloadi32 iaddr:$src),
          (LWZ8 iaddr:$src)>;
def : Pat<(extloadi32 xaddr:$src),
          (LWZX8 xaddr:$src)>;

// Standard shifts.  These are represented separately from the real shifts above
// so that we can distinguish between shifts that allow 6-bit and 7-bit shift
// amounts.
def : Pat<(sra i64:$rS, i32:$rB),
          (SRAD $rS, $rB)>;
def : Pat<(srl i64:$rS, i32:$rB),
          (SRD $rS, $rB)>;
def : Pat<(shl i64:$rS, i32:$rB),
          (SLD $rS, $rB)>;

// SUBFIC
def : Pat<(sub imm64SExt16:$imm, i64:$in),
          (SUBFIC8 $in, imm:$imm)>;

// SHL/SRL
def : Pat<(shl i64:$in, (i32 imm:$imm)),
          (RLDICR $in, imm:$imm, (SHL64 imm:$imm))>;
def : Pat<(srl i64:$in, (i32 imm:$imm)),
          (RLDICL $in, (SRL64 imm:$imm), imm:$imm)>;

// ROTL
def : Pat<(rotl i64:$in, i32:$sh),
          (RLDCL $in, $sh, 0)>;
def : Pat<(rotl i64:$in, (i32 imm:$imm)),
          (RLDICL $in, imm:$imm, 0)>;

// Hi and Lo for Darwin Global Addresses.
def : Pat<(PPChi tglobaladdr:$in, 0), (LIS8 tglobaladdr:$in)>;
def : Pat<(PPClo tglobaladdr:$in, 0), (LI8  tglobaladdr:$in)>;
def : Pat<(PPChi tconstpool:$in , 0), (LIS8 tconstpool:$in)>;
def : Pat<(PPClo tconstpool:$in , 0), (LI8  tconstpool:$in)>;
def : Pat<(PPChi tjumptable:$in , 0), (LIS8 tjumptable:$in)>;
def : Pat<(PPClo tjumptable:$in , 0), (LI8  tjumptable:$in)>;
def : Pat<(PPChi tblockaddress:$in, 0), (LIS8 tblockaddress:$in)>;
def : Pat<(PPClo tblockaddress:$in, 0), (LI8  tblockaddress:$in)>;
def : Pat<(PPChi tglobaltlsaddr:$g, i64:$in),
          (ADDIS8 $in, tglobaltlsaddr:$g)>;
def : Pat<(PPClo tglobaltlsaddr:$g, i64:$in),
          (ADDI8 $in, tglobaltlsaddr:$g)>;
def : Pat<(add i64:$in, (PPChi tglobaladdr:$g, 0)),
          (ADDIS8 $in, tglobaladdr:$g)>;
def : Pat<(add i64:$in, (PPChi tconstpool:$g, 0)),
          (ADDIS8 $in, tconstpool:$g)>;
def : Pat<(add i64:$in, (PPChi tjumptable:$g, 0)),
          (ADDIS8 $in, tjumptable:$g)>;
def : Pat<(add i64:$in, (PPChi tblockaddress:$g, 0)),
          (ADDIS8 $in, tblockaddress:$g)>;

// Patterns to match r+r indexed loads and stores for
// addresses without at least 4-byte alignment.
def : Pat<(i64 (unaligned4sextloadi32 xoaddr:$src)),
          (LWAX xoaddr:$src)>;
def : Pat<(i64 (unaligned4load xoaddr:$src)),
          (LDX xoaddr:$src)>;
def : Pat<(unaligned4store i64:$rS, xoaddr:$dst),
          (STDX $rS, xoaddr:$dst)>;

// 64-bits atomic loads and stores
def : Pat<(atomic_load_64 ixaddr:$src), (LD  memrix:$src)>;
def : Pat<(atomic_load_64 xaddr:$src),  (LDX memrr:$src)>;

def : Pat<(atomic_store_64 ixaddr:$ptr, i64:$val), (STD  g8rc:$val, memrix:$ptr)>;
def : Pat<(atomic_store_64 xaddr:$ptr,  i64:$val), (STDX g8rc:$val, memrr:$ptr)>;

let Predicates = [IsISA3_0] in {

class X_L1_RA5_RB5<bits<6> opcode, bits<10> xo, string opc, RegisterOperand ty,
                   InstrItinClass itin, list<dag> pattern>
  : X_L1_RS5_RS5<opcode, xo, (outs), (ins ty:$rA, ty:$rB, u1imm:$L),
                 !strconcat(opc, " $rA, $rB, $L"), itin, pattern>;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
def CP_COPY8   : X_L1_RA5_RB5<31, 774, "copy"  , g8rc, IIC_LdStCOPY, []>;
def CP_PASTE8  : X_L1_RA5_RB5<31, 902, "paste" , g8rc, IIC_LdStPASTE, []>;
def CP_PASTE8o : X_L1_RA5_RB5<31, 902, "paste.", g8rc, IIC_LdStPASTE, []>,isDOT;
}

// SLB Invalidate Entry Global
def SLBIEG : XForm_26<31, 466, (outs), (ins gprc:$RS, gprc:$RB),
                      "slbieg $RS, $RB", IIC_SprSLBIEG, []>;
// SLB Synchronize
def SLBSYNC : XForm_0<31, 338, (outs), (ins), "slbsync", IIC_SprSLBSYNC, []>;

} // IsISA3_0
