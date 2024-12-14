/*
 * Copyright © 2010 Intel Corporation
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
 *
 * Authors:
 *	Li Peng <peng.li@intel.com>
 */

#include <linux/delay.h>

#include <drm/drm.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_simple_kms_helper.h>

#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"

#define HDMI_READ(reg)		readl(hdmi_dev->regs + (reg))
#define HDMI_WRITE(reg, val)	writel(val, hdmi_dev->regs + (reg))

#define HDMI_HCR	0x1000
#define HCR_ENABLE_HDCP		(1 << 5)
#define HCR_ENABLE_AUDIO	(1 << 2)
#define HCR_ENABLE_PIXEL	(1 << 1)
#define HCR_ENABLE_TMDS		(1 << 0)

#define HDMI_HICR	0x1004
#define HDMI_HSR	0x1008
#define HDMI_HISR	0x100C
#define HDMI_DETECT_HDP		(1 << 0)

#define HDMI_VIDEO_REG	0x3000
#define HDMI_UNIT_EN		(1 << 7)
#define HDMI_MODE_OUTPUT	(1 << 0)
#define HDMI_HBLANK_A	0x3100

#define HDMI_AUDIO_CTRL	0x4000
#define HDMI_ENABLE_AUDIO	(1 << 0)

#define PCH_HTOTAL_B	0x3100
#define PCH_HBLANK_B	0x3104
#define PCH_HSYNC_B	0x3108
#define PCH_VTOTAL_B	0x310C
#define PCH_VBLANK_B	0x3110
#define PCH_VSYNC_B	0x3114
#define PCH_PIPEBSRC	0x311C

#define PCH_PIPEB_DSL	0x3800
#define PCH_PIPEB_SLC	0x3804
#define PCH_PIPEBCONF	0x3808
#define PCH_PIPEBSTAT	0x3824

#define CDVO_DFT	0x5000
#define CDVO_SLEWRATE	0x5004
#define CDVO_STRENGTH	0x5008
#define CDVO_RCOMP	0x500C

#define DPLL_CTRL       0x6000
#define DPLL_PDIV_SHIFT		16
#define DPLL_PDIV_MASK		(0xf << 16)
#define DPLL_PWRDN		(1 << 4)
#define DPLL_RESET		(1 << 3)
#define DPLL_FASTEN		(1 << 2)
#define DPLL_ENSTAT		(1 << 1)
#define DPLL_DITHEN		(1 << 0)

#define DPLL_DIV_CTRL   0x6004
#define DPLL_CLKF_MASK		0xffffffc0
#define DPLL_CLKR_MASK		(0x3f)

#define DPLL_CLK_ENABLE 0x6008
#define DPLL_EN_DISP		(1 << 31)
#define DPLL_SEL_HDMI		(1 << 8)
#define DPLL_EN_HDMI		(1 << 1)
#define DPLL_EN_VGA		(1 << 0)

#define DPLL_ADJUST     0x600C
#define DPLL_STATUS     0x6010
#define DPLL_UPDATE     0x6014
#define DPLL_DFT        0x6020

struct intel_range {
	int	min, max;
};

struct oaktrail_hdmi_limit {
	struct intel_range vco, np, nr, nf;
};

struct oaktrail_hdmi_clock {
	int np;
	int nr;
	int nf;
	int dot;
};

#define VCO_MIN		320000
#define VCO_MAX		1650000
#define	NP_MIN		1
#define	NP_MAX		15
#define	NR_MIN		1
#define	NR_MAX		64
#define NF_MIN		2
#define NF_MAX		4095

static const struct oaktrail_hdmi_limit oaktrail_hdmi_limit = {
	.vco = { .min = VCO_MIN,		.max = VCO_MAX },
	.np  = { .min = NP_MIN,			.max = NP_MAX  },
	.nr  = { .min = NR_MIN,			.max = NR_MAX  },
	.nf  = { .min = NF_MIN,			.max = NF_MAX  },
};

static void oaktrail_hdmi_audio_enable(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;

	HDMI_WRITE(HDMI_HCR, 0x67);
	HDMI_READ(HDMI_HCR);

	HDMI_WRITE(0x51a8, 0x10);
	HDMI_READ(0x51a8);

	HDMI_WRITE(HDMI_AUDIO_CTRL, 0x1);
	HDMI_READ(HDMI_AUDIO_CTRL);
}

static void oaktrail_hdmi_audio_disable(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;

	HDMI_WRITE(0x51a8, 0x0);
	HDMI_READ(0x51a8);

	HDMI_WRITE(HDMI_AUDIO_CTRL, 0x0);
	HDMI_READ(HDMI_AUDIO_CTRL);

	HDMI_WRITE(HDMI_HCR, 0x47);
	HDMI_READ(HDMI_HCR);
}

static unsigned int htotal_calculate(struct drm_display_mode *mode)
{
	u32 new_crtc_htotal;

	/*
	 * 1024 x 768  new_crtc_htotal = 0x1024;
	 * 1280 x 1024 new_crtc_htotal = 0x0c34;
	 */
	new_crtc_htotal = (mode->crtc_htotal - 1) * 200 * 1000 / mode->clock;

	DRM_DEBUG_KMS("new crtc htotal 0x%4x\n", new_crtc_htotal);
	return (mode->crtc_hdisplay - 1) | (new_crtc_htotal << 16);
}

static void oaktrail_hdmi_find_dpll(struct drm_crtc *crtc, int target,
				int refclk, struct oaktrail_hdmi_clock *best_clock)
{
	int np_min, np_max, nr_min, nr_max;
	int np, nr, nf;

	np_min = DIV_ROUND_UP(oaktrail_hdmi_limit.vco.min, target * 10);
	np_max = oaktrail_hdmi_limit.vco.max / (target * 10);
	if (np_min < oaktrail_hdmi_limit.np.min)
		np_min = oaktrail_hdmi_limit.np.min;
	if (np_max > oaktrail_hdmi_limit.np.max)
		np_max = oaktrail_hdmi_limit.np.max;

	nr_min = DIV_ROUND_UP((refclk * 1000), (target * 10 * np_max));
	nr_max = DIV_ROUND_UP((refclk * 1000), (target * 10 * np_min));
	if (nr_min < oaktrail_hdmi_limit.nr.min)
		nr_min = oaktrail_hdmi_limit.nr.min;
	if (nr_max > oaktrail_hdmi_limit.nr.max)
		nr_max = oaktrail_hdmi_limit.nr.max;

	np = DIV_ROUND_UP((refclk * 1000), (target * 10 * nr_max));
	nr = DIV_ROUND_UP((refclk * 1000), (target * 10 * np));
	nf = DIV_ROUND_CLOSEST((target * 10 * np * nr), refclk);
	DRM_DEBUG_KMS("np, nr, nf %d %d %d\n", np, nr, nf);

	/*
	 * 1024 x 768  np = 1; nr = 0x26; nf = 0x0fd8000;
	 * 1280 x 1024 np = 1; nr = 0x17; nf = 0x1034000;
	 */
	best_clock->np = np;
	best_clock->nr = nr - 1;
	best_clock->nf = (nf << 14);
}

static void scu_busy_loop(void __iomem *scu_base)
{
	u32 status = 0;
	u32 loop_count = 0;

	status = readl(scu_base + 0x04);
	while (status & 1) {
		udelay(1); /* scu processing time is in few u secods */
		status = readl(scu_base + 0x04);
		loop_count++;
		/* break if scu doesn't reset busy bit after huge retry */
		if (loop_count > 1000) {
			DRM_DEBUG_KMS("SCU IPC timed out");
			return;
		}
	}
}

/*
 *	You don't want to know, you really really don't want to know....
 *
 *	This is magic. However it's safe magic because of the way the platform
 *	works and it is necessary magic.
 */
static void oaktrail_hdmi_reset(struct drm_device *dev)
{
	void __iomem *base;
	unsigned long scu_ipc_mmio = 0xff11c000UL;
	int scu_len = 1024;

	base = ioremap((resource_size_t)scu_ipc_mmio, scu_len);
	if (base == NULL) {
		DRM_ERROR("failed to map scu mmio\n");
		return;
	}

	/* scu ipc: assert hdmi controller reset */
	writel(0xff11d118, base + 0x0c);
	writel(0x7fffffdf, base + 0x80);
	writel(0x42005, base + 0x0);
	scu_busy_loop(base);

	/* scu ipc: de-assert hdmi controller reset */
	writel(0xff11d118, base + 0x0c);
	writel(0x7fffffff, base + 0x80);
	writel(0x42005, base + 0x0);
	scu_busy_loop(base);

	iounmap(base);
}

int oaktrail_crtc_hdmi_mode_set(struct drm_crtc *crtc,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode,
			    int x, int y,
			    struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;
	int pipe = 1;
	int htot_reg = (pipe == 0) ? HTOTAL_A : HTOTAL_B;
	int hblank_reg = (pipe == 0) ? HBLANK_A : HBLANK_B;
	int hsync_reg = (pipe == 0) ? HSYNC_A : HSYNC_B;
	int vtot_reg = (pipe == 0) ? VTOTAL_A : VTOTAL_B;
	int vblank_reg = (pipe == 0) ? VBLANK_A : VBLANK_B;
	int vsync_reg = (pipe == 0) ? VSYNC_A : VSYNC_B;
	int dspsize_reg = (pipe == 0) ? DSPASIZE : DSPBSIZE;
	int dsppos_reg = (pipe == 0) ? DSPAPOS : DSPBPOS;
	int pipesrc_reg = (pipe == 0) ? PIPEASRC : PIPEBSRC;
	int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
	int refclk;
	struct oaktrail_hdmi_clock clock;
	u32 dspcntr, pipeconf, dpll, temp;
	int dspcntr_reg = DSPBCNTR;

	if (!gma_power_begin(dev, true))
		return 0;

	/* Disable the VGA plane that we never use */
	REG_WRITE(VGACNTRL, VGA_DISP_DISABLE);

	/* Disable dpll if necessary */
	dpll = REG_READ(DPLL_CTRL);
	if ((dpll & DPLL_PWRDN) == 0) {
		REG_WRITE(DPLL_CTRL, dpll | (DPLL_PWRDN | DPLL_RESET));
		REG_WRITE(DPLL_DIV_CTRL, 0x00000000);
		REG_WRITE(DPLL_STATUS, 0x1);
	}
	udelay(150);

	/* Reset controller */
	oaktrail_hdmi_reset(dev);

	/* program and enable dpll */
	refclk = 25000;
	oaktrail_hdmi_find_dpll(crtc, adjusted_mode->clock, refclk, &clock);

	/* Set the DPLL */
	dpll = REG_READ(DPLL_CTRL);
	dpll &= ~DPLL_PDIV_MASK;
	dpll &= ~(DPLL_PWRDN | DPLL_RESET);
	REG_WRITE(DPLL_CTRL, 0x00000008);
	REG_WRITE(DPLL_DIV_CTRL, ((clock.nf << 6) | clock.nr));
	REG_WRITE(DPLL_ADJUST, ((clock.nf >> 14) - 1));
	REG_WRITE(DPLL_CTRL, (dpll | (clock.np << DPLL_PDIV_SHIFT) | DPLL_ENSTAT | DPLL_DITHEN));
	REG_WRITE(DPLL_UPDATE, 0x80000000);
	REG_WRITE(DPLL_CLK_ENABLE, 0x80050102);
	udelay(150);

	/* configure HDMI */
	HDMI_WRITE(0x1004, 0x1fd);
	HDMI_WRITE(0x2000, 0x1);
	HDMI_WRITE(0x2008, 0x0);
	HDMI_WRITE(0x3130, 0x8);
	HDMI_WRITE(0x101c, 0x1800810);

	temp = htotal_calculate(adjusted_mode);
	REG_WRITE(htot_reg, temp);
	REG_WRITE(hblank_reg, (adjusted_mode->crtc_hblank_start - 1) | ((adjusted_mode->crtc_hblank_end - 1) << 16));
	REG_WRITE(hsync_reg, (adjusted_mode->crtc_hsync_start - 1) | ((adjusted_mode->crtc_hsync_end - 1) << 16));
	REG_WRITE(vtot_reg, (adjusted_mode->crtc_vdisplay - 1) | ((adjusted_mode->crtc_vtotal - 1) << 16));
	REG_WRITE(vblank_reg, (adjusted_mode->crtc_vblank_start - 1) | ((adjusted_mode->crtc_vblank_end - 1) << 16));
	REG_WRITE(vsync_reg, (adjusted_mode->crtc_vsync_start - 1) | ((adjusted_mode->crtc_vsync_end - 1) << 16));
	REG_WRITE(pipesrc_reg, ((mode->crtc_hdisplay - 1) << 16) |  (mode->crtc_vdisplay - 1));

	REG_WRITE(PCH_HTOTAL_B, (adjusted_mode->crtc_hdisplay - 1) | ((adjusted_mode->crtc_htotal - 1) << 16));
	REG_WRITE(PCH_HBLANK_B, (adjusted_mode->crtc_hblank_start - 1) | ((adjusted_mode->crtc_hblank_end - 1) << 16));
	REG_WRITE(PCH_HSYNC_B, (adjusted_mode->crtc_hsync_start - 1) | ((adjusted_mode->crtc_hsync_end - 1) << 16));
	REG_WRITE(PCH_VTOTAL_B, (adjusted_mode->crtc_vdisplay - 1) | ((adjusted_mode->crtc_vtotal - 1) << 16));
	REG_WRITE(PCH_VBLANK_B, (adjusted_mode->crtc_vblank_start - 1) | ((adjusted_mode->crtc_vblank_end - 1) << 16));
	REG_WRITE(PCH_VSYNC_B, (adjusted_mode->crtc_vsync_start - 1) | ((adjusted_mode->crtc_vsync_end - 1) << 16));
	REG_WRITE(PCH_PIPEBSRC, ((mode->crtc_hdisplay - 1) << 16) |  (mode->crtc_vdisplay - 1));

	temp = adjusted_mode->crtc_hblank_end - adjusted_mode->crtc_hblank_start;
	HDMI_WRITE(HDMI_HBLANK_A, ((adjusted_mode->crtc_hdisplay - 1) << 16) |  temp);

	REG_WRITE(dspsize_reg, ((mode->vdisplay - 1) << 16) | (mode->hdisplay - 1));
	REG_WRITE(dsppos_reg, 0);

	/* Flush the plane changes */
	{
		const struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
		crtc_funcs->mode_set_base(crtc, x, y, old_fb);
	}

	/* Set up the display plane register */
	dspcntr = REG_READ(dspcntr_reg);
	dspcntr |= DISPPLANE_GAMMA_ENABLE;
	dspcntr |= DISPPLANE_SEL_PIPE_B;
	dspcntr |= DISPLAY_PLANE_ENABLE;

	/* setup pipeconf */
	pipeconf = REG_READ(pipeconf_reg);
	pipeconf |= PIPEACONF_ENABLE;

	REG_WRITE(pipeconf_reg, pipeconf);
	REG_READ(pipeconf_reg);

	REG_WRITE(PCH_PIPEBCONF, pipeconf);
	REG_READ(PCH_PIPEBCONF);
	gma_wait_for_vblank(dev);

	REG_WRITE(dspcntr_reg, dspcntr);
	gma_wait_for_vblank(dev);

	gma_power_end(dev);

	return 0;
}

void oaktrail_crtc_hdmi_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	u32 temp;

	DRM_DEBUG_KMS("%s %d\n", __func__, mode);

	switch (mode) {
	case DRM_MODE_DPMS_OFF:
		REG_WRITE(VGACNTRL, 0x80000000);

		/* Disable plane */
		temp = REG_READ(DSPBCNTR);
		if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
			REG_WRITE(DSPBCNTR, temp & ~DISPLAY_PLANE_ENABLE);
			REG_READ(DSPBCNTR);
			/* Flush the plane changes */
			REG_WRITE(DSPBSURF, REG_READ(DSPBSURF));
			REG_READ(DSPBSURF);
		}

		/* Disable pipe B */
		temp = REG_READ(PIPEBCONF);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			REG_WRITE(PIPEBCONF, temp & ~PIPEACONF_ENABLE);
			REG_READ(PIPEBCONF);
		}

		/* Disable LNW Pipes, etc */
		temp = REG_READ(PCH_PIPEBCONF);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			REG_WRITE(PCH_PIPEBCONF, temp & ~PIPEACONF_ENABLE);
			REG_READ(PCH_PIPEBCONF);
		}

		/* wait for pipe off */
		udelay(150);

		/* Disable dpll */
		temp = REG_READ(DPLL_CTRL);
		if ((temp & DPLL_PWRDN) == 0) {
			REG_WRITE(DPLL_CTRL, temp | (DPLL_PWRDN | DPLL_RESET));
			REG_WRITE(DPLL_STATUS, 0x1);
		}

		/* wait for dpll off */
		udelay(150);

		break;
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		/* Enable dpll */
		temp = REG_READ(DPLL_CTRL);
		if ((temp & DPLL_PWRDN) != 0) {
			REG_WRITE(DPLL_CTRL, temp & ~(DPLL_PWRDN | DPLL_RESET));
			temp = REG_READ(DPLL_CLK_ENABLE);
			REG_WRITE(DPLL_CLK_ENABLE, temp | DPLL_EN_DISP | DPLL_SEL_HDMI | DPLL_EN_HDMI);
			REG_READ(DPLL_CLK_ENABLE);
		}
		/* wait for dpll warm up */
		udelay(150);

		/* Enable pipe B */
		temp = REG_READ(PIPEBCONF);
		if ((temp & PIPEACONF_ENABLE) == 0) {
			REG_WRITE(PIPEBCONF, temp | PIPEACONF_ENABLE);
			REG_READ(PIPEBCONF);
		}

		/* Enable LNW Pipe B */
		temp = REG_READ(PCH_PIPEBCONF);
		if ((temp & PIPEACONF_ENABLE) == 0) {
			REG_WRITE(PCH_PIPEBCONF, temp | PIPEACONF_ENABLE);
			REG_READ(PCH_PIPEBCONF);
		}

		gma_wait_for_vblank(dev);

		/* Enable plane */
		temp = REG_READ(DSPBCNTR);
		if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
			REG_WRITE(DSPBCNTR, temp | DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			REG_WRITE(DSPBSURF, REG_READ(DSPBSURF));
			REG_READ(DSPBSURF);
		}

		gma_crtc_load_lut(crtc);
	}

	/* DSPARB */
	REG_WRITE(DSPARB, 0x00003fbf);

	/* FW1 */
	REG_WRITE(0x70034, 0x3f880a0a);

	/* FW2 */
	REG_WRITE(0x70038, 0x0b060808);

	/* FW4 */
	REG_WRITE(0x70050, 0x08030404);

	/* FW5 */
	REG_WRITE(0x70054, 0x04040404);

	/* LNC Chicken Bits - Squawk! */
	REG_WRITE(0x70400, 0x4000);

	return;
}

static void oaktrail_hdmi_dpms(struct drm_encoder *encoder, int mode)
{
	static int dpms_mode = -1;

	struct drm_device *dev = encoder->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;
	u32 temp;

	if (dpms_mode == mode)
		return;

	if (mode != DRM_MODE_DPMS_ON)
		temp = 0x0;
	else
		temp = 0x99;

	dpms_mode = mode;
	HDMI_WRITE(HDMI_VIDEO_REG, temp);
}

static enum drm_mode_status oaktrail_hdmi_mode_valid(struct drm_connector *connector,
				const struct drm_display_mode *mode)
{
	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;
	if (mode->clock < 20000)
		return MODE_CLOCK_LOW;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	return MODE_OK;
}

static enum drm_connector_status
oaktrail_hdmi_detect(struct drm_connector *connector, bool force)
{
	enum drm_connector_status status;
	struct drm_device *dev = connector->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;
	u32 temp;

	temp = HDMI_READ(HDMI_HSR);
	DRM_DEBUG_KMS("HDMI_HSR %x\n", temp);

	if ((temp & HDMI_DETECT_HDP) != 0)
		status = connector_status_connected;
	else
		status = connector_status_disconnected;

	return status;
}

static const unsigned char raw_edid[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x10, 0xac, 0x2f, 0xa0,
	0x53, 0x55, 0x33, 0x30, 0x16, 0x13, 0x01, 0x03, 0x0e, 0x3a, 0x24, 0x78,
	0xea, 0xe9, 0xf5, 0xac, 0x51, 0x30, 0xb4, 0x25, 0x11, 0x50, 0x54, 0xa5,
	0x4b, 0x00, 0x81, 0x80, 0xa9, 0x40, 0x71, 0x4f, 0xb3, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x28, 0x3c, 0x80, 0xa0, 0x70, 0xb0,
	0x23, 0x40, 0x30, 0x20, 0x36, 0x00, 0x46, 0x6c, 0x21, 0x00, 0x00, 0x1a,
	0x00, 0x00, 0x00, 0xff, 0x00, 0x47, 0x4e, 0x37, 0x32, 0x31, 0x39, 0x35,
	0x52, 0x30, 0x33, 0x55, 0x53, 0x0a, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x44,
	0x45, 0x4c, 0x4c, 0x20, 0x32, 0x37, 0x30, 0x39, 0x57, 0x0a, 0x20, 0x20,
	0x00, 0x00, 0x00, 0xfd, 0x00, 0x38, 0x4c, 0x1e, 0x53, 0x11, 0x00, 0x0a,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x8d
};

static int oaktrail_hdmi_get_modes(struct drm_connector *connector)
{
	struct i2c_adapter *i2c_adap;
	struct edid *edid;
	int ret = 0;

	/*
	 *	FIXME: We need to figure this lot out. In theory we can
	 *	read the EDID somehow but I've yet to find working reference
	 *	code.
	 */
	i2c_adap = i2c_get_adapter(3);
	if (i2c_adap == NULL) {
		DRM_ERROR("No ddc adapter available!\n");
		edid = (struct edid *)raw_edid;
	} else {
		edid = (struct edid *)raw_edid;
		/* FIXME ? edid = drm_get_edid(connector, i2c_adap); */
	}

	if (edid) {
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
	}
	return ret;
}

static void oaktrail_hdmi_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;

	oaktrail_hdmi_audio_enable(dev);
	return;
}

static void oaktrail_hdmi_destroy(struct drm_connector *connector)
{
	return;
}

static const struct drm_encoder_helper_funcs oaktrail_hdmi_helper_funcs = {
	.dpms = oaktrail_hdmi_dpms,
	.prepare = gma_encoder_prepare,
	.mode_set = oaktrail_hdmi_mode_set,
	.commit = gma_encoder_commit,
};

static const struct drm_connector_helper_funcs
					oaktrail_hdmi_connector_helper_funcs = {
	.get_modes = oaktrail_hdmi_get_modes,
	.mode_valid = oaktrail_hdmi_mode_valid,
	.best_encoder = gma_best_encoder,
};

static const struct drm_connector_funcs oaktrail_hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = oaktrail_hdmi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = oaktrail_hdmi_destroy,
};

void oaktrail_hdmi_init(struct drm_device *dev,
					struct psb_intel_mode_device *mode_dev)
{
	struct gma_encoder *gma_encoder;
	struct gma_connector *gma_connector;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	gma_encoder = kzalloc(sizeof(struct gma_encoder), GFP_KERNEL);
	if (!gma_encoder)
		return;

	gma_connector = kzalloc(sizeof(struct gma_connector), GFP_KERNEL);
	if (!gma_connector)
		goto failed_connector;

	connector = &gma_connector->base;
	encoder = &gma_encoder->base;
	drm_connector_init(dev, connector,
			   &oaktrail_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_DVID);

	drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_TMDS);

	gma_connector_attach_encoder(gma_connector, gma_encoder);

	gma_encoder->type = INTEL_OUTPUT_HDMI;
	drm_encoder_helper_add(encoder, &oaktrail_hdmi_helper_funcs);
	drm_connector_helper_add(connector, &oaktrail_hdmi_connector_helper_funcs);

	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;
	dev_info(dev->dev, "HDMI initialised.\n");

	return;

failed_connector:
	kfree(gma_encoder);
}

void oaktrail_hdmi_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev;
	struct oaktrail_hdmi_dev *hdmi_dev;
	int ret;

	pdev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x080d, NULL);
	if (!pdev)
		return;

	hdmi_dev = kzalloc(sizeof(struct oaktrail_hdmi_dev), GFP_KERNEL);
	if (!hdmi_dev) {
		dev_err(dev->dev, "failed to allocate memory\n");
		goto out;
	}


	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(dev->dev, "failed to enable hdmi controller\n");
		goto free;
	}

	hdmi_dev->mmio = pci_resource_start(pdev, 0);
	hdmi_dev->mmio_len = pci_resource_len(pdev, 0);
	hdmi_dev->regs = ioremap(hdmi_dev->mmio, hdmi_dev->mmio_len);
	if (!hdmi_dev->regs) {
		dev_err(dev->dev, "failed to map hdmi mmio\n");
		goto free;
	}

	hdmi_dev->dev = pdev;
	pci_set_drvdata(pdev, hdmi_dev);

	/* Initialize i2c controller */
	ret = oaktrail_hdmi_i2c_init(hdmi_dev->dev);
	if (ret)
		dev_err(dev->dev, "HDMI I2C initialization failed\n");

	dev_priv->hdmi_priv = hdmi_dev;
	oaktrail_hdmi_audio_disable(dev);

	dev_info(dev->dev, "HDMI hardware present.\n");

	return;

free:
	kfree(hdmi_dev);
out:
	return;
}

void oaktrail_hdmi_teardown(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;
	struct pci_dev *pdev;

	if (hdmi_dev) {
		pdev = hdmi_dev->dev;
		pci_set_drvdata(pdev, NULL);
		oaktrail_hdmi_i2c_exit(pdev);
		iounmap(hdmi_dev->regs);
		kfree(hdmi_dev);
		pci_dev_put(pdev);
	}
}

/* save HDMI register state */
void oaktrail_hdmi_save(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;
	struct psb_state *regs = &dev_priv->regs.psb;
	struct psb_pipe *pipeb = &dev_priv->regs.pipe[1];
	int i;

	/* dpll */
	hdmi_dev->saveDPLL_CTRL = PSB_RVDC32(DPLL_CTRL);
	hdmi_dev->saveDPLL_DIV_CTRL = PSB_RVDC32(DPLL_DIV_CTRL);
	hdmi_dev->saveDPLL_ADJUST = PSB_RVDC32(DPLL_ADJUST);
	hdmi_dev->saveDPLL_UPDATE = PSB_RVDC32(DPLL_UPDATE);
	hdmi_dev->saveDPLL_CLK_ENABLE = PSB_RVDC32(DPLL_CLK_ENABLE);

	/* pipe B */
	pipeb->conf = PSB_RVDC32(PIPEBCONF);
	pipeb->src = PSB_RVDC32(PIPEBSRC);
	pipeb->htotal = PSB_RVDC32(HTOTAL_B);
	pipeb->hblank = PSB_RVDC32(HBLANK_B);
	pipeb->hsync = PSB_RVDC32(HSYNC_B);
	pipeb->vtotal = PSB_RVDC32(VTOTAL_B);
	pipeb->vblank = PSB_RVDC32(VBLANK_B);
	pipeb->vsync = PSB_RVDC32(VSYNC_B);

	hdmi_dev->savePCH_PIPEBCONF = PSB_RVDC32(PCH_PIPEBCONF);
	hdmi_dev->savePCH_PIPEBSRC = PSB_RVDC32(PCH_PIPEBSRC);
	hdmi_dev->savePCH_HTOTAL_B = PSB_RVDC32(PCH_HTOTAL_B);
	hdmi_dev->savePCH_HBLANK_B = PSB_RVDC32(PCH_HBLANK_B);
	hdmi_dev->savePCH_HSYNC_B  = PSB_RVDC32(PCH_HSYNC_B);
	hdmi_dev->savePCH_VTOTAL_B = PSB_RVDC32(PCH_VTOTAL_B);
	hdmi_dev->savePCH_VBLANK_B = PSB_RVDC32(PCH_VBLANK_B);
	hdmi_dev->savePCH_VSYNC_B  = PSB_RVDC32(PCH_VSYNC_B);

	/* plane */
	pipeb->cntr = PSB_RVDC32(DSPBCNTR);
	pipeb->stride = PSB_RVDC32(DSPBSTRIDE);
	pipeb->addr = PSB_RVDC32(DSPBBASE);
	pipeb->surf = PSB_RVDC32(DSPBSURF);
	pipeb->linoff = PSB_RVDC32(DSPBLINOFF);
	pipeb->tileoff = PSB_RVDC32(DSPBTILEOFF);

	/* cursor B */
	regs->saveDSPBCURSOR_CTRL = PSB_RVDC32(CURBCNTR);
	regs->saveDSPBCURSOR_BASE = PSB_RVDC32(CURBBASE);
	regs->saveDSPBCURSOR_POS = PSB_RVDC32(CURBPOS);

	/* save palette */
	for (i = 0; i < 256; i++)
		pipeb->palette[i] = PSB_RVDC32(PALETTE_B + (i << 2));
}

/* restore HDMI register state */
void oaktrail_hdmi_restore(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct oaktrail_hdmi_dev *hdmi_dev = dev_priv->hdmi_priv;
	struct psb_state *regs = &dev_priv->regs.psb;
	struct psb_pipe *pipeb = &dev_priv->regs.pipe[1];
	int i;

	/* dpll */
	PSB_WVDC32(hdmi_dev->saveDPLL_CTRL, DPLL_CTRL);
	PSB_WVDC32(hdmi_dev->saveDPLL_DIV_CTRL, DPLL_DIV_CTRL);
	PSB_WVDC32(hdmi_dev->saveDPLL_ADJUST, DPLL_ADJUST);
	PSB_WVDC32(hdmi_dev->saveDPLL_UPDATE, DPLL_UPDATE);
	PSB_WVDC32(hdmi_dev->saveDPLL_CLK_ENABLE, DPLL_CLK_ENABLE);
	udelay(150);

	/* pipe */
	PSB_WVDC32(pipeb->src, PIPEBSRC);
	PSB_WVDC32(pipeb->htotal, HTOTAL_B);
	PSB_WVDC32(pipeb->hblank, HBLANK_B);
	PSB_WVDC32(pipeb->hsync,  HSYNC_B);
	PSB_WVDC32(pipeb->vtotal, VTOTAL_B);
	PSB_WVDC32(pipeb->vblank, VBLANK_B);
	PSB_WVDC32(pipeb->vsync,  VSYNC_B);

	PSB_WVDC32(hdmi_dev->savePCH_PIPEBSRC, PCH_PIPEBSRC);
	PSB_WVDC32(hdmi_dev->savePCH_HTOTAL_B, PCH_HTOTAL_B);
	PSB_WVDC32(hdmi_dev->savePCH_HBLANK_B, PCH_HBLANK_B);
	PSB_WVDC32(hdmi_dev->savePCH_HSYNC_B,  PCH_HSYNC_B);
	PSB_WVDC32(hdmi_dev->savePCH_VTOTAL_B, PCH_VTOTAL_B);
	PSB_WVDC32(hdmi_dev->savePCH_VBLANK_B, PCH_VBLANK_B);
	PSB_WVDC32(hdmi_dev->savePCH_VSYNC_B,  PCH_VSYNC_B);

	PSB_WVDC32(pipeb->conf, PIPEBCONF);
	PSB_WVDC32(hdmi_dev->savePCH_PIPEBCONF, PCH_PIPEBCONF);

	/* plane */
	PSB_WVDC32(pipeb->linoff, DSPBLINOFF);
	PSB_WVDC32(pipeb->stride, DSPBSTRIDE);
	PSB_WVDC32(pipeb->tileoff, DSPBTILEOFF);
	PSB_WVDC32(pipeb->cntr, DSPBCNTR);
	PSB_WVDC32(pipeb->surf, DSPBSURF);

	/* cursor B */
	PSB_WVDC32(regs->saveDSPBCURSOR_CTRL, CURBCNTR);
	PSB_WVDC32(regs->saveDSPBCURSOR_POS, CURBPOS);
	PSB_WVDC32(regs->saveDSPBCURSOR_BASE, CURBBASE);

	/* restore palette */
	for (i = 0; i < 256; i++)
		PSB_WVDC32(pipeb->palette[i], PALETTE_B + (i << 2));
}
