//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Automatically generated file, please consult code owner before editing.
//===----------------------------------------------------------------------===//


// V5 Scalar Instructions.

def: Pat<(int_hexagon_S2_asr_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asr_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsatwh DoubleRegs:$src1),
         (S2_vsatwh DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpysu_up IntRegs:$src1, IntRegs:$src2),
         (M2_mpysu_up IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpysc_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpysc_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpysc_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpysc_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_cmpyi_whc DoubleRegs:$src1, IntRegs:$src2),
         (M4_cmpyi_whc DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_shuffoh DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_shuffoh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfmax IntRegs:$src1, IntRegs:$src2),
         (F2_sfmax IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vabswsat DoubleRegs:$src1),
         (A2_vabswsat DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_r IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_asr_i_r IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_p DoubleRegs:$src1, u6_0ImmPred:$src2),
         (S2_asr_i_p DoubleRegs:$src1, u6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_combineri IntRegs:$src1, s32_0ImmPred:$src2),
         (A4_combineri IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vpmpyh_acc DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_vpmpyh_acc DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vcmpy_s0_sat_i DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vcmpy_s0_sat_i DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_notp DoubleRegs:$src1),
         (A2_notp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_or_and PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_or_and PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2s_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_vmac2s_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2s_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_vmac2s_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_brevp DoubleRegs:$src1),
         (S2_brevp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_pmpyw_acc DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_pmpyw_acc DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_cl1 IntRegs:$src1),
         (S2_cl1 IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_cmplte IntRegs:$src1, IntRegs:$src2),
         (C4_cmplte IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyul_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyul_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vaddws DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vaddws DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_maxup DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_maxup DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmphgti DoubleRegs:$src1, s8_0ImmPred:$src2),
         (A4_vcmphgti DoubleRegs:$src1, s8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_interleave DoubleRegs:$src1),
         (S2_interleave DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmpyi_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vrcmpyi_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_abssat IntRegs:$src1),
         (A2_abssat IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmpwgtu DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmpwgtu DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpgtu IntRegs:$src1, IntRegs:$src2),
         (C2_cmpgtu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpgtp DoubleRegs:$src1, DoubleRegs:$src2),
         (C2_cmpgtp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmphgtui IntRegs:$src1, u32_0ImmPred:$src2),
         (A4_cmphgtui IntRegs:$src1, u32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpgti IntRegs:$src1, s32_0ImmPred:$src2),
         (C2_cmpgti IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyi IntRegs:$src1, IntRegs:$src2),
         (M2_mpyi IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2uw_chop DoubleRegs:$src1),
         (F2_conv_df2uw_chop DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpheq IntRegs:$src1, IntRegs:$src2),
         (A4_cmpheq IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_r_xacc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_lsr_i_r_xacc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vrcnegh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_vrcnegh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_extractup DoubleRegs:$src1, u6_0ImmPred:$src2, u6_0ImmPred:$src3),
         (S2_extractup DoubleRegs:$src1, u6_0ImmPred:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_ntstbit_r IntRegs:$src1, IntRegs:$src2),
         (S4_ntstbit_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_w2sf IntRegs:$src1),
         (F2_conv_w2sf IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_not PredRegs:$src1),
         (C2_not PredRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_tfrpr PredRegs:$src1),
         (C2_tfrpr PredRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpbgt IntRegs:$src1, IntRegs:$src2),
         (A4_cmpbgt IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asr_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_rcmpneqi IntRegs:$src1, s32_0ImmPred:$src2),
         (A4_rcmpneqi IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asl_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_subacc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_subacc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_orp DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_orp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_up IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_up IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_vh DoubleRegs:$src1, u4_0ImmPred:$src2),
         (S2_asr_i_vh DoubleRegs:$src1, u4_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_vw DoubleRegs:$src1, u5_0ImmPred:$src2),
         (S2_asr_i_vw DoubleRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpbgtu IntRegs:$src1, IntRegs:$src2),
         (A4_cmpbgtu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpbeq_any DoubleRegs:$src1, DoubleRegs:$src2),
         (A4_vcmpbeq_any DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpbgti IntRegs:$src1, s8_0ImmPred:$src2),
         (A4_cmpbgti IntRegs:$src1, s8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asl_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_lsr_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addsp IntRegs:$src1, DoubleRegs:$src2),
         (A2_addsp IntRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vxsubaddw DoubleRegs:$src1, DoubleRegs:$src2),
         (S4_vxsubaddw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpheqi DoubleRegs:$src1, s8_0ImmPred:$src2),
         (A4_vcmpheqi DoubleRegs:$src1, s8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vxsubaddh DoubleRegs:$src1, DoubleRegs:$src2),
         (S4_vxsubaddh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_pmpyw IntRegs:$src1, IntRegs:$src2),
         (M4_pmpyw IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsathb DoubleRegs:$src1),
         (S2_vsathb DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asr_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_pxorf PredRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (A2_pxorf PredRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsubub DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsubub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_p DoubleRegs:$src1, u6_0ImmPred:$src2),
         (S2_asl_i_p DoubleRegs:$src1, u6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_r IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_asl_i_r IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrminuw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrminuw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffma IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (F2_sffma IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_absp DoubleRegs:$src1),
         (A2_absp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_all8 PredRegs:$src1),
         (C2_all8 PredRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrminuh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrminuh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffma_lib IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (F2_sffma_lib IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyoh_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M4_vrmpyoh_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyoh_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M4_vrmpyoh_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_bitsset IntRegs:$src1, IntRegs:$src2),
         (C2_bitsset IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpysip IntRegs:$src1, u32_0ImmPred:$src2),
         (M2_mpysip IntRegs:$src1, u32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpysin IntRegs:$src1, u8_0ImmPred:$src2),
         (M2_mpysin IntRegs:$src1, u8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_boundscheck IntRegs:$src1, DoubleRegs:$src2),
         (A4_boundscheck IntRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vrmpybuu DoubleRegs:$src1, DoubleRegs:$src2),
         (M5_vrmpybuu DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_fastcorner9 PredRegs:$src1, PredRegs:$src2),
         (C4_fastcorner9 PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmpys_s1rp DoubleRegs:$src1, IntRegs:$src2),
         (M2_vrcmpys_s1rp DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subsat IntRegs:$src1, IntRegs:$src2),
         (A2_subsat IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_r IntRegs:$src1, IntRegs:$src2),
         (S2_asl_r_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_p DoubleRegs:$src1, IntRegs:$src2),
         (S2_asl_r_p DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vnavgh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vnavgh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_ud2df DoubleRegs:$src1),
         (F2_conv_ud2df DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vnavgw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vnavgw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asl_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_subi_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_subi_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vzxthw IntRegs:$src1),
         (S2_vzxthw IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfadd IntRegs:$src1, IntRegs:$src2),
         (F2_sfadd IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_sub IntRegs:$src1, IntRegs:$src2),
         (A2_sub IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2su_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_vmac2su_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2su_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_vmac2su_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_dpmpyss_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_dpmpyss_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_insert IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3, u5_0ImmPred:$src4),
         (S2_insert IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3, u5_0ImmPred:$src4)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_packhl IntRegs:$src1, IntRegs:$src2),
         (S2_packhl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpwgti DoubleRegs:$src1, s8_0ImmPred:$src2),
         (A4_vcmpwgti DoubleRegs:$src1, s8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavguwr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavguwr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asl_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svsubhs IntRegs:$src1, IntRegs:$src2),
         (A2_svsubhs IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_l16_hl IntRegs:$src1, IntRegs:$src2),
         (A2_addh_l16_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_and_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_and_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_d2df DoubleRegs:$src1),
         (F2_conv_d2df DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpgtui IntRegs:$src1, u32_0ImmPred:$src2),
         (C2_cmpgtui IntRegs:$src1, u32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vconj DoubleRegs:$src1),
         (A2_vconj DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_vw DoubleRegs:$src1, IntRegs:$src2),
         (S2_lsr_r_vw DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_vh DoubleRegs:$src1, IntRegs:$src2),
         (S2_lsr_r_vh DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_l16_hl IntRegs:$src1, IntRegs:$src2),
         (A2_subh_l16_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vxsubaddhr DoubleRegs:$src1, DoubleRegs:$src2),
         (S4_vxsubaddhr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_clbp DoubleRegs:$src1),
         (S2_clbp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_deinterleave DoubleRegs:$src1),
         (S2_deinterleave DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_any8 PredRegs:$src1),
         (C2_any8 PredRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_togglebit_r IntRegs:$src1, IntRegs:$src2),
         (S2_togglebit_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_togglebit_i IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_togglebit_i IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_uw2sf IntRegs:$src1),
         (F2_conv_uw2sf IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsathb_nopack DoubleRegs:$src1),
         (S2_vsathb_nopack DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmacs_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cmacs_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmacs_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cmacs_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacuhs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacuhs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacuhs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacuhs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_clrbit_r IntRegs:$src1, IntRegs:$src2),
         (S2_clrbit_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_or_andn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_or_andn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asl_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asl_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpwgtui DoubleRegs:$src1, u7_0ImmPred:$src2),
         (A4_vcmpwgtui DoubleRegs:$src1, u7_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyoh_acc_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M4_vrmpyoh_acc_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyoh_acc_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M4_vrmpyoh_acc_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrmaxh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrmaxh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmpbeq DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmpbeq DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmphgt DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmphgt DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vnavgwcr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vnavgwcr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmacr_s0c DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vrcmacr_s0c DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavgwcr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavgwcr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_p_xacc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asl_i_p_xacc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrmaxw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrmaxw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vnavghr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vnavghr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_cmpyi_wh DoubleRegs:$src1, IntRegs:$src2),
         (M4_cmpyi_wh DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_tfrsi s32_0ImmPred:$src1),
         (A2_tfrsi s32_0ImmPred:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asr_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svnavgh IntRegs:$src1, IntRegs:$src2),
         (A2_svnavgh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_r IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_lsr_i_r IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_vmac2 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmphgtui DoubleRegs:$src1, u7_0ImmPred:$src2),
         (A4_vcmphgtui DoubleRegs:$src1, u7_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svavgh IntRegs:$src1, IntRegs:$src2),
         (A2_svavgh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyeh_acc_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M4_vrmpyeh_acc_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyeh_acc_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M4_vrmpyeh_acc_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_p DoubleRegs:$src1, u6_0ImmPred:$src2),
         (S2_lsr_i_p DoubleRegs:$src1, u6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_combine_hl IntRegs:$src1, IntRegs:$src2),
         (A2_combine_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_up IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_up IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_combine_hh IntRegs:$src1, IntRegs:$src2),
         (A2_combine_hh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_negsat IntRegs:$src1),
         (A2_negsat IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_bitsplit IntRegs:$src1, IntRegs:$src2),
         (A4_bitsplit IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vabshsat DoubleRegs:$src1),
         (A2_vabshsat DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyui IntRegs:$src1, IntRegs:$src2),
         (M2_mpyui IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_l16_sat_ll IntRegs:$src1, IntRegs:$src2),
         (A2_addh_l16_sat_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyul_rs0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyul_rs0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmacr_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cmacr_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_or_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_or_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_mpyrr_addi u32_0ImmPred:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_mpyrr_addi u32_0ImmPred:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_or_andi IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3),
         (S4_or_andi IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_mpyrr_addr IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_mpyrr_addr IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmachs_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmachs_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmachs_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmachs_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmpyr_s0c DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vrcmpyr_s0c DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffixupn IntRegs:$src1, IntRegs:$src2),
         (F2_sffixupn IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vadduhs DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vadduhs DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsubuhs DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsubuhs DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_hl IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_hh IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_hh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_xorp DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_xorp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_tfrpcp DoubleRegs:$src1),
         (A4_tfrpcp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_lh IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_lh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_sat_hl IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_sat_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_ll IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_sat_hh IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_sat_hh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_zxtb IntRegs:$src1),
         (A2_zxtb IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_zxth IntRegs:$src1),
         (A2_zxth IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vnavgwr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vnavgwr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_or_xor IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_or_xor IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vmacbsu DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M5_vmacbsu DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_dpmpyuu_acc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_dpmpyuu_acc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffms_lib IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (F2_sffms_lib IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_cmpneqi IntRegs:$src1, s32_0ImmPred:$src2),
         (C4_cmpneqi IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_and_xor IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_and_xor IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_sat DoubleRegs:$src1),
         (A2_sat DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addsat IntRegs:$src1, IntRegs:$src2),
         (A2_addsat IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svavghs IntRegs:$src1, IntRegs:$src2),
         (A2_svavghs IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vrsadub_acc DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (A2_vrsadub_acc DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_bitsclri IntRegs:$src1, u6_0ImmPred:$src2),
         (C2_bitsclri IntRegs:$src1, u6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_sat_hh IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_sat_hh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_sat_hl IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_sat_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmaculs_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmaculs_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmaculs_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmaculs_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vradduh DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vradduh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_addp_c DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3),
         (A4_addp_c DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_xor PredRegs:$src1, PredRegs:$src2),
         (C2_xor PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyh_rs1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyh_rs1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyh_rs0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyh_rs0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2ud_chop DoubleRegs:$src1),
         (F2_conv_df2ud_chop DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_or_or PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_or_or PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vxaddsubhr DoubleRegs:$src1, DoubleRegs:$src2),
         (S4_vxaddsubhr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsathub DoubleRegs:$src1),
         (S2_vsathub DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2sf DoubleRegs:$src1),
         (F2_conv_df2sf DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_hmmpyh_rs1 IntRegs:$src1, IntRegs:$src2),
         (M2_hmmpyh_rs1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_hmmpyh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_hmmpyh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavgwr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavgwr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_sxth IntRegs:$src1),
         (A2_sxth IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_sxtb IntRegs:$src1),
         (A2_sxtb IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_or_orn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_or_orn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmaci_s0c DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vrcmaci_s0c DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_sxtw IntRegs:$src1),
         (A2_sxtw IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vabsdiffh DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vabsdiffh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_hmmpyl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_hmmpyl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_cl1p DoubleRegs:$src1),
         (S2_cl1p DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vabsdiffw DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vabsdiffw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_andnp DoubleRegs:$src1, DoubleRegs:$src2),
         (A4_andnp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_vmux PredRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (C2_vmux PredRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_parityp DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_parityp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_lsr_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asr_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfcmpeq IntRegs:$src1, IntRegs:$src2),
         (F2_sfcmpeq IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vaddb_map DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vaddub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmpheq DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmpheq DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_clbnorm IntRegs:$src1),
         (S2_clbnorm IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cnacsc_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cnacsc_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cnacsc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cnacsc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_subaddi IntRegs:$src1, s32_0ImmPred:$src2, IntRegs:$src3),
         (S4_subaddi IntRegs:$src1, s32_0ImmPred:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_tstbit_r IntRegs:$src1, IntRegs:$src2),
         (S2_tstbit_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vrcrotate DoubleRegs:$src1, IntRegs:$src2, u2_0ImmPred:$src3),
         (S4_vrcrotate DoubleRegs:$src1, IntRegs:$src2, u2_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmachs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmachs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmachs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmachs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_tstbit_i IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_tstbit_i IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_up_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_up_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_extractu_rp IntRegs:$src1, DoubleRegs:$src2),
         (S2_extractu_rp IntRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyuh_rs0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyuh_rs0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_vw DoubleRegs:$src1, u5_0ImmPred:$src2),
         (S2_lsr_i_vw DoubleRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_or_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_or_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asl_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_w2df IntRegs:$src1),
         (F2_conv_w2df IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_l16_sat_hl IntRegs:$src1, IntRegs:$src2),
         (A2_subh_l16_sat_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpeqi IntRegs:$src1, s32_0ImmPred:$src2),
         (C2_cmpeqi IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asl_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vcnegh DoubleRegs:$src1, IntRegs:$src2),
         (S2_vcnegh DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpweqi DoubleRegs:$src1, s8_0ImmPred:$src2),
         (A4_vcmpweqi DoubleRegs:$src1, s8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vdmpyrs_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vdmpyrs_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vdmpyrs_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vdmpyrs_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_xor_xacc DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M4_xor_xacc DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vdmpys_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vdmpys_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vdmpys_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vdmpys_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavgubr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavgubr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asl_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_cl0p DoubleRegs:$src1),
         (S2_cl0p DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_valignib DoubleRegs:$src1, DoubleRegs:$src2, u3_0ImmPred:$src3),
         (S2_valignib DoubleRegs:$src1, DoubleRegs:$src2, u3_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffixupd IntRegs:$src1, IntRegs:$src2),
         (F2_sffixupd IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmacsc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cmacsc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmacsc_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cmacsc_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_ct1 IntRegs:$src1),
         (S2_ct1 IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_ct0 IntRegs:$src1),
         (S2_ct0 IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_dpmpyuu_nac_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_dpmpyuu_nac_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyul_rs1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyul_rs1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_ntstbit_i IntRegs:$src1, u5_0ImmPred:$src2),
         (S4_ntstbit_i IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffixupr IntRegs:$src1),
         (F2_sffixupr IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asr_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmphgtu DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmphgtu DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_andn PredRegs:$src1, PredRegs:$src2),
         (C2_andn PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2s_s0pack IntRegs:$src1, IntRegs:$src2),
         (M2_vmpy2s_s0pack IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_addaddi IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3),
         (S4_addaddi IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_rcmpeqi IntRegs:$src1, s32_0ImmPred:$src2),
         (A4_rcmpeqi IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_xor_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_xor_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asl_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyuh_rs1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyuh_rs1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asr_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_round_ri IntRegs:$src1, u5_0ImmPred:$src2),
         (A4_round_ri IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_max IntRegs:$src1, IntRegs:$src2),
         (A2_max IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_round_rr IntRegs:$src1, IntRegs:$src2),
         (A4_round_rr IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_combineii s8_0ImmPred:$src1, u32_0ImmPred:$src2),
         (A4_combineii s8_0ImmPred:$src1, u32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_combineir s32_0ImmPred:$src1, IntRegs:$src2),
         (A4_combineir s32_0ImmPred:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_and_orn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_and_orn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vmacbuu DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M5_vmacbuu DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_rcmpeq IntRegs:$src1, IntRegs:$src2),
         (A4_rcmpeq IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_cmpyr_whc DoubleRegs:$src1, IntRegs:$src2),
         (M4_cmpyr_whc DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_lsr_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vzxtbh IntRegs:$src1),
         (S2_vzxtbh IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacuhs_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacuhs_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_r_sat IntRegs:$src1, IntRegs:$src2),
         (S2_asr_r_r_sat IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_combinew IntRegs:$src1, IntRegs:$src2),
         (A2_combinew IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpyi_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpyi_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asl_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_ori_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_ori_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_nbitsset IntRegs:$src1, IntRegs:$src2),
         (C4_nbitsset IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_l16_ll IntRegs:$src1, IntRegs:$src2),
         (A2_addh_l16_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_r_and IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_modwrapu IntRegs:$src1, IntRegs:$src2),
         (A4_modwrapu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_rcmpneq IntRegs:$src1, IntRegs:$src2),
         (A4_rcmpneq IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfimm_p u10_0ImmPred:$src1),
         (F2_sfimm_p u10_0ImmPred:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfimm_n u10_0ImmPred:$src1),
         (F2_sfimm_n u10_0ImmPred:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_cmpyr_wh DoubleRegs:$src1, IntRegs:$src2),
         (M4_cmpyr_wh DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavgub DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavgub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_d2sf DoubleRegs:$src1),
         (F2_conv_d2sf DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavguh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavguh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpbeqi IntRegs:$src1, u8_0ImmPred:$src2),
         (A4_cmpbeqi IntRegs:$src1, u8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfcmpuo IntRegs:$src1, IntRegs:$src2),
         (F2_sfcmpuo IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavguw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavguw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asr_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsatwh_nopack DoubleRegs:$src1),
         (S2_vsatwh_nopack DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_minu IntRegs:$src1, IntRegs:$src2),
         (A2_minu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_or_andn IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_or_andn IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_minp DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_minp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_or_andix IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3),
         (S4_or_andix IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_rnd_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_rnd_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyuh_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyuh_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyuh_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyuh_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfcmpge IntRegs:$src1, IntRegs:$src2),
         (F2_sfcmpge IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfmin IntRegs:$src1, IntRegs:$src2),
         (F2_sfmin IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfcmpgt IntRegs:$src1, IntRegs:$src2),
         (F2_sfcmpgt IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vpmpyh IntRegs:$src1, IntRegs:$src2),
         (M4_vpmpyh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacuhs_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacuhs_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_roundsat DoubleRegs:$src1),
         (A2_roundsat DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_ct1p DoubleRegs:$src1),
         (S2_ct1p DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_extract_rp IntRegs:$src1, DoubleRegs:$src2),
         (S4_extract_rp IntRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_cmplteui IntRegs:$src1, u32_0ImmPred:$src2),
         (C4_cmplteui IntRegs:$src1, u32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_addi_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_addi_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_tfrcpp CtrRegs64:$src1),
         (A4_tfrcpp CtrRegs64:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_svw_trun DoubleRegs:$src1, u5_0ImmPred:$src2),
         (S2_asr_i_svw_trun DoubleRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmphgti IntRegs:$src1, s32_0ImmPred:$src2),
         (A4_cmphgti IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrminh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrminh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrminw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrminw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmphgtu IntRegs:$src1, IntRegs:$src2),
         (A4_cmphgtu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_insertp_rp DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (S2_insertp_rp DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vnavghcr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vnavghcr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_subi_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_subi_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_vh DoubleRegs:$src1, IntRegs:$src2),
         (S2_lsl_r_vh DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsubws DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsubws DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_sath IntRegs:$src1),
         (A2_sath IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asl_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_satb IntRegs:$src1),
         (A2_satb IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_insertp DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3, u6_0ImmPred:$src4),
         (S2_insertp DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3, u6_0ImmPred:$src4)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_lsr_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_extractup_rp DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_extractup_rp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vxaddsubw DoubleRegs:$src1, DoubleRegs:$src2),
         (S4_vxaddsubw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vxaddsubh DoubleRegs:$src1, DoubleRegs:$src2),
         (S4_vxaddsubh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_asrh IntRegs:$src1),
         (A2_asrh IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_extractp_rp DoubleRegs:$src1, DoubleRegs:$src2),
         (S4_extractp_rp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_or PredRegs:$src1, PredRegs:$src2),
         (C2_or PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyul_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyul_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmacr_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vrcmacr_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_xor IntRegs:$src1, IntRegs:$src2),
         (A2_xor IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_add IntRegs:$src1, IntRegs:$src2),
         (A2_add IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsububs DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsububs DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2s_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_vmpy2s_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2s_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_vmpy2s_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vraddub_acc DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (A2_vraddub_acc DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfinvsqrta IntRegs:$src1),
         (F2_sfinvsqrta IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_ct0p DoubleRegs:$src1),
         (S2_ct0p DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svaddh IntRegs:$src1, IntRegs:$src2),
         (A2_svaddh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vcrotate DoubleRegs:$src1, IntRegs:$src2),
         (S2_vcrotate DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_aslh IntRegs:$src1),
         (A2_aslh IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_lh IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_lh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_ll IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_hmmpyl_rs1 IntRegs:$src1, IntRegs:$src2),
         (M2_hmmpyl_rs1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_p DoubleRegs:$src1, IntRegs:$src2),
         (S2_asr_r_p DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsplatrh IntRegs:$src1),
         (S2_vsplatrh IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_r IntRegs:$src1, IntRegs:$src2),
         (S2_asr_r_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_hl IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsplatrb IntRegs:$src1),
         (S2_vsplatrb IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_hh IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_hh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpyr_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpyr_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_dpmpyss_rnd_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_dpmpyss_rnd_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_muxri PredRegs:$src1, s32_0ImmPred:$src2, IntRegs:$src3),
         (C2_muxri PredRegs:$src1, s32_0ImmPred:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2es_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vmac2es_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2es_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vmac2es_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_pxfer_map PredRegs:$src1),
         (C2_pxfer_map PredRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyu_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asl_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_acc_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_acc_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asr_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vaddw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vaddw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asr_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vaddh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vaddh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpeqp DoubleRegs:$src1, DoubleRegs:$src2),
         (C2_cmpeqp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_mpyri_addi u32_0ImmPred:$src1, IntRegs:$src2, u6_0ImmPred:$src3),
         (M4_mpyri_addi u32_0ImmPred:$src1, IntRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_andi_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_andi_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_macsip IntRegs:$src1, IntRegs:$src2, u32_0ImmPred:$src3),
         (M2_macsip IntRegs:$src1, IntRegs:$src2, u32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_tfrcrr CtrRegs:$src1),
         (A2_tfrcrr CtrRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_macsin IntRegs:$src1, IntRegs:$src2, u32_0ImmPred:$src3),
         (M2_macsin IntRegs:$src1, IntRegs:$src2, u32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_orn PredRegs:$src1, PredRegs:$src2),
         (C2_orn PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_and_andn IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_and_andn IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfmpy IntRegs:$src1, IntRegs:$src2),
         (F2_sfmpy IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_vw DoubleRegs:$src1, IntRegs:$src2),
         (S2_asr_r_vw DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_and_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_and_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_vh DoubleRegs:$src1, IntRegs:$src2),
         (S2_asr_r_vh DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_mask PredRegs:$src1),
         (C2_mask PredRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_up_s1_sat IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_up_s1_sat IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpbgt DoubleRegs:$src1, DoubleRegs:$src2),
         (A4_vcmpbgt DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vrmacbsu DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M5_vrmacbsu DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vrsadub DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vrsadub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_tfrrcr IntRegs:$src1),
         (A2_tfrrcr IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmpys_acc_s1 DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (M2_vrcmpys_acc_s1 DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_dfcmpge DoubleRegs:$src1, DoubleRegs:$src2),
         (F2_dfcmpge DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_accii IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3),
         (M2_accii IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A5_vaddhubs DoubleRegs:$src1, DoubleRegs:$src2),
         (A5_vaddhubs DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vmaxw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vmaxw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vmaxb DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vmaxb DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vmaxh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vmaxh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsxthw IntRegs:$src1),
         (S2_vsxthw IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_andi_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_andi_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asl_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpgt IntRegs:$src1, IntRegs:$src2),
         (C2_cmpgt IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2d_chop DoubleRegs:$src1),
         (F2_conv_df2d_chop DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2w IntRegs:$src1),
         (F2_conv_sf2w IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_p_or DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfclass IntRegs:$src1, u5_0ImmPred:$src2),
         (F2_sfclass IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_xor_andn IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_xor_andn IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_addasl_rrri IntRegs:$src1, IntRegs:$src2, u3_0ImmPred:$src3),
         (S2_addasl_rrri IntRegs:$src1, IntRegs:$src2, u3_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vdmpybsu DoubleRegs:$src1, DoubleRegs:$src2),
         (M5_vdmpybsu DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_nac_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_nac_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addi IntRegs:$src1, s32_0ImmPred:$src2),
         (A2_addi IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addp DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_addp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2s_s1pack IntRegs:$src1, IntRegs:$src2),
         (M2_vmpy2s_s1pack IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_clbpnorm DoubleRegs:$src1),
         (S4_clbpnorm DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_round_rr_sat IntRegs:$src1, IntRegs:$src2),
         (A4_round_rr_sat IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_nacci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_nacci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_shuffeh DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_shuffeh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_lsr_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2uw IntRegs:$src1),
         (F2_conv_sf2uw IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsubh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsubh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2ud IntRegs:$src1),
         (F2_conv_sf2ud IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsubw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsubw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmpwgt DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmpwgt DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_xor_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_xor_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2uw_chop IntRegs:$src1),
         (F2_conv_sf2uw_chop IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_vw DoubleRegs:$src1, IntRegs:$src2),
         (S2_asl_r_vw DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsatwuh_nopack DoubleRegs:$src1),
         (S2_vsatwuh_nopack DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_vh DoubleRegs:$src1, IntRegs:$src2),
         (S2_asl_r_vh DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svsubuhs IntRegs:$src1, IntRegs:$src2),
         (A2_svsubuhs IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vmpybsu IntRegs:$src1, IntRegs:$src2),
         (M5_vmpybsu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_l16_sat_ll IntRegs:$src1, IntRegs:$src2),
         (A2_subh_l16_sat_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_and_and PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_and_and PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_p DoubleRegs:$src1, IntRegs:$src2),
         (S2_lsr_r_p DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_r IntRegs:$src1, IntRegs:$src2),
         (S2_lsr_r_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_subp_c DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3),
         (A4_subp_c DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsubhs DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsubhs DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_vitpack PredRegs:$src1, PredRegs:$src2),
         (C2_vitpack PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavguhr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavguhr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsplicerb DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3),
         (S2_vsplicerb DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_nbitsclr IntRegs:$src1, IntRegs:$src2),
         (C4_nbitsclr IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmpbgtu DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmpbgtu DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpys_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpys_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpys_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpys_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_dfcmpuo DoubleRegs:$src1, DoubleRegs:$src2),
         (F2_dfcmpuo DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_shuffob DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_shuffob DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_and PredRegs:$src1, PredRegs:$src2),
         (C2_and PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S5_popcountp DoubleRegs:$src1),
         (S5_popcountp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_extractp DoubleRegs:$src1, u6_0ImmPred:$src2, u6_0ImmPred:$src3),
         (S4_extractp DoubleRegs:$src1, u6_0ImmPred:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_cl0 IntRegs:$src1),
         (S2_cl0 IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpbgti DoubleRegs:$src1, s8_0ImmPred:$src2),
         (A4_vcmpbgti DoubleRegs:$src1, s8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacls_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacls_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacls_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacls_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_cmpneq IntRegs:$src1, IntRegs:$src2),
         (C4_cmpneq IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmac2es DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vmac2es DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vdmacs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vdmacs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vdmacs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vdmacs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_clb IntRegs:$src1),
         (S2_clb IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_hl_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_hl_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_maci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_maci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vmaxuh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vmaxuh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_bitspliti IntRegs:$src1, u5_0ImmPred:$src2),
         (A4_bitspliti IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vmaxub DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vmaxub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrmac_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vrmac_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_r_sat IntRegs:$src1, IntRegs:$src2),
         (S2_asl_r_r_sat IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2d IntRegs:$src1),
         (F2_conv_sf2d IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asr_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_dfimm_n u10_0ImmPred:$src1),
         (F2_dfimm_n u10_0ImmPred:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmphgt IntRegs:$src1, IntRegs:$src2),
         (A4_cmphgt IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_dfimm_p u10_0ImmPred:$src1),
         (F2_dfimm_p u10_0ImmPred:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_acc_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_acc_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vcmpy_s1_sat_r DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vcmpy_s1_sat_r DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_mpyri_addr_u2 IntRegs:$src1, u6_2ImmPred:$src2, IntRegs:$src3),
         (M4_mpyri_addr_u2 IntRegs:$src1, u6_2ImmPred:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vcmpy_s1_sat_i DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vcmpy_s1_sat_i DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_p_nac DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vrmacbuu DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M5_vrmacbuu DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vspliceib DoubleRegs:$src1, DoubleRegs:$src2, u3_0ImmPred:$src3),
         (S2_vspliceib DoubleRegs:$src1, DoubleRegs:$src2, u3_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_dpmpyss_acc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_dpmpyss_acc_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cnacs_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cnacs_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cnacs_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cnacs_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_maxu IntRegs:$src1, IntRegs:$src2),
         (A2_maxu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_maxp DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_maxp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_andir IntRegs:$src1, s32_0ImmPred:$src2),
         (A2_andir IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfrecipa IntRegs:$src1, IntRegs:$src2),
         (F2_sfrecipa IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_combineii s32_0ImmPred:$src1, s8_0ImmPred:$src2),
         (A2_combineii s32_0ImmPred:$src1, s8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_orn IntRegs:$src1, IntRegs:$src2),
         (A4_orn IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpbgtui IntRegs:$src1, u32_0ImmPred:$src2),
         (A4_cmpbgtui IntRegs:$src1, u32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpbeqi DoubleRegs:$src1, u8_0ImmPred:$src2),
         (A4_vcmpbeqi DoubleRegs:$src1, u8_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_r IntRegs:$src1, IntRegs:$src2),
         (S2_lsl_r_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_p DoubleRegs:$src1, IntRegs:$src2),
         (S2_lsl_r_p DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_or IntRegs:$src1, IntRegs:$src2),
         (A2_or IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_dfcmpeq DoubleRegs:$src1, DoubleRegs:$src2),
         (F2_dfcmpeq DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpeq IntRegs:$src1, IntRegs:$src2),
         (C2_cmpeq IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_tfrp DoubleRegs:$src1),
         (A2_tfrp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_and_andn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_and_andn PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsathub_nopack DoubleRegs:$src1),
         (S2_vsathub_nopack DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_satuh IntRegs:$src1),
         (A2_satuh IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_satub IntRegs:$src1),
         (A2_satub IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmpys_s1 DoubleRegs:$src1, IntRegs:$src2),
         (M2_vrcmpys_s1 DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_or_ori IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3),
         (S4_or_ori IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_fastcorner9_not PredRegs:$src1, PredRegs:$src2),
         (C4_fastcorner9_not PredRegs:$src1, PredRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_tfrih IntRegs:$src1, u16_0ImmPred:$src2),
         (A2_tfrih IntRegs:$src1, u16_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_tfril IntRegs:$src1, u16_0ImmPred:$src2),
         (A2_tfril IntRegs:$src1, u16_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_mpyri_addr IntRegs:$src1, IntRegs:$src2, u32_0ImmPred:$src3),
         (M4_mpyri_addr IntRegs:$src1, IntRegs:$src2, u32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vtrunehb DoubleRegs:$src1),
         (S2_vtrunehb DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vabsw DoubleRegs:$src1),
         (A2_vabsw DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vabsh DoubleRegs:$src1),
         (A2_vabsh DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sfsub IntRegs:$src1, IntRegs:$src2),
         (F2_sfsub IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_muxii PredRegs:$src1, s32_0ImmPred:$src2, s8_0ImmPred:$src3),
         (C2_muxii PredRegs:$src1, s32_0ImmPred:$src2, s8_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_muxir PredRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3),
         (C2_muxir PredRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_swiz IntRegs:$src1),
         (A2_swiz IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asr_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpyrsc_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpyrsc_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpyrsc_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpyrsc_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vraddub DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vraddub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_tlbmatch DoubleRegs:$src1, IntRegs:$src2),
         (A4_tlbmatch DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2w_chop DoubleRegs:$src1),
         (F2_conv_df2w_chop DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_and IntRegs:$src1, IntRegs:$src2),
         (A2_and IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_extract IntRegs:$src1, u5_0ImmPred:$src2, u5_0ImmPred:$src3),
         (S4_extract IntRegs:$src1, u5_0ImmPred:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vcmpweq DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vcmpweq DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_acci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_acci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_lsr_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_lsr_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_ud2sf DoubleRegs:$src1),
         (F2_conv_ud2sf DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_tfr IntRegs:$src1),
         (A2_tfr IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asr_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subri s32_0ImmPred:$src1, IntRegs:$src2),
         (A2_subri s32_0ImmPred:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrmaxuw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrmaxuw DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vmpybuu IntRegs:$src1, IntRegs:$src2),
         (M5_vmpybuu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vrmaxuh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (A4_vrmaxuh DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_vw DoubleRegs:$src1, u5_0ImmPred:$src2),
         (S2_asl_i_vw DoubleRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavgw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavgw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_brev IntRegs:$src1),
         (S2_brev IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavgh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavgh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_clrbit_i IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_clrbit_i IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_vh DoubleRegs:$src1, u4_0ImmPred:$src2),
         (S2_asl_i_vh DoubleRegs:$src1, u4_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_lsr_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_lsl_r_r_nac IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyl_rs1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyl_rs1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyl_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyl_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyl_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyl_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_naccii IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3),
         (M2_naccii IntRegs:$src1, IntRegs:$src2, s32_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vrndpackwhs DoubleRegs:$src1),
         (S2_vrndpackwhs DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vtrunewh DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_vtrunewh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_dpmpyss_nac_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_dpmpyss_nac_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_mac_up_s1_sat IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_mac_up_s1_sat IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_vrcrotate_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3, u2_0ImmPred:$src4),
         (S4_vrcrotate_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3, u2_0ImmPred:$src4)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_uw2df IntRegs:$src1),
         (F2_conv_uw2df IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vaddubs DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vaddubs DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asr_r_r_acc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_orir IntRegs:$src1, s32_0ImmPred:$src2),
         (A2_orir IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_andp DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_andp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lfsp DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_lfsp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_min IntRegs:$src1, IntRegs:$src2),
         (A2_min IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpysmi IntRegs:$src1, m32_0ImmPred:$src2),
         (M2_mpysmi IntRegs:$src1, m32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vcmpy_s0_sat_r DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vcmpy_s0_sat_r DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyu_acc_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyu_acc_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_svw_trun DoubleRegs:$src1, IntRegs:$src2),
         (S2_asr_r_svw_trun DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyh_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyh_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyh_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyh_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2df IntRegs:$src1),
         (F2_conv_sf2df IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vtrunohb DoubleRegs:$src1),
         (S2_vtrunohb DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2d_chop IntRegs:$src1),
         (F2_conv_sf2d_chop IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2w DoubleRegs:$src1),
         (F2_conv_df2w DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S5_asrhub_sat DoubleRegs:$src1, u4_0ImmPred:$src2),
         (S5_asrhub_sat DoubleRegs:$src1, u4_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_r_xacc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asl_i_r_xacc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2d DoubleRegs:$src1),
         (F2_conv_df2d DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmaculs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmaculs_s1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmaculs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmaculs_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svadduhs IntRegs:$src1, IntRegs:$src2),
         (A2_svadduhs IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2w_chop IntRegs:$src1),
         (F2_conv_sf2w_chop IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_svsathub IntRegs:$src1),
         (S2_svsathub IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_hl_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_hl_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_setbit_r IntRegs:$src1, IntRegs:$src2),
         (S2_setbit_r IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavghr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavghr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffma_sc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3, PredRegs:$src4),
         (F2_sffma_sc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3, PredRegs:$src4)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_dfclass DoubleRegs:$src1, u5_0ImmPred:$src2),
         (F2_dfclass DoubleRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2ud DoubleRegs:$src1),
         (F2_conv_df2ud DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_df2uw DoubleRegs:$src1),
         (F2_conv_df2uw DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpyrs_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpyrs_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmpyrs_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_cmpyrs_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_cmpltei IntRegs:$src1, s32_0ImmPred:$src2),
         (C4_cmpltei IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_cmplteu IntRegs:$src1, IntRegs:$src2),
         (C4_cmplteu IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vsubb_map DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vsubub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_l16_ll IntRegs:$src1, IntRegs:$src2),
         (A2_subh_l16_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_r_rnd IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_asr_i_r_rnd IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrmpy_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vrmpy_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_rnd_hh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyd_rnd_hh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_minup DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_minup DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_valignrb DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3),
         (S2_valignrb DoubleRegs:$src1, DoubleRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asr_r_p_acc DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmpyl_rs0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_mmpyl_rs0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmaci_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vrcmaci_s0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vaddub DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vaddub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_combine_lh IntRegs:$src1, IntRegs:$src2),
         (A2_combine_lh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vdmacbsu DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M5_vdmacbsu DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_combine_ll IntRegs:$src1, IntRegs:$src2),
         (A2_combine_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_hl_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_hl_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmpyi_s0c DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vrcmpyi_s0c DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_p_rnd DoubleRegs:$src1, u6_0ImmPred:$src2),
         (S2_asr_i_p_rnd DoubleRegs:$src1, u6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addpsat DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_addpsat DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svaddhs IntRegs:$src1, IntRegs:$src2),
         (A2_svaddhs IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_ori_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_ori_lsr_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_ll_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_ll_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_sat_rnd_ll_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_sat_rnd_ll_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vminw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vminw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vminh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vminh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vrcmpyr_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vrcmpyr_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vminb DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vminb DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vcmac_s0_sat_i DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vcmac_s0_sat_i DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_lh_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_lh_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_lh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpyud_lh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (S2_asl_r_r_or IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_lsli s6_0ImmPred:$src1, IntRegs:$src2),
         (S4_lsli s6_0ImmPred:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsl_r_vw DoubleRegs:$src1, IntRegs:$src2),
         (S2_lsl_r_vw DoubleRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_hh_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_mpy_hh_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyeh_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M4_vrmpyeh_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_vrmpyeh_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M4_vrmpyeh_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_lh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_lh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vraddh DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vraddh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_tfrrp IntRegs:$src1),
         (C2_tfrrp IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_ll_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_ll_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vtrunowh DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_vtrunowh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_abs IntRegs:$src1),
         (A2_abs IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpbeq IntRegs:$src1, IntRegs:$src2),
         (A4_cmpbeq IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_negp DoubleRegs:$src1),
         (A2_negp DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_r_sat IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_asl_i_r_sat IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_l16_sat_hl IntRegs:$src1, IntRegs:$src2),
         (A2_addh_l16_sat_hl IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsatwuh DoubleRegs:$src1),
         (S2_vsatwuh DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_dfcmpgt DoubleRegs:$src1, DoubleRegs:$src2),
         (F2_dfcmpgt DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_svsathb IntRegs:$src1),
         (S2_svsathb IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_cmpgtup DoubleRegs:$src1, DoubleRegs:$src2),
         (C2_cmpgtup DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cround_ri IntRegs:$src1, u5_0ImmPred:$src2),
         (A4_cround_ri IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_clbpaddi DoubleRegs:$src1, s6_0ImmPred:$src2),
         (S4_clbpaddi DoubleRegs:$src1, s6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cround_rr IntRegs:$src1, IntRegs:$src2),
         (A4_cround_rr IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_mux PredRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (C2_mux PredRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_dpmpyuu_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_dpmpyuu_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_shuffeb DoubleRegs:$src1, DoubleRegs:$src2),
         (S2_shuffeb DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vminuw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vminuw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vaddhs DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vaddhs DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_insert_rp IntRegs:$src1, IntRegs:$src2, DoubleRegs:$src3),
         (S2_insert_rp IntRegs:$src1, IntRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vminuh DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vminuh DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vminub DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vminub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_extractu IntRegs:$src1, u5_0ImmPred:$src2, u5_0ImmPred:$src3),
         (S2_extractu IntRegs:$src1, u5_0ImmPred:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_svsubh IntRegs:$src1, IntRegs:$src2),
         (A2_svsubh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_clbaddi IntRegs:$src1, s6_0ImmPred:$src2),
         (S4_clbaddi IntRegs:$src1, s6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_sffms IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (F2_sffms IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vsxtbh IntRegs:$src1),
         (S2_vsxtbh IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_ll_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_ll_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subp DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_subp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2es_s1 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vmpy2es_s1 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2es_s0 DoubleRegs:$src1, DoubleRegs:$src2),
         (M2_vmpy2es_s0 DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_parity IntRegs:$src1, IntRegs:$src2),
         (S4_parity IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S4_addi_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S4_addi_asl_ri u32_0ImmPred:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_hh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyd_nac_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyd_nac_hh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S2_asr_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_cmpheqi IntRegs:$src1, s32_0ImmPred:$src2),
         (A4_cmpheqi IntRegs:$src1, s32_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_lsr_r_p_xor DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_F2_conv_sf2ud_chop IntRegs:$src1),
         (F2_conv_sf2ud_chop IntRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_acc_sat_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_acc_sat_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3),
         (S2_asl_r_p_and DoubleRegs:$src1, DoubleRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_sat_lh IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_sat_lh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_addh_h16_sat_ll IntRegs:$src1, IntRegs:$src2),
         (A2_addh_h16_sat_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M4_nac_up_s1_sat IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M4_nac_up_s1_sat IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_lh_s1 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpyud_nac_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpyud_nac_lh_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_round_ri_sat IntRegs:$src1, u5_0ImmPred:$src2),
         (A4_round_ri_sat IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_hl_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_hl_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vavghcr DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vavghcr DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacls_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacls_rs0 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mmacls_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_mmacls_rs1 DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_cmaci_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_cmaci_s0 DoubleRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_setbit_i IntRegs:$src1, u5_0ImmPred:$src2),
         (S2_setbit_i IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asl_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asl_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_andn IntRegs:$src1, IntRegs:$src2),
         (A4_andn IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M5_vrmpybsu DoubleRegs:$src1, DoubleRegs:$src2),
         (M5_vrmpybsu DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_vrndpackwh DoubleRegs:$src1),
         (S2_vrndpackwh DoubleRegs:$src1)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vcmac_s0_sat_r DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (M2_vcmac_s0_sat_r DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_vmaxuw DoubleRegs:$src1, DoubleRegs:$src2),
         (A2_vmaxuw DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C2_bitsclr IntRegs:$src1, IntRegs:$src2),
         (C2_bitsclr IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_xor_xacc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_xor_xacc IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_vcmpbgtui DoubleRegs:$src1, u7_0ImmPred:$src2),
         (A4_vcmpbgtui DoubleRegs:$src1, u7_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A4_ornp DoubleRegs:$src1, DoubleRegs:$src2),
         (A4_ornp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_and_or PredRegs:$src1, PredRegs:$src2, PredRegs:$src3),
         (C4_and_or PredRegs:$src1, PredRegs:$src2, PredRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_hh_s1 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_mpy_nac_sat_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mpy_nac_sat_hh_s0 IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_sat_ll IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_sat_ll IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_A2_subh_h16_sat_lh IntRegs:$src1, IntRegs:$src2),
         (A2_subh_h16_sat_lh IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2su_s1 IntRegs:$src1, IntRegs:$src2),
         (M2_vmpy2su_s1 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_M2_vmpy2su_s0 IntRegs:$src1, IntRegs:$src2),
         (M2_vmpy2su_s0 IntRegs:$src1, IntRegs:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_asr_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_asr_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_C4_nbitsclri IntRegs:$src1, u6_0ImmPred:$src2),
         (C4_nbitsclri IntRegs:$src1, u6_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_vh DoubleRegs:$src1, u4_0ImmPred:$src2),
         (S2_lsr_i_vh DoubleRegs:$src1, u4_0ImmPred:$src2)>, Requires<[HasV5]>;
def: Pat<(int_hexagon_S2_lsr_i_p_xacc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S2_lsr_i_p_xacc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV5]>;

// V55 Scalar Instructions.

def: Pat<(int_hexagon_A5_ACS DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3),
         (A5_ACS DoubleRegs:$src1, DoubleRegs:$src2, DoubleRegs:$src3)>, Requires<[HasV55]>;

// V60 Scalar Instructions.

def: Pat<(int_hexagon_S6_rol_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S6_rol_i_p_and DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_r_xacc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S6_rol_i_r_xacc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S6_rol_i_r_and IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S6_rol_i_r_acc IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_p_xacc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S6_rol_i_p_xacc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_p DoubleRegs:$src1, u6_0ImmPred:$src2),
         (S6_rol_i_p DoubleRegs:$src1, u6_0ImmPred:$src2)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S6_rol_i_p_nac DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S6_rol_i_p_acc DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S6_rol_i_r_or IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_r IntRegs:$src1, u5_0ImmPred:$src2),
         (S6_rol_i_r IntRegs:$src1, u5_0ImmPred:$src2)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3),
         (S6_rol_i_r_nac IntRegs:$src1, IntRegs:$src2, u5_0ImmPred:$src3)>, Requires<[HasV60]>;
def: Pat<(int_hexagon_S6_rol_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3),
         (S6_rol_i_p_or DoubleRegs:$src1, DoubleRegs:$src2, u6_0ImmPred:$src3)>, Requires<[HasV60]>;

// V62 Scalar Instructions.

def: Pat<(int_hexagon_S6_vtrunehb_ppp DoubleRegs:$src1, DoubleRegs:$src2),
         (S6_vtrunehb_ppp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV62]>;
def: Pat<(int_hexagon_V6_ldntnt0 IntRegs:$src1),
         (V6_ldntnt0 IntRegs:$src1)>, Requires<[HasV62]>;
def: Pat<(int_hexagon_M6_vabsdiffub DoubleRegs:$src1, DoubleRegs:$src2),
         (M6_vabsdiffub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV62]>;
def: Pat<(int_hexagon_S6_vtrunohb_ppp DoubleRegs:$src1, DoubleRegs:$src2),
         (S6_vtrunohb_ppp DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV62]>;
def: Pat<(int_hexagon_M6_vabsdiffb DoubleRegs:$src1, DoubleRegs:$src2),
         (M6_vabsdiffb DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV62]>;
def: Pat<(int_hexagon_A6_vminub_RdP DoubleRegs:$src1, DoubleRegs:$src2),
         (A6_vminub_RdP DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV62]>;
def: Pat<(int_hexagon_S6_vsplatrbp IntRegs:$src1),
         (S6_vsplatrbp IntRegs:$src1)>, Requires<[HasV62]>;

// V65 Scalar Instructions.

def: Pat<(int_hexagon_A6_vcmpbeq_notany DoubleRegs:$src1, DoubleRegs:$src2),
         (A6_vcmpbeq_notany DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV65]>;

// V66 Scalar Instructions.

def: Pat<(int_hexagon_F2_dfsub DoubleRegs:$src1, DoubleRegs:$src2),
         (F2_dfsub DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV66]>;
def: Pat<(int_hexagon_F2_dfadd DoubleRegs:$src1, DoubleRegs:$src2),
         (F2_dfadd DoubleRegs:$src1, DoubleRegs:$src2)>, Requires<[HasV66]>;
def: Pat<(int_hexagon_M2_mnaci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3),
         (M2_mnaci IntRegs:$src1, IntRegs:$src2, IntRegs:$src3)>, Requires<[HasV66]>;
def: Pat<(int_hexagon_S2_mask u5_0ImmPred:$src1, u5_0ImmPred:$src2),
         (S2_mask u5_0ImmPred:$src1, u5_0ImmPred:$src2)>, Requires<[HasV66]>;

// V60 HVX Instructions.

def: Pat<(int_hexagon_V6_veqb_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqb_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqb_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqb_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vminub HvxVR:$src1, HvxVR:$src2),
         (V6_vminub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vminub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vminub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaslw_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vaslw_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaslw_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vaslw_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhvsrs HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyhvsrs HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhvsrs_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyhvsrs HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsathub HvxVR:$src1, HvxVR:$src2),
         (V6_vsathub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsathub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsathub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddh_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vaddh_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddh_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vaddh_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybusi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3),
         (V6_vrmpybusi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybusi_128B HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3),
         (V6_vrmpybusi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshufoh HvxVR:$src1, HvxVR:$src2),
         (V6_vshufoh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshufoh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vshufoh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrwv HvxVR:$src1, HvxVR:$src2),
         (V6_vasrwv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrwv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vasrwv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhsuisat HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpyhsuisat HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhsuisat_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpyhsuisat HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrsadubi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4),
         (V6_vrsadubi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrsadubi_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4),
         (V6_vrsadubi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vnavgw HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vnavgw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vnavgh HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vnavgh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavgub HvxVR:$src1, HvxVR:$src2),
         (V6_vavgub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavgub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavgub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubb HvxVR:$src1, HvxVR:$src2),
         (V6_vsubb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtw_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavgubrnd HvxVR:$src1, HvxVR:$src2),
         (V6_vavgubrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavgubrnd_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavgubrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybusv HvxVR:$src1, HvxVR:$src2),
         (V6_vrmpybusv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybusv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vrmpybusv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubbnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubbnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubbnq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubbnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vroundhb HvxVR:$src1, HvxVR:$src2),
         (V6_vroundhb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vroundhb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vroundhb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vadduhsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vadduhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vadduhsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vadduhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsububsat HvxVR:$src1, HvxVR:$src2),
         (V6_vsububsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsububsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsububsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpabus_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpabus_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpabus_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpabus_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmux HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmux HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmux_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmux HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhus HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyhus HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhus_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyhus HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackeb HvxVR:$src1, HvxVR:$src2),
         (V6_vpackeb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackeb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackeb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubhnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubhnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubhnq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubhnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavghrnd HvxVR:$src1, HvxVR:$src2),
         (V6_vavghrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavghrnd_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavghrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vtran2x2_map HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vtran2x2_map HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vtran2x2_map_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vtran2x2_map HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdelta HvxVR:$src1, HvxVR:$src2),
         (V6_vdelta HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdelta_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vdelta HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuh_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuh_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuh_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuh_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vtmpyhb HvxWR:$src1, IntRegs:$src2),
         (V6_vtmpyhb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vtmpyhb_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vtmpyhb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackob HvxVR:$src1, HvxVR:$src2),
         (V6_vpackob HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackob_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackob HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmaxh HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmaxh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vtmpybus_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vtmpybus_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vtmpybus_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vtmpybus_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubuhsat HvxVR:$src1, HvxVR:$src2),
         (V6_vsubuhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubuhsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubuhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrw_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vasrw_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrw_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vasrw_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_or HvxQR:$src1, HvxQR:$src2),
         (V6_pred_or HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_or_128B HvxQR:$src1, HvxQR:$src2),
         (V6_pred_or HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyub_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vrmpyub_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyub_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vrmpyub_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_lo HvxWR:$src1),
         (V6_lo HvxWR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_lo_128B HvxWR:$src1),
         (V6_lo HvxWR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubb_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubb_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubb_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubb_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubhsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubhsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiwh HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyiwh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiwh_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyiwh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiwb HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyiwb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiwb_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyiwb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldu0 IntRegs:$src1),
         (V6_ldu0 IntRegs:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldu0_128B IntRegs:$src1),
         (V6_ldu0 IntRegs:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuh_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuh_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuh_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuh_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgth_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgth_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgth_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgth_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavgh HvxVR:$src1, HvxVR:$src2),
         (V6_vavgh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavgh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavgh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlalignb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlalignb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlalignb_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlalignb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsh HvxVR:$src1),
         (V6_vsh HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsh_128B HvxVR:$src1),
         (V6_vsh HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_and_n HvxQR:$src1, HvxQR:$src2),
         (V6_pred_and_n HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_and_n_128B HvxQR:$src1, HvxQR:$src2),
         (V6_pred_and_n HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsb HvxVR:$src1),
         (V6_vsb HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsb_128B HvxVR:$src1),
         (V6_vsb HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vroundwuh HvxVR:$src1, HvxVR:$src2),
         (V6_vroundwuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vroundwuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vroundwuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrhv HvxVR:$src1, HvxVR:$src2),
         (V6_vasrhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrhv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vasrhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshuffh HvxVR:$src1),
         (V6_vshuffh HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshuffh_128B HvxVR:$src1),
         (V6_vshuffh HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddhsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vaddhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddhsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vaddhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vnavgub HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vnavgub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybv HvxVR:$src1, HvxVR:$src2),
         (V6_vrmpybv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vrmpybv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vnormamth HvxVR:$src1),
         (V6_vnormamth HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vnormamth_128B HvxVR:$src1),
         (V6_vnormamth HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhb HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpyhb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhb_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpyhb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavguh HvxVR:$src1, HvxVR:$src2),
         (V6_vavguh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavguh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavguh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlsrwv HvxVR:$src1, HvxVR:$src2),
         (V6_vlsrwv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlsrwv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vlsrwv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlsrhv HvxVR:$src1, HvxVR:$src2),
         (V6_vlsrhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlsrhv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vlsrhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhisat HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpyhisat HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhisat_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpyhisat HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhvsat HvxVR:$src1, HvxVR:$src2),
         (V6_vdmpyhvsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhvsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vdmpyhvsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddw HvxVR:$src1, HvxVR:$src2),
         (V6_vaddw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vzh HvxVR:$src1),
         (V6_vzh HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vzh_128B HvxVR:$src1),
         (V6_vzh HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddh HvxVR:$src1, HvxVR:$src2),
         (V6_vaddh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmaxub HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmaxub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyhv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhv_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyhv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vadduhsat HvxVR:$src1, HvxVR:$src2),
         (V6_vadduhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vadduhsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vadduhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshufoeh HvxVR:$src1, HvxVR:$src2),
         (V6_vshufoeh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshufoeh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vshufoeh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyuhv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyuhv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyuhv_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyuhv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqh HvxVR:$src1, HvxVR:$src2),
         (V6_veqh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_veqh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpabuuv HvxWR:$src1, HvxWR:$src2),
         (V6_vmpabuuv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpabuuv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vmpabuuv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrwhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrwhsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vminuh HvxVR:$src1, HvxVR:$src2),
         (V6_vminuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vminuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vminuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vror HvxVR:$src1, IntRegs:$src2),
         (V6_vror HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vror_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vror HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyowh_rnd_sacc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyowh_rnd_sacc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyowh_rnd_sacc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyowh_rnd_sacc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmaxuh HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmaxuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsh_sat HvxVR:$src1),
         (V6_vabsh_sat HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsh_sat_128B HvxVR:$src1),
         (V6_vabsh_sat HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_or_n HvxQR:$src1, HvxQR:$src2),
         (V6_pred_or_n HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_or_n_128B HvxQR:$src1, HvxQR:$src2),
         (V6_pred_or_n HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdealb HvxVR:$src1),
         (V6_vdealb HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdealb_128B HvxVR:$src1),
         (V6_vdealb HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpybusv HvxVR:$src1, HvxVR:$src2),
         (V6_vmpybusv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpybusv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpybusv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vzb HvxVR:$src1),
         (V6_vzb HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vzb_128B HvxVR:$src1),
         (V6_vzb HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpybus_dv HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpybus_dv HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpybus_dv_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpybus_dv HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddbq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddbq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddbq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddbq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddb HvxVR:$src1, HvxVR:$src2),
         (V6_vaddb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddwq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddwq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddwq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddwq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrhubrndsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrhubsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshufoeb HvxVR:$src1, HvxVR:$src2),
         (V6_vshufoeb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshufoeb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vshufoeb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackhub_sat HvxVR:$src1, HvxVR:$src2),
         (V6_vpackhub_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackhub_sat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackhub_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiwh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyiwh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiwh_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyiwh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vtmpyb HvxWR:$src1, IntRegs:$src2),
         (V6_vtmpyb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vtmpyb_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vtmpyb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpabusv HvxWR:$src1, HvxWR:$src2),
         (V6_vmpabusv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpabusv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vmpabusv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_and HvxQR:$src1, HvxQR:$src2),
         (V6_pred_and HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_and_128B HvxQR:$src1, HvxQR:$src2),
         (V6_pred_and HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubwnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubwnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubwnq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubwnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackwuh_sat HvxVR:$src1, HvxVR:$src2),
         (V6_vpackwuh_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackwuh_sat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackwuh_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vswap HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vswap HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vswap_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vswap HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyubv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vrmpyubv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyubv_acc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vrmpyubv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtb_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtb_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtb_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtb_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaslw HvxVR:$src1, IntRegs:$src2),
         (V6_vaslw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaslw_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vaslw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackhb_sat HvxVR:$src1, HvxVR:$src2),
         (V6_vpackhb_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackhb_sat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackhb_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyih_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyih_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyih_acc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyih_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshuffvdd HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vshuffvdd HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshuffvdd_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vshuffvdd HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddb_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vaddb_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddb_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vaddb_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vunpackub HvxVR:$src1),
         (V6_vunpackub HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vunpackub_128B HvxVR:$src1),
         (V6_vunpackub HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuw HvxVR:$src1, HvxVR:$src2),
         (V6_vgtuw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vgtuw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvwh HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvwh HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvwh_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvwh HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtub HvxVR:$src1, HvxVR:$src2),
         (V6_vgtub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vgtub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyowh HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyowh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyowh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyowh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyieoh HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyieoh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyieoh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyieoh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_extractw HvxVR:$src1, IntRegs:$src2),
         (V6_extractw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_extractw_128B HvxVR:$src1, IntRegs:$src2),
         (V6_extractw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavgwrnd HvxVR:$src1, HvxVR:$src2),
         (V6_vavgwrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavgwrnd_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavgwrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhsat_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpyhsat_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhsat_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpyhsat_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtub_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtub_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtub_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtub_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyub HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyub HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyub_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyub HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyuh HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyuh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyuh_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyuh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vunpackob HvxWR:$src1, HvxVR:$src2),
         (V6_vunpackob HvxWR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vunpackob_128B HvxWR:$src1, HvxVR:$src2),
         (V6_vunpackob HvxWR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpahb HvxWR:$src1, IntRegs:$src2),
         (V6_vmpahb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpahb_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vmpahb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqw_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vandqrt HvxQR:$src1, IntRegs:$src2),
         (V6_vandqrt HvxQR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandqrt_128B HvxQR:$src1, IntRegs:$src2),
         (V6_vandqrt HvxQR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vxor HvxVR:$src1, HvxVR:$src2),
         (V6_vxor HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vxor_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vxor HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrwhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrwhrndsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhsat_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyhsat_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhsat_acc_128B HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyhsat_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybus_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vrmpybus_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybus_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vrmpybus_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubhw HvxVR:$src1, HvxVR:$src2),
         (V6_vsubhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubhw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdealb4w HvxVR:$src1, HvxVR:$src2),
         (V6_vdealb4w HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdealb4w_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vdealb4w HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyowh_sacc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyowh_sacc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyowh_sacc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyowh_sacc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpybv HvxVR:$src1, HvxVR:$src2),
         (V6_vmpybv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpybv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpybv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsdiffh HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsdiffh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshuffob HvxVR:$src1, HvxVR:$src2),
         (V6_vshuffob HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshuffob_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vshuffob HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyub_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyub_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyub_acc_128B HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyub_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vnormamtw HvxVR:$src1),
         (V6_vnormamtw HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vnormamtw_128B HvxVR:$src1),
         (V6_vnormamtw HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vunpackuh HvxVR:$src1),
         (V6_vunpackuh HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vunpackuh_128B HvxVR:$src1),
         (V6_vunpackuh HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuh_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuh_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuh_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuh_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiewuh_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyiewuh_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiewuh_acc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyiewuh_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vunpackoh HvxWR:$src1, HvxVR:$src2),
         (V6_vunpackoh HvxWR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vunpackoh_128B HvxWR:$src1, HvxVR:$src2),
         (V6_vunpackoh HvxWR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhsat HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpyhsat HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhsat_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpyhsat HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyubv HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyubv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyubv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyubv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhss HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyhss HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhss_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyhss HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_hi HvxWR:$src1),
         (V6_hi HvxWR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_hi_128B HvxWR:$src1),
         (V6_hi HvxWR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrwuhsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqw HvxVR:$src1, HvxVR:$src2),
         (V6_veqw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_veqw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdsaduh HvxWR:$src1, IntRegs:$src2),
         (V6_vdsaduh HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdsaduh_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vdsaduh HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubw HvxVR:$src1, HvxVR:$src2),
         (V6_vsubw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubw_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubw_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubw_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubw_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqb_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqb_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqb_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqb_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyih HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyih HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyih_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyih HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vtmpyb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vtmpyb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vtmpyb_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vtmpyb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybus HvxVR:$src1, IntRegs:$src2),
         (V6_vrmpybus HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybus_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vrmpybus HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpybus_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpybus_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpybus_acc_128B HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpybus_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgth_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgth_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgth_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgth_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubhsat HvxVR:$src1, HvxVR:$src2),
         (V6_vsubhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubhsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyubi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4),
         (V6_vrmpyubi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyubi_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4),
         (V6_vrmpyubi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsw HvxVR:$src1),
         (V6_vabsw HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsw_128B HvxVR:$src1),
         (V6_vabsw HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddwsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vaddwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddwsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vaddwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlsrw HvxVR:$src1, IntRegs:$src2),
         (V6_vlsrw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlsrw_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vlsrw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsh HvxVR:$src1),
         (V6_vabsh HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsh_128B HvxVR:$src1),
         (V6_vabsh HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlsrh HvxVR:$src1, IntRegs:$src2),
         (V6_vlsrh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlsrh_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vlsrh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_valignb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_valignb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_valignb_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_valignb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubhq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubhq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubhq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubhq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackoh HvxVR:$src1, HvxVR:$src2),
         (V6_vpackoh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackoh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackoh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpybus_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpybus_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpybus_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpybus_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhvsat_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vdmpyhvsat_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhvsat_acc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vdmpyhvsat_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vrmpybv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybv_acc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vrmpybv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddhsat HvxVR:$src1, HvxVR:$src2),
         (V6_vaddhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddhsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddhsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vcombine HvxVR:$src1, HvxVR:$src2),
         (V6_vcombine HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vcombine_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vcombine HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vandqrt_acc HvxVR:$src1, HvxQR:$src2, IntRegs:$src3),
         (V6_vandqrt_acc HvxVR:$src1, HvxQR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandqrt_acc_128B HvxVR:$src1, HvxQR:$src2, IntRegs:$src3),
         (V6_vandqrt_acc HvxVR:$src1, HvxQR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaslhv HvxVR:$src1, HvxVR:$src2),
         (V6_vaslhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaslhv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaslhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vinsertwr HvxVR:$src1, IntRegs:$src2),
         (V6_vinsertwr HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vinsertwr_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vinsertwr HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubh_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubh_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubh_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubh_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshuffb HvxVR:$src1),
         (V6_vshuffb HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshuffb_128B HvxVR:$src1),
         (V6_vshuffb HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vand HvxVR:$src1, HvxVR:$src2),
         (V6_vand HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vand_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vand HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhv HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhsuisat_acc HvxVR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpyhsuisat_acc HvxVR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhsuisat_acc_128B HvxVR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpyhsuisat_acc HvxVR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsububsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsububsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsububsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsububsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtb_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtb_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtb_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtb_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdsaduh_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdsaduh_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdsaduh_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdsaduh_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyub HvxVR:$src1, IntRegs:$src2),
         (V6_vrmpyub HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyub_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vrmpyub HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyuh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyuh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyuh_acc_128B HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyuh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vcl0h HvxVR:$src1),
         (V6_vcl0h HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vcl0h_128B HvxVR:$src1),
         (V6_vcl0h HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhus_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyhus_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhus_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyhus_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpybv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpybv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpybv_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpybv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrsadubi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3),
         (V6_vrsadubi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrsadubi_128B HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3),
         (V6_vrsadubi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhb_dv_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpyhb_dv_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhb_dv_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpyhb_dv_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshufeh HvxVR:$src1, HvxVR:$src2),
         (V6_vshufeh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshufeh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vshufeh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyewuh HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyewuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyewuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyewuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyhsrs HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyhsrs HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyhsrs_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyhsrs HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpybus_dv_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpybus_dv_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpybus_dv_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpybus_dv_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddubh HvxVR:$src1, HvxVR:$src2),
         (V6_vaddubh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddubh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddubh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrwh HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwh HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrwh_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwh HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ld0 IntRegs:$src1),
         (V6_ld0 IntRegs:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ld0_128B IntRegs:$src1),
         (V6_ld0 IntRegs:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpopcounth HvxVR:$src1),
         (V6_vpopcounth HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpopcounth_128B HvxVR:$src1),
         (V6_vpopcounth HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldnt0 IntRegs:$src1),
         (V6_ldnt0 IntRegs:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldnt0_128B IntRegs:$src1),
         (V6_ldnt0 IntRegs:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgth_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgth_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgth_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgth_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddubsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vaddubsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddubsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vaddubsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackeh HvxVR:$src1, HvxVR:$src2),
         (V6_vpackeh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackeh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackeh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyh HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyh_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vminh HvxVR:$src1, HvxVR:$src2),
         (V6_vminh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vminh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vminh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_scalar2 IntRegs:$src1),
         (V6_pred_scalar2 IntRegs:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_scalar2_128B IntRegs:$src1),
         (V6_pred_scalar2 IntRegs:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdealh HvxVR:$src1),
         (V6_vdealh HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdealh_128B HvxVR:$src1),
         (V6_vdealh HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vpackwh_sat HvxVR:$src1, HvxVR:$src2),
         (V6_vpackwh_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vpackwh_sat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vpackwh_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaslh HvxVR:$src1, IntRegs:$src2),
         (V6_vaslh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaslh_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vaslh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuw_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vor HvxVR:$src1, HvxVR:$src2),
         (V6_vor HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vor_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vor HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvvb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvvb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvvb_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvvb HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiowh HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyiowh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiowh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyiowh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvvb_oracc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4),
         (V6_vlutvvb_oracc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvvb_oracc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4),
         (V6_vlutvvb_oracc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vandvrt HvxVR:$src1, IntRegs:$src2),
         (V6_vandvrt HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandvrt_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vandvrt HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqh_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqh_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqh_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqh_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vadduhw HvxVR:$src1, HvxVR:$src2),
         (V6_vadduhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vadduhw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vadduhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vcl0w HvxVR:$src1),
         (V6_vcl0w HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vcl0w_128B HvxVR:$src1),
         (V6_vcl0w HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyihb HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyihb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyihb_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyihb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vtmpybus HvxWR:$src1, IntRegs:$src2),
         (V6_vtmpybus HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vtmpybus_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vtmpybus HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vd0 ),
         (V6_vd0 )>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vd0_128B ),
         (V6_vd0 )>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqh_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqh_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqh_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqh_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtw_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpybus HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpybus HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpybus_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpybus HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtub_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtub_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtub_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtub_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpybus HvxVR:$src1, IntRegs:$src2),
         (V6_vmpybus HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpybus_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpybus HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpyhb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhb_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpyhb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vandvrt_acc HvxQR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vandvrt_acc HvxQR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandvrt_acc_128B HvxQR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vandvrt_acc HvxQR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vassign HvxVR:$src1),
         (V6_vassign HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vassign_128B HvxVR:$src1),
         (V6_vassign HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddwnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddwnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddwnq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddwnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtub_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtub_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtub_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtub_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhb_dv HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpyhb_dv HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhb_dv_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vdmpyhb_dv HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vunpackb HvxVR:$src1),
         (V6_vunpackb HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vunpackb_128B HvxVR:$src1),
         (V6_vunpackb HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vunpackh HvxVR:$src1),
         (V6_vunpackh HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vunpackh_128B HvxVR:$src1),
         (V6_vunpackh HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpahb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpahb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpahb_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpahb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddbnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddbnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddbnq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddbnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlalignbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_vlalignbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlalignbi_128B HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_vlalignbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsatwh HvxVR:$src1, HvxVR:$src2),
         (V6_vsatwh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsatwh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsatwh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuh HvxVR:$src1, HvxVR:$src2),
         (V6_vgtuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vgtuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyihb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyihb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyihb_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyihb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybusv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vrmpybusv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybusv_acc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vrmpybusv_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrdelta HvxVR:$src1, HvxVR:$src2),
         (V6_vrdelta HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrdelta_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vrdelta HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vroundwh HvxVR:$src1, HvxVR:$src2),
         (V6_vroundwh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vroundwh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vroundwh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddw_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vaddw_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddw_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vaddw_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiwb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyiwb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiwb_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyiwb_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubbq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubbq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubbq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubbq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqh_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqh_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqh_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqh_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_valignbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_valignbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_valignbi_128B HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_valignbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddwsat HvxVR:$src1, HvxVR:$src2),
         (V6_vaddwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddwsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqw_and_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqw_and HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsdiffub HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsdiffub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vshuffeb HvxVR:$src1, HvxVR:$src2),
         (V6_vshuffeb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vshuffeb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vshuffeb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsdiffuh HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsdiffuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqw_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgth HvxVR:$src1, HvxVR:$src2),
         (V6_vgth HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgth_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vgth HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuw_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtb HvxVR:$src1, HvxVR:$src2),
         (V6_vgtb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vgtb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtw HvxVR:$src1, HvxVR:$src2),
         (V6_vgtw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vgtw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubwq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubwq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubwq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vsubwq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vnot HvxVR:$src1),
         (V6_vnot HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vnot_128B HvxVR:$src1),
         (V6_vnot HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtb_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtb_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtb_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtb_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtuw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtuw_or_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtuw_or HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddubsat HvxVR:$src1, HvxVR:$src2),
         (V6_vaddubsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddubsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddubsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmaxw HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmaxw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaslwv HvxVR:$src1, HvxVR:$src2),
         (V6_vaslwv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaslwv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaslwv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsw_sat HvxVR:$src1),
         (V6_vabsw_sat HvxVR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsw_sat_128B HvxVR:$src1),
         (V6_vabsw_sat HvxVR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubwsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubwsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vroundhub HvxVR:$src1, HvxVR:$src2),
         (V6_vroundhub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vroundhub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vroundhub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhisat_acc HvxVR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpyhisat_acc HvxVR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhisat_acc_128B HvxVR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vdmpyhisat_acc HvxVR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpabus HvxWR:$src1, IntRegs:$src2),
         (V6_vmpabus HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpabus_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vmpabus HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vassignp HvxWR:$src1),
         (V6_vassignp HvxWR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vassignp_128B HvxWR:$src1),
         (V6_vassignp HvxWR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqb HvxVR:$src1, HvxVR:$src2),
         (V6_veqb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_veqb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsububh HvxVR:$src1, HvxVR:$src2),
         (V6_vsububh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsububh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsububh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_lvsplatw IntRegs:$src1),
         (V6_lvsplatw IntRegs:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_lvsplatw_128B IntRegs:$src1),
         (V6_lvsplatw IntRegs:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddhnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddhnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddhnq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddhnq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhsusat HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpyhsusat HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhsusat_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vdmpyhsusat HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_not HvxQR:$src1),
         (V6_pred_not HvxQR:$src1)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_not_128B HvxQR:$src1),
         (V6_pred_not HvxQR:$src1)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvwh_oracc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4),
         (V6_vlutvwh_oracc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvwh_oracc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4),
         (V6_vlutvwh_oracc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegsLow8:$src4)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiewh_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyiewh_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiewh_acc_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyiewh_acc HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdealvdd HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vdealvdd HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdealvdd_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vdealvdd HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavgw HvxVR:$src1, HvxVR:$src2),
         (V6_vavgw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavgw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavgw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdmpyhsusat_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpyhsusat_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdmpyhsusat_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vdmpyhsusat_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vgtw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vgtw_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vgtw_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vtmpyhb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vtmpyhb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vtmpyhb_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vtmpyhb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddhw HvxVR:$src1, HvxVR:$src2),
         (V6_vaddhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddhw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddhq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddhq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddhq_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddhq HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyubv HvxVR:$src1, HvxVR:$src2),
         (V6_vrmpyubv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyubv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vrmpyubv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubh HvxVR:$src1, HvxVR:$src2),
         (V6_vsubh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyubi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3),
         (V6_vrmpyubi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyubi_128B HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3),
         (V6_vrmpyubi HvxWR:$src1, IntRegs:$src2, u1_0ImmPred:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vminw HvxVR:$src1, HvxVR:$src2),
         (V6_vminw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vminw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vminw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyubv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyubv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyubv_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyubv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_xor HvxQR:$src1, HvxQR:$src2),
         (V6_pred_xor HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_xor_128B HvxQR:$src1, HvxQR:$src2),
         (V6_pred_xor HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_veqb_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqb_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_veqb_xor_128B HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_veqb_xor HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiewuh HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyiewuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiewuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyiewuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpybusv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpybusv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpybusv_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpybusv_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavguhrnd HvxVR:$src1, HvxVR:$src2),
         (V6_vavguhrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavguhrnd_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavguhrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyowh_rnd HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyowh_rnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyowh_rnd_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyowh_rnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubwsat HvxVR:$src1, HvxVR:$src2),
         (V6_vsubwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubwsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubuhw HvxVR:$src1, HvxVR:$src2),
         (V6_vsubuhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubuhw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubuhw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybusi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4),
         (V6_vrmpybusi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybusi_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4),
         (V6_vrmpybusi_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, u1_0ImmPred:$src4)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrw HvxVR:$src1, IntRegs:$src2),
         (V6_vasrw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrw_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vasrw HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrh HvxVR:$src1, IntRegs:$src2),
         (V6_vasrh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrh_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vasrh HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyuhv HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyuhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyuhv_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyuhv HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrhbrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhbrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrhbrndsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhbrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubuhsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubuhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubuhsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubuhsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV60, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsdiffw HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsdiffw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vabsdiffw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV60, UseHVX128B]>;

// V62 HVX Instructions.

def: Pat<(int_hexagon_V6_vandnqrt_acc HvxVR:$src1, HvxQR:$src2, IntRegs:$src3),
         (V6_vandnqrt_acc HvxVR:$src1, HvxQR:$src2, IntRegs:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandnqrt_acc_128B HvxVR:$src1, HvxQR:$src2, IntRegs:$src3),
         (V6_vandnqrt_acc HvxVR:$src1, HvxQR:$src2, IntRegs:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddclbh HvxVR:$src1, HvxVR:$src2),
         (V6_vaddclbh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddclbh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddclbh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyowh_64_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyowh_64_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyowh_64_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vmpyowh_64_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyewuh_64 HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyewuh_64 HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyewuh_64_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmpyewuh_64 HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsatuwuh HvxVR:$src1, HvxVR:$src2),
         (V6_vsatuwuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsatuwuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsatuwuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_shuffeqh HvxQR:$src1, HvxQR:$src2),
         (V6_shuffeqh HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_shuffeqh_128B HvxQR:$src1, HvxQR:$src2),
         (V6_shuffeqh HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_shuffeqw HvxQR:$src1, HvxQR:$src2),
         (V6_shuffeqw HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_shuffeqw_128B HvxQR:$src1, HvxQR:$src2),
         (V6_shuffeqw HvxQR:$src1, HvxQR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldcnpnt0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldcnpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldcnpnt0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldcnpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubcarry HvxVR:$src1, HvxVR:$src2, HvxQR:$src3),
         (V6_vsubcarry HvxVR:$src1, HvxVR:$src2, HvxQR:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubcarry_128B HvxVR:$src1, HvxVR:$src2, HvxQR:$src3),
         (V6_vsubcarry HvxVR:$src1, HvxVR:$src2, HvxQR:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrhbsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhbsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrhbsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrhbsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vminb HvxVR:$src1, HvxVR:$src2),
         (V6_vminb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vminb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vminb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpauhb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpauhb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpauhb_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpauhb_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddhw_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddhw_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddhw_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddhw_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlsrb HvxVR:$src1, IntRegs:$src2),
         (V6_vlsrb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlsrb_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vlsrb HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvwhi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_vlutvwhi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvwhi_128B HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_vlutvwhi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddububb_sat HvxVR:$src1, HvxVR:$src2),
         (V6_vaddububb_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddububb_sat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddububb_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubbsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubbsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubbsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubbsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldtp0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldtp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldtp0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldtp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvvb_oracci HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4),
         (V6_vlutvvb_oracci HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvvb_oracci_128B HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4),
         (V6_vlutvvb_oracci HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubuwsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vsubuwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubuwsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vsubuwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldpnt0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldpnt0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vandvnqv HvxQR:$src1, HvxVR:$src2),
         (V6_vandvnqv HvxQR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandvnqv_128B HvxQR:$src1, HvxVR:$src2),
         (V6_vandvnqv HvxQR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_lvsplatb IntRegs:$src1),
         (V6_lvsplatb IntRegs:$src1)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_lvsplatb_128B IntRegs:$src1),
         (V6_lvsplatb IntRegs:$src1)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_lvsplath IntRegs:$src1),
         (V6_lvsplath IntRegs:$src1)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_lvsplath_128B IntRegs:$src1),
         (V6_lvsplath IntRegs:$src1)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldtpnt0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldtpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldtpnt0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldtpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvwh_nm HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvwh_nm HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvwh_nm_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvwh_nm HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldnpnt0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldnpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldnpnt0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldnpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpauhb HvxWR:$src1, IntRegs:$src2),
         (V6_vmpauhb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpauhb_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vmpauhb HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldtnp0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldtnp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldtnp0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldtnp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrounduhub HvxVR:$src1, HvxVR:$src2),
         (V6_vrounduhub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrounduhub_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vrounduhub HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vadduhw_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vadduhw_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vadduhw_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vadduhw_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldcp0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldcp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldcp0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldcp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vadduwsat HvxVR:$src1, HvxVR:$src2),
         (V6_vadduwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vadduwsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vadduwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldtnpnt0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldtnpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldtnpnt0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldtnpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddbsat HvxVR:$src1, HvxVR:$src2),
         (V6_vaddbsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddbsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddbsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vandnqrt HvxQR:$src1, IntRegs:$src2),
         (V6_vandnqrt HvxQR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandnqrt_128B HvxQR:$src1, IntRegs:$src2),
         (V6_vandnqrt HvxQR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiwub_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyiwub_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiwub_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyiwub_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmaxb HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmaxb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vmaxb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vandvqv HvxQR:$src1, HvxVR:$src2),
         (V6_vandvqv HvxQR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vandvqv_128B HvxQR:$src1, HvxVR:$src2),
         (V6_vandvqv HvxQR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddcarry HvxVR:$src1, HvxVR:$src2, HvxQR:$src3),
         (V6_vaddcarry HvxVR:$src1, HvxVR:$src2, HvxQR:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddcarry_128B HvxVR:$src1, HvxVR:$src2, HvxQR:$src3),
         (V6_vaddcarry HvxVR:$src1, HvxVR:$src2, HvxQR:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrwuhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwuhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrwuhrndsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasrwuhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvvbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_vlutvvbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvvbi_128B HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
         (V6_vlutvvbi HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubuwsat HvxVR:$src1, HvxVR:$src2),
         (V6_vsubuwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubuwsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubuwsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddbsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vaddbsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddbsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vaddbsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldnp0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldnp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldnp0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldnp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasruwuhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruwuhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasruwuhrndsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruwuhrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrounduwuh HvxVR:$src1, HvxVR:$src2),
         (V6_vrounduwuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrounduwuh_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vrounduwuh HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvvb_nm HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvvb_nm HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvvb_nm_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vlutvvb_nm HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_pred_scalar2v2 IntRegs:$src1),
         (V6_pred_scalar2v2 IntRegs:$src1)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_pred_scalar2v2_128B IntRegs:$src1),
         (V6_pred_scalar2v2 IntRegs:$src1)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldp0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldp0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddubh_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddubh_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddubh_acc_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vaddubh_acc HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaddclbw HvxVR:$src1, HvxVR:$src2),
         (V6_vaddclbw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddclbw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vaddclbw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldcpnt0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldcpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldcpnt0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldcpnt0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vadduwsat_dv HvxWR:$src1, HvxWR:$src2),
         (V6_vadduwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vadduwsat_dv_128B HvxWR:$src1, HvxWR:$src2),
         (V6_vadduwsat_dv HvxWR:$src1, HvxWR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyiwub HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyiwub HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyiwub_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyiwub HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubububb_sat HvxVR:$src1, HvxVR:$src2),
         (V6_vsubububb_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubububb_sat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubububb_sat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_ldcnp0 PredRegs:$src1, IntRegs:$src2),
         (V6_ldcnp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_ldcnp0_128B PredRegs:$src1, IntRegs:$src2),
         (V6_ldcnp0 PredRegs:$src1, IntRegs:$src2)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlutvwh_oracci HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4),
         (V6_vlutvwh_oracci HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlutvwh_oracci_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4),
         (V6_vlutvwh_oracci HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, u3_0ImmPred:$src4)>, Requires<[HasV62, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsubbsat HvxVR:$src1, HvxVR:$src2),
         (V6_vsubbsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsubbsat_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsubbsat HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV62, UseHVX128B]>;

// V65 HVX Instructions.

def: Pat<(int_hexagon_V6_vasruhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasruhubrndsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruhubrndsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt HvxVR:$src1, DoubleRegs:$src2),
         (V6_vrmpybub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt_128B HvxVR:$src1, DoubleRegs:$src2),
         (V6_vrmpybub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpahhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vmpahhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpahhsat_128B HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vmpahhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavguwrnd HvxVR:$src1, HvxVR:$src2),
         (V6_vavguwrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavguwrnd_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavguwrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vnavgb HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vnavgb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vnavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasrh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vasrh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasrh_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vasrh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpauhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vmpauhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpauhuhsat_128B HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vmpauhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyh_acc_128B HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyh_acc HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vrmpybub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpybub_rtt_acc_128B HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vrmpybub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavgb HvxVR:$src1, HvxVR:$src2),
         (V6_vavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavgb_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavgb HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vaslh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vaslh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaslh_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vaslh_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavguw HvxVR:$src1, HvxVR:$src2),
         (V6_vavguw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavguw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavguw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vlut4 HvxVR:$src1, DoubleRegs:$src2),
         (V6_vlut4 HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vlut4_128B HvxVR:$src1, DoubleRegs:$src2),
         (V6_vlut4 HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyuhe_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyuhe_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyuhe_acc_128B HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
         (V6_vmpyuhe_acc HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt HvxVR:$src1, DoubleRegs:$src2),
         (V6_vrmpyub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt_128B HvxVR:$src1, DoubleRegs:$src2),
         (V6_vrmpyub_rtt HvxVR:$src1, DoubleRegs:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpsuhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vmpsuhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpsuhuhsat_128B HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vmpsuhuhsat HvxVR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasruhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasruhubsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruhubsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpyuhe HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyuhe HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpyuhe_128B HvxVR:$src1, IntRegs:$src2),
         (V6_vmpyuhe HvxVR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vrmpyub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrmpyub_rtt_acc_128B HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3),
         (V6_vrmpyub_rtt_acc HvxWR:$src1, HvxVR:$src2, DoubleRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasruwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasruwuhsat_128B HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3),
         (V6_vasruwuhsat HvxVR:$src1, HvxVR:$src2, IntRegsLow8:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpabuu_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpabuu_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpabuu_acc_128B HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
         (V6_vmpabuu_acc HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vprefixqw HvxQR:$src1),
         (V6_vprefixqw HvxQR:$src1)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vprefixqw_128B HvxQR:$src1),
         (V6_vprefixqw HvxQR:$src1)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vprefixqh HvxQR:$src1),
         (V6_vprefixqh HvxQR:$src1)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vprefixqh_128B HvxQR:$src1),
         (V6_vprefixqh HvxQR:$src1)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vprefixqb HvxQR:$src1),
         (V6_vprefixqb HvxQR:$src1)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vprefixqb_128B HvxQR:$src1),
         (V6_vprefixqb HvxQR:$src1)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsb HvxVR:$src1),
         (V6_vabsb HvxVR:$src1)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsb_128B HvxVR:$src1),
         (V6_vabsb HvxVR:$src1)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vavgbrnd HvxVR:$src1, HvxVR:$src2),
         (V6_vavgbrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vavgbrnd_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vavgbrnd HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vdd0 ),
         (V6_vdd0 )>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdd0_128B ),
         (V6_vdd0 )>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vmpabuu HvxWR:$src1, IntRegs:$src2),
         (V6_vmpabuu HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vmpabuu_128B HvxWR:$src1, IntRegs:$src2),
         (V6_vmpabuu HvxWR:$src1, IntRegs:$src2)>, Requires<[HasV65, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vabsb_sat HvxVR:$src1),
         (V6_vabsb_sat HvxVR:$src1)>, Requires<[HasV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vabsb_sat_128B HvxVR:$src1),
         (V6_vabsb_sat HvxVR:$src1)>, Requires<[HasV65, UseHVX128B]>;

// V66 HVX Instructions.

def: Pat<(int_hexagon_V6_vaddcarrysat HvxVR:$src1, HvxVR:$src2, HvxQR:$src3),
         (V6_vaddcarrysat HvxVR:$src1, HvxVR:$src2, HvxQR:$src3)>, Requires<[HasV66, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vaddcarrysat_128B HvxVR:$src1, HvxVR:$src2, HvxQR:$src3),
         (V6_vaddcarrysat HvxVR:$src1, HvxVR:$src2, HvxQR:$src3)>, Requires<[HasV66, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vasr_into HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vasr_into HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV66, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vasr_into_128B HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
         (V6_vasr_into HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>, Requires<[HasV66, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vsatdw HvxVR:$src1, HvxVR:$src2),
         (V6_vsatdw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV66, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vsatdw_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vsatdw HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV66, UseHVX128B]>;
def: Pat<(int_hexagon_V6_vrotr HvxVR:$src1, HvxVR:$src2),
         (V6_vrotr HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV66, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vrotr_128B HvxVR:$src1, HvxVR:$src2),
         (V6_vrotr HvxVR:$src1, HvxVR:$src2)>, Requires<[HasV66, UseHVX128B]>;
