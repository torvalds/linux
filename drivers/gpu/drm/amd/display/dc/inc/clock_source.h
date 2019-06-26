/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_CLOCK_SOURCE_H__
#define __DC_CLOCK_SOURCE_H__

#include "dc_types.h"
#include "include/grph_object_id.h"
#include "include/bios_parser_types.h"

struct clock_source;

struct spread_spectrum_data {
	uint32_t percentage;		/*> In unit of 0.01% or 0.001%*/
	uint32_t percentage_divider;	/*> 100 or 1000	*/
	uint32_t freq_range_khz;
	uint32_t modulation_freq_hz;

	struct spread_spectrum_flags flags;
};

struct delta_sigma_data {
	uint32_t feedback_amount;
	uint32_t nfrac_amount;
	uint32_t ds_frac_size;
	uint32_t ds_frac_amount;
};

/**
 *  Pixel Clock Parameters structure
 *  These parameters are required as input
 *  when calculating Pixel Clock Dividers for requested Pixel Clock
 */
struct pixel_clk_flags {
	uint32_t ENABLE_SS:1;
	uint32_t DISPLAY_BLANKED:1;
	uint32_t PROGRAM_PIXEL_CLOCK:1;
	uint32_t PROGRAM_ID_CLOCK:1;
	uint32_t SUPPORT_YCBCR420:1;
};

/**
 *  Display Port HW De spread of Reference Clock related Parameters structure
 *  Store it once at boot for later usage
  */
struct csdp_ref_clk_ds_params {
	bool hw_dso_n_dp_ref_clk;
/* Flag for HW De Spread enabled (if enabled SS on DP Reference Clock)*/
	uint32_t avg_dp_ref_clk_khz;
/* Average DP Reference clock (in KHz)*/
	uint32_t ss_percentage_on_dp_ref_clk;
/* DP Reference clock SS percentage
 * (not to be mixed with DP IDCLK SS from PLL Settings)*/
	uint32_t ss_percentage_divider;
/* DP Reference clock SS percentage divider */
};

struct pixel_clk_params {
	uint32_t requested_pix_clk_100hz;
/*> Requested Pixel Clock
 * (based on Video Timing standard used for requested mode)*/
	uint32_t requested_sym_clk; /* in KHz */
/*> Requested Sym Clock (relevant only for display port)*/
	uint32_t dp_ref_clk; /* in KHz */
/*> DP reference clock - calculated only for DP signal for specific cases*/
	struct graphics_object_id encoder_object_id;
/*> Encoder object Id - needed by VBIOS Exec table*/
	enum signal_type signal_type;
/*> signalType -> Encoder Mode - needed by VBIOS Exec table*/
	enum controller_id controller_id;
/*> ControllerId - which controller using this PLL*/
	enum dc_color_depth color_depth;
	struct csdp_ref_clk_ds_params de_spread_params;
/*> de-spread info, relevant only for on-the-fly tune-up pixel rate*/
	enum dc_pixel_encoding pixel_encoding;
	struct pixel_clk_flags flags;
};

/**
 *  Pixel Clock Dividers structure with desired Pixel Clock
 *  (adjusted after VBIOS exec table),
 *  with actually calculated Clock and reference Crystal frequency
 */
struct pll_settings {
	uint32_t actual_pix_clk_100hz;
	uint32_t adjusted_pix_clk_100hz;
	uint32_t calculated_pix_clk_100hz;
	uint32_t vco_freq;
	uint32_t reference_freq;
	uint32_t reference_divider;
	uint32_t feedback_divider;
	uint32_t fract_feedback_divider;
	uint32_t pix_clk_post_divider;
	uint32_t ss_percentage;
	bool use_external_clk;
};

struct calc_pll_clock_source_init_data {
	struct dc_bios *bp;
	uint32_t min_pix_clk_pll_post_divider;
	uint32_t max_pix_clk_pll_post_divider;
	uint32_t min_pll_ref_divider;
	uint32_t max_pll_ref_divider;
	uint32_t min_override_input_pxl_clk_pll_freq_khz;
/* if not 0, override the firmware info */

	uint32_t max_override_input_pxl_clk_pll_freq_khz;
/* if not 0, override the firmware info */

	uint32_t num_fract_fb_divider_decimal_point;
/* number of decimal point for fractional feedback divider value */

	uint32_t num_fract_fb_divider_decimal_point_precision;
/* number of decimal point to round off for fractional feedback divider value*/
	struct dc_context *ctx;

};

struct calc_pll_clock_source {
	uint32_t ref_freq_khz;
	uint32_t min_pix_clock_pll_post_divider;
	uint32_t max_pix_clock_pll_post_divider;
	uint32_t min_pll_ref_divider;
	uint32_t max_pll_ref_divider;

	uint32_t max_vco_khz;
	uint32_t min_vco_khz;
	uint32_t min_pll_input_freq_khz;
	uint32_t max_pll_input_freq_khz;

	uint32_t fract_fb_divider_decimal_points_num;
	uint32_t fract_fb_divider_factor;
	uint32_t fract_fb_divider_precision;
	uint32_t fract_fb_divider_precision_factor;
	struct dc_context *ctx;
};

struct clock_source_funcs {
	bool (*cs_power_down)(
			struct clock_source *);
	bool (*program_pix_clk)(struct clock_source *,
			struct pixel_clk_params *, struct pll_settings *);
	uint32_t (*get_pix_clk_dividers)(
			struct clock_source *,
			struct pixel_clk_params *,
			struct pll_settings *);
	bool (*get_pixel_clk_frequency_100hz)(
			struct clock_source *clock_source,
			unsigned int inst,
			unsigned int *pixel_clk_khz);
};

struct clock_source {
	const struct clock_source_funcs *funcs;
	struct dc_context *ctx;
	enum clock_source_id id;
	bool dp_clk_src;
};

#endif
