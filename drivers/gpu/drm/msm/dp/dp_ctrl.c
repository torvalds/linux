// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/pm_opp.h>
#include <linux/string_choices.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/drm_fixed.h>
#include <drm/drm_print.h>

#include "dp_reg.h"
#include "dp_ctrl.h"
#include "dp_link.h"

#define DP_KHZ_TO_HZ 1000
#define IDLE_PATTERN_COMPLETION_TIMEOUT_JIFFIES	(30 * HZ / 1000) /* 30 ms */
#define PSR_OPERATION_COMPLETION_TIMEOUT_JIFFIES       (300 * HZ / 1000) /* 300 ms */
#define WAIT_FOR_VIDEO_READY_TIMEOUT_JIFFIES (HZ / 2)

#define DP_CTRL_INTR_READY_FOR_VIDEO     BIT(0)
#define DP_CTRL_INTR_IDLE_PATTERN_SENT  BIT(3)

#define MR_LINK_TRAINING1  0x8
#define MR_LINK_SYMBOL_ERM 0x80
#define MR_LINK_PRBS7 0x100
#define MR_LINK_CUSTOM80 0x200
#define MR_LINK_TRAINING4  0x40

enum {
	DP_TRAINING_NONE,
	DP_TRAINING_1,
	DP_TRAINING_2,
};

struct msm_dp_tu_calc_input {
	u64 lclk;        /* 162, 270, 540 and 810 */
	u64 pclk_khz;    /* in KHz */
	u64 hactive;     /* active h-width */
	u64 hporch;      /* bp + fp + pulse */
	int nlanes;      /* no.of.lanes */
	int bpp;         /* bits */
	int pixel_enc;   /* 444, 420, 422 */
	int dsc_en;     /* dsc on/off */
	int async_en;   /* async mode */
	int fec_en;     /* fec */
	int compress_ratio; /* 2:1 = 200, 3:1 = 300, 3.75:1 = 375 */
	int num_of_dsc_slices; /* number of slices per line */
};

struct msm_dp_vc_tu_mapping_table {
	u32 vic;
	u8 lanes;
	u8 lrate; /* DP_LINK_RATE -> 162(6), 270(10), 540(20), 810 (30) */
	u8 bpp;
	u8 valid_boundary_link;
	u16 delay_start_link;
	bool boundary_moderation_en;
	u8 valid_lower_boundary_link;
	u8 upper_boundary_count;
	u8 lower_boundary_count;
	u8 tu_size_minus1;
};

struct msm_dp_ctrl_private {
	struct msm_dp_ctrl msm_dp_ctrl;
	struct drm_device *drm_dev;
	struct device *dev;
	struct drm_dp_aux *aux;
	struct msm_dp_panel *panel;
	struct msm_dp_link *link;
	struct msm_dp_catalog *catalog;

	struct phy *phy;

	unsigned int num_core_clks;
	struct clk_bulk_data *core_clks;

	unsigned int num_link_clks;
	struct clk_bulk_data *link_clks;

	struct clk *pixel_clk;

	union phy_configure_opts phy_opts;

	struct completion idle_comp;
	struct completion psr_op_comp;
	struct completion video_comp;

	bool core_clks_on;
	bool link_clks_on;
	bool stream_clks_on;
};

static int msm_dp_aux_link_configure(struct drm_dp_aux *aux,
					struct msm_dp_link_info *link)
{
	u8 values[2];
	int err;

	values[0] = drm_dp_link_rate_to_bw_code(link->rate);
	values[1] = link->num_lanes;

	if (link->capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err < 0)
		return err;

	return 0;
}

void msm_dp_ctrl_push_idle(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	reinit_completion(&ctrl->idle_comp);
	msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, DP_STATE_CTRL_PUSH_IDLE);

	if (!wait_for_completion_timeout(&ctrl->idle_comp,
			IDLE_PATTERN_COMPLETION_TIMEOUT_JIFFIES))
		pr_warn("PUSH_IDLE pattern timedout\n");

	drm_dbg_dp(ctrl->drm_dev, "mainlink off\n");
}

static void msm_dp_ctrl_config_ctrl(struct msm_dp_ctrl_private *ctrl)
{
	u32 config = 0, tbd;
	const u8 *dpcd = ctrl->panel->dpcd;

	/* Default-> LSCLK DIV: 1/4 LCLK  */
	config |= (2 << DP_CONFIGURATION_CTRL_LSCLK_DIV_SHIFT);

	if (ctrl->panel->msm_dp_mode.out_fmt_is_yuv_420)
		config |= DP_CONFIGURATION_CTRL_RGB_YUV; /* YUV420 */

	/* Scrambler reset enable */
	if (drm_dp_alternate_scrambler_reset_cap(dpcd))
		config |= DP_CONFIGURATION_CTRL_ASSR;

	tbd = msm_dp_link_get_test_bits_depth(ctrl->link,
			ctrl->panel->msm_dp_mode.bpp);

	config |= tbd << DP_CONFIGURATION_CTRL_BPC_SHIFT;

	/* Num of Lanes */
	config |= ((ctrl->link->link_params.num_lanes - 1)
			<< DP_CONFIGURATION_CTRL_NUM_OF_LANES_SHIFT);

	if (drm_dp_enhanced_frame_cap(dpcd))
		config |= DP_CONFIGURATION_CTRL_ENHANCED_FRAMING;

	config |= DP_CONFIGURATION_CTRL_P_INTERLACED; /* progressive video */

	/* sync clock & static Mvid */
	config |= DP_CONFIGURATION_CTRL_STATIC_DYNAMIC_CN;
	config |= DP_CONFIGURATION_CTRL_SYNC_ASYNC_CLK;

	if (ctrl->panel->psr_cap.version)
		config |= DP_CONFIGURATION_CTRL_SEND_VSC;

	msm_dp_catalog_ctrl_config_ctrl(ctrl->catalog, config);
}

static void msm_dp_ctrl_configure_source_params(struct msm_dp_ctrl_private *ctrl)
{
	u32 cc, tb;

	msm_dp_catalog_ctrl_lane_mapping(ctrl->catalog);
	msm_dp_catalog_setup_peripheral_flush(ctrl->catalog);

	msm_dp_ctrl_config_ctrl(ctrl);

	tb = msm_dp_link_get_test_bits_depth(ctrl->link,
		ctrl->panel->msm_dp_mode.bpp);
	cc = msm_dp_link_get_colorimetry_config(ctrl->link);
	msm_dp_catalog_ctrl_config_misc(ctrl->catalog, cc, tb);
	msm_dp_panel_timing_cfg(ctrl->panel);
}

/*
 * The structure and few functions present below are IP/Hardware
 * specific implementation. Most of the implementation will not
 * have coding comments
 */
struct tu_algo_data {
	s64 lclk_fp;
	s64 pclk_fp;
	s64 lwidth;
	s64 lwidth_fp;
	s64 hbp_relative_to_pclk;
	s64 hbp_relative_to_pclk_fp;
	int nlanes;
	int bpp;
	int pixelEnc;
	int dsc_en;
	int async_en;
	int bpc;

	uint delay_start_link_extra_pixclk;
	int extra_buffer_margin;
	s64 ratio_fp;
	s64 original_ratio_fp;

	s64 err_fp;
	s64 n_err_fp;
	s64 n_n_err_fp;
	int tu_size;
	int tu_size_desired;
	int tu_size_minus1;

	int valid_boundary_link;
	s64 resulting_valid_fp;
	s64 total_valid_fp;
	s64 effective_valid_fp;
	s64 effective_valid_recorded_fp;
	int n_tus;
	int n_tus_per_lane;
	int paired_tus;
	int remainder_tus;
	int remainder_tus_upper;
	int remainder_tus_lower;
	int extra_bytes;
	int filler_size;
	int delay_start_link;

	int extra_pclk_cycles;
	int extra_pclk_cycles_in_link_clk;
	s64 ratio_by_tu_fp;
	s64 average_valid2_fp;
	int new_valid_boundary_link;
	int remainder_symbols_exist;
	int n_symbols;
	s64 n_remainder_symbols_per_lane_fp;
	s64 last_partial_tu_fp;
	s64 TU_ratio_err_fp;

	int n_tus_incl_last_incomplete_tu;
	int extra_pclk_cycles_tmp;
	int extra_pclk_cycles_in_link_clk_tmp;
	int extra_required_bytes_new_tmp;
	int filler_size_tmp;
	int lower_filler_size_tmp;
	int delay_start_link_tmp;

	bool boundary_moderation_en;
	int boundary_mod_lower_err;
	int upper_boundary_count;
	int lower_boundary_count;
	int i_upper_boundary_count;
	int i_lower_boundary_count;
	int valid_lower_boundary_link;
	int even_distribution_BF;
	int even_distribution_legacy;
	int even_distribution;
	int min_hblank_violated;
	s64 delay_start_time_fp;
	s64 hbp_time_fp;
	s64 hactive_time_fp;
	s64 diff_abs_fp;

	s64 ratio;
};

static int _tu_param_compare(s64 a, s64 b)
{
	u32 a_sign;
	u32 b_sign;
	s64 a_temp, b_temp, minus_1;

	if (a == b)
		return 0;

	minus_1 = drm_fixp_from_fraction(-1, 1);

	a_sign = (a >> 32) & 0x80000000 ? 1 : 0;

	b_sign = (b >> 32) & 0x80000000 ? 1 : 0;

	if (a_sign > b_sign)
		return 2;
	else if (b_sign > a_sign)
		return 1;

	if (!a_sign && !b_sign) { /* positive */
		if (a > b)
			return 1;
		else
			return 2;
	} else { /* negative */
		a_temp = drm_fixp_mul(a, minus_1);
		b_temp = drm_fixp_mul(b, minus_1);

		if (a_temp > b_temp)
			return 2;
		else
			return 1;
	}
}

static void msm_dp_panel_update_tu_timings(struct msm_dp_tu_calc_input *in,
					struct tu_algo_data *tu)
{
	int nlanes = in->nlanes;
	int dsc_num_slices = in->num_of_dsc_slices;
	int dsc_num_bytes  = 0;
	int numerator;
	s64 pclk_dsc_fp;
	s64 dwidth_dsc_fp;
	s64 hbp_dsc_fp;

	int tot_num_eoc_symbols = 0;
	int tot_num_hor_bytes   = 0;
	int tot_num_dummy_bytes = 0;
	int dwidth_dsc_bytes    = 0;
	int  eoc_bytes           = 0;

	s64 temp1_fp, temp2_fp, temp3_fp;

	tu->lclk_fp              = drm_fixp_from_fraction(in->lclk, 1);
	tu->pclk_fp              = drm_fixp_from_fraction(in->pclk_khz, 1000);
	tu->lwidth               = in->hactive;
	tu->hbp_relative_to_pclk = in->hporch;
	tu->nlanes               = in->nlanes;
	tu->bpp                  = in->bpp;
	tu->pixelEnc             = in->pixel_enc;
	tu->dsc_en               = in->dsc_en;
	tu->async_en             = in->async_en;
	tu->lwidth_fp            = drm_fixp_from_fraction(in->hactive, 1);
	tu->hbp_relative_to_pclk_fp = drm_fixp_from_fraction(in->hporch, 1);

	if (tu->pixelEnc == 420) {
		temp1_fp = drm_fixp_from_fraction(2, 1);
		tu->pclk_fp = drm_fixp_div(tu->pclk_fp, temp1_fp);
		tu->lwidth_fp = drm_fixp_div(tu->lwidth_fp, temp1_fp);
		tu->hbp_relative_to_pclk_fp =
				drm_fixp_div(tu->hbp_relative_to_pclk_fp, 2);
	}

	if (tu->pixelEnc == 422) {
		switch (tu->bpp) {
		case 24:
			tu->bpp = 16;
			tu->bpc = 8;
			break;
		case 30:
			tu->bpp = 20;
			tu->bpc = 10;
			break;
		default:
			tu->bpp = 16;
			tu->bpc = 8;
			break;
		}
	} else {
		tu->bpc = tu->bpp/3;
	}

	if (!in->dsc_en)
		goto fec_check;

	temp1_fp = drm_fixp_from_fraction(in->compress_ratio, 100);
	temp2_fp = drm_fixp_from_fraction(in->bpp, 1);
	temp3_fp = drm_fixp_div(temp2_fp, temp1_fp);
	temp2_fp = drm_fixp_mul(tu->lwidth_fp, temp3_fp);

	temp1_fp = drm_fixp_from_fraction(8, 1);
	temp3_fp = drm_fixp_div(temp2_fp, temp1_fp);

	numerator = drm_fixp2int(temp3_fp);

	dsc_num_bytes  = numerator / dsc_num_slices;
	eoc_bytes           = dsc_num_bytes % nlanes;
	tot_num_eoc_symbols = nlanes * dsc_num_slices;
	tot_num_hor_bytes   = dsc_num_bytes * dsc_num_slices;
	tot_num_dummy_bytes = (nlanes - eoc_bytes) * dsc_num_slices;

	if (dsc_num_bytes == 0)
		pr_info("incorrect no of bytes per slice=%d\n", dsc_num_bytes);

	dwidth_dsc_bytes = (tot_num_hor_bytes +
				tot_num_eoc_symbols +
				(eoc_bytes == 0 ? 0 : tot_num_dummy_bytes));

	dwidth_dsc_fp = drm_fixp_from_fraction(dwidth_dsc_bytes, 3);

	temp2_fp = drm_fixp_mul(tu->pclk_fp, dwidth_dsc_fp);
	temp1_fp = drm_fixp_div(temp2_fp, tu->lwidth_fp);
	pclk_dsc_fp = temp1_fp;

	temp1_fp = drm_fixp_div(pclk_dsc_fp, tu->pclk_fp);
	temp2_fp = drm_fixp_mul(tu->hbp_relative_to_pclk_fp, temp1_fp);
	hbp_dsc_fp = temp2_fp;

	/* output */
	tu->pclk_fp = pclk_dsc_fp;
	tu->lwidth_fp = dwidth_dsc_fp;
	tu->hbp_relative_to_pclk_fp = hbp_dsc_fp;

fec_check:
	if (in->fec_en) {
		temp1_fp = drm_fixp_from_fraction(976, 1000); /* 0.976 */
		tu->lclk_fp = drm_fixp_mul(tu->lclk_fp, temp1_fp);
	}
}

static void _tu_valid_boundary_calc(struct tu_algo_data *tu)
{
	s64 temp1_fp, temp2_fp, temp, temp1, temp2;
	int compare_result_1, compare_result_2, compare_result_3;

	temp1_fp = drm_fixp_from_fraction(tu->tu_size, 1);
	temp2_fp = drm_fixp_mul(tu->ratio_fp, temp1_fp);

	tu->new_valid_boundary_link = drm_fixp2int_ceil(temp2_fp);

	temp = (tu->i_upper_boundary_count *
				tu->new_valid_boundary_link +
				tu->i_lower_boundary_count *
				(tu->new_valid_boundary_link-1));
	tu->average_valid2_fp = drm_fixp_from_fraction(temp,
					(tu->i_upper_boundary_count +
					tu->i_lower_boundary_count));

	temp1_fp = drm_fixp_from_fraction(tu->bpp, 8);
	temp2_fp = tu->lwidth_fp;
	temp1_fp = drm_fixp_mul(temp2_fp, temp1_fp);
	temp2_fp = drm_fixp_div(temp1_fp, tu->average_valid2_fp);
	tu->n_tus = drm_fixp2int(temp2_fp);
	if ((temp2_fp & 0xFFFFFFFF) > 0xFFFFF000)
		tu->n_tus += 1;

	temp1_fp = drm_fixp_from_fraction(tu->n_tus, 1);
	temp2_fp = drm_fixp_mul(temp1_fp, tu->average_valid2_fp);
	temp1_fp = drm_fixp_from_fraction(tu->n_symbols, 1);
	temp2_fp = temp1_fp - temp2_fp;
	temp1_fp = drm_fixp_from_fraction(tu->nlanes, 1);
	temp2_fp = drm_fixp_div(temp2_fp, temp1_fp);
	tu->n_remainder_symbols_per_lane_fp = temp2_fp;

	temp1_fp = drm_fixp_from_fraction(tu->tu_size, 1);
	tu->last_partial_tu_fp =
			drm_fixp_div(tu->n_remainder_symbols_per_lane_fp,
					temp1_fp);

	if (tu->n_remainder_symbols_per_lane_fp != 0)
		tu->remainder_symbols_exist = 1;
	else
		tu->remainder_symbols_exist = 0;

	temp1_fp = drm_fixp_from_fraction(tu->n_tus, tu->nlanes);
	tu->n_tus_per_lane = drm_fixp2int(temp1_fp);

	tu->paired_tus = (int)((tu->n_tus_per_lane) /
					(tu->i_upper_boundary_count +
					 tu->i_lower_boundary_count));

	tu->remainder_tus = tu->n_tus_per_lane - tu->paired_tus *
						(tu->i_upper_boundary_count +
						tu->i_lower_boundary_count);

	if ((tu->remainder_tus - tu->i_upper_boundary_count) > 0) {
		tu->remainder_tus_upper = tu->i_upper_boundary_count;
		tu->remainder_tus_lower = tu->remainder_tus -
						tu->i_upper_boundary_count;
	} else {
		tu->remainder_tus_upper = tu->remainder_tus;
		tu->remainder_tus_lower = 0;
	}

	temp = tu->paired_tus * (tu->i_upper_boundary_count *
				tu->new_valid_boundary_link +
				tu->i_lower_boundary_count *
				(tu->new_valid_boundary_link - 1)) +
				(tu->remainder_tus_upper *
				 tu->new_valid_boundary_link) +
				(tu->remainder_tus_lower *
				(tu->new_valid_boundary_link - 1));
	tu->total_valid_fp = drm_fixp_from_fraction(temp, 1);

	if (tu->remainder_symbols_exist) {
		temp1_fp = tu->total_valid_fp +
				tu->n_remainder_symbols_per_lane_fp;
		temp2_fp = drm_fixp_from_fraction(tu->n_tus_per_lane, 1);
		temp2_fp = temp2_fp + tu->last_partial_tu_fp;
		temp1_fp = drm_fixp_div(temp1_fp, temp2_fp);
	} else {
		temp2_fp = drm_fixp_from_fraction(tu->n_tus_per_lane, 1);
		temp1_fp = drm_fixp_div(tu->total_valid_fp, temp2_fp);
	}
	tu->effective_valid_fp = temp1_fp;

	temp1_fp = drm_fixp_from_fraction(tu->tu_size, 1);
	temp2_fp = drm_fixp_mul(tu->ratio_fp, temp1_fp);
	tu->n_n_err_fp = tu->effective_valid_fp - temp2_fp;

	temp1_fp = drm_fixp_from_fraction(tu->tu_size, 1);
	temp2_fp = drm_fixp_mul(tu->ratio_fp, temp1_fp);
	tu->n_err_fp = tu->average_valid2_fp - temp2_fp;

	tu->even_distribution = tu->n_tus % tu->nlanes == 0 ? 1 : 0;

	temp1_fp = drm_fixp_from_fraction(tu->bpp, 8);
	temp2_fp = tu->lwidth_fp;
	temp1_fp = drm_fixp_mul(temp2_fp, temp1_fp);
	temp2_fp = drm_fixp_div(temp1_fp, tu->average_valid2_fp);

	if (temp2_fp)
		tu->n_tus_incl_last_incomplete_tu = drm_fixp2int_ceil(temp2_fp);
	else
		tu->n_tus_incl_last_incomplete_tu = 0;

	temp1 = 0;
	temp1_fp = drm_fixp_from_fraction(tu->tu_size, 1);
	temp2_fp = drm_fixp_mul(tu->original_ratio_fp, temp1_fp);
	temp1_fp = tu->average_valid2_fp - temp2_fp;
	temp2_fp = drm_fixp_from_fraction(tu->n_tus_incl_last_incomplete_tu, 1);
	temp1_fp = drm_fixp_mul(temp2_fp, temp1_fp);

	if (temp1_fp)
		temp1 = drm_fixp2int_ceil(temp1_fp);

	temp = tu->i_upper_boundary_count * tu->nlanes;
	temp1_fp = drm_fixp_from_fraction(tu->tu_size, 1);
	temp2_fp = drm_fixp_mul(tu->original_ratio_fp, temp1_fp);
	temp1_fp = drm_fixp_from_fraction(tu->new_valid_boundary_link, 1);
	temp2_fp = temp1_fp - temp2_fp;
	temp1_fp = drm_fixp_from_fraction(temp, 1);
	temp2_fp = drm_fixp_mul(temp1_fp, temp2_fp);

	if (temp2_fp)
		temp2 = drm_fixp2int_ceil(temp2_fp);
	else
		temp2 = 0;
	tu->extra_required_bytes_new_tmp = (int)(temp1 + temp2);

	temp1_fp = drm_fixp_from_fraction(8, tu->bpp);
	temp2_fp = drm_fixp_from_fraction(
	tu->extra_required_bytes_new_tmp, 1);
	temp1_fp = drm_fixp_mul(temp2_fp, temp1_fp);

	if (temp1_fp)
		tu->extra_pclk_cycles_tmp = drm_fixp2int_ceil(temp1_fp);
	else
		tu->extra_pclk_cycles_tmp = 0;

	temp1_fp = drm_fixp_from_fraction(tu->extra_pclk_cycles_tmp, 1);
	temp2_fp = drm_fixp_div(tu->lclk_fp, tu->pclk_fp);
	temp1_fp = drm_fixp_mul(temp1_fp, temp2_fp);

	if (temp1_fp)
		tu->extra_pclk_cycles_in_link_clk_tmp =
						drm_fixp2int_ceil(temp1_fp);
	else
		tu->extra_pclk_cycles_in_link_clk_tmp = 0;

	tu->filler_size_tmp = tu->tu_size - tu->new_valid_boundary_link;

	tu->lower_filler_size_tmp = tu->filler_size_tmp + 1;

	tu->delay_start_link_tmp = tu->extra_pclk_cycles_in_link_clk_tmp +
					tu->lower_filler_size_tmp +
					tu->extra_buffer_margin;

	temp1_fp = drm_fixp_from_fraction(tu->delay_start_link_tmp, 1);
	tu->delay_start_time_fp = drm_fixp_div(temp1_fp, tu->lclk_fp);

	compare_result_1 = _tu_param_compare(tu->n_n_err_fp, tu->diff_abs_fp);
	if (compare_result_1 == 2)
		compare_result_1 = 1;
	else
		compare_result_1 = 0;

	compare_result_2 = _tu_param_compare(tu->n_n_err_fp, tu->err_fp);
	if (compare_result_2 == 2)
		compare_result_2 = 1;
	else
		compare_result_2 = 0;

	compare_result_3 = _tu_param_compare(tu->hbp_time_fp,
					tu->delay_start_time_fp);
	if (compare_result_3 == 2)
		compare_result_3 = 0;
	else
		compare_result_3 = 1;

	if (((tu->even_distribution == 1) ||
			((tu->even_distribution_BF == 0) &&
			(tu->even_distribution_legacy == 0))) &&
			tu->n_err_fp >= 0 && tu->n_n_err_fp >= 0 &&
			compare_result_2 &&
			(compare_result_1 || (tu->min_hblank_violated == 1)) &&
			(tu->new_valid_boundary_link - 1) > 0 &&
			compare_result_3 &&
			(tu->delay_start_link_tmp <= 1023)) {
		tu->upper_boundary_count = tu->i_upper_boundary_count;
		tu->lower_boundary_count = tu->i_lower_boundary_count;
		tu->err_fp = tu->n_n_err_fp;
		tu->boundary_moderation_en = true;
		tu->tu_size_desired = tu->tu_size;
		tu->valid_boundary_link = tu->new_valid_boundary_link;
		tu->effective_valid_recorded_fp = tu->effective_valid_fp;
		tu->even_distribution_BF = 1;
		tu->delay_start_link = tu->delay_start_link_tmp;
	} else if (tu->boundary_mod_lower_err == 0) {
		compare_result_1 = _tu_param_compare(tu->n_n_err_fp,
							tu->diff_abs_fp);
		if (compare_result_1 == 2)
			tu->boundary_mod_lower_err = 1;
	}
}

static void _dp_ctrl_calc_tu(struct msm_dp_ctrl_private *ctrl,
				struct msm_dp_tu_calc_input *in,
				struct msm_dp_vc_tu_mapping_table *tu_table)
{
	struct tu_algo_data *tu;
	int compare_result_1, compare_result_2;
	u64 temp = 0;
	s64 temp_fp = 0, temp1_fp = 0, temp2_fp = 0;

	s64 LCLK_FAST_SKEW_fp = drm_fixp_from_fraction(6, 10000); /* 0.0006 */
	s64 const_p49_fp = drm_fixp_from_fraction(49, 100); /* 0.49 */
	s64 const_p56_fp = drm_fixp_from_fraction(56, 100); /* 0.56 */
	s64 RATIO_SCALE_fp = drm_fixp_from_fraction(1001, 1000);

	u8 DP_BRUTE_FORCE = 1;
	s64 BRUTE_FORCE_THRESHOLD_fp = drm_fixp_from_fraction(1, 10); /* 0.1 */
	uint EXTRA_PIXCLK_CYCLE_DELAY = 4;
	uint HBLANK_MARGIN = 4;

	tu = kzalloc(sizeof(*tu), GFP_KERNEL);
	if (!tu)
		return;

	msm_dp_panel_update_tu_timings(in, tu);

	tu->err_fp = drm_fixp_from_fraction(1000, 1); /* 1000 */

	temp1_fp = drm_fixp_from_fraction(4, 1);
	temp2_fp = drm_fixp_mul(temp1_fp, tu->lclk_fp);
	temp_fp = drm_fixp_div(temp2_fp, tu->pclk_fp);
	tu->extra_buffer_margin = drm_fixp2int_ceil(temp_fp);

	temp1_fp = drm_fixp_from_fraction(tu->bpp, 8);
	temp2_fp = drm_fixp_mul(tu->pclk_fp, temp1_fp);
	temp1_fp = drm_fixp_from_fraction(tu->nlanes, 1);
	temp2_fp = drm_fixp_div(temp2_fp, temp1_fp);
	tu->ratio_fp = drm_fixp_div(temp2_fp, tu->lclk_fp);

	tu->original_ratio_fp = tu->ratio_fp;
	tu->boundary_moderation_en = false;
	tu->upper_boundary_count = 0;
	tu->lower_boundary_count = 0;
	tu->i_upper_boundary_count = 0;
	tu->i_lower_boundary_count = 0;
	tu->valid_lower_boundary_link = 0;
	tu->even_distribution_BF = 0;
	tu->even_distribution_legacy = 0;
	tu->even_distribution = 0;
	tu->delay_start_time_fp = 0;

	tu->err_fp = drm_fixp_from_fraction(1000, 1);
	tu->n_err_fp = 0;
	tu->n_n_err_fp = 0;

	tu->ratio = drm_fixp2int(tu->ratio_fp);
	temp1_fp = drm_fixp_from_fraction(tu->nlanes, 1);
	div64_u64_rem(tu->lwidth_fp, temp1_fp, &temp2_fp);
	if (temp2_fp != 0 &&
			!tu->ratio && tu->dsc_en == 0) {
		tu->ratio_fp = drm_fixp_mul(tu->ratio_fp, RATIO_SCALE_fp);
		tu->ratio = drm_fixp2int(tu->ratio_fp);
		if (tu->ratio)
			tu->ratio_fp = drm_fixp_from_fraction(1, 1);
	}

	if (tu->ratio > 1)
		tu->ratio = 1;

	if (tu->ratio == 1)
		goto tu_size_calc;

	compare_result_1 = _tu_param_compare(tu->ratio_fp, const_p49_fp);
	if (!compare_result_1 || compare_result_1 == 1)
		compare_result_1 = 1;
	else
		compare_result_1 = 0;

	compare_result_2 = _tu_param_compare(tu->ratio_fp, const_p56_fp);
	if (!compare_result_2 || compare_result_2 == 2)
		compare_result_2 = 1;
	else
		compare_result_2 = 0;

	if (tu->dsc_en && compare_result_1 && compare_result_2) {
		HBLANK_MARGIN += 4;
		drm_dbg_dp(ctrl->drm_dev,
			"increase HBLANK_MARGIN to %d\n", HBLANK_MARGIN);
	}

tu_size_calc:
	for (tu->tu_size = 32; tu->tu_size <= 64; tu->tu_size++) {
		temp1_fp = drm_fixp_from_fraction(tu->tu_size, 1);
		temp2_fp = drm_fixp_mul(tu->ratio_fp, temp1_fp);
		temp = drm_fixp2int_ceil(temp2_fp);
		temp1_fp = drm_fixp_from_fraction(temp, 1);
		tu->n_err_fp = temp1_fp - temp2_fp;

		if (tu->n_err_fp < tu->err_fp) {
			tu->err_fp = tu->n_err_fp;
			tu->tu_size_desired = tu->tu_size;
		}
	}

	tu->tu_size_minus1 = tu->tu_size_desired - 1;

	temp1_fp = drm_fixp_from_fraction(tu->tu_size_desired, 1);
	temp2_fp = drm_fixp_mul(tu->ratio_fp, temp1_fp);
	tu->valid_boundary_link = drm_fixp2int_ceil(temp2_fp);

	temp1_fp = drm_fixp_from_fraction(tu->bpp, 8);
	temp2_fp = tu->lwidth_fp;
	temp2_fp = drm_fixp_mul(temp2_fp, temp1_fp);

	temp1_fp = drm_fixp_from_fraction(tu->valid_boundary_link, 1);
	temp2_fp = drm_fixp_div(temp2_fp, temp1_fp);
	tu->n_tus = drm_fixp2int(temp2_fp);
	if ((temp2_fp & 0xFFFFFFFF) > 0xFFFFF000)
		tu->n_tus += 1;

	tu->even_distribution_legacy = tu->n_tus % tu->nlanes == 0 ? 1 : 0;

	drm_dbg_dp(ctrl->drm_dev,
			"n_sym = %d, num_of_tus = %d\n",
			tu->valid_boundary_link, tu->n_tus);

	temp1_fp = drm_fixp_from_fraction(tu->tu_size_desired, 1);
	temp2_fp = drm_fixp_mul(tu->original_ratio_fp, temp1_fp);
	temp1_fp = drm_fixp_from_fraction(tu->valid_boundary_link, 1);
	temp2_fp = temp1_fp - temp2_fp;
	temp1_fp = drm_fixp_from_fraction(tu->n_tus + 1, 1);
	temp2_fp = drm_fixp_mul(temp1_fp, temp2_fp);

	temp = drm_fixp2int(temp2_fp);
	if (temp && temp2_fp)
		tu->extra_bytes = drm_fixp2int_ceil(temp2_fp);
	else
		tu->extra_bytes = 0;

	temp1_fp = drm_fixp_from_fraction(tu->extra_bytes, 1);
	temp2_fp = drm_fixp_from_fraction(8, tu->bpp);
	temp1_fp = drm_fixp_mul(temp1_fp, temp2_fp);

	if (temp && temp1_fp)
		tu->extra_pclk_cycles = drm_fixp2int_ceil(temp1_fp);
	else
		tu->extra_pclk_cycles = drm_fixp2int(temp1_fp);

	temp1_fp = drm_fixp_div(tu->lclk_fp, tu->pclk_fp);
	temp2_fp = drm_fixp_from_fraction(tu->extra_pclk_cycles, 1);
	temp1_fp = drm_fixp_mul(temp2_fp, temp1_fp);

	if (temp1_fp)
		tu->extra_pclk_cycles_in_link_clk = drm_fixp2int_ceil(temp1_fp);
	else
		tu->extra_pclk_cycles_in_link_clk = drm_fixp2int(temp1_fp);

	tu->filler_size = tu->tu_size_desired - tu->valid_boundary_link;

	temp1_fp = drm_fixp_from_fraction(tu->tu_size_desired, 1);
	tu->ratio_by_tu_fp = drm_fixp_mul(tu->ratio_fp, temp1_fp);

	tu->delay_start_link = tu->extra_pclk_cycles_in_link_clk +
				tu->filler_size + tu->extra_buffer_margin;

	tu->resulting_valid_fp =
			drm_fixp_from_fraction(tu->valid_boundary_link, 1);

	temp1_fp = drm_fixp_from_fraction(tu->tu_size_desired, 1);
	temp2_fp = drm_fixp_div(tu->resulting_valid_fp, temp1_fp);
	tu->TU_ratio_err_fp = temp2_fp - tu->original_ratio_fp;

	temp1_fp = drm_fixp_from_fraction(HBLANK_MARGIN, 1);
	temp1_fp = tu->hbp_relative_to_pclk_fp - temp1_fp;
	tu->hbp_time_fp = drm_fixp_div(temp1_fp, tu->pclk_fp);

	temp1_fp = drm_fixp_from_fraction(tu->delay_start_link, 1);
	tu->delay_start_time_fp = drm_fixp_div(temp1_fp, tu->lclk_fp);

	compare_result_1 = _tu_param_compare(tu->hbp_time_fp,
					tu->delay_start_time_fp);
	if (compare_result_1 == 2) /* if (hbp_time_fp < delay_start_time_fp) */
		tu->min_hblank_violated = 1;

	tu->hactive_time_fp = drm_fixp_div(tu->lwidth_fp, tu->pclk_fp);

	compare_result_2 = _tu_param_compare(tu->hactive_time_fp,
						tu->delay_start_time_fp);
	if (compare_result_2 == 2)
		tu->min_hblank_violated = 1;

	tu->delay_start_time_fp = 0;

	/* brute force */

	tu->delay_start_link_extra_pixclk = EXTRA_PIXCLK_CYCLE_DELAY;
	tu->diff_abs_fp = tu->resulting_valid_fp - tu->ratio_by_tu_fp;

	temp = drm_fixp2int(tu->diff_abs_fp);
	if (!temp && tu->diff_abs_fp <= 0xffff)
		tu->diff_abs_fp = 0;

	/* if(diff_abs < 0) diff_abs *= -1 */
	if (tu->diff_abs_fp < 0)
		tu->diff_abs_fp = drm_fixp_mul(tu->diff_abs_fp, -1);

	tu->boundary_mod_lower_err = 0;
	if ((tu->diff_abs_fp != 0 &&
			((tu->diff_abs_fp > BRUTE_FORCE_THRESHOLD_fp) ||
			 (tu->even_distribution_legacy == 0) ||
			 (DP_BRUTE_FORCE == 1))) ||
			(tu->min_hblank_violated == 1)) {
		do {
			tu->err_fp = drm_fixp_from_fraction(1000, 1);

			temp1_fp = drm_fixp_div(tu->lclk_fp, tu->pclk_fp);
			temp2_fp = drm_fixp_from_fraction(
					tu->delay_start_link_extra_pixclk, 1);
			temp1_fp = drm_fixp_mul(temp2_fp, temp1_fp);

			if (temp1_fp)
				tu->extra_buffer_margin =
					drm_fixp2int_ceil(temp1_fp);
			else
				tu->extra_buffer_margin = 0;

			temp1_fp = drm_fixp_from_fraction(tu->bpp, 8);
			temp1_fp = drm_fixp_mul(tu->lwidth_fp, temp1_fp);

			if (temp1_fp)
				tu->n_symbols = drm_fixp2int_ceil(temp1_fp);
			else
				tu->n_symbols = 0;

			for (tu->tu_size = 32; tu->tu_size <= 64; tu->tu_size++) {
				for (tu->i_upper_boundary_count = 1;
					tu->i_upper_boundary_count <= 15;
					tu->i_upper_boundary_count++) {
					for (tu->i_lower_boundary_count = 1;
						tu->i_lower_boundary_count <= 15;
						tu->i_lower_boundary_count++) {
						_tu_valid_boundary_calc(tu);
					}
				}
			}
			tu->delay_start_link_extra_pixclk--;
		} while (tu->boundary_moderation_en != true &&
			tu->boundary_mod_lower_err == 1 &&
			tu->delay_start_link_extra_pixclk != 0);

		if (tu->boundary_moderation_en == true) {
			temp1_fp = drm_fixp_from_fraction(
					(tu->upper_boundary_count *
					tu->valid_boundary_link +
					tu->lower_boundary_count *
					(tu->valid_boundary_link - 1)), 1);
			temp2_fp = drm_fixp_from_fraction(
					(tu->upper_boundary_count +
					tu->lower_boundary_count), 1);
			tu->resulting_valid_fp =
					drm_fixp_div(temp1_fp, temp2_fp);

			temp1_fp = drm_fixp_from_fraction(
					tu->tu_size_desired, 1);
			tu->ratio_by_tu_fp =
				drm_fixp_mul(tu->original_ratio_fp, temp1_fp);

			tu->valid_lower_boundary_link =
				tu->valid_boundary_link - 1;

			temp1_fp = drm_fixp_from_fraction(tu->bpp, 8);
			temp1_fp = drm_fixp_mul(tu->lwidth_fp, temp1_fp);
			temp2_fp = drm_fixp_div(temp1_fp,
						tu->resulting_valid_fp);
			tu->n_tus = drm_fixp2int(temp2_fp);

			tu->tu_size_minus1 = tu->tu_size_desired - 1;
			tu->even_distribution_BF = 1;

			temp1_fp =
				drm_fixp_from_fraction(tu->tu_size_desired, 1);
			temp2_fp =
				drm_fixp_div(tu->resulting_valid_fp, temp1_fp);
			tu->TU_ratio_err_fp = temp2_fp - tu->original_ratio_fp;
		}
	}

	temp2_fp = drm_fixp_mul(LCLK_FAST_SKEW_fp, tu->lwidth_fp);

	if (temp2_fp)
		temp = drm_fixp2int_ceil(temp2_fp);
	else
		temp = 0;

	temp1_fp = drm_fixp_from_fraction(tu->nlanes, 1);
	temp2_fp = drm_fixp_mul(tu->original_ratio_fp, temp1_fp);
	temp1_fp = drm_fixp_from_fraction(tu->bpp, 8);
	temp2_fp = drm_fixp_div(temp1_fp, temp2_fp);
	temp1_fp = drm_fixp_from_fraction(temp, 1);
	temp2_fp = drm_fixp_mul(temp1_fp, temp2_fp);
	temp = drm_fixp2int(temp2_fp);

	if (tu->async_en)
		tu->delay_start_link += (int)temp;

	temp1_fp = drm_fixp_from_fraction(tu->delay_start_link, 1);
	tu->delay_start_time_fp = drm_fixp_div(temp1_fp, tu->lclk_fp);

	/* OUTPUTS */
	tu_table->valid_boundary_link       = tu->valid_boundary_link;
	tu_table->delay_start_link          = tu->delay_start_link;
	tu_table->boundary_moderation_en    = tu->boundary_moderation_en;
	tu_table->valid_lower_boundary_link = tu->valid_lower_boundary_link;
	tu_table->upper_boundary_count      = tu->upper_boundary_count;
	tu_table->lower_boundary_count      = tu->lower_boundary_count;
	tu_table->tu_size_minus1            = tu->tu_size_minus1;

	drm_dbg_dp(ctrl->drm_dev, "TU: valid_boundary_link: %d\n",
				tu_table->valid_boundary_link);
	drm_dbg_dp(ctrl->drm_dev, "TU: delay_start_link: %d\n",
				tu_table->delay_start_link);
	drm_dbg_dp(ctrl->drm_dev, "TU: boundary_moderation_en: %d\n",
			tu_table->boundary_moderation_en);
	drm_dbg_dp(ctrl->drm_dev, "TU: valid_lower_boundary_link: %d\n",
			tu_table->valid_lower_boundary_link);
	drm_dbg_dp(ctrl->drm_dev, "TU: upper_boundary_count: %d\n",
			tu_table->upper_boundary_count);
	drm_dbg_dp(ctrl->drm_dev, "TU: lower_boundary_count: %d\n",
			tu_table->lower_boundary_count);
	drm_dbg_dp(ctrl->drm_dev, "TU: tu_size_minus1: %d\n",
			tu_table->tu_size_minus1);

	kfree(tu);
}

static void msm_dp_ctrl_calc_tu_parameters(struct msm_dp_ctrl_private *ctrl,
		struct msm_dp_vc_tu_mapping_table *tu_table)
{
	struct msm_dp_tu_calc_input in;
	struct drm_display_mode *drm_mode;

	drm_mode = &ctrl->panel->msm_dp_mode.drm_mode;

	in.lclk = ctrl->link->link_params.rate / 1000;
	in.pclk_khz = drm_mode->clock;
	in.hactive = drm_mode->hdisplay;
	in.hporch = drm_mode->htotal - drm_mode->hdisplay;
	in.nlanes = ctrl->link->link_params.num_lanes;
	in.bpp = ctrl->panel->msm_dp_mode.bpp;
	in.pixel_enc = ctrl->panel->msm_dp_mode.out_fmt_is_yuv_420 ? 420 : 444;
	in.dsc_en = 0;
	in.async_en = 0;
	in.fec_en = 0;
	in.num_of_dsc_slices = 0;
	in.compress_ratio = 100;

	_dp_ctrl_calc_tu(ctrl, &in, tu_table);
}

static void msm_dp_ctrl_setup_tr_unit(struct msm_dp_ctrl_private *ctrl)
{
	u32 msm_dp_tu = 0x0;
	u32 valid_boundary = 0x0;
	u32 valid_boundary2 = 0x0;
	struct msm_dp_vc_tu_mapping_table tu_calc_table;

	msm_dp_ctrl_calc_tu_parameters(ctrl, &tu_calc_table);

	msm_dp_tu |= tu_calc_table.tu_size_minus1;
	valid_boundary |= tu_calc_table.valid_boundary_link;
	valid_boundary |= (tu_calc_table.delay_start_link << 16);

	valid_boundary2 |= (tu_calc_table.valid_lower_boundary_link << 1);
	valid_boundary2 |= (tu_calc_table.upper_boundary_count << 16);
	valid_boundary2 |= (tu_calc_table.lower_boundary_count << 20);

	if (tu_calc_table.boundary_moderation_en)
		valid_boundary2 |= BIT(0);

	pr_debug("dp_tu=0x%x, valid_boundary=0x%x, valid_boundary2=0x%x\n",
			msm_dp_tu, valid_boundary, valid_boundary2);

	msm_dp_catalog_ctrl_update_transfer_unit(ctrl->catalog,
				msm_dp_tu, valid_boundary, valid_boundary2);
}

static int msm_dp_ctrl_wait4video_ready(struct msm_dp_ctrl_private *ctrl)
{
	int ret = 0;

	if (!wait_for_completion_timeout(&ctrl->video_comp,
				WAIT_FOR_VIDEO_READY_TIMEOUT_JIFFIES)) {
		DRM_ERROR("wait4video timedout\n");
		ret = -ETIMEDOUT;
	}
	return ret;
}

static int msm_dp_ctrl_set_vx_px(struct msm_dp_ctrl_private *ctrl,
			     u8 v_level, u8 p_level)
{
	union phy_configure_opts *phy_opts = &ctrl->phy_opts;

	/* TODO: Update for all lanes instead of just first one */
	phy_opts->dp.voltage[0] = v_level;
	phy_opts->dp.pre[0] = p_level;
	phy_opts->dp.set_voltages = 1;
	phy_configure(ctrl->phy, phy_opts);
	phy_opts->dp.set_voltages = 0;

	return 0;
}

static int msm_dp_ctrl_update_vx_px(struct msm_dp_ctrl_private *ctrl)
{
	struct msm_dp_link *link = ctrl->link;
	int ret = 0, lane, lane_cnt;
	u8 buf[4];
	u32 max_level_reached = 0;
	u32 voltage_swing_level = link->phy_params.v_level;
	u32 pre_emphasis_level = link->phy_params.p_level;

	drm_dbg_dp(ctrl->drm_dev,
		"voltage level: %d emphasis level: %d\n",
			voltage_swing_level, pre_emphasis_level);
	ret = msm_dp_ctrl_set_vx_px(ctrl,
		voltage_swing_level, pre_emphasis_level);

	if (ret)
		return ret;

	if (voltage_swing_level >= DP_TRAIN_LEVEL_MAX) {
		drm_dbg_dp(ctrl->drm_dev,
				"max. voltage swing level reached %d\n",
				voltage_swing_level);
		max_level_reached |= DP_TRAIN_MAX_SWING_REACHED;
	}

	if (pre_emphasis_level >= DP_TRAIN_LEVEL_MAX) {
		drm_dbg_dp(ctrl->drm_dev,
				"max. pre-emphasis level reached %d\n",
				pre_emphasis_level);
		max_level_reached  |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
	}

	pre_emphasis_level <<= DP_TRAIN_PRE_EMPHASIS_SHIFT;

	lane_cnt = ctrl->link->link_params.num_lanes;
	for (lane = 0; lane < lane_cnt; lane++)
		buf[lane] = voltage_swing_level | pre_emphasis_level
				| max_level_reached;

	drm_dbg_dp(ctrl->drm_dev, "sink: p|v=0x%x\n",
			voltage_swing_level | pre_emphasis_level);
	ret = drm_dp_dpcd_write(ctrl->aux, DP_TRAINING_LANE0_SET,
					buf, lane_cnt);
	if (ret == lane_cnt)
		ret = 0;

	return ret;
}

static bool msm_dp_ctrl_train_pattern_set(struct msm_dp_ctrl_private *ctrl,
		u8 pattern)
{
	u8 buf;
	int ret = 0;

	drm_dbg_dp(ctrl->drm_dev, "sink: pattern=%x\n", pattern);

	buf = pattern;

	if (pattern && pattern != DP_TRAINING_PATTERN_4)
		buf |= DP_LINK_SCRAMBLING_DISABLE;

	ret = drm_dp_dpcd_writeb(ctrl->aux, DP_TRAINING_PATTERN_SET, buf);
	return ret == 1;
}

static int msm_dp_ctrl_read_link_status(struct msm_dp_ctrl_private *ctrl,
				    u8 *link_status)
{
	int ret = 0, len;

	len = drm_dp_dpcd_read_link_status(ctrl->aux, link_status);
	if (len != DP_LINK_STATUS_SIZE) {
		DRM_ERROR("DP link status read failed, err: %d\n", len);
		ret = -EINVAL;
	}

	return ret;
}

static int msm_dp_ctrl_link_train_1(struct msm_dp_ctrl_private *ctrl,
			int *training_step)
{
	int tries, old_v_level, ret = 0;
	u8 link_status[DP_LINK_STATUS_SIZE];
	int const maximum_retries = 4;

	msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);

	*training_step = DP_TRAINING_1;

	ret = msm_dp_catalog_ctrl_set_pattern_state_bit(ctrl->catalog, 1);
	if (ret)
		return ret;
	msm_dp_ctrl_train_pattern_set(ctrl, DP_TRAINING_PATTERN_1 |
		DP_LINK_SCRAMBLING_DISABLE);

	ret = msm_dp_ctrl_update_vx_px(ctrl);
	if (ret)
		return ret;

	tries = 0;
	old_v_level = ctrl->link->phy_params.v_level;
	for (tries = 0; tries < maximum_retries; tries++) {
		drm_dp_link_train_clock_recovery_delay(ctrl->aux, ctrl->panel->dpcd);

		ret = msm_dp_ctrl_read_link_status(ctrl, link_status);
		if (ret)
			return ret;

		if (drm_dp_clock_recovery_ok(link_status,
			ctrl->link->link_params.num_lanes)) {
			return 0;
		}

		if (ctrl->link->phy_params.v_level >=
			DP_TRAIN_LEVEL_MAX) {
			DRM_ERROR_RATELIMITED("max v_level reached\n");
			return -EAGAIN;
		}

		if (old_v_level != ctrl->link->phy_params.v_level) {
			tries = 0;
			old_v_level = ctrl->link->phy_params.v_level;
		}

		msm_dp_link_adjust_levels(ctrl->link, link_status);
		ret = msm_dp_ctrl_update_vx_px(ctrl);
		if (ret)
			return ret;
	}

	DRM_ERROR("max tries reached\n");
	return -ETIMEDOUT;
}

static int msm_dp_ctrl_link_rate_down_shift(struct msm_dp_ctrl_private *ctrl)
{
	int ret = 0;

	switch (ctrl->link->link_params.rate) {
	case 810000:
		ctrl->link->link_params.rate = 540000;
		break;
	case 540000:
		ctrl->link->link_params.rate = 270000;
		break;
	case 270000:
		ctrl->link->link_params.rate = 162000;
		break;
	case 162000:
	default:
		ret = -EINVAL;
		break;
	}

	if (!ret) {
		drm_dbg_dp(ctrl->drm_dev, "new rate=0x%x\n",
				ctrl->link->link_params.rate);
	}

	return ret;
}

static int msm_dp_ctrl_link_lane_down_shift(struct msm_dp_ctrl_private *ctrl)
{

	if (ctrl->link->link_params.num_lanes == 1)
		return -1;

	ctrl->link->link_params.num_lanes /= 2;
	ctrl->link->link_params.rate = ctrl->panel->link_info.rate;

	ctrl->link->phy_params.p_level = 0;
	ctrl->link->phy_params.v_level = 0;

	return 0;
}

static void msm_dp_ctrl_clear_training_pattern(struct msm_dp_ctrl_private *ctrl)
{
	msm_dp_ctrl_train_pattern_set(ctrl, DP_TRAINING_PATTERN_DISABLE);
	drm_dp_link_train_channel_eq_delay(ctrl->aux, ctrl->panel->dpcd);
}

static int msm_dp_ctrl_link_train_2(struct msm_dp_ctrl_private *ctrl,
			int *training_step)
{
	int tries = 0, ret = 0;
	u8 pattern;
	u32 state_ctrl_bit;
	int const maximum_retries = 5;
	u8 link_status[DP_LINK_STATUS_SIZE];

	msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);

	*training_step = DP_TRAINING_2;

	if (drm_dp_tps4_supported(ctrl->panel->dpcd)) {
		pattern = DP_TRAINING_PATTERN_4;
		state_ctrl_bit = 4;
	} else if (drm_dp_tps3_supported(ctrl->panel->dpcd)) {
		pattern = DP_TRAINING_PATTERN_3;
		state_ctrl_bit = 3;
	} else {
		pattern = DP_TRAINING_PATTERN_2;
		state_ctrl_bit = 2;
	}

	ret = msm_dp_catalog_ctrl_set_pattern_state_bit(ctrl->catalog, state_ctrl_bit);
	if (ret)
		return ret;

	msm_dp_ctrl_train_pattern_set(ctrl, pattern);

	for (tries = 0; tries <= maximum_retries; tries++) {
		drm_dp_link_train_channel_eq_delay(ctrl->aux, ctrl->panel->dpcd);

		ret = msm_dp_ctrl_read_link_status(ctrl, link_status);
		if (ret)
			return ret;

		if (drm_dp_channel_eq_ok(link_status,
			ctrl->link->link_params.num_lanes)) {
			return 0;
		}

		msm_dp_link_adjust_levels(ctrl->link, link_status);
		ret = msm_dp_ctrl_update_vx_px(ctrl);
		if (ret)
			return ret;

	}

	return -ETIMEDOUT;
}

static int msm_dp_ctrl_link_train(struct msm_dp_ctrl_private *ctrl,
			int *training_step)
{
	int ret = 0;
	const u8 *dpcd = ctrl->panel->dpcd;
	u8 encoding[] = { 0, DP_SET_ANSI_8B10B };
	u8 assr;
	struct msm_dp_link_info link_info = {0};

	msm_dp_ctrl_config_ctrl(ctrl);

	link_info.num_lanes = ctrl->link->link_params.num_lanes;
	link_info.rate = ctrl->link->link_params.rate;
	link_info.capabilities = DP_LINK_CAP_ENHANCED_FRAMING;

	msm_dp_link_reset_phy_params_vx_px(ctrl->link);

	msm_dp_aux_link_configure(ctrl->aux, &link_info);

	if (drm_dp_max_downspread(dpcd))
		encoding[0] |= DP_SPREAD_AMP_0_5;

	/* config DOWNSPREAD_CTRL and MAIN_LINK_CHANNEL_CODING_SET */
	drm_dp_dpcd_write(ctrl->aux, DP_DOWNSPREAD_CTRL, encoding, 2);

	if (drm_dp_alternate_scrambler_reset_cap(dpcd)) {
		assr = DP_ALTERNATE_SCRAMBLER_RESET_ENABLE;
		drm_dp_dpcd_write(ctrl->aux, DP_EDP_CONFIGURATION_SET,
				&assr, 1);
	}

	ret = msm_dp_ctrl_link_train_1(ctrl, training_step);
	if (ret) {
		DRM_ERROR("link training #1 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	drm_dbg_dp(ctrl->drm_dev, "link training #1 successful\n");

	ret = msm_dp_ctrl_link_train_2(ctrl, training_step);
	if (ret) {
		DRM_ERROR("link training #2 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	drm_dbg_dp(ctrl->drm_dev, "link training #2 successful\n");

end:
	msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);

	return ret;
}

static int msm_dp_ctrl_setup_main_link(struct msm_dp_ctrl_private *ctrl,
			int *training_step)
{
	int ret = 0;

	msm_dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, true);

	if (ctrl->link->sink_request & DP_TEST_LINK_PHY_TEST_PATTERN)
		return ret;

	/*
	 * As part of previous calls, DP controller state might have
	 * transitioned to PUSH_IDLE. In order to start transmitting
	 * a link training pattern, we have to first do soft reset.
	 */

	ret = msm_dp_ctrl_link_train(ctrl, training_step);

	return ret;
}

int msm_dp_ctrl_core_clk_enable(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	int ret = 0;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	if (ctrl->core_clks_on) {
		drm_dbg_dp(ctrl->drm_dev, "core clks already enabled\n");
		return 0;
	}

	ret = clk_bulk_prepare_enable(ctrl->num_core_clks, ctrl->core_clks);
	if (ret)
		return ret;

	ctrl->core_clks_on = true;

	drm_dbg_dp(ctrl->drm_dev, "enable core clocks \n");
	drm_dbg_dp(ctrl->drm_dev, "stream_clks:%s link_clks:%s core_clks:%s\n",
		   str_on_off(ctrl->stream_clks_on),
		   str_on_off(ctrl->link_clks_on),
		   str_on_off(ctrl->core_clks_on));

	return 0;
}

void msm_dp_ctrl_core_clk_disable(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	clk_bulk_disable_unprepare(ctrl->num_core_clks, ctrl->core_clks);

	ctrl->core_clks_on = false;

	drm_dbg_dp(ctrl->drm_dev, "disable core clocks \n");
	drm_dbg_dp(ctrl->drm_dev, "stream_clks:%s link_clks:%s core_clks:%s\n",
		   str_on_off(ctrl->stream_clks_on),
		   str_on_off(ctrl->link_clks_on),
		   str_on_off(ctrl->core_clks_on));
}

static int msm_dp_ctrl_link_clk_enable(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	int ret = 0;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	if (ctrl->link_clks_on) {
		drm_dbg_dp(ctrl->drm_dev, "links clks already enabled\n");
		return 0;
	}

	if (!ctrl->core_clks_on) {
		drm_dbg_dp(ctrl->drm_dev, "Enable core clks before link clks\n");

		msm_dp_ctrl_core_clk_enable(msm_dp_ctrl);
	}

	ret = clk_bulk_prepare_enable(ctrl->num_link_clks, ctrl->link_clks);
	if (ret)
		return ret;

	ctrl->link_clks_on = true;

	drm_dbg_dp(ctrl->drm_dev, "enable link clocks\n");
	drm_dbg_dp(ctrl->drm_dev, "stream_clks:%s link_clks:%s core_clks:%s\n",
		   str_on_off(ctrl->stream_clks_on),
		   str_on_off(ctrl->link_clks_on),
		   str_on_off(ctrl->core_clks_on));

	return 0;
}

static void msm_dp_ctrl_link_clk_disable(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	clk_bulk_disable_unprepare(ctrl->num_link_clks, ctrl->link_clks);

	ctrl->link_clks_on = false;

	drm_dbg_dp(ctrl->drm_dev, "disabled link clocks\n");
	drm_dbg_dp(ctrl->drm_dev, "stream_clks:%s link_clks:%s core_clks:%s\n",
		   str_on_off(ctrl->stream_clks_on),
		   str_on_off(ctrl->link_clks_on),
		   str_on_off(ctrl->core_clks_on));
}

static int msm_dp_ctrl_enable_mainlink_clocks(struct msm_dp_ctrl_private *ctrl)
{
	int ret = 0;
	struct phy *phy = ctrl->phy;
	const u8 *dpcd = ctrl->panel->dpcd;

	ctrl->phy_opts.dp.lanes = ctrl->link->link_params.num_lanes;
	ctrl->phy_opts.dp.link_rate = ctrl->link->link_params.rate / 100;
	ctrl->phy_opts.dp.ssc = drm_dp_max_downspread(dpcd);

	phy_configure(phy, &ctrl->phy_opts);
	phy_power_on(phy);

	dev_pm_opp_set_rate(ctrl->dev, ctrl->link->link_params.rate * 1000);
	ret = msm_dp_ctrl_link_clk_enable(&ctrl->msm_dp_ctrl);
	if (ret)
		DRM_ERROR("Unable to start link clocks. ret=%d\n", ret);

	drm_dbg_dp(ctrl->drm_dev, "link rate=%d\n", ctrl->link->link_params.rate);

	return ret;
}

void msm_dp_ctrl_reset_irq_ctrl(struct msm_dp_ctrl *msm_dp_ctrl, bool enable)
{
	struct msm_dp_ctrl_private *ctrl;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	msm_dp_catalog_ctrl_reset(ctrl->catalog);

	/*
	 * all dp controller programmable registers will not
	 * be reset to default value after DP_SW_RESET
	 * therefore interrupt mask bits have to be updated
	 * to enable/disable interrupts
	 */
	msm_dp_catalog_ctrl_enable_irq(ctrl->catalog, enable);
}

void msm_dp_ctrl_config_psr(struct msm_dp_ctrl *msm_dp_ctrl)
{
	u8 cfg;
	struct msm_dp_ctrl_private *ctrl = container_of(msm_dp_ctrl,
			struct msm_dp_ctrl_private, msm_dp_ctrl);

	if (!ctrl->panel->psr_cap.version)
		return;

	msm_dp_catalog_ctrl_config_psr(ctrl->catalog);

	cfg = DP_PSR_ENABLE;
	drm_dp_dpcd_write(ctrl->aux, DP_PSR_EN_CFG, &cfg, 1);
}

void msm_dp_ctrl_set_psr(struct msm_dp_ctrl *msm_dp_ctrl, bool enter)
{
	struct msm_dp_ctrl_private *ctrl = container_of(msm_dp_ctrl,
			struct msm_dp_ctrl_private, msm_dp_ctrl);

	if (!ctrl->panel->psr_cap.version)
		return;

	/*
	 * When entering PSR,
	 * 1. Send PSR enter SDP and wait for the PSR_UPDATE_INT
	 * 2. Turn off video
	 * 3. Disable the mainlink
	 *
	 * When exiting PSR,
	 * 1. Enable the mainlink
	 * 2. Send the PSR exit SDP
	 */
	if (enter) {
		reinit_completion(&ctrl->psr_op_comp);
		msm_dp_catalog_ctrl_set_psr(ctrl->catalog, true);

		if (!wait_for_completion_timeout(&ctrl->psr_op_comp,
			PSR_OPERATION_COMPLETION_TIMEOUT_JIFFIES)) {
			DRM_ERROR("PSR_ENTRY timedout\n");
			msm_dp_catalog_ctrl_set_psr(ctrl->catalog, false);
			return;
		}

		msm_dp_ctrl_push_idle(msm_dp_ctrl);
		msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);

		msm_dp_catalog_ctrl_psr_mainlink_enable(ctrl->catalog, false);
	} else {
		msm_dp_catalog_ctrl_psr_mainlink_enable(ctrl->catalog, true);

		msm_dp_catalog_ctrl_set_psr(ctrl->catalog, false);
		msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, DP_STATE_CTRL_SEND_VIDEO);
		msm_dp_ctrl_wait4video_ready(ctrl);
		msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);
	}
}

void msm_dp_ctrl_phy_init(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	struct phy *phy;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);
	phy = ctrl->phy;

	msm_dp_catalog_ctrl_phy_reset(ctrl->catalog);
	phy_init(phy);

	drm_dbg_dp(ctrl->drm_dev, "phy=%p init=%d power_on=%d\n",
			phy, phy->init_count, phy->power_count);
}

void msm_dp_ctrl_phy_exit(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	struct phy *phy;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);
	phy = ctrl->phy;

	msm_dp_catalog_ctrl_phy_reset(ctrl->catalog);
	phy_exit(phy);
	drm_dbg_dp(ctrl->drm_dev, "phy=%p init=%d power_on=%d\n",
			phy, phy->init_count, phy->power_count);
}

static int msm_dp_ctrl_reinitialize_mainlink(struct msm_dp_ctrl_private *ctrl)
{
	struct phy *phy = ctrl->phy;
	int ret = 0;

	msm_dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);
	ctrl->phy_opts.dp.lanes = ctrl->link->link_params.num_lanes;
	phy_configure(phy, &ctrl->phy_opts);
	/*
	 * Disable and re-enable the mainlink clock since the
	 * link clock might have been adjusted as part of the
	 * link maintenance.
	 */
	dev_pm_opp_set_rate(ctrl->dev, 0);

	msm_dp_ctrl_link_clk_disable(&ctrl->msm_dp_ctrl);

	phy_power_off(phy);
	/* hw recommended delay before re-enabling clocks */
	msleep(20);

	ret = msm_dp_ctrl_enable_mainlink_clocks(ctrl);
	if (ret) {
		DRM_ERROR("Failed to enable mainlink clks. ret=%d\n", ret);
		return ret;
	}

	return ret;
}

static int msm_dp_ctrl_deinitialize_mainlink(struct msm_dp_ctrl_private *ctrl)
{
	struct phy *phy;

	phy = ctrl->phy;

	msm_dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);

	msm_dp_catalog_ctrl_reset(ctrl->catalog);

	dev_pm_opp_set_rate(ctrl->dev, 0);
	msm_dp_ctrl_link_clk_disable(&ctrl->msm_dp_ctrl);

	phy_power_off(phy);

	/* aux channel down, reinit phy */
	phy_exit(phy);
	phy_init(phy);

	drm_dbg_dp(ctrl->drm_dev, "phy=%p init=%d power_on=%d\n",
			phy, phy->init_count, phy->power_count);
	return 0;
}

static int msm_dp_ctrl_link_maintenance(struct msm_dp_ctrl_private *ctrl)
{
	int ret = 0;
	int training_step = DP_TRAINING_NONE;

	msm_dp_ctrl_push_idle(&ctrl->msm_dp_ctrl);

	ctrl->link->phy_params.p_level = 0;
	ctrl->link->phy_params.v_level = 0;

	ret = msm_dp_ctrl_setup_main_link(ctrl, &training_step);
	if (ret)
		goto end;

	msm_dp_ctrl_clear_training_pattern(ctrl);

	msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, DP_STATE_CTRL_SEND_VIDEO);

	ret = msm_dp_ctrl_wait4video_ready(ctrl);
end:
	return ret;
}

static bool msm_dp_ctrl_send_phy_test_pattern(struct msm_dp_ctrl_private *ctrl)
{
	bool success = false;
	u32 pattern_sent = 0x0;
	u32 pattern_requested = ctrl->link->phy_params.phy_test_pattern_sel;

	drm_dbg_dp(ctrl->drm_dev, "request: 0x%x\n", pattern_requested);

	if (msm_dp_ctrl_set_vx_px(ctrl,
			ctrl->link->phy_params.v_level,
			ctrl->link->phy_params.p_level)) {
		DRM_ERROR("Failed to set v/p levels\n");
		return false;
	}
	msm_dp_catalog_ctrl_send_phy_pattern(ctrl->catalog, pattern_requested);
	msm_dp_ctrl_update_vx_px(ctrl);
	msm_dp_link_send_test_response(ctrl->link);

	pattern_sent = msm_dp_catalog_ctrl_read_phy_pattern(ctrl->catalog);

	switch (pattern_sent) {
	case MR_LINK_TRAINING1:
		success = (pattern_requested ==
				DP_PHY_TEST_PATTERN_D10_2);
		break;
	case MR_LINK_SYMBOL_ERM:
		success = ((pattern_requested ==
			DP_PHY_TEST_PATTERN_ERROR_COUNT) ||
				(pattern_requested ==
				DP_PHY_TEST_PATTERN_CP2520));
		break;
	case MR_LINK_PRBS7:
		success = (pattern_requested ==
				DP_PHY_TEST_PATTERN_PRBS7);
		break;
	case MR_LINK_CUSTOM80:
		success = (pattern_requested ==
				DP_PHY_TEST_PATTERN_80BIT_CUSTOM);
		break;
	case MR_LINK_TRAINING4:
		success = (pattern_requested ==
				DP_PHY_TEST_PATTERN_SEL_MASK);
		break;
	default:
		success = false;
	}

	drm_dbg_dp(ctrl->drm_dev, "%s: test->0x%x\n",
		success ? "success" : "failed", pattern_requested);
	return success;
}

static int msm_dp_ctrl_process_phy_test_request(struct msm_dp_ctrl_private *ctrl)
{
	int ret;
	unsigned long pixel_rate;

	if (!ctrl->link->phy_params.phy_test_pattern_sel) {
		drm_dbg_dp(ctrl->drm_dev,
			"no test pattern selected by sink\n");
		return 0;
	}

	/*
	 * The global reset will need DP link related clocks to be
	 * running. Add the global reset just before disabling the
	 * link clocks and core clocks.
	 */
	msm_dp_ctrl_off(&ctrl->msm_dp_ctrl);

	ret = msm_dp_ctrl_on_link(&ctrl->msm_dp_ctrl);
	if (ret) {
		DRM_ERROR("failed to enable DP link controller\n");
		return ret;
	}

	pixel_rate = ctrl->panel->msm_dp_mode.drm_mode.clock;
	ret = clk_set_rate(ctrl->pixel_clk, pixel_rate * 1000);
	if (ret) {
		DRM_ERROR("Failed to set pixel clock rate. ret=%d\n", ret);
		return ret;
	}

	if (ctrl->stream_clks_on) {
		drm_dbg_dp(ctrl->drm_dev, "pixel clks already enabled\n");
	} else {
		ret = clk_prepare_enable(ctrl->pixel_clk);
		if (ret) {
			DRM_ERROR("Failed to start pixel clocks. ret=%d\n", ret);
			return ret;
		}
		ctrl->stream_clks_on = true;
	}

	msm_dp_ctrl_send_phy_test_pattern(ctrl);

	return 0;
}

void msm_dp_ctrl_handle_sink_request(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	u32 sink_request = 0x0;

	if (!msm_dp_ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);
	sink_request = ctrl->link->sink_request;

	if (sink_request & DP_TEST_LINK_PHY_TEST_PATTERN) {
		drm_dbg_dp(ctrl->drm_dev, "PHY_TEST_PATTERN request\n");
		if (msm_dp_ctrl_process_phy_test_request(ctrl)) {
			DRM_ERROR("process phy_test_req failed\n");
			return;
		}
	}

	if (sink_request & DP_LINK_STATUS_UPDATED) {
		if (msm_dp_ctrl_link_maintenance(ctrl)) {
			DRM_ERROR("LM failed: TEST_LINK_TRAINING\n");
			return;
		}
	}

	if (sink_request & DP_TEST_LINK_TRAINING) {
		msm_dp_link_send_test_response(ctrl->link);
		if (msm_dp_ctrl_link_maintenance(ctrl)) {
			DRM_ERROR("LM failed: TEST_LINK_TRAINING\n");
			return;
		}
	}
}

static bool msm_dp_ctrl_clock_recovery_any_ok(
			const u8 link_status[DP_LINK_STATUS_SIZE],
			int lane_count)
{
	int reduced_cnt;

	if (lane_count <= 1)
		return false;

	/*
	 * only interested in the lane number after reduced
	 * lane_count = 4, then only interested in 2 lanes
	 * lane_count = 2, then only interested in 1 lane
	 */
	reduced_cnt = lane_count >> 1;

	return drm_dp_clock_recovery_ok(link_status, reduced_cnt);
}

static bool msm_dp_ctrl_channel_eq_ok(struct msm_dp_ctrl_private *ctrl)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	int num_lanes = ctrl->link->link_params.num_lanes;

	msm_dp_ctrl_read_link_status(ctrl, link_status);

	return drm_dp_channel_eq_ok(link_status, num_lanes);
}

int msm_dp_ctrl_on_link(struct msm_dp_ctrl *msm_dp_ctrl)
{
	int rc = 0;
	struct msm_dp_ctrl_private *ctrl;
	u32 rate;
	int link_train_max_retries = 5;
	u32 const phy_cts_pixel_clk_khz = 148500;
	u8 link_status[DP_LINK_STATUS_SIZE];
	unsigned int training_step;
	unsigned long pixel_rate;

	if (!msm_dp_ctrl)
		return -EINVAL;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	rate = ctrl->panel->link_info.rate;
	pixel_rate = ctrl->panel->msm_dp_mode.drm_mode.clock;

	msm_dp_ctrl_core_clk_enable(&ctrl->msm_dp_ctrl);

	if (ctrl->link->sink_request & DP_TEST_LINK_PHY_TEST_PATTERN) {
		drm_dbg_dp(ctrl->drm_dev,
				"using phy test link parameters\n");
		if (!pixel_rate)
			pixel_rate = phy_cts_pixel_clk_khz;
	} else {
		ctrl->link->link_params.rate = rate;
		ctrl->link->link_params.num_lanes =
			ctrl->panel->link_info.num_lanes;
		if (ctrl->panel->msm_dp_mode.out_fmt_is_yuv_420)
			pixel_rate >>= 1;
	}

	drm_dbg_dp(ctrl->drm_dev, "rate=%d, num_lanes=%d, pixel_rate=%lu\n",
		ctrl->link->link_params.rate, ctrl->link->link_params.num_lanes,
		pixel_rate);

	rc = msm_dp_ctrl_enable_mainlink_clocks(ctrl);
	if (rc)
		return rc;

	while (--link_train_max_retries) {
		training_step = DP_TRAINING_NONE;
		rc = msm_dp_ctrl_setup_main_link(ctrl, &training_step);
		if (rc == 0) {
			/* training completed successfully */
			break;
		} else if (training_step == DP_TRAINING_1) {
			/* link train_1 failed */
			if (!msm_dp_catalog_link_is_connected(ctrl->catalog))
				break;

			msm_dp_ctrl_read_link_status(ctrl, link_status);

			rc = msm_dp_ctrl_link_rate_down_shift(ctrl);
			if (rc < 0) { /* already in RBR = 1.6G */
				if (msm_dp_ctrl_clock_recovery_any_ok(link_status,
					ctrl->link->link_params.num_lanes)) {
					/*
					 * some lanes are ready,
					 * reduce lane number
					 */
					rc = msm_dp_ctrl_link_lane_down_shift(ctrl);
					if (rc < 0) { /* lane == 1 already */
						/* end with failure */
						break;
					}
				} else {
					/* end with failure */
					break; /* lane == 1 already */
				}
			}
		} else if (training_step == DP_TRAINING_2) {
			/* link train_2 failed */
			if (!msm_dp_catalog_link_is_connected(ctrl->catalog))
				break;

			msm_dp_ctrl_read_link_status(ctrl, link_status);

			if (!drm_dp_clock_recovery_ok(link_status,
					ctrl->link->link_params.num_lanes))
				rc = msm_dp_ctrl_link_rate_down_shift(ctrl);
			else
				rc = msm_dp_ctrl_link_lane_down_shift(ctrl);

			if (rc < 0) {
				/* end with failure */
				break; /* lane == 1 already */
			}

			/* stop link training before start re training  */
			msm_dp_ctrl_clear_training_pattern(ctrl);
		}

		rc = msm_dp_ctrl_reinitialize_mainlink(ctrl);
		if (rc) {
			DRM_ERROR("Failed to reinitialize mainlink. rc=%d\n", rc);
			break;
		}
	}

	if (ctrl->link->sink_request & DP_TEST_LINK_PHY_TEST_PATTERN)
		return rc;

	if (rc == 0) {  /* link train successfully */
		/*
		 * do not stop train pattern here
		 * stop link training at on_stream
		 * to pass compliance test
		 */
	} else  {
		/*
		 * link training failed
		 * end txing train pattern here
		 */
		msm_dp_ctrl_clear_training_pattern(ctrl);

		msm_dp_ctrl_deinitialize_mainlink(ctrl);
		rc = -ECONNRESET;
	}

	return rc;
}

static int msm_dp_ctrl_link_retrain(struct msm_dp_ctrl_private *ctrl)
{
	int training_step = DP_TRAINING_NONE;

	return msm_dp_ctrl_setup_main_link(ctrl, &training_step);
}

int msm_dp_ctrl_on_stream(struct msm_dp_ctrl *msm_dp_ctrl, bool force_link_train)
{
	int ret = 0;
	bool mainlink_ready = false;
	struct msm_dp_ctrl_private *ctrl;
	unsigned long pixel_rate;
	unsigned long pixel_rate_orig;

	if (!msm_dp_ctrl)
		return -EINVAL;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	pixel_rate = pixel_rate_orig = ctrl->panel->msm_dp_mode.drm_mode.clock;

	if (msm_dp_ctrl->wide_bus_en || ctrl->panel->msm_dp_mode.out_fmt_is_yuv_420)
		pixel_rate >>= 1;

	drm_dbg_dp(ctrl->drm_dev, "rate=%d, num_lanes=%d, pixel_rate=%lu\n",
		ctrl->link->link_params.rate,
		ctrl->link->link_params.num_lanes, pixel_rate);

	drm_dbg_dp(ctrl->drm_dev,
		"core_clk_on=%d link_clk_on=%d stream_clk_on=%d\n",
		ctrl->core_clks_on, ctrl->link_clks_on, ctrl->stream_clks_on);

	if (!ctrl->link_clks_on) { /* link clk is off */
		ret = msm_dp_ctrl_enable_mainlink_clocks(ctrl);
		if (ret) {
			DRM_ERROR("Failed to start link clocks. ret=%d\n", ret);
			goto end;
		}
	}

	ret = clk_set_rate(ctrl->pixel_clk, pixel_rate * 1000);
	if (ret) {
		DRM_ERROR("Failed to set pixel clock rate. ret=%d\n", ret);
		goto end;
	}

	if (ctrl->stream_clks_on) {
		drm_dbg_dp(ctrl->drm_dev, "pixel clks already enabled\n");
	} else {
		ret = clk_prepare_enable(ctrl->pixel_clk);
		if (ret) {
			DRM_ERROR("Failed to start pixel clocks. ret=%d\n", ret);
			goto end;
		}
		ctrl->stream_clks_on = true;
	}

	if (force_link_train || !msm_dp_ctrl_channel_eq_ok(ctrl))
		msm_dp_ctrl_link_retrain(ctrl);

	/* stop txing train pattern to end link training */
	msm_dp_ctrl_clear_training_pattern(ctrl);

	/*
	 * Set up transfer unit values and set controller state to send
	 * video.
	 */
	reinit_completion(&ctrl->video_comp);

	msm_dp_ctrl_configure_source_params(ctrl);

	msm_dp_catalog_ctrl_config_msa(ctrl->catalog,
		ctrl->link->link_params.rate,
		pixel_rate_orig,
		ctrl->panel->msm_dp_mode.out_fmt_is_yuv_420);

	msm_dp_ctrl_setup_tr_unit(ctrl);

	msm_dp_catalog_ctrl_state_ctrl(ctrl->catalog, DP_STATE_CTRL_SEND_VIDEO);

	ret = msm_dp_ctrl_wait4video_ready(ctrl);
	if (ret)
		return ret;

	mainlink_ready = msm_dp_catalog_ctrl_mainlink_ready(ctrl->catalog);
	drm_dbg_dp(ctrl->drm_dev,
		"mainlink %s\n", mainlink_ready ? "READY" : "NOT READY");

end:
	return ret;
}

void msm_dp_ctrl_off_link_stream(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	struct phy *phy;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);
	phy = ctrl->phy;

	msm_dp_catalog_panel_disable_vsc_sdp(ctrl->catalog);

	/* set dongle to D3 (power off) mode */
	msm_dp_link_psm_config(ctrl->link, &ctrl->panel->link_info, true);

	msm_dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);

	if (ctrl->stream_clks_on) {
		clk_disable_unprepare(ctrl->pixel_clk);
		ctrl->stream_clks_on = false;
	}

	dev_pm_opp_set_rate(ctrl->dev, 0);
	msm_dp_ctrl_link_clk_disable(&ctrl->msm_dp_ctrl);

	phy_power_off(phy);

	/* aux channel down, reinit phy */
	phy_exit(phy);
	phy_init(phy);

	drm_dbg_dp(ctrl->drm_dev, "phy=%p init=%d power_on=%d\n",
			phy, phy->init_count, phy->power_count);
}

void msm_dp_ctrl_off_link(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	struct phy *phy;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);
	phy = ctrl->phy;

	msm_dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);

	dev_pm_opp_set_rate(ctrl->dev, 0);
	msm_dp_ctrl_link_clk_disable(&ctrl->msm_dp_ctrl);

	DRM_DEBUG_DP("Before, phy=%p init_count=%d power_on=%d\n",
		phy, phy->init_count, phy->power_count);

	phy_power_off(phy);

	DRM_DEBUG_DP("After, phy=%p init_count=%d power_on=%d\n",
		phy, phy->init_count, phy->power_count);
}

void msm_dp_ctrl_off(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	struct phy *phy;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);
	phy = ctrl->phy;

	msm_dp_catalog_panel_disable_vsc_sdp(ctrl->catalog);

	msm_dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);

	msm_dp_catalog_ctrl_reset(ctrl->catalog);

	if (ctrl->stream_clks_on) {
		clk_disable_unprepare(ctrl->pixel_clk);
		ctrl->stream_clks_on = false;
	}

	dev_pm_opp_set_rate(ctrl->dev, 0);
	msm_dp_ctrl_link_clk_disable(&ctrl->msm_dp_ctrl);

	phy_power_off(phy);
	drm_dbg_dp(ctrl->drm_dev, "phy=%p init=%d power_on=%d\n",
			phy, phy->init_count, phy->power_count);
}

irqreturn_t msm_dp_ctrl_isr(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	u32 isr;
	irqreturn_t ret = IRQ_NONE;

	if (!msm_dp_ctrl)
		return IRQ_NONE;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);

	if (ctrl->panel->psr_cap.version) {
		isr = msm_dp_catalog_ctrl_read_psr_interrupt_status(ctrl->catalog);

		if (isr)
			complete(&ctrl->psr_op_comp);

		if (isr & PSR_EXIT_INT)
			drm_dbg_dp(ctrl->drm_dev, "PSR exit done\n");

		if (isr & PSR_UPDATE_INT)
			drm_dbg_dp(ctrl->drm_dev, "PSR frame update done\n");

		if (isr & PSR_CAPTURE_INT)
			drm_dbg_dp(ctrl->drm_dev, "PSR frame capture done\n");
	}

	isr = msm_dp_catalog_ctrl_get_interrupt(ctrl->catalog);


	if (isr & DP_CTRL_INTR_READY_FOR_VIDEO) {
		drm_dbg_dp(ctrl->drm_dev, "dp_video_ready\n");
		complete(&ctrl->video_comp);
		ret = IRQ_HANDLED;
	}

	if (isr & DP_CTRL_INTR_IDLE_PATTERN_SENT) {
		drm_dbg_dp(ctrl->drm_dev, "idle_patterns_sent\n");
		complete(&ctrl->idle_comp);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static const char *core_clks[] = {
	"core_iface",
	"core_aux",
};

static const char *ctrl_clks[] = {
	"ctrl_link",
	"ctrl_link_iface",
};

static int msm_dp_ctrl_clk_init(struct msm_dp_ctrl *msm_dp_ctrl)
{
	struct msm_dp_ctrl_private *ctrl;
	struct device *dev;
	int i, rc;

	ctrl = container_of(msm_dp_ctrl, struct msm_dp_ctrl_private, msm_dp_ctrl);
	dev = ctrl->dev;

	ctrl->num_core_clks = ARRAY_SIZE(core_clks);
	ctrl->core_clks = devm_kcalloc(dev, ctrl->num_core_clks, sizeof(*ctrl->core_clks), GFP_KERNEL);
	if (!ctrl->core_clks)
		return -ENOMEM;

	for (i = 0; i < ctrl->num_core_clks; i++)
		ctrl->core_clks[i].id = core_clks[i];

	rc = devm_clk_bulk_get(dev, ctrl->num_core_clks, ctrl->core_clks);
	if (rc)
		return rc;

	ctrl->num_link_clks = ARRAY_SIZE(ctrl_clks);
	ctrl->link_clks = devm_kcalloc(dev, ctrl->num_link_clks, sizeof(*ctrl->link_clks), GFP_KERNEL);
	if (!ctrl->link_clks)
		return -ENOMEM;

	for (i = 0; i < ctrl->num_link_clks; i++)
		ctrl->link_clks[i].id = ctrl_clks[i];

	rc = devm_clk_bulk_get(dev, ctrl->num_link_clks, ctrl->link_clks);
	if (rc)
		return rc;

	ctrl->pixel_clk = devm_clk_get(dev, "stream_pixel");
	if (IS_ERR(ctrl->pixel_clk))
		return PTR_ERR(ctrl->pixel_clk);

	return 0;
}

struct msm_dp_ctrl *msm_dp_ctrl_get(struct device *dev, struct msm_dp_link *link,
			struct msm_dp_panel *panel,	struct drm_dp_aux *aux,
			struct msm_dp_catalog *catalog,
			struct phy *phy)
{
	struct msm_dp_ctrl_private *ctrl;
	int ret;

	if (!dev || !panel || !aux ||
	    !link || !catalog) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		DRM_ERROR("Mem allocation failure\n");
		return ERR_PTR(-ENOMEM);
	}

	ret = devm_pm_opp_set_clkname(dev, "ctrl_link");
	if (ret) {
		dev_err(dev, "invalid DP OPP table in device tree\n");
		/* caller do PTR_ERR(opp_table) */
		return (struct msm_dp_ctrl *)ERR_PTR(ret);
	}

	/* OPP table is optional */
	ret = devm_pm_opp_of_add_table(dev);
	if (ret)
		dev_err(dev, "failed to add DP OPP table\n");

	init_completion(&ctrl->idle_comp);
	init_completion(&ctrl->psr_op_comp);
	init_completion(&ctrl->video_comp);

	/* in parameters */
	ctrl->panel    = panel;
	ctrl->aux      = aux;
	ctrl->link     = link;
	ctrl->catalog  = catalog;
	ctrl->dev      = dev;
	ctrl->phy      = phy;

	ret = msm_dp_ctrl_clk_init(&ctrl->msm_dp_ctrl);
	if (ret) {
		dev_err(dev, "failed to init clocks\n");
		return ERR_PTR(ret);
	}

	return &ctrl->msm_dp_ctrl;
}
