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
#include <drm/drm_hdcp.h>
#include <drm/drm_probe_helper.h>
#include <drm/i915_drm.h>

#include "i915_debugfs.h"
#include "i915_drv.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_ddi.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dpio_phy.h"
#include "intel_drv.h"
#include "intel_fifo_underrun.h"
#include "intel_hdcp.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_lspcon.h"
#include "intel_lvds.h"
#include "intel_panel.h"
#include "intel_psr.h"
#include "intel_sideband.h"
#include "intel_vdsc.h"

#define DP_DPRX_ESI_LEN 14

/* DP DSC small joiner has 2 FIFOs each of 640 x 6 bytes */
#define DP_DSC_MAX_SMALL_JOINER_RAM_BUFFER	61440
#define DP_DSC_MIN_SUPPORTED_BPC		8
#define DP_DSC_MAX_SUPPORTED_BPC		10

/* DP DSC throughput values used for slice count calculations KPixels/s */
#define DP_DSC_PEAK_PIXEL_RATE			2720000
#define DP_DSC_MAX_ENC_THROUGHPUT_0		340000
#define DP_DSC_MAX_ENC_THROUGHPUT_1		400000

/* DP DSC FEC Overhead factor = (100 - 2.4)/100 */
#define DP_DSC_FEC_OVERHEAD_FACTOR		976

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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);

	return intel_dig_port->base.type == INTEL_OUTPUT_EDP;
}

static struct intel_dp *intel_attached_dp(struct drm_connector *connector)
{
	return enc_to_intel_dp(&intel_attached_encoder(connector)->base);
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

static int intel_dp_get_fia_supported_lane_count(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	intel_wakeref_t wakeref;
	u32 lane_info;

	if (tc_port == PORT_TC_NONE || dig_port->tc_type != TC_PORT_TYPEC)
		return 4;

	lane_info = 0;
	with_intel_display_power(dev_priv, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		lane_info = (I915_READ(PORT_TX_DFLEXDPSP) &
			     DP_LANE_ASSIGNMENT_MASK(tc_port)) >>
				DP_LANE_ASSIGNMENT_SHIFT(tc_port);

	switch (lane_info) {
	default:
		MISSING_CASE(lane_info);
		/* fall through */
	case 1:
	case 2:
	case 4:
	case 8:
		return 1;
	case 3:
	case 12:
		return 2;
	case 15:
		return 4;
	}
}

/* Theoretical max between source and sink */
static int intel_dp_max_common_lane_count(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	int source_max = intel_dig_port->max_lanes;
	int sink_max = drm_dp_max_lane_count(intel_dp->dpcd);
	int fia_max = intel_dp_get_fia_supported_lane_count(intel_dp);

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

static int
intel_dp_downstream_max_dotclock(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &intel_dig_port->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int max_dotclk = dev_priv->max_dotclk_freq;
	int ds_max_dotclk;

	int type = intel_dp->downstream_ports[0] & DP_DS_PORT_TYPE_MASK;

	if (type != DP_DS_PORT_TYPE_VGA)
		return max_dotclk;

	ds_max_dotclk = drm_dp_downstream_max_clock(intel_dp->dpcd,
						    intel_dp->downstream_ports);

	if (ds_max_dotclk != 0)
		max_dotclk = min(max_dotclk, ds_max_dotclk);

	return max_dotclk;
}

static int cnl_max_source_rate(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum port port = dig_port->base.port;

	u32 voltage = I915_READ(CNL_PORT_COMP_DW3) & VOLTAGE_INFO_MASK;

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
	enum port port = dig_port->base.port;

	if (intel_port_is_combophy(dev_priv, port) &&
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
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	const struct ddi_vbt_port_info *info =
		&dev_priv->vbt.ddi_port_info[dig_port->base.port];
	const int *source_rates;
	int size, max_rate = 0, vbt_max_rate = info->dp_max_link_rate;

	/* This should only be done once */
	WARN_ON(intel_dp->source_rates || intel_dp->num_source_rates);

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
	WARN_ON(!intel_dp->num_source_rates || !intel_dp->num_sink_rates);

	intel_dp->num_common_rates = intersect_rates(intel_dp->source_rates,
						     intel_dp->num_source_rates,
						     intel_dp->sink_rates,
						     intel_dp->num_sink_rates,
						     intel_dp->common_rates);

	/* Paranoia, there should always be something in common. */
	if (WARN_ON(intel_dp->num_common_rates == 0)) {
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
	int index;

	index = intel_dp_rate_index(intel_dp->common_rates,
				    intel_dp->num_common_rates,
				    link_rate);
	if (index > 0) {
		if (intel_dp_is_edp(intel_dp) &&
		    !intel_dp_can_link_train_fallback_for_edp(intel_dp,
							      intel_dp->common_rates[index - 1],
							      lane_count)) {
			DRM_DEBUG_KMS("Retrying Link training for eDP with same parameters\n");
			return 0;
		}
		intel_dp->max_link_rate = intel_dp->common_rates[index - 1];
		intel_dp->max_link_lane_count = lane_count;
	} else if (lane_count > 1) {
		if (intel_dp_is_edp(intel_dp) &&
		    !intel_dp_can_link_train_fallback_for_edp(intel_dp,
							      intel_dp_max_common_rate(intel_dp),
							      lane_count >> 1)) {
			DRM_DEBUG_KMS("Retrying Link training for eDP with same parameters\n");
			return 0;
		}
		intel_dp->max_link_rate = intel_dp_max_common_rate(intel_dp);
		intel_dp->max_link_lane_count = lane_count >> 1;
	} else {
		DRM_ERROR("Link Training Unsuccessful\n");
		return -1;
	}

	return 0;
}

static enum drm_mode_status
intel_dp_mode_valid(struct drm_connector *connector,
		    struct drm_display_mode *mode)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	int target_clock = mode->clock;
	int max_rate, mode_rate, max_lanes, max_link_clock;
	int max_dotclk;
	u16 dsc_max_output_bpp = 0;
	u8 dsc_slice_count = 0;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	max_dotclk = intel_dp_downstream_max_dotclock(intel_dp);

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
	mode_rate = intel_dp_link_required(target_clock, 18);

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
				intel_dp_dsc_get_output_bpp(max_link_clock,
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

	return MODE_OK;
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe = intel_dp->pps_pipe;
	bool pll_enabled, release_cl_override = false;
	enum dpio_phy phy = DPIO_PHY(pipe);
	enum dpio_channel ch = vlv_pipe_to_channel(pipe);
	u32 DP;

	if (WARN(I915_READ(intel_dp->output_reg) & DP_PORT_EN,
		 "skipping pipe %c power sequencer kick due to port %c being active\n",
		 pipe_name(pipe), port_name(intel_dig_port->base.port)))
		return;

	DRM_DEBUG_KMS("kicking pipe %c power sequencer for port %c\n",
		      pipe_name(pipe), port_name(intel_dig_port->base.port));

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	DP = I915_READ(intel_dp->output_reg) & DP_DETECTED;
	DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	DP |= DP_PORT_WIDTH(1);
	DP |= DP_LINK_TRAIN_PAT_1;

	if (IS_CHERRYVIEW(dev_priv))
		DP |= DP_PIPE_SEL_CHV(pipe);
	else
		DP |= DP_PIPE_SEL(pipe);

	pll_enabled = I915_READ(DPLL(pipe)) & DPLL_VCO_ENABLE;

	/*
	 * The DPLL for the pipe must be enabled for this to work.
	 * So enable temporarily it if it's not already enabled.
	 */
	if (!pll_enabled) {
		release_cl_override = IS_CHERRYVIEW(dev_priv) &&
			!chv_phy_powergate_ch(dev_priv, phy, ch, true);

		if (vlv_force_pll_on(dev_priv, pipe, IS_CHERRYVIEW(dev_priv) ?
				     &chv_dpll[0].dpll : &vlv_dpll[0].dpll)) {
			DRM_ERROR("Failed to force on pll for pipe %c!\n",
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
	I915_WRITE(intel_dp->output_reg, DP);
	POSTING_READ(intel_dp->output_reg);

	I915_WRITE(intel_dp->output_reg, DP | DP_PORT_EN);
	POSTING_READ(intel_dp->output_reg);

	I915_WRITE(intel_dp->output_reg, DP & ~DP_PORT_EN);
	POSTING_READ(intel_dp->output_reg);

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
		struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

		if (encoder->type == INTEL_OUTPUT_EDP) {
			WARN_ON(intel_dp->active_pipe != INVALID_PIPE &&
				intel_dp->active_pipe != intel_dp->pps_pipe);

			if (intel_dp->pps_pipe != INVALID_PIPE)
				pipes &= ~(1 << intel_dp->pps_pipe);
		} else {
			WARN_ON(intel_dp->pps_pipe != INVALID_PIPE);

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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum pipe pipe;

	lockdep_assert_held(&dev_priv->pps_mutex);

	/* We should never land here with regular DP ports */
	WARN_ON(!intel_dp_is_edp(intel_dp));

	WARN_ON(intel_dp->active_pipe != INVALID_PIPE &&
		intel_dp->active_pipe != intel_dp->pps_pipe);

	if (intel_dp->pps_pipe != INVALID_PIPE)
		return intel_dp->pps_pipe;

	pipe = vlv_find_free_pps(dev_priv);

	/*
	 * Didn't find one. This should not happen since there
	 * are two power sequencers and up to two eDP ports.
	 */
	if (WARN_ON(pipe == INVALID_PIPE))
		pipe = PIPE_A;

	vlv_steal_power_sequencer(dev_priv, pipe);
	intel_dp->pps_pipe = pipe;

	DRM_DEBUG_KMS("picked pipe %c power sequencer for port %c\n",
		      pipe_name(intel_dp->pps_pipe),
		      port_name(intel_dig_port->base.port));

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
	WARN_ON(!intel_dp_is_edp(intel_dp));

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
	return I915_READ(PP_STATUS(pipe)) & PP_ON;
}

static bool vlv_pipe_has_vdd_on(struct drm_i915_private *dev_priv,
				enum pipe pipe)
{
	return I915_READ(PP_CONTROL(pipe)) & EDP_FORCE_VDD;
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
		u32 port_sel = I915_READ(PP_ON_DELAYS(pipe)) &
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->base.port;

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
		DRM_DEBUG_KMS("no initial power sequencer for port %c\n",
			      port_name(port));
		return;
	}

	DRM_DEBUG_KMS("initial power sequencer for port %c: pipe %c\n",
		      port_name(port), pipe_name(intel_dp->pps_pipe));

	intel_dp_init_panel_power_sequencer(intel_dp);
	intel_dp_init_panel_power_sequencer_registers(intel_dp, false);
}

void intel_power_sequencer_reset(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	if (WARN_ON(!IS_VALLEYVIEW(dev_priv) && !IS_CHERRYVIEW(dev_priv) &&
		    !IS_GEN9_LP(dev_priv)))
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
		struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

		WARN_ON(intel_dp->active_pipe != INVALID_PIPE);

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
			pp_div = I915_READ(pp_div_reg);
			pp_div &= PP_REFERENCE_DIVIDER_MASK;

			/* 0x1F write to PP_DIV_REG sets max cycle delay */
			I915_WRITE(pp_div_reg, pp_div | 0x1F);
			I915_WRITE(pp_ctrl_reg, PANEL_UNLOCK_REGS);
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

	return (I915_READ(_pp_stat_reg(intel_dp)) & PP_ON) != 0;
}

static bool edp_have_panel_vdd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	lockdep_assert_held(&dev_priv->pps_mutex);

	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    intel_dp->pps_pipe == INVALID_PIPE)
		return false;

	return I915_READ(_pp_ctrl_reg(intel_dp)) & EDP_FORCE_VDD;
}

static void
intel_dp_check_edp(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	if (!edp_have_panel_power(intel_dp) && !edp_have_panel_vdd(intel_dp)) {
		WARN(1, "eDP powered off while attempting aux channel communication.\n");
		DRM_DEBUG_KMS("Status 0x%08x Control 0x%08x\n",
			      I915_READ(_pp_stat_reg(intel_dp)),
			      I915_READ(_pp_ctrl_reg(intel_dp)));
	}
}

static u32
intel_dp_aux_wait_done(struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	i915_reg_t ch_ctl = intel_dp->aux_ch_ctl_reg(intel_dp);
	u32 status;
	bool done;

#define C (((status = intel_uncore_read_notrace(&i915->uncore, ch_ctl)) & DP_AUX_CH_CTL_SEND_BUSY) == 0)
	done = wait_event_timeout(i915->gmbus_wait_queue, C,
				  msecs_to_jiffies_timeout(10));

	/* just trace the final value */
	trace_i915_reg_rw(false, ch_ctl, status, sizeof(status), true);

	if (!done)
		DRM_ERROR("dp aux hw did not signal timeout!\n");
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
	return DIV_ROUND_CLOSEST(dev_priv->rawclk_freq, 2000);
}

static u32 ilk_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	if (index)
		return 0;

	/*
	 * The clock divider is based off the cdclk or PCH rawclk, and would
	 * like to run at 2MHz.  So, take the cdclk or PCH rawclk value and
	 * divide by 2000 and use that
	 */
	if (dig_port->aux_ch == AUX_CH_A)
		return DIV_ROUND_CLOSEST(dev_priv->cdclk.hw.cdclk, 2000);
	else
		return DIV_ROUND_CLOSEST(dev_priv->rawclk_freq, 2000);
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv =
			to_i915(intel_dig_port->base.base.dev);
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
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

	if (intel_dig_port->tc_type == TC_PORT_TBT)
		ret |= DP_AUX_CH_CTL_TBT_IO;

	return ret;
}

static int
intel_dp_aux_xfer(struct intel_dp *intel_dp,
		  const u8 *send, int send_bytes,
		  u8 *recv, int recv_size,
		  u32 aux_send_ctl_flags)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *i915 =
			to_i915(intel_dig_port->base.base.dev);
	struct intel_uncore *uncore = &i915->uncore;
	i915_reg_t ch_ctl, ch_data[5];
	u32 aux_clock_divider;
	enum intel_display_power_domain aux_domain =
		intel_aux_power_domain(intel_dig_port);
	intel_wakeref_t aux_wakeref;
	intel_wakeref_t pps_wakeref;
	int i, ret, recv_bytes;
	int try, clock = 0;
	u32 status;
	bool vdd;

	ch_ctl = intel_dp->aux_ch_ctl_reg(intel_dp);
	for (i = 0; i < ARRAY_SIZE(ch_data); i++)
		ch_data[i] = intel_dp->aux_ch_data_reg(intel_dp, i);

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
	pm_qos_update_request(&i915->pm_qos, 0);

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
		static u32 last_status = -1;
		const u32 status = intel_uncore_read(uncore, ch_ctl);

		if (status != last_status) {
			WARN(1, "dp_aux_ch not started status 0x%08x\n",
			     status);
			last_status = status;
		}

		ret = -EBUSY;
		goto out;
	}

	/* Only 5 data registers! */
	if (WARN_ON(send_bytes > 20 || recv_size > 20)) {
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
		DRM_ERROR("dp_aux_ch not done status 0x%08x\n", status);
		ret = -EBUSY;
		goto out;
	}

done:
	/* Check for timeout or receive error.
	 * Timeouts occur when the sink is not connected
	 */
	if (status & DP_AUX_CH_CTL_RECEIVE_ERROR) {
		DRM_ERROR("dp_aux_ch receive error status 0x%08x\n", status);
		ret = -EIO;
		goto out;
	}

	/* Timeouts occur when the device isn't connected, so they're
	 * "normal" -- don't fill the kernel log with these */
	if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR) {
		DRM_DEBUG_KMS("dp_aux_ch timeout status 0x%08x\n", status);
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
		DRM_DEBUG_KMS("Forbidden recv_bytes = %d on aux transaction\n",
			      recv_bytes);
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
	pm_qos_update_request(&i915->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (vdd)
		edp_panel_vdd_off(intel_dp, false);

	pps_unlock(intel_dp, pps_wakeref);
	intel_display_power_put_async(i915, aux_domain, aux_wakeref);

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

static ssize_t
intel_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct intel_dp *intel_dp = container_of(aux, struct intel_dp, aux);
	u8 txbuf[20], rxbuf[20];
	size_t txsize, rxsize;
	int ret;

	intel_dp_aux_header(txbuf, msg);

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		txsize = msg->size ? HEADER_SIZE + msg->size : BARE_ADDRESS_SIZE;
		rxsize = 2; /* 0 or 1 data bytes */

		if (WARN_ON(txsize > 20))
			return -E2BIG;

		WARN_ON(!msg->buffer != !msg->size);

		if (msg->buffer)
			memcpy(txbuf + HEADER_SIZE, msg->buffer, msg->size);

		ret = intel_dp_aux_xfer(intel_dp, txbuf, txsize,
					rxbuf, rxsize, 0);
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

		if (WARN_ON(rxsize > 20))
			return -E2BIG;

		ret = intel_dp_aux_xfer(intel_dp, txbuf, txsize,
					rxbuf, rxsize, 0);
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
	intel_dp->aux.name = kasprintf(GFP_KERNEL, "DPDDC-%c",
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
	char str[128]; /* FIXME: too big for stack? */

	if ((drm_debug & DRM_UT_KMS) == 0)
		return;

	snprintf_int_array(str, sizeof(str),
			   intel_dp->source_rates, intel_dp->num_source_rates);
	DRM_DEBUG_KMS("source rates: %s\n", str);

	snprintf_int_array(str, sizeof(str),
			   intel_dp->sink_rates, intel_dp->num_sink_rates);
	DRM_DEBUG_KMS("sink rates: %s\n", str);

	snprintf_int_array(str, sizeof(str),
			   intel_dp->common_rates, intel_dp->num_common_rates);
	DRM_DEBUG_KMS("common rates: %s\n", str);
}

int
intel_dp_max_link_rate(struct intel_dp *intel_dp)
{
	int len;

	len = intel_dp_common_len_rate_limit(intel_dp, intel_dp->max_link_rate);
	if (WARN_ON(len <= 0))
		return 162000;

	return intel_dp->common_rates[len - 1];
}

int intel_dp_rate_select(struct intel_dp *intel_dp, int rate)
{
	int i = intel_dp_rate_index(intel_dp->sink_rates,
				    intel_dp->num_sink_rates, rate);

	if (WARN_ON(i < 0))
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

	return INTEL_GEN(dev_priv) >= 11 &&
		pipe_config->cpu_transcoder != TRANSCODER_A;
}

static bool intel_dp_supports_fec(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *pipe_config)
{
	return intel_dp_source_supports_fec(intel_dp, pipe_config) &&
		drm_dp_sink_supports_fec(intel_dp->fec_capable);
}

static bool intel_dp_source_supports_dsc(struct intel_dp *intel_dp,
					 const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	return INTEL_GEN(dev_priv) >= 10 &&
		pipe_config->cpu_transcoder != TRANSCODER_A;
}

static bool intel_dp_supports_dsc(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *pipe_config)
{
	if (!intel_dp_is_edp(intel_dp) && !pipe_config->fec_enable)
		return false;

	return intel_dp_source_supports_dsc(intel_dp, pipe_config) &&
		drm_dp_sink_supports_dsc(intel_dp->dsc_dpcd);
}

static int intel_dp_compute_bpp(struct intel_dp *intel_dp,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	int bpp, bpc;

	bpp = pipe_config->pipe_bpp;
	bpc = drm_dp_downstream_max_bpc(intel_dp->dpcd, intel_dp->downstream_ports);

	if (bpc > 0)
		bpp = min(bpp, 3*bpc);

	if (intel_dp_is_edp(intel_dp)) {
		/* Get bpp from vbt only for panels that dont have bpp in edid */
		if (intel_connector->base.display_info.bpc == 0 &&
		    dev_priv->vbt.edp.bpp && dev_priv->vbt.edp.bpp < bpp) {
			DRM_DEBUG_KMS("clamping bpp for eDP panel to BIOS-provided %i\n",
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
	/* For DP Compliance we override the computed bpp for the pipe */
	if (intel_dp->compliance.test_data.bpc != 0) {
		int bpp = 3 * intel_dp->compliance.test_data.bpc;

		limits->min_bpp = limits->max_bpp = bpp;
		pipe_config->dither_force_disable = bpp == 6 * 3;

		DRM_DEBUG_KMS("Setting pipe_bpp to %d\n", bpp);
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

static int intel_dp_output_bpp(const struct intel_crtc_state *crtc_state, int bpp)
{
	/*
	 * bpp value was assumed to RGB format. And YCbCr 4:2:0 output
	 * format of the number of bytes per pixel will be half the number
	 * of bytes of RGB pixel.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		bpp /= 2;

	return bpp;
}

/* Optimize link config in order: max bpp, min clock, min lanes */
static int
intel_dp_compute_link_config_wide(struct intel_dp *intel_dp,
				  struct intel_crtc_state *pipe_config,
				  const struct link_config_limits *limits)
{
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	int bpp, clock, lane_count;
	int mode_rate, link_clock, link_avail;

	for (bpp = limits->max_bpp; bpp >= limits->min_bpp; bpp -= 2 * 3) {
		mode_rate = intel_dp_link_required(adjusted_mode->crtc_clock,
						   bpp);

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

static int intel_dp_dsc_compute_config(struct intel_dp *intel_dp,
				       struct intel_crtc_state *pipe_config,
				       struct drm_connector_state *conn_state,
				       struct link_config_limits *limits)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	u8 dsc_max_bpc;
	int pipe_bpp;
	int ret;

	pipe_config->fec_enable = !intel_dp_is_edp(intel_dp) &&
		intel_dp_supports_fec(intel_dp, pipe_config);

	if (!intel_dp_supports_dsc(intel_dp, pipe_config))
		return -EINVAL;

	dsc_max_bpc = min_t(u8, DP_DSC_MAX_SUPPORTED_BPC,
			    conn_state->max_requested_bpc);

	pipe_bpp = intel_dp_dsc_compute_bpp(intel_dp, dsc_max_bpc);
	if (pipe_bpp < DP_DSC_MIN_SUPPORTED_BPC * 3) {
		DRM_DEBUG_KMS("No DSC support for less than 8bpc\n");
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
		pipe_config->dsc_params.compressed_bpp =
			min_t(u16, drm_edp_dsc_sink_output_bpp(intel_dp->dsc_dpcd) >> 4,
			      pipe_config->pipe_bpp);
		pipe_config->dsc_params.slice_count =
			drm_dp_dsc_sink_max_slice_count(intel_dp->dsc_dpcd,
							true);
	} else {
		u16 dsc_max_output_bpp;
		u8 dsc_dp_slice_count;

		dsc_max_output_bpp =
			intel_dp_dsc_get_output_bpp(pipe_config->port_clock,
						    pipe_config->lane_count,
						    adjusted_mode->crtc_clock,
						    adjusted_mode->crtc_hdisplay);
		dsc_dp_slice_count =
			intel_dp_dsc_get_slice_count(intel_dp,
						     adjusted_mode->crtc_clock,
						     adjusted_mode->crtc_hdisplay);
		if (!dsc_max_output_bpp || !dsc_dp_slice_count) {
			DRM_DEBUG_KMS("Compressed BPP/Slice Count not supported\n");
			return -EINVAL;
		}
		pipe_config->dsc_params.compressed_bpp = min_t(u16,
							       dsc_max_output_bpp >> 4,
							       pipe_config->pipe_bpp);
		pipe_config->dsc_params.slice_count = dsc_dp_slice_count;
	}
	/*
	 * VDSC engine operates at 1 Pixel per clock, so if peak pixel rate
	 * is greater than the maximum Cdclock and if slice count is even
	 * then we need to use 2 VDSC instances.
	 */
	if (adjusted_mode->crtc_clock > dev_priv->max_cdclk_freq) {
		if (pipe_config->dsc_params.slice_count > 1) {
			pipe_config->dsc_params.dsc_split = true;
		} else {
			DRM_DEBUG_KMS("Cannot split stream to use 2 VDSC instances\n");
			return -EINVAL;
		}
	}

	ret = intel_dp_compute_dsc_params(intel_dp, pipe_config);
	if (ret < 0) {
		DRM_DEBUG_KMS("Cannot compute valid DSC parameters for Input Bpp = %d "
			      "Compressed BPP = %d\n",
			      pipe_config->pipe_bpp,
			      pipe_config->dsc_params.compressed_bpp);
		return ret;
	}

	pipe_config->dsc_params.compression_enable = true;
	DRM_DEBUG_KMS("DP DSC computed with Input Bpp = %d "
		      "Compressed Bpp = %d Slice Count = %d\n",
		      pipe_config->pipe_bpp,
		      pipe_config->dsc_params.compressed_bpp,
		      pipe_config->dsc_params.slice_count);

	return 0;
}

int intel_dp_min_bpp(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_RGB)
		return 6 * 3;
	else
		return 8 * 3;
}

static int
intel_dp_compute_link_config(struct intel_encoder *encoder,
			     struct intel_crtc_state *pipe_config,
			     struct drm_connector_state *conn_state)
{
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct link_config_limits limits;
	int common_len;
	int ret;

	common_len = intel_dp_common_len_rate_limit(intel_dp,
						    intel_dp->max_link_rate);

	/* No common link rates between source and sink */
	WARN_ON(common_len <= 0);

	limits.min_clock = 0;
	limits.max_clock = common_len - 1;

	limits.min_lane_count = 1;
	limits.max_lane_count = intel_dp_max_lane_count(intel_dp);

	limits.min_bpp = intel_dp_min_bpp(pipe_config);
	limits.max_bpp = intel_dp_compute_bpp(intel_dp, pipe_config);

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

	DRM_DEBUG_KMS("DP link computation with max lane count %i "
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
	DRM_DEBUG_KMS("Force DSC en = %d\n", intel_dp->force_dsc_en);
	if (ret || intel_dp->force_dsc_en) {
		ret = intel_dp_dsc_compute_config(intel_dp, pipe_config,
						  conn_state, &limits);
		if (ret < 0)
			return ret;
	}

	if (pipe_config->dsc_params.compression_enable) {
		DRM_DEBUG_KMS("DP lane count %d clock %d Input bpp %d Compressed bpp %d\n",
			      pipe_config->lane_count, pipe_config->port_clock,
			      pipe_config->pipe_bpp,
			      pipe_config->dsc_params.compressed_bpp);

		DRM_DEBUG_KMS("DP link rate required %i available %i\n",
			      intel_dp_link_required(adjusted_mode->crtc_clock,
						     pipe_config->dsc_params.compressed_bpp),
			      intel_dp_max_data_rate(pipe_config->port_clock,
						     pipe_config->lane_count));
	} else {
		DRM_DEBUG_KMS("DP lane count %d clock %d bpp %d\n",
			      pipe_config->lane_count, pipe_config->port_clock,
			      pipe_config->pipe_bpp);

		DRM_DEBUG_KMS("DP link rate required %i available %i\n",
			      intel_dp_link_required(adjusted_mode->crtc_clock,
						     pipe_config->pipe_bpp),
			      intel_dp_max_data_rate(pipe_config->port_clock,
						     pipe_config->lane_count));
	}
	return 0;
}

static int
intel_dp_ycbcr420_config(struct intel_dp *intel_dp,
			 struct drm_connector *connector,
			 struct intel_crtc_state *crtc_state)
{
	const struct drm_display_info *info = &connector->display_info;
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	int ret;

	if (!drm_mode_is_420_only(info, adjusted_mode) ||
	    !intel_dp_get_colorimetry_status(intel_dp) ||
	    !connector->ycbcr_420_allowed)
		return 0;

	crtc_state->output_format = INTEL_OUTPUT_FORMAT_YCBCR420;

	/* YCBCR 420 output conversion needs a scaler */
	ret = skl_update_scaler_crtc(crtc_state);
	if (ret) {
		DRM_DEBUG_KMS("Scaler allocation for output failed\n");
		return ret;
	}

	intel_pch_panel_fitting(crtc, crtc_state, DRM_MODE_SCALE_FULLSCREEN);

	return 0;
}

bool intel_dp_limited_color_range(const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	const struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;

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

int
intel_dp_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_state *pipe_config,
			struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_lspcon *lspcon = enc_to_intel_lspcon(&encoder->base);
	enum port port = encoder->port;
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->base.crtc);
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct intel_digital_connector_state *intel_conn_state =
		to_intel_digital_connector_state(conn_state);
	bool constant_n = drm_dp_has_quirk(&intel_dp->desc,
					   DP_DPCD_QUIRK_CONSTANT_N);
	int ret = 0, output_bpp;

	if (HAS_PCH_SPLIT(dev_priv) && !HAS_DDI(dev_priv) && port != PORT_A)
		pipe_config->has_pch_encoder = true;

	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	if (lspcon->active)
		lspcon_ycbcr420_config(&intel_connector->base, pipe_config);
	else
		ret = intel_dp_ycbcr420_config(intel_dp, &intel_connector->base,
					       pipe_config);

	if (ret)
		return ret;

	pipe_config->has_drrs = false;
	if (IS_G4X(dev_priv) || port == PORT_A)
		pipe_config->has_audio = false;
	else if (intel_conn_state->force_audio == HDMI_AUDIO_AUTO)
		pipe_config->has_audio = intel_dp->has_audio;
	else
		pipe_config->has_audio = intel_conn_state->force_audio == HDMI_AUDIO_ON;

	if (intel_dp_is_edp(intel_dp) && intel_connector->panel.fixed_mode) {
		intel_fixed_panel_mode(intel_connector->panel.fixed_mode,
				       adjusted_mode);

		if (INTEL_GEN(dev_priv) >= 9) {
			ret = skl_update_scaler_crtc(pipe_config);
			if (ret)
				return ret;
		}

		if (HAS_GMCH(dev_priv))
			intel_gmch_panel_fitting(intel_crtc, pipe_config,
						 conn_state->scaling_mode);
		else
			intel_pch_panel_fitting(intel_crtc, pipe_config,
						conn_state->scaling_mode);
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	if (HAS_GMCH(dev_priv) &&
	    adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		return -EINVAL;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK)
		return -EINVAL;

	ret = intel_dp_compute_link_config(encoder, pipe_config, conn_state);
	if (ret < 0)
		return ret;

	pipe_config->limited_color_range =
		intel_dp_limited_color_range(pipe_config, conn_state);

	if (pipe_config->dsc_params.compression_enable)
		output_bpp = pipe_config->dsc_params.compressed_bpp;
	else
		output_bpp = intel_dp_output_bpp(pipe_config, pipe_config->pipe_bpp);

	intel_link_compute_m_n(output_bpp,
			       pipe_config->lane_count,
			       adjusted_mode->crtc_clock,
			       pipe_config->port_clock,
			       &pipe_config->dp_m_n,
			       constant_n);

	if (intel_connector->panel.downclock_mode != NULL &&
		dev_priv->drrs.type == SEAMLESS_DRRS_SUPPORT) {
			pipe_config->has_drrs = true;
			intel_link_compute_m_n(output_bpp,
					       pipe_config->lane_count,
					       intel_connector->panel.downclock_mode->clock,
					       pipe_config->port_clock,
					       &pipe_config->dp_m2_n2,
					       constant_n);
	}

	if (!HAS_DDI(dev_priv))
		intel_dp_set_clock(encoder, pipe_config);

	intel_psr_compute_config(intel_dp, pipe_config);

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
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = encoder->port;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);
	const struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;

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
	 * configuration happens (oddly) in ironlake_pch_enable
	 */

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	intel_dp->DP = I915_READ(intel_dp->output_reg) & DP_DETECTED;

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

		trans_dp = I915_READ(TRANS_DP_CTL(crtc->pipe));
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			trans_dp |= TRANS_DP_ENH_FRAMING;
		else
			trans_dp &= ~TRANS_DP_ENH_FRAMING;
		I915_WRITE(TRANS_DP_CTL(crtc->pipe), trans_dp);
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

	DRM_DEBUG_KMS("mask %08x value %08x status %08x control %08x\n",
			mask, value,
			I915_READ(pp_stat_reg),
			I915_READ(pp_ctrl_reg));

	if (intel_wait_for_register(&dev_priv->uncore,
				    pp_stat_reg, mask, value,
				    5000))
		DRM_ERROR("Panel status timeout: status %08x control %08x\n",
				I915_READ(pp_stat_reg),
				I915_READ(pp_ctrl_reg));

	DRM_DEBUG_KMS("Wait complete\n");
}

static void wait_panel_on(struct intel_dp *intel_dp)
{
	DRM_DEBUG_KMS("Wait for panel power on\n");
	wait_panel_status(intel_dp, IDLE_ON_MASK, IDLE_ON_VALUE);
}

static void wait_panel_off(struct intel_dp *intel_dp)
{
	DRM_DEBUG_KMS("Wait for panel power off time\n");
	wait_panel_status(intel_dp, IDLE_OFF_MASK, IDLE_OFF_VALUE);
}

static void wait_panel_power_cycle(struct intel_dp *intel_dp)
{
	ktime_t panel_power_on_time;
	s64 panel_power_off_duration;

	DRM_DEBUG_KMS("Wait for panel power cycle\n");

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

static  u32 ironlake_get_pp_control(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	u32 control;

	lockdep_assert_held(&dev_priv->pps_mutex);

	control = I915_READ(_pp_ctrl_reg(intel_dp));
	if (WARN_ON(!HAS_DDI(dev_priv) &&
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
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
				intel_aux_power_domain(intel_dig_port));

	DRM_DEBUG_KMS("Turning eDP port %c VDD on\n",
		      port_name(intel_dig_port->base.port));

	if (!edp_have_panel_power(intel_dp))
		wait_panel_power_cycle(intel_dp);

	pp = ironlake_get_pp_control(intel_dp);
	pp |= EDP_FORCE_VDD;

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);
	DRM_DEBUG_KMS("PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			I915_READ(pp_stat_reg), I915_READ(pp_ctrl_reg));
	/*
	 * If the panel wasn't on, delay before accessing aux channel
	 */
	if (!edp_have_panel_power(intel_dp)) {
		DRM_DEBUG_KMS("eDP port %c panel power wasn't enabled\n",
			      port_name(intel_dig_port->base.port));
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
	I915_STATE_WARN(!vdd, "eDP port %c VDD already requested on\n",
	     port_name(dp_to_dig_port(intel_dp)->base.port));
}

static void edp_panel_vdd_off_sync(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *intel_dig_port =
		dp_to_dig_port(intel_dp);
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	WARN_ON(intel_dp->want_panel_vdd);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	DRM_DEBUG_KMS("Turning eDP port %c VDD off\n",
		      port_name(intel_dig_port->base.port));

	pp = ironlake_get_pp_control(intel_dp);
	pp &= ~EDP_FORCE_VDD;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp_stat_reg = _pp_stat_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	/* Make sure sequencer is idle before allowing subsequent activity */
	DRM_DEBUG_KMS("PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
	I915_READ(pp_stat_reg), I915_READ(pp_ctrl_reg));

	if ((pp & PANEL_POWER_ON) == 0)
		intel_dp->panel_power_off_time = ktime_get_boottime();

	intel_display_power_put_unchecked(dev_priv,
					  intel_aux_power_domain(intel_dig_port));
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

	I915_STATE_WARN(!intel_dp->want_panel_vdd, "eDP port %c VDD not forced on",
	     port_name(dp_to_dig_port(intel_dp)->base.port));

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

	DRM_DEBUG_KMS("Turn eDP port %c panel power on\n",
		      port_name(dp_to_dig_port(intel_dp)->base.port));

	if (WARN(edp_have_panel_power(intel_dp),
		 "eDP port %c panel power already on\n",
		 port_name(dp_to_dig_port(intel_dp)->base.port)))
		return;

	wait_panel_power_cycle(intel_dp);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp = ironlake_get_pp_control(intel_dp);
	if (IS_GEN(dev_priv, 5)) {
		/* ILK workaround: disable reset around power sequence */
		pp &= ~PANEL_POWER_RESET;
		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
	}

	pp |= PANEL_POWER_ON;
	if (!IS_GEN(dev_priv, 5))
		pp |= PANEL_POWER_RESET;

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	wait_panel_on(intel_dp);
	intel_dp->last_power_on = jiffies;

	if (IS_GEN(dev_priv, 5)) {
		pp |= PANEL_POWER_RESET; /* restore panel reset bit */
		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
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

	DRM_DEBUG_KMS("Turn eDP port %c panel power off\n",
		      port_name(dig_port->base.port));

	WARN(!intel_dp->want_panel_vdd, "Need eDP port %c VDD to turn off panel\n",
	     port_name(dig_port->base.port));

	pp = ironlake_get_pp_control(intel_dp);
	/* We need to switch off panel power _and_ force vdd, for otherwise some
	 * panels get very unhappy and cease to work. */
	pp &= ~(PANEL_POWER_ON | PANEL_POWER_RESET | EDP_FORCE_VDD |
		EDP_BLC_ENABLE);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	intel_dp->want_panel_vdd = false;

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

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

		pp = ironlake_get_pp_control(intel_dp);
		pp |= EDP_BLC_ENABLE;

		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
	}
}

/* Enable backlight PWM and backlight PP control. */
void intel_edp_backlight_on(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(conn_state->best_encoder);

	if (!intel_dp_is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("\n");

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

		pp = ironlake_get_pp_control(intel_dp);
		pp &= ~EDP_BLC_ENABLE;

		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
	}

	intel_dp->last_backlight_off = jiffies;
	edp_wait_backlight_off(intel_dp);
}

/* Disable backlight PP control and backlight PWM. */
void intel_edp_backlight_off(const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(old_conn_state->best_encoder);

	if (!intel_dp_is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("\n");

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
	struct intel_dp *intel_dp = intel_attached_dp(&connector->base);
	intel_wakeref_t wakeref;
	bool is_enabled;

	is_enabled = false;
	with_pps_lock(intel_dp, wakeref)
		is_enabled = ironlake_get_pp_control(intel_dp) & EDP_BLC_ENABLE;
	if (is_enabled == enable)
		return;

	DRM_DEBUG_KMS("panel power control backlight %s\n",
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
	bool cur_state = I915_READ(intel_dp->output_reg) & DP_PORT_EN;

	I915_STATE_WARN(cur_state != state,
			"DP port %c state assertion failure (expected %s, current %s)\n",
			port_name(dig_port->base.port),
			onoff(state), onoff(cur_state));
}
#define assert_dp_port_disabled(d) assert_dp_port((d), false)

static void assert_edp_pll(struct drm_i915_private *dev_priv, bool state)
{
	bool cur_state = I915_READ(DP_A) & DP_PLL_ENABLE;

	I915_STATE_WARN(cur_state != state,
			"eDP PLL state assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}
#define assert_edp_pll_enabled(d) assert_edp_pll((d), true)
#define assert_edp_pll_disabled(d) assert_edp_pll((d), false)

static void ironlake_edp_pll_on(struct intel_dp *intel_dp,
				const struct intel_crtc_state *pipe_config)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	assert_pipe_disabled(dev_priv, crtc->pipe);
	assert_dp_port_disabled(intel_dp);
	assert_edp_pll_disabled(dev_priv);

	DRM_DEBUG_KMS("enabling eDP PLL for clock %d\n",
		      pipe_config->port_clock);

	intel_dp->DP &= ~DP_PLL_FREQ_MASK;

	if (pipe_config->port_clock == 162000)
		intel_dp->DP |= DP_PLL_FREQ_162MHZ;
	else
		intel_dp->DP |= DP_PLL_FREQ_270MHZ;

	I915_WRITE(DP_A, intel_dp->DP);
	POSTING_READ(DP_A);
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

	I915_WRITE(DP_A, intel_dp->DP);
	POSTING_READ(DP_A);
	udelay(200);
}

static void ironlake_edp_pll_off(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	assert_pipe_disabled(dev_priv, crtc->pipe);
	assert_dp_port_disabled(intel_dp);
	assert_edp_pll_enabled(dev_priv);

	DRM_DEBUG_KMS("disabling eDP PLL\n");

	intel_dp->DP &= ~DP_PLL_ENABLE;

	I915_WRITE(DP_A, intel_dp->DP);
	POSTING_READ(DP_A);
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
		intel_dp->dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_PRESENT &&
		intel_dp->downstream_ports[0] & DP_DS_PORT_HPD;
}

void intel_dp_sink_set_decompression_state(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state,
					   bool enable)
{
	int ret;

	if (!crtc_state->dsc_params.compression_enable)
		return;

	ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_DSC_ENABLE,
				 enable ? DP_DECOMPRESSION_EN : 0);
	if (ret < 0)
		DRM_DEBUG_KMS("Failed to %s sink decompression state\n",
			      enable ? "enable" : "disable");
}

/* If the sink supports it, try to set the power state appropriately */
void intel_dp_sink_dpms(struct intel_dp *intel_dp, int mode)
{
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
		DRM_DEBUG_KMS("failed to %s sink power state\n",
			      mode == DRM_MODE_DPMS_ON ? "enable" : "disable");
}

static bool cpt_dp_port_selected(struct drm_i915_private *dev_priv,
				 enum port port, enum pipe *pipe)
{
	enum pipe p;

	for_each_pipe(dev_priv, p) {
		u32 val = I915_READ(TRANS_DP_CTL(p));

		if ((val & TRANS_DP_PORT_SEL_MASK) == TRANS_DP_PORT_SEL(port)) {
			*pipe = p;
			return true;
		}
	}

	DRM_DEBUG_KMS("No pipe for DP port %c found\n", port_name(port));

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

	val = I915_READ(dp_reg);

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
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
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
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	u32 tmp, flags = 0;
	enum port port = encoder->port;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);

	if (encoder->type == INTEL_OUTPUT_EDP)
		pipe_config->output_types |= BIT(INTEL_OUTPUT_EDP);
	else
		pipe_config->output_types |= BIT(INTEL_OUTPUT_DP);

	tmp = I915_READ(intel_dp->output_reg);

	pipe_config->has_audio = tmp & DP_AUDIO_OUTPUT_ENABLE && port != PORT_A;

	if (HAS_PCH_CPT(dev_priv) && port != PORT_A) {
		u32 trans_dp = I915_READ(TRANS_DP_CTL(crtc->pipe));

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

	pipe_config->base.adjusted_mode.flags |= flags;

	if (IS_G4X(dev_priv) && tmp & DP_COLOR_RANGE_16_235)
		pipe_config->limited_color_range = true;

	pipe_config->lane_count =
		((tmp & DP_PORT_WIDTH_MASK) >> DP_PORT_WIDTH_SHIFT) + 1;

	intel_dp_get_m_n(crtc, pipe_config);

	if (port == PORT_A) {
		if ((I915_READ(DP_A) & DP_PLL_FREQ_MASK) == DP_PLL_FREQ_162MHZ)
			pipe_config->port_clock = 162000;
		else
			pipe_config->port_clock = 270000;
	}

	pipe_config->base.adjusted_mode.crtc_clock =
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
		DRM_DEBUG_KMS("pipe has %d bpp for eDP panel, overriding BIOS-provided max %d bpp\n",
			      pipe_config->pipe_bpp, dev_priv->vbt.edp.bpp);
		dev_priv->vbt.edp.bpp = pipe_config->pipe_bpp;
	}
}

static void intel_disable_dp(struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

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

static void g4x_disable_dp(struct intel_encoder *encoder,
			   const struct intel_crtc_state *old_crtc_state,
			   const struct drm_connector_state *old_conn_state)
{
	intel_disable_dp(encoder, old_crtc_state, old_conn_state);
}

static void vlv_disable_dp(struct intel_encoder *encoder,
			   const struct intel_crtc_state *old_crtc_state,
			   const struct drm_connector_state *old_conn_state)
{
	intel_disable_dp(encoder, old_crtc_state, old_conn_state);
}

static void g4x_post_disable_dp(struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
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
		ironlake_edp_pll_off(intel_dp, old_crtc_state);
}

static void vlv_post_disable_dp(struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	intel_dp_link_down(encoder, old_crtc_state);
}

static void chv_post_disable_dp(struct intel_encoder *encoder,
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
_intel_dp_set_link_train(struct intel_dp *intel_dp,
			 u32 *DP,
			 u8 dp_train_pat)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->base.port;
	u8 train_pat_mask = drm_dp_training_pattern_mask(intel_dp->dpcd);

	if (dp_train_pat & train_pat_mask)
		DRM_DEBUG_KMS("Using DP training pattern TPS%d\n",
			      dp_train_pat & train_pat_mask);

	if (HAS_DDI(dev_priv)) {
		u32 temp = I915_READ(DP_TP_CTL(port));

		if (dp_train_pat & DP_LINK_SCRAMBLING_DISABLE)
			temp |= DP_TP_CTL_SCRAMBLE_DISABLE;
		else
			temp &= ~DP_TP_CTL_SCRAMBLE_DISABLE;

		temp &= ~DP_TP_CTL_LINK_TRAIN_MASK;
		switch (dp_train_pat & train_pat_mask) {
		case DP_TRAINING_PATTERN_DISABLE:
			temp |= DP_TP_CTL_LINK_TRAIN_NORMAL;

			break;
		case DP_TRAINING_PATTERN_1:
			temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
			break;
		case DP_TRAINING_PATTERN_2:
			temp |= DP_TP_CTL_LINK_TRAIN_PAT2;
			break;
		case DP_TRAINING_PATTERN_3:
			temp |= DP_TP_CTL_LINK_TRAIN_PAT3;
			break;
		case DP_TRAINING_PATTERN_4:
			temp |= DP_TP_CTL_LINK_TRAIN_PAT4;
			break;
		}
		I915_WRITE(DP_TP_CTL(port), temp);

	} else if ((IS_IVYBRIDGE(dev_priv) && port == PORT_A) ||
		   (HAS_PCH_CPT(dev_priv) && port != PORT_A)) {
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
			DRM_DEBUG_KMS("TPS3 not supported, using TPS2 instead\n");
			*DP |= DP_LINK_TRAIN_PAT_2_CPT;
			break;
		}

	} else {
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
			DRM_DEBUG_KMS("TPS3 not supported, using TPS2 instead\n");
			*DP |= DP_LINK_TRAIN_PAT_2;
			break;
		}
	}
}

static void intel_dp_enable_port(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *old_crtc_state)
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
	if (old_crtc_state->has_audio)
		intel_dp->DP |= DP_AUDIO_OUTPUT_ENABLE;

	I915_WRITE(intel_dp->output_reg, intel_dp->DP);
	POSTING_READ(intel_dp->output_reg);
}

static void intel_enable_dp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->base.crtc);
	u32 dp_reg = I915_READ(intel_dp->output_reg);
	enum pipe pipe = crtc->pipe;
	intel_wakeref_t wakeref;

	if (WARN_ON(dp_reg & DP_PORT_EN))
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
	intel_dp_start_link_train(intel_dp);
	intel_dp_stop_link_train(intel_dp);

	if (pipe_config->has_audio) {
		DRM_DEBUG_DRIVER("Enabling DP audio on pipe %c\n",
				 pipe_name(pipe));
		intel_audio_codec_enable(encoder, pipe_config, conn_state);
	}
}

static void g4x_enable_dp(struct intel_encoder *encoder,
			  const struct intel_crtc_state *pipe_config,
			  const struct drm_connector_state *conn_state)
{
	intel_enable_dp(encoder, pipe_config, conn_state);
	intel_edp_backlight_on(pipe_config, conn_state);
}

static void vlv_enable_dp(struct intel_encoder *encoder,
			  const struct intel_crtc_state *pipe_config,
			  const struct drm_connector_state *conn_state)
{
	intel_edp_backlight_on(pipe_config, conn_state);
}

static void g4x_pre_enable_dp(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = encoder->port;

	intel_dp_prepare(encoder, pipe_config);

	/* Only ilk+ has port A */
	if (port == PORT_A)
		ironlake_edp_pll_on(intel_dp, pipe_config);
}

static void vlv_detach_power_sequencer(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(intel_dig_port->base.base.dev);
	enum pipe pipe = intel_dp->pps_pipe;
	i915_reg_t pp_on_reg = PP_ON_DELAYS(pipe);

	WARN_ON(intel_dp->active_pipe != INVALID_PIPE);

	if (WARN_ON(pipe != PIPE_A && pipe != PIPE_B))
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
	DRM_DEBUG_KMS("detaching pipe %c power sequencer from port %c\n",
		      pipe_name(pipe), port_name(intel_dig_port->base.port));
	I915_WRITE(pp_on_reg, 0);
	POSTING_READ(pp_on_reg);

	intel_dp->pps_pipe = INVALID_PIPE;
}

static void vlv_steal_power_sequencer(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	struct intel_encoder *encoder;

	lockdep_assert_held(&dev_priv->pps_mutex);

	for_each_intel_dp(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
		enum port port = encoder->port;

		WARN(intel_dp->active_pipe == pipe,
		     "stealing pipe %c power sequencer from active (e)DP port %c\n",
		     pipe_name(pipe), port_name(port));

		if (intel_dp->pps_pipe != pipe)
			continue;

		DRM_DEBUG_KMS("stealing pipe %c power sequencer from port %c\n",
			      pipe_name(pipe), port_name(port));

		/* make sure vdd is off before we steal it */
		vlv_detach_power_sequencer(intel_dp);
	}
}

static void vlv_init_panel_power_sequencer(struct intel_encoder *encoder,
					   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);

	lockdep_assert_held(&dev_priv->pps_mutex);

	WARN_ON(intel_dp->active_pipe != INVALID_PIPE);

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

	DRM_DEBUG_KMS("initializing pipe %c power sequencer for port %c\n",
		      pipe_name(intel_dp->pps_pipe), port_name(encoder->port));

	/* init power sequencer on this pipe and port */
	intel_dp_init_panel_power_sequencer(intel_dp);
	intel_dp_init_panel_power_sequencer_registers(intel_dp, true);
}

static void vlv_pre_enable_dp(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	vlv_phy_pre_encoder_enable(encoder, pipe_config);

	intel_enable_dp(encoder, pipe_config, conn_state);
}

static void vlv_dp_pre_pll_enable(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	intel_dp_prepare(encoder, pipe_config);

	vlv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_pre_enable_dp(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	chv_phy_pre_encoder_enable(encoder, pipe_config);

	intel_enable_dp(encoder, pipe_config, conn_state);

	/* Second common lane will stay alive on its own now */
	chv_phy_release_cl2_override(encoder);
}

static void chv_dp_pre_pll_enable(struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	intel_dp_prepare(encoder, pipe_config);

	chv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_dp_post_pll_disable(struct intel_encoder *encoder,
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

/* These are source-specific values. */
u8
intel_dp_voltage_max(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	enum port port = encoder->port;

	if (HAS_DDI(dev_priv))
		return intel_ddi_dp_voltage_max(encoder);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	else if (IS_IVYBRIDGE(dev_priv) && port == PORT_A)
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
	else if (HAS_PCH_CPT(dev_priv) && port != PORT_A)
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	else
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
}

u8
intel_dp_pre_emphasis_max(struct intel_dp *intel_dp, u8 voltage_swing)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	enum port port = encoder->port;

	if (HAS_DDI(dev_priv)) {
		return intel_ddi_dp_pre_emphasis_max(encoder, voltage_swing);
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			return DP_TRAIN_PRE_EMPH_LEVEL_3;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			return DP_TRAIN_PRE_EMPH_LEVEL_2;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			return DP_TRAIN_PRE_EMPH_LEVEL_1;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
		default:
			return DP_TRAIN_PRE_EMPH_LEVEL_0;
		}
	} else if (IS_IVYBRIDGE(dev_priv) && port == PORT_A) {
		switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			return DP_TRAIN_PRE_EMPH_LEVEL_2;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			return DP_TRAIN_PRE_EMPH_LEVEL_1;
		default:
			return DP_TRAIN_PRE_EMPH_LEVEL_0;
		}
	} else {
		switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			return DP_TRAIN_PRE_EMPH_LEVEL_2;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			return DP_TRAIN_PRE_EMPH_LEVEL_2;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			return DP_TRAIN_PRE_EMPH_LEVEL_1;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
		default:
			return DP_TRAIN_PRE_EMPH_LEVEL_0;
		}
	}
}

static u32 vlv_signal_levels(struct intel_dp *intel_dp)
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
			return 0;
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
			return 0;
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
			return 0;
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
			return 0;
		}
		break;
	default:
		return 0;
	}

	vlv_set_phy_signal_level(encoder, demph_reg_value, preemph_reg_value,
				 uniqtranscale_reg_value, 0);

	return 0;
}

static u32 chv_signal_levels(struct intel_dp *intel_dp)
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
			return 0;
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
			return 0;
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
			return 0;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 43;
			margin_reg_value = 154;
			break;
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	chv_set_phy_signal_level(encoder, deemph_reg_value,
				 margin_reg_value, uniq_trans_scale);

	return 0;
}

static u32
g4x_signal_levels(u8 train_set)
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

/* SNB CPU eDP voltage swing and pre-emphasis control */
static u32
snb_cpu_edp_signal_levels(u8 train_set)
{
	int signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
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

/* IVB CPU eDP voltage swing and pre-emphasis control */
static u32
ivb_cpu_edp_signal_levels(u8 train_set)
{
	int signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					 DP_TRAIN_PRE_EMPHASIS_MASK);
	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_400MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_400MV_3_5DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2:
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

void
intel_dp_set_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->base.port;
	u32 signal_levels, mask = 0;
	u8 train_set = intel_dp->train_set[0];

	if (IS_GEN9_LP(dev_priv) || INTEL_GEN(dev_priv) >= 10) {
		signal_levels = bxt_signal_levels(intel_dp);
	} else if (HAS_DDI(dev_priv)) {
		signal_levels = ddi_signal_levels(intel_dp);
		mask = DDI_BUF_EMP_MASK;
	} else if (IS_CHERRYVIEW(dev_priv)) {
		signal_levels = chv_signal_levels(intel_dp);
	} else if (IS_VALLEYVIEW(dev_priv)) {
		signal_levels = vlv_signal_levels(intel_dp);
	} else if (IS_IVYBRIDGE(dev_priv) && port == PORT_A) {
		signal_levels = ivb_cpu_edp_signal_levels(train_set);
		mask = EDP_LINK_TRAIN_VOL_EMP_MASK_IVB;
	} else if (IS_GEN(dev_priv, 6) && port == PORT_A) {
		signal_levels = snb_cpu_edp_signal_levels(train_set);
		mask = EDP_LINK_TRAIN_VOL_EMP_MASK_SNB;
	} else {
		signal_levels = g4x_signal_levels(train_set);
		mask = DP_VOLTAGE_MASK | DP_PRE_EMPHASIS_MASK;
	}

	if (mask)
		DRM_DEBUG_KMS("Using signal levels %08x\n", signal_levels);

	DRM_DEBUG_KMS("Using vswing level %d\n",
		train_set & DP_TRAIN_VOLTAGE_SWING_MASK);
	DRM_DEBUG_KMS("Using pre-emphasis level %d\n",
		(train_set & DP_TRAIN_PRE_EMPHASIS_MASK) >>
			DP_TRAIN_PRE_EMPHASIS_SHIFT);

	intel_dp->DP = (intel_dp->DP & ~mask) | signal_levels;

	I915_WRITE(intel_dp->output_reg, intel_dp->DP);
	POSTING_READ(intel_dp->output_reg);
}

void
intel_dp_program_link_training_pattern(struct intel_dp *intel_dp,
				       u8 dp_train_pat)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv =
		to_i915(intel_dig_port->base.base.dev);

	_intel_dp_set_link_train(intel_dp, &intel_dp->DP, dp_train_pat);

	I915_WRITE(intel_dp->output_reg, intel_dp->DP);
	POSTING_READ(intel_dp->output_reg);
}

void intel_dp_set_idle_link_train(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->base.port;
	u32 val;

	if (!HAS_DDI(dev_priv))
		return;

	val = I915_READ(DP_TP_CTL(port));
	val &= ~DP_TP_CTL_LINK_TRAIN_MASK;
	val |= DP_TP_CTL_LINK_TRAIN_IDLE;
	I915_WRITE(DP_TP_CTL(port), val);

	/*
	 * On PORT_A we can have only eDP in SST mode. There the only reason
	 * we need to set idle transmission mode is to work around a HW issue
	 * where we enable the pipe while not in idle link-training mode.
	 * In this case there is requirement to wait for a minimum number of
	 * idle patterns to be sent.
	 */
	if (port == PORT_A)
		return;

	if (intel_wait_for_register(&dev_priv->uncore, DP_TP_STATUS(port),
				    DP_TP_STATUS_IDLE_DONE,
				    DP_TP_STATUS_IDLE_DONE,
				    1))
		DRM_ERROR("Timed out waiting for DP idle patterns\n");
}

static void
intel_dp_link_down(struct intel_encoder *encoder,
		   const struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->base.crtc);
	enum port port = encoder->port;
	u32 DP = intel_dp->DP;

	if (WARN_ON((I915_READ(intel_dp->output_reg) & DP_PORT_EN) == 0))
		return;

	DRM_DEBUG_KMS("\n");

	if ((IS_IVYBRIDGE(dev_priv) && port == PORT_A) ||
	    (HAS_PCH_CPT(dev_priv) && port != PORT_A)) {
		DP &= ~DP_LINK_TRAIN_MASK_CPT;
		DP |= DP_LINK_TRAIN_PAT_IDLE_CPT;
	} else {
		DP &= ~DP_LINK_TRAIN_MASK;
		DP |= DP_LINK_TRAIN_PAT_IDLE;
	}
	I915_WRITE(intel_dp->output_reg, DP);
	POSTING_READ(intel_dp->output_reg);

	DP &= ~(DP_PORT_EN | DP_AUDIO_OUTPUT_ENABLE);
	I915_WRITE(intel_dp->output_reg, DP);
	POSTING_READ(intel_dp->output_reg);

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
		I915_WRITE(intel_dp->output_reg, DP);
		POSTING_READ(intel_dp->output_reg);

		DP &= ~DP_PORT_EN;
		I915_WRITE(intel_dp->output_reg, DP);
		POSTING_READ(intel_dp->output_reg);

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

static void
intel_dp_extended_receiver_capabilities(struct intel_dp *intel_dp)
{
	u8 dpcd_ext[6];

	/*
	 * Prior to DP1.3 the bit represented by
	 * DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT was reserved.
	 * if it is set DP_DPCD_REV at 0000h could be at a value less than
	 * the true capability of the panel. The only way to check is to
	 * then compare 0000h and 2200h.
	 */
	if (!(intel_dp->dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
	      DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT))
		return;

	if (drm_dp_dpcd_read(&intel_dp->aux, DP_DP13_DPCD_REV,
			     &dpcd_ext, sizeof(dpcd_ext)) != sizeof(dpcd_ext)) {
		DRM_ERROR("DPCD failed read at extended capabilities\n");
		return;
	}

	if (intel_dp->dpcd[DP_DPCD_REV] > dpcd_ext[DP_DPCD_REV]) {
		DRM_DEBUG_KMS("DPCD extended DPCD rev less than base DPCD rev\n");
		return;
	}

	if (!memcmp(intel_dp->dpcd, dpcd_ext, sizeof(dpcd_ext)))
		return;

	DRM_DEBUG_KMS("Base DPCD: %*ph\n",
		      (int)sizeof(intel_dp->dpcd), intel_dp->dpcd);

	memcpy(intel_dp->dpcd, dpcd_ext, sizeof(dpcd_ext));
}

bool
intel_dp_read_dpcd(struct intel_dp *intel_dp)
{
	if (drm_dp_dpcd_read(&intel_dp->aux, 0x000, intel_dp->dpcd,
			     sizeof(intel_dp->dpcd)) < 0)
		return false; /* aux transfer failed */

	intel_dp_extended_receiver_capabilities(intel_dp);

	DRM_DEBUG_KMS("DPCD: %*ph\n", (int) sizeof(intel_dp->dpcd), intel_dp->dpcd);

	return intel_dp->dpcd[DP_DPCD_REV] != 0;
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
			DRM_ERROR("Failed to read DPCD register 0x%x\n",
				  DP_DSC_SUPPORT);

		DRM_DEBUG_KMS("DSC DPCD: %*ph\n",
			      (int)sizeof(intel_dp->dsc_dpcd),
			      intel_dp->dsc_dpcd);

		/* FEC is supported only on DP 1.4 */
		if (!intel_dp_is_edp(intel_dp) &&
		    drm_dp_dpcd_readb(&intel_dp->aux, DP_FEC_CAPABILITY,
				      &intel_dp->fec_capable) < 0)
			DRM_ERROR("Failed to read FEC DPCD register\n");

		DRM_DEBUG_KMS("FEC CAPABILITY: %x\n", intel_dp->fec_capable);
	}
}

static bool
intel_edp_init_dpcd(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv =
		to_i915(dp_to_dig_port(intel_dp)->base.base.dev);

	/* this function is meant to be called only once */
	WARN_ON(intel_dp->dpcd[DP_DPCD_REV] != 0);

	if (!intel_dp_read_dpcd(intel_dp))
		return false;

	drm_dp_read_desc(&intel_dp->aux, &intel_dp->desc,
			 drm_dp_is_branch(intel_dp->dpcd));

	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11)
		dev_priv->no_aux_handshake = intel_dp->dpcd[DP_MAX_DOWNSPREAD] &
			DP_NO_AUX_HANDSHAKE_LINK_TRAINING;

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
		DRM_DEBUG_KMS("eDP DPCD: %*ph\n", (int) sizeof(intel_dp->edp_dpcd),
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
intel_dp_get_dpcd(struct intel_dp *intel_dp)
{
	if (!intel_dp_read_dpcd(intel_dp))
		return false;

	/* Don't clobber cached eDP rates. */
	if (!intel_dp_is_edp(intel_dp)) {
		intel_dp_set_sink_rates(intel_dp);
		intel_dp_set_common_rates(intel_dp);
	}

	/*
	 * Some eDP panels do not set a valid value for sink count, that is why
	 * it don't care about read it here and in intel_edp_init_dpcd().
	 */
	if (!intel_dp_is_edp(intel_dp)) {
		u8 count;
		ssize_t r;

		r = drm_dp_dpcd_readb(&intel_dp->aux, DP_SINK_COUNT, &count);
		if (r < 1)
			return false;

		/*
		 * Sink count can change between short pulse hpd hence
		 * a member variable in intel_dp will track any changes
		 * between short pulse interrupts.
		 */
		intel_dp->sink_count = DP_GET_SINK_COUNT(count);

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

	if (!drm_dp_is_branch(intel_dp->dpcd))
		return true; /* native DP sink */

	if (intel_dp->dpcd[DP_DPCD_REV] == 0x10)
		return true; /* no per-port downstream info */

	if (drm_dp_dpcd_read(&intel_dp->aux, DP_DOWNSTREAM_PORT_0,
			     intel_dp->downstream_ports,
			     DP_MAX_DOWNSTREAM_PORTS) < 0)
		return false; /* downstream port status fetch failed */

	return true;
}

static bool
intel_dp_sink_can_mst(struct intel_dp *intel_dp)
{
	u8 mstm_cap;

	if (intel_dp->dpcd[DP_DPCD_REV] < 0x12)
		return false;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_MSTM_CAP, &mstm_cap) != 1)
		return false;

	return mstm_cap & DP_MST_CAP;
}

static bool
intel_dp_can_mst(struct intel_dp *intel_dp)
{
	return i915_modparams.enable_dp_mst &&
		intel_dp->can_mst &&
		intel_dp_sink_can_mst(intel_dp);
}

static void
intel_dp_configure_mst(struct intel_dp *intel_dp)
{
	struct intel_encoder *encoder =
		&dp_to_dig_port(intel_dp)->base;
	bool sink_can_mst = intel_dp_sink_can_mst(intel_dp);

	DRM_DEBUG_KMS("MST support? port %c: %s, sink: %s, modparam: %s\n",
		      port_name(encoder->port), yesno(intel_dp->can_mst),
		      yesno(sink_can_mst), yesno(i915_modparams.enable_dp_mst));

	if (!intel_dp->can_mst)
		return;

	intel_dp->is_mst = sink_can_mst &&
		i915_modparams.enable_dp_mst;

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

u16 intel_dp_dsc_get_output_bpp(int link_clock, u8 lane_count,
				int mode_clock, int mode_hdisplay)
{
	u16 bits_per_pixel, max_bpp_small_joiner_ram;
	int i;

	/*
	 * Available Link Bandwidth(Kbits/sec) = (NumberOfLanes)*
	 * (LinkSymbolClock)* 8 * ((100-FECOverhead)/100)*(TimeSlotsPerMTP)
	 * FECOverhead = 2.4%, for SST -> TimeSlotsPerMTP is 1,
	 * for MST -> TimeSlotsPerMTP has to be calculated
	 */
	bits_per_pixel = (link_clock * lane_count * 8 *
			  DP_DSC_FEC_OVERHEAD_FACTOR) /
		mode_clock;

	/* Small Joiner Check: output bpp <= joiner RAM (bits) / Horiz. width */
	max_bpp_small_joiner_ram = DP_DSC_MAX_SMALL_JOINER_RAM_BUFFER /
		mode_hdisplay;

	/*
	 * Greatest allowed DSC BPP = MIN (output BPP from avaialble Link BW
	 * check, output bpp from small joiner RAM check)
	 */
	bits_per_pixel = min(bits_per_pixel, max_bpp_small_joiner_ram);

	/* Error out if the max bpp is less than smallest allowed valid bpp */
	if (bits_per_pixel < valid_dsc_bpp[0]) {
		DRM_DEBUG_KMS("Unsupported BPP %d\n", bits_per_pixel);
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

u8 intel_dp_dsc_get_slice_count(struct intel_dp *intel_dp,
				int mode_clock,
				int mode_hdisplay)
{
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
		DRM_DEBUG_KMS("Unsupported slice width %d by DP DSC Sink device\n",
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

	DRM_DEBUG_KMS("Unsupported Slice Count %d\n", min_slice_count);
	return 0;
}

static void
intel_pixel_encoding_setup_vsc(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct dp_sdp vsc_sdp = {};

	/* Prepare VSC Header for SU as per DP 1.4a spec, Table 2-119 */
	vsc_sdp.sdp_header.HB0 = 0;
	vsc_sdp.sdp_header.HB1 = 0x7;

	/*
	 * VSC SDP supporting 3D stereo, PSR2, and Pixel Encoding/
	 * Colorimetry Format indication.
	 */
	vsc_sdp.sdp_header.HB2 = 0x5;

	/*
	 * VSC SDP supporting 3D stereo, + PSR2, + Pixel Encoding/
	 * Colorimetry Format indication (HB2 = 05h).
	 */
	vsc_sdp.sdp_header.HB3 = 0x13;

	/*
	 * YCbCr 420 = 3h DB16[7:4] ITU-R BT.601 = 0h, ITU-R BT.709 = 1h
	 * DB16[3:0] DP 1.4a spec, Table 2-120
	 */
	vsc_sdp.db[16] = 0x3 << 4; /* 0x3 << 4 , YCbCr 420*/
	/* RGB->YCBCR color conversion uses the BT.709 color space. */
	vsc_sdp.db[16] |= 0x1; /* 0x1, ITU-R BT.709 */

	/*
	 * For pixel encoding formats YCbCr444, YCbCr422, YCbCr420, and Y Only,
	 * the following Component Bit Depth values are defined:
	 * 001b = 8bpc.
	 * 010b = 10bpc.
	 * 011b = 12bpc.
	 * 100b = 16bpc.
	 */
	switch (crtc_state->pipe_bpp) {
	case 24: /* 8bpc */
		vsc_sdp.db[17] = 0x1;
		break;
	case 30: /* 10bpc */
		vsc_sdp.db[17] = 0x2;
		break;
	case 36: /* 12bpc */
		vsc_sdp.db[17] = 0x3;
		break;
	case 48: /* 16bpc */
		vsc_sdp.db[17] = 0x4;
		break;
	default:
		MISSING_CASE(crtc_state->pipe_bpp);
		break;
	}

	/*
	 * Dynamic Range (Bit 7)
	 * 0 = VESA range, 1 = CTA range.
	 * all YCbCr are always limited range
	 */
	vsc_sdp.db[17] |= 0x80;

	/*
	 * Content Type (Bits 2:0)
	 * 000b = Not defined.
	 * 001b = Graphics.
	 * 010b = Photo.
	 * 011b = Video.
	 * 100b = Game
	 * All other values are RESERVED.
	 * Note: See CTA-861-G for the definition and expected
	 * processing by a stream sink for the above contect types.
	 */
	vsc_sdp.db[18] = 0;

	intel_dig_port->write_infoframe(&intel_dig_port->base,
			crtc_state, DP_SDP_VSC, &vsc_sdp, sizeof(vsc_sdp));
}

void intel_dp_ycbcr_420_enable(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_YCBCR420)
		return;

	intel_pixel_encoding_setup_vsc(intel_dp, crtc_state);
}

static u8 intel_dp_autotest_link_training(struct intel_dp *intel_dp)
{
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
		DRM_DEBUG_KMS("Lane count read failed\n");
		return DP_TEST_NAK;
	}
	test_lane_count &= DP_MAX_LANE_COUNT_MASK;

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_LINK_RATE,
				   &test_link_bw);
	if (status <= 0) {
		DRM_DEBUG_KMS("Link Rate read failed\n");
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
	u8 test_pattern;
	u8 test_misc;
	__be16 h_width, v_height;
	int status = 0;

	/* Read the TEST_PATTERN (DP CTS 3.1.5) */
	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_PATTERN,
				   &test_pattern);
	if (status <= 0) {
		DRM_DEBUG_KMS("Test pattern read failed\n");
		return DP_TEST_NAK;
	}
	if (test_pattern != DP_COLOR_RAMP)
		return DP_TEST_NAK;

	status = drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_H_WIDTH_HI,
				  &h_width, 2);
	if (status <= 0) {
		DRM_DEBUG_KMS("H Width read failed\n");
		return DP_TEST_NAK;
	}

	status = drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_V_HEIGHT_HI,
				  &v_height, 2);
	if (status <= 0) {
		DRM_DEBUG_KMS("V Height read failed\n");
		return DP_TEST_NAK;
	}

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_MISC0,
				   &test_misc);
	if (status <= 0) {
		DRM_DEBUG_KMS("TEST MISC read failed\n");
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
	intel_dp->compliance.test_active = 1;

	return DP_TEST_ACK;
}

static u8 intel_dp_autotest_edid(struct intel_dp *intel_dp)
{
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
			DRM_DEBUG_KMS("EDID read had %d NACKs, %d DEFERs\n",
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
			DRM_DEBUG_KMS("Failed to write EDID checksum\n");

		test_result = DP_TEST_ACK | DP_TEST_EDID_CHECKSUM_WRITE;
		intel_dp->compliance.test_data.edid = INTEL_DP_RESOLUTION_PREFERRED;
	}

	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance.test_active = 1;

	return test_result;
}

static u8 intel_dp_autotest_phy_pattern(struct intel_dp *intel_dp)
{
	u8 test_result = DP_TEST_NAK;
	return test_result;
}

static void intel_dp_handle_test_request(struct intel_dp *intel_dp)
{
	u8 response = DP_TEST_NAK;
	u8 request = 0;
	int status;

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_REQUEST, &request);
	if (status <= 0) {
		DRM_DEBUG_KMS("Could not read test request from sink\n");
		goto update_status;
	}

	switch (request) {
	case DP_TEST_LINK_TRAINING:
		DRM_DEBUG_KMS("LINK_TRAINING test requested\n");
		response = intel_dp_autotest_link_training(intel_dp);
		break;
	case DP_TEST_LINK_VIDEO_PATTERN:
		DRM_DEBUG_KMS("TEST_PATTERN test requested\n");
		response = intel_dp_autotest_video_pattern(intel_dp);
		break;
	case DP_TEST_LINK_EDID_READ:
		DRM_DEBUG_KMS("EDID test requested\n");
		response = intel_dp_autotest_edid(intel_dp);
		break;
	case DP_TEST_LINK_PHY_TEST_PATTERN:
		DRM_DEBUG_KMS("PHY_PATTERN test requested\n");
		response = intel_dp_autotest_phy_pattern(intel_dp);
		break;
	default:
		DRM_DEBUG_KMS("Invalid test request '%02x'\n", request);
		break;
	}

	if (response & DP_TEST_ACK)
		intel_dp->compliance.test_type = request;

update_status:
	status = drm_dp_dpcd_writeb(&intel_dp->aux, DP_TEST_RESPONSE, response);
	if (status <= 0)
		DRM_DEBUG_KMS("Could not write test response to sink\n");
}

static int
intel_dp_check_mst_status(struct intel_dp *intel_dp)
{
	bool bret;

	if (intel_dp->is_mst) {
		u8 esi[DP_DPRX_ESI_LEN] = { 0 };
		int ret = 0;
		int retry;
		bool handled;

		WARN_ON_ONCE(intel_dp->active_mst_links < 0);
		bret = intel_dp_get_sink_irq_esi(intel_dp, esi);
go_again:
		if (bret == true) {

			/* check link status - esi[10] = 0x200c */
			if (intel_dp->active_mst_links > 0 &&
			    !drm_dp_channel_eq_ok(&esi[10], intel_dp->lane_count)) {
				DRM_DEBUG_KMS("channel EQ not ok, retraining\n");
				intel_dp_start_link_train(intel_dp);
				intel_dp_stop_link_train(intel_dp);
			}

			DRM_DEBUG_KMS("got esi %3ph\n", esi);
			ret = drm_dp_mst_hpd_irq(&intel_dp->mst_mgr, esi, &handled);

			if (handled) {
				for (retry = 0; retry < 3; retry++) {
					int wret;
					wret = drm_dp_dpcd_write(&intel_dp->aux,
								 DP_SINK_COUNT_ESI+1,
								 &esi[1], 3);
					if (wret == 3) {
						break;
					}
				}

				bret = intel_dp_get_sink_irq_esi(intel_dp, esi);
				if (bret == true) {
					DRM_DEBUG_KMS("got esi2 %3ph\n", esi);
					goto go_again;
				}
			} else
				ret = 0;

			return ret;
		} else {
			DRM_DEBUG_KMS("failed to get ESI - device may have failed\n");
			intel_dp->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
							intel_dp->is_mst);
		}
	}
	return -EINVAL;
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

int intel_dp_retrain_link(struct intel_encoder *encoder,
			  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_connector *connector = intel_dp->attached_connector;
	struct drm_connector_state *conn_state;
	struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	int ret;

	/* FIXME handle the MST connectors as well */

	if (!connector || connector->base.status != connector_status_connected)
		return 0;

	ret = drm_modeset_lock(&dev_priv->drm.mode_config.connection_mutex,
			       ctx);
	if (ret)
		return ret;

	conn_state = connector->base.state;

	crtc = to_intel_crtc(conn_state->crtc);
	if (!crtc)
		return 0;

	ret = drm_modeset_lock(&crtc->base.mutex, ctx);
	if (ret)
		return ret;

	crtc_state = to_intel_crtc_state(crtc->base.state);

	WARN_ON(!intel_crtc_has_dp_encoder(crtc_state));

	if (!crtc_state->base.active)
		return 0;

	if (conn_state->commit &&
	    !try_wait_for_completion(&conn_state->commit->hw_done))
		return 0;

	if (!intel_dp_needs_link_retrain(intel_dp))
		return 0;

	/* Suppress underruns caused by re-training */
	intel_set_cpu_fifo_underrun_reporting(dev_priv, crtc->pipe, false);
	if (crtc_state->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv,
						      intel_crtc_pch_transcoder(crtc), false);

	intel_dp_start_link_train(intel_dp);
	intel_dp_stop_link_train(intel_dp);

	/* Keep underrun reporting disabled until things are stable */
	intel_wait_for_vblank(dev_priv, crtc->pipe);

	intel_set_cpu_fifo_underrun_reporting(dev_priv, crtc->pipe, true);
	if (crtc_state->has_pch_encoder)
		intel_set_pch_fifo_underrun_reporting(dev_priv,
						      intel_crtc_pch_transcoder(crtc), true);

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
static bool intel_dp_hotplug(struct intel_encoder *encoder,
			     struct intel_connector *connector)
{
	struct drm_modeset_acquire_ctx ctx;
	bool changed;
	int ret;

	changed = intel_encoder_hotplug(encoder, connector);

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
	WARN(ret, "Acquiring modeset locks failed with %i\n", ret);

	return changed;
}

static void intel_dp_check_service_irq(struct intel_dp *intel_dp)
{
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
		DRM_DEBUG_DRIVER("Sink specific irq unhandled\n");
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
		DRM_DEBUG_KMS("Link Training Compliance Test requested\n");
		/* Send a Hotplug Uevent to userspace to start modeset */
		drm_kms_helper_hotplug_event(&dev_priv->drm);
	}

	return true;
}

/* XXX this is probably wrong for multiple downstream ports */
static enum drm_connector_status
intel_dp_detect_dpcd(struct intel_dp *intel_dp)
{
	struct intel_lspcon *lspcon = dp_to_lspcon(intel_dp);
	u8 *dpcd = intel_dp->dpcd;
	u8 type;

	if (WARN_ON(intel_dp_is_edp(intel_dp)))
		return connector_status_connected;

	if (lspcon->active)
		lspcon_resume(lspcon);

	if (!intel_dp_get_dpcd(intel_dp))
		return connector_status_disconnected;

	/* if there's no downstream port, we're done */
	if (!drm_dp_is_branch(dpcd))
		return connector_status_connected;

	/* If we're HPD-aware, SINK_COUNT changes dynamically */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
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
	DRM_DEBUG_KMS("Broken DP branch device, ignoring\n");
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
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_B:
		bit = SDE_PORTB_HOTPLUG;
		break;
	case HPD_PORT_C:
		bit = SDE_PORTC_HOTPLUG;
		break;
	case HPD_PORT_D:
		bit = SDE_PORTD_HOTPLUG;
		break;
	default:
		MISSING_CASE(encoder->hpd_pin);
		return false;
	}

	return I915_READ(SDEISR) & bit;
}

static bool cpt_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_B:
		bit = SDE_PORTB_HOTPLUG_CPT;
		break;
	case HPD_PORT_C:
		bit = SDE_PORTC_HOTPLUG_CPT;
		break;
	case HPD_PORT_D:
		bit = SDE_PORTD_HOTPLUG_CPT;
		break;
	default:
		MISSING_CASE(encoder->hpd_pin);
		return false;
	}

	return I915_READ(SDEISR) & bit;
}

static bool spt_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_A:
		bit = SDE_PORTA_HOTPLUG_SPT;
		break;
	case HPD_PORT_E:
		bit = SDE_PORTE_HOTPLUG_SPT;
		break;
	default:
		return cpt_digital_port_connected(encoder);
	}

	return I915_READ(SDEISR) & bit;
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

	return I915_READ(PORT_HOTPLUG_STAT) & bit;
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

	return I915_READ(PORT_HOTPLUG_STAT) & bit;
}

static bool ilk_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (encoder->hpd_pin == HPD_PORT_A)
		return I915_READ(DEISR) & DE_DP_A_HOTPLUG;
	else
		return ibx_digital_port_connected(encoder);
}

static bool snb_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (encoder->hpd_pin == HPD_PORT_A)
		return I915_READ(DEISR) & DE_DP_A_HOTPLUG;
	else
		return cpt_digital_port_connected(encoder);
}

static bool ivb_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (encoder->hpd_pin == HPD_PORT_A)
		return I915_READ(DEISR) & DE_DP_A_HOTPLUG_IVB;
	else
		return cpt_digital_port_connected(encoder);
}

static bool bdw_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (encoder->hpd_pin == HPD_PORT_A)
		return I915_READ(GEN8_DE_PORT_ISR) & GEN8_PORT_DP_A_HOTPLUG;
	else
		return cpt_digital_port_connected(encoder);
}

static bool bxt_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_A:
		bit = BXT_DE_PORT_HP_DDIA;
		break;
	case HPD_PORT_B:
		bit = BXT_DE_PORT_HP_DDIB;
		break;
	case HPD_PORT_C:
		bit = BXT_DE_PORT_HP_DDIC;
		break;
	default:
		MISSING_CASE(encoder->hpd_pin);
		return false;
	}

	return I915_READ(GEN8_DE_PORT_ISR) & bit;
}

static bool icl_combo_port_connected(struct drm_i915_private *dev_priv,
				     struct intel_digital_port *intel_dig_port)
{
	enum port port = intel_dig_port->base.port;

	return I915_READ(SDEISR) & SDE_DDI_HOTPLUG_ICP(port);
}

static const char *tc_type_name(enum tc_port_type type)
{
	static const char * const names[] = {
		[TC_PORT_UNKNOWN] = "unknown",
		[TC_PORT_LEGACY] = "legacy",
		[TC_PORT_TYPEC] = "typec",
		[TC_PORT_TBT] = "tbt",
	};

	if (WARN_ON(type >= ARRAY_SIZE(names)))
		type = TC_PORT_UNKNOWN;

	return names[type];
}

static void icl_update_tc_port_type(struct drm_i915_private *dev_priv,
				    struct intel_digital_port *intel_dig_port,
				    bool is_legacy, bool is_typec, bool is_tbt)
{
	enum port port = intel_dig_port->base.port;
	enum tc_port_type old_type = intel_dig_port->tc_type;

	WARN_ON(is_legacy + is_typec + is_tbt != 1);

	if (is_legacy)
		intel_dig_port->tc_type = TC_PORT_LEGACY;
	else if (is_typec)
		intel_dig_port->tc_type = TC_PORT_TYPEC;
	else if (is_tbt)
		intel_dig_port->tc_type = TC_PORT_TBT;
	else
		return;

	/* Types are not supposed to be changed at runtime. */
	WARN_ON(old_type != TC_PORT_UNKNOWN &&
		old_type != intel_dig_port->tc_type);

	if (old_type != intel_dig_port->tc_type)
		DRM_DEBUG_KMS("Port %c has TC type %s\n", port_name(port),
			      tc_type_name(intel_dig_port->tc_type));
}

/*
 * This function implements the first part of the Connect Flow described by our
 * specification, Gen11 TypeC Programming chapter. The rest of the flow (reading
 * lanes, EDID, etc) is done as needed in the typical places.
 *
 * Unlike the other ports, type-C ports are not available to use as soon as we
 * get a hotplug. The type-C PHYs can be shared between multiple controllers:
 * display, USB, etc. As a result, handshaking through FIA is required around
 * connect and disconnect to cleanly transfer ownership with the controller and
 * set the type-C power state.
 *
 * We could opt to only do the connect flow when we actually try to use the AUX
 * channels or do a modeset, then immediately run the disconnect flow after
 * usage, but there are some implications on this for a dynamic environment:
 * things may go away or change behind our backs. So for now our driver is
 * always trying to acquire ownership of the controller as soon as it gets an
 * interrupt (or polls state and sees a port is connected) and only gives it
 * back when it sees a disconnect. Implementation of a more fine-grained model
 * will require a lot of coordination with user space and thorough testing for
 * the extra possible cases.
 */
static bool icl_tc_phy_connect(struct drm_i915_private *dev_priv,
			       struct intel_digital_port *dig_port)
{
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	u32 val;

	if (dig_port->tc_type != TC_PORT_LEGACY &&
	    dig_port->tc_type != TC_PORT_TYPEC)
		return true;

	val = I915_READ(PORT_TX_DFLEXDPPMS);
	if (!(val & DP_PHY_MODE_STATUS_COMPLETED(tc_port))) {
		DRM_DEBUG_KMS("DP PHY for TC port %d not ready\n", tc_port);
		WARN_ON(dig_port->tc_legacy_port);
		return false;
	}

	/*
	 * This function may be called many times in a row without an HPD event
	 * in between, so try to avoid the write when we can.
	 */
	val = I915_READ(PORT_TX_DFLEXDPCSSS);
	if (!(val & DP_PHY_MODE_STATUS_NOT_SAFE(tc_port))) {
		val |= DP_PHY_MODE_STATUS_NOT_SAFE(tc_port);
		I915_WRITE(PORT_TX_DFLEXDPCSSS, val);
	}

	/*
	 * Now we have to re-check the live state, in case the port recently
	 * became disconnected. Not necessary for legacy mode.
	 */
	if (dig_port->tc_type == TC_PORT_TYPEC &&
	    !(I915_READ(PORT_TX_DFLEXDPSP) & TC_LIVE_STATE_TC(tc_port))) {
		DRM_DEBUG_KMS("TC PHY %d sudden disconnect.\n", tc_port);
		icl_tc_phy_disconnect(dev_priv, dig_port);
		return false;
	}

	return true;
}

/*
 * See the comment at the connect function. This implements the Disconnect
 * Flow.
 */
void icl_tc_phy_disconnect(struct drm_i915_private *dev_priv,
			   struct intel_digital_port *dig_port)
{
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);

	if (dig_port->tc_type == TC_PORT_UNKNOWN)
		return;

	/*
	 * TBT disconnection flow is read the live status, what was done in
	 * caller.
	 */
	if (dig_port->tc_type == TC_PORT_TYPEC ||
	    dig_port->tc_type == TC_PORT_LEGACY) {
		u32 val;

		val = I915_READ(PORT_TX_DFLEXDPCSSS);
		val &= ~DP_PHY_MODE_STATUS_NOT_SAFE(tc_port);
		I915_WRITE(PORT_TX_DFLEXDPCSSS, val);
	}

	DRM_DEBUG_KMS("Port %c TC type %s disconnected\n",
		      port_name(dig_port->base.port),
		      tc_type_name(dig_port->tc_type));

	dig_port->tc_type = TC_PORT_UNKNOWN;
}

/*
 * The type-C ports are different because even when they are connected, they may
 * not be available/usable by the graphics driver: see the comment on
 * icl_tc_phy_connect(). So in our driver instead of adding the additional
 * concept of "usable" and make everything check for "connected and usable" we
 * define a port as "connected" when it is not only connected, but also when it
 * is usable by the rest of the driver. That maintains the old assumption that
 * connected ports are usable, and avoids exposing to the users objects they
 * can't really use.
 */
static bool icl_tc_port_connected(struct drm_i915_private *dev_priv,
				  struct intel_digital_port *intel_dig_port)
{
	enum port port = intel_dig_port->base.port;
	enum tc_port tc_port = intel_port_to_tc(dev_priv, port);
	bool is_legacy, is_typec, is_tbt;
	u32 dpsp;

	/*
	 * Complain if we got a legacy port HPD, but VBT didn't mark the port as
	 * legacy. Treat the port as legacy from now on.
	 */
	if (!intel_dig_port->tc_legacy_port &&
	    I915_READ(SDEISR) & SDE_TC_HOTPLUG_ICP(tc_port)) {
		DRM_ERROR("VBT incorrectly claims port %c is not TypeC legacy\n",
			  port_name(port));
		intel_dig_port->tc_legacy_port = true;
	}
	is_legacy = intel_dig_port->tc_legacy_port;

	/*
	 * The spec says we shouldn't be using the ISR bits for detecting
	 * between TC and TBT. We should use DFLEXDPSP.
	 */
	dpsp = I915_READ(PORT_TX_DFLEXDPSP);
	is_typec = dpsp & TC_LIVE_STATE_TC(tc_port);
	is_tbt = dpsp & TC_LIVE_STATE_TBT(tc_port);

	if (!is_legacy && !is_typec && !is_tbt) {
		icl_tc_phy_disconnect(dev_priv, intel_dig_port);

		return false;
	}

	icl_update_tc_port_type(dev_priv, intel_dig_port, is_legacy, is_typec,
				is_tbt);

	if (!icl_tc_phy_connect(dev_priv, intel_dig_port))
		return false;

	return true;
}

static bool icl_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(&encoder->base);

	if (intel_port_is_combophy(dev_priv, encoder->port))
		return icl_combo_port_connected(dev_priv, dig_port);
	else if (intel_port_is_tc(dev_priv, encoder->port))
		return icl_tc_port_connected(dev_priv, dig_port);
	else
		MISSING_CASE(encoder->hpd_pin);

	return false;
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
static bool __intel_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (HAS_GMCH(dev_priv)) {
		if (IS_GM45(dev_priv))
			return gm45_digital_port_connected(encoder);
		else
			return g4x_digital_port_connected(encoder);
	}

	if (INTEL_GEN(dev_priv) >= 11)
		return icl_digital_port_connected(encoder);
	else if (IS_GEN(dev_priv, 10) || IS_GEN9_BC(dev_priv))
		return spt_digital_port_connected(encoder);
	else if (IS_GEN9_LP(dev_priv))
		return bxt_digital_port_connected(encoder);
	else if (IS_GEN(dev_priv, 8))
		return bdw_digital_port_connected(encoder);
	else if (IS_GEN(dev_priv, 7))
		return ivb_digital_port_connected(encoder);
	else if (IS_GEN(dev_priv, 6))
		return snb_digital_port_connected(encoder);
	else if (IS_GEN(dev_priv, 5))
		return ilk_digital_port_connected(encoder);

	MISSING_CASE(INTEL_GEN(dev_priv));
	return false;
}

bool intel_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	bool is_connected = false;
	intel_wakeref_t wakeref;

	with_intel_display_power(dev_priv, POWER_DOMAIN_DISPLAY_CORE, wakeref)
		is_connected = __intel_digital_port_connected(encoder);

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
intel_dp_set_edid(struct intel_dp *intel_dp)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct edid *edid;

	intel_dp_unset_edid(intel_dp);
	edid = intel_dp_get_edid(intel_dp);
	intel_connector->detect_edid = edid;

	intel_dp->has_audio = drm_detect_monitor_audio(edid);
	drm_dp_cec_set_edid(&intel_dp->aux, edid);
}

static void
intel_dp_unset_edid(struct intel_dp *intel_dp)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;

	drm_dp_cec_unset_edid(&intel_dp->aux);
	kfree(intel_connector->detect_edid);
	intel_connector->detect_edid = NULL;

	intel_dp->has_audio = false;
}

static int
intel_dp_detect(struct drm_connector *connector,
		struct drm_modeset_acquire_ctx *ctx,
		bool force)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	enum drm_connector_status status;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);
	WARN_ON(!drm_modeset_is_locked(&dev_priv->drm.mode_config.connection_mutex));

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
			DRM_DEBUG_KMS("MST device may have disappeared %d vs %d\n",
				      intel_dp->is_mst,
				      intel_dp->mst_mgr.mst_state);
			intel_dp->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
							intel_dp->is_mst);
		}

		goto out;
	}

	if (intel_dp->reset_link_params) {
		/* Initial max link lane count */
		intel_dp->max_link_lane_count = intel_dp_max_common_lane_count(intel_dp);

		/* Initial max link rate */
		intel_dp->max_link_rate = intel_dp_max_common_rate(intel_dp);

		intel_dp->reset_link_params = false;
	}

	intel_dp_print_rates(intel_dp);

	/* Read DP Sink DSC Cap DPCD regs for DP v1.4 */
	if (INTEL_GEN(dev_priv) >= 11)
		intel_dp_get_dsc_sink_cap(intel_dp);

	drm_dp_read_desc(&intel_dp->aux, &intel_dp->desc,
			 drm_dp_is_branch(intel_dp->dpcd));

	intel_dp_configure_mst(intel_dp);

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

	return status;
}

static void
intel_dp_force(struct drm_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &dig_port->base;
	struct drm_i915_private *dev_priv = to_i915(intel_encoder->base.dev);
	enum intel_display_power_domain aux_domain =
		intel_aux_power_domain(dig_port);
	intel_wakeref_t wakeref;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
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
	if (intel_dp_is_edp(intel_attached_dp(connector)) &&
	    intel_connector->panel.fixed_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev,
					  intel_connector->panel.fixed_mode);
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
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct drm_device *dev = connector->dev;
	int ret;

	ret = intel_connector_register(connector);
	if (ret)
		return ret;

	i915_debugfs_connector_add(connector);

	DRM_DEBUG_KMS("registering %s bus for %s\n",
		      intel_dp->aux.name, connector->kdev->kobj.name);

	intel_dp->aux.dev = connector->kdev;
	ret = drm_dp_aux_register(&intel_dp->aux);
	if (!ret)
		drm_dp_cec_register_connector(&intel_dp->aux,
					      connector->name, dev->dev);
	return ret;
}

static void
intel_dp_connector_unregister(struct drm_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);

	drm_dp_cec_unregister_connector(&intel_dp->aux);
	drm_dp_aux_unregister(&intel_dp->aux);
	intel_connector_unregister(connector);
}

void intel_dp_encoder_flush_work(struct drm_encoder *encoder)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &intel_dig_port->dp;

	intel_dp_mst_encoder_cleanup(intel_dig_port);
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
	kfree(enc_to_dig_port(encoder));
}

void intel_dp_encoder_suspend(struct intel_encoder *intel_encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);
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

static void intel_dp_hdcp_wait_for_cp_irq(struct intel_hdcp *hdcp, int timeout)
{
	long ret;

#define C (hdcp->cp_irq_count_cached != atomic_read(&hdcp->cp_irq_count))
	ret = wait_event_interruptible_timeout(hdcp->cp_irq_queue, C,
					       msecs_to_jiffies(timeout));

	if (!ret)
		DRM_DEBUG_KMS("Timedout at waiting for CP_IRQ\n");
}

static
int intel_dp_hdcp_write_an_aksv(struct intel_digital_port *intel_dig_port,
				u8 *an)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_dig_port->base.base);
	static const struct drm_dp_aux_msg msg = {
		.request = DP_AUX_NATIVE_WRITE,
		.address = DP_AUX_HDCP_AKSV,
		.size = DRM_HDCP_KSV_LEN,
	};
	u8 txbuf[HEADER_SIZE + DRM_HDCP_KSV_LEN] = {}, rxbuf[2], reply = 0;
	ssize_t dpcd_ret;
	int ret;

	/* Output An first, that's easy */
	dpcd_ret = drm_dp_dpcd_write(&intel_dig_port->dp.aux, DP_AUX_HDCP_AN,
				     an, DRM_HDCP_AN_LEN);
	if (dpcd_ret != DRM_HDCP_AN_LEN) {
		DRM_DEBUG_KMS("Failed to write An over DP/AUX (%zd)\n",
			      dpcd_ret);
		return dpcd_ret >= 0 ? -EIO : dpcd_ret;
	}

	/*
	 * Since Aksv is Oh-So-Secret, we can't access it in software. So in
	 * order to get it on the wire, we need to create the AUX header as if
	 * we were writing the data, and then tickle the hardware to output the
	 * data once the header is sent out.
	 */
	intel_dp_aux_header(txbuf, &msg);

	ret = intel_dp_aux_xfer(intel_dp, txbuf, HEADER_SIZE + msg.size,
				rxbuf, sizeof(rxbuf),
				DP_AUX_CH_CTL_AUX_AKSV_SELECT);
	if (ret < 0) {
		DRM_DEBUG_KMS("Write Aksv over DP/AUX failed (%d)\n", ret);
		return ret;
	} else if (ret == 0) {
		DRM_DEBUG_KMS("Aksv write over DP/AUX was empty\n");
		return -EIO;
	}

	reply = (rxbuf[0] >> 4) & DP_AUX_NATIVE_REPLY_MASK;
	if (reply != DP_AUX_NATIVE_REPLY_ACK) {
		DRM_DEBUG_KMS("Aksv write: no DP_AUX_NATIVE_REPLY_ACK %x\n",
			      reply);
		return -EIO;
	}
	return 0;
}

static int intel_dp_hdcp_read_bksv(struct intel_digital_port *intel_dig_port,
				   u8 *bksv)
{
	ssize_t ret;
	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux, DP_AUX_HDCP_BKSV, bksv,
			       DRM_HDCP_KSV_LEN);
	if (ret != DRM_HDCP_KSV_LEN) {
		DRM_DEBUG_KMS("Read Bksv from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static int intel_dp_hdcp_read_bstatus(struct intel_digital_port *intel_dig_port,
				      u8 *bstatus)
{
	ssize_t ret;
	/*
	 * For some reason the HDMI and DP HDCP specs call this register
	 * definition by different names. In the HDMI spec, it's called BSTATUS,
	 * but in DP it's called BINFO.
	 */
	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux, DP_AUX_HDCP_BINFO,
			       bstatus, DRM_HDCP_BSTATUS_LEN);
	if (ret != DRM_HDCP_BSTATUS_LEN) {
		DRM_DEBUG_KMS("Read bstatus from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static
int intel_dp_hdcp_read_bcaps(struct intel_digital_port *intel_dig_port,
			     u8 *bcaps)
{
	ssize_t ret;

	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux, DP_AUX_HDCP_BCAPS,
			       bcaps, 1);
	if (ret != 1) {
		DRM_DEBUG_KMS("Read bcaps from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}

	return 0;
}

static
int intel_dp_hdcp_repeater_present(struct intel_digital_port *intel_dig_port,
				   bool *repeater_present)
{
	ssize_t ret;
	u8 bcaps;

	ret = intel_dp_hdcp_read_bcaps(intel_dig_port, &bcaps);
	if (ret)
		return ret;

	*repeater_present = bcaps & DP_BCAPS_REPEATER_PRESENT;
	return 0;
}

static
int intel_dp_hdcp_read_ri_prime(struct intel_digital_port *intel_dig_port,
				u8 *ri_prime)
{
	ssize_t ret;
	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux, DP_AUX_HDCP_RI_PRIME,
			       ri_prime, DRM_HDCP_RI_LEN);
	if (ret != DRM_HDCP_RI_LEN) {
		DRM_DEBUG_KMS("Read Ri' from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static
int intel_dp_hdcp_read_ksv_ready(struct intel_digital_port *intel_dig_port,
				 bool *ksv_ready)
{
	ssize_t ret;
	u8 bstatus;
	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux, DP_AUX_HDCP_BSTATUS,
			       &bstatus, 1);
	if (ret != 1) {
		DRM_DEBUG_KMS("Read bstatus from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}
	*ksv_ready = bstatus & DP_BSTATUS_READY;
	return 0;
}

static
int intel_dp_hdcp_read_ksv_fifo(struct intel_digital_port *intel_dig_port,
				int num_downstream, u8 *ksv_fifo)
{
	ssize_t ret;
	int i;

	/* KSV list is read via 15 byte window (3 entries @ 5 bytes each) */
	for (i = 0; i < num_downstream; i += 3) {
		size_t len = min(num_downstream - i, 3) * DRM_HDCP_KSV_LEN;
		ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux,
				       DP_AUX_HDCP_KSV_FIFO,
				       ksv_fifo + i * DRM_HDCP_KSV_LEN,
				       len);
		if (ret != len) {
			DRM_DEBUG_KMS("Read ksv[%d] from DP/AUX failed (%zd)\n",
				      i, ret);
			return ret >= 0 ? -EIO : ret;
		}
	}
	return 0;
}

static
int intel_dp_hdcp_read_v_prime_part(struct intel_digital_port *intel_dig_port,
				    int i, u32 *part)
{
	ssize_t ret;

	if (i >= DRM_HDCP_V_PRIME_NUM_PARTS)
		return -EINVAL;

	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux,
			       DP_AUX_HDCP_V_PRIME(i), part,
			       DRM_HDCP_V_PRIME_PART_LEN);
	if (ret != DRM_HDCP_V_PRIME_PART_LEN) {
		DRM_DEBUG_KMS("Read v'[%d] from DP/AUX failed (%zd)\n", i, ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static
int intel_dp_hdcp_toggle_signalling(struct intel_digital_port *intel_dig_port,
				    bool enable)
{
	/* Not used for single stream DisplayPort setups */
	return 0;
}

static
bool intel_dp_hdcp_check_link(struct intel_digital_port *intel_dig_port)
{
	ssize_t ret;
	u8 bstatus;

	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux, DP_AUX_HDCP_BSTATUS,
			       &bstatus, 1);
	if (ret != 1) {
		DRM_DEBUG_KMS("Read bstatus from DP/AUX failed (%zd)\n", ret);
		return false;
	}

	return !(bstatus & (DP_BSTATUS_LINK_FAILURE | DP_BSTATUS_REAUTH_REQ));
}

static
int intel_dp_hdcp_capable(struct intel_digital_port *intel_dig_port,
			  bool *hdcp_capable)
{
	ssize_t ret;
	u8 bcaps;

	ret = intel_dp_hdcp_read_bcaps(intel_dig_port, &bcaps);
	if (ret)
		return ret;

	*hdcp_capable = bcaps & DP_BCAPS_HDCP_CAPABLE;
	return 0;
}

struct hdcp2_dp_errata_stream_type {
	u8	msg_id;
	u8	stream_type;
} __packed;

static struct hdcp2_dp_msg_data {
	u8 msg_id;
	u32 offset;
	bool msg_detectable;
	u32 timeout;
	u32 timeout2; /* Added for non_paired situation */
	} hdcp2_msg_data[] = {
		{HDCP_2_2_AKE_INIT, DP_HDCP_2_2_AKE_INIT_OFFSET, false, 0, 0},
		{HDCP_2_2_AKE_SEND_CERT, DP_HDCP_2_2_AKE_SEND_CERT_OFFSET,
				false, HDCP_2_2_CERT_TIMEOUT_MS, 0},
		{HDCP_2_2_AKE_NO_STORED_KM, DP_HDCP_2_2_AKE_NO_STORED_KM_OFFSET,
				false, 0, 0},
		{HDCP_2_2_AKE_STORED_KM, DP_HDCP_2_2_AKE_STORED_KM_OFFSET,
				false, 0, 0},
		{HDCP_2_2_AKE_SEND_HPRIME, DP_HDCP_2_2_AKE_SEND_HPRIME_OFFSET,
				true, HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS,
				HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS},
		{HDCP_2_2_AKE_SEND_PAIRING_INFO,
				DP_HDCP_2_2_AKE_SEND_PAIRING_INFO_OFFSET, true,
				HDCP_2_2_PAIRING_TIMEOUT_MS, 0},
		{HDCP_2_2_LC_INIT, DP_HDCP_2_2_LC_INIT_OFFSET, false, 0, 0},
		{HDCP_2_2_LC_SEND_LPRIME, DP_HDCP_2_2_LC_SEND_LPRIME_OFFSET,
				false, HDCP_2_2_DP_LPRIME_TIMEOUT_MS, 0},
		{HDCP_2_2_SKE_SEND_EKS, DP_HDCP_2_2_SKE_SEND_EKS_OFFSET, false,
				0, 0},
		{HDCP_2_2_REP_SEND_RECVID_LIST,
				DP_HDCP_2_2_REP_SEND_RECVID_LIST_OFFSET, true,
				HDCP_2_2_RECVID_LIST_TIMEOUT_MS, 0},
		{HDCP_2_2_REP_SEND_ACK, DP_HDCP_2_2_REP_SEND_ACK_OFFSET, false,
				0, 0},
		{HDCP_2_2_REP_STREAM_MANAGE,
				DP_HDCP_2_2_REP_STREAM_MANAGE_OFFSET, false,
				0, 0},
		{HDCP_2_2_REP_STREAM_READY, DP_HDCP_2_2_REP_STREAM_READY_OFFSET,
				false, HDCP_2_2_STREAM_READY_TIMEOUT_MS, 0},
/* local define to shovel this through the write_2_2 interface */
#define HDCP_2_2_ERRATA_DP_STREAM_TYPE	50
		{HDCP_2_2_ERRATA_DP_STREAM_TYPE,
				DP_HDCP_2_2_REG_STREAM_TYPE_OFFSET, false,
				0, 0},
		};

static inline
int intel_dp_hdcp2_read_rx_status(struct intel_digital_port *intel_dig_port,
				  u8 *rx_status)
{
	ssize_t ret;

	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux,
			       DP_HDCP_2_2_REG_RXSTATUS_OFFSET, rx_status,
			       HDCP_2_2_DP_RXSTATUS_LEN);
	if (ret != HDCP_2_2_DP_RXSTATUS_LEN) {
		DRM_DEBUG_KMS("Read bstatus from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}

	return 0;
}

static
int hdcp2_detect_msg_availability(struct intel_digital_port *intel_dig_port,
				  u8 msg_id, bool *msg_ready)
{
	u8 rx_status;
	int ret;

	*msg_ready = false;
	ret = intel_dp_hdcp2_read_rx_status(intel_dig_port, &rx_status);
	if (ret < 0)
		return ret;

	switch (msg_id) {
	case HDCP_2_2_AKE_SEND_HPRIME:
		if (HDCP_2_2_DP_RXSTATUS_H_PRIME(rx_status))
			*msg_ready = true;
		break;
	case HDCP_2_2_AKE_SEND_PAIRING_INFO:
		if (HDCP_2_2_DP_RXSTATUS_PAIRING(rx_status))
			*msg_ready = true;
		break;
	case HDCP_2_2_REP_SEND_RECVID_LIST:
		if (HDCP_2_2_DP_RXSTATUS_READY(rx_status))
			*msg_ready = true;
		break;
	default:
		DRM_ERROR("Unidentified msg_id: %d\n", msg_id);
		return -EINVAL;
	}

	return 0;
}

static ssize_t
intel_dp_hdcp2_wait_for_msg(struct intel_digital_port *intel_dig_port,
			    struct hdcp2_dp_msg_data *hdcp2_msg_data)
{
	struct intel_dp *dp = &intel_dig_port->dp;
	struct intel_hdcp *hdcp = &dp->attached_connector->hdcp;
	u8 msg_id = hdcp2_msg_data->msg_id;
	int ret, timeout;
	bool msg_ready = false;

	if (msg_id == HDCP_2_2_AKE_SEND_HPRIME && !hdcp->is_paired)
		timeout = hdcp2_msg_data->timeout2;
	else
		timeout = hdcp2_msg_data->timeout;

	/*
	 * There is no way to detect the CERT, LPRIME and STREAM_READY
	 * availability. So Wait for timeout and read the msg.
	 */
	if (!hdcp2_msg_data->msg_detectable) {
		mdelay(timeout);
		ret = 0;
	} else {
		/*
		 * As we want to check the msg availability at timeout, Ignoring
		 * the timeout at wait for CP_IRQ.
		 */
		intel_dp_hdcp_wait_for_cp_irq(hdcp, timeout);
		ret = hdcp2_detect_msg_availability(intel_dig_port,
						    msg_id, &msg_ready);
		if (!msg_ready)
			ret = -ETIMEDOUT;
	}

	if (ret)
		DRM_DEBUG_KMS("msg_id %d, ret %d, timeout(mSec): %d\n",
			      hdcp2_msg_data->msg_id, ret, timeout);

	return ret;
}

static struct hdcp2_dp_msg_data *get_hdcp2_dp_msg_data(u8 msg_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdcp2_msg_data); i++)
		if (hdcp2_msg_data[i].msg_id == msg_id)
			return &hdcp2_msg_data[i];

	return NULL;
}

static
int intel_dp_hdcp2_write_msg(struct intel_digital_port *intel_dig_port,
			     void *buf, size_t size)
{
	struct intel_dp *dp = &intel_dig_port->dp;
	struct intel_hdcp *hdcp = &dp->attached_connector->hdcp;
	unsigned int offset;
	u8 *byte = buf;
	ssize_t ret, bytes_to_write, len;
	struct hdcp2_dp_msg_data *hdcp2_msg_data;

	hdcp2_msg_data = get_hdcp2_dp_msg_data(*byte);
	if (!hdcp2_msg_data)
		return -EINVAL;

	offset = hdcp2_msg_data->offset;

	/* No msg_id in DP HDCP2.2 msgs */
	bytes_to_write = size - 1;
	byte++;

	hdcp->cp_irq_count_cached = atomic_read(&hdcp->cp_irq_count);

	while (bytes_to_write) {
		len = bytes_to_write > DP_AUX_MAX_PAYLOAD_BYTES ?
				DP_AUX_MAX_PAYLOAD_BYTES : bytes_to_write;

		ret = drm_dp_dpcd_write(&intel_dig_port->dp.aux,
					offset, (void *)byte, len);
		if (ret < 0)
			return ret;

		bytes_to_write -= ret;
		byte += ret;
		offset += ret;
	}

	return size;
}

static
ssize_t get_receiver_id_list_size(struct intel_digital_port *intel_dig_port)
{
	u8 rx_info[HDCP_2_2_RXINFO_LEN];
	u32 dev_cnt;
	ssize_t ret;

	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux,
			       DP_HDCP_2_2_REG_RXINFO_OFFSET,
			       (void *)rx_info, HDCP_2_2_RXINFO_LEN);
	if (ret != HDCP_2_2_RXINFO_LEN)
		return ret >= 0 ? -EIO : ret;

	dev_cnt = (HDCP_2_2_DEV_COUNT_HI(rx_info[0]) << 4 |
		   HDCP_2_2_DEV_COUNT_LO(rx_info[1]));

	if (dev_cnt > HDCP_2_2_MAX_DEVICE_COUNT)
		dev_cnt = HDCP_2_2_MAX_DEVICE_COUNT;

	ret = sizeof(struct hdcp2_rep_send_receiverid_list) -
		HDCP_2_2_RECEIVER_IDS_MAX_LEN +
		(dev_cnt * HDCP_2_2_RECEIVER_ID_LEN);

	return ret;
}

static
int intel_dp_hdcp2_read_msg(struct intel_digital_port *intel_dig_port,
			    u8 msg_id, void *buf, size_t size)
{
	unsigned int offset;
	u8 *byte = buf;
	ssize_t ret, bytes_to_recv, len;
	struct hdcp2_dp_msg_data *hdcp2_msg_data;

	hdcp2_msg_data = get_hdcp2_dp_msg_data(msg_id);
	if (!hdcp2_msg_data)
		return -EINVAL;
	offset = hdcp2_msg_data->offset;

	ret = intel_dp_hdcp2_wait_for_msg(intel_dig_port, hdcp2_msg_data);
	if (ret < 0)
		return ret;

	if (msg_id == HDCP_2_2_REP_SEND_RECVID_LIST) {
		ret = get_receiver_id_list_size(intel_dig_port);
		if (ret < 0)
			return ret;

		size = ret;
	}
	bytes_to_recv = size - 1;

	/* DP adaptation msgs has no msg_id */
	byte++;

	while (bytes_to_recv) {
		len = bytes_to_recv > DP_AUX_MAX_PAYLOAD_BYTES ?
		      DP_AUX_MAX_PAYLOAD_BYTES : bytes_to_recv;

		ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux, offset,
				       (void *)byte, len);
		if (ret < 0) {
			DRM_DEBUG_KMS("msg_id %d, ret %zd\n", msg_id, ret);
			return ret;
		}

		bytes_to_recv -= ret;
		byte += ret;
		offset += ret;
	}
	byte = buf;
	*byte = msg_id;

	return size;
}

static
int intel_dp_hdcp2_config_stream_type(struct intel_digital_port *intel_dig_port,
				      bool is_repeater, u8 content_type)
{
	struct hdcp2_dp_errata_stream_type stream_type_msg;

	if (is_repeater)
		return 0;

	/*
	 * Errata for DP: As Stream type is used for encryption, Receiver
	 * should be communicated with stream type for the decryption of the
	 * content.
	 * Repeater will be communicated with stream type as a part of it's
	 * auth later in time.
	 */
	stream_type_msg.msg_id = HDCP_2_2_ERRATA_DP_STREAM_TYPE;
	stream_type_msg.stream_type = content_type;

	return intel_dp_hdcp2_write_msg(intel_dig_port, &stream_type_msg,
					sizeof(stream_type_msg));
}

static
int intel_dp_hdcp2_check_link(struct intel_digital_port *intel_dig_port)
{
	u8 rx_status;
	int ret;

	ret = intel_dp_hdcp2_read_rx_status(intel_dig_port, &rx_status);
	if (ret)
		return ret;

	if (HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(rx_status))
		ret = HDCP_REAUTH_REQUEST;
	else if (HDCP_2_2_DP_RXSTATUS_LINK_FAILED(rx_status))
		ret = HDCP_LINK_INTEGRITY_FAILURE;
	else if (HDCP_2_2_DP_RXSTATUS_READY(rx_status))
		ret = HDCP_TOPOLOGY_CHANGE;

	return ret;
}

static
int intel_dp_hdcp2_capable(struct intel_digital_port *intel_dig_port,
			   bool *capable)
{
	u8 rx_caps[3];
	int ret;

	*capable = false;
	ret = drm_dp_dpcd_read(&intel_dig_port->dp.aux,
			       DP_HDCP_2_2_REG_RX_CAPS_OFFSET,
			       rx_caps, HDCP_2_2_RXCAPS_LEN);
	if (ret != HDCP_2_2_RXCAPS_LEN)
		return ret >= 0 ? -EIO : ret;

	if (rx_caps[0] == HDCP_2_2_RX_CAPS_VERSION_VAL &&
	    HDCP_2_2_DP_HDCP_CAPABLE(rx_caps[2]))
		*capable = true;

	return 0;
}

static const struct intel_hdcp_shim intel_dp_hdcp_shim = {
	.write_an_aksv = intel_dp_hdcp_write_an_aksv,
	.read_bksv = intel_dp_hdcp_read_bksv,
	.read_bstatus = intel_dp_hdcp_read_bstatus,
	.repeater_present = intel_dp_hdcp_repeater_present,
	.read_ri_prime = intel_dp_hdcp_read_ri_prime,
	.read_ksv_ready = intel_dp_hdcp_read_ksv_ready,
	.read_ksv_fifo = intel_dp_hdcp_read_ksv_fifo,
	.read_v_prime_part = intel_dp_hdcp_read_v_prime_part,
	.toggle_signalling = intel_dp_hdcp_toggle_signalling,
	.check_link = intel_dp_hdcp_check_link,
	.hdcp_capable = intel_dp_hdcp_capable,
	.write_2_2_msg = intel_dp_hdcp2_write_msg,
	.read_2_2_msg = intel_dp_hdcp2_read_msg,
	.config_stream_type = intel_dp_hdcp2_config_stream_type,
	.check_2_2_link = intel_dp_hdcp2_check_link,
	.hdcp_2_2_capable = intel_dp_hdcp2_capable,
	.protocol = HDCP_PROTOCOL_DP,
};

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
	DRM_DEBUG_KMS("VDD left on by BIOS, adjusting state tracking\n");
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
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_lspcon *lspcon = dp_to_lspcon(intel_dp);
	intel_wakeref_t wakeref;

	if (!HAS_DDI(dev_priv))
		intel_dp->DP = I915_READ(intel_dp->output_reg);

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
	.atomic_check = intel_digital_connector_atomic_check,
};

static const struct drm_encoder_funcs intel_dp_enc_funcs = {
	.reset = intel_dp_encoder_reset,
	.destroy = intel_dp_encoder_destroy,
};

enum irqreturn
intel_dp_hpd_pulse(struct intel_digital_port *intel_dig_port, bool long_hpd)
{
	struct intel_dp *intel_dp = &intel_dig_port->dp;

	if (long_hpd && intel_dig_port->base.type == INTEL_OUTPUT_EDP) {
		/*
		 * vdd off can generate a long pulse on eDP which
		 * would require vdd on to handle it, and thus we
		 * would end up in an endless cycle of
		 * "vdd off -> long hpd -> vdd on -> detect -> vdd off -> ..."
		 */
		DRM_DEBUG_KMS("ignoring long hpd on eDP port %c\n",
			      port_name(intel_dig_port->base.port));
		return IRQ_HANDLED;
	}

	DRM_DEBUG_KMS("got hpd irq on port %c - %s\n",
		      port_name(intel_dig_port->base.port),
		      long_hpd ? "long" : "short");

	if (long_hpd) {
		intel_dp->reset_link_params = true;
		return IRQ_NONE;
	}

	if (intel_dp->is_mst) {
		if (intel_dp_check_mst_status(intel_dp) == -EINVAL) {
			/*
			 * If we were in MST mode, and device is not
			 * there, get out of MST mode
			 */
			DRM_DEBUG_KMS("MST device may have disappeared %d vs %d\n",
				      intel_dp->is_mst, intel_dp->mst_mgr.mst_state);
			intel_dp->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
							intel_dp->is_mst);

			return IRQ_NONE;
		}
	}

	if (!intel_dp->is_mst) {
		bool handled;

		handled = intel_dp_short_pulse(intel_dp);

		if (!handled)
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

	if (!IS_G4X(dev_priv) && port != PORT_A)
		intel_attach_force_audio_property(connector);

	intel_attach_broadcast_rgb_property(connector);
	if (HAS_GMCH(dev_priv))
		drm_connector_attach_max_bpc_property(connector, 6, 10);
	else if (INTEL_GEN(dev_priv) >= 5)
		drm_connector_attach_max_bpc_property(connector, 6, 12);

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

	pp_ctl = ironlake_get_pp_control(intel_dp);

	/* Ensure PPS is unlocked */
	if (!HAS_DDI(dev_priv))
		I915_WRITE(regs.pp_ctrl, pp_ctl);

	pp_on = I915_READ(regs.pp_on);
	pp_off = I915_READ(regs.pp_off);

	/* Pull timing values out of registers */
	seq->t1_t3 = REG_FIELD_GET(PANEL_POWER_UP_DELAY_MASK, pp_on);
	seq->t8 = REG_FIELD_GET(PANEL_LIGHT_ON_DELAY_MASK, pp_on);
	seq->t9 = REG_FIELD_GET(PANEL_LIGHT_OFF_DELAY_MASK, pp_off);
	seq->t10 = REG_FIELD_GET(PANEL_POWER_DOWN_DELAY_MASK, pp_off);

	if (i915_mmio_reg_valid(regs.pp_div)) {
		u32 pp_div;

		pp_div = I915_READ(regs.pp_div);

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
		DRM_DEBUG_KMS("Increasing T12 panel delay as per the quirk to %d\n",
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

	DRM_DEBUG_KMS("panel power up delay %d, power down delay %d, power cycle delay %d\n",
		      intel_dp->panel_power_up_delay, intel_dp->panel_power_down_delay,
		      intel_dp->panel_power_cycle_delay);

	DRM_DEBUG_KMS("backlight on delay %d, off delay %d\n",
		      intel_dp->backlight_on_delay, intel_dp->backlight_off_delay);

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
	int div = dev_priv->rawclk_freq / 1000;
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
		u32 pp = ironlake_get_pp_control(intel_dp);

		WARN(pp & PANEL_POWER_ON, "Panel power already on\n");

		if (pp & EDP_FORCE_VDD)
			DRM_DEBUG_KMS("VDD already on, disabling first\n");

		pp &= ~EDP_FORCE_VDD;

		I915_WRITE(regs.pp_ctrl, pp);
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

	I915_WRITE(regs.pp_on, pp_on);
	I915_WRITE(regs.pp_off, pp_off);

	/*
	 * Compute the divisor for the pp clock, simply match the Bspec formula.
	 */
	if (i915_mmio_reg_valid(regs.pp_div)) {
		I915_WRITE(regs.pp_div,
			   REG_FIELD_PREP(PP_REFERENCE_DIVIDER_MASK, (100 * div) / 2 - 1) |
			   REG_FIELD_PREP(PANEL_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(seq->t11_t12, 1000)));
	} else {
		u32 pp_ctl;

		pp_ctl = I915_READ(regs.pp_ctrl);
		pp_ctl &= ~BXT_POWER_CYCLE_DELAY_MASK;
		pp_ctl |= REG_FIELD_PREP(BXT_POWER_CYCLE_DELAY_MASK, DIV_ROUND_UP(seq->t11_t12, 1000));
		I915_WRITE(regs.pp_ctrl, pp_ctl);
	}

	DRM_DEBUG_KMS("panel power sequencer register settings: PP_ON %#x, PP_OFF %#x, PP_DIV %#x\n",
		      I915_READ(regs.pp_on),
		      I915_READ(regs.pp_off),
		      i915_mmio_reg_valid(regs.pp_div) ?
		      I915_READ(regs.pp_div) :
		      (I915_READ(regs.pp_ctrl) & BXT_POWER_CYCLE_DELAY_MASK));
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
	struct intel_encoder *encoder;
	struct intel_digital_port *dig_port = NULL;
	struct intel_dp *intel_dp = dev_priv->drrs.dp;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);
	enum drrs_refresh_rate_type index = DRRS_HIGH_RR;

	if (refresh_rate <= 0) {
		DRM_DEBUG_KMS("Refresh rate should be positive non-zero.\n");
		return;
	}

	if (intel_dp == NULL) {
		DRM_DEBUG_KMS("DRRS not supported.\n");
		return;
	}

	dig_port = dp_to_dig_port(intel_dp);
	encoder = &dig_port->base;

	if (!intel_crtc) {
		DRM_DEBUG_KMS("DRRS: intel_crtc not initialized\n");
		return;
	}

	if (dev_priv->drrs.type < SEAMLESS_DRRS_SUPPORT) {
		DRM_DEBUG_KMS("Only Seamless DRRS supported.\n");
		return;
	}

	if (intel_dp->attached_connector->panel.downclock_mode->vrefresh ==
			refresh_rate)
		index = DRRS_LOW_RR;

	if (index == dev_priv->drrs.refresh_rate_type) {
		DRM_DEBUG_KMS(
			"DRRS requested for previously set RR...ignoring\n");
		return;
	}

	if (!crtc_state->base.active) {
		DRM_DEBUG_KMS("eDP encoder disabled. CRTC not Active\n");
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
			DRM_ERROR("Unsupported refreshrate type\n");
		}
	} else if (INTEL_GEN(dev_priv) > 6) {
		i915_reg_t reg = PIPECONF(crtc_state->cpu_transcoder);
		u32 val;

		val = I915_READ(reg);
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
		I915_WRITE(reg, val);
	}

	dev_priv->drrs.refresh_rate_type = index;

	DRM_DEBUG_KMS("eDP Refresh Rate set to : %dHz\n", refresh_rate);
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

	if (!crtc_state->has_drrs) {
		DRM_DEBUG_KMS("Panel doesn't support DRRS\n");
		return;
	}

	if (dev_priv->psr.enabled) {
		DRM_DEBUG_KMS("PSR enabled. Not enabling DRRS.\n");
		return;
	}

	mutex_lock(&dev_priv->drrs.mutex);
	if (dev_priv->drrs.dp) {
		DRM_DEBUG_KMS("DRRS already enabled\n");
		goto unlock;
	}

	dev_priv->drrs.busy_frontbuffer_bits = 0;

	dev_priv->drrs.dp = intel_dp;

unlock:
	mutex_unlock(&dev_priv->drrs.mutex);
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

	if (dev_priv->drrs.refresh_rate_type == DRRS_LOW_RR)
		intel_dp_set_drrs_state(dev_priv, old_crtc_state,
			intel_dp->attached_connector->panel.fixed_mode->vrefresh);

	dev_priv->drrs.dp = NULL;
	mutex_unlock(&dev_priv->drrs.mutex);

	cancel_delayed_work_sync(&dev_priv->drrs.work);
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
			intel_dp->attached_connector->panel.downclock_mode->vrefresh);
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
	struct drm_crtc *crtc;
	enum pipe pipe;

	if (dev_priv->drrs.type == DRRS_NOT_SUPPORTED)
		return;

	cancel_delayed_work(&dev_priv->drrs.work);

	mutex_lock(&dev_priv->drrs.mutex);
	if (!dev_priv->drrs.dp) {
		mutex_unlock(&dev_priv->drrs.mutex);
		return;
	}

	crtc = dp_to_dig_port(dev_priv->drrs.dp)->base.base.crtc;
	pipe = to_intel_crtc(crtc)->pipe;

	frontbuffer_bits &= INTEL_FRONTBUFFER_ALL_MASK(pipe);
	dev_priv->drrs.busy_frontbuffer_bits |= frontbuffer_bits;

	/* invalidate means busy screen hence upclock */
	if (frontbuffer_bits && dev_priv->drrs.refresh_rate_type == DRRS_LOW_RR)
		intel_dp_set_drrs_state(dev_priv, to_intel_crtc(crtc)->config,
			dev_priv->drrs.dp->attached_connector->panel.fixed_mode->vrefresh);

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
	struct drm_crtc *crtc;
	enum pipe pipe;

	if (dev_priv->drrs.type == DRRS_NOT_SUPPORTED)
		return;

	cancel_delayed_work(&dev_priv->drrs.work);

	mutex_lock(&dev_priv->drrs.mutex);
	if (!dev_priv->drrs.dp) {
		mutex_unlock(&dev_priv->drrs.mutex);
		return;
	}

	crtc = dp_to_dig_port(dev_priv->drrs.dp)->base.base.crtc;
	pipe = to_intel_crtc(crtc)->pipe;

	frontbuffer_bits &= INTEL_FRONTBUFFER_ALL_MASK(pipe);
	dev_priv->drrs.busy_frontbuffer_bits &= ~frontbuffer_bits;

	/* flush means busy screen hence upclock */
	if (frontbuffer_bits && dev_priv->drrs.refresh_rate_type == DRRS_LOW_RR)
		intel_dp_set_drrs_state(dev_priv, to_intel_crtc(crtc)->config,
				dev_priv->drrs.dp->attached_connector->panel.fixed_mode->vrefresh);

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
		DRM_DEBUG_KMS("DRRS supported for Gen7 and above\n");
		return NULL;
	}

	if (dev_priv->vbt.drrs_type != SEAMLESS_DRRS_SUPPORT) {
		DRM_DEBUG_KMS("VBT doesn't support DRRS\n");
		return NULL;
	}

	downclock_mode = intel_panel_edid_downclock_mode(connector, fixed_mode);
	if (!downclock_mode) {
		DRM_DEBUG_KMS("Downclock mode is not found. DRRS not supported\n");
		return NULL;
	}

	dev_priv->drrs.type = dev_priv->vbt.drrs_type;

	dev_priv->drrs.refresh_rate_type = DRRS_HIGH_RR;
	DRM_DEBUG_KMS("seamless DRRS supported for eDP panel.\n");
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
		WARN_ON(!(HAS_PCH_IBX(dev_priv) || HAS_PCH_CPT(dev_priv)));
		DRM_INFO("LVDS was detected, not registering eDP\n");

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
		DRM_INFO("failed to retrieve link info, disabling eDP\n");
		goto out_vdd_off;
	}

	mutex_lock(&dev->mode_config.mutex);
	edid = drm_get_edid(connector, &intel_dp->aux.ddc);
	if (edid) {
		if (drm_add_edid_modes(connector, edid)) {
			drm_connector_update_edid_property(connector,
								edid);
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

		DRM_DEBUG_KMS("using pipe %c for initial backlight setup\n",
			      pipe_name(pipe));
	}

	intel_panel_init(&intel_connector->panel, fixed_mode, downclock_mode);
	intel_connector->panel.backlight.power = intel_edp_backlight_power;
	intel_panel_setup_backlight(connector, pipe);

	if (fixed_mode)
		drm_connector_init_panel_orientation_property(
			connector, fixed_mode->hdisplay, fixed_mode->vdisplay);

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
intel_dp_init_connector(struct intel_digital_port *intel_dig_port,
			struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = intel_encoder->port;
	int type;

	/* Initialize the work for modeset in case of link train failure */
	INIT_WORK(&intel_connector->modeset_retry_work,
		  intel_dp_modeset_retry_work_fn);

	if (WARN(intel_dig_port->max_lanes < 1,
		 "Not enough lanes (%d) for DP on port %c\n",
		 intel_dig_port->max_lanes, port_name(port)))
		return false;

	intel_dp_set_source_rates(intel_dp);

	intel_dp->reset_link_params = true;
	intel_dp->pps_pipe = INVALID_PIPE;
	intel_dp->active_pipe = INVALID_PIPE;

	/* Preserve the current hw state. */
	intel_dp->DP = I915_READ(intel_dp->output_reg);
	intel_dp->attached_connector = intel_connector;

	if (intel_dp_is_port_edp(dev_priv, port)) {
		/*
		 * Currently we don't support eDP on TypeC ports, although in
		 * theory it could work on TypeC legacy ports.
		 */
		WARN_ON(intel_port_is_tc(dev_priv, port));
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
	if (WARN_ON((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
		    intel_dp_is_edp(intel_dp) &&
		    port != PORT_B && port != PORT_C))
		return false;

	DRM_DEBUG_KMS("Adding %s connector on port %c\n",
			type == DRM_MODE_CONNECTOR_eDP ? "eDP" : "DP",
			port_name(port));

	drm_connector_init(dev, connector, &intel_dp_connector_funcs, type);
	drm_connector_helper_add(connector, &intel_dp_connector_helper_funcs);

	if (!HAS_GMCH(dev_priv))
		connector->interlace_allowed = true;
	connector->doublescan_allowed = 0;

	if (INTEL_GEN(dev_priv) >= 11)
		connector->ycbcr_420_allowed = true;

	intel_encoder->hpd_pin = intel_hpd_pin_default(dev_priv, port);

	intel_dp_aux_init(intel_dp);

	intel_connector_attach_encoder(intel_connector, intel_encoder);

	if (HAS_DDI(dev_priv))
		intel_connector->get_hw_state = intel_ddi_connector_get_hw_state;
	else
		intel_connector->get_hw_state = intel_connector_get_hw_state;

	/* init MST on ports that can support it */
	if (HAS_DP_MST(dev_priv) && !intel_dp_is_edp(intel_dp) &&
	    (port == PORT_B || port == PORT_C ||
	     port == PORT_D || port == PORT_F))
		intel_dp_mst_encoder_init(intel_dig_port,
					  intel_connector->base.base.id);

	if (!intel_edp_init_connector(intel_dp, intel_connector)) {
		intel_dp_aux_fini(intel_dp);
		intel_dp_mst_encoder_cleanup(intel_dig_port);
		goto fail;
	}

	intel_dp_add_properties(intel_dp, connector);

	if (is_hdcp_supported(dev_priv, port) && !intel_dp_is_edp(intel_dp)) {
		int ret = intel_hdcp_init(intel_connector, &intel_dp_hdcp_shim);
		if (ret)
			DRM_DEBUG_KMS("HDCP init failed, skipping.\n");
	}

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G45(dev_priv)) {
		u32 temp = I915_READ(PEG_BAND_GAP_DATA);
		I915_WRITE(PEG_BAND_GAP_DATA, (temp & ~0xf) | 0xd);
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
	struct intel_digital_port *intel_dig_port;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	struct intel_connector *intel_connector;

	intel_dig_port = kzalloc(sizeof(*intel_dig_port), GFP_KERNEL);
	if (!intel_dig_port)
		return false;

	intel_connector = intel_connector_alloc();
	if (!intel_connector)
		goto err_connector_alloc;

	intel_encoder = &intel_dig_port->base;
	encoder = &intel_encoder->base;

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

	intel_dig_port->dp.output_reg = output_reg;
	intel_dig_port->max_lanes = 4;

	intel_encoder->type = INTEL_OUTPUT_DP;
	intel_encoder->power_domain = intel_port_to_power_domain(port);
	if (IS_CHERRYVIEW(dev_priv)) {
		if (port == PORT_D)
			intel_encoder->crtc_mask = 1 << 2;
		else
			intel_encoder->crtc_mask = (1 << 0) | (1 << 1);
	} else {
		intel_encoder->crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);
	}
	intel_encoder->cloneable = 0;
	intel_encoder->port = port;

	intel_dig_port->hpd_pulse = intel_dp_hpd_pulse;

	if (port != PORT_A)
		intel_infoframe_init(intel_dig_port);

	intel_dig_port->aux_ch = intel_bios_port_aux_ch(dev_priv, port);
	if (!intel_dp_init_connector(intel_dig_port, intel_connector))
		goto err_init_connector;

	return true;

err_init_connector:
	drm_encoder_cleanup(encoder);
err_encoder_init:
	kfree(intel_connector);
err_connector_alloc:
	kfree(intel_dig_port);
	return false;
}

void intel_dp_mst_suspend(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		struct intel_dp *intel_dp;

		if (encoder->type != INTEL_OUTPUT_DDI)
			continue;

		intel_dp = enc_to_intel_dp(&encoder->base);

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

		intel_dp = enc_to_intel_dp(&encoder->base);

		if (!intel_dp->can_mst)
			continue;

		ret = drm_dp_mst_topology_mgr_resume(&intel_dp->mst_mgr);
		if (ret) {
			intel_dp->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr,
							false);
		}
	}
}
