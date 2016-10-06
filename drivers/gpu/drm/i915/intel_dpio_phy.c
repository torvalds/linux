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

#include "intel_drv.h"

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

bool bxt_ddi_phy_is_enabled(struct drm_i915_private *dev_priv,
			    enum dpio_phy phy)
{
	enum port port;

	if (!(I915_READ(BXT_P_CR_GT_DISP_PWRON) & GT_DISPLAY_POWER_ON(phy)))
		return false;

	if ((I915_READ(BXT_PORT_CL1CM_DW0(phy)) &
	     (PHY_POWER_GOOD | PHY_RESERVED)) != PHY_POWER_GOOD) {
		DRM_DEBUG_DRIVER("DDI PHY %d powered, but power hasn't settled\n",
				 phy);

		return false;
	}

	if (phy == DPIO_PHY1 &&
	    !(I915_READ(BXT_PORT_REF_DW3(DPIO_PHY1)) & GRC_DONE)) {
		DRM_DEBUG_DRIVER("DDI PHY 1 powered, but GRC isn't done\n");

		return false;
	}

	if (!(I915_READ(BXT_PHY_CTL_FAMILY(phy)) & COMMON_RESET_DIS)) {
		DRM_DEBUG_DRIVER("DDI PHY %d powered, but still in reset\n",
				 phy);

		return false;
	}

	for_each_port_masked(port,
			     phy == DPIO_PHY0 ? BIT(PORT_B) | BIT(PORT_C) :
						BIT(PORT_A)) {
		u32 tmp = I915_READ(BXT_PHY_CTL(port));

		if (tmp & BXT_PHY_CMNLANE_POWERDOWN_ACK) {
			DRM_DEBUG_DRIVER("DDI PHY %d powered, but common lane "
					 "for port %c powered down "
					 "(PHY_CTL %08x)\n",
					 phy, port_name(port), tmp);

			return false;
		}
	}

	return true;
}

static u32 bxt_get_grc(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
	u32 val = I915_READ(BXT_PORT_REF_DW6(phy));

	return (val & GRC_CODE_MASK) >> GRC_CODE_SHIFT;
}

static void bxt_phy_wait_grc_done(struct drm_i915_private *dev_priv,
				  enum dpio_phy phy)
{
	if (intel_wait_for_register(dev_priv,
				    BXT_PORT_REF_DW3(phy),
				    GRC_DONE, GRC_DONE,
				    10))
		DRM_ERROR("timeout waiting for PHY%d GRC\n", phy);
}

void bxt_ddi_phy_init(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
	u32 val;

	if (bxt_ddi_phy_is_enabled(dev_priv, phy)) {
		/* Still read out the GRC value for state verification */
		if (phy == DPIO_PHY0)
			dev_priv->bxt_phy_grc = bxt_get_grc(dev_priv, phy);

		if (bxt_ddi_phy_verify_state(dev_priv, phy)) {
			DRM_DEBUG_DRIVER("DDI PHY %d already enabled, "
					 "won't reprogram it\n", phy);

			return;
		}

		DRM_DEBUG_DRIVER("DDI PHY %d enabled with invalid state, "
				 "force reprogramming it\n", phy);
	}

	val = I915_READ(BXT_P_CR_GT_DISP_PWRON);
	val |= GT_DISPLAY_POWER_ON(phy);
	I915_WRITE(BXT_P_CR_GT_DISP_PWRON, val);

	/*
	 * The PHY registers start out inaccessible and respond to reads with
	 * all 1s.  Eventually they become accessible as they power up, then
	 * the reserved bit will give the default 0.  Poll on the reserved bit
	 * becoming 0 to find when the PHY is accessible.
	 * HW team confirmed that the time to reach phypowergood status is
	 * anywhere between 50 us and 100us.
	 */
	if (wait_for_us(((I915_READ(BXT_PORT_CL1CM_DW0(phy)) &
		(PHY_RESERVED | PHY_POWER_GOOD)) == PHY_POWER_GOOD), 100)) {
		DRM_ERROR("timeout during PHY%d power on\n", phy);
	}

	/* Program PLL Rcomp code offset */
	val = I915_READ(BXT_PORT_CL1CM_DW9(phy));
	val &= ~IREF0RC_OFFSET_MASK;
	val |= 0xE4 << IREF0RC_OFFSET_SHIFT;
	I915_WRITE(BXT_PORT_CL1CM_DW9(phy), val);

	val = I915_READ(BXT_PORT_CL1CM_DW10(phy));
	val &= ~IREF1RC_OFFSET_MASK;
	val |= 0xE4 << IREF1RC_OFFSET_SHIFT;
	I915_WRITE(BXT_PORT_CL1CM_DW10(phy), val);

	/* Program power gating */
	val = I915_READ(BXT_PORT_CL1CM_DW28(phy));
	val |= OCL1_POWER_DOWN_EN | DW28_OLDO_DYN_PWR_DOWN_EN |
		SUS_CLK_CONFIG;
	I915_WRITE(BXT_PORT_CL1CM_DW28(phy), val);

	if (phy == DPIO_PHY0) {
		val = I915_READ(BXT_PORT_CL2CM_DW6_BC);
		val |= DW6_OLDO_DYN_PWR_DOWN_EN;
		I915_WRITE(BXT_PORT_CL2CM_DW6_BC, val);
	}

	val = I915_READ(BXT_PORT_CL1CM_DW30(phy));
	val &= ~OCL2_LDOFUSE_PWR_DIS;
	/*
	 * On PHY1 disable power on the second channel, since no port is
	 * connected there. On PHY0 both channels have a port, so leave it
	 * enabled.
	 * TODO: port C is only connected on BXT-P, so on BXT0/1 we should
	 * power down the second channel on PHY0 as well.
	 *
	 * FIXME: Clarify programming of the following, the register is
	 * read-only with bit 6 fixed at 0 at least in stepping A.
	 */
	if (phy == DPIO_PHY1)
		val |= OCL2_LDOFUSE_PWR_DIS;
	I915_WRITE(BXT_PORT_CL1CM_DW30(phy), val);

	if (phy == DPIO_PHY0) {
		uint32_t grc_code;
		/*
		 * PHY0 isn't connected to an RCOMP resistor so copy over
		 * the corresponding calibrated value from PHY1, and disable
		 * the automatic calibration on PHY0.
		 */
		val = dev_priv->bxt_phy_grc = bxt_get_grc(dev_priv, DPIO_PHY1);
		grc_code = val << GRC_CODE_FAST_SHIFT |
			   val << GRC_CODE_SLOW_SHIFT |
			   val;
		I915_WRITE(BXT_PORT_REF_DW6(DPIO_PHY0), grc_code);

		val = I915_READ(BXT_PORT_REF_DW8(DPIO_PHY0));
		val |= GRC_DIS | GRC_RDY_OVRD;
		I915_WRITE(BXT_PORT_REF_DW8(DPIO_PHY0), val);
	}

	val = I915_READ(BXT_PHY_CTL_FAMILY(phy));
	val |= COMMON_RESET_DIS;
	I915_WRITE(BXT_PHY_CTL_FAMILY(phy), val);

	if (phy == DPIO_PHY1)
		bxt_phy_wait_grc_done(dev_priv, DPIO_PHY1);
}

void bxt_ddi_phy_uninit(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
	uint32_t val;

	val = I915_READ(BXT_PHY_CTL_FAMILY(phy));
	val &= ~COMMON_RESET_DIS;
	I915_WRITE(BXT_PHY_CTL_FAMILY(phy), val);

	val = I915_READ(BXT_P_CR_GT_DISP_PWRON);
	val &= ~GT_DISPLAY_POWER_ON(phy);
	I915_WRITE(BXT_P_CR_GT_DISP_PWRON, val);
}

static bool __printf(6, 7)
__phy_reg_verify_state(struct drm_i915_private *dev_priv, enum dpio_phy phy,
		       i915_reg_t reg, u32 mask, u32 expected,
		       const char *reg_fmt, ...)
{
	struct va_format vaf;
	va_list args;
	u32 val;

	val = I915_READ(reg);
	if ((val & mask) == expected)
		return true;

	va_start(args, reg_fmt);
	vaf.fmt = reg_fmt;
	vaf.va = &args;

	DRM_DEBUG_DRIVER("DDI PHY %d reg %pV [%08x] state mismatch: "
			 "current %08x, expected %08x (mask %08x)\n",
			 phy, &vaf, reg.reg, val, (val & ~mask) | expected,
			 mask);

	va_end(args);

	return false;
}

bool bxt_ddi_phy_verify_state(struct drm_i915_private *dev_priv,
			      enum dpio_phy phy)
{
	uint32_t mask;
	bool ok;

#define _CHK(reg, mask, exp, fmt, ...)					\
	__phy_reg_verify_state(dev_priv, phy, reg, mask, exp, fmt,	\
			       ## __VA_ARGS__)

	if (!bxt_ddi_phy_is_enabled(dev_priv, phy))
		return false;

	ok = true;

	/* PLL Rcomp code offset */
	ok &= _CHK(BXT_PORT_CL1CM_DW9(phy),
		    IREF0RC_OFFSET_MASK, 0xe4 << IREF0RC_OFFSET_SHIFT,
		    "BXT_PORT_CL1CM_DW9(%d)", phy);
	ok &= _CHK(BXT_PORT_CL1CM_DW10(phy),
		    IREF1RC_OFFSET_MASK, 0xe4 << IREF1RC_OFFSET_SHIFT,
		    "BXT_PORT_CL1CM_DW10(%d)", phy);

	/* Power gating */
	mask = OCL1_POWER_DOWN_EN | DW28_OLDO_DYN_PWR_DOWN_EN | SUS_CLK_CONFIG;
	ok &= _CHK(BXT_PORT_CL1CM_DW28(phy), mask, mask,
		    "BXT_PORT_CL1CM_DW28(%d)", phy);

	if (phy == DPIO_PHY0)
		ok &= _CHK(BXT_PORT_CL2CM_DW6_BC,
			   DW6_OLDO_DYN_PWR_DOWN_EN, DW6_OLDO_DYN_PWR_DOWN_EN,
			   "BXT_PORT_CL2CM_DW6_BC");

	/*
	 * TODO: Verify BXT_PORT_CL1CM_DW30 bit OCL2_LDOFUSE_PWR_DIS,
	 * at least on stepping A this bit is read-only and fixed at 0.
	 */

	if (phy == DPIO_PHY0) {
		u32 grc_code = dev_priv->bxt_phy_grc;

		grc_code = grc_code << GRC_CODE_FAST_SHIFT |
			   grc_code << GRC_CODE_SLOW_SHIFT |
			   grc_code;
		mask = GRC_CODE_FAST_MASK | GRC_CODE_SLOW_MASK |
		       GRC_CODE_NOM_MASK;
		ok &= _CHK(BXT_PORT_REF_DW6(DPIO_PHY0), mask, grc_code,
			    "BXT_PORT_REF_DW6(%d)", DPIO_PHY0);

		mask = GRC_DIS | GRC_RDY_OVRD;
		ok &= _CHK(BXT_PORT_REF_DW8(DPIO_PHY0), mask, mask,
			    "BXT_PORT_REF_DW8(%d)", DPIO_PHY0);
	}

	return ok;
#undef _CHK
}

uint8_t
bxt_ddi_phy_calc_lane_lat_optim_mask(struct intel_encoder *encoder,
				     uint8_t lane_count)
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

void bxt_ddi_phy_set_lane_optim_mask(struct intel_encoder *encoder,
				     uint8_t lane_lat_optim_mask)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);
	enum port port = dport->port;
	int lane;

	for (lane = 0; lane < 4; lane++) {
		u32 val = I915_READ(BXT_PORT_TX_DW14_LN(port, lane));

		/*
		 * Note that on CHV this flag is called UPAR, but has
		 * the same function.
		 */
		val &= ~LATENCY_OPTIM;
		if (lane_lat_optim_mask & BIT(lane))
			val |= LATENCY_OPTIM;

		I915_WRITE(BXT_PORT_TX_DW14_LN(port, lane), val);
	}
}

uint8_t
bxt_ddi_phy_get_lane_lat_optim_mask(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(dport->base.base.dev);
	enum port port = dport->port;
	int lane;
	uint8_t mask;

	mask = 0;
	for (lane = 0; lane < 4; lane++) {
		u32 val = I915_READ(BXT_PORT_TX_DW14_LN(port, lane));

		if (val & LATENCY_OPTIM)
			mask |= BIT(lane);
	}

	return mask;
}


void chv_set_phy_signal_level(struct intel_encoder *encoder,
			      u32 deemph_reg_value, u32 margin_reg_value,
			      bool uniq_trans_scale)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct intel_crtc *intel_crtc = to_intel_crtc(dport->base.base.crtc);
	enum dpio_channel ch = vlv_dport_to_channel(dport);
	enum pipe pipe = intel_crtc->pipe;
	u32 val;
	int i;

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
		if (uniq_trans_scale)
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

}

void chv_data_lane_soft_reset(struct intel_encoder *encoder,
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

void chv_phy_pre_pll_enable(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	enum dpio_channel ch = vlv_dport_to_channel(dport);
	enum pipe pipe = intel_crtc->pipe;
	unsigned int lane_mask =
		intel_dp_unused_lane_mask(intel_crtc->config->lane_count);
	u32 val;

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

void chv_phy_pre_encoder_enable(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
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
}

void chv_phy_release_cl2_override(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (dport->release_cl2_override) {
		chv_phy_powergate_ch(dev_priv, DPIO_PHY0, DPIO_CH1, false);
		dport->release_cl2_override = false;
	}
}

void chv_phy_post_pll_disable(struct intel_encoder *encoder)
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

void vlv_set_phy_signal_level(struct intel_encoder *encoder,
			      u32 demph_reg_value, u32 preemph_reg_value,
			      u32 uniqtranscale_reg_value, u32 tx3_demph)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

	mutex_lock(&dev_priv->sb_lock);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW5(port), 0x00000000);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW4(port), demph_reg_value);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW2(port),
			 uniqtranscale_reg_value);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW3(port), 0x0C782040);

	if (tx3_demph)
		vlv_dpio_write(dev_priv, pipe, VLV_TX3_DW4(port), tx3_demph);

	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW11(port), 0x00030000);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW9(port), preemph_reg_value);
	vlv_dpio_write(dev_priv, pipe, VLV_TX_DW5(port), DPIO_TX_OCALINIT_EN);
	mutex_unlock(&dev_priv->sb_lock);
}

void vlv_phy_pre_pll_enable(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

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

void vlv_phy_pre_encoder_enable(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *dport = dp_to_dig_port(intel_dp);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;
	u32 val;

	mutex_lock(&dev_priv->sb_lock);

	/* Enable clock channels for this port */
	val = vlv_dpio_read(dev_priv, pipe, VLV_PCS01_DW8(port));
	val = 0;
	if (pipe)
		val |= (1<<21);
	else
		val &= ~(1<<21);
	val |= 0x001000c4;
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW8(port), val);

	/* Program lane clock */
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW14(port), 0x00760018);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW23(port), 0x00400888);

	mutex_unlock(&dev_priv->sb_lock);
}

void vlv_phy_reset_lanes(struct intel_encoder *encoder)
{
	struct intel_digital_port *dport = enc_to_dig_port(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

	mutex_lock(&dev_priv->sb_lock);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW0(port), 0x00000000);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW1(port), 0x00e00060);
	mutex_unlock(&dev_priv->sb_lock);
}
