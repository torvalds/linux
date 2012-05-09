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

/* HDMI/DVI modes ignore everything but the last 2 items. So we share
 * them for both DP and FDI transports, allowing those ports to
 * automatically adapt to HDMI connections as well
 */
static const u32 hsw_ddi_translations_dp[] = {
	0x00FFFFFF, 0x0006000E,		/* DP parameters */
	0x00D75FFF, 0x0005000A,
	0x00C30FFF, 0x00040006,
	0x80AAAFFF, 0x000B0000,
	0x00FFFFFF, 0x0005000A,
	0x00D75FFF, 0x000C0004,
	0x80C30FFF, 0x000B0000,
	0x00FFFFFF, 0x00040006,
	0x80D75FFF, 0x000B0000,
	0x00FFFFFF, 0x00040006		/* HDMI parameters */
};

static const u32 hsw_ddi_translations_fdi[] = {
	0x00FFFFFF, 0x0007000E,		/* FDI parameters */
	0x00D75FFF, 0x000F000A,
	0x00C30FFF, 0x00060006,
	0x00AAAFFF, 0x001E0000,
	0x00FFFFFF, 0x000F000A,
	0x00D75FFF, 0x00160004,
	0x00C30FFF, 0x001E0000,
	0x00FFFFFF, 0x00060006,
	0x00D75FFF, 0x001E0000,
	0x00FFFFFF, 0x00040006		/* HDMI parameters */
};

/* On Haswell, DDI port buffers must be programmed with correct values
 * in advance. The buffer values are different for FDI and DP modes,
 * but the HDMI/DVI fields are shared among those. So we program the DDI
 * in either FDI or DP modes only, as HDMI connections will work with both
 * of those
 */
void intel_prepare_ddi_buffers(struct drm_device *dev, enum port port, bool use_fdi_mode)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg;
	int i;
	const u32 *ddi_translations = ((use_fdi_mode) ?
		hsw_ddi_translations_fdi :
		hsw_ddi_translations_dp);

	DRM_DEBUG_DRIVER("Initializing DDI buffers for port %c in %s mode\n",
			port_name(port),
			use_fdi_mode ? "FDI" : "DP");

	WARN((use_fdi_mode && (port != PORT_E)),
		"Programming port %c in FDI mode, this probably will not work.\n",
		port_name(port));

	for (i=0, reg=DDI_BUF_TRANS(port); i < ARRAY_SIZE(hsw_ddi_translations_fdi); i++) {
		I915_WRITE(reg, ddi_translations[i]);
		reg += 4;
	}
}

/* Program DDI buffers translations for DP. By default, program ports A-D in DP
 * mode and port E for FDI.
 */
void intel_prepare_ddi(struct drm_device *dev)
{
	int port;

	if (IS_HASWELL(dev)) {
		for (port = PORT_A; port < PORT_E; port++)
			intel_prepare_ddi_buffers(dev, port, false);

		/* DDI E is the suggested one to work in FDI mode, so program is as such by
		 * default. It will have to be re-programmed in case a digital DP output
		 * will be detected on it
		 */
		intel_prepare_ddi_buffers(dev, PORT_E, true);
	}
}

static const long hsw_ddi_buf_ctl_values[] = {
	DDI_BUF_EMP_400MV_0DB_HSW,
	DDI_BUF_EMP_400MV_3_5DB_HSW,
	DDI_BUF_EMP_400MV_6DB_HSW,
	DDI_BUF_EMP_400MV_9_5DB_HSW,
	DDI_BUF_EMP_600MV_0DB_HSW,
	DDI_BUF_EMP_600MV_3_5DB_HSW,
	DDI_BUF_EMP_600MV_6DB_HSW,
	DDI_BUF_EMP_800MV_0DB_HSW,
	DDI_BUF_EMP_800MV_3_5DB_HSW
};


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
	int pipe = intel_crtc->pipe;
	u32 reg, temp, i;

	/* Configure CPU PLL, wait for warmup */
	I915_WRITE(SPLL_CTL,
			SPLL_PLL_ENABLE |
			SPLL_PLL_FREQ_1350MHz |
			SPLL_PLL_SCC);

	/* Use SPLL to drive the output when in FDI mode */
	I915_WRITE(PORT_CLK_SEL(PORT_E),
			PORT_CLK_SEL_SPLL);
	I915_WRITE(PIPE_CLK_SEL(pipe),
			PIPE_CLK_SEL_PORT(PORT_E));

	udelay(20);

	/* Start the training iterating through available voltages and emphasis */
	for (i=0; i < ARRAY_SIZE(hsw_ddi_buf_ctl_values); i++) {
		/* Configure DP_TP_CTL with auto-training */
		I915_WRITE(DP_TP_CTL(PORT_E),
					DP_TP_CTL_FDI_AUTOTRAIN |
					DP_TP_CTL_ENHANCED_FRAME_ENABLE |
					DP_TP_CTL_LINK_TRAIN_PAT1 |
					DP_TP_CTL_ENABLE);

		/* Configure and enable DDI_BUF_CTL for DDI E with next voltage */
		temp = I915_READ(DDI_BUF_CTL(PORT_E));
		temp = (temp & ~DDI_BUF_EMP_MASK);
		I915_WRITE(DDI_BUF_CTL(PORT_E),
				temp |
				DDI_BUF_CTL_ENABLE |
				DDI_PORT_WIDTH_X2 |
				hsw_ddi_buf_ctl_values[i]);

		udelay(600);

		/* Enable CPU FDI Receiver with auto-training */
		reg = FDI_RX_CTL(pipe);
		I915_WRITE(reg,
				I915_READ(reg) |
					FDI_LINK_TRAIN_AUTO |
					FDI_RX_ENABLE |
					FDI_LINK_TRAIN_PATTERN_1_CPT |
					FDI_RX_ENHANCE_FRAME_ENABLE |
					FDI_PORT_WIDTH_2X_LPT |
					FDI_RX_PLL_ENABLE);
		POSTING_READ(reg);
		udelay(100);

		temp = I915_READ(DP_TP_STATUS(PORT_E));
		if (temp & DP_TP_STATUS_AUTOTRAIN_DONE) {
			DRM_DEBUG_DRIVER("BUF_CTL training done on %d step\n", i);

			/* Enable normal pixel sending for FDI */
			I915_WRITE(DP_TP_CTL(PORT_E),
						DP_TP_CTL_FDI_AUTOTRAIN |
						DP_TP_CTL_LINK_TRAIN_NORMAL |
						DP_TP_CTL_ENHANCED_FRAME_ENABLE |
						DP_TP_CTL_ENABLE);

			/* Enable PIPE_DDI_FUNC_CTL for the pipe to work in FDI mode */
			temp = I915_READ(DDI_FUNC_CTL(pipe));
			temp &= ~PIPE_DDI_PORT_MASK;
			temp |= PIPE_DDI_SELECT_PORT(PORT_E) |
					PIPE_DDI_MODE_SELECT_FDI |
					PIPE_DDI_FUNC_ENABLE |
					PIPE_DDI_PORT_WIDTH_X2;
			I915_WRITE(DDI_FUNC_CTL(pipe),
					temp);
			break;
		} else {
			DRM_ERROR("Error training BUF_CTL %d\n", i);

			/* Disable DP_TP_CTL and FDI_RX_CTL) and retry */
			I915_WRITE(DP_TP_CTL(PORT_E),
					I915_READ(DP_TP_CTL(PORT_E)) &
						~DP_TP_CTL_ENABLE);
			I915_WRITE(FDI_RX_CTL(pipe),
					I915_READ(FDI_RX_CTL(pipe)) &
						~FDI_RX_PLL_ENABLE);
			continue;
		}
	}

	DRM_DEBUG_KMS("FDI train done.\n");
}

/* For DDI connections, it is possible to support different outputs over the
 * same DDI port, such as HDMI or DP or even VGA via FDI. So we don't know by
 * the time the output is detected what exactly is on the other end of it. This
 * function aims at providing support for this detection and proper output
 * configuration.
 */
void intel_ddi_init(struct drm_device *dev, enum port port)
{
	/* For now, we don't do any proper output detection and assume that we
	 * handle HDMI only */

	switch(port){
	case PORT_A:
		/* We don't handle eDP and DP yet */
		DRM_DEBUG_DRIVER("Found digital output on DDI port A\n");
		break;
	/* Assume that the  ports B, C and D are working in HDMI mode for now */
	case PORT_B:
	case PORT_C:
	case PORT_D:
		intel_hdmi_init(dev, DDI_BUF_CTL(port));
		break;
	default:
		DRM_DEBUG_DRIVER("No handlers defined for port %d, skipping DDI initialization\n",
				port);
		break;
	}
}
