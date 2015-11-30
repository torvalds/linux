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

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

#define DP_LINK_CHECK_TIMEOUT	(10 * 1000)

/* Compliance test status bits  */
#define INTEL_DP_RESOLUTION_SHIFT_MASK	0
#define INTEL_DP_RESOLUTION_PREFERRED	(1 << INTEL_DP_RESOLUTION_SHIFT_MASK)
#define INTEL_DP_RESOLUTION_STANDARD	(2 << INTEL_DP_RESOLUTION_SHIFT_MASK)
#define INTEL_DP_RESOLUTION_FAILSAFE	(3 << INTEL_DP_RESOLUTION_SHIFT_MASK)

struct dp_link_dpll {
	int clock;
	struct dpll dpll;
};

static const struct dp_link_dpll gen4_dpll[] = {
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
	{ 540000,	/* m2_int = 27, m2_fraction = 0 */
		{ .p1 = 2, .p2 = 1, .n = 1, .m1 = 2, .m2 = 0x6c00000 } }
};

static const int bxt_rates[] = { 162000, 216000, 243000, 270000,
				  324000, 432000, 540000 };
static const int skl_rates[] = { 162000, 216000, 270000,
				  324000, 432000, 540000 };
static const int default_rates[] = { 162000, 270000, 540000 };

/**
 * is_edp - is the given port attached to an eDP panel (either CPU or PCH)
 * @intel_dp: DP struct
 *
 * If a CPU or PCH DP output is attached to an eDP panel, this function
 * will return true, and false otherwise.
 */
static bool is_edp(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);

	return intel_dig_port->base.type == INTEL_OUTPUT_EDP;
}

static struct drm_device *intel_dp_to_dev(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);

	return intel_dig_port->base.base.dev;
}

static struct intel_dp *intel_attached_dp(struct drm_connector *connector)
{
	return enc_to_intel_dp(&intel_attached_encoder(connector)->base);
}

static void intel_dp_link_down(struct intel_dp *intel_dp);
static bool edp_panel_vdd_on(struct intel_dp *intel_dp);
static void edp_panel_vdd_off(struct intel_dp *intel_dp, bool sync);
static void vlv_init_panel_power_sequencer(struct intel_dp *intel_dp);
static void vlv_steal_power_sequencer(struct drm_device *dev,
				      enum pipe pipe);

static unsigned int intel_dp_unused_lane_mask(int lane_count)
{
	return ~((1 << lane_count) - 1) & 0xf;
}

static int
intel_dp_max_link_bw(struct intel_dp  *intel_dp)
{
	int max_link_bw = intel_dp->dpcd[DP_MAX_LINK_RATE];

	switch (max_link_bw) {
	case DP_LINK_BW_1_62:
	case DP_LINK_BW_2_7:
	case DP_LINK_BW_5_4:
		break;
	default:
		WARN(1, "invalid max DP link bw val %x, using 1.62Gbps\n",
		     max_link_bw);
		max_link_bw = DP_LINK_BW_1_62;
		break;
	}
	return max_link_bw;
}

static u8 intel_dp_max_lane_count(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	u8 source_max, sink_max;

	source_max = 4;
	if (HAS_DDI(dev) && intel_dig_port->port == PORT_A &&
	    (intel_dig_port->saved_port_bits & DDI_A_4_LANES) == 0)
		source_max = 2;

	sink_max = drm_dp_max_lane_count(intel_dp->dpcd);

	return min(source_max, sink_max);
}

/*
 * The units on the numbers in the next two are... bizarre.  Examples will
 * make it clearer; this one parallels an example in the eDP spec.
 *
 * intel_dp_max_data_rate for one lane of 2.7GHz evaluates as:
 *
 *     270000 * 1 * 8 / 10 == 216000
 *
 * The actual data capacity of that configuration is 2.16Gbit/s, so the
 * units are decakilobits.  ->clock in a drm_display_mode is in kilohertz -
 * or equivalently, kilopixels per second - so for 1680x1050R it'd be
 * 119000.  At 18bpp that's 2142000 kilobits per second.
 *
 * Thus the strange-looking division by 10 in intel_dp_link_required, to
 * get the result in decakilobits instead of kilobits.
 */

static int
intel_dp_link_required(int pixel_clock, int bpp)
{
	return (pixel_clock * bpp + 9) / 10;
}

static int
intel_dp_max_data_rate(int max_link_clock, int max_lanes)
{
	return (max_link_clock * max_lanes * 8) / 10;
}

static enum drm_mode_status
intel_dp_mode_valid(struct drm_connector *connector,
		    struct drm_display_mode *mode)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	int target_clock = mode->clock;
	int max_rate, mode_rate, max_lanes, max_link_clock;

	if (is_edp(intel_dp) && fixed_mode) {
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

	if (mode_rate > max_rate)
		return MODE_CLOCK_HIGH;

	if (mode->clock < 10000)
		return MODE_CLOCK_LOW;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_H_ILLEGAL;

	return MODE_OK;
}

uint32_t intel_dp_pack_aux(const uint8_t *src, int src_bytes)
{
	int	i;
	uint32_t v = 0;

	if (src_bytes > 4)
		src_bytes = 4;
	for (i = 0; i < src_bytes; i++)
		v |= ((uint32_t) src[i]) << ((3-i) * 8);
	return v;
}

static void intel_dp_unpack_aux(uint32_t src, uint8_t *dst, int dst_bytes)
{
	int i;
	if (dst_bytes > 4)
		dst_bytes = 4;
	for (i = 0; i < dst_bytes; i++)
		dst[i] = src >> ((3-i) * 8);
}

static void
intel_dp_init_panel_power_sequencer(struct drm_device *dev,
				    struct intel_dp *intel_dp);
static void
intel_dp_init_panel_power_sequencer_registers(struct drm_device *dev,
					      struct intel_dp *intel_dp);

static void pps_lock(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &intel_dig_port->base;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain power_domain;

	/*
	 * See vlv_power_sequencer_reset() why we need
	 * a power domain reference here.
	 */
	power_domain = intel_display_port_aux_power_domain(encoder);
	intel_display_power_get(dev_priv, power_domain);

	mutex_lock(&dev_priv->pps_mutex);
}

static void pps_unlock(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &intel_dig_port->base;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain power_domain;

	mutex_unlock(&dev_priv->pps_mutex);

	power_domain = intel_display_port_aux_power_domain(encoder);
	intel_display_power_put(dev_priv, power_domain);
}

static void
vlv_power_sequencer_kick(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe = intel_dp->pps_pipe;
	bool pll_enabled, release_cl_override = false;
	enum dpio_phy phy = DPIO_PHY(pipe);
	enum dpio_channel ch = vlv_pipe_to_channel(pipe);
	uint32_t DP;

	if (WARN(I915_READ(intel_dp->output_reg) & DP_PORT_EN,
		 "skipping pipe %c power seqeuncer kick due to port %c being active\n",
		 pipe_name(pipe), port_name(intel_dig_port->port)))
		return;

	DRM_DEBUG_KMS("kicking pipe %c power sequencer for port %c\n",
		      pipe_name(pipe), port_name(intel_dig_port->port));

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	DP = I915_READ(intel_dp->output_reg) & DP_DETECTED;
	DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	DP |= DP_PORT_WIDTH(1);
	DP |= DP_LINK_TRAIN_PAT_1;

	if (IS_CHERRYVIEW(dev))
		DP |= DP_PIPE_SELECT_CHV(pipe);
	else if (pipe == PIPE_B)
		DP |= DP_PIPEB_SELECT;

	pll_enabled = I915_READ(DPLL(pipe)) & DPLL_VCO_ENABLE;

	/*
	 * The DPLL for the pipe must be enabled for this to work.
	 * So enable temporarily it if it's not already enabled.
	 */
	if (!pll_enabled) {
		release_cl_override = IS_CHERRYVIEW(dev) &&
			!chv_phy_powergate_ch(dev_priv, phy, ch, true);

		vlv_force_pll_on(dev, pipe, IS_CHERRYVIEW(dev) ?
				 &chv_dpll[0].dpll : &vlv_dpll[0].dpll);
	}

	/*
	 * Similar magic as in intel_dp_enable_port().
	 * We _must_ do this port enable + disable trick
	 * to make this power seqeuencer lock onto the port.
	 * Otherwise even VDD force bit won't work.
	 */
	I915_WRITE(intel_dp->output_reg, DP);
	POSTING_READ(intel_dp->output_reg);

	I915_WRITE(intel_dp->output_reg, DP | DP_PORT_EN);
	POSTING_READ(intel_dp->output_reg);

	I915_WRITE(intel_dp->output_reg, DP & ~DP_PORT_EN);
	POSTING_READ(intel_dp->output_reg);

	if (!pll_enabled) {
		vlv_force_pll_off(dev, pipe);

		if (release_cl_override)
			chv_phy_powergate_ch(dev_priv, phy, ch, false);
	}
}

static enum pipe
vlv_power_sequencer_pipe(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	unsigned int pipes = (1 << PIPE_A) | (1 << PIPE_B);
	enum pipe pipe;

	lockdep_assert_held(&dev_priv->pps_mutex);

	/* We should never land here with regular DP ports */
	WARN_ON(!is_edp(intel_dp));

	if (intel_dp->pps_pipe != INVALID_PIPE)
		return intel_dp->pps_pipe;

	/*
	 * We don't have power sequencer currently.
	 * Pick one that's not used by other ports.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list,
			    base.head) {
		struct intel_dp *tmp;

		if (encoder->type != INTEL_OUTPUT_EDP)
			continue;

		tmp = enc_to_intel_dp(&encoder->base);

		if (tmp->pps_pipe != INVALID_PIPE)
			pipes &= ~(1 << tmp->pps_pipe);
	}

	/*
	 * Didn't find one. This should not happen since there
	 * are two power sequencers and up to two eDP ports.
	 */
	if (WARN_ON(pipes == 0))
		pipe = PIPE_A;
	else
		pipe = ffs(pipes) - 1;

	vlv_steal_power_sequencer(dev, pipe);
	intel_dp->pps_pipe = pipe;

	DRM_DEBUG_KMS("picked pipe %c power sequencer for port %c\n",
		      pipe_name(intel_dp->pps_pipe),
		      port_name(intel_dig_port->port));

	/* init power sequencer on this pipe and port */
	intel_dp_init_panel_power_sequencer(dev, intel_dp);
	intel_dp_init_panel_power_sequencer_registers(dev, intel_dp);

	/*
	 * Even vdd force doesn't work until we've made
	 * the power sequencer lock in on the port.
	 */
	vlv_power_sequencer_kick(intel_dp);

	return intel_dp->pps_pipe;
}

typedef bool (*vlv_pipe_check)(struct drm_i915_private *dev_priv,
			       enum pipe pipe);

static bool vlv_pipe_has_pp_on(struct drm_i915_private *dev_priv,
			       enum pipe pipe)
{
	return I915_READ(VLV_PIPE_PP_STATUS(pipe)) & PP_ON;
}

static bool vlv_pipe_has_vdd_on(struct drm_i915_private *dev_priv,
				enum pipe pipe)
{
	return I915_READ(VLV_PIPE_PP_CONTROL(pipe)) & EDP_FORCE_VDD;
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
		u32 port_sel = I915_READ(VLV_PIPE_PP_ON_DELAYS(pipe)) &
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;

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

	intel_dp_init_panel_power_sequencer(dev, intel_dp);
	intel_dp_init_panel_power_sequencer_registers(dev, intel_dp);
}

void vlv_power_sequencer_reset(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct intel_encoder *encoder;

	if (WARN_ON(!IS_VALLEYVIEW(dev)))
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

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, base.head) {
		struct intel_dp *intel_dp;

		if (encoder->type != INTEL_OUTPUT_EDP)
			continue;

		intel_dp = enc_to_intel_dp(&encoder->base);
		intel_dp->pps_pipe = INVALID_PIPE;
	}
}

static i915_reg_t
_pp_ctrl_reg(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	if (IS_BROXTON(dev))
		return BXT_PP_CONTROL(0);
	else if (HAS_PCH_SPLIT(dev))
		return PCH_PP_CONTROL;
	else
		return VLV_PIPE_PP_CONTROL(vlv_power_sequencer_pipe(intel_dp));
}

static i915_reg_t
_pp_stat_reg(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);

	if (IS_BROXTON(dev))
		return BXT_PP_STATUS(0);
	else if (HAS_PCH_SPLIT(dev))
		return PCH_PP_STATUS;
	else
		return VLV_PIPE_PP_STATUS(vlv_power_sequencer_pipe(intel_dp));
}

/* Reboot notifier handler to shutdown panel power to guarantee T12 timing
   This function only applicable when panel PM state is not to be tracked */
static int edp_notify_handler(struct notifier_block *this, unsigned long code,
			      void *unused)
{
	struct intel_dp *intel_dp = container_of(this, typeof(* intel_dp),
						 edp_notifier);
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!is_edp(intel_dp) || code != SYS_RESTART)
		return 0;

	pps_lock(intel_dp);

	if (IS_VALLEYVIEW(dev)) {
		enum pipe pipe = vlv_power_sequencer_pipe(intel_dp);
		i915_reg_t pp_ctrl_reg, pp_div_reg;
		u32 pp_div;

		pp_ctrl_reg = VLV_PIPE_PP_CONTROL(pipe);
		pp_div_reg  = VLV_PIPE_PP_DIVISOR(pipe);
		pp_div = I915_READ(pp_div_reg);
		pp_div &= PP_REFERENCE_DIVIDER_MASK;

		/* 0x1F write to PP_DIV_REG sets max cycle delay */
		I915_WRITE(pp_div_reg, pp_div | 0x1F);
		I915_WRITE(pp_ctrl_reg, PANEL_UNLOCK_REGS | PANEL_POWER_OFF);
		msleep(intel_dp->panel_power_cycle_delay);
	}

	pps_unlock(intel_dp);

	return 0;
}

static bool edp_have_panel_power(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (IS_VALLEYVIEW(dev) &&
	    intel_dp->pps_pipe == INVALID_PIPE)
		return false;

	return (I915_READ(_pp_stat_reg(intel_dp)) & PP_ON) != 0;
}

static bool edp_have_panel_vdd(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (IS_VALLEYVIEW(dev) &&
	    intel_dp->pps_pipe == INVALID_PIPE)
		return false;

	return I915_READ(_pp_ctrl_reg(intel_dp)) & EDP_FORCE_VDD;
}

static void
intel_dp_check_edp(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!is_edp(intel_dp))
		return;

	if (!edp_have_panel_power(intel_dp) && !edp_have_panel_vdd(intel_dp)) {
		WARN(1, "eDP powered off while attempting aux channel communication.\n");
		DRM_DEBUG_KMS("Status 0x%08x Control 0x%08x\n",
			      I915_READ(_pp_stat_reg(intel_dp)),
			      I915_READ(_pp_ctrl_reg(intel_dp)));
	}
}

static uint32_t
intel_dp_aux_wait_done(struct intel_dp *intel_dp, bool has_aux_irq)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t ch_ctl = intel_dp->aux_ch_ctl_reg;
	uint32_t status;
	bool done;

#define C (((status = I915_READ_NOTRACE(ch_ctl)) & DP_AUX_CH_CTL_SEND_BUSY) == 0)
	if (has_aux_irq)
		done = wait_event_timeout(dev_priv->gmbus_wait_queue, C,
					  msecs_to_jiffies_timeout(10));
	else
		done = wait_for_atomic(C, 10) == 0;
	if (!done)
		DRM_ERROR("dp aux hw did not signal timeout (has irq: %i)!\n",
			  has_aux_irq);
#undef C

	return status;
}

static uint32_t i9xx_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;

	/*
	 * The clock divider is based off the hrawclk, and would like to run at
	 * 2MHz.  So, take the hrawclk value and divide by 2 and use that
	 */
	return index ? 0 : DIV_ROUND_CLOSEST(intel_hrawclk(dev), 2);
}

static uint32_t ilk_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (index)
		return 0;

	if (intel_dig_port->port == PORT_A) {
		return DIV_ROUND_CLOSEST(dev_priv->cdclk_freq, 2000);

	} else {
		return DIV_ROUND_CLOSEST(intel_pch_rawclk(dev), 2);
	}
}

static uint32_t hsw_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (intel_dig_port->port == PORT_A) {
		if (index)
			return 0;
		return DIV_ROUND_CLOSEST(dev_priv->cdclk_freq, 2000);
	} else if (HAS_PCH_LPT_H(dev_priv)) {
		/* Workaround for non-ULT HSW */
		switch (index) {
		case 0: return 63;
		case 1: return 72;
		default: return 0;
		}
	} else  {
		return index ? 0 : DIV_ROUND_CLOSEST(intel_pch_rawclk(dev), 2);
	}
}

static uint32_t vlv_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	return index ? 0 : 100;
}

static uint32_t skl_get_aux_clock_divider(struct intel_dp *intel_dp, int index)
{
	/*
	 * SKL doesn't need us to program the AUX clock divider (Hardware will
	 * derive the clock from CDCLK automatically). We still implement the
	 * get_aux_clock_divider vfunc to plug-in into the existing code.
	 */
	return index ? 0 : 1;
}

static uint32_t i9xx_get_aux_send_ctl(struct intel_dp *intel_dp,
				      bool has_aux_irq,
				      int send_bytes,
				      uint32_t aux_clock_divider)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	uint32_t precharge, timeout;

	if (IS_GEN6(dev))
		precharge = 3;
	else
		precharge = 5;

	if (IS_BROADWELL(dev) && intel_dig_port->port == PORT_A)
		timeout = DP_AUX_CH_CTL_TIME_OUT_600us;
	else
		timeout = DP_AUX_CH_CTL_TIME_OUT_400us;

	return DP_AUX_CH_CTL_SEND_BUSY |
	       DP_AUX_CH_CTL_DONE |
	       (has_aux_irq ? DP_AUX_CH_CTL_INTERRUPT : 0) |
	       DP_AUX_CH_CTL_TIME_OUT_ERROR |
	       timeout |
	       DP_AUX_CH_CTL_RECEIVE_ERROR |
	       (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
	       (precharge << DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT) |
	       (aux_clock_divider << DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT);
}

static uint32_t skl_get_aux_send_ctl(struct intel_dp *intel_dp,
				      bool has_aux_irq,
				      int send_bytes,
				      uint32_t unused)
{
	return DP_AUX_CH_CTL_SEND_BUSY |
	       DP_AUX_CH_CTL_DONE |
	       (has_aux_irq ? DP_AUX_CH_CTL_INTERRUPT : 0) |
	       DP_AUX_CH_CTL_TIME_OUT_ERROR |
	       DP_AUX_CH_CTL_TIME_OUT_1600us |
	       DP_AUX_CH_CTL_RECEIVE_ERROR |
	       (send_bytes << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
	       DP_AUX_CH_CTL_SYNC_PULSE_SKL(32);
}

static int
intel_dp_aux_ch(struct intel_dp *intel_dp,
		const uint8_t *send, int send_bytes,
		uint8_t *recv, int recv_size)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t ch_ctl = intel_dp->aux_ch_ctl_reg;
	uint32_t aux_clock_divider;
	int i, ret, recv_bytes;
	uint32_t status;
	int try, clock = 0;
	bool has_aux_irq = HAS_AUX_IRQ(dev);
	bool vdd;

	pps_lock(intel_dp);

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
	pm_qos_update_request(&dev_priv->pm_qos, 0);

	intel_dp_check_edp(intel_dp);

	/* Try to wait for any previous AUX channel activity */
	for (try = 0; try < 3; try++) {
		status = I915_READ_NOTRACE(ch_ctl);
		if ((status & DP_AUX_CH_CTL_SEND_BUSY) == 0)
			break;
		msleep(1);
	}

	if (try == 3) {
		static u32 last_status = -1;
		const u32 status = I915_READ(ch_ctl);

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
							  has_aux_irq,
							  send_bytes,
							  aux_clock_divider);

		/* Must try at least 3 times according to DP spec */
		for (try = 0; try < 5; try++) {
			/* Load the send data into the aux channel data registers */
			for (i = 0; i < send_bytes; i += 4)
				I915_WRITE(intel_dp->aux_ch_data_reg[i >> 2],
					   intel_dp_pack_aux(send + i,
							     send_bytes - i));

			/* Send the command and wait for it to complete */
			I915_WRITE(ch_ctl, send_ctl);

			status = intel_dp_aux_wait_done(intel_dp, has_aux_irq);

			/* Clear done status and any errors */
			I915_WRITE(ch_ctl,
				   status |
				   DP_AUX_CH_CTL_DONE |
				   DP_AUX_CH_CTL_TIME_OUT_ERROR |
				   DP_AUX_CH_CTL_RECEIVE_ERROR);

			if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR)
				continue;

			/* DP CTS 1.2 Core Rev 1.1, 4.2.1.1 & 4.2.1.2
			 *   400us delay required for errors and timeouts
			 *   Timeout errors from the HW already meet this
			 *   requirement so skip to next iteration
			 */
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
	if (recv_bytes > recv_size)
		recv_bytes = recv_size;

	for (i = 0; i < recv_bytes; i += 4)
		intel_dp_unpack_aux(I915_READ(intel_dp->aux_ch_data_reg[i >> 2]),
				    recv + i, recv_bytes - i);

	ret = recv_bytes;
out:
	pm_qos_update_request(&dev_priv->pm_qos, PM_QOS_DEFAULT_VALUE);

	if (vdd)
		edp_panel_vdd_off(intel_dp, false);

	pps_unlock(intel_dp);

	return ret;
}

#define BARE_ADDRESS_SIZE	3
#define HEADER_SIZE		(BARE_ADDRESS_SIZE + 1)
static ssize_t
intel_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct intel_dp *intel_dp = container_of(aux, struct intel_dp, aux);
	uint8_t txbuf[20], rxbuf[20];
	size_t txsize, rxsize;
	int ret;

	txbuf[0] = (msg->request << 4) |
		((msg->address >> 16) & 0xf);
	txbuf[1] = (msg->address >> 8) & 0xff;
	txbuf[2] = msg->address & 0xff;
	txbuf[3] = msg->size - 1;

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		txsize = msg->size ? HEADER_SIZE + msg->size : BARE_ADDRESS_SIZE;
		rxsize = 2; /* 0 or 1 data bytes */

		if (WARN_ON(txsize > 20))
			return -E2BIG;

		memcpy(txbuf + HEADER_SIZE, msg->buffer, msg->size);

		ret = intel_dp_aux_ch(intel_dp, txbuf, txsize, rxbuf, rxsize);
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

		ret = intel_dp_aux_ch(intel_dp, txbuf, txsize, rxbuf, rxsize);
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

static i915_reg_t g4x_aux_ctl_reg(struct drm_i915_private *dev_priv,
				       enum port port)
{
	switch (port) {
	case PORT_B:
	case PORT_C:
	case PORT_D:
		return DP_AUX_CH_CTL(port);
	default:
		MISSING_CASE(port);
		return DP_AUX_CH_CTL(PORT_B);
	}
}

static i915_reg_t g4x_aux_data_reg(struct drm_i915_private *dev_priv,
					enum port port, int index)
{
	switch (port) {
	case PORT_B:
	case PORT_C:
	case PORT_D:
		return DP_AUX_CH_DATA(port, index);
	default:
		MISSING_CASE(port);
		return DP_AUX_CH_DATA(PORT_B, index);
	}
}

static i915_reg_t ilk_aux_ctl_reg(struct drm_i915_private *dev_priv,
				       enum port port)
{
	switch (port) {
	case PORT_A:
		return DP_AUX_CH_CTL(port);
	case PORT_B:
	case PORT_C:
	case PORT_D:
		return PCH_DP_AUX_CH_CTL(port);
	default:
		MISSING_CASE(port);
		return DP_AUX_CH_CTL(PORT_A);
	}
}

static i915_reg_t ilk_aux_data_reg(struct drm_i915_private *dev_priv,
					enum port port, int index)
{
	switch (port) {
	case PORT_A:
		return DP_AUX_CH_DATA(port, index);
	case PORT_B:
	case PORT_C:
	case PORT_D:
		return PCH_DP_AUX_CH_DATA(port, index);
	default:
		MISSING_CASE(port);
		return DP_AUX_CH_DATA(PORT_A, index);
	}
}

/*
 * On SKL we don't have Aux for port E so we rely
 * on VBT to set a proper alternate aux channel.
 */
static enum port skl_porte_aux_port(struct drm_i915_private *dev_priv)
{
	const struct ddi_vbt_port_info *info =
		&dev_priv->vbt.ddi_port_info[PORT_E];

	switch (info->alternate_aux_channel) {
	case DP_AUX_A:
		return PORT_A;
	case DP_AUX_B:
		return PORT_B;
	case DP_AUX_C:
		return PORT_C;
	case DP_AUX_D:
		return PORT_D;
	default:
		MISSING_CASE(info->alternate_aux_channel);
		return PORT_A;
	}
}

static i915_reg_t skl_aux_ctl_reg(struct drm_i915_private *dev_priv,
				       enum port port)
{
	if (port == PORT_E)
		port = skl_porte_aux_port(dev_priv);

	switch (port) {
	case PORT_A:
	case PORT_B:
	case PORT_C:
	case PORT_D:
		return DP_AUX_CH_CTL(port);
	default:
		MISSING_CASE(port);
		return DP_AUX_CH_CTL(PORT_A);
	}
}

static i915_reg_t skl_aux_data_reg(struct drm_i915_private *dev_priv,
					enum port port, int index)
{
	if (port == PORT_E)
		port = skl_porte_aux_port(dev_priv);

	switch (port) {
	case PORT_A:
	case PORT_B:
	case PORT_C:
	case PORT_D:
		return DP_AUX_CH_DATA(port, index);
	default:
		MISSING_CASE(port);
		return DP_AUX_CH_DATA(PORT_A, index);
	}
}

static i915_reg_t intel_aux_ctl_reg(struct drm_i915_private *dev_priv,
					 enum port port)
{
	if (INTEL_INFO(dev_priv)->gen >= 9)
		return skl_aux_ctl_reg(dev_priv, port);
	else if (HAS_PCH_SPLIT(dev_priv))
		return ilk_aux_ctl_reg(dev_priv, port);
	else
		return g4x_aux_ctl_reg(dev_priv, port);
}

static i915_reg_t intel_aux_data_reg(struct drm_i915_private *dev_priv,
					  enum port port, int index)
{
	if (INTEL_INFO(dev_priv)->gen >= 9)
		return skl_aux_data_reg(dev_priv, port, index);
	else if (HAS_PCH_SPLIT(dev_priv))
		return ilk_aux_data_reg(dev_priv, port, index);
	else
		return g4x_aux_data_reg(dev_priv, port, index);
}

static void intel_aux_reg_init(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dp_to_dev(intel_dp));
	enum port port = dp_to_dig_port(intel_dp)->port;
	int i;

	intel_dp->aux_ch_ctl_reg = intel_aux_ctl_reg(dev_priv, port);
	for (i = 0; i < ARRAY_SIZE(intel_dp->aux_ch_data_reg); i++)
		intel_dp->aux_ch_data_reg[i] = intel_aux_data_reg(dev_priv, port, i);
}

static void
intel_dp_aux_fini(struct intel_dp *intel_dp)
{
	drm_dp_aux_unregister(&intel_dp->aux);
	kfree(intel_dp->aux.name);
}

static int
intel_dp_aux_init(struct intel_dp *intel_dp, struct intel_connector *connector)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->port;
	int ret;

	intel_aux_reg_init(intel_dp);

	intel_dp->aux.name = kasprintf(GFP_KERNEL, "DPDDC-%c", port_name(port));
	if (!intel_dp->aux.name)
		return -ENOMEM;

	intel_dp->aux.dev = dev->dev;
	intel_dp->aux.transfer = intel_dp_aux_transfer;

	DRM_DEBUG_KMS("registering %s bus for %s\n",
		      intel_dp->aux.name,
		      connector->base.kdev->kobj.name);

	ret = drm_dp_aux_register(&intel_dp->aux);
	if (ret < 0) {
		DRM_ERROR("drm_dp_aux_register() for %s failed (%d)\n",
			  intel_dp->aux.name, ret);
		kfree(intel_dp->aux.name);
		return ret;
	}

	ret = sysfs_create_link(&connector->base.kdev->kobj,
				&intel_dp->aux.ddc.dev.kobj,
				intel_dp->aux.ddc.dev.kobj.name);
	if (ret < 0) {
		DRM_ERROR("sysfs_create_link() for %s failed (%d)\n",
			  intel_dp->aux.name, ret);
		intel_dp_aux_fini(intel_dp);
		return ret;
	}

	return 0;
}

static void
intel_dp_connector_unregister(struct intel_connector *intel_connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(&intel_connector->base);

	if (!intel_connector->mst_port)
		sysfs_remove_link(&intel_connector->base.kdev->kobj,
				  intel_dp->aux.ddc.dev.kobj.name);
	intel_connector_unregister(intel_connector);
}

static void
skl_edp_set_pll_config(struct intel_crtc_state *pipe_config)
{
	u32 ctrl1;

	memset(&pipe_config->dpll_hw_state, 0,
	       sizeof(pipe_config->dpll_hw_state));

	pipe_config->ddi_pll_sel = SKL_DPLL0;
	pipe_config->dpll_hw_state.cfgcr1 = 0;
	pipe_config->dpll_hw_state.cfgcr2 = 0;

	ctrl1 = DPLL_CTRL1_OVERRIDE(SKL_DPLL0);
	switch (pipe_config->port_clock / 2) {
	case 81000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_810,
					      SKL_DPLL0);
		break;
	case 135000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1350,
					      SKL_DPLL0);
		break;
	case 270000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_2700,
					      SKL_DPLL0);
		break;
	case 162000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1620,
					      SKL_DPLL0);
		break;
	/* TBD: For DP link rates 2.16 GHz and 4.32 GHz, VCO is 8640 which
	results in CDCLK change. Need to handle the change of CDCLK by
	disabling pipes and re-enabling them */
	case 108000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1080,
					      SKL_DPLL0);
		break;
	case 216000:
		ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_2160,
					      SKL_DPLL0);
		break;

	}
	pipe_config->dpll_hw_state.ctrl1 = ctrl1;
}

void
hsw_dp_set_ddi_pll_sel(struct intel_crtc_state *pipe_config)
{
	memset(&pipe_config->dpll_hw_state, 0,
	       sizeof(pipe_config->dpll_hw_state));

	switch (pipe_config->port_clock / 2) {
	case 81000:
		pipe_config->ddi_pll_sel = PORT_CLK_SEL_LCPLL_810;
		break;
	case 135000:
		pipe_config->ddi_pll_sel = PORT_CLK_SEL_LCPLL_1350;
		break;
	case 270000:
		pipe_config->ddi_pll_sel = PORT_CLK_SEL_LCPLL_2700;
		break;
	}
}

static int
intel_dp_sink_rates(struct intel_dp *intel_dp, const int **sink_rates)
{
	if (intel_dp->num_sink_rates) {
		*sink_rates = intel_dp->sink_rates;
		return intel_dp->num_sink_rates;
	}

	*sink_rates = default_rates;

	return (intel_dp_max_link_bw(intel_dp) >> 3) + 1;
}

bool intel_dp_source_supports_hbr2(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;

	/* WaDisableHBR2:skl */
	if (IS_SKL_REVID(dev, 0, SKL_REVID_B0))
		return false;

	if ((IS_HASWELL(dev) && !IS_HSW_ULX(dev)) || IS_BROADWELL(dev) ||
	    (INTEL_INFO(dev)->gen >= 9))
		return true;
	else
		return false;
}

static int
intel_dp_source_rates(struct intel_dp *intel_dp, const int **source_rates)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	int size;

	if (IS_BROXTON(dev)) {
		*source_rates = bxt_rates;
		size = ARRAY_SIZE(bxt_rates);
	} else if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev)) {
		*source_rates = skl_rates;
		size = ARRAY_SIZE(skl_rates);
	} else {
		*source_rates = default_rates;
		size = ARRAY_SIZE(default_rates);
	}

	/* This depends on the fact that 5.4 is last value in the array */
	if (!intel_dp_source_supports_hbr2(intel_dp))
		size--;

	return size;
}

static void
intel_dp_set_clock(struct intel_encoder *encoder,
		   struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;
	const struct dp_link_dpll *divisor = NULL;
	int i, count = 0;

	if (IS_G4X(dev)) {
		divisor = gen4_dpll;
		count = ARRAY_SIZE(gen4_dpll);
	} else if (HAS_PCH_SPLIT(dev)) {
		divisor = pch_dpll;
		count = ARRAY_SIZE(pch_dpll);
	} else if (IS_CHERRYVIEW(dev)) {
		divisor = chv_dpll;
		count = ARRAY_SIZE(chv_dpll);
	} else if (IS_VALLEYVIEW(dev)) {
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

static int intel_dp_common_rates(struct intel_dp *intel_dp,
				 int *common_rates)
{
	const int *source_rates, *sink_rates;
	int source_len, sink_len;

	sink_len = intel_dp_sink_rates(intel_dp, &sink_rates);
	source_len = intel_dp_source_rates(intel_dp, &source_rates);

	return intersect_rates(source_rates, source_len,
			       sink_rates, sink_len,
			       common_rates);
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
	const int *source_rates, *sink_rates;
	int source_len, sink_len, common_len;
	int common_rates[DP_MAX_SUPPORTED_RATES];
	char str[128]; /* FIXME: too big for stack? */

	if ((drm_debug & DRM_UT_KMS) == 0)
		return;

	source_len = intel_dp_source_rates(intel_dp, &source_rates);
	snprintf_int_array(str, sizeof(str), source_rates, source_len);
	DRM_DEBUG_KMS("source rates: %s\n", str);

	sink_len = intel_dp_sink_rates(intel_dp, &sink_rates);
	snprintf_int_array(str, sizeof(str), sink_rates, sink_len);
	DRM_DEBUG_KMS("sink rates: %s\n", str);

	common_len = intel_dp_common_rates(intel_dp, common_rates);
	snprintf_int_array(str, sizeof(str), common_rates, common_len);
	DRM_DEBUG_KMS("common rates: %s\n", str);
}

static int rate_to_index(int find, const int *rates)
{
	int i = 0;

	for (i = 0; i < DP_MAX_SUPPORTED_RATES; ++i)
		if (find == rates[i])
			break;

	return i;
}

int
intel_dp_max_link_rate(struct intel_dp *intel_dp)
{
	int rates[DP_MAX_SUPPORTED_RATES] = {};
	int len;

	len = intel_dp_common_rates(intel_dp, rates);
	if (WARN_ON(len <= 0))
		return 162000;

	return rates[rate_to_index(0, rates) - 1];
}

int intel_dp_rate_select(struct intel_dp *intel_dp, int rate)
{
	return rate_to_index(rate, intel_dp->sink_rates);
}

void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock,
			   uint8_t *link_bw, uint8_t *rate_select)
{
	if (intel_dp->num_sink_rates) {
		*link_bw = 0;
		*rate_select =
			intel_dp_rate_select(intel_dp, port_clock);
	} else {
		*link_bw = drm_dp_link_rate_to_bw_code(port_clock);
		*rate_select = 0;
	}
}

bool
intel_dp_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->base.crtc);
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	int lane_count, clock;
	int min_lane_count = 1;
	int max_lane_count = intel_dp_max_lane_count(intel_dp);
	/* Conveniently, the link BW constants become indices with a shift...*/
	int min_clock = 0;
	int max_clock;
	int bpp, mode_rate;
	int link_avail, link_clock;
	int common_rates[DP_MAX_SUPPORTED_RATES] = {};
	int common_len;
	uint8_t link_bw, rate_select;

	common_len = intel_dp_common_rates(intel_dp, common_rates);

	/* No common link rates between source and sink */
	WARN_ON(common_len <= 0);

	max_clock = common_len - 1;

	if (HAS_PCH_SPLIT(dev) && !HAS_DDI(dev) && port != PORT_A)
		pipe_config->has_pch_encoder = true;

	pipe_config->has_dp_encoder = true;
	pipe_config->has_drrs = false;
	pipe_config->has_audio = intel_dp->has_audio && port != PORT_A;

	if (is_edp(intel_dp) && intel_connector->panel.fixed_mode) {
		intel_fixed_panel_mode(intel_connector->panel.fixed_mode,
				       adjusted_mode);

		if (INTEL_INFO(dev)->gen >= 9) {
			int ret;
			ret = skl_update_scaler_crtc(pipe_config);
			if (ret)
				return ret;
		}

		if (HAS_GMCH_DISPLAY(dev))
			intel_gmch_panel_fitting(intel_crtc, pipe_config,
						 intel_connector->panel.fitting_mode);
		else
			intel_pch_panel_fitting(intel_crtc, pipe_config,
						intel_connector->panel.fitting_mode);
	}

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLCLK)
		return false;

	DRM_DEBUG_KMS("DP link computation with max lane count %i "
		      "max bw %d pixel clock %iKHz\n",
		      max_lane_count, common_rates[max_clock],
		      adjusted_mode->crtc_clock);

	/* Walk through all bpp values. Luckily they're all nicely spaced with 2
	 * bpc in between. */
	bpp = pipe_config->pipe_bpp;
	if (is_edp(intel_dp)) {

		/* Get bpp from vbt only for panels that dont have bpp in edid */
		if (intel_connector->base.display_info.bpc == 0 &&
			(dev_priv->vbt.edp_bpp && dev_priv->vbt.edp_bpp < bpp)) {
			DRM_DEBUG_KMS("clamping bpp for eDP panel to BIOS-provided %i\n",
				      dev_priv->vbt.edp_bpp);
			bpp = dev_priv->vbt.edp_bpp;
		}

		/*
		 * Use the maximum clock and number of lanes the eDP panel
		 * advertizes being capable of. The panels are generally
		 * designed to support only a single clock and lane
		 * configuration, and typically these values correspond to the
		 * native resolution of the panel.
		 */
		min_lane_count = max_lane_count;
		min_clock = max_clock;
	}

	for (; bpp >= 6*3; bpp -= 2*3) {
		mode_rate = intel_dp_link_required(adjusted_mode->crtc_clock,
						   bpp);

		for (clock = min_clock; clock <= max_clock; clock++) {
			for (lane_count = min_lane_count;
				lane_count <= max_lane_count;
				lane_count <<= 1) {

				link_clock = common_rates[clock];
				link_avail = intel_dp_max_data_rate(link_clock,
								    lane_count);

				if (mode_rate <= link_avail) {
					goto found;
				}
			}
		}
	}

	return false;

found:
	if (intel_dp->color_range_auto) {
		/*
		 * See:
		 * CEA-861-E - 5.1 Default Encoding Parameters
		 * VESA DisplayPort Ver.1.2a - 5.1.1.1 Video Colorimetry
		 */
		pipe_config->limited_color_range =
			bpp != 18 && drm_match_cea_mode(adjusted_mode) > 1;
	} else {
		pipe_config->limited_color_range =
			intel_dp->limited_color_range;
	}

	pipe_config->lane_count = lane_count;

	pipe_config->pipe_bpp = bpp;
	pipe_config->port_clock = common_rates[clock];

	intel_dp_compute_rate(intel_dp, pipe_config->port_clock,
			      &link_bw, &rate_select);

	DRM_DEBUG_KMS("DP link bw %02x rate select %02x lane count %d clock %d bpp %d\n",
		      link_bw, rate_select, pipe_config->lane_count,
		      pipe_config->port_clock, bpp);
	DRM_DEBUG_KMS("DP link bw required %i available %i\n",
		      mode_rate, link_avail);

	intel_link_compute_m_n(bpp, lane_count,
			       adjusted_mode->crtc_clock,
			       pipe_config->port_clock,
			       &pipe_config->dp_m_n);

	if (intel_connector->panel.downclock_mode != NULL &&
		dev_priv->drrs.type == SEAMLESS_DRRS_SUPPORT) {
			pipe_config->has_drrs = true;
			intel_link_compute_m_n(bpp, lane_count,
				intel_connector->panel.downclock_mode->clock,
				pipe_config->port_clock,
				&pipe_config->dp_m2_n2);
	}

	if ((IS_SKYLAKE(dev)  || IS_KABYLAKE(dev)) && is_edp(intel_dp))
		skl_edp_set_pll_config(pipe_config);
	else if (IS_BROXTON(dev))
		/* handled in ddi */;
	else if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		hsw_dp_set_ddi_pll_sel(pipe_config);
	else
		intel_dp_set_clock(encoder, pipe_config);

	return true;
}

void intel_dp_set_link_params(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *pipe_config)
{
	intel_dp->link_rate = pipe_config->port_clock;
	intel_dp->lane_count = pipe_config->lane_count;
}

static void intel_dp_prepare(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	const struct drm_display_mode *adjusted_mode = &crtc->config->base.adjusted_mode;

	intel_dp_set_link_params(intel_dp, crtc->config);

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
	intel_dp->DP |= DP_PORT_WIDTH(crtc->config->lane_count);

	/* Split out the IBX/CPU vs CPT settings */

	if (IS_GEN7(dev) && port == PORT_A) {
		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;

		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		intel_dp->DP |= crtc->pipe << 29;
	} else if (HAS_PCH_CPT(dev) && port != PORT_A) {
		u32 trans_dp;

		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;

		trans_dp = I915_READ(TRANS_DP_CTL(crtc->pipe));
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			trans_dp |= TRANS_DP_ENH_FRAMING;
		else
			trans_dp &= ~TRANS_DP_ENH_FRAMING;
		I915_WRITE(TRANS_DP_CTL(crtc->pipe), trans_dp);
	} else {
		if (!HAS_PCH_SPLIT(dev) && !IS_VALLEYVIEW(dev) &&
		    crtc->config->limited_color_range)
			intel_dp->DP |= DP_COLOR_RANGE_16_235;

		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF;

		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		if (IS_CHERRYVIEW(dev))
			intel_dp->DP |= DP_PIPE_SELECT_CHV(crtc->pipe);
		else if (crtc->pipe == PIPE_B)
			intel_dp->DP |= DP_PIPEB_SELECT;
	}
}

#define IDLE_ON_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | PP_SEQUENCE_STATE_MASK)
#define IDLE_ON_VALUE   	(PP_ON | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_ON_IDLE)

#define IDLE_OFF_MASK		(PP_ON | PP_SEQUENCE_MASK | 0                     | 0)
#define IDLE_OFF_VALUE		(0     | PP_SEQUENCE_NONE | 0                     | 0)

#define IDLE_CYCLE_MASK		(PP_ON | PP_SEQUENCE_MASK | PP_CYCLE_DELAY_ACTIVE | PP_SEQUENCE_STATE_MASK)
#define IDLE_CYCLE_VALUE	(0     | PP_SEQUENCE_NONE | 0                     | PP_SEQUENCE_STATE_OFF_IDLE)

static void wait_panel_status(struct intel_dp *intel_dp,
				       u32 mask,
				       u32 value)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	pp_stat_reg = _pp_stat_reg(intel_dp);
	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	DRM_DEBUG_KMS("mask %08x value %08x status %08x control %08x\n",
			mask, value,
			I915_READ(pp_stat_reg),
			I915_READ(pp_ctrl_reg));

	if (_wait_for((I915_READ(pp_stat_reg) & mask) == value, 5000, 10)) {
		DRM_ERROR("Panel status timeout: status %08x control %08x\n",
				I915_READ(pp_stat_reg),
				I915_READ(pp_ctrl_reg));
	}

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
	DRM_DEBUG_KMS("Wait for panel power cycle\n");

	/* When we disable the VDD override bit last we have to do the manual
	 * wait. */
	wait_remaining_ms_from_jiffies(intel_dp->last_power_cycle,
				       intel_dp->panel_power_cycle_delay);

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
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 control;

	lockdep_assert_held(&dev_priv->pps_mutex);

	control = I915_READ(_pp_ctrl_reg(intel_dp));
	if (!IS_BROXTON(dev)) {
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
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain power_domain;
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;
	bool need_to_disable = !intel_dp->want_panel_vdd;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!is_edp(intel_dp))
		return false;

	cancel_delayed_work(&intel_dp->panel_vdd_work);
	intel_dp->want_panel_vdd = true;

	if (edp_have_panel_vdd(intel_dp))
		return need_to_disable;

	power_domain = intel_display_port_aux_power_domain(intel_encoder);
	intel_display_power_get(dev_priv, power_domain);

	DRM_DEBUG_KMS("Turning eDP port %c VDD on\n",
		      port_name(intel_dig_port->port));

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
			      port_name(intel_dig_port->port));
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
	bool vdd;

	if (!is_edp(intel_dp))
		return;

	pps_lock(intel_dp);
	vdd = edp_panel_vdd_on(intel_dp);
	pps_unlock(intel_dp);

	I915_STATE_WARN(!vdd, "eDP port %c VDD already requested on\n",
	     port_name(dp_to_dig_port(intel_dp)->port));
}

static void edp_panel_vdd_off_sync(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *intel_dig_port =
		dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	enum intel_display_power_domain power_domain;
	u32 pp;
	i915_reg_t pp_stat_reg, pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	WARN_ON(intel_dp->want_panel_vdd);

	if (!edp_have_panel_vdd(intel_dp))
		return;

	DRM_DEBUG_KMS("Turning eDP port %c VDD off\n",
		      port_name(intel_dig_port->port));

	pp = ironlake_get_pp_control(intel_dp);
	pp &= ~EDP_FORCE_VDD;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp_stat_reg = _pp_stat_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	/* Make sure sequencer is idle before allowing subsequent activity */
	DRM_DEBUG_KMS("PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
	I915_READ(pp_stat_reg), I915_READ(pp_ctrl_reg));

	if ((pp & POWER_TARGET_ON) == 0)
		intel_dp->last_power_cycle = jiffies;

	power_domain = intel_display_port_aux_power_domain(intel_encoder);
	intel_display_power_put(dev_priv, power_domain);
}

static void edp_panel_vdd_work(struct work_struct *__work)
{
	struct intel_dp *intel_dp = container_of(to_delayed_work(__work),
						 struct intel_dp, panel_vdd_work);

	pps_lock(intel_dp);
	if (!intel_dp->want_panel_vdd)
		edp_panel_vdd_off_sync(intel_dp);
	pps_unlock(intel_dp);
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
	struct drm_i915_private *dev_priv =
		intel_dp_to_dev(intel_dp)->dev_private;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!is_edp(intel_dp))
		return;

	I915_STATE_WARN(!intel_dp->want_panel_vdd, "eDP port %c VDD not forced on",
	     port_name(dp_to_dig_port(intel_dp)->port));

	intel_dp->want_panel_vdd = false;

	if (sync)
		edp_panel_vdd_off_sync(intel_dp);
	else
		edp_panel_vdd_schedule_off(intel_dp);
}

static void edp_panel_on(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("Turn eDP port %c panel power on\n",
		      port_name(dp_to_dig_port(intel_dp)->port));

	if (WARN(edp_have_panel_power(intel_dp),
		 "eDP port %c panel power already on\n",
		 port_name(dp_to_dig_port(intel_dp)->port)))
		return;

	wait_panel_power_cycle(intel_dp);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);
	pp = ironlake_get_pp_control(intel_dp);
	if (IS_GEN5(dev)) {
		/* ILK workaround: disable reset around power sequence */
		pp &= ~PANEL_POWER_RESET;
		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
	}

	pp |= POWER_TARGET_ON;
	if (!IS_GEN5(dev))
		pp |= PANEL_POWER_RESET;

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	wait_panel_on(intel_dp);
	intel_dp->last_power_on = jiffies;

	if (IS_GEN5(dev)) {
		pp |= PANEL_POWER_RESET; /* restore panel reset bit */
		I915_WRITE(pp_ctrl_reg, pp);
		POSTING_READ(pp_ctrl_reg);
	}
}

void intel_edp_panel_on(struct intel_dp *intel_dp)
{
	if (!is_edp(intel_dp))
		return;

	pps_lock(intel_dp);
	edp_panel_on(intel_dp);
	pps_unlock(intel_dp);
}


static void edp_panel_off(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain power_domain;
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("Turn eDP port %c panel power off\n",
		      port_name(dp_to_dig_port(intel_dp)->port));

	WARN(!intel_dp->want_panel_vdd, "Need eDP port %c VDD to turn off panel\n",
	     port_name(dp_to_dig_port(intel_dp)->port));

	pp = ironlake_get_pp_control(intel_dp);
	/* We need to switch off panel power _and_ force vdd, for otherwise some
	 * panels get very unhappy and cease to work. */
	pp &= ~(POWER_TARGET_ON | PANEL_POWER_RESET | EDP_FORCE_VDD |
		EDP_BLC_ENABLE);

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	intel_dp->want_panel_vdd = false;

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	intel_dp->last_power_cycle = jiffies;
	wait_panel_off(intel_dp);

	/* We got a reference when we enabled the VDD. */
	power_domain = intel_display_port_aux_power_domain(intel_encoder);
	intel_display_power_put(dev_priv, power_domain);
}

void intel_edp_panel_off(struct intel_dp *intel_dp)
{
	if (!is_edp(intel_dp))
		return;

	pps_lock(intel_dp);
	edp_panel_off(intel_dp);
	pps_unlock(intel_dp);
}

/* Enable backlight in the panel power control. */
static void _intel_edp_backlight_on(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	/*
	 * If we enable the backlight right away following a panel power
	 * on, we may see slight flicker as the panel syncs with the eDP
	 * link.  So delay a bit to make sure the image is solid before
	 * allowing it to appear.
	 */
	wait_backlight_on(intel_dp);

	pps_lock(intel_dp);

	pp = ironlake_get_pp_control(intel_dp);
	pp |= EDP_BLC_ENABLE;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	pps_unlock(intel_dp);
}

/* Enable backlight PWM and backlight PP control. */
void intel_edp_backlight_on(struct intel_dp *intel_dp)
{
	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("\n");

	intel_panel_enable_backlight(intel_dp->attached_connector);
	_intel_edp_backlight_on(intel_dp);
}

/* Disable backlight in the panel power control. */
static void _intel_edp_backlight_off(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp;
	i915_reg_t pp_ctrl_reg;

	if (!is_edp(intel_dp))
		return;

	pps_lock(intel_dp);

	pp = ironlake_get_pp_control(intel_dp);
	pp &= ~EDP_BLC_ENABLE;

	pp_ctrl_reg = _pp_ctrl_reg(intel_dp);

	I915_WRITE(pp_ctrl_reg, pp);
	POSTING_READ(pp_ctrl_reg);

	pps_unlock(intel_dp);

	intel_dp->last_backlight_off = jiffies;
	edp_wait_backlight_off(intel_dp);
}

/* Disable backlight PP control and backlight PWM. */
void intel_edp_backlight_off(struct intel_dp *intel_dp)
{
	if (!is_edp(intel_dp))
		return;

	DRM_DEBUG_KMS("\n");

	_intel_edp_backlight_off(intel_dp);
	intel_panel_disable_backlight(intel_dp->attached_connector);
}

/*
 * Hook for controlling the panel power control backlight through the bl_power
 * sysfs attribute. Take care to handle multiple calls.
 */
static void intel_edp_backlight_power(struct intel_connector *connector,
				      bool enable)
{
	struct intel_dp *intel_dp = intel_attached_dp(&connector->base);
	bool is_enabled;

	pps_lock(intel_dp);
	is_enabled = ironlake_get_pp_control(intel_dp) & EDP_BLC_ENABLE;
	pps_unlock(intel_dp);

	if (is_enabled == enable)
		return;

	DRM_DEBUG_KMS("panel power control backlight %s\n",
		      enable ? "enable" : "disable");

	if (enable)
		_intel_edp_backlight_on(intel_dp);
	else
		_intel_edp_backlight_off(intel_dp);
}

static const char *state_string(bool enabled)
{
	return enabled ? "on" : "off";
}

static void assert_dp_port(struct intel_dp *intel_dp, bool state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	bool cur_state = I915_READ(intel_dp->output_reg) & DP_PORT_EN;

	I915_STATE_WARN(cur_state != state,
			"DP port %c state assertion failure (expected %s, current %s)\n",
			port_name(dig_port->port),
			state_string(state), state_string(cur_state));
}
#define assert_dp_port_disabled(d) assert_dp_port((d), false)

static void assert_edp_pll(struct drm_i915_private *dev_priv, bool state)
{
	bool cur_state = I915_READ(DP_A) & DP_PLL_ENABLE;

	I915_STATE_WARN(cur_state != state,
			"eDP PLL state assertion failure (expected %s, current %s)\n",
			state_string(state), state_string(cur_state));
}
#define assert_edp_pll_enabled(d) assert_edp_pll((d), true)
#define assert_edp_pll_disabled(d) assert_edp_pll((d), false)

static void ironlake_edp_pll_on(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_crtc *crtc = to_intel_crtc(intel_dig_port->base.base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	assert_pipe_disabled(dev_priv, crtc->pipe);
	assert_dp_port_disabled(intel_dp);
	assert_edp_pll_disabled(dev_priv);

	DRM_DEBUG_KMS("enabling eDP PLL for clock %d\n",
		      crtc->config->port_clock);

	intel_dp->DP &= ~DP_PLL_FREQ_MASK;

	if (crtc->config->port_clock == 162000)
		intel_dp->DP |= DP_PLL_FREQ_162MHZ;
	else
		intel_dp->DP |= DP_PLL_FREQ_270MHZ;

	I915_WRITE(DP_A, intel_dp->DP);
	POSTING_READ(DP_A);
	udelay(500);

	intel_dp->DP |= DP_PLL_ENABLE;

	I915_WRITE(DP_A, intel_dp->DP);
	POSTING_READ(DP_A);
	udelay(200);
}

static void ironlake_edp_pll_off(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_crtc *crtc = to_intel_crtc(intel_dig_port->base.base.crtc);
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

/* If the sink supports it, try to set the power state appropriately */
void intel_dp_sink_dpms(struct intel_dp *intel_dp, int mode)
{
	int ret, i;

	/* Should have a valid DPCD by this point */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (mode != DRM_MODE_DPMS_ON) {
		ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER,
					 DP_SET_POWER_D3);
	} else {
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
	}

	if (ret != 1)
		DRM_DEBUG_KMS("failed to %s sink power state\n",
			      mode == DRM_MODE_DPMS_ON ? "enable" : "disable");
}

static bool intel_dp_get_hw_state(struct intel_encoder *encoder,
				  enum pipe *pipe)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain power_domain;
	u32 tmp;

	power_domain = intel_display_port_power_domain(encoder);
	if (!intel_display_power_is_enabled(dev_priv, power_domain))
		return false;

	tmp = I915_READ(intel_dp->output_reg);

	if (!(tmp & DP_PORT_EN))
		return false;

	if (IS_GEN7(dev) && port == PORT_A) {
		*pipe = PORT_TO_PIPE_CPT(tmp);
	} else if (HAS_PCH_CPT(dev) && port != PORT_A) {
		enum pipe p;

		for_each_pipe(dev_priv, p) {
			u32 trans_dp = I915_READ(TRANS_DP_CTL(p));
			if (TRANS_DP_PIPE_TO_PORT(trans_dp) == port) {
				*pipe = p;
				return true;
			}
		}

		DRM_DEBUG_KMS("No pipe for dp port 0x%x found\n",
			      i915_mmio_reg_offset(intel_dp->output_reg));
	} else if (IS_CHERRYVIEW(dev)) {
		*pipe = DP_PORT_TO_PIPE_CHV(tmp);
	} else {
		*pipe = PORT_TO_PIPE(tmp);
	}

	return true;
}

static void intel_dp_get_config(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	u32 tmp, flags = 0;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = dp_to_dig_port(intel_dp)->port;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	int dotclock;

	tmp = I915_READ(intel_dp->output_reg);

	pipe_config->has_audio = tmp & DP_AUDIO_OUTPUT_ENABLE && port != PORT_A;

	if (HAS_PCH_CPT(dev) && port != PORT_A) {
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

	if (!HAS_PCH_SPLIT(dev) && !IS_VALLEYVIEW(dev) &&
	    tmp & DP_COLOR_RANGE_16_235)
		pipe_config->limited_color_range = true;

	pipe_config->has_dp_encoder = true;

	pipe_config->lane_count =
		((tmp & DP_PORT_WIDTH_MASK) >> DP_PORT_WIDTH_SHIFT) + 1;

	intel_dp_get_m_n(crtc, pipe_config);

	if (port == PORT_A) {
		if ((I915_READ(DP_A) & DP_PLL_FREQ_MASK) == DP_PLL_FREQ_162MHZ)
			pipe_config->port_clock = 162000;
		else
			pipe_config->port_clock = 270000;
	}

	dotclock = intel_dotclock_calculate(pipe_config->port_clock,
					    &pipe_config->dp_m_n);

	if (HAS_PCH_SPLIT(dev_priv->dev) && port != PORT_A)
		ironlake_check_encoder_dotclock(pipe_config, dotclock);

	pipe_config->base.adjusted_mode.crtc_clock = dotclock;

	if (is_edp(intel_dp) && dev_priv->vbt.edp_bpp &&
	    pipe_config->pipe_bpp > dev_priv->vbt.edp_bpp) {
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
			      pipe_config->pipe_bpp, dev_priv->vbt.edp_bpp);
		dev_priv->vbt.edp_bpp = pipe_config->pipe_bpp;
	}
}

static void intel_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);

	if (crtc->config->has_audio)
		intel_audio_codec_disable(encoder);

	if (HAS_PSR(dev) && !HAS_DDI(dev))
		intel_psr_disable(intel_dp);

	/* Make sure the panel is off before trying to change the mode. But also
	 * ensure that we have vdd while we switch off the panel. */
	intel_edp_panel_vdd_on(intel_dp);
	intel_edp_backlight_off(intel_dp);
	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_OFF);
	intel_edp_panel_off(intel_dp);

	/* disable the port before the pipe on g4x */
	if (INTEL_INFO(dev)->gen < 5)
		intel_dp_link_down(intel_dp);
}

static void ilk_post_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;

	intel_dp_link_down(intel_dp);

	/* Only ilk+ has port A */
	if (port == PORT_A)
		ironlake_edp_pll_off(intel_dp);
}

static void vlv_post_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

	intel_dp_link_down(intel_dp);
}

static void chv_data_lane_soft_reset(struct intel_encoder *encoder,
				     bool reset)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum dpio_channel ch = vlv_dport_to_channel(enc_to_dig_port(&encoder->base));
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	enum pipe pipe = crtc->pipe;
	uint32_t val;

	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW0(ch));
	if (reset)
		val &= ~(DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET);
	else
		val |= DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW0(ch), val);

	if (crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW0(ch));
		if (reset)
			val &= ~(DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET);
		else
			val |= DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET;
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW0(ch), val);
	}

	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW1(ch));
	val |= CHV_PCS_REQ_SOFTRESET_EN;
	if (reset)
		val &= ~DPIO_PCS_CLK_SOFT_RESET;
	else
		val |= DPIO_PCS_CLK_SOFT_RESET;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW1(ch), val);

	if (crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW1(ch));
		val |= CHV_PCS_REQ_SOFTRESET_EN;
		if (reset)
			val &= ~DPIO_PCS_CLK_SOFT_RESET;
		else
			val |= DPIO_PCS_CLK_SOFT_RESET;
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW1(ch), val);
	}
}

static void chv_post_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	intel_dp_link_down(intel_dp);

	mutex_lock(&dev_priv->sb_lock);

	/* Assert data lane reset */
	chv_data_lane_soft_reset(encoder, true);

	mutex_unlock(&dev_priv->sb_lock);
}

static void
_intel_dp_set_link_train(struct intel_dp *intel_dp,
			 uint32_t *DP,
			 uint8_t dp_train_pat)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;

	if (HAS_DDI(dev)) {
		uint32_t temp = I915_READ(DP_TP_CTL(port));

		if (dp_train_pat & DP_LINK_SCRAMBLING_DISABLE)
			temp |= DP_TP_CTL_SCRAMBLE_DISABLE;
		else
			temp &= ~DP_TP_CTL_SCRAMBLE_DISABLE;

		temp &= ~DP_TP_CTL_LINK_TRAIN_MASK;
		switch (dp_train_pat & DP_TRAINING_PATTERN_MASK) {
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
		}
		I915_WRITE(DP_TP_CTL(port), temp);

	} else if ((IS_GEN7(dev) && port == PORT_A) ||
		   (HAS_PCH_CPT(dev) && port != PORT_A)) {
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
			DRM_ERROR("DP training pattern 3 not supported\n");
			*DP |= DP_LINK_TRAIN_PAT_2_CPT;
			break;
		}

	} else {
		if (IS_CHERRYVIEW(dev))
			*DP &= ~DP_LINK_TRAIN_MASK_CHV;
		else
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
			if (IS_CHERRYVIEW(dev)) {
				*DP |= DP_LINK_TRAIN_PAT_3_CHV;
			} else {
				DRM_ERROR("DP training pattern 3 not supported\n");
				*DP |= DP_LINK_TRAIN_PAT_2;
			}
			break;
		}
	}
}

static void intel_dp_enable_port(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc =
		to_intel_crtc(dp_to_dig_port(intel_dp)->base.base.crtc);

	/* enable with pattern 1 (as per spec) */
	_intel_dp_set_link_train(intel_dp, &intel_dp->DP,
				 DP_TRAINING_PATTERN_1);

	I915_WRITE(intel_dp->output_reg, intel_dp->DP);
	POSTING_READ(intel_dp->output_reg);

	/*
	 * Magic for VLV/CHV. We _must_ first set up the register
	 * without actually enabling the port, and then do another
	 * write to enable the port. Otherwise link training will
	 * fail when the power sequencer is freshly used for this port.
	 */
	intel_dp->DP |= DP_PORT_EN;
	if (crtc->config->has_audio)
		intel_dp->DP |= DP_AUDIO_OUTPUT_ENABLE;

	I915_WRITE(intel_dp->output_reg, intel_dp->DP);
	POSTING_READ(intel_dp->output_reg);
}

static void intel_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	uint32_t dp_reg = I915_READ(intel_dp->output_reg);
	enum port port = dp_to_dig_port(intel_dp)->port;
	enum pipe pipe = crtc->pipe;

	if (WARN_ON(dp_reg & DP_PORT_EN))
		return;

	pps_lock(intel_dp);

	if (IS_VALLEYVIEW(dev))
		vlv_init_panel_power_sequencer(intel_dp);

	/*
	 * We get an occasional spurious underrun between the port
	 * enable and vdd enable, when enabling port A eDP.
	 *
	 * FIXME: Not sure if this applies to (PCH) port D eDP as well
	 */
	if (port == PORT_A)
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, false);

	intel_dp_enable_port(intel_dp);

	if (port == PORT_A && IS_GEN5(dev_priv)) {
		/*
		 * Underrun reporting for the other pipe was disabled in
		 * g4x_pre_enable_dp(). The eDP PLL and port have now been
		 * enabled, so it's now safe to re-enable underrun reporting.
		 */
		intel_wait_for_vblank_if_active(dev_priv->dev, !pipe);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, !pipe, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, !pipe, true);
	}

	edp_panel_vdd_on(intel_dp);
	edp_panel_on(intel_dp);
	edp_panel_vdd_off(intel_dp, true);

	if (port == PORT_A)
		intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	pps_unlock(intel_dp);

	if (IS_VALLEYVIEW(dev)) {
		unsigned int lane_mask = 0x0;

		if (IS_CHERRYVIEW(dev))
			lane_mask = intel_dp_unused_lane_mask(crtc->config->lane_count);

		vlv_wait_port_ready(dev_priv, dp_to_dig_port(intel_dp),
				    lane_mask);
	}

	intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);
	intel_dp_start_link_train(intel_dp);
	intel_dp_stop_link_train(intel_dp);

	if (crtc->config->has_audio) {
		DRM_DEBUG_DRIVER("Enabling DP audio on pipe %c\n",
				 pipe_name(pipe));
		intel_audio_codec_enable(encoder);
	}
}

static void g4x_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

	intel_enable_dp(encoder);
	intel_edp_backlight_on(intel_dp);
}

static void vlv_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);

	intel_edp_backlight_on(intel_dp);
	intel_psr_enable(intel_dp);
}

static void g4x_pre_enable_dp(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	enum port port = dp_to_dig_port(intel_dp)->port;
	enum pipe pipe = to_intel_crtc(encoder->base.crtc)->pipe;

	intel_dp_prepare(encoder);

	if (port == PORT_A && IS_GEN5(dev_priv)) {
		/*
		 * We get FIFO underruns on the other pipe when
		 * enabling the CPU eDP PLL, and when enabling CPU
		 * eDP port. We could potentially avoid the PLL
		 * underrun with a vblank wait just prior to enabling
		 * the PLL, but that doesn't appear to help the port
		 * enable case. Just sweep it all under the rug.
		 */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, !pipe, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, !pipe, false);
	}

	/* Only ilk+ has port A */
	if (port == PORT_A)
		ironlake_edp_pll_on(intel_dp);
}

static void vlv_detach_power_sequencer(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = intel_dig_port->base.base.dev->dev_private;
	enum pipe pipe = intel_dp->pps_pipe;
	i915_reg_t pp_on_reg = VLV_PIPE_PP_ON_DELAYS(pipe);

	edp_panel_vdd_off_sync(intel_dp);

	/*
	 * VLV seems to get confused when multiple power seqeuencers
	 * have the same port selected (even if only one has power/vdd
	 * enabled). The failure manifests as vlv_wait_port_ready() failing
	 * CHV on the other hand doesn't seem to mind having the same port
	 * selected in multiple power seqeuencers, but let's clear the
	 * port select always when logically disconnecting a power sequencer
	 * from a port.
	 */
	DRM_DEBUG_KMS("detaching pipe %c power sequencer from port %c\n",
		      pipe_name(pipe), port_name(intel_dig_port->port));
	I915_WRITE(pp_on_reg, 0);
	POSTING_READ(pp_on_reg);

	intel_dp->pps_pipe = INVALID_PIPE;
}

static void vlv_steal_power_sequencer(struct drm_device *dev,
				      enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (WARN_ON(pipe != PIPE_A && pipe != PIPE_B))
		return;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list,
			    base.head) {
		struct intel_dp *intel_dp;
		enum port port;

		if (encoder->type != INTEL_OUTPUT_EDP)
			continue;

		intel_dp = enc_to_intel_dp(&encoder->base);
		port = dp_to_dig_port(intel_dp)->port;

		if (intel_dp->pps_pipe != pipe)
			continue;

		DRM_DEBUG_KMS("stealing pipe %c power sequencer from port %c\n",
			      pipe_name(pipe), port_name(port));

		WARN(encoder->base.crtc,
		     "stealing pipe %c power sequencer from active eDP port %c\n",
		     pipe_name(pipe), port_name(port));

		/* make sure vdd is off before we steal it */
		vlv_detach_power_sequencer(intel_dp);
	}
}

static void vlv_init_panel_power_sequencer(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &intel_dig_port->base;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (!is_edp(intel_dp))
		return;

	if (intel_dp->pps_pipe == crtc->pipe)
		return;

	/*
	 * If another power sequencer was being used on this
	 * port previously make sure to turn off vdd there while
	 * we still have control of it.
	 */
	if (intel_dp->pps_pipe != INVALID_PIPE)
		vlv_detach_power_sequencer(intel_dp);

	/*
	 * We may be stealing the power
	 * sequencer from another port.
	 */
	vlv_steal_power_sequencer(dev, crtc->pipe);

	/* now it's all ours */
	intel_dp->pps_pipe = crtc->pipe;

	DRM_DEBUG_KMS("initializing pipe %c power sequencer for port %c\n",
		      pipe_name(intel_dp->pps_pipe), port_name(intel_dig_port->port));

	/* init power sequencer on this pipe and port */
	intel_dp_init_panel_power_sequencer(dev, intel_dp);
	intel_dp_init_panel_power_sequencer_registers(dev, intel_dp);
}

static void vlv_pre_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;
	u32 val;

	mutex_lock(&dev_priv->sb_lock);

	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW8(port));
	val = 0;
	if (pipe)
		val |= (1<<21);
	else
		val &= ~(1<<21);
	val |= 0x001000c4;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW8(port), val);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW14(port), 0x00760018);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW23(port), 0x00400888);

	mutex_unlock(&dev_priv->sb_lock);

	intel_enable_dp(encoder);
}

static void vlv_dp_pre_pll_enable(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

	intel_dp_prepare(encoder);

	/* Program Tx lane resets to default */
	mutex_lock(&dev_priv->sb_lock);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW0(port),
			 DPIO_PCS_TX_LANE2_RESET |
			 DPIO_PCS_TX_LANE1_RESET);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW1(port),
			 DPIO_PCS_CLK_CRI_RXEB_EIOS_EN |
			 DPIO_PCS_CLK_CRI_RXDIGFILTSG_EN |
			 (1<<DPIO_PCS_CLK_DATAWIDTH_SHIFT) |
				 DPIO_PCS_CLK_SOFT_RESET);

	/* Fix up inter-pair skew failure */
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW12(port), 0x00750f00);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW11(port), 0x00001500);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW14(port), 0x40400000);
	mutex_unlock(&dev_priv->sb_lock);
}

static void chv_pre_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	enum dpio_channel ch = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;
	int data, i, stagger;
	u32 val;

	mutex_lock(&dev_priv->sb_lock);

	/* allow hardware to manage TX FIFO reset source */
	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW11(ch));
	val &= ~DPIO_LANEDESKEW_STRAP_OVRD;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW11(ch), val);

	if (intel_crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW11(ch));
		val &= ~DPIO_LANEDESKEW_STRAP_OVRD;
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW11(ch), val);
	}

	/* Program Tx lane latency optimal setting*/
	for (i = 0; i < intel_crtc->config->lane_count; i++) {
		/* Set the upar bit */
		if (intel_crtc->config->lane_count == 1)
			data = 0x0;
		else
			data = (i == 1) ? 0x0 : 0x1;
		vlv_dpio_write(dev_priv, pipe, CHV_TX_DW14(ch, i),
				data << DPIO_UPAR_SHIFT);
	}

	/* Data lane stagger programming */
	if (intel_crtc->config->port_clock > 270000)
		stagger = 0x18;
	else if (intel_crtc->config->port_clock > 135000)
		stagger = 0xd;
	else if (intel_crtc->config->port_clock > 67500)
		stagger = 0x7;
	else if (intel_crtc->config->port_clock > 33750)
		stagger = 0x4;
	else
		stagger = 0x2;

	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW11(ch));
	val |= DPIO_TX2_STAGGER_MASK(0x1f);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW11(ch), val);

	if (intel_crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW11(ch));
		val |= DPIO_TX2_STAGGER_MASK(0x1f);
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW11(ch), val);
	}

	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW12(ch),
		       DPIO_LANESTAGGER_STRAP(stagger) |
		       DPIO_LANESTAGGER_STRAP_OVRD |
		       DPIO_TX1_STAGGER_MASK(0x1f) |
		       DPIO_TX1_STAGGER_MULT(6) |
		       DPIO_TX2_STAGGER_MULT(0));

	if (intel_crtc->config->lane_count > 2) {
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW12(ch),
			       DPIO_LANESTAGGER_STRAP(stagger) |
			       DPIO_LANESTAGGER_STRAP_OVRD |
			       DPIO_TX1_STAGGER_MASK(0x1f) |
			       DPIO_TX1_STAGGER_MULT(7) |
			       DPIO_TX2_STAGGER_MULT(5));
	}

	/* Deassert data lane reset */
	chv_data_lane_soft_reset(encoder, false);

	mutex_unlock(&dev_priv->sb_lock);

	intel_enable_dp(encoder);

	/* Second common lane will stay alive on its own now */
	if (dport->release_cl2_override) {
		chv_phy_powergate_ch(dev_priv, DPIO_PHY0, DPIO_CH1, false);
		dport->release_cl2_override = false;
	}
}

static void chv_dp_pre_pll_enable(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	enum dpio_channel ch = vlv_dport_to_channel(dport);
	enum pipe pipe = intel_crtc->pipe;
	unsigned int lane_mask =
		intel_dp_unused_lane_mask(intel_crtc->config->lane_count);
	u32 val;

	intel_dp_prepare(encoder);

	/*
	 * Must trick the second common lane into life.
	 * Otherwise we can't even access the PLL.
	 */
	if (ch == DPIO_CH0 && pipe == PIPE_B)
		dport->release_cl2_override =
			!chv_phy_powergate_ch(dev_priv, DPIO_PHY0, DPIO_CH1, true);

	chv_phy_powergate_lanes(encoder, true, lane_mask);

	mutex_lock(&dev_priv->sb_lock);

	/* Assert data lane reset */
	chv_data_lane_soft_reset(encoder, true);

	/* program left/right clock distribution */
	if (pipe != PIPE_B) {
		val = vlv_dpio_read(dev_priv, pipe, _CHV_CMN_DW5_CH0);
		val &= ~(CHV_BUFLEFTENA1_MASK | CHV_BUFRIGHTENA1_MASK);
		if (ch == DPIO_CH0)
			val |= CHV_BUFLEFTENA1_FORCE;
		if (ch == DPIO_CH1)
			val |= CHV_BUFRIGHTENA1_FORCE;
		vlv_dpio_write(dev_priv, pipe, _CHV_CMN_DW5_CH0, val);
	} else {
		val = vlv_dpio_read(dev_priv, pipe, _CHV_CMN_DW1_CH1);
		val &= ~(CHV_BUFLEFTENA2_MASK | CHV_BUFRIGHTENA2_MASK);
		if (ch == DPIO_CH0)
			val |= CHV_BUFLEFTENA2_FORCE;
		if (ch == DPIO_CH1)
			val |= CHV_BUFRIGHTENA2_FORCE;
		vlv_dpio_write(dev_priv, pipe, _CHV_CMN_DW1_CH1, val);
	}

	/* program clock channel usage */
	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW8(ch));
	val |= CHV_PCS_USEDCLKCHANNEL_OVRRIDE;
	if (pipe != PIPE_B)
		val &= ~CHV_PCS_USEDCLKCHANNEL;
	else
		val |= CHV_PCS_USEDCLKCHANNEL;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW8(ch), val);

	if (intel_crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW8(ch));
		val |= CHV_PCS_USEDCLKCHANNEL_OVRRIDE;
		if (pipe != PIPE_B)
			val &= ~CHV_PCS_USEDCLKCHANNEL;
		else
			val |= CHV_PCS_USEDCLKCHANNEL;
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW8(ch), val);
	}

	/*
	 * This a a bit weird since generally CL
	 * matches the pipe, but here we need to
	 * pick the CL based on the port.
	 */
	val = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW19(ch));
	if (pipe != PIPE_B)
		val &= ~CHV_CMN_USEDCLKCHANNEL;
	else
		val |= CHV_CMN_USEDCLKCHANNEL;
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW19(ch), val);

	mutex_unlock(&dev_priv->sb_lock);
}

static void chv_dp_post_pll_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum pipe pipe = to_intel_crtc(encoder->base.crtc)->pipe;
	u32 val;

	mutex_lock(&dev_priv->sb_lock);

	/* disable left/right clock distribution */
	if (pipe != PIPE_B) {
		val = vlv_dpio_read(dev_priv, pipe, _CHV_CMN_DW5_CH0);
		val &= ~(CHV_BUFLEFTENA1_MASK | CHV_BUFRIGHTENA1_MASK);
		vlv_dpio_write(dev_priv, pipe, _CHV_CMN_DW5_CH0, val);
	} else {
		val = vlv_dpio_read(dev_priv, pipe, _CHV_CMN_DW1_CH1);
		val &= ~(CHV_BUFLEFTENA2_MASK | CHV_BUFRIGHTENA2_MASK);
		vlv_dpio_write(dev_priv, pipe, _CHV_CMN_DW1_CH1, val);
	}

	mutex_unlock(&dev_priv->sb_lock);

	/*
	 * Leave the power down bit cleared for at least one
	 * lane so that chv_powergate_phy_ch() will power
	 * on something when the channel is otherwise unused.
	 * When the port is off and the override is removed
	 * the lanes power down anyway, so otherwise it doesn't
	 * really matter what the state of power down bits is
	 * after this.
	 */
	chv_phy_powergate_lanes(encoder, false, 0x0);
}

/*
 * Native read with retry for link status and receiver capability reads for
 * cases where the sink may still be asleep.
 *
 * Sinks are *supposed* to come up within 1ms from an off state, but we're also
 * supposed to retry 3 times per the spec.
 */
static ssize_t
intel_dp_dpcd_read_wake(struct drm_dp_aux *aux, unsigned int offset,
			void *buffer, size_t size)
{
	ssize_t ret;
	int i;

	/*
	 * Sometime we just get the same incorrect byte repeated
	 * over the entire buffer. Doing just one throw away read
	 * initially seems to "solve" it.
	 */
	drm_dp_dpcd_read(aux, DP_DPCD_REV, buffer, 1);

	for (i = 0; i < 3; i++) {
		ret = drm_dp_dpcd_read(aux, offset, buffer, size);
		if (ret == size)
			return ret;
		msleep(1);
	}

	return ret;
}

/*
 * Fetch AUX CH registers 0x202 - 0x207 which contain
 * link status information
 */
bool
intel_dp_get_link_status(struct intel_dp *intel_dp, uint8_t link_status[DP_LINK_STATUS_SIZE])
{
	return intel_dp_dpcd_read_wake(&intel_dp->aux,
				       DP_LANE0_1_STATUS,
				       link_status,
				       DP_LINK_STATUS_SIZE) == DP_LINK_STATUS_SIZE;
}

/* These are source-specific values. */
uint8_t
intel_dp_voltage_max(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = dp_to_dig_port(intel_dp)->port;

	if (IS_BROXTON(dev))
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	else if (INTEL_INFO(dev)->gen >= 9) {
		if (dev_priv->edp_low_vswing && port == PORT_A)
			return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
	} else if (IS_VALLEYVIEW(dev))
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	else if (IS_GEN7(dev) && port == PORT_A)
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
	else if (HAS_PCH_CPT(dev) && port != PORT_A)
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	else
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
}

uint8_t
intel_dp_pre_emphasis_max(struct intel_dp *intel_dp, uint8_t voltage_swing)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	enum port port = dp_to_dig_port(intel_dp)->port;

	if (INTEL_INFO(dev)->gen >= 9) {
		switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			return DP_TRAIN_PRE_EMPH_LEVEL_3;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			return DP_TRAIN_PRE_EMPH_LEVEL_2;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			return DP_TRAIN_PRE_EMPH_LEVEL_1;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
			return DP_TRAIN_PRE_EMPH_LEVEL_0;
		default:
			return DP_TRAIN_PRE_EMPH_LEVEL_0;
		}
	} else if (IS_HASWELL(dev) || IS_BROADWELL(dev)) {
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
	} else if (IS_VALLEYVIEW(dev)) {
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
	} else if (IS_GEN7(dev) && port == PORT_A) {
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

static uint32_t vlv_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct intel_crtc *intel_crtc =
		to_intel_crtc(dport->base.base.crtc);
	unsigned long demph_reg_value, preemph_reg_value,
		uniqtranscale_reg_value;
	uint8_t train_set = intel_dp->train_set[0];
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

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

	mutex_lock(&dev_priv->sb_lock);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW5(port), 0x00000000);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW4(port), demph_reg_value);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW2(port),
			 uniqtranscale_reg_value);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW3(port), 0x0C782040);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW11(port), 0x00030000);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW9(port), preemph_reg_value);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW5(port), 0x80000000);
	mutex_unlock(&dev_priv->sb_lock);

	return 0;
}

static bool chv_need_uniq_trans_scale(uint8_t train_set)
{
	return (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) == DP_TRAIN_PRE_EMPH_LEVEL_0 &&
		(train_set & DP_TRAIN_VOLTAGE_SWING_MASK) == DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
}

static uint32_t chv_signal_levels(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct intel_crtc *intel_crtc = to_intel_crtc(dport->base.base.crtc);
	u32 deemph_reg_value, margin_reg_value, val;
	uint8_t train_set = intel_dp->train_set[0];
	enum dpio_channel ch = vlv_dport_to_channel(dport);
	enum pipe pipe = intel_crtc->pipe;
	int i;

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
			/* FIXME extra to set for 1200 */
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

	mutex_lock(&dev_priv->sb_lock);

	/* Clear calc init */
	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW10(ch));
	val &= ~(DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3);
	val &= ~(DPIO_PCS_TX1DEEMP_MASK | DPIO_PCS_TX2DEEMP_MASK);
	val |= DPIO_PCS_TX1DEEMP_9P5 | DPIO_PCS_TX2DEEMP_9P5;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW10(ch), val);

	if (intel_crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW10(ch));
		val &= ~(DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3);
		val &= ~(DPIO_PCS_TX1DEEMP_MASK | DPIO_PCS_TX2DEEMP_MASK);
		val |= DPIO_PCS_TX1DEEMP_9P5 | DPIO_PCS_TX2DEEMP_9P5;
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW10(ch), val);
	}

	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW9(ch));
	val &= ~(DPIO_PCS_TX1MARGIN_MASK | DPIO_PCS_TX2MARGIN_MASK);
	val |= DPIO_PCS_TX1MARGIN_000 | DPIO_PCS_TX2MARGIN_000;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW9(ch), val);

	if (intel_crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW9(ch));
		val &= ~(DPIO_PCS_TX1MARGIN_MASK | DPIO_PCS_TX2MARGIN_MASK);
		val |= DPIO_PCS_TX1MARGIN_000 | DPIO_PCS_TX2MARGIN_000;
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW9(ch), val);
	}

	/* Program swing deemph */
	for (i = 0; i < intel_crtc->config->lane_count; i++) {
		val = vlv_dpio_read(dev_priv, pipe, CHV_TX_DW4(ch, i));
		val &= ~DPIO_SWING_DEEMPH9P5_MASK;
		val |= deemph_reg_value << DPIO_SWING_DEEMPH9P5_SHIFT;
		vlv_dpio_write(dev_priv, pipe, CHV_TX_DW4(ch, i), val);
	}

	/* Program swing margin */
	for (i = 0; i < intel_crtc->config->lane_count; i++) {
		val = vlv_dpio_read(dev_priv, pipe, CHV_TX_DW2(ch, i));

		val &= ~DPIO_SWING_MARGIN000_MASK;
		val |= margin_reg_value << DPIO_SWING_MARGIN000_SHIFT;

		/*
		 * Supposedly this value shouldn't matter when unique transition
		 * scale is disabled, but in fact it does matter. Let's just
		 * always program the same value and hope it's OK.
		 */
		val &= ~(0xff << DPIO_UNIQ_TRANS_SCALE_SHIFT);
		val |= 0x9a << DPIO_UNIQ_TRANS_SCALE_SHIFT;

		vlv_dpio_write(dev_priv, pipe, CHV_TX_DW2(ch, i), val);
	}

	/*
	 * The document said it needs to set bit 27 for ch0 and bit 26
	 * for ch1. Might be a typo in the doc.
	 * For now, for this unique transition scale selection, set bit
	 * 27 for ch0 and ch1.
	 */
	for (i = 0; i < intel_crtc->config->lane_count; i++) {
		val = vlv_dpio_read(dev_priv, pipe, CHV_TX_DW3(ch, i));
		if (chv_need_uniq_trans_scale(train_set))
			val |= DPIO_TX_UNIQ_TRANS_SCALE_EN;
		else
			val &= ~DPIO_TX_UNIQ_TRANS_SCALE_EN;
		vlv_dpio_write(dev_priv, pipe, CHV_TX_DW3(ch, i), val);
	}

	/* Start swing calculation */
	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW10(ch));
	val |= DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS01_DW10(ch), val);

	if (intel_crtc->config->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, pipe, VLV_PCS23_DW10(ch));
		val |= DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3;
		vlv_dpio_write(dev_priv, pipe, VLV_PCS23_DW10(ch), val);
	}

	mutex_unlock(&dev_priv->sb_lock);

	return 0;
}

static uint32_t
gen4_signal_levels(uint8_t train_set)
{
	uint32_t	signal_levels = 0;

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

/* Gen6's DP voltage swing and pre-emphasis control */
static uint32_t
gen6_edp_signal_levels(uint8_t train_set)
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

/* Gen7's DP voltage swing and pre-emphasis control */
static uint32_t
gen7_edp_signal_levels(uint8_t train_set)
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	enum port port = intel_dig_port->port;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint32_t signal_levels, mask = 0;
	uint8_t train_set = intel_dp->train_set[0];

	if (HAS_DDI(dev)) {
		signal_levels = ddi_signal_levels(intel_dp);

		if (IS_BROXTON(dev))
			signal_levels = 0;
		else
			mask = DDI_BUF_EMP_MASK;
	} else if (IS_CHERRYVIEW(dev)) {
		signal_levels = chv_signal_levels(intel_dp);
	} else if (IS_VALLEYVIEW(dev)) {
		signal_levels = vlv_signal_levels(intel_dp);
	} else if (IS_GEN7(dev) && port == PORT_A) {
		signal_levels = gen7_edp_signal_levels(train_set);
		mask = EDP_LINK_TRAIN_VOL_EMP_MASK_IVB;
	} else if (IS_GEN6(dev) && port == PORT_A) {
		signal_levels = gen6_edp_signal_levels(train_set);
		mask = EDP_LINK_TRAIN_VOL_EMP_MASK_SNB;
	} else {
		signal_levels = gen4_signal_levels(train_set);
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
				       uint8_t dp_train_pat)
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
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	uint32_t val;

	if (!HAS_DDI(dev))
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

	if (wait_for((I915_READ(DP_TP_STATUS(port)) & DP_TP_STATUS_IDLE_DONE),
		     1))
		DRM_ERROR("Timed out waiting for DP idle patterns\n");
}

static void
intel_dp_link_down(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_crtc *crtc = to_intel_crtc(intel_dig_port->base.base.crtc);
	enum port port = intel_dig_port->port;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t DP = intel_dp->DP;

	if (WARN_ON(HAS_DDI(dev)))
		return;

	if (WARN_ON((I915_READ(intel_dp->output_reg) & DP_PORT_EN) == 0))
		return;

	DRM_DEBUG_KMS("\n");

	if ((IS_GEN7(dev) && port == PORT_A) ||
	    (HAS_PCH_CPT(dev) && port != PORT_A)) {
		DP &= ~DP_LINK_TRAIN_MASK_CPT;
		DP |= DP_LINK_TRAIN_PAT_IDLE_CPT;
	} else {
		if (IS_CHERRYVIEW(dev))
			DP &= ~DP_LINK_TRAIN_MASK_CHV;
		else
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
	if (HAS_PCH_IBX(dev) && crtc->pipe == PIPE_B && port != PORT_A) {
		/*
		 * We get CPU/PCH FIFO underruns on the other pipe when
		 * doing the workaround. Sweep them under the rug.
		 */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, false);

		/* always enable with pattern 1 (as per spec) */
		DP &= ~(DP_PIPEB_SELECT | DP_LINK_TRAIN_MASK);
		DP |= DP_PORT_EN | DP_LINK_TRAIN_PAT_1;
		I915_WRITE(intel_dp->output_reg, DP);
		POSTING_READ(intel_dp->output_reg);

		DP &= ~DP_PORT_EN;
		I915_WRITE(intel_dp->output_reg, DP);
		POSTING_READ(intel_dp->output_reg);

		intel_wait_for_vblank_if_active(dev_priv->dev, PIPE_A);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, true);
	}

	msleep(intel_dp->panel_power_down_delay);

	intel_dp->DP = DP;
}

static bool
intel_dp_get_dpcd(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint8_t rev;

	if (intel_dp_dpcd_read_wake(&intel_dp->aux, 0x000, intel_dp->dpcd,
				    sizeof(intel_dp->dpcd)) < 0)
		return false; /* aux transfer failed */

	DRM_DEBUG_KMS("DPCD: %*ph\n", (int) sizeof(intel_dp->dpcd), intel_dp->dpcd);

	if (intel_dp->dpcd[DP_DPCD_REV] == 0)
		return false; /* DPCD not present */

	/* Check if the panel supports PSR */
	memset(intel_dp->psr_dpcd, 0, sizeof(intel_dp->psr_dpcd));
	if (is_edp(intel_dp)) {
		intel_dp_dpcd_read_wake(&intel_dp->aux, DP_PSR_SUPPORT,
					intel_dp->psr_dpcd,
					sizeof(intel_dp->psr_dpcd));
		if (intel_dp->psr_dpcd[0] & DP_PSR_IS_SUPPORTED) {
			dev_priv->psr.sink_support = true;
			DRM_DEBUG_KMS("Detected EDP PSR Panel.\n");
		}

		if (INTEL_INFO(dev)->gen >= 9 &&
			(intel_dp->psr_dpcd[0] & DP_PSR2_IS_SUPPORTED)) {
			uint8_t frame_sync_cap;

			dev_priv->psr.sink_support = true;
			intel_dp_dpcd_read_wake(&intel_dp->aux,
					DP_SINK_DEVICE_AUX_FRAME_SYNC_CAP,
					&frame_sync_cap, 1);
			dev_priv->psr.aux_frame_sync = frame_sync_cap ? true : false;
			/* PSR2 needs frame sync as well */
			dev_priv->psr.psr2_support = dev_priv->psr.aux_frame_sync;
			DRM_DEBUG_KMS("PSR2 %s on sink",
				dev_priv->psr.psr2_support ? "supported" : "not supported");
		}
	}

	DRM_DEBUG_KMS("Display Port TPS3 support: source %s, sink %s\n",
		      yesno(intel_dp_source_supports_hbr2(intel_dp)),
		      yesno(drm_dp_tps3_supported(intel_dp->dpcd)));

	/* Intermediate frequency support */
	if (is_edp(intel_dp) &&
	    (intel_dp->dpcd[DP_EDP_CONFIGURATION_CAP] &	DP_DPCD_DISPLAY_CONTROL_CAPABLE) &&
	    (intel_dp_dpcd_read_wake(&intel_dp->aux, DP_EDP_DPCD_REV, &rev, 1) == 1) &&
	    (rev >= 0x03)) { /* eDp v1.4 or higher */
		__le16 sink_rates[DP_MAX_SUPPORTED_RATES];
		int i;

		intel_dp_dpcd_read_wake(&intel_dp->aux,
				DP_SUPPORTED_LINK_RATES,
				sink_rates,
				sizeof(sink_rates));

		for (i = 0; i < ARRAY_SIZE(sink_rates); i++) {
			int val = le16_to_cpu(sink_rates[i]);

			if (val == 0)
				break;

			/* Value read is in kHz while drm clock is saved in deca-kHz */
			intel_dp->sink_rates[i] = (val * 200) / 10;
		}
		intel_dp->num_sink_rates = i;
	}

	intel_dp_print_rates(intel_dp);

	if (!(intel_dp->dpcd[DP_DOWNSTREAMPORT_PRESENT] &
	      DP_DWN_STRM_PORT_PRESENT))
		return true; /* native DP sink */

	if (intel_dp->dpcd[DP_DPCD_REV] == 0x10)
		return true; /* no per-port downstream info */

	if (intel_dp_dpcd_read_wake(&intel_dp->aux, DP_DOWNSTREAM_PORT_0,
				    intel_dp->downstream_ports,
				    DP_MAX_DOWNSTREAM_PORTS) < 0)
		return false; /* downstream port status fetch failed */

	return true;
}

static void
intel_dp_probe_oui(struct intel_dp *intel_dp)
{
	u8 buf[3];

	if (!(intel_dp->dpcd[DP_DOWN_STREAM_PORT_COUNT] & DP_OUI_SUPPORT))
		return;

	if (intel_dp_dpcd_read_wake(&intel_dp->aux, DP_SINK_OUI, buf, 3) == 3)
		DRM_DEBUG_KMS("Sink OUI: %02hx%02hx%02hx\n",
			      buf[0], buf[1], buf[2]);

	if (intel_dp_dpcd_read_wake(&intel_dp->aux, DP_BRANCH_OUI, buf, 3) == 3)
		DRM_DEBUG_KMS("Branch OUI: %02hx%02hx%02hx\n",
			      buf[0], buf[1], buf[2]);
}

static bool
intel_dp_probe_mst(struct intel_dp *intel_dp)
{
	u8 buf[1];

	if (!intel_dp->can_mst)
		return false;

	if (intel_dp->dpcd[DP_DPCD_REV] < 0x12)
		return false;

	if (intel_dp_dpcd_read_wake(&intel_dp->aux, DP_MSTM_CAP, buf, 1)) {
		if (buf[0] & DP_MST_CAP) {
			DRM_DEBUG_KMS("Sink is MST capable\n");
			intel_dp->is_mst = true;
		} else {
			DRM_DEBUG_KMS("Sink is not MST capable\n");
			intel_dp->is_mst = false;
		}
	}

	drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr, intel_dp->is_mst);
	return intel_dp->is_mst;
}

static int intel_dp_sink_crc_stop(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(dig_port->base.base.crtc);
	u8 buf;
	int ret = 0;
	int count = 0;
	int attempts = 10;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_SINK, &buf) < 0) {
		DRM_DEBUG_KMS("Sink CRC couldn't be stopped properly\n");
		ret = -EIO;
		goto out;
	}

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_TEST_SINK,
			       buf & ~DP_TEST_SINK_START) < 0) {
		DRM_DEBUG_KMS("Sink CRC couldn't be stopped properly\n");
		ret = -EIO;
		goto out;
	}

	do {
		intel_wait_for_vblank(dev, intel_crtc->pipe);

		if (drm_dp_dpcd_readb(&intel_dp->aux,
				      DP_TEST_SINK_MISC, &buf) < 0) {
			ret = -EIO;
			goto out;
		}
		count = buf & DP_TEST_COUNT_MASK;
	} while (--attempts && count);

	if (attempts == 0) {
		DRM_ERROR("TIMEOUT: Sink CRC counter is not zeroed\n");
		ret = -ETIMEDOUT;
	}

 out:
	hsw_enable_ips(intel_crtc);
	return ret;
}

static int intel_dp_sink_crc_start(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(dig_port->base.base.crtc);
	u8 buf;
	int ret;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_SINK_MISC, &buf) < 0)
		return -EIO;

	if (!(buf & DP_TEST_CRC_SUPPORTED))
		return -ENOTTY;

	if (drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_SINK, &buf) < 0)
		return -EIO;

	if (buf & DP_TEST_SINK_START) {
		ret = intel_dp_sink_crc_stop(intel_dp);
		if (ret)
			return ret;
	}

	hsw_disable_ips(intel_crtc);

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_TEST_SINK,
			       buf | DP_TEST_SINK_START) < 0) {
		hsw_enable_ips(intel_crtc);
		return -EIO;
	}

	intel_wait_for_vblank(dev, intel_crtc->pipe);
	return 0;
}

int intel_dp_sink_crc(struct intel_dp *intel_dp, u8 *crc)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(dig_port->base.base.crtc);
	u8 buf;
	int count, ret;
	int attempts = 6;

	ret = intel_dp_sink_crc_start(intel_dp);
	if (ret)
		return ret;

	do {
		intel_wait_for_vblank(dev, intel_crtc->pipe);

		if (drm_dp_dpcd_readb(&intel_dp->aux,
				      DP_TEST_SINK_MISC, &buf) < 0) {
			ret = -EIO;
			goto stop;
		}
		count = buf & DP_TEST_COUNT_MASK;

	} while (--attempts && count == 0);

	if (attempts == 0) {
		DRM_ERROR("Panel is unable to calculate any CRC after 6 vblanks\n");
		ret = -ETIMEDOUT;
		goto stop;
	}

	if (drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_CRC_R_CR, crc, 6) < 0) {
		ret = -EIO;
		goto stop;
	}

stop:
	intel_dp_sink_crc_stop(intel_dp);
	return ret;
}

static bool
intel_dp_get_sink_irq(struct intel_dp *intel_dp, u8 *sink_irq_vector)
{
	return intel_dp_dpcd_read_wake(&intel_dp->aux,
				       DP_DEVICE_SERVICE_IRQ_VECTOR,
				       sink_irq_vector, 1) == 1;
}

static bool
intel_dp_get_sink_irq_esi(struct intel_dp *intel_dp, u8 *sink_irq_vector)
{
	int ret;

	ret = intel_dp_dpcd_read_wake(&intel_dp->aux,
					     DP_SINK_COUNT_ESI,
					     sink_irq_vector, 14);
	if (ret != 14)
		return false;

	return true;
}

static uint8_t intel_dp_autotest_link_training(struct intel_dp *intel_dp)
{
	uint8_t test_result = DP_TEST_ACK;
	return test_result;
}

static uint8_t intel_dp_autotest_video_pattern(struct intel_dp *intel_dp)
{
	uint8_t test_result = DP_TEST_NAK;
	return test_result;
}

static uint8_t intel_dp_autotest_edid(struct intel_dp *intel_dp)
{
	uint8_t test_result = DP_TEST_NAK;
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
		intel_dp->compliance_test_data = INTEL_DP_RESOLUTION_FAILSAFE;
	} else {
		struct edid *block = intel_connector->detect_edid;

		/* We have to write the checksum
		 * of the last block read
		 */
		block += intel_connector->detect_edid->extensions;

		if (!drm_dp_dpcd_write(&intel_dp->aux,
					DP_TEST_EDID_CHECKSUM,
					&block->checksum,
					1))
			DRM_DEBUG_KMS("Failed to write EDID checksum\n");

		test_result = DP_TEST_ACK | DP_TEST_EDID_CHECKSUM_WRITE;
		intel_dp->compliance_test_data = INTEL_DP_RESOLUTION_STANDARD;
	}

	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance_test_active = 1;

	return test_result;
}

static uint8_t intel_dp_autotest_phy_pattern(struct intel_dp *intel_dp)
{
	uint8_t test_result = DP_TEST_NAK;
	return test_result;
}

static void intel_dp_handle_test_request(struct intel_dp *intel_dp)
{
	uint8_t response = DP_TEST_NAK;
	uint8_t rxdata = 0;
	int status = 0;

	status = drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_REQUEST, &rxdata, 1);
	if (status <= 0) {
		DRM_DEBUG_KMS("Could not read test request from sink\n");
		goto update_status;
	}

	switch (rxdata) {
	case DP_TEST_LINK_TRAINING:
		DRM_DEBUG_KMS("LINK_TRAINING test requested\n");
		intel_dp->compliance_test_type = DP_TEST_LINK_TRAINING;
		response = intel_dp_autotest_link_training(intel_dp);
		break;
	case DP_TEST_LINK_VIDEO_PATTERN:
		DRM_DEBUG_KMS("TEST_PATTERN test requested\n");
		intel_dp->compliance_test_type = DP_TEST_LINK_VIDEO_PATTERN;
		response = intel_dp_autotest_video_pattern(intel_dp);
		break;
	case DP_TEST_LINK_EDID_READ:
		DRM_DEBUG_KMS("EDID test requested\n");
		intel_dp->compliance_test_type = DP_TEST_LINK_EDID_READ;
		response = intel_dp_autotest_edid(intel_dp);
		break;
	case DP_TEST_LINK_PHY_TEST_PATTERN:
		DRM_DEBUG_KMS("PHY_PATTERN test requested\n");
		intel_dp->compliance_test_type = DP_TEST_LINK_PHY_TEST_PATTERN;
		response = intel_dp_autotest_phy_pattern(intel_dp);
		break;
	default:
		DRM_DEBUG_KMS("Invalid test request '%02x'\n", rxdata);
		break;
	}

update_status:
	status = drm_dp_dpcd_write(&intel_dp->aux,
				   DP_TEST_RESPONSE,
				   &response, 1);
	if (status <= 0)
		DRM_DEBUG_KMS("Could not write test response to sink\n");
}

static int
intel_dp_check_mst_status(struct intel_dp *intel_dp)
{
	bool bret;

	if (intel_dp->is_mst) {
		u8 esi[16] = { 0 };
		int ret = 0;
		int retry;
		bool handled;
		bret = intel_dp_get_sink_irq_esi(intel_dp, esi);
go_again:
		if (bret == true) {

			/* check link status - esi[10] = 0x200c */
			if (intel_dp->active_mst_links &&
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
			struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
			DRM_DEBUG_KMS("failed to get ESI - device may have failed\n");
			intel_dp->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr, intel_dp->is_mst);
			/* send a hotplug event */
			drm_kms_helper_hotplug_event(intel_dig_port->base.base.dev);
		}
	}
	return -EINVAL;
}

/*
 * According to DP spec
 * 5.1.2:
 *  1. Read DPCD
 *  2. Configure link according to Receiver Capabilities
 *  3. Use Link Training from 2.5.3.3 and 3.5.1.3
 *  4. Check link status on receipt of hot-plug interrupt
 */
static void
intel_dp_check_link_status(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct intel_encoder *intel_encoder = &dp_to_dig_port(intel_dp)->base;
	u8 sink_irq_vector;
	u8 link_status[DP_LINK_STATUS_SIZE];

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));

	/*
	 * Clearing compliance test variables to allow capturing
	 * of values for next automated test request.
	 */
	intel_dp->compliance_test_active = 0;
	intel_dp->compliance_test_type = 0;
	intel_dp->compliance_test_data = 0;

	if (!intel_encoder->base.crtc)
		return;

	if (!to_intel_crtc(intel_encoder->base.crtc)->active)
		return;

	/* Try to read receiver status if the link appears to be up */
	if (!intel_dp_get_link_status(intel_dp, link_status)) {
		return;
	}

	/* Now read the DPCD to see if it's actually running */
	if (!intel_dp_get_dpcd(intel_dp)) {
		return;
	}

	/* Try to read the source of the interrupt */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
	    intel_dp_get_sink_irq(intel_dp, &sink_irq_vector)) {
		/* Clear interrupt source */
		drm_dp_dpcd_writeb(&intel_dp->aux,
				   DP_DEVICE_SERVICE_IRQ_VECTOR,
				   sink_irq_vector);

		if (sink_irq_vector & DP_AUTOMATED_TEST_REQUEST)
			DRM_DEBUG_DRIVER("Test request in short pulse not handled\n");
		if (sink_irq_vector & (DP_CP_IRQ | DP_SINK_SPECIFIC_IRQ))
			DRM_DEBUG_DRIVER("CP or sink specific irq unhandled\n");
	}

	/* if link training is requested we should perform it always */
	if ((intel_dp->compliance_test_type == DP_TEST_LINK_TRAINING) ||
		(!drm_dp_channel_eq_ok(link_status, intel_dp->lane_count))) {
		DRM_DEBUG_KMS("%s: channel EQ not ok, retraining\n",
			      intel_encoder->base.name);
		intel_dp_start_link_train(intel_dp);
		intel_dp_stop_link_train(intel_dp);
	}
}

/* XXX this is probably wrong for multiple downstream ports */
static enum drm_connector_status
intel_dp_detect_dpcd(struct intel_dp *intel_dp)
{
	uint8_t *dpcd = intel_dp->dpcd;
	uint8_t type;

	if (!intel_dp_get_dpcd(intel_dp))
		return connector_status_disconnected;

	/* if there's no downstream port, we're done */
	if (!(dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_PRESENT))
		return connector_status_connected;

	/* If we're HPD-aware, SINK_COUNT changes dynamically */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
	    intel_dp->downstream_ports[0] & DP_DS_PORT_HPD) {
		uint8_t reg;

		if (intel_dp_dpcd_read_wake(&intel_dp->aux, DP_SINK_COUNT,
					    &reg, 1) < 0)
			return connector_status_unknown;

		return DP_GET_SINK_COUNT(reg) ? connector_status_connected
					      : connector_status_disconnected;
	}

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
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	enum drm_connector_status status;

	status = intel_panel_detect(dev);
	if (status == connector_status_unknown)
		status = connector_status_connected;

	return status;
}

static bool ibx_digital_port_connected(struct drm_i915_private *dev_priv,
				       struct intel_digital_port *port)
{
	u32 bit;

	switch (port->port) {
	case PORT_A:
		return true;
	case PORT_B:
		bit = SDE_PORTB_HOTPLUG;
		break;
	case PORT_C:
		bit = SDE_PORTC_HOTPLUG;
		break;
	case PORT_D:
		bit = SDE_PORTD_HOTPLUG;
		break;
	default:
		MISSING_CASE(port->port);
		return false;
	}

	return I915_READ(SDEISR) & bit;
}

static bool cpt_digital_port_connected(struct drm_i915_private *dev_priv,
				       struct intel_digital_port *port)
{
	u32 bit;

	switch (port->port) {
	case PORT_A:
		return true;
	case PORT_B:
		bit = SDE_PORTB_HOTPLUG_CPT;
		break;
	case PORT_C:
		bit = SDE_PORTC_HOTPLUG_CPT;
		break;
	case PORT_D:
		bit = SDE_PORTD_HOTPLUG_CPT;
		break;
	case PORT_E:
		bit = SDE_PORTE_HOTPLUG_SPT;
		break;
	default:
		MISSING_CASE(port->port);
		return false;
	}

	return I915_READ(SDEISR) & bit;
}

static bool g4x_digital_port_connected(struct drm_i915_private *dev_priv,
				       struct intel_digital_port *port)
{
	u32 bit;

	switch (port->port) {
	case PORT_B:
		bit = PORTB_HOTPLUG_LIVE_STATUS_G4X;
		break;
	case PORT_C:
		bit = PORTC_HOTPLUG_LIVE_STATUS_G4X;
		break;
	case PORT_D:
		bit = PORTD_HOTPLUG_LIVE_STATUS_G4X;
		break;
	default:
		MISSING_CASE(port->port);
		return false;
	}

	return I915_READ(PORT_HOTPLUG_STAT) & bit;
}

static bool vlv_digital_port_connected(struct drm_i915_private *dev_priv,
				       struct intel_digital_port *port)
{
	u32 bit;

	switch (port->port) {
	case PORT_B:
		bit = PORTB_HOTPLUG_LIVE_STATUS_VLV;
		break;
	case PORT_C:
		bit = PORTC_HOTPLUG_LIVE_STATUS_VLV;
		break;
	case PORT_D:
		bit = PORTD_HOTPLUG_LIVE_STATUS_VLV;
		break;
	default:
		MISSING_CASE(port->port);
		return false;
	}

	return I915_READ(PORT_HOTPLUG_STAT) & bit;
}

static bool bxt_digital_port_connected(struct drm_i915_private *dev_priv,
				       struct intel_digital_port *intel_dig_port)
{
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	enum port port;
	u32 bit;

	intel_hpd_pin_to_port(intel_encoder->hpd_pin, &port);
	switch (port) {
	case PORT_A:
		bit = BXT_DE_PORT_HP_DDIA;
		break;
	case PORT_B:
		bit = BXT_DE_PORT_HP_DDIB;
		break;
	case PORT_C:
		bit = BXT_DE_PORT_HP_DDIC;
		break;
	default:
		MISSING_CASE(port);
		return false;
	}

	return I915_READ(GEN8_DE_PORT_ISR) & bit;
}

/*
 * intel_digital_port_connected - is the specified port connected?
 * @dev_priv: i915 private structure
 * @port: the port to test
 *
 * Return %true if @port is connected, %false otherwise.
 */
bool intel_digital_port_connected(struct drm_i915_private *dev_priv,
					 struct intel_digital_port *port)
{
	if (HAS_PCH_IBX(dev_priv))
		return ibx_digital_port_connected(dev_priv, port);
	if (HAS_PCH_SPLIT(dev_priv))
		return cpt_digital_port_connected(dev_priv, port);
	else if (IS_BROXTON(dev_priv))
		return bxt_digital_port_connected(dev_priv, port);
	else if (IS_VALLEYVIEW(dev_priv))
		return vlv_digital_port_connected(dev_priv, port);
	else
		return g4x_digital_port_connected(dev_priv, port);
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

	edid = intel_dp_get_edid(intel_dp);
	intel_connector->detect_edid = edid;

	if (intel_dp->force_audio != HDMI_AUDIO_AUTO)
		intel_dp->has_audio = intel_dp->force_audio == HDMI_AUDIO_ON;
	else
		intel_dp->has_audio = drm_detect_monitor_audio(edid);
}

static void
intel_dp_unset_edid(struct intel_dp *intel_dp)
{
	struct intel_connector *intel_connector = intel_dp->attached_connector;

	kfree(intel_connector->detect_edid);
	intel_connector->detect_edid = NULL;

	intel_dp->has_audio = false;
}

static enum drm_connector_status
intel_dp_detect(struct drm_connector *connector, bool force)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = connector->dev;
	enum drm_connector_status status;
	enum intel_display_power_domain power_domain;
	bool ret;
	u8 sink_irq_vector;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);
	intel_dp_unset_edid(intel_dp);

	if (intel_dp->is_mst) {
		/* MST devices are disconnected from a monitor POV */
		if (intel_encoder->type != INTEL_OUTPUT_EDP)
			intel_encoder->type = INTEL_OUTPUT_DISPLAYPORT;
		return connector_status_disconnected;
	}

	power_domain = intel_display_port_aux_power_domain(intel_encoder);
	intel_display_power_get(to_i915(dev), power_domain);

	/* Can't disconnect eDP, but you can close the lid... */
	if (is_edp(intel_dp))
		status = edp_detect(intel_dp);
	else if (intel_digital_port_connected(to_i915(dev),
					      dp_to_dig_port(intel_dp)))
		status = intel_dp_detect_dpcd(intel_dp);
	else
		status = connector_status_disconnected;

	if (status != connector_status_connected) {
		intel_dp->compliance_test_active = 0;
		intel_dp->compliance_test_type = 0;
		intel_dp->compliance_test_data = 0;

		goto out;
	}

	intel_dp_probe_oui(intel_dp);

	ret = intel_dp_probe_mst(intel_dp);
	if (ret) {
		/* if we are in MST mode then this connector
		   won't appear connected or have anything with EDID on it */
		if (intel_encoder->type != INTEL_OUTPUT_EDP)
			intel_encoder->type = INTEL_OUTPUT_DISPLAYPORT;
		status = connector_status_disconnected;
		goto out;
	}

	/*
	 * Clearing NACK and defer counts to get their exact values
	 * while reading EDID which are required by Compliance tests
	 * 4.2.2.4 and 4.2.2.5
	 */
	intel_dp->aux.i2c_nack_count = 0;
	intel_dp->aux.i2c_defer_count = 0;

	intel_dp_set_edid(intel_dp);

	if (intel_encoder->type != INTEL_OUTPUT_EDP)
		intel_encoder->type = INTEL_OUTPUT_DISPLAYPORT;
	status = connector_status_connected;

	/* Try to read the source of the interrupt */
	if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11 &&
	    intel_dp_get_sink_irq(intel_dp, &sink_irq_vector)) {
		/* Clear interrupt source */
		drm_dp_dpcd_writeb(&intel_dp->aux,
				   DP_DEVICE_SERVICE_IRQ_VECTOR,
				   sink_irq_vector);

		if (sink_irq_vector & DP_AUTOMATED_TEST_REQUEST)
			intel_dp_handle_test_request(intel_dp);
		if (sink_irq_vector & (DP_CP_IRQ | DP_SINK_SPECIFIC_IRQ))
			DRM_DEBUG_DRIVER("CP or sink specific irq unhandled\n");
	}

out:
	intel_display_power_put(to_i915(dev), power_domain);
	return status;
}

static void
intel_dp_force(struct drm_connector *connector)
{
	struct intel_dp *intel_dp = intel_attached_dp(connector);
	struct intel_encoder *intel_encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(intel_encoder->base.dev);
	enum intel_display_power_domain power_domain;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);
	intel_dp_unset_edid(intel_dp);

	if (connector->status != connector_status_connected)
		return;

	power_domain = intel_display_port_aux_power_domain(intel_encoder);
	intel_display_power_get(dev_priv, power_domain);

	intel_dp_set_edid(intel_dp);

	intel_display_power_put(dev_priv, power_domain);

	if (intel_encoder->type != INTEL_OUTPUT_EDP)
		intel_encoder->type = INTEL_OUTPUT_DISPLAYPORT;
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
	if (is_edp(intel_attached_dp(connector)) &&
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

static bool
intel_dp_detect_audio(struct drm_connector *connector)
{
	bool has_audio = false;
	struct edid *edid;

	edid = to_intel_connector(connector)->detect_edid;
	if (edid)
		has_audio = drm_detect_monitor_audio(edid);

	return has_audio;
}

static int
intel_dp_set_property(struct drm_connector *connector,
		      struct drm_property *property,
		      uint64_t val)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_encoder *intel_encoder = intel_attached_encoder(connector);
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);
	int ret;

	ret = drm_object_property_set_value(&connector->base, property, val);
	if (ret)
		return ret;

	if (property == dev_priv->force_audio_property) {
		int i = val;
		bool has_audio;

		if (i == intel_dp->force_audio)
			return 0;

		intel_dp->force_audio = i;

		if (i == HDMI_AUDIO_AUTO)
			has_audio = intel_dp_detect_audio(connector);
		else
			has_audio = (i == HDMI_AUDIO_ON);

		if (has_audio == intel_dp->has_audio)
			return 0;

		intel_dp->has_audio = has_audio;
		goto done;
	}

	if (property == dev_priv->broadcast_rgb_property) {
		bool old_auto = intel_dp->color_range_auto;
		bool old_range = intel_dp->limited_color_range;

		switch (val) {
		case INTEL_BROADCAST_RGB_AUTO:
			intel_dp->color_range_auto = true;
			break;
		case INTEL_BROADCAST_RGB_FULL:
			intel_dp->color_range_auto = false;
			intel_dp->limited_color_range = false;
			break;
		case INTEL_BROADCAST_RGB_LIMITED:
			intel_dp->color_range_auto = false;
			intel_dp->limited_color_range = true;
			break;
		default:
			return -EINVAL;
		}

		if (old_auto == intel_dp->color_range_auto &&
		    old_range == intel_dp->limited_color_range)
			return 0;

		goto done;
	}

	if (is_edp(intel_dp) &&
	    property == connector->dev->mode_config.scaling_mode_property) {
		if (val == DRM_MODE_SCALE_NONE) {
			DRM_DEBUG_KMS("no scaling not supported\n");
			return -EINVAL;
		}

		if (intel_connector->panel.fitting_mode == val) {
			/* the eDP scaling property is not changed */
			return 0;
		}
		intel_connector->panel.fitting_mode = val;

		goto done;
	}

	return -EINVAL;

done:
	if (intel_encoder->base.crtc)
		intel_crtc_restore_mode(intel_encoder->base.crtc);

	return 0;
}

static void
intel_dp_connector_destroy(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	kfree(intel_connector->detect_edid);

	if (!IS_ERR_OR_NULL(intel_connector->edid))
		kfree(intel_connector->edid);

	/* Can't call is_edp() since the encoder may have been destroyed
	 * already. */
	if (connector->connector_type == DRM_MODE_CONNECTOR_eDP)
		intel_panel_fini(&intel_connector->panel);

	drm_connector_cleanup(connector);
	kfree(connector);
}

void intel_dp_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &intel_dig_port->dp;

	intel_dp_aux_fini(intel_dp);
	intel_dp_mst_encoder_cleanup(intel_dig_port);
	if (is_edp(intel_dp)) {
		cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
		/*
		 * vdd might still be enabled do to the delayed vdd off.
		 * Make sure vdd is actually turned off here.
		 */
		pps_lock(intel_dp);
		edp_panel_vdd_off_sync(intel_dp);
		pps_unlock(intel_dp);

		if (intel_dp->edp_notifier.notifier_call) {
			unregister_reboot_notifier(&intel_dp->edp_notifier);
			intel_dp->edp_notifier.notifier_call = NULL;
		}
	}
	drm_encoder_cleanup(encoder);
	kfree(intel_dig_port);
}

static void intel_dp_encoder_suspend(struct intel_encoder *intel_encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);

	if (!is_edp(intel_dp))
		return;

	/*
	 * vdd might still be enabled do to the delayed vdd off.
	 * Make sure vdd is actually turned off here.
	 */
	cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
	pps_lock(intel_dp);
	edp_panel_vdd_off_sync(intel_dp);
	pps_unlock(intel_dp);
}

static void intel_edp_panel_vdd_sanitize(struct intel_dp *intel_dp)
{
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain power_domain;

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
	power_domain = intel_display_port_aux_power_domain(&intel_dig_port->base);
	intel_display_power_get(dev_priv, power_domain);

	edp_panel_vdd_schedule_off(intel_dp);
}

static void intel_dp_encoder_reset(struct drm_encoder *encoder)
{
	struct intel_dp *intel_dp;

	if (to_intel_encoder(encoder)->type != INTEL_OUTPUT_EDP)
		return;

	intel_dp = enc_to_intel_dp(encoder);

	pps_lock(intel_dp);

	/*
	 * Read out the current power sequencer assignment,
	 * in case the BIOS did something with it.
	 */
	if (IS_VALLEYVIEW(encoder->dev))
		vlv_initial_power_sequencer_setup(intel_dp);

	intel_edp_panel_vdd_sanitize(intel_dp);

	pps_unlock(intel_dp);
}

static const struct drm_connector_funcs intel_dp_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = intel_dp_detect,
	.force = intel_dp_force,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_dp_set_property,
	.atomic_get_property = intel_connector_atomic_get_property,
	.destroy = intel_dp_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs intel_dp_connector_helper_funcs = {
	.get_modes = intel_dp_get_modes,
	.mode_valid = intel_dp_mode_valid,
	.best_encoder = intel_best_encoder,
};

static const struct drm_encoder_funcs intel_dp_enc_funcs = {
	.reset = intel_dp_encoder_reset,
	.destroy = intel_dp_encoder_destroy,
};

enum irqreturn
intel_dp_hpd_pulse(struct intel_digital_port *intel_dig_port, bool long_hpd)
{
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum intel_display_power_domain power_domain;
	enum irqreturn ret = IRQ_NONE;

	if (intel_dig_port->base.type != INTEL_OUTPUT_EDP)
		intel_dig_port->base.type = INTEL_OUTPUT_DISPLAYPORT;

	if (long_hpd && intel_dig_port->base.type == INTEL_OUTPUT_EDP) {
		/*
		 * vdd off can generate a long pulse on eDP which
		 * would require vdd on to handle it, and thus we
		 * would end up in an endless cycle of
		 * "vdd off -> long hpd -> vdd on -> detect -> vdd off -> ..."
		 */
		DRM_DEBUG_KMS("ignoring long hpd on eDP port %c\n",
			      port_name(intel_dig_port->port));
		return IRQ_HANDLED;
	}

	DRM_DEBUG_KMS("got hpd irq on port %c - %s\n",
		      port_name(intel_dig_port->port),
		      long_hpd ? "long" : "short");

	power_domain = intel_display_port_aux_power_domain(intel_encoder);
	intel_display_power_get(dev_priv, power_domain);

	if (long_hpd) {
		/* indicate that we need to restart link training */
		intel_dp->train_set_valid = false;

		if (!intel_digital_port_connected(dev_priv, intel_dig_port))
			goto mst_fail;

		if (!intel_dp_get_dpcd(intel_dp)) {
			goto mst_fail;
		}

		intel_dp_probe_oui(intel_dp);

		if (!intel_dp_probe_mst(intel_dp)) {
			drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
			intel_dp_check_link_status(intel_dp);
			drm_modeset_unlock(&dev->mode_config.connection_mutex);
			goto mst_fail;
		}
	} else {
		if (intel_dp->is_mst) {
			if (intel_dp_check_mst_status(intel_dp) == -EINVAL)
				goto mst_fail;
		}

		if (!intel_dp->is_mst) {
			drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
			intel_dp_check_link_status(intel_dp);
			drm_modeset_unlock(&dev->mode_config.connection_mutex);
		}
	}

	ret = IRQ_HANDLED;

	goto put_power;
mst_fail:
	/* if we were in MST mode, and device is not there get out of MST mode */
	if (intel_dp->is_mst) {
		DRM_DEBUG_KMS("MST device may have disappeared %d vs %d\n", intel_dp->is_mst, intel_dp->mst_mgr.mst_state);
		intel_dp->is_mst = false;
		drm_dp_mst_topology_mgr_set_mst(&intel_dp->mst_mgr, intel_dp->is_mst);
	}
put_power:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

/* check the VBT to see whether the eDP is on another port */
bool intel_dp_is_edp(struct drm_device *dev, enum port port)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	union child_device_config *p_child;
	int i;
	static const short port_mapping[] = {
		[PORT_B] = DVO_PORT_DPB,
		[PORT_C] = DVO_PORT_DPC,
		[PORT_D] = DVO_PORT_DPD,
		[PORT_E] = DVO_PORT_DPE,
	};

	/*
	 * eDP not supported on g4x. so bail out early just
	 * for a bit extra safety in case the VBT is bonkers.
	 */
	if (INTEL_INFO(dev)->gen < 5)
		return false;

	if (port == PORT_A)
		return true;

	if (!dev_priv->vbt.child_dev_num)
		return false;

	for (i = 0; i < dev_priv->vbt.child_dev_num; i++) {
		p_child = dev_priv->vbt.child_dev + i;

		if (p_child->common.dvo_port == port_mapping[port] &&
		    (p_child->common.device_type & DEVICE_TYPE_eDP_BITS) ==
		    (DEVICE_TYPE_eDP & DEVICE_TYPE_eDP_BITS))
			return true;
	}
	return false;
}

void
intel_dp_add_properties(struct intel_dp *intel_dp, struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	intel_attach_force_audio_property(connector);
	intel_attach_broadcast_rgb_property(connector);
	intel_dp->color_range_auto = true;

	if (is_edp(intel_dp)) {
		drm_mode_create_scaling_mode_property(connector->dev);
		drm_object_attach_property(
			&connector->base,
			connector->dev->mode_config.scaling_mode_property,
			DRM_MODE_SCALE_ASPECT);
		intel_connector->panel.fitting_mode = DRM_MODE_SCALE_ASPECT;
	}
}

static void intel_dp_init_panel_power_timestamps(struct intel_dp *intel_dp)
{
	intel_dp->last_power_cycle = jiffies;
	intel_dp->last_power_on = jiffies;
	intel_dp->last_backlight_off = jiffies;
}

static void
intel_dp_init_panel_power_sequencer(struct drm_device *dev,
				    struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct edp_power_seq cur, vbt, spec,
		*final = &intel_dp->pps_delays;
	u32 pp_on, pp_off, pp_div = 0, pp_ctl = 0;
	i915_reg_t pp_ctrl_reg, pp_on_reg, pp_off_reg, pp_div_reg;

	lockdep_assert_held(&dev_priv->pps_mutex);

	/* already initialized? */
	if (final->t11_t12 != 0)
		return;

	if (IS_BROXTON(dev)) {
		/*
		 * TODO: BXT has 2 sets of PPS registers.
		 * Correct Register for Broxton need to be identified
		 * using VBT. hardcoding for now
		 */
		pp_ctrl_reg = BXT_PP_CONTROL(0);
		pp_on_reg = BXT_PP_ON_DELAYS(0);
		pp_off_reg = BXT_PP_OFF_DELAYS(0);
	} else if (HAS_PCH_SPLIT(dev)) {
		pp_ctrl_reg = PCH_PP_CONTROL;
		pp_on_reg = PCH_PP_ON_DELAYS;
		pp_off_reg = PCH_PP_OFF_DELAYS;
		pp_div_reg = PCH_PP_DIVISOR;
	} else {
		enum pipe pipe = vlv_power_sequencer_pipe(intel_dp);

		pp_ctrl_reg = VLV_PIPE_PP_CONTROL(pipe);
		pp_on_reg = VLV_PIPE_PP_ON_DELAYS(pipe);
		pp_off_reg = VLV_PIPE_PP_OFF_DELAYS(pipe);
		pp_div_reg = VLV_PIPE_PP_DIVISOR(pipe);
	}

	/* Workaround: Need to write PP_CONTROL with the unlock key as
	 * the very first thing. */
	pp_ctl = ironlake_get_pp_control(intel_dp);

	pp_on = I915_READ(pp_on_reg);
	pp_off = I915_READ(pp_off_reg);
	if (!IS_BROXTON(dev)) {
		I915_WRITE(pp_ctrl_reg, pp_ctl);
		pp_div = I915_READ(pp_div_reg);
	}

	/* Pull timing values out of registers */
	cur.t1_t3 = (pp_on & PANEL_POWER_UP_DELAY_MASK) >>
		PANEL_POWER_UP_DELAY_SHIFT;

	cur.t8 = (pp_on & PANEL_LIGHT_ON_DELAY_MASK) >>
		PANEL_LIGHT_ON_DELAY_SHIFT;

	cur.t9 = (pp_off & PANEL_LIGHT_OFF_DELAY_MASK) >>
		PANEL_LIGHT_OFF_DELAY_SHIFT;

	cur.t10 = (pp_off & PANEL_POWER_DOWN_DELAY_MASK) >>
		PANEL_POWER_DOWN_DELAY_SHIFT;

	if (IS_BROXTON(dev)) {
		u16 tmp = (pp_ctl & BXT_POWER_CYCLE_DELAY_MASK) >>
			BXT_POWER_CYCLE_DELAY_SHIFT;
		if (tmp > 0)
			cur.t11_t12 = (tmp - 1) * 1000;
		else
			cur.t11_t12 = 0;
	} else {
		cur.t11_t12 = ((pp_div & PANEL_POWER_CYCLE_DELAY_MASK) >>
		       PANEL_POWER_CYCLE_DELAY_SHIFT) * 1000;
	}

	DRM_DEBUG_KMS("cur t1_t3 %d t8 %d t9 %d t10 %d t11_t12 %d\n",
		      cur.t1_t3, cur.t8, cur.t9, cur.t10, cur.t11_t12);

	vbt = dev_priv->vbt.edp_pps;

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

	DRM_DEBUG_KMS("vbt t1_t3 %d t8 %d t9 %d t10 %d t11_t12 %d\n",
		      vbt.t1_t3, vbt.t8, vbt.t9, vbt.t10, vbt.t11_t12);

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
}

static void
intel_dp_init_panel_power_sequencer_registers(struct drm_device *dev,
					      struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 pp_on, pp_off, pp_div, port_sel = 0;
	int div = HAS_PCH_SPLIT(dev) ? intel_pch_rawclk(dev) : intel_hrawclk(dev);
	i915_reg_t pp_on_reg, pp_off_reg, pp_div_reg, pp_ctrl_reg;
	enum port port = dp_to_dig_port(intel_dp)->port;
	const struct edp_power_seq *seq = &intel_dp->pps_delays;

	lockdep_assert_held(&dev_priv->pps_mutex);

	if (IS_BROXTON(dev)) {
		/*
		 * TODO: BXT has 2 sets of PPS registers.
		 * Correct Register for Broxton need to be identified
		 * using VBT. hardcoding for now
		 */
		pp_ctrl_reg = BXT_PP_CONTROL(0);
		pp_on_reg = BXT_PP_ON_DELAYS(0);
		pp_off_reg = BXT_PP_OFF_DELAYS(0);

	} else if (HAS_PCH_SPLIT(dev)) {
		pp_on_reg = PCH_PP_ON_DELAYS;
		pp_off_reg = PCH_PP_OFF_DELAYS;
		pp_div_reg = PCH_PP_DIVISOR;
	} else {
		enum pipe pipe = vlv_power_sequencer_pipe(intel_dp);

		pp_on_reg = VLV_PIPE_PP_ON_DELAYS(pipe);
		pp_off_reg = VLV_PIPE_PP_OFF_DELAYS(pipe);
		pp_div_reg = VLV_PIPE_PP_DIVISOR(pipe);
	}

	/*
	 * And finally store the new values in the power sequencer. The
	 * backlight delays are set to 1 because we do manual waits on them. For
	 * T8, even BSpec recommends doing it. For T9, if we don't do this,
	 * we'll end up waiting for the backlight off delay twice: once when we
	 * do the manual sleep, and once when we disable the panel and wait for
	 * the PP_STATUS bit to become zero.
	 */
	pp_on = (seq->t1_t3 << PANEL_POWER_UP_DELAY_SHIFT) |
		(1 << PANEL_LIGHT_ON_DELAY_SHIFT);
	pp_off = (1 << PANEL_LIGHT_OFF_DELAY_SHIFT) |
		 (seq->t10 << PANEL_POWER_DOWN_DELAY_SHIFT);
	/* Compute the divisor for the pp clock, simply match the Bspec
	 * formula. */
	if (IS_BROXTON(dev)) {
		pp_div = I915_READ(pp_ctrl_reg);
		pp_div &= ~BXT_POWER_CYCLE_DELAY_MASK;
		pp_div |= (DIV_ROUND_UP((seq->t11_t12 + 1), 1000)
				<< BXT_POWER_CYCLE_DELAY_SHIFT);
	} else {
		pp_div = ((100 * div)/2 - 1) << PP_REFERENCE_DIVIDER_SHIFT;
		pp_div |= (DIV_ROUND_UP(seq->t11_t12, 1000)
				<< PANEL_POWER_CYCLE_DELAY_SHIFT);
	}

	/* Haswell doesn't have any port selection bits for the panel
	 * power sequencer any more. */
	if (IS_VALLEYVIEW(dev)) {
		port_sel = PANEL_PORT_SELECT_VLV(port);
	} else if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev)) {
		if (port == PORT_A)
			port_sel = PANEL_PORT_SELECT_DPA;
		else
			port_sel = PANEL_PORT_SELECT_DPD;
	}

	pp_on |= port_sel;

	I915_WRITE(pp_on_reg, pp_on);
	I915_WRITE(pp_off_reg, pp_off);
	if (IS_BROXTON(dev))
		I915_WRITE(pp_ctrl_reg, pp_div);
	else
		I915_WRITE(pp_div_reg, pp_div);

	DRM_DEBUG_KMS("panel power sequencer register settings: PP_ON %#x, PP_OFF %#x, PP_DIV %#x\n",
		      I915_READ(pp_on_reg),
		      I915_READ(pp_off_reg),
		      IS_BROXTON(dev) ?
		      (I915_READ(pp_ctrl_reg) & BXT_POWER_CYCLE_DELAY_MASK) :
		      I915_READ(pp_div_reg));
}

/**
 * intel_dp_set_drrs_state - program registers for RR switch to take effect
 * @dev: DRM device
 * @refresh_rate: RR to be programmed
 *
 * This function gets called when refresh rate (RR) has to be changed from
 * one frequency to another. Switches can be between high and low RR
 * supported by the panel or to any other RR based on media playback (in
 * this case, RR value needs to be passed from user space).
 *
 * The caller of this function needs to take a lock on dev_priv->drrs.
 */
static void intel_dp_set_drrs_state(struct drm_device *dev, int refresh_rate)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *encoder;
	struct intel_digital_port *dig_port = NULL;
	struct intel_dp *intel_dp = dev_priv->drrs.dp;
	struct intel_crtc_state *config = NULL;
	struct intel_crtc *intel_crtc = NULL;
	enum drrs_refresh_rate_type index = DRRS_HIGH_RR;

	if (refresh_rate <= 0) {
		DRM_DEBUG_KMS("Refresh rate should be positive non-zero.\n");
		return;
	}

	if (intel_dp == NULL) {
		DRM_DEBUG_KMS("DRRS not supported.\n");
		return;
	}

	/*
	 * FIXME: This needs proper synchronization with psr state for some
	 * platforms that cannot have PSR and DRRS enabled at the same time.
	 */

	dig_port = dp_to_dig_port(intel_dp);
	encoder = &dig_port->base;
	intel_crtc = to_intel_crtc(encoder->base.crtc);

	if (!intel_crtc) {
		DRM_DEBUG_KMS("DRRS: intel_crtc not initialized\n");
		return;
	}

	config = intel_crtc->config;

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

	if (!intel_crtc->active) {
		DRM_DEBUG_KMS("eDP encoder disabled. CRTC not Active\n");
		return;
	}

	if (INTEL_INFO(dev)->gen >= 8 && !IS_CHERRYVIEW(dev)) {
		switch (index) {
		case DRRS_HIGH_RR:
			intel_dp_set_m_n(intel_crtc, M1_N1);
			break;
		case DRRS_LOW_RR:
			intel_dp_set_m_n(intel_crtc, M2_N2);
			break;
		case DRRS_MAX_RR:
		default:
			DRM_ERROR("Unsupported refreshrate type\n");
		}
	} else if (INTEL_INFO(dev)->gen > 6) {
		i915_reg_t reg = PIPECONF(intel_crtc->config->cpu_transcoder);
		u32 val;

		val = I915_READ(reg);
		if (index > DRRS_HIGH_RR) {
			if (IS_VALLEYVIEW(dev))
				val |= PIPECONF_EDP_RR_MODE_SWITCH_VLV;
			else
				val |= PIPECONF_EDP_RR_MODE_SWITCH;
		} else {
			if (IS_VALLEYVIEW(dev))
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
 *
 * Initializes frontbuffer_bits and drrs.dp
 */
void intel_edp_drrs_enable(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_crtc *crtc = dig_port->base.base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	if (!intel_crtc->config->has_drrs) {
		DRM_DEBUG_KMS("Panel doesn't support DRRS\n");
		return;
	}

	mutex_lock(&dev_priv->drrs.mutex);
	if (WARN_ON(dev_priv->drrs.dp)) {
		DRM_ERROR("DRRS already enabled\n");
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
 *
 */
void intel_edp_drrs_disable(struct intel_dp *intel_dp)
{
	struct drm_device *dev = intel_dp_to_dev(intel_dp);
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_crtc *crtc = dig_port->base.base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	if (!intel_crtc->config->has_drrs)
		return;

	mutex_lock(&dev_priv->drrs.mutex);
	if (!dev_priv->drrs.dp) {
		mutex_unlock(&dev_priv->drrs.mutex);
		return;
	}

	if (dev_priv->drrs.refresh_rate_type == DRRS_LOW_RR)
		intel_dp_set_drrs_state(dev_priv->dev,
			intel_dp->attached_connector->panel.
			fixed_mode->vrefresh);

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

	if (dev_priv->drrs.refresh_rate_type != DRRS_LOW_RR)
		intel_dp_set_drrs_state(dev_priv->dev,
			intel_dp->attached_connector->panel.
			downclock_mode->vrefresh);

unlock:
	mutex_unlock(&dev_priv->drrs.mutex);
}

/**
 * intel_edp_drrs_invalidate - Disable Idleness DRRS
 * @dev: DRM device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called everytime rendering on the given planes start.
 * Hence DRRS needs to be Upclocked, i.e. (LOW_RR -> HIGH_RR).
 *
 * Dirty frontbuffers relevant to DRRS are tracked in busy_frontbuffer_bits.
 */
void intel_edp_drrs_invalidate(struct drm_device *dev,
		unsigned frontbuffer_bits)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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
		intel_dp_set_drrs_state(dev_priv->dev,
				dev_priv->drrs.dp->attached_connector->panel.
				fixed_mode->vrefresh);

	mutex_unlock(&dev_priv->drrs.mutex);
}

/**
 * intel_edp_drrs_flush - Restart Idleness DRRS
 * @dev: DRM device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called every time rendering on the given planes has
 * completed or flip on a crtc is completed. So DRRS should be upclocked
 * (LOW_RR -> HIGH_RR). And also Idleness detection should be started again,
 * if no other planes are dirty.
 *
 * Dirty frontbuffers relevant to DRRS are tracked in busy_frontbuffer_bits.
 */
void intel_edp_drrs_flush(struct drm_device *dev,
		unsigned frontbuffer_bits)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
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
		intel_dp_set_drrs_state(dev_priv->dev,
				dev_priv->drrs.dp->attached_connector->panel.
				fixed_mode->vrefresh);

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
 * eDP DRRS:-
 *        The implementation is based on frontbuffer tracking implementation.
 * When there is a disturbance on the screen triggered by user activity or a
 * periodic system activity, DRRS is disabled (RR is changed to high RR).
 * When there is no movement on screen, after a timeout of 1 second, a switch
 * to low RR is made.
 *        For integration with frontbuffer tracking code,
 * intel_edp_drrs_invalidate() and intel_edp_drrs_flush() are called.
 *
 * DRRS can be further extended to support other internal panels and also
 * the scenario of video playback wherein RR is set based on the rate
 * requested by userspace.
 */

/**
 * intel_dp_drrs_init - Init basic DRRS work and mutex.
 * @intel_connector: eDP connector
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
intel_dp_drrs_init(struct intel_connector *intel_connector,
		struct drm_display_mode *fixed_mode)
{
	struct drm_connector *connector = &intel_connector->base;
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *downclock_mode = NULL;

	INIT_DELAYED_WORK(&dev_priv->drrs.work, intel_edp_drrs_downclock_work);
	mutex_init(&dev_priv->drrs.mutex);

	if (INTEL_INFO(dev)->gen <= 6) {
		DRM_DEBUG_KMS("DRRS supported for Gen7 and above\n");
		return NULL;
	}

	if (dev_priv->vbt.drrs_type != SEAMLESS_DRRS_SUPPORT) {
		DRM_DEBUG_KMS("VBT doesn't support DRRS\n");
		return NULL;
	}

	downclock_mode = intel_find_panel_downclock
					(dev, fixed_mode, connector);

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
	struct drm_connector *connector = &intel_connector->base;
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_display_mode *fixed_mode = NULL;
	struct drm_display_mode *downclock_mode = NULL;
	bool has_dpcd;
	struct drm_display_mode *scan;
	struct edid *edid;
	enum pipe pipe = INVALID_PIPE;

	if (!is_edp(intel_dp))
		return true;

	pps_lock(intel_dp);
	intel_edp_panel_vdd_sanitize(intel_dp);
	pps_unlock(intel_dp);

	/* Cache DPCD and EDID for edp. */
	has_dpcd = intel_dp_get_dpcd(intel_dp);

	if (has_dpcd) {
		if (intel_dp->dpcd[DP_DPCD_REV] >= 0x11)
			dev_priv->no_aux_handshake =
				intel_dp->dpcd[DP_MAX_DOWNSPREAD] &
				DP_NO_AUX_HANDSHAKE_LINK_TRAINING;
	} else {
		/* if this fails, presume the device is a ghost */
		DRM_INFO("failed to retrieve link info, disabling eDP\n");
		return false;
	}

	/* We now know it's not a ghost, init power sequence regs. */
	pps_lock(intel_dp);
	intel_dp_init_panel_power_sequencer_registers(dev, intel_dp);
	pps_unlock(intel_dp);

	mutex_lock(&dev->mode_config.mutex);
	edid = drm_get_edid(connector, &intel_dp->aux.ddc);
	if (edid) {
		if (drm_add_edid_modes(connector, edid)) {
			drm_mode_connector_update_edid_property(connector,
								edid);
			drm_edid_to_eld(connector, edid);
		} else {
			kfree(edid);
			edid = ERR_PTR(-EINVAL);
		}
	} else {
		edid = ERR_PTR(-ENOENT);
	}
	intel_connector->edid = edid;

	/* prefer fixed mode from EDID if available */
	list_for_each_entry(scan, &connector->probed_modes, head) {
		if ((scan->type & DRM_MODE_TYPE_PREFERRED)) {
			fixed_mode = drm_mode_duplicate(dev, scan);
			downclock_mode = intel_dp_drrs_init(
						intel_connector, fixed_mode);
			break;
		}
	}

	/* fallback to VBT if available for eDP */
	if (!fixed_mode && dev_priv->vbt.lfp_lvds_vbt_mode) {
		fixed_mode = drm_mode_duplicate(dev,
					dev_priv->vbt.lfp_lvds_vbt_mode);
		if (fixed_mode)
			fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;
	}
	mutex_unlock(&dev->mode_config.mutex);

	if (IS_VALLEYVIEW(dev)) {
		intel_dp->edp_notifier.notifier_call = edp_notify_handler;
		register_reboot_notifier(&intel_dp->edp_notifier);

		/*
		 * Figure out the current pipe for the initial backlight setup.
		 * If the current pipe isn't valid, try the PPS pipe, and if that
		 * fails just assume pipe A.
		 */
		if (IS_CHERRYVIEW(dev))
			pipe = DP_PORT_TO_PIPE_CHV(intel_dp->DP);
		else
			pipe = PORT_TO_PIPE(intel_dp->DP);

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

	return true;
}

bool
intel_dp_init_connector(struct intel_digital_port *intel_dig_port,
			struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct intel_encoder *intel_encoder = &intel_dig_port->base;
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	int type, ret;

	intel_dp->pps_pipe = INVALID_PIPE;

	/* intel_dp vfuncs */
	if (INTEL_INFO(dev)->gen >= 9)
		intel_dp->get_aux_clock_divider = skl_get_aux_clock_divider;
	else if (IS_VALLEYVIEW(dev))
		intel_dp->get_aux_clock_divider = vlv_get_aux_clock_divider;
	else if (IS_HASWELL(dev) || IS_BROADWELL(dev))
		intel_dp->get_aux_clock_divider = hsw_get_aux_clock_divider;
	else if (HAS_PCH_SPLIT(dev))
		intel_dp->get_aux_clock_divider = ilk_get_aux_clock_divider;
	else
		intel_dp->get_aux_clock_divider = i9xx_get_aux_clock_divider;

	if (INTEL_INFO(dev)->gen >= 9)
		intel_dp->get_aux_send_ctl = skl_get_aux_send_ctl;
	else
		intel_dp->get_aux_send_ctl = i9xx_get_aux_send_ctl;

	if (HAS_DDI(dev))
		intel_dp->prepare_link_retrain = intel_ddi_prepare_link_retrain;

	/* Preserve the current hw state. */
	intel_dp->DP = I915_READ(intel_dp->output_reg);
	intel_dp->attached_connector = intel_connector;

	if (intel_dp_is_edp(dev, port))
		type = DRM_MODE_CONNECTOR_eDP;
	else
		type = DRM_MODE_CONNECTOR_DisplayPort;

	/*
	 * For eDP we always set the encoder type to INTEL_OUTPUT_EDP, but
	 * for DP the encoder type can be set by the caller to
	 * INTEL_OUTPUT_UNKNOWN for DDI, so don't rewrite it.
	 */
	if (type == DRM_MODE_CONNECTOR_eDP)
		intel_encoder->type = INTEL_OUTPUT_EDP;

	/* eDP only on port B and/or C on vlv/chv */
	if (WARN_ON(IS_VALLEYVIEW(dev) && is_edp(intel_dp) &&
		    port != PORT_B && port != PORT_C))
		return false;

	DRM_DEBUG_KMS("Adding %s connector on port %c\n",
			type == DRM_MODE_CONNECTOR_eDP ? "eDP" : "DP",
			port_name(port));

	drm_connector_init(dev, connector, &intel_dp_connector_funcs, type);
	drm_connector_helper_add(connector, &intel_dp_connector_helper_funcs);

	connector->interlace_allowed = true;
	connector->doublescan_allowed = 0;

	INIT_DELAYED_WORK(&intel_dp->panel_vdd_work,
			  edp_panel_vdd_work);

	intel_connector_attach_encoder(intel_connector, intel_encoder);
	drm_connector_register(connector);

	if (HAS_DDI(dev))
		intel_connector->get_hw_state = intel_ddi_connector_get_hw_state;
	else
		intel_connector->get_hw_state = intel_connector_get_hw_state;
	intel_connector->unregister = intel_dp_connector_unregister;

	/* Set up the hotplug pin. */
	switch (port) {
	case PORT_A:
		intel_encoder->hpd_pin = HPD_PORT_A;
		break;
	case PORT_B:
		intel_encoder->hpd_pin = HPD_PORT_B;
		if (IS_BXT_REVID(dev, 0, BXT_REVID_A1))
			intel_encoder->hpd_pin = HPD_PORT_A;
		break;
	case PORT_C:
		intel_encoder->hpd_pin = HPD_PORT_C;
		break;
	case PORT_D:
		intel_encoder->hpd_pin = HPD_PORT_D;
		break;
	case PORT_E:
		intel_encoder->hpd_pin = HPD_PORT_E;
		break;
	default:
		BUG();
	}

	if (is_edp(intel_dp)) {
		pps_lock(intel_dp);
		intel_dp_init_panel_power_timestamps(intel_dp);
		if (IS_VALLEYVIEW(dev))
			vlv_initial_power_sequencer_setup(intel_dp);
		else
			intel_dp_init_panel_power_sequencer(dev, intel_dp);
		pps_unlock(intel_dp);
	}

	ret = intel_dp_aux_init(intel_dp, intel_connector);
	if (ret)
		goto fail;

	/* init MST on ports that can support it */
	if (HAS_DP_MST(dev) &&
	    (port == PORT_B || port == PORT_C || port == PORT_D))
		intel_dp_mst_encoder_init(intel_dig_port,
					  intel_connector->base.base.id);

	if (!intel_edp_init_connector(intel_dp, intel_connector)) {
		intel_dp_aux_fini(intel_dp);
		intel_dp_mst_encoder_cleanup(intel_dig_port);
		goto fail;
	}

	intel_dp_add_properties(intel_dp, connector);

	/* For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	if (IS_G4X(dev) && !IS_GM45(dev)) {
		u32 temp = I915_READ(PEG_BAND_GAP_DATA);
		I915_WRITE(PEG_BAND_GAP_DATA, (temp & ~0xf) | 0xd);
	}

	i915_debugfs_connector_add(connector);

	return true;

fail:
	if (is_edp(intel_dp)) {
		cancel_delayed_work_sync(&intel_dp->panel_vdd_work);
		/*
		 * vdd might still be enabled do to the delayed vdd off.
		 * Make sure vdd is actually turned off here.
		 */
		pps_lock(intel_dp);
		edp_panel_vdd_off_sync(intel_dp);
		pps_unlock(intel_dp);
	}
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	return false;
}

void
intel_dp_init(struct drm_device *dev,
	      i915_reg_t output_reg, enum port port)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *intel_dig_port;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	struct intel_connector *intel_connector;

	intel_dig_port = kzalloc(sizeof(*intel_dig_port), GFP_KERNEL);
	if (!intel_dig_port)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector)
		goto err_connector_alloc;

	intel_encoder = &intel_dig_port->base;
	encoder = &intel_encoder->base;

	drm_encoder_init(dev, &intel_encoder->base, &intel_dp_enc_funcs,
			 DRM_MODE_ENCODER_TMDS);

	intel_encoder->compute_config = intel_dp_compute_config;
	intel_encoder->disable = intel_disable_dp;
	intel_encoder->get_hw_state = intel_dp_get_hw_state;
	intel_encoder->get_config = intel_dp_get_config;
	intel_encoder->suspend = intel_dp_encoder_suspend;
	if (IS_CHERRYVIEW(dev)) {
		intel_encoder->pre_pll_enable = chv_dp_pre_pll_enable;
		intel_encoder->pre_enable = chv_pre_enable_dp;
		intel_encoder->enable = vlv_enable_dp;
		intel_encoder->post_disable = chv_post_disable_dp;
		intel_encoder->post_pll_disable = chv_dp_post_pll_disable;
	} else if (IS_VALLEYVIEW(dev)) {
		intel_encoder->pre_pll_enable = vlv_dp_pre_pll_enable;
		intel_encoder->pre_enable = vlv_pre_enable_dp;
		intel_encoder->enable = vlv_enable_dp;
		intel_encoder->post_disable = vlv_post_disable_dp;
	} else {
		intel_encoder->pre_enable = g4x_pre_enable_dp;
		intel_encoder->enable = g4x_enable_dp;
		if (INTEL_INFO(dev)->gen >= 5)
			intel_encoder->post_disable = ilk_post_disable_dp;
	}

	intel_dig_port->port = port;
	intel_dig_port->dp.output_reg = output_reg;

	intel_encoder->type = INTEL_OUTPUT_DISPLAYPORT;
	if (IS_CHERRYVIEW(dev)) {
		if (port == PORT_D)
			intel_encoder->crtc_mask = 1 << 2;
		else
			intel_encoder->crtc_mask = (1 << 0) | (1 << 1);
	} else {
		intel_encoder->crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);
	}
	intel_encoder->cloneable = 0;

	intel_dig_port->hpd_pulse = intel_dp_hpd_pulse;
	dev_priv->hotplug.irq_port[port] = intel_dig_port;

	if (!intel_dp_init_connector(intel_dig_port, intel_connector))
		goto err_init_connector;

	return;

err_init_connector:
	drm_encoder_cleanup(encoder);
	kfree(intel_connector);
err_connector_alloc:
	kfree(intel_dig_port);

	return;
}

void intel_dp_mst_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	/* disable MST */
	for (i = 0; i < I915_MAX_PORTS; i++) {
		struct intel_digital_port *intel_dig_port = dev_priv->hotplug.irq_port[i];
		if (!intel_dig_port)
			continue;

		if (intel_dig_port->base.type == INTEL_OUTPUT_DISPLAYPORT) {
			if (!intel_dig_port->dp.can_mst)
				continue;
			if (intel_dig_port->dp.is_mst)
				drm_dp_mst_topology_mgr_suspend(&intel_dig_port->dp.mst_mgr);
		}
	}
}

void intel_dp_mst_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	for (i = 0; i < I915_MAX_PORTS; i++) {
		struct intel_digital_port *intel_dig_port = dev_priv->hotplug.irq_port[i];
		if (!intel_dig_port)
			continue;
		if (intel_dig_port->base.type == INTEL_OUTPUT_DISPLAYPORT) {
			int ret;

			if (!intel_dig_port->dp.can_mst)
				continue;

			ret = drm_dp_mst_topology_mgr_resume(&intel_dig_port->dp.mst_mgr);
			if (ret != 0) {
				intel_dp_check_mst_status(&intel_dig_port->dp);
			}
		}
	}
}
