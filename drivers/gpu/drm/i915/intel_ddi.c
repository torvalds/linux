/*
 * Copyright © 2012 Intel Corporation
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
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

#include "i915_drv.h"
#include "intel_drv.h"

struct ddi_buf_trans {
	u32 trans1;	/* balance leg enable, de-emph level */
	u32 trans2;	/* vref sel, vswing */
};

/* HDMI/DVI modes ignore everything but the last 2 items. So we share
 * them for both DP and FDI transports, allowing those ports to
 * automatically adapt to HDMI connections as well
 */
static const struct ddi_buf_trans hsw_ddi_translations_dp[] = {
	{ 0x00FFFFFF, 0x0006000E },
	{ 0x00D75FFF, 0x0005000A },
	{ 0x00C30FFF, 0x00040006 },
	{ 0x80AAAFFF, 0x000B0000 },
	{ 0x00FFFFFF, 0x0005000A },
	{ 0x00D75FFF, 0x000C0004 },
	{ 0x80C30FFF, 0x000B0000 },
	{ 0x00FFFFFF, 0x00040006 },
	{ 0x80D75FFF, 0x000B0000 },
};

static const struct ddi_buf_trans hsw_ddi_translations_fdi[] = {
	{ 0x00FFFFFF, 0x0007000E },
	{ 0x00D75FFF, 0x000F000A },
	{ 0x00C30FFF, 0x00060006 },
	{ 0x00AAAFFF, 0x001E0000 },
	{ 0x00FFFFFF, 0x000F000A },
	{ 0x00D75FFF, 0x00160004 },
	{ 0x00C30FFF, 0x001E0000 },
	{ 0x00FFFFFF, 0x00060006 },
	{ 0x00D75FFF, 0x001E0000 },
};

static const struct ddi_buf_trans hsw_ddi_translations_hdmi[] = {
					/* Idx	NT mV d	T mV d	db	*/
	{ 0x00FFFFFF, 0x0006000E },	/* 0:	400	400	0	*/
	{ 0x00E79FFF, 0x000E000C },	/* 1:	400	500	2	*/
	{ 0x00D75FFF, 0x0005000A },	/* 2:	400	600	3.5	*/
	{ 0x00FFFFFF, 0x0005000A },	/* 3:	600	600	0	*/
	{ 0x00E79FFF, 0x001D0007 },	/* 4:	600	750	2	*/
	{ 0x00D75FFF, 0x000C0004 },	/* 5:	600	900	3.5	*/
	{ 0x00FFFFFF, 0x00040006 },	/* 6:	800	800	0	*/
	{ 0x80E79FFF, 0x00030002 },	/* 7:	800	1000	2	*/
	{ 0x00FFFFFF, 0x00140005 },	/* 8:	850	850	0	*/
	{ 0x00FFFFFF, 0x000C0004 },	/* 9:	900	900	0	*/
	{ 0x00FFFFFF, 0x001C0003 },	/* 10:	950	950	0	*/
	{ 0x80FFFFFF, 0x00030002 },	/* 11:	1000	1000	0	*/
};

static const struct ddi_buf_trans bdw_ddi_translations_edp[] = {
	{ 0x00FFFFFF, 0x00000012 },
	{ 0x00EBAFFF, 0x00020011 },
	{ 0x00C71FFF, 0x0006000F },
	{ 0x00AAAFFF, 0x000E000A },
	{ 0x00FFFFFF, 0x00020011 },
	{ 0x00DB6FFF, 0x0005000F },
	{ 0x00BEEFFF, 0x000A000C },
	{ 0x00FFFFFF, 0x0005000F },
	{ 0x00DB6FFF, 0x000A000C },
};

static const struct ddi_buf_trans bdw_ddi_translations_dp[] = {
	{ 0x00FFFFFF, 0x0007000E },
	{ 0x00D75FFF, 0x000E000A },
	{ 0x00BEFFFF, 0x00140006 },
	{ 0x80B2CFFF, 0x001B0002 },
	{ 0x00FFFFFF, 0x000E000A },
	{ 0x00DB6FFF, 0x00160005 },
	{ 0x80C71FFF, 0x001A0002 },
	{ 0x00F7DFFF, 0x00180004 },
	{ 0x80D75FFF, 0x001B0002 },
};

static const struct ddi_buf_trans bdw_ddi_translations_fdi[] = {
	{ 0x00FFFFFF, 0x0001000E },
	{ 0x00D75FFF, 0x0004000A },
	{ 0x00C30FFF, 0x00070006 },
	{ 0x00AAAFFF, 0x000C0000 },
	{ 0x00FFFFFF, 0x0004000A },
	{ 0x00D75FFF, 0x00090004 },
	{ 0x00C30FFF, 0x000C0000 },
	{ 0x00FFFFFF, 0x00070006 },
	{ 0x00D75FFF, 0x000C0000 },
};

static const struct ddi_buf_trans bdw_ddi_translations_hdmi[] = {
					/* Idx	NT mV d	T mV df	db	*/
	{ 0x00FFFFFF, 0x0007000E },	/* 0:	400	400	0	*/
	{ 0x00D75FFF, 0x000E000A },	/* 1:	400	600	3.5	*/
	{ 0x00BEFFFF, 0x00140006 },	/* 2:	400	800	6	*/
	{ 0x00FFFFFF, 0x0009000D },	/* 3:	450	450	0	*/
	{ 0x00FFFFFF, 0x000E000A },	/* 4:	600	600	0	*/
	{ 0x00D7FFFF, 0x00140006 },	/* 5:	600	800	2.5	*/
	{ 0x80CB2FFF, 0x001B0002 },	/* 6:	600	1000	4.5	*/
	{ 0x00FFFFFF, 0x00140006 },	/* 7:	800	800	0	*/
	{ 0x80E79FFF, 0x001B0002 },	/* 8:	800	1000	2	*/
	{ 0x80FFFFFF, 0x001B0002 },	/* 9:	1000	1000	0	*/
};

static const struct ddi_buf_trans skl_ddi_translations_dp[] = {
	{ 0x00000018, 0x000000a2 },
	{ 0x00004014, 0x0000009B },
	{ 0x00006012, 0x00000088 },
	{ 0x00008010, 0x00000087 },
	{ 0x00000018, 0x0000009B },
	{ 0x00004014, 0x00000088 },
	{ 0x00006012, 0x00000087 },
	{ 0x00000018, 0x00000088 },
	{ 0x00004014, 0x00000087 },
};

/* eDP 1.4 low vswing translation parameters */
static const struct ddi_buf_trans skl_ddi_translations_edp[] = {
	{ 0x00000018, 0x000000a8 },
	{ 0x00002016, 0x000000ab },
	{ 0x00006012, 0x000000a2 },
	{ 0x00008010, 0x00000088 },
	{ 0x00000018, 0x000000ab },
	{ 0x00004014, 0x000000a2 },
	{ 0x00006012, 0x000000a6 },
	{ 0x00000018, 0x000000a2 },
	{ 0x00005013, 0x0000009c },
	{ 0x00000018, 0x00000088 },
};


static const struct ddi_buf_trans skl_ddi_translations_hdmi[] = {
	{ 0x00000018, 0x000000ac },
	{ 0x00005012, 0x0000009d },
	{ 0x00007011, 0x00000088 },
	{ 0x00000018, 0x000000a1 },
	{ 0x00000018, 0x00000098 },
	{ 0x00004013, 0x00000088 },
	{ 0x00006012, 0x00000087 },
	{ 0x00000018, 0x000000df },
	{ 0x00003015, 0x00000087 },
	{ 0x00003015, 0x000000c7 },
	{ 0x00000018, 0x000000c7 },
};

struct bxt_ddi_buf_trans {
	u32 margin;	/* swing value */
	u32 scale;	/* scale value */
	u32 enable;	/* scale enable */
	u32 deemphasis;
	bool default_index; /* true if the entry represents default value */
};

/* BSpec does not define separate vswing/pre-emphasis values for eDP.
 * Using DP values for eDP as well.
 */
static const struct bxt_ddi_buf_trans bxt_ddi_translations_dp[] = {
					/* Idx	NT mV diff	db  */
	{ 52,  0,    0, 128, true  },	/* 0:	400		0   */
	{ 78,  0,    0, 85,  false },	/* 1:	400		3.5 */
	{ 104, 0,    0, 64,  false },	/* 2:	400		6   */
	{ 154, 0,    0, 43,  false },	/* 3:	400		9.5 */
	{ 77,  0,    0, 128, false },	/* 4:	600		0   */
	{ 116, 0,    0, 85,  false },	/* 5:	600		3.5 */
	{ 154, 0,    0, 64,  false },	/* 6:	600		6   */
	{ 102, 0,    0, 128, false },	/* 7:	800		0   */
	{ 154, 0,    0, 85,  false },	/* 8:	800		3.5 */
	{ 154, 0x9A, 1, 128, false },  /* 9:	1200		0   */
};

/* BSpec has 2 recommended values - entries 0 and 8.
 * Using the entry with higher vswing.
 */
static const struct bxt_ddi_buf_trans bxt_ddi_translations_hdmi[] = {
					/* Idx	NT mV diff	db  */
	{ 52,  0,    0, 128, false },	/* 0:	400		0   */
	{ 52,  0,    0, 85,  false },	/* 1:	400		3.5 */
	{ 52,  0,    0, 64,  false },	/* 2:	400		6   */
	{ 42,  0,    0, 43,  false },	/* 3:	400		9.5 */
	{ 77,  0,    0, 128, false },	/* 4:	600		0   */
	{ 77,  0,    0, 85,  false },	/* 5:	600		3.5 */
	{ 77,  0,    0, 64,  false },	/* 6:	600		6   */
	{ 102, 0,    0, 128, false },	/* 7:	800		0   */
	{ 102, 0,    0, 85,  false },	/* 8:	800		3.5 */
	{ 154, 0x9A, 1, 128, true },	/* 9:	1200		0   */
};

static void ddi_get_encoder_port(struct intel_encoder *intel_encoder,
				 struct intel_digital_port **dig_port,
				 enum port *port)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	int type = intel_encoder->type;

	if (type == INTEL_OUTPUT_DP_MST) {
		*dig_port = enc_to_mst(encoder)->primary;
		*port = (*dig_port)->port;
	} else if (type == INTEL_OUTPUT_DISPLAYPORT || type == INTEL_OUTPUT_EDP ||
	    type == INTEL_OUTPUT_HDMI || type == INTEL_OUTPUT_UNKNOWN) {
		*dig_port = enc_to_dig_port(encoder);
		*port = (*dig_port)->port;
	} else if (type == INTEL_OUTPUT_ANALOG) {
		*dig_port = NULL;
		*port = PORT_E;
	} else {
		DRM_ERROR("Invalid DDI encoder type %d\n", type);
		BUG();
	}
}

enum port intel_ddi_get_encoder_port(struct intel_encoder *intel_encoder)
{
	struct intel_digital_port *dig_port;
	enum port port;

	ddi_get_encoder_port(intel_encoder, &dig_port, &port);

	return port;
}

static bool
intel_dig_port_supports_hdmi(const struct intel_digital_port *intel_dig_port)
{
	return intel_dig_port->hdmi.hdmi_reg;
}

/*
 * Starting with Haswell, DDI port buffers must be programmed with correct
 * values in advance. The buffer values are different for FDI and DP modes,
 * but the HDMI/DVI fields are shared among those. So we program the DDI
 * in either FDI or DP modes only, as HDMI connections will work with both
 * of those
 */
static void intel_prepare_ddi_buffers(struct drm_device *dev, enum port port,
				      bool supports_hdmi)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg;
	int i, n_hdmi_entries, n_dp_entries, n_edp_entries, hdmi_default_entry,
	    size;
	int hdmi_level = dev_priv->vbt.ddi_port_info[port].hdmi_level_shift;
	const struct ddi_buf_trans *ddi_translations_fdi;
	const struct ddi_buf_trans *ddi_translations_dp;
	const struct ddi_buf_trans *ddi_translations_edp;
	const struct ddi_buf_trans *ddi_translations_hdmi;
	const struct ddi_buf_trans *ddi_translations;

	if (IS_BROXTON(dev)) {
		if (!supports_hdmi)
			return;

		/* Vswing programming for HDMI */
		bxt_ddi_vswing_sequence(dev, hdmi_level, port,
					INTEL_OUTPUT_HDMI);
		return;
	} else if (IS_SKYLAKE(dev)) {
		ddi_translations_fdi = NULL;
		ddi_translations_dp = skl_ddi_translations_dp;
		n_dp_entries = ARRAY_SIZE(skl_ddi_translations_dp);
		if (dev_priv->edp_low_vswing) {
			ddi_translations_edp = skl_ddi_translations_edp;
			n_edp_entries = ARRAY_SIZE(skl_ddi_translations_edp);
		} else {
			ddi_translations_edp = skl_ddi_translations_dp;
			n_edp_entries = ARRAY_SIZE(skl_ddi_translations_dp);
		}

		ddi_translations_hdmi = skl_ddi_translations_hdmi;
		n_hdmi_entries = ARRAY_SIZE(skl_ddi_translations_hdmi);
		hdmi_default_entry = 7;
	} else if (IS_BROADWELL(dev)) {
		ddi_translations_fdi = bdw_ddi_translations_fdi;
		ddi_translations_dp = bdw_ddi_translations_dp;
		ddi_translations_edp = bdw_ddi_translations_edp;
		ddi_translations_hdmi = bdw_ddi_translations_hdmi;
		n_edp_entries = ARRAY_SIZE(bdw_ddi_translations_edp);
		n_dp_entries = ARRAY_SIZE(bdw_ddi_translations_dp);
		n_hdmi_entries = ARRAY_SIZE(bdw_ddi_translations_hdmi);
		hdmi_default_entry = 7;
	} else if (IS_HASWELL(dev)) {
		ddi_translations_fdi = hsw_ddi_translations_fdi;
		ddi_translations_dp = hsw_ddi_translations_dp;
		ddi_translations_edp = hsw_ddi_translations_dp;
		ddi_translations_hdmi = hsw_ddi_translations_hdmi;
		n_dp_entries = n_edp_entries = ARRAY_SIZE(hsw_ddi_translations_dp);
		n_hdmi_entries = ARRAY_SIZE(hsw_ddi_translations_hdmi);
		hdmi_default_entry = 6;
	} else {
		WARN(1, "ddi translation table missing\n");
		ddi_translations_edp = bdw_ddi_translations_dp;
		ddi_translations_fdi = bdw_ddi_translations_fdi;
		ddi_translations_dp = bdw_ddi_translations_dp;
		ddi_translations_hdmi = bdw_ddi_translations_hdmi;
		n_edp_entries = ARRAY_SIZE(bdw_ddi_translations_edp);
		n_dp_entries = ARRAY_SIZE(bdw_ddi_translations_dp);
		n_hdmi_entries = ARRAY_SIZE(bdw_ddi_translations_hdmi);
		hdmi_default_entry = 7;
	}

	switch (port) {
	case PORT_A:
		ddi_translations = ddi_translations_edp;
		size = n_edp_entries;
		break;
	case PORT_B:
	case PORT_C:
		ddi_translations = ddi_translations_dp;
		size = n_dp_entries;
		break;
	case PORT_D:
		if (intel_dp_is_edp(dev, PORT_D)) {
			ddi_translations = ddi_translations_edp;
			size = n_edp_entries;
		} else {
			ddi_translations = ddi_translations_dp;
			size = n_dp_entries;
		}
		break;
	case PORT_E:
		if (ddi_translations_fdi)
			ddi_translations = ddi_translations_fdi;
		else
			ddi_translations = ddi_translations_dp;
		size = n_dp_entries;
		break;
	default:
		BUG();
	}

	for (i = 0, reg = DDI_BUF_TRANS(port); i < size; i++) {
		I915_WRITE(reg, ddi_translations[i].trans1);
		reg += 4;
		I915_WRITE(reg, ddi_translations[i].trans2);
		reg += 4;
	}

	if (!supports_hdmi)
		return;

	/* Choose a good default if VBT is badly populated */
	if (hdmi_level == HDMI_LEVEL_SHIFT_UNKNOWN ||
	    hdmi_level >= n_hdmi_entries)
		hdmi_level = hdmi_default_entry;

	/* Entry 9 is for HDMI: */
	I915_WRITE(reg, ddi_translations_hdmi[hdmi_level].trans1);
	reg += 4;
	I915_WRITE(reg, ddi_translations_hdmi[hdmi_level].trans2);
	reg += 4;
}

/* Program DDI buffers translations for DP. By default, program ports A-D in DP
 * mode and port E for FDI.
 */
void intel_prepare_ddi(struct drm_device *dev)
{
	struct intel_encoder *intel_encoder;
	bool visited[I915_MAX_PORTS] = { 0, };

	if (!HAS_DDI(dev))
		return;

	for_each_intel_encoder(dev, intel_encoder) {
		struct intel_digital_port *intel_dig_port;
		enum port port;
		bool supports_hdmi;

		ddi_get_encoder_port(intel_encoder, &intel_dig_port, &port);

		if (visited[port])
			continue;

		supports_hdmi = intel_dig_port &&
				intel_dig_port_supports_hdmi(intel_dig_port);

		intel_prepare_ddi_buffers(dev, port, supports_hdmi);
		visited[port] = true;
	}
}

static void intel_wait_ddi_buf_idle(struct drm_i915_private *dev_priv,
				    enum port port)
{
	uint32_t reg = DDI_BUF_CTL(port);
	int i;

	for (i = 0; i < 16; i++) {
		udelay(1);
		if (I915_READ(reg) & DDI_BUF_IS_IDLE)
			return;
	}
	DRM_ERROR("Timeout waiting for DDI BUF %c idle bit\n", port_name(port));
}

/* Starting with Haswell, different DDI ports can work in FDI mode for
 * connection to the PCH-located connectors. For this, it is necessary to train
 * both the DDI port and PCH receiver for the desired DDI buffer settings.
 *
 * The recommended port to work in FDI mode is DDI E, which we use here. Also,
 * please note that when FDI mode is active on DDI E, it shares 2 lines with
 * DDI A (which is used for eDP)
 */

void hsw_fdi_link_train(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	u32 temp, i, rx_ctl_val;

	/* Set the FDI_RX_MISC pwrdn lanes and the 2 workarounds listed at the
	 * mode set "sequence for CRT port" document:
	 * - TP1 to TP2 time with the default value
	 * - FDI delay to 90h
	 *
	 * WaFDIAutoLinkSetTimingOverrride:hsw
	 */
	I915_WRITE(_FDI_RXA_MISC, FDI_RX_PWRDN_LANE1_VAL(2) |
				  FDI_RX_PWRDN_LANE0_VAL(2) |
				  FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

	/* Enable the PCH Receiver FDI PLL */
	rx_ctl_val = dev_priv->fdi_rx_config | FDI_RX_ENHANCE_FRAME_ENABLE |
		     FDI_RX_PLL_ENABLE |
		     FDI_DP_PORT_WIDTH(intel_crtc->config->fdi_lanes);
	I915_WRITE(_FDI_RXA_CTL, rx_ctl_val);
	POSTING_READ(_FDI_RXA_CTL);
	udelay(220);

	/* Switch from Rawclk to PCDclk */
	rx_ctl_val |= FDI_PCDCLK;
	I915_WRITE(_FDI_RXA_CTL, rx_ctl_val);

	/* Configure Port Clock Select */
	I915_WRITE(PORT_CLK_SEL(PORT_E), intel_crtc->config->ddi_pll_sel);
	WARN_ON(intel_crtc->config->ddi_pll_sel != PORT_CLK_SEL_SPLL);

	/* Start the training iterating through available voltages and emphasis,
	 * testing each value twice. */
	for (i = 0; i < ARRAY_SIZE(hsw_ddi_translations_fdi) * 2; i++) {
		/* Configure DP_TP_CTL with auto-training */
		I915_WRITE(DP_TP_CTL(PORT_E),
					DP_TP_CTL_FDI_AUTOTRAIN |
					DP_TP_CTL_ENHANCED_FRAME_ENABLE |
					DP_TP_CTL_LINK_TRAIN_PAT1 |
					DP_TP_CTL_ENABLE);

		/* Configure and enable DDI_BUF_CTL for DDI E with next voltage.
		 * DDI E does not support port reversal, the functionality is
		 * achieved on the PCH side in FDI_RX_CTL, so no need to set the
		 * port reversal bit */
		I915_WRITE(DDI_BUF_CTL(PORT_E),
			   DDI_BUF_CTL_ENABLE |
			   ((intel_crtc->config->fdi_lanes - 1) << 1) |
			   DDI_BUF_TRANS_SELECT(i / 2));
		POSTING_READ(DDI_BUF_CTL(PORT_E));

		udelay(600);

		/* Program PCH FDI Receiver TU */
		I915_WRITE(_FDI_RXA_TUSIZE1, TU_SIZE(64));

		/* Enable PCH FDI Receiver with auto-training */
		rx_ctl_val |= FDI_RX_ENABLE | FDI_LINK_TRAIN_AUTO;
		I915_WRITE(_FDI_RXA_CTL, rx_ctl_val);
		POSTING_READ(_FDI_RXA_CTL);

		/* Wait for FDI receiver lane calibration */
		udelay(30);

		/* Unset FDI_RX_MISC pwrdn lanes */
		temp = I915_READ(_FDI_RXA_MISC);
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		I915_WRITE(_FDI_RXA_MISC, temp);
		POSTING_READ(_FDI_RXA_MISC);

		/* Wait for FDI auto training time */
		udelay(5);

		temp = I915_READ(DP_TP_STATUS(PORT_E));
		if (temp & DP_TP_STATUS_AUTOTRAIN_DONE) {
			DRM_DEBUG_KMS("FDI link training done on step %d\n", i);

			/* Enable normal pixel sending for FDI */
			I915_WRITE(DP_TP_CTL(PORT_E),
				   DP_TP_CTL_FDI_AUTOTRAIN |
				   DP_TP_CTL_LINK_TRAIN_NORMAL |
				   DP_TP_CTL_ENHANCED_FRAME_ENABLE |
				   DP_TP_CTL_ENABLE);

			return;
		}

		temp = I915_READ(DDI_BUF_CTL(PORT_E));
		temp &= ~DDI_BUF_CTL_ENABLE;
		I915_WRITE(DDI_BUF_CTL(PORT_E), temp);
		POSTING_READ(DDI_BUF_CTL(PORT_E));

		/* Disable DP_TP_CTL and FDI_RX_CTL and retry */
		temp = I915_READ(DP_TP_CTL(PORT_E));
		temp &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
		I915_WRITE(DP_TP_CTL(PORT_E), temp);
		POSTING_READ(DP_TP_CTL(PORT_E));

		intel_wait_ddi_buf_idle(dev_priv, PORT_E);

		rx_ctl_val &= ~FDI_RX_ENABLE;
		I915_WRITE(_FDI_RXA_CTL, rx_ctl_val);
		POSTING_READ(_FDI_RXA_CTL);

		/* Reset FDI_RX_MISC pwrdn lanes */
		temp = I915_READ(_FDI_RXA_MISC);
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		temp |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
		I915_WRITE(_FDI_RXA_MISC, temp);
		POSTING_READ(_FDI_RXA_MISC);
	}

	DRM_ERROR("FDI link training failed!\n");
}

void intel_ddi_init_dp_buf_reg(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(&encoder->base);
	struct intel_digital_port *intel_dig_port =
		enc_to_dig_port(&encoder->base);

	intel_dp->DP = intel_dig_port->saved_port_bits |
		DDI_BUF_CTL_ENABLE | DDI_BUF_TRANS_SELECT(0);
	intel_dp->DP |= DDI_PORT_WIDTH(intel_dp->lane_count);

}

static struct intel_encoder *
intel_ddi_get_crtc_encoder(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder, *ret = NULL;
	int num_encoders = 0;

	for_each_encoder_on_crtc(dev, crtc, intel_encoder) {
		ret = intel_encoder;
		num_encoders++;
	}

	if (num_encoders != 1)
		WARN(1, "%d encoders on crtc for pipe %c\n", num_encoders,
		     pipe_name(intel_crtc->pipe));

	BUG_ON(ret == NULL);
	return ret;
}

struct intel_encoder *
intel_ddi_get_crtc_new_encoder(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_encoder *ret = NULL;
	struct drm_atomic_state *state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	int num_encoders = 0;
	int i;

	state = crtc_state->base.state;

	for_each_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc != crtc_state->base.crtc)
			continue;

		ret = to_intel_encoder(connector_state->best_encoder);
		num_encoders++;
	}

	WARN(num_encoders != 1, "%d encoders on crtc for pipe %c\n", num_encoders,
	     pipe_name(crtc->pipe));

	BUG_ON(ret == NULL);
	return ret;
}

#define LC_FREQ 2700
#define LC_FREQ_2K U64_C(LC_FREQ * 2000)

#define P_MIN 2
#define P_MAX 64
#define P_INC 2

/* Constraints for PLL good behavior */
#define REF_MIN 48
#define REF_MAX 400
#define VCO_MIN 2400
#define VCO_MAX 4800

#define abs_diff(a, b) ({			\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	(void) (&__a == &__b);			\
	__a > __b ? (__a - __b) : (__b - __a); })

struct wrpll_rnp {
	unsigned p, n2, r2;
};

static unsigned wrpll_get_budget_for_freq(int clock)
{
	unsigned budget;

	switch (clock) {
	case 25175000:
	case 25200000:
	case 27000000:
	case 27027000:
	case 37762500:
	case 37800000:
	case 40500000:
	case 40541000:
	case 54000000:
	case 54054000:
	case 59341000:
	case 59400000:
	case 72000000:
	case 74176000:
	case 74250000:
	case 81000000:
	case 81081000:
	case 89012000:
	case 89100000:
	case 108000000:
	case 108108000:
	case 111264000:
	case 111375000:
	case 148352000:
	case 148500000:
	case 162000000:
	case 162162000:
	case 222525000:
	case 222750000:
	case 296703000:
	case 297000000:
		budget = 0;
		break;
	case 233500000:
	case 245250000:
	case 247750000:
	case 253250000:
	case 298000000:
		budget = 1500;
		break;
	case 169128000:
	case 169500000:
	case 179500000:
	case 202000000:
		budget = 2000;
		break;
	case 256250000:
	case 262500000:
	case 270000000:
	case 272500000:
	case 273750000:
	case 280750000:
	case 281250000:
	case 286000000:
	case 291750000:
		budget = 4000;
		break;
	case 267250000:
	case 268500000:
		budget = 5000;
		break;
	default:
		budget = 1000;
		break;
	}

	return budget;
}

static void wrpll_update_rnp(uint64_t freq2k, unsigned budget,
			     unsigned r2, unsigned n2, unsigned p,
			     struct wrpll_rnp *best)
{
	uint64_t a, b, c, d, diff, diff_best;

	/* No best (r,n,p) yet */
	if (best->p == 0) {
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
		return;
	}

	/*
	 * Output clock is (LC_FREQ_2K / 2000) * N / (P * R), which compares to
	 * freq2k.
	 *
	 * delta = 1e6 *
	 *	   abs(freq2k - (LC_FREQ_2K * n2/(p * r2))) /
	 *	   freq2k;
	 *
	 * and we would like delta <= budget.
	 *
	 * If the discrepancy is above the PPM-based budget, always prefer to
	 * improve upon the previous solution.  However, if you're within the
	 * budget, try to maximize Ref * VCO, that is N / (P * R^2).
	 */
	a = freq2k * budget * p * r2;
	b = freq2k * budget * best->p * best->r2;
	diff = abs_diff(freq2k * p * r2, LC_FREQ_2K * n2);
	diff_best = abs_diff(freq2k * best->p * best->r2,
			     LC_FREQ_2K * best->n2);
	c = 1000000 * diff;
	d = 1000000 * diff_best;

	if (a < c && b < d) {
		/* If both are above the budget, pick the closer */
		if (best->p * best->r2 * diff < p * r2 * diff_best) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	} else if (a >= c && b < d) {
		/* If A is below the threshold but B is above it?  Update. */
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
	} else if (a >= c && b >= d) {
		/* Both are below the limit, so pick the higher n2/(r2*r2) */
		if (n2 * best->r2 * best->r2 > best->n2 * r2 * r2) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	}
	/* Otherwise a < c && b >= d, do nothing */
}

static int intel_ddi_calc_wrpll_link(struct drm_i915_private *dev_priv,
				     int reg)
{
	int refclk = LC_FREQ;
	int n, p, r;
	u32 wrpll;

	wrpll = I915_READ(reg);
	switch (wrpll & WRPLL_PLL_REF_MASK) {
	case WRPLL_PLL_SSC:
	case WRPLL_PLL_NON_SSC:
		/*
		 * We could calculate spread here, but our checking
		 * code only cares about 5% accuracy, and spread is a max of
		 * 0.5% downspread.
		 */
		refclk = 135;
		break;
	case WRPLL_PLL_LCPLL:
		refclk = LC_FREQ;
		break;
	default:
		WARN(1, "bad wrpll refclk\n");
		return 0;
	}

	r = wrpll & WRPLL_DIVIDER_REF_MASK;
	p = (wrpll & WRPLL_DIVIDER_POST_MASK) >> WRPLL_DIVIDER_POST_SHIFT;
	n = (wrpll & WRPLL_DIVIDER_FB_MASK) >> WRPLL_DIVIDER_FB_SHIFT;

	/* Convert to KHz, p & r have a fixed point portion */
	return (refclk * n * 100) / (p * r);
}

static int skl_calc_wrpll_link(struct drm_i915_private *dev_priv,
			       uint32_t dpll)
{
	uint32_t cfgcr1_reg, cfgcr2_reg;
	uint32_t cfgcr1_val, cfgcr2_val;
	uint32_t p0, p1, p2, dco_freq;

	cfgcr1_reg = GET_CFG_CR1_REG(dpll);
	cfgcr2_reg = GET_CFG_CR2_REG(dpll);

	cfgcr1_val = I915_READ(cfgcr1_reg);
	cfgcr2_val = I915_READ(cfgcr2_reg);

	p0 = cfgcr2_val & DPLL_CFGCR2_PDIV_MASK;
	p2 = cfgcr2_val & DPLL_CFGCR2_KDIV_MASK;

	if (cfgcr2_val &  DPLL_CFGCR2_QDIV_MODE(1))
		p1 = (cfgcr2_val & DPLL_CFGCR2_QDIV_RATIO_MASK) >> 8;
	else
		p1 = 1;


	switch (p0) {
	case DPLL_CFGCR2_PDIV_1:
		p0 = 1;
		break;
	case DPLL_CFGCR2_PDIV_2:
		p0 = 2;
		break;
	case DPLL_CFGCR2_PDIV_3:
		p0 = 3;
		break;
	case DPLL_CFGCR2_PDIV_7:
		p0 = 7;
		break;
	}

	switch (p2) {
	case DPLL_CFGCR2_KDIV_5:
		p2 = 5;
		break;
	case DPLL_CFGCR2_KDIV_2:
		p2 = 2;
		break;
	case DPLL_CFGCR2_KDIV_3:
		p2 = 3;
		break;
	case DPLL_CFGCR2_KDIV_1:
		p2 = 1;
		break;
	}

	dco_freq = (cfgcr1_val & DPLL_CFGCR1_DCO_INTEGER_MASK) * 24 * 1000;

	dco_freq += (((cfgcr1_val & DPLL_CFGCR1_DCO_FRACTION_MASK) >> 9) * 24 *
		1000) / 0x8000;

	return dco_freq / (p0 * p1 * p2 * 5);
}


static void skl_ddi_clock_get(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	int link_clock = 0;
	uint32_t dpll_ctl1, dpll;

	dpll = pipe_config->ddi_pll_sel;

	dpll_ctl1 = I915_READ(DPLL_CTRL1);

	if (dpll_ctl1 & DPLL_CTRL1_HDMI_MODE(dpll)) {
		link_clock = skl_calc_wrpll_link(dev_priv, dpll);
	} else {
		link_clock = dpll_ctl1 & DPLL_CTRL1_LINK_RATE_MASK(dpll);
		link_clock >>= DPLL_CTRL1_LINK_RATE_SHIFT(dpll);

		switch (link_clock) {
		case DPLL_CTRL1_LINK_RATE_810:
			link_clock = 81000;
			break;
		case DPLL_CTRL1_LINK_RATE_1080:
			link_clock = 108000;
			break;
		case DPLL_CTRL1_LINK_RATE_1350:
			link_clock = 135000;
			break;
		case DPLL_CTRL1_LINK_RATE_1620:
			link_clock = 162000;
			break;
		case DPLL_CTRL1_LINK_RATE_2160:
			link_clock = 216000;
			break;
		case DPLL_CTRL1_LINK_RATE_2700:
			link_clock = 270000;
			break;
		default:
			WARN(1, "Unsupported link rate\n");
			break;
		}
		link_clock *= 2;
	}

	pipe_config->port_clock = link_clock;

	if (pipe_config->has_dp_encoder)
		pipe_config->base.adjusted_mode.crtc_clock =
			intel_dotclock_calculate(pipe_config->port_clock,
						 &pipe_config->dp_m_n);
	else
		pipe_config->base.adjusted_mode.crtc_clock = pipe_config->port_clock;
}

static void hsw_ddi_clock_get(struct intel_encoder *encoder,
			      struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	int link_clock = 0;
	u32 val, pll;

	val = pipe_config->ddi_pll_sel;
	switch (val & PORT_CLK_SEL_MASK) {
	case PORT_CLK_SEL_LCPLL_810:
		link_clock = 81000;
		break;
	case PORT_CLK_SEL_LCPLL_1350:
		link_clock = 135000;
		break;
	case PORT_CLK_SEL_LCPLL_2700:
		link_clock = 270000;
		break;
	case PORT_CLK_SEL_WRPLL1:
		link_clock = intel_ddi_calc_wrpll_link(dev_priv, WRPLL_CTL1);
		break;
	case PORT_CLK_SEL_WRPLL2:
		link_clock = intel_ddi_calc_wrpll_link(dev_priv, WRPLL_CTL2);
		break;
	case PORT_CLK_SEL_SPLL:
		pll = I915_READ(SPLL_CTL) & SPLL_PLL_FREQ_MASK;
		if (pll == SPLL_PLL_FREQ_810MHz)
			link_clock = 81000;
		else if (pll == SPLL_PLL_FREQ_1350MHz)
			link_clock = 135000;
		else if (pll == SPLL_PLL_FREQ_2700MHz)
			link_clock = 270000;
		else {
			WARN(1, "bad spll freq\n");
			return;
		}
		break;
	default:
		WARN(1, "bad port clock sel\n");
		return;
	}

	pipe_config->port_clock = link_clock * 2;

	if (pipe_config->has_pch_encoder)
		pipe_config->base.adjusted_mode.crtc_clock =
			intel_dotclock_calculate(pipe_config->port_clock,
						 &pipe_config->fdi_m_n);
	else if (pipe_config->has_dp_encoder)
		pipe_config->base.adjusted_mode.crtc_clock =
			intel_dotclock_calculate(pipe_config->port_clock,
						 &pipe_config->dp_m_n);
	else
		pipe_config->base.adjusted_mode.crtc_clock = pipe_config->port_clock;
}

static int bxt_calc_pll_link(struct drm_i915_private *dev_priv,
				enum intel_dpll_id dpll)
{
	/* FIXME formula not available in bspec */
	return 0;
}

static void bxt_ddi_clock_get(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	enum port port = intel_ddi_get_encoder_port(encoder);
	uint32_t dpll = port;

	pipe_config->port_clock =
		bxt_calc_pll_link(dev_priv, dpll);

	if (pipe_config->has_dp_encoder)
		pipe_config->base.adjusted_mode.crtc_clock =
			intel_dotclock_calculate(pipe_config->port_clock,
							&pipe_config->dp_m_n);
	else
		pipe_config->base.adjusted_mode.crtc_clock =
							pipe_config->port_clock;
}

void intel_ddi_clock_get(struct intel_encoder *encoder,
			 struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;

	if (INTEL_INFO(dev)->gen <= 8)
		hsw_ddi_clock_get(encoder, pipe_config);
	else if (IS_SKYLAKE(dev))
		skl_ddi_clock_get(encoder, pipe_config);
	else if (IS_BROXTON(dev))
		bxt_ddi_clock_get(encoder, pipe_config);
}

static void
hsw_ddi_calculate_wrpll(int clock /* in Hz */,
			unsigned *r2_out, unsigned *n2_out, unsigned *p_out)
{
	uint64_t freq2k;
	unsigned p, n2, r2;
	struct wrpll_rnp best = { 0, 0, 0 };
	unsigned budget;

	freq2k = clock / 100;

	budget = wrpll_get_budget_for_freq(clock);

	/* Special case handling for 540 pixel clock: bypass WR PLL entirely
	 * and directly pass the LC PLL to it. */
	if (freq2k == 5400000) {
		*n2_out = 2;
		*p_out = 1;
		*r2_out = 2;
		return;
	}

	/*
	 * Ref = LC_FREQ / R, where Ref is the actual reference input seen by
	 * the WR PLL.
	 *
	 * We want R so that REF_MIN <= Ref <= REF_MAX.
	 * Injecting R2 = 2 * R gives:
	 *   REF_MAX * r2 > LC_FREQ * 2 and
	 *   REF_MIN * r2 < LC_FREQ * 2
	 *
	 * Which means the desired boundaries for r2 are:
	 *  LC_FREQ * 2 / REF_MAX < r2 < LC_FREQ * 2 / REF_MIN
	 *
	 */
	for (r2 = LC_FREQ * 2 / REF_MAX + 1;
	     r2 <= LC_FREQ * 2 / REF_MIN;
	     r2++) {

		/*
		 * VCO = N * Ref, that is: VCO = N * LC_FREQ / R
		 *
		 * Once again we want VCO_MIN <= VCO <= VCO_MAX.
		 * Injecting R2 = 2 * R and N2 = 2 * N, we get:
		 *   VCO_MAX * r2 > n2 * LC_FREQ and
		 *   VCO_MIN * r2 < n2 * LC_FREQ)
		 *
		 * Which means the desired boundaries for n2 are:
		 * VCO_MIN * r2 / LC_FREQ < n2 < VCO_MAX * r2 / LC_FREQ
		 */
		for (n2 = VCO_MIN * r2 / LC_FREQ + 1;
		     n2 <= VCO_MAX * r2 / LC_FREQ;
		     n2++) {

			for (p = P_MIN; p <= P_MAX; p += P_INC)
				wrpll_update_rnp(freq2k, budget,
						 r2, n2, p, &best);
		}
	}

	*n2_out = best.n2;
	*p_out = best.p;
	*r2_out = best.r2;
}

static bool
hsw_ddi_pll_select(struct intel_crtc *intel_crtc,
		   struct intel_crtc_state *crtc_state,
		   struct intel_encoder *intel_encoder,
		   int clock)
{
	if (intel_encoder->type == INTEL_OUTPUT_HDMI) {
		struct intel_shared_dpll *pll;
		uint32_t val;
		unsigned p, n2, r2;

		hsw_ddi_calculate_wrpll(clock * 1000, &r2, &n2, &p);

		val = WRPLL_PLL_ENABLE | WRPLL_PLL_LCPLL |
		      WRPLL_DIVIDER_REFERENCE(r2) | WRPLL_DIVIDER_FEEDBACK(n2) |
		      WRPLL_DIVIDER_POST(p);

		memset(&crtc_state->dpll_hw_state, 0,
		       sizeof(crtc_state->dpll_hw_state));

		crtc_state->dpll_hw_state.wrpll = val;

		pll = intel_get_shared_dpll(intel_crtc, crtc_state);
		if (pll == NULL) {
			DRM_DEBUG_DRIVER("failed to find PLL for pipe %c\n",
					 pipe_name(intel_crtc->pipe));
			return false;
		}

		crtc_state->ddi_pll_sel = PORT_CLK_SEL_WRPLL(pll->id);
	}

	return true;
}

struct skl_wrpll_params {
	uint32_t        dco_fraction;
	uint32_t        dco_integer;
	uint32_t        qdiv_ratio;
	uint32_t        qdiv_mode;
	uint32_t        kdiv;
	uint32_t        pdiv;
	uint32_t        central_freq;
};

static void
skl_ddi_calculate_wrpll(int clock /* in Hz */,
			struct skl_wrpll_params *wrpll_params)
{
	uint64_t afe_clock = clock * 5; /* AFE Clock is 5x Pixel clock */
	uint64_t dco_central_freq[3] = {8400000000ULL,
					9000000000ULL,
					9600000000ULL};
	uint32_t min_dco_deviation = 400;
	uint32_t min_dco_index = 3;
	uint32_t P0[4] = {1, 2, 3, 7};
	uint32_t P2[4] = {1, 2, 3, 5};
	bool found = false;
	uint32_t candidate_p = 0;
	uint32_t candidate_p0[3] = {0}, candidate_p1[3] = {0};
	uint32_t candidate_p2[3] = {0};
	uint32_t dco_central_freq_deviation[3];
	uint32_t i, P1, k, dco_count;
	bool retry_with_odd = false;
	uint64_t dco_freq;

	/* Determine P0, P1 or P2 */
	for (dco_count = 0; dco_count < 3; dco_count++) {
		found = false;
		candidate_p =
			div64_u64(dco_central_freq[dco_count], afe_clock);
		if (retry_with_odd == false)
			candidate_p = (candidate_p % 2 == 0 ?
				candidate_p : candidate_p + 1);

		for (P1 = 1; P1 < candidate_p; P1++) {
			for (i = 0; i < 4; i++) {
				if (!(P0[i] != 1 || P1 == 1))
					continue;

				for (k = 0; k < 4; k++) {
					if (P1 != 1 && P2[k] != 2)
						continue;

					if (candidate_p == P0[i] * P1 * P2[k]) {
						/* Found possible P0, P1, P2 */
						found = true;
						candidate_p0[dco_count] = P0[i];
						candidate_p1[dco_count] = P1;
						candidate_p2[dco_count] = P2[k];
						goto found;
					}

				}
			}
		}

found:
		if (found) {
			dco_central_freq_deviation[dco_count] =
				div64_u64(10000 *
					  abs_diff((candidate_p * afe_clock),
						   dco_central_freq[dco_count]),
					  dco_central_freq[dco_count]);

			if (dco_central_freq_deviation[dco_count] <
				min_dco_deviation) {
				min_dco_deviation =
					dco_central_freq_deviation[dco_count];
				min_dco_index = dco_count;
			}
		}

		if (min_dco_index > 2 && dco_count == 2) {
			retry_with_odd = true;
			dco_count = 0;
		}
	}

	if (min_dco_index > 2) {
		WARN(1, "No valid values found for the given pixel clock\n");
	} else {
		wrpll_params->central_freq = dco_central_freq[min_dco_index];

		switch (dco_central_freq[min_dco_index]) {
		case 9600000000ULL:
			wrpll_params->central_freq = 0;
			break;
		case 9000000000ULL:
			wrpll_params->central_freq = 1;
			break;
		case 8400000000ULL:
			wrpll_params->central_freq = 3;
		}

		switch (candidate_p0[min_dco_index]) {
		case 1:
			wrpll_params->pdiv = 0;
			break;
		case 2:
			wrpll_params->pdiv = 1;
			break;
		case 3:
			wrpll_params->pdiv = 2;
			break;
		case 7:
			wrpll_params->pdiv = 4;
			break;
		default:
			WARN(1, "Incorrect PDiv\n");
		}

		switch (candidate_p2[min_dco_index]) {
		case 5:
			wrpll_params->kdiv = 0;
			break;
		case 2:
			wrpll_params->kdiv = 1;
			break;
		case 3:
			wrpll_params->kdiv = 2;
			break;
		case 1:
			wrpll_params->kdiv = 3;
			break;
		default:
			WARN(1, "Incorrect KDiv\n");
		}

		wrpll_params->qdiv_ratio = candidate_p1[min_dco_index];
		wrpll_params->qdiv_mode =
			(wrpll_params->qdiv_ratio == 1) ? 0 : 1;

		dco_freq = candidate_p0[min_dco_index] *
			candidate_p1[min_dco_index] *
			candidate_p2[min_dco_index] * afe_clock;

		/*
		 * Intermediate values are in Hz.
		 * Divide by MHz to match bsepc
		 */
		wrpll_params->dco_integer = div_u64(dco_freq, (24 * MHz(1)));
		wrpll_params->dco_fraction =
			div_u64(((div_u64(dco_freq, 24) -
				  wrpll_params->dco_integer * MHz(1)) * 0x8000), MHz(1));

	}
}


static bool
skl_ddi_pll_select(struct intel_crtc *intel_crtc,
		   struct intel_crtc_state *crtc_state,
		   struct intel_encoder *intel_encoder,
		   int clock)
{
	struct intel_shared_dpll *pll;
	uint32_t ctrl1, cfgcr1, cfgcr2;

	/*
	 * See comment in intel_dpll_hw_state to understand why we always use 0
	 * as the DPLL id in this function.
	 */

	ctrl1 = DPLL_CTRL1_OVERRIDE(0);

	if (intel_encoder->type == INTEL_OUTPUT_HDMI) {
		struct skl_wrpll_params wrpll_params = { 0, };

		ctrl1 |= DPLL_CTRL1_HDMI_MODE(0);

		skl_ddi_calculate_wrpll(clock * 1000, &wrpll_params);

		cfgcr1 = DPLL_CFGCR1_FREQ_ENABLE |
			 DPLL_CFGCR1_DCO_FRACTION(wrpll_params.dco_fraction) |
			 wrpll_params.dco_integer;

		cfgcr2 = DPLL_CFGCR2_QDIV_RATIO(wrpll_params.qdiv_ratio) |
			 DPLL_CFGCR2_QDIV_MODE(wrpll_params.qdiv_mode) |
			 DPLL_CFGCR2_KDIV(wrpll_params.kdiv) |
			 DPLL_CFGCR2_PDIV(wrpll_params.pdiv) |
			 wrpll_params.central_freq;
	} else if (intel_encoder->type == INTEL_OUTPUT_DISPLAYPORT) {
		struct drm_encoder *encoder = &intel_encoder->base;
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		switch (intel_dp->link_bw) {
		case DP_LINK_BW_1_62:
			ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_810, 0);
			break;
		case DP_LINK_BW_2_7:
			ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1350, 0);
			break;
		case DP_LINK_BW_5_4:
			ctrl1 |= DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_2700, 0);
			break;
		}

		cfgcr1 = cfgcr2 = 0;
	} else /* eDP */
		return true;

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	crtc_state->dpll_hw_state.ctrl1 = ctrl1;
	crtc_state->dpll_hw_state.cfgcr1 = cfgcr1;
	crtc_state->dpll_hw_state.cfgcr2 = cfgcr2;

	pll = intel_get_shared_dpll(intel_crtc, crtc_state);
	if (pll == NULL) {
		DRM_DEBUG_DRIVER("failed to find PLL for pipe %c\n",
				 pipe_name(intel_crtc->pipe));
		return false;
	}

	/* shared DPLL id 0 is DPLL 1 */
	crtc_state->ddi_pll_sel = pll->id + 1;

	return true;
}

/* bxt clock parameters */
struct bxt_clk_div {
	uint32_t p1;
	uint32_t p2;
	uint32_t m2_int;
	uint32_t m2_frac;
	bool m2_frac_en;
	uint32_t n;
};

/* pre-calculated values for DP linkrates */
static struct bxt_clk_div bxt_dp_clk_val[7] = {
	/* 162 */ {4, 2, 32, 1677722, 1, 1},
	/* 270 */ {4, 1, 27,       0, 0, 1},
	/* 540 */ {2, 1, 27,       0, 0, 1},
	/* 216 */ {3, 2, 32, 1677722, 1, 1},
	/* 243 */ {4, 1, 24, 1258291, 1, 1},
	/* 324 */ {4, 1, 32, 1677722, 1, 1},
	/* 432 */ {3, 1, 32, 1677722, 1, 1}
};

static bool
bxt_ddi_pll_select(struct intel_crtc *intel_crtc,
		   struct intel_crtc_state *crtc_state,
		   struct intel_encoder *intel_encoder,
		   int clock)
{
	struct intel_shared_dpll *pll;
	struct bxt_clk_div clk_div = {0};
	int vco = 0;
	uint32_t prop_coef, int_coef, gain_ctl, targ_cnt;
	uint32_t dcoampovr_en_h, dco_amp, lanestagger;

	if (intel_encoder->type == INTEL_OUTPUT_HDMI) {
		intel_clock_t best_clock;

		/* Calculate HDMI div */
		/*
		 * FIXME: tie the following calculation into
		 * i9xx_crtc_compute_clock
		 */
		if (!bxt_find_best_dpll(crtc_state, clock, &best_clock)) {
			DRM_DEBUG_DRIVER("no PLL dividers found for clock %d pipe %c\n",
					 clock, pipe_name(intel_crtc->pipe));
			return false;
		}

		clk_div.p1 = best_clock.p1;
		clk_div.p2 = best_clock.p2;
		WARN_ON(best_clock.m1 != 2);
		clk_div.n = best_clock.n;
		clk_div.m2_int = best_clock.m2 >> 22;
		clk_div.m2_frac = best_clock.m2 & ((1 << 22) - 1);
		clk_div.m2_frac_en = clk_div.m2_frac != 0;

		vco = best_clock.vco;
	} else if (intel_encoder->type == INTEL_OUTPUT_DISPLAYPORT ||
			intel_encoder->type == INTEL_OUTPUT_EDP) {
		struct drm_encoder *encoder = &intel_encoder->base;
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		switch (intel_dp->link_bw) {
		case DP_LINK_BW_1_62:
			clk_div = bxt_dp_clk_val[0];
			break;
		case DP_LINK_BW_2_7:
			clk_div = bxt_dp_clk_val[1];
			break;
		case DP_LINK_BW_5_4:
			clk_div = bxt_dp_clk_val[2];
			break;
		default:
			clk_div = bxt_dp_clk_val[0];
			DRM_ERROR("Unknown link rate\n");
		}
		vco = clock * 10 / 2 * clk_div.p1 * clk_div.p2;
	}

	dco_amp = 15;
	dcoampovr_en_h = 0;
	if (vco >= 6200000 && vco <= 6480000) {
		prop_coef = 4;
		int_coef = 9;
		gain_ctl = 3;
		targ_cnt = 8;
	} else if ((vco > 5400000 && vco < 6200000) ||
			(vco >= 4800000 && vco < 5400000)) {
		prop_coef = 5;
		int_coef = 11;
		gain_ctl = 3;
		targ_cnt = 9;
		if (vco >= 4800000 && vco < 5400000)
			dcoampovr_en_h = 1;
	} else if (vco == 5400000) {
		prop_coef = 3;
		int_coef = 8;
		gain_ctl = 1;
		targ_cnt = 9;
	} else {
		DRM_ERROR("Invalid VCO\n");
		return false;
	}

	memset(&crtc_state->dpll_hw_state, 0,
	       sizeof(crtc_state->dpll_hw_state));

	if (clock > 270000)
		lanestagger = 0x18;
	else if (clock > 135000)
		lanestagger = 0x0d;
	else if (clock > 67000)
		lanestagger = 0x07;
	else if (clock > 33000)
		lanestagger = 0x04;
	else
		lanestagger = 0x02;

	crtc_state->dpll_hw_state.ebb0 =
		PORT_PLL_P1(clk_div.p1) | PORT_PLL_P2(clk_div.p2);
	crtc_state->dpll_hw_state.pll0 = clk_div.m2_int;
	crtc_state->dpll_hw_state.pll1 = PORT_PLL_N(clk_div.n);
	crtc_state->dpll_hw_state.pll2 = clk_div.m2_frac;

	if (clk_div.m2_frac_en)
		crtc_state->dpll_hw_state.pll3 =
			PORT_PLL_M2_FRAC_ENABLE;

	crtc_state->dpll_hw_state.pll6 =
		prop_coef | PORT_PLL_INT_COEFF(int_coef);
	crtc_state->dpll_hw_state.pll6 |=
		PORT_PLL_GAIN_CTL(gain_ctl);

	crtc_state->dpll_hw_state.pll8 = targ_cnt;

	if (dcoampovr_en_h)
		crtc_state->dpll_hw_state.pll10 = PORT_PLL_DCO_AMP_OVR_EN_H;

	crtc_state->dpll_hw_state.pll10 |= PORT_PLL_DCO_AMP(dco_amp);

	crtc_state->dpll_hw_state.pcsdw12 =
		LANESTAGGER_STRAP_OVRD | lanestagger;

	pll = intel_get_shared_dpll(intel_crtc, crtc_state);
	if (pll == NULL) {
		DRM_DEBUG_DRIVER("failed to find PLL for pipe %c\n",
			pipe_name(intel_crtc->pipe));
		return false;
	}

	/* shared DPLL id 0 is DPLL A */
	crtc_state->ddi_pll_sel = pll->id;

	return true;
}

/*
 * Tries to find a *shared* PLL for the CRTC and store it in
 * intel_crtc->ddi_pll_sel.
 *
 * For private DPLLs, compute_config() should do the selection for us. This
 * function should be folded into compute_config() eventually.
 */
bool intel_ddi_pll_select(struct intel_crtc *intel_crtc,
			  struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct intel_encoder *intel_encoder =
		intel_ddi_get_crtc_new_encoder(crtc_state);
	int clock = crtc_state->port_clock;

	if (IS_SKYLAKE(dev))
		return skl_ddi_pll_select(intel_crtc, crtc_state,
					  intel_encoder, clock);
	else if (IS_BROXTON(dev))
		return bxt_ddi_pll_select(intel_crtc, crtc_state,
					  intel_encoder, clock);
	else
		return hsw_ddi_pll_select(intel_crtc, crtc_state,
					  intel_encoder, clock);
}

void intel_ddi_set_pipe_settings(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder = intel_ddi_get_crtc_encoder(crtc);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	int type = intel_encoder->type;
	uint32_t temp;

	if (type == INTEL_OUTPUT_DISPLAYPORT || type == INTEL_OUTPUT_EDP || type == INTEL_OUTPUT_DP_MST) {
		temp = TRANS_MSA_SYNC_CLK;
		switch (intel_crtc->config->pipe_bpp) {
		case 18:
			temp |= TRANS_MSA_6_BPC;
			break;
		case 24:
			temp |= TRANS_MSA_8_BPC;
			break;
		case 30:
			temp |= TRANS_MSA_10_BPC;
			break;
		case 36:
			temp |= TRANS_MSA_12_BPC;
			break;
		default:
			BUG();
		}
		I915_WRITE(TRANS_MSA_MISC(cpu_transcoder), temp);
	}
}

void intel_ddi_set_vc_payload_alloc(struct drm_crtc *crtc, bool state)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	uint32_t temp;
	temp = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (state == true)
		temp |= TRANS_DDI_DP_VC_PAYLOAD_ALLOC;
	else
		temp &= ~TRANS_DDI_DP_VC_PAYLOAD_ALLOC;
	I915_WRITE(TRANS_DDI_FUNC_CTL(cpu_transcoder), temp);
}

void intel_ddi_enable_transcoder_func(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder = intel_ddi_get_crtc_encoder(crtc);
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum pipe pipe = intel_crtc->pipe;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	int type = intel_encoder->type;
	uint32_t temp;

	/* Enable TRANS_DDI_FUNC_CTL for the pipe to work in HDMI mode */
	temp = TRANS_DDI_FUNC_ENABLE;
	temp |= TRANS_DDI_SELECT_PORT(port);

	switch (intel_crtc->config->pipe_bpp) {
	case 18:
		temp |= TRANS_DDI_BPC_6;
		break;
	case 24:
		temp |= TRANS_DDI_BPC_8;
		break;
	case 30:
		temp |= TRANS_DDI_BPC_10;
		break;
	case 36:
		temp |= TRANS_DDI_BPC_12;
		break;
	default:
		BUG();
	}

	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_PVSYNC)
		temp |= TRANS_DDI_PVSYNC;
	if (intel_crtc->config->base.adjusted_mode.flags & DRM_MODE_FLAG_PHSYNC)
		temp |= TRANS_DDI_PHSYNC;

	if (cpu_transcoder == TRANSCODER_EDP) {
		switch (pipe) {
		case PIPE_A:
			/* On Haswell, can only use the always-on power well for
			 * eDP when not using the panel fitter, and when not
			 * using motion blur mitigation (which we don't
			 * support). */
			if (IS_HASWELL(dev) &&
			    (intel_crtc->config->pch_pfit.enabled ||
			     intel_crtc->config->pch_pfit.force_thru))
				temp |= TRANS_DDI_EDP_INPUT_A_ONOFF;
			else
				temp |= TRANS_DDI_EDP_INPUT_A_ON;
			break;
		case PIPE_B:
			temp |= TRANS_DDI_EDP_INPUT_B_ONOFF;
			break;
		case PIPE_C:
			temp |= TRANS_DDI_EDP_INPUT_C_ONOFF;
			break;
		default:
			BUG();
			break;
		}
	}

	if (type == INTEL_OUTPUT_HDMI) {
		if (intel_crtc->config->has_hdmi_sink)
			temp |= TRANS_DDI_MODE_SELECT_HDMI;
		else
			temp |= TRANS_DDI_MODE_SELECT_DVI;

	} else if (type == INTEL_OUTPUT_ANALOG) {
		temp |= TRANS_DDI_MODE_SELECT_FDI;
		temp |= (intel_crtc->config->fdi_lanes - 1) << 1;

	} else if (type == INTEL_OUTPUT_DISPLAYPORT ||
		   type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (intel_dp->is_mst) {
			temp |= TRANS_DDI_MODE_SELECT_DP_MST;
		} else
			temp |= TRANS_DDI_MODE_SELECT_DP_SST;

		temp |= DDI_PORT_WIDTH(intel_dp->lane_count);
	} else if (type == INTEL_OUTPUT_DP_MST) {
		struct intel_dp *intel_dp = &enc_to_mst(encoder)->primary->dp;

		if (intel_dp->is_mst) {
			temp |= TRANS_DDI_MODE_SELECT_DP_MST;
		} else
			temp |= TRANS_DDI_MODE_SELECT_DP_SST;

		temp |= DDI_PORT_WIDTH(intel_dp->lane_count);
	} else {
		WARN(1, "Invalid encoder type %d for pipe %c\n",
		     intel_encoder->type, pipe_name(pipe));
	}

	I915_WRITE(TRANS_DDI_FUNC_CTL(cpu_transcoder), temp);
}

void intel_ddi_disable_transcoder_func(struct drm_i915_private *dev_priv,
				       enum transcoder cpu_transcoder)
{
	uint32_t reg = TRANS_DDI_FUNC_CTL(cpu_transcoder);
	uint32_t val = I915_READ(reg);

	val &= ~(TRANS_DDI_FUNC_ENABLE | TRANS_DDI_PORT_MASK | TRANS_DDI_DP_VC_PAYLOAD_ALLOC);
	val |= TRANS_DDI_PORT_NONE;
	I915_WRITE(reg, val);
}

bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector)
{
	struct drm_device *dev = intel_connector->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = intel_connector->encoder;
	int type = intel_connector->base.connector_type;
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	enum pipe pipe = 0;
	enum transcoder cpu_transcoder;
	enum intel_display_power_domain power_domain;
	uint32_t tmp;

	power_domain = intel_display_port_power_domain(intel_encoder);
	if (!intel_display_power_is_enabled(dev_priv, power_domain))
		return false;

	if (!intel_encoder->get_hw_state(intel_encoder, &pipe))
		return false;

	if (port == PORT_A)
		cpu_transcoder = TRANSCODER_EDP;
	else
		cpu_transcoder = (enum transcoder) pipe;

	tmp = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));

	switch (tmp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
	case TRANS_DDI_MODE_SELECT_DVI:
		return (type == DRM_MODE_CONNECTOR_HDMIA);

	case TRANS_DDI_MODE_SELECT_DP_SST:
		if (type == DRM_MODE_CONNECTOR_eDP)
			return true;
		return (type == DRM_MODE_CONNECTOR_DisplayPort);
	case TRANS_DDI_MODE_SELECT_DP_MST:
		/* if the transcoder is in MST state then
		 * connector isn't connected */
		return false;

	case TRANS_DDI_MODE_SELECT_FDI:
		return (type == DRM_MODE_CONNECTOR_VGA);

	default:
		return false;
	}
}

bool intel_ddi_get_hw_state(struct intel_encoder *encoder,
			    enum pipe *pipe)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_ddi_get_encoder_port(encoder);
	enum intel_display_power_domain power_domain;
	u32 tmp;
	int i;

	power_domain = intel_display_port_power_domain(encoder);
	if (!intel_display_power_is_enabled(dev_priv, power_domain))
		return false;

	tmp = I915_READ(DDI_BUF_CTL(port));

	if (!(tmp & DDI_BUF_CTL_ENABLE))
		return false;

	if (port == PORT_A) {
		tmp = I915_READ(TRANS_DDI_FUNC_CTL(TRANSCODER_EDP));

		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		case TRANS_DDI_EDP_INPUT_A_ON:
		case TRANS_DDI_EDP_INPUT_A_ONOFF:
			*pipe = PIPE_A;
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			*pipe = PIPE_B;
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			*pipe = PIPE_C;
			break;
		}

		return true;
	} else {
		for (i = TRANSCODER_A; i <= TRANSCODER_C; i++) {
			tmp = I915_READ(TRANS_DDI_FUNC_CTL(i));

			if ((tmp & TRANS_DDI_PORT_MASK)
			    == TRANS_DDI_SELECT_PORT(port)) {
				if ((tmp & TRANS_DDI_MODE_SELECT_MASK) == TRANS_DDI_MODE_SELECT_DP_MST)
					return false;

				*pipe = i;
				return true;
			}
		}
	}

	DRM_DEBUG_KMS("No pipe for ddi port %c found\n", port_name(port));

	return false;
}

void intel_ddi_enable_pipe_clock(struct intel_crtc *intel_crtc)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;
	struct intel_encoder *intel_encoder = intel_ddi_get_crtc_encoder(crtc);
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	if (cpu_transcoder != TRANSCODER_EDP)
		I915_WRITE(TRANS_CLK_SEL(cpu_transcoder),
			   TRANS_CLK_SEL_PORT(port));
}

void intel_ddi_disable_pipe_clock(struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = intel_crtc->base.dev->dev_private;
	enum transcoder cpu_transcoder = intel_crtc->config->cpu_transcoder;

	if (cpu_transcoder != TRANSCODER_EDP)
		I915_WRITE(TRANS_CLK_SEL(cpu_transcoder),
			   TRANS_CLK_SEL_DISABLED);
}

void bxt_ddi_vswing_sequence(struct drm_device *dev, u32 level,
			     enum port port, int type)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	const struct bxt_ddi_buf_trans *ddi_translations;
	u32 n_entries, i;
	uint32_t val;

	if (type == INTEL_OUTPUT_DISPLAYPORT || type == INTEL_OUTPUT_EDP) {
		n_entries = ARRAY_SIZE(bxt_ddi_translations_dp);
		ddi_translations = bxt_ddi_translations_dp;
	} else if (type == INTEL_OUTPUT_HDMI) {
		n_entries = ARRAY_SIZE(bxt_ddi_translations_hdmi);
		ddi_translations = bxt_ddi_translations_hdmi;
	} else {
		DRM_DEBUG_KMS("Vswing programming not done for encoder %d\n",
				type);
		return;
	}

	/* Check if default value has to be used */
	if (level >= n_entries ||
	    (type == INTEL_OUTPUT_HDMI && level == HDMI_LEVEL_SHIFT_UNKNOWN)) {
		for (i = 0; i < n_entries; i++) {
			if (ddi_translations[i].default_index) {
				level = i;
				break;
			}
		}
	}

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers and we pick lanes 0/1 for that.
	 */
	val = I915_READ(BXT_PORT_PCS_DW10_LN01(port));
	val &= ~(TX2_SWING_CALC_INIT | TX1_SWING_CALC_INIT);
	I915_WRITE(BXT_PORT_PCS_DW10_GRP(port), val);

	val = I915_READ(BXT_PORT_TX_DW2_LN0(port));
	val &= ~(MARGIN_000 | UNIQ_TRANS_SCALE);
	val |= ddi_translations[level].margin << MARGIN_000_SHIFT |
	       ddi_translations[level].scale << UNIQ_TRANS_SCALE_SHIFT;
	I915_WRITE(BXT_PORT_TX_DW2_GRP(port), val);

	val = I915_READ(BXT_PORT_TX_DW3_LN0(port));
	val &= ~UNIQE_TRANGE_EN_METHOD;
	if (ddi_translations[level].enable)
		val |= UNIQE_TRANGE_EN_METHOD;
	I915_WRITE(BXT_PORT_TX_DW3_GRP(port), val);

	val = I915_READ(BXT_PORT_TX_DW4_LN0(port));
	val &= ~DE_EMPHASIS;
	val |= ddi_translations[level].deemphasis << DEEMPH_SHIFT;
	I915_WRITE(BXT_PORT_TX_DW4_GRP(port), val);

	val = I915_READ(BXT_PORT_PCS_DW10_LN01(port));
	val |= TX2_SWING_CALC_INIT | TX1_SWING_CALC_INIT;
	I915_WRITE(BXT_PORT_PCS_DW10_GRP(port), val);
}

static void intel_ddi_pre_enable(struct intel_encoder *intel_encoder)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *crtc = to_intel_crtc(encoder->crtc);
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	int type = intel_encoder->type;
	int hdmi_level;

	if (type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
		intel_edp_panel_on(intel_dp);
	}

	if (IS_SKYLAKE(dev)) {
		uint32_t dpll = crtc->config->ddi_pll_sel;
		uint32_t val;

		/*
		 * DPLL0 is used for eDP and is the only "private" DPLL (as
		 * opposed to shared) on SKL
		 */
		if (type == INTEL_OUTPUT_EDP) {
			WARN_ON(dpll != SKL_DPLL0);

			val = I915_READ(DPLL_CTRL1);

			val &= ~(DPLL_CTRL1_HDMI_MODE(dpll) |
				 DPLL_CTRL1_SSC(dpll) |
				 DPLL_CTRL1_LINK_RATE_MASK(dpll));
			val |= crtc->config->dpll_hw_state.ctrl1 << (dpll * 6);

			I915_WRITE(DPLL_CTRL1, val);
			POSTING_READ(DPLL_CTRL1);
		}

		/* DDI -> PLL mapping  */
		val = I915_READ(DPLL_CTRL2);

		val &= ~(DPLL_CTRL2_DDI_CLK_OFF(port) |
			DPLL_CTRL2_DDI_CLK_SEL_MASK(port));
		val |= (DPLL_CTRL2_DDI_CLK_SEL(dpll, port) |
			DPLL_CTRL2_DDI_SEL_OVERRIDE(port));

		I915_WRITE(DPLL_CTRL2, val);

	} else if (INTEL_INFO(dev)->gen < 9) {
		WARN_ON(crtc->config->ddi_pll_sel == PORT_CLK_SEL_NONE);
		I915_WRITE(PORT_CLK_SEL(port), crtc->config->ddi_pll_sel);
	}

	if (type == INTEL_OUTPUT_DISPLAYPORT || type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		intel_ddi_init_dp_buf_reg(intel_encoder);

		intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);
		intel_dp_start_link_train(intel_dp);
		intel_dp_complete_link_train(intel_dp);
		if (port != PORT_A || INTEL_INFO(dev)->gen >= 9)
			intel_dp_stop_link_train(intel_dp);
	} else if (type == INTEL_OUTPUT_HDMI) {
		struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(encoder);

		if (IS_BROXTON(dev)) {
			hdmi_level = dev_priv->vbt.
				ddi_port_info[port].hdmi_level_shift;
			bxt_ddi_vswing_sequence(dev, hdmi_level, port,
					INTEL_OUTPUT_HDMI);
		}
		intel_hdmi->set_infoframes(encoder,
					   crtc->config->has_hdmi_sink,
					   &crtc->config->base.adjusted_mode);
	}
}

static void intel_ddi_post_disable(struct intel_encoder *intel_encoder)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	int type = intel_encoder->type;
	uint32_t val;
	bool wait = false;

	val = I915_READ(DDI_BUF_CTL(port));
	if (val & DDI_BUF_CTL_ENABLE) {
		val &= ~DDI_BUF_CTL_ENABLE;
		I915_WRITE(DDI_BUF_CTL(port), val);
		wait = true;
	}

	val = I915_READ(DP_TP_CTL(port));
	val &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
	val |= DP_TP_CTL_LINK_TRAIN_PAT1;
	I915_WRITE(DP_TP_CTL(port), val);

	if (wait)
		intel_wait_ddi_buf_idle(dev_priv, port);

	if (type == INTEL_OUTPUT_DISPLAYPORT || type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
		intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_OFF);
		intel_edp_panel_vdd_on(intel_dp);
		intel_edp_panel_off(intel_dp);
	}

	if (IS_SKYLAKE(dev))
		I915_WRITE(DPLL_CTRL2, (I915_READ(DPLL_CTRL2) |
					DPLL_CTRL2_DDI_CLK_OFF(port)));
	else if (INTEL_INFO(dev)->gen < 9)
		I915_WRITE(PORT_CLK_SEL(port), PORT_CLK_SEL_NONE);
}

static void intel_enable_ddi(struct intel_encoder *intel_encoder)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_ddi_get_encoder_port(intel_encoder);
	int type = intel_encoder->type;

	if (type == INTEL_OUTPUT_HDMI) {
		struct intel_digital_port *intel_dig_port =
			enc_to_dig_port(encoder);

		/* In HDMI/DVI mode, the port width, and swing/emphasis values
		 * are ignored so nothing special needs to be done besides
		 * enabling the port.
		 */
		I915_WRITE(DDI_BUF_CTL(port),
			   intel_dig_port->saved_port_bits |
			   DDI_BUF_CTL_ENABLE);
	} else if (type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		if (port == PORT_A && INTEL_INFO(dev)->gen < 9)
			intel_dp_stop_link_train(intel_dp);

		intel_edp_backlight_on(intel_dp);
		intel_psr_enable(intel_dp);
		intel_edp_drrs_enable(intel_dp);
	}

	if (intel_crtc->config->has_audio) {
		intel_display_power_get(dev_priv, POWER_DOMAIN_AUDIO);
		intel_audio_codec_enable(intel_encoder);
	}
}

static void intel_disable_ddi(struct intel_encoder *intel_encoder)
{
	struct drm_encoder *encoder = &intel_encoder->base;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	int type = intel_encoder->type;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (intel_crtc->config->has_audio) {
		intel_audio_codec_disable(intel_encoder);
		intel_display_power_put(dev_priv, POWER_DOMAIN_AUDIO);
	}

	if (type == INTEL_OUTPUT_EDP) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		intel_edp_drrs_disable(intel_dp);
		intel_psr_disable(intel_dp);
		intel_edp_backlight_off(intel_dp);
	}
}

static void hsw_ddi_pll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	I915_WRITE(WRPLL_CTL(pll->id), pll->config.hw_state.wrpll);
	POSTING_READ(WRPLL_CTL(pll->id));
	udelay(20);
}

static void hsw_ddi_pll_disable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	uint32_t val;

	val = I915_READ(WRPLL_CTL(pll->id));
	I915_WRITE(WRPLL_CTL(pll->id), val & ~WRPLL_PLL_ENABLE);
	POSTING_READ(WRPLL_CTL(pll->id));
}

static bool hsw_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
				     struct intel_shared_dpll *pll,
				     struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(WRPLL_CTL(pll->id));
	hw_state->wrpll = val;

	return val & WRPLL_PLL_ENABLE;
}

static const char * const hsw_ddi_pll_names[] = {
	"WRPLL 1",
	"WRPLL 2",
};

static void hsw_shared_dplls_init(struct drm_i915_private *dev_priv)
{
	int i;

	dev_priv->num_shared_dpll = 2;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = hsw_ddi_pll_names[i];
		dev_priv->shared_dplls[i].disable = hsw_ddi_pll_disable;
		dev_priv->shared_dplls[i].enable = hsw_ddi_pll_enable;
		dev_priv->shared_dplls[i].get_hw_state =
			hsw_ddi_pll_get_hw_state;
	}
}

static const char * const skl_ddi_pll_names[] = {
	"DPLL 1",
	"DPLL 2",
	"DPLL 3",
};

struct skl_dpll_regs {
	u32 ctl, cfgcr1, cfgcr2;
};

/* this array is indexed by the *shared* pll id */
static const struct skl_dpll_regs skl_dpll_regs[3] = {
	{
		/* DPLL 1 */
		.ctl = LCPLL2_CTL,
		.cfgcr1 = DPLL1_CFGCR1,
		.cfgcr2 = DPLL1_CFGCR2,
	},
	{
		/* DPLL 2 */
		.ctl = WRPLL_CTL1,
		.cfgcr1 = DPLL2_CFGCR1,
		.cfgcr2 = DPLL2_CFGCR2,
	},
	{
		/* DPLL 3 */
		.ctl = WRPLL_CTL2,
		.cfgcr1 = DPLL3_CFGCR1,
		.cfgcr2 = DPLL3_CFGCR2,
	},
};

static void skl_ddi_pll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	uint32_t val;
	unsigned int dpll;
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	/* DPLL0 is not part of the shared DPLLs, so pll->id is 0 for DPLL1 */
	dpll = pll->id + 1;

	val = I915_READ(DPLL_CTRL1);

	val &= ~(DPLL_CTRL1_HDMI_MODE(dpll) | DPLL_CTRL1_SSC(dpll) |
		 DPLL_CTRL1_LINK_RATE_MASK(dpll));
	val |= pll->config.hw_state.ctrl1 << (dpll * 6);

	I915_WRITE(DPLL_CTRL1, val);
	POSTING_READ(DPLL_CTRL1);

	I915_WRITE(regs[pll->id].cfgcr1, pll->config.hw_state.cfgcr1);
	I915_WRITE(regs[pll->id].cfgcr2, pll->config.hw_state.cfgcr2);
	POSTING_READ(regs[pll->id].cfgcr1);
	POSTING_READ(regs[pll->id].cfgcr2);

	/* the enable bit is always bit 31 */
	I915_WRITE(regs[pll->id].ctl,
		   I915_READ(regs[pll->id].ctl) | LCPLL_PLL_ENABLE);

	if (wait_for(I915_READ(DPLL_STATUS) & DPLL_LOCK(dpll), 5))
		DRM_ERROR("DPLL %d not locked\n", dpll);
}

static void skl_ddi_pll_disable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	/* the enable bit is always bit 31 */
	I915_WRITE(regs[pll->id].ctl,
		   I915_READ(regs[pll->id].ctl) & ~LCPLL_PLL_ENABLE);
	POSTING_READ(regs[pll->id].ctl);
}

static bool skl_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
				     struct intel_shared_dpll *pll,
				     struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;
	unsigned int dpll;
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	if (!intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	/* DPLL0 is not part of the shared DPLLs, so pll->id is 0 for DPLL1 */
	dpll = pll->id + 1;

	val = I915_READ(regs[pll->id].ctl);
	if (!(val & LCPLL_PLL_ENABLE))
		return false;

	val = I915_READ(DPLL_CTRL1);
	hw_state->ctrl1 = (val >> (dpll * 6)) & 0x3f;

	/* avoid reading back stale values if HDMI mode is not enabled */
	if (val & DPLL_CTRL1_HDMI_MODE(dpll)) {
		hw_state->cfgcr1 = I915_READ(regs[pll->id].cfgcr1);
		hw_state->cfgcr2 = I915_READ(regs[pll->id].cfgcr2);
	}

	return true;
}

static void skl_shared_dplls_init(struct drm_i915_private *dev_priv)
{
	int i;

	dev_priv->num_shared_dpll = 3;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = skl_ddi_pll_names[i];
		dev_priv->shared_dplls[i].disable = skl_ddi_pll_disable;
		dev_priv->shared_dplls[i].enable = skl_ddi_pll_enable;
		dev_priv->shared_dplls[i].get_hw_state =
			skl_ddi_pll_get_hw_state;
	}
}

static void broxton_phy_init(struct drm_i915_private *dev_priv,
			     enum dpio_phy phy)
{
	enum port port;
	uint32_t val;

	val = I915_READ(BXT_P_CR_GT_DISP_PWRON);
	val |= GT_DISPLAY_POWER_ON(phy);
	I915_WRITE(BXT_P_CR_GT_DISP_PWRON, val);

	/* Considering 10ms timeout until BSpec is updated */
	if (wait_for(I915_READ(BXT_PORT_CL1CM_DW0(phy)) & PHY_POWER_GOOD, 10))
		DRM_ERROR("timeout during PHY%d power on\n", phy);

	for (port =  (phy == DPIO_PHY0 ? PORT_B : PORT_A);
	     port <= (phy == DPIO_PHY0 ? PORT_C : PORT_A); port++) {
		int lane;

		for (lane = 0; lane < 4; lane++) {
			val = I915_READ(BXT_PORT_TX_DW14_LN(port, lane));
			/*
			 * Note that on CHV this flag is called UPAR, but has
			 * the same function.
			 */
			val &= ~LATENCY_OPTIM;
			if (lane != 1)
				val |= LATENCY_OPTIM;

			I915_WRITE(BXT_PORT_TX_DW14_LN(port, lane), val);
		}
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
		if (wait_for(I915_READ(BXT_PORT_REF_DW3(DPIO_PHY1)) & GRC_DONE,
			     10))
			DRM_ERROR("timeout waiting for PHY1 GRC\n");

		val = I915_READ(BXT_PORT_REF_DW6(DPIO_PHY1));
		val = (val & GRC_CODE_MASK) >> GRC_CODE_SHIFT;
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
}

void broxton_ddi_phy_init(struct drm_device *dev)
{
	/* Enable PHY1 first since it provides Rcomp for PHY0 */
	broxton_phy_init(dev->dev_private, DPIO_PHY1);
	broxton_phy_init(dev->dev_private, DPIO_PHY0);
}

static void broxton_phy_uninit(struct drm_i915_private *dev_priv,
			       enum dpio_phy phy)
{
	uint32_t val;

	val = I915_READ(BXT_PHY_CTL_FAMILY(phy));
	val &= ~COMMON_RESET_DIS;
	I915_WRITE(BXT_PHY_CTL_FAMILY(phy), val);
}

void broxton_ddi_phy_uninit(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	broxton_phy_uninit(dev_priv, DPIO_PHY1);
	broxton_phy_uninit(dev_priv, DPIO_PHY0);

	/* FIXME: do this in broxton_phy_uninit per phy */
	I915_WRITE(BXT_P_CR_GT_DISP_PWRON, 0);
}

static const char * const bxt_ddi_pll_names[] = {
	"PORT PLL A",
	"PORT PLL B",
	"PORT PLL C",
};

static void bxt_ddi_pll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	uint32_t temp;
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */

	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp &= ~PORT_PLL_REF_SEL;
	/* Non-SSC reference */
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);

	/* Disable 10 bit clock */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(port));
	temp &= ~PORT_PLL_10BIT_CLK_ENABLE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);

	/* Write P1 & P2 */
	temp = I915_READ(BXT_PORT_PLL_EBB_0(port));
	temp &= ~(PORT_PLL_P1_MASK | PORT_PLL_P2_MASK);
	temp |= pll->config.hw_state.ebb0;
	I915_WRITE(BXT_PORT_PLL_EBB_0(port), temp);

	/* Write M2 integer */
	temp = I915_READ(BXT_PORT_PLL(port, 0));
	temp &= ~PORT_PLL_M2_MASK;
	temp |= pll->config.hw_state.pll0;
	I915_WRITE(BXT_PORT_PLL(port, 0), temp);

	/* Write N */
	temp = I915_READ(BXT_PORT_PLL(port, 1));
	temp &= ~PORT_PLL_N_MASK;
	temp |= pll->config.hw_state.pll1;
	I915_WRITE(BXT_PORT_PLL(port, 1), temp);

	/* Write M2 fraction */
	temp = I915_READ(BXT_PORT_PLL(port, 2));
	temp &= ~PORT_PLL_M2_FRAC_MASK;
	temp |= pll->config.hw_state.pll2;
	I915_WRITE(BXT_PORT_PLL(port, 2), temp);

	/* Write M2 fraction enable */
	temp = I915_READ(BXT_PORT_PLL(port, 3));
	temp &= ~PORT_PLL_M2_FRAC_ENABLE;
	temp |= pll->config.hw_state.pll3;
	I915_WRITE(BXT_PORT_PLL(port, 3), temp);

	/* Write coeff */
	temp = I915_READ(BXT_PORT_PLL(port, 6));
	temp &= ~PORT_PLL_PROP_COEFF_MASK;
	temp &= ~PORT_PLL_INT_COEFF_MASK;
	temp &= ~PORT_PLL_GAIN_CTL_MASK;
	temp |= pll->config.hw_state.pll6;
	I915_WRITE(BXT_PORT_PLL(port, 6), temp);

	/* Write calibration val */
	temp = I915_READ(BXT_PORT_PLL(port, 8));
	temp &= ~PORT_PLL_TARGET_CNT_MASK;
	temp |= pll->config.hw_state.pll8;
	I915_WRITE(BXT_PORT_PLL(port, 8), temp);

	temp = I915_READ(BXT_PORT_PLL(port, 9));
	temp &= ~PORT_PLL_LOCK_THRESHOLD_MASK;
	temp |= (5 << 1);
	I915_WRITE(BXT_PORT_PLL(port, 9), temp);

	temp = I915_READ(BXT_PORT_PLL(port, 10));
	temp &= ~PORT_PLL_DCO_AMP_OVR_EN_H;
	temp &= ~PORT_PLL_DCO_AMP_MASK;
	temp |= pll->config.hw_state.pll10;
	I915_WRITE(BXT_PORT_PLL(port, 10), temp);

	/* Recalibrate with new settings */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(port));
	temp |= PORT_PLL_RECALIBRATE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);
	/* Enable 10 bit clock */
	temp |= PORT_PLL_10BIT_CLK_ENABLE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);

	/* Enable PLL */
	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp |= PORT_PLL_ENABLE;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);
	POSTING_READ(BXT_PORT_PLL_ENABLE(port));

	if (wait_for_atomic_us((I915_READ(BXT_PORT_PLL_ENABLE(port)) &
			PORT_PLL_LOCK), 200))
		DRM_ERROR("PLL %d not locked\n", port);

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers and we pick lanes 0/1 for that.
	 */
	temp = I915_READ(BXT_PORT_PCS_DW12_LN01(port));
	temp &= ~LANE_STAGGER_MASK;
	temp &= ~LANESTAGGER_STRAP_OVRD;
	temp |= pll->config.hw_state.pcsdw12;
	I915_WRITE(BXT_PORT_PCS_DW12_GRP(port), temp);
}

static void bxt_ddi_pll_disable(struct drm_i915_private *dev_priv,
					struct intel_shared_dpll *pll)
{
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	uint32_t temp;

	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp &= ~PORT_PLL_ENABLE;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);
	POSTING_READ(BXT_PORT_PLL_ENABLE(port));
}

static bool bxt_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
					struct intel_shared_dpll *pll,
					struct intel_dpll_hw_state *hw_state)
{
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	uint32_t val;

	if (!intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(BXT_PORT_PLL_ENABLE(port));
	if (!(val & PORT_PLL_ENABLE))
		return false;

	hw_state->ebb0 = I915_READ(BXT_PORT_PLL_EBB_0(port));
	hw_state->pll0 = I915_READ(BXT_PORT_PLL(port, 0));
	hw_state->pll1 = I915_READ(BXT_PORT_PLL(port, 1));
	hw_state->pll2 = I915_READ(BXT_PORT_PLL(port, 2));
	hw_state->pll3 = I915_READ(BXT_PORT_PLL(port, 3));
	hw_state->pll6 = I915_READ(BXT_PORT_PLL(port, 6));
	hw_state->pll8 = I915_READ(BXT_PORT_PLL(port, 8));
	hw_state->pll10 = I915_READ(BXT_PORT_PLL(port, 10));
	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers. We configure all lanes the same way, so
	 * here just read out lanes 0/1 and output a note if lanes 2/3 differ.
	 */
	hw_state->pcsdw12 = I915_READ(BXT_PORT_PCS_DW12_LN01(port));
	if (I915_READ(BXT_PORT_PCS_DW12_LN23(port) != hw_state->pcsdw12))
		DRM_DEBUG_DRIVER("lane stagger config different for lane 01 (%08x) and 23 (%08x)\n",
				 hw_state->pcsdw12,
				 I915_READ(BXT_PORT_PCS_DW12_LN23(port)));

	return true;
}

static void bxt_shared_dplls_init(struct drm_i915_private *dev_priv)
{
	int i;

	dev_priv->num_shared_dpll = 3;

	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		dev_priv->shared_dplls[i].id = i;
		dev_priv->shared_dplls[i].name = bxt_ddi_pll_names[i];
		dev_priv->shared_dplls[i].disable = bxt_ddi_pll_disable;
		dev_priv->shared_dplls[i].enable = bxt_ddi_pll_enable;
		dev_priv->shared_dplls[i].get_hw_state =
			bxt_ddi_pll_get_hw_state;
	}
}

void intel_ddi_pll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t val = I915_READ(LCPLL_CTL);
	int cdclk_freq;

	if (IS_SKYLAKE(dev))
		skl_shared_dplls_init(dev_priv);
	else if (IS_BROXTON(dev))
		bxt_shared_dplls_init(dev_priv);
	else
		hsw_shared_dplls_init(dev_priv);

	cdclk_freq = dev_priv->display.get_display_clock_speed(dev);
	DRM_DEBUG_KMS("CDCLK running at %dKHz\n", cdclk_freq);

	if (IS_SKYLAKE(dev)) {
		dev_priv->skl_boot_cdclk = cdclk_freq;
		if (!(I915_READ(LCPLL1_CTL) & LCPLL_PLL_ENABLE))
			DRM_ERROR("LCPLL1 is disabled\n");
		else
			intel_display_power_get(dev_priv, POWER_DOMAIN_PLLS);
	} else if (IS_BROXTON(dev)) {
		broxton_init_cdclk(dev);
		broxton_ddi_phy_init(dev);
	} else {
		/*
		 * The LCPLL register should be turned on by the BIOS. For now
		 * let's just check its state and print errors in case
		 * something is wrong.  Don't even try to turn it on.
		 */

		if (val & LCPLL_CD_SOURCE_FCLK)
			DRM_ERROR("CDCLK source is not LCPLL\n");

		if (val & LCPLL_PLL_DISABLE)
			DRM_ERROR("LCPLL is disabled\n");
	}
}

void intel_ddi_prepare_link_retrain(struct drm_encoder *encoder)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct drm_i915_private *dev_priv = encoder->dev->dev_private;
	enum port port = intel_dig_port->port;
	uint32_t val;
	bool wait = false;

	if (I915_READ(DP_TP_CTL(port)) & DP_TP_CTL_ENABLE) {
		val = I915_READ(DDI_BUF_CTL(port));
		if (val & DDI_BUF_CTL_ENABLE) {
			val &= ~DDI_BUF_CTL_ENABLE;
			I915_WRITE(DDI_BUF_CTL(port), val);
			wait = true;
		}

		val = I915_READ(DP_TP_CTL(port));
		val &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		val |= DP_TP_CTL_LINK_TRAIN_PAT1;
		I915_WRITE(DP_TP_CTL(port), val);
		POSTING_READ(DP_TP_CTL(port));

		if (wait)
			intel_wait_ddi_buf_idle(dev_priv, port);
	}

	val = DP_TP_CTL_ENABLE |
	      DP_TP_CTL_LINK_TRAIN_PAT1 | DP_TP_CTL_SCRAMBLE_DISABLE;
	if (intel_dp->is_mst)
		val |= DP_TP_CTL_MODE_MST;
	else {
		val |= DP_TP_CTL_MODE_SST;
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			val |= DP_TP_CTL_ENHANCED_FRAME_ENABLE;
	}
	I915_WRITE(DP_TP_CTL(port), val);
	POSTING_READ(DP_TP_CTL(port));

	intel_dp->DP |= DDI_BUF_CTL_ENABLE;
	I915_WRITE(DDI_BUF_CTL(port), intel_dp->DP);
	POSTING_READ(DDI_BUF_CTL(port));

	udelay(600);
}

void intel_ddi_fdi_disable(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = crtc->dev->dev_private;
	struct intel_encoder *intel_encoder = intel_ddi_get_crtc_encoder(crtc);
	uint32_t val;

	intel_ddi_post_disable(intel_encoder);

	val = I915_READ(_FDI_RXA_CTL);
	val &= ~FDI_RX_ENABLE;
	I915_WRITE(_FDI_RXA_CTL, val);

	val = I915_READ(_FDI_RXA_MISC);
	val &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
	val |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
	I915_WRITE(_FDI_RXA_MISC, val);

	val = I915_READ(_FDI_RXA_CTL);
	val &= ~FDI_PCDCLK;
	I915_WRITE(_FDI_RXA_CTL, val);

	val = I915_READ(_FDI_RXA_CTL);
	val &= ~FDI_RX_PLL_ENABLE;
	I915_WRITE(_FDI_RXA_CTL, val);
}

static void intel_ddi_hot_plug(struct intel_encoder *intel_encoder)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(&intel_encoder->base);
	int type = intel_dig_port->base.type;

	if (type != INTEL_OUTPUT_DISPLAYPORT &&
	    type != INTEL_OUTPUT_EDP &&
	    type != INTEL_OUTPUT_UNKNOWN) {
		return;
	}

	intel_dp_hot_plug(intel_encoder);
}

void intel_ddi_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	struct intel_hdmi *intel_hdmi;
	u32 temp, flags = 0;

	temp = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (temp & TRANS_DDI_PHSYNC)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (temp & TRANS_DDI_PVSYNC)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	pipe_config->base.adjusted_mode.flags |= flags;

	switch (temp & TRANS_DDI_BPC_MASK) {
	case TRANS_DDI_BPC_6:
		pipe_config->pipe_bpp = 18;
		break;
	case TRANS_DDI_BPC_8:
		pipe_config->pipe_bpp = 24;
		break;
	case TRANS_DDI_BPC_10:
		pipe_config->pipe_bpp = 30;
		break;
	case TRANS_DDI_BPC_12:
		pipe_config->pipe_bpp = 36;
		break;
	default:
		break;
	}

	switch (temp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
		pipe_config->has_hdmi_sink = true;
		intel_hdmi = enc_to_intel_hdmi(&encoder->base);

		if (intel_hdmi->infoframe_enabled(&encoder->base))
			pipe_config->has_infoframe = true;
		break;
	case TRANS_DDI_MODE_SELECT_DVI:
	case TRANS_DDI_MODE_SELECT_FDI:
		break;
	case TRANS_DDI_MODE_SELECT_DP_SST:
	case TRANS_DDI_MODE_SELECT_DP_MST:
		pipe_config->has_dp_encoder = true;
		intel_dp_get_m_n(intel_crtc, pipe_config);
		break;
	default:
		break;
	}

	if (intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_AUDIO)) {
		temp = I915_READ(HSW_AUD_PIN_ELD_CP_VLD);
		if (temp & AUDIO_OUTPUT_ENABLE(intel_crtc->pipe))
			pipe_config->has_audio = true;
	}

	if (encoder->type == INTEL_OUTPUT_EDP && dev_priv->vbt.edp_bpp &&
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

	intel_ddi_clock_get(encoder, pipe_config);
}

static void intel_ddi_destroy(struct drm_encoder *encoder)
{
	/* HDMI has nothing special to destroy, so we can go with this. */
	intel_dp_encoder_destroy(encoder);
}

static bool intel_ddi_compute_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *pipe_config)
{
	int type = encoder->type;
	int port = intel_ddi_get_encoder_port(encoder);

	WARN(type == INTEL_OUTPUT_UNKNOWN, "compute_config() on unknown output!\n");

	if (port == PORT_A)
		pipe_config->cpu_transcoder = TRANSCODER_EDP;

	if (type == INTEL_OUTPUT_HDMI)
		return intel_hdmi_compute_config(encoder, pipe_config);
	else
		return intel_dp_compute_config(encoder, pipe_config);
}

static const struct drm_encoder_funcs intel_ddi_funcs = {
	.destroy = intel_ddi_destroy,
};

static struct intel_connector *
intel_ddi_init_dp_connector(struct intel_digital_port *intel_dig_port)
{
	struct intel_connector *connector;
	enum port port = intel_dig_port->port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	intel_dig_port->dp.output_reg = DDI_BUF_CTL(port);
	if (!intel_dp_init_connector(intel_dig_port, connector)) {
		kfree(connector);
		return NULL;
	}

	return connector;
}

static struct intel_connector *
intel_ddi_init_hdmi_connector(struct intel_digital_port *intel_dig_port)
{
	struct intel_connector *connector;
	enum port port = intel_dig_port->port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	intel_dig_port->hdmi.hdmi_reg = DDI_BUF_CTL(port);
	intel_hdmi_init_connector(intel_dig_port, connector);

	return connector;
}

void intel_ddi_init(struct drm_device *dev, enum port port)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_digital_port *intel_dig_port;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	bool init_hdmi, init_dp;

	init_hdmi = (dev_priv->vbt.ddi_port_info[port].supports_dvi ||
		     dev_priv->vbt.ddi_port_info[port].supports_hdmi);
	init_dp = dev_priv->vbt.ddi_port_info[port].supports_dp;
	if (!init_dp && !init_hdmi) {
		DRM_DEBUG_KMS("VBT says port %c is not DVI/HDMI/DP compatible, assuming it is\n",
			      port_name(port));
		init_hdmi = true;
		init_dp = true;
	}

	intel_dig_port = kzalloc(sizeof(*intel_dig_port), GFP_KERNEL);
	if (!intel_dig_port)
		return;

	intel_encoder = &intel_dig_port->base;
	encoder = &intel_encoder->base;

	drm_encoder_init(dev, encoder, &intel_ddi_funcs,
			 DRM_MODE_ENCODER_TMDS);

	intel_encoder->compute_config = intel_ddi_compute_config;
	intel_encoder->enable = intel_enable_ddi;
	intel_encoder->pre_enable = intel_ddi_pre_enable;
	intel_encoder->disable = intel_disable_ddi;
	intel_encoder->post_disable = intel_ddi_post_disable;
	intel_encoder->get_hw_state = intel_ddi_get_hw_state;
	intel_encoder->get_config = intel_ddi_get_config;

	intel_dig_port->port = port;
	intel_dig_port->saved_port_bits = I915_READ(DDI_BUF_CTL(port)) &
					  (DDI_BUF_PORT_REVERSAL |
					   DDI_A_4_LANES);

	intel_encoder->type = INTEL_OUTPUT_UNKNOWN;
	intel_encoder->crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);
	intel_encoder->cloneable = 0;
	intel_encoder->hot_plug = intel_ddi_hot_plug;

	if (init_dp) {
		if (!intel_ddi_init_dp_connector(intel_dig_port))
			goto err;

		intel_dig_port->hpd_pulse = intel_dp_hpd_pulse;
		dev_priv->hpd_irq_port[port] = intel_dig_port;
	}

	/* In theory we don't need the encoder->type check, but leave it just in
	 * case we have some really bad VBTs... */
	if (intel_encoder->type != INTEL_OUTPUT_EDP && init_hdmi) {
		if (!intel_ddi_init_hdmi_connector(intel_dig_port))
			goto err;
	}

	return;

err:
	drm_encoder_cleanup(encoder);
	kfree(intel_dig_port);
}
