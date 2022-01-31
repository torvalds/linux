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
#include <drm/drm_fixed.h>
#include <drm/dp/drm_dp_helper.h>
#include <drm/drm_print.h>

#include "dp_reg.h"
#include "dp_ctrl.h"
#include "dp_link.h"

#define DP_KHZ_TO_HZ 1000
#define IDLE_PATTERN_COMPLETION_TIMEOUT_JIFFIES	(30 * HZ / 1000) /* 30 ms */
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

struct dp_tu_calc_input {
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

struct dp_vc_tu_mapping_table {
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

struct dp_ctrl_private {
	struct dp_ctrl dp_ctrl;
	struct device *dev;
	struct drm_dp_aux *aux;
	struct dp_panel *panel;
	struct dp_link *link;
	struct dp_power *power;
	struct dp_parser *parser;
	struct dp_catalog *catalog;

	struct completion idle_comp;
	struct completion video_comp;
};

static int dp_aux_link_configure(struct drm_dp_aux *aux,
					struct dp_link_info *link)
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

void dp_ctrl_push_idle(struct dp_ctrl *dp_ctrl)
{
	struct dp_ctrl_private *ctrl;

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);

	reinit_completion(&ctrl->idle_comp);
	dp_catalog_ctrl_state_ctrl(ctrl->catalog, DP_STATE_CTRL_PUSH_IDLE);

	if (!wait_for_completion_timeout(&ctrl->idle_comp,
			IDLE_PATTERN_COMPLETION_TIMEOUT_JIFFIES))
		pr_warn("PUSH_IDLE pattern timedout\n");

	DRM_DEBUG_DP("mainlink off done\n");
}

static void dp_ctrl_config_ctrl(struct dp_ctrl_private *ctrl)
{
	u32 config = 0, tbd;
	const u8 *dpcd = ctrl->panel->dpcd;

	/* Default-> LSCLK DIV: 1/4 LCLK  */
	config |= (2 << DP_CONFIGURATION_CTRL_LSCLK_DIV_SHIFT);

	/* Scrambler reset enable */
	if (drm_dp_alternate_scrambler_reset_cap(dpcd))
		config |= DP_CONFIGURATION_CTRL_ASSR;

	tbd = dp_link_get_test_bits_depth(ctrl->link,
			ctrl->panel->dp_mode.bpp);

	if (tbd == DP_TEST_BIT_DEPTH_UNKNOWN) {
		pr_debug("BIT_DEPTH not set. Configure default\n");
		tbd = DP_TEST_BIT_DEPTH_8;
	}

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

	dp_catalog_ctrl_config_ctrl(ctrl->catalog, config);
}

static void dp_ctrl_configure_source_params(struct dp_ctrl_private *ctrl)
{
	u32 cc, tb;

	dp_catalog_ctrl_lane_mapping(ctrl->catalog);
	dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, true);

	dp_ctrl_config_ctrl(ctrl);

	tb = dp_link_get_test_bits_depth(ctrl->link,
		ctrl->panel->dp_mode.bpp);
	cc = dp_link_get_colorimetry_config(ctrl->link);
	dp_catalog_ctrl_config_misc(ctrl->catalog, cc, tb);
	dp_panel_timing_cfg(ctrl->panel);
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

static void dp_panel_update_tu_timings(struct dp_tu_calc_input *in,
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

static void _dp_ctrl_calc_tu(struct dp_tu_calc_input *in,
				   struct dp_vc_tu_mapping_table *tu_table)
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

	dp_panel_update_tu_timings(in, tu);

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
		DRM_DEBUG_DP("Info: increase HBLANK_MARGIN to %d\n",
				HBLANK_MARGIN);
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
	DRM_DEBUG_DP("Info: n_sym = %d, num_of_tus = %d\n",
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

	DRM_DEBUG_DP("TU: valid_boundary_link: %d\n",
				tu_table->valid_boundary_link);
	DRM_DEBUG_DP("TU: delay_start_link: %d\n",
				tu_table->delay_start_link);
	DRM_DEBUG_DP("TU: boundary_moderation_en: %d\n",
			tu_table->boundary_moderation_en);
	DRM_DEBUG_DP("TU: valid_lower_boundary_link: %d\n",
			tu_table->valid_lower_boundary_link);
	DRM_DEBUG_DP("TU: upper_boundary_count: %d\n",
			tu_table->upper_boundary_count);
	DRM_DEBUG_DP("TU: lower_boundary_count: %d\n",
			tu_table->lower_boundary_count);
	DRM_DEBUG_DP("TU: tu_size_minus1: %d\n", tu_table->tu_size_minus1);

	kfree(tu);
}

static void dp_ctrl_calc_tu_parameters(struct dp_ctrl_private *ctrl,
		struct dp_vc_tu_mapping_table *tu_table)
{
	struct dp_tu_calc_input in;
	struct drm_display_mode *drm_mode;

	drm_mode = &ctrl->panel->dp_mode.drm_mode;

	in.lclk = ctrl->link->link_params.rate / 1000;
	in.pclk_khz = drm_mode->clock;
	in.hactive = drm_mode->hdisplay;
	in.hporch = drm_mode->htotal - drm_mode->hdisplay;
	in.nlanes = ctrl->link->link_params.num_lanes;
	in.bpp = ctrl->panel->dp_mode.bpp;
	in.pixel_enc = 444;
	in.dsc_en = 0;
	in.async_en = 0;
	in.fec_en = 0;
	in.num_of_dsc_slices = 0;
	in.compress_ratio = 100;

	_dp_ctrl_calc_tu(&in, tu_table);
}

static void dp_ctrl_setup_tr_unit(struct dp_ctrl_private *ctrl)
{
	u32 dp_tu = 0x0;
	u32 valid_boundary = 0x0;
	u32 valid_boundary2 = 0x0;
	struct dp_vc_tu_mapping_table tu_calc_table;

	dp_ctrl_calc_tu_parameters(ctrl, &tu_calc_table);

	dp_tu |= tu_calc_table.tu_size_minus1;
	valid_boundary |= tu_calc_table.valid_boundary_link;
	valid_boundary |= (tu_calc_table.delay_start_link << 16);

	valid_boundary2 |= (tu_calc_table.valid_lower_boundary_link << 1);
	valid_boundary2 |= (tu_calc_table.upper_boundary_count << 16);
	valid_boundary2 |= (tu_calc_table.lower_boundary_count << 20);

	if (tu_calc_table.boundary_moderation_en)
		valid_boundary2 |= BIT(0);

	pr_debug("dp_tu=0x%x, valid_boundary=0x%x, valid_boundary2=0x%x\n",
			dp_tu, valid_boundary, valid_boundary2);

	dp_catalog_ctrl_update_transfer_unit(ctrl->catalog,
				dp_tu, valid_boundary, valid_boundary2);
}

static int dp_ctrl_wait4video_ready(struct dp_ctrl_private *ctrl)
{
	int ret = 0;

	if (!wait_for_completion_timeout(&ctrl->video_comp,
				WAIT_FOR_VIDEO_READY_TIMEOUT_JIFFIES)) {
		DRM_ERROR("wait4video timedout\n");
		ret = -ETIMEDOUT;
	}
	return ret;
}

static int dp_ctrl_update_vx_px(struct dp_ctrl_private *ctrl)
{
	struct dp_link *link = ctrl->link;
	int ret = 0, lane, lane_cnt;
	u8 buf[4];
	u32 max_level_reached = 0;
	u32 voltage_swing_level = link->phy_params.v_level;
	u32 pre_emphasis_level = link->phy_params.p_level;

	DRM_DEBUG_DP("voltage level: %d emphasis level: %d\n", voltage_swing_level,
			pre_emphasis_level);
	ret = dp_catalog_ctrl_update_vx_px(ctrl->catalog,
		voltage_swing_level, pre_emphasis_level);

	if (ret)
		return ret;

	if (voltage_swing_level >= DP_TRAIN_VOLTAGE_SWING_MAX) {
		DRM_DEBUG_DP("max. voltage swing level reached %d\n",
				voltage_swing_level);
		max_level_reached |= DP_TRAIN_MAX_SWING_REACHED;
	}

	if (pre_emphasis_level >= DP_TRAIN_PRE_EMPHASIS_MAX) {
		DRM_DEBUG_DP("max. pre-emphasis level reached %d\n",
				pre_emphasis_level);
		max_level_reached  |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
	}

	pre_emphasis_level <<= DP_TRAIN_PRE_EMPHASIS_SHIFT;

	lane_cnt = ctrl->link->link_params.num_lanes;
	for (lane = 0; lane < lane_cnt; lane++)
		buf[lane] = voltage_swing_level | pre_emphasis_level
				| max_level_reached;

	DRM_DEBUG_DP("sink: p|v=0x%x\n", voltage_swing_level
					| pre_emphasis_level);
	ret = drm_dp_dpcd_write(ctrl->aux, DP_TRAINING_LANE0_SET,
					buf, lane_cnt);
	if (ret == lane_cnt)
		ret = 0;

	return ret;
}

static bool dp_ctrl_train_pattern_set(struct dp_ctrl_private *ctrl,
		u8 pattern)
{
	u8 buf;
	int ret = 0;

	DRM_DEBUG_DP("sink: pattern=%x\n", pattern);

	buf = pattern;

	if (pattern && pattern != DP_TRAINING_PATTERN_4)
		buf |= DP_LINK_SCRAMBLING_DISABLE;

	ret = drm_dp_dpcd_writeb(ctrl->aux, DP_TRAINING_PATTERN_SET, buf);
	return ret == 1;
}

static int dp_ctrl_read_link_status(struct dp_ctrl_private *ctrl,
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

static int dp_ctrl_link_train_1(struct dp_ctrl_private *ctrl,
			int *training_step)
{
	int tries, old_v_level, ret = 0;
	u8 link_status[DP_LINK_STATUS_SIZE];
	int const maximum_retries = 4;

	dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);

	*training_step = DP_TRAINING_1;

	ret = dp_catalog_ctrl_set_pattern(ctrl->catalog, DP_TRAINING_PATTERN_1);
	if (ret)
		return ret;
	dp_ctrl_train_pattern_set(ctrl, DP_TRAINING_PATTERN_1 |
		DP_LINK_SCRAMBLING_DISABLE);

	ret = dp_ctrl_update_vx_px(ctrl);
	if (ret)
		return ret;

	tries = 0;
	old_v_level = ctrl->link->phy_params.v_level;
	for (tries = 0; tries < maximum_retries; tries++) {
		drm_dp_link_train_clock_recovery_delay(ctrl->aux, ctrl->panel->dpcd);

		ret = dp_ctrl_read_link_status(ctrl, link_status);
		if (ret)
			return ret;

		if (drm_dp_clock_recovery_ok(link_status,
			ctrl->link->link_params.num_lanes)) {
			return 0;
		}

		if (ctrl->link->phy_params.v_level >=
			DP_TRAIN_VOLTAGE_SWING_MAX) {
			DRM_ERROR_RATELIMITED("max v_level reached\n");
			return -EAGAIN;
		}

		if (old_v_level != ctrl->link->phy_params.v_level) {
			tries = 0;
			old_v_level = ctrl->link->phy_params.v_level;
		}

		DRM_DEBUG_DP("clock recovery not done, adjusting vx px\n");

		dp_link_adjust_levels(ctrl->link, link_status);
		ret = dp_ctrl_update_vx_px(ctrl);
		if (ret)
			return ret;
	}

	DRM_ERROR("max tries reached\n");
	return -ETIMEDOUT;
}

static int dp_ctrl_link_rate_down_shift(struct dp_ctrl_private *ctrl)
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

	if (!ret)
		DRM_DEBUG_DP("new rate=0x%x\n", ctrl->link->link_params.rate);

	return ret;
}

static int dp_ctrl_link_lane_down_shift(struct dp_ctrl_private *ctrl)
{

	if (ctrl->link->link_params.num_lanes == 1)
		return -1;

	ctrl->link->link_params.num_lanes /= 2;
	ctrl->link->link_params.rate = ctrl->panel->link_info.rate;

	ctrl->link->phy_params.p_level = 0;
	ctrl->link->phy_params.v_level = 0;

	return 0;
}

static void dp_ctrl_clear_training_pattern(struct dp_ctrl_private *ctrl)
{
	dp_ctrl_train_pattern_set(ctrl, DP_TRAINING_PATTERN_DISABLE);
	drm_dp_link_train_channel_eq_delay(ctrl->aux, ctrl->panel->dpcd);
}

static int dp_ctrl_link_train_2(struct dp_ctrl_private *ctrl,
			int *training_step)
{
	int tries = 0, ret = 0;
	char pattern;
	int const maximum_retries = 5;
	u8 link_status[DP_LINK_STATUS_SIZE];

	dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);

	*training_step = DP_TRAINING_2;

	if (drm_dp_tps3_supported(ctrl->panel->dpcd))
		pattern = DP_TRAINING_PATTERN_3;
	else
		pattern = DP_TRAINING_PATTERN_2;

	ret = dp_catalog_ctrl_set_pattern(ctrl->catalog, pattern);
	if (ret)
		return ret;

	dp_ctrl_train_pattern_set(ctrl, pattern | DP_RECOVERED_CLOCK_OUT_EN);

	for (tries = 0; tries <= maximum_retries; tries++) {
		drm_dp_link_train_channel_eq_delay(ctrl->aux, ctrl->panel->dpcd);

		ret = dp_ctrl_read_link_status(ctrl, link_status);
		if (ret)
			return ret;

		if (drm_dp_channel_eq_ok(link_status,
			ctrl->link->link_params.num_lanes)) {
			return 0;
		}

		dp_link_adjust_levels(ctrl->link, link_status);
		ret = dp_ctrl_update_vx_px(ctrl);
		if (ret)
			return ret;

	}

	return -ETIMEDOUT;
}

static int dp_ctrl_reinitialize_mainlink(struct dp_ctrl_private *ctrl);

static int dp_ctrl_link_train(struct dp_ctrl_private *ctrl,
			int *training_step)
{
	int ret = 0;
	const u8 *dpcd = ctrl->panel->dpcd;
	u8 encoding = DP_SET_ANSI_8B10B;
	u8 ssc;
	u8 assr;
	struct dp_link_info link_info = {0};

	dp_ctrl_config_ctrl(ctrl);

	link_info.num_lanes = ctrl->link->link_params.num_lanes;
	link_info.rate = ctrl->link->link_params.rate;
	link_info.capabilities = DP_LINK_CAP_ENHANCED_FRAMING;

	dp_aux_link_configure(ctrl->aux, &link_info);

	if (drm_dp_max_downspread(dpcd)) {
		ssc = DP_SPREAD_AMP_0_5;
		drm_dp_dpcd_write(ctrl->aux, DP_DOWNSPREAD_CTRL, &ssc, 1);
	}

	drm_dp_dpcd_write(ctrl->aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				&encoding, 1);

	if (drm_dp_alternate_scrambler_reset_cap(dpcd)) {
		assr = DP_ALTERNATE_SCRAMBLER_RESET_ENABLE;
		drm_dp_dpcd_write(ctrl->aux, DP_EDP_CONFIGURATION_SET,
				&assr, 1);
	}

	ret = dp_ctrl_link_train_1(ctrl, training_step);
	if (ret) {
		DRM_ERROR("link training #1 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	DRM_DEBUG_DP("link training #1 successful\n");

	ret = dp_ctrl_link_train_2(ctrl, training_step);
	if (ret) {
		DRM_ERROR("link training #2 failed. ret=%d\n", ret);
		goto end;
	}

	/* print success info as this is a result of user initiated action */
	DRM_DEBUG_DP("link training #2 successful\n");

end:
	dp_catalog_ctrl_state_ctrl(ctrl->catalog, 0);

	return ret;
}

static int dp_ctrl_setup_main_link(struct dp_ctrl_private *ctrl,
			int *training_step)
{
	int ret = 0;

	dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, true);

	if (ctrl->link->sink_request & DP_TEST_LINK_PHY_TEST_PATTERN)
		return ret;

	/*
	 * As part of previous calls, DP controller state might have
	 * transitioned to PUSH_IDLE. In order to start transmitting
	 * a link training pattern, we have to first do soft reset.
	 */

	ret = dp_ctrl_link_train(ctrl, training_step);

	return ret;
}

static void dp_ctrl_set_clock_rate(struct dp_ctrl_private *ctrl,
			enum dp_pm_type module, char *name, unsigned long rate)
{
	u32 num = ctrl->parser->mp[module].num_clk;
	struct dss_clk *cfg = ctrl->parser->mp[module].clk_config;

	while (num && strcmp(cfg->clk_name, name)) {
		num--;
		cfg++;
	}

	DRM_DEBUG_DP("setting rate=%lu on clk=%s\n", rate, name);

	if (num)
		cfg->rate = rate;
	else
		DRM_ERROR("%s clock doesn't exit to set rate %lu\n",
				name, rate);
}

static int dp_ctrl_enable_mainlink_clocks(struct dp_ctrl_private *ctrl)
{
	int ret = 0;
	struct dp_io *dp_io = &ctrl->parser->io;
	struct phy *phy = dp_io->phy;
	struct phy_configure_opts_dp *opts_dp = &dp_io->phy_opts.dp;
	const u8 *dpcd = ctrl->panel->dpcd;

	opts_dp->lanes = ctrl->link->link_params.num_lanes;
	opts_dp->link_rate = ctrl->link->link_params.rate / 100;
	opts_dp->ssc = drm_dp_max_downspread(dpcd);
	dp_ctrl_set_clock_rate(ctrl, DP_CTRL_PM, "ctrl_link",
					ctrl->link->link_params.rate * 1000);

	phy_configure(phy, &dp_io->phy_opts);
	phy_power_on(phy);

	ret = dp_power_clk_enable(ctrl->power, DP_CTRL_PM, true);
	if (ret)
		DRM_ERROR("Unable to start link clocks. ret=%d\n", ret);

	DRM_DEBUG_DP("link rate=%d pixel_clk=%d\n",
		ctrl->link->link_params.rate, ctrl->dp_ctrl.pixel_rate);

	return ret;
}

static int dp_ctrl_enable_stream_clocks(struct dp_ctrl_private *ctrl)
{
	int ret = 0;

	dp_ctrl_set_clock_rate(ctrl, DP_STREAM_PM, "stream_pixel",
					ctrl->dp_ctrl.pixel_rate * 1000);

	ret = dp_power_clk_enable(ctrl->power, DP_STREAM_PM, true);
	if (ret)
		DRM_ERROR("Unabled to start pixel clocks. ret=%d\n", ret);

	DRM_DEBUG_DP("link rate=%d pixel_clk=%d\n",
			ctrl->link->link_params.rate, ctrl->dp_ctrl.pixel_rate);

	return ret;
}

int dp_ctrl_host_init(struct dp_ctrl *dp_ctrl, bool flip, bool reset)
{
	struct dp_ctrl_private *ctrl;
	struct dp_io *dp_io;
	struct phy *phy;

	if (!dp_ctrl) {
		DRM_ERROR("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);
	dp_io = &ctrl->parser->io;
	phy = dp_io->phy;

	ctrl->dp_ctrl.orientation = flip;

	if (reset)
		dp_catalog_ctrl_reset(ctrl->catalog);

	DRM_DEBUG_DP("flip=%d\n", flip);
	dp_catalog_ctrl_phy_reset(ctrl->catalog);
	phy_init(phy);
	dp_catalog_ctrl_enable_irq(ctrl->catalog, true);

	return 0;
}

/**
 * dp_ctrl_host_deinit() - Uninitialize DP controller
 * @dp_ctrl: Display Port Driver data
 *
 * Perform required steps to uninitialize DP controller
 * and its resources.
 */
void dp_ctrl_host_deinit(struct dp_ctrl *dp_ctrl)
{
	struct dp_ctrl_private *ctrl;
	struct dp_io *dp_io;
	struct phy *phy;

	if (!dp_ctrl) {
		DRM_ERROR("Invalid input data\n");
		return;
	}

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);
	dp_io = &ctrl->parser->io;
	phy = dp_io->phy;

	dp_catalog_ctrl_enable_irq(ctrl->catalog, false);
	phy_exit(phy);

	DRM_DEBUG_DP("Host deinitialized successfully\n");
}

static bool dp_ctrl_use_fixed_nvid(struct dp_ctrl_private *ctrl)
{
	const u8 *dpcd = ctrl->panel->dpcd;

	/*
	 * For better interop experience, used a fixed NVID=0x8000
	 * whenever connected to a VGA dongle downstream.
	 */
	if (drm_dp_is_branch(dpcd))
		return (drm_dp_has_quirk(&ctrl->panel->desc,
					 DP_DPCD_QUIRK_CONSTANT_N));

	return false;
}

static int dp_ctrl_reinitialize_mainlink(struct dp_ctrl_private *ctrl)
{
	int ret = 0;
	struct dp_io *dp_io = &ctrl->parser->io;
	struct phy *phy = dp_io->phy;
	struct phy_configure_opts_dp *opts_dp = &dp_io->phy_opts.dp;

	dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);
	opts_dp->lanes = ctrl->link->link_params.num_lanes;
	phy_configure(phy, &dp_io->phy_opts);
	/*
	 * Disable and re-enable the mainlink clock since the
	 * link clock might have been adjusted as part of the
	 * link maintenance.
	 */
	ret = dp_power_clk_enable(ctrl->power, DP_CTRL_PM, false);
	if (ret) {
		DRM_ERROR("Failed to disable clocks. ret=%d\n", ret);
		return ret;
	}
	phy_power_off(phy);
	/* hw recommended delay before re-enabling clocks */
	msleep(20);

	ret = dp_ctrl_enable_mainlink_clocks(ctrl);
	if (ret) {
		DRM_ERROR("Failed to enable mainlink clks. ret=%d\n", ret);
		return ret;
	}

	return ret;
}

static int dp_ctrl_deinitialize_mainlink(struct dp_ctrl_private *ctrl)
{
	struct dp_io *dp_io;
	struct phy *phy;
	int ret;

	dp_io = &ctrl->parser->io;
	phy = dp_io->phy;

	dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);

	dp_catalog_ctrl_reset(ctrl->catalog);

	ret = dp_power_clk_enable(ctrl->power, DP_CTRL_PM, false);
	if (ret) {
		DRM_ERROR("Failed to disable link clocks. ret=%d\n", ret);
	}

	phy_power_off(phy);
	phy_exit(phy);

	return 0;
}

static int dp_ctrl_link_maintenance(struct dp_ctrl_private *ctrl)
{
	int ret = 0;
	int training_step = DP_TRAINING_NONE;

	dp_ctrl_push_idle(&ctrl->dp_ctrl);

	ctrl->link->phy_params.p_level = 0;
	ctrl->link->phy_params.v_level = 0;

	ctrl->dp_ctrl.pixel_rate = ctrl->panel->dp_mode.drm_mode.clock;

	ret = dp_ctrl_setup_main_link(ctrl, &training_step);
	if (ret)
		goto end;

	dp_ctrl_clear_training_pattern(ctrl);

	dp_catalog_ctrl_state_ctrl(ctrl->catalog, DP_STATE_CTRL_SEND_VIDEO);

	ret = dp_ctrl_wait4video_ready(ctrl);
end:
	return ret;
}

static int dp_ctrl_process_phy_test_request(struct dp_ctrl_private *ctrl)
{
	int ret = 0;

	if (!ctrl->link->phy_params.phy_test_pattern_sel) {
		DRM_DEBUG_DP("no test pattern selected by sink\n");
		return ret;
	}

	/*
	 * The global reset will need DP link related clocks to be
	 * running. Add the global reset just before disabling the
	 * link clocks and core clocks.
	 */
	ret = dp_ctrl_off_link_stream(&ctrl->dp_ctrl);
	if (ret) {
		DRM_ERROR("failed to disable DP controller\n");
		return ret;
	}

	ret = dp_ctrl_on_link(&ctrl->dp_ctrl);
	if (!ret)
		ret = dp_ctrl_on_stream(&ctrl->dp_ctrl);
	else
		DRM_ERROR("failed to enable DP link controller\n");

	return ret;
}

static bool dp_ctrl_send_phy_test_pattern(struct dp_ctrl_private *ctrl)
{
	bool success = false;
	u32 pattern_sent = 0x0;
	u32 pattern_requested = ctrl->link->phy_params.phy_test_pattern_sel;

	DRM_DEBUG_DP("request: 0x%x\n", pattern_requested);

	if (dp_catalog_ctrl_update_vx_px(ctrl->catalog,
			ctrl->link->phy_params.v_level,
			ctrl->link->phy_params.p_level)) {
		DRM_ERROR("Failed to set v/p levels\n");
		return false;
	}
	dp_catalog_ctrl_send_phy_pattern(ctrl->catalog, pattern_requested);
	dp_ctrl_update_vx_px(ctrl);
	dp_link_send_test_response(ctrl->link);

	pattern_sent = dp_catalog_ctrl_read_phy_pattern(ctrl->catalog);

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

	DRM_DEBUG_DP("%s: test->0x%x\n", success ? "success" : "failed",
						pattern_requested);
	return success;
}

void dp_ctrl_handle_sink_request(struct dp_ctrl *dp_ctrl)
{
	struct dp_ctrl_private *ctrl;
	u32 sink_request = 0x0;

	if (!dp_ctrl) {
		DRM_ERROR("invalid input\n");
		return;
	}

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);
	sink_request = ctrl->link->sink_request;

	if (sink_request & DP_TEST_LINK_PHY_TEST_PATTERN) {
		DRM_DEBUG_DP("PHY_TEST_PATTERN request\n");
		if (dp_ctrl_process_phy_test_request(ctrl)) {
			DRM_ERROR("process phy_test_req failed\n");
			return;
		}
	}

	if (sink_request & DP_LINK_STATUS_UPDATED) {
		if (dp_ctrl_link_maintenance(ctrl)) {
			DRM_ERROR("LM failed: TEST_LINK_TRAINING\n");
			return;
		}
	}

	if (sink_request & DP_TEST_LINK_TRAINING) {
		dp_link_send_test_response(ctrl->link);
		if (dp_ctrl_link_maintenance(ctrl)) {
			DRM_ERROR("LM failed: TEST_LINK_TRAINING\n");
			return;
		}
	}
}

static bool dp_ctrl_clock_recovery_any_ok(
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

static bool dp_ctrl_channel_eq_ok(struct dp_ctrl_private *ctrl)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	int num_lanes = ctrl->link->link_params.num_lanes;

	dp_ctrl_read_link_status(ctrl, link_status);

	return drm_dp_channel_eq_ok(link_status, num_lanes);
}

int dp_ctrl_on_link(struct dp_ctrl *dp_ctrl)
{
	int rc = 0;
	struct dp_ctrl_private *ctrl;
	u32 rate = 0;
	int link_train_max_retries = 5;
	u32 const phy_cts_pixel_clk_khz = 148500;
	u8 link_status[DP_LINK_STATUS_SIZE];
	unsigned int training_step;

	if (!dp_ctrl)
		return -EINVAL;

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);

	rate = ctrl->panel->link_info.rate;

	dp_power_clk_enable(ctrl->power, DP_CORE_PM, true);

	if (ctrl->link->sink_request & DP_TEST_LINK_PHY_TEST_PATTERN) {
		DRM_DEBUG_DP("using phy test link parameters\n");
		if (!ctrl->panel->dp_mode.drm_mode.clock)
			ctrl->dp_ctrl.pixel_rate = phy_cts_pixel_clk_khz;
	} else {
		ctrl->link->link_params.rate = rate;
		ctrl->link->link_params.num_lanes =
			ctrl->panel->link_info.num_lanes;
		ctrl->dp_ctrl.pixel_rate = ctrl->panel->dp_mode.drm_mode.clock;
	}

	DRM_DEBUG_DP("rate=%d, num_lanes=%d, pixel_rate=%d\n",
		ctrl->link->link_params.rate,
		ctrl->link->link_params.num_lanes, ctrl->dp_ctrl.pixel_rate);

	ctrl->link->phy_params.p_level = 0;
	ctrl->link->phy_params.v_level = 0;

	rc = dp_ctrl_enable_mainlink_clocks(ctrl);
	if (rc)
		return rc;

	while (--link_train_max_retries) {
		rc = dp_ctrl_reinitialize_mainlink(ctrl);
		if (rc) {
			DRM_ERROR("Failed to reinitialize mainlink. rc=%d\n",
					rc);
			break;
		}

		training_step = DP_TRAINING_NONE;
		rc = dp_ctrl_setup_main_link(ctrl, &training_step);
		if (rc == 0) {
			/* training completed successfully */
			break;
		} else if (training_step == DP_TRAINING_1) {
			/* link train_1 failed */
			if (!dp_catalog_link_is_connected(ctrl->catalog))
				break;

			dp_ctrl_read_link_status(ctrl, link_status);

			rc = dp_ctrl_link_rate_down_shift(ctrl);
			if (rc < 0) { /* already in RBR = 1.6G */
				if (dp_ctrl_clock_recovery_any_ok(link_status,
					ctrl->link->link_params.num_lanes)) {
					/*
					 * some lanes are ready,
					 * reduce lane number
					 */
					rc = dp_ctrl_link_lane_down_shift(ctrl);
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
			if (!dp_catalog_link_is_connected(ctrl->catalog))
				break;

			dp_ctrl_read_link_status(ctrl, link_status);

			if (!drm_dp_clock_recovery_ok(link_status,
					ctrl->link->link_params.num_lanes))
				rc = dp_ctrl_link_rate_down_shift(ctrl);
			else
				rc = dp_ctrl_link_lane_down_shift(ctrl);

			if (rc < 0) {
				/* end with failure */
				break; /* lane == 1 already */
			}
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
		dp_ctrl_clear_training_pattern(ctrl);

		dp_ctrl_deinitialize_mainlink(ctrl);
		rc = -ECONNRESET;
	}

	return rc;
}

static int dp_ctrl_link_retrain(struct dp_ctrl_private *ctrl)
{
	int training_step = DP_TRAINING_NONE;

	return dp_ctrl_setup_main_link(ctrl, &training_step);
}

int dp_ctrl_on_stream(struct dp_ctrl *dp_ctrl)
{
	int ret = 0;
	bool mainlink_ready = false;
	struct dp_ctrl_private *ctrl;

	if (!dp_ctrl)
		return -EINVAL;

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);

	ctrl->dp_ctrl.pixel_rate = ctrl->panel->dp_mode.drm_mode.clock;

	DRM_DEBUG_DP("rate=%d, num_lanes=%d, pixel_rate=%d\n",
		ctrl->link->link_params.rate,
		ctrl->link->link_params.num_lanes, ctrl->dp_ctrl.pixel_rate);

	if (!dp_power_clk_status(ctrl->power, DP_CTRL_PM)) { /* link clk is off */
		ret = dp_ctrl_enable_mainlink_clocks(ctrl);
		if (ret) {
			DRM_ERROR("Failed to start link clocks. ret=%d\n", ret);
			goto end;
		}
	}

	if (!dp_ctrl_channel_eq_ok(ctrl))
		dp_ctrl_link_retrain(ctrl);

	/* stop txing train pattern to end link training */
	dp_ctrl_clear_training_pattern(ctrl);

	ret = dp_ctrl_enable_stream_clocks(ctrl);
	if (ret) {
		DRM_ERROR("Failed to start pixel clocks. ret=%d\n", ret);
		goto end;
	}

	if (ctrl->link->sink_request & DP_TEST_LINK_PHY_TEST_PATTERN) {
		dp_ctrl_send_phy_test_pattern(ctrl);
		return 0;
	}

	/*
	 * Set up transfer unit values and set controller state to send
	 * video.
	 */
	reinit_completion(&ctrl->video_comp);

	dp_ctrl_configure_source_params(ctrl);

	dp_catalog_ctrl_config_msa(ctrl->catalog,
		ctrl->link->link_params.rate,
		ctrl->dp_ctrl.pixel_rate, dp_ctrl_use_fixed_nvid(ctrl));

	dp_ctrl_setup_tr_unit(ctrl);

	dp_catalog_ctrl_state_ctrl(ctrl->catalog, DP_STATE_CTRL_SEND_VIDEO);

	ret = dp_ctrl_wait4video_ready(ctrl);
	if (ret)
		return ret;

	mainlink_ready = dp_catalog_ctrl_mainlink_ready(ctrl->catalog);
	DRM_DEBUG_DP("mainlink %s\n", mainlink_ready ? "READY" : "NOT READY");

end:
	return ret;
}

int dp_ctrl_off_link_stream(struct dp_ctrl *dp_ctrl)
{
	struct dp_ctrl_private *ctrl;
	struct dp_io *dp_io;
	struct phy *phy;
	int ret;

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);
	dp_io = &ctrl->parser->io;
	phy = dp_io->phy;

	/* set dongle to D3 (power off) mode */
	dp_link_psm_config(ctrl->link, &ctrl->panel->link_info, true);

	dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);

	if (dp_power_clk_status(ctrl->power, DP_STREAM_PM)) {
		ret = dp_power_clk_enable(ctrl->power, DP_STREAM_PM, false);
		if (ret) {
			DRM_ERROR("Failed to disable pclk. ret=%d\n", ret);
			return ret;
		}
	}

	ret = dp_power_clk_enable(ctrl->power, DP_CTRL_PM, false);
	if (ret) {
		DRM_ERROR("Failed to disable link clocks. ret=%d\n", ret);
		return ret;
	}

	phy_power_off(phy);

	/* aux channel down, reinit phy */
	phy_exit(phy);
	phy_init(phy);

	DRM_DEBUG_DP("DP off link/stream done\n");
	return ret;
}

void dp_ctrl_off_phy(struct dp_ctrl *dp_ctrl)
{
	struct dp_ctrl_private *ctrl;
	struct dp_io *dp_io;
	struct phy *phy;

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);
	dp_io = &ctrl->parser->io;
	phy = dp_io->phy;

	dp_catalog_ctrl_reset(ctrl->catalog);

	phy_exit(phy);

	DRM_DEBUG_DP("DP off phy done\n");
}

int dp_ctrl_off(struct dp_ctrl *dp_ctrl)
{
	struct dp_ctrl_private *ctrl;
	struct dp_io *dp_io;
	struct phy *phy;
	int ret = 0;

	if (!dp_ctrl)
		return -EINVAL;

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);
	dp_io = &ctrl->parser->io;
	phy = dp_io->phy;

	dp_catalog_ctrl_mainlink_ctrl(ctrl->catalog, false);

	dp_catalog_ctrl_reset(ctrl->catalog);

	ret = dp_power_clk_enable(ctrl->power, DP_STREAM_PM, false);
	if (ret)
		DRM_ERROR("Failed to disable pixel clocks. ret=%d\n", ret);

	ret = dp_power_clk_enable(ctrl->power, DP_CTRL_PM, false);
	if (ret) {
		DRM_ERROR("Failed to disable link clocks. ret=%d\n", ret);
	}

	phy_power_off(phy);
	phy_exit(phy);

	DRM_DEBUG_DP("DP off done\n");
	return ret;
}

void dp_ctrl_isr(struct dp_ctrl *dp_ctrl)
{
	struct dp_ctrl_private *ctrl;
	u32 isr;

	if (!dp_ctrl)
		return;

	ctrl = container_of(dp_ctrl, struct dp_ctrl_private, dp_ctrl);

	isr = dp_catalog_ctrl_get_interrupt(ctrl->catalog);

	if (isr & DP_CTRL_INTR_READY_FOR_VIDEO) {
		DRM_DEBUG_DP("dp_video_ready\n");
		complete(&ctrl->video_comp);
	}

	if (isr & DP_CTRL_INTR_IDLE_PATTERN_SENT) {
		DRM_DEBUG_DP("idle_patterns_sent\n");
		complete(&ctrl->idle_comp);
	}
}

struct dp_ctrl *dp_ctrl_get(struct device *dev, struct dp_link *link,
			struct dp_panel *panel,	struct drm_dp_aux *aux,
			struct dp_power *power, struct dp_catalog *catalog,
			struct dp_parser *parser)
{
	struct dp_ctrl_private *ctrl;
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
		return (struct dp_ctrl *)ERR_PTR(ret);
	}

	/* OPP table is optional */
	ret = devm_pm_opp_of_add_table(dev);
	if (ret)
		dev_err(dev, "failed to add DP OPP table\n");

	init_completion(&ctrl->idle_comp);
	init_completion(&ctrl->video_comp);

	/* in parameters */
	ctrl->parser   = parser;
	ctrl->panel    = panel;
	ctrl->power    = power;
	ctrl->aux      = aux;
	ctrl->link     = link;
	ctrl->catalog  = catalog;
	ctrl->dev      = dev;

	return &ctrl->dp_ctrl;
}
