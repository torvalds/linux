//===-- VOP3Instructions.td - Vector Instruction Defintions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// VOP3 Classes
//===----------------------------------------------------------------------===//

class getVOP3ModPat<VOPProfile P, SDPatternOperator node> {
  dag src0 = !if(P.HasOMod,
    (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omod),
    (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp));

  list<dag> ret3 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT src0),
          (P.Src1VT (VOP3Mods P.Src1VT:$src1, i32:$src1_modifiers)),
          (P.Src2VT (VOP3Mods P.Src2VT:$src2, i32:$src2_modifiers))))];

  list<dag> ret2 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT src0),
          (P.Src1VT (VOP3Mods P.Src1VT:$src1, i32:$src1_modifiers))))];

  list<dag> ret1 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT src0)))];

  list<dag> ret = !if(!eq(P.NumSrcArgs, 3), ret3,
                  !if(!eq(P.NumSrcArgs, 2), ret2,
                  ret1));
}

class getVOP3PModPat<VOPProfile P, SDPatternOperator node> {
  list<dag> ret3 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT !if(P.HasClamp, (VOP3PMods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp),
                                    (VOP3PMods P.Src0VT:$src0, i32:$src0_modifiers))),
          (P.Src1VT (VOP3PMods P.Src1VT:$src1, i32:$src1_modifiers)),
          (P.Src2VT (VOP3PMods P.Src2VT:$src2, i32:$src2_modifiers))))];

  list<dag> ret2 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret !if(P.HasClamp, (P.Src0VT (VOP3PMods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp)),
                          (P.Src0VT (VOP3PMods P.Src0VT:$src0, i32:$src0_modifiers))),
          (P.Src1VT (VOP3PMods P.Src1VT:$src1, i32:$src1_modifiers))))];

  list<dag> ret1 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT (VOP3PMods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp))))];

  list<dag> ret = !if(!eq(P.NumSrcArgs, 3), ret3,
                  !if(!eq(P.NumSrcArgs, 2), ret2,
                  ret1));
}

class getVOP3OpSelPat<VOPProfile P, SDPatternOperator node> {
  list<dag> ret3 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT !if(P.HasClamp, (VOP3OpSel0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp),
                                    (VOP3OpSel P.Src0VT:$src0, i32:$src0_modifiers))),
          (P.Src1VT (VOP3OpSel P.Src1VT:$src1, i32:$src1_modifiers)),
          (P.Src2VT (VOP3OpSel P.Src2VT:$src2, i32:$src2_modifiers))))];

  list<dag> ret2 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret !if(P.HasClamp, (P.Src0VT (VOP3OpSel0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp)),
                          (P.Src0VT (VOP3OpSel P.Src0VT:$src0, i32:$src0_modifiers))),
          (P.Src1VT (VOP3OpSel P.Src1VT:$src1, i32:$src1_modifiers))))];

  list<dag> ret1 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT (VOP3OpSel0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp))))];

  list<dag> ret = !if(!eq(P.NumSrcArgs, 3), ret3,
                  !if(!eq(P.NumSrcArgs, 2), ret2,
                  ret1));
}

class getVOP3OpSelModPat<VOPProfile P, SDPatternOperator node> {
  list<dag> ret3 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT !if(P.HasClamp, (VOP3OpSelMods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp),
                                    (VOP3OpSelMods P.Src0VT:$src0, i32:$src0_modifiers))),
          (P.Src1VT (VOP3OpSelMods P.Src1VT:$src1, i32:$src1_modifiers)),
          (P.Src2VT (VOP3OpSelMods P.Src2VT:$src2, i32:$src2_modifiers))))];

  list<dag> ret2 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret !if(P.HasClamp, (P.Src0VT (VOP3OpSelMods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp)),
                          (P.Src0VT (VOP3OpSelMods P.Src0VT:$src0, i32:$src0_modifiers))),
          (P.Src1VT (VOP3OpSelMods P.Src1VT:$src1, i32:$src1_modifiers))))];

  list<dag> ret1 = [(set P.DstVT:$vdst,
    (DivergentFragOrOp<node, P>.ret (P.Src0VT (VOP3OpSelMods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp))))];

  list<dag> ret = !if(!eq(P.NumSrcArgs, 3), ret3,
                  !if(!eq(P.NumSrcArgs, 2), ret2,
                  ret1));
}

class getVOP3Pat<VOPProfile P, SDPatternOperator node> {
  list<dag> ret3 = [(set P.DstVT:$vdst, (DivergentFragOrOp<node, P>.ret P.Src0VT:$src0, P.Src1VT:$src1, P.Src2VT:$src2))];
  list<dag> ret2 = [(set P.DstVT:$vdst, (DivergentFragOrOp<node, P>.ret P.Src0VT:$src0, P.Src1VT:$src1))];
  list<dag> ret1 = [(set P.DstVT:$vdst, (DivergentFragOrOp<node, P>.ret P.Src0VT:$src0))];
  list<dag> ret = !if(!eq(P.NumSrcArgs, 3), ret3,
                  !if(!eq(P.NumSrcArgs, 2), ret2,
                  ret1));
}

class getVOP3ClampPat<VOPProfile P, SDPatternOperator node> {
  list<dag> ret3 = [(set P.DstVT:$vdst, (node P.Src0VT:$src0, P.Src1VT:$src1, P.Src2VT:$src2, i1:$clamp))];
  list<dag> ret2 = [(set P.DstVT:$vdst, (node P.Src0VT:$src0, P.Src1VT:$src1, i1:$clamp))];
  list<dag> ret1 = [(set P.DstVT:$vdst, (node P.Src0VT:$src0, i1:$clamp))];
  list<dag> ret = !if(!eq(P.NumSrcArgs, 3), ret3,
                  !if(!eq(P.NumSrcArgs, 2), ret2,
                  ret1));
}

class VOP3Inst<string OpName, VOPProfile P, SDPatternOperator node = null_frag, bit VOP3Only = 0> :
  VOP3_Pseudo<OpName, P,
    !if(P.HasOpSel,
        !if(P.HasModifiers,
            getVOP3OpSelModPat<P, node>.ret,
            getVOP3OpSelPat<P, node>.ret),
        !if(P.HasModifiers,
            getVOP3ModPat<P, node>.ret,
            !if(P.HasIntClamp,
                getVOP3ClampPat<P, node>.ret,
                getVOP3Pat<P, node>.ret))),
    VOP3Only, 0, P.HasOpSel> {

  let IntClamp = P.HasIntClamp;
  let AsmMatchConverter =
    !if(P.HasOpSel,
        "cvtVOP3OpSel",
        !if(!or(P.HasModifiers, !or(P.HasOMod, P.HasIntClamp)),
            "cvtVOP3",
            ""));
}

// Special case for v_div_fmas_{f32|f64}, since it seems to be the
// only VOP instruction that implicitly reads VCC.
let Asm64 = " $vdst, $src0_modifiers, $src1_modifiers, $src2_modifiers$clamp$omod" in {
def VOP_F32_F32_F32_F32_VCC : VOPProfile<[f32, f32, f32, f32]> {
  let Outs64 = (outs DstRC.RegClass:$vdst);
}
def VOP_F64_F64_F64_F64_VCC : VOPProfile<[f64, f64, f64, f64]> {
  let Outs64 = (outs DstRC.RegClass:$vdst);
}
}

class getVOP3VCC<VOPProfile P, SDPatternOperator node> {
  list<dag> ret =
    [(set P.DstVT:$vdst,
      (node (P.Src0VT (VOP3Mods0 P.Src0VT:$src0, i32:$src0_modifiers, i1:$clamp, i32:$omod)),
            (P.Src1VT (VOP3Mods P.Src1VT:$src1, i32:$src1_modifiers)),
            (P.Src2VT (VOP3Mods P.Src2VT:$src2, i32:$src2_modifiers)),
            (i1 VCC)))];
}

class VOP3Features<bit Clamp, bit OpSel, bit Packed> {
  bit HasClamp = Clamp;
  bit HasOpSel = OpSel;
  bit IsPacked = Packed;
}

def VOP3_REGULAR : VOP3Features<0, 0, 0>;
def VOP3_CLAMP   : VOP3Features<1, 0, 0>;
def VOP3_OPSEL   : VOP3Features<1, 1, 0>;
def VOP3_PACKED  : VOP3Features<1, 1, 1>;

class VOP3_Profile<VOPProfile P, VOP3Features Features = VOP3_REGULAR> : VOPProfile<P.ArgVT> {

  let HasClamp = !if(Features.HasClamp, 1, P.HasClamp);
  let HasOpSel = !if(Features.HasOpSel, 1, P.HasOpSel);
  let IsPacked = !if(Features.IsPacked, 1, P.IsPacked);

  let HasModifiers = !if(Features.IsPacked, 1, P.HasModifiers);

  // FIXME: Hack to stop printing _e64
  let Outs64 = (outs DstRC.RegClass:$vdst);
  let Asm64 =
    " " # !if(Features.HasOpSel,
              getAsmVOP3OpSel<NumSrcArgs,
                              HasIntClamp,
                              HasSrc0FloatMods,
                              HasSrc1FloatMods,
                              HasSrc2FloatMods>.ret,
              !if(Features.HasClamp,
                  getAsm64<HasDst, NumSrcArgs, HasIntClamp,
                           HasModifiers, HasOMod, DstVT>.ret,
                  P.Asm64));
  let NeedPatGen = P.NeedPatGen;
}

class VOP3b_Profile<ValueType vt> : VOPProfile<[vt, vt, vt, vt]> {
  // v_div_scale_{f32|f64} do not support input modifiers.
  let HasModifiers = 0;
  let HasOMod = 0;
  let Outs64 = (outs DstRC:$vdst, SReg_64:$sdst);
  let Asm64 = " $vdst, $sdst, $src0, $src1, $src2";
}

def VOP3b_F32_I1_F32_F32_F32 : VOP3b_Profile<f32> {
  // FIXME: Hack to stop printing _e64
  let DstRC = RegisterOperand<VGPR_32>;
}

def VOP3b_F64_I1_F64_F64_F64 : VOP3b_Profile<f64> {
  // FIXME: Hack to stop printing _e64
  let DstRC = RegisterOperand<VReg_64>;
}

def VOP3b_I64_I1_I32_I32_I64 : VOPProfile<[i64, i32, i32, i64]> {
  let HasClamp = 1;

  // FIXME: Hack to stop printing _e64
  let DstRC = RegisterOperand<VReg_64>;

  let Outs64 = (outs DstRC:$vdst, SReg_64:$sdst);
  let Asm64 = " $vdst, $sdst, $src0, $src1, $src2$clamp";
}

//===----------------------------------------------------------------------===//
// VOP3 INTERP
//===----------------------------------------------------------------------===//

class VOP3Interp<string OpName, VOPProfile P, list<dag> pattern = []> :
                 VOP3_Pseudo<OpName, P, pattern> {
  let AsmMatchConverter = "cvtVOP3Interp";
}

def VOP3_INTERP : VOPProfile<[f32, f32, i32, untyped]> {
  let Ins64 = (ins Src0Mod:$src0_modifiers, VRegSrc_32:$src0,
                   Attr:$attr, AttrChan:$attrchan,
                   clampmod:$clamp, omod:$omod);

  let Asm64 = "$vdst, $src0_modifiers, $attr$attrchan$clamp$omod";
}

def VOP3_INTERP_MOV : VOPProfile<[f32, i32, i32, untyped]> {
  let Ins64 = (ins InterpSlot:$src0,
                   Attr:$attr, AttrChan:$attrchan,
                   clampmod:$clamp, omod:$omod);

  let Asm64 = "$vdst, $src0, $attr$attrchan$clamp$omod";

  let HasClamp = 1;
}

class getInterp16Asm <bit HasSrc2, bit HasOMod> {
  string src2 = !if(HasSrc2, ", $src2_modifiers", "");
  string omod = !if(HasOMod, "$omod", "");
  string ret =
    " $vdst, $src0_modifiers, $attr$attrchan"#src2#"$high$clamp"#omod;
}

class getInterp16Ins <bit HasSrc2, bit HasOMod,
                      Operand Src0Mod, Operand Src2Mod> {
  dag ret = !if(HasSrc2,
                !if(HasOMod,
                    (ins Src0Mod:$src0_modifiers, VRegSrc_32:$src0,
                         Attr:$attr, AttrChan:$attrchan,
                         Src2Mod:$src2_modifiers, VRegSrc_32:$src2,
                         highmod:$high, clampmod:$clamp, omod:$omod),
                    (ins Src0Mod:$src0_modifiers, VRegSrc_32:$src0,
                         Attr:$attr, AttrChan:$attrchan,
                         Src2Mod:$src2_modifiers, VRegSrc_32:$src2,
                         highmod:$high, clampmod:$clamp)
                ),
                (ins Src0Mod:$src0_modifiers, VRegSrc_32:$src0,
                     Attr:$attr, AttrChan:$attrchan,
                     highmod:$high, clampmod:$clamp, omod:$omod)
            );
}

class VOP3_INTERP16 <list<ValueType> ArgVT> : VOPProfile<ArgVT> {

  let HasOMod = !if(!eq(DstVT.Value, f16.Value), 0, 1);
  let HasHigh = 1;

  let Outs64 = (outs VGPR_32:$vdst);
  let Ins64 = getInterp16Ins<HasSrc2, HasOMod, Src0Mod, Src2Mod>.ret;
  let Asm64 = getInterp16Asm<HasSrc2, HasOMod>.ret;
}

//===----------------------------------------------------------------------===//
// VOP3 Instructions
//===----------------------------------------------------------------------===//

let isCommutable = 1 in {

def V_MAD_LEGACY_F32 : VOP3Inst <"v_mad_legacy_f32", VOP3_Profile<VOP_F32_F32_F32_F32>>;
def V_MAD_F32 : VOP3Inst <"v_mad_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, fmad>;
def V_MAD_I32_I24 : VOP3Inst <"v_mad_i32_i24", VOP3_Profile<VOP_I32_I32_I32_I32, VOP3_CLAMP>>;
def V_MAD_U32_U24 : VOP3Inst <"v_mad_u32_u24", VOP3_Profile<VOP_I32_I32_I32_I32, VOP3_CLAMP>>;
def V_FMA_F32 : VOP3Inst <"v_fma_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, fma>;
def V_LERP_U8 : VOP3Inst <"v_lerp_u8", VOP3_Profile<VOP_I32_I32_I32_I32>, int_amdgcn_lerp>;

let SchedRW = [WriteDoubleAdd] in {
let FPDPRounding = 1 in {
def V_FMA_F64 : VOP3Inst <"v_fma_f64", VOP3_Profile<VOP_F64_F64_F64_F64>, fma>;
def V_ADD_F64 : VOP3Inst <"v_add_f64", VOP3_Profile<VOP_F64_F64_F64>, fadd, 1>;
def V_MUL_F64 : VOP3Inst <"v_mul_f64", VOP3_Profile<VOP_F64_F64_F64>, fmul, 1>;
} // End FPDPRounding = 1
def V_MIN_F64 : VOP3Inst <"v_min_f64", VOP3_Profile<VOP_F64_F64_F64>, fminnum_like, 1>;
def V_MAX_F64 : VOP3Inst <"v_max_f64", VOP3_Profile<VOP_F64_F64_F64>, fmaxnum_like, 1>;
} // End SchedRW = [WriteDoubleAdd]

let SchedRW = [WriteQuarterRate32] in {
def V_MUL_LO_U32 : VOP3Inst <"v_mul_lo_u32", VOP3_Profile<VOP_I32_I32_I32>>;
def V_MUL_HI_U32 : VOP3Inst <"v_mul_hi_u32", VOP3_Profile<VOP_I32_I32_I32>, mulhu>;
def V_MUL_LO_I32 : VOP3Inst <"v_mul_lo_i32", VOP3_Profile<VOP_I32_I32_I32>>;
def V_MUL_HI_I32 : VOP3Inst <"v_mul_hi_i32", VOP3_Profile<VOP_I32_I32_I32>, mulhs>;
} // End SchedRW = [WriteQuarterRate32]

let Uses = [VCC, EXEC] in {
// v_div_fmas_f32:
//   result = src0 * src1 + src2
//   if (vcc)
//     result *= 2^32
//
def V_DIV_FMAS_F32 : VOP3_Pseudo <"v_div_fmas_f32", VOP_F32_F32_F32_F32_VCC,
  getVOP3VCC<VOP_F32_F32_F32_F32_VCC, AMDGPUdiv_fmas>.ret> {
  let SchedRW = [WriteFloatFMA];
}
// v_div_fmas_f64:
//   result = src0 * src1 + src2
//   if (vcc)
//     result *= 2^64
//
def V_DIV_FMAS_F64 : VOP3_Pseudo <"v_div_fmas_f64", VOP_F64_F64_F64_F64_VCC,
  getVOP3VCC<VOP_F64_F64_F64_F64_VCC, AMDGPUdiv_fmas>.ret> {
  let SchedRW = [WriteDouble];
  let FPDPRounding = 1;
}
} // End Uses = [VCC, EXEC]

} // End isCommutable = 1

def V_CUBEID_F32 : VOP3Inst <"v_cubeid_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, int_amdgcn_cubeid>;
def V_CUBESC_F32 : VOP3Inst <"v_cubesc_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, int_amdgcn_cubesc>;
def V_CUBETC_F32 : VOP3Inst <"v_cubetc_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, int_amdgcn_cubetc>;
def V_CUBEMA_F32 : VOP3Inst <"v_cubema_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, int_amdgcn_cubema>;
def V_BFE_U32 : VOP3Inst <"v_bfe_u32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUbfe_u32>;
def V_BFE_I32 : VOP3Inst <"v_bfe_i32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUbfe_i32>;
def V_BFI_B32 : VOP3Inst <"v_bfi_b32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUbfi>;
def V_ALIGNBIT_B32 : VOP3Inst <"v_alignbit_b32", VOP3_Profile<VOP_I32_I32_I32_I32>, int_amdgcn_alignbit>;
def V_ALIGNBYTE_B32 : VOP3Inst <"v_alignbyte_b32", VOP3_Profile<VOP_I32_I32_I32_I32>, int_amdgcn_alignbyte>;
def V_MIN3_F32 : VOP3Inst <"v_min3_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, AMDGPUfmin3>;
def V_MIN3_I32 : VOP3Inst <"v_min3_i32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUsmin3>;
def V_MIN3_U32 : VOP3Inst <"v_min3_u32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUumin3>;
def V_MAX3_F32 : VOP3Inst <"v_max3_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, AMDGPUfmax3>;
def V_MAX3_I32 : VOP3Inst <"v_max3_i32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUsmax3>;
def V_MAX3_U32 : VOP3Inst <"v_max3_u32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUumax3>;
def V_MED3_F32 : VOP3Inst <"v_med3_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, AMDGPUfmed3>;
def V_MED3_I32 : VOP3Inst <"v_med3_i32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUsmed3>;
def V_MED3_U32 : VOP3Inst <"v_med3_u32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUumed3>;
def V_SAD_U8 : VOP3Inst <"v_sad_u8", VOP3_Profile<VOP_I32_I32_I32_I32, VOP3_CLAMP>>;
def V_SAD_HI_U8 : VOP3Inst <"v_sad_hi_u8", VOP3_Profile<VOP_I32_I32_I32_I32, VOP3_CLAMP>>;
def V_SAD_U16 : VOP3Inst <"v_sad_u16", VOP3_Profile<VOP_I32_I32_I32_I32, VOP3_CLAMP>>;
def V_SAD_U32 : VOP3Inst <"v_sad_u32", VOP3_Profile<VOP_I32_I32_I32_I32, VOP3_CLAMP>>;
def V_CVT_PK_U8_F32 : VOP3Inst<"v_cvt_pk_u8_f32", VOP3_Profile<VOP_I32_F32_I32_I32>, int_amdgcn_cvt_pk_u8_f32>;
def V_DIV_FIXUP_F32 : VOP3Inst <"v_div_fixup_f32", VOP3_Profile<VOP_F32_F32_F32_F32>, AMDGPUdiv_fixup>;

let SchedRW = [WriteDoubleAdd], FPDPRounding = 1 in {
def V_DIV_FIXUP_F64 : VOP3Inst <"v_div_fixup_f64", VOP3_Profile<VOP_F64_F64_F64_F64>, AMDGPUdiv_fixup>;
def V_LDEXP_F64 : VOP3Inst <"v_ldexp_f64", VOP3_Profile<VOP_F64_F64_I32>, AMDGPUldexp, 1>;
} // End SchedRW = [WriteDoubleAdd], FPDPRounding = 1

def V_DIV_SCALE_F32 : VOP3_Pseudo <"v_div_scale_f32", VOP3b_F32_I1_F32_F32_F32, [], 1> {
  let SchedRW = [WriteFloatFMA, WriteSALU];
  let AsmMatchConverter = "";
}

// Double precision division pre-scale.
def V_DIV_SCALE_F64 : VOP3_Pseudo <"v_div_scale_f64", VOP3b_F64_I1_F64_F64_F64, [], 1> {
  let SchedRW = [WriteDouble, WriteSALU];
  let AsmMatchConverter = "";
  let FPDPRounding = 1;
}

def V_MSAD_U8 : VOP3Inst <"v_msad_u8", VOP3_Profile<VOP_I32_I32_I32_I32, VOP3_CLAMP>>;

let Constraints = "@earlyclobber $vdst" in {
def V_MQSAD_PK_U16_U8 : VOP3Inst <"v_mqsad_pk_u16_u8", VOP3_Profile<VOP_I64_I64_I32_I64, VOP3_CLAMP>>;
} // End Constraints = "@earlyclobber $vdst"

def V_TRIG_PREOP_F64 : VOP3Inst <"v_trig_preop_f64", VOP3_Profile<VOP_F64_F64_I32>, AMDGPUtrig_preop> {
  let SchedRW = [WriteDouble];
}

let SchedRW = [Write64Bit] in {
// These instructions only exist on SI and CI
let SubtargetPredicate = isSICI, Predicates = [isSICI] in {
def V_LSHL_B64 : VOP3Inst <"v_lshl_b64", VOP3_Profile<VOP_PAT_GEN<VOP_I64_I64_I32>>, shl>;
def V_LSHR_B64 : VOP3Inst <"v_lshr_b64", VOP3_Profile<VOP_PAT_GEN<VOP_I64_I64_I32>>, srl>;
def V_ASHR_I64 : VOP3Inst <"v_ashr_i64", VOP3_Profile<VOP_PAT_GEN<VOP_I64_I64_I32>>, sra>;
def V_MULLIT_F32 : VOP3Inst <"v_mullit_f32", VOP3_Profile<VOP_F32_F32_F32_F32>>;
} // End SubtargetPredicate = isSICI, Predicates = [isSICI]

let SubtargetPredicate = isVI in {
def V_LSHLREV_B64 : VOP3Inst <"v_lshlrev_b64", VOP3_Profile<VOP_I64_I32_I64>>;
def V_LSHRREV_B64 : VOP3Inst <"v_lshrrev_b64", VOP3_Profile<VOP_I64_I32_I64>>;
def V_ASHRREV_I64 : VOP3Inst <"v_ashrrev_i64", VOP3_Profile<VOP_I64_I32_I64>>;
} // End SubtargetPredicate = isVI
} // End SchedRW = [Write64Bit]

let Predicates = [isVI] in {
def : GCNPat <
 (getDivergentFrag<shl>.ret i64:$x, i32:$y),
 (V_LSHLREV_B64 $y, $x)
>;
def : AMDGPUPat <
 (getDivergentFrag<srl>.ret i64:$x, i32:$y),
 (V_LSHRREV_B64 $y, $x)
>;
def : AMDGPUPat <
 (getDivergentFrag<sra>.ret i64:$x, i32:$y),
 (V_ASHRREV_I64 $y, $x)
>;
}


let SubtargetPredicate = isCIVI in {

let Constraints = "@earlyclobber $vdst", SchedRW = [WriteQuarterRate32] in {
def V_QSAD_PK_U16_U8 : VOP3Inst <"v_qsad_pk_u16_u8", VOP3_Profile<VOP_I64_I64_I32_I64, VOP3_CLAMP>>;
def V_MQSAD_U32_U8 : VOP3Inst <"v_mqsad_u32_u8", VOP3_Profile<VOP_V4I32_I64_I32_V4I32, VOP3_CLAMP>>;
} // End Constraints = "@earlyclobber $vdst", SchedRW = [WriteQuarterRate32]

let isCommutable = 1 in {
let SchedRW = [WriteQuarterRate32, WriteSALU] in {
def V_MAD_U64_U32 : VOP3Inst <"v_mad_u64_u32", VOP3b_I64_I1_I32_I32_I64>;
def V_MAD_I64_I32 : VOP3Inst <"v_mad_i64_i32", VOP3b_I64_I1_I32_I32_I64>;
} // End SchedRW = [WriteDouble, WriteSALU]
} // End isCommutable = 1

} // End SubtargetPredicate = isCIVI


def V_DIV_FIXUP_F16 : VOP3Inst <"v_div_fixup_f16", VOP3_Profile<VOP_F16_F16_F16_F16>, AMDGPUdiv_fixup> {
  let Predicates = [Has16BitInsts, isVIOnly];
  let FPDPRounding = 1;
}
def V_DIV_FIXUP_F16_gfx9 : VOP3Inst <"v_div_fixup_f16_gfx9",
                                      VOP3_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL>, AMDGPUdiv_fixup> {
  let renamedInGFX9 = 1;
  let Predicates = [Has16BitInsts, isGFX9];
  let FPDPRounding = 1;
}

def V_FMA_F16 : VOP3Inst <"v_fma_f16", VOP3_Profile<VOP_F16_F16_F16_F16>, fma> {
  let Predicates = [Has16BitInsts, isVIOnly];
  let FPDPRounding = 1;
}
def V_FMA_F16_gfx9 : VOP3Inst <"v_fma_f16_gfx9", VOP3_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL>, fma> {
  let renamedInGFX9 = 1;
  let Predicates = [Has16BitInsts, isGFX9];
  let FPDPRounding = 1;
}

let SubtargetPredicate = Has16BitInsts, isCommutable = 1 in {

let renamedInGFX9 = 1 in {
def V_MAD_U16 : VOP3Inst <"v_mad_u16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_CLAMP>>;
def V_MAD_I16 : VOP3Inst <"v_mad_i16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_CLAMP>>;
let FPDPRounding = 1 in {
def V_MAD_F16 : VOP3Inst <"v_mad_f16", VOP3_Profile<VOP_F16_F16_F16_F16>, fmad>;
let Uses = [M0, EXEC] in {
def V_INTERP_P2_F16 : VOP3Interp <"v_interp_p2_f16", VOP3_INTERP16<[f16, f32, i32, f32]>>;
} // End Uses = [M0, EXEC]
} // End FPDPRounding = 1
} // End renamedInGFX9 = 1

let SubtargetPredicate = isGFX9 in {
def V_MAD_F16_gfx9   : VOP3Inst <"v_mad_f16_gfx9", VOP3_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL>> {
  let FPDPRounding = 1;
}
def V_MAD_U16_gfx9   : VOP3Inst <"v_mad_u16_gfx9", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>>;
def V_MAD_I16_gfx9   : VOP3Inst <"v_mad_i16_gfx9", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>>;
def V_INTERP_P2_F16_gfx9 : VOP3Interp <"v_interp_p2_f16_gfx9", VOP3_INTERP16<[f16, f32, i32, f32]>>;
} // End SubtargetPredicate = isGFX9

let Uses = [M0, EXEC], FPDPRounding = 1 in {
def V_INTERP_P1LL_F16 : VOP3Interp <"v_interp_p1ll_f16", VOP3_INTERP16<[f32, f32, i32, untyped]>>;
def V_INTERP_P1LV_F16 : VOP3Interp <"v_interp_p1lv_f16", VOP3_INTERP16<[f32, f32, i32, f16]>>;
} // End Uses = [M0, EXEC], FPDPRounding = 1

} // End SubtargetPredicate = Has16BitInsts, isCommutable = 1

let SubtargetPredicate = isVI in {
def V_INTERP_P1_F32_e64  : VOP3Interp <"v_interp_p1_f32", VOP3_INTERP>;
def V_INTERP_P2_F32_e64  : VOP3Interp <"v_interp_p2_f32", VOP3_INTERP>;
def V_INTERP_MOV_F32_e64 : VOP3Interp <"v_interp_mov_f32", VOP3_INTERP_MOV>;

def V_PERM_B32 : VOP3Inst <"v_perm_b32", VOP3_Profile<VOP_I32_I32_I32_I32>, AMDGPUperm>;
} // End SubtargetPredicate = isVI

let Predicates = [Has16BitInsts] in {

multiclass Ternary_i16_Pats <SDPatternOperator op1, SDPatternOperator op2,
                             Instruction inst, SDPatternOperator op3> {
def : GCNPat <
  (op2 (op1 i16:$src0, i16:$src1), i16:$src2),
  (inst i16:$src0, i16:$src1, i16:$src2, (i1 0))
>;

}

defm: Ternary_i16_Pats<mul, add, V_MAD_U16, zext>;
defm: Ternary_i16_Pats<mul, add, V_MAD_I16, sext>;

} // End Predicates = [Has16BitInsts]

class ThreeOpFrag<SDPatternOperator op1, SDPatternOperator op2> : PatFrag<
  (ops node:$x, node:$y, node:$z),
  // When the inner operation is used multiple times, selecting 3-op
  // instructions may still be beneficial -- if the other users can be
  // combined similarly. Let's be conservative for now.
  (op2 (HasOneUseBinOp<op1> node:$x, node:$y), node:$z),
  [{
    // Only use VALU ops when the result is divergent.
    if (!N->isDivergent())
      return false;

    // Check constant bus limitations.
    //
    // Note: Use !isDivergent as a conservative proxy for whether the value
    //       is in an SGPR (uniform values can end up in VGPRs as well).
    unsigned ConstantBusUses = 0;
    for (unsigned i = 0; i < 3; ++i) {
      if (!Operands[i]->isDivergent() &&
          !isInlineImmediate(Operands[i].getNode())) {
        ConstantBusUses++;
        if (ConstantBusUses >= 2)
          return false;
      }
    }

    return true;
  }]
> {
  let PredicateCodeUsesOperands = 1;
}

let SubtargetPredicate = isGFX9 in {
def V_PACK_B32_F16 : VOP3Inst <"v_pack_b32_f16", VOP3_Profile<VOP_B32_F16_F16, VOP3_OPSEL>>;
def V_LSHL_ADD_U32 : VOP3Inst <"v_lshl_add_u32", VOP3_Profile<VOP_I32_I32_I32_I32>>;
def V_ADD_LSHL_U32 : VOP3Inst <"v_add_lshl_u32", VOP3_Profile<VOP_I32_I32_I32_I32>>;
def V_ADD3_U32 : VOP3Inst <"v_add3_u32", VOP3_Profile<VOP_I32_I32_I32_I32>>;
def V_LSHL_OR_B32 : VOP3Inst <"v_lshl_or_b32", VOP3_Profile<VOP_I32_I32_I32_I32>>;
def V_AND_OR_B32 : VOP3Inst <"v_and_or_b32", VOP3_Profile<VOP_I32_I32_I32_I32>>;
def V_OR3_B32 : VOP3Inst <"v_or3_b32", VOP3_Profile<VOP_I32_I32_I32_I32>>;

def V_XAD_U32 : VOP3Inst <"v_xad_u32", VOP3_Profile<VOP_I32_I32_I32_I32>>;

def V_MED3_F16 : VOP3Inst <"v_med3_f16", VOP3_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL>, AMDGPUfmed3>;
def V_MED3_I16 : VOP3Inst <"v_med3_i16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>, AMDGPUsmed3>;
def V_MED3_U16 : VOP3Inst <"v_med3_u16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>, AMDGPUumed3>;

def V_MIN3_F16 : VOP3Inst <"v_min3_f16", VOP3_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL>, AMDGPUfmin3>;
def V_MIN3_I16 : VOP3Inst <"v_min3_i16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>, AMDGPUsmin3>;
def V_MIN3_U16 : VOP3Inst <"v_min3_u16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>, AMDGPUumin3>;

def V_MAX3_F16 : VOP3Inst <"v_max3_f16", VOP3_Profile<VOP_F16_F16_F16_F16, VOP3_OPSEL>, AMDGPUfmax3>;
def V_MAX3_I16 : VOP3Inst <"v_max3_i16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>, AMDGPUsmax3>;
def V_MAX3_U16 : VOP3Inst <"v_max3_u16", VOP3_Profile<VOP_I16_I16_I16_I16, VOP3_OPSEL>, AMDGPUumax3>;

def V_ADD_I16 : VOP3Inst <"v_add_i16", VOP3_Profile<VOP_I16_I16_I16, VOP3_OPSEL>>;
def V_SUB_I16 : VOP3Inst <"v_sub_i16", VOP3_Profile<VOP_I16_I16_I16, VOP3_OPSEL>>;

def V_MAD_U32_U16 : VOP3Inst <"v_mad_u32_u16", VOP3_Profile<VOP_I32_I16_I16_I32, VOP3_OPSEL>>;
def V_MAD_I32_I16 : VOP3Inst <"v_mad_i32_i16", VOP3_Profile<VOP_I32_I16_I16_I32, VOP3_OPSEL>>;

def V_CVT_PKNORM_I16_F16 : VOP3Inst <"v_cvt_pknorm_i16_f16", VOP3_Profile<VOP_B32_F16_F16, VOP3_OPSEL>>;
def V_CVT_PKNORM_U16_F16 : VOP3Inst <"v_cvt_pknorm_u16_f16", VOP3_Profile<VOP_B32_F16_F16, VOP3_OPSEL>>;

def V_ADD_I32_gfx9 : VOP3Inst <"v_add_i32_gfx9", VOP3_Profile<VOP_I32_I32_I32>>;
def V_SUB_I32_gfx9 : VOP3Inst <"v_sub_i32_gfx9", VOP3_Profile<VOP_I32_I32_I32>>;


class ThreeOp_i32_Pats <SDPatternOperator op1, SDPatternOperator op2, Instruction inst> : GCNPat <
  // This matches (op2 (op1 i32:$src0, i32:$src1), i32:$src2) with conditions.
  (ThreeOpFrag<op1, op2> i32:$src0, i32:$src1, i32:$src2),
  (inst i32:$src0, i32:$src1, i32:$src2)
>;

def : ThreeOp_i32_Pats<shl, add, V_LSHL_ADD_U32>;
def : ThreeOp_i32_Pats<add, shl, V_ADD_LSHL_U32>;
def : ThreeOp_i32_Pats<add, add, V_ADD3_U32>;
def : ThreeOp_i32_Pats<shl, or, V_LSHL_OR_B32>;
def : ThreeOp_i32_Pats<and, or, V_AND_OR_B32>;
def : ThreeOp_i32_Pats<or, or, V_OR3_B32>;
def : ThreeOp_i32_Pats<xor, add, V_XAD_U32>;

} // End SubtargetPredicate = isGFX9

//===----------------------------------------------------------------------===//
// Integer Clamp Patterns
//===----------------------------------------------------------------------===//

class getClampPat<VOPProfile P, SDPatternOperator node> {
  dag ret3 = (P.DstVT (node P.Src0VT:$src0, P.Src1VT:$src1, P.Src2VT:$src2));
  dag ret2 = (P.DstVT (node P.Src0VT:$src0, P.Src1VT:$src1));
  dag ret1 = (P.DstVT (node P.Src0VT:$src0));
  dag ret = !if(!eq(P.NumSrcArgs, 3), ret3,
            !if(!eq(P.NumSrcArgs, 2), ret2,
            ret1));
}

class getClampRes<VOPProfile P, Instruction inst> {
  dag ret3 = (inst P.Src0VT:$src0, P.Src1VT:$src1, P.Src2VT:$src2, (i1 0));
  dag ret2 = (inst P.Src0VT:$src0, P.Src1VT:$src1, (i1 0));
  dag ret1 = (inst P.Src0VT:$src0, (i1 0));
  dag ret = !if(!eq(P.NumSrcArgs, 3), ret3,
            !if(!eq(P.NumSrcArgs, 2), ret2,
            ret1));
}

class IntClampPat<VOP3Inst inst, SDPatternOperator node> : GCNPat<
  getClampPat<inst.Pfl, node>.ret,
  getClampRes<inst.Pfl, inst>.ret
>;

def : IntClampPat<V_MAD_I32_I24, AMDGPUmad_i24>;
def : IntClampPat<V_MAD_U32_U24, AMDGPUmad_u24>;

def : IntClampPat<V_SAD_U8, int_amdgcn_sad_u8>;
def : IntClampPat<V_SAD_HI_U8, int_amdgcn_sad_hi_u8>;
def : IntClampPat<V_SAD_U16, int_amdgcn_sad_u16>;

def : IntClampPat<V_MSAD_U8, int_amdgcn_msad_u8>;
def : IntClampPat<V_MQSAD_PK_U16_U8, int_amdgcn_mqsad_pk_u16_u8>;

def : IntClampPat<V_QSAD_PK_U16_U8, int_amdgcn_qsad_pk_u16_u8>;
def : IntClampPat<V_MQSAD_U32_U8, int_amdgcn_mqsad_u32_u8>;

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SI
//===----------------------------------------------------------------------===//

let AssemblerPredicates = [isSICI], DecoderNamespace = "SICI" in {

multiclass VOP3_Real_si<bits<9> op> {
  def _si : VOP3_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.SI>,
            VOP3e_si <op, !cast<VOP3_Pseudo>(NAME).Pfl>;
}

multiclass VOP3be_Real_si<bits<9> op> {
  def _si : VOP3_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.SI>,
            VOP3be_si <op, !cast<VOP3_Pseudo>(NAME).Pfl>;
}

} // End AssemblerPredicates = [isSICI], DecoderNamespace = "SICI"

defm V_MAD_LEGACY_F32   : VOP3_Real_si <0x140>;
defm V_MAD_F32          : VOP3_Real_si <0x141>;
defm V_MAD_I32_I24      : VOP3_Real_si <0x142>;
defm V_MAD_U32_U24      : VOP3_Real_si <0x143>;
defm V_CUBEID_F32       : VOP3_Real_si <0x144>;
defm V_CUBESC_F32       : VOP3_Real_si <0x145>;
defm V_CUBETC_F32       : VOP3_Real_si <0x146>;
defm V_CUBEMA_F32       : VOP3_Real_si <0x147>;
defm V_BFE_U32          : VOP3_Real_si <0x148>;
defm V_BFE_I32          : VOP3_Real_si <0x149>;
defm V_BFI_B32          : VOP3_Real_si <0x14a>;
defm V_FMA_F32          : VOP3_Real_si <0x14b>;
defm V_FMA_F64          : VOP3_Real_si <0x14c>;
defm V_LERP_U8          : VOP3_Real_si <0x14d>;
defm V_ALIGNBIT_B32     : VOP3_Real_si <0x14e>;
defm V_ALIGNBYTE_B32    : VOP3_Real_si <0x14f>;
defm V_MULLIT_F32       : VOP3_Real_si <0x150>;
defm V_MIN3_F32         : VOP3_Real_si <0x151>;
defm V_MIN3_I32         : VOP3_Real_si <0x152>;
defm V_MIN3_U32         : VOP3_Real_si <0x153>;
defm V_MAX3_F32         : VOP3_Real_si <0x154>;
defm V_MAX3_I32         : VOP3_Real_si <0x155>;
defm V_MAX3_U32         : VOP3_Real_si <0x156>;
defm V_MED3_F32         : VOP3_Real_si <0x157>;
defm V_MED3_I32         : VOP3_Real_si <0x158>;
defm V_MED3_U32         : VOP3_Real_si <0x159>;
defm V_SAD_U8           : VOP3_Real_si <0x15a>;
defm V_SAD_HI_U8        : VOP3_Real_si <0x15b>;
defm V_SAD_U16          : VOP3_Real_si <0x15c>;
defm V_SAD_U32          : VOP3_Real_si <0x15d>;
defm V_CVT_PK_U8_F32    : VOP3_Real_si <0x15e>;
defm V_DIV_FIXUP_F32    : VOP3_Real_si <0x15f>;
defm V_DIV_FIXUP_F64    : VOP3_Real_si <0x160>;
defm V_LSHL_B64         : VOP3_Real_si <0x161>;
defm V_LSHR_B64         : VOP3_Real_si <0x162>;
defm V_ASHR_I64         : VOP3_Real_si <0x163>;
defm V_ADD_F64          : VOP3_Real_si <0x164>;
defm V_MUL_F64          : VOP3_Real_si <0x165>;
defm V_MIN_F64          : VOP3_Real_si <0x166>;
defm V_MAX_F64          : VOP3_Real_si <0x167>;
defm V_LDEXP_F64        : VOP3_Real_si <0x168>;
defm V_MUL_LO_U32       : VOP3_Real_si <0x169>;
defm V_MUL_HI_U32       : VOP3_Real_si <0x16a>;
defm V_MUL_LO_I32       : VOP3_Real_si <0x16b>;
defm V_MUL_HI_I32       : VOP3_Real_si <0x16c>;
defm V_DIV_SCALE_F32    : VOP3be_Real_si <0x16d>;
defm V_DIV_SCALE_F64    : VOP3be_Real_si <0x16e>;
defm V_DIV_FMAS_F32     : VOP3_Real_si <0x16f>;
defm V_DIV_FMAS_F64     : VOP3_Real_si <0x170>;
defm V_MSAD_U8          : VOP3_Real_si <0x171>;
defm V_MQSAD_PK_U16_U8  : VOP3_Real_si <0x173>;
defm V_TRIG_PREOP_F64   : VOP3_Real_si <0x174>;

//===----------------------------------------------------------------------===//
// CI
//===----------------------------------------------------------------------===//

multiclass VOP3_Real_ci<bits<9> op> {
  def _ci : VOP3_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.SI>,
            VOP3e_si <op, !cast<VOP3_Pseudo>(NAME).Pfl> {
    let AssemblerPredicates = [isCIOnly];
    let DecoderNamespace = "CI";
  }
}

multiclass VOP3be_Real_ci<bits<9> op> {
  def _ci : VOP3_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.SI>,
            VOP3be_si <op, !cast<VOP3_Pseudo>(NAME).Pfl> {
    let AssemblerPredicates = [isCIOnly];
    let DecoderNamespace = "CI";
  }
}

defm V_QSAD_PK_U16_U8   : VOP3_Real_ci <0x172>;
defm V_MQSAD_U32_U8     : VOP3_Real_ci <0x175>;
defm V_MAD_U64_U32      : VOP3be_Real_ci <0x176>;
defm V_MAD_I64_I32      : VOP3be_Real_ci <0x177>;

//===----------------------------------------------------------------------===//
// VI
//===----------------------------------------------------------------------===//

let AssemblerPredicates = [isVI], DecoderNamespace = "VI" in {

multiclass VOP3_Real_vi<bits<10> op> {
  def _vi : VOP3_Real<!cast<VOP_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP3e_vi <op, !cast<VOP_Pseudo>(NAME).Pfl>;
}

multiclass VOP3be_Real_vi<bits<10> op> {
  def _vi : VOP3_Real<!cast<VOP_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP3be_vi <op, !cast<VOP_Pseudo>(NAME).Pfl>;
}

multiclass VOP3OpSel_Real_gfx9<bits<10> op> {
  def _vi : VOP3_Real<!cast<VOP_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP3OpSel_gfx9 <op, !cast<VOP_Pseudo>(NAME).Pfl>;
}

multiclass VOP3Interp_Real_vi<bits<10> op> {
  def _vi : VOP3_Real<!cast<VOP_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP3Interp_vi <op, !cast<VOP_Pseudo>(NAME).Pfl>;
}

} // End AssemblerPredicates = [isVI], DecoderNamespace = "VI"

let AssemblerPredicates = [isVIOnly], DecoderNamespace = "VI" in {

multiclass VOP3_F16_Real_vi<bits<10> op> {
  def _vi : VOP3_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP3e_vi <op, !cast<VOP3_Pseudo>(NAME).Pfl>;
}

multiclass VOP3Interp_F16_Real_vi<bits<10> op> {
  def _vi : VOP3_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.VI>,
            VOP3Interp_vi <op, !cast<VOP3_Pseudo>(NAME).Pfl>;
}

} // End AssemblerPredicates = [isVIOnly], DecoderNamespace = "VI"

let AssemblerPredicates = [isGFX9], DecoderNamespace = "GFX9" in {

multiclass VOP3_F16_Real_gfx9<bits<10> op, string OpName, string AsmName> {
  def _gfx9 : VOP3_Real<!cast<VOP3_Pseudo>(OpName), SIEncodingFamily.GFX9>,
            VOP3e_vi <op, !cast<VOP3_Pseudo>(OpName).Pfl> {
              VOP3_Pseudo ps = !cast<VOP3_Pseudo>(OpName);
              let AsmString = AsmName # ps.AsmOperands;
            }
}

multiclass VOP3OpSel_F16_Real_gfx9<bits<10> op, string AsmName> {
  def _gfx9 : VOP3_Real<!cast<VOP3_Pseudo>(NAME), SIEncodingFamily.GFX9>,
            VOP3OpSel_gfx9 <op, !cast<VOP3_Pseudo>(NAME).Pfl> {
              VOP3_Pseudo ps = !cast<VOP3_Pseudo>(NAME);
              let AsmString = AsmName # ps.AsmOperands;
            }
}

multiclass VOP3Interp_F16_Real_gfx9<bits<10> op, string OpName, string AsmName> {
  def _gfx9 : VOP3_Real<!cast<VOP3_Pseudo>(OpName), SIEncodingFamily.GFX9>,
            VOP3Interp_vi <op, !cast<VOP3_Pseudo>(OpName).Pfl> {
              VOP3_Pseudo ps = !cast<VOP3_Pseudo>(OpName);
              let AsmString = AsmName # ps.AsmOperands;
            }
}

multiclass VOP3_Real_gfx9<bits<10> op, string AsmName> {
  def _gfx9 : VOP3_Real<!cast<VOP_Pseudo>(NAME), SIEncodingFamily.GFX9>,
              VOP3e_vi <op, !cast<VOP_Pseudo>(NAME).Pfl> {
              VOP_Pseudo ps = !cast<VOP_Pseudo>(NAME);
              let AsmString = AsmName # ps.AsmOperands;
            }
}

} // End AssemblerPredicates = [isGFX9], DecoderNamespace = "GFX9"

defm V_MAD_U64_U32      : VOP3be_Real_vi <0x1E8>;
defm V_MAD_I64_I32      : VOP3be_Real_vi <0x1E9>;

defm V_MAD_LEGACY_F32   : VOP3_Real_vi <0x1c0>;
defm V_MAD_F32          : VOP3_Real_vi <0x1c1>;
defm V_MAD_I32_I24      : VOP3_Real_vi <0x1c2>;
defm V_MAD_U32_U24      : VOP3_Real_vi <0x1c3>;
defm V_CUBEID_F32       : VOP3_Real_vi <0x1c4>;
defm V_CUBESC_F32       : VOP3_Real_vi <0x1c5>;
defm V_CUBETC_F32       : VOP3_Real_vi <0x1c6>;
defm V_CUBEMA_F32       : VOP3_Real_vi <0x1c7>;
defm V_BFE_U32          : VOP3_Real_vi <0x1c8>;
defm V_BFE_I32          : VOP3_Real_vi <0x1c9>;
defm V_BFI_B32          : VOP3_Real_vi <0x1ca>;
defm V_FMA_F32          : VOP3_Real_vi <0x1cb>;
defm V_FMA_F64          : VOP3_Real_vi <0x1cc>;
defm V_LERP_U8          : VOP3_Real_vi <0x1cd>;
defm V_ALIGNBIT_B32     : VOP3_Real_vi <0x1ce>;
defm V_ALIGNBYTE_B32    : VOP3_Real_vi <0x1cf>;
defm V_MIN3_F32         : VOP3_Real_vi <0x1d0>;
defm V_MIN3_I32         : VOP3_Real_vi <0x1d1>;
defm V_MIN3_U32         : VOP3_Real_vi <0x1d2>;
defm V_MAX3_F32         : VOP3_Real_vi <0x1d3>;
defm V_MAX3_I32         : VOP3_Real_vi <0x1d4>;
defm V_MAX3_U32         : VOP3_Real_vi <0x1d5>;
defm V_MED3_F32         : VOP3_Real_vi <0x1d6>;
defm V_MED3_I32         : VOP3_Real_vi <0x1d7>;
defm V_MED3_U32         : VOP3_Real_vi <0x1d8>;
defm V_SAD_U8           : VOP3_Real_vi <0x1d9>;
defm V_SAD_HI_U8        : VOP3_Real_vi <0x1da>;
defm V_SAD_U16          : VOP3_Real_vi <0x1db>;
defm V_SAD_U32          : VOP3_Real_vi <0x1dc>;
defm V_CVT_PK_U8_F32    : VOP3_Real_vi <0x1dd>;
defm V_DIV_FIXUP_F32    : VOP3_Real_vi <0x1de>;
defm V_DIV_FIXUP_F64    : VOP3_Real_vi <0x1df>;
defm V_DIV_SCALE_F32    : VOP3be_Real_vi <0x1e0>;
defm V_DIV_SCALE_F64    : VOP3be_Real_vi <0x1e1>;
defm V_DIV_FMAS_F32     : VOP3_Real_vi <0x1e2>;
defm V_DIV_FMAS_F64     : VOP3_Real_vi <0x1e3>;
defm V_MSAD_U8          : VOP3_Real_vi <0x1e4>;
defm V_QSAD_PK_U16_U8   : VOP3_Real_vi <0x1e5>;
defm V_MQSAD_PK_U16_U8  : VOP3_Real_vi <0x1e6>;
defm V_MQSAD_U32_U8     : VOP3_Real_vi <0x1e7>;

defm V_PERM_B32         : VOP3_Real_vi <0x1ed>;

defm V_MAD_F16          : VOP3_F16_Real_vi <0x1ea>;
defm V_MAD_U16          : VOP3_F16_Real_vi <0x1eb>;
defm V_MAD_I16          : VOP3_F16_Real_vi <0x1ec>;
defm V_FMA_F16          : VOP3_F16_Real_vi <0x1ee>;
defm V_DIV_FIXUP_F16    : VOP3_F16_Real_vi <0x1ef>;
defm V_INTERP_P2_F16    : VOP3Interp_F16_Real_vi <0x276>;

let FPDPRounding = 1 in {
defm V_MAD_LEGACY_F16       : VOP3_F16_Real_gfx9 <0x1ea, "V_MAD_F16",       "v_mad_legacy_f16">;
defm V_FMA_LEGACY_F16       : VOP3_F16_Real_gfx9 <0x1ee, "V_FMA_F16",       "v_fma_legacy_f16">;
defm V_DIV_FIXUP_LEGACY_F16 : VOP3_F16_Real_gfx9 <0x1ef, "V_DIV_FIXUP_F16", "v_div_fixup_legacy_f16">;
defm V_INTERP_P2_LEGACY_F16 : VOP3Interp_F16_Real_gfx9 <0x276, "V_INTERP_P2_F16", "v_interp_p2_legacy_f16">;
} // End FPDPRounding = 1

defm V_MAD_LEGACY_U16       : VOP3_F16_Real_gfx9 <0x1eb, "V_MAD_U16",       "v_mad_legacy_u16">;
defm V_MAD_LEGACY_I16       : VOP3_F16_Real_gfx9 <0x1ec, "V_MAD_I16",       "v_mad_legacy_i16">;

defm V_MAD_F16_gfx9         : VOP3OpSel_F16_Real_gfx9 <0x203, "v_mad_f16">;
defm V_MAD_U16_gfx9         : VOP3OpSel_F16_Real_gfx9 <0x204, "v_mad_u16">;
defm V_MAD_I16_gfx9         : VOP3OpSel_F16_Real_gfx9 <0x205, "v_mad_i16">;
defm V_FMA_F16_gfx9         : VOP3OpSel_F16_Real_gfx9 <0x206, "v_fma_f16">;
defm V_DIV_FIXUP_F16_gfx9   : VOP3OpSel_F16_Real_gfx9 <0x207, "v_div_fixup_f16">;
defm V_INTERP_P2_F16_gfx9   : VOP3Interp_F16_Real_gfx9 <0x277, "V_INTERP_P2_F16_gfx9", "v_interp_p2_f16">;

defm V_ADD_I32_gfx9         : VOP3_Real_gfx9 <0x29c, "v_add_i32">;
defm V_SUB_I32_gfx9         : VOP3_Real_gfx9 <0x29d, "v_sub_i32">;

defm V_INTERP_P1_F32_e64  : VOP3Interp_Real_vi <0x270>;
defm V_INTERP_P2_F32_e64  : VOP3Interp_Real_vi <0x271>;
defm V_INTERP_MOV_F32_e64 : VOP3Interp_Real_vi <0x272>;

defm V_INTERP_P1LL_F16  : VOP3Interp_Real_vi <0x274>;
defm V_INTERP_P1LV_F16  : VOP3Interp_Real_vi <0x275>;
defm V_ADD_F64          : VOP3_Real_vi <0x280>;
defm V_MUL_F64          : VOP3_Real_vi <0x281>;
defm V_MIN_F64          : VOP3_Real_vi <0x282>;
defm V_MAX_F64          : VOP3_Real_vi <0x283>;
defm V_LDEXP_F64        : VOP3_Real_vi <0x284>;
defm V_MUL_LO_U32       : VOP3_Real_vi <0x285>;

// removed from VI as identical to V_MUL_LO_U32
let isAsmParserOnly = 1 in {
defm V_MUL_LO_I32       : VOP3_Real_vi <0x285>;
}

defm V_MUL_HI_U32       : VOP3_Real_vi <0x286>;
defm V_MUL_HI_I32       : VOP3_Real_vi <0x287>;

defm V_READLANE_B32     : VOP3_Real_vi <0x289>;
defm V_WRITELANE_B32    : VOP3_Real_vi <0x28a>;

defm V_LSHLREV_B64      : VOP3_Real_vi <0x28f>;
defm V_LSHRREV_B64      : VOP3_Real_vi <0x290>;
defm V_ASHRREV_I64      : VOP3_Real_vi <0x291>;
defm V_TRIG_PREOP_F64   : VOP3_Real_vi <0x292>;

defm V_LSHL_ADD_U32 : VOP3_Real_vi <0x1fd>;
defm V_ADD_LSHL_U32 : VOP3_Real_vi <0x1fe>;
defm V_ADD3_U32 : VOP3_Real_vi <0x1ff>;
defm V_LSHL_OR_B32 : VOP3_Real_vi <0x200>;
defm V_AND_OR_B32 : VOP3_Real_vi <0x201>;
defm V_OR3_B32 : VOP3_Real_vi <0x202>;
defm V_PACK_B32_F16 : VOP3OpSel_Real_gfx9 <0x2a0>;

defm V_XAD_U32 : VOP3_Real_vi <0x1f3>;

defm V_MIN3_F16 : VOP3OpSel_Real_gfx9 <0x1f4>;
defm V_MIN3_I16 : VOP3OpSel_Real_gfx9 <0x1f5>;
defm V_MIN3_U16 : VOP3OpSel_Real_gfx9 <0x1f6>;

defm V_MAX3_F16 : VOP3OpSel_Real_gfx9 <0x1f7>;
defm V_MAX3_I16 : VOP3OpSel_Real_gfx9 <0x1f8>;
defm V_MAX3_U16 : VOP3OpSel_Real_gfx9 <0x1f9>;

defm V_MED3_F16 : VOP3OpSel_Real_gfx9 <0x1fa>;
defm V_MED3_I16 : VOP3OpSel_Real_gfx9 <0x1fb>;
defm V_MED3_U16 : VOP3OpSel_Real_gfx9 <0x1fc>;

defm V_ADD_I16  : VOP3OpSel_Real_gfx9 <0x29e>;
defm V_SUB_I16  : VOP3OpSel_Real_gfx9 <0x29f>;

defm V_MAD_U32_U16 : VOP3OpSel_Real_gfx9 <0x1f1>;
defm V_MAD_I32_I16 : VOP3OpSel_Real_gfx9 <0x1f2>;

defm V_CVT_PKNORM_I16_F16 : VOP3OpSel_Real_gfx9 <0x299>;
defm V_CVT_PKNORM_U16_F16 : VOP3OpSel_Real_gfx9 <0x29a>;
