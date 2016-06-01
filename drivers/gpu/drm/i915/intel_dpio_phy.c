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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = dev->dev_private;
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
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_crtc *intel_crtc =
		to_intel_crtc(encoder->base.crtc);
	enum dpio_channel port = vlv_dport_to_channel(dport);
	int pipe = intel_crtc->pipe;

	mutex_lock(&dev_priv->sb_lock);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW0(port), 0x00000000);
	vlv_dpio_write(dev_priv, pipe, VLV_PCS_DW1(port), 0x00e00060);
	mutex_unlock(&dev_priv->sb_lock);
}
