//===-- VOP1Instructions.td - Vector Instruction Defintions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VOP1 Classes
//===----------------------------------------------------------------------===//

class VOP1e <bits<8> op, VOPProfile P> : Enc32 {
  bits<8> vdst;
  bits<9> src0;

  let Inst{8-0}   = !if(P.HasSrc0, src0{8-0}, 0);
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; //encoding
}

class VOP1_SDWAe <bits<8> op, VOPProfile P> : VOP_SDWAe <P> {
  bits<8> vdst;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; // encoding
}

class VOP1_SDWA9Ae <bits<8> op, VOPProfile P> : VOP_SDWA9Ae <P> {
  bits<8> vdst;

  let Inst{8-0}   = 0xf9; // sdwa
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; // encoding
}

class VOP1_Pseudo <string opName, VOPProfile P, list<dag> pattern=[], bit VOP1Only = 0> :
  VOP_Pseudo <opName, !if(VOP1Only, "", "_e32"), P, P.Outs32, P.Ins32, "", pattern> {

  let AsmOperands = P.Asm32;

  let Size = 4;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let SubtargetPredicate = isGCN;

  let VOP1 = 1;
  let VALU = 1;
  let Uses = [EXEC];

  let AsmVariantName = AMDGPUAsmVariants.Default;
}

class VOP1_Real <VOP1_Pseudo ps, int EncodingFamily> :
  InstSI <ps.OutOperandList, ps.InOperandList, ps.Mnemonic # ps.AsmOperands, []>,
  SIMCInstr <ps.PseudoInstr, EncodingFamily> {

  let isPseudo = 0;
  let isCodeGenOnly = 0;

  let Constraints     = ps.Constraints;
  let DisableEncoding = ps.DisableEncoding;

  // copy relevant pseudo op flags
  let SubtargetPredicate = ps.SubtargetPredicate;
  let AsmMatchConverter  = ps.AsmMatchConverter;
  let AsmVariantName     = ps.AsmVariantName;
  let Constraints        = ps.Constraints;
  let DisableEncoding    = ps.DisableEncoding;
  let TSFlags            = ps.TSFlags;
  let UseNamedOperandTable = ps.UseNamedOperandTable;
  let Uses                 = ps.Uses;
  let Defs                 = ps.Defs;
}

class VOP1_SDWA_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_SDWA_Pseudo <OpName, P, pattern> {
  let AsmMatchConverter = "cvtSdwaVOP1";
}

class VOP1_DPP_Pseudo <string OpName, VOPProfile P, list<dag> pattern=[]> :
  VOP_DPP_Pseudo <OpName, P, pattern> {
}

class getVOP1Pat64 <SDPatternOperator node, VOPProfile P> : LetDummies {
  list<dag> ret =
    !if(P.HasModifiers,
        [(set P.DstVT:$vdst, (node (P.Src0VT (VOP3Mods0 P.Src0VT:$src0,
                                              i32:$src0_modifiers,
                                              i1:$clamp, i32:$omod))))],
        !if(P.HasOMod,
            [(set P.DstVT:$vdst, (node (P.Src0VT (VOP3OMods P.Src0VT:$src0,
                                                  i1:$clamp, i32:$omod))))],
            [(set P.DstVT:$vdst, (node P.Src0VT:$src0))]
        )
    );
}

multiclass VOP1Inst <string opName, VOPProfile P,
                     SDPatternOperator node = null_frag> {
  def _e32 : VOP1_Pseudo <opName, P>;
  def _e64 : VOP3_Pseudo <opName, P, getVOP1Pat64<node, P>.ret>;
  def _sdwa : VOP1_SDWA_Pseudo <opName, P>;
  foreach _ = BoolToList<P.HasExtDPP>.ret in
    def _dpp : VOP1_DPP_Pseudo <opName, P>;
}

// Special profile for instructions which have clamp
// and output modifiers (but have no input modifiers)
class VOPProfileI2F<ValueType dstVt, ValueType srcVt> :
  VOPProfile<[dstVt, srcVt, untyped, untyped]> {

  let Ins64 = (ins Src0RC64:$src0, clampmod:$clamp, omod:$omod);
  let Asm64 = "$vdst, $src0$clamp$omod";

  let HasModifiers = 0;
  let HasClamp = 1;
  let HasOMod = 1;
}

def VOP1_F64_I32 : VOPProfileI2F <f64, i32>;
def VOP1_F32_I32 : VOPProfileI2F <f32, i32>;
def VOP1_F16_I16 : VOPProfileI2F <f16, i16>;

//===----------------------------------------------------------------------===//
// VOP1 Instructions
//===----------------------------------------------------------------------===//

let VOPAsmPrefer32Bit = 1 in {
defm V_NOP : VOP1Inst <"v_nop", VOP_NONE>;
}

let isMoveImm = 1, isReMaterializable = 1, isAsCheapAsAMove = 1 in {
defm V_MOV_B32 : VOP1Inst <"v_mov_b32", VOP_I32_I32>;
} // End isMoveImm = 1

// FIXME: Specify SchedRW for READFIRSTLANE_B32
// TODO: Make profile for this, there is VOP3 encoding also
def V_READFIRSTLANE_B32 :
  InstSI <(outs SReg_32:$vdst),
    (ins VGPR_32:$src0),
    "v_readfirstlane_b32 $vdst, $src0",
    [(set i32:$vdst, (int_amdgcn_readfirstlane i32:$src0))]>,
  Enc32 {

  let isCodeGenOnly = 0;
  let UseNamedOperandTable = 1;

  let Size = 4;
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let SubtargetPredicate = isGCN;

  let VOP1 = 1;
  let VALU = 1;
  let Uses = [EXEC];
  let isConvergent = 1;

  bits<8> vdst;
  bits<9> src0;

  let Inst{8-0}   = src0;
  let Inst{16-9}  = 0x2;
  let Inst{24-17} = vdst;
  let Inst{31-25} = 0x3f; //encoding
}

let SchedRW = [WriteQuarterRate32] in {
defm V_CVT_I32_F64 : VOP1Inst <"v_cvt_i32_f64", VOP_I32_F64, fp_to_sint>;
defm V_CVT_F64_I32 : VOP1Inst <"v_cvt_f64_i32", VOP1_F64_I32, sint_to_fp>;
defm V_CVT_F32_I32 : VOP1Inst <"v_cvt_f32_i32", VOP1_F32_I32, sint_to_fp>;
defm V_CVT_F32_U32 : VOP1Inst <"v_cvt_f32_u32", VOP1_F32_I32, uint_to_fp>;
defm V_CVT_U32_F32 : VOP1Inst <"v_cvt_u32_f32", VOP_I32_F32, fp_to_uint>;
defm V_CVT_I32_F32 : VOP1Inst <"v_cvt_i32_f32", VOP_I32_F32, fp_to_sint>;
let FPDPRounding = 1 in {
defm V_CVT_F16_F32 : VOP1Inst <"v_cvt_f16_f32", VOP_F16_F32, fpround>;
} // End FPDPRounding = 1
defm V_CVT_F32_F16 : VOP1Inst <"v_cvt_f32_f16", VOP_F32_F16, fpextend>;
defm V_CVT_RPI_I32_F32 : VOP1Inst <"v_cvt_rpi_i32_f32", VOP_I32_F32, cvt_rpi_i32_f32>;
defm V_CVT_FLR_I32_F32 : VOP1Inst <"v_cvt_flr_i32_f32", VOP_I32_F32, cvt_flr_i32_f32>;
defm V_CVT_OFF_F32_I4 : VOP1Inst  <"v_cvt_off_f32_i4", VOP1_F32_I32>;
defm V_CVT_F32_F64 : VOP1Inst <"v_cvt_f32_f64", VOP_F32_F64, fpround>;
defm V_CVT_F64_F32 : VOP1Inst <"v_cvt_f64_f32", VOP_F64_F32, fpextend>;
defm V_CVT_F32_UBYTE0 : VOP1Inst <"v_cvt_f32_ubyte0", VOP1_F32_I32, AMDGPUcvt_f32_ubyte0>;
defm V_CVT_F32_UBYTE1 : VOP1Inst <"v_cvt_f32_ubyte1", VOP1_F32_I32, AMDGPUcvt_f32_ubyte1>;
defm V_CVT_F32_UBYTE2 : VOP1Inst <"v_cvt_f32_ubyte2", VOP1_F32_I32, AMDGPUcvt_f32_ubyte2>;
defm V_CVT_F32_UBYTE3 : VOP1Inst <"v_cvt_f32_ubyte3", VOP1_F32_I32, AMDGPUcvt_f32_ubyte3>;
defm V_CVT_U32_F64 : VOP1Inst <"v_cvt_u32_f64", VOP_I32_F64, fp_to_uint>;
defm V_CVT_F64_U32 : VOP1Inst <"v_cvt_f64_u32", VOP1_F64_I32, uint_to_fp>;
} // End SchedRW = [WriteQuarterRate32]

defm V_FRACT_F32 : VOP1Inst <"v_fract_f32", VOP_F32_F32, AMDGPUfract>;
defm V_TRUNC_F32 : VOP1Inst <"v_trunc_f32", VOP_F32_F32, ftrunc>;
defm V_CEIL_F32 : VOP1Inst <"v_ceil_f32", VOP_F32_F32, fceil>;
defm V_RNDNE_F32 : VOP1Inst <"v_rndne_f32", VOP_F32_F32, frint>;
defm V_FLOOR_F32 : VOP1Inst <"v_floor_f32", VOP_F32_F32, ffloor>;

let SchedRW = [WriteQuarterRate32] in {
defm V_EXP_F32 : VOP1Inst <"v_exp_f32", VOP_F32_F32, fexp2>;
defm V_LOG_F32 : VOP1Inst <"v_log_f32", VOP_F32_F32, flog2>;
defm V_RCP_F32 : VOP1Inst <"v_rcp_f32", VOP_F32_F32, AMDGPUrcp>;
defm V_RCP_IFLAG_F32 : VOP1Inst <"v_rcp_iflag_f32", VOP_F32_F32, AMDGPUrcp_iflag>;
defm V_RSQ_F32 : VOP1Inst <"v_rsq_f32", VOP_F32_F32, AMDGPUrsq>;
defm V_SQRT_F32 : VOP1Inst <"v_sqrt_f32", VOP_F32_F32, fsqrt>;
} // End SchedRW = [WriteQuarterRate32]

let SchedRW = [WriteDouble] in {
defm V_RCP_F64 : VOP1Inst <"v_rcp_f64", VOP_F64_F64, AMDGPUrcp>;
defm V_RSQ_F64 : VOP1Inst <"v_rsq_f64", VOP_F64_F64, AMDGPUrsq>;
} // End SchedRW = [WriteDouble];

let SchedRW = [WriteDouble] in {
defm V_SQRT_F64 : VOP1Inst <"v_sqrt_f64", VOP_F64_F64, fsqrt>;
} // End SchedRW = [WriteDouble]

let SchedRW = [WriteQuarterRate32] in {
defm V_SIN_F32 : VOP1Inst <"v_sin_f32", VOP_F32_F32, AMDGPUsin>;
defm V_COS_F32 : VOP1Inst <"v_cos_f32", VOP_F32_F32, AMDGPUcos>;
} // End SchedRW = [WriteQuarterRate32]

defm V_NOT_B32 : VOP1Inst <"v_not_b32", VOP_I32_I32>;
defm V_BFREV_B32 : VOP1Inst <"v_bfrev_b32", VOP_I32_I32>;
defm V_FFBH_U32 : VOP1Inst <"v_ffbh_u32", VOP_I32_I32>;
defm V_FFBL_B32 : VOP1Inst <"v_ffbl_b32", VOP_I32_I32>;
defm V_FFBH_I32 : VOP1Inst <"v_ffbh_i32", VOP_I32_I32>;

let SchedRW = [WriteDoubleAdd] in {
defm V_FREXP_EXP_I32_F64 : VOP1Inst <"v_frexp_exp_i32_f64", VOP_I32_F64, int_amdgcn_frexp_exp>;
defm V_FREXP_MANT_F64 : VOP1Inst <"v_frexp_mant_f64", VOP_F64_F64, int_amdgcn_frexp_mant>;
let FPDPRounding = 1 in {
defm V_FRACT_F64 : VOP1Inst <"v_fract_f64", VOP_F64_F64, AMDGPUfract>;
} // End FPDPRounding = 1
} // End SchedRW = [WriteDoubleAdd]

defm V_FREXP_EXP_I32_F32 : VOP1Inst <"v_frexp_exp_i32_f32", VOP_I32_F32, int_amdgcn_frexp_exp>;
defm V_FREXP_MANT_F32 : VOP1Inst <"v_frexp_mant_f32", VOP_F32_F32, int_amdgcn_frexp_mant>;

let VOPAsmPrefer32Bit = 1 in {
defm V_CLREXCP : VOP1Inst <"v_clrexcp", VOP_NO_EXT<VOP_NONE>>;
}

// Restrict src0 to be VGPR
def VOP_I32_VI32_NO_EXT : VOPProfile<[i32, i32, untyped, untyped]> {
  let Src0RC32 = VRegSrc_32;
  let Src0RC64 = VRegSrc_32;

  let HasExt = 0;
  let HasExtDPP = 0;
  let HasExtSDWA = 0;
  let HasExtSDWA9 = 0;
}

// Special case because there are no true output operands.  Hack vdst
// to be a src operand. The custom inserter must add a tied implicit
// def and use of the super register since there seems to be no way to
// add an implicit def of a virtual register in tablegen.
def VOP_MOVRELD : VOPProfile<[untyped, i32, untyped, untyped]> {
  let Src0RC32 = VOPDstOperand<VGPR_32>;
  let Src0RC64 = VOPDstOperand<VGPR_32>;

  let Outs = (outs);
  let Ins32 = (ins Src0RC32:$vdst, VSrc_b32:$src0);
  let Ins64 = (ins Src0RC64:$vdst, VSrc_b32:$src0);
  let InsDPP = (ins DstRC:$vdst, DstRC:$old, Src0RC32:$src0,
                    dpp_ctrl:$dpp_ctrl, row_mask:$row_mask,
                    bank_mask:$bank_mask, bound_ctrl:$bound_ctrl);

  let InsSDWA = (ins Src0RC32:$vdst, Src0ModSDWA:$src0_modifiers, Src0SDWA:$src0,
                     clampmod:$clamp, omod:$omod, dst_sel:$dst_sel, dst_unused:$dst_unused,
                     src0_sel:$src0_sel);

  let Asm32 = getAsm32<1, 1>.ret;
  let Asm64 = getAsm64<1, 1, 0, 0, 1>.ret;
  let AsmDPP = getAsmDPP<1, 1, 0>.ret;
  let AsmSDWA = getAsmSDWA<1, 1>.ret;
  let AsmSDWA9 = getAsmSDWA9<1, 0, 1>.ret;

  let HasExt = 0;
  let HasExtDPP = 0;
  let HasExtSDWA = 0;
  let HasExtSDWA9 = 0;

  let HasDst = 0;
  let EmitDst = 1; // force vdst emission
}

let SubtargetPredicate = HasMovrel, Uses = [M0, EXEC] in {
// v_movreld_b32 is a special case because the destination output
 // register is really a source. It isn't actually read (but may be
 // written), and is only to provide the base register to start
 // indexing from. Tablegen seems to not let you define an implicit
 // virtual register output for the super register being written into,
 // so this must have an implicit def of the register added to it.
defm V_MOVRELD_B32 : VOP1Inst <"v_movreld_b32", VOP_MOVRELD>;
defm V_MOVRELS_B32 : VOP1Inst <"v_movrels_b32", VOP_I32_VI32_NO_EXT>;
defm V_MOVRELSD_B32 : VOP1Inst <"v_movrelsd_b32", VOP_NO_EXT<VOP_I32_I32>>;
} // End Uses = [M0, EXEC]

defm V_MOV_FED_B32 : VOP1Inst <"v_mov_fed_b32", VOP_I32_I32>;

// These instruction only exist on SI and CI
let SubtargetPredicate = isSICI in {

let SchedRW = [WriteQuarterRate32] in {
defm V_LOG_CLAMP_F32 : VOP1Inst <"v_log_clamp_f32", VOP_F32_F32, int_amdgcn_log_clamp>;
defm V_RCP_CLAMP_F32 : VOP1Inst <"v_rcp_clamp_f32", VOP_F32_F32>;
defm V_RCP_LEGACY_F32 : VOP1Inst <"v_rcp_legacy_f32", VOP_F32_F32, AMDGPUrcp_legacy>;
defm V_RSQ_CLAMP_F32 : VOP1Inst <"v_rsq_clamp_f32", VOP_F32_F32, AMDGPUrsq_clamp>;
defm V_RSQ_LEGACY_F32 : VOP1Inst <"v_rsq_legacy_f32", VOP_F32_F32, AMDGPUrsq_legacy>;
} // End SchedRW = [WriteQuarterRate32]

let SchedRW = [WriteDouble] in {
defm V_RCP_CLAMP_F64 : VOP1Inst <"v_rcp_clamp_f64", VOP_F64_F64>;
defm V_RSQ_CLAMP_F64 : VOP1Inst <"v_rsq_clamp_f64", VOP_F64_F64, AMDGPUrsq_clamp>;
} // End SchedRW = [WriteDouble]

} // End SubtargetPredicate = isSICI


let SubtargetPredicate = isCIVI in {

let SchedRW = [WriteDoubleAdd] in {
defm V_TRUNC_F64 : VOP1Inst <"v_trunc_f64", VOP_F64_F64, ftrunc>;
defm V_CEIL_F64 : VOP1Inst <"v_ceil_f64", VOP_F64_F64, fceil>;
defm V_FLOOR_F64 : VOP1Inst <"v_floor_f64", VOP_F64_F64, ffloor>;
defm V_RNDNE_F64 : VOP1Inst <"v_rndne_f64", VOP_F64_F64, frint>;
} // End SchedRW = [WriteDoubleAdd]

let SchedRW = [WriteQuarterRate32] in {
defm V_LOG_LEGACY_F32 : VOP1Inst <"v_log_legacy_f32", VOP_F32_F32>;
defm V_EXP_LEGACY_F32 : VOP1Inst <"v_exp_legacy_f32", VOP_F32_F32>;
} // End SchedRW = [WriteQuarterRate32]

} // End SubtargetPredicate = isCIVI


let SubtargetPredicate = Has16BitInsts in {

let FPDPRounding = 1 in {
defm V_CVT_F16_U16 : VOP1Inst <"v_cvt_f16_u16", VOP1_F16_I16, uint_to_fp>;
defm V_CVT_F16_I16 : VOP1Inst <"v_cvt_f16_i16", VOP1_F16_I16, sint_to_fp>;
} // End FPDPRounding = 1
defm V_CVT_U16_F16 : VOP1Inst <"v_cvt_u16_f16", VOP_I16_F16, fp_to_uint>;
defm V_CVT_I16_F16 : VOP1Inst <"v_cvt_i16_f16", VOP_I16_F16, fp_to_sint>;
let SchedRW = [WriteQuarterRate32] in {
defm V_RCP_F16 : VOP1Inst <"v_rcp_f16", VOP_F16_F16, AMDGPUrcp>;
defm V_SQRT_F16 : VOP1Inst <"v_sqrt_f16", VOP_F16_F16, fsqrt>;
defm V_RSQ_F16 : VOP1Inst <"v_rsq_f16", VOP_F16_F16, AMDGPUrsq>;
defm V_LOG_F16 : VOP1Inst <"v_log_f16", VOP_F16_F16, flog2>;
defm V_EXP_F16 : VOP1Inst <"v_exp_f16", VOP_F16_F16, fexp2>;
defm V_SIN_F16 : VOP1Inst <"v_sin_f16", VOP_F16_F16, AMDGPUsin>;
defm V_COS_F16 : VOP1Inst <"v_cos_f16", VOP_F16_F16, AMDGPUcos>;
} // End SchedRW = [WriteQuarterRate32]
defm V_FREXP_MANT_F16 : VOP1Inst <"v_frexp_mant_f16", VOP_F16_F16, int_amdgcn_frexp_mant>;
defm V_FREXP_EXP_I16_F16 : VOP1Inst <"v_frexp_exp_i16_f16", VOP_I16_F16, int_amdgcn_frexp_exp>;
defm V_FLOOR_F16 : VOP1Inst <"v_floor_f16", VOP_F16_F16, ffloor>;
defm V_CEIL_F16 : VOP1Inst <"v_ceil_f16", VOP_F16_F16, fceil>;
defm V_TRUNC_F16 : VOP1Inst <"v_trunc_f16", VOP_F16_F16, ftrunc>;
defm V_RNDNE_F16 : VOP1Inst <"v_rndne_f16", VOP_F16_F16, frint>;
let FPDPRounding = 1 in {
defm V_FRACT_F16 : VOP1Inst <"v_fract_f16", VOP_F16_F16, AMDGPUfract>;
} // End FPDPRounding = 1

}

let OtherPredicates = [Has16BitInsts] in {

def : GCNPat<
    (f32 (f16_to_fp i16:$src)),
    (V_CVT_F32_F16_e32 $src)
>;

def : GCNPat<
    (i16 (AMDGPUfp_to_f16 f32:$src)),
    (V_CVT_F16_F32_e32 $src)
>;

}

def VOP_SWAP_I32 : VOPProfile<[i32, i32, i32, untyped]> {
  let Outs32 = (outs VGPR_32:$vdst, VGPR_32:$vdst1);
  let Ins32 = (ins VGPR_32:$src0, VGPR_32:$src1);
  let Outs64 = Outs32;
  let Asm32 = " $vdst, $src0";
  let Asm64 = "";
  let Ins64 = (ins);
}

let SubtargetPredicate = isGFX9 in {
  let Constraints = "$vdst = $src1, $vdst1 = $src0",
      DisableEncoding="$vdst1,$src1",
      SchedRW = [Write64Bit, Write64Bit] in {
// Never VOP3. Takes as long as 2 v_mov_b32s
def V_SWAP_B32 : VOP1_Pseudo <"v_swap_b32", VOP_SWAP_I32, [], 1>;
}

defm V_SCREEN_PARTITION_4SE_B32 : VOP1Inst <"v_screen_partition_4se_b32", VOP_I32_I32>;

defm V_SAT_PK_U8_I16    : VOP1Inst<"v_sat_pk_u8_i16", VOP_I32_I32>;
defm V_CVT_NORM_I16_F16 : VOP1Inst<"v_cvt_norm_i16_f16", VOP_I16_F16>;
defm V_CVT_NORM_U16_F16 : VOP1Inst<"v_cvt_norm_u16_f16", VOP_I16_F16>;

} // End SubtargetPredicate = isGFX9

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SI
//===----------------------------------------------------------------------===//

multiclass VOP1_Real_si <bits<9> op> {
  let AssemblerPredicates = [isSICI], DecoderNamespace = "SICI" in {
    def _e32_si :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME#"_e32"), SIEncodingFamily.SI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32").Pfl>;
    def _e64_si :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.SI>,
      VOP3e_si <{1, 1, op{6-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
}

defm V_NOP               : VOP1_Real_si <0x0>;
defm V_MOV_B32           : VOP1_Real_si <0x1>;
defm V_CVT_I32_F64       : VOP1_Real_si <0x3>;
defm V_CVT_F64_I32       : VOP1_Real_si <0x4>;
defm V_CVT_F32_I32       : VOP1_Real_si <0x5>;
defm V_CVT_F32_U32       : VOP1_Real_si <0x6>;
defm V_CVT_U32_F32       : VOP1_Real_si <0x7>;
defm V_CVT_I32_F32       : VOP1_Real_si <0x8>;
defm V_MOV_FED_B32       : VOP1_Real_si <0x9>;
defm V_CVT_F16_F32       : VOP1_Real_si <0xa>;
defm V_CVT_F32_F16       : VOP1_Real_si <0xb>;
defm V_CVT_RPI_I32_F32   : VOP1_Real_si <0xc>;
defm V_CVT_FLR_I32_F32   : VOP1_Real_si <0xd>;
defm V_CVT_OFF_F32_I4    : VOP1_Real_si <0xe>;
defm V_CVT_F32_F64       : VOP1_Real_si <0xf>;
defm V_CVT_F64_F32       : VOP1_Real_si <0x10>;
defm V_CVT_F32_UBYTE0    : VOP1_Real_si <0x11>;
defm V_CVT_F32_UBYTE1    : VOP1_Real_si <0x12>;
defm V_CVT_F32_UBYTE2    : VOP1_Real_si <0x13>;
defm V_CVT_F32_UBYTE3    : VOP1_Real_si <0x14>;
defm V_CVT_U32_F64       : VOP1_Real_si <0x15>;
defm V_CVT_F64_U32       : VOP1_Real_si <0x16>;
defm V_FRACT_F32         : VOP1_Real_si <0x20>;
defm V_TRUNC_F32         : VOP1_Real_si <0x21>;
defm V_CEIL_F32          : VOP1_Real_si <0x22>;
defm V_RNDNE_F32         : VOP1_Real_si <0x23>;
defm V_FLOOR_F32         : VOP1_Real_si <0x24>;
defm V_EXP_F32           : VOP1_Real_si <0x25>;
defm V_LOG_CLAMP_F32     : VOP1_Real_si <0x26>;
defm V_LOG_F32           : VOP1_Real_si <0x27>;
defm V_RCP_CLAMP_F32     : VOP1_Real_si <0x28>;
defm V_RCP_LEGACY_F32    : VOP1_Real_si <0x29>;
defm V_RCP_F32           : VOP1_Real_si <0x2a>;
defm V_RCP_IFLAG_F32     : VOP1_Real_si <0x2b>;
defm V_RSQ_CLAMP_F32     : VOP1_Real_si <0x2c>;
defm V_RSQ_LEGACY_F32    : VOP1_Real_si <0x2d>;
defm V_RSQ_F32           : VOP1_Real_si <0x2e>;
defm V_RCP_F64           : VOP1_Real_si <0x2f>;
defm V_RCP_CLAMP_F64     : VOP1_Real_si <0x30>;
defm V_RSQ_F64           : VOP1_Real_si <0x31>;
defm V_RSQ_CLAMP_F64     : VOP1_Real_si <0x32>;
defm V_SQRT_F32          : VOP1_Real_si <0x33>;
defm V_SQRT_F64          : VOP1_Real_si <0x34>;
defm V_SIN_F32           : VOP1_Real_si <0x35>;
defm V_COS_F32           : VOP1_Real_si <0x36>;
defm V_NOT_B32           : VOP1_Real_si <0x37>;
defm V_BFREV_B32         : VOP1_Real_si <0x38>;
defm V_FFBH_U32          : VOP1_Real_si <0x39>;
defm V_FFBL_B32          : VOP1_Real_si <0x3a>;
defm V_FFBH_I32          : VOP1_Real_si <0x3b>;
defm V_FREXP_EXP_I32_F64 : VOP1_Real_si <0x3c>;
defm V_FREXP_MANT_F64    : VOP1_Real_si <0x3d>;
defm V_FRACT_F64         : VOP1_Real_si <0x3e>;
defm V_FREXP_EXP_I32_F32 : VOP1_Real_si <0x3f>;
defm V_FREXP_MANT_F32    : VOP1_Real_si <0x40>;
defm V_CLREXCP           : VOP1_Real_si <0x41>;
defm V_MOVRELD_B32       : VOP1_Real_si <0x42>;
defm V_MOVRELS_B32       : VOP1_Real_si <0x43>;
defm V_MOVRELSD_B32      : VOP1_Real_si <0x44>;

//===----------------------------------------------------------------------===//
// CI
//===----------------------------------------------------------------------===//

multiclass VOP1_Real_ci <bits<9> op> {
  let AssemblerPredicates = [isCIOnly], DecoderNamespace = "CI" in {
    def _e32_ci :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME#"_e32"), SIEncodingFamily.SI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32").Pfl>;
    def _e64_ci :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.SI>,
      VOP3e_si <{1, 1, op{6-0}}, !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
}

defm V_TRUNC_F64         : VOP1_Real_ci <0x17>;
defm V_CEIL_F64          : VOP1_Real_ci <0x18>;
defm V_FLOOR_F64         : VOP1_Real_ci <0x1A>;
defm V_RNDNE_F64         : VOP1_Real_ci <0x19>;
defm V_LOG_LEGACY_F32    : VOP1_Real_ci <0x45>;
defm V_EXP_LEGACY_F32    : VOP1_Real_ci <0x46>;

//===----------------------------------------------------------------------===//
// VI
//===----------------------------------------------------------------------===//

class VOP1_DPPe <bits<8> op, VOP1_DPP_Pseudo ps, VOPProfile P = ps.Pfl> :
  VOP_DPPe <P> {
  bits<8> vdst;
  let Inst{8-0}   = 0xfa; // dpp
  let Inst{16-9}  = op;
  let Inst{24-17} = !if(P.EmitDst, vdst{7-0}, 0);
  let Inst{31-25} = 0x3f; //encoding
}

multiclass VOP1Only_Real_vi <bits<10> op> {
  let AssemblerPredicates = [isVI], DecoderNamespace = "VI" in {
    def _vi :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME), SIEncodingFamily.VI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME).Pfl>;
  }
}

multiclass VOP1_Real_e32e64_vi <bits<10> op> {
  let AssemblerPredicates = [isVI], DecoderNamespace = "VI" in {
    def _e32_vi :
      VOP1_Real<!cast<VOP1_Pseudo>(NAME#"_e32"), SIEncodingFamily.VI>,
      VOP1e<op{7-0}, !cast<VOP1_Pseudo>(NAME#"_e32").Pfl>;
    def _e64_vi :
      VOP3_Real<!cast<VOP3_Pseudo>(NAME#"_e64"), SIEncodingFamily.VI>,
      VOP3e_vi <!add(0x140, op), !cast<VOP3_Pseudo>(NAME#"_e64").Pfl>;
  }
}

multiclass VOP1_Real_vi <bits<10> op> {
  defm NAME : VOP1_Real_e32e64_vi <op>;

  def _sdwa_vi :
    VOP_SDWA_Real <!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOP1_SDWAe <op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  def _sdwa_gfx9 :
    VOP_SDWA9_Real <!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOP1_SDWA9Ae <op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  foreach _ = BoolToList<!cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtDPP>.ret in
    def _dpp_vi :
      VOP_DPP_Real<!cast<VOP1_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.VI>,
      VOP1_DPPe<op{7-0}, !cast<VOP1_DPP_Pseudo>(NAME#"_dpp")>;
}

defm V_NOP               : VOP1_Real_vi <0x0>;
defm V_MOV_B32           : VOP1_Real_vi <0x1>;
defm V_CVT_I32_F64       : VOP1_Real_vi <0x3>;
defm V_CVT_F64_I32       : VOP1_Real_vi <0x4>;
defm V_CVT_F32_I32       : VOP1_Real_vi <0x5>;
defm V_CVT_F32_U32       : VOP1_Real_vi <0x6>;
defm V_CVT_U32_F32       : VOP1_Real_vi <0x7>;
defm V_CVT_I32_F32       : VOP1_Real_vi <0x8>;
defm V_MOV_FED_B32       : VOP1_Real_vi <0x9>;
defm V_CVT_F16_F32       : VOP1_Real_vi <0xa>;
defm V_CVT_F32_F16       : VOP1_Real_vi <0xb>;
defm V_CVT_RPI_I32_F32   : VOP1_Real_vi <0xc>;
defm V_CVT_FLR_I32_F32   : VOP1_Real_vi <0xd>;
defm V_CVT_OFF_F32_I4    : VOP1_Real_vi <0xe>;
defm V_CVT_F32_F64       : VOP1_Real_vi <0xf>;
defm V_CVT_F64_F32       : VOP1_Real_vi <0x10>;
defm V_CVT_F32_UBYTE0    : VOP1_Real_vi <0x11>;
defm V_CVT_F32_UBYTE1    : VOP1_Real_vi <0x12>;
defm V_CVT_F32_UBYTE2    : VOP1_Real_vi <0x13>;
defm V_CVT_F32_UBYTE3    : VOP1_Real_vi <0x14>;
defm V_CVT_U32_F64       : VOP1_Real_vi <0x15>;
defm V_CVT_F64_U32       : VOP1_Real_vi <0x16>;
defm V_FRACT_F32         : VOP1_Real_vi <0x1b>;
defm V_TRUNC_F32         : VOP1_Real_vi <0x1c>;
defm V_CEIL_F32          : VOP1_Real_vi <0x1d>;
defm V_RNDNE_F32         : VOP1_Real_vi <0x1e>;
defm V_FLOOR_F32         : VOP1_Real_vi <0x1f>;
defm V_EXP_F32           : VOP1_Real_vi <0x20>;
defm V_LOG_F32           : VOP1_Real_vi <0x21>;
defm V_RCP_F32           : VOP1_Real_vi <0x22>;
defm V_RCP_IFLAG_F32     : VOP1_Real_vi <0x23>;
defm V_RSQ_F32           : VOP1_Real_vi <0x24>;
defm V_RCP_F64           : VOP1_Real_vi <0x25>;
defm V_RSQ_F64           : VOP1_Real_vi <0x26>;
defm V_SQRT_F32          : VOP1_Real_vi <0x27>;
defm V_SQRT_F64          : VOP1_Real_vi <0x28>;
defm V_SIN_F32           : VOP1_Real_vi <0x29>;
defm V_COS_F32           : VOP1_Real_vi <0x2a>;
defm V_NOT_B32           : VOP1_Real_vi <0x2b>;
defm V_BFREV_B32         : VOP1_Real_vi <0x2c>;
defm V_FFBH_U32          : VOP1_Real_vi <0x2d>;
defm V_FFBL_B32          : VOP1_Real_vi <0x2e>;
defm V_FFBH_I32          : VOP1_Real_vi <0x2f>;
defm V_FREXP_EXP_I32_F64 : VOP1_Real_vi <0x30>;
defm V_FREXP_MANT_F64    : VOP1_Real_vi <0x31>;
defm V_FRACT_F64         : VOP1_Real_vi <0x32>;
defm V_FREXP_EXP_I32_F32 : VOP1_Real_vi <0x33>;
defm V_FREXP_MANT_F32    : VOP1_Real_vi <0x34>;
defm V_CLREXCP           : VOP1_Real_vi <0x35>;
defm V_MOVRELD_B32       : VOP1_Real_e32e64_vi <0x36>;
defm V_MOVRELS_B32       : VOP1_Real_e32e64_vi <0x37>;
defm V_MOVRELSD_B32      : VOP1_Real_e32e64_vi <0x38>;
defm V_TRUNC_F64         : VOP1_Real_vi <0x17>;
defm V_CEIL_F64          : VOP1_Real_vi <0x18>;
defm V_FLOOR_F64         : VOP1_Real_vi <0x1A>;
defm V_RNDNE_F64         : VOP1_Real_vi <0x19>;
defm V_LOG_LEGACY_F32    : VOP1_Real_vi <0x4c>;
defm V_EXP_LEGACY_F32    : VOP1_Real_vi <0x4b>;
defm V_CVT_F16_U16       : VOP1_Real_vi <0x39>;
defm V_CVT_F16_I16       : VOP1_Real_vi <0x3a>;
defm V_CVT_U16_F16       : VOP1_Real_vi <0x3b>;
defm V_CVT_I16_F16       : VOP1_Real_vi <0x3c>;
defm V_RCP_F16           : VOP1_Real_vi <0x3d>;
defm V_SQRT_F16          : VOP1_Real_vi <0x3e>;
defm V_RSQ_F16           : VOP1_Real_vi <0x3f>;
defm V_LOG_F16           : VOP1_Real_vi <0x40>;
defm V_EXP_F16           : VOP1_Real_vi <0x41>;
defm V_FREXP_MANT_F16    : VOP1_Real_vi <0x42>;
defm V_FREXP_EXP_I16_F16 : VOP1_Real_vi <0x43>;
defm V_FLOOR_F16         : VOP1_Real_vi <0x44>;
defm V_CEIL_F16          : VOP1_Real_vi <0x45>;
defm V_TRUNC_F16         : VOP1_Real_vi <0x46>;
defm V_RNDNE_F16         : VOP1_Real_vi <0x47>;
defm V_FRACT_F16         : VOP1_Real_vi <0x48>;
defm V_SIN_F16           : VOP1_Real_vi <0x49>;
defm V_COS_F16           : VOP1_Real_vi <0x4a>;
defm V_SWAP_B32          : VOP1Only_Real_vi <0x51>;

defm V_SAT_PK_U8_I16     : VOP1_Real_vi<0x4f>;
defm V_CVT_NORM_I16_F16  : VOP1_Real_vi<0x4d>;
defm V_CVT_NORM_U16_F16  : VOP1_Real_vi<0x4e>;

// Copy of v_mov_b32 with $vdst as a use operand for use with VGPR
// indexing mode. vdst can't be treated as a def for codegen purposes,
// and an implicit use and def of the super register should be added.
def V_MOV_B32_indirect : VPseudoInstSI<(outs),
  (ins getVALUDstForVT<i32>.ret:$vdst, getVOPSrc0ForVT<i32>.ret:$src0)>,
  PseudoInstExpansion<(V_MOV_B32_e32_vi getVALUDstForVT<i32>.ret:$vdst,
                                        getVOPSrc0ForVT<i32>.ret:$src0)> {
  let VOP1 = 1;
  let SubtargetPredicate = isVI;
}

// This is a pseudo variant of the v_movreld_b32 instruction in which the
// vector operand appears only twice, once as def and once as use. Using this
// pseudo avoids problems with the Two Address instructions pass.
class V_MOVRELD_B32_pseudo<RegisterClass rc> : VPseudoInstSI <
  (outs rc:$vdst),
  (ins rc:$vsrc, VSrc_b32:$val, i32imm:$offset)> {
  let VOP1 = 1;

  let Constraints = "$vsrc = $vdst";
  let Uses = [M0, EXEC];

  let SubtargetPredicate = HasMovrel;
}

def V_MOVRELD_B32_V1 : V_MOVRELD_B32_pseudo<VGPR_32>;
def V_MOVRELD_B32_V2 : V_MOVRELD_B32_pseudo<VReg_64>;
def V_MOVRELD_B32_V4 : V_MOVRELD_B32_pseudo<VReg_128>;
def V_MOVRELD_B32_V8 : V_MOVRELD_B32_pseudo<VReg_256>;
def V_MOVRELD_B32_V16 : V_MOVRELD_B32_pseudo<VReg_512>;

let OtherPredicates = [isVI] in {

def : GCNPat <
  (i32 (int_amdgcn_mov_dpp i32:$src, imm:$dpp_ctrl, imm:$row_mask, imm:$bank_mask,
                      imm:$bound_ctrl)),
  (V_MOV_B32_dpp $src, $src, (as_i32imm $dpp_ctrl),
                       (as_i32imm $row_mask), (as_i32imm $bank_mask),
                       (as_i1imm $bound_ctrl))
>;

def : GCNPat <
  (i32 (int_amdgcn_update_dpp i32:$old, i32:$src, imm:$dpp_ctrl, imm:$row_mask,
                      imm:$bank_mask, imm:$bound_ctrl)),
  (V_MOV_B32_dpp $old, $src, (as_i32imm $dpp_ctrl),
                       (as_i32imm $row_mask), (as_i32imm $bank_mask),
                       (as_i1imm $bound_ctrl))
>;

def : GCNPat<
  (i32 (anyext i16:$src)),
  (COPY $src)
>;

def : GCNPat<
   (i64 (anyext i16:$src)),
   (REG_SEQUENCE VReg_64,
     (i32 (COPY $src)), sub0,
     (V_MOV_B32_e32 (i32 0)), sub1)
>;

def : GCNPat<
  (i16 (trunc i32:$src)),
  (COPY $src)
>;

def : GCNPat <
  (i16 (trunc i64:$src)),
  (EXTRACT_SUBREG $src, sub0)
>;

} // End OtherPredicates = [isVI]

//===----------------------------------------------------------------------===//
// GFX9
//===----------------------------------------------------------------------===//

multiclass VOP1_Real_gfx9 <bits<10> op> {
  let AssemblerPredicates = [isGFX9], DecoderNamespace = "GFX9" in {
    defm NAME : VOP1_Real_e32e64_vi <op>;
  }

  def _sdwa_gfx9 :
    VOP_SDWA9_Real <!cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa")>,
    VOP1_SDWA9Ae <op{7-0}, !cast<VOP1_SDWA_Pseudo>(NAME#"_sdwa").Pfl>;

  foreach _ = BoolToList<!cast<VOP1_Pseudo>(NAME#"_e32").Pfl.HasExtDPP>.ret in
    def _dpp_gfx9 :
      VOP_DPP_Real<!cast<VOP1_DPP_Pseudo>(NAME#"_dpp"), SIEncodingFamily.GFX9>,
      VOP1_DPPe<op{7-0}, !cast<VOP1_DPP_Pseudo>(NAME#"_dpp")>;

}

defm V_SCREEN_PARTITION_4SE_B32 : VOP1_Real_gfx9 <0x37>;
