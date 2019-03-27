//===-- SIInstructions.td - SI Instruction Defintions ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file was originally auto-generated from a GPU register header file and
// all the instruction definitions were originally commented out.  Instructions
// that are not yet supported remain commented out.
//===----------------------------------------------------------------------===//

class GCNPat<dag pattern, dag result> : Pat<pattern, result>, GCNPredicateControl {
  let SubtargetPredicate = isGCN;
}

include "SOPInstructions.td"
include "VOPInstructions.td"
include "SMInstructions.td"
include "FLATInstructions.td"
include "BUFInstructions.td"

//===----------------------------------------------------------------------===//
// EXP Instructions
//===----------------------------------------------------------------------===//

defm EXP : EXP_m<0, AMDGPUexport>;
defm EXP_DONE : EXP_m<1, AMDGPUexport_done>;

//===----------------------------------------------------------------------===//
// VINTRP Instructions
//===----------------------------------------------------------------------===//

// Used to inject printing of "_e32" suffix for VI (there are "_e64" variants for VI)
def VINTRPDst : VINTRPDstOperand <VGPR_32>;

let Uses = [M0, EXEC] in {

// FIXME: Specify SchedRW for VINTRP insturctions.

multiclass V_INTERP_P1_F32_m : VINTRP_m <
  0x00000000,
  (outs VINTRPDst:$vdst),
  (ins VGPR_32:$vsrc, Attr:$attr, AttrChan:$attrchan),
  "v_interp_p1_f32$vdst, $vsrc, $attr$attrchan",
  [(set f32:$vdst, (AMDGPUinterp_p1 f32:$vsrc, (i32 imm:$attrchan),
                                               (i32 imm:$attr)))]
>;

let OtherPredicates = [has32BankLDS] in {

defm V_INTERP_P1_F32 : V_INTERP_P1_F32_m;

} // End OtherPredicates = [has32BankLDS]

let OtherPredicates = [has16BankLDS], Constraints = "@earlyclobber $vdst", isAsmParserOnly=1 in {

defm V_INTERP_P1_F32_16bank : V_INTERP_P1_F32_m;

} // End OtherPredicates = [has32BankLDS], Constraints = "@earlyclobber $vdst", isAsmParserOnly=1

let DisableEncoding = "$src0", Constraints = "$src0 = $vdst" in {

defm V_INTERP_P2_F32 : VINTRP_m <
  0x00000001,
  (outs VINTRPDst:$vdst),
  (ins VGPR_32:$src0, VGPR_32:$vsrc, Attr:$attr, AttrChan:$attrchan),
  "v_interp_p2_f32$vdst, $vsrc, $attr$attrchan",
  [(set f32:$vdst, (AMDGPUinterp_p2 f32:$src0, f32:$vsrc, (i32 imm:$attrchan),
                                                          (i32 imm:$attr)))]>;

} // End DisableEncoding = "$src0", Constraints = "$src0 = $vdst"

defm V_INTERP_MOV_F32 : VINTRP_m <
  0x00000002,
  (outs VINTRPDst:$vdst),
  (ins InterpSlot:$vsrc, Attr:$attr, AttrChan:$attrchan),
  "v_interp_mov_f32$vdst, $vsrc, $attr$attrchan",
  [(set f32:$vdst, (AMDGPUinterp_mov (i32 imm:$vsrc), (i32 imm:$attrchan),
                                     (i32 imm:$attr)))]>;

} // End Uses = [M0, EXEC]

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//
def ATOMIC_FENCE : SPseudoInstSI<
  (outs), (ins i32imm:$ordering, i32imm:$scope),
  [(atomic_fence (i32 imm:$ordering), (i32 imm:$scope))],
  "ATOMIC_FENCE $ordering, $scope"> {
  let hasSideEffects = 1;
  let maybeAtomic = 1;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Uses = [EXEC] in {

// For use in patterns
def V_CNDMASK_B64_PSEUDO : VOP3Common <(outs VReg_64:$vdst),
  (ins VSrc_b64:$src0, VSrc_b64:$src1, SSrc_b64:$src2), "", []> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let usesCustomInserter = 1;
}

// 64-bit vector move instruction. This is mainly used by the
// SIFoldOperands pass to enable folding of inline immediates.
def V_MOV_B64_PSEUDO : VPseudoInstSI <(outs VReg_64:$vdst),
                                      (ins VSrc_b64:$src0)>;

// Pseudoinstruction for @llvm.amdgcn.wqm. It is turned into a copy after the
// WQM pass processes it.
def WQM : PseudoInstSI <(outs unknown:$vdst), (ins unknown:$src0)>;

// Pseudoinstruction for @llvm.amdgcn.wwm. It is turned into a copy post-RA, so
// that the @earlyclobber is respected. The @earlyclobber is to make sure that
// the instruction that defines $src0 (which is run in WWM) doesn't
// accidentally clobber inactive channels of $vdst.
let Constraints = "@earlyclobber $vdst" in {
def WWM : PseudoInstSI <(outs unknown:$vdst), (ins unknown:$src0)>;
}

} // End let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Uses = [EXEC]

def EXIT_WWM : SPseudoInstSI <(outs SReg_64:$sdst), (ins SReg_64:$src0)> {
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
}

// Invert the exec mask and overwrite the inactive lanes of dst with inactive,
// restoring it after we're done.
def V_SET_INACTIVE_B32 : VPseudoInstSI <(outs VGPR_32:$vdst),
  (ins VGPR_32: $src, VSrc_b32:$inactive),
  [(set i32:$vdst, (int_amdgcn_set_inactive i32:$src, i32:$inactive))]> {
  let Constraints = "$src = $vdst";
}

def V_SET_INACTIVE_B64 : VPseudoInstSI <(outs VReg_64:$vdst),
  (ins VReg_64: $src, VSrc_b64:$inactive),
  [(set i64:$vdst, (int_amdgcn_set_inactive i64:$src, i64:$inactive))]> {
  let Constraints = "$src = $vdst";
}


let usesCustomInserter = 1, Defs = [SCC] in {
def S_ADD_U64_PSEUDO : SPseudoInstSI <
  (outs SReg_64:$vdst), (ins SSrc_b64:$src0, SSrc_b64:$src1),
  [(set SReg_64:$vdst, (add i64:$src0, i64:$src1))]
>;

def S_SUB_U64_PSEUDO : SPseudoInstSI <
  (outs SReg_64:$vdst), (ins SSrc_b64:$src0, SSrc_b64:$src1),
  [(set SReg_64:$vdst, (sub i64:$src0, i64:$src1))]
>;

def S_ADD_U64_CO_PSEUDO : SPseudoInstSI <
  (outs SReg_64:$vdst, VOPDstS64:$sdst), (ins SSrc_b64:$src0, SSrc_b64:$src1)
>;

def S_SUB_U64_CO_PSEUDO : SPseudoInstSI <
  (outs SReg_64:$vdst, VOPDstS64:$sdst), (ins SSrc_b64:$src0, SSrc_b64:$src1)
>;

} // End usesCustomInserter = 1, Defs = [SCC]

let usesCustomInserter = 1 in {
def GET_GROUPSTATICSIZE : SPseudoInstSI <(outs SReg_32:$sdst), (ins),
  [(set SReg_32:$sdst, (int_amdgcn_groupstaticsize))]>;
} // End let usesCustomInserter = 1, SALU = 1

def S_MOV_B64_term : SPseudoInstSI<(outs SReg_64:$dst),
   (ins SSrc_b64:$src0)> {
  let isAsCheapAsAMove = 1;
  let isTerminator = 1;
}

def S_XOR_B64_term : SPseudoInstSI<(outs SReg_64:$dst),
   (ins SSrc_b64:$src0, SSrc_b64:$src1)> {
  let isAsCheapAsAMove = 1;
  let isTerminator = 1;
  let Defs = [SCC];
}

def S_ANDN2_B64_term : SPseudoInstSI<(outs SReg_64:$dst),
   (ins SSrc_b64:$src0, SSrc_b64:$src1)> {
  let isAsCheapAsAMove = 1;
  let isTerminator = 1;
}

def WAVE_BARRIER : SPseudoInstSI<(outs), (ins),
  [(int_amdgcn_wave_barrier)]> {
  let SchedRW = [];
  let hasNoSchedulingInfo = 1;
  let hasSideEffects = 1;
  let mayLoad = 1;
  let mayStore = 1;
  let isBarrier = 1;
  let isConvergent = 1;
  let FixedSize = 1;
  let Size = 0;
}

// SI pseudo instructions. These are used by the CFG structurizer pass
// and should be lowered to ISA instructions prior to codegen.

// Dummy terminator instruction to use after control flow instructions
// replaced with exec mask operations.
def SI_MASK_BRANCH : VPseudoInstSI <
  (outs), (ins brtarget:$target)> {
  let isBranch = 0;
  let isTerminator = 1;
  let isBarrier = 0;
  let SchedRW = [];
  let hasNoSchedulingInfo = 1;
  let FixedSize = 1;
  let Size = 0;
}

let isTerminator = 1 in {

let OtherPredicates = [EnableLateCFGStructurize] in {
 def SI_NON_UNIFORM_BRCOND_PSEUDO : CFPseudoInstSI <
  (outs),
  (ins SReg_64:$vcc, brtarget:$target),
  [(brcond i1:$vcc, bb:$target)]> {
    let Size = 12;
}
}

def SI_IF: CFPseudoInstSI <
  (outs SReg_64:$dst), (ins SReg_64:$vcc, brtarget:$target),
  [(set i64:$dst, (AMDGPUif i1:$vcc, bb:$target))], 1, 1> {
  let Constraints = "";
  let Size = 12;
  let hasSideEffects = 1;
}

def SI_ELSE : CFPseudoInstSI <
  (outs SReg_64:$dst),
  (ins SReg_64:$src, brtarget:$target, i1imm:$execfix), [], 1, 1> {
  let Size = 12;
  let hasSideEffects = 1;
}

def SI_LOOP : CFPseudoInstSI <
  (outs), (ins SReg_64:$saved, brtarget:$target),
  [(AMDGPUloop i64:$saved, bb:$target)], 1, 1> {
  let Size = 8;
  let isBranch = 1;
  let hasSideEffects = 1;
}

} // End isTerminator = 1

def SI_END_CF : CFPseudoInstSI <
  (outs), (ins SReg_64:$saved),
  [(int_amdgcn_end_cf i64:$saved)], 1, 1> {
  let Size = 4;
  let isAsCheapAsAMove = 1;
  let isReMaterializable = 1;
  let hasSideEffects = 1;
  let mayLoad = 1; // FIXME: Should not need memory flags
  let mayStore = 1;
}

def SI_IF_BREAK : CFPseudoInstSI <
  (outs SReg_64:$dst), (ins SReg_64:$vcc, SReg_64:$src),
  [(set i64:$dst, (int_amdgcn_if_break i1:$vcc, i64:$src))]> {
  let Size = 4;
  let isAsCheapAsAMove = 1;
  let isReMaterializable = 1;
}

let Uses = [EXEC] in {

multiclass PseudoInstKill <dag ins> {
  // Even though this pseudo can usually be expanded without an SCC def, we
  // conservatively assume that it has an SCC def, both because it is sometimes
  // required in degenerate cases (when V_CMPX cannot be used due to constant
  // bus limitations) and because it allows us to avoid having to track SCC
  // liveness across basic blocks.
  let Defs = [EXEC,VCC,SCC] in
  def _PSEUDO : PseudoInstSI <(outs), ins> {
    let isConvergent = 1;
    let usesCustomInserter = 1;
  }

  let Defs = [EXEC,VCC,SCC] in
  def _TERMINATOR : SPseudoInstSI <(outs), ins> {
    let isTerminator = 1;
  }
}

defm SI_KILL_I1 : PseudoInstKill <(ins SSrc_b64:$src, i1imm:$killvalue)>;
defm SI_KILL_F32_COND_IMM : PseudoInstKill <(ins VSrc_b32:$src0, i32imm:$src1, i32imm:$cond)>;

let Defs = [EXEC,VCC] in
def SI_ILLEGAL_COPY : SPseudoInstSI <
  (outs unknown:$dst), (ins unknown:$src),
  [], " ; illegal copy $src to $dst">;

} // End Uses = [EXEC], Defs = [EXEC,VCC]

// Branch on undef scc. Used to avoid intermediate copy from
// IMPLICIT_DEF to SCC.
def SI_BR_UNDEF : SPseudoInstSI <(outs), (ins sopp_brtarget:$simm16)> {
  let isTerminator = 1;
  let usesCustomInserter = 1;
  let isBranch = 1;
}

def SI_PS_LIVE : PseudoInstSI <
  (outs SReg_64:$dst), (ins),
  [(set i1:$dst, (int_amdgcn_ps_live))]> {
  let SALU = 1;
}

def SI_MASKED_UNREACHABLE : SPseudoInstSI <(outs), (ins),
  [(int_amdgcn_unreachable)],
  "; divergent unreachable"> {
  let Size = 0;
  let hasNoSchedulingInfo = 1;
  let FixedSize = 1;
}

// Used as an isel pseudo to directly emit initialization with an
// s_mov_b32 rather than a copy of another initialized
// register. MachineCSE skips copies, and we don't want to have to
// fold operands before it runs.
def SI_INIT_M0 : SPseudoInstSI <(outs), (ins SSrc_b32:$src)> {
  let Defs = [M0];
  let usesCustomInserter = 1;
  let isAsCheapAsAMove = 1;
  let isReMaterializable = 1;
}

def SI_INIT_EXEC : SPseudoInstSI <
  (outs), (ins i64imm:$src), []> {
  let Defs = [EXEC];
  let usesCustomInserter = 1;
  let isAsCheapAsAMove = 1;
}

def SI_INIT_EXEC_FROM_INPUT : SPseudoInstSI <
  (outs), (ins SSrc_b32:$input, i32imm:$shift), []> {
  let Defs = [EXEC];
  let usesCustomInserter = 1;
}

// Return for returning shaders to a shader variant epilog.
def SI_RETURN_TO_EPILOG : SPseudoInstSI <
  (outs), (ins variable_ops), [(AMDGPUreturn_to_epilog)]> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isReturn = 1;
  let hasNoSchedulingInfo = 1;
  let DisableWQM = 1;
  let FixedSize = 1;
}

// Return for returning function calls.
def SI_RETURN : SPseudoInstSI <
  (outs), (ins), [],
  "; return"> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isReturn = 1;
  let SchedRW = [WriteBranch];
}

// Return for returning function calls without output register.
//
// This version is only needed so we can fill in the output regiter in
// the custom inserter.
def SI_CALL_ISEL : SPseudoInstSI <
  (outs), (ins SSrc_b64:$src0), [(AMDGPUcall i64:$src0)]> {
  let Size = 4;
  let isCall = 1;
  let SchedRW = [WriteBranch];
  let usesCustomInserter = 1;
}

// Wrapper around s_swappc_b64 with extra $callee parameter to track
// the called function after regalloc.
def SI_CALL : SPseudoInstSI <
  (outs SReg_64:$dst), (ins SSrc_b64:$src0, unknown:$callee)> {
  let Size = 4;
  let isCall = 1;
  let UseNamedOperandTable = 1;
  let SchedRW = [WriteBranch];
}

// Tail call handling pseudo
def SI_TCRETURN_ISEL : SPseudoInstSI<(outs),
  (ins SSrc_b64:$src0, i32imm:$fpdiff),
  [(AMDGPUtc_return i64:$src0, i32:$fpdiff)]> {
  let isCall = 1;
  let isTerminator = 1;
  let isReturn = 1;
  let isBarrier = 1;
  let SchedRW = [WriteBranch];
  let usesCustomInserter = 1;
}

def SI_TCRETURN : SPseudoInstSI <
  (outs),
  (ins SSrc_b64:$src0, unknown:$callee, i32imm:$fpdiff)> {
  let Size = 4;
  let isCall = 1;
  let isTerminator = 1;
  let isReturn = 1;
  let isBarrier = 1;
  let UseNamedOperandTable = 1;
  let SchedRW = [WriteBranch];
}


def ADJCALLSTACKUP : SPseudoInstSI<
  (outs), (ins i32imm:$amt0, i32imm:$amt1),
  [(callseq_start timm:$amt0, timm:$amt1)],
  "; adjcallstackup $amt0 $amt1"> {
  let Size = 8; // Worst case. (s_add_u32 + constant)
  let FixedSize = 1;
  let hasSideEffects = 1;
  let usesCustomInserter = 1;
}

def ADJCALLSTACKDOWN : SPseudoInstSI<
  (outs), (ins i32imm:$amt1, i32imm:$amt2),
  [(callseq_end timm:$amt1, timm:$amt2)],
  "; adjcallstackdown $amt1"> {
  let Size = 8; // Worst case. (s_add_u32 + constant)
  let hasSideEffects = 1;
  let usesCustomInserter = 1;
}

let Defs = [M0, EXEC, SCC],
  UseNamedOperandTable = 1 in {

class SI_INDIRECT_SRC<RegisterClass rc> : VPseudoInstSI <
  (outs VGPR_32:$vdst),
  (ins rc:$src, VS_32:$idx, i32imm:$offset)> {
  let usesCustomInserter = 1;
}

class SI_INDIRECT_DST<RegisterClass rc> : VPseudoInstSI <
  (outs rc:$vdst),
  (ins rc:$src, VS_32:$idx, i32imm:$offset, VGPR_32:$val)> {
  let Constraints = "$src = $vdst";
  let usesCustomInserter = 1;
}

// TODO: We can support indirect SGPR access.
def SI_INDIRECT_SRC_V1 : SI_INDIRECT_SRC<VGPR_32>;
def SI_INDIRECT_SRC_V2 : SI_INDIRECT_SRC<VReg_64>;
def SI_INDIRECT_SRC_V4 : SI_INDIRECT_SRC<VReg_128>;
def SI_INDIRECT_SRC_V8 : SI_INDIRECT_SRC<VReg_256>;
def SI_INDIRECT_SRC_V16 : SI_INDIRECT_SRC<VReg_512>;

def SI_INDIRECT_DST_V1 : SI_INDIRECT_DST<VGPR_32>;
def SI_INDIRECT_DST_V2 : SI_INDIRECT_DST<VReg_64>;
def SI_INDIRECT_DST_V4 : SI_INDIRECT_DST<VReg_128>;
def SI_INDIRECT_DST_V8 : SI_INDIRECT_DST<VReg_256>;
def SI_INDIRECT_DST_V16 : SI_INDIRECT_DST<VReg_512>;

} // End Uses = [EXEC], Defs = [M0, EXEC]

multiclass SI_SPILL_SGPR <RegisterClass sgpr_class> {
  let UseNamedOperandTable = 1, SGPRSpill = 1, Uses = [EXEC] in {
    def _SAVE : PseudoInstSI <
      (outs),
      (ins sgpr_class:$data, i32imm:$addr)> {
      let mayStore = 1;
      let mayLoad = 0;
    }

    def _RESTORE : PseudoInstSI <
      (outs sgpr_class:$data),
      (ins i32imm:$addr)> {
      let mayStore = 0;
      let mayLoad = 1;
    }
  } // End UseNamedOperandTable = 1
}

// You cannot use M0 as the output of v_readlane_b32 instructions or
// use it in the sdata operand of SMEM instructions. We still need to
// be able to spill the physical register m0, so allow it for
// SI_SPILL_32_* instructions.
defm SI_SPILL_S32  : SI_SPILL_SGPR <SReg_32>;
defm SI_SPILL_S64  : SI_SPILL_SGPR <SReg_64>;
defm SI_SPILL_S128 : SI_SPILL_SGPR <SReg_128>;
defm SI_SPILL_S256 : SI_SPILL_SGPR <SReg_256>;
defm SI_SPILL_S512 : SI_SPILL_SGPR <SReg_512>;

multiclass SI_SPILL_VGPR <RegisterClass vgpr_class> {
  let UseNamedOperandTable = 1, VGPRSpill = 1,
       SchedRW = [WriteVMEM] in {
    def _SAVE : VPseudoInstSI <
      (outs),
      (ins vgpr_class:$vdata, i32imm:$vaddr, SReg_128:$srsrc,
           SReg_32:$soffset, i32imm:$offset)> {
      let mayStore = 1;
      let mayLoad = 0;
      // (2 * 4) + (8 * num_subregs) bytes maximum
      let Size = !add(!shl(!srl(vgpr_class.Size, 5), 3), 8);
    }

    def _RESTORE : VPseudoInstSI <
      (outs vgpr_class:$vdata),
      (ins i32imm:$vaddr, SReg_128:$srsrc, SReg_32:$soffset,
           i32imm:$offset)> {
      let mayStore = 0;
      let mayLoad = 1;

      // (2 * 4) + (8 * num_subregs) bytes maximum
      let Size = !add(!shl(!srl(vgpr_class.Size, 5), 3), 8);
    }
  } // End UseNamedOperandTable = 1, VGPRSpill = 1, SchedRW = [WriteVMEM]
}

defm SI_SPILL_V32  : SI_SPILL_VGPR <VGPR_32>;
defm SI_SPILL_V64  : SI_SPILL_VGPR <VReg_64>;
defm SI_SPILL_V96  : SI_SPILL_VGPR <VReg_96>;
defm SI_SPILL_V128 : SI_SPILL_VGPR <VReg_128>;
defm SI_SPILL_V256 : SI_SPILL_VGPR <VReg_256>;
defm SI_SPILL_V512 : SI_SPILL_VGPR <VReg_512>;

def SI_PC_ADD_REL_OFFSET : SPseudoInstSI <
  (outs SReg_64:$dst),
  (ins si_ga:$ptr_lo, si_ga:$ptr_hi),
  [(set SReg_64:$dst,
   (i64 (SIpc_add_rel_offset (tglobaladdr:$ptr_lo), (tglobaladdr:$ptr_hi))))]> {
  let Defs = [SCC];
}

def : GCNPat <
  (AMDGPUinit_exec i64:$src),
  (SI_INIT_EXEC (as_i64imm $src))
>;

def : GCNPat <
  (AMDGPUinit_exec_from_input i32:$input, i32:$shift),
  (SI_INIT_EXEC_FROM_INPUT (i32 $input), (as_i32imm $shift))
>;

def : GCNPat<
  (AMDGPUtrap timm:$trapid),
  (S_TRAP $trapid)
>;

def : GCNPat<
  (AMDGPUelse i64:$src, bb:$target),
  (SI_ELSE $src, $target, 0)
>;

def : Pat <
  // -1.0 as i32 (LowerINTRINSIC_VOID converts all other constants to -1.0)
  (AMDGPUkill (i32 -1082130432)),
  (SI_KILL_I1_PSEUDO (i1 0), 0)
>;

def : Pat <
  (int_amdgcn_kill i1:$src),
  (SI_KILL_I1_PSEUDO $src, 0)
>;

def : Pat <
  (int_amdgcn_kill (i1 (not i1:$src))),
  (SI_KILL_I1_PSEUDO $src, -1)
>;

def : Pat <
  (AMDGPUkill i32:$src),
  (SI_KILL_F32_COND_IMM_PSEUDO $src, 0, 3) // 3 means SETOGE
>;

def : Pat <
  (int_amdgcn_kill (i1 (setcc f32:$src, InlineFPImm<f32>:$imm, cond:$cond))),
  (SI_KILL_F32_COND_IMM_PSEUDO $src, (bitcast_fpimm_to_i32 $imm), (cond_as_i32imm $cond))
>;

  // TODO: we could add more variants for other types of conditionals

def : Pat <
  (int_amdgcn_icmp i1:$src, (i1 0), (i32 33)),
  (COPY $src) // Return the SGPRs representing i1 src
>;

//===----------------------------------------------------------------------===//
// VOP1 Patterns
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGCN, OtherPredicates = [UnsafeFPMath] in {

//def : RcpPat<V_RCP_F64_e32, f64>;
//defm : RsqPat<V_RSQ_F64_e32, f64>;
//defm : RsqPat<V_RSQ_F32_e32, f32>;

def : RsqPat<V_RSQ_F32_e32, f32>;
def : RsqPat<V_RSQ_F64_e32, f64>;

// Convert (x - floor(x)) to fract(x)
def : GCNPat <
  (f32 (fsub (f32 (VOP3Mods f32:$x, i32:$mods)),
             (f32 (ffloor (f32 (VOP3Mods f32:$x, i32:$mods)))))),
  (V_FRACT_F32_e64 $mods, $x, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

// Convert (x + (-floor(x))) to fract(x)
def : GCNPat <
  (f64 (fadd (f64 (VOP3Mods f64:$x, i32:$mods)),
             (f64 (fneg (f64 (ffloor (f64 (VOP3Mods f64:$x, i32:$mods)))))))),
  (V_FRACT_F64_e64 $mods, $x, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

} // End SubtargetPredicate = isGCN, OtherPredicates = [UnsafeFPMath]


// f16_to_fp patterns
def : GCNPat <
  (f32 (f16_to_fp i32:$src0)),
  (V_CVT_F32_F16_e64 SRCMODS.NONE, $src0, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

def : GCNPat <
  (f32 (f16_to_fp (and_oneuse i32:$src0, 0x7fff))),
  (V_CVT_F32_F16_e64 SRCMODS.ABS, $src0, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

def : GCNPat <
  (f32 (f16_to_fp (i32 (srl_oneuse (and_oneuse i32:$src0, 0x7fff0000), (i32 16))))),
  (V_CVT_F32_F16_e64 SRCMODS.ABS, (i32 (V_LSHRREV_B32_e64 (i32 16), i32:$src0)), DSTCLAMP.NONE, DSTOMOD.NONE)
>;

def : GCNPat <
  (f32 (f16_to_fp (or_oneuse i32:$src0, 0x8000))),
  (V_CVT_F32_F16_e64 SRCMODS.NEG_ABS, $src0, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

def : GCNPat <
  (f32 (f16_to_fp (xor_oneuse i32:$src0, 0x8000))),
  (V_CVT_F32_F16_e64 SRCMODS.NEG, $src0, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

def : GCNPat <
  (f64 (fpextend f16:$src)),
  (V_CVT_F64_F32_e32 (V_CVT_F32_F16_e32 $src))
>;

// fp_to_fp16 patterns
def : GCNPat <
  (i32 (AMDGPUfp_to_f16 (f32 (VOP3Mods f32:$src0, i32:$src0_modifiers)))),
  (V_CVT_F16_F32_e64 $src0_modifiers, f32:$src0, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

def : GCNPat <
  (i32 (fp_to_sint f16:$src)),
  (V_CVT_I32_F32_e32 (V_CVT_F32_F16_e32 $src))
>;

def : GCNPat <
  (i32 (fp_to_uint f16:$src)),
  (V_CVT_U32_F32_e32 (V_CVT_F32_F16_e32 $src))
>;

def : GCNPat <
  (f16 (sint_to_fp i32:$src)),
  (V_CVT_F16_F32_e32 (V_CVT_F32_I32_e32 $src))
>;

def : GCNPat <
  (f16 (uint_to_fp i32:$src)),
  (V_CVT_F16_F32_e32 (V_CVT_F32_U32_e32 $src))
>;

//===----------------------------------------------------------------------===//
// VOP2 Patterns
//===----------------------------------------------------------------------===//

multiclass FMADPat <ValueType vt, Instruction inst> {
  def : GCNPat <
    (vt (fmad (VOP3NoMods vt:$src0),
              (VOP3NoMods vt:$src1),
              (VOP3NoMods vt:$src2))),
    (inst SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
          SRCMODS.NONE, $src2, DSTCLAMP.NONE, DSTOMOD.NONE)
  >;
}

defm : FMADPat <f16, V_MAC_F16_e64>;
defm : FMADPat <f32, V_MAC_F32_e64>;

class FMADModsPat<Instruction inst, SDPatternOperator mad_opr, ValueType Ty>
  : GCNPat<
  (Ty (mad_opr (VOP3Mods Ty:$src0, i32:$src0_mod),
  (VOP3Mods Ty:$src1, i32:$src1_mod),
  (VOP3Mods Ty:$src2, i32:$src2_mod))),
  (inst $src0_mod, $src0, $src1_mod, $src1,
  $src2_mod, $src2, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

def : FMADModsPat<V_MAD_F32, AMDGPUfmad_ftz, f32>;
def : FMADModsPat<V_MAD_F16, AMDGPUfmad_ftz, f16> {
  let SubtargetPredicate = Has16BitInsts;
}

multiclass SelectPat <ValueType vt, Instruction inst> {
  def : GCNPat <
    (vt (select i1:$src0, vt:$src1, vt:$src2)),
    (inst $src2, $src1, $src0)
  >;
}

defm : SelectPat <i16, V_CNDMASK_B32_e64>;
defm : SelectPat <i32, V_CNDMASK_B32_e64>;
defm : SelectPat <f16, V_CNDMASK_B32_e64>;
defm : SelectPat <f32, V_CNDMASK_B32_e64>;

let AddedComplexity = 1 in {
def : GCNPat <
  (i32 (add (i32 (getDivergentFrag<ctpop>.ret i32:$popcnt)), i32:$val)),
  (V_BCNT_U32_B32_e64 $popcnt, $val)
>;
}
def : GCNPat <
  (i16 (add (i16 (trunc (getDivergentFrag<ctpop>.ret i32:$popcnt))), i16:$val)),
  (V_BCNT_U32_B32_e64 $popcnt, $val)
>;

/********** ============================================ **********/
/********** Extraction, Insertion, Building and Casting  **********/
/********** ============================================ **********/

foreach Index = 0-2 in {
  def Extract_Element_v2i32_#Index : Extract_Element <
    i32, v2i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v2i32_#Index : Insert_Element <
    i32, v2i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v2f32_#Index : Extract_Element <
    f32, v2f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v2f32_#Index : Insert_Element <
    f32, v2f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-3 in {
  def Extract_Element_v4i32_#Index : Extract_Element <
    i32, v4i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v4i32_#Index : Insert_Element <
    i32, v4i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v4f32_#Index : Extract_Element <
    f32, v4f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v4f32_#Index : Insert_Element <
    f32, v4f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-7 in {
  def Extract_Element_v8i32_#Index : Extract_Element <
    i32, v8i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v8i32_#Index : Insert_Element <
    i32, v8i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v8f32_#Index : Extract_Element <
    f32, v8f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v8f32_#Index : Insert_Element <
    f32, v8f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-15 in {
  def Extract_Element_v16i32_#Index : Extract_Element <
    i32, v16i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v16i32_#Index : Insert_Element <
    i32, v16i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v16f32_#Index : Extract_Element <
    f32, v16f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v16f32_#Index : Insert_Element <
    f32, v16f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}


def : Pat <
  (extract_subvector v4i16:$vec, (i32 0)),
  (v2i16 (EXTRACT_SUBREG v4i16:$vec, sub0))
>;

def : Pat <
  (extract_subvector v4i16:$vec, (i32 2)),
  (v2i16 (EXTRACT_SUBREG v4i16:$vec, sub1))
>;

def : Pat <
  (extract_subvector v4f16:$vec, (i32 0)),
  (v2f16 (EXTRACT_SUBREG v4f16:$vec, sub0))
>;

def : Pat <
  (extract_subvector v4f16:$vec, (i32 2)),
  (v2f16 (EXTRACT_SUBREG v4f16:$vec, sub1))
>;

let SubtargetPredicate = isGCN in {

// FIXME: Why do only some of these type combinations for SReg and
// VReg?
// 16-bit bitcast
def : BitConvert <i16, f16, VGPR_32>;
def : BitConvert <f16, i16, VGPR_32>;
def : BitConvert <i16, f16, SReg_32>;
def : BitConvert <f16, i16, SReg_32>;

// 32-bit bitcast
def : BitConvert <i32, f32, VGPR_32>;
def : BitConvert <f32, i32, VGPR_32>;
def : BitConvert <i32, f32, SReg_32>;
def : BitConvert <f32, i32, SReg_32>;
def : BitConvert <v2i16, i32, SReg_32>;
def : BitConvert <i32, v2i16, SReg_32>;
def : BitConvert <v2f16, i32, SReg_32>;
def : BitConvert <i32, v2f16, SReg_32>;
def : BitConvert <v2i16, v2f16, SReg_32>;
def : BitConvert <v2f16, v2i16, SReg_32>;
def : BitConvert <v2f16, f32, SReg_32>;
def : BitConvert <f32, v2f16, SReg_32>;
def : BitConvert <v2i16, f32, SReg_32>;
def : BitConvert <f32, v2i16, SReg_32>;

// 64-bit bitcast
def : BitConvert <i64, f64, VReg_64>;
def : BitConvert <f64, i64, VReg_64>;
def : BitConvert <v2i32, v2f32, VReg_64>;
def : BitConvert <v2f32, v2i32, VReg_64>;
def : BitConvert <i64, v2i32, VReg_64>;
def : BitConvert <v2i32, i64, VReg_64>;
def : BitConvert <i64, v2f32, VReg_64>;
def : BitConvert <v2f32, i64, VReg_64>;
def : BitConvert <f64, v2f32, VReg_64>;
def : BitConvert <v2f32, f64, VReg_64>;
def : BitConvert <f64, v2i32, VReg_64>;
def : BitConvert <v2i32, f64, VReg_64>;
def : BitConvert <v4i16, v4f16, VReg_64>;
def : BitConvert <v4f16, v4i16, VReg_64>;

// FIXME: Make SGPR
def : BitConvert <v2i32, v4f16, VReg_64>;
def : BitConvert <v4f16, v2i32, VReg_64>;
def : BitConvert <v2i32, v4f16, VReg_64>;
def : BitConvert <v2i32, v4i16, VReg_64>;
def : BitConvert <v4i16, v2i32, VReg_64>;
def : BitConvert <v2f32, v4f16, VReg_64>;
def : BitConvert <v4f16, v2f32, VReg_64>;
def : BitConvert <v2f32, v4i16, VReg_64>;
def : BitConvert <v4i16, v2f32, VReg_64>;
def : BitConvert <v4i16, f64, VReg_64>;
def : BitConvert <v4f16, f64, VReg_64>;
def : BitConvert <f64, v4i16, VReg_64>;
def : BitConvert <f64, v4f16, VReg_64>;
def : BitConvert <v4i16, i64, VReg_64>;
def : BitConvert <v4f16, i64, VReg_64>;
def : BitConvert <i64, v4i16, VReg_64>;
def : BitConvert <i64, v4f16, VReg_64>;

def : BitConvert <v4i32, v4f32, VReg_128>;
def : BitConvert <v4f32, v4i32, VReg_128>;

// 128-bit bitcast
def : BitConvert <v2i64, v4i32, SReg_128>;
def : BitConvert <v4i32, v2i64, SReg_128>;
def : BitConvert <v2f64, v4f32, VReg_128>;
def : BitConvert <v2f64, v4i32, VReg_128>;
def : BitConvert <v4f32, v2f64, VReg_128>;
def : BitConvert <v4i32, v2f64, VReg_128>;
def : BitConvert <v2i64, v2f64, VReg_128>;
def : BitConvert <v2f64, v2i64, VReg_128>;

// 256-bit bitcast
def : BitConvert <v8i32, v8f32, SReg_256>;
def : BitConvert <v8f32, v8i32, SReg_256>;
def : BitConvert <v8i32, v8f32, VReg_256>;
def : BitConvert <v8f32, v8i32, VReg_256>;

// 512-bit bitcast
def : BitConvert <v16i32, v16f32, VReg_512>;
def : BitConvert <v16f32, v16i32, VReg_512>;

} // End SubtargetPredicate = isGCN

/********** =================== **********/
/********** Src & Dst modifiers **********/
/********** =================== **********/


// If denormals are not enabled, it only impacts the compare of the
// inputs. The output result is not flushed.
class ClampPat<Instruction inst, ValueType vt> : GCNPat <
  (vt (AMDGPUclamp (VOP3Mods vt:$src0, i32:$src0_modifiers))),
  (inst i32:$src0_modifiers, vt:$src0,
        i32:$src0_modifiers, vt:$src0, DSTCLAMP.ENABLE, DSTOMOD.NONE)
>;

def : ClampPat<V_MAX_F32_e64, f32>;
def : ClampPat<V_MAX_F64, f64>;
def : ClampPat<V_MAX_F16_e64, f16>;

let SubtargetPredicate = HasVOP3PInsts in {
def : GCNPat <
  (v2f16 (AMDGPUclamp (VOP3PMods v2f16:$src0, i32:$src0_modifiers))),
  (V_PK_MAX_F16 $src0_modifiers, $src0,
                $src0_modifiers, $src0, DSTCLAMP.ENABLE)
>;
}

/********** ================================ **********/
/********** Floating point absolute/negative **********/
/********** ================================ **********/

// Prevent expanding both fneg and fabs.

def : GCNPat <
  (fneg (fabs f32:$src)),
  (S_OR_B32 $src, (S_MOV_B32(i32 0x80000000))) // Set sign bit
>;

// FIXME: Should use S_OR_B32
def : GCNPat <
  (fneg (fabs f64:$src)),
  (REG_SEQUENCE VReg_64,
    (i32 (EXTRACT_SUBREG f64:$src, sub0)),
    sub0,
    (V_OR_B32_e32 (i32 (EXTRACT_SUBREG f64:$src, sub1)),
                  (V_MOV_B32_e32 (i32 0x80000000))), // Set sign bit.
    sub1)
>;

def : GCNPat <
  (fabs f32:$src),
  (S_AND_B32 $src, (S_MOV_B32 (i32 0x7fffffff)))
>;

def : GCNPat <
  (fneg f32:$src),
  (V_XOR_B32_e32 $src, (V_MOV_B32_e32 (i32 0x80000000)))
>;

def : GCNPat <
  (fabs f64:$src),
  (REG_SEQUENCE VReg_64,
    (i32 (EXTRACT_SUBREG f64:$src, sub0)),
    sub0,
    (V_AND_B32_e64 (i32 (EXTRACT_SUBREG f64:$src, sub1)),
                   (V_MOV_B32_e32 (i32 0x7fffffff))), // Set sign bit.
     sub1)
>;

def : GCNPat <
  (fneg f64:$src),
  (REG_SEQUENCE VReg_64,
    (i32 (EXTRACT_SUBREG f64:$src, sub0)),
    sub0,
    (V_XOR_B32_e32 (i32 (EXTRACT_SUBREG f64:$src, sub1)),
                   (i32 (V_MOV_B32_e32 (i32 0x80000000)))),
    sub1)
>;

def : GCNPat <
  (fcopysign f16:$src0, f16:$src1),
  (V_BFI_B32 (S_MOV_B32 (i32 0x00007fff)), $src0, $src1)
>;

def : GCNPat <
  (fcopysign f32:$src0, f16:$src1),
  (V_BFI_B32 (S_MOV_B32 (i32 0x7fffffff)), $src0,
             (V_LSHLREV_B32_e64 (i32 16), $src1))
>;

def : GCNPat <
  (fcopysign f64:$src0, f16:$src1),
  (REG_SEQUENCE SReg_64,
    (i32 (EXTRACT_SUBREG $src0, sub0)), sub0,
    (V_BFI_B32 (S_MOV_B32 (i32 0x7fffffff)), (i32 (EXTRACT_SUBREG $src0, sub1)),
               (V_LSHLREV_B32_e64 (i32 16), $src1)), sub1)
>;

def : GCNPat <
  (fcopysign f16:$src0, f32:$src1),
  (V_BFI_B32 (S_MOV_B32 (i32 0x00007fff)), $src0,
             (V_LSHRREV_B32_e64 (i32 16), $src1))
>;

def : GCNPat <
  (fcopysign f16:$src0, f64:$src1),
  (V_BFI_B32 (S_MOV_B32 (i32 0x00007fff)), $src0,
             (V_LSHRREV_B32_e64 (i32 16), (EXTRACT_SUBREG $src1, sub1)))
>;

def : GCNPat <
  (fneg f16:$src),
  (S_XOR_B32 $src, (S_MOV_B32 (i32 0x00008000)))
>;

def : GCNPat <
  (fabs f16:$src),
  (S_AND_B32 $src, (S_MOV_B32 (i32 0x00007fff)))
>;

def : GCNPat <
  (fneg (fabs f16:$src)),
  (S_OR_B32 $src, (S_MOV_B32 (i32 0x00008000))) // Set sign bit
>;

def : GCNPat <
  (fneg v2f16:$src),
  (S_XOR_B32 $src, (S_MOV_B32 (i32 0x80008000)))
>;

def : GCNPat <
  (fabs v2f16:$src),
  (S_AND_B32 $src, (S_MOV_B32 (i32 0x7fff7fff)))
>;

// This is really (fneg (fabs v2f16:$src))
//
// fabs is not reported as free because there is modifier for it in
// VOP3P instructions, so it is turned into the bit op.
def : GCNPat <
  (fneg (v2f16 (bitconvert (and_oneuse i32:$src, 0x7fff7fff)))),
  (S_OR_B32 $src, (S_MOV_B32 (i32 0x80008000))) // Set sign bit
>;

def : GCNPat <
  (fneg (v2f16 (fabs v2f16:$src))),
  (S_OR_B32 $src, (S_MOV_B32 (i32 0x80008000))) // Set sign bit
>;

/********** ================== **********/
/********** Immediate Patterns **********/
/********** ================== **********/

def : GCNPat <
  (VGPRImm<(i32 imm)>:$imm),
  (V_MOV_B32_e32 imm:$imm)
>;

def : GCNPat <
  (VGPRImm<(f32 fpimm)>:$imm),
  (V_MOV_B32_e32 (f32 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (i32 imm:$imm),
  (S_MOV_B32 imm:$imm)
>;

// FIXME: Workaround for ordering issue with peephole optimizer where
// a register class copy interferes with immediate folding.  Should
// use s_mov_b32, which can be shrunk to s_movk_i32
def : GCNPat <
  (VGPRImm<(f16 fpimm)>:$imm),
  (V_MOV_B32_e32 (f16 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (f32 fpimm:$imm),
  (S_MOV_B32 (f32 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (f16 fpimm:$imm),
  (S_MOV_B32 (i32 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
 (i32 frameindex:$fi),
 (V_MOV_B32_e32 (i32 (frameindex_to_targetframeindex $fi)))
>;

def : GCNPat <
  (i64 InlineImm<i64>:$imm),
  (S_MOV_B64 InlineImm<i64>:$imm)
>;

// XXX - Should this use a s_cmp to set SCC?

// Set to sign-extended 64-bit value (true = -1, false = 0)
def : GCNPat <
  (i1 imm:$imm),
  (S_MOV_B64 (i64 (as_i64imm $imm)))
>;

def : GCNPat <
  (f64 InlineFPImm<f64>:$imm),
  (S_MOV_B64 (f64 (bitcast_fpimm_to_i64 InlineFPImm<f64>:$imm)))
>;

/********** ================== **********/
/********** Intrinsic Patterns **********/
/********** ================== **********/

let SubtargetPredicate = isGCN in {
def : POW_Common <V_LOG_F32_e32, V_EXP_F32_e32, V_MUL_LEGACY_F32_e32>;
}

def : GCNPat <
  (i32 (sext i1:$src0)),
  (V_CNDMASK_B32_e64 (i32 0), (i32 -1), $src0)
>;

class Ext32Pat <SDNode ext> : GCNPat <
  (i32 (ext i1:$src0)),
  (V_CNDMASK_B32_e64 (i32 0), (i32 1), $src0)
>;

def : Ext32Pat <zext>;
def : Ext32Pat <anyext>;

// The multiplication scales from [0,1] to the unsigned integer range
def : GCNPat <
  (AMDGPUurecip i32:$src0),
  (V_CVT_U32_F32_e32
    (V_MUL_F32_e32 (i32 CONST.FP_UINT_MAX_PLUS_1),
                   (V_RCP_IFLAG_F32_e32 (V_CVT_F32_U32_e32 $src0))))
>;

//===----------------------------------------------------------------------===//
// VOP3 Patterns
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGCN in {

def : IMad24Pat<V_MAD_I32_I24, 1>;
def : UMad24Pat<V_MAD_U32_U24, 1>;

// FIXME: This should only be done for VALU inputs
defm : BFIPatterns <V_BFI_B32, S_MOV_B32, SReg_64>;
def : ROTRPattern <V_ALIGNBIT_B32>;

}

def : GCNPat<(i32 (trunc (srl i64:$src0, (and i32:$src1, (i32 31))))),
          (V_ALIGNBIT_B32 (i32 (EXTRACT_SUBREG (i64 $src0), sub1)),
                          (i32 (EXTRACT_SUBREG (i64 $src0), sub0)), $src1)>;

def : GCNPat<(i32 (trunc (srl i64:$src0, (i32 ShiftAmt32Imm:$src1)))),
          (V_ALIGNBIT_B32 (i32 (EXTRACT_SUBREG (i64 $src0), sub1)),
                          (i32 (EXTRACT_SUBREG (i64 $src0), sub0)), $src1)>;

/********** ====================== **********/
/**********   Indirect addressing  **********/
/********** ====================== **********/

multiclass SI_INDIRECT_Pattern <ValueType vt, ValueType eltvt, string VecSize> {
  // Extract with offset
  def : GCNPat<
    (eltvt (extractelt vt:$src, (MOVRELOffset i32:$idx, (i32 imm:$offset)))),
    (!cast<Instruction>("SI_INDIRECT_SRC_"#VecSize) $src, $idx, imm:$offset)
  >;

  // Insert with offset
  def : GCNPat<
    (insertelt vt:$src, eltvt:$val, (MOVRELOffset i32:$idx, (i32 imm:$offset))),
    (!cast<Instruction>("SI_INDIRECT_DST_"#VecSize) $src, $idx, imm:$offset, $val)
  >;
}

defm : SI_INDIRECT_Pattern <v2f32, f32, "V2">;
defm : SI_INDIRECT_Pattern <v4f32, f32, "V4">;
defm : SI_INDIRECT_Pattern <v8f32, f32, "V8">;
defm : SI_INDIRECT_Pattern <v16f32, f32, "V16">;

defm : SI_INDIRECT_Pattern <v2i32, i32, "V2">;
defm : SI_INDIRECT_Pattern <v4i32, i32, "V4">;
defm : SI_INDIRECT_Pattern <v8i32, i32, "V8">;
defm : SI_INDIRECT_Pattern <v16i32, i32, "V16">;

//===----------------------------------------------------------------------===//
// SAD Patterns
//===----------------------------------------------------------------------===//

def : GCNPat <
  (add (sub_oneuse (umax i32:$src0, i32:$src1),
                   (umin i32:$src0, i32:$src1)),
       i32:$src2),
  (V_SAD_U32 $src0, $src1, $src2, (i1 0))
>;

def : GCNPat <
  (add (select_oneuse (i1 (setugt i32:$src0, i32:$src1)),
                      (sub i32:$src0, i32:$src1),
                      (sub i32:$src1, i32:$src0)),
       i32:$src2),
  (V_SAD_U32 $src0, $src1, $src2, (i1 0))
>;

//===----------------------------------------------------------------------===//
// Conversion Patterns
//===----------------------------------------------------------------------===//

def : GCNPat<(i32 (sext_inreg i32:$src, i1)),
  (S_BFE_I32 i32:$src, (i32 65536))>; // 0 | 1 << 16

// Handle sext_inreg in i64
def : GCNPat <
  (i64 (sext_inreg i64:$src, i1)),
  (S_BFE_I64 i64:$src, (i32 0x10000)) // 0 | 1 << 16
>;

def : GCNPat <
  (i16 (sext_inreg i16:$src, i1)),
  (S_BFE_I32 $src, (i32 0x00010000)) // 0 | 1 << 16
>;

def : GCNPat <
  (i16 (sext_inreg i16:$src, i8)),
  (S_BFE_I32 $src, (i32 0x80000)) // 0 | 8 << 16
>;

def : GCNPat <
  (i64 (sext_inreg i64:$src, i8)),
  (S_BFE_I64 i64:$src, (i32 0x80000)) // 0 | 8 << 16
>;

def : GCNPat <
  (i64 (sext_inreg i64:$src, i16)),
  (S_BFE_I64 i64:$src, (i32 0x100000)) // 0 | 16 << 16
>;

def : GCNPat <
  (i64 (sext_inreg i64:$src, i32)),
  (S_BFE_I64 i64:$src, (i32 0x200000)) // 0 | 32 << 16
>;

def : GCNPat <
  (i64 (zext i32:$src)),
  (REG_SEQUENCE SReg_64, $src, sub0, (S_MOV_B32 (i32 0)), sub1)
>;

def : GCNPat <
  (i64 (anyext i32:$src)),
  (REG_SEQUENCE SReg_64, $src, sub0, (i32 (IMPLICIT_DEF)), sub1)
>;

class ZExt_i64_i1_Pat <SDNode ext> : GCNPat <
  (i64 (ext i1:$src)),
    (REG_SEQUENCE VReg_64,
      (V_CNDMASK_B32_e64 (i32 0), (i32 1), $src), sub0,
      (S_MOV_B32 (i32 0)), sub1)
>;


def : ZExt_i64_i1_Pat<zext>;
def : ZExt_i64_i1_Pat<anyext>;

// FIXME: We need to use COPY_TO_REGCLASS to work-around the fact that
// REG_SEQUENCE patterns don't support instructions with multiple outputs.
def : GCNPat <
  (i64 (sext i32:$src)),
    (REG_SEQUENCE SReg_64, $src, sub0,
    (i32 (COPY_TO_REGCLASS (S_ASHR_I32 $src, (i32 31)), SReg_32_XM0)), sub1)
>;

def : GCNPat <
  (i64 (sext i1:$src)),
  (REG_SEQUENCE VReg_64,
    (V_CNDMASK_B32_e64 (i32 0), (i32 -1), $src), sub0,
    (V_CNDMASK_B32_e64 (i32 0), (i32 -1), $src), sub1)
>;

class FPToI1Pat<Instruction Inst, int KOne, ValueType kone_type, ValueType vt, SDPatternOperator fp_to_int> : GCNPat <
  (i1 (fp_to_int (vt (VOP3Mods vt:$src0, i32:$src0_modifiers)))),
  (i1 (Inst 0, (kone_type KOne), $src0_modifiers, $src0, DSTCLAMP.NONE))
>;

def : FPToI1Pat<V_CMP_EQ_F32_e64, CONST.FP32_ONE, i32, f32, fp_to_uint>;
def : FPToI1Pat<V_CMP_EQ_F32_e64, CONST.FP32_NEG_ONE, i32, f32, fp_to_sint>;
def : FPToI1Pat<V_CMP_EQ_F64_e64, CONST.FP64_ONE, i64, f64, fp_to_uint>;
def : FPToI1Pat<V_CMP_EQ_F64_e64, CONST.FP64_NEG_ONE, i64, f64, fp_to_sint>;

// If we need to perform a logical operation on i1 values, we need to
// use vector comparisons since there is only one SCC register. Vector
// comparisons still write to a pair of SGPRs, so treat these as
// 64-bit comparisons. When legalizing SGPR copies, instructions
// resulting in the copies from SCC to these instructions will be
// moved to the VALU.
def : GCNPat <
  (i1 (and i1:$src0, i1:$src1)),
  (S_AND_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (or i1:$src0, i1:$src1)),
  (S_OR_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (xor i1:$src0, i1:$src1)),
  (S_XOR_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (add i1:$src0, i1:$src1)),
  (S_XOR_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (sub i1:$src0, i1:$src1)),
  (S_XOR_B64 $src0, $src1)
>;

let AddedComplexity = 1 in {
def : GCNPat <
  (i1 (add i1:$src0, (i1 -1))),
  (S_NOT_B64 $src0)
>;

def : GCNPat <
  (i1 (sub i1:$src0, (i1 -1))),
  (S_NOT_B64 $src0)
>;
}

def : GCNPat <
  (f16 (sint_to_fp i1:$src)),
  (V_CVT_F16_F32_e32 (V_CNDMASK_B32_e64 (i32 0), (i32 CONST.FP32_NEG_ONE), $src))
>;

def : GCNPat <
  (f16 (uint_to_fp i1:$src)),
  (V_CVT_F16_F32_e32 (V_CNDMASK_B32_e64 (i32 0), (i32 CONST.FP32_ONE), $src))
>;

def : GCNPat <
  (f32 (sint_to_fp i1:$src)),
  (V_CNDMASK_B32_e64 (i32 0), (i32 CONST.FP32_NEG_ONE), $src)
>;

def : GCNPat <
  (f32 (uint_to_fp i1:$src)),
  (V_CNDMASK_B32_e64 (i32 0), (i32 CONST.FP32_ONE), $src)
>;

def : GCNPat <
  (f64 (sint_to_fp i1:$src)),
  (V_CVT_F64_I32_e32 (V_CNDMASK_B32_e64 (i32 0), (i32 -1), $src))
>;

def : GCNPat <
  (f64 (uint_to_fp i1:$src)),
  (V_CVT_F64_U32_e32 (V_CNDMASK_B32_e64 (i32 0), (i32 1), $src))
>;

//===----------------------------------------------------------------------===//
// Miscellaneous Patterns
//===----------------------------------------------------------------------===//
def : GCNPat <
  (i32 (AMDGPUfp16_zext f16:$src)),
  (COPY $src)
>;


def : GCNPat <
  (i32 (trunc i64:$a)),
  (EXTRACT_SUBREG $a, sub0)
>;

def : GCNPat <
  (i1 (trunc i32:$a)),
  (V_CMP_EQ_U32_e64 (S_AND_B32 (i32 1), $a), (i32 1))
>;

def : GCNPat <
  (i1 (trunc i16:$a)),
  (V_CMP_EQ_U32_e64 (S_AND_B32 (i32 1), $a), (i32 1))
>;

def : GCNPat <
  (i1 (trunc i64:$a)),
  (V_CMP_EQ_U32_e64 (S_AND_B32 (i32 1),
                    (i32 (EXTRACT_SUBREG $a, sub0))), (i32 1))
>;

def : GCNPat <
  (i32 (bswap i32:$a)),
  (V_BFI_B32 (S_MOV_B32 (i32 0x00ff00ff)),
             (V_ALIGNBIT_B32 $a, $a, (i32 24)),
             (V_ALIGNBIT_B32 $a, $a, (i32 8)))
>;

let OtherPredicates = [NoFP16Denormals] in {
def : GCNPat<
  (fcanonicalize (f16 (VOP3Mods f16:$src, i32:$src_mods))),
  (V_MUL_F16_e64 0, (i32 CONST.FP16_ONE), $src_mods, $src, 0, 0)
>;

def : GCNPat<
  (fcanonicalize (f16 (fneg (VOP3Mods f16:$src, i32:$src_mods)))),
  (V_MUL_F16_e64 0, (i32 CONST.FP16_NEG_ONE), $src_mods, $src, 0, 0)
>;

def : GCNPat<
  (fcanonicalize (v2f16 (VOP3PMods v2f16:$src, i32:$src_mods))),
  (V_PK_MUL_F16 0, (i32 CONST.V2FP16_ONE), $src_mods, $src, DSTCLAMP.NONE)
>;
}

let OtherPredicates = [FP16Denormals] in {
def : GCNPat<
  (fcanonicalize (f16 (VOP3Mods f16:$src, i32:$src_mods))),
  (V_MAX_F16_e64 $src_mods, $src, $src_mods, $src, 0, 0)
>;

let SubtargetPredicate = HasVOP3PInsts in {
def : GCNPat<
  (fcanonicalize (v2f16 (VOP3PMods v2f16:$src, i32:$src_mods))),
  (V_PK_MAX_F16 $src_mods, $src, $src_mods, $src, DSTCLAMP.NONE)
>;
}
}

let OtherPredicates = [NoFP32Denormals] in {
def : GCNPat<
  (fcanonicalize (f32 (VOP3Mods f32:$src, i32:$src_mods))),
  (V_MUL_F32_e64 0, (i32 CONST.FP32_ONE), $src_mods, $src, 0, 0)
>;

def : GCNPat<
  (fcanonicalize (f32 (fneg (VOP3Mods f32:$src, i32:$src_mods)))),
  (V_MUL_F32_e64 0, (i32 CONST.FP32_NEG_ONE), $src_mods, $src, 0, 0)
>;
}

let OtherPredicates = [FP32Denormals] in {
def : GCNPat<
  (fcanonicalize (f32 (VOP3Mods f32:$src, i32:$src_mods))),
  (V_MAX_F32_e64 $src_mods, $src, $src_mods, $src, 0, 0)
>;
}

let OtherPredicates = [NoFP64Denormals] in {
def : GCNPat<
  (fcanonicalize (f64 (VOP3Mods f64:$src, i32:$src_mods))),
  (V_MUL_F64 0, CONST.FP64_ONE, $src_mods, $src, 0, 0)
>;
}

let OtherPredicates = [FP64Denormals] in {
def : GCNPat<
  (fcanonicalize (f64 (VOP3Mods f64:$src, i32:$src_mods))),
  (V_MAX_F64 $src_mods, $src, $src_mods, $src, 0, 0)
>;
}

let OtherPredicates = [HasDLInsts] in {
def : GCNPat <
  (fma (f32 (VOP3Mods0 f32:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omod)),
       (f32 (VOP3Mods f32:$src1, i32:$src1_modifiers)),
       (f32 (VOP3NoMods f32:$src2))),
  (V_FMAC_F32_e64 $src0_modifiers, $src0, $src1_modifiers, $src1,
                  SRCMODS.NONE, $src2, $clamp, $omod)
>;
} // End OtherPredicates = [HasDLInsts]


// Allow integer inputs
class ExpPattern<SDPatternOperator node, ValueType vt, Instruction Inst> : GCNPat<
  (node (i8 timm:$tgt), (i8 timm:$en), vt:$src0, vt:$src1, vt:$src2, vt:$src3, (i1 timm:$compr), (i1 timm:$vm)),
  (Inst i8:$tgt, vt:$src0, vt:$src1, vt:$src2, vt:$src3, i1:$vm, i1:$compr, i8:$en)
>;

def : ExpPattern<AMDGPUexport, i32, EXP>;
def : ExpPattern<AMDGPUexport_done, i32, EXP_DONE>;

// COPY is workaround tablegen bug from multiple outputs
// from S_LSHL_B32's multiple outputs from implicit scc def.
def : GCNPat <
  (v2i16 (build_vector (i16 0), i16:$src1)),
  (v2i16 (COPY (S_LSHL_B32 i16:$src1, (i16 16))))
>;

def : GCNPat <
  (v2i16 (build_vector i16:$src0, (i16 undef))),
  (v2i16 (COPY $src0))
>;

def : GCNPat <
  (v2f16 (build_vector f16:$src0, (f16 undef))),
  (v2f16 (COPY $src0))
>;

def : GCNPat <
  (v2i16 (build_vector (i16 undef), i16:$src1)),
  (v2i16 (COPY (S_LSHL_B32 $src1, (i32 16))))
>;

def : GCNPat <
  (v2f16 (build_vector (f16 undef), f16:$src1)),
  (v2f16 (COPY (S_LSHL_B32 $src1, (i32 16))))
>;

let SubtargetPredicate = HasVOP3PInsts in {
def : GCNPat <
  (v2i16 (build_vector i16:$src0, i16:$src1)),
  (v2i16 (S_PACK_LL_B32_B16 $src0, $src1))
>;

// With multiple uses of the shift, this will duplicate the shift and
// increase register pressure.
def : GCNPat <
  (v2i16 (build_vector i16:$src0, (i16 (trunc (srl_oneuse i32:$src1, (i32 16)))))),
  (v2i16 (S_PACK_LH_B32_B16 i16:$src0, i32:$src1))
>;


def : GCNPat <
  (v2i16 (build_vector (i16 (trunc (srl_oneuse i32:$src0, (i32 16)))),
                       (i16 (trunc (srl_oneuse i32:$src1, (i32 16)))))),
  (v2i16 (S_PACK_HH_B32_B16 $src0, $src1))
>;

// TODO: Should source modifiers be matched to v_pack_b32_f16?
def : GCNPat <
  (v2f16 (build_vector f16:$src0, f16:$src1)),
  (v2f16 (S_PACK_LL_B32_B16 $src0, $src1))
>;

} // End SubtargetPredicate = HasVOP3PInsts


def : GCNPat <
  (v2f16 (scalar_to_vector f16:$src0)),
  (COPY $src0)
>;

def : GCNPat <
  (v2i16 (scalar_to_vector i16:$src0)),
  (COPY $src0)
>;

def : GCNPat <
  (v4i16 (scalar_to_vector i16:$src0)),
  (INSERT_SUBREG (IMPLICIT_DEF), $src0, sub0)
>;

def : GCNPat <
  (v4f16 (scalar_to_vector f16:$src0)),
  (INSERT_SUBREG (IMPLICIT_DEF), $src0, sub0)
>;

//===----------------------------------------------------------------------===//
// Fract Patterns
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isSI in {

// V_FRACT is buggy on SI, so the F32 version is never used and (x-floor(x)) is
// used instead. However, SI doesn't have V_FLOOR_F64, so the most efficient
// way to implement it is using V_FRACT_F64.
// The workaround for the V_FRACT bug is:
//    fract(x) = isnan(x) ? x : min(V_FRACT(x), 0.99999999999999999)

// Convert floor(x) to (x - fract(x))
def : GCNPat <
  (f64 (ffloor (f64 (VOP3Mods f64:$x, i32:$mods)))),
  (V_ADD_F64
      $mods,
      $x,
      SRCMODS.NEG,
      (V_CNDMASK_B64_PSEUDO
         (V_MIN_F64
             SRCMODS.NONE,
             (V_FRACT_F64_e64 $mods, $x, DSTCLAMP.NONE, DSTOMOD.NONE),
             SRCMODS.NONE,
             (V_MOV_B64_PSEUDO 0x3fefffffffffffff),
             DSTCLAMP.NONE, DSTOMOD.NONE),
         $x,
         (V_CMP_CLASS_F64_e64 SRCMODS.NONE, $x, (i32 3 /*NaN*/))),
      DSTCLAMP.NONE, DSTOMOD.NONE)
>;

} // End SubtargetPredicates = isSI

//============================================================================//
// Miscellaneous Optimization Patterns
//============================================================================//

// Undo sub x, c -> add x, -c canonicalization since c is more likely
// an inline immediate than -c.
// TODO: Also do for 64-bit.
def : GCNPat<
  (add i32:$src0, (i32 NegSubInlineConst32:$src1)),
  (S_SUB_I32 $src0, NegSubInlineConst32:$src1)
>;


multiclass BFMPatterns <ValueType vt, InstSI BFM, InstSI MOV> {
  def : GCNPat <
    (vt (shl (vt (add (vt (shl 1, vt:$a)), -1)), vt:$b)),
    (BFM $a, $b)
  >;

  def : GCNPat <
    (vt (add (vt (shl 1, vt:$a)), -1)),
    (BFM $a, (MOV (i32 0)))
  >;
}

let SubtargetPredicate = isGCN in {

defm : BFMPatterns <i32, S_BFM_B32, S_MOV_B32>;
// FIXME: defm : BFMPatterns <i64, S_BFM_B64, S_MOV_B64>;

defm : BFEPattern <V_BFE_U32, V_BFE_I32, S_MOV_B32>;
defm : SHA256MaPattern <V_BFI_B32, V_XOR_B32_e64, SReg_64>;

defm : IntMed3Pat<V_MED3_I32, smin, smax, smin_oneuse, smax_oneuse>;
defm : IntMed3Pat<V_MED3_U32, umin, umax, umin_oneuse, umax_oneuse>;

}

// This matches 16 permutations of
// max(min(x, y), min(max(x, y), z))
class FPMed3Pat<ValueType vt,
                //SDPatternOperator max, SDPatternOperator min,
                Instruction med3Inst> : GCNPat<
  (fmaxnum_like (fminnum_like_oneuse (VOP3Mods_nnan vt:$src0, i32:$src0_mods),
                           (VOP3Mods_nnan vt:$src1, i32:$src1_mods)),
           (fminnum_like_oneuse (fmaxnum_like_oneuse (VOP3Mods_nnan vt:$src0, i32:$src0_mods),
                                           (VOP3Mods_nnan vt:$src1, i32:$src1_mods)),
                           (vt (VOP3Mods_nnan vt:$src2, i32:$src2_mods)))),
  (med3Inst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

class FP16Med3Pat<ValueType vt,
                Instruction med3Inst> : GCNPat<
  (fmaxnum_like (fminnum_like_oneuse (VOP3Mods_nnan vt:$src0, i32:$src0_mods),
                                     (VOP3Mods_nnan vt:$src1, i32:$src1_mods)),
           (fminnum_like_oneuse (fmaxnum_like_oneuse (VOP3Mods_nnan vt:$src0, i32:$src0_mods),
                                                     (VOP3Mods_nnan vt:$src1, i32:$src1_mods)),
                           (vt (VOP3Mods_nnan vt:$src2, i32:$src2_mods)))),
  (med3Inst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2, DSTCLAMP.NONE)
>;

multiclass Int16Med3Pat<Instruction med3Inst,
                   SDPatternOperator min,
                   SDPatternOperator max,
                   SDPatternOperator max_oneuse,
                   SDPatternOperator min_oneuse,
                   ValueType vt = i16> {
  // This matches 16 permutations of
  // max(min(x, y), min(max(x, y), z))
  def : GCNPat <
  (max (min_oneuse vt:$src0, vt:$src1),
       (min_oneuse (max_oneuse vt:$src0, vt:$src1), vt:$src2)),
  (med3Inst SRCMODS.NONE, $src0, SRCMODS.NONE, $src1, SRCMODS.NONE, $src2, DSTCLAMP.NONE)
>;

  // This matches 16 permutations of
  // min(max(a, b), max(min(a, b), c))
  def : GCNPat <
  (min (max_oneuse vt:$src0, vt:$src1),
      (max_oneuse (min_oneuse vt:$src0, vt:$src1), vt:$src2)),
  (med3Inst SRCMODS.NONE, $src0, SRCMODS.NONE, $src1, SRCMODS.NONE, $src2, DSTCLAMP.NONE)
>;
}

def : FPMed3Pat<f32, V_MED3_F32>;

let OtherPredicates = [isGFX9] in {
def : FP16Med3Pat<f16, V_MED3_F16>;
defm : Int16Med3Pat<V_MED3_I16, smin, smax, smax_oneuse, smin_oneuse>;
defm : Int16Med3Pat<V_MED3_U16, umin, umax, umax_oneuse, umin_oneuse>;
} // End Predicates = [isGFX9]
