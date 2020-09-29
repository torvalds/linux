/*
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Packard <keithp@keithp.com>
 *
 */

#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "i915_debugfs.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_ddi.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dpio_phy.h"
#include "intel_fifo_underrun.h"
#include "intel_hdcp.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_lspcon.h"
#include "intel_lvds.h"
#include "intel_panel.h"
#include "intel_psr.h"
#include "intel_sideband.h"
#include "intel_tc.h"
#include "intel_vdsc.h"

#define DP_DPRX_ESI_LEN 14

/* DP DSC throughput values used for slice count calculations KPixels/s */
#define DP_DSC_PEAK_PIXEL_RATE			2720000
#define DP_DSC_MAX_ENC_THROUGHPUT_0		340000
#define DP_DSC_MAX_ENC_THROUGHPUT_1		400000

/* DP DSC FEC Overhead factor = 1/(0.972261) */
#define DP_DSC_FEC_OVERHEAD_FACTOR		972261

/* Compliance test status bits  */
#define INTEL_DP_RESOLUTION_SHIFT_MASK	0
#define INTEL_DP_RESOLUTION_PREFERRED	(1 << INTEL_DP_RESOLUTION_SHIFT_MASK)
#define INTEL_DP_RESOLUTION_STANDARD	(2 << INTEL_DP_RESOLUTION_SHIFT_MASK)
#define INTEL_DP_RESOLUTION_FAILSAFE	(3 << INTEL_DP_RESOLUTION_SHIFT_MASK)

struct dp_link_dpll {
	int clock;
	struct dpll dpll;
};

static const struct dp_link_dpll g4x_dpll[] = {
	{ 162000,
		{ .p1 = 2, .p2 = 10, .n = 2, .m1 = 23, .m2 = 8 } },
	{ 270000,
		{ .p1 = 1, .p2 = 10, .n = 1, .m1 = 14, .m2 = 2 } }
};

static const struct dp_link_dpll pch_dpll[] = {
	{ 162000,
		{ .p1 = 2, .p2 = 10, .n = 1, .m1 = 12, .m2 = 9 } },
	{ 270000,
		{ .p1 = 1, .p2 = 10, .n = 2, .m1 = 14, .m2 = 8 } }
};

static const struct dp_link_dpll vlv_dpll[] = {
	{ 162000,
		{ .p1 = 3, .p2 = 2, .n = 5, .m1 = 3, .m2 = 81 } },
	{ 270000,
		{ .p1 = 2, .p2 = 2, .n = 1, .m1 = 2, .m2 = 27 } }
};

/*
 * CHV supports eDP 1.4 that have  more link rates.
 * Below only provides the fixed rate but exclude variable rate.
 */
static const struct dp_link_dpll chv_dpll[] = {
	/*
	 * CHV requires to program fractional division for m2.
	 * m2 is stored in fixed point format using formula below
	 * (m2_int << 22) | m2_fraction
	 */
	{ 162000,	/* m2_int = 32, m2_fraction = 1677722 */
		{ .p1 = 4, .p2 = 2, .n = 1, .m1 = 2, .m2 = 0x819999a } },
	{ 270000,	/* m2_int = 27, m2_fraction = 0 */
		{ .p1 = 4, .p2 = 1, .n = 1, .m1 = 2, .m2 = 0x6c00000 } },
};

/* Constants for DP DSC configurations */
static const u8 valid_dsc_bpp[] = {6, 8, 10, 12, 15};

/* With Single pipe configuration, HW is capable of supporting maximum
 * of 4 slices per line.
 */
static const u8 valid_dsc_slicecount[] = {1, 2, 4};

/**
 * intel_dp_is_edp - is the given port attached to an eDP panel (either CPU or PCH)
 * @intel_dp: DP struct
 *
 * If a CPU or PCH DP output is attached to an eDP panel, this function
 * will return true, and false otherwise.
 */
bool intel_dp_is_edp(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	return dig_port->base.type == INTEL_OUTPUT_EDP;
}

static void intel_dp_link_down(struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state);
static bool edp_panel_vdd_on(struct intel_dp *intel_dp);
static void edp_panel_vdd_off(struct intel_dp *intel_dp, bool sync);
static void vlv_init_panel_power_sequencer(struct intel_encoder *encoder,
					   const struct intel_crtc_state *crtc_state);
static void vlv_steal_power_sequencer(struct drm_i915_private *dev_priv,
				      enum pipe pipe);
static void intel_dp_unset_edid(struct intel_dp *intel_dp);

/* update sink rates from dpcd */
static void intel_dp_set_sink_rates(struct intel_dp *intel_dp)
{
	static const int dp_rates[] = {
		162000, 270000, 540000, 810000
	};
	int i, max_rate;

	if (drm_dp_has_quirk(&intel_dp->desc, 0,
			     DP_DPCD_QUIRK_CAN_DO_MAX_LINK_RATE_3_24_GBPS)) {
		/* Needed, e.g., for Apple MBP 2017, 15 inch eDP Retina panel */
		static const int quirk_rates[] = { 162000, 270000, 324000 };

		memcpy(intel_dp->sink_rates, quirk_rates, sizeof(quirk_rates));
		intel_dp->num_sink_rates = ARRAY_SIZE(quirk_rates);

		return;
	}

	max_rate = drm_dp_bw_code_to_link_rate(intel_dp->dpcd[DP_MAX_LINK_RATE]);

	for (i = 0; i < ARRAY_SIZE(dp_rates); i++) {
		if (dp_rates[i] > max_rate)
			break;
		intel_dp->sink_rates[i] = dp_rates[i];
	}

	intel_dp->num_sink_rates = i;
}

/* Get length of rates array potentially limited by max_rate. */
static int intel_dp_rate_limit_len(const int *rates, int len, int max_rate)
{
	int i;

	/* Limit results by potentially reduced max rate */
	for (i = 0; i < len; i++) {
		if (rates[len - i - 1] <= max_rate)
			return len - i;
	}

	return 0;
}

/* Get length of common rates array potentially limited by max_rate. */
static int intel_dp_common_len_rate_limit(const struct intel_dp *intel_dp,
					  int max_rate)
{
	return intel_dp_rate_limit_len(intel_dp->common_rates,
				       intel_dp->num_common_rates, max_rate);
}

/* Theoretical max between source and sink */
static int intel_dp_max_common_rate(struct intel_dp *intel_dp)
{
	return intel_dp->common_rates[intel_dp->num_common_rates - 1];
}

/* Theoretical max between source and sink */
static int intel_dp_max_common_lane_count(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	int source_max = dig_port->max_lanes;
	int sink_max = drm_dp_max_lane_count(intel_dp->dpcd);
	int fia_max = intel_tc_port_fia_max_lane_count(dig_port);

	return min3(source_max, sink_max, fia_max);
}

int intel_dp_max_lane_count(struct intel_dp *intel_dp)
{
	return intel_dp->max_link_lane_count;
}

int
intel_dp_link_required(int pixel_clock, int bpp)
{
	/* pixel_clock is in kHz, divide bpp by 8 for bit to Byte conversion */
	return DIV_ROUND_UP(pixel_clock * bpp, 8);
}

int
intel_dp_max_data_rate(int max_link_clock, int max_lanes)
{
	/* max_link_clock is the link symbol clock (LS_Clk) in kHz and not the
	 * link rate that is generally expressed in Gbps. Since, 8 bits of data
	 * is transmitted every LS_Clk per lane, there is no need to account for
	 * the channel encoding that is done in the PHY layer here.
	 */

	return max_link_clock * max_lanes;
}

static int cnl_max_source_rate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum port port = dig_port->base.port;

	u32 voltage = intel_de_read(dev_priv, CNL_PORT_COMP_DW3) & VOLTAGE_INFO_MASK;

	/* Low voltage SKUs are limited to max of 5.4G */
	if (voltage == VOLTAGE_INFO_0_85V)
		return 540000;

	/* For this SKU 8.1G is supported in all ports */
	if (IS_CNL_WITH_PORT_F(dev_priv))
		return 810000;

	/* For other SKUs, max rate on ports A and D is 5.4G */
	if (port == PORT_A || port == PORT_D)
		return 540000;

	return 810000;
}

static int icl_max_source_rate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, dig_port->base.port);

	if (intel_phy_is_combo(dev_priv, phy) &&
	    !IS_ELKHARTLAKE(dev_priv) &&
	    !intel_dp_is_edp(intel_dp))
		return 540000;

	return 810000;
}

static void
intel_dp_set_source_rates(struct intel_dp *intel_dp)
{
	/* The values must be in increasing order */
	static const int cnl_rates[] = {
		162000, 216000, 270000, 324000, 432000, 540000, 648000, 810000
	};
	static const int bxt_rates[] = {
		162000, 216000, 243000, 270000, 324000, 432000, 540000
	};
	static const int skl_rates[] = {
		162000, 216000, 270000, 324000, 432000, 540000
	};
	static const int hsw_rates[] = {
		162000, 270000, 540000
	};
	static const int g4x_rates[] = {
		162000, 270000
	};
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	const int *source_rates;
	int size, max_rate = 0, vbt_max_rate;

	/* This should only be done once */
	drm_WARN_ON(&dev_priv->drm,
		    intel_dp->source_rates || intel_dp->num_source_rates);

	if (INTEL_GEN(dev_priv) >= 10) {
		source_rates = cnl_rates;
		size = ARRAY_SIZE(cnl_rates);
		if (IS_GEN(dev_priv, 10))
			max_rate = cnl_max_source_rate(intel_dp);
		else
			max_rate = icl_max_source_rate(intel_dp);
	} else if (IS_GEN9_LP(dev_priv)) {
		source_rates = bxt_rates;
		size = ARRAY_SIZE(bxt_rates);
	} else if (IS_GEN9_BC(dev_priv)) {
		source_rates = skl_rates;
		size = ARRAY_SIZE(skl_rates);
	} else if ((IS_HASWELL(dev_priv) && !IS_HSW_ULX(dev_priv)) ||
		   IS_BROADWELL(dev_priv)) {
		source_rates = hsw_rates;
		size = ARRAY_SIZE(hsw_rates);
	} else {
		source_rates = g4x_rates;
		size = ARRAY_SIZE(g4x_rates);
	}

	vbt_max_rate = intel_bios_dp_max_link_rate(encoder);
	if (max_rate && vbt_max_rate)
		max_rate = min(max_rate, vbt_max_rate);
	else if (vbt_max_rate)
		max_rate = vbt_max_rate;

	if (max_rate)
		size = intel_dp_rate_limit_len(source_rates, size, max_rate);

	intel_dp->source_rates = source_rates;
	intel_dp->num_source_rates = size;
}

static int intersect_rates(const int *source_rates, int source_len,
			   const int *sink_rates, int sink_len,
			   int *common_rates)
{
	int i = 0, j = 0, k = 0;

	while (i < source_len && j < sink_len) {
		if (source_rates[i] == sink_rates[j]) {
			if (WARN_ON(k >= DP_MAX_SUPPORTED_RATES))
				return k;
			common_rates[k] = source_rates[i];
			++k;
			++i;
			++j;
		} else if (source_rates[i] < sink_rates[j]) {
			++i;
		} else {
			++j;
		}
	}
	return k;
}

/* return index of rate in rates array, or -1 if not found */
static int intel_dp_rate_index(const int *rates, int len, int rate)
{
	int i;

	for (i = 0; i < len; i++)
		if (rate == rates[i])
			return i;

	return -1;
}

static void intel_dp_set_common_rates(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	drm_WARN_ON(&i915->drm,
		    !intel_dp->num_source_rates || !intel_dp->num_sink_rates);

	intel_dp->num_common_rates = intersect_rates(intel_dp->source_rates,
						     intel_dp->num_source_rates,
						     intel_dp->sink_rates,
						     intel_dp->num_sink_rates,
						     intel_dp->common_rates);

	/* Paranoia, there should always be something in common. */
	if (drm_WARN_ON(&i915->drm, intel_dp->num_common_rates == 0)) {
		intel_dp->common_rates[0] = 162000;
		intel_dp->num_common_rates = 1;
	}
}

static bool intel_dp_link_params_valid(struct intel_dp *intel_dp, int link_rate,
				       u8 lane_count)
{
	/*
	 * FIXME: we need to synchronize the current link parameters with
	 * hardware readout. Currently fast link training doesn't work on
	 * boot-up.
	 */
	if (link_rate == 0 ||
	    link_rate > intel_dp->max_link_rate)
		return false;

	if (lane_count == 0 ||
	    lane_count > intel_dp_max_lane_count(intel_dp))
		return false;

	return true;
}

static bool intel_dp_can_link_train_fallback_for_edp(struct intel_dp *intel_dp,
						     int link_rate,
						     u8 lane_count)
{
	const struct drm_display_mode *fixed_mode =
		intel_dp->attached_connector->panel.fixed_mode;
	int mode_rate, max_rate;

	mode_rate = intel_dp_link_required(fixed_mode->clock, 18);
	max_rate = intel_dp_max_data_rate(link_rate, lane_count);
	if (mode_rate > max_rate)
		return false;

	return true;
}

int intel_dp_get_link_train_fallback_values(struct intel_dp *intel_dp,
					    int link_rate, u8 lane_count)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int index;

	/*
	 * TODO: Enable fallback on MST links once MST link compute can handle
	 * the fallback params.
	 */
	if (intel_dp->is_mst) {
		drm_err(&i915->drm, "Link Training Unsuccessful\n");
		return -1;
	}

	index = intel_dp_rate_index(intel_dp->common_rates,
				    intel_dp->num_common_rates,
				    link_rate);
	if (index > 0) {
		if (intel_dp_is_edp(intel_dp) &&
		    !intel_dp_can_link_train_fallback_for_edp(intel_dp,
							      intel_dp->common_rates[index - 1],
							      lane_count)) {
			drm_dbg_kms(&i915->drm,
				    "Retrying Link training for eDP with same parameters\n");
			return 0;
		}
		intel_dp->max_link_rate = intel_dp->common_rates[index - 1];
		intel_dp->max_link_lane_count = lane_count;
	} else if (lane_count > 1) {
		if (intel_dp_is_edp(intel_dp) &&
		    !intel_dp_can_link_train_fallback_for_edp(intel_dp,
							      intel_dp_max_common_rate(intel_dp),
							      lane_count >> 1)) {
			drm_dbg_kms(&i915->drm,
				    "Retrying Link training for eDP with same parameters\n");
			return 0;
		}
		intel_dp->max_link_rate = intel_dp_max_common_rate(intel_dp);
		intel_dp->max_link_lane_count = lane_count >> 1;
	} else {
		drm_err(&i915->drm, "Link Training Unsuccessful\n");
		return -1;
	}

	return 0;
}

u32 intel_dp_mode_to_fec_clock(u32 mode_clock)
{
	return div_u64(mul_u32_u32(mode_clock, 1000000U),
		       DP_DSC_FEC_OVERHEAD_FACTOR);
}

static int
small_joiner_ram_size_bits(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) >= 11)
		return 7680 * 8;
	else
		return 6144 * 8;
}

static u16 intel_dp_dsc_get_output_bpp(struct drm_i915_private *i915,
				       u32 link_clock, u32 lane_count,
				       u32 mode_clock, u32 mode_hdisplay)
{
	u32 bits_per_pixel, max_bpp_small_joiner_ram;
	int i;

	/*
	 * Available Link Bandwidth(Kbits/sec) = (NumberOfLanes)*
	 * (LinkSymbolClock)* 8 * (TimeSlotsPerMTP)
	 * for SST -> TimeSlotsPerMTP is 1,
	 * for MST -> TimeSlotsPerMTP has to be calculated
	 */
	bits_per_pixel = (link_clock * lane_count * 8) /
			 intel_dp_mode_to_fec_clock(mode_clock);
	drm_dbg_kms(&i915->drm, "Max link bpp: %u\n", bits_per_pixel);

	/* Small Joiner Check: output bpp <= joiner RAM (bits) / Horiz. width */
	max_bpp_small_joiner_ram = small_joiner_ram_size_bits(i915) /
		mode_hdisplay;
	drm_dbg_kms(&i915->drm, "Max small joiner bpp: %u\n",
		    max_bpp_small_joiner_ram);

	/*
	 * Greatest allowed DSC BPP = MIN (output BPP from available Link BW
	 * check, output bpp from small joiner RAM check)
	 */
	bits_per_pixel = min(bits_per_pixel, max_bpp_small_joiner_ram);

	/* Error out if the max bpp is less than smallest allowed valid bpp */
	if (bits_per_pixel < valid_dsc_bpp[0]) {
		drm_dbg_kms(&i915->drm, "Unsupported BPP %u, min %u\n",
			    bits_per_pixel, valid_dsc_bpp[0]);
		return 0;
	}

	/* Find the nearest match in the array of known BPPs from VESA */
	for (i = 0; i < ARRAY_SIZE(valid_dsc_bpp) - 1; i++) {
		if (bits_per_pixel < valid_dsc_bpp[i + 1])
			break;
	}
	bits_per_pixel = valid_dsc_bpp[i];

	/*
	 * Compressed BPP in U6.4 format so multiply by 16, for Gen 11,
	 * fractional part is 0
	 */
	return bits_per_pixel << 4;
}

static u8 intel_dp_dsc_get_slice_count(struct intel_dp *intel_dp,
				       int mode_clock, int mode_hdisplay)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 min_slice_count, i;
	int max_slice_width;

	if (mode_clock <= DP_DSC_PEAK_PIXEL_RATE)
		min_slice_count = DIV_ROUND_UP(mode_clock,
					       DP_DSC_MAX_ENC_THROUGHPUT_0);
	else
		min_slice_count = DIV_ROUND_UP(mode_clock,
					       DP_DSC_MAX_ENC_THROUGHPUT_1);

	max_slice_width = drm_dp_dsc_sink_max_slice_width(intel_dp->dsc_dpcd);
	if (max_slice_width < DP_DSC_MIN_SLICE_WIDTH_VALUE) {
		drm_dbg_kms(&i915->drm,
			    "Unsupported slice width %d by DP DSC Sink device\n",
			    max_slice_width);
		return 0;
	}
	/* Also take into account max slice width */
	min_slice_count = min_t(u8, min_slice_count,
				DIV_ROUND_UP(mode_hdisplay,
					     max_slice_width));

	/* Find the closest match to the valid slice count values */
	for (i = 0; i < ARRAY_SIZE(valid_dsc_slicecount); i++) {
		if (valid_dsc_slicecount[i] >
		    drm_dp_dsc_sink_max_slice_count(intel_dp->dsc_dpcd,
						    false))
			break;
		if (min_slice_count  <= valid_dsc_slicecount[i])
			return valid_dsc_slicecount[i];
	}

	drm_dbg_kms(&i915->drm, "Unsupported Slice Count %d\n",
		    min_slice_count);
	return 0;
}

static enum intel_output_format
intel_dp_output_format(struct drm_connector *connector,
		       const struct drm_display_mode *mode)
{
	struct intel_dp *intel_dp = intel_attached_dp(to_intel_connector(connector));
	const struct drm_display_info *info = &connector->display_info;

	if (!drm_mode_is_420_only(info, mode))
		return INTEL_OUTPUT_FORMAT_RGB;

	if (intel_dp->dfp.ycbcr_444_to_420)
		return INTEL_OUTPUT_FORMAT_YCBCR444;
	else
		return INTEL_OUTPUT_FORMAT_YCBCR420;
}

int intel_dp_min_bpp(enum intel_output_format output_format)
{
	if (output_format == INTEL_OUTPUT_FORMAT_RGB)
		return 6 * 3;
	else
		return 8 * 3;
}

static int intel_dp_output_bpp(enum intel_output_format output_format, int bpp)
{
	/*
	 * bpp value was assumed to RGB format. And YCbCr 4:2:0 output
	 * format of the number of bytes per pixel will be half the number
	 * of bytes of RGB pixel.
	 */
	if (output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		bpp /= 2;

	return bpp;
}

static int
intel_dp_mode_min_output_bpp(struct drm_connector *connector,
			     const struct drm_display_mode *mode)
{
	enum intel_output_format output_format =
		intel_dp_output_format(connector, mode);

	return intel_dp_output_bpp(output_format, intel_dp_min_bpp(output_format));
}

static bool intel_dp_hdisplay_bad(struct drm_i915_private *dev_priv,
				  int hdisplay)
{
	/*
	 * Older platforms don't like hdisplay==4096 with DP.
	 *
	 * On ILK/SNB/IVB the pipe seems to be somewhat running (scanline
	 * and frame counter increment), but we don't get vblank interrupts,
	 * and the pipe underruns immediately. The link also doesn't seem
	 * to get trained properly.
	 *
	 * On CHV the vblank interrupts don't seem to disappear but
	 * otherwise the symptoms are similar.
	 *
	 * TODO: confirm the behaviour on HSW+
	 */
	return hdisplay == 4096 && !HAS_DDI(dev_priv);
}

static enum drm_mode_status
intel_dp_mode_valid_downstream(struct intel_connector *connector,
			       const struct drm_display_mode *mode,
			       int target_clock)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	const struct drm_display_info *info = &connector->base.display_info;
	int tmds_clock;

	if (intel_dp->dfp.max_dotclock &&
	    target_clock > intel_dp->dfp.max_dotclock)
		return MODE_CLOCK_HIGH;

	/* Assume 8bpc for the DP++/HDMI/DVI TMDS clock check */
	tmds_clock = target_clock;
	if (drm_mode_is_420_only(info, mode))
		tmds_clock /= 2;

	if (intel_dp->dfp.min_tmds_clock &&
	    tmds_clock < intel_dp->dfp.min_tmds_clock)
		return MODE_CLOCK_LOW;
	if (intel_dp->dfp.max_tmds_clock &&
	    tmds_clock > intel_dp->dfp.max_tmds_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static enum drm_mode_status
intel_dp_mode_valid(struct drm_connector *connector,
		    struct drm_display_mode *mode)
{
	struct intel_dp *intel_dp = intel_attached_dp(to_intel_connector(connector));
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	int target_clock = mode->clock;
	int max_rate, mode_rate, max_lanes, max_link_clock;
	int max_dotclk = dev_priv->max_dotclk_freq;
	u16 dsc_max_output_bpp = 0;
	u8 dsc_slice_count = 0;
	enum drm_mode_status status;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (intel_dp_is_edp(intel_dp) && fixed_mode) {
		if (mode->hdisplay > fixed_mode->hdisplay)
			return MODE_PANEL;

		if (mode->vdisplay > fixed_mode->vdisplay)
			return MODE_PANEL;

		target_clock = fixed_mode->clock;
	}

	max_link_clock = intel_dp_max_link_rate(intel_dp);
	max_lanes = intel_dp_max_lane_count(intel_dp);

	max_rate = intel_dp_max_data_rate(max_link_clock, max_lanes);
	mode_rate = intel_dp_link_required(target_clock,
					   intel_dp_mode_min_output_bpp(connector, mode));

	if (intel_dp_hdisplay_bad(dev_priv, mode->hdisplay))
		return MODE_H_ILLEGAL;

	/*
	 * Output bpp is stored in 6.4 format so right shift by 4 to get the
	 * integer value since we support only integer values of bpp.
	 */
	if ((INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv)) &&
	    drm_dp_sink_supports_dsc(intel_dp->dsc_dpcd)) {
		if (intel_dp_is_edp(intel_dp)) {
			dsc_max_output_bpp =
				drm_edp_dsc_sink_output_bpp(intel_dp->dsc_dpcd) >> 4;
			dsc_slice_count =
				drm_dp_dsc_sink_max_slice_count(intel_dp->dsc_dpcd,
								true);
		} else if (drm_dp_sink_supports_fec(intel_dp->fec_capable)) {
			dsc_max_output_bpp =
				intel_dp_dsc_get_output_bpp(dev_priv,
							    max_link_clock,
							    max_lanes,
							    target_clock,
							    mode->hdisplay) >> 4;
			dsc_slice_count =
				intel_dp_dsc_get_slice_count(intel_dp,
							     target_clock,
							     mode->hdisplay);
		}
	}

	if ((mode_rate > max_rate && !(dsc_max_output_bpp && dsc_slice_count)) ||
	    target_clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	if (mode->clock < 10000)
		return MODE_CLOCK_LOW;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_H_ILLEGAL;

	status = intel_dp_mode_valid_downstream(intel_connector,
						mode, target_clock);
	if (status != MODE_OK)
		return status;

	return intel_mode_valid_max_plane_size(dev_priv, mode);
}

u32 intel_dp_pack_aux(const u8 *src, int src_bytes)
{
	int i;
	u32 v = 0;

	if (src_bytes > 4)
		src_bytes = 4;
	for (i = 0; i < src_bytes; i++)
		v |= ((u32)src[i]) << ((3 - i) * 8);
	return v;
}

static void intel_dp_unpack_aux(u32 src, u8 *dst, int dst_bytes)
{
	int i;
	if (dst_bytes > 4)
		dst_bytes = 4;
	for (i = 0; i < dst_bytes; i++)
		dst[i] = src >> ((3-i) * 8);
}

static void
intel_dp_init_panel_power_sequencer(struct intel_dp *intel_dp);
static void
intel_dp_init_panel_power_sequencer_registers(struct intel_dp *intel_dp,
					      bool force_disable_vdd);
static void
intel_dp_pps_init(struct intel_dp *intel_dp);

static intel_wakeref_t
pps_lock(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	/*
	 * See intel_power_sequencer_reset() why we need
	 * a power domain reference here.
	 */
	wakeref = intel_display_power_get(dev_priv,
					  intel_aux_power_domain(dp_to_dig_port(intel_dp)));

	mutex_lock(&dev_priv->pps_mutex);

	return wakeref;
}

static intel_wakeref_t
pps_unlock(struct intel_dp *intel_dp, intel_wakeref_t wakeref)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	mutex_unlock(&dev_priv->pps_mutex);
	intel_display_power_put(dev_priv,
				intel_aux_power_domain(dp_to_dig_port(intel_dp)),
				wakeref);
	return 0;
}

#define with_pps_lock(dp, wf) \
	for ((wf) = pps_lock(dp); (wf); (wf) = pps_unlock((dp), (wf)))

static void
vlv_power_sequencer_kick(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe = intel_dp->pps_pipe;
	bool pll_enabled, release_cl_override = false;
	enum dpio_phy phy = DPIO_PHY(pipe);
	enum dpio_channel ch = vlv_pipe_to_channel(pipe);
	u32 DP;

	if (drm_WARN(&dev_priv->drm,
		     intel_de_read(dev_priv, intel_dp->output_reg) & DP_PORT_EN,
		     "skipping pipe %c power sequencer kick due to [ENCODER:%d:%s] being active\n",
		     pipe_name(pipe), dig_port->base.base.base.id,
		     dig_port->base.base.name))
		return;

	drm_dbg_kms(&dev_priv->drm,
		    "kicking pipe %c power sequencer for [ENCODER:%d:%s]\n",
		    pipe_name(pipe), dig_port->base.base.base.id,
		    dig_port->base.base.name);

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	DP = intel_de_read(dev_priv, intel_dp->output_reg) & DP_DETECTED;
	DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	DP |= DP_PORT_WIDTH(1);
	DP |= DP_LINK_TRAIN_PAT_1;

	if (IS_CHERRYVIEW(dev_priv))
		DP |= DP_PIPE_SEL_CHV(pipe);
	else
		DP |= DP_PIPE_SEL(pipe);

	pll_enabled = intel_de_read(dev_priv, DPLL(pipe)) & DPLL_VCO_ENABLE;

	/*
	 * The DPLL for the pipe must be enabled for this to work.
	 * So enable temporarily it if it's not already enabled.
	 */
	if (!pll_enabled) {
		release_cl_override = IS_CHERRYVIEW(dev_priv) &&
			!chv_phy_powergate_ch(dev_priv, phy, ch, true);

		if (vlv_force_pll_on(dev_priv, pipe, IS_CHERRYVIEW(dev_priv) ?
				     &chv_dpll[0].dpll : &vlv_dpll[0].dpll)) {
			drm_err(&dev_priv->drm,
				"Failed to force on pll for pipe %c!\n",
				pipe_name(pipe));
			return;
		}
	}

	/*
	 * Similar magic as in intel_dp_enable_port().
	 * We _must_ do this port enable + disable trick
	 * to make this power sequencer lock onto the port.
	 * Otherwise even VDD force bit won't work.
	 */
	intel_de_write(dev_priv, intel_dp->output_reg, DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	intel_de_write(dev_priv, intel_dp->output_reg, DP | DP_PORT_EN);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	intel_de_write(dev_priv, intel_dp->output_reg, DP & ~DP_PORT_EN);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	if (!pll_enabled) {
		vlv_force_pll_off(dev_priv, pipe);

		if (release_cl_override)
			chv_phy_powergate_ch(dev_priv, phy, ch, false);
	}
}

static enum pipe vlv_find_free_pps(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	unsigned int pipes = (1 << PIPE_A) | (1 << PIPE_B);

	/*
	 * We don't have power sequencer currently.
	 * Pick one that's not used by other ports.
	 */
	for_each_intel_dp(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (encoder->type == INTEL_OUTPUT_EDP) {
			drm_WARN_ON(&dev_priv->drm,
				    intel_dp->active_pipe != INVALID_PIPE &&
				    intel_dp->active_pipe !=
				    intel_dp->pps_pipe);

			if (intel_dp->pps_pipe != INVALID_PIPE)
				pipes &= ~(1 << intel_dp->pps_pipe);
		} else {
			drm_WARN_ON(&dev_priv->drm,
				    intel_dp->pps_pipe != INVALID_PIPE);

			if (intel_dp->active_pipe != INVALID_PIPE)
				pipes &= ~(1 << intel_dp->active_pipe);
		}
	}

	if (pipes == 0)
		return INVALID_PIPE;

	return ffs(pipes) - 1;
}

static enum pipe
vlv_power_sequencer_pipe(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe;

	lockdep_assert_held(&dev_priv->pps_mutex);

	/* We should never land here with regular DP ports */
	drm_WARN_ON(&dev_priv->drm, !intel_dp_is_edp(intel_dp));

	drm_WARN_ON(&dev_priv->drm, intel_dp->active_pipe != INVALID_PIPE &&
		    intel_dp->active_pipe != intel_dp->pps_pipe);

	if (intel_dp->pps_pipe != INVALID_PIPE)
		return intel_dp->pps_pipe;

	pipe = vlv_find_free_pps(dev_priv);

	/*
	 * Didn't find one. This should not happen since there
	 * are two power sequencers and up to two eDP ports.
	 */
	if (drm_WARN_ON(&dev_priv->drm, pipe == INVALID_PIPE))
		pipe = PIPE_A;

	vlv_steal_power_sequencer(dev_priv, pipe);
	intel_dp->pps_pipe = pipe;

	drm_dbg_kms(&dev_priv->drm,
		    "picked pipe %c power sequencer for [ENCODER:%d:%s]\n",
		    pipe_name(intel_dp->pps_pipe),
		    dig_port->base.base.base.id,
		    dig_port->base.base.name);

	/* init power sequencer on this pipe and port */
	intel_dp_init_panel_power_sequencer(intel_dp);
	intel_dp_init_panel_power_sequencer_registers(intel_dp, true);

	/*
	 * Even vdd force doesn't work until we've made
	 * the power sequencer lock in on the port.
	 */
	vlv_power_sequencer_kick(intel_dp);

	return intel_dp->pps_pipe;
}

static int
bxt_power_sequencer_idx(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	int backlight_controller = dev_priv->vbt.backlight.controller;

	lockdep_assert_held(&dev_priv->pps_mutex);

	/* We should never land here with regular DP ports */
	drm_WARN_ON(&dev_priv->drm, !intel_dp_is_edp(intel_dp));

	if (!intel_dp->pps_reset)
		return backlight_controller;

	intel_dp->pps_reset = false;

	/*
	 * Only the HW needs to be reprogrammed, the SW state is fixed and
	 * has been setup during connector init.
	 */
	intel_dp_init_panel_power_sequencer_registers(intel_dp, false);

	return backlight_controller;
}

typedef bool (*vlv_pipe_check)(struct drm_i915_private *dev_priv,
			       enum pipe pipe);

static bool vlv_pipe_has_pp_on(struct drm_i915_private *dev_priv,
			       enum pipe pipe)
{
	return intel_de_read(dev_priv, PP_STATUS(pipe)) & PP_ON;
}

static bool vlv_pipe_has_vdd_on(struct drm_i915_private *dev_priv,
				enum pipe pipe)
{
	return intel_de_read(dev_priv, PP_CONTROL(pipe)) & EDP_FORCE_VDD;
}

static bool vlv_pipe_any(struct drm_i915_private *dev_priv,
			 enum pipe pipe)
{
	return true;
}

static enum pipe
vlv_initial_pps_pipe(struct drm_i915_private *dev_priv,
		     enum port port,
		     vlv_pipe_check pipe_check)
{
	enum pipe pipe;

	for (pipe = PIPE_A; pipe <= PIPE_B; pipe++) {
		u32 port_sel = intel_de_read(dev_priv, PP_ON_DELAYS(pipe)) &
			PANEL_PORT_SELECT_MASK;

		if (port_sel != PANEL_PORT_SELECT_VLV(port))
			continue;

		if (!pipe_check(dev_priv, pipe))
			continue;

		return pipe;
	}

	return INVALID_PIPE;
}

static void
vlv_initial_power_sequencer_setup(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum port port = dig_port->base.port;

	lockdep_assert_held(&dev_priv->pps_mutex);

	/* try to find a pipe with this port selected */
	/* first pick one where the panel is on */
	intel_dp->pps_pipe = vlv_initial_pps_pipe(dev_priv, port,
						  vlv_pipe_has_pp_on);
	/* didn't find one? pick one where vdd is on */
	if (intel_dp->pps_pipe == INVALID_PIPE)
		intel_dp->pps_pipe = vlv_initial_pps_pipe(dev_priv, port,
							  vlv_pipe_has_vdd_on);
	/* didn't find one? pick one with just the correct port */
	if (intel_dp->pps_pipe == INVALID_PIPE)
		intel_dp->pps_pipe = vlv_initial_pps_pipe(dev_priv, port,
							  vlv_pipe_any);

	/* didn't find one? just let vlv_power_sequencer_pipe() pick one when needed */
	if (intel_dp->pps_pipe == INVALID_PIPE) {
		drm_dbg_kms(&dev_priv->drm,
			    "no initial power sequencer for [ENCODER:%d:%s]\n",
			    dig_port->base.base.base.id,
			    dig_port->base.base.name);
		return;
	}

	drm_dbg_kms(&dev_priv->drm,
		    "initial power sequencer for [ENCODER:%d:%s]: pipe %c\n",
		    dig_port->base.base.base.id,
		    dig_port->base.base.name,
		    pipe_name(intel_dp->pps_pipe));

	intel_dp_init_panel_power_sequencer(intel_dp);
	intel_dp_init_panel_power_sequencer_registers(intel_dp, false);
}

void intel_power_sequencer_reset(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	if (drm_WARN_ON(&dev_priv->drm,
			!(IS_VALLEYVIEW(dev_priv) ||
			  IS_CHERRYVIEW(dev_priv) ||
			  IS_GEN9_LP(dev_priv))))
		return;

	/*
	 * We can't grab pps_mutex here due to deadlock with power_domain
	 * mutex when power_domain functions are called while holding pps_mutex.
	 * That also means that in order to use pps_pipe the code needs to
	 * hold both a power domain reference and pps_mutex, and the power domain
	 * reference get/put must be done while _not_ holding pps_mutex.
	 * pps_{lock,unlock}() do these steps in the correct order, so one
	 * should use them always.
	 */

	for_each_intel_dp(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		drm_WARN_ON(&dev_priv->drm,
			    intel_dp->active_pipe != INVALID_PIPE);

		if (encoder->type != INTEL_OUTPUT_EDP)
			continue;

		if (IS_GEN9_LP(dev_priv))
			intel_dp->pps_reset = true;
		else
			intel_dp->pps_pipe = INVALID_PIPE;
	}
}

struct pps_registers {
	i915_reg_t pp_ctrl;
	i915_reg_t pp_stat;
	i915_reg_t pp_on;
	i915_reg_t pp_off;
	i915_reg_t pp_div;
};

static void intel_pps_get_registers(struct intel_dp *intel_dp,
				    struct pps_registers *regs)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	int pps_idx = 0;

	memset(regs, 0, sizeof(*regs));

	if (IS_GEN9_LP(dev_priv))
		pps_idx = bxt_power_sequencer_idx(intel_dp);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		pps_idx = vlv_power_sequencer_pipe(intel_dp);

	regs->pp_ctrl = PP_CONTROL(pps_idx);
	regs->pp_stat = PP_STATUS(pps_idx);
	regs->pp_on = PP_ON_DELAYS(pps_idx);
	regs->pp_off = PP_OFF_DELAYS(pps_idx);

	/* Cycle delay moved from PP_DIVISOR to PP_CONTROL */
	if (IS_GEN9_LP(dev_priv) || INTEL_PCH_TYPE(dev_priv) >= PCH_CNP)
		regs->pp_div = INVALID_MMIO_REG;
	else
		regs->pp_div = PP_DIVISOR(pps_idx);
}

static i915_reg_t
_pp_ctrl_reg(struct intel_dp *intel_dp)
{
	struct pps_registers regs;

	intel_pps_get_registers(intel_dp, &regs);

	return regs.pp_ctrl;
}

static i915_reg_t
_pp_stat_reg(struct intel_dp *intel_dp)
{
	struct pps_registers regs;

	intel_pps_get_registers(intel_dp, &regs);

	return regs.pp_stat;
}

/* Reboot notifier handler to shutdown panel power to guarantee T12 timing
   This function only applicable when panel PM state is not to be tracked */
static int edp_notify_handler(struct notifier_block *this, unsigned long code,
			      void *unused)
{
	struct intel_dp *intel_dp = container_of(this, typeof(* intel_dp),
						 edp_notifier);
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp) || code != SYS_RESTART)
		return 0;

	with_pps_lock(intel_dp, wakeref) {
		if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
			enum pipe pipe = vlv_power_sequencer_pipe(intel_dp);
			i915_reg_t pp_ctrl_reg, pp_div_reg;
			u32 pp_div;

			pp_ctrl_reg = PP_CONTROL(pipe);
			pp_div_reg  = PP_DIVISOR(pipe);
			pp_div = intel_de_read(dev_priv, pp_div_reg);
			pp_div &= PP_REFERENCE_DIVIDER_MASK;

			/* 0x1F write to PP_DIV_REG sets max cycle delay */
			intel_de_write(dev_priv, pp_div_reg, pp_div | 0x1F);
			intel_de_write(dev_priv, pp_ctrl_reg,
				       PANEL_UNLOCK_REGS);
			msleep(intel_dp->panel_power_cycle_delay);
		}
	}

	return 0;
}

static bool edp_have_panel_power(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->pps_mutex);

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    intel_dp->pps_pipe == INVALID_PIPE)
		return false;

	return (intel_de_read(dev_priv, _pp_stat_reg(intel_dp)) & PP_ON) != 0;
}

static bool edp_have_panel_vdd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->pps_mutex);

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    intel_dp->pps_pipe == INVALID_PIPE)
		return false;

	return intel_de_read(dev_priv, _pp_ctrl_reg(intel_dp)) & EDP_FORCE_VDD;
}

static void
intel_dp_check_edp(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	if (!edp_have_panel_power(intel_dp) && !edp_have_panel_vdd(intel_dp)) {
		drm_WARN(&dev_priv->drm, 1,
			 "eDP powered off while attempting aux channel communication.\n");
		drm_dbg_kms(&dev_priv->drm, "Status 0x%08x Control 0x%08x\n",
			    intel_de_read(dev_priv, _pp_stat_reg(intel_dp)),
			    intel_de_read(dev_priv, _pp_ctrl_reg(intel_dp)));
	}
}

static u32
intel_dp_aux_wait_done(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	i915_reg_t ch_ctl = intel_dp->aux_ch_ctl_reg(intel_dp);
	const unsigned int timeout_ms = 10;
	u32 status;
	bool done;

#define C (((status = intel_uncore_read_notrace(&i915->uncore, ch_ctl)) & DP_AUX_CH_CTL_SEND_BUSY) == 0)
	done = wait_event_timeout(i915->gmbus_wait_queue, C,
				  msecs_to_jiffies_timeout(timeout_ms));

	/* just trace the final value */
	trace_i915_reg_rw(false, ch_ctl, status, sizeof(status), true);

	if (!done)
		drm_err(&i915->drm,
			"%s: did not complete or timeout within %ums (status 0x%08x)\n",
			intel_dp->aux.name, timeout_ms, status);
#undef C

	return status;
}

static u32 g4x_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (index)
		return 0;

	/*
	 * The clock divider is based off the hrawclk, and would like to run at
	 * 2MHz.  So, take the hrawclk value and divide by 2000 and use that
	 */
	return DIV_ROUND_CLOSEST(RUNTIME_INFO(dev_priv)->rawclk_freq, 2000);
}

static u32 ilk_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 freq;

	if (index)
		return 0;

	/*
	 * The clock divider is based off the cdclk or PCH rawclk, and would
	 * like to run at 2MHz.  So, take the cdclk or PCH rawclk value and
	 * divide by 2000 and use that
	 */
	if (dig_port->aux_ch == AUX_CH_A)
		freq = dev_priv->cdclk.hw.cdclk;
	else
		freq = RUNTIME_INFO(dev_priv)->rawclk_freq;
	return DIV_ROUND_CLOSEST(freq, 2000);
}

static u32 hsw_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	if (dig_port->aux_ch != AUX_CH_A && HAS_PCH_LPT_H(dev_priv)) {
		/* Workaround for non-ULT HSW */
		switch (index) {
		case 0: return 63;
		case 1: return 72;
		default: return 0;
		}
	}

	return ilk_get_aux_clock_divider(intel_dp, index);
}

static u32 skl_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	/*
	 * SKL doesn't need us to program the AUX clock divider (Hardware will
	 * derive the clock from CDCLK automatically). We still implement the
	 * get_aux_clock_divider vfunc to plug-in into the existing code.
	 */
	return index ? 0 : 1;
}

static u32 g4x_get_aux_send_ctl(struct intel_dp *intel_dp,
				int send_bytes,
				u32 aux_clock_divider)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv =
			to_i915(dig_port->base.base.dev);
	u32 precharge, timeout;

	if (IS_GEN(dev_priv, 6))
		precharge = 3;
	else
		precharge = 5;

	if (IS_BROADWELL(dev_priv))
		timeout = DP_AUX_CH_CTL_TIME_OUT_600us;
	else
		timeout = DP_AUX_CH_CTL_TIME_OUT_400us;

	return DP_AUX_CH_CTL_SEND_BUSY |
	       DP_AUX_CH_CTL_DONE |
	       DP_AUX_CH_CTL_INTERRUPT |
	       DP_AUX_CH_CTL_TIME_OUT_ERROR |
	       timeout |
	       DP_AUX_CH_CTL_RECEIVE_ERROR |
	       (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
	       (precharge << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
	       (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT);
}

static u32 skl_get_aux_send_ctl(struct intel_dp *intel_dp,
				int send_bytes,
				u32 unused)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *i915 =
			to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);
	u32 ret;

	ret = DP_AUX_CH_CTL_SEND_BUSY |
	      DP_AUX_CH_CTL_DONE |
	      DP_AUX_CH_CTL_INTERRUPT |
	      DP_AUX_CH_CTL_TIME_OUT_ERROR |
	      DP_AUX_CH_CTL_TIME_OUT_MAX |
	      DP_AUX_CH_CTL_RECEIVE_ERROR |
	      (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
	      DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(32) |
	      DP_AUX_CH_CTL_SYNC_PULSE_SKL(32);

	if (intel_phy_is_tc(i915, phy) &&
	    dig_port->tc_mode == TC_PORT_TBT_ALT)
		ret |= DP_AUX_CH_CTL_TBT_IO;

	return ret;
}

static int
intel_dp_aux_xfer(struct intel_dp *intel_dp,
		  const u8 *send, int send_bytes,
		  u8 *recv, int recv_size,
		  u32 aux_send_ctl_flags)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *i915 =
			to_i915(dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);
	bool is_tc_port = intel_phy_is_tc(i915, phy);
	i915_reg_t ch_ctl, ch_data[5];
	u32 aux_clock_divider;
	enum intel_display_power_domain aux_domain;
	intel_wakeref_t aux_wakeref;
	intel_wakeref_t pps_wakeref;
	int i, ret, recv_bytes;
	int try, clock = 0;
	u32 status;
	bool vdd;

	ch_ctl = intel_dp->aux_ch_ctl_reg(intel_dp);
	for (i = 0; i < ARRAY_SIZE(ch_data); i++)
		ch_data[i] = intel_dp->aux_ch_data_reg(intel_dp, i);

	if (is_tc_port)
		intel_tc_port_lock(dig_port);

	aux_domain = intel_aux_power_domain(dig_port);

	aux_wakeref = intel_display_power_get(i915, aux_domain);
	pps_wakeref = pps_lock(intel_dp);

	/*
	 * We will be called with VDD already enabled for dpcd/edid/oui reads.
	 * In such cases we want to leave VDD enabled and it's up to upper layers
	 * to turn it off. But for eg. i2c-dev access we need to turn it on/off
	 * ourselves.
	 */
	vdd = edp_panel_vdd_on(intel_dp);

	/* dp aux is extremely sensitive to irq latency, hence request the
	 * lowest possible wakeup latency and so prevent the cpu from going into
	 * deep sleep states.
	 */
	cpu_latency_qos_update_request(&i915->pm_qos, 0);

	intel_dp_check_edp(intel_dp);

	/* Try to wait for any previous AUX channel activity */
	for (try = 0; try < 3; try++) {
		status = intel_uncore_read_notrace(uncore, ch_ctl);
		if ((status & DP_AUX_CH_CTL_SEND_BUSY) == 0)
			break;
		msleep(1);
	}
	/* just trace the final value */
	trace_i915_reg_rw(false, ch_ctl, status, sizeof(status), true);

	if (try == 3) {
		const u32 status = intel_uncore_read(uncore, ch_ctl);

		if (status != intel_dp->aux_busy_last_status) {
			drm_WARN(&i915->drm, 1,
				 "%s: not started (status 0x%08x)\n",
				 intel_dp->aux.name, status);
			intel_dp->aux_busy_last_status = status;
		}

		ret = -EBUSY;
		goto out;
	}

	/* Only 5 data registers! */
	if (drm_WARN_ON(&i915->drm, send_bytes > 20 || recv_size > 20)) {
		ret = -E2BIG;
		goto out;
	}

	while ((aux_clock_divider = intel_dp->get_aux_clock_divider(intel_dp, clock++))) {
		u32 send_ctl = intel_dp->get_aux_send_ctl(intel_dp,
							  send_bytes,
							  aux_clock_divider);

		send_ctl |= aux_send_ctl_flags;

		/* Must try at least 3 times according to DP spec */
		for (try = 0; try < 5; try++) {
			/* Load the send data into the aux channel data registers */
			for (i = 0; i < send_bytes; i += 4)
				intel_uncore_write(uncore,
						   ch_data[i >> 2],
						   intel_dp_pack_aux(send + i,
								     send_bytes - i));

			/* Send the command and wait for it to complete */
			intel_uncore_write(uncore, ch_ctl, send_ctl);

			status = intel_dp_aux_wait_done(intel_dp);

			/* Clear done status and any errors */
			intel_uncore_write(uncore,
					   ch_ctl,
					   status |
					   DP_AUX_CH_CTL_DONE |
					   DP_AUX_CH_CTL_TIME_OUT_ERROR |
					   DP_AUX_CH_CTL_RECEIVE_ERROR);

			/* DP CTS 1.2 Core Rev 1.1, 4.2.1.1 & 4.2.1.2
			 *   400us delay required for errors and timeouts
			 *   Timeout errors from the HW already meet this
			 *   requirement so skip to next iteration
			 */
			if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR)
				continue;

			if (status & DP_AUX_CH_CTL_RECEIVE_ERROR) {
				usleep_range(400, 500);
				continue;
			}
			if (status & DP_AUX_CH_CTL_DONE)
				goto done;
		}
	}

	if ((status & DP_AUX_CH_CTL_DONE) == 0) {
		drm_err(&i915->drm, "%s: not done (status 0x%08x)\n",
			intel_dp->aux.name, status);
		ret = -EBUSY;
		goto out;
	}

done:
	/* Check for timeout or receive error.
	 * Timeouts occur when the sink is not connected
	 */
	if (status & DP_AUX_CH_CTL_RECEIVE_ERROR) {
		drm_err(&i915->drm, "%s: receive error (status 0x%08x)\n",
			intel_dp->aux.name, status);
		ret = -EIO;
		goto out;
	}

	/* Timeouts occur when the device isn't connected, so they're
	 * "normal" -- don't fill the kernel log with these */
	if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR) {
		drm_dbg_kms(&i915->drm, "%s: timeout (status 0x%08x)\n",
			    intel_dp->aux.name, status);
		ret = -ETIMEDOUT;
		goto out;
	}

	/* Unload any bytes sent back from the other side */
	recv_bytes = ((status & DP_AUX_CH_CTL_MESSAGE_SIZE_MASK) >>
		      DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT);

	/*
	 * By BSpec: "Message sizes of 0 or >20 are not allowed."
	 * We have no idea of what happened so we return -EBUSY so
	 * drm layer takes care for the necessary retries.
	 */
	if (recv_bytes == 0 || recv_bytes > 20) {
		drm_dbg_kms(&i915->drm,
			    "%s: Forbidden recv_bytes = %d on aux transaction\n",
			    intel_dp->aux.name, recv_bytes);
		ret = -EBUSY;
		goto out;
	}

	if (recv_bytes > recv_size)
		recv_bytes = recv_size;

	for (i = 0; i < recv_bytes; i += 4)
		intel_dp_unpack_aux(intel_uncore_read(uncore, ch_data[i >> 2]),
				    recv + i, recv_bytes - i);

	ret = recv_bytes;
out:
	cpu_latency_qos_update_request(&i915->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (vdd)
		edp_panel_vdd_off(intel_dp, false);

	pps_unlock(intel_dp, pps_wakeref);
	intel_display_power_put_async(i915, aux_domain, aux_wakeref);

	if (is_tc_port)
		intel_tc_port_unlock(dig_port);

	return ret;
}

#define BARE_ADDRESS_SIZE	3
#define HEADER_SIZE		(BARE_ADDRESS_SIZE + 1)

static void
intel_dp_aux_header(u8 txbuf[HEADER_SIZE],
		    const struct drm_dp_aux_msg *msg)
{
	txbuf[0] = (msg->request << 4) | ((msg->address >> 16) & 0xf);
	txbuf[1] = (msg->address >> 8) & 0xff;
	txbuf[2] = msg->address & 0xff;
	txbuf[3] = msg->size - 1;
}

static u32 intel_dp_aux_xfer_flags(const struct drm_dp_aux_msg *msg)
{
	/*
	 * If we're trying to send the HDCP Aksv, we need to set a the Aksv
	 * select bit to inform the hardware to send the Aksv after our header
	 * since we can't access that data from software.
	 */
	if ((msg->request & ~DP_AUX_I2C_MOT) == DP_AUX_NATIVE_WRITE &&
	    msg->address == DP_AUX_HDCP_AKSV)
		return DP_AUX_CH_CTL_AUX_AKSV_SELECT;

	return 0;
}

static ssize_t
intel_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct intel_dp *intel_dp = container_of(aux, struct intel_dp, aux);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 txbuf[20], rxbuf[20];
	size_t txsize, rxsize;
	u32 flags = intel_dp_aux_xfer_flags(msg);
	int ret;

	intel_dp_aux_header(txbuf, msg);

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		txsize = msg->size ? HEADER_SIZE + msg->size : BARE_ADDRESS_SIZE;
		rxsize = 2; /* 0 or 1 data bytes */

		if (drm_WARN_ON(&i915->drm, txsize > 20))
			return -E2BIG;

		drm_WARN_ON(&i915->drm, !msg->buffer != !msg->size);

		if (msg->buffer)
			memcpy(txbuf + HEADER_SIZE, msg->buffer, msg->size);

		ret = intel_dp_aux_xfer(intel_dp, txbuf, txsize,
					rxbuf, rxsize, flags);
		if (ret > 0) {
			msg->reply = rxbuf[0] >> 4;

			if (ret > 1) {
				/* Number of bytes written in a short write. */
				ret = clamp_t(int, rxbuf[1], 0, msg->size);
			} else {
				/* Return payload size. */
				ret = msg->size;
			}
		}
		break;

	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		txsize = msg->size ? HEADER_SIZE : BARE_ADDRESS_SIZE;
		rxsize = msg->size + 1;

		if (drm_WARN_ON(&i915->drm, rxsize > 20))
			return -E2BIG;

		ret = intel_dp_aux_xfer(intel_dp, txbuf, txsize,
					rxbuf, rxsize, flags);
		if (ret > 0) {
			msg->reply = rxbuf[0] >> 4;
			/*
			 * Assume happy day, and copy the data. The caller is
			 * expected to check msg->reply before touching it.
			 *
			 * Return payload size.
			 */
			ret--;
			memcpy(msg->buffer, rxbuf + 1, ret);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}


static i915_reg_t g4x_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_CTL(AUX_CH_B);
	}
}

static i915_reg_t g4x_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_DATA(AUX_CH_B, index);
	}
}

static i915_reg_t ilk_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
		return DP_AUX_CH_CTL(aux_ch);
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return PCH_DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_CTL(AUX_CH_A);
	}
}

static i915_reg_t ilk_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
		return DP_AUX_CH_DATA(aux_ch, index);
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
		return PCH_DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_DATA(AUX_CH_A, index);
	}
}

static i915_reg_t skl_aux_ctl_reg(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
	case AUX_CH_E:
	case AUX_CH_F:
	case AUX_CH_G:
		return DP_AUX_CH_CTL(aux_ch);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_CTL(AUX_CH_A);
	}
}

static i915_reg_t skl_aux_data_reg(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum aux_ch aux_ch = dig_port->aux_ch;

	switch (aux_ch) {
	case AUX_CH_A:
	case AUX_CH_B:
	case AUX_CH_C:
	case AUX_CH_D:
	case AUX_CH_E:
	case AUX_CH_F:
	case AUX_CH_G:
		return DP_AUX_CH_DATA(aux_ch, index);
	default:
		MISSING_CASE(aux_ch);
		return DP_AUX_CH_DATA(AUX_CH_A, index);
	}
}

static void
intel_dp_aux_fini(struct intel_dp *intel_dp)
{
	kfree(intel_dp->aux.name);
}

static void
intel_dp_aux_init(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;

	if (INTEL_GEN(dev_priv) >= 9) {
		intel_dp->aux_ch_ctl_reg = skl_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = skl_aux_data_reg;
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		intel_dp->aux_ch_ctl_reg = ilk_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = ilk_aux_data_reg;
	} else {
		intel_dp->aux_ch_ctl_reg = g4x_aux_ctl_reg;
		intel_dp->aux_ch_data_reg = g4x_aux_data_reg;
	}

	if (INTEL_GEN(dev_priv) >= 9)
		intel_dp->get_aux_clock_divider = skl_get_aux_clock_divider;
	else if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
		intel_dp->get_aux_clock_divider = hsw_get_aux_clock_divider;
	else if (HAS_PCH_SPLIT(dev_priv))
		intel_dp->get_aux_clock_divider = ilk_get_aux_clock_divider;
	else
		intel_dp->get_aux_clock_divider = g4x_get_aux_clock_divider;

	if (INTEL_GEN(dev_priv) >= 9)
		intel_dp->get_aux_send_ctl = skl_get_aux_send_ctl;
	else
		intel_dp->get_aux_send_ctl = g4x_get_aux_send_ctl;

	drm_dp_aux_init(&intel_dp->aux);

	/* Failure to allocate our preferred name is not critical */
	intel_dp->aux.name = kasprintf(GFP_KERNEL, "AUX %c/port %c",
				       aux_ch_name(dig_port->aux_ch),
				       port_name(encoder->port));
	intel_dp->aux.transfer = intel_dp_aux_transfer;
}

bool intel_dp_source_supports_hbr2(struct intel_dp *intel_dp)
{
	int max_rate = intel_dp->source_rates[intel_dp->num_source_rates - 1];

	return max_rate >= 540000;
}

bool intel_dp_source_supports_hbr3(struct intel_dp *intel_dp)
{
	int max_rate = intel_dp->source_rates[intel_dp->num_source_rates - 1];

	return max_rate >= 810000;
}

static void
intel_dp_set_clock(struct intel_encoder *encoder,
		   struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct dp_link_dpll *divisor = NULL;
	int i, count = 0;

	if (IS_G4X(dev_priv)) {
		divisor = g4x_dpll;
		count = ARRAY_SIZE(g4x_dpll);
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		divisor = pch_dpll;
		count = ARRAY_SIZE(pch_dpll);
	} else if (IS_CHERRYVIEW(dev_priv)) {
		divisor = chv_dpll;
		count = ARRAY_SIZE(chv_dpll);
	} else if (IS_VALLEYVIEW(dev_priv)) {
		divisor = vlv_dpll;
		count = ARRAY_SIZE(vlv_dpll);
	}

	if (divisor && count) {
		for (i = 0; i < count; i++) {
			if (pipe_config->port_clock == divisor[i].clock) {
				pipe_config->dpll = divisor[i].dpll;
				pipe_config->clock_set = true;
				break;
			}
		}
	}
}

static void snprintf_int_array(char *str, size_t len,
			       const int *array, int nelem)
{
	int i;

	str[0] = '\0';

	for (i = 0; i < nelem; i++) {
		int r = snprintf(str, len, "%s%d", i ? ", " : "", array[i]);
		if (r >= len)
			return;
		str += r;
		len -= r;
	}
}

static void intel_dp_print_rates(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	char str[128]; /* FIXME: too big for stack? */

	if (!drm_debug_enabled(DRM_UT_KMS))
		return;

	snprintf_int_array(str, sizeof(str),
			   intel_dp->source_rates, intel_dp->num_source_rates);
	drm_dbg_kms(&i915->drm, "source rates: %s\n", str);

	snprintf_int_array(str, sizeof(str),
			   intel_dp->sink_rates, intel_dp->num_sink_rates);
	drm_dbg_kms(&i915->drm, "sink rates: %s\n", str);

	snprintf_int_array(str, sizeof(str),
			   intel_dp->common_rates, intel_dp->num_common_rates);
	drm_dbg_kms(&i915->drm, "common rates: %s\n", str);
}

int
intel_dp_max_link_rate(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int len;

	len = intel_dp_common_len_rate_limit(intel_dp, intel_dp->max_link_rate);
	if (drm_WARN_ON(&i915->drm, len <= 0))
		return 162000;

	return intel_dp->common_rates[len - 1];
}

int intel_dp_rate_select(struct intel_dp *intel_dp, int rate)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int i = intel_dp_rate_index(intel_dp->sink_rates,
				    intel_dp->num_sink_rates, rate);

	if (drm_WARN_ON(&i915->drm, i < 0))
		i = 0;

	return i;
}

void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock,
			   u8 *link_bw, u8 *rate_select)
{
	/* eDP 1.4 rate select method. */
	if (intel_dp->use_rate_select) {
		*link_bw = 0;
		*rate_select =
			intel_dp_rate_select(intel_dp, port_clock);
	} else {
		*link_bw = drm_dp_link_rate_to_bw_code(port_clock);
		*rate_select = 0;
	}
}

static bool intel_dp_source_supports_fec(struct intel_dp *intel_dp,
					 const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	/* On TGL, FEC is supported on all Pipes */
	if (INTEL_GEN(dev_priv) >= 12)
		return true;

	if (IS_GEN(dev_priv, 11) && pipe_config->cpu_transcoder != TRANSCODER_A)
		return true;

	return false;
}

static bool intel_dp_supports_fec(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *pipe_config)
{
	return intel_dp_source_supports_fec(intel_dp, pipe_config) &&
		drm_dp_sink_supports_fec(intel_dp->fec_capable);
}

static bool intel_dp_supports_dsc(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;

	if (!intel_dp_is_edp(intel_dp) && !crtc_state->fec_enable)
		return false;

	return intel_dsc_source_support(encoder, crtc_state) &&
		drm_dp_sink_supports_dsc(intel_dp->dsc_dpcd);
}

static bool intel_dp_hdmi_ycbcr420(struct intel_dp *intel_dp,
				   const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 ||
		(crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444 &&
		 intel_dp->dfp.ycbcr_444_to_420);
}

static int intel_dp_hdmi_tmds_clock(struct intel_dp *intel_dp,
				    const struct intel_crtc_state *crtc_state, int bpc)
{
	int clock = crtc_state->hw.adjusted_mode.crtc_clock * bpc / 8;

	if (intel_dp_hdmi_ycbcr420(intel_dp, crtc_state))
		clock /= 2;

	return clock;
}

static bool intel_dp_hdmi_tmds_clock_valid(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state, int bpc)
{
	int tmds_clock = intel_dp_hdmi_tmds_clock(intel_dp, crtc_state, bpc);

	if (intel_dp->dfp.min_tmds_clock &&
	    tmds_clock < intel_dp->dfp.min_tmds_clock)
		return false;

	if (intel_dp->dfp.max_tmds_clock &&
	    tmds_clock > intel_dp->dfp.max_tmds_clock)
		return false;

	return true;
}

static bool intel_dp_hdmi_deep_color_possible(struct intel_dp *intel_dp,
					      const struct intel_crtc_state *crtc_state,
					      int bpc)
{

	return intel_hdmi_deep_color_possible(crtc_state, bpc,
					      intel_dp->has_hdmi_sink,
					      intel_dp_hdmi_ycbcr420(intel_dp, crtc_state)) &&
		intel_dp_hdmi_tmds_clock_valid(intel_dp, crtc_state, bpc);
}

static int intel_dp_max_bpp(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	int bpp, bpc;

	bpc = crtc_state->pipe_bpp / 3;

	if (intel_dp->dfp.max_bpc)
		bpc = min_t(int, bpc, intel_dp->dfp.max_bpc);

	if (intel_dp->dfp.min_tmds_clock) {
		for (; bpc >= 10; bpc -= 2) {
			if (intel_dp_hdmi_deep_color_possible(intel_dp, crtc_state, bpc))
				break;
		}
	}

	bpp = bpc * 3;
	if (intel_dp_is_edp(intel_dp)) {
		/* Get bpp from vbt only for panels that dont have bpp in edid */
		if (intel_connector->base.display_info.bpc == 0 &&
		    dev_priv->vbt.edp.bpp && dev_priv->vbt.edp.bpp < bpp) {
			drm_dbg_kms(&dev_priv->drm,
				    "clamping bpp for eDP panel to BIOS-provided %i\n",
				    dev_priv->vbt.edp.bpp);
			bpp = dev_priv->vbt.edp.bpp;
		}
	}

	return bpp;
}

/* Adjust link config limits based on compliance test requests. */
void
intel_dp_adjust_compliance_config(struct intel_dp *intel_dp,
				  struct intel_crtc_state *pipe_config,
				  struct link_config_limits *limits)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	/* For DP Compliance we override the computed bpp for the pipe */
	if (intel_dp->compliance.test_data.bpc != 0) {
		int bpp = 3 * intel_dp->compliance.test_data.bpc;

		limits->min_bpp = limits->max_bpp = bpp;
		pipe_config->dither_force_disable = bpp == 6 * 3;

		drm_dbg_kms(&i915->drm, "Setting pipe_bpp to %d\n", bpp);
	}

	/* Use values requested by Compliance Test Request */
	if (intel_dp->compliance.test_type == DP_TEST_LINK_TRAINING) {
		int index;

		/* Validate the compliance test data since max values
		 * might have changed due to link train fallback.
		 */
		if (intel_dp_link_params_valid(intel_dp, intel_dp->compliance.test_link_rate,
					       intel_dp->compliance.test_lane_count)) {
			index = intel_dp_rate_index(intel_dp->common_rates,
						    intel_dp->num_common_rates,
						    intel_dp->compliance.test_link_rate);
			if (index >= 0)
				limits->min_clock = limits->max_clock = index;
			limits->min_lane_count = limits->max_lane_count =
				intel_dp->compliance.test_lane_count;
		}
	}
}

/* Optimize link config in order: max bpp, min clock, min lanes */
static int
intel_dp_compute_link_config_wide(struct intel_dp *intel_dp,
				  struct intel_crtc_state *pipe_config,
				  const struct link_config_limits *limits)
{
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	int bpp, clock, lane_count;
	int mode_rate, link_clock, link_avail;

	for (bpp = limits->max_bpp; bpp >= limits->min_bpp; bpp -= 2 * 3) {
		int output_bpp = intel_dp_output_bpp(pipe_config->output_format, bpp);

		mode_rate = intel_dp_link_required(adjusted_mode->crtc_clock,
						   output_bpp);

		for (clock = limits->min_clock; clock <= limits->max_clock; clock++) {
			for (lane_count = limits->min_lane_count;
			     lane_count <= limits->max_lane_count;
			     lane_count <<= 1) {
				link_clock = intel_dp->common_rates[clock];
				link_avail = intel_dp_max_data_rate(link_clock,
								    lane_count);

				if (mode_rate <= link_avail) {
					pipe_config->lane_count = lane_count;
					pipe_config->pipe_bpp = bpp;
					pipe_config->port_clock = link_clock;

					return 0;
				}
			}
		}
	}

	return -EINVAL;
}

static int intel_dp_dsc_compute_bpp(struct intel_dp *intel_dp, u8 dsc_max_bpc)
{
	int i, num_bpc;
	u8 dsc_bpc[3] = {0};

	num_bpc = drm_dp_dsc_sink_supported_input_bpcs(intel_dp->dsc_dpcd,
						       dsc_bpc);
	for (i = 0; i < num_bpc; i++) {
		if (dsc_max_bpc >= dsc_bpc[i])
			return dsc_bpc[i] * 3;
	}

	return 0;
}

#define DSC_SUPPORTED_VERSION_MIN		1

static int intel_dp_dsc_compute_params(struct intel_encoder *encoder,
				       struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	u8 line_buf_depth;
	int ret;

	ret = intel_dsc_compute_params(encoder, crtc_state);
	if (ret)
		return ret;

	/*
	 * Slice Height of 8 works for all currently available panels. So start
	 * with that if pic_height is an integral multiple of 8. Eventually add
	 * logic to try multiple slice heights.
	 */
	if (vdsc_cfg->pic_height % 8 == 0)
		vdsc_cfg->slice_height = 8;
	else if (vdsc_cfg->pic_height % 4 == 0)
		vdsc_cfg->slice_height = 4;
	else
		vdsc_cfg->slice_height = 2;

	vdsc_cfg->dsc_version_major =
		(intel_dp->dsc_dpcd[DP_DSC_REV - DP_DSC_SUPPORT] &
		 DP_DSC_MAJOR_MASK) >> DP_DSC_MAJOR_SHIFT;
	vdsc_cfg->dsc_version_minor =
		min(DSC_SUPPORTED_VERSION_MIN,
		    (intel_dp->dsc_dpcd[DP_DSC_REV - DP_DSC_SUPPORT] &
		     DP_DSC_MINOR_MASK) >> DP_DSC_MINOR_SHIFT);

	vdsc_cfg->convert_rgb = intel_dp->dsc_dpcd[DP_DSC_DEC_COLOR_FORMAT_CAP - DP_DSC_SUPPORT] &
		DP_DSC_RGB;

	line_buf_depth = drm_dp_dsc_sink_line_buf_depth(intel_dp->dsc_dpcd);
	if (!line_buf_depth) {
		drm_dbg_kms(&i915->drm,
			    "DSC Sink Line Buffer Depth invalid\n");
		return -EINVAL;
	}

	if (vdsc_cfg->dsc_version_minor == 2)
		vdsc_cfg->line_buf_depth = (line_buf_depth == DSC_1_2_MAX_LINEBUF_DEPTH_BITS) ?
			DSC_1_2_MAX_LINEBUF_DEPTH_VAL : line_buf_depth;
	else
		vdsc_cfg->line_buf_depth = (line_buf_depth > DSC_1_1_MAX_LINEBUF_DEPTH_BITS) ?
			DSC_1_1_MAX_LINEBUF_DEPTH_BITS : line_buf_depth;

	vdsc_cfg->block_pred_enable =
		intel_dp->dsc_dpcd[DP_DSC_BLK_PREDICTION_SUPPORT - DP_DSC_SUPPORT] &
		DP_DSC_BLK_PREDICTION_IS_SUPPORTED;

	return drm_dsc_compute_rc_parameters(vdsc_cfg);
}

static int intel_dp_dsc_compute_config(struct intel_dp *intel_dp,
				       struct intel_crtc_state *pipe_config,
				       struct drm_connector_state *conn_state,
				       struct link_config_limits *limits)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	const struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	u8 dsc_max_bpc;
	int pipe_bpp;
	int ret;

	pipe_config->fec_enable = !intel_dp_is_edp(intel_dp) &&
		intel_dp_supports_fec(intel_dp, pipe_config);

	if (!intel_dp_supports_dsc(intel_dp, pipe_config))
		return -EINVAL;

	/* Max DSC Input BPC for ICL is 10 and for TGL+ is 12 */
	if (INTEL_GEN(dev_priv) >= 12)
		dsc_max_bpc = min_t(u8, 12, conn_state->max_requested_bpc);
	else
		dsc_max_bpc = min_t(u8, 10,
				    conn_state->max_requested_bpc);

	pipe_bpp = intel_dp_dsc_compute_bpp(intel_dp, dsc_max_bpc);

	/* Min Input BPC for ICL+ is 8 */
	if (pipe_bpp < 8 * 3) {
		drm_dbg_kms(&dev_priv->drm,
			    "No DSC support for less than 8bpc\n");
		return -EINVAL;
	}

	/*
	 * For now enable DSC for max bpp, max link rate, max lane count.
	 * Optimize this later for the minimum possible link rate/lane count
	 * with DSC enabled for the requested mode.
	 */
	pipe_config->pipe_bpp = pipe_bpp;
	pipe_config->port_clock = intel_dp->common_rates[limits->max_clock];
	pipe_config->lane_count = limits->max_lane_count;

	if (intel_dp_is_edp(intel_dp)) {
		pipe_config->dsc.compressed_bpp =
			min_t(u16, drm_edp_dsc_sink_output_bpp(intel_dp->dsc_dpcd) >> 4,
			      pipe_config->pipe_bpp);
		pipe_config->dsc.slice_count =
			drm_dp_dsc_sink_max_slice_count(intel_dp->dsc_dpcd,
							true);
	} else {
		u16 dsc_max_output_bpp;
		u8 dsc_dp_slice_count;

		dsc_max_output_bpp =
			intel_dp_dsc_get_output_bpp(dev_priv,
						    pipe_config->port_clock,
						    pipe_config->lane_count,
						    adjusted_mode->crtc_clock,
						    adjusted_mode->crtc_hdisplay);
		dsc_dp_slice_count =
			intel_dp_dsc_get_slice_count(intel_dp,
						     adjusted_mode->crtc_clock,
						     adjusted_mode->crtc_hdisplay);
		if (!dsc_max_output_bpp || !dsc_dp_slice_count) {
			drm_dbg_kms(&dev_priv->drm,
				    "Compressed BPP/Slice Count not supported\n");
			return -EINVAL;
		}
		pipe_config->dsc.compressed_bpp = min_t(u16,
							       dsc_max_output_bpp >> 4,
							       pipe_config->pipe_bpp);
		pipe_config->dsc.slice_count = dsc_dp_slice_count;
	}
	/*
	 * VDSC engine operates at 1 Pixel per clock, so if peak pixel rate
	 * is greater than the maximum Cdclock and if slice count is even
	 * then we need to use 2 VDSC instances.
	 */
	if (adjusted_mode->crtc_clock > dev_priv->max_cdclk_freq) {
		if (pipe_config->dsc.slice_count > 1) {
			pipe_config->dsc.dsc_split = true;
		} else {
			drm_dbg_kms(&dev_priv->drm,
				    "Cannot split stream to use 2 VDSC instances\n");
			return -EINVAL;
		}
	}

	ret = intel_dp_dsc_compute_params(&dig_port->base, pipe_config);
	if (ret < 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "Cannot compute valid DSC parameters for Input Bpp = %d "
			    "Compressed BPP = %d\n",
			    pipe_config->pipe_bpp,
			    pipe_config->dsc.compressed_bpp);
		return ret;
	}

	pipe_config->dsc.compression_enable = true;
	drm_dbg_kms(&dev_priv->drm, "DP DSC computed with Input Bpp = %d "
		    "Compressed Bpp = %d Slice Count = %d\n",
		    pipe_config->pipe_bpp,
		    pipe_config->dsc.compressed_bpp,
		    pipe_config->dsc.slice_count);

	return 0;
}

static int
intel_dp_compute_link_config(struct intel_encoder *encoder,
			     struct intel_crtc_state *pipe_config,
			     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct link_config_limits limits;
	int common_len;
	int ret;

	common_len = intel_dp_common_len_rate_limit(intel_dp,
						    intel_dp->max_link_rate);

	/* No common link rates between source and sink */
	drm_WARN_ON(encoder->base.dev, common_len <= 0);

	limits.min_clock = 0;
	limits.max_clock = common_len - 1;

	limits.min_lane_count = 1;
	limits.max_lane_count = intel_dp_max_lane_count(intel_dp);

	limits.min_bpp = intel_dp_min_bpp(pipe_config->output_format);
	limits.max_bpp = intel_dp_max_bpp(intel_dp, pipe_config);

	if (intel_dp_is_edp(intel_dp)) {
		/*
		 * Use the maximum clock and number of lanes the eDP panel
		 * advertizes being capable of. The panels are generally
		 * designed to support only a single clock and lane
		 * configuration, and typically these values correspond to the
		 * native resolution of the panel.
		 */
		limits.min_lane_count = limits.max_lane_count;
		limits.min_clock = limits.max_clock;
	}

	intel_dp_adjust_compliance_config(intel_dp, pipe_config, &limits);

	drm_dbg_kms(&i915->drm, "DP link computation with max lane count %i "
		    "max rate %d max bpp %d pixel clock %iKHz\n",
		    limits.max_lane_count,
		    intel_dp->common_rates[limits.max_clock],
		    limits.max_bpp, adjusted_mode->crtc_clock);

	/*
	 * Optimize for slow and wide. This is the place to add alternative
	 * optimization policy.
	 */
	ret = intel_dp_compute_link_config_wide(intel_dp, pipe_config, &limits);

	/* enable compression if the mode doesn't fit available BW */
	drm_dbg_kms(&i915->drm, "Force DSC en = %d\n", intel_dp->force_dsc_en);
	if (ret || intel_dp->force_dsc_en) {
		ret = intel_dp_dsc_compute_config(intel_dp, pipe_config,
						  conn_state, &limits);
		if (ret < 0)
			return ret;
	}

	if (pipe_config->dsc.compression_enable) {
		drm_dbg_kms(&i915->drm,
			    "DP lane count %d clock %d Input bpp %d Compressed bpp %d\n",
			    pipe_config->lane_count, pipe_config->port_clock,
			    pipe_config->pipe_bpp,
			    pipe_config->dsc.compressed_bpp);

		drm_dbg_kms(&i915->drm,
			    "DP link rate required %i available %i\n",
			    intel_dp_link_required(adjusted_mode->crtc_clock,
						   pipe_config->dsc.compressed_bpp),
			    intel_dp_max_data_rate(pipe_config->port_clock,
						   pipe_config->lane_count));
	} else {
		drm_dbg_kms(&i915->drm, "DP lane count %d clock %d bpp %d\n",
			    pipe_config->lane_count, pipe_config->port_clock,
			    pipe_config->pipe_bpp);

		drm_dbg_kms(&i915->drm,
			    "DP link rate required %i available %i\n",
			    intel_dp_link_required(adjusted_mode->crtc_clock,
						   pipe_config->pipe_bpp),
			    intel_dp_max_data_rate(pipe_config->port_clock,
						   pipe_config->lane_count));
	}
	return 0;
}

static int
intel_dp_ycbcr420_config(struct intel_crtc_state *crtc_state,
			 const struct drm_connector_state *conn_state)
{
	struct drm_connector *connector = conn_state->connector;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (!connector->ycbcr_420_allowed)
		return 0;

	crtc_state->output_format = intel_dp_output_format(connector, adjusted_mode);

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_YCBCR420)
		return 0;

	return intel_pch_panel_fitting(crtc_state, conn_state);
}

bool intel_dp_limited_color_range(const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	const struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	/*
	 * Our YCbCr output is always limited range.
	 * crtc_state->limited_color_range only applies to RGB,
	 * and it must never be set for YCbCr or we risk setting
	 * some conflicting bits in PIPECONF which will mess up
	 * the colors on the monitor.
	 */
	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB)
		return false;

	if (intel_conn_state->broadcast_rgb == INTEL_BROADCAST_RGB_AUTO) {
		/*
		 * See:
		 * CEA-861-E - 5.1 Default Encoding Parameters
		 * VESA DisplayPort Ver.1.2a - 5.1.1.1 Video Colorimetry
		 */
		return crtc_state->pipe_bpp != 18 &&
			drm_default_rgb_quant_range(adjusted_mode) ==
			HDMI_QUANTIZATION_RANGE_LIMITED;
	} else {
		return intel_conn_state->broadcast_rgb ==
			INTEL_BROADCAST_RGB_LIMITED;
	}
}

static bool intel_dp_port_has_audio(struct drm_i915_private *dev_priv,
				    enum port port)
{
	if (IS_G4X(dev_priv))
		return false;
	if (INTEL_GEN(dev_priv) < 12 && port == PORT_A)
		return false;

	return true;
}

static void intel_dp_compute_vsc_colorimetry(const struct intel_crtc_state *crtc_state,
					     const struct drm_connector_state *conn_state,
					     struct drm_dp_vsc_sdp *vsc)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	/*
	 * Prepare VSC Header for SU as per DP 1.4 spec, Table 2-118
	 * VSC SDP supporting 3D stereo, PSR2, and Pixel Encoding/
	 * Colorimetry Format indication.
	 */
	vsc->revision = 0x5;
	vsc->length = 0x13;

	/* DP 1.4a spec, Table 2-120 */
	switch (crtc_state->output_format) {
	case INTEL_OUTPUT_FORMAT_YCBCR444:
		vsc->pixelformat = DP_PIXELFORMAT_YUV444;
		break;
	case INTEL_OUTPUT_FORMAT_YCBCR420:
		vsc->pixelformat = DP_PIXELFORMAT_YUV420;
		break;
	case INTEL_OUTPUT_FORMAT_RGB:
	default:
		vsc->pixelformat = DP_PIXELFORMAT_RGB;
	}

	switch (conn_state->colorspace) {
	case DRM_MODE_COLORIMETRY_BT709_YCC:
		vsc->colorimetry = DP_COLORIMETRY_BT709_YCC;
		break;
	case DRM_MODE_COLORIMETRY_XVYCC_601:
		vsc->colorimetry = DP_COLORIMETRY_XVYCC_601;
		break;
	case DRM_MODE_COLORIMETRY_XVYCC_709:
		vsc->colorimetry = DP_COLORIMETRY_XVYCC_709;
		break;
	case DRM_MODE_COLORIMETRY_SYCC_601:
		vsc->colorimetry = DP_COLORIMETRY_SYCC_601;
		break;
	case DRM_MODE_COLORIMETRY_OPYCC_601:
		vsc->colorimetry = DP_COLORIMETRY_OPYCC_601;
		break;
	case DRM_MODE_COLORIMETRY_BT2020_CYCC:
		vsc->colorimetry = DP_COLORIMETRY_BT2020_CYCC;
		break;
	case DRM_MODE_COLORIMETRY_BT2020_RGB:
		vsc->colorimetry = DP_COLORIMETRY_BT2020_RGB;
		break;
	case DRM_MODE_COLORIMETRY_BT2020_YCC:
		vsc->colorimetry = DP_COLORIMETRY_BT2020_YCC;
		break;
	case DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65:
	case DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER:
		vsc->colorimetry = DP_COLORIMETRY_DCI_P3_RGB;
		break;
	default:
		/*
		 * RGB->YCBCR color conversion uses the BT.709
		 * color space.
		 */
		if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
			vsc->colorimetry = DP_COLORIMETRY_BT709_YCC;
		else
			vsc->colorimetry = DP_COLORIMETRY_DEFAULT;
		break;
	}

	vsc->bpc = crtc_state->pipe_bpp / 3;

	/* only RGB pixelformat supports 6 bpc */
	drm_WARN_ON(&dev_priv->drm,
		    vsc->bpc == 6 && vsc->pixelformat != DP_PIXELFORMAT_RGB);

	/* all YCbCr are always limited range */
	vsc->dynamic_range = DP_DYNAMIC_RANGE_CTA;
	vsc->content_type = DP_CONTENT_TYPE_NOT_DEFINED;
}

static void intel_dp_compute_vsc_sdp(struct intel_dp *intel_dp,
				     struct intel_crtc_state *crtc_state,
				     const struct drm_connector_state *conn_state)
{
	struct drm_dp_vsc_sdp *vsc = &crtc_state->infoframes.vsc;

	/* When a crtc state has PSR, VSC SDP will be handled by PSR routine */
	if (crtc_state->has_psr)
		return;

	if (!intel_dp_needs_vsc_sdp(crtc_state, conn_state))
		return;

	crtc_state->infoframes.enable |= intel_hdmi_infoframe_enable(DP_SDP_VSC);
	vsc->sdp_type = DP_SDP_VSC;
	intel_dp_compute_vsc_colorimetry(crtc_state, conn_state,
					 &crtc_state->infoframes.vsc);
}

void intel_dp_compute_psr_vsc_sdp(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state,
				  struct drm_dp_vsc_sdp *vsc)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	vsc->sdp_type = DP_SDP_VSC;

	if (dev_priv->psr.psr2_enabled) {
		if (dev_priv->psr.colorimetry_support &&
		    intel_dp_needs_vsc_sdp(crtc_state, conn_state)) {
			/* [PSR2, +Colorimetry] */
			intel_dp_compute_vsc_colorimetry(crtc_state, conn_state,
							 vsc);
		} else {
			/*
			 * [PSR2, -Colorimetry]
			 * Prepare VSC Header for SU as per eDP 1.4 spec, Table 6-11
			 * 3D stereo + PSR/PSR2 + Y-coordinate.
			 */
			vsc->revision = 0x4;
			vsc->length = 0xe;
		}
	} else {
		/*
		 * [PSR1]
		 * Prepare VSC Header for SU as per DP 1.4 spec, Table 2-118
		 * VSC SDP supporting 3D stereo + PSR (applies to eDP v1.3 or
		 * higher).
		 */
		vsc->revision = 0x2;
		vsc->length = 0x8;
	}
}

static void
intel_dp_compute_hdr_metadata_infoframe_sdp(struct intel_dp *intel_dp,
					    struct intel_crtc_state *crtc_state,
					    const struct drm_connector_state *conn_state)
{
	int ret;
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct hdmi_drm_infoframe *drm_infoframe = &crtc_state->infoframes.drm.drm;

	if (!conn_state->hdr_output_metadata)
		return;

	ret = drm_hdmi_infoframe_set_hdr_metadata(drm_infoframe, conn_state);

	if (ret) {
		drm_dbg_kms(&dev_priv->drm, "couldn't set HDR metadata in infoframe\n");
		return;
	}

	crtc_state->infoframes.enable |=
		intel_hdmi_infoframe_enable(HDMI_PACKET_TYPE_GAMUT_METADATA);
}

static void
intel_dp_drrs_compute_config(struct intel_dp *intel_dp,
			     struct intel_crtc_state *pipe_config,
			     int output_bpp, bool constant_n)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	/*
	 * DRRS and PSR can't be enable together, so giving preference to PSR
	 * as it allows more power-savings by complete shutting down display,
	 * so to guarantee this, intel_dp_drrs_compute_config() must be called
	 * after intel_psr_compute_config().
	 */
	if (pipe_config->has_psr)
		return;

	if (!intel_connector->panel.downclock_mode ||
	    dev_priv->drrs.type != SEAMLESS_DRRS_SUPPORT)
		return;

	pipe_config->has_drrs = true;
	intel_link_compute_m_n(output_bpp, pipe_config->lane_count,
			       intel_connector->panel.downclock_mode->clock,
			       pipe_config->port_clock, &pipe_config->dp_m2_n2,
			       constant_n, pipe_config->fec_enable);
}

int
intel_dp_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_state *pipe_config,
			struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_lspcon *lspcon = enc_to_intel_lspcon(encoder);
	enum port port = encoder->port;
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);
	bool constant_n = drm_dp_has_quirk(&intel_dp->desc, 0,
					   DP_DPCD_QUIRK_CONSTANT_N);
	int ret = 0, output_bpp;

	if (HAS_PCH_SPLIT(dev_priv) && !HAS_DDI(dev_priv) && port != PORT_A)
		pipe_config->has_pch_encoder = true;

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	if (lspcon->active)
		lspcon_ycbcr420_config(&intel_connector->base, pipe_config);
	else
		ret = intel_dp_ycbcr420_config(pipe_config, conn_state);
	if (ret)
		return ret;

	if (!intel_dp_port_has_audio(dev_priv, port))
		pipe_config->has_audio = false;
	else if (intel_conn_state->force_audio == HDMI_AUDIO_AUTO)
		pipe_config->has_audio = intel_dp->has_audio;
	else
		pipe_config->has_audio = intel_conn_state->force_audio == HDMI_AUDIO_ON;

	if (intel_dp_is_edp(intel_dp) && intel_connector->panel.fixed_mode) {
		intel_fixed_panel_mode(intel_connector->panel.fixed_mode,
				       adjusted_mode);

		if (HAS_GMCH(dev_priv))
			ret = intel_gmch_panel_fitting(pipe_config, conn_state);
		else
			ret = intel_pch_panel_fitting(pipe_config, conn_state);
		if (ret)
			return ret;
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	if (HAS_GMCH(dev_priv) &&
	    adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		return -EINVAL;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK)
		return -EINVAL;

	if (intel_dp_hdisplay_bad(dev_priv, adjusted_mode->crtc_hdisplay))
		return -EINVAL;

	ret = intel_dp_compute_link_config(encoder, pipe_config, conn_state);
	if (ret < 0)
		return ret;

	pipe_config->limited_color_range =
		intel_dp_limited_color_range(pipe_config, conn_state);

	if (pipe_config->dsc.compression_enable)
		output_bpp = pipe_config->dsc.compressed_bpp;
	else
		output_bpp = intel_dp_output_bpp(pipe_config->output_format,
						 pipe_config->pipe_bpp);

	intel_link_compute_m_n(output_bpp,
			       pipe_config->lane_count,
			       adjusted_mode->crtc_clock,
			       pipe_config->port_clock,
			       &pipe_config->dp_m_n,
			       constant_n, pipe_config->fec_enable);

	if (!HAS_DDI(dev_priv))
		intel_dp_set_clock(encoder, pipe_config);

	intel_psr_compute_config(intel_dp, pipe_config);
	intel_dp_drrs_compute_config(intel_dp, pipe_config, output_bpp,
				     constant_n);
	intel_dp_compute_vsc_sdp(intel_dp, pipe_config, conn_state);
	intel_dp_compute_hdr_metadata_infoframe_sdp(intel_dp, pipe_config, conn_state);

	return 0;
}

void intel_dp_set_link_params(struct intel_dp *intel_dp,
			      int link_rate, u8 lane_count,
			      bool link_mst)
{
	intel_dp->link_trained = false;
	intel_dp->link_rate = link_rate;
	intel_dp->lane_count = lane_count;
	intel_dp->link_mst = link_mst;
}

static void intel_dp_prepare(struct intel_encoder *encoder,
			     const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;

	intel_dp_set_link_params(intel_dp, pipe_config->port_clock,
				 pipe_config->lane_count,
				 intel_crtc_has_type(pipe_config,
						     INTEL_OUTPUT_DP_MST));

	/*
	 * There are four kinds of DP registers:
	 *
	 * 	IBX PCH
	 * 	SNB CPU
	 *	IVB CPU
	 * 	CPT PCH
	 *
	 * IBX PCH and CPU are the same for almost everything,
	 * except that the CPU DP PLL is configured in this
	 * register
	 *
	 * CPT PCH is quite different, having many bits moved
	 * to the TRANS_DP_CTL register instead. That
	 * configuration happens (oddly) in ilk_pch_enable
	 */

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	intel_dp->DP = intel_de_read(dev_priv, intel_dp->output_reg) & DP_DETECTED;

	/* Handle DP bits in common between all three register formats */
	intel_dp->DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	intel_dp->DP |= DP_PORT_WIDTH(pipe_config->lane_count);

	/* Split out the IBX/CPU vs CPT settings */

	if (IS_IVYBRIDGE(dev_priv) && port == PORT_A) {
		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;

		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		intel_dp->DP |= DP_PIPE_SEL_IVB(crtc->pipe);
	} else if (HAS_PCH_CPT(dev_priv) && port != PORT_A) {
		u32 trans_dp;

		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;

		trans_dp = intel_de_read(dev_priv, TRANS_DP_CTL(crtc->pipe));
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			trans_dp |= TRANS_DP_ENH_FRAMING;
		else
			trans_dp &= ~TRANS_DP_ENH_FRAMING;
		intel_de_write(dev_priv, TRANS_DP_CTL(crtc->pipe), trans_dp);
	} else {
		if (IS_G4X(dev_priv) && pipe_config->limited_color_range)
			intel_dp->DP |= DP_COLOR_RANGE_16_235;

		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF;

		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		if (IS_CHERRYVIEW(dev_priv))
			intel_dp->DP |= DP_PIPE_SEL_CHV(crtc->pipe);
		else
			intel_dp->DP |= DP_PIPE_SEL(crtc->pipe);
	}
}

#define IDLE_ON_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | PP_SEQUENCE_STATE_MASK)
#define IDLE_ON_VALUE   	(PP_ON | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_ON_IDLE)

#define IDLE_OFF_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | 0)
#define IDLE_OFF_VALUE		(0     | PP_SEQUENCE_NONE | 0                     | 0)

#define IDLE_CYCLE_MASK		(PP_ON | PP_SEQUENCE_MASK | PP_CYCLE_DELAY_ACTIVE | PP_SEQUENCE_STATE_MASK)
#define IDLE_CYCLE_VALUE	(0     | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_OFF_IDLE)

static void intel_pps_verify_state(struct intel_dp *intel_dp);

static void wait_panel_status(struct intel_dp *intel_dp,
				       u32 mask,
				       u32 value)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	intel_pps_verify_state(intel_dp);

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	drm_dbg_kms(&dev_priv->drm,
		    "mask %08x value %08x status %08x control %08x\n",
		    mask, value,
		    intel_de_read(dev_priv, pp_stat_reg),
		    intel_de_read(dev_priv, pp_ctrl_reg));

	if (intel_de_wait_for_register(dev_priv, pp_stat_reg,
				       mask, value, 5000))
		drm_err(&dev_priv->drm,
			"Panel status timeout: status %08x control %08x\n",
			intel_de_read(dev_priv, pp_stat_reg),
			intel_de_read(dev_priv, pp_ctrl_reg));

	drm_dbg_kms(&dev_priv->drm, "Wait complete\n");
}

static void wait_panel_on(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	drm_dbg_kms(&i915->drm, "Wait for panel power on\n");
	wait_panel_status(intel_dp, IDLE_ON_MASK, IDLE_ON_VALUE);
}

static void wait_panel_off(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	drm_dbg_kms(&i915->drm, "Wait for panel power off time\n");
	wait_panel_status(intel_dp, IDLE_OFF_MASK, IDLE_OFF_VALUE);
}

static void wait_panel_power_cycle(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	ktime_t panel_power_on_time;
	s64 panel_power_off_duration;

	drm_dbg_kms(&i915->drm, "Wait for panel power cycle\n");

	/* take the difference of currrent time and panel power off time
	 * and then make panel wait for t11_t12 if needed. */
	panel_power_on_time = ktime_get_boottime();
	panel_power_off_duration = ktime_ms_delta(panel_power_on_time, intel_dp->panel_power_off_time);

	/* When we disable the VDD override bit last we have to do the manual
	 * wait. */
	if (panel_power_off_duration < (s64)intel_dp->panel_power_cycle_delay)
		wait_remaining_ms_from_jiffies(jiffies,
				       intel_dp->panel_power_cycle_delay - panel_power_off_duration);

	wait_panel_status(intel_dp, IDLE_CYCLE_MASK, IDLE_CYCLE_VALUE);
}

static void wait_backlight_on(struct intel_dp *intel_dp)
{
	wait_remaining_ms_from_jiffies(intel_dp->last_power_on,
				       intel_dp->backlight_on_delay);
}

static void edp_wait_backlight_off(struct intel_dp *intel_dp)
{
	wait_remaining_ms_from_jiffies(intel_dp->last_backlight_off,
				       intel_dp->backlight_off_delay);
}

/* Read the current pp_control value, unlocking the register if it
 * is locked
 */

static  u32 ilk_get_pp_control(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 control;

	lockdep_assert_held(&dev_priv->pps_mutex);

	control = intel_de_read(dev_priv, _pp_ctrl_reg(intel_dp));
	if (drm_WARN_ON(&dev_priv->drm, !HAS_DDI(dev_priv) &&
			(control & PANEL_UNLOCK_MASK) != PANEL_UNLOCK_REGS)) {
		control &= ~PANEL_UNLOCK_MASK;
		control |= PANEL_UNLOCK_REGS;
	}
	return control;
}

/*
 * Must be paired with edp_panel_vdd_off().
 * Must hold pps_mutex around the whole on/off sequence.
 * Can be nested with intel_edp_panel_vdd_{on,off}() calls.
 */
static bool edp_panel_vdd_on(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;
	bool need_to_disable = !intel_dp->want_panel_vdd;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!intel_dp_is_edp(intel_dp))
		return false;

	cancel_delayed_work(&intel_dp->panel_vdd_work);
	intel_dp->want_panel_vdd = true;

	if (edp_have_panel_vdd(intel_dp))
		return need_to_disable;

	intel_display_power_get(dev_priv,
				intel_aux_power_domain(dig_port));

	drm_dbg_kms(&dev_priv->drm, "Turning [ENCODER:%d:%s] VDD on\n",
		    dig_port->base.base.base.id,
		    dig_port->base.base.name);

	if (!edp_have_panel_power(intel_dp))
		wait_panel_power_cycle(intel_dp);

	pp = ilk_get_pp_control(intel_dp);
	pp |= EDP_FORCE_VDD;

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);
	drm_dbg_kms(&dev_priv->drm, "PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		    intel_de_read(dev_priv, pp_stat_reg),
		    intel_de_read(dev_priv, pp_ctrl_reg));
	/*
	 * If the panel wasn't on, delay before accessing aux channel
	 */
	if (!edp_have_panel_power(intel_dp)) {
		drm_dbg_kms(&dev_priv->drm,
			    "[ENCODER:%d:%s] panel power wasn't enabled\n",
			    dig_port->base.base.base.id,
			    dig_port->base.base.name);
		msleep(intel_dp->panel_power_up_delay);
	}

	return need_to_disable;
}

/*
 * Must be paired with intel_edp_panel_vdd_off() or
 * intel_edp_panel_off().
 * Nested calls to these functions are not allowed since
 * we drop the lock. Caller must use some higher level
 * locking to prevent nested calls from other threads.
 */
void intel_edp_panel_vdd_on(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool vdd;

	if (!intel_dp_is_edp(intel_dp))
		return;

	vdd = false;
	with_pps_lock(intel_dp, wakeref)
		vdd = edp_panel_vdd_on(intel_dp);
	I915_STATE_WARN(!vdd, "[ENCODER:%d:%s] VDD already requested on\n",
			dp_to_dig_port(intel_dp)->base.base.base.id,
			dp_to_dig_port(intel_dp)->base.base.name);
}

static void edp_panel_vdd_off_sync(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port =
		dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	drm_WARN_ON(&dev_priv->drm, intel_dp->want_panel_vdd);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	drm_dbg_kms(&dev_priv->drm, "Turning [ENCODER:%d:%s] VDD off\n",
		    dig_port->base.base.base.id,
		    dig_port->base.base.name);

	pp = ilk_get_pp_control(intel_dp);
	pp &= ~EDP_FORCE_VDD;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp_stat_reg = _pp_stat_reg(intel_dp);

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);

	/* Make sure sequencer is idle before allowing subsequent activity */
	drm_dbg_kms(&dev_priv->drm, "PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
		    intel_de_read(dev_priv, pp_stat_reg),
		    intel_de_read(dev_priv, pp_ctrl_reg));

	if ((pp & PANEL_POWER_ON) == 0)
		intel_dp->panel_power_off_time = ktime_get_boottime();

	intel_display_power_put_unchecked(dev_priv,
					  intel_aux_power_domain(dig_port));
}

static void edp_panel_vdd_work(struct work_struct *__work)
{
	struct intel_dp *intel_dp =
		container_of(to_delayed_work(__work),
			     struct intel_dp, panel_vdd_work);
	intel_wakeref_t wakeref;

	with_pps_lock(intel_dp, wakeref) {
		if (!intel_dp->want_panel_vdd)
			edp_panel_vdd_off_sync(intel_dp);
	}
}

static void edp_panel_vdd_schedule_off(struct intel_dp *intel_dp)
{
	unsigned long delay;

	/*
	 * Queue the timer to fire a long time from now (relative to the power
	 * down delay) to keep the panel power up across a sequence of
	 * operations.
	 */
	delay = msecs_to_jiffies(intel_dp->panel_power_cycle_delay * 5);
	schedule_delayed_work(&intel_dp->panel_vdd_work, delay);
}

/*
 * Must be paired with edp_panel_vdd_on().
 * Must hold pps_mutex around the whole on/off sequence.
 * Can be nested with intel_edp_panel_vdd_{on,off}() calls.
 */
static void edp_panel_vdd_off(struct intel_dp *intel_dp, bool sync)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	I915_STATE_WARN(!intel_dp->want_panel_vdd, "[ENCODER:%d:%s] VDD not forced on",
			dp_to_dig_port(intel_dp)->base.base.base.id,
			dp_to_dig_port(intel_dp)->base.base.name);

	intel_dp->want_panel_vdd = false;

	if (sync)
		edp_panel_vdd_off_sync(intel_dp);
	else
		edp_panel_vdd_schedule_off(intel_dp);
}

static void edp_panel_on(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&dev_priv->drm, "Turn [ENCODER:%d:%s] panel power on\n",
		    dp_to_dig_port(intel_dp)->base.base.base.id,
		    dp_to_dig_port(intel_dp)->base.base.name);

	if (drm_WARN(&dev_priv->drm, edp_have_panel_power(intel_dp),
		     "[ENCODER:%d:%s] panel power already on\n",
		     dp_to_dig_port(intel_dp)->base.base.base.id,
		     dp_to_dig_port(intel_dp)->base.base.name))
		return;

	wait_panel_power_cycle(intel_dp);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp = ilk_get_pp_control(intel_dp);
	if (IS_GEN(dev_priv, 5)) {
		/* ILK workaround: disable reset around power sequence */
		pp &= ~PANEL_POWER_RESET;
		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
	}

	pp |= PANEL_POWER_ON;
	if (!IS_GEN(dev_priv, 5))
		pp |= PANEL_POWER_RESET;

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);

	wait_panel_on(intel_dp);
	intel_dp->last_power_on = jiffies;

	if (IS_GEN(dev_priv, 5)) {
		pp |= PANEL_POWER_RESET; /* restore panel reset bit */
		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
	}
}

void intel_edp_panel_on(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_pps_lock(intel_dp, wakeref)
		edp_panel_on(intel_dp);
}


static void edp_panel_off(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&dev_priv->drm, "Turn [ENCODER:%d:%s] panel power off\n",
		    dig_port->base.base.base.id, dig_port->base.base.name);

	drm_WARN(&dev_priv->drm, !intel_dp->want_panel_vdd,
		 "Need [ENCODER:%d:%s] VDD to turn off panel\n",
		 dig_port->base.base.base.id, dig_port->base.base.name);

	pp = ilk_get_pp_control(intel_dp);
	/* We need to switch off panel power _and_ force vdd, for otherwise some
	 * panels get very unhappy and cease to work. */
	pp &= ~(PANEL_POWER_ON | PANEL_POWER_RESET | EDP_FORCE_VDD |
		EDP_BLC_ENABLE);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	intel_dp->want_panel_vdd = false;

	intel_de_write(dev_priv, pp_ctrl_reg, pp);
	intel_de_posting_read(dev_priv, pp_ctrl_reg);

	wait_panel_off(intel_dp);
	intel_dp->panel_power_off_time = ktime_get_boottime();

	/* We got a reference when we enabled the VDD. */
	intel_display_power_put_unchecked(dev_priv, intel_aux_power_domain(dig_port));
}

void intel_edp_panel_off(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_pps_lock(intel_dp, wakeref)
		edp_panel_off(intel_dp);
}

/* Enable backlight in the panel power control. */
static void _intel_edp_backlight_on(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	/*
	 * If we enable the backlight right away following a panel power
	 * on, we may see slight flicker as the panel syncs with the eDP
	 * link.  So delay a bit to make sure the image is solid before
	 * allowing it to appear.
	 */
	wait_backlight_on(intel_dp);

	with_pps_lock(intel_dp, wakeref) {
		i915_reg_t pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
		u32 pp;

		pp = ilk_get_pp_control(intel_dp);
		pp |= EDP_BLC_ENABLE;

		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
	}
}

/* Enable backlight PWM and backlight PP control. */
void intel_edp_backlight_on(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(conn_state->best_encoder));
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&i915->drm, "\n");

	intel_panel_enable_backlight(crtc_state, conn_state);
	_intel_edp_backlight_on(intel_dp);
}

/* Disable backlight in the panel power control. */
static void _intel_edp_backlight_off(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	with_pps_lock(intel_dp, wakeref) {
		i915_reg_t pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
		u32 pp;

		pp = ilk_get_pp_control(intel_dp);
		pp &= ~EDP_BLC_ENABLE;

		intel_de_write(dev_priv, pp_ctrl_reg, pp);
		intel_de_posting_read(dev_priv, pp_ctrl_reg);
	}

	intel_dp->last_backlight_off = jiffies;
	edp_wait_backlight_off(intel_dp);
}

/* Disable backlight PP control and backlight PWM. */
void intel_edp_backlight_off(const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(old_conn_state->best_encoder));
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&i915->drm, "\n");

	_intel_edp_backlight_off(intel_dp);
	intel_panel_disable_backlight(old_conn_state);
}

/*
 * Hook for controlling the panel power control backlight through the bl_power
 * sysfs attribute. Take care to handle multiple calls.
 */
static void intel_edp_backlight_power(struct intel_connector *connector,
				      bool enable)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	intel_wakeref_t wakeref;
	bool is_enabled;

	is_enabled = false;
	with_pps_lock(intel_dp, wakeref)
		is_enabled = ilk_get_pp_control(intel_dp) & EDP_BLC_ENABLE;
	if (is_enabled == enable)
		return;

	drm_dbg_kms(&i915->drm, "panel power control backlight %s\n",
		    enable ? "enable" : "disable");

	if (enable)
		_intel_edp_backlight_on(intel_dp);
	else
		_intel_edp_backlight_off(intel_dp);
}

static void assert_dp_port(struct intel_dp *intel_dp, bool state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	bool cur_state = intel_de_read(dev_priv, intel_dp->output_reg) & DP_PORT_EN;

	I915_STATE_WARN(cur_state != state,
			"[ENCODER:%d:%s] state assertion failure (expected %s, current %s)\n",
			dig_port->base.base.base.id, dig_port->base.base.name,
			onoff(state), onoff(cur_state));
}
#define assert_dp_port_disabled(d) assert_dp_port((d), false)

static void assert_edp_pll(struct drm_i915_private *dev_priv, bool state)
{
	bool cur_state = intel_de_read(dev_priv, DP_A) & DP_PLL_ENABLE;

	I915_STATE_WARN(cur_state != state,
			"eDP PLL state assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}
#define assert_edp_pll_enabled(d) assert_edp_pll((d), true)
#define assert_edp_pll_disabled(d) assert_edp_pll((d), false)

static void ilk_edp_pll_on(struct intel_dp *intel_dp,
			   const struct intel_crtc_state *pipe_config)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	assert_pipe_disabled(dev_priv, pipe_config->cpu_transcoder);
	assert_dp_port_disabled(intel_dp);
	assert_edp_pll_disabled(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "enabling eDP PLL for clock %d\n",
		    pipe_config->port_clock);

	intel_dp->DP &= ~DP_PLL_FREQ_MASK;

	if (pipe_config->port_clock == 162000)
		intel_dp->DP |= DP_PLL_FREQ_162MHZ;
	else
		intel_dp->DP |= DP_PLL_FREQ_270MHZ;

	intel_de_write(dev_priv, DP_A, intel_dp->DP);
	intel_de_posting_read(dev_priv, DP_A);
	udelay(500);

	/*
	 * [DevILK] Work around required when enabling DP PLL
	 * while a pipe is enabled going to FDI:
	 * 1. Wait for the start of vertical blank on the enabled pipe going to FDI
	 * 2. Program DP PLL enable
	 */
	if (IS_GEN(dev_priv, 5))
		intel_wait_for_vblank_if_active(dev_priv, !crtc->pipe);

	intel_dp->DP |= DP_PLL_ENABLE;

	intel_de_write(dev_priv, DP_A, intel_dp->DP);
	intel_de_posting_read(dev_priv, DP_A);
	udelay(200);
}

static void ilk_edp_pll_off(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	assert_pipe_disabled(dev_priv, old_crtc_state->cpu_transcoder);
	assert_dp_port_disabled(intel_dp);
	assert_edp_pll_enabled(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "disabling eDP PLL\n");

	intel_dp->DP &= ~DP_PLL_ENABLE;

	intel_de_write(dev_priv, DP_A, intel_dp->DP);
	intel_de_posting_read(dev_priv, DP_A);
	udelay(200);
}

static bool downstream_hpd_needs_d0(struct intel_dp *intel_dp)
{
	/*
	 * DPCD 1.2+ should support BRANCH_DEVICE_CTRL, and thus
	 * be capable of signalling downstream hpd with a long pulse.
	 * Whether or not that means D3 is safe to use is not clear,
	 * but let's assume so until proven otherwise.
	 *
	 * FIXME should really check all downstream ports...
	 */
	return intel_dp->dpcd[DP_DPCD_REV] == 0x11 &&
		drm_dp_is_branch(intel_dp->dpcd) &&
		intel_dp->downstream_ports[0] & DP_DS_PORT_HPD;
}

void intel_dp_sink_set_decompression_state(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state,
					   bool enable)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int ret;

	if (!crtc_state->dsc.compression_enable)
		return;

	ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_DSC_ENABLE,
				 enable ? DP_DECOMPRESSION_EN : 0);
	if (ret < 0)
		drm_dbg_kms(&i915->drm,
			    "Failed to %s sink decompression state\n",
			    enable ? "enable" : "disable");
}

/* If the sink supports it, try to set the power state appropriately */
void intel_dp_sink_dpms(struct intel_dp *intel_dp, int mode)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int ret, i;

	/* Should have a valid DPCD by this point */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (mode != DRM_MODE_DPMS_ON) {
		if (downstream_hpd_needs_d0(intel_dp))
			return;

		ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER,
					 DP_SET_POWER_D3);
	} else {
		struct intel_lspcon *lspcon = dp_to_lspcon(intel_dp);

		/*
		 * When turning on, we need to retry for 1ms to give the sink
		 * time to wake up.
		 */
		for (i = 0; i < 3; i++) {
			ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER,
						 DP_SET_POWER_D0);
			if (ret == 1)
				break;
			msleep(1);
		}

		if (ret == 1 && lspcon->active)
			lspcon_wait_pcon_mode(lspcon);
	}

	if (ret != 1)
		drm_dbg_kms(&i915->drm, "failed to %s sink power state\n",
			    mode == DRM_MODE_DPMS_ON ? "enable" : "disable");
}

static bool cpt_dp_port_selected(struct drm_i915_private *dev_priv,
				 enum port port, enum pipe *pipe)
{
	enum pipe p;

	for_each_pipe(dev_priv, p) {
		u32 val = intel_de_read(dev_priv, TRANS_DP_CTL(p));

		if ((val & TRANS_DP_PORT_SEL_MASK) == TRANS_DP_PORT_SEL(port)) {
			*pipe = p;
			return true;
		}
	}

	drm_dbg_kms(&dev_priv->drm, "No pipe for DP port %c found\n",
		    port_name(port));

	/* must initialize pipe to something for the asserts */
	*pipe = PIPE_A;

	return false;
}

bool intel_dp_port_enabled(struct drm_i915_private *dev_priv,
			   i915_reg_t dp_reg, enum port port,
			   enum pipe *pipe)
{
	bool ret;
	u32 val;

	val = intel_de_read(dev_priv, dp_reg);

	ret = val & DP_PORT_EN;

	/* asserts want to know the pipe even if the port is disabled */
	if (IS_IVYBRIDGE(dev_priv) && port == PORT_A)
		*pipe = (val & DP_PIPE_SEL_MASK_IVB) >> DP_PIPE_SEL_SHIFT_IVB;
	else if (HAS_PCH_CPT(dev_priv) && port != PORT_A)
		ret &= cpt_dp_port_selected(dev_priv, port, pipe);
	else if (IS_CHERRYVIEW(dev_priv))
		*pipe = (val & DP_PIPE_SEL_MASK_CHV) >> DP_PIPE_SEL_SHIFT_CHV;
	else
		*pipe = (val & DP_PIPE_SEL_MASK) >> DP_PIPE_SEL_SHIFT;

	return ret;
}

static bool intel_dp_get_hw_state(struct intel_encoder *encoder,
				  enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	intel_wakeref_t wakeref;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	ret = intel_dp_port_enabled(dev_priv, intel_dp->output_reg,
				    encoder->port, pipe);

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

static void intel_dp_get_config(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	u32 tmp, flags = 0;
	enum port port = encoder->port;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);

	if (encoder->type == INTEL_OUTPUT_EDP)
		pipe_config->output_types |= BIT(INTEL_OUTPUT_EDP);
	else
		pipe_config->output_types |= BIT(INTEL_OUTPUT_DP);

	tmp = intel_de_read(dev_priv, intel_dp->output_reg);

	pipe_config->has_audio = tmp & DP_AUDIO_OUTPUT_ENABLE && port != PORT_A;

	if (HAS_PCH_CPT(dev_priv) && port != PORT_A) {
		u32 trans_dp = intel_de_read(dev_priv,
					     TRANS_DP_CTL(crtc->pipe));

		if (trans_dp & TRANS_DP_HSYNC_ACTIVE_HIGH)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;

		if (trans_dp & TRANS_DP_VSYNC_ACTIVE_HIGH)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
	} else {
		if (tmp & DP_SYNC_HS_HIGH)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;

		if (tmp & DP_SYNC_VS_HIGH)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
	}

	pipe_config->hw.adjusted_mode.flags |= flags;

	if (IS_G4X(dev_priv) && tmp & DP_COLOR_RANGE_16_235)
		pipe_config->limited_color_range = true;

	pipe_config->lane_count =
		((tmp & DP_PORT_WIDTH_MASK) >> DP_PORT_WIDTH_SHIFT) + 1;

	intel_dp_get_m_n(crtc, pipe_config);

	if (port == PORT_A) {
		if ((intel_de_read(dev_priv, DP_A) & DP_PLL_FREQ_MASK) == DP_PLL_FREQ_162MHZ)
			pipe_config->port_clock = 162000;
		else
			pipe_config->port_clock = 270000;
	}

	pipe_config->hw.adjusted_mode.crtc_clock =
		intel_dotclock_calculate(pipe_config->port_clock,
					 &pipe_config->dp_m_n);

	if (intel_dp_is_edp(intel_dp) && dev_priv->vbt.edp.bpp &&
	    pipe_config->pipe_bpp > dev_priv->vbt.edp.bpp) {
		/*
		 * This is a big fat ugly hack.
		 *
		 * Some machines in UEFI boot mode provide us a VBT that has 18
		 * bpp and 1.62 GHz link bandwidth for eDP, which for reasons
		 * unknown we fail to light up. Yet the same BIOS boots up with
		 * 24 bpp and 2.7 GHz link. Use the same bpp as the BIOS uses as
		 * max, not what it tells us to use.
		 *
		 * Note: This will still be broken if the eDP panel is not lit
		 * up by the BIOS, and thus we can't get the mode at module
		 * load.
		 */
		drm_dbg_kms(&dev_priv->drm,
			    "pipe has %d bpp for eDP panel, overriding BIOS-provided max %d bpp\n",
			    pipe_config->pipe_bpp, dev_priv->vbt.edp.bpp);
		dev_priv->vbt.edp.bpp = pipe_config->pipe_bpp;
	}
}

static void intel_disable_dp(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_dp->link_trained = false;

	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	/* Make sure the panel is off before trying to change the mode. But also
	 * ensure that we have vdd while we switch off the panel. */
	intel_edp_panel_vdd_on(intel_dp);
	intel_edp_backlight_off(old_conn_state);
	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_OFF);
	intel_edp_panel_off(intel_dp);
}

static void g4x_disable_dp(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *old_crtc_state,
			   const struct drm_connector_state *old_conn_state)
{
	intel_disable_dp(state, encoder, old_crtc_state, old_conn_state);
}

static void vlv_disable_dp(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *old_crtc_state,
			   const struct drm_connector_state *old_conn_state)
{
	intel_disable_dp(state, encoder, old_crtc_state, old_conn_state);
}

static void g4x_post_disable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;

	/*
	 * Bspec does not list a specific disable sequence for g4x DP.
	 * Follow the ilk+ sequence (disable pipe before the port) for
	 * g4x DP as it does not suffer from underruns like the normal
	 * g4x modeset sequence (disable pipe after the port).
	 */
	intel_dp_link_down(encoder, old_crtc_state);

	/* Only ilk+ has port A */
	if (port == PORT_A)
		ilk_edp_pll_off(intel_dp, old_crtc_state);
}

static void vlv_post_disable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	intel_dp_link_down(encoder, old_crtc_state);
}

static void chv_post_disable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	intel_dp_link_down(encoder, old_crtc_state);

	vlv_dpio_get(dev_priv);

	/* Assert data lane reset */
	chv_data_lane_soft_reset(encoder, old_crtc_state, true);

	vlv_dpio_put(dev_priv);
}

static void
cpt_set_link_train(struct intel_dp *intel_dp,
		   u8 dp_train_pat)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 *DP = &intel_dp->DP;

	*DP &= ~DP_LINK_TRAIN_MASK_CPT;

	switch (dp_train_pat & DP_TRAINING_PATTERN_MASK) {
	case DP_TRAINING_PATTERN_DISABLE:
		*DP |= DP_LINK_TRAIN_OFF_CPT;
		break;
	case DP_TRAINING_PATTERN_1:
		*DP |= DP_LINK_TRAIN_PAT_1_CPT;
		break;
	case DP_TRAINING_PATTERN_2:
		*DP |= DP_LINK_TRAIN_PAT_2_CPT;
		break;
	case DP_TRAINING_PATTERN_3:
		drm_dbg_kms(&dev_priv->drm,
			    "TPS3 not supported, using TPS2 instead\n");
		*DP |= DP_LINK_TRAIN_PAT_2_CPT;
		break;
	}

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

static void
g4x_set_link_train(struct intel_dp *intel_dp,
		   u8 dp_train_pat)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 *DP = &intel_dp->DP;

	*DP &= ~DP_LINK_TRAIN_MASK;

	switch (dp_train_pat & DP_TRAINING_PATTERN_MASK) {
	case DP_TRAINING_PATTERN_DISABLE:
		*DP |= DP_LINK_TRAIN_OFF;
		break;
	case DP_TRAINING_PATTERN_1:
		*DP |= DP_LINK_TRAIN_PAT_1;
		break;
	case DP_TRAINING_PATTERN_2:
		*DP |= DP_LINK_TRAIN_PAT_2;
		break;
	case DP_TRAINING_PATTERN_3:
		drm_dbg_kms(&dev_priv->drm,
			    "TPS3 not supported, using TPS2 instead\n");
		*DP |= DP_LINK_TRAIN_PAT_2;
		break;
	}

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

static void intel_dp_enable_port(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	/* enable with pattern 1 (as per spec) */

	intel_dp_program_link_training_pattern(intel_dp, DP_TRAINING_PATTERN_1);

	/*
	 * Magic for VLV/CHV. We _must_ first set up the register
	 * without actually enabling the port, and then do another
	 * write to enable the port. Otherwise link training will
	 * fail when the power sequencer is freshly used for this port.
	 */
	intel_dp->DP |= DP_PORT_EN;
	if (crtc_state->has_audio)
		intel_dp->DP |= DP_AUDIO_OUTPUT_ENABLE;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

void intel_dp_configure_protocol_converter(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 tmp;

	if (intel_dp->dpcd[DP_DPCD_REV] < 0x13)
		return;

	if (!drm_dp_is_branch(intel_dp->dpcd))
		return;

	tmp = intel_dp->has_hdmi_sink ?
		DP_HDMI_DVI_OUTPUT_CONFIG : 0;

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_PROTOCOL_CONVERTER_CONTROL_0, tmp) != 1)
		drm_dbg_kms(&i915->drm, "Failed to set protocol converter HDMI mode to %s\n",
			    enableddisabled(intel_dp->has_hdmi_sink));

	tmp = intel_dp->dfp.ycbcr_444_to_420 ?
		DP_CONVERSION_TO_YCBCR420_ENABLE : 0;

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_PROTOCOL_CONVERTER_CONTROL_1, tmp) != 1)
		drm_dbg_kms(&i915->drm,
			    "Failed to set protocol converter YCbCr 4:2:0 conversion mode to %s\n",
			    enableddisabled(intel_dp->dfp.ycbcr_444_to_420));

	tmp = 0;

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_PROTOCOL_CONVERTER_CONTROL_2, tmp) <= 0)
		drm_dbg_kms(&i915->drm,
			    "Failed to set protocol converter YCbCr 4:2:2 conversion mode to %s\n",
			    enableddisabled(false));
}

static void intel_enable_dp(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	u32 dp_reg = intel_de_read(dev_priv, intel_dp->output_reg);
	enum pipe pipe = crtc->pipe;
	intel_wakeref_t wakeref;

	if (drm_WARN_ON(&dev_priv->drm, dp_reg & DP_PORT_EN))
		return;

	with_pps_lock(intel_dp, wakeref) {
		if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			vlv_init_panel_power_sequencer(encoder, pipe_config);

		intel_dp_enable_port(intel_dp, pipe_config);

		edp_panel_vdd_on(intel_dp);
		edp_panel_on(intel_dp);
		edp_panel_vdd_off(intel_dp, true);
	}

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		unsigned int lane_mask = 0x0;

		if (IS_CHERRYVIEW(dev_priv))
			lane_mask = intel_dp_unused_lane_mask(pipe_config->lane_count);

		vlv_wait_port_ready(dev_priv, dp_to_dig_port(intel_dp),
				    lane_mask);
	}

	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);
	intel_dp_configure_protocol_converter(intel_dp);
	intel_dp_start_link_train(intel_dp);
	intel_dp_stop_link_train(intel_dp);

	if (pipe_config->has_audio) {
		drm_dbg(&dev_priv->drm, "Enabling DP audio on pipe %c\n",
			pipe_name(pipe));
		intel_audio_codec_enable(encoder, pipe_config, conn_state);
	}
}

static void g4x_enable_dp(struct intel_atomic_state *state,
			  struct intel_encoder *encoder,
			  const struct intel_crtc_state *pipe_config,
			  const struct drm_connector_state *conn_state)
{
	intel_enable_dp(state, encoder, pipe_config, conn_state);
	intel_edp_backlight_on(pipe_config, conn_state);
}

static void vlv_enable_dp(struct intel_atomic_state *state,
			  struct intel_encoder *encoder,
			  const struct intel_crtc_state *pipe_config,
			  const struct drm_connector_state *conn_state)
{
	intel_edp_backlight_on(pipe_config, conn_state);
}

static void g4x_pre_enable_dp(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;

	intel_dp_prepare(encoder, pipe_config);

	/* Only ilk+ has port A */
	if (port == PORT_A)
		ilk_edp_pll_on(intel_dp, pipe_config);
}

static void vlv_detach_power_sequencer(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum pipe pipe = intel_dp->pps_pipe;
	i915_reg_t pp_on_reg = PP_ON_DELAYS(pipe);

	drm_WARN_ON(&dev_priv->drm, intel_dp->active_pipe != INVALID_PIPE);

	if (drm_WARN_ON(&dev_priv->drm, pipe != PIPE_A && pipe != PIPE_B))
		return;

	edp_panel_vdd_off_sync(intel_dp);

	/*
	 * VLV seems to get confused when multiple power sequencers
	 * have the same port selected (even if only one has power/vdd
	 * enabled). The failure manifests as vlv_wait_port_ready() failing
	 * CHV on the other hand doesn't seem to mind having the same port
	 * selected in multiple power sequencers, but let's clear the
	 * port select always when logically disconnecting a power sequencer
	 * from a port.
	 */
	drm_dbg_kms(&dev_priv->drm,
		    "detaching pipe %c power sequencer from [ENCODER:%d:%s]\n",
		    pipe_name(pipe), dig_port->base.base.base.id,
		    dig_port->base.base.name);
	intel_de_write(dev_priv, pp_on_reg, 0);
	intel_de_posting_read(dev_priv, pp_on_reg);

	intel_dp->pps_pipe = INVALID_PIPE;
}

static void vlv_steal_power_sequencer(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	struct intel_encoder *encoder;

	lockdep_assert_held(&dev_priv->pps_mutex);

	for_each_intel_dp(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		drm_WARN(&dev_priv->drm, intel_dp->active_pipe == pipe,
			 "stealing pipe %c power sequencer from active [ENCODER:%d:%s]\n",
			 pipe_name(pipe), encoder->base.base.id,
			 encoder->base.name);

		if (intel_dp->pps_pipe != pipe)
			continue;

		drm_dbg_kms(&dev_priv->drm,
			    "stealing pipe %c power sequencer from [ENCODER:%d:%s]\n",
			    pipe_name(pipe), encoder->base.base.id,
			    encoder->base.name);

		/* make sure vdd is off before we steal it */
		vlv_detach_power_sequencer(intel_dp);
	}
}

static void vlv_init_panel_power_sequencer(struct intel_encoder *encoder,
					   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	lockdep_assert_held(&dev_priv->pps_mutex);

	drm_WARN_ON(&dev_priv->drm, intel_dp->active_pipe != INVALID_PIPE);

	if (intel_dp->pps_pipe != INVALID_PIPE &&
	    intel_dp->pps_pipe != crtc->pipe) {
		/*
		 * If another power sequencer was being used on this
		 * port previously make sure to turn off vdd there while
		 * we still have control of it.
		 */
		vlv_detach_power_sequencer(intel_dp);
	}

	/*
	 * We may be stealing the power
	 * sequencer from another port.
	 */
	vlv_steal_power_sequencer(dev_priv, crtc->pipe);

	intel_dp->active_pipe = crtc->pipe;

	if (!intel_dp_is_edp(intel_dp))
		return;

	/* now it's all ours */
	intel_dp->pps_pipe = crtc->pipe;

	drm_dbg_kms(&dev_priv->drm,
		    "initializing pipe %c power sequencer for [ENCODER:%d:%s]\n",
		    pipe_name(intel_dp->pps_pipe), encoder->base.base.id,
		    encoder->base.name);

	/* init power sequencer on this pipe and port */
	intel_dp_init_panel_power_sequencer(intel_dp);
	intel_dp_init_panel_power_sequencer_registers(intel_dp, true);
}

static void vlv_pre_enable_dp(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	vlv_phy_pre_encoder_enable(encoder, pipe_config);

	intel_enable_dp(state, encoder, pipe_config, conn_state);
}

static void vlv_dp_pre_pll_enable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	intel_dp_prepare(encoder, pipe_config);

	vlv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_pre_enable_dp(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	chv_phy_pre_encoder_enable(encoder, pipe_config);

	intel_enable_dp(state, encoder, pipe_config, conn_state);

	/* Second common lane will stay alive on its own now */
	chv_phy_release_cl2_override(encoder);
}

static void chv_dp_pre_pll_enable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	intel_dp_prepare(encoder, pipe_config);

	chv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_dp_post_pll_disable(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	chv_phy_post_pll_disable(encoder, old_crtc_state);
}

/*
 * Fetch AUX CH registers 0x202 - 0x207 which contain
 * link status information
 */
bool
intel_dp_get_link_status(struct intel_dp *intel_dp, u8 link_status[DP_LINK_STATUS_SIZE])
{
	return drm_dp_dpcd_read(&intel_dp->aux, DP_LANE0_1_STATUS, link_status,
				DP_LINK_STATUS_SIZE) == DP_LINK_STATUS_SIZE;
}

static u8 intel_dp_voltage_max_2(struct intel_dp *intel_dp)
{
	return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
}

static u8 intel_dp_voltage_max_3(struct intel_dp *intel_dp)
{
	return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
}

static u8 intel_dp_preemph_max_2(struct intel_dp *intel_dp)
{
	return DP_TRAIN_PRE_EMPH_LEVEL_2;
}

static u8 intel_dp_preemph_max_3(struct intel_dp *intel_dp)
{
	return DP_TRAIN_PRE_EMPH_LEVEL_3;
}

static void vlv_set_signal_levels(struct intel_dp *intel_dp)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	unsigned long demph_reg_value, preemph_reg_value,
		uniqtranscale_reg_value;
	u8 train_set = intel_dp->train_set[0];

	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
		preemph_reg_value = 0x0004000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x2B405555;
			uniqtranscale_reg_value = 0x552AB83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x5548B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			demph_reg_value = 0x2B245555;
			uniqtranscale_reg_value = 0x5560B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
			demph_reg_value = 0x2B405555;
			uniqtranscale_reg_value = 0x5598DA3A;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		preemph_reg_value = 0x0002000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x5552B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			demph_reg_value = 0x2B404848;
			uniqtranscale_reg_value = 0x5580B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		preemph_reg_value = 0x0000000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x2B305555;
			uniqtranscale_reg_value = 0x5570B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			demph_reg_value = 0x2B2B4040;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
		preemph_reg_value = 0x0006000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x1B405555;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}

	vlv_set_phy_signal_level(encoder, demph_reg_value, preemph_reg_value,
				 uniqtranscale_reg_value, 0);
}

static void chv_set_signal_levels(struct intel_dp *intel_dp)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	u32 deemph_reg_value, margin_reg_value;
	bool uniq_trans_scale = false;
	u8 train_set = intel_dp->train_set[0];

	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 128;
			margin_reg_value = 52;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			deemph_reg_value = 128;
			margin_reg_value = 77;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			deemph_reg_value = 128;
			margin_reg_value = 102;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
			deemph_reg_value = 128;
			margin_reg_value = 154;
			uniq_trans_scale = true;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 85;
			margin_reg_value = 78;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			deemph_reg_value = 85;
			margin_reg_value = 116;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			deemph_reg_value = 85;
			margin_reg_value = 154;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 64;
			margin_reg_value = 104;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			deemph_reg_value = 64;
			margin_reg_value = 154;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 43;
			margin_reg_value = 154;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}

	chv_set_phy_signal_level(encoder, deemph_reg_value,
				 margin_reg_value, uniq_trans_scale);
}

static u32 g4x_signal_levels(u8 train_set)
{
	u32 signal_levels = 0;

	switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
	default:
		signal_levels |= DP_VOLTAGE_0_4;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
		signal_levels |= DP_VOLTAGE_0_6;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
		signal_levels |= DP_VOLTAGE_0_8;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
		signal_levels |= DP_VOLTAGE_1_2;
		break;
	}
	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
	default:
		signal_levels |= DP_PRE_EMPHASIS_0;
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		signal_levels |= DP_PRE_EMPHASIS_3_5;
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		signal_levels |= DP_PRE_EMPHASIS_6;
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
		signal_levels |= DP_PRE_EMPHASIS_9_5;
		break;
	}
	return signal_levels;
}

static void
g4x_set_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 train_set = intel_dp->train_set[0];
	u32 signal_levels;

	signal_levels = g4x_signal_levels(train_set);

	drm_dbg_kms(&dev_priv->drm, "Using signal levels %08x\n",
		    signal_levels);

	intel_dp->DP &= ~(DP_VOLTAGE_MASK | DP_PRE_EMPHASIS_MASK);
	intel_dp->DP |= signal_levels;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

/* SNB CPU eDP voltage swing and pre-emphasis control */
static u32 snb_cpu_edp_signal_levels(u8 train_set)
{
	u8 signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					DP_TRAIN_PRE_EMPHASIS_MASK);

	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_400_600MV_0DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_400MV_3_5DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2:
		return EDP_LINK_TRAIN_400_600MV_6DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_600_800MV_3_5DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_3 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_800_1200MV_0DB_SNB_B;
	default:
		DRM_DEBUG_KMS("Unsupported voltage swing/pre-emphasis level:"
			      "0x%x\n", signal_levels);
		return EDP_LINK_TRAIN_400_600MV_0DB_SNB_B;
	}
}

static void
snb_cpu_edp_set_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 train_set = intel_dp->train_set[0];
	u32 signal_levels;

	signal_levels = snb_cpu_edp_signal_levels(train_set);

	drm_dbg_kms(&dev_priv->drm, "Using signal levels %08x\n",
		    signal_levels);

	intel_dp->DP &= ~EDP_LINK_TRAIN_VOL_EMP_MASK_SNB;
	intel_dp->DP |= signal_levels;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

/* IVB CPU eDP voltage swing and pre-emphasis control */
static u32 ivb_cpu_edp_signal_levels(u8 train_set)
{
	u8 signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					DP_TRAIN_PRE_EMPHASIS_MASK);

	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_400MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_400MV_3_5DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2:
		return EDP_LINK_TRAIN_400MV_6DB_IVB;

	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_600MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_600MV_3_5DB_IVB;

	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_800MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_800MV_3_5DB_IVB;

	default:
		DRM_DEBUG_KMS("Unsupported voltage swing/pre-emphasis level:"
			      "0x%x\n", signal_levels);
		return EDP_LINK_TRAIN_500MV_0DB_IVB;
	}
}

static void
ivb_cpu_edp_set_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 train_set = intel_dp->train_set[0];
	u32 signal_levels;

	signal_levels = ivb_cpu_edp_signal_levels(train_set);

	drm_dbg_kms(&dev_priv->drm, "Using signal levels %08x\n",
		    signal_levels);

	intel_dp->DP &= ~EDP_LINK_TRAIN_VOL_EMP_MASK_IVB;
	intel_dp->DP |= signal_levels;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

void intel_dp_set_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 train_set = intel_dp->train_set[0];

	drm_dbg_kms(&dev_priv->drm, "Using vswing level %d%s\n",
		    train_set & DP_TRAIN_VOLTAGE_SWING_MASK,
		    train_set & DP_TRAIN_MAX_SWING_REACHED ? " (max)" : "");
	drm_dbg_kms(&dev_priv->drm, "Using pre-emphasis level %d%s\n",
		    (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) >>
		    DP_TRAIN_PRE_EMPHASIS_SHIFT,
		    train_set & DP_TRAIN_MAX_PRE_EMPHASIS_REACHED ?
		    " (max)" : "");

	intel_dp->set_signal_levels(intel_dp);
}

void
intel_dp_program_link_training_pattern(struct intel_dp *intel_dp,
				       u8 dp_train_pat)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 train_pat_mask = drm_dp_training_pattern_mask(intel_dp->dpcd);

	if (dp_train_pat & train_pat_mask)
		drm_dbg_kms(&dev_priv->drm,
			    "Using DP training pattern TPS%d\n",
			    dp_train_pat & train_pat_mask);

	intel_dp->set_link_train(intel_dp, dp_train_pat);
}

void intel_dp_set_idle_link_train(struct intel_dp *intel_dp)
{
	if (intel_dp->set_idle_link_train)
		intel_dp->set_idle_link_train(intel_dp);
}

static void
intel_dp_link_down(struct intel_encoder *encoder,
		   const struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	enum port port = encoder->port;
	u32 DP = intel_dp->DP;

	if (drm_WARN_ON(&dev_priv->drm,
			(intel_de_read(dev_priv, intel_dp->output_reg) &
			 DP_PORT_EN) == 0))
		return;

	drm_dbg_kms(&dev_priv->drm, "\n");

	if ((IS_IVYBRIDGE(dev_priv) && port == PORT_A) ||
	    (HAS_PCH_CPT(dev_priv) && port != PORT_A)) {
		DP &= ~DP_LINK_TRAIN_MASK_CPT;
		DP |= DP_LINK_TRAIN_PAT_IDLE_CPT;
	} else {
		DP &= ~DP_LINK_TRAIN_MASK;
		DP |= DP_LINK_TRAIN_PAT_IDLE;
	}
	intel_de_write(dev_priv, intel_dp->output_reg, DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	DP &= ~(DP_PORT_EN | DP_AUDIO_OUTPUT_ENABLE);
	intel_de_write(dev_priv, intel_dp->output_reg, DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	/*
	 * HW workaround for IBX, we need to move the port
	 * to transcoder A after disabling it to allow the
	 * matching HDMI port to be enabled on transcoder A.
	 */
	if (HAS_PCH_IBX(dev_priv) && crtc->pipe == PIPE_B && port != PORT_A) {
		/*
		 * We get CPU/PCH FIFO underruns on the other pipe when
		 * doing the workaround. Sweep them under the rug.
		 */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, false);

		/* always enable with pattern 1 (as per spec) */
		DP &= ~(DP_PIPE_SEL_MASK | DP_LINK_TRAIN_MASK);
		DP |= DP_PORT_EN | DP_PIPE_SEL(PIPE_A) |
			DP_LINK_TRAIN_PAT_1;
		intel_de_write(dev_priv, intel_dp->output_reg, DP);
		intel_de_posting_read(dev_priv, intel_dp->output_reg);

		DP &= ~DP_PORT_EN;
		intel_de_write(dev_priv, intel_dp->output_reg, DP);
		intel_de_posting_read(dev_priv, intel_dp->output_reg);

		intel_wait_for_vblank_if_active(dev_priv, PIPE_A);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, true);
	}

	msleep(intel_dp->panel_power_down_delay);

	intel_dp->DP = DP;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		intel_wakeref_t wakeref;

		with_pps_lock(intel_dp, wakeref)
			intel_dp->active_pipe = INVALID_PIPE;
	}
}

bool intel_dp_get_colorimetry_status(struct intel_dp *intel_dp)
{
	u8 dprx = 0;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_DPRX_FEATURE_ENUMERATION_LIST,
			      &dprx) != 1)
		return false;
	return dprx & DP_VSC_SDP_EXT_FOR_COLORIMETRY_SUPPORTED;
}

static void intel_dp_get_dsc_sink_cap(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	/*
	 * Clear the cached register set to avoid using stale values
	 * for the sinks that do not support DSC.
	 */
	memset(intel_dp->dsc_dpcd, 0, sizeof(intel_dp->dsc_dpcd));

	/* Clear fec_capable to avoid using stale values */
	intel_dp->fec_capable = 0;

	/* Cache the DSC DPCD if eDP or DP rev >= 1.4 */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x14 ||
	    intel_dp->edp_dpcd[0] >= DP_EDP_14) {
		if (drm_dp_dpcd_read(&intel_dp->aux, DP_DSC_SUPPORT,
				     intel_dp->dsc_dpcd,
				     sizeof(intel_dp->dsc_dpcd)) < 0)
			drm_err(&i915->drm,
				"Failed to read DPCD register 0x%x\n",
				DP_DSC_SUPPORT);

		drm_dbg_kms(&i915->drm, "DSC DPCD: %*ph\n",
			    (int)sizeof(intel_dp->dsc_dpcd),
			    intel_dp->dsc_dpcd);

		/* FEC is supported only on DP 1.4 */
		if (!intel_dp_is_edp(intel_dp) &&
		    drm_dp_dpcd_readb(&intel_dp->aux, DP_FEC_CAPABILITY,
				      &intel_dp->fec_capable) < 0)
			drm_err(&i915->drm,
				"Failed to read FEC DPCD register\n");

		drm_dbg_kms(&i915->drm, "FEC CAPABILITY: %x\n",
			    intel_dp->fec_capable);
	}
}

static bool
intel_edp_init_dpcd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv =
		to_i915(dp_to_dig_port(intel_dp)->base.base.dev);

	/* this function is meant to be called only once */
	drm_WARN_ON(&dev_priv->drm, intel_dp->dpcd[DP_DPCD_REV] != 0);

	if (drm_dp_read_dpcd_caps(&intel_dp->aux, intel_dp->dpcd) != 0)
		return false;

	drm_dp_read_desc(&intel_dp->aux, &intel_dp->desc,
			 drm_dp_is_branch(intel_dp->dpcd));

	/*
	 * Read the eDP display control registers.
	 *
	 * Do this independent of DP_DPCD_DISPLAY_CONTROL_CAPABLE bit in
	 * DP_EDP_CONFIGURATION_CAP, because some buggy displays do not have it
	 * set, but require eDP 1.4+ detection (e.g. for supported link rates
	 * method). The display control registers should read zero if they're
	 * not supported anyway.
	 */
	if (drm_dp_dpcd_read(&intel_dp->aux, DP_EDP_DPCD_REV,
			     intel_dp->edp_dpcd, sizeof(intel_dp->edp_dpcd)) ==
			     sizeof(intel_dp->edp_dpcd))
		drm_dbg_kms(&dev_priv->drm, "eDP DPCD: %*ph\n",
			    (int)sizeof(intel_dp->edp_dpcd),
			    intel_dp->edp_dpcd);

	/*
	 * This has to be called after intel_dp->edp_dpcd is filled, PSR checks
	 * for SET_POWER_CAPABLE bit in intel_dp->edp_dpcd[1]
	 */
	intel_psr_init_dpcd(intel_dp);

	/* Read the eDP 1.4+ supported link rates. */
	if (intel_dp->edp_dpcd[0] >= DP_EDP_14) {
		__le16 sink_rates[DP_MAX_SUPPORTED_RATES];
		int i;

		drm_dp_dpcd_read(&intel_dp->aux, DP_SUPPORTED_LINK_RATES,
				sink_rates, sizeof(sink_rates));

		for (i = 0; i < ARRAY_SIZE(sink_rates); i++) {
			int val = le16_to_cpu(sink_rates[i]);

			if (val == 0)
				break;

			/* Value read multiplied by 200kHz gives the per-lane
			 * link rate in kHz. The source rates are, however,
			 * stored in terms of LS_Clk kHz. The full conversion
			 * back to symbols is
			 * (val * 200kHz)*(8/10 ch. encoding)*(1/8 bit to Byte)
			 */
			intel_dp->sink_rates[i] = (val * 200) / 10;
		}
		intel_dp->num_sink_rates = i;
	}

	/*
	 * Use DP_LINK_RATE_SET if DP_SUPPORTED_LINK_RATES are available,
	 * default to DP_MAX_LINK_RATE and DP_LINK_BW_SET otherwise.
	 */
	if (intel_dp->num_sink_rates)
		intel_dp->use_rate_select = true;
	else
		intel_dp_set_sink_rates(intel_dp);

	intel_dp_set_common_rates(intel_dp);

	/* Read the eDP DSC DPCD registers */
	if (INTEL_GEN(dev_priv) >= 10 || IS_GEMINILAKE(dev_priv))
		intel_dp_get_dsc_sink_cap(intel_dp);

	return true;
}

static bool
intel_dp_has_sink_count(struct intel_dp *intel_dp)
{
	if (!intel_dp->attached_connector)
		return false;

	return drm_dp_read_sink_count_cap(&intel_dp->attached_connector->base,
					  intel_dp->dpcd,
					  &intel_dp->desc);
}

static bool
intel_dp_get_dpcd(struct intel_dp *intel_dp)
{
	int ret;

	if (drm_dp_read_dpcd_caps(&intel_dp->aux, intel_dp->dpcd))
		return false;

	/*
	 * Don't clobber cached eDP rates. Also skip re-reading
	 * the OUI/ID since we know it won't change.
	 */
	if (!intel_dp_is_edp(intel_dp)) {
		drm_dp_read_desc(&intel_dp->aux, &intel_dp->desc,
				 drm_dp_is_branch(intel_dp->dpcd));

		intel_dp_set_sink_rates(intel_dp);
		intel_dp_set_common_rates(intel_dp);
	}

	if (intel_dp_has_sink_count(intel_dp)) {
		ret = drm_dp_read_sink_count(&intel_dp->aux);
		if (ret < 0)
			return false;

		/*
		 * Sink count can change between short pulse hpd hence
		 * a member variable in intel_dp will track any changes
		 * between short pulse interrupts.
		 */
		intel_dp->sink_count = ret;

		/*
		 * SINK_COUNT == 0 and DOWNSTREAM_PORT_PRESENT == 1 implies that
		 * a dongle is present but no display. Unless we require to know
		 * if a dongle is present or not, we don't need to update
		 * downstream port information. So, an early return here saves
		 * time from performing other operations which are not required.
		 */
		if (!intel_dp->sink_count)
			return false;
	}

	return drm_dp_read_downstream_info(&intel_dp->aux, intel_dp->dpcd,
					   intel_dp->downstream_ports) == 0;
}

static bool
intel_dp_can_mst(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	return i915->params.enable_dp_mst &&
		intel_dp->can_mst &&
		drm_dp_read_mst_cap(&intel_dp->aux, intel_dp->dpcd);
}

static void
intel_dp_configure_mst(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_encoder *encoder =
		&dp_to_dig_port(intel_dp)->base;
	bool sink_can_mst = drm_dp_read_mst_cap(&intel_dp->aux, intel_dp->dpcd);

	drm_dbg_kms(&i915->drm,
		    "[ENCODER:%d:%s] MST support: port: %s, sink: %s, modparam: %s\n",
		    encoder->base.base.id, encoder->base.name,
		    yesno(intel_dp->can_mst), yesno(sink_can_mst),
		    yesno(i915->params.enable_dp_mst));

	if (!intel_dp->can_mst)
		return;

	intel_dp->is_mst = sink_can_mst &&
		i915->params.enable_dp_mst;

	drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
					intel_dp->is_mst);
}

static bool
intel_dp_get_sink_irq_esi(struct intel_dp *intel_dp, u8 *sink_irq_vector)
{
	return drm_dp_dpcd_read(&intel_dp->aux, DP_SINK_COUNT_ESI,
				sink_irq_vector, DP_DPRX_ESI_LEN) ==
		DP_DPRX_ESI_LEN;
}

bool
intel_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state,
		       const struct drm_connector_state *conn_state)
{
	/*
	 * As per DP 1.4a spec section 2.2.4.3 [MSA Field for Indication
	 * of Color Encoding Format and Content Color Gamut], in order to
	 * sending YCBCR 420 or HDR BT.2020 signals we should use DP VSC SDP.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		return true;

	switch (conn_state->colorspace) {
	case DRM_MODE_COLORIMETRY_SYCC_601:
	case DRM_MODE_COLORIMETRY_OPYCC_601:
	case DRM_MODE_COLORIMETRY_BT2020_YCC:
	case DRM_MODE_COLORIMETRY_BT2020_RGB:
	case DRM_MODE_COLORIMETRY_BT2020_CYCC:
		return true;
	default:
		break;
	}

	return false;
}

static ssize_t intel_dp_vsc_sdp_pack(const struct drm_dp_vsc_sdp *vsc,
				     struct dp_sdp *sdp, size_t size)
{
	size_t length = sizeof(struct dp_sdp);

	if (size < length)
		return -ENOSPC;

	memset(sdp, 0, size);

	/*
	 * Prepare VSC Header for SU as per DP 1.4a spec, Table 2-119
	 * VSC SDP Header Bytes
	 */
	sdp->sdp_header.HB0 = 0; /* Secondary-Data Packet ID = 0 */
	sdp->sdp_header.HB1 = vsc->sdp_type; /* Secondary-data Packet Type */
	sdp->sdp_header.HB2 = vsc->revision; /* Revision Number */
	sdp->sdp_header.HB3 = vsc->length; /* Number of Valid Data Bytes */

	/*
	 * Only revision 0x5 supports Pixel Encoding/Colorimetry Format as
	 * per DP 1.4a spec.
	 */
	if (vsc->revision != 0x5)
		goto out;

	/* VSC SDP Payload for DB16 through DB18 */
	/* Pixel Encoding and Colorimetry Formats  */
	sdp->db[16] = (vsc->pixelformat & 0xf) << 4; /* DB16[7:4] */
	sdp->db[16] |= vsc->colorimetry & 0xf; /* DB16[3:0] */

	switch (vsc->bpc) {
	case 6:
		/* 6bpc: 0x0 */
		break;
	case 8:
		sdp->db[17] = 0x1; /* DB17[3:0] */
		break;
	case 10:
		sdp->db[17] = 0x2;
		break;
	case 12:
		sdp->db[17] = 0x3;
		break;
	case 16:
		sdp->db[17] = 0x4;
		break;
	default:
		MISSING_CASE(vsc->bpc);
		break;
	}
	/* Dynamic Range and Component Bit Depth */
	if (vsc->dynamic_range == DP_DYNAMIC_RANGE_CTA)
		sdp->db[17] |= 0x80;  /* DB17[7] */

	/* Content Type */
	sdp->db[18] = vsc->content_type & 0x7;

out:
	return length;
}

static ssize_t
intel_dp_hdr_metadata_infoframe_sdp_pack(const struct hdmi_drm_infoframe *drm_infoframe,
					 struct dp_sdp *sdp,
					 size_t size)
{
	size_t length = sizeof(struct dp_sdp);
	const int infoframe_size = HDMI_INFOFRAME_HEADER_SIZE + HDMI_DRM_INFOFRAME_SIZE;
	unsigned char buf[HDMI_INFOFRAME_HEADER_SIZE + HDMI_DRM_INFOFRAME_SIZE];
	ssize_t len;

	if (size < length)
		return -ENOSPC;

	memset(sdp, 0, size);

	len = hdmi_drm_infoframe_pack_only(drm_infoframe, buf, sizeof(buf));
	if (len < 0) {
		DRM_DEBUG_KMS("buffer size is smaller than hdr metadata infoframe\n");
		return -ENOSPC;
	}

	if (len != infoframe_size) {
		DRM_DEBUG_KMS("wrong static hdr metadata size\n");
		return -ENOSPC;
	}

	/*
	 * Set up the infoframe sdp packet for HDR static metadata.
	 * Prepare VSC Header for SU as per DP 1.4a spec,
	 * Table 2-100 and Table 2-101
	 */

	/* Secondary-Data Packet ID, 00h for non-Audio INFOFRAME */
	sdp->sdp_header.HB0 = 0;
	/*
	 * Packet Type 80h + Non-audio INFOFRAME Type value
	 * HDMI_INFOFRAME_TYPE_DRM: 0x87
	 * - 80h + Non-audio INFOFRAME Type value
	 * - InfoFrame Type: 0x07
	 *    [CTA-861-G Table-42 Dynamic Range and Mastering InfoFrame]
	 */
	sdp->sdp_header.HB1 = drm_infoframe->type;
	/*
	 * Least Significant Eight Bits of (Data Byte Count – 1)
	 * infoframe_size - 1
	 */
	sdp->sdp_header.HB2 = 0x1D;
	/* INFOFRAME SDP Version Number */
	sdp->sdp_header.HB3 = (0x13 << 2);
	/* CTA Header Byte 2 (INFOFRAME Version Number) */
	sdp->db[0] = drm_infoframe->version;
	/* CTA Header Byte 3 (Length of INFOFRAME): HDMI_DRM_INFOFRAME_SIZE */
	sdp->db[1] = drm_infoframe->length;
	/*
	 * Copy HDMI_DRM_INFOFRAME_SIZE size from a buffer after
	 * HDMI_INFOFRAME_HEADER_SIZE
	 */
	BUILD_BUG_ON(sizeof(sdp->db) < HDMI_DRM_INFOFRAME_SIZE + 2);
	memcpy(&sdp->db[2], &buf[HDMI_INFOFRAME_HEADER_SIZE],
	       HDMI_DRM_INFOFRAME_SIZE);

	/*
	 * Size of DP infoframe sdp packet for HDR static metadata consists of
	 * - DP SDP Header(struct dp_sdp_header): 4 bytes
	 * - Two Data Blocks: 2 bytes
	 *    CTA Header Byte2 (INFOFRAME Version Number)
	 *    CTA Header Byte3 (Length of INFOFRAME)
	 * - HDMI_DRM_INFOFRAME_SIZE: 26 bytes
	 *
	 * Prior to GEN11's GMP register size is identical to DP HDR static metadata
	 * infoframe size. But GEN11+ has larger than that size, write_infoframe
	 * will pad rest of the size.
	 */
	return sizeof(struct dp_sdp_header) + 2 + HDMI_DRM_INFOFRAME_SIZE;
}

static void intel_write_dp_sdp(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       unsigned int type)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct dp_sdp sdp = {};
	ssize_t len;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(type)) == 0)
		return;

	switch (type) {
	case DP_SDP_VSC:
		len = intel_dp_vsc_sdp_pack(&crtc_state->infoframes.vsc, &sdp,
					    sizeof(sdp));
		break;
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		len = intel_dp_hdr_metadata_infoframe_sdp_pack(&crtc_state->infoframes.drm.drm,
							       &sdp, sizeof(sdp));
		break;
	default:
		MISSING_CASE(type);
		return;
	}

	if (drm_WARN_ON(&dev_priv->drm, len < 0))
		return;

	dig_port->write_infoframe(encoder, crtc_state, type, &sdp, len);
}

void intel_write_dp_vsc_sdp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    struct drm_dp_vsc_sdp *vsc)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct dp_sdp sdp = {};
	ssize_t len;

	len = intel_dp_vsc_sdp_pack(vsc, &sdp, sizeof(sdp));

	if (drm_WARN_ON(&dev_priv->drm, len < 0))
		return;

	dig_port->write_infoframe(encoder, crtc_state, DP_SDP_VSC,
					&sdp, len);
}

void intel_dp_set_infoframes(struct intel_encoder *encoder,
			     bool enable,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	i915_reg_t reg = HSW_TVIDEO_DIP_CTL(crtc_state->cpu_transcoder);
	u32 dip_enable = VIDEO_DIP_ENABLE_AVI_HSW | VIDEO_DIP_ENABLE_GCP_HSW |
			 VIDEO_DIP_ENABLE_VS_HSW | VIDEO_DIP_ENABLE_GMP_HSW |
			 VIDEO_DIP_ENABLE_SPD_HSW | VIDEO_DIP_ENABLE_DRM_GLK;
	u32 val = intel_de_read(dev_priv, reg);

	/* TODO: Add DSC case (DIP_ENABLE_PPS) */
	/* When PSR is enabled, this routine doesn't disable VSC DIP */
	if (intel_psr_enabled(intel_dp))
		val &= ~dip_enable;
	else
		val &= ~(dip_enable | VIDEO_DIP_ENABLE_VSC_HSW);

	if (!enable) {
		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);
		return;
	}

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	/* When PSR is enabled, VSC SDP is handled by PSR routine */
	if (!intel_psr_enabled(intel_dp))
		intel_write_dp_sdp(encoder, crtc_state, DP_SDP_VSC);

	intel_write_dp_sdp(encoder, crtc_state, HDMI_PACKET_TYPE_GAMUT_METADATA);
}

static int intel_dp_vsc_sdp_unpack(struct drm_dp_vsc_sdp *vsc,
				   const void *buffer, size_t size)
{
	const struct dp_sdp *sdp = buffer;

	if (size < sizeof(struct dp_sdp))
		return -EINVAL;

	memset(vsc, 0, size);

	if (sdp->sdp_header.HB0 != 0)
		return -EINVAL;

	if (sdp->sdp_header.HB1 != DP_SDP_VSC)
		return -EINVAL;

	vsc->sdp_type = sdp->sdp_header.HB1;
	vsc->revision = sdp->sdp_header.HB2;
	vsc->length = sdp->sdp_header.HB3;

	if ((sdp->sdp_header.HB2 == 0x2 && sdp->sdp_header.HB3 == 0x8) ||
	    (sdp->sdp_header.HB2 == 0x4 && sdp->sdp_header.HB3 == 0xe)) {
		/*
		 * - HB2 = 0x2, HB3 = 0x8
		 *   VSC SDP supporting 3D stereo + PSR
		 * - HB2 = 0x4, HB3 = 0xe
		 *   VSC SDP supporting 3D stereo + PSR2 with Y-coordinate of
		 *   first scan line of the SU region (applies to eDP v1.4b
		 *   and higher).
		 */
		return 0;
	} else if (sdp->sdp_header.HB2 == 0x5 && sdp->sdp_header.HB3 == 0x13) {
		/*
		 * - HB2 = 0x5, HB3 = 0x13
		 *   VSC SDP supporting 3D stereo + PSR2 + Pixel Encoding/Colorimetry
		 *   Format.
		 */
		vsc->pixelformat = (sdp->db[16] >> 4) & 0xf;
		vsc->colorimetry = sdp->db[16] & 0xf;
		vsc->dynamic_range = (sdp->db[17] >> 7) & 0x1;

		switch (sdp->db[17] & 0x7) {
		case 0x0:
			vsc->bpc = 6;
			break;
		case 0x1:
			vsc->bpc = 8;
			break;
		case 0x2:
			vsc->bpc = 10;
			break;
		case 0x3:
			vsc->bpc = 12;
			break;
		case 0x4:
			vsc->bpc = 16;
			break;
		default:
			MISSING_CASE(sdp->db[17] & 0x7);
			return -EINVAL;
		}

		vsc->content_type = sdp->db[18] & 0x7;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int
intel_dp_hdr_metadata_infoframe_sdp_unpack(struct hdmi_drm_infoframe *drm_infoframe,
					   const void *buffer, size_t size)
{
	int ret;

	const struct dp_sdp *sdp = buffer;

	if (size < sizeof(struct dp_sdp))
		return -EINVAL;

	if (sdp->sdp_header.HB0 != 0)
		return -EINVAL;

	if (sdp->sdp_header.HB1 != HDMI_INFOFRAME_TYPE_DRM)
		return -EINVAL;

	/*
	 * Least Significant Eight Bits of (Data Byte Count – 1)
	 * 1Dh (i.e., Data Byte Count = 30 bytes).
	 */
	if (sdp->sdp_header.HB2 != 0x1D)
		return -EINVAL;

	/* Most Significant Two Bits of (Data Byte Count – 1), Clear to 00b. */
	if ((sdp->sdp_header.HB3 & 0x3) != 0)
		return -EINVAL;

	/* INFOFRAME SDP Version Number */
	if (((sdp->sdp_header.HB3 >> 2) & 0x3f) != 0x13)
		return -EINVAL;

	/* CTA Header Byte 2 (INFOFRAME Version Number) */
	if (sdp->db[0] != 1)
		return -EINVAL;

	/* CTA Header Byte 3 (Length of INFOFRAME): HDMI_DRM_INFOFRAME_SIZE */
	if (sdp->db[1] != HDMI_DRM_INFOFRAME_SIZE)
		return -EINVAL;

	ret = hdmi_drm_infoframe_unpack_only(drm_infoframe, &sdp->db[2],
					     HDMI_DRM_INFOFRAME_SIZE);

	return ret;
}

static void intel_read_dp_vsc_sdp(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state,
				  struct drm_dp_vsc_sdp *vsc)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	unsigned int type = DP_SDP_VSC;
	struct dp_sdp sdp = {};
	int ret;

	/* When PSR is enabled, VSC SDP is handled by PSR routine */
	if (intel_psr_enabled(intel_dp))
		return;

	if ((crtc_state->infoframes.enable &
	     intel_hdmi_infoframe_enable(type)) == 0)
		return;

	dig_port->read_infoframe(encoder, crtc_state, type, &sdp, sizeof(sdp));

	ret = intel_dp_vsc_sdp_unpack(vsc, &sdp, sizeof(sdp));

	if (ret)
		drm_dbg_kms(&dev_priv->drm, "Failed to unpack DP VSC SDP\n");
}

static void intel_read_dp_hdr_metadata_infoframe_sdp(struct intel_encoder *encoder,
						     struct intel_crtc_state *crtc_state,
						     struct hdmi_drm_infoframe *drm_infoframe)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	unsigned int type = HDMI_PACKET_TYPE_GAMUT_METADATA;
	struct dp_sdp sdp = {};
	int ret;

	if ((crtc_state->infoframes.enable &
	    intel_hdmi_infoframe_enable(type)) == 0)
		return;

	dig_port->read_infoframe(encoder, crtc_state, type, &sdp,
				 sizeof(sdp));

	ret = intel_dp_hdr_metadata_infoframe_sdp_unpack(drm_infoframe, &sdp,
							 sizeof(sdp));

	if (ret)
		drm_dbg_kms(&dev_priv->drm,
			    "Failed to unpack DP HDR Metadata Infoframe SDP\n");
}

void intel_read_dp_sdp(struct intel_encoder *encoder,
		       struct intel_crtc_state *crtc_state,
		       unsigned int type)
{
	if (encoder->type != INTEL_OUTPUT_DDI)
		return;

	switch (type) {
	case DP_SDP_VSC:
		intel_read_dp_vsc_sdp(encoder, crtc_state,
				      &crtc_state->infoframes.vsc);
		break;
	case HDMI_PACKET_TYPE_GAMUT_METADATA:
		intel_read_dp_hdr_metadata_infoframe_sdp(encoder, crtc_state,
							 &crtc_state->infoframes.drm.drm);
		break;
	default:
		MISSING_CASE(type);
		break;
	}
}

static u8 intel_dp_autotest_link_training(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int status = 0;
	int test_link_rate;
	u8 test_lane_count, test_link_bw;
	/* (DP CTS 1.2)
	 * 4.3.1.11
	 */
	/* Read the TEST_LANE_COUNT and TEST_LINK_RTAE fields (DP CTS 3.1.4) */
	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_LANE_COUNT,
				   &test_lane_count);

	if (status <= 0) {
		drm_dbg_kms(&i915->drm, "Lane count read failed\n");
		return DP_TEST_NAK;
	}
	test_lane_count &= DP_MAX_LANE_COUNT_MASK;

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_LINK_RATE,
				   &test_link_bw);
	if (status <= 0) {
		drm_dbg_kms(&i915->drm, "Link Rate read failed\n");
		return DP_TEST_NAK;
	}
	test_link_rate = drm_dp_bw_code_to_link_rate(test_link_bw);

	/* Validate the requested link rate and lane count */
	if (!intel_dp_link_params_valid(intel_dp, test_link_rate,
					test_lane_count))
		return DP_TEST_NAK;

	intel_dp->compliance.test_lane_count = test_lane_count;
	intel_dp->compliance.test_link_rate = test_link_rate;

	return DP_TEST_ACK;
}

static u8 intel_dp_autotest_video_pattern(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 test_pattern;
	u8 test_misc;
	__be16 h_width, v_height;
	int status = 0;

	/* Read the TEST_PATTERN (DP CTS 3.1.5) */
	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_PATTERN,
				   &test_pattern);
	if (status <= 0) {
		drm_dbg_kms(&i915->drm, "Test pattern read failed\n");
		return DP_TEST_NAK;
	}
	if (test_pattern != DP_COLOR_RAMP)
		return DP_TEST_NAK;

	status = drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_H_WIDTH_HI,
				  &h_width, 2);
	if (status <= 0) {
		drm_dbg_kms(&i915->drm, "H Width read failed\n");
		return DP_TEST_NAK;
	}

	status = drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_V_HEIGHT_HI,
				  &v_height, 2);
	if (status <= 0) {
		drm_dbg_kms(&i915->drm, "V Height read failed\n");
		return DP_TEST_NAK;
	}

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_MISC0,
				   &test_misc);
	if (status <= 0) {
		drm_dbg_kms(&i915->drm, "TEST MISC read failed\n");
		return DP_TEST_NAK;
	}
	if ((test_misc & DP_TEST_COLOR_FORMAT_MASK) != DP_COLOR_FORMAT_RGB)
		return DP_TEST_NAK;
	if (test_misc & DP_TEST_DYNAMIC_RANGE_CEA)
		return DP_TEST_NAK;
	switch (test_misc & DP_TEST_BIT_DEPTH_MASK) {
	case DP_TEST_BIT_DEPTH_6:
		intel_dp->compliance.test_data.bpc = 6;
		break;
	case DP_TEST_BIT_DEPTH_8:
		intel_dp->compliance.test_data.bpc = 8;
		break;
	default:
		return DP_TEST_NAK;
	}

	intel_dp->compliance.test_data.video_pattern = test_pattern;
	intel_dp->compliance.test_data.hdisplay = be16_to_cpu(h_width);
	intel_dp->compliance.test_data.vdisplay = be16_to_cpu(v_height);
	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance.test_active = true;

	return DP_TEST_ACK;
}

static u8 intel_dp_autotest_edid(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 test_result = DP_TEST_ACK;
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct drm_connector *connector = &intel_connector->base;

	if (intel_connector->detect_edid == NULL ||
	    connector->edid_corrupt ||
	    intel_dp->aux.i2c_defer_count > 6) {
		/* Check EDID read for NACKs, DEFERs and corruption
		 * (DP CTS 1.2 Core r1.1)
		 *    4.2.2.4 : Failed EDID read, I2C_NAK
		 *    4.2.2.5 : Failed EDID read, I2C_DEFER
		 *    4.2.2.6 : EDID corruption detected
		 * Use failsafe mode for all cases
		 */
		if (intel_dp->aux.i2c_nack_count > 0 ||
			intel_dp->aux.i2c_defer_count > 0)
			drm_dbg_kms(&i915->drm,
				    "EDID read had %d NACKs, %d DEFERs\n",
				    intel_dp->aux.i2c_nack_count,
				    intel_dp->aux.i2c_defer_count);
		intel_dp->compliance.test_data.edid = INTEL_DP_RESOLUTION_FAILSAFE;
	} else {
		struct edid *block = intel_connector->detect_edid;

		/* We have to write the checksum
		 * of the last block read
		 */
		block += intel_connector->detect_edid->extensions;

		if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_TEST_EDID_CHECKSUM,
				       block->checksum) <= 0)
			drm_dbg_kms(&i915->drm,
				    "Failed to write EDID checksum\n");

		test_result = DP_TEST_ACK | DP_TEST_EDID_CHECKSUM_WRITE;
		intel_dp->compliance.test_data.edid = INTEL_DP_RESOLUTION_PREFERRED;
	}

	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance.test_active = true;

	return test_result;
}

static u8 intel_dp_prepare_phytest(struct intel_dp *intel_dp)
{
	struct drm_dp_phy_test_params *data =
		&intel_dp->compliance.test_data.phytest;

	if (drm_dp_get_phy_test_pattern(&intel_dp->aux, data)) {
		DRM_DEBUG_KMS("DP Phy Test pattern AUX read failure\n");
		return DP_TEST_NAK;
	}

	/*
	 * link_mst is set to false to avoid executing mst related code
	 * during compliance testing.
	 */
	intel_dp->link_mst = false;

	return DP_TEST_ACK;
}

static void intel_dp_phy_pattern_update(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv =
			to_i915(dp_to_dig_port(intel_dp)->base.base.dev);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_dp_phy_test_params *data =
			&intel_dp->compliance.test_data.phytest;
	struct intel_crtc *crtc = to_intel_crtc(dig_port->base.base.crtc);
	enum pipe pipe = crtc->pipe;
	u32 pattern_val;

	switch (data->phy_pattern) {
	case DP_PHY_TEST_PATTERN_NONE:
		DRM_DEBUG_KMS("Disable Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe), 0x0);
		break;
	case DP_PHY_TEST_PATTERN_D10_2:
		DRM_DEBUG_KMS("Set D10.2 Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_D10_2);
		break;
	case DP_PHY_TEST_PATTERN_ERROR_COUNT:
		DRM_DEBUG_KMS("Set Error Count Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE |
			       DDI_DP_COMP_CTL_SCRAMBLED_0);
		break;
	case DP_PHY_TEST_PATTERN_PRBS7:
		DRM_DEBUG_KMS("Set PRBS7 Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_PRBS7);
		break;
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
		/*
		 * FIXME: Ideally pattern should come from DPCD 0x250. As
		 * current firmware of DPR-100 could not set it, so hardcoding
		 * now for complaince test.
		 */
		DRM_DEBUG_KMS("Set 80Bit Custom Phy Test Pattern 0x3e0f83e0 0x0f83e0f8 0x0000f83e\n");
		pattern_val = 0x3e0f83e0;
		intel_de_write(dev_priv, DDI_DP_COMP_PAT(pipe, 0), pattern_val);
		pattern_val = 0x0f83e0f8;
		intel_de_write(dev_priv, DDI_DP_COMP_PAT(pipe, 1), pattern_val);
		pattern_val = 0x0000f83e;
		intel_de_write(dev_priv, DDI_DP_COMP_PAT(pipe, 2), pattern_val);
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE |
			       DDI_DP_COMP_CTL_CUSTOM80);
		break;
	case DP_PHY_TEST_PATTERN_CP2520:
		/*
		 * FIXME: Ideally pattern should come from DPCD 0x24A. As
		 * current firmware of DPR-100 could not set it, so hardcoding
		 * now for complaince test.
		 */
		DRM_DEBUG_KMS("Set HBR2 compliance Phy Test Pattern\n");
		pattern_val = 0xFB;
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_HBR2 |
			       pattern_val);
		break;
	default:
		WARN(1, "Invalid Phy Test Pattern\n");
	}
}

static void
intel_dp_autotest_phy_ddi_disable(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *crtc = to_intel_crtc(dig_port->base.base.crtc);
	enum pipe pipe = crtc->pipe;
	u32 trans_ddi_func_ctl_value, trans_conf_value, dp_tp_ctl_value;

	trans_ddi_func_ctl_value = intel_de_read(dev_priv,
						 TRANS_DDI_FUNC_CTL(pipe));
	trans_conf_value = intel_de_read(dev_priv, PIPECONF(pipe));
	dp_tp_ctl_value = intel_de_read(dev_priv, TGL_DP_TP_CTL(pipe));

	trans_ddi_func_ctl_value &= ~(TRANS_DDI_FUNC_ENABLE |
				      TGL_TRANS_DDI_PORT_MASK);
	trans_conf_value &= ~PIPECONF_ENABLE;
	dp_tp_ctl_value &= ~DP_TP_CTL_ENABLE;

	intel_de_write(dev_priv, PIPECONF(pipe), trans_conf_value);
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(pipe),
		       trans_ddi_func_ctl_value);
	intel_de_write(dev_priv, TGL_DP_TP_CTL(pipe), dp_tp_ctl_value);
}

static void
intel_dp_autotest_phy_ddi_enable(struct intel_dp *intel_dp, uint8_t lane_cnt)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = dig_port->base.port;
	struct intel_crtc *crtc = to_intel_crtc(dig_port->base.base.crtc);
	enum pipe pipe = crtc->pipe;
	u32 trans_ddi_func_ctl_value, trans_conf_value, dp_tp_ctl_value;

	trans_ddi_func_ctl_value = intel_de_read(dev_priv,
						 TRANS_DDI_FUNC_CTL(pipe));
	trans_conf_value = intel_de_read(dev_priv, PIPECONF(pipe));
	dp_tp_ctl_value = intel_de_read(dev_priv, TGL_DP_TP_CTL(pipe));

	trans_ddi_func_ctl_value |= TRANS_DDI_FUNC_ENABLE |
				    TGL_TRANS_DDI_SELECT_PORT(port);
	trans_conf_value |= PIPECONF_ENABLE;
	dp_tp_ctl_value |= DP_TP_CTL_ENABLE;

	intel_de_write(dev_priv, PIPECONF(pipe), trans_conf_value);
	intel_de_write(dev_priv, TGL_DP_TP_CTL(pipe), dp_tp_ctl_value);
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(pipe),
		       trans_ddi_func_ctl_value);
}

void intel_dp_process_phy_request(struct intel_dp *intel_dp)
{
	struct drm_dp_phy_test_params *data =
		&intel_dp->compliance.test_data.phytest;
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (!intel_dp_get_link_status(intel_dp, link_status)) {
		DRM_DEBUG_KMS("failed to get link status\n");
		return;
	}

	/* retrieve vswing & pre-emphasis setting */
	intel_dp_get_adjust_train(intel_dp, link_status);

	intel_dp_autotest_phy_ddi_disable(intel_dp);

	intel_dp_set_signal_levels(intel_dp);

	intel_dp_phy_pattern_update(intel_dp);

	intel_dp_autotest_phy_ddi_enable(intel_dp, data->num_lanes);

	drm_dp_set_phy_test_pattern(&intel_dp->aux, data,
				    link_status[DP_DPCD_REV]);
}

static u8 intel_dp_autotest_phy_pattern(struct intel_dp *intel_dp)
{
	u8 test_result;

	test_result = intel_dp_prepare_phytest(intel_dp);
	if (test_result != DP_TEST_ACK)
		DRM_ERROR("Phy test preparation failed\n");

	intel_dp_process_phy_request(intel_dp);

	return test_result;
}

static void intel_dp_handle_test_request(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 response = DP_TEST_NAK;
	u8 request = 0;
	int status;

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_REQUEST, &request);
	if (status <= 0) {
		drm_dbg_kms(&i915->drm,
			    "Could not read test request from sink\n");
		goto update_status;
	}

	switch (request) {
	case DP_TEST_LINK_TRAINING:
		drm_dbg_kms(&i915->drm, "LINK_TRAINING test requested\n");
		response = intel_dp_autotest_link_training(intel_dp);
		break;
	case DP_TEST_LINK_VIDEO_PATTERN:
		drm_dbg_kms(&i915->drm, "TEST_PATTERN test requested\n");
		response = intel_dp_autotest_video_pattern(intel_dp);
		break;
	case DP_TEST_LINK_EDID_READ:
		drm_dbg_kms(&i915->drm, "EDID test requested\n");
		response = intel_dp_autotest_edid(intel_dp);
		break;
	case DP_TEST_LINK_PHY_TEST_PATTERN:
		drm_dbg_kms(&i915->drm, "PHY_PATTERN test requested\n");
		response = intel_dp_autotest_phy_pattern(intel_dp);
		break;
	default:
		drm_dbg_kms(&i915->drm, "Invalid test request '%02x'\n",
			    request);
		break;
	}

	if (response & DP_TEST_ACK)
		intel_dp->compliance.test_type = request;

update_status:
	status = drm_dp_dpcd_writeb(&intel_dp->aux, DP_TEST_RESPONSE, response);
	if (status <= 0)
		drm_dbg_kms(&i915->drm,
			    "Could not write test response to sink\n");
}

/**
 * intel_dp_check_mst_status - service any pending MST interrupts, check link status
 * @intel_dp: Intel DP struct
 *
 * Read any pending MST interrupts, call MST core to handle these and ack the
 * interrupts. Check if the main and AUX link state is ok.
 *
 * Returns:
 * - %true if pending interrupts were serviced (or no interrupts were
 *   pending) w/o detecting an error condition.
 * - %false if an error condition - like AUX failure or a loss of link - is
 *   detected, which needs servicing from the hotplug work.
 */
static bool
intel_dp_check_mst_status(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	bool link_ok = true;

	drm_WARN_ON_ONCE(&i915->drm, intel_dp->active_mst_links < 0);

	for (;;) {
		u8 esi[DP_DPRX_ESI_LEN] = {};
		bool handled;
		int retry;

		if (!intel_dp_get_sink_irq_esi(intel_dp, esi)) {
			drm_dbg_kms(&i915->drm,
				    "failed to get ESI - device may have failed\n");
			link_ok = false;

			break;
		}

		/* check link status - esi[10] = 0x200c */
		if (intel_dp->active_mst_links > 0 && link_ok &&
		    !drm_dp_channel_eq_ok(&esi[10], intel_dp->lane_count)) {
			drm_dbg_kms(&i915->drm,
				    "channel EQ not ok, retraining\n");
			link_ok = false;
		}

		drm_dbg_kms(&i915->drm, "got esi %3ph\n", esi);

		drm_dp_mst_hpd_irq(&intel_dp->mst_mgr, esi, &handled);
		if (!handled)
			break;

		for (retry = 0; retry < 3; retry++) {
			int wret;

			wret = drm_dp_dpcd_write(&intel_dp->aux,
						 DP_SINK_COUNT_ESI+1,
						 &esi[1], 3);
			if (wret == 3)
				break;
		}
	}

	return link_ok;
}

static bool
intel_dp_needs_link_retrain(struct intel_dp *intel_dp)
{
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (!intel_dp->link_trained)
		return false;

	/*
	 * While PSR source HW is enabled, it will control main-link sending
	 * frames, enabling and disabling it so trying to do a retrain will fail
	 * as the link would or not be on or it could mix training patterns
	 * and frame data at the same time causing retrain to fail.
	 * Also when exiting PSR, HW will retrain the link anyways fixing
	 * any link status error.
	 */
	if (intel_psr_enabled(intel_dp))
		return false;

	if (!intel_dp_get_link_status(intel_dp, link_status))
		return false;

	/*
	 * Validate the cached values of intel_dp->link_rate and
	 * intel_dp->lane_count before attempting to retrain.
	 */
	if (!intel_dp_link_params_valid(intel_dp, intel_dp->link_rate,
					intel_dp->lane_count))
		return false;

	/* Retrain if Channel EQ or CR not ok */
	return !drm_dp_channel_eq_ok(link_status, intel_dp->lane_count);
}

static bool intel_dp_has_connector(struct intel_dp *intel_dp,
				   const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_encoder *encoder;
	enum pipe pipe;

	if (!conn_state->best_encoder)
		return false;

	/* SST */
	encoder = &dp_to_dig_port(intel_dp)->base;
	if (conn_state->best_encoder == &encoder->base)
		return true;

	/* MST */
	for_each_pipe(i915, pipe) {
		encoder = &intel_dp->mst_encoders[pipe]->base;
		if (conn_state->best_encoder == &encoder->base)
			return true;
	}

	return false;
}

static int intel_dp_prep_link_retrain(struct intel_dp *intel_dp,
				      struct drm_modeset_acquire_ctx *ctx,
				      u32 *crtc_mask)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	int ret = 0;

	*crtc_mask = 0;

	if (!intel_dp_needs_link_retrain(intel_dp))
		return 0;

	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *conn_state =
			connector->base.state;
		struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (!intel_dp_has_connector(intel_dp, conn_state))
			continue;

		crtc = to_intel_crtc(conn_state->crtc);
		if (!crtc)
			continue;

		ret = drm_modeset_lock(&crtc->base.mutex, ctx);
		if (ret)
			break;

		crtc_state = to_intel_crtc_state(crtc->base.state);

		drm_WARN_ON(&i915->drm, !intel_crtc_has_dp_encoder(crtc_state));

		if (!crtc_state->hw.active)
			continue;

		if (conn_state->commit &&
		    !try_wait_for_completion(&conn_state->commit->hw_done))
			continue;

		*crtc_mask |= drm_crtc_mask(&crtc->base);
	}
	drm_connector_list_iter_end(&conn_iter);

	if (!intel_dp_needs_link_retrain(intel_dp))
		*crtc_mask = 0;

	return ret;
}

static bool intel_dp_is_connected(struct intel_dp *intel_dp)
{
	struct intel_connector *connector = intel_dp->attached_connector;

	return connector->base.status == connector_status_connected ||
		intel_dp->is_mst;
}

int intel_dp_retrain_link(struct intel_encoder *encoder,
			  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc;
	u32 crtc_mask;
	int ret;

	if (!intel_dp_is_connected(intel_dp))
		return 0;

	ret = drm_modeset_lock(&dev_priv->drm.mode_config.connection_mutex,
			       ctx);
	if (ret)
		return ret;

	ret = intel_dp_prep_link_retrain(intel_dp, ctx, &crtc_mask);
	if (ret)
		return ret;

	if (crtc_mask == 0)
		return 0;

	drm_dbg_kms(&dev_priv->drm, "[ENCODER:%d:%s] retraining link\n",
		    encoder->base.base.id, encoder->base.name);

	for_each_intel_crtc_mask(&dev_priv->drm, crtc, crtc_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		/* Suppress underruns caused by re-training */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, crtc->pipe, false);
		if (crtc_state->has_pch_encoder)
			intel_set_pch_fifo_underrun_reporting(dev_priv,
							      intel_crtc_pch_transcoder(crtc), false);
	}

	intel_dp_start_link_train(intel_dp);
	intel_dp_stop_link_train(intel_dp);

	for_each_intel_crtc_mask(&dev_priv->drm, crtc, crtc_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		/* Keep underrun reporting disabled until things are stable */
		intel_wait_for_vblank(dev_priv, crtc->pipe);

		intel_set_cpu_fifo_underrun_reporting(dev_priv, crtc->pipe, true);
		if (crtc_state->has_pch_encoder)
			intel_set_pch_fifo_underrun_reporting(dev_priv,
							      intel_crtc_pch_transcoder(crtc), true);
	}

	return 0;
}

/*
 * If display is now connected check links status,
 * there has been known issues of link loss triggering
 * long pulse.
 *
 * Some sinks (eg. ASUS PB287Q) seem to perform some
 * weird HPD ping pong during modesets. So we can apparently
 * end up with HPD going low during a modeset, and then
 * going back up soon after. And once that happens we must
 * retrain the link to get a picture. That's in case no
 * userspace component reacted to intermittent HPD dip.
 */
static enum intel_hotplug_state
intel_dp_hotplug(struct intel_encoder *encoder,
		 struct intel_connector *connector)
{
	struct drm_modeset_acquire_ctx ctx;
	enum intel_hotplug_state state;
	int ret;

	state = intel_encoder_hotplug(encoder, connector);

	drm_modeset_acquire_init(&ctx, 0);

	for (;;) {
		ret = intel_dp_retrain_link(encoder, &ctx);

		if (ret == -EDEADLK) {
			drm_modeset_backoff(&ctx);
			continue;
		}

		break;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	drm_WARN(encoder->base.dev, ret,
		 "Acquiring modeset locks failed with %i\n", ret);

	/*
	 * Keeping it consistent with intel_ddi_hotplug() and
	 * intel_hdmi_hotplug().
	 */
	if (state == INTEL_HOTPLUG_UNCHANGED && !connector->hotplug_retries)
		state = INTEL_HOTPLUG_RETRY;

	return state;
}

static void intel_dp_check_service_irq(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 val;

	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_DEVICE_SERVICE_IRQ_VECTOR, &val) != 1 || !val)
		return;

	drm_dp_dpcd_writeb(&intel_dp->aux, DP_DEVICE_SERVICE_IRQ_VECTOR, val);

	if (val & DP_AUTOMATED_TEST_REQUEST)
		intel_dp_handle_test_request(intel_dp);

	if (val & DP_CP_IRQ)
		intel_hdcp_handle_cp_irq(intel_dp->attached_connector);

	if (val & DP_SINK_SPECIFIC_IRQ)
		drm_dbg_kms(&i915->drm, "Sink specific irq unhandled\n");
}

/*
 * According to DP spec
 * 5.1.2:
 *  1. Read DPCD
 *  2. Configure link according to Receiver Capabilities
 *  3. Use Link Training from 2.5.3.3 and 3.5.1.3
 *  4. Check link status on receipt of hot-plug interrupt
 *
 * intel_dp_short_pulse -  handles short pulse interrupts
 * when full detection is not required.
 * Returns %true if short pulse is handled and full detection
 * is NOT required and %false otherwise.
 */
static bool
intel_dp_short_pulse(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u8 old_sink_count = intel_dp->sink_count;
	bool ret;

	/*
	 * Clearing compliance test variables to allow capturing
	 * of values for next automated test request.
	 */
	memset(&intel_dp->compliance, 0, sizeof(intel_dp->compliance));

	/*
	 * Now read the DPCD to see if it's actually running
	 * If the current value of sink count doesn't match with
	 * the value that was stored earlier or dpcd read failed
	 * we need to do full detection
	 */
	ret = intel_dp_get_dpcd(intel_dp);

	if ((old_sink_count != intel_dp->sink_count) || !ret) {
		/* No need to proceed if we are going to do full detect */
		return false;
	}

	intel_dp_check_service_irq(intel_dp);

	/* Handle CEC interrupts, if any */
	drm_dp_cec_irq(&intel_dp->aux);

	/* defer to the hotplug work for link retraining if needed */
	if (intel_dp_needs_link_retrain(intel_dp))
		return false;

	intel_psr_short_pulse(intel_dp);

	if (intel_dp->compliance.test_type == DP_TEST_LINK_TRAINING) {
		drm_dbg_kms(&dev_priv->drm,
			    "Link Training Compliance Test requested\n");
		/* Send a Hotplug Uevent to userspace to start modeset */
		drm_kms_helper_hotplug_event(&dev_priv->drm);
	}

	return true;
}

/* XXX this is probably wrong for multiple downstream ports */
static enum drm_connector_status
intel_dp_detect_dpcd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_lspcon *lspcon = dp_to_lspcon(intel_dp);
	u8 *dpcd = intel_dp->dpcd;
	u8 type;

	if (drm_WARN_ON(&i915->drm, intel_dp_is_edp(intel_dp)))
		return connector_status_connected;

	if (lspcon->active)
		lspcon_resume(lspcon);

	if (!intel_dp_get_dpcd(intel_dp))
		return connector_status_disconnected;

	/* if there's no downstream port, we're done */
	if (!drm_dp_is_branch(dpcd))
		return connector_status_connected;

	/* If we're HPD-aware, SINK_COUNT changes dynamically */
	if (intel_dp_has_sink_count(intel_dp) &&
	    intel_dp->downstream_ports[0] & DP_DS_PORT_HPD) {
		return intel_dp->sink_count ?
		connector_status_connected : connector_status_disconnected;
	}

	if (intel_dp_can_mst(intel_dp))
		return connector_status_connected;

	/* If no HPD, poke DDC gently */
	if (drm_probe_ddc(&intel_dp->aux.ddc))
		return connector_status_connected;

	/* Well we tried, say unknown for unreliable port types */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11) {
		type = intel_dp->downstream_ports[0] & DP_DS_PORT_TYPE_MASK;
		if (type == DP_DS_PORT_TYPE_VGA ||
		    type == DP_DS_PORT_TYPE_NON_EDID)
			return connector_status_unknown;
	} else {
		type = intel_dp->dpcd[DP_DOWNSTREAMPORT_PRESENT] &
			DP_DWN_STRM_PORT_TYPE_MASK;
		if (type == DP_DWN_STRM_PORT_TYPE_ANALOG ||
		    type == DP_DWN_STRM_PORT_TYPE_OTHER)
			return connector_status_unknown;
	}

	/* Anything else is out of spec, warn and ignore */
	drm_dbg_kms(&i915->drm, "Broken DP branch device, ignoring\n");
	return connector_status_disconnected;
}

static enum drm_connector_status
edp_detect(struct intel_dp *intel_dp)
{
	return connector_status_connected;
}

static bool ibx_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->hotplug.pch_hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, SDEISR) & bit;
}

static bool g4x_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_B:
		bit = PORTB_HOTPLUG_LIVE_STATUS_G4X;
		break;
	case HPD_PORT_C:
		bit = PORTC_HOTPLUG_LIVE_STATUS_G4X;
		break;
	case HPD_PORT_D:
		bit = PORTD_HOTPLUG_LIVE_STATUS_G4X;
		break;
	default:
		MISSING_CASE(encoder->hpd_pin);
		return false;
	}

	return intel_de_read(dev_priv, PORT_HOTPLUG_STAT) & bit;
}

static bool gm45_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_B:
		bit = PORTB_HOTPLUG_LIVE_STATUS_GM45;
		break;
	case HPD_PORT_C:
		bit = PORTC_HOTPLUG_LIVE_STATUS_GM45;
		break;
	case HPD_PORT_D:
		bit = PORTD_HOTPLUG_LIVE_STATUS_GM45;
		break;
	default:
		MISSING_CASE(encoder->hpd_pin);
		return false;
	}

	return intel_de_read(dev_priv, PORT_HOTPLUG_STAT) & bit;
}

static bool ilk_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->hotplug.hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, DEISR) & bit;
}

/*
 * intel_digital_port_connected - is the specified port connected?
 * @encoder: intel_encoder
 *
 * In cases where there's a connector physically connected but it can't be used
 * by our hardware we also return false, since the rest of the driver should
 * pretty much treat the port as disconnected. This is relevant for type-C
 * (starting on ICL) where there's ownership involved.
 *
 * Return %true if port is connected, %false otherwise.
 */
bool intel_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_connected = false;
	intel_wakeref_t wakeref;

	with_intel_display_power(dev_priv, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		is_connected = dig_port->connected(encoder);

	return is_connected;
}

static struct edid *
intel_dp_get_edid(struct intel_dp *intel_dp)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;

	/* use cached edid if we have one */
	if (intel_connector->edid) {
		/* invalid edid */
		if (IS_ERR(intel_connector->edid))
			return NULL;

		return drm_edid_duplicate(intel_connector->edid);
	} else
		return drm_get_edid(&intel_connector->base,
				    &intel_dp->aux.ddc);
}

static void
intel_dp_update_dfp(struct intel_dp *intel_dp,
		    const struct edid *edid)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;

	intel_dp->dfp.max_bpc =
		drm_dp_downstream_max_bpc(intel_dp->dpcd,
					  intel_dp->downstream_ports, edid);

	intel_dp->dfp.max_dotclock =
		drm_dp_downstream_max_dotclock(intel_dp->dpcd,
					       intel_dp->downstream_ports);

	intel_dp->dfp.min_tmds_clock =
		drm_dp_downstream_min_tmds_clock(intel_dp->dpcd,
						 intel_dp->downstream_ports,
						 edid);
	intel_dp->dfp.max_tmds_clock =
		drm_dp_downstream_max_tmds_clock(intel_dp->dpcd,
						 intel_dp->downstream_ports,
						 edid);

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] DFP max bpc %d, max dotclock %d, TMDS clock %d-%d\n",
		    connector->base.base.id, connector->base.name,
		    intel_dp->dfp.max_bpc,
		    intel_dp->dfp.max_dotclock,
		    intel_dp->dfp.min_tmds_clock,
		    intel_dp->dfp.max_tmds_clock);
}

static void
intel_dp_update_420(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	bool is_branch, ycbcr_420_passthrough, ycbcr_444_to_420;

	/* No YCbCr output support on gmch platforms */
	if (HAS_GMCH(i915))
		return;

	/*
	 * ILK doesn't seem capable of DP YCbCr output. The
	 * displayed image is severly corrupted. SNB+ is fine.
	 */
	if (IS_GEN(i915, 5))
		return;

	is_branch = drm_dp_is_branch(intel_dp->dpcd);
	ycbcr_420_passthrough =
		drm_dp_downstream_420_passthrough(intel_dp->dpcd,
						  intel_dp->downstream_ports);
	ycbcr_444_to_420 =
		drm_dp_downstream_444_to_420_conversion(intel_dp->dpcd,
							intel_dp->downstream_ports);

	if (INTEL_GEN(i915) >= 11) {
		/* Prefer 4:2:0 passthrough over 4:4:4->4:2:0 conversion */
		intel_dp->dfp.ycbcr_444_to_420 =
			ycbcr_444_to_420 && !ycbcr_420_passthrough;

		connector->base.ycbcr_420_allowed =
			!is_branch || ycbcr_444_to_420 || ycbcr_420_passthrough;
	} else {
		/* 4:4:4->4:2:0 conversion is the only way */
		intel_dp->dfp.ycbcr_444_to_420 = ycbcr_444_to_420;

		connector->base.ycbcr_420_allowed = ycbcr_444_to_420;
	}

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] YCbCr 4:2:0 allowed? %s, YCbCr 4:4:4->4:2:0 conversion? %s\n",
		    connector->base.base.id, connector->base.name,
		    yesno(connector->base.ycbcr_420_allowed),
		    yesno(intel_dp->dfp.ycbcr_444_to_420));
}

static void
intel_dp_set_edid(struct intel_dp *intel_dp)
{
	struct intel_connector *connector = intel_dp->attached_connector;
	struct edid *edid;

	intel_dp_unset_edid(intel_dp);
	edid = intel_dp_get_edid(intel_dp);
	connector->detect_edid = edid;

	intel_dp_update_dfp(intel_dp, edid);
	intel_dp_update_420(intel_dp);

	if (edid && edid->input & DRM_EDID_INPUT_DIGITAL) {
		intel_dp->has_hdmi_sink = drm_detect_hdmi_monitor(edid);
		intel_dp->has_audio = drm_detect_monitor_audio(edid);
	}

	drm_dp_cec_set_edid(&intel_dp->aux, edid);
	intel_dp->edid_quirks = drm_dp_get_edid_quirks(edid);
}

static void
intel_dp_unset_edid(struct intel_dp *intel_dp)
{
	struct intel_connector *connector = intel_dp->attached_connector;

	drm_dp_cec_unset_edid(&intel_dp->aux);
	kfree(connector->detect_edid);
	connector->detect_edid = NULL;

	intel_dp->has_hdmi_sink = false;
	intel_dp->has_audio = false;
	intel_dp->edid_quirks = 0;

	intel_dp->dfp.max_bpc = 0;
	intel_dp->dfp.max_dotclock = 0;
	intel_dp->dfp.min_tmds_clock = 0;
	intel_dp->dfp.max_tmds_clock = 0;

	intel_dp->dfp.ycbcr_444_to_420 = false;
	connector->base.ycbcr_420_allowed = false;
}

static int
intel_dp_detect(struct drm_connector *connector,
		struct drm_modeset_acquire_ctx *ctx,
		bool force)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_dp *intel_dp = intel_attached_dp(to_intel_connector(connector));
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	enum drm_connector_status status;

	drm_dbg_kms(&dev_priv->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.id, connector->name);
	drm_WARN_ON(&dev_priv->drm,
		    !drm_modeset_is_locked(&dev_priv->drm.mode_config.connection_mutex));

	if (!INTEL_DISPLAY_ENABLED(dev_priv))
		return connector_status_disconnected;

	/* Can't disconnect eDP */
	if (intel_dp_is_edp(intel_dp))
		status = edp_detect(intel_dp);
	else if (intel_digital_port_connected(encoder))
		status = intel_dp_detect_dpcd(intel_dp);
	else
		status = connector_status_disconnected;

	if (status == connector_status_disconnected) {
		memset(&intel_dp->compliance, 0, sizeof(intel_dp->compliance));
		memset(intel_dp->dsc_dpcd, 0, sizeof(intel_dp->dsc_dpcd));

		if (intel_dp->is_mst) {
			drm_dbg_kms(&dev_priv->drm,
				    "MST device may have disappeared %d vs %d\n",
				    intel_dp->is_mst,
				    intel_dp->mst_mgr.mst_state);
			intel_dp->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
							intel_dp->is_mst);
		}

		goto out;
	}

	/* Read DP Sink DSC Cap DPCD regs for DP v1.4 */
	if (INTEL_GEN(dev_priv) >= 11)
		intel_dp_get_dsc_sink_cap(intel_dp);

	intel_dp_configure_mst(intel_dp);

	/*
	 * TODO: Reset link params when switching to MST mode, until MST
	 * supports link training fallback params.
	 */
	if (intel_dp->reset_link_params || intel_dp->is_mst) {
		/* Initial max link lane count */
		intel_dp->max_link_lane_count = intel_dp_max_common_lane_count(intel_dp);

		/* Initial max link rate */
		intel_dp->max_link_rate = intel_dp_max_common_rate(intel_dp);

		intel_dp->reset_link_params = false;
	}

	intel_dp_print_rates(intel_dp);

	if (intel_dp->is_mst) {
		/*
		 * If we are in MST mode then this connector
		 * won't appear connected or have anything
		 * with EDID on it
		 */
		status = connector_status_disconnected;
		goto out;
	}

	/*
	 * Some external monitors do not signal loss of link synchronization
	 * with an IRQ_HPD, so force a link status check.
	 */
	if (!intel_dp_is_edp(intel_dp)) {
		int ret;

		ret = intel_dp_retrain_link(encoder, ctx);
		if (ret)
			return ret;
	}

	/*
	 * Clearing NACK and defer counts to get their exact values
	 * while reading EDID which are required by Compliance tests
	 * 4.2.2.4 and 4.2.2.5
	 */
	intel_dp->aux.i2c_nack_count = 0;
	intel_dp->aux.i2c_defer_count = 0;

	intel_dp_set_edid(intel_dp);
	if (intel_dp_is_edp(intel_dp) ||
	    to_intel_connector(connector)->detect_edid)
		status = connector_status_connected;

	intel_dp_check_service_irq(intel_dp);

out:
	if (status != connector_status_connected && !intel_dp->is_mst)
		intel_dp_unset_edid(intel_dp);

	/*
	 * Make sure the refs for power wells enabled during detect are
	 * dropped to avoid a new detect cycle triggered by HPD polling.
	 */
	intel_display_power_flush_work(dev_priv);

	if (!intel_dp_is_edp(intel_dp))
		drm_dp_set_subconnector_property(connector,
						 status,
						 intel_dp->dpcd,
						 intel_dp->downstream_ports);
	return status;
}

static void
intel_dp_force(struct drm_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(to_intel_connector(connector));
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &dig_port->base;
	struct drm_i915_private *dev_priv = to_i915(intel_encoder->base.dev);
	enum intel_display_power_domain aux_domain =
		intel_aux_power_domain(dig_port);
	intel_wakeref_t wakeref;

	drm_dbg_kms(&dev_priv->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.id, connector->name);
	intel_dp_unset_edid(intel_dp);

	if (connector->status != connector_status_connected)
		return;

	wakeref = intel_display_power_get(dev_priv, aux_domain);

	intel_dp_set_edid(intel_dp);

	intel_display_power_put(dev_priv, aux_domain, wakeref);
}

static int intel_dp_get_modes(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct edid *edid;

	edid = intel_connector->detect_edid;
	if (edid) {
		int ret = intel_connector_update_modes(connector, edid);
		if (ret)
			return ret;
	}

	/* if eDP has no EDID, fall back to fixed mode */
	if (intel_dp_is_edp(intel_attached_dp(intel_connector)) &&
	    intel_connector->panel.fixed_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev,
					  intel_connector->panel.fixed_mode);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			return 1;
		}
	}

	if (!edid) {
		struct intel_dp *intel_dp = intel_attached_dp(intel_connector);
		struct drm_display_mode *mode;

		mode = drm_dp_downstream_mode(connector->dev,
					      intel_dp->dpcd,
					      intel_dp->downstream_ports);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			return 1;
		}
	}

	return 0;
}

static int
intel_dp_connector_register(struct drm_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_dp *intel_dp = intel_attached_dp(to_intel_connector(connector));
	int ret;

	ret = intel_connector_register(connector);
	if (ret)
		return ret;

	drm_dbg_kms(&i915->drm, "registering %s bus for %s\n",
		    intel_dp->aux.name, connector->kdev->kobj.name);

	intel_dp->aux.dev = connector->kdev;
	ret = drm_dp_aux_register(&intel_dp->aux);
	if (!ret)
		drm_dp_cec_register_connector(&intel_dp->aux, connector);
	return ret;
}

static void
intel_dp_connector_unregister(struct drm_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(to_intel_connector(connector));

	drm_dp_cec_unregister_connector(&intel_dp->aux);
	drm_dp_aux_unregister(&intel_dp->aux);
	intel_connector_unregister(connector);
}

void intel_dp_encoder_flush_work(struct drm_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(to_intel_encoder(encoder));
	struct intel_dp *intel_dp = &dig_port->dp;

	intel_dp_mst_encoder_cleanup(dig_port);
	if (intel_dp_is_edp(intel_dp)) {
		intel_wakeref_t wakeref;

		cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
		/*
		 * vdd might still be enabled do to the delayed vdd off.
		 * Make sure vdd is actually turned off here.
		 */
		with_pps_lock(intel_dp, wakeref)
			edp_panel_vdd_off_sync(intel_dp);

		if (intel_dp->edp_notifier.notifier_call) {
			unregister_reboot_notifier(&intel_dp->edp_notifier);
			intel_dp->edp_notifier.notifier_call = NULL;
		}
	}

	intel_dp_aux_fini(intel_dp);
}

static void intel_dp_encoder_destroy(struct drm_encoder *encoder)
{
	intel_dp_encoder_flush_work(encoder);

	drm_encoder_cleanup(encoder);
	kfree(enc_to_dig_port(to_intel_encoder(encoder)));
}

void intel_dp_encoder_suspend(struct intel_encoder *intel_encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(intel_encoder);
	intel_wakeref_t wakeref;

	if (!intel_dp_is_edp(intel_dp))
		return;

	/*
	 * vdd might still be enabled do to the delayed vdd off.
	 * Make sure vdd is actually turned off here.
	 */
	cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
	with_pps_lock(intel_dp, wakeref)
		edp_panel_vdd_off_sync(intel_dp);
}

static void intel_edp_panel_vdd_sanitize(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	/*
	 * The VDD bit needs a power domain reference, so if the bit is
	 * already enabled when we boot or resume, grab this reference and
	 * schedule a vdd off, so we don't hold on to the reference
	 * indefinitely.
	 */
	drm_dbg_kms(&dev_priv->drm,
		    "VDD left on by BIOS, adjusting state tracking\n");
	intel_display_power_get(dev_priv, intel_aux_power_domain(dig_port));

	edp_panel_vdd_schedule_off(intel_dp);
}

static enum pipe vlv_active_pipe(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	enum pipe pipe;

	if (intel_dp_port_enabled(dev_priv, intel_dp->output_reg,
				  encoder->port, &pipe))
		return pipe;

	return INVALID_PIPE;
}

void intel_dp_encoder_reset(struct drm_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(encoder));
	struct intel_lspcon *lspcon = dp_to_lspcon(intel_dp);
	intel_wakeref_t wakeref;

	if (!HAS_DDI(dev_priv))
		intel_dp->DP = intel_de_read(dev_priv, intel_dp->output_reg);

	if (lspcon->active)
		lspcon_resume(lspcon);

	intel_dp->reset_link_params = true;

	if (!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv) &&
	    !intel_dp_is_edp(intel_dp))
		return;

	with_pps_lock(intel_dp, wakeref) {
		if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			intel_dp->active_pipe = vlv_active_pipe(intel_dp);

		if (intel_dp_is_edp(intel_dp)) {
			/*
			 * Reinit the power sequencer, in case BIOS did
			 * something nasty with it.
			 */
			intel_dp_pps_init(intel_dp);
			intel_edp_panel_vdd_sanitize(intel_dp);
		}
	}
}

static int intel_modeset_tile_group(struct intel_atomic_state *state,
				    int tile_group_id)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	int ret = 0;

	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *conn_state;
		struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (!connector->has_tile ||
		    connector->tile_group->id != tile_group_id)
			continue;

		conn_state = drm_atomic_get_connector_state(&state->base,
							    connector);
		if (IS_ERR(conn_state)) {
			ret = PTR_ERR(conn_state);
			break;
		}

		crtc = to_intel_crtc(conn_state->crtc);

		if (!crtc)
			continue;

		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
		crtc_state->uapi.mode_changed = true;

		ret = drm_atomic_add_affected_planes(&state->base, &crtc->base);
		if (ret)
			break;
	}
	drm_connector_list_iter_end(&conn_iter);

	return ret;
}

static int intel_modeset_affected_transcoders(struct intel_atomic_state *state, u8 transcoders)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct intel_crtc *crtc;

	if (transcoders == 0)
		return 0;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state;
		int ret;

		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		if (!crtc_state->hw.enable)
			continue;

		if (!(transcoders & BIT(crtc_state->cpu_transcoder)))
			continue;

		crtc_state->uapi.mode_changed = true;

		ret = drm_atomic_add_affected_connectors(&state->base, &crtc->base);
		if (ret)
			return ret;

		ret = drm_atomic_add_affected_planes(&state->base, &crtc->base);
		if (ret)
			return ret;

		transcoders &= ~BIT(crtc_state->cpu_transcoder);
	}

	drm_WARN_ON(&dev_priv->drm, transcoders != 0);

	return 0;
}

static int intel_modeset_synced_crtcs(struct intel_atomic_state *state,
				      struct drm_connector *connector)
{
	const struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(&state->base, connector);
	const struct intel_crtc_state *old_crtc_state;
	struct intel_crtc *crtc;
	u8 transcoders;

	crtc = to_intel_crtc(old_conn_state->crtc);
	if (!crtc)
		return 0;

	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);

	if (!old_crtc_state->hw.active)
		return 0;

	transcoders = old_crtc_state->sync_mode_slaves_mask;
	if (old_crtc_state->master_transcoder != INVALID_TRANSCODER)
		transcoders |= BIT(old_crtc_state->master_transcoder);

	return intel_modeset_affected_transcoders(state,
						  transcoders);
}

static int intel_dp_connector_atomic_check(struct drm_connector *conn,
					   struct drm_atomic_state *_state)
{
	struct drm_i915_private *dev_priv = to_i915(conn->dev);
	struct intel_atomic_state *state = to_intel_atomic_state(_state);
	int ret;

	ret = intel_digital_connector_atomic_check(conn, &state->base);
	if (ret)
		return ret;

	/*
	 * We don't enable port sync on BDW due to missing w/as and
	 * due to not having adjusted the modeset sequence appropriately.
	 */
	if (INTEL_GEN(dev_priv) < 9)
		return 0;

	if (!intel_connector_needs_modeset(state, conn))
		return 0;

	if (conn->has_tile) {
		ret = intel_modeset_tile_group(state, conn->tile_group->id);
		if (ret)
			return ret;
	}

	return intel_modeset_synced_crtcs(state, conn);
}

static const struct drm_connector_funcs intel_dp_connector_funcs = {
	.force = intel_dp_force,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.late_register = intel_dp_connector_register,
	.early_unregister = intel_dp_connector_unregister,
	.destroy = intel_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs intel_dp_connector_helper_funcs = {
	.detect_ctx = intel_dp_detect,
	.get_modes = intel_dp_get_modes,
	.mode_valid = intel_dp_mode_valid,
	.atomic_check = intel_dp_connector_atomic_check,
};

static const struct drm_encoder_funcs intel_dp_enc_funcs = {
	.reset = intel_dp_encoder_reset,
	.destroy = intel_dp_encoder_destroy,
};

static bool intel_edp_have_power(struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool have_power = false;

	with_pps_lock(intel_dp, wakeref) {
		have_power = edp_have_panel_power(intel_dp) &&
						  edp_have_panel_vdd(intel_dp);
	}

	return have_power;
}

enum irqreturn
intel_dp_hpd_pulse(struct intel_digital_port *dig_port, bool long_hpd)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_dp *intel_dp = &dig_port->dp;

	if (dig_port->base.type == INTEL_OUTPUT_EDP &&
	    (long_hpd || !intel_edp_have_power(intel_dp))) {
		/*
		 * vdd off can generate a long/short pulse on eDP which
		 * would require vdd on to handle it, and thus we
		 * would end up in an endless cycle of
		 * "vdd off -> long/short hpd -> vdd on -> detect -> vdd off -> ..."
		 */
		drm_dbg_kms(&i915->drm,
			    "ignoring %s hpd on eDP [ENCODER:%d:%s]\n",
			    long_hpd ? "long" : "short",
			    dig_port->base.base.base.id,
			    dig_port->base.base.name);
		return IRQ_HANDLED;
	}

	drm_dbg_kms(&i915->drm, "got hpd irq on [ENCODER:%d:%s] - %s\n",
		    dig_port->base.base.base.id,
		    dig_port->base.base.name,
		    long_hpd ? "long" : "short");

	if (long_hpd) {
		intel_dp->reset_link_params = true;
		return IRQ_NONE;
	}

	if (intel_dp->is_mst) {
		if (!intel_dp_check_mst_status(intel_dp))
			return IRQ_NONE;
	} else if (!intel_dp_short_pulse(intel_dp)) {
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

/* check the VBT to see whether the eDP is on another port */
bool intel_dp_is_port_edp(struct drm_i915_private *dev_priv, enum port port)
{
	/*
	 * eDP not supported on g4x. so bail out early just
	 * for a bit extra safety in case the VBT is bonkers.
	 */
	if (INTEL_GEN(dev_priv) < 5)
		return false;

	if (INTEL_GEN(dev_priv) < 9 && port == PORT_A)
		return true;

	return intel_bios_is_port_edp(dev_priv, port);
}

static void
intel_dp_add_properties(struct intel_dp *intel_dp, struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	enum port port = dp_to_dig_port(intel_dp)->base.port;

	if (!intel_dp_is_edp(intel_dp))
		drm_connector_attach_dp_subconnector_property(connector);

	if (!IS_G4X(dev_priv) && port != PORT_A)
		intel_attach_force_audio_property(connector);

	intel_attach_broadcast_rgb_property(connector);
	if (HAS_GMCH(dev_priv))
		drm_connector_attach_max_bpc_property(connector, 6, 10);
	else if (INTEL_GEN(dev_priv) >= 5)
		drm_connector_attach_max_bpc_property(connector, 6, 12);

	intel_attach_colorspace_property(connector);

	if (IS_GEMINILAKE(dev_priv) || INTEL_GEN(dev_priv) >= 11)
		drm_object_attach_property(&connector->base,
					   connector->dev->mode_config.hdr_output_metadata_property,
					   0);

	if (intel_dp_is_edp(intel_dp)) {
		u32 allowed_scalers;

		allowed_scalers = BIT(DRM_MODE_SCALE_ASPECT) | BIT(DRM_MODE_SCALE_FULLSCREEN);
		if (!HAS_GMCH(dev_priv))
			allowed_scalers |= BIT(DRM_MODE_SCALE_CENTER);

		drm_connector_attach_scaling_mode_property(connector, allowed_scalers);

		connector->state->scaling_mode = DRM_MODE_SCALE_ASPECT;

	}
}

static void intel_dp_init_panel_power_timestamps(struct intel_dp *intel_dp)
{
	intel_dp->panel_power_off_time = ktime_get_boottime();
	intel_dp->last_power_on = jiffies;
	intel_dp->last_backlight_off = jiffies;
}

static void
intel_pps_readout_hw_state(struct intel_dp *intel_dp, struct edp_power_seq *seq)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 pp_on, pp_off, pp_ctl;
	struct pps_registers regs;

	intel_pps_get_registers(intel_dp, &regs);

	pp_ctl = ilk_get_pp_control(intel_dp);

	/* Ensure PPS is unlocked */
	if (!HAS_DDI(dev_priv))
		intel_de_write(dev_priv, regs.pp_ctrl, pp_ctl);

	pp_on = intel_de_read(dev_priv, regs.pp_on);
	pp_off = intel_de_read(dev_priv, regs.pp_off);

	/* Pull timing values out of registers */
	seq->t1_t3 = REG_FIELD_GET(PANEL_POWER_UP_DELAY_MASK, pp_on);
	seq->t8 = REG_FIELD_GET(PANEL_LIGHT_ON_DELAY_MASK, pp_on);
	seq->t9 = REG_FIELD_GET(PANEL_LIGHT_OFF_DELAY_MASK, pp_off);
	seq->t10 = REG_FIELD_GET(PANEL_POWER_DOWN_DELAY_MASK, pp_off);

	if (i915_mmio_reg_valid(regs.pp_div)) {
		u32 pp_div;

		pp_div = intel_de_read(dev_priv, regs.pp_div);

		seq->t11_t12 = REG_FIELD_GET(PANEL_POWER_CYCLE_DELAY_MASK, pp_div) * 1000;
	} else {
		seq->t11_t12 = REG_FIELD_GET(BXT_POWER_CYCLE_DELAY_MASK, pp_ctl) * 1000;
	}
}

static void
intel_pps_dump_state(const char *state_name, const struct edp_power_seq *seq)
{
	DRM_DEBUG_KMS("%s t1_t3 %d t8 %d t9 %d t10 %d t11_t12 %d\n",
		      state_name,
		      seq->t1_t3, seq->t8, seq->t9, seq->t10, seq->t11_t12);
}

static void
intel_pps_verify_state(struct intel_dp *intel_dp)
{
	struct edp_power_seq hw;
	struct edp_power_seq *sw = &intel_dp->pps_delays;

	intel_pps_readout_hw_state(intel_dp, &hw);

	if (hw.t1_t3 != sw->t1_t3 || hw.t8 != sw->t8 || hw.t9 != sw->t9 ||
	    hw.t10 != sw->t10 || hw.t11_t12 != sw->t11_t12) {
		DRM_ERROR("PPS state mismatch\n");
		intel_pps_dump_state("sw", sw);
		intel_pps_dump_state("hw", &hw);
	}
}

static void
intel_dp_init_panel_power_sequencer(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct edp_power_seq cur, vbt, spec,
		*final = &intel_dp->pps_delays;

	lockdep_assert_held(&dev_priv->pps_mutex);

	/* already initialized? */
	if (final->t11_t12 != 0)
		return;

	intel_pps_readout_hw_state(intel_dp, &cur);

	intel_pps_dump_state("cur", &cur);

	vbt = dev_priv->vbt.edp.pps;
	/* On Toshiba Satellite P50-C-18C system the VBT T12 delay
	 * of 500ms appears to be too short. Ocassionally the panel
	 * just fails to power back on. Increasing the delay to 800ms
	 * seems sufficient to avoid this problem.
	 */
	if (dev_priv->quirks & QUIRK_INCREASE_T12_DELAY) {
		vbt.t11_t12 = max_t(u16, vbt.t11_t12, 1300 * 10);
		drm_dbg_kms(&dev_priv->drm,
			    "Increasing T12 panel delay as per the quirk to %d\n",
			    vbt.t11_t12);
	}
	/* T11_T12 delay is special and actually in units of 100ms, but zero
	 * based in the hw (so we need to add 100 ms). But the sw vbt
	 * table multiplies it with 1000 to make it in units of 100usec,
	 * too. */
	vbt.t11_t12 += 100 * 10;

	/* Upper limits from eDP 1.3 spec. Note that we use the clunky units of
	 * our hw here, which are all in 100usec. */
	spec.t1_t3 = 210 * 10;
	spec.t8 = 50 * 10; /* no limit for t8, use t7 instead */
	spec.t9 = 50 * 10; /* no limit for t9, make it symmetric with t8 */
	spec.t10 = 500 * 10;
	/* This one is special and actually in units of 100ms, but zero
	 * based in the hw (so we need to add 100 ms). But the sw vbt
	 * table multiplies it with 1000 to make it in units of 100usec,
	 * too. */
	spec.t11_t12 = (510 + 100) * 10;

	intel_pps_dump_state("vbt", &vbt);

	/* Use the max of the register settings and vbt. If both are
	 * unset, fall back to the spec limits. */
#define assign_final(field)	final->field = (max(cur.field, vbt.field) == 0 ? \
				       spec.field : \
				       max(cur.field, vbt.field))
	assign_final(t1_t3);
	assign_final(t8);
	assign_final(t9);
	assign_final(t10);
	assign_final(t11_t12);
#undef assign_final

#define get_delay(field)	(DIV_ROUND_UP(final->field, 10))
	intel_dp->panel_power_up_delay = get_delay(t1_t3);
	intel_dp->backlight_on_delay = get_delay(t8);
	intel_dp->backlight_off_delay = get_delay(t9);
	intel_dp->panel_power_down_delay = get_delay(t10);
	intel_dp->panel_power_cycle_delay = get_delay(t11_t12);
#undef get_delay

	drm_dbg_kms(&dev_priv->drm,
		    "panel power up delay %d, power down delay %d, power cycle delay %d\n",
		    intel_dp->panel_power_up_delay,
		    intel_dp->panel_power_down_delay,
		    intel_dp->panel_power_cycle_delay);

	drm_dbg_kms(&dev_priv->drm, "backlight on delay %d, off delay %d\n",
		    intel_dp->backlight_on_delay,
		    intel_dp->backlight_off_delay);

	/*
	 * We override the HW backlight delays to 1 because we do manual waits
	 * on them. For T8, even BSpec recommends doing it. For T9, if we
	 * don't do this, we'll end up waiting for the backlight off delay
	 * twice: once when we do the manual sleep, and once when we disable
	 * the panel and wait for the PP_STATUS bit to become zero.
	 */
	final->t8 = 1;
	final->t9 = 1;

	/*
	 * HW has only a 100msec granularity for t11_t12 so round it up
	 * accordingly.
	 */
	final->t11_t12 = roundup(final->t11_t12, 100 * 10);
}

static void
intel_dp_init_panel_power_sequencer_registers(struct intel_dp *intel_dp,
					      bool force_disable_vdd)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 pp_on, pp_off, port_sel = 0;
	int div = RUNTIME_INFO(dev_priv)->rawclk_freq / 1000;
	struct pps_registers regs;
	enum port port = dp_to_dig_port(intel_dp)->base.port;
	const struct edp_power_seq *seq = &intel_dp->pps_delays;

	lockdep_assert_held(&dev_priv->pps_mutex);

	intel_pps_get_registers(intel_dp, &regs);

	/*
	 * On some VLV machines the BIOS can leave the VDD
	 * enabled even on power sequencers which aren't
	 * hooked up to any port. This would mess up the
	 * power domain tracking the first time we pick
	 * one of these power sequencers for use since
	 * edp_panel_vdd_on() would notice that the VDD was
	 * already on and therefore wouldn't grab the power
	 * domain reference. Disable VDD first to avoid this.
	 * This also avoids spuriously turning the VDD on as
	 * soon as the new power sequencer gets initialized.
	 */
	if (force_disable_vdd) {
		u32 pp = ilk_get_pp_control(intel_dp);

		drm_WARN(&dev_priv->drm, pp & PANEL_POWER_ON,
			 "Panel power already on\n");

		if (pp & EDP_FORCE_VDD)
			drm_dbg_kms(&dev_priv->drm,
				    "VDD already on, disabling first\n");

		pp &= ~EDP_FORCE_VDD;

		intel_de_write(dev_priv, regs.pp_ctrl, pp);
	}

	pp_on = REG_FIELD_PREP(PANEL_POWER_UP_DELAY_MASK, seq->t1_t3) |
		REG_FIELD_PREP(PANEL_LIGHT_ON_DELAY_MASK, seq->t8);
	pp_off = REG_FIELD_PREP(PANEL_LIGHT_OFF_DELAY_MASK, seq->t9) |
		REG_FIELD_PREP(PANEL_POWER_DOWN_DELAY_MASK, seq->t10);

	/* Haswell doesn't have any port selection bits for the panel
	 * power sequencer any more. */
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		port_sel = PANEL_PORT_SELECT_VLV(port);
	} else if (HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv)) {
		switch (port) {
		case PORT_A:
			port_sel = PANEL_PORT_SELECT_DPA;
			break;
		case PORT_C:
			port_sel = PANEL_PORT_SELECT_DPC;
			break;
		case PORT_D:
			port_sel = PANEL_PORT_SELECT_DPD;
			break;
		default:
			MISSING_CASE(port);
			break;
		}
	}

	pp_on |= port_sel;

	intel_de_write(dev_priv, regs.pp_on, pp_on);
	intel_de_write(dev_priv, regs.pp_off, pp_off);

	/*
	 * Compute the divisor for the pp clock, simply match the Bspec formula.
	 */
	if (i915_mmio_reg_valid(regs.pp_div)) {
		intel_de_write(dev_priv, regs.pp_div,
			       REG_FIELD_PREP(PP_REFERENCE_DIVIDER_MASK, (100 * div) / 2 - 1) | REG_FIELD_PREP(PANEL_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(seq->t11_t12, 1000)));
	} else {
		u32 pp_ctl;

		pp_ctl = intel_de_read(dev_priv, regs.pp_ctrl);
		pp_ctl &= ~BXT_POWER_CYCLE_DELAY_MASK;
		pp_ctl |= REG_FIELD_PREP(BXT_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(seq->t11_t12, 1000));
		intel_de_write(dev_priv, regs.pp_ctrl, pp_ctl);
	}

	drm_dbg_kms(&dev_priv->drm,
		    "panel power sequencer register settings: PP_ON %#x, PP_OFF %#x, PP_DIV %#x\n",
		    intel_de_read(dev_priv, regs.pp_on),
		    intel_de_read(dev_priv, regs.pp_off),
		    i915_mmio_reg_valid(regs.pp_div) ?
		    intel_de_read(dev_priv, regs.pp_div) :
		    (intel_de_read(dev_priv, regs.pp_ctrl) & BXT_POWER_CYCLE_DELAY_MASK));
}

static void intel_dp_pps_init(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		vlv_initial_power_sequencer_setup(intel_dp);
	} else {
		intel_dp_init_panel_power_sequencer(intel_dp);
		intel_dp_init_panel_power_sequencer_registers(intel_dp, false);
	}
}

/**
 * intel_dp_set_drrs_state - program registers for RR switch to take effect
 * @dev_priv: i915 device
 * @crtc_state: a pointer to the active intel_crtc_state
 * @refresh_rate: RR to be programmed
 *
 * This function gets called when refresh rate (RR) has to be changed from
 * one frequency to another. Switches can be between high and low RR
 * supported by the panel or to any other RR based on media playback (in
 * this case, RR value needs to be passed from user space).
 *
 * The caller of this function needs to take a lock on dev_priv->drrs.
 */
static void intel_dp_set_drrs_state(struct drm_i915_private *dev_priv,
				    const struct intel_crtc_state *crtc_state,
				    int refresh_rate)
{
	struct intel_dp *intel_dp = dev_priv->drrs.dp;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum drrs_refresh_rate_type index = DRRS_HIGH_RR;

	if (refresh_rate <= 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "Refresh rate should be positive non-zero.\n");
		return;
	}

	if (intel_dp == NULL) {
		drm_dbg_kms(&dev_priv->drm, "DRRS not supported.\n");
		return;
	}

	if (!intel_crtc) {
		drm_dbg_kms(&dev_priv->drm,
			    "DRRS: intel_crtc not initialized\n");
		return;
	}

	if (dev_priv->drrs.type < SEAMLESS_DRRS_SUPPORT) {
		drm_dbg_kms(&dev_priv->drm, "Only Seamless DRRS supported.\n");
		return;
	}

	if (drm_mode_vrefresh(intel_dp->attached_connector->panel.downclock_mode) ==
			refresh_rate)
		index = DRRS_LOW_RR;

	if (index == dev_priv->drrs.refresh_rate_type) {
		drm_dbg_kms(&dev_priv->drm,
			    "DRRS requested for previously set RR...ignoring\n");
		return;
	}

	if (!crtc_state->hw.active) {
		drm_dbg_kms(&dev_priv->drm,
			    "eDP encoder disabled. CRTC not Active\n");
		return;
	}

	if (INTEL_GEN(dev_priv) >= 8 && !IS_CHERRYVIEW(dev_priv)) {
		switch (index) {
		case DRRS_HIGH_RR:
			intel_dp_set_m_n(crtc_state, M1_N1);
			break;
		case DRRS_LOW_RR:
			intel_dp_set_m_n(crtc_state, M2_N2);
			break;
		case DRRS_MAX_RR:
		default:
			drm_err(&dev_priv->drm,
				"Unsupported refreshrate type\n");
		}
	} else if (INTEL_GEN(dev_priv) > 6) {
		i915_reg_t reg = PIPECONF(crtc_state->cpu_transcoder);
		u32 val;

		val = intel_de_read(dev_priv, reg);
		if (index > DRRS_HIGH_RR) {
			if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
				val |= PIPECONF_EDP_RR_MODE_SWITCH_VLV;
			else
				val |= PIPECONF_EDP_RR_MODE_SWITCH;
		} else {
			if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
				val &= ~PIPECONF_EDP_RR_MODE_SWITCH_VLV;
			else
				val &= ~PIPECONF_EDP_RR_MODE_SWITCH;
		}
		intel_de_write(dev_priv, reg, val);
	}

	dev_priv->drrs.refresh_rate_type = index;

	drm_dbg_kms(&dev_priv->drm, "eDP Refresh Rate set to : %dHz\n",
		    refresh_rate);
}

static void
intel_edp_drrs_enable_locked(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	dev_priv->drrs.busy_frontbuffer_bits = 0;
	dev_priv->drrs.dp = intel_dp;
}

/**
 * intel_edp_drrs_enable - init drrs struct if supported
 * @intel_dp: DP struct
 * @crtc_state: A pointer to the active crtc state.
 *
 * Initializes frontbuffer_bits and drrs.dp
 */
void intel_edp_drrs_enable(struct intel_dp *intel_dp,
			   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!crtc_state->has_drrs)
		return;

	drm_dbg_kms(&dev_priv->drm, "Enabling DRRS\n");

	mutex_lock(&dev_priv->drrs.mutex);

	if (dev_priv->drrs.dp) {
		drm_warn(&dev_priv->drm, "DRRS already enabled\n");
		goto unlock;
	}

	intel_edp_drrs_enable_locked(intel_dp);

unlock:
	mutex_unlock(&dev_priv->drrs.mutex);
}

static void
intel_edp_drrs_disable_locked(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (dev_priv->drrs.refresh_rate_type == DRRS_LOW_RR) {
		int refresh;

		refresh = drm_mode_vrefresh(intel_dp->attached_connector->panel.fixed_mode);
		intel_dp_set_drrs_state(dev_priv, crtc_state, refresh);
	}

	dev_priv->drrs.dp = NULL;
}

/**
 * intel_edp_drrs_disable - Disable DRRS
 * @intel_dp: DP struct
 * @old_crtc_state: Pointer to old crtc_state.
 *
 */
void intel_edp_drrs_disable(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!old_crtc_state->has_drrs)
		return;

	mutex_lock(&dev_priv->drrs.mutex);
	if (!dev_priv->drrs.dp) {
		mutex_unlock(&dev_priv->drrs.mutex);
		return;
	}

	intel_edp_drrs_disable_locked(intel_dp, old_crtc_state);
	mutex_unlock(&dev_priv->drrs.mutex);

	cancel_delayed_work_sync(&dev_priv->drrs.work);
}

/**
 * intel_edp_drrs_update - Update DRRS state
 * @intel_dp: Intel DP
 * @crtc_state: new CRTC state
 *
 * This function will update DRRS states, disabling or enabling DRRS when
 * executing fastsets. For full modeset, intel_edp_drrs_disable() and
 * intel_edp_drrs_enable() should be called instead.
 */
void
intel_edp_drrs_update(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (dev_priv->drrs.type != SEAMLESS_DRRS_SUPPORT)
		return;

	mutex_lock(&dev_priv->drrs.mutex);

	/* New state matches current one? */
	if (crtc_state->has_drrs == !!dev_priv->drrs.dp)
		goto unlock;

	if (crtc_state->has_drrs)
		intel_edp_drrs_enable_locked(intel_dp);
	else
		intel_edp_drrs_disable_locked(intel_dp, crtc_state);

unlock:
	mutex_unlock(&dev_priv->drrs.mutex);
}

static void intel_edp_drrs_downclock_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), drrs.work.work);
	struct intel_dp *intel_dp;

	mutex_lock(&dev_priv->drrs.mutex);

	intel_dp = dev_priv->drrs.dp;

	if (!intel_dp)
		goto unlock;

	/*
	 * The delayed work can race with an invalidate hence we need to
	 * recheck.
	 */

	if (dev_priv->drrs.busy_frontbuffer_bits)
		goto unlock;

	if (dev_priv->drrs.refresh_rate_type != DRRS_LOW_RR) {
		struct drm_crtc *crtc = dp_to_dig_port(intel_dp)->base.base.crtc;

		intel_dp_set_drrs_state(dev_priv, to_intel_crtc(crtc)->config,
			drm_mode_vrefresh(intel_dp->attached_connector->panel.downclock_mode));
	}

unlock:
	mutex_unlock(&dev_priv->drrs.mutex);
}

/**
 * intel_edp_drrs_invalidate - Disable Idleness DRRS
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called everytime rendering on the given planes start.
 * Hence DRRS needs to be Upclocked, i.e. (LOW_RR -> HIGH_RR).
 *
 * Dirty frontbuffers relevant to DRRS are tracked in busy_frontbuffer_bits.
 */
void intel_edp_drrs_invalidate(struct drm_i915_private *dev_priv,
			       unsigned int frontbuffer_bits)
{
	struct intel_dp *intel_dp;
	struct drm_crtc *crtc;
	enum pipe pipe;

	if (dev_priv->drrs.type == DRRS_NOT_SUPPORTED)
		return;

	cancel_delayed_work(&dev_priv->drrs.work);

	mutex_lock(&dev_priv->drrs.mutex);

	intel_dp = dev_priv->drrs.dp;
	if (!intel_dp) {
		mutex_unlock(&dev_priv->drrs.mutex);
		return;
	}

	crtc = dp_to_dig_port(intel_dp)->base.base.crtc;
	pipe = to_intel_crtc(crtc)->pipe;

	frontbuffer_bits &= INTEL_FRONTBUFFER_ALL_MASK(pipe);
	dev_priv->drrs.busy_frontbuffer_bits |= frontbuffer_bits;

	/* invalidate means busy screen hence upclock */
	if (frontbuffer_bits && dev_priv->drrs.refresh_rate_type == DRRS_LOW_RR)
		intel_dp_set_drrs_state(dev_priv, to_intel_crtc(crtc)->config,
					drm_mode_vrefresh(intel_dp->attached_connector->panel.fixed_mode));

	mutex_unlock(&dev_priv->drrs.mutex);
}

/**
 * intel_edp_drrs_flush - Restart Idleness DRRS
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called every time rendering on the given planes has
 * completed or flip on a crtc is completed. So DRRS should be upclocked
 * (LOW_RR -> HIGH_RR). And also Idleness detection should be started again,
 * if no other planes are dirty.
 *
 * Dirty frontbuffers relevant to DRRS are tracked in busy_frontbuffer_bits.
 */
void intel_edp_drrs_flush(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits)
{
	struct intel_dp *intel_dp;
	struct drm_crtc *crtc;
	enum pipe pipe;

	if (dev_priv->drrs.type == DRRS_NOT_SUPPORTED)
		return;

	cancel_delayed_work(&dev_priv->drrs.work);

	mutex_lock(&dev_priv->drrs.mutex);

	intel_dp = dev_priv->drrs.dp;
	if (!intel_dp) {
		mutex_unlock(&dev_priv->drrs.mutex);
		return;
	}

	crtc = dp_to_dig_port(intel_dp)->base.base.crtc;
	pipe = to_intel_crtc(crtc)->pipe;

	frontbuffer_bits &= INTEL_FRONTBUFFER_ALL_MASK(pipe);
	dev_priv->drrs.busy_frontbuffer_bits &= ~frontbuffer_bits;

	/* flush means busy screen hence upclock */
	if (frontbuffer_bits && dev_priv->drrs.refresh_rate_type == DRRS_LOW_RR)
		intel_dp_set_drrs_state(dev_priv, to_intel_crtc(crtc)->config,
					drm_mode_vrefresh(intel_dp->attached_connector->panel.fixed_mode));

	/*
	 * flush also means no more activity hence schedule downclock, if all
	 * other fbs are quiescent too
	 */
	if (!dev_priv->drrs.busy_frontbuffer_bits)
		schedule_delayed_work(&dev_priv->drrs.work,
				msecs_to_jiffies(1000));
	mutex_unlock(&dev_priv->drrs.mutex);
}

/**
 * DOC: Display Refresh Rate Switching (DRRS)
 *
 * Display Refresh Rate Switching (DRRS) is a power conservation feature
 * which enables swtching between low and high refresh rates,
 * dynamically, based on the usage scenario. This feature is applicable
 * for internal panels.
 *
 * Indication that the panel supports DRRS is given by the panel EDID, which
 * would list multiple refresh rates for one resolution.
 *
 * DRRS is of 2 types - static and seamless.
 * Static DRRS involves changing refresh rate (RR) by doing a full modeset
 * (may appear as a blink on screen) and is used in dock-undock scenario.
 * Seamless DRRS involves changing RR without any visual effect to the user
 * and can be used during normal system usage. This is done by programming
 * certain registers.
 *
 * Support for static/seamless DRRS may be indicated in the VBT based on
 * inputs from the panel spec.
 *
 * DRRS saves power by switching to low RR based on usage scenarios.
 *
 * The implementation is based on frontbuffer tracking implementation.  When
 * there is a disturbance on the screen triggered by user activity or a periodic
 * system activity, DRRS is disabled (RR is changed to high RR).  When there is
 * no movement on screen, after a timeout of 1 second, a switch to low RR is
 * made.
 *
 * For integration with frontbuffer tracking code, intel_edp_drrs_invalidate()
 * and intel_edp_drrs_flush() are called.
 *
 * DRRS can be further extended to support other internal panels and also
 * the scenario of video playback wherein RR is set based on the rate
 * requested by userspace.
 */

/**
 * intel_dp_drrs_init - Init basic DRRS work and mutex.
 * @connector: eDP connector
 * @fixed_mode: preferred mode of panel
 *
 * This function is  called only once at driver load to initialize basic
 * DRRS stuff.
 *
 * Returns:
 * Downclock mode if panel supports it, else return NULL.
 * DRRS support is determined by the presence of downclock mode (apart
 * from VBT setting).
 */
static struct drm_display_mode *
intel_dp_drrs_init(struct intel_connector *connector,
		   struct drm_display_mode *fixed_mode)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct drm_display_mode *downclock_mode = NULL;

	INIT_DELAYED_WORK(&dev_priv->drrs.work, intel_edp_drrs_downclock_work);
	mutex_init(&dev_priv->drrs.mutex);

	if (INTEL_GEN(dev_priv) <= 6) {
		drm_dbg_kms(&dev_priv->drm,
			    "DRRS supported for Gen7 and above\n");
		return NULL;
	}

	if (dev_priv->vbt.drrs_type != SEAMLESS_DRRS_SUPPORT) {
		drm_dbg_kms(&dev_priv->drm, "VBT doesn't support DRRS\n");
		return NULL;
	}

	downclock_mode = intel_panel_edid_downclock_mode(connector, fixed_mode);
	if (!downclock_mode) {
		drm_dbg_kms(&dev_priv->drm,
			    "Downclock mode is not found. DRRS not supported\n");
		return NULL;
	}

	dev_priv->drrs.type = dev_priv->vbt.drrs_type;

	dev_priv->drrs.refresh_rate_type = DRRS_HIGH_RR;
	drm_dbg_kms(&dev_priv->drm,
		    "seamless DRRS supported for eDP panel.\n");
	return downclock_mode;
}

static bool intel_edp_init_connector(struct intel_dp *intel_dp,
				     struct intel_connector *intel_connector)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector = &intel_connector->base;
	struct drm_display_mode *fixed_mode = NULL;
	struct drm_display_mode *downclock_mode = NULL;
	bool has_dpcd;
	enum pipe pipe = INVALID_PIPE;
	intel_wakeref_t wakeref;
	struct edid *edid;

	if (!intel_dp_is_edp(intel_dp))
		return true;

	INIT_DELAYED_WORK(&intel_dp->panel_vdd_work, edp_panel_vdd_work);

	/*
	 * On IBX/CPT we may get here with LVDS already registered. Since the
	 * driver uses the only internal power sequencer available for both
	 * eDP and LVDS bail out early in this case to prevent interfering
	 * with an already powered-on LVDS power sequencer.
	 */
	if (intel_get_lvds_encoder(dev_priv)) {
		drm_WARN_ON(dev,
			    !(HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv)));
		drm_info(&dev_priv->drm,
			 "LVDS was detected, not registering eDP\n");

		return false;
	}

	with_pps_lock(intel_dp, wakeref) {
		intel_dp_init_panel_power_timestamps(intel_dp);
		intel_dp_pps_init(intel_dp);
		intel_edp_panel_vdd_sanitize(intel_dp);
	}

	/* Cache DPCD and EDID for edp. */
	has_dpcd = intel_edp_init_dpcd(intel_dp);

	if (!has_dpcd) {
		/* if this fails, presume the device is a ghost */
		drm_info(&dev_priv->drm,
			 "failed to retrieve link info, disabling eDP\n");
		goto out_vdd_off;
	}

	mutex_lock(&dev->mode_config.mutex);
	edid = drm_get_edid(connector, &intel_dp->aux.ddc);
	if (edid) {
		if (drm_add_edid_modes(connector, edid)) {
			drm_connector_update_edid_property(connector, edid);
			intel_dp->edid_quirks = drm_dp_get_edid_quirks(edid);
		} else {
			kfree(edid);
			edid = ERR_PTR(-EINVAL);
		}
	} else {
		edid = ERR_PTR(-ENOENT);
	}
	intel_connector->edid = edid;

	fixed_mode = intel_panel_edid_fixed_mode(intel_connector);
	if (fixed_mode)
		downclock_mode = intel_dp_drrs_init(intel_connector, fixed_mode);

	/* fallback to VBT if available for eDP */
	if (!fixed_mode)
		fixed_mode = intel_panel_vbt_fixed_mode(intel_connector);
	mutex_unlock(&dev->mode_config.mutex);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		intel_dp->edp_notifier.notifier_call = edp_notify_handler;
		register_reboot_notifier(&intel_dp->edp_notifier);

		/*
		 * Figure out the current pipe for the initial backlight setup.
		 * If the current pipe isn't valid, try the PPS pipe, and if that
		 * fails just assume pipe A.
		 */
		pipe = vlv_active_pipe(intel_dp);

		if (pipe != PIPE_A && pipe != PIPE_B)
			pipe = intel_dp->pps_pipe;

		if (pipe != PIPE_A && pipe != PIPE_B)
			pipe = PIPE_A;

		drm_dbg_kms(&dev_priv->drm,
			    "using pipe %c for initial backlight setup\n",
			    pipe_name(pipe));
	}

	intel_panel_init(&intel_connector->panel, fixed_mode, downclock_mode);
	intel_connector->panel.backlight.power = intel_edp_backlight_power;
	intel_panel_setup_backlight(connector, pipe);

	if (fixed_mode) {
		drm_connector_set_panel_orientation_with_quirk(connector,
				dev_priv->vbt.orientation,
				fixed_mode->hdisplay, fixed_mode->vdisplay);
	}

	return true;

out_vdd_off:
	cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
	/*
	 * vdd might still be enabled do to the delayed vdd off.
	 * Make sure vdd is actually turned off here.
	 */
	with_pps_lock(intel_dp, wakeref)
		edp_panel_vdd_off_sync(intel_dp);

	return false;
}

static void intel_dp_modeset_retry_work_fn(struct work_struct *work)
{
	struct intel_connector *intel_connector;
	struct drm_connector *connector;

	intel_connector = container_of(work, typeof(*intel_connector),
				       modeset_retry_work);
	connector = &intel_connector->base;
	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n", connector->base.id,
		      connector->name);

	/* Grab the locks before changing connector property*/
	mutex_lock(&connector->dev->mode_config.mutex);
	/* Set connector link status to BAD and send a Uevent to notify
	 * userspace to do a modeset.
	 */
	drm_connector_set_link_status_property(connector,
					       DRM_MODE_LINK_STATUS_BAD);
	mutex_unlock(&connector->dev->mode_config.mutex);
	/* Send Hotplug uevent so userspace can reprobe */
	drm_kms_helper_hotplug_event(connector->dev);
}

bool
intel_dp_init_connector(struct intel_digital_port *dig_port,
			struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct intel_encoder *intel_encoder = &dig_port->base;
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_encoder->port;
	enum phy phy = intel_port_to_phy(dev_priv, port);
	int type;

	/* Initialize the work for modeset in case of link train failure */
	INIT_WORK(&intel_connector->modeset_retry_work,
		  intel_dp_modeset_retry_work_fn);

	if (drm_WARN(dev, dig_port->max_lanes < 1,
		     "Not enough lanes (%d) for DP on [ENCODER:%d:%s]\n",
		     dig_port->max_lanes, intel_encoder->base.base.id,
		     intel_encoder->base.name))
		return false;

	intel_dp_set_source_rates(intel_dp);

	intel_dp->reset_link_params = true;
	intel_dp->pps_pipe = INVALID_PIPE;
	intel_dp->active_pipe = INVALID_PIPE;

	/* Preserve the current hw state. */
	intel_dp->DP = intel_de_read(dev_priv, intel_dp->output_reg);
	intel_dp->attached_connector = intel_connector;

	if (intel_dp_is_port_edp(dev_priv, port)) {
		/*
		 * Currently we don't support eDP on TypeC ports, although in
		 * theory it could work on TypeC legacy ports.
		 */
		drm_WARN_ON(dev, intel_phy_is_tc(dev_priv, phy));
		type = DRM_MODE_CONNECTOR_eDP;
	} else {
		type = DRM_MODE_CONNECTOR_DisplayPort;
	}

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		intel_dp->active_pipe = vlv_active_pipe(intel_dp);

	/*
	 * For eDP we always set the encoder type to INTEL_OUTPUT_EDP, but
	 * for DP the encoder type can be set by the caller to
	 * INTEL_OUTPUT_UNKNOWN for DDI, so don't rewrite it.
	 */
	if (type == DRM_MODE_CONNECTOR_eDP)
		intel_encoder->type = INTEL_OUTPUT_EDP;

	/* eDP only on port B and/or C on vlv/chv */
	if (drm_WARN_ON(dev, (IS_VALLEYVIEW(dev_priv) ||
			      IS_CHERRYVIEW(dev_priv)) &&
			intel_dp_is_edp(intel_dp) &&
			port != PORT_B && port != PORT_C))
		return false;

	drm_dbg_kms(&dev_priv->drm,
		    "Adding %s connector on [ENCODER:%d:%s]\n",
		    type == DRM_MODE_CONNECTOR_eDP ? "eDP" : "DP",
		    intel_encoder->base.base.id, intel_encoder->base.name);

	drm_connector_init(dev, connector, &intel_dp_connector_funcs, type);
	drm_connector_helper_add(connector, &intel_dp_connector_helper_funcs);

	if (!HAS_GMCH(dev_priv))
		connector->interlace_allowed = true;
	connector->doublescan_allowed = 0;

	intel_connector->polled = DRM_CONNECTOR_POLL_HPD;

	intel_dp_aux_init(intel_dp);

	intel_connector_attach_encoder(intel_connector, intel_encoder);

	if (HAS_DDI(dev_priv))
		intel_connector->get_hw_state = intel_ddi_connector_get_hw_state;
	else
		intel_connector->get_hw_state = intel_connector_get_hw_state;

	/* init MST on ports that can support it */
	intel_dp_mst_encoder_init(dig_port,
				  intel_connector->base.base.id);

	if (!intel_edp_init_connector(intel_dp, intel_connector)) {
		intel_dp_aux_fini(intel_dp);
		intel_dp_mst_encoder_cleanup(dig_port);
		goto fail;
	}

	intel_dp_add_properties(intel_dp, connector);

	if (is_hdcp_supported(dev_priv, port) && !intel_dp_is_edp(intel_dp)) {
		int ret = intel_dp_init_hdcp(dig_port, intel_connector);
		if (ret)
			drm_dbg_kms(&dev_priv->drm,
				    "HDCP init failed, skipping.\n");
	}

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G45(dev_priv)) {
		u32 temp = intel_de_read(dev_priv, PEG_BAND_GAP_DATA);
		intel_de_write(dev_priv, PEG_BAND_GAP_DATA,
			       (temp & ~0xf) | 0xd);
	}

	return true;

fail:
	drm_connector_cleanup(connector);

	return false;
}

bool intel_dp_init(struct drm_i915_private *dev_priv,
		   i915_reg_t output_reg,
		   enum port port)
{
	struct intel_digital_port *dig_port;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	struct intel_connector *intel_connector;

	dig_port = kzalloc(sizeof(*dig_port), GFP_KERNEL);
	if (!dig_port)
		return false;

	intel_connector = intel_connector_alloc();
	if (!intel_connector)
		goto err_connector_alloc;

	intel_encoder = &dig_port->base;
	encoder = &intel_encoder->base;

	mutex_init(&dig_port->hdcp_mutex);

	if (drm_encoder_init(&dev_priv->drm, &intel_encoder->base,
			     &intel_dp_enc_funcs, DRM_MODE_ENCODER_TMDS,
			     "DP %c", port_name(port)))
		goto err_encoder_init;

	intel_encoder->hotplug = intel_dp_hotplug;
	intel_encoder->compute_config = intel_dp_compute_config;
	intel_encoder->get_hw_state = intel_dp_get_hw_state;
	intel_encoder->get_config = intel_dp_get_config;
	intel_encoder->update_pipe = intel_panel_update_backlight;
	intel_encoder->suspend = intel_dp_encoder_suspend;
	if (IS_CHERRYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = chv_dp_pre_pll_enable;
		intel_encoder->pre_enable = chv_pre_enable_dp;
		intel_encoder->enable = vlv_enable_dp;
		intel_encoder->disable = vlv_disable_dp;
		intel_encoder->post_disable = chv_post_disable_dp;
		intel_encoder->post_pll_disable = chv_dp_post_pll_disable;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = vlv_dp_pre_pll_enable;
		intel_encoder->pre_enable = vlv_pre_enable_dp;
		intel_encoder->enable = vlv_enable_dp;
		intel_encoder->disable = vlv_disable_dp;
		intel_encoder->post_disable = vlv_post_disable_dp;
	} else {
		intel_encoder->pre_enable = g4x_pre_enable_dp;
		intel_encoder->enable = g4x_enable_dp;
		intel_encoder->disable = g4x_disable_dp;
		intel_encoder->post_disable = g4x_post_disable_dp;
	}

	if ((IS_IVYBRIDGE(dev_priv) && port == PORT_A) ||
	    (HAS_PCH_CPT(dev_priv) && port != PORT_A))
		dig_port->dp.set_link_train = cpt_set_link_train;
	else
		dig_port->dp.set_link_train = g4x_set_link_train;

	if (IS_CHERRYVIEW(dev_priv))
		dig_port->dp.set_signal_levels = chv_set_signal_levels;
	else if (IS_VALLEYVIEW(dev_priv))
		dig_port->dp.set_signal_levels = vlv_set_signal_levels;
	else if (IS_IVYBRIDGE(dev_priv) && port == PORT_A)
		dig_port->dp.set_signal_levels = ivb_cpu_edp_set_signal_levels;
	else if (IS_GEN(dev_priv, 6) && port == PORT_A)
		dig_port->dp.set_signal_levels = snb_cpu_edp_set_signal_levels;
	else
		dig_port->dp.set_signal_levels = g4x_set_signal_levels;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv) ||
	    (HAS_PCH_SPLIT(dev_priv) && port != PORT_A)) {
		dig_port->dp.preemph_max = intel_dp_preemph_max_3;
		dig_port->dp.voltage_max = intel_dp_voltage_max_3;
	} else {
		dig_port->dp.preemph_max = intel_dp_preemph_max_2;
		dig_port->dp.voltage_max = intel_dp_voltage_max_2;
	}

	dig_port->dp.output_reg = output_reg;
	dig_port->max_lanes = 4;
	dig_port->dp.regs.dp_tp_ctl = DP_TP_CTL(port);
	dig_port->dp.regs.dp_tp_status = DP_TP_STATUS(port);

	intel_encoder->type = INTEL_OUTPUT_DP;
	intel_encoder->power_domain = intel_port_to_power_domain(port);
	if (IS_CHERRYVIEW(dev_priv)) {
		if (port == PORT_D)
			intel_encoder->pipe_mask = BIT(PIPE_C);
		else
			intel_encoder->pipe_mask = BIT(PIPE_A) | BIT(PIPE_B);
	} else {
		intel_encoder->pipe_mask = ~0;
	}
	intel_encoder->cloneable = 0;
	intel_encoder->port = port;
	intel_encoder->hpd_pin = intel_hpd_pin_default(dev_priv, port);

	dig_port->hpd_pulse = intel_dp_hpd_pulse;

	if (HAS_GMCH(dev_priv)) {
		if (IS_GM45(dev_priv))
			dig_port->connected = gm45_digital_port_connected;
		else
			dig_port->connected = g4x_digital_port_connected;
	} else {
		if (port == PORT_A)
			dig_port->connected = ilk_digital_port_connected;
		else
			dig_port->connected = ibx_digital_port_connected;
	}

	if (port != PORT_A)
		intel_infoframe_init(dig_port);

	dig_port->aux_ch = intel_bios_port_aux_ch(dev_priv, port);
	if (!intel_dp_init_connector(dig_port, intel_connector))
		goto err_init_connector;

	return true;

err_init_connector:
	drm_encoder_cleanup(encoder);
err_encoder_init:
	kfree(intel_connector);
err_connector_alloc:
	kfree(dig_port);
	return false;
}

void intel_dp_mst_suspend(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp;

		if (encoder->type != INTEL_OUTPUT_DDI)
			continue;

		intel_dp = enc_to_intel_dp(encoder);

		if (!intel_dp->can_mst)
			continue;

		if (intel_dp->is_mst)
			drm_dp_mst_topology_mgr_suspend(&intel_dp->mst_mgr);
	}
}

void intel_dp_mst_resume(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp;
		int ret;

		if (encoder->type != INTEL_OUTPUT_DDI)
			continue;

		intel_dp = enc_to_intel_dp(encoder);

		if (!intel_dp->can_mst)
			continue;

		ret = drm_dp_mst_topology_mgr_resume(&intel_dp->mst_mgr,
						     true);
		if (ret) {
			intel_dp->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
							false);
		}
	}
}
