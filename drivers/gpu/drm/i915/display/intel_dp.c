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
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "g4x_dp.h"
#include "i915_debugfs.h"
#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_backlight.h"
#include "intel_combo_phy_regs.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_aux.h"
#include "intel_dp_hdcp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dpio_phy.h"
#include "intel_dpll.h"
#include "intel_fifo_underrun.h"
#include "intel_hdcp.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_lspcon.h"
#include "intel_lvds.h"
#include "intel_panel.h"
#include "intel_pch_display.h"
#include "intel_pps.h"
#include "intel_psr.h"
#include "intel_tc.h"
#include "intel_vdsc.h"
#include "intel_vrr.h"

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
 *
 * This function is not safe to use prior to encoder type being set.
 */
bool intel_dp_is_edp(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	return dig_port->base.type == INTEL_OUTPUT_EDP;
}

static void intel_dp_unset_edid(struct intel_dp *intel_dp);
static int intel_dp_dsc_compute_bpp(struct intel_dp *intel_dp, u8 dsc_max_bpc);

/* Is link rate UHBR and thus 128b/132b? */
bool intel_dp_is_uhbr(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->port_clock >= 1000000;
}

static void intel_dp_set_default_sink_rates(struct intel_dp *intel_dp)
{
	intel_dp->sink_rates[0] = 162000;
	intel_dp->num_sink_rates = 1;
}

/* update sink rates from dpcd */
static void intel_dp_set_dpcd_sink_rates(struct intel_dp *intel_dp)
{
	static const int dp_rates[] = {
		162000, 270000, 540000, 810000
	};
	int i, max_rate;
	int max_lttpr_rate;

	if (drm_dp_has_quirk(&intel_dp->desc, DP_DPCD_QUIRK_CAN_DO_MAX_LINK_RATE_3_24_GBPS)) {
		/* Needed, e.g., for Apple MBP 2017, 15 inch eDP Retina panel */
		static const int quirk_rates[] = { 162000, 270000, 324000 };

		memcpy(intel_dp->sink_rates, quirk_rates, sizeof(quirk_rates));
		intel_dp->num_sink_rates = ARRAY_SIZE(quirk_rates);

		return;
	}

	/*
	 * Sink rates for 8b/10b.
	 */
	max_rate = drm_dp_bw_code_to_link_rate(intel_dp->dpcd[DP_MAX_LINK_RATE]);
	max_lttpr_rate = drm_dp_lttpr_max_link_rate(intel_dp->lttpr_common_caps);
	if (max_lttpr_rate)
		max_rate = min(max_rate, max_lttpr_rate);

	for (i = 0; i < ARRAY_SIZE(dp_rates); i++) {
		if (dp_rates[i] > max_rate)
			break;
		intel_dp->sink_rates[i] = dp_rates[i];
	}

	/*
	 * Sink rates for 128b/132b. If set, sink should support all 8b/10b
	 * rates and 10 Gbps.
	 */
	if (intel_dp->dpcd[DP_MAIN_LINK_CHANNEL_CODING] & DP_CAP_ANSI_128B132B) {
		u8 uhbr_rates = 0;

		BUILD_BUG_ON(ARRAY_SIZE(intel_dp->sink_rates) < ARRAY_SIZE(dp_rates) + 3);

		drm_dp_dpcd_readb(&intel_dp->aux,
				  DP_128B132B_SUPPORTED_LINK_RATES, &uhbr_rates);

		if (drm_dp_lttpr_count(intel_dp->lttpr_common_caps)) {
			/* We have a repeater */
			if (intel_dp->lttpr_common_caps[0] >= 0x20 &&
			    intel_dp->lttpr_common_caps[DP_MAIN_LINK_CHANNEL_CODING_PHY_REPEATER -
							DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV] &
			    DP_PHY_REPEATER_128B132B_SUPPORTED) {
				/* Repeater supports 128b/132b, valid UHBR rates */
				uhbr_rates &= intel_dp->lttpr_common_caps[DP_PHY_REPEATER_128B132B_RATES -
									  DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];
			} else {
				/* Does not support 128b/132b */
				uhbr_rates = 0;
			}
		}

		if (uhbr_rates & DP_UHBR10)
			intel_dp->sink_rates[i++] = 1000000;
		if (uhbr_rates & DP_UHBR13_5)
			intel_dp->sink_rates[i++] = 1350000;
		if (uhbr_rates & DP_UHBR20)
			intel_dp->sink_rates[i++] = 2000000;
	}

	intel_dp->num_sink_rates = i;
}

static void intel_dp_set_sink_rates(struct intel_dp *intel_dp)
{
	struct intel_connector *connector = intel_dp->attached_connector;
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &intel_dig_port->base;

	intel_dp_set_dpcd_sink_rates(intel_dp);

	if (intel_dp->num_sink_rates)
		return;

	drm_err(&dp_to_i915(intel_dp)->drm,
		"[CONNECTOR:%d:%s][ENCODER:%d:%s] Invalid DPCD with no link rates, using defaults\n",
		connector->base.base.id, connector->base.name,
		encoder->base.base.id, encoder->base.name);

	intel_dp_set_default_sink_rates(intel_dp);
}

static void intel_dp_set_default_max_sink_lane_count(struct intel_dp *intel_dp)
{
	intel_dp->max_sink_lane_count = 1;
}

static void intel_dp_set_max_sink_lane_count(struct intel_dp *intel_dp)
{
	struct intel_connector *connector = intel_dp->attached_connector;
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &intel_dig_port->base;

	intel_dp->max_sink_lane_count = drm_dp_max_lane_count(intel_dp->dpcd);

	switch (intel_dp->max_sink_lane_count) {
	case 1:
	case 2:
	case 4:
		return;
	}

	drm_err(&dp_to_i915(intel_dp)->drm,
		"[CONNECTOR:%d:%s][ENCODER:%d:%s] Invalid DPCD max lane count (%d), using default\n",
		connector->base.base.id, connector->base.name,
		encoder->base.base.id, encoder->base.name,
		intel_dp->max_sink_lane_count);

	intel_dp_set_default_max_sink_lane_count(intel_dp);
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

static int intel_dp_common_rate(struct intel_dp *intel_dp, int index)
{
	if (drm_WARN_ON(&dp_to_i915(intel_dp)->drm,
			index < 0 || index >= intel_dp->num_common_rates))
		return 162000;

	return intel_dp->common_rates[index];
}

/* Theoretical max between source and sink */
static int intel_dp_max_common_rate(struct intel_dp *intel_dp)
{
	return intel_dp_common_rate(intel_dp, intel_dp->num_common_rates - 1);
}

/* Theoretical max between source and sink */
static int intel_dp_max_common_lane_count(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	int source_max = dig_port->max_lanes;
	int sink_max = intel_dp->max_sink_lane_count;
	int fia_max = intel_tc_port_fia_max_lane_count(dig_port);
	int lttpr_max = drm_dp_lttpr_max_lane_count(intel_dp->lttpr_common_caps);

	if (lttpr_max)
		sink_max = min(sink_max, lttpr_max);

	return min3(source_max, sink_max, fia_max);
}

int intel_dp_max_lane_count(struct intel_dp *intel_dp)
{
	switch (intel_dp->max_link_lane_count) {
	case 1:
	case 2:
	case 4:
		return intel_dp->max_link_lane_count;
	default:
		MISSING_CASE(intel_dp->max_link_lane_count);
		return 1;
	}
}

/*
 * The required data bandwidth for a mode with given pixel clock and bpp. This
 * is the required net bandwidth independent of the data bandwidth efficiency.
 */
int
intel_dp_link_required(int pixel_clock, int bpp)
{
	/* pixel_clock is in kHz, divide bpp by 8 for bit to Byte conversion */
	return DIV_ROUND_UP(pixel_clock * bpp, 8);
}

/*
 * Given a link rate and lanes, get the data bandwidth.
 *
 * Data bandwidth is the actual payload rate, which depends on the data
 * bandwidth efficiency and the link rate.
 *
 * For 8b/10b channel encoding, SST and non-FEC, the data bandwidth efficiency
 * is 80%. For example, for a 1.62 Gbps link, 1.62*10^9 bps * 0.80 * (1/8) =
 * 162000 kBps. With 8-bit symbols, we have 162000 kHz symbol clock. Just by
 * coincidence, the port clock in kHz matches the data bandwidth in kBps, and
 * they equal the link bit rate in Gbps multiplied by 100000. (Note that this no
 * longer holds for data bandwidth as soon as FEC or MST is taken into account!)
 *
 * For 128b/132b channel encoding, the data bandwidth efficiency is 96.71%. For
 * example, for a 10 Gbps link, 10*10^9 bps * 0.9671 * (1/8) = 1208875
 * kBps. With 32-bit symbols, we have 312500 kHz symbol clock. The value 1000000
 * does not match the symbol clock, the port clock (not even if you think in
 * terms of a byte clock), nor the data bandwidth. It only matches the link bit
 * rate in units of 10000 bps.
 */
int
intel_dp_max_data_rate(int max_link_rate, int max_lanes)
{
	if (max_link_rate >= 1000000) {
		/*
		 * UHBR rates always use 128b/132b channel encoding, and have
		 * 97.71% data bandwidth efficiency. Consider max_link_rate the
		 * link bit rate in units of 10000 bps.
		 */
		int max_link_rate_kbps = max_link_rate * 10;

		max_link_rate_kbps = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(max_link_rate_kbps, 9671), 10000);
		max_link_rate = max_link_rate_kbps / 8;
	}

	/*
	 * Lower than UHBR rates always use 8b/10b channel encoding, and have
	 * 80% data bandwidth efficiency for SST non-FEC. However, this turns
	 * out to be a nop by coincidence, and can be skipped:
	 *
	 *	int max_link_rate_kbps = max_link_rate * 10;
	 *	max_link_rate_kbps = DIV_ROUND_CLOSEST_ULL(max_link_rate_kbps * 8, 10);
	 *	max_link_rate = max_link_rate_kbps / 8;
	 */

	return max_link_rate * max_lanes;
}

bool intel_dp_can_bigjoiner(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &intel_dig_port->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	return DISPLAY_VER(dev_priv) >= 12 ||
		(DISPLAY_VER(dev_priv) == 11 &&
		 encoder->port != PORT_A);
}

static int dg2_max_source_rate(struct intel_dp *intel_dp)
{
	return intel_dp_is_edp(intel_dp) ? 810000 : 1350000;
}

static bool is_low_voltage_sku(struct drm_i915_private *i915, enum phy phy)
{
	u32 voltage;

	voltage = intel_de_read(i915, ICL_PORT_COMP_DW3(phy)) & VOLTAGE_INFO_MASK;

	return voltage == VOLTAGE_INFO_0_85V;
}

static int icl_max_source_rate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, dig_port->base.port);

	if (intel_phy_is_combo(dev_priv, phy) &&
	    (is_low_voltage_sku(dev_priv, phy) || !intel_dp_is_edp(intel_dp)))
		return 540000;

	return 810000;
}

static int ehl_max_source_rate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, dig_port->base.port);

	if (intel_dp_is_edp(intel_dp) || is_low_voltage_sku(dev_priv, phy))
		return 540000;

	return 810000;
}

static int dg1_max_source_rate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);

	if (intel_phy_is_combo(i915, phy) && is_low_voltage_sku(i915, phy))
		return 540000;

	return 810000;
}

static int vbt_max_link_rate(struct intel_dp *intel_dp)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	int max_rate;

	max_rate = intel_bios_dp_max_link_rate(encoder);

	if (intel_dp_is_edp(intel_dp)) {
		struct intel_connector *connector = intel_dp->attached_connector;
		int edp_max_rate = connector->panel.vbt.edp.max_link_rate;

		if (max_rate && edp_max_rate)
			max_rate = min(max_rate, edp_max_rate);
		else if (edp_max_rate)
			max_rate = edp_max_rate;
	}

	return max_rate;
}

static void
intel_dp_set_source_rates(struct intel_dp *intel_dp)
{
	/* The values must be in increasing order */
	static const int icl_rates[] = {
		162000, 216000, 270000, 324000, 432000, 540000, 648000, 810000,
		1000000, 1350000,
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
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	const int *source_rates;
	int size, max_rate = 0, vbt_max_rate;

	/* This should only be done once */
	drm_WARN_ON(&dev_priv->drm,
		    intel_dp->source_rates || intel_dp->num_source_rates);

	if (DISPLAY_VER(dev_priv) >= 11) {
		source_rates = icl_rates;
		size = ARRAY_SIZE(icl_rates);
		if (IS_DG2(dev_priv))
			max_rate = dg2_max_source_rate(intel_dp);
		else if (IS_ALDERLAKE_P(dev_priv) || IS_ALDERLAKE_S(dev_priv) ||
			 IS_DG1(dev_priv) || IS_ROCKETLAKE(dev_priv))
			max_rate = dg1_max_source_rate(intel_dp);
		else if (IS_JSL_EHL(dev_priv))
			max_rate = ehl_max_source_rate(intel_dp);
		else
			max_rate = icl_max_source_rate(intel_dp);
	} else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) {
		source_rates = bxt_rates;
		size = ARRAY_SIZE(bxt_rates);
	} else if (DISPLAY_VER(dev_priv) == 9) {
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

	vbt_max_rate = vbt_max_link_rate(intel_dp);
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
	/* FIXME figure out what we actually want here */
	const struct drm_display_mode *fixed_mode =
		intel_panel_preferred_fixed_mode(intel_dp->attached_connector);
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

	if (intel_dp_is_edp(intel_dp) && !intel_dp->use_max_params) {
		drm_dbg_kms(&i915->drm,
			    "Retrying Link training for eDP with max parameters\n");
		intel_dp->use_max_params = true;
		return 0;
	}

	index = intel_dp_rate_index(intel_dp->common_rates,
				    intel_dp->num_common_rates,
				    link_rate);
	if (index > 0) {
		if (intel_dp_is_edp(intel_dp) &&
		    !intel_dp_can_link_train_fallback_for_edp(intel_dp,
							      intel_dp_common_rate(intel_dp, index - 1),
							      lane_count)) {
			drm_dbg_kms(&i915->drm,
				    "Retrying Link training for eDP with same parameters\n");
			return 0;
		}
		intel_dp->max_link_rate = intel_dp_common_rate(intel_dp, index - 1);
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
	if (DISPLAY_VER(i915) >= 13)
		return 17280 * 8;
	else if (DISPLAY_VER(i915) >= 11)
		return 7680 * 8;
	else
		return 6144 * 8;
}

static u16 intel_dp_dsc_get_output_bpp(struct drm_i915_private *i915,
				       u32 link_clock, u32 lane_count,
				       u32 mode_clock, u32 mode_hdisplay,
				       bool bigjoiner,
				       u32 pipe_bpp)
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

	/* Small Joiner Check: output bpp <= joiner RAM (bits) / Horiz. width */
	max_bpp_small_joiner_ram = small_joiner_ram_size_bits(i915) /
		mode_hdisplay;

	if (bigjoiner)
		max_bpp_small_joiner_ram *= 2;

	/*
	 * Greatest allowed DSC BPP = MIN (output BPP from available Link BW
	 * check, output bpp from small joiner RAM check)
	 */
	bits_per_pixel = min(bits_per_pixel, max_bpp_small_joiner_ram);

	if (bigjoiner) {
		u32 max_bpp_bigjoiner =
			i915->max_cdclk_freq * 48 /
			intel_dp_mode_to_fec_clock(mode_clock);

		bits_per_pixel = min(bits_per_pixel, max_bpp_bigjoiner);
	}

	/* Error out if the max bpp is less than smallest allowed valid bpp */
	if (bits_per_pixel < valid_dsc_bpp[0]) {
		drm_dbg_kms(&i915->drm, "Unsupported BPP %u, min %u\n",
			    bits_per_pixel, valid_dsc_bpp[0]);
		return 0;
	}

	/* From XE_LPD onwards we support from bpc upto uncompressed bpp-1 BPPs */
	if (DISPLAY_VER(i915) >= 13) {
		bits_per_pixel = min(bits_per_pixel, pipe_bpp - 1);
	} else {
		/* Find the nearest match in the array of known BPPs from VESA */
		for (i = 0; i < ARRAY_SIZE(valid_dsc_bpp) - 1; i++) {
			if (bits_per_pixel < valid_dsc_bpp[i + 1])
				break;
		}
		bits_per_pixel = valid_dsc_bpp[i];
	}

	/*
	 * Compressed BPP in U6.4 format so multiply by 16, for Gen 11,
	 * fractional part is 0
	 */
	return bits_per_pixel << 4;
}

static u8 intel_dp_dsc_get_slice_count(struct intel_dp *intel_dp,
				       int mode_clock, int mode_hdisplay,
				       bool bigjoiner)
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
	min_slice_count = max_t(u8, min_slice_count,
				DIV_ROUND_UP(mode_hdisplay,
					     max_slice_width));

	/* Find the closest match to the valid slice count values */
	for (i = 0; i < ARRAY_SIZE(valid_dsc_slicecount); i++) {
		u8 test_slice_count = valid_dsc_slicecount[i] << bigjoiner;

		if (test_slice_count >
		    drm_dp_dsc_sink_max_slice_count(intel_dp->dsc_dpcd, false))
			break;

		/* big joiner needs small joiner to be enabled */
		if (bigjoiner && test_slice_count < 4)
			continue;

		if (min_slice_count <= test_slice_count)
			return test_slice_count;
	}

	drm_dbg_kms(&i915->drm, "Unsupported Slice Count %d\n",
		    min_slice_count);
	return 0;
}

static enum intel_output_format
intel_dp_output_format(struct intel_connector *connector,
		       bool ycbcr_420_output)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);

	if (!connector->base.ycbcr_420_allowed || !ycbcr_420_output)
		return INTEL_OUTPUT_FORMAT_RGB;

	if (intel_dp->dfp.rgb_to_ycbcr &&
	    intel_dp->dfp.ycbcr_444_to_420)
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
intel_dp_mode_min_output_bpp(struct intel_connector *connector,
			     const struct drm_display_mode *mode)
{
	const struct drm_display_info *info = &connector->base.display_info;
	enum intel_output_format output_format =
		intel_dp_output_format(connector, drm_mode_is_420_only(info, mode));

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

static int intel_dp_max_tmds_clock(struct intel_dp *intel_dp)
{
	struct intel_connector *connector = intel_dp->attached_connector;
	const struct drm_display_info *info = &connector->base.display_info;
	int max_tmds_clock = intel_dp->dfp.max_tmds_clock;

	/* Only consider the sink's max TMDS clock if we know this is a HDMI DFP */
	if (max_tmds_clock && info->max_tmds_clock)
		max_tmds_clock = min(max_tmds_clock, info->max_tmds_clock);

	return max_tmds_clock;
}

static enum drm_mode_status
intel_dp_tmds_clock_valid(struct intel_dp *intel_dp,
			  int clock, int bpc, bool ycbcr420_output,
			  bool respect_downstream_limits)
{
	int tmds_clock, min_tmds_clock, max_tmds_clock;

	if (!respect_downstream_limits)
		return MODE_OK;

	tmds_clock = intel_hdmi_tmds_clock(clock, bpc, ycbcr420_output);

	min_tmds_clock = intel_dp->dfp.min_tmds_clock;
	max_tmds_clock = intel_dp_max_tmds_clock(intel_dp);

	if (min_tmds_clock && tmds_clock < min_tmds_clock)
		return MODE_CLOCK_LOW;

	if (max_tmds_clock && tmds_clock > max_tmds_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static enum drm_mode_status
intel_dp_mode_valid_downstream(struct intel_connector *connector,
			       const struct drm_display_mode *mode,
			       int target_clock)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	const struct drm_display_info *info = &connector->base.display_info;
	enum drm_mode_status status;
	bool ycbcr_420_only;

	/* If PCON supports FRL MODE, check FRL bandwidth constraints */
	if (intel_dp->dfp.pcon_max_frl_bw) {
		int target_bw;
		int max_frl_bw;
		int bpp = intel_dp_mode_min_output_bpp(connector, mode);

		target_bw = bpp * target_clock;

		max_frl_bw = intel_dp->dfp.pcon_max_frl_bw;

		/* converting bw from Gbps to Kbps*/
		max_frl_bw = max_frl_bw * 1000000;

		if (target_bw > max_frl_bw)
			return MODE_CLOCK_HIGH;

		return MODE_OK;
	}

	if (intel_dp->dfp.max_dotclock &&
	    target_clock > intel_dp->dfp.max_dotclock)
		return MODE_CLOCK_HIGH;

	ycbcr_420_only = drm_mode_is_420_only(info, mode);

	/* Assume 8bpc for the DP++/HDMI/DVI TMDS clock check */
	status = intel_dp_tmds_clock_valid(intel_dp, target_clock,
					   8, ycbcr_420_only, true);

	if (status != MODE_OK) {
		if (ycbcr_420_only ||
		    !connector->base.ycbcr_420_allowed ||
		    !drm_mode_is_420_also(info, mode))
			return status;

		status = intel_dp_tmds_clock_valid(intel_dp, target_clock,
						   8, true, true);
		if (status != MODE_OK)
			return status;
	}

	return MODE_OK;
}

static bool intel_dp_need_bigjoiner(struct intel_dp *intel_dp,
				    int hdisplay, int clock)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!intel_dp_can_bigjoiner(intel_dp))
		return false;

	return clock > i915->max_dotclk_freq || hdisplay > 5120;
}

static enum drm_mode_status
intel_dp_mode_valid(struct drm_connector *_connector,
		    struct drm_display_mode *mode)
{
	struct intel_connector *connector = to_intel_connector(_connector);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	const struct drm_display_mode *fixed_mode;
	int target_clock = mode->clock;
	int max_rate, mode_rate, max_lanes, max_link_clock;
	int max_dotclk = dev_priv->max_dotclk_freq;
	u16 dsc_max_output_bpp = 0;
	u8 dsc_slice_count = 0;
	enum drm_mode_status status;
	bool dsc = false, bigjoiner = false;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_H_ILLEGAL;

	fixed_mode = intel_panel_fixed_mode(connector, mode);
	if (intel_dp_is_edp(intel_dp) && fixed_mode) {
		status = intel_panel_mode_valid(connector, mode);
		if (status != MODE_OK)
			return status;

		target_clock = fixed_mode->clock;
	}

	if (mode->clock < 10000)
		return MODE_CLOCK_LOW;

	if (intel_dp_need_bigjoiner(intel_dp, mode->hdisplay, target_clock)) {
		bigjoiner = true;
		max_dotclk *= 2;
	}
	if (target_clock > max_dotclk)
		return MODE_CLOCK_HIGH;

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
	if (DISPLAY_VER(dev_priv) >= 10 &&
	    drm_dp_sink_supports_dsc(intel_dp->dsc_dpcd)) {
		/*
		 * TBD pass the connector BPC,
		 * for now U8_MAX so that max BPC on that platform would be picked
		 */
		int pipe_bpp = intel_dp_dsc_compute_bpp(intel_dp, U8_MAX);

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
							    mode->hdisplay,
							    bigjoiner,
							    pipe_bpp) >> 4;
			dsc_slice_count =
				intel_dp_dsc_get_slice_count(intel_dp,
							     target_clock,
							     mode->hdisplay,
							     bigjoiner);
		}

		dsc = dsc_max_output_bpp && dsc_slice_count;
	}

	/*
	 * Big joiner configuration needs DSC for TGL which is not true for
	 * XE_LPD where uncompressed joiner is supported.
	 */
	if (DISPLAY_VER(dev_priv) < 13 && bigjoiner && !dsc)
		return MODE_CLOCK_HIGH;

	if (mode_rate > max_rate && !dsc)
		return MODE_CLOCK_HIGH;

	status = intel_dp_mode_valid_downstream(connector, mode, target_clock);
	if (status != MODE_OK)
		return status;

	return intel_mode_valid_max_plane_size(dev_priv, mode, bigjoiner);
}

bool intel_dp_source_supports_tps3(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) >= 9 || IS_BROADWELL(i915) || IS_HASWELL(i915);
}

bool intel_dp_source_supports_tps4(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) >= 10;
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
	int len;

	len = intel_dp_common_len_rate_limit(intel_dp, intel_dp->max_link_rate);

	return intel_dp_common_rate(intel_dp, len - 1);
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
	if (DISPLAY_VER(dev_priv) >= 12)
		return true;

	if (DISPLAY_VER(dev_priv) == 11 && pipe_config->cpu_transcoder != TRANSCODER_A)
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
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP) && !crtc_state->fec_enable)
		return false;

	return intel_dsc_source_support(crtc_state) &&
		drm_dp_sink_supports_dsc(intel_dp->dsc_dpcd);
}

static bool intel_dp_is_ycbcr420(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 ||
		(crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444 &&
		 intel_dp->dfp.ycbcr_444_to_420);
}

static int intel_dp_hdmi_compute_bpc(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state,
				     int bpc, bool respect_downstream_limits)
{
	bool ycbcr420_output = intel_dp_is_ycbcr420(intel_dp, crtc_state);
	int clock = crtc_state->hw.adjusted_mode.crtc_clock;

	/*
	 * Current bpc could already be below 8bpc due to
	 * FDI bandwidth constraints or other limits.
	 * HDMI minimum is 8bpc however.
	 */
	bpc = max(bpc, 8);

	/*
	 * We will never exceed downstream TMDS clock limits while
	 * attempting deep color. If the user insists on forcing an
	 * out of spec mode they will have to be satisfied with 8bpc.
	 */
	if (!respect_downstream_limits)
		bpc = 8;

	for (; bpc >= 8; bpc -= 2) {
		if (intel_hdmi_bpc_possible(crtc_state, bpc,
					    intel_dp->has_hdmi_sink, ycbcr420_output) &&
		    intel_dp_tmds_clock_valid(intel_dp, clock, bpc, ycbcr420_output,
					      respect_downstream_limits) == MODE_OK)
			return bpc;
	}

	return -EINVAL;
}

static int intel_dp_max_bpp(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state,
			    bool respect_downstream_limits)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	int bpp, bpc;

	bpc = crtc_state->pipe_bpp / 3;

	if (intel_dp->dfp.max_bpc)
		bpc = min_t(int, bpc, intel_dp->dfp.max_bpc);

	if (intel_dp->dfp.min_tmds_clock) {
		int max_hdmi_bpc;

		max_hdmi_bpc = intel_dp_hdmi_compute_bpc(intel_dp, crtc_state, bpc,
							 respect_downstream_limits);
		if (max_hdmi_bpc < 0)
			return 0;

		bpc = min(bpc, max_hdmi_bpc);
	}

	bpp = bpc * 3;
	if (intel_dp_is_edp(intel_dp)) {
		/* Get bpp from vbt only for panels that dont have bpp in edid */
		if (intel_connector->base.display_info.bpc == 0 &&
		    intel_connector->panel.vbt.edp.bpp &&
		    intel_connector->panel.vbt.edp.bpp < bpp) {
			drm_dbg_kms(&dev_priv->drm,
				    "clamping bpp for eDP panel to BIOS-provided %i\n",
				    intel_connector->panel.vbt.edp.bpp);
			bpp = intel_connector->panel.vbt.edp.bpp;
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
				limits->min_rate = limits->max_rate =
					intel_dp->compliance.test_link_rate;
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
	int bpp, i, lane_count;
	int mode_rate, link_rate, link_avail;

	for (bpp = limits->max_bpp; bpp >= limits->min_bpp; bpp -= 2 * 3) {
		int output_bpp = intel_dp_output_bpp(pipe_config->output_format, bpp);

		mode_rate = intel_dp_link_required(adjusted_mode->crtc_clock,
						   output_bpp);

		for (i = 0; i < intel_dp->num_common_rates; i++) {
			link_rate = intel_dp_common_rate(intel_dp, i);
			if (link_rate < limits->min_rate ||
			    link_rate > limits->max_rate)
				continue;

			for (lane_count = limits->min_lane_count;
			     lane_count <= limits->max_lane_count;
			     lane_count <<= 1) {
				link_avail = intel_dp_max_data_rate(link_rate,
								    lane_count);

				if (mode_rate <= link_avail) {
					pipe_config->lane_count = lane_count;
					pipe_config->pipe_bpp = bpp;
					pipe_config->port_clock = link_rate;

					return 0;
				}
			}
		}
	}

	return -EINVAL;
}

static int intel_dp_dsc_compute_bpp(struct intel_dp *intel_dp, u8 max_req_bpc)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int i, num_bpc;
	u8 dsc_bpc[3] = {0};
	u8 dsc_max_bpc;

	/* Max DSC Input BPC for ICL is 10 and for TGL+ is 12 */
	if (DISPLAY_VER(i915) >= 12)
		dsc_max_bpc = min_t(u8, 12, max_req_bpc);
	else
		dsc_max_bpc = min_t(u8, 10, max_req_bpc);

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

	/*
	 * RC_MODEL_SIZE is currently a constant across all configurations.
	 *
	 * FIXME: Look into using sink defined DPCD DP_DSC_RC_BUF_BLK_SIZE and
	 * DP_DSC_RC_BUF_SIZE for this.
	 */
	vdsc_cfg->rc_model_size = DSC_RC_MODEL_SIZE_CONST;

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

	ret = intel_dsc_compute_params(crtc_state);
	if (ret)
		return ret;

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
	int pipe_bpp;
	int ret;

	pipe_config->fec_enable = !intel_dp_is_edp(intel_dp) &&
		intel_dp_supports_fec(intel_dp, pipe_config);

	if (!intel_dp_supports_dsc(intel_dp, pipe_config))
		return -EINVAL;

	pipe_bpp = intel_dp_dsc_compute_bpp(intel_dp, conn_state->max_requested_bpc);

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
	pipe_config->port_clock = limits->max_rate;
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
						    adjusted_mode->crtc_hdisplay,
						    pipe_config->bigjoiner_pipes,
						    pipe_bpp);
		dsc_dp_slice_count =
			intel_dp_dsc_get_slice_count(intel_dp,
						     adjusted_mode->crtc_clock,
						     adjusted_mode->crtc_hdisplay,
						     pipe_config->bigjoiner_pipes);
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

	/* As of today we support DSC for only RGB */
	if (intel_dp->force_dsc_bpp) {
		if (intel_dp->force_dsc_bpp >= 8 &&
		    intel_dp->force_dsc_bpp < pipe_bpp) {
			drm_dbg_kms(&dev_priv->drm,
				    "DSC BPP forced to %d",
				    intel_dp->force_dsc_bpp);
			pipe_config->dsc.compressed_bpp =
						intel_dp->force_dsc_bpp;
		} else {
			drm_dbg_kms(&dev_priv->drm,
				    "Invalid DSC BPP %d",
				    intel_dp->force_dsc_bpp);
		}
	}

	/*
	 * VDSC engine operates at 1 Pixel per clock, so if peak pixel rate
	 * is greater than the maximum Cdclock and if slice count is even
	 * then we need to use 2 VDSC instances.
	 */
	if (adjusted_mode->crtc_clock > dev_priv->max_cdclk_freq ||
	    pipe_config->bigjoiner_pipes) {
		if (pipe_config->dsc.slice_count < 2) {
			drm_dbg_kms(&dev_priv->drm,
				    "Cannot split stream to use 2 VDSC instances\n");
			return -EINVAL;
		}

		pipe_config->dsc.dsc_split = true;
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
			     struct drm_connector_state *conn_state,
			     bool respect_downstream_limits)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	const struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct link_config_limits limits;
	bool joiner_needs_dsc = false;
	int ret;

	limits.min_rate = intel_dp_common_rate(intel_dp, 0);
	limits.max_rate = intel_dp_max_link_rate(intel_dp);

	limits.min_lane_count = 1;
	limits.max_lane_count = intel_dp_max_lane_count(intel_dp);

	limits.min_bpp = intel_dp_min_bpp(pipe_config->output_format);
	limits.max_bpp = intel_dp_max_bpp(intel_dp, pipe_config, respect_downstream_limits);

	if (intel_dp->use_max_params) {
		/*
		 * Use the maximum clock and number of lanes the eDP panel
		 * advertizes being capable of in case the initial fast
		 * optimal params failed us. The panels are generally
		 * designed to support only a single clock and lane
		 * configuration, and typically on older panels these
		 * values correspond to the native resolution of the panel.
		 */
		limits.min_lane_count = limits.max_lane_count;
		limits.min_rate = limits.max_rate;
	}

	intel_dp_adjust_compliance_config(intel_dp, pipe_config, &limits);

	drm_dbg_kms(&i915->drm, "DP link computation with max lane count %i "
		    "max rate %d max bpp %d pixel clock %iKHz\n",
		    limits.max_lane_count, limits.max_rate,
		    limits.max_bpp, adjusted_mode->crtc_clock);

	if (intel_dp_need_bigjoiner(intel_dp, adjusted_mode->crtc_hdisplay,
				    adjusted_mode->crtc_clock))
		pipe_config->bigjoiner_pipes = GENMASK(crtc->pipe + 1, crtc->pipe);

	/*
	 * Pipe joiner needs compression up to display 12 due to bandwidth
	 * limitation. DG2 onwards pipe joiner can be enabled without
	 * compression.
	 */
	joiner_needs_dsc = DISPLAY_VER(i915) < 13 && pipe_config->bigjoiner_pipes;

	/*
	 * Optimize for slow and wide for everything, because there are some
	 * eDP 1.3 and 1.4 panels don't work well with fast and narrow.
	 */
	ret = intel_dp_compute_link_config_wide(intel_dp, pipe_config, &limits);

	if (ret || joiner_needs_dsc || intel_dp->force_dsc_en) {
		drm_dbg_kms(&i915->drm, "Try DSC (fallback=%s, joiner=%s, force=%s)\n",
			    str_yes_no(ret), str_yes_no(joiner_needs_dsc),
			    str_yes_no(intel_dp->force_dsc_en));
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
	if (DISPLAY_VER(dev_priv) < 12 && port == PORT_A)
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
	vsc->sdp_type = DP_SDP_VSC;

	if (crtc_state->has_psr2) {
		if (intel_dp->psr.colorimetry_support &&
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

static bool cpu_transcoder_has_drrs(struct drm_i915_private *i915,
				    enum transcoder cpu_transcoder)
{
	/* M1/N1 is double buffered */
	if (DISPLAY_VER(i915) >= 9 || IS_BROADWELL(i915))
		return true;

	return intel_cpu_transcoder_has_m2_n2(i915, cpu_transcoder);
}

static bool can_enable_drrs(struct intel_connector *connector,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_display_mode *downclock_mode)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);

	if (pipe_config->vrr.enable)
		return false;

	/*
	 * DRRS and PSR can't be enable together, so giving preference to PSR
	 * as it allows more power-savings by complete shutting down display,
	 * so to guarantee this, intel_drrs_compute_config() must be called
	 * after intel_psr_compute_config().
	 */
	if (pipe_config->has_psr)
		return false;

	/* FIXME missing FDI M2/N2 etc. */
	if (pipe_config->has_pch_encoder)
		return false;

	if (!cpu_transcoder_has_drrs(i915, pipe_config->cpu_transcoder))
		return false;

	return downclock_mode &&
		intel_panel_drrs_type(connector) == DRRS_TYPE_SEAMLESS;
}

static void
intel_dp_drrs_compute_config(struct intel_connector *connector,
			     struct intel_crtc_state *pipe_config,
			     int output_bpp, bool constant_n)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *downclock_mode =
		intel_panel_downclock_mode(connector, &pipe_config->hw.adjusted_mode);
	int pixel_clock;

	if (!can_enable_drrs(connector, pipe_config, downclock_mode)) {
		if (intel_cpu_transcoder_has_m2_n2(i915, pipe_config->cpu_transcoder))
			intel_zero_m_n(&pipe_config->dp_m2_n2);
		return;
	}

	if (IS_IRONLAKE(i915) || IS_SANDYBRIDGE(i915) || IS_IVYBRIDGE(i915))
		pipe_config->msa_timing_delay = connector->panel.vbt.edp.drrs_msa_timing_delay;

	pipe_config->has_drrs = true;

	pixel_clock = downclock_mode->clock;
	if (pipe_config->splitter.enable)
		pixel_clock /= pipe_config->splitter.link_count;

	intel_link_compute_m_n(output_bpp, pipe_config->lane_count, pixel_clock,
			       pipe_config->port_clock, &pipe_config->dp_m2_n2,
			       constant_n, pipe_config->fec_enable);

	/* FIXME: abstract this better */
	if (pipe_config->splitter.enable)
		pipe_config->dp_m2_n2.data_m *= pipe_config->splitter.link_count;
}

static bool intel_dp_has_audio(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	const struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);

	if (!intel_dp_port_has_audio(i915, encoder->port))
		return false;

	if (intel_conn_state->force_audio == HDMI_AUDIO_AUTO)
		return intel_dp->has_audio;
	else
		return intel_conn_state->force_audio == HDMI_AUDIO_ON;
}

static int
intel_dp_compute_output_format(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state,
			       bool respect_downstream_limits)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_connector *connector = intel_dp->attached_connector;
	const struct drm_display_info *info = &connector->base.display_info;
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	bool ycbcr_420_only;
	int ret;

	ycbcr_420_only = drm_mode_is_420_only(info, adjusted_mode);

	crtc_state->output_format = intel_dp_output_format(connector, ycbcr_420_only);

	if (ycbcr_420_only && !intel_dp_is_ycbcr420(intel_dp, crtc_state)) {
		drm_dbg_kms(&i915->drm,
			    "YCbCr 4:2:0 mode but YCbCr 4:2:0 output not possible. Falling back to RGB.\n");
		crtc_state->output_format = INTEL_OUTPUT_FORMAT_RGB;
	}

	ret = intel_dp_compute_link_config(encoder, crtc_state, conn_state,
					   respect_downstream_limits);
	if (ret) {
		if (intel_dp_is_ycbcr420(intel_dp, crtc_state) ||
		    !connector->base.ycbcr_420_allowed ||
		    !drm_mode_is_420_also(info, adjusted_mode))
			return ret;

		crtc_state->output_format = intel_dp_output_format(connector, true);
		ret = intel_dp_compute_link_config(encoder, crtc_state, conn_state,
						   respect_downstream_limits);
	}

	return ret;
}

int
intel_dp_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_state *pipe_config,
			struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	const struct drm_display_mode *fixed_mode;
	struct intel_connector *connector = intel_dp->attached_connector;
	bool constant_n = drm_dp_has_quirk(&intel_dp->desc, DP_DPCD_QUIRK_CONSTANT_N);
	int ret = 0, output_bpp;

	if (HAS_PCH_SPLIT(dev_priv) && !HAS_DDI(dev_priv) && encoder->port != PORT_A)
		pipe_config->has_pch_encoder = true;

	pipe_config->has_audio = intel_dp_has_audio(encoder, pipe_config, conn_state);

	fixed_mode = intel_panel_fixed_mode(connector, adjusted_mode);
	if (intel_dp_is_edp(intel_dp) && fixed_mode) {
		ret = intel_panel_compute_config(connector, adjusted_mode);
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

	/*
	 * Try to respect downstream TMDS clock limits first, if
	 * that fails assume the user might know something we don't.
	 */
	ret = intel_dp_compute_output_format(encoder, pipe_config, conn_state, true);
	if (ret)
		ret = intel_dp_compute_output_format(encoder, pipe_config, conn_state, false);
	if (ret)
		return ret;

	if ((intel_dp_is_edp(intel_dp) && fixed_mode) ||
	    pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR420) {
		ret = intel_panel_fitting(pipe_config, conn_state);
		if (ret)
			return ret;
	}

	pipe_config->limited_color_range =
		intel_dp_limited_color_range(pipe_config, conn_state);

	if (pipe_config->dsc.compression_enable)
		output_bpp = pipe_config->dsc.compressed_bpp;
	else
		output_bpp = intel_dp_output_bpp(pipe_config->output_format,
						 pipe_config->pipe_bpp);

	if (intel_dp->mso_link_count) {
		int n = intel_dp->mso_link_count;
		int overlap = intel_dp->mso_pixel_overlap;

		pipe_config->splitter.enable = true;
		pipe_config->splitter.link_count = n;
		pipe_config->splitter.pixel_overlap = overlap;

		drm_dbg_kms(&dev_priv->drm, "MSO link count %d, pixel overlap %d\n",
			    n, overlap);

		adjusted_mode->crtc_hdisplay = adjusted_mode->crtc_hdisplay / n + overlap;
		adjusted_mode->crtc_hblank_start = adjusted_mode->crtc_hblank_start / n + overlap;
		adjusted_mode->crtc_hblank_end = adjusted_mode->crtc_hblank_end / n + overlap;
		adjusted_mode->crtc_hsync_start = adjusted_mode->crtc_hsync_start / n + overlap;
		adjusted_mode->crtc_hsync_end = adjusted_mode->crtc_hsync_end / n + overlap;
		adjusted_mode->crtc_htotal = adjusted_mode->crtc_htotal / n + overlap;
		adjusted_mode->crtc_clock /= n;
	}

	intel_link_compute_m_n(output_bpp,
			       pipe_config->lane_count,
			       adjusted_mode->crtc_clock,
			       pipe_config->port_clock,
			       &pipe_config->dp_m_n,
			       constant_n, pipe_config->fec_enable);

	/* FIXME: abstract this better */
	if (pipe_config->splitter.enable)
		pipe_config->dp_m_n.data_m *= pipe_config->splitter.link_count;

	if (!HAS_DDI(dev_priv))
		g4x_dp_set_clock(encoder, pipe_config);

	intel_vrr_compute_config(pipe_config, conn_state);
	intel_psr_compute_config(intel_dp, pipe_config, conn_state);
	intel_dp_drrs_compute_config(connector, pipe_config,
				     output_bpp, constant_n);
	intel_dp_compute_vsc_sdp(intel_dp, pipe_config, conn_state);
	intel_dp_compute_hdr_metadata_infoframe_sdp(intel_dp, pipe_config, conn_state);

	return 0;
}

void intel_dp_set_link_params(struct intel_dp *intel_dp,
			      int link_rate, int lane_count)
{
	memset(intel_dp->train_set, 0, sizeof(intel_dp->train_set));
	intel_dp->link_trained = false;
	intel_dp->link_rate = link_rate;
	intel_dp->lane_count = lane_count;
}

static void intel_dp_reset_max_link_params(struct intel_dp *intel_dp)
{
	intel_dp->max_link_lane_count = intel_dp_max_common_lane_count(intel_dp);
	intel_dp->max_link_rate = intel_dp_max_common_rate(intel_dp);
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

	intel_backlight_enable(crtc_state, conn_state);
	intel_pps_backlight_on(intel_dp);
}

/* Disable backlight PP control and backlight PWM. */
void intel_edp_backlight_off(const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(old_conn_state->best_encoder));
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&i915->drm, "\n");

	intel_pps_backlight_off(intel_dp);
	intel_backlight_disable(old_conn_state);
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
			    str_enable_disable(enable));
}

static void
intel_edp_init_source_oui(struct intel_dp *intel_dp, bool careful)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 oui[] = { 0x00, 0xaa, 0x01 };
	u8 buf[3] = { 0 };

	/*
	 * During driver init, we want to be careful and avoid changing the source OUI if it's
	 * already set to what we want, so as to avoid clearing any state by accident
	 */
	if (careful) {
		if (drm_dp_dpcd_read(&intel_dp->aux, DP_SOURCE_OUI, buf, sizeof(buf)) < 0)
			drm_err(&i915->drm, "Failed to read source OUI\n");

		if (memcmp(oui, buf, sizeof(oui)) == 0)
			return;
	}

	if (drm_dp_dpcd_write(&intel_dp->aux, DP_SOURCE_OUI, oui, sizeof(oui)) < 0)
		drm_err(&i915->drm, "Failed to write source OUI\n");

	intel_dp->last_oui_write = jiffies;
}

void intel_dp_wait_source_oui(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	drm_dbg_kms(&i915->drm, "Performing OUI wait\n");
	wait_remaining_ms_from_jiffies(intel_dp->last_oui_write, 30);
}

/* If the device supports it, try to set the power state appropriately */
void intel_dp_set_power(struct intel_dp *intel_dp, u8 mode)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	int ret, i;

	/* Should have a valid DPCD by this point */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (mode != DP_SET_POWER_D0) {
		if (downstream_hpd_needs_d0(intel_dp))
			return;

		ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER, mode);
	} else {
		struct intel_lspcon *lspcon = dp_to_lspcon(intel_dp);

		lspcon_resume(dp_to_dig_port(intel_dp));

		/* Write the source OUI as early as possible */
		if (intel_dp_is_edp(intel_dp))
			intel_edp_init_source_oui(intel_dp, false);

		/*
		 * When turning on, we need to retry for 1ms to give the sink
		 * time to wake up.
		 */
		for (i = 0; i < 3; i++) {
			ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER, mode);
			if (ret == 1)
				break;
			msleep(1);
		}

		if (ret == 1 && lspcon->active)
			lspcon_wait_pcon_mode(lspcon);
	}

	if (ret != 1)
		drm_dbg_kms(&i915->drm, "[ENCODER:%d:%s] Set power to %s failed\n",
			    encoder->base.base.id, encoder->base.name,
			    mode == DP_SET_POWER_D0 ? "D0" : "D3");
}

static bool
intel_dp_get_dpcd(struct intel_dp *intel_dp);

/**
 * intel_dp_sync_state - sync the encoder state during init/resume
 * @encoder: intel encoder to sync
 * @crtc_state: state for the CRTC connected to the encoder
 *
 * Sync any state stored in the encoder wrt. HW state during driver init
 * and system resume.
 */
void intel_dp_sync_state(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	if (!crtc_state)
		return;

	/*
	 * Don't clobber DPCD if it's been already read out during output
	 * setup (eDP) or detect.
	 */
	if (intel_dp->dpcd[DP_DPCD_REV] == 0)
		intel_dp_get_dpcd(intel_dp);

	intel_dp_reset_max_link_params(intel_dp);
}

bool intel_dp_initial_fastset_check(struct intel_encoder *encoder,
				    struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	/*
	 * If BIOS has set an unsupported or non-standard link rate for some
	 * reason force an encoder recompute and full modeset.
	 */
	if (intel_dp_rate_index(intel_dp->source_rates, intel_dp->num_source_rates,
				crtc_state->port_clock) < 0) {
		drm_dbg_kms(&i915->drm, "Forcing full modeset due to unsupported link rate\n");
		crtc_state->uapi.connectors_changed = true;
		return false;
	}

	/*
	 * FIXME hack to force full modeset when DSC is being used.
	 *
	 * As long as we do not have full state readout and config comparison
	 * of crtc_state->dsc, we have no way to ensure reliable fastset.
	 * Remove once we have readout for DSC.
	 */
	if (crtc_state->dsc.compression_enable) {
		drm_dbg_kms(&i915->drm, "Forcing full modeset due to DSC being enabled\n");
		crtc_state->uapi.mode_changed = true;
		return false;
	}

	if (CAN_PSR(intel_dp)) {
		drm_dbg_kms(&i915->drm, "Forcing full modeset to compute PSR state\n");
		crtc_state->uapi.mode_changed = true;
		return false;
	}

	return true;
}

static void intel_dp_get_pcon_dsc_cap(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	/* Clear the cached register set to avoid using stale values */

	memset(intel_dp->pcon_dsc_dpcd, 0, sizeof(intel_dp->pcon_dsc_dpcd));

	if (drm_dp_dpcd_read(&intel_dp->aux, DP_PCON_DSC_ENCODER,
			     intel_dp->pcon_dsc_dpcd,
			     sizeof(intel_dp->pcon_dsc_dpcd)) < 0)
		drm_err(&i915->drm, "Failed to read DPCD register 0x%x\n",
			DP_PCON_DSC_ENCODER);

	drm_dbg_kms(&i915->drm, "PCON ENCODER DSC DPCD: %*ph\n",
		    (int)sizeof(intel_dp->pcon_dsc_dpcd), intel_dp->pcon_dsc_dpcd);
}

static int intel_dp_pcon_get_frl_mask(u8 frl_bw_mask)
{
	int bw_gbps[] = {9, 18, 24, 32, 40, 48};
	int i;

	for (i = ARRAY_SIZE(bw_gbps) - 1; i >= 0; i--) {
		if (frl_bw_mask & (1 << i))
			return bw_gbps[i];
	}
	return 0;
}

static int intel_dp_pcon_set_frl_mask(int max_frl)
{
	switch (max_frl) {
	case 48:
		return DP_PCON_FRL_BW_MASK_48GBPS;
	case 40:
		return DP_PCON_FRL_BW_MASK_40GBPS;
	case 32:
		return DP_PCON_FRL_BW_MASK_32GBPS;
	case 24:
		return DP_PCON_FRL_BW_MASK_24GBPS;
	case 18:
		return DP_PCON_FRL_BW_MASK_18GBPS;
	case 9:
		return DP_PCON_FRL_BW_MASK_9GBPS;
	}

	return 0;
}

static int intel_dp_hdmi_sink_max_frl(struct intel_dp *intel_dp)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct drm_connector *connector = &intel_connector->base;
	int max_frl_rate;
	int max_lanes, rate_per_lane;
	int max_dsc_lanes, dsc_rate_per_lane;

	max_lanes = connector->display_info.hdmi.max_lanes;
	rate_per_lane = connector->display_info.hdmi.max_frl_rate_per_lane;
	max_frl_rate = max_lanes * rate_per_lane;

	if (connector->display_info.hdmi.dsc_cap.v_1p2) {
		max_dsc_lanes = connector->display_info.hdmi.dsc_cap.max_lanes;
		dsc_rate_per_lane = connector->display_info.hdmi.dsc_cap.max_frl_rate_per_lane;
		if (max_dsc_lanes && dsc_rate_per_lane)
			max_frl_rate = min(max_frl_rate, max_dsc_lanes * dsc_rate_per_lane);
	}

	return max_frl_rate;
}

static bool
intel_dp_pcon_is_frl_trained(struct intel_dp *intel_dp,
			     u8 max_frl_bw_mask, u8 *frl_trained_mask)
{
	if (drm_dp_pcon_hdmi_link_active(&intel_dp->aux) &&
	    drm_dp_pcon_hdmi_link_mode(&intel_dp->aux, frl_trained_mask) == DP_PCON_HDMI_MODE_FRL &&
	    *frl_trained_mask >= max_frl_bw_mask)
		return true;

	return false;
}

static int intel_dp_pcon_start_frl_training(struct intel_dp *intel_dp)
{
#define TIMEOUT_FRL_READY_MS 500
#define TIMEOUT_HDMI_LINK_ACTIVE_MS 1000

	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int max_frl_bw, max_pcon_frl_bw, max_edid_frl_bw, ret;
	u8 max_frl_bw_mask = 0, frl_trained_mask;
	bool is_active;

	max_pcon_frl_bw = intel_dp->dfp.pcon_max_frl_bw;
	drm_dbg(&i915->drm, "PCON max rate = %d Gbps\n", max_pcon_frl_bw);

	max_edid_frl_bw = intel_dp_hdmi_sink_max_frl(intel_dp);
	drm_dbg(&i915->drm, "Sink max rate from EDID = %d Gbps\n", max_edid_frl_bw);

	max_frl_bw = min(max_edid_frl_bw, max_pcon_frl_bw);

	if (max_frl_bw <= 0)
		return -EINVAL;

	max_frl_bw_mask = intel_dp_pcon_set_frl_mask(max_frl_bw);
	drm_dbg(&i915->drm, "MAX_FRL_BW_MASK = %u\n", max_frl_bw_mask);

	if (intel_dp_pcon_is_frl_trained(intel_dp, max_frl_bw_mask, &frl_trained_mask))
		goto frl_trained;

	ret = drm_dp_pcon_frl_prepare(&intel_dp->aux, false);
	if (ret < 0)
		return ret;
	/* Wait for PCON to be FRL Ready */
	wait_for(is_active = drm_dp_pcon_is_frl_ready(&intel_dp->aux) == true, TIMEOUT_FRL_READY_MS);

	if (!is_active)
		return -ETIMEDOUT;

	ret = drm_dp_pcon_frl_configure_1(&intel_dp->aux, max_frl_bw,
					  DP_PCON_ENABLE_SEQUENTIAL_LINK);
	if (ret < 0)
		return ret;
	ret = drm_dp_pcon_frl_configure_2(&intel_dp->aux, max_frl_bw_mask,
					  DP_PCON_FRL_LINK_TRAIN_NORMAL);
	if (ret < 0)
		return ret;
	ret = drm_dp_pcon_frl_enable(&intel_dp->aux);
	if (ret < 0)
		return ret;
	/*
	 * Wait for FRL to be completed
	 * Check if the HDMI Link is up and active.
	 */
	wait_for(is_active =
		 intel_dp_pcon_is_frl_trained(intel_dp, max_frl_bw_mask, &frl_trained_mask),
		 TIMEOUT_HDMI_LINK_ACTIVE_MS);

	if (!is_active)
		return -ETIMEDOUT;

frl_trained:
	drm_dbg(&i915->drm, "FRL_TRAINED_MASK = %u\n", frl_trained_mask);
	intel_dp->frl.trained_rate_gbps = intel_dp_pcon_get_frl_mask(frl_trained_mask);
	intel_dp->frl.is_trained = true;
	drm_dbg(&i915->drm, "FRL trained with : %d Gbps\n", intel_dp->frl.trained_rate_gbps);

	return 0;
}

static bool intel_dp_is_hdmi_2_1_sink(struct intel_dp *intel_dp)
{
	if (drm_dp_is_branch(intel_dp->dpcd) &&
	    intel_dp->has_hdmi_sink &&
	    intel_dp_hdmi_sink_max_frl(intel_dp) > 0)
		return true;

	return false;
}

static
int intel_dp_pcon_set_tmds_mode(struct intel_dp *intel_dp)
{
	int ret;
	u8 buf = 0;

	/* Set PCON source control mode */
	buf |= DP_PCON_ENABLE_SOURCE_CTL_MODE;

	ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_PCON_HDMI_LINK_CONFIG_1, buf);
	if (ret < 0)
		return ret;

	/* Set HDMI LINK ENABLE */
	buf |= DP_PCON_ENABLE_HDMI_LINK;
	ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_PCON_HDMI_LINK_CONFIG_1, buf);
	if (ret < 0)
		return ret;

	return 0;
}

void intel_dp_check_frl_training(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	/*
	 * Always go for FRL training if:
	 * -PCON supports SRC_CTL_MODE (VESA DP2.0-HDMI2.1 PCON Spec Draft-1 Sec-7)
	 * -sink is HDMI2.1
	 */
	if (!(intel_dp->downstream_ports[2] & DP_PCON_SOURCE_CTL_MODE) ||
	    !intel_dp_is_hdmi_2_1_sink(intel_dp) ||
	    intel_dp->frl.is_trained)
		return;

	if (intel_dp_pcon_start_frl_training(intel_dp) < 0) {
		int ret, mode;

		drm_dbg(&dev_priv->drm, "Couldn't set FRL mode, continuing with TMDS mode\n");
		ret = intel_dp_pcon_set_tmds_mode(intel_dp);
		mode = drm_dp_pcon_hdmi_link_mode(&intel_dp->aux, NULL);

		if (ret < 0 || mode != DP_PCON_HDMI_MODE_TMDS)
			drm_dbg(&dev_priv->drm, "Issue with PCON, cannot set TMDS mode\n");
	} else {
		drm_dbg(&dev_priv->drm, "FRL training Completed\n");
	}
}

static int
intel_dp_pcon_dsc_enc_slice_height(const struct intel_crtc_state *crtc_state)
{
	int vactive = crtc_state->hw.adjusted_mode.vdisplay;

	return intel_hdmi_dsc_get_slice_height(vactive);
}

static int
intel_dp_pcon_dsc_enc_slices(struct intel_dp *intel_dp,
			     const struct intel_crtc_state *crtc_state)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct drm_connector *connector = &intel_connector->base;
	int hdmi_throughput = connector->display_info.hdmi.dsc_cap.clk_per_slice;
	int hdmi_max_slices = connector->display_info.hdmi.dsc_cap.max_slices;
	int pcon_max_slices = drm_dp_pcon_dsc_max_slices(intel_dp->pcon_dsc_dpcd);
	int pcon_max_slice_width = drm_dp_pcon_dsc_max_slice_width(intel_dp->pcon_dsc_dpcd);

	return intel_hdmi_dsc_get_num_slices(crtc_state, pcon_max_slices,
					     pcon_max_slice_width,
					     hdmi_max_slices, hdmi_throughput);
}

static int
intel_dp_pcon_dsc_enc_bpp(struct intel_dp *intel_dp,
			  const struct intel_crtc_state *crtc_state,
			  int num_slices, int slice_width)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct drm_connector *connector = &intel_connector->base;
	int output_format = crtc_state->output_format;
	bool hdmi_all_bpp = connector->display_info.hdmi.dsc_cap.all_bpp;
	int pcon_fractional_bpp = drm_dp_pcon_dsc_bpp_incr(intel_dp->pcon_dsc_dpcd);
	int hdmi_max_chunk_bytes =
		connector->display_info.hdmi.dsc_cap.total_chunk_kbytes * 1024;

	return intel_hdmi_dsc_get_bpp(pcon_fractional_bpp, slice_width,
				      num_slices, output_format, hdmi_all_bpp,
				      hdmi_max_chunk_bytes);
}

void
intel_dp_pcon_dsc_configure(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state)
{
	u8 pps_param[6];
	int slice_height;
	int slice_width;
	int num_slices;
	int bits_per_pixel;
	int ret;
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct drm_connector *connector;
	bool hdmi_is_dsc_1_2;

	if (!intel_dp_is_hdmi_2_1_sink(intel_dp))
		return;

	if (!intel_connector)
		return;
	connector = &intel_connector->base;
	hdmi_is_dsc_1_2 = connector->display_info.hdmi.dsc_cap.v_1p2;

	if (!drm_dp_pcon_enc_is_dsc_1_2(intel_dp->pcon_dsc_dpcd) ||
	    !hdmi_is_dsc_1_2)
		return;

	slice_height = intel_dp_pcon_dsc_enc_slice_height(crtc_state);
	if (!slice_height)
		return;

	num_slices = intel_dp_pcon_dsc_enc_slices(intel_dp, crtc_state);
	if (!num_slices)
		return;

	slice_width = DIV_ROUND_UP(crtc_state->hw.adjusted_mode.hdisplay,
				   num_slices);

	bits_per_pixel = intel_dp_pcon_dsc_enc_bpp(intel_dp, crtc_state,
						   num_slices, slice_width);
	if (!bits_per_pixel)
		return;

	pps_param[0] = slice_height & 0xFF;
	pps_param[1] = slice_height >> 8;
	pps_param[2] = slice_width & 0xFF;
	pps_param[3] = slice_width >> 8;
	pps_param[4] = bits_per_pixel & 0xFF;
	pps_param[5] = (bits_per_pixel >> 8) & 0x3;

	ret = drm_dp_pcon_pps_override_param(&intel_dp->aux, pps_param);
	if (ret < 0)
		drm_dbg_kms(&i915->drm, "Failed to set pcon DSC\n");
}

void intel_dp_configure_protocol_converter(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state)
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
		drm_dbg_kms(&i915->drm, "Failed to %s protocol converter HDMI mode\n",
			    str_enable_disable(intel_dp->has_hdmi_sink));

	tmp = crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444 &&
		intel_dp->dfp.ycbcr_444_to_420 ? DP_CONVERSION_TO_YCBCR420_ENABLE : 0;

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_PROTOCOL_CONVERTER_CONTROL_1, tmp) != 1)
		drm_dbg_kms(&i915->drm,
			    "Failed to %s protocol converter YCbCr 4:2:0 conversion mode\n",
			    str_enable_disable(intel_dp->dfp.ycbcr_444_to_420));

	tmp = intel_dp->dfp.rgb_to_ycbcr ?
		DP_CONVERSION_BT709_RGB_YCBCR_ENABLE : 0;

	if (drm_dp_pcon_convert_rgb_to_ycbcr(&intel_dp->aux, tmp) < 0)
		drm_dbg_kms(&i915->drm,
			   "Failed to %s protocol converter RGB->YCbCr conversion mode\n",
			   str_enable_disable(tmp));
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

static void intel_edp_mso_mode_fixup(struct intel_connector *connector,
				     struct drm_display_mode *mode)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	int n = intel_dp->mso_link_count;
	int overlap = intel_dp->mso_pixel_overlap;

	if (!mode || !n)
		return;

	mode->hdisplay = (mode->hdisplay - overlap) * n;
	mode->hsync_start = (mode->hsync_start - overlap) * n;
	mode->hsync_end = (mode->hsync_end - overlap) * n;
	mode->htotal = (mode->htotal - overlap) * n;
	mode->clock *= n;

	drm_mode_set_name(mode);

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] using generated MSO mode: " DRM_MODE_FMT "\n",
		    connector->base.base.id, connector->base.name,
		    DRM_MODE_ARG(mode));
}

void intel_edp_fixup_vbt_bpp(struct intel_encoder *encoder, int pipe_bpp)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_connector *connector = intel_dp->attached_connector;

	if (connector->panel.vbt.edp.bpp && pipe_bpp > connector->panel.vbt.edp.bpp) {
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
			    pipe_bpp, connector->panel.vbt.edp.bpp);
		connector->panel.vbt.edp.bpp = pipe_bpp;
	}
}

static void intel_edp_mso_init(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	struct drm_display_info *info = &connector->base.display_info;
	u8 mso;

	if (intel_dp->edp_dpcd[0] < DP_EDP_14)
		return;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_EDP_MSO_LINK_CAPABILITIES, &mso) != 1) {
		drm_err(&i915->drm, "Failed to read MSO cap\n");
		return;
	}

	/* Valid configurations are SST or MSO 2x1, 2x2, 4x1 */
	mso &= DP_EDP_MSO_NUMBER_OF_LINKS_MASK;
	if (mso % 2 || mso > drm_dp_max_lane_count(intel_dp->dpcd)) {
		drm_err(&i915->drm, "Invalid MSO link count cap %u\n", mso);
		mso = 0;
	}

	if (mso) {
		drm_dbg_kms(&i915->drm, "Sink MSO %ux%u configuration, pixel overlap %u\n",
			    mso, drm_dp_max_lane_count(intel_dp->dpcd) / mso,
			    info->mso_pixel_overlap);
		if (!HAS_MSO(i915)) {
			drm_err(&i915->drm, "No source MSO support, disabling\n");
			mso = 0;
		}
	}

	intel_dp->mso_link_count = mso;
	intel_dp->mso_pixel_overlap = mso ? info->mso_pixel_overlap : 0;
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
			     sizeof(intel_dp->edp_dpcd)) {
		drm_dbg_kms(&dev_priv->drm, "eDP DPCD: %*ph\n",
			    (int)sizeof(intel_dp->edp_dpcd),
			    intel_dp->edp_dpcd);

		intel_dp->use_max_params = intel_dp->edp_dpcd[0] < DP_EDP_14;
	}

	/*
	 * This has to be called after intel_dp->edp_dpcd is filled, PSR checks
	 * for SET_POWER_CAPABLE bit in intel_dp->edp_dpcd[1]
	 */
	intel_psr_init_dpcd(intel_dp);

	/* Clear the default sink rates */
	intel_dp->num_sink_rates = 0;

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
	intel_dp_set_max_sink_lane_count(intel_dp);

	/* Read the eDP DSC DPCD registers */
	if (DISPLAY_VER(dev_priv) >= 10)
		intel_dp_get_dsc_sink_cap(intel_dp);

	/*
	 * If needed, program our source OUI so we can make various Intel-specific AUX services
	 * available (such as HDR backlight controls)
	 */
	intel_edp_init_source_oui(intel_dp, true);

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

	if (intel_dp_init_lttpr_and_dprx_caps(intel_dp) < 0)
		return false;

	/*
	 * Don't clobber cached eDP rates. Also skip re-reading
	 * the OUI/ID since we know it won't change.
	 */
	if (!intel_dp_is_edp(intel_dp)) {
		drm_dp_read_desc(&intel_dp->aux, &intel_dp->desc,
				 drm_dp_is_branch(intel_dp->dpcd));

		intel_dp_set_sink_rates(intel_dp);
		intel_dp_set_max_sink_lane_count(intel_dp);
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
		intel_dp_mst_source_support(intel_dp) &&
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
		    str_yes_no(intel_dp_mst_source_support(intel_dp)),
		    str_yes_no(sink_can_mst),
		    str_yes_no(i915->params.enable_dp_mst));

	if (!intel_dp_mst_source_support(intel_dp))
		return;

	intel_dp->is_mst = sink_can_mst &&
		i915->params.enable_dp_mst;

	drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
					intel_dp->is_mst);
}

static bool
intel_dp_get_sink_irq_esi(struct intel_dp *intel_dp, u8 *esi)
{
	return drm_dp_dpcd_read(&intel_dp->aux, DP_SINK_COUNT_ESI, esi, 4) == 4;
}

static bool intel_dp_ack_sink_irq_esi(struct intel_dp *intel_dp, u8 esi[4])
{
	int retry;

	for (retry = 0; retry < 3; retry++) {
		if (drm_dp_dpcd_write(&intel_dp->aux, DP_SINK_COUNT_ESI + 1,
				      &esi[1], 3) == 3)
			return true;
	}

	return false;
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
intel_dp_hdr_metadata_infoframe_sdp_pack(struct drm_i915_private *i915,
					 const struct hdmi_drm_infoframe *drm_infoframe,
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
		drm_dbg_kms(&i915->drm, "buffer size is smaller than hdr metadata infoframe\n");
		return -ENOSPC;
	}

	if (len != infoframe_size) {
		drm_dbg_kms(&i915->drm, "wrong static hdr metadata size\n");
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
		len = intel_dp_hdr_metadata_infoframe_sdp_pack(dev_priv,
							       &crtc_state->infoframes.drm.drm,
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
			    const struct drm_dp_vsc_sdp *vsc)
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
	i915_reg_t reg = HSW_TVIDEO_DIP_CTL(crtc_state->cpu_transcoder);
	u32 dip_enable = VIDEO_DIP_ENABLE_AVI_HSW | VIDEO_DIP_ENABLE_GCP_HSW |
			 VIDEO_DIP_ENABLE_VS_HSW | VIDEO_DIP_ENABLE_GMP_HSW |
			 VIDEO_DIP_ENABLE_SPD_HSW | VIDEO_DIP_ENABLE_DRM_GLK;
	u32 val = intel_de_read(dev_priv, reg) & ~dip_enable;

	/* TODO: Add DSC case (DIP_ENABLE_PPS) */
	/* When PSR is enabled, this routine doesn't disable VSC DIP */
	if (!crtc_state->has_psr)
		val &= ~VIDEO_DIP_ENABLE_VSC_HSW;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	if (!enable)
		return;

	/* When PSR is enabled, VSC SDP is handled by PSR routine */
	if (!crtc_state->has_psr)
		intel_write_dp_sdp(encoder, crtc_state, DP_SDP_VSC);

	intel_write_dp_sdp(encoder, crtc_state, HDMI_PACKET_TYPE_GAMUT_METADATA);
}

static int intel_dp_vsc_sdp_unpack(struct drm_dp_vsc_sdp *vsc,
				   const void *buffer, size_t size)
{
	const struct dp_sdp *sdp = buffer;

	if (size < sizeof(struct dp_sdp))
		return -EINVAL;

	memset(vsc, 0, sizeof(*vsc));

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
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	unsigned int type = DP_SDP_VSC;
	struct dp_sdp sdp = {};
	int ret;

	/* When PSR is enabled, VSC SDP is handled by PSR routine */
	if (crtc_state->has_psr)
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

static void intel_dp_phy_pattern_update(struct intel_dp *intel_dp,
					const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv =
			to_i915(dp_to_dig_port(intel_dp)->base.base.dev);
	struct drm_dp_phy_test_params *data =
			&intel_dp->compliance.test_data.phytest;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	u32 pattern_val;

	switch (data->phy_pattern) {
	case DP_PHY_TEST_PATTERN_NONE:
		drm_dbg_kms(&dev_priv->drm, "Disable Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe), 0x0);
		break;
	case DP_PHY_TEST_PATTERN_D10_2:
		drm_dbg_kms(&dev_priv->drm, "Set D10.2 Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_D10_2);
		break;
	case DP_PHY_TEST_PATTERN_ERROR_COUNT:
		drm_dbg_kms(&dev_priv->drm, "Set Error Count Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE |
			       DDI_DP_COMP_CTL_SCRAMBLED_0);
		break;
	case DP_PHY_TEST_PATTERN_PRBS7:
		drm_dbg_kms(&dev_priv->drm, "Set PRBS7 Phy Test Pattern\n");
		intel_de_write(dev_priv, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_PRBS7);
		break;
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
		/*
		 * FIXME: Ideally pattern should come from DPCD 0x250. As
		 * current firmware of DPR-100 could not set it, so hardcoding
		 * now for complaince test.
		 */
		drm_dbg_kms(&dev_priv->drm,
			    "Set 80Bit Custom Phy Test Pattern 0x3e0f83e0 0x0f83e0f8 0x0000f83e\n");
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
		drm_dbg_kms(&dev_priv->drm, "Set HBR2 compliance Phy Test Pattern\n");
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
intel_dp_autotest_phy_ddi_disable(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state)
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
intel_dp_autotest_phy_ddi_enable(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
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

static void intel_dp_process_phy_request(struct intel_dp *intel_dp,
					 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct drm_dp_phy_test_params *data =
		&intel_dp->compliance.test_data.phytest;
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (drm_dp_dpcd_read_phy_link_status(&intel_dp->aux, DP_PHY_DPRX,
					     link_status) < 0) {
		drm_dbg_kms(&i915->drm, "failed to get link status\n");
		return;
	}

	/* retrieve vswing & pre-emphasis setting */
	intel_dp_get_adjust_train(intel_dp, crtc_state, DP_PHY_DPRX,
				  link_status);

	intel_dp_autotest_phy_ddi_disable(intel_dp, crtc_state);

	intel_dp_set_signal_levels(intel_dp, crtc_state, DP_PHY_DPRX);

	intel_dp_phy_pattern_update(intel_dp, crtc_state);

	intel_dp_autotest_phy_ddi_enable(intel_dp, crtc_state);

	drm_dp_dpcd_write(&intel_dp->aux, DP_TRAINING_LANE0_SET,
			  intel_dp->train_set, crtc_state->lane_count);

	drm_dp_set_phy_test_pattern(&intel_dp->aux, data,
				    link_status[DP_DPCD_REV]);
}

static u8 intel_dp_autotest_phy_pattern(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct drm_dp_phy_test_params *data =
		&intel_dp->compliance.test_data.phytest;

	if (drm_dp_get_phy_test_pattern(&intel_dp->aux, data)) {
		drm_dbg_kms(&i915->drm, "DP Phy Test pattern AUX read failure\n");
		return DP_TEST_NAK;
	}

	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance.test_active = true;

	return DP_TEST_ACK;
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

static bool intel_dp_link_ok(struct intel_dp *intel_dp,
			     u8 link_status[DP_LINK_STATUS_SIZE])
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	bool uhbr = intel_dp->link_rate >= 1000000;
	bool ok;

	if (uhbr)
		ok = drm_dp_128b132b_lane_channel_eq_done(link_status,
							  intel_dp->lane_count);
	else
		ok = drm_dp_channel_eq_ok(link_status, intel_dp->lane_count);

	if (ok)
		return true;

	intel_dp_dump_link_status(intel_dp, DP_PHY_DPRX, link_status);
	drm_dbg_kms(&i915->drm,
		    "[ENCODER:%d:%s] %s link not ok, retraining\n",
		    encoder->base.base.id, encoder->base.name,
		    uhbr ? "128b/132b" : "8b/10b");

	return false;
}

static void
intel_dp_mst_hpd_irq(struct intel_dp *intel_dp, u8 *esi, u8 *ack)
{
	bool handled = false;

	drm_dp_mst_hpd_irq(&intel_dp->mst_mgr, esi, &handled);
	if (handled)
		ack[1] |= esi[1] & (DP_DOWN_REP_MSG_RDY | DP_UP_REQ_MSG_RDY);

	if (esi[1] & DP_CP_IRQ) {
		intel_hdcp_handle_cp_irq(intel_dp->attached_connector);
		ack[1] |= DP_CP_IRQ;
	}
}

static bool intel_dp_mst_link_status(struct intel_dp *intel_dp)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u8 link_status[DP_LINK_STATUS_SIZE] = {};
	const size_t esi_link_status_size = DP_LINK_STATUS_SIZE - 2;

	if (drm_dp_dpcd_read(&intel_dp->aux, DP_LANE0_1_STATUS_ESI, link_status,
			     esi_link_status_size) != esi_link_status_size) {
		drm_err(&i915->drm,
			"[ENCODER:%d:%s] Failed to read link status\n",
			encoder->base.base.id, encoder->base.name);
		return false;
	}

	return intel_dp_link_ok(intel_dp, link_status);
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
		u8 esi[4] = {};
		u8 ack[4] = {};

		if (!intel_dp_get_sink_irq_esi(intel_dp, esi)) {
			drm_dbg_kms(&i915->drm,
				    "failed to get ESI - device may have failed\n");
			link_ok = false;

			break;
		}

		drm_dbg_kms(&i915->drm, "DPRX ESI: %4ph\n", esi);

		if (intel_dp->active_mst_links > 0 && link_ok &&
		    esi[3] & LINK_STATUS_CHANGED) {
			if (!intel_dp_mst_link_status(intel_dp))
				link_ok = false;
			ack[3] |= LINK_STATUS_CHANGED;
		}

		intel_dp_mst_hpd_irq(intel_dp, esi, ack);

		if (!memchr_inv(ack, 0, sizeof(ack)))
			break;

		if (!intel_dp_ack_sink_irq_esi(intel_dp, ack))
			drm_dbg_kms(&i915->drm, "Failed to ack ESI\n");
	}

	return link_ok;
}

static void
intel_dp_handle_hdmi_link_status_change(struct intel_dp *intel_dp)
{
	bool is_active;
	u8 buf = 0;

	is_active = drm_dp_pcon_hdmi_link_active(&intel_dp->aux);
	if (intel_dp->frl.is_trained && !is_active) {
		if (drm_dp_dpcd_readb(&intel_dp->aux, DP_PCON_HDMI_LINK_CONFIG_1, &buf) < 0)
			return;

		buf &=  ~DP_PCON_ENABLE_HDMI_LINK;
		if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_PCON_HDMI_LINK_CONFIG_1, buf) < 0)
			return;

		drm_dp_pcon_hdmi_frl_link_error_count(&intel_dp->aux, &intel_dp->attached_connector->base);

		/* Restart FRL training or fall back to TMDS mode */
		intel_dp_check_frl_training(intel_dp);
	}
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

	if (drm_dp_dpcd_read_phy_link_status(&intel_dp->aux, DP_PHY_DPRX,
					     link_status) < 0)
		return false;

	/*
	 * Validate the cached values of intel_dp->link_rate and
	 * intel_dp->lane_count before attempting to retrain.
	 *
	 * FIXME would be nice to user the crtc state here, but since
	 * we need to call this from the short HPD handler that seems
	 * a bit hard.
	 */
	if (!intel_dp_link_params_valid(intel_dp, intel_dp->link_rate,
					intel_dp->lane_count))
		return false;

	/* Retrain if link not ok */
	return !intel_dp_link_ok(intel_dp, link_status);
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
				      u8 *pipe_mask)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	int ret = 0;

	*pipe_mask = 0;

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

		*pipe_mask |= BIT(crtc->pipe);
	}
	drm_connector_list_iter_end(&conn_iter);

	if (!intel_dp_needs_link_retrain(intel_dp))
		*pipe_mask = 0;

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
	u8 pipe_mask;
	int ret;

	if (!intel_dp_is_connected(intel_dp))
		return 0;

	ret = drm_modeset_lock(&dev_priv->drm.mode_config.connection_mutex,
			       ctx);
	if (ret)
		return ret;

	ret = intel_dp_prep_link_retrain(intel_dp, ctx, &pipe_mask);
	if (ret)
		return ret;

	if (pipe_mask == 0)
		return 0;

	drm_dbg_kms(&dev_priv->drm, "[ENCODER:%d:%s] retraining link\n",
		    encoder->base.base.id, encoder->base.name);

	for_each_intel_crtc_in_pipe_mask(&dev_priv->drm, crtc, pipe_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		/* Suppress underruns caused by re-training */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, crtc->pipe, false);
		if (crtc_state->has_pch_encoder)
			intel_set_pch_fifo_underrun_reporting(dev_priv,
							      intel_crtc_pch_transcoder(crtc), false);
	}

	for_each_intel_crtc_in_pipe_mask(&dev_priv->drm, crtc, pipe_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		/* retrain on the MST master transcoder */
		if (DISPLAY_VER(dev_priv) >= 12 &&
		    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST) &&
		    !intel_dp_mst_is_master_trans(crtc_state))
			continue;

		intel_dp_check_frl_training(intel_dp);
		intel_dp_pcon_dsc_configure(intel_dp, crtc_state);
		intel_dp_start_link_train(intel_dp, crtc_state);
		intel_dp_stop_link_train(intel_dp, crtc_state);
		break;
	}

	for_each_intel_crtc_in_pipe_mask(&dev_priv->drm, crtc, pipe_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		/* Keep underrun reporting disabled until things are stable */
		intel_crtc_wait_for_next_vblank(crtc);

		intel_set_cpu_fifo_underrun_reporting(dev_priv, crtc->pipe, true);
		if (crtc_state->has_pch_encoder)
			intel_set_pch_fifo_underrun_reporting(dev_priv,
							      intel_crtc_pch_transcoder(crtc), true);
	}

	return 0;
}

static int intel_dp_prep_phy_test(struct intel_dp *intel_dp,
				  struct drm_modeset_acquire_ctx *ctx,
				  u8 *pipe_mask)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	int ret = 0;

	*pipe_mask = 0;

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

		*pipe_mask |= BIT(crtc->pipe);
	}
	drm_connector_list_iter_end(&conn_iter);

	return ret;
}

static int intel_dp_do_phy_test(struct intel_encoder *encoder,
				struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc;
	u8 pipe_mask;
	int ret;

	ret = drm_modeset_lock(&dev_priv->drm.mode_config.connection_mutex,
			       ctx);
	if (ret)
		return ret;

	ret = intel_dp_prep_phy_test(intel_dp, ctx, &pipe_mask);
	if (ret)
		return ret;

	if (pipe_mask == 0)
		return 0;

	drm_dbg_kms(&dev_priv->drm, "[ENCODER:%d:%s] PHY test\n",
		    encoder->base.base.id, encoder->base.name);

	for_each_intel_crtc_in_pipe_mask(&dev_priv->drm, crtc, pipe_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		/* test on the MST master transcoder */
		if (DISPLAY_VER(dev_priv) >= 12 &&
		    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST) &&
		    !intel_dp_mst_is_master_trans(crtc_state))
			continue;

		intel_dp_process_phy_request(intel_dp, crtc_state);
		break;
	}

	return 0;
}

void intel_dp_phy_test(struct intel_encoder *encoder)
{
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	drm_modeset_acquire_init(&ctx, 0);

	for (;;) {
		ret = intel_dp_do_phy_test(encoder, &ctx);

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
}

static void intel_dp_check_device_service_irq(struct intel_dp *intel_dp)
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

static void intel_dp_check_link_service_irq(struct intel_dp *intel_dp)
{
	u8 val;

	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (drm_dp_dpcd_readb(&intel_dp->aux,
			      DP_LINK_SERVICE_IRQ_VECTOR_ESI0, &val) != 1 || !val)
		return;

	if (drm_dp_dpcd_writeb(&intel_dp->aux,
			       DP_LINK_SERVICE_IRQ_VECTOR_ESI0, val) != 1)
		return;

	if (val & HDMI_LINK_STATUS_CHANGED)
		intel_dp_handle_hdmi_link_status_change(intel_dp);
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

	intel_dp_check_device_service_irq(intel_dp);
	intel_dp_check_link_service_irq(intel_dp);

	/* Handle CEC interrupts, if any */
	drm_dp_cec_irq(&intel_dp->aux);

	/* defer to the hotplug work for link retraining if needed */
	if (intel_dp_needs_link_retrain(intel_dp))
		return false;

	intel_psr_short_pulse(intel_dp);

	switch (intel_dp->compliance.test_type) {
	case DP_TEST_LINK_TRAINING:
		drm_dbg_kms(&dev_priv->drm,
			    "Link Training Compliance Test requested\n");
		/* Send a Hotplug Uevent to userspace to start modeset */
		drm_kms_helper_hotplug_event(&dev_priv->drm);
		break;
	case DP_TEST_LINK_PHY_TEST_PATTERN:
		drm_dbg_kms(&dev_priv->drm,
			    "PHY test pattern Compliance Test requested\n");
		/*
		 * Schedule long hpd to do the test
		 *
		 * FIXME get rid of the ad-hoc phy test modeset code
		 * and properly incorporate it into the normal modeset.
		 */
		return false;
	}

	return true;
}

/* XXX this is probably wrong for multiple downstream ports */
static enum drm_connector_status
intel_dp_detect_dpcd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	u8 *dpcd = intel_dp->dpcd;
	u8 type;

	if (drm_WARN_ON(&i915->drm, intel_dp_is_edp(intel_dp)))
		return connector_status_connected;

	lspcon_resume(dig_port);

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

	intel_dp->dfp.pcon_max_frl_bw =
		drm_dp_get_pcon_max_frl_bw(intel_dp->dpcd,
					   intel_dp->downstream_ports);

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] DFP max bpc %d, max dotclock %d, TMDS clock %d-%d, PCON Max FRL BW %dGbps\n",
		    connector->base.base.id, connector->base.name,
		    intel_dp->dfp.max_bpc,
		    intel_dp->dfp.max_dotclock,
		    intel_dp->dfp.min_tmds_clock,
		    intel_dp->dfp.max_tmds_clock,
		    intel_dp->dfp.pcon_max_frl_bw);

	intel_dp_get_pcon_dsc_cap(intel_dp);
}

static void
intel_dp_update_420(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	bool is_branch, ycbcr_420_passthrough, ycbcr_444_to_420, rgb_to_ycbcr;

	/* No YCbCr output support on gmch platforms */
	if (HAS_GMCH(i915))
		return;

	/*
	 * ILK doesn't seem capable of DP YCbCr output. The
	 * displayed image is severly corrupted. SNB+ is fine.
	 */
	if (IS_IRONLAKE(i915))
		return;

	is_branch = drm_dp_is_branch(intel_dp->dpcd);
	ycbcr_420_passthrough =
		drm_dp_downstream_420_passthrough(intel_dp->dpcd,
						  intel_dp->downstream_ports);
	/* on-board LSPCON always assumed to support 4:4:4->4:2:0 conversion */
	ycbcr_444_to_420 =
		dp_to_dig_port(intel_dp)->lspcon.active ||
		drm_dp_downstream_444_to_420_conversion(intel_dp->dpcd,
							intel_dp->downstream_ports);
	rgb_to_ycbcr = drm_dp_downstream_rgb_to_ycbcr_conversion(intel_dp->dpcd,
								 intel_dp->downstream_ports,
								 DP_DS_HDMI_BT709_RGB_YCBCR_CONV);

	if (DISPLAY_VER(i915) >= 11) {
		/* Let PCON convert from RGB->YCbCr if possible */
		if (is_branch && rgb_to_ycbcr && ycbcr_444_to_420) {
			intel_dp->dfp.rgb_to_ycbcr = true;
			intel_dp->dfp.ycbcr_444_to_420 = true;
			connector->base.ycbcr_420_allowed = true;
		} else {
		/* Prefer 4:2:0 passthrough over 4:4:4->4:2:0 conversion */
			intel_dp->dfp.ycbcr_444_to_420 =
				ycbcr_444_to_420 && !ycbcr_420_passthrough;

			connector->base.ycbcr_420_allowed =
				!is_branch || ycbcr_444_to_420 || ycbcr_420_passthrough;
		}
	} else {
		/* 4:4:4->4:2:0 conversion is the only way */
		intel_dp->dfp.ycbcr_444_to_420 = ycbcr_444_to_420;

		connector->base.ycbcr_420_allowed = ycbcr_444_to_420;
	}

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] RGB->YcbCr conversion? %s, YCbCr 4:2:0 allowed? %s, YCbCr 4:4:4->4:2:0 conversion? %s\n",
		    connector->base.base.id, connector->base.name,
		    str_yes_no(intel_dp->dfp.rgb_to_ycbcr),
		    str_yes_no(connector->base.ycbcr_420_allowed),
		    str_yes_no(intel_dp->dfp.ycbcr_444_to_420));
}

static void
intel_dp_set_edid(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	struct edid *edid;
	bool vrr_capable;

	intel_dp_unset_edid(intel_dp);
	edid = intel_dp_get_edid(intel_dp);
	connector->detect_edid = edid;

	vrr_capable = intel_vrr_is_capable(connector);
	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s] VRR capable: %s\n",
		    connector->base.base.id, connector->base.name, str_yes_no(vrr_capable));
	drm_connector_set_vrr_capable_property(&connector->base, vrr_capable);

	intel_dp_update_dfp(intel_dp, edid);
	intel_dp_update_420(intel_dp);

	if (edid && edid->input & DRM_EDID_INPUT_DIGITAL) {
		intel_dp->has_hdmi_sink = drm_detect_hdmi_monitor(edid);
		intel_dp->has_audio = drm_detect_monitor_audio(edid);
	}

	drm_dp_cec_set_edid(&intel_dp->aux, edid);
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

	intel_dp->dfp.max_bpc = 0;
	intel_dp->dfp.max_dotclock = 0;
	intel_dp->dfp.min_tmds_clock = 0;
	intel_dp->dfp.max_tmds_clock = 0;

	intel_dp->dfp.pcon_max_frl_bw = 0;

	intel_dp->dfp.ycbcr_444_to_420 = false;
	connector->base.ycbcr_420_allowed = false;

	drm_connector_set_vrr_capable_property(&connector->base,
					       false);
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
	if (DISPLAY_VER(dev_priv) >= 11)
		intel_dp_get_dsc_sink_cap(intel_dp);

	intel_dp_configure_mst(intel_dp);

	/*
	 * TODO: Reset link params when switching to MST mode, until MST
	 * supports link training fallback params.
	 */
	if (intel_dp->reset_link_params || intel_dp->is_mst) {
		intel_dp_reset_max_link_params(intel_dp);
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

	intel_dp_check_device_service_irq(intel_dp);

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
	int num_modes = 0;

	edid = intel_connector->detect_edid;
	if (edid)
		num_modes = intel_connector_update_modes(connector, edid);

	/* Also add fixed mode, which may or may not be present in EDID */
	if (intel_dp_is_edp(intel_attached_dp(intel_connector)))
		num_modes += intel_panel_get_modes(intel_connector);

	if (num_modes)
		return num_modes;

	if (!edid) {
		struct intel_dp *intel_dp = intel_attached_dp(intel_connector);
		struct drm_display_mode *mode;

		mode = drm_dp_downstream_mode(connector->dev,
					      intel_dp->dpcd,
					      intel_dp->downstream_ports);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}

	return num_modes;
}

static int
intel_dp_connector_register(struct drm_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_dp *intel_dp = intel_attached_dp(to_intel_connector(connector));
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_lspcon *lspcon = &dig_port->lspcon;
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

	if (!intel_bios_is_lspcon_present(i915, dig_port->base.port))
		return ret;

	/*
	 * ToDo: Clean this up to handle lspcon init and resume more
	 * efficiently and streamlined.
	 */
	if (lspcon_init(dig_port)) {
		lspcon_detect_hdr_capability(lspcon);
		if (lspcon->hdr_supported)
			drm_connector_attach_hdr_output_metadata_property(connector);
	}

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

	intel_pps_vdd_off_sync(intel_dp);

	intel_dp_aux_fini(intel_dp);
}

void intel_dp_encoder_suspend(struct intel_encoder *intel_encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(intel_encoder);

	intel_pps_vdd_off_sync(intel_dp);
}

void intel_dp_encoder_shutdown(struct intel_encoder *intel_encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(intel_encoder);

	intel_pps_wait_power_cycle(intel_dp);
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
	if (DISPLAY_VER(dev_priv) < 9)
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

static void intel_dp_oob_hotplug_event(struct drm_connector *connector)
{
	struct intel_encoder *encoder = intel_attached_encoder(to_intel_connector(connector));
	struct drm_i915_private *i915 = to_i915(connector->dev);

	spin_lock_irq(&i915->irq_lock);
	i915->hotplug.event_bits |= BIT(encoder->hpd_pin);
	spin_unlock_irq(&i915->irq_lock);
	queue_delayed_work(system_wq, &i915->hotplug.hotplug_work, 0);
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
	.oob_hotplug_event = intel_dp_oob_hotplug_event,
};

static const struct drm_connector_helper_funcs intel_dp_connector_helper_funcs = {
	.detect_ctx = intel_dp_detect,
	.get_modes = intel_dp_get_modes,
	.mode_valid = intel_dp_mode_valid,
	.atomic_check = intel_dp_connector_atomic_check,
};

enum irqreturn
intel_dp_hpd_pulse(struct intel_digital_port *dig_port, bool long_hpd)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_dp *intel_dp = &dig_port->dp;

	if (dig_port->base.type == INTEL_OUTPUT_EDP &&
	    (long_hpd || !intel_pps_have_panel_power_or_vdd(intel_dp))) {
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
	if (DISPLAY_VER(dev_priv) < 5)
		return false;

	if (DISPLAY_VER(dev_priv) < 9 && port == PORT_A)
		return true;

	return intel_bios_is_port_edp(dev_priv, port);
}

static bool
has_gamut_metadata_dip(struct drm_i915_private *i915, enum port port)
{
	if (intel_bios_is_lspcon_present(i915, port))
		return false;

	if (DISPLAY_VER(i915) >= 11)
		return true;

	if (port == PORT_A)
		return false;

	if (IS_HASWELL(i915) || IS_BROADWELL(i915) ||
	    DISPLAY_VER(i915) >= 9)
		return true;

	return false;
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
	else if (DISPLAY_VER(dev_priv) >= 5)
		drm_connector_attach_max_bpc_property(connector, 6, 12);

	/* Register HDMI colorspace for case of lspcon */
	if (intel_bios_is_lspcon_present(dev_priv, port)) {
		drm_connector_attach_content_type_property(connector);
		intel_attach_hdmi_colorspace_property(connector);
	} else {
		intel_attach_dp_colorspace_property(connector);
	}

	if (has_gamut_metadata_dip(dev_priv, port))
		drm_connector_attach_hdr_output_metadata_property(connector);

	if (intel_dp_is_edp(intel_dp)) {
		u32 allowed_scalers;

		allowed_scalers = BIT(DRM_MODE_SCALE_ASPECT) | BIT(DRM_MODE_SCALE_FULLSCREEN);
		if (!HAS_GMCH(dev_priv))
			allowed_scalers |= BIT(DRM_MODE_SCALE_CENTER);

		drm_connector_attach_scaling_mode_property(connector, allowed_scalers);

		connector->state->scaling_mode = DRM_MODE_SCALE_ASPECT;

	}

	if (HAS_VRR(dev_priv))
		drm_connector_attach_vrr_capable_property(connector);
}

static void
intel_edp_add_properties(struct intel_dp *intel_dp)
{
	struct intel_connector *connector = intel_dp->attached_connector;
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *fixed_mode =
		intel_panel_preferred_fixed_mode(connector);

	if (!fixed_mode)
		return;

	drm_connector_set_panel_orientation_with_quirk(&connector->base,
						       i915->vbt.orientation,
						       fixed_mode->hdisplay,
						       fixed_mode->vdisplay);
}

static bool intel_edp_init_connector(struct intel_dp *intel_dp,
				     struct intel_connector *intel_connector)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector = &intel_connector->base;
	struct drm_display_mode *fixed_mode;
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	bool has_dpcd;
	enum pipe pipe = INVALID_PIPE;
	struct edid *edid;

	if (!intel_dp_is_edp(intel_dp))
		return true;

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

	intel_pps_init(intel_dp);

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
	if (!edid) {
		/* Fallback to EDID from ACPI OpRegion, if any */
		edid = intel_opregion_get_edid(intel_connector);
		if (edid)
			drm_dbg_kms(&dev_priv->drm,
				    "[CONNECTOR:%d:%s] Using OpRegion EDID\n",
				    connector->base.id, connector->name);
	}
	if (edid) {
		if (drm_add_edid_modes(connector, edid)) {
			drm_connector_update_edid_property(connector, edid);
		} else {
			kfree(edid);
			edid = ERR_PTR(-EINVAL);
		}
	} else {
		edid = ERR_PTR(-ENOENT);
	}
	intel_connector->edid = edid;

	intel_bios_init_panel(dev_priv, &intel_connector->panel,
			      encoder->devdata, IS_ERR(edid) ? NULL : edid);

	intel_panel_add_edid_fixed_modes(intel_connector,
					 intel_connector->panel.vbt.drrs_type != DRRS_TYPE_NONE,
					 intel_vrr_is_capable(intel_connector));

	/* MSO requires information from the EDID */
	intel_edp_mso_init(intel_dp);

	/* multiply the mode clock and horizontal timings for MSO */
	list_for_each_entry(fixed_mode, &intel_connector->panel.fixed_modes, head)
		intel_edp_mso_mode_fixup(intel_connector, fixed_mode);

	/* fallback to VBT if available for eDP */
	if (!intel_panel_preferred_fixed_mode(intel_connector))
		intel_panel_add_vbt_lfp_fixed_mode(intel_connector);

	mutex_unlock(&dev->mode_config.mutex);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		/*
		 * Figure out the current pipe for the initial backlight setup.
		 * If the current pipe isn't valid, try the PPS pipe, and if that
		 * fails just assume pipe A.
		 */
		pipe = vlv_active_pipe(intel_dp);

		if (pipe != PIPE_A && pipe != PIPE_B)
			pipe = intel_dp->pps.pps_pipe;

		if (pipe != PIPE_A && pipe != PIPE_B)
			pipe = PIPE_A;

		drm_dbg_kms(&dev_priv->drm,
			    "using pipe %c for initial backlight setup\n",
			    pipe_name(pipe));
	}

	intel_panel_init(intel_connector);

	intel_backlight_setup(intel_connector, pipe);

	intel_edp_add_properties(intel_dp);

	intel_pps_init_late(intel_dp);

	return true;

out_vdd_off:
	intel_pps_vdd_off_sync(intel_dp);

	return false;
}

static void intel_dp_modeset_retry_work_fn(struct work_struct *work)
{
	struct intel_connector *intel_connector;
	struct drm_connector *connector;

	intel_connector = container_of(work, typeof(*intel_connector),
				       modeset_retry_work);
	connector = &intel_connector->base;
	drm_dbg_kms(connector->dev, "[CONNECTOR:%d:%s]\n", connector->base.id,
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
	drm_kms_helper_connector_hotplug_event(connector);
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

	intel_dp->reset_link_params = true;
	intel_dp->pps.pps_pipe = INVALID_PIPE;
	intel_dp->pps.active_pipe = INVALID_PIPE;

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
		intel_encoder->type = INTEL_OUTPUT_EDP;

		/* eDP only on port B and/or C on vlv/chv */
		if (drm_WARN_ON(dev, (IS_VALLEYVIEW(dev_priv) ||
				      IS_CHERRYVIEW(dev_priv)) &&
				port != PORT_B && port != PORT_C))
			return false;
	} else {
		type = DRM_MODE_CONNECTOR_DisplayPort;
	}

	intel_dp_set_default_sink_rates(intel_dp);
	intel_dp_set_default_max_sink_lane_count(intel_dp);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		intel_dp->pps.active_pipe = vlv_active_pipe(intel_dp);

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

	if (!intel_edp_init_connector(intel_dp, intel_connector)) {
		intel_dp_aux_fini(intel_dp);
		goto fail;
	}

	intel_dp_set_source_rates(intel_dp);
	intel_dp_set_common_rates(intel_dp);
	intel_dp_reset_max_link_params(intel_dp);

	/* init MST on ports that can support it */
	intel_dp_mst_encoder_init(dig_port,
				  intel_connector->base.base.id);

	intel_dp_add_properties(intel_dp, connector);

	if (is_hdcp_supported(dev_priv, port) && !intel_dp_is_edp(intel_dp)) {
		int ret = intel_dp_hdcp_init(dig_port, intel_connector);
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

	intel_dp->frl.is_trained = false;
	intel_dp->frl.trained_rate_gbps = 0;

	intel_psr_init(intel_dp);

	return true;

fail:
	drm_connector_cleanup(connector);

	return false;
}

void intel_dp_mst_suspend(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(dev_priv))
		return;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp;

		if (encoder->type != INTEL_OUTPUT_DDI)
			continue;

		intel_dp = enc_to_intel_dp(encoder);

		if (!intel_dp_mst_source_support(intel_dp))
			continue;

		if (intel_dp->is_mst)
			drm_dp_mst_topology_mgr_suspend(&intel_dp->mst_mgr);
	}
}

void intel_dp_mst_resume(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	if (!HAS_DISPLAY(dev_priv))
		return;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp;
		int ret;

		if (encoder->type != INTEL_OUTPUT_DDI)
			continue;

		intel_dp = enc_to_intel_dp(encoder);

		if (!intel_dp_mst_source_support(intel_dp))
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
