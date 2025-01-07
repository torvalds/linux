/*
 * Copyright © 2014-2016 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "bxt_dpio_phy_regs.h"
#include "i915_reg.h"
#include "intel_ddi.h"
#include "intel_ddi_buf_trans.h"
#include "intel_de.h"
#include "intel_display_power_well.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dpio_phy.h"
#include "vlv_dpio_phy_regs.h"
#include "vlv_sideband.h"

/**
 * DOC: DPIO
 *
 * VLV, CHV and BXT have slightly peculiar display PHYs for driving DP/HDMI
 * ports. DPIO is the name given to such a display PHY. These PHYs
 * don't follow the standard programming model using direct MMIO
 * registers, and instead their registers must be accessed trough IOSF
 * sideband. VLV has one such PHY for driving ports B and C, and CHV
 * adds another PHY for driving port D. Each PHY responds to specific
 * IOSF-SB port.
 *
 * Each display PHY is made up of one or two channels. Each channel
 * houses a common lane part which contains the PLL and other common
 * logic. CH0 common lane also contains the IOSF-SB logic for the
 * Common Register Interface (CRI) ie. the DPIO registers. CRI clock
 * must be running when any DPIO registers are accessed.
 *
 * In addition to having their own registers, the PHYs are also
 * controlled through some dedicated signals from the display
 * controller. These include PLL reference clock enable, PLL enable,
 * and CRI clock selection, for example.
 *
 * Eeach channel also has two splines (also called data lanes), and
 * each spline is made up of one Physical Access Coding Sub-Layer
 * (PCS) block and two TX lanes. So each channel has two PCS blocks
 * and four TX lanes. The TX lanes are used as DP lanes or TMDS
 * data/clock pairs depending on the output type.
 *
 * Additionally the PHY also contains an AUX lane with AUX blocks
 * for each channel. This is used for DP AUX communication, but
 * this fact isn't really relevant for the driver since AUX is
 * controlled from the display controller side. No DPIO registers
 * need to be accessed during AUX communication,
 *
 * Generally on VLV/CHV the common lane corresponds to the pipe and
 * the spline (PCS/TX) corresponds to the port.
 *
 * For dual channel PHY (VLV/CHV):
 *
 *  pipe A == CMN/PLL/REF CH0
 *
 *  pipe B == CMN/PLL/REF CH1
 *
 *  port B == PCS/TX CH0
 *
 *  port C == PCS/TX CH1
 *
 * This is especially important when we cross the streams
 * ie. drive port B with pipe B, or port C with pipe A.
 *
 * For single channel PHY (CHV):
 *
 *  pipe C == CMN/PLL/REF CH0
 *
 *  port D == PCS/TX CH0
 *
 * On BXT the entire PHY channel corresponds to the port. That means
 * the PLL is also now associated with the port rather than the pipe,
 * and so the clock needs to be routed to the appropriate transcoder.
 * Port A PLL is directly connected to transcoder EDP and port B/C
 * PLLs can be routed to any transcoder A/B/C.
 *
 * Note: DDI0 is digital port B, DD1 is digital port C, and DDI2 is
 * digital port D (CHV) or port A (BXT). ::
 *
 *
 *     Dual channel PHY (VLV/CHV/BXT)
 *     ---------------------------------
 *     |      CH0      |      CH1      |
 *     |  CMN/PLL/REF  |  CMN/PLL/REF  |
 *     |---------------|---------------| Display PHY
 *     | PCS01 | PCS23 | PCS01 | PCS23 |
 *     |-------|-------|-------|-------|
 *     |TX0|TX1|TX2|TX3|TX0|TX1|TX2|TX3|
 *     ---------------------------------
 *     |     DDI0      |     DDI1      | DP/HDMI ports
 *     ---------------------------------
 *
 *     Single channel PHY (CHV/BXT)
 *     -----------------
 *     |      CH0      |
 *     |  CMN/PLL/REF  |
 *     |---------------| Display PHY
 *     | PCS01 | PCS23 |
 *     |-------|-------|
 *     |TX0|TX1|TX2|TX3|
 *     -----------------
 *     |     DDI2      | DP/HDMI port
 *     -----------------
 */

/**
 * struct bxt_dpio_phy_info - Hold info for a broxton DDI phy
 */
struct bxt_dpio_phy_info {
	/**
	 * @dual_channel: true if this phy has a second channel.
	 */
	bool dual_channel;

	/**
	 * @rcomp_phy: If -1, indicates this phy has its own rcomp resistor.
	 * Otherwise the GRC value will be copied from the phy indicated by
	 * this field.
	 */
	enum dpio_phy rcomp_phy;

	/**
	 * @reset_delay: delay in us to wait before setting the common reset
	 * bit in BXT_PHY_CTL_FAMILY, which effectively enables the phy.
	 */
	int reset_delay;

	/**
	 * @pwron_mask: Mask with the appropriate bit set that would cause the
	 * punit to power this phy if written to BXT_P_CR_GT_DISP_PWRON.
	 */
	u32 pwron_mask;

	/**
	 * @channel: struct containing per channel information.
	 */
	struct {
		/**
		 * @channel.port: which port maps to this channel.
		 */
		enum port port;
	} channel[2];
};

static const struct bxt_dpio_phy_info bxt_dpio_phy_info[] = {
	[DPIO_PHY0] = {
		.dual_channel = true,
		.rcomp_phy = DPIO_PHY1,
		.pwron_mask = BIT(0),

		.channel = {
			[DPIO_CH0] = { .port = PORT_B },
			[DPIO_CH1] = { .port = PORT_C },
		}
	},
	[DPIO_PHY1] = {
		.dual_channel = false,
		.rcomp_phy = -1,
		.pwron_mask = BIT(1),

		.channel = {
			[DPIO_CH0] = { .port = PORT_A },
		}
	},
};

static const struct bxt_dpio_phy_info glk_dpio_phy_info[] = {
	[DPIO_PHY0] = {
		.dual_channel = false,
		.rcomp_phy = DPIO_PHY1,
		.pwron_mask = BIT(0),
		.reset_delay = 20,

		.channel = {
			[DPIO_CH0] = { .port = PORT_B },
		}
	},
	[DPIO_PHY1] = {
		.dual_channel = false,
		.rcomp_phy = -1,
		.pwron_mask = BIT(3),
		.reset_delay = 20,

		.channel = {
			[DPIO_CH0] = { .port = PORT_A },
		}
	},
	[DPIO_PHY2] = {
		.dual_channel = false,
		.rcomp_phy = DPIO_PHY1,
		.pwron_mask = BIT(1),
		.reset_delay = 20,

		.channel = {
			[DPIO_CH0] = { .port = PORT_C },
		}
	},
};

static const struct bxt_dpio_phy_info *
bxt_get_phy_list(struct intel_display *display, int *count)
{
	struct drm_i915_private *dev_priv = to_i915(display->drm);

	if (IS_GEMINILAKE(dev_priv)) {
		*count =  ARRAY_SIZE(glk_dpio_phy_info);
		return glk_dpio_phy_info;
	} else {
		*count =  ARRAY_SIZE(bxt_dpio_phy_info);
		return bxt_dpio_phy_info;
	}
}

static const struct bxt_dpio_phy_info *
bxt_get_phy_info(struct intel_display *display, enum dpio_phy phy)
{
	int count;
	const struct bxt_dpio_phy_info *phy_list =
		bxt_get_phy_list(display, &count);

	return &phy_list[phy];
}

void bxt_port_to_phy_channel(struct intel_display *display, enum port port,
			     enum dpio_phy *phy, enum dpio_channel *ch)
{
	const struct bxt_dpio_phy_info *phy_info, *phys;
	int i, count;

	phys = bxt_get_phy_list(display, &count);

	for (i = 0; i < count; i++) {
		phy_info = &phys[i];

		if (port == phy_info->channel[DPIO_CH0].port) {
			*phy = i;
			*ch = DPIO_CH0;
			return;
		}

		if (phy_info->dual_channel &&
		    port == phy_info->channel[DPIO_CH1].port) {
			*phy = i;
			*ch = DPIO_CH1;
			return;
		}
	}

	drm_WARN(display->drm, 1, "PHY not found for PORT %c",
		 port_name(port));
	*phy = DPIO_PHY0;
	*ch = DPIO_CH0;
}

/*
 * Like intel_de_rmw() but reads from a single per-lane register and
 * writes to the group register to write the same value to all the lanes.
 */
static u32 bxt_dpio_phy_rmw_grp(struct intel_display *display,
				i915_reg_t reg_single,
				i915_reg_t reg_group,
				u32 clear, u32 set)
{
	u32 old, val;

	old = intel_de_read(display, reg_single);
	val = (old & ~clear) | set;
	intel_de_write(display, reg_group, val);

	return old;
}

void bxt_dpio_phy_set_signal_levels(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	const struct intel_ddi_buf_trans *trans;
	enum dpio_channel ch;
	enum dpio_phy phy;
	int lane, n_entries;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(display->drm, !trans))
		return;

	bxt_port_to_phy_channel(display, encoder->port, &phy, &ch);

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers and we pick lanes 0/1 for that.
	 */
	bxt_dpio_phy_rmw_grp(display, BXT_PORT_PCS_DW10_LN01(phy, ch),
			     BXT_PORT_PCS_DW10_GRP(phy, ch),
			     TX2_SWING_CALC_INIT | TX1_SWING_CALC_INIT, 0);

	for (lane = 0; lane < crtc_state->lane_count; lane++) {
		int level = intel_ddi_level(encoder, crtc_state, lane);

		intel_de_rmw(display, BXT_PORT_TX_DW2_LN(phy, ch, lane),
			     MARGIN_000_MASK | UNIQ_TRANS_SCALE_MASK,
			     MARGIN_000(trans->entries[level].bxt.margin) |
			     UNIQ_TRANS_SCALE(trans->entries[level].bxt.scale));
	}

	for (lane = 0; lane < crtc_state->lane_count; lane++) {
		int level = intel_ddi_level(encoder, crtc_state, lane);
		u32 val;

		intel_de_rmw(display, BXT_PORT_TX_DW3_LN(phy, ch, lane),
			     SCALE_DCOMP_METHOD,
			     trans->entries[level].bxt.enable ?
			     SCALE_DCOMP_METHOD : 0);

		val = intel_de_read(display, BXT_PORT_TX_DW3_LN(phy, ch, lane));
		if ((val & UNIQUE_TRANGE_EN_METHOD) && !(val & SCALE_DCOMP_METHOD))
			drm_err(display->drm,
				"Disabled scaling while ouniqetrangenmethod was set");
	}

	for (lane = 0; lane < crtc_state->lane_count; lane++) {
		int level = intel_ddi_level(encoder, crtc_state, lane);

		intel_de_rmw(display, BXT_PORT_TX_DW4_LN(phy, ch, lane),
			     DE_EMPHASIS_MASK,
			     DE_EMPHASIS(trans->entries[level].bxt.deemphasis));
	}

	bxt_dpio_phy_rmw_grp(display, BXT_PORT_PCS_DW10_LN01(phy, ch),
			     BXT_PORT_PCS_DW10_GRP(phy, ch),
			     0, TX2_SWING_CALC_INIT | TX1_SWING_CALC_INIT);
}

bool bxt_dpio_phy_is_enabled(struct intel_display *display,
			     enum dpio_phy phy)
{
	const struct bxt_dpio_phy_info *phy_info;

	phy_info = bxt_get_phy_info(display, phy);

	if (!(intel_de_read(display, BXT_P_CR_GT_DISP_PWRON) & phy_info->pwron_mask))
		return false;

	if ((intel_de_read(display, BXT_PORT_CL1CM_DW0(phy)) &
	     (PHY_POWER_GOOD | PHY_RESERVED)) != PHY_POWER_GOOD) {
		drm_dbg(display->drm,
			"DDI PHY %d powered, but power hasn't settled\n", phy);

		return false;
	}

	if (!(intel_de_read(display, BXT_PHY_CTL_FAMILY(phy)) & COMMON_RESET_DIS)) {
		drm_dbg(display->drm,
			"DDI PHY %d powered, but still in reset\n", phy);

		return false;
	}

	return true;
}

static u32 bxt_get_grc(struct intel_display *display, enum dpio_phy phy)
{
	u32 val = intel_de_read(display, BXT_PORT_REF_DW6(phy));

	return REG_FIELD_GET(GRC_CODE_MASK, val);
}

static void bxt_phy_wait_grc_done(struct intel_display *display,
				  enum dpio_phy phy)
{
	if (intel_de_wait_for_set(display, BXT_PORT_REF_DW3(phy), GRC_DONE, 10))
		drm_err(display->drm, "timeout waiting for PHY%d GRC\n", phy);
}

static void _bxt_dpio_phy_init(struct intel_display *display, enum dpio_phy phy)
{
	const struct bxt_dpio_phy_info *phy_info;
	u32 val;

	phy_info = bxt_get_phy_info(display, phy);

	if (bxt_dpio_phy_is_enabled(display, phy)) {
		/* Still read out the GRC value for state verification */
		if (phy_info->rcomp_phy != -1)
			display->state.bxt_phy_grc = bxt_get_grc(display, phy);

		if (bxt_dpio_phy_verify_state(display, phy)) {
			drm_dbg(display->drm, "DDI PHY %d already enabled, "
				"won't reprogram it\n", phy);
			return;
		}

		drm_dbg(display->drm,
			"DDI PHY %d enabled with invalid state, "
			"force reprogramming it\n", phy);
	}

	intel_de_rmw(display, BXT_P_CR_GT_DISP_PWRON, 0, phy_info->pwron_mask);

	/*
	 * The PHY registers start out inaccessible and respond to reads with
	 * all 1s.  Eventually they become accessible as they power up, then
	 * the reserved bit will give the default 0.  Poll on the reserved bit
	 * becoming 0 to find when the PHY is accessible.
	 * The flag should get set in 100us according to the HW team, but
	 * use 1ms due to occasional timeouts observed with that.
	 */
	if (intel_de_wait_fw(display, BXT_PORT_CL1CM_DW0(phy),
			     PHY_RESERVED | PHY_POWER_GOOD, PHY_POWER_GOOD, 1))
		drm_err(display->drm, "timeout during PHY%d power on\n",
			phy);

	/* Program PLL Rcomp code offset */
	intel_de_rmw(display, BXT_PORT_CL1CM_DW9(phy),
		     IREF0RC_OFFSET_MASK, IREF0RC_OFFSET(0xE4));

	intel_de_rmw(display, BXT_PORT_CL1CM_DW10(phy),
		     IREF1RC_OFFSET_MASK, IREF1RC_OFFSET(0xE4));

	/* Program power gating */
	intel_de_rmw(display, BXT_PORT_CL1CM_DW28(phy), 0,
		     OCL1_POWER_DOWN_EN | DW28_OLDO_DYN_PWR_DOWN_EN | SUS_CLK_CONFIG);

	if (phy_info->dual_channel)
		intel_de_rmw(display, BXT_PORT_CL2CM_DW6(phy), 0,
			     DW6_OLDO_DYN_PWR_DOWN_EN);

	if (phy_info->rcomp_phy != -1) {
		u32 grc_code;

		bxt_phy_wait_grc_done(display, phy_info->rcomp_phy);

		/*
		 * PHY0 isn't connected to an RCOMP resistor so copy over
		 * the corresponding calibrated value from PHY1, and disable
		 * the automatic calibration on PHY0.
		 */
		val = bxt_get_grc(display, phy_info->rcomp_phy);
		display->state.bxt_phy_grc = val;

		grc_code = GRC_CODE_FAST(val) |
			GRC_CODE_SLOW(val) |
			GRC_CODE_NOM(val);
		intel_de_write(display, BXT_PORT_REF_DW6(phy), grc_code);
		intel_de_rmw(display, BXT_PORT_REF_DW8(phy),
			     0, GRC_DIS | GRC_RDY_OVRD);
	}

	if (phy_info->reset_delay)
		udelay(phy_info->reset_delay);

	intel_de_rmw(display, BXT_PHY_CTL_FAMILY(phy), 0, COMMON_RESET_DIS);
}

void bxt_dpio_phy_uninit(struct intel_display *display, enum dpio_phy phy)
{
	const struct bxt_dpio_phy_info *phy_info;

	phy_info = bxt_get_phy_info(display, phy);

	intel_de_rmw(display, BXT_PHY_CTL_FAMILY(phy), COMMON_RESET_DIS, 0);

	intel_de_rmw(display, BXT_P_CR_GT_DISP_PWRON, phy_info->pwron_mask, 0);
}

void bxt_dpio_phy_init(struct intel_display *display, enum dpio_phy phy)
{
	const struct bxt_dpio_phy_info *phy_info = bxt_get_phy_info(display, phy);
	enum dpio_phy rcomp_phy = phy_info->rcomp_phy;
	bool was_enabled;

	lockdep_assert_held(&display->power.domains.lock);

	was_enabled = true;
	if (rcomp_phy != -1)
		was_enabled = bxt_dpio_phy_is_enabled(display, rcomp_phy);

	/*
	 * We need to copy the GRC calibration value from rcomp_phy,
	 * so make sure it's powered up.
	 */
	if (!was_enabled)
		_bxt_dpio_phy_init(display, rcomp_phy);

	_bxt_dpio_phy_init(display, phy);

	if (!was_enabled)
		bxt_dpio_phy_uninit(display, rcomp_phy);
}

static bool __printf(6, 7)
__phy_reg_verify_state(struct intel_display *display, enum dpio_phy phy,
		       i915_reg_t reg, u32 mask, u32 expected,
		       const char *reg_fmt, ...)
{
	struct va_format vaf;
	va_list args;
	u32 val;

	val = intel_de_read(display, reg);
	if ((val & mask) == expected)
		return true;

	va_start(args, reg_fmt);
	vaf.fmt = reg_fmt;
	vaf.va = &args;

	drm_dbg(display->drm, "DDI PHY %d reg %pV [%08x] state mismatch: "
			 "current %08x, expected %08x (mask %08x)\n",
			 phy, &vaf, reg.reg, val, (val & ~mask) | expected,
			 mask);

	va_end(args);

	return false;
}

bool bxt_dpio_phy_verify_state(struct intel_display *display,
			       enum dpio_phy phy)
{
	const struct bxt_dpio_phy_info *phy_info;
	u32 mask;
	bool ok;

	phy_info = bxt_get_phy_info(display, phy);

#define _CHK(reg, mask, exp, fmt, ...)					\
	__phy_reg_verify_state(display, phy, reg, mask, exp, fmt,	\
			       ## __VA_ARGS__)

	if (!bxt_dpio_phy_is_enabled(display, phy))
		return false;

	ok = true;

	/* PLL Rcomp code offset */
	ok &= _CHK(BXT_PORT_CL1CM_DW9(phy),
		   IREF0RC_OFFSET_MASK, IREF0RC_OFFSET(0xe4),
		   "BXT_PORT_CL1CM_DW9(%d)", phy);
	ok &= _CHK(BXT_PORT_CL1CM_DW10(phy),
		   IREF1RC_OFFSET_MASK, IREF1RC_OFFSET(0xe4),
		   "BXT_PORT_CL1CM_DW10(%d)", phy);

	/* Power gating */
	mask = OCL1_POWER_DOWN_EN | DW28_OLDO_DYN_PWR_DOWN_EN | SUS_CLK_CONFIG;
	ok &= _CHK(BXT_PORT_CL1CM_DW28(phy), mask, mask,
		   "BXT_PORT_CL1CM_DW28(%d)", phy);

	if (phy_info->dual_channel)
		ok &= _CHK(BXT_PORT_CL2CM_DW6(phy),
			   DW6_OLDO_DYN_PWR_DOWN_EN, DW6_OLDO_DYN_PWR_DOWN_EN,
			   "BXT_PORT_CL2CM_DW6(%d)", phy);

	if (phy_info->rcomp_phy != -1) {
		u32 grc_code = display->state.bxt_phy_grc;

		grc_code = GRC_CODE_FAST(grc_code) |
			GRC_CODE_SLOW(grc_code) |
			GRC_CODE_NOM(grc_code);
		mask = GRC_CODE_FAST_MASK | GRC_CODE_SLOW_MASK |
		       GRC_CODE_NOM_MASK;
		ok &= _CHK(BXT_PORT_REF_DW6(phy), mask, grc_code,
			   "BXT_PORT_REF_DW6(%d)", phy);

		mask = GRC_DIS | GRC_RDY_OVRD;
		ok &= _CHK(BXT_PORT_REF_DW8(phy), mask, mask,
			   "BXT_PORT_REF_DW8(%d)", phy);
	}

	return ok;
#undef _CHK
}

u8
bxt_dpio_phy_calc_lane_lat_optim_mask(u8 lane_count)
{
	switch (lane_count) {
	case 1:
		return 0;
	case 2:
		return BIT(2) | BIT(0);
	case 4:
		return BIT(3) | BIT(2) | BIT(0);
	default:
		MISSING_CASE(lane_count);

		return 0;
	}
}

void bxt_dpio_phy_set_lane_optim_mask(struct intel_encoder *encoder,
				      u8 lane_lat_optim_mask)
{
	struct intel_display *display = to_intel_display(encoder);
	enum port port = encoder->port;
	enum dpio_phy phy;
	enum dpio_channel ch;
	int lane;

	bxt_port_to_phy_channel(display, port, &phy, &ch);

	for (lane = 0; lane < 4; lane++) {
		/*
		 * Note that on CHV this flag is called UPAR, but has
		 * the same function.
		 */
		intel_de_rmw(display, BXT_PORT_TX_DW14_LN(phy, ch, lane),
			     LATENCY_OPTIM,
			     lane_lat_optim_mask & BIT(lane) ? LATENCY_OPTIM : 0);
	}
}

u8
bxt_dpio_phy_get_lane_lat_optim_mask(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	enum port port = encoder->port;
	enum dpio_phy phy;
	enum dpio_channel ch;
	int lane;
	u8 mask;

	bxt_port_to_phy_channel(display, port, &phy, &ch);

	mask = 0;
	for (lane = 0; lane < 4; lane++) {
		u32 val = intel_de_read(display,
					BXT_PORT_TX_DW14_LN(phy, ch, lane));

		if (val & LATENCY_OPTIM)
			mask |= BIT(lane);
	}

	return mask;
}

enum dpio_channel vlv_dig_port_to_channel(struct intel_digital_port *dig_port)
{
	switch (dig_port->base.port) {
	default:
		MISSING_CASE(dig_port->base.port);
		fallthrough;
	case PORT_B:
	case PORT_D:
		return DPIO_CH0;
	case PORT_C:
		return DPIO_CH1;
	}
}

enum dpio_phy vlv_dig_port_to_phy(struct intel_digital_port *dig_port)
{
	switch (dig_port->base.port) {
	default:
		MISSING_CASE(dig_port->base.port);
		fallthrough;
	case PORT_B:
	case PORT_C:
		return DPIO_PHY0;
	case PORT_D:
		return DPIO_PHY1;
	}
}

enum dpio_phy vlv_pipe_to_phy(enum pipe pipe)
{
	switch (pipe) {
	default:
		MISSING_CASE(pipe);
		fallthrough;
	case PIPE_A:
	case PIPE_B:
		return DPIO_PHY0;
	case PIPE_C:
		return DPIO_PHY1;
	}
}

enum dpio_channel vlv_pipe_to_channel(enum pipe pipe)
{
	switch (pipe) {
	default:
		MISSING_CASE(pipe);
		fallthrough;
	case PIPE_A:
	case PIPE_C:
		return DPIO_CH0;
	case PIPE_B:
		return DPIO_CH1;
	}
}

void chv_set_phy_signal_level(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      u32 deemph_reg_value, u32 margin_reg_value,
			      bool uniq_trans_scale)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);
	u32 val;
	int i;

	vlv_dpio_get(dev_priv);

	/* Clear calc init */
	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW10(ch));
	val &= ~(DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3);
	val &= ~(DPIO_PCS_TX1DEEMP_MASK | DPIO_PCS_TX2DEEMP_MASK);
	val |= DPIO_PCS_TX1DEEMP_9P5 | DPIO_PCS_TX2DEEMP_9P5;
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW10(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW10(ch));
		val &= ~(DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3);
		val &= ~(DPIO_PCS_TX1DEEMP_MASK | DPIO_PCS_TX2DEEMP_MASK);
		val |= DPIO_PCS_TX1DEEMP_9P5 | DPIO_PCS_TX2DEEMP_9P5;
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW10(ch), val);
	}

	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW9(ch));
	val &= ~(DPIO_PCS_TX1MARGIN_MASK | DPIO_PCS_TX2MARGIN_MASK);
	val |= DPIO_PCS_TX1MARGIN_000 | DPIO_PCS_TX2MARGIN_000;
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW9(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW9(ch));
		val &= ~(DPIO_PCS_TX1MARGIN_MASK | DPIO_PCS_TX2MARGIN_MASK);
		val |= DPIO_PCS_TX1MARGIN_000 | DPIO_PCS_TX2MARGIN_000;
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW9(ch), val);
	}

	/* Program swing deemph */
	for (i = 0; i < crtc_state->lane_count; i++) {
		val = vlv_dpio_read(dev_priv, phy, CHV_TX_DW4(ch, i));
		val &= ~DPIO_SWING_DEEMPH9P5_MASK;
		val |= DPIO_SWING_DEEMPH9P5(deemph_reg_value);
		vlv_dpio_write(dev_priv, phy, CHV_TX_DW4(ch, i), val);
	}

	/* Program swing margin */
	for (i = 0; i < crtc_state->lane_count; i++) {
		val = vlv_dpio_read(dev_priv, phy, CHV_TX_DW2(ch, i));

		val &= ~DPIO_SWING_MARGIN000_MASK;
		val |= DPIO_SWING_MARGIN000(margin_reg_value);

		/*
		 * Supposedly this value shouldn't matter when unique transition
		 * scale is disabled, but in fact it does matter. Let's just
		 * always program the same value and hope it's OK.
		 */
		val &= ~DPIO_UNIQ_TRANS_SCALE_MASK;
		val |= DPIO_UNIQ_TRANS_SCALE(0x9a);

		vlv_dpio_write(dev_priv, phy, CHV_TX_DW2(ch, i), val);
	}

	/*
	 * The document said it needs to set bit 27 for ch0 and bit 26
	 * for ch1. Might be a typo in the doc.
	 * For now, for this unique transition scale selection, set bit
	 * 27 for ch0 and ch1.
	 */
	for (i = 0; i < crtc_state->lane_count; i++) {
		val = vlv_dpio_read(dev_priv, phy, CHV_TX_DW3(ch, i));
		if (uniq_trans_scale)
			val |= DPIO_TX_UNIQ_TRANS_SCALE_EN;
		else
			val &= ~DPIO_TX_UNIQ_TRANS_SCALE_EN;
		vlv_dpio_write(dev_priv, phy, CHV_TX_DW3(ch, i), val);
	}

	/* Start swing calculation */
	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW10(ch));
	val |= DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3;
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW10(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW10(ch));
		val |= DPIO_PCS_SWING_CALC_TX0_TX2 | DPIO_PCS_SWING_CALC_TX1_TX3;
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW10(ch), val);
	}

	vlv_dpio_put(dev_priv);
}

void chv_data_lane_soft_reset(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      bool reset)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);
	u32 val;

	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW0(ch));
	if (reset)
		val &= ~(DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET);
	else
		val |= DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET;
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW0(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW0(ch));
		if (reset)
			val &= ~(DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET);
		else
			val |= DPIO_PCS_TX_LANE2_RESET | DPIO_PCS_TX_LANE1_RESET;
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW0(ch), val);
	}

	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW1(ch));
	val |= CHV_PCS_REQ_SOFTRESET_EN;
	if (reset)
		val &= ~DPIO_PCS_CLK_SOFT_RESET;
	else
		val |= DPIO_PCS_CLK_SOFT_RESET;
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW1(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW1(ch));
		val |= CHV_PCS_REQ_SOFTRESET_EN;
		if (reset)
			val &= ~DPIO_PCS_CLK_SOFT_RESET;
		else
			val |= DPIO_PCS_CLK_SOFT_RESET;
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW1(ch), val);
	}
}

void chv_phy_pre_pll_enable(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);
	enum pipe pipe = crtc->pipe;
	unsigned int lane_mask =
		intel_dp_unused_lane_mask(crtc_state->lane_count);
	u32 val;

	/*
	 * Must trick the second common lane into life.
	 * Otherwise we can't even access the PLL.
	 */
	if (ch == DPIO_CH0 && pipe == PIPE_B)
		dig_port->release_cl2_override =
			!chv_phy_powergate_ch(display, DPIO_PHY0, DPIO_CH1, true);

	chv_phy_powergate_lanes(encoder, true, lane_mask);

	vlv_dpio_get(dev_priv);

	/* Assert data lane reset */
	chv_data_lane_soft_reset(encoder, crtc_state, true);

	/* program left/right clock distribution */
	if (pipe != PIPE_B) {
		val = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW5_CH0);
		val &= ~(CHV_BUFLEFTENA1_MASK | CHV_BUFRIGHTENA1_MASK);
		if (ch == DPIO_CH0)
			val |= CHV_BUFLEFTENA1_FORCE;
		if (ch == DPIO_CH1)
			val |= CHV_BUFRIGHTENA1_FORCE;
		vlv_dpio_write(dev_priv, phy, CHV_CMN_DW5_CH0, val);
	} else {
		val = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW1_CH1);
		val &= ~(CHV_BUFLEFTENA2_MASK | CHV_BUFRIGHTENA2_MASK);
		if (ch == DPIO_CH0)
			val |= CHV_BUFLEFTENA2_FORCE;
		if (ch == DPIO_CH1)
			val |= CHV_BUFRIGHTENA2_FORCE;
		vlv_dpio_write(dev_priv, phy, CHV_CMN_DW1_CH1, val);
	}

	/* program clock channel usage */
	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW8(ch));
	val |= DPIO_PCS_USEDCLKCHANNEL_OVRRIDE;
	if (pipe == PIPE_B)
		val |= DPIO_PCS_USEDCLKCHANNEL;
	else
		val &= ~DPIO_PCS_USEDCLKCHANNEL;
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW8(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW8(ch));
		val |= DPIO_PCS_USEDCLKCHANNEL_OVRRIDE;
		if (pipe == PIPE_B)
			val |= DPIO_PCS_USEDCLKCHANNEL;
		else
			val &= ~DPIO_PCS_USEDCLKCHANNEL;
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW8(ch), val);
	}

	/*
	 * This a a bit weird since generally CL
	 * matches the pipe, but here we need to
	 * pick the CL based on the port.
	 */
	val = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW19(ch));
	if (pipe == PIPE_B)
		val |= CHV_CMN_USEDCLKCHANNEL;
	else
		val &= ~CHV_CMN_USEDCLKCHANNEL;
	vlv_dpio_write(dev_priv, phy, CHV_CMN_DW19(ch), val);

	vlv_dpio_put(dev_priv);
}

void chv_phy_pre_encoder_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);
	int data, i, stagger;
	u32 val;

	vlv_dpio_get(dev_priv);

	/* allow hardware to manage TX FIFO reset source */
	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW11(ch));
	val &= ~DPIO_LANEDESKEW_STRAP_OVRD;
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW11(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW11(ch));
		val &= ~DPIO_LANEDESKEW_STRAP_OVRD;
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW11(ch), val);
	}

	/* Program Tx lane latency optimal setting*/
	for (i = 0; i < crtc_state->lane_count; i++) {
		/* Set the upar bit */
		if (crtc_state->lane_count == 1)
			data = 0;
		else
			data = (i == 1) ? 0 : DPIO_UPAR;
		vlv_dpio_write(dev_priv, phy, CHV_TX_DW14(ch, i), data);
	}

	/* Data lane stagger programming */
	if (crtc_state->port_clock > 270000)
		stagger = 0x18;
	else if (crtc_state->port_clock > 135000)
		stagger = 0xd;
	else if (crtc_state->port_clock > 67500)
		stagger = 0x7;
	else if (crtc_state->port_clock > 33750)
		stagger = 0x4;
	else
		stagger = 0x2;

	val = vlv_dpio_read(dev_priv, phy, VLV_PCS01_DW11(ch));
	val |= DPIO_TX2_STAGGER_MASK(0x1f);
	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW11(ch), val);

	if (crtc_state->lane_count > 2) {
		val = vlv_dpio_read(dev_priv, phy, VLV_PCS23_DW11(ch));
		val |= DPIO_TX2_STAGGER_MASK(0x1f);
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW11(ch), val);
	}

	vlv_dpio_write(dev_priv, phy, VLV_PCS01_DW12(ch),
		       DPIO_LANESTAGGER_STRAP(stagger) |
		       DPIO_LANESTAGGER_STRAP_OVRD |
		       DPIO_TX1_STAGGER_MASK(0x1f) |
		       DPIO_TX1_STAGGER_MULT(6) |
		       DPIO_TX2_STAGGER_MULT(0));

	if (crtc_state->lane_count > 2) {
		vlv_dpio_write(dev_priv, phy, VLV_PCS23_DW12(ch),
			       DPIO_LANESTAGGER_STRAP(stagger) |
			       DPIO_LANESTAGGER_STRAP_OVRD |
			       DPIO_TX1_STAGGER_MASK(0x1f) |
			       DPIO_TX1_STAGGER_MULT(7) |
			       DPIO_TX2_STAGGER_MULT(5));
	}

	/* Deassert data lane reset */
	chv_data_lane_soft_reset(encoder, crtc_state, false);

	vlv_dpio_put(dev_priv);
}

void chv_phy_release_cl2_override(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (dig_port->release_cl2_override) {
		chv_phy_powergate_ch(display, DPIO_PHY0, DPIO_CH1, false);
		dig_port->release_cl2_override = false;
	}
}

void chv_phy_post_pll_disable(struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum dpio_phy phy = vlv_dig_port_to_phy(enc_to_dig_port(encoder));
	enum pipe pipe = to_intel_crtc(old_crtc_state->uapi.crtc)->pipe;
	u32 val;

	vlv_dpio_get(dev_priv);

	/* disable left/right clock distribution */
	if (pipe != PIPE_B) {
		val = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW5_CH0);
		val &= ~(CHV_BUFLEFTENA1_MASK | CHV_BUFRIGHTENA1_MASK);
		vlv_dpio_write(dev_priv, phy, CHV_CMN_DW5_CH0, val);
	} else {
		val = vlv_dpio_read(dev_priv, phy, CHV_CMN_DW1_CH1);
		val &= ~(CHV_BUFLEFTENA2_MASK | CHV_BUFRIGHTENA2_MASK);
		vlv_dpio_write(dev_priv, phy, CHV_CMN_DW1_CH1, val);
	}

	vlv_dpio_put(dev_priv);

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

void vlv_set_phy_signal_level(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      u32 demph_reg_value, u32 preemph_reg_value,
			      u32 uniqtranscale_reg_value, u32 tx3_demph)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);

	vlv_dpio_get(dev_priv);

	vlv_dpio_write(dev_priv, phy, VLV_TX_DW5_GRP(ch), 0x00000000);
	vlv_dpio_write(dev_priv, phy, VLV_TX_DW4_GRP(ch), demph_reg_value);
	vlv_dpio_write(dev_priv, phy, VLV_TX_DW2_GRP(ch),
			 uniqtranscale_reg_value);
	vlv_dpio_write(dev_priv, phy, VLV_TX_DW3_GRP(ch), 0x0C782040);

	if (tx3_demph)
		vlv_dpio_write(dev_priv, phy, VLV_TX_DW4(ch, 3), tx3_demph);

	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW11_GRP(ch), 0x00030000);
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW9_GRP(ch), preemph_reg_value);
	vlv_dpio_write(dev_priv, phy, VLV_TX_DW5_GRP(ch), DPIO_TX_OCALINIT_EN);

	vlv_dpio_put(dev_priv);
}

void vlv_phy_pre_pll_enable(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);

	/* Program Tx lane resets to default */
	vlv_dpio_get(dev_priv);

	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW0_GRP(ch),
		       DPIO_PCS_TX_LANE2_RESET |
		       DPIO_PCS_TX_LANE1_RESET);
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW1_GRP(ch),
		       DPIO_PCS_CLK_CRI_RXEB_EIOS_EN |
		       DPIO_PCS_CLK_CRI_RXDIGFILTSG_EN |
		       DPIO_PCS_CLK_DATAWIDTH_8_10 |
		       DPIO_PCS_CLK_SOFT_RESET);

	/* Fix up inter-pair skew failure */
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW12_GRP(ch), 0x00750f00);
	vlv_dpio_write(dev_priv, phy, VLV_TX_DW11_GRP(ch), 0x00001500);
	vlv_dpio_write(dev_priv, phy, VLV_TX_DW14_GRP(ch), 0x40400000);

	vlv_dpio_put(dev_priv);
}

void vlv_phy_pre_encoder_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);
	enum pipe pipe = crtc->pipe;
	u32 val;

	vlv_dpio_get(dev_priv);

	/* Enable clock channels for this port */
	val = DPIO_PCS_USEDCLKCHANNEL_OVRRIDE;
	if (pipe == PIPE_B)
		val |= DPIO_PCS_USEDCLKCHANNEL;
	val |= 0xc4;
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW8_GRP(ch), val);

	/* Program lane clock */
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW14_GRP(ch), 0x00760018);
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW23_GRP(ch), 0x00400888);

	vlv_dpio_put(dev_priv);
}

void vlv_phy_reset_lanes(struct intel_encoder *encoder,
			 const struct intel_crtc_state *old_crtc_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum dpio_channel ch = vlv_dig_port_to_channel(dig_port);
	enum dpio_phy phy = vlv_dig_port_to_phy(dig_port);

	vlv_dpio_get(dev_priv);
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW0_GRP(ch), 0x00000000);
	vlv_dpio_write(dev_priv, phy, VLV_PCS_DW1_GRP(ch), 0x00e00060);
	vlv_dpio_put(dev_priv);
}
