/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "display_mode_util.h"

static dml_float_t _log(float in)
{
	int * const exp_ptr = (int *)(&in);
	int x = *exp_ptr;
	const int log_2 = ((x >> 23) & 255) - 128;

	x &= ~(255 << 23);
	x += 127 << 23;
	*exp_ptr = x;

	in = ((-1.0f / 3) * in + 2) * in - 2.0f / 3;

	return (in + log_2);
}

dml_bool_t dml_util_is_420(enum dml_source_format_class source_format)
{
	dml_bool_t val = false;

	switch (source_format) {
	case dml_444_16:
		val = 0;
		break;
	case dml_444_32:
		val = 0;
		break;
	case dml_444_64:
		val = 0;
		break;
	case dml_420_8:
		val = 1;
		break;
	case dml_420_10:
		val = 1;
		break;
	case dml_422_8:
		val = 0;
		break;
	case dml_422_10:
		val = 0;
		break;
	default:
		ASSERT(0);
		break;
	}
	return val;
}

static inline float dcn_bw_pow(float a, float exp)
{
	float temp;
	/*ASSERT(exp == (int)exp);*/
	if ((int)exp == 0)
		return 1;
	temp = dcn_bw_pow(a, (int)(exp / 2));
	if (((int)exp % 2) == 0) {
		return temp * temp;
	} else {
		if ((int)exp > 0)
			return a * temp * temp;
		else
			return (temp * temp) / a;
	}
}

static inline float dcn_bw_ceil2(const float arg, const float significance)
{
	ASSERT(significance != 0);

	return ((int)(arg / significance + 0.99999)) * significance;
}

static inline float dcn_bw_floor2(const float arg, const float significance)
{
	ASSERT(significance != 0);

	return ((int)(arg / significance)) * significance;
}

dml_float_t dml_ceil(dml_float_t x, dml_float_t granularity)
{
	if (granularity == 0)
		return 0;
	//return (dml_float_t) (ceil(x / granularity) * granularity);
	return (dml_float_t)dcn_bw_ceil2(x, granularity);
}

dml_float_t dml_floor(dml_float_t x, dml_float_t granularity)
{
	if (granularity == 0)
	return 0;
	//return (dml_float_t) (floor(x / granularity) * granularity);
	return (dml_float_t)dcn_bw_floor2(x, granularity);
}

dml_float_t dml_min(dml_float_t x, dml_float_t y)
{
	if (x != x)
		return y;
	if (y != y)
		return x;
	if (x < y)
		return x;
	else
		return y;
}

dml_float_t dml_min3(dml_float_t x, dml_float_t y, dml_float_t z)
{
	return dml_min(dml_min(x, y), z);
}

dml_float_t dml_min4(dml_float_t x, dml_float_t y, dml_float_t z, dml_float_t w)
{
	return dml_min(dml_min(dml_min(x, y), z), w);
}

dml_float_t dml_max(dml_float_t x, dml_float_t y)
{
	if (x != x)
		return y;
	if (y != y)
		return x;
if (x > y)
		return x;
	else
		return y;
}
dml_float_t dml_max3(dml_float_t x, dml_float_t y, dml_float_t z)
{
	return dml_max(dml_max(x, y), z);
}
dml_float_t dml_max4(dml_float_t a, dml_float_t b, dml_float_t c, dml_float_t d)
{
	return dml_max(dml_max(a, b), dml_max(c, d));
}
dml_float_t dml_max5(dml_float_t a, dml_float_t b, dml_float_t c, dml_float_t d, dml_float_t e)
{
	return dml_max(dml_max4(a, b, c, d), e);
}
dml_float_t dml_log(dml_float_t x, dml_float_t base)
{
	return (dml_float_t) (_log(x) / _log(base));
}

dml_float_t dml_log2(dml_float_t x)
{
	return (dml_float_t) (_log(x) / _log(2));
}

dml_float_t dml_round(dml_float_t val, dml_bool_t bankers_rounding)
{
//	if (bankers_rounding)
//			return (dml_float_t) lrint(val);
//	else {
//		return round(val);
		double round_pt = 0.5;
		double ceil = dml_ceil(val, 1);
		double floor = dml_floor(val, 1);

		if (val - floor >= round_pt)
			return ceil;
		else
			return floor;
//	}
}

dml_float_t dml_pow(dml_float_t base, int exp)
{
	return (dml_float_t) dcn_bw_pow(base, exp);
}

dml_uint_t dml_round_to_multiple(dml_uint_t num, dml_uint_t multiple, dml_bool_t up)
{
	dml_uint_t remainder;

	if (multiple == 0)
		return num;

	remainder = num % multiple;
	if (remainder == 0)
		return num;

	if (up)
		return (num + multiple - remainder);
	else
		return (num - remainder);
}

void dml_print_data_rq_regs_st(const dml_display_plane_rq_regs_st *rq_regs)
{
	dml_print("DML: ===================================== \n");
	dml_print("DML: DISPLAY_PLANE_RQ_REGS_ST\n");
	dml_print("DML: chunk_size = 0x%x\n", rq_regs->chunk_size);
	dml_print("DML: min_chunk_size = 0x%x\n", rq_regs->min_chunk_size);
	dml_print("DML: meta_chunk_size = 0x%x\n", rq_regs->meta_chunk_size);
	dml_print("DML: min_meta_chunk_size = 0x%x\n", rq_regs->min_meta_chunk_size);
	dml_print("DML: dpte_group_size = 0x%x\n", rq_regs->dpte_group_size);
	dml_print("DML: mpte_group_size = 0x%x\n", rq_regs->mpte_group_size);
	dml_print("DML: swath_height = 0x%x\n", rq_regs->swath_height);
	dml_print("DML: pte_row_height_linear = 0x%x\n", rq_regs->pte_row_height_linear);
	dml_print("DML: ===================================== \n");
}

void dml_print_rq_regs_st(const dml_display_rq_regs_st *rq_regs)
{
	dml_print("DML: ===================================== \n");
	dml_print("DML: DISPLAY_RQ_REGS_ST\n");
	dml_print("DML: <LUMA> \n");
	dml_print_data_rq_regs_st(&rq_regs->rq_regs_l);
	dml_print("DML: <CHROMA> \n");
	dml_print_data_rq_regs_st(&rq_regs->rq_regs_c);
	dml_print("DML: drq_expansion_mode = 0x%x\n", rq_regs->drq_expansion_mode);
	dml_print("DML: prq_expansion_mode = 0x%x\n", rq_regs->prq_expansion_mode);
	dml_print("DML: mrq_expansion_mode = 0x%x\n", rq_regs->mrq_expansion_mode);
	dml_print("DML: crq_expansion_mode = 0x%x\n", rq_regs->crq_expansion_mode);
	dml_print("DML: plane1_base_address = 0x%x\n", rq_regs->plane1_base_address);
	dml_print("DML: ===================================== \n");
}

void dml_print_dlg_regs_st(const dml_display_dlg_regs_st *dlg_regs)
{
	dml_print("DML: ===================================== \n");
	dml_print("DML: DISPLAY_DLG_REGS_ST \n");
	dml_print("DML: refcyc_h_blank_end = 0x%x\n", dlg_regs->refcyc_h_blank_end);
	dml_print("DML: dlg_vblank_end = 0x%x\n", dlg_regs->dlg_vblank_end);
	dml_print("DML: min_dst_y_next_start = 0x%x\n", dlg_regs->min_dst_y_next_start);
	dml_print("DML: refcyc_per_htotal = 0x%x\n", dlg_regs->refcyc_per_htotal);
	dml_print("DML: refcyc_x_after_scaler = 0x%x\n", dlg_regs->refcyc_x_after_scaler);
	dml_print("DML: dst_y_after_scaler = 0x%x\n", dlg_regs->dst_y_after_scaler);
	dml_print("DML: dst_y_prefetch = 0x%x\n", dlg_regs->dst_y_prefetch);
	dml_print("DML: dst_y_per_vm_vblank = 0x%x\n", dlg_regs->dst_y_per_vm_vblank);
	dml_print("DML: dst_y_per_row_vblank = 0x%x\n", dlg_regs->dst_y_per_row_vblank);
	dml_print("DML: dst_y_per_vm_flip = 0x%x\n", dlg_regs->dst_y_per_vm_flip);
	dml_print("DML: dst_y_per_row_flip = 0x%x\n", dlg_regs->dst_y_per_row_flip);
	dml_print("DML: ref_freq_to_pix_freq = 0x%x\n", dlg_regs->ref_freq_to_pix_freq);
	dml_print("DML: vratio_prefetch = 0x%x\n", dlg_regs->vratio_prefetch);
	dml_print("DML: vratio_prefetch_c = 0x%x\n", dlg_regs->vratio_prefetch_c);
	dml_print("DML: refcyc_per_pte_group_vblank_l = 0x%x\n", dlg_regs->refcyc_per_pte_group_vblank_l);
	dml_print("DML: refcyc_per_pte_group_vblank_c = 0x%x\n", dlg_regs->refcyc_per_pte_group_vblank_c);
	dml_print("DML: refcyc_per_meta_chunk_vblank_l = 0x%x\n", dlg_regs->refcyc_per_meta_chunk_vblank_l);
	dml_print("DML: refcyc_per_meta_chunk_vblank_c = 0x%x\n", dlg_regs->refcyc_per_meta_chunk_vblank_c);
	dml_print("DML: refcyc_per_pte_group_flip_l = 0x%x\n", dlg_regs->refcyc_per_pte_group_flip_l);
	dml_print("DML: refcyc_per_pte_group_flip_c = 0x%x\n", dlg_regs->refcyc_per_pte_group_flip_c);
	dml_print("DML: refcyc_per_meta_chunk_flip_l = 0x%x\n", dlg_regs->refcyc_per_meta_chunk_flip_l);
	dml_print("DML: refcyc_per_meta_chunk_flip_c = 0x%x\n", dlg_regs->refcyc_per_meta_chunk_flip_c);
	dml_print("DML: dst_y_per_pte_row_nom_l = 0x%x\n", dlg_regs->dst_y_per_pte_row_nom_l);
	dml_print("DML: dst_y_per_pte_row_nom_c = 0x%x\n", dlg_regs->dst_y_per_pte_row_nom_c);
	dml_print("DML: refcyc_per_pte_group_nom_l = 0x%x\n", dlg_regs->refcyc_per_pte_group_nom_l);
	dml_print("DML: refcyc_per_pte_group_nom_c = 0x%x\n", dlg_regs->refcyc_per_pte_group_nom_c);
	dml_print("DML: dst_y_per_meta_row_nom_l = 0x%x\n", dlg_regs->dst_y_per_meta_row_nom_l);
	dml_print("DML: dst_y_per_meta_row_nom_c = 0x%x\n", dlg_regs->dst_y_per_meta_row_nom_c);
	dml_print("DML: refcyc_per_meta_chunk_nom_l = 0x%x\n", dlg_regs->refcyc_per_meta_chunk_nom_l);
	dml_print("DML: refcyc_per_meta_chunk_nom_c = 0x%x\n", dlg_regs->refcyc_per_meta_chunk_nom_c);
	dml_print("DML: refcyc_per_line_delivery_pre_l = 0x%x\n", dlg_regs->refcyc_per_line_delivery_pre_l);
	dml_print("DML: refcyc_per_line_delivery_pre_c = 0x%x\n", dlg_regs->refcyc_per_line_delivery_pre_c);
	dml_print("DML: refcyc_per_line_delivery_l = 0x%x\n", dlg_regs->refcyc_per_line_delivery_l);
	dml_print("DML: refcyc_per_line_delivery_c = 0x%x\n", dlg_regs->refcyc_per_line_delivery_c);
	dml_print("DML: refcyc_per_vm_group_vblank = 0x%x\n", dlg_regs->refcyc_per_vm_group_vblank);
	dml_print("DML: refcyc_per_vm_group_flip = 0x%x\n", dlg_regs->refcyc_per_vm_group_flip);
	dml_print("DML: refcyc_per_vm_req_vblank = 0x%x\n", dlg_regs->refcyc_per_vm_req_vblank);
	dml_print("DML: refcyc_per_vm_req_flip = 0x%x\n", dlg_regs->refcyc_per_vm_req_flip);
	dml_print("DML: chunk_hdl_adjust_cur0 = 0x%x\n", dlg_regs->chunk_hdl_adjust_cur0);
	dml_print("DML: dst_y_offset_cur1 = 0x%x\n", dlg_regs->dst_y_offset_cur1);
	dml_print("DML: chunk_hdl_adjust_cur1 = 0x%x\n", dlg_regs->chunk_hdl_adjust_cur1);
	dml_print("DML: vready_after_vcount0 = 0x%x\n", dlg_regs->vready_after_vcount0);
	dml_print("DML: dst_y_delta_drq_limit = 0x%x\n", dlg_regs->dst_y_delta_drq_limit);
	dml_print("DML: refcyc_per_vm_dmdata = 0x%x\n", dlg_regs->refcyc_per_vm_dmdata);
	dml_print("DML: ===================================== \n");
}

void dml_print_ttu_regs_st(const dml_display_ttu_regs_st *ttu_regs)
{
	dml_print("DML: ===================================== \n");
	dml_print("DML: DISPLAY_TTU_REGS_ST \n");
	dml_print("DML: qos_level_low_wm = 0x%x\n", ttu_regs->qos_level_low_wm);
	dml_print("DML: qos_level_high_wm = 0x%x\n", ttu_regs->qos_level_high_wm);
	dml_print("DML: min_ttu_vblank = 0x%x\n", ttu_regs->min_ttu_vblank);
	dml_print("DML: qos_level_flip = 0x%x\n", ttu_regs->qos_level_flip);
	dml_print("DML: refcyc_per_req_delivery_pre_l = 0x%x\n", ttu_regs->refcyc_per_req_delivery_pre_l);
	dml_print("DML: refcyc_per_req_delivery_l = 0x%x\n", ttu_regs->refcyc_per_req_delivery_l);
	dml_print("DML: refcyc_per_req_delivery_pre_c = 0x%x\n", ttu_regs->refcyc_per_req_delivery_pre_c);
	dml_print("DML: refcyc_per_req_delivery_c = 0x%x\n", ttu_regs->refcyc_per_req_delivery_c);
	dml_print("DML: refcyc_per_req_delivery_cur0 = 0x%x\n", ttu_regs->refcyc_per_req_delivery_cur0);
	dml_print("DML: refcyc_per_req_delivery_pre_cur0 = 0x%x\n", ttu_regs->refcyc_per_req_delivery_pre_cur0);
	dml_print("DML: refcyc_per_req_delivery_cur1 = 0x%x\n", ttu_regs->refcyc_per_req_delivery_cur1);
	dml_print("DML: refcyc_per_req_delivery_pre_cur1 = 0x%x\n", ttu_regs->refcyc_per_req_delivery_pre_cur1);
	dml_print("DML: qos_level_fixed_l = 0x%x\n", ttu_regs->qos_level_fixed_l);
	dml_print("DML: qos_ramp_disable_l = 0x%x\n", ttu_regs->qos_ramp_disable_l);
	dml_print("DML: qos_level_fixed_c = 0x%x\n", ttu_regs->qos_level_fixed_c);
	dml_print("DML: qos_ramp_disable_c = 0x%x\n", ttu_regs->qos_ramp_disable_c);
	dml_print("DML: qos_level_fixed_cur0 = 0x%x\n", ttu_regs->qos_level_fixed_cur0);
	dml_print("DML: qos_ramp_disable_cur0 = 0x%x\n", ttu_regs->qos_ramp_disable_cur0);
	dml_print("DML: qos_level_fixed_cur1 = 0x%x\n", ttu_regs->qos_level_fixed_cur1);
	dml_print("DML: qos_ramp_disable_cur1 = 0x%x\n", ttu_regs->qos_ramp_disable_cur1);
	dml_print("DML: ===================================== \n");
}

void dml_print_dml_policy(const struct dml_mode_eval_policy_st *policy)
{
	dml_print("DML: ===================================== \n");
	dml_print("DML: DML_MODE_EVAL_POLICY_ST\n");
	dml_print("DML: Policy: UseUnboundedRequesting = 0x%x\n", policy->UseUnboundedRequesting);
	dml_print("DML: Policy: UseMinimumRequiredDCFCLK = 0x%x\n", policy->UseMinimumRequiredDCFCLK);
	dml_print("DML: Policy: DRAMClockChangeRequirementFinal = 0x%x\n", policy->DRAMClockChangeRequirementFinal);
	dml_print("DML: Policy: FCLKChangeRequirementFinal = 0x%x\n", policy->FCLKChangeRequirementFinal);
	dml_print("DML: Policy: USRRetrainingRequiredFinal = 0x%x\n", policy->USRRetrainingRequiredFinal);
	dml_print("DML: Policy: EnhancedPrefetchScheduleAccelerationFinal = 0x%x\n", policy->EnhancedPrefetchScheduleAccelerationFinal);
	dml_print("DML: Policy: NomDETInKByteOverrideEnable = 0x%x\n", policy->NomDETInKByteOverrideEnable);
	dml_print("DML: Policy: NomDETInKByteOverrideValue = 0x%x\n", policy->NomDETInKByteOverrideValue);
	dml_print("DML: Policy: DCCProgrammingAssumesScanDirectionUnknownFinal = 0x%x\n", policy->DCCProgrammingAssumesScanDirectionUnknownFinal);
	dml_print("DML: Policy: SynchronizeTimingsFinal = 0x%x\n", policy->SynchronizeTimingsFinal);
	dml_print("DML: Policy: SynchronizeDRRDisplaysForUCLKPStateChangeFinal = 0x%x\n", policy->SynchronizeDRRDisplaysForUCLKPStateChangeFinal);
	dml_print("DML: Policy: AssumeModeSupportAtMaxPwrStateEvenDRAMClockChangeNotSupported = 0x%x\n", policy->AssumeModeSupportAtMaxPwrStateEvenDRAMClockChangeNotSupported);
	dml_print("DML: Policy: AssumeModeSupportAtMaxPwrStateEvenFClockChangeNotSupported = 0x%x\n", policy->AssumeModeSupportAtMaxPwrStateEvenFClockChangeNotSupported);

	for (dml_uint_t i = 0; i < DCN_DML__NUM_PLANE; i++) {
		dml_print("DML: i=%0d, Policy: MPCCombineUse = 0x%x\n", i, policy->MPCCombineUse[i]);
		dml_print("DML: i=%0d, Policy: ODMUse = 0x%x\n", i, policy->ODMUse[i]);
		dml_print("DML: i=%0d, Policy: ImmediateFlipRequirement = 0x%x\n", i, policy->ImmediateFlipRequirement[i]);
		dml_print("DML: i=%0d, Policy: AllowForPStateChangeOrStutterInVBlank = 0x%x\n", i, policy->AllowForPStateChangeOrStutterInVBlank[i]);
	}
	dml_print("DML: ===================================== \n");
}

void dml_print_mode_support(struct display_mode_lib_st *mode_lib, dml_uint_t j)
{
	dml_print("DML: MODE SUPPORT: ===============================================\n");
	dml_print("DML: MODE SUPPORT: Voltage State %d\n", j);
	dml_print("DML: MODE SUPPORT:     Mode Supported              : %s\n", mode_lib->ms.support.ModeSupport[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Scale Ratio And Taps                : %s\n", mode_lib->ms.support.ScaleRatioAndTapsSupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Source Format Pixel And Scan        : %s\n", mode_lib->ms.support.SourceFormatPixelAndScanSupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Viewport Size                       : %s\n", mode_lib->ms.support.ViewportSizeSupport[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Link Rate Does Not Match DP Version        : %s\n", mode_lib->ms.support.LinkRateDoesNotMatchDPVersion == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Link Rate For Multistream Not Indicated    : %s\n", mode_lib->ms.support.LinkRateForMultistreamNotIndicated == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     BPP For Multi stream Not Indicated         : %s\n", mode_lib->ms.support.BPPForMultistreamNotIndicated == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Multistream With HDMI Or eDP               : %s\n", mode_lib->ms.support.MultistreamWithHDMIOreDP == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Exceeded Multistream Slots                 : %s\n", mode_lib->ms.support.ExceededMultistreamSlots == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     MSO Or ODM Split With Non DP Link          : %s\n", mode_lib->ms.support.MSOOrODMSplitWithNonDPLink == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Not Enough Lanes For MSO                   : %s\n", mode_lib->ms.support.NotEnoughLanesForMSO == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     LinkCapacitySupport                        : %s\n", mode_lib->ms.support.LinkCapacitySupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     P2IWith420                                 : %s\n", mode_lib->ms.support.P2IWith420 == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     DSCOnlyIfNecessaryWithBPP                  : %s\n", mode_lib->ms.support.DSCOnlyIfNecessaryWithBPP == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     DSC422NativeNotSupported                   : %s\n", mode_lib->ms.support.DSC422NativeNotSupported == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     MPCCombineMethodIncompatible               : %s\n", mode_lib->ms.support.MPCCombineMethodIncompatible == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     ODMCombineTwoToOneSupportCheckOK           : %s\n", mode_lib->ms.support.ODMCombineTwoToOneSupportCheckOK == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     ODMCombineFourToOneSupportCheckOK          : %s\n", mode_lib->ms.support.ODMCombineFourToOneSupportCheckOK == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     NotEnoughDSCUnits                          : %s\n", mode_lib->ms.support.NotEnoughDSCUnits == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     NotEnoughDSCSlices                         : %s\n", mode_lib->ms.support.NotEnoughDSCSlices == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe : %s\n", mode_lib->ms.support.ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     InvalidCombinationOfMALLUseForPStateAndStaticScreen          : %s\n", mode_lib->ms.support.InvalidCombinationOfMALLUseForPStateAndStaticScreen == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     DSCCLKRequiredMoreThanSupported            : %s\n", mode_lib->ms.support.DSCCLKRequiredMoreThanSupported == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     PixelsPerLinePerDSCUnitSupport             : %s\n", mode_lib->ms.support.PixelsPerLinePerDSCUnitSupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     DTBCLKRequiredMoreThanSupported            : %s\n", mode_lib->ms.support.DTBCLKRequiredMoreThanSupported == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     InvalidCombinationOfMALLUseForPState       : %s\n", mode_lib->ms.support.InvalidCombinationOfMALLUseForPState == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified : %s\n", mode_lib->ms.support.ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     ROB Support                                : %s\n", mode_lib->ms.support.ROBSupport[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     DISPCLK DPPCLK Support                     : %s\n", mode_lib->ms.support.DISPCLK_DPPCLK_Support[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Total Available Pipes Support              : %s\n", mode_lib->ms.support.TotalAvailablePipesSupport[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Number Of OTG Support                      : %s\n", mode_lib->ms.support.NumberOfOTGSupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Number Of DP2p0 Support                    : %s\n", mode_lib->ms.support.NumberOfDP2p0Support == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Writeback Latency Support                  : %s\n", mode_lib->ms.support.WritebackLatencySupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Writeback Scale Ratio And Taps Support     : %s\n", mode_lib->ms.support.WritebackScaleRatioAndTapsSupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Cursor Support                             : %s\n", mode_lib->ms.support.CursorSupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Pitch Support                              : %s\n", mode_lib->ms.support.PitchSupport == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Viewport Exceeds Surface                   : %s\n", mode_lib->ms.support.ViewportExceedsSurface == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Prefetch Supported                         : %s\n", mode_lib->ms.support.PrefetchSupported[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     VActive Bandwith Support                   : %s\n", mode_lib->ms.support.VActiveBandwithSupport[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Dynamic Metadata Supported                 : %s\n", mode_lib->ms.support.DynamicMetadataSupported[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Total Vertical Active Bandwidth Support    : %s\n", mode_lib->ms.support.TotalVerticalActiveBandwidthSupport[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     VRatio In Prefetch Supported               : %s\n", mode_lib->ms.support.VRatioInPrefetchSupported[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     PTE Buffer Size Not Exceeded               : %s\n", mode_lib->ms.support.PTEBufferSizeNotExceeded[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     DCC Meta Buffer Size Not Exceeded          : %s\n", mode_lib->ms.support.DCCMetaBufferSizeNotExceeded[j] == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Non supported DSC Input BPC                : %s\n", mode_lib->ms.support.NonsupportedDSCInputBPC == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Exceeded MALL Size                         : %s\n", mode_lib->ms.support.ExceededMALLSize == false ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     Host VM or Immediate Flip Supported        : %s\n", ((mode_lib->ms.cache_display_cfg.plane.HostVMEnable == false && !mode_lib->scratch.dml_core_mode_support_locals.ImmediateFlipRequiredFinal) || mode_lib->ms.support.ImmediateFlipSupportedForState[j]) ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     dram clock change support                  : %s\n", mode_lib->scratch.dml_core_mode_support_locals.dram_clock_change_support == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     f_clock change support                     : %s\n", mode_lib->scratch.dml_core_mode_support_locals.f_clock_change_support == true ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT:     USR Retraining Support                     : %s\n", (!mode_lib->ms.policy.USRRetrainingRequiredFinal || &mode_lib->ms.support.USRRetrainingSupport[j]) ? "Supported" : "NOT Supported");
	dml_print("DML: MODE SUPPORT: ===============================================\n");
}

void dml_print_dml_mode_support_info(const struct dml_mode_support_info_st *support, dml_bool_t fail_only)
{
	dml_print("DML: ===================================== \n");
	dml_print("DML: DML_MODE_SUPPORT_INFO_ST\n");
	if (!fail_only || support->ModeIsSupported == 0)
		dml_print("DML: support: ModeIsSupported = 0x%x\n", support->ModeIsSupported);
	if (!fail_only || support->ImmediateFlipSupport == 0)
		dml_print("DML: support: ImmediateFlipSupport = 0x%x\n", support->ImmediateFlipSupport);
	if (!fail_only || support->WritebackLatencySupport == 0)
		dml_print("DML: support: WritebackLatencySupport = 0x%x\n", support->WritebackLatencySupport);
	if (!fail_only || support->ScaleRatioAndTapsSupport == 0)
		dml_print("DML: support: ScaleRatioAndTapsSupport = 0x%x\n", support->ScaleRatioAndTapsSupport);
	if (!fail_only || support->SourceFormatPixelAndScanSupport == 0)
		dml_print("DML: support: SourceFormatPixelAndScanSupport = 0x%x\n", support->SourceFormatPixelAndScanSupport);
	if (!fail_only || support->MPCCombineMethodIncompatible == 1)
		dml_print("DML: support: MPCCombineMethodIncompatible = 0x%x\n", support->MPCCombineMethodIncompatible);
	if (!fail_only || support->P2IWith420 == 1)
		dml_print("DML: support: P2IWith420 = 0x%x\n", support->P2IWith420);
	if (!fail_only || support->DSCOnlyIfNecessaryWithBPP == 1)
		dml_print("DML: support: DSCOnlyIfNecessaryWithBPP = 0x%x\n", support->DSCOnlyIfNecessaryWithBPP);
	if (!fail_only || support->DSC422NativeNotSupported == 1)
		dml_print("DML: support: DSC422NativeNotSupported = 0x%x\n", support->DSC422NativeNotSupported);
	if (!fail_only || support->LinkRateDoesNotMatchDPVersion == 1)
		dml_print("DML: support: LinkRateDoesNotMatchDPVersion = 0x%x\n", support->LinkRateDoesNotMatchDPVersion);
	if (!fail_only || support->LinkRateForMultistreamNotIndicated == 1)
		dml_print("DML: support: LinkRateForMultistreamNotIndicated = 0x%x\n", support->LinkRateForMultistreamNotIndicated);
	if (!fail_only || support->BPPForMultistreamNotIndicated == 1)
		dml_print("DML: support: BPPForMultistreamNotIndicated = 0x%x\n", support->BPPForMultistreamNotIndicated);
	if (!fail_only || support->MultistreamWithHDMIOreDP == 1)
		dml_print("DML: support: MultistreamWithHDMIOreDP = 0x%x\n", support->MultistreamWithHDMIOreDP);
	if (!fail_only || support->MSOOrODMSplitWithNonDPLink == 1)
		dml_print("DML: support: MSOOrODMSplitWithNonDPLink = 0x%x\n", support->MSOOrODMSplitWithNonDPLink);
	if (!fail_only || support->NotEnoughLanesForMSO == 1)
		dml_print("DML: support: NotEnoughLanesForMSO = 0x%x\n", support->NotEnoughLanesForMSO);
	if (!fail_only || support->NumberOfOTGSupport == 0)
		dml_print("DML: support: NumberOfOTGSupport = 0x%x\n", support->NumberOfOTGSupport);
	if (!fail_only || support->NumberOfDP2p0Support == 0)
		dml_print("DML: support: NumberOfDP2p0Support = 0x%x\n", support->NumberOfDP2p0Support);
	if (!fail_only || support->NonsupportedDSCInputBPC == 1)
		dml_print("DML: support: NonsupportedDSCInputBPC = 0x%x\n", support->NonsupportedDSCInputBPC);
	if (!fail_only || support->WritebackScaleRatioAndTapsSupport == 0)
		dml_print("DML: support: WritebackScaleRatioAndTapsSupport = 0x%x\n", support->WritebackScaleRatioAndTapsSupport);
	if (!fail_only || support->CursorSupport == 0)
		dml_print("DML: support: CursorSupport = 0x%x\n", support->CursorSupport);
	if (!fail_only || support->PitchSupport == 0)
		dml_print("DML: support: PitchSupport = 0x%x\n", support->PitchSupport);
	if (!fail_only || support->ViewportExceedsSurface == 1)
		dml_print("DML: support: ViewportExceedsSurface = 0x%x\n", support->ViewportExceedsSurface);
	if (!fail_only || support->ExceededMALLSize == 1)
		dml_print("DML: support: ExceededMALLSize = 0x%x\n", support->ExceededMALLSize);
	if (!fail_only || support->EnoughWritebackUnits == 0)
		dml_print("DML: support: EnoughWritebackUnits = 0x%x\n", support->EnoughWritebackUnits);
	if (!fail_only || support->ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified == 1)
		dml_print("DML: support: ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified = 0x%x\n", support->ImmediateFlipRequiredButTheRequirementForEachSurfaceIsNotSpecified);
	if (!fail_only || support->ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe == 1)
		dml_print("DML: support: ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe = 0x%x\n", support->ImmediateFlipOrHostVMAndPStateWithMALLFullFrameOrPhantomPipe);
	if (!fail_only || support->InvalidCombinationOfMALLUseForPStateAndStaticScreen == 1)
		dml_print("DML: support: InvalidCombinationOfMALLUseForPStateAndStaticScreen = 0x%x\n", support->InvalidCombinationOfMALLUseForPStateAndStaticScreen);
	if (!fail_only || support->InvalidCombinationOfMALLUseForPState == 1)
		dml_print("DML: support: InvalidCombinationOfMALLUseForPState = 0x%x\n", support->InvalidCombinationOfMALLUseForPState);

	if (!fail_only || support->ExceededMultistreamSlots == 1)
		dml_print("DML: support: ExceededMultistreamSlots = 0x%x\n", support->ExceededMultistreamSlots);
	if (!fail_only || support->ODMCombineTwoToOneSupportCheckOK == 0)
		dml_print("DML: support: ODMCombineTwoToOneSupportCheckOK = 0x%x\n", support->ODMCombineTwoToOneSupportCheckOK);
	if (!fail_only || support->ODMCombineFourToOneSupportCheckOK == 0)
		dml_print("DML: support: ODMCombineFourToOneSupportCheckOK = 0x%x\n", support->ODMCombineFourToOneSupportCheckOK);
	if (!fail_only || support->NotEnoughDSCUnits == 1)
		dml_print("DML: support: NotEnoughDSCUnits = 0x%x\n", support->NotEnoughDSCUnits);
	if (!fail_only || support->NotEnoughDSCSlices == 1)
		dml_print("DML: support: NotEnoughDSCSlices = 0x%x\n", support->NotEnoughDSCSlices);
	if (!fail_only || support->PixelsPerLinePerDSCUnitSupport == 0)
		dml_print("DML: support: PixelsPerLinePerDSCUnitSupport = 0x%x\n", support->PixelsPerLinePerDSCUnitSupport);
	if (!fail_only || support->DSCCLKRequiredMoreThanSupported == 1)
		dml_print("DML: support: DSCCLKRequiredMoreThanSupported = 0x%x\n", support->DSCCLKRequiredMoreThanSupported);
	if (!fail_only || support->DTBCLKRequiredMoreThanSupported == 1)
		dml_print("DML: support: DTBCLKRequiredMoreThanSupported = 0x%x\n", support->DTBCLKRequiredMoreThanSupported);
	if (!fail_only || support->LinkCapacitySupport == 0)
		dml_print("DML: support: LinkCapacitySupport = 0x%x\n", support->LinkCapacitySupport);

	for (dml_uint_t j = 0; j < 2; j++) {
		if (!fail_only || support->DRAMClockChangeSupport[j] == dml_dram_clock_change_unsupported)
			dml_print("DML: support: combine=%d, DRAMClockChangeSupport = %d\n", j, support->DRAMClockChangeSupport[j]);
		if (!fail_only || support->FCLKChangeSupport[j] == dml_fclock_change_unsupported)
			dml_print("DML: support: combine=%d, FCLKChangeSupport = %d\n", j, support->FCLKChangeSupport[j]);
		if (!fail_only || support->ROBSupport[j] == 0)
			dml_print("DML: support: combine=%d, ROBSupport = %d\n", j, support->ROBSupport[j]);
		if (!fail_only || support->PTEBufferSizeNotExceeded[j] == 0)
			dml_print("DML: support: combine=%d, PTEBufferSizeNotExceeded = %d\n", j, support->PTEBufferSizeNotExceeded[j]);
		if (!fail_only || support->DCCMetaBufferSizeNotExceeded[j] == 0)
			dml_print("DML: support: combine=%d, DCCMetaBufferSizeNotExceeded = %d\n", j, support->DCCMetaBufferSizeNotExceeded[j]);
		if (!fail_only || support->TotalVerticalActiveBandwidthSupport[j] == 0)
			dml_print("DML: support: combine=%d, TotalVerticalActiveBandwidthSupport = %d\n", j, support->TotalVerticalActiveBandwidthSupport[j]);
		if (!fail_only || support->USRRetrainingSupport[j] == 0)
			dml_print("DML: support: combine=%d, USRRetrainingSupport = %d\n", j, support->USRRetrainingSupport[j]);
		if (!fail_only || support->VActiveBandwithSupport[j] == 0)
			dml_print("DML: support: combine=%d, VActiveBandwithSupport = %d\n", j, support->VActiveBandwithSupport[j]);
		if (!fail_only || support->PrefetchSupported[j] == 0)
			dml_print("DML: support: combine=%d, PrefetchSupported = %d\n", j, support->PrefetchSupported[j]);
		if (!fail_only || support->DynamicMetadataSupported[j] == 0)
			dml_print("DML: support: combine=%d, DynamicMetadataSupported = %d\n", j, support->DynamicMetadataSupported[j]);
		if (!fail_only || support->VRatioInPrefetchSupported[j] == 0)
			dml_print("DML: support: combine=%d, VRatioInPrefetchSupported = %d\n", j, support->VRatioInPrefetchSupported[j]);
		if (!fail_only || support->DISPCLK_DPPCLK_Support[j] == 0)
			dml_print("DML: support: combine=%d, DISPCLK_DPPCLK_Support = %d\n", j, support->DISPCLK_DPPCLK_Support[j]);
		if (!fail_only || support->TotalAvailablePipesSupport[j] == 0)
			dml_print("DML: support: combine=%d, TotalAvailablePipesSupport = %d\n", j, support->TotalAvailablePipesSupport[j]);
		if (!fail_only || support->ModeSupport[j] == 0)
			dml_print("DML: support: combine=%d, ModeSupport = %d\n", j, support->ModeSupport[j]);
		if (!fail_only || support->ViewportSizeSupport[j] == 0)
			dml_print("DML: support: combine=%d, ViewportSizeSupport = %d\n", j, support->ViewportSizeSupport[j]);
		if (!fail_only || support->ImmediateFlipSupportedForState[j] == 0)
			dml_print("DML: support: combine=%d, ImmediateFlipSupportedForState = %d\n", j, support->ImmediateFlipSupportedForState[j]);
	}
}

void dml_print_dml_display_cfg_timing(const struct dml_timing_cfg_st *timing, dml_uint_t num_plane)
{
	for (dml_uint_t i = 0; i < num_plane; i++) {
		dml_print("DML: timing_cfg: plane=%d, HTotal = %d\n", i, timing->HTotal[i]);
		dml_print("DML: timing_cfg: plane=%d, VTotal = %d\n", i, timing->VTotal[i]);
		dml_print("DML: timing_cfg: plane=%d, HActive = %d\n", i, timing->HActive[i]);
		dml_print("DML: timing_cfg: plane=%d, VActive = %d\n", i, timing->VActive[i]);
		dml_print("DML: timing_cfg: plane=%d, VFrontPorch = %d\n", i, timing->VFrontPorch[i]);
		dml_print("DML: timing_cfg: plane=%d, VBlankNom = %d\n", i, timing->VBlankNom[i]);
		dml_print("DML: timing_cfg: plane=%d, RefreshRate = %d\n", i, timing->RefreshRate[i]);
		dml_print("DML: timing_cfg: plane=%d, PixelClock = %f\n", i, timing->PixelClock[i]);
		dml_print("DML: timing_cfg: plane=%d, Interlace = %d\n", i, timing->Interlace[i]);
		dml_print("DML: timing_cfg: plane=%d, DRRDisplay = %d\n", i, timing->DRRDisplay[i]);
	}
}

void dml_print_dml_display_cfg_plane(const struct dml_plane_cfg_st *plane, dml_uint_t num_plane)
{
	dml_print("DML: plane_cfg: num_plane = %d\n", num_plane);
	dml_print("DML: plane_cfg: GPUVMEnable = %d\n", plane->GPUVMEnable);
	dml_print("DML: plane_cfg: HostVMEnable = %d\n", plane->HostVMEnable);
	dml_print("DML: plane_cfg: GPUVMMaxPageTableLevels = %d\n", plane->GPUVMMaxPageTableLevels);
	dml_print("DML: plane_cfg: HostVMMaxPageTableLevels = %d\n", plane->HostVMMaxPageTableLevels);

	for (dml_uint_t i = 0; i < num_plane; i++) {
		dml_print("DML: plane_cfg: plane=%d, GPUVMMinPageSizeKBytes = %d\n", i, plane->GPUVMMinPageSizeKBytes[i]);
		dml_print("DML: plane_cfg: plane=%d, ForceOneRowForFrame = %d\n", i, plane->ForceOneRowForFrame[i]);
		dml_print("DML: plane_cfg: plane=%d, PTEBufferModeOverrideEn = %d\n", i, plane->PTEBufferModeOverrideEn[i]);
		dml_print("DML: plane_cfg: plane=%d, PTEBufferMode = %d\n", i, plane->PTEBufferMode[i]);
		dml_print("DML: plane_cfg: plane=%d, DETSizeOverride = %d\n", i, plane->DETSizeOverride[i]);
		dml_print("DML: plane_cfg: plane=%d, UseMALLForStaticScreen = %d\n", i, plane->UseMALLForStaticScreen[i]);
		dml_print("DML: plane_cfg: plane=%d, UseMALLForPStateChange = %d\n", i, plane->UseMALLForPStateChange[i]);
		dml_print("DML: plane_cfg: plane=%d, BlendingAndTiming = %d\n", i, plane->BlendingAndTiming[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportWidth = %d\n", i, plane->ViewportWidth[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportHeight = %d\n", i, plane->ViewportHeight[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportWidthChroma = %d\n", i, plane->ViewportWidthChroma[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportHeightChroma = %d\n", i, plane->ViewportHeightChroma[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportXStart = %d\n", i, plane->ViewportXStart[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportXStartC = %d\n", i, plane->ViewportXStartC[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportYStart = %d\n", i, plane->ViewportYStart[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportYStartC = %d\n", i, plane->ViewportYStartC[i]);
		dml_print("DML: plane_cfg: plane=%d, ViewportStationary = %d\n", i, plane->ViewportStationary[i]);
		dml_print("DML: plane_cfg: plane=%d, ScalerEnabled = %d\n", i, plane->ScalerEnabled[i]);
		dml_print("DML: plane_cfg: plane=%d, HRatio = %3.2f\n", i, plane->HRatio[i]);
		dml_print("DML: plane_cfg: plane=%d, VRatio = %3.2f\n", i, plane->VRatio[i]);
		dml_print("DML: plane_cfg: plane=%d, HRatioChroma = %3.2f\n", i, plane->HRatioChroma[i]);
		dml_print("DML: plane_cfg: plane=%d, VRatioChroma = %3.2f\n", i, plane->VRatioChroma[i]);
		dml_print("DML: plane_cfg: plane=%d, HTaps = %d\n", i, plane->HTaps[i]);
		dml_print("DML: plane_cfg: plane=%d, VTaps = %d\n", i, plane->VTaps[i]);
		dml_print("DML: plane_cfg: plane=%d, HTapsChroma = %d\n", i, plane->HTapsChroma[i]);
		dml_print("DML: plane_cfg: plane=%d, VTapsChroma = %d\n", i, plane->VTapsChroma[i]);
		dml_print("DML: plane_cfg: plane=%d, LBBitPerPixel = %d\n", i, plane->LBBitPerPixel[i]);
		dml_print("DML: plane_cfg: plane=%d, SourceScan = %d\n", i, plane->SourceScan[i]);
		dml_print("DML: plane_cfg: plane=%d, ScalerRecoutWidth = %d\n", i, plane->ScalerRecoutWidth[i]);
		dml_print("DML: plane_cfg: plane=%d, NumberOfCursors = %d\n", i, plane->NumberOfCursors[i]);
		dml_print("DML: plane_cfg: plane=%d, CursorWidth = %d\n", i, plane->CursorWidth[i]);
		dml_print("DML: plane_cfg: plane=%d, CursorBPP = %d\n", i, plane->CursorBPP[i]);

		dml_print("DML: plane_cfg: plane=%d, DynamicMetadataEnable = %d\n", i, plane->DynamicMetadataEnable[i]);
		dml_print("DML: plane_cfg: plane=%d, DynamicMetadataLinesBeforeActiveRequired = %d\n", i, plane->DynamicMetadataLinesBeforeActiveRequired[i]);
		dml_print("DML: plane_cfg: plane=%d, DynamicMetadataTransmittedBytes = %d\n", i, plane->DynamicMetadataTransmittedBytes[i]);
	}
}

void dml_print_dml_display_cfg_surface(const struct dml_surface_cfg_st *surface, dml_uint_t num_plane)
{
	for (dml_uint_t i = 0; i < num_plane; i++) {
		dml_print("DML: surface_cfg: plane=%d, PitchY = %d\n", i, surface->PitchY[i]);
		dml_print("DML: surface_cfg: plane=%d, SurfaceWidthY = %d\n", i, surface->SurfaceWidthY[i]);
		dml_print("DML: surface_cfg: plane=%d, SurfaceHeightY = %d\n", i, surface->SurfaceHeightY[i]);
		dml_print("DML: surface_cfg: plane=%d, PitchC = %d\n", i, surface->PitchC[i]);
		dml_print("DML: surface_cfg: plane=%d, SurfaceWidthC = %d\n", i, surface->SurfaceWidthC[i]);
		dml_print("DML: surface_cfg: plane=%d, SurfaceHeightC = %d\n", i, surface->SurfaceHeightC[i]);
		dml_print("DML: surface_cfg: plane=%d, DCCEnable = %d\n", i, surface->DCCEnable[i]);
		dml_print("DML: surface_cfg: plane=%d, DCCMetaPitchY = %d\n", i, surface->DCCMetaPitchY[i]);
		dml_print("DML: surface_cfg: plane=%d, DCCMetaPitchC = %d\n", i, surface->DCCMetaPitchC[i]);
		dml_print("DML: surface_cfg: plane=%d, DCCRateLuma = %f\n", i, surface->DCCRateLuma[i]);
		dml_print("DML: surface_cfg: plane=%d, DCCRateChroma = %f\n", i, surface->DCCRateChroma[i]);
		dml_print("DML: surface_cfg: plane=%d, DCCFractionOfZeroSizeRequestsLuma = %f\n", i, surface->DCCFractionOfZeroSizeRequestsLuma[i]);
		dml_print("DML: surface_cfg: plane=%d, DCCFractionOfZeroSizeRequestsChroma= %f\n", i, surface->DCCFractionOfZeroSizeRequestsChroma[i]);
	}
}

void dml_print_dml_display_cfg_hw_resource(const struct dml_hw_resource_st *hw, dml_uint_t num_plane)
{
	for (dml_uint_t i = 0; i < num_plane; i++) {
		dml_print("DML: hw_resource: plane=%d, ODMMode = %d\n", i, hw->ODMMode[i]);
		dml_print("DML: hw_resource: plane=%d, DPPPerSurface = %d\n", i, hw->DPPPerSurface[i]);
		dml_print("DML: hw_resource: plane=%d, DSCEnabled = %d\n", i, hw->DSCEnabled[i]);
		dml_print("DML: hw_resource: plane=%d, NumberOfDSCSlices = %d\n", i, hw->NumberOfDSCSlices[i]);
	}
	dml_print("DML: hw_resource: DLGRefClkFreqMHz   = %f\n", hw->DLGRefClkFreqMHz);
}

__DML_DLL_EXPORT__ void dml_print_soc_state_bounding_box(const struct soc_state_bounding_box_st *state)
{
	dml_print("DML: state_bbox: socclk_mhz = %f\n", state->socclk_mhz);
	dml_print("DML: state_bbox: dscclk_mhz = %f\n", state->dscclk_mhz);
	dml_print("DML: state_bbox: phyclk_mhz = %f\n", state->phyclk_mhz);
	dml_print("DML: state_bbox: phyclk_d18_mhz = %f\n", state->phyclk_d18_mhz);
	dml_print("DML: state_bbox: phyclk_d32_mhz = %f\n", state->phyclk_d32_mhz);
	dml_print("DML: state_bbox: dtbclk_mhz = %f\n", state->dtbclk_mhz);
	dml_print("DML: state_bbox: dispclk_mhz = %f\n", state->dispclk_mhz);
	dml_print("DML: state_bbox: dppclk_mhz = %f\n", state->dppclk_mhz);
	dml_print("DML: state_bbox: fabricclk_mhz = %f\n", state->fabricclk_mhz);
	dml_print("DML: state_bbox: dcfclk_mhz = %f\n", state->dcfclk_mhz);
	dml_print("DML: state_bbox: dram_speed_mts = %f\n", state->dram_speed_mts);
	dml_print("DML: state_bbox: urgent_latency_pixel_data_only_us = %f\n", state->urgent_latency_pixel_data_only_us);
	dml_print("DML: state_bbox: urgent_latency_pixel_mixed_with_vm_data_us = %f\n", state->urgent_latency_pixel_mixed_with_vm_data_us);
	dml_print("DML: state_bbox: urgent_latency_vm_data_only_us = %f\n", state->urgent_latency_vm_data_only_us);
	dml_print("DML: state_bbox: writeback_latency_us = %f\n", state->writeback_latency_us);
	dml_print("DML: state_bbox: urgent_latency_adjustment_fabric_clock_component_us = %f\n", state->urgent_latency_adjustment_fabric_clock_component_us);
	dml_print("DML: state_bbox: urgent_latency_adjustment_fabric_clock_reference_mhz= %f\n", state->urgent_latency_adjustment_fabric_clock_reference_mhz);
	dml_print("DML: state_bbox: sr_exit_time_us = %f\n", state->sr_exit_time_us);
	dml_print("DML: state_bbox: sr_enter_plus_exit_time_us = %f\n", state->sr_enter_plus_exit_time_us);
	dml_print("DML: state_bbox: sr_exit_z8_time_us = %f\n", state->sr_exit_z8_time_us);
	dml_print("DML: state_bbox: sr_enter_plus_exit_z8_time_us = %f\n", state->sr_enter_plus_exit_z8_time_us);
	dml_print("DML: state_bbox: dram_clock_change_latency_us = %f\n", state->dram_clock_change_latency_us);
	dml_print("DML: state_bbox: fclk_change_latency_us = %f\n", state->fclk_change_latency_us);
	dml_print("DML: state_bbox: usr_retraining_latency_us = %f\n", state->usr_retraining_latency_us);
	dml_print("DML: state_bbox: use_ideal_dram_bw_strobe = %d\n", state->use_ideal_dram_bw_strobe);
}

__DML_DLL_EXPORT__ void dml_print_soc_bounding_box(const struct soc_bounding_box_st *soc)
{
	dml_print("DML: soc_bbox: dprefclk_mhz = %f\n", soc->dprefclk_mhz);
	dml_print("DML: soc_bbox: xtalclk_mhz = %f\n", soc->xtalclk_mhz);
	dml_print("DML: soc_bbox: pcierefclk_mhz = %f\n", soc->pcierefclk_mhz);
	dml_print("DML: soc_bbox: refclk_mhz = %f\n", soc->refclk_mhz);
	dml_print("DML: soc_bbox: amclk_mhz = %f\n", soc->amclk_mhz);

	dml_print("DML: soc_bbox: max_outstanding_reqs = %f\n", soc->max_outstanding_reqs);
	dml_print("DML: soc_bbox: pct_ideal_sdp_bw_after_urgent = %f\n", soc->pct_ideal_sdp_bw_after_urgent);
	dml_print("DML: soc_bbox: pct_ideal_fabric_bw_after_urgent = %f\n", soc->pct_ideal_fabric_bw_after_urgent);
	dml_print("DML: soc_bbox: pct_ideal_dram_bw_after_urgent_pixel_only = %f\n", soc->pct_ideal_dram_bw_after_urgent_pixel_only);
	dml_print("DML: soc_bbox: pct_ideal_dram_bw_after_urgent_pixel_and_vm = %f\n", soc->pct_ideal_dram_bw_after_urgent_pixel_and_vm);
	dml_print("DML: soc_bbox: pct_ideal_dram_bw_after_urgent_vm_only = %f\n", soc->pct_ideal_dram_bw_after_urgent_vm_only);
	dml_print("DML: soc_bbox: pct_ideal_dram_bw_after_urgent_strobe = %f\n", soc->pct_ideal_dram_bw_after_urgent_strobe);
	dml_print("DML: soc_bbox: max_avg_sdp_bw_use_normal_percent = %f\n", soc->max_avg_sdp_bw_use_normal_percent);
	dml_print("DML: soc_bbox: max_avg_fabric_bw_use_normal_percent = %f\n", soc->max_avg_fabric_bw_use_normal_percent);
	dml_print("DML: soc_bbox: max_avg_dram_bw_use_normal_percent = %f\n", soc->max_avg_dram_bw_use_normal_percent);
	dml_print("DML: soc_bbox: max_avg_dram_bw_use_normal_strobe_percent = %f\n", soc->max_avg_dram_bw_use_normal_strobe_percent);
	dml_print("DML: soc_bbox: round_trip_ping_latency_dcfclk_cycles = %d\n", soc->round_trip_ping_latency_dcfclk_cycles);
	dml_print("DML: soc_bbox: urgent_out_of_order_return_per_channel_pixel_only_bytes = %d\n", soc->urgent_out_of_order_return_per_channel_pixel_only_bytes);
	dml_print("DML: soc_bbox: urgent_out_of_order_return_per_channel_pixel_and_vm_bytes = %d\n", soc->urgent_out_of_order_return_per_channel_pixel_and_vm_bytes);
	dml_print("DML: soc_bbox: urgent_out_of_order_return_per_channel_vm_only_bytes = %d\n", soc->urgent_out_of_order_return_per_channel_vm_only_bytes);
	dml_print("DML: soc_bbox: num_chans = %d\n", soc->num_chans);
	dml_print("DML: soc_bbox: return_bus_width_bytes = %d\n", soc->return_bus_width_bytes);
	dml_print("DML: soc_bbox: dram_channel_width_bytes = %d\n", soc->dram_channel_width_bytes);
	dml_print("DML: soc_bbox: fabric_datapath_to_dcn_data_return_bytes = %d\n", soc->fabric_datapath_to_dcn_data_return_bytes);
	dml_print("DML: soc_bbox: hostvm_min_page_size_kbytes = %d\n", soc->hostvm_min_page_size_kbytes);
	dml_print("DML: soc_bbox: gpuvm_min_page_size_kbytes = %d\n", soc->gpuvm_min_page_size_kbytes);
	dml_print("DML: soc_bbox: phy_downspread_percent = %f\n", soc->phy_downspread_percent);
	dml_print("DML: soc_bbox: dcn_downspread_percent = %f\n", soc->dcn_downspread_percent);
	dml_print("DML: soc_bbox: smn_latency_us = %f\n", soc->smn_latency_us);
	dml_print("DML: soc_bbox: mall_allocated_for_dcn_mbytes = %d\n", soc->mall_allocated_for_dcn_mbytes);
	dml_print("DML: soc_bbox: dispclk_dppclk_vco_speed_mhz = %f\n", soc->dispclk_dppclk_vco_speed_mhz);
	dml_print("DML: soc_bbox: do_urgent_latency_adjustment = %d\n", soc->do_urgent_latency_adjustment);
}

__DML_DLL_EXPORT__ void dml_print_clk_cfg(const struct dml_clk_cfg_st *clk_cfg)
{
	dml_print("DML: clk_cfg: 0-use_required, 1-use pipe.clks_cfg, 2-use state bbox\n");
	dml_print("DML: clk_cfg: dcfclk_option = %d\n", clk_cfg->dcfclk_option);
	dml_print("DML: clk_cfg: dispclk_option = %d\n", clk_cfg->dispclk_option);

	dml_print("DML: clk_cfg: dcfclk_freq_mhz = %f\n", clk_cfg->dcfclk_freq_mhz);
	dml_print("DML: clk_cfg: dispclk_freq_mhz = %f\n", clk_cfg->dispclk_freq_mhz);

	for (dml_uint_t i = 0; i < DCN_DML__NUM_PLANE; i++) {
		dml_print("DML: clk_cfg: i=%d, dppclk_option = %d\n", i, clk_cfg->dppclk_option[i]);
		dml_print("DML: clk_cfg: i=%d, dppclk_freq_mhz = %f\n", i, clk_cfg->dppclk_freq_mhz[i]);
	}
}

dml_bool_t dml_is_vertical_rotation(enum dml_rotation_angle Scan)
{
	dml_bool_t is_vert = false;
	if (Scan == dml_rotation_90 || Scan == dml_rotation_90m || Scan == dml_rotation_270 || Scan == dml_rotation_270m) {
		is_vert = true;
	} else {
		is_vert = false;
	}
	return is_vert;
} // dml_is_vertical_rotation

dml_uint_t dml_get_cursor_bit_per_pixel(enum dml_cursor_bpp ebpp)
{
	switch (ebpp) {
	case dml_cur_2bit:
		return 2;
	case dml_cur_32bit:
		return 32;
	case dml_cur_64bit:
		return 64;
	default:
		return 0;
	}
}

/// @brief Determine the physical pipe to logical plane mapping using the display_cfg
dml_uint_t dml_get_num_active_planes(const struct dml_display_cfg_st *display_cfg)
{
	dml_uint_t num_active_planes = 0;

	for (dml_uint_t k = 0; k < __DML_NUM_PLANES__; k++) {
		if (display_cfg->plane.ViewportWidth[k] > 0)
		num_active_planes = num_active_planes + 1;
	}
#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: num_active_planes = %d\n", __func__, num_active_planes);
#endif
	return num_active_planes;
}

/// @brief Determine the physical pipe to logical plane mapping using the display_cfg
dml_uint_t dml_get_num_active_pipes(const struct dml_display_cfg_st *display_cfg)
{
	dml_uint_t num_active_pipes = 0;

	for (dml_uint_t j = 0; j < dml_get_num_active_planes(display_cfg); j++) {
		num_active_pipes = num_active_pipes + display_cfg->hw.DPPPerSurface[j];
	}

#ifdef __DML_VBA_DEBUG__
	dml_print("DML::%s: num_active_pipes    = %d\n", __func__, num_active_pipes);
#endif
	return num_active_pipes;
}

dml_uint_t dml_get_plane_idx(const struct display_mode_lib_st *mode_lib, dml_uint_t pipe_idx)
{
	dml_uint_t plane_idx = mode_lib->mp.pipe_plane[pipe_idx];
	return plane_idx;
}

dml_uint_t dml_get_pipe_idx(const struct display_mode_lib_st *mode_lib, dml_uint_t plane_idx)
{
	dml_uint_t     pipe_idx = 0;
	dml_bool_t     pipe_found = 0;

	ASSERT(plane_idx < __DML_NUM_PLANES__);

	for (dml_uint_t i = 0; i < __DML_NUM_PLANES__; i++) {
		if (plane_idx == mode_lib->mp.pipe_plane[i]) {
			pipe_idx = i;
			pipe_found = 1;
			break;
		}
	}
	ASSERT(pipe_found != 0);

	return pipe_idx;
}

void dml_calc_pipe_plane_mapping(const struct dml_hw_resource_st *hw, dml_uint_t *pipe_plane)
{
	dml_uint_t pipe_idx = 0;

	for (dml_uint_t k = 0; k < __DML_NUM_PLANES__; ++k) {
		pipe_plane[k] = __DML_PIPE_NO_PLANE__;
	}

	for (dml_uint_t plane_idx = 0; plane_idx < __DML_NUM_PLANES__; plane_idx++) {
		for (dml_uint_t i = 0; i < hw->DPPPerSurface[plane_idx]; i++) {
			pipe_plane[pipe_idx] = plane_idx;
			pipe_idx++;
		}
	}
}


