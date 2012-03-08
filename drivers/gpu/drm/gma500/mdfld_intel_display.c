/*
 * Copyright Â© 2006-2007 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */

#include <linux/i2c.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include "psb_intel_reg.h"
#include "psb_intel_display.h"
#include "framebuffer.h"
#include "mdfld_output.h"
#include "mdfld_dsi_output.h"

/* Hardcoded currently */
static int ksel = KSEL_CRYSTAL_19;

struct psb_intel_range_t {
	int min, max;
};

struct mrst_limit_t {
	struct psb_intel_range_t dot, m, p1;
};

struct mrst_clock_t {
	/* derived values */
	int dot;
	int m;
	int p1;
};

#define COUNT_MAX 0x10000000

void mdfldWaitForPipeDisable(struct drm_device *dev, int pipe)
{
	int count, temp;
	u32 pipeconf_reg = PIPEACONF;

	switch (pipe) {
	case 0:
		break;
	case 1:
		pipeconf_reg = PIPEBCONF;
		break;
	case 2:
		pipeconf_reg = PIPECCONF;
		break;
	default:
		DRM_ERROR("Illegal Pipe Number.\n");
		return;
	}

	/* FIXME JLIU7_PO */
	psb_intel_wait_for_vblank(dev);
	return;

	/* Wait for for the pipe disable to take effect. */
	for (count = 0; count < COUNT_MAX; count++) {
		temp = REG_READ(pipeconf_reg);
		if ((temp & PIPEACONF_PIPE_STATE) == 0)
			break;
	}
}

void mdfldWaitForPipeEnable(struct drm_device *dev, int pipe)
{
	int count, temp;
	u32 pipeconf_reg = PIPEACONF;

	switch (pipe) {
	case 0:
		break;
	case 1:
		pipeconf_reg = PIPEBCONF;
		break;
	case 2:
		pipeconf_reg = PIPECCONF;
		break;
	default:
		DRM_ERROR("Illegal Pipe Number.\n");
		return;
	}

	/* FIXME JLIU7_PO */
	psb_intel_wait_for_vblank(dev);
	return;

	/* Wait for for the pipe enable to take effect. */
	for (count = 0; count < COUNT_MAX; count++) {
		temp = REG_READ(pipeconf_reg);
		if ((temp & PIPEACONF_PIPE_STATE) == 1)
			break;
	}
}

static void psb_intel_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void psb_intel_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
}

static bool psb_intel_crtc_mode_fixup(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

/**
 * Return the pipe currently connected to the panel fitter,
 * or -1 if the panel fitter is not present or not in use
 */
static int psb_intel_panel_fitter_pipe(struct drm_device *dev)
{
	u32 pfit_control;

	pfit_control = REG_READ(PFIT_CONTROL);

	/* See if the panel fitter is in use */
	if ((pfit_control & PFIT_ENABLE) == 0)
		return -1;

	/* 965 can place panel fitter on either pipe */
	return (pfit_control >> 29) & 0x3;
}

static struct drm_device globle_dev;

void mdfld__intel_plane_set_alpha(int enable)
{
	struct drm_device *dev = &globle_dev;
	int dspcntr_reg = DSPACNTR;
	u32 dspcntr;

	dspcntr = REG_READ(dspcntr_reg);

	if (enable) {
		dspcntr &= ~DISPPLANE_32BPP_NO_ALPHA;
		dspcntr |= DISPPLANE_32BPP;
	} else {
		dspcntr &= ~DISPPLANE_32BPP;
		dspcntr |= DISPPLANE_32BPP_NO_ALPHA;
	}

	REG_WRITE(dspcntr_reg, dspcntr);
}

static int check_fb(struct drm_framebuffer *fb)
{
	if (!fb)
		return 0;

	switch (fb->bits_per_pixel) {
	case 8:
	case 16:
	case 24:
	case 32:
		return 0;
	default:
		DRM_ERROR("Unknown color depth\n");
		return -EINVAL;
	}
}

static int mdfld__intel_pipe_set_base(struct drm_crtc *crtc, int x, int y,
				struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	/* struct drm_i915_master_private *master_priv; */
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	struct psb_framebuffer *psbfb = to_psb_fb(crtc->fb);
	int pipe = psb_intel_crtc->pipe;
	unsigned long start, offset;
	int dsplinoff = DSPALINOFF;
	int dspsurf = DSPASURF;
	int dspstride = DSPASTRIDE;
	int dspcntr_reg = DSPACNTR;
	u32 dspcntr;
	int ret;

	memcpy(&globle_dev, dev, sizeof(struct drm_device));

	dev_dbg(dev->dev, "pipe = 0x%x.\n", pipe);

	/* no fb bound */
	if (!crtc->fb) {
		dev_dbg(dev->dev, "No FB bound\n");
		return 0;
	}

	ret = check_fb(crtc->fb);
	if (ret)
		return ret;

	switch (pipe) {
	case 0:
		dsplinoff = DSPALINOFF;
		break;
	case 1:
		dsplinoff = DSPBLINOFF;
		dspsurf = DSPBSURF;
		dspstride = DSPBSTRIDE;
		dspcntr_reg = DSPBCNTR;
		break;
	case 2:
		dsplinoff = DSPCLINOFF;
		dspsurf = DSPCSURF;
		dspstride = DSPCSTRIDE;
		dspcntr_reg = DSPCCNTR;
		break;
	default:
		DRM_ERROR("Illegal Pipe Number.\n");
		return -EINVAL;
	}

	if (!gma_power_begin(dev, true))
		return 0;

	start = psbfb->gtt->offset;
	offset = y * crtc->fb->pitches[0] + x * (crtc->fb->bits_per_pixel / 8);

	REG_WRITE(dspstride, crtc->fb->pitches[0]);
	dspcntr = REG_READ(dspcntr_reg);
	dspcntr &= ~DISPPLANE_PIXFORMAT_MASK;

	switch (crtc->fb->bits_per_pixel) {
	case 8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case 16:
		if (crtc->fb->depth == 15)
			dspcntr |= DISPPLANE_15_16BPP;
		else
			dspcntr |= DISPPLANE_16BPP;
		break;
	case 24:
	case 32:
		dspcntr |= DISPPLANE_32BPP_NO_ALPHA;
		break;
	}
	REG_WRITE(dspcntr_reg, dspcntr);

	dev_dbg(dev->dev, "Writing base %08lX %08lX %d %d\n",
						start, offset, x, y);
	REG_WRITE(dsplinoff, offset);
	REG_READ(dsplinoff);
	REG_WRITE(dspsurf, start);
	REG_READ(dspsurf);

	gma_power_end(dev);

	return 0;
}

/*
 * Disable the pipe, plane and pll.
 *
 */
void mdfld_disable_crtc(struct drm_device *dev, int pipe)
{
	int dpll_reg = MRST_DPLL_A;
	int dspcntr_reg = DSPACNTR;
	int dspbase_reg = MRST_DSPABASE;
	int pipeconf_reg = PIPEACONF;
	u32 temp;

	dev_dbg(dev->dev, "pipe = %d\n", pipe);


	switch (pipe) {
	case 0:
		break;
	case 1:
		dpll_reg = MDFLD_DPLL_B;
		dspcntr_reg = DSPBCNTR;
		dspbase_reg = DSPBSURF;
		pipeconf_reg = PIPEBCONF;
		break;
	case 2:
		dpll_reg = MRST_DPLL_A;
		dspcntr_reg = DSPCCNTR;
		dspbase_reg = MDFLD_DSPCBASE;
		pipeconf_reg = PIPECCONF;
		break;
	default:
		DRM_ERROR("Illegal Pipe Number.\n");
		return;
	}

	if (pipe != 1)
		mdfld_dsi_gen_fifo_ready(dev, MIPI_GEN_FIFO_STAT_REG(pipe),
				HS_CTRL_FIFO_EMPTY | HS_DATA_FIFO_EMPTY);

	/* Disable display plane */
	temp = REG_READ(dspcntr_reg);
	if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
		REG_WRITE(dspcntr_reg,
			  temp & ~DISPLAY_PLANE_ENABLE);
		/* Flush the plane changes */
		REG_WRITE(dspbase_reg, REG_READ(dspbase_reg));
		REG_READ(dspbase_reg);
	}

	/* FIXME_JLIU7 MDFLD_PO revisit */

	/* Next, disable display pipes */
	temp = REG_READ(pipeconf_reg);
	if ((temp & PIPEACONF_ENABLE) != 0) {
		temp &= ~PIPEACONF_ENABLE;
		temp |= PIPECONF_PLANE_OFF | PIPECONF_CURSOR_OFF;
		REG_WRITE(pipeconf_reg, temp);
		REG_READ(pipeconf_reg);

		/* Wait for for the pipe disable to take effect. */
		mdfldWaitForPipeDisable(dev, pipe);
	}

	temp = REG_READ(dpll_reg);
	if (temp & DPLL_VCO_ENABLE) {
		if ((pipe != 1 &&
			!((REG_READ(PIPEACONF) | REG_READ(PIPECCONF))
				& PIPEACONF_ENABLE)) || pipe == 1) {
			temp &= ~(DPLL_VCO_ENABLE);
			REG_WRITE(dpll_reg, temp);
			REG_READ(dpll_reg);
			/* Wait for the clocks to turn off. */
			/* FIXME_MDFLD PO may need more delay */
			udelay(500);

			if (!(temp & MDFLD_PWR_GATE_EN)) {
				/* gating power of DPLL */
				REG_WRITE(dpll_reg, temp | MDFLD_PWR_GATE_EN);
				/* FIXME_MDFLD PO - change 500 to 1 after PO */
				udelay(5000);
			}
		}
	}

}

/**
 * Sets the power management mode of the pipe and plane.
 *
 * This code should probably grow support for turning the cursor off and back
 * on appropriately at the same time as we're turning the pipe off/on.
 */
static void mdfld_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	int dpll_reg = MRST_DPLL_A;
	int dspcntr_reg = DSPACNTR;
	int dspbase_reg = MRST_DSPABASE;
	int pipeconf_reg = PIPEACONF;
	u32 pipestat_reg = PIPEASTAT;
	u32 pipeconf = dev_priv->pipeconf[pipe];
	u32 temp;
	int timeout = 0;

	dev_dbg(dev->dev, "mode = %d, pipe = %d\n", mode, pipe);

/* FIXME_JLIU7 MDFLD_PO replaced w/ the following function */
/* mdfld_dbi_dpms (struct drm_device *dev, int pipe, bool enabled) */

	switch (pipe) {
	case 0:
		break;
	case 1:
		dpll_reg = DPLL_B;
		dspcntr_reg = DSPBCNTR;
		dspbase_reg = MRST_DSPBBASE;
		pipeconf_reg = PIPEBCONF;
		dpll_reg = MDFLD_DPLL_B;
		break;
	case 2:
		dpll_reg = MRST_DPLL_A;
		dspcntr_reg = DSPCCNTR;
		dspbase_reg = MDFLD_DSPCBASE;
		pipeconf_reg = PIPECCONF;
		pipestat_reg = PIPECSTAT;
		break;
	default:
		DRM_ERROR("Illegal Pipe Number.\n");
		return;
	}

	if (!gma_power_begin(dev, true))
		return;

	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */
	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		/* Enable the DPLL */
		temp = REG_READ(dpll_reg);

		if ((temp & DPLL_VCO_ENABLE) == 0) {
			/* When ungating power of DPLL, needs to wait 0.5us
			   before enable the VCO */
			if (temp & MDFLD_PWR_GATE_EN) {
				temp &= ~MDFLD_PWR_GATE_EN;
				REG_WRITE(dpll_reg, temp);
				/* FIXME_MDFLD PO - change 500 to 1 after PO */
				udelay(500);
			}

			REG_WRITE(dpll_reg, temp);
			REG_READ(dpll_reg);
			/* FIXME_MDFLD PO - change 500 to 1 after PO */
			udelay(500);

			REG_WRITE(dpll_reg, temp | DPLL_VCO_ENABLE);
			REG_READ(dpll_reg);

			/**
			 * wait for DSI PLL to lock
			 * NOTE: only need to poll status of pipe 0 and pipe 1,
			 * since both MIPI pipes share the same PLL.
			 */
			while ((pipe != 2) && (timeout < 20000) &&
			  !(REG_READ(pipeconf_reg) & PIPECONF_DSIPLL_LOCK)) {
				udelay(150);
				timeout++;
			}
		}

		/* Enable the plane */
		temp = REG_READ(dspcntr_reg);
		if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
			REG_WRITE(dspcntr_reg,
				temp | DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			REG_WRITE(dspbase_reg, REG_READ(dspbase_reg));
		}

		/* Enable the pipe */
		temp = REG_READ(pipeconf_reg);
		if ((temp & PIPEACONF_ENABLE) == 0) {
			REG_WRITE(pipeconf_reg, pipeconf);

			/* Wait for for the pipe enable to take effect. */
			mdfldWaitForPipeEnable(dev, pipe);
		}

		/*workaround for sighting 3741701 Random X blank display*/
		/*perform w/a in video mode only on pipe A or C*/
		if (pipe == 0 || pipe == 2) {
			REG_WRITE(pipestat_reg, REG_READ(pipestat_reg));
			msleep(100);
			if (PIPE_VBLANK_STATUS & REG_READ(pipestat_reg))
				dev_dbg(dev->dev, "OK");
			else {
				dev_dbg(dev->dev, "STUCK!!!!");
				/*shutdown controller*/
				temp = REG_READ(dspcntr_reg);
				REG_WRITE(dspcntr_reg,
						temp & ~DISPLAY_PLANE_ENABLE);
				REG_WRITE(dspbase_reg, REG_READ(dspbase_reg));
				/*mdfld_dsi_dpi_shut_down(dev, pipe);*/
				REG_WRITE(0xb048, 1);
				msleep(100);
				temp = REG_READ(pipeconf_reg);
				temp &= ~PIPEACONF_ENABLE;
				REG_WRITE(pipeconf_reg, temp);
				msleep(100); /*wait for pipe disable*/
				REG_WRITE(MIPI_DEVICE_READY_REG(pipe), 0);
				msleep(100);
				REG_WRITE(0xb004, REG_READ(0xb004));
				/* try to bring the controller back up again*/
				REG_WRITE(MIPI_DEVICE_READY_REG(pipe), 1);
				temp = REG_READ(dspcntr_reg);
				REG_WRITE(dspcntr_reg,
						temp | DISPLAY_PLANE_ENABLE);
				REG_WRITE(dspbase_reg, REG_READ(dspbase_reg));
				/*mdfld_dsi_dpi_turn_on(dev, pipe);*/
				REG_WRITE(0xb048, 2);
				msleep(100);
				temp = REG_READ(pipeconf_reg);
				temp |= PIPEACONF_ENABLE;
				REG_WRITE(pipeconf_reg, temp);
			}
		}

		psb_intel_crtc_load_lut(crtc);

		/* Give the overlay scaler a chance to enable
		   if it's on this pipe */
		/* psb_intel_crtc_dpms_video(crtc, true); TODO */

		break;
	case DRM_MODE_DPMS_OFF:
		/* Give the overlay scaler a chance to disable
		 * if it's on this pipe */
		/* psb_intel_crtc_dpms_video(crtc, FALSE); TODO */
		if (pipe != 1)
			mdfld_dsi_gen_fifo_ready(dev,
				MIPI_GEN_FIFO_STAT_REG(pipe),
				HS_CTRL_FIFO_EMPTY | HS_DATA_FIFO_EMPTY);

		/* Disable the VGA plane that we never use */
		REG_WRITE(VGACNTRL, VGA_DISP_DISABLE);

		/* Disable display plane */
		temp = REG_READ(dspcntr_reg);
		if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
			REG_WRITE(dspcntr_reg,
				  temp & ~DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			REG_WRITE(dspbase_reg, REG_READ(dspbase_reg));
			REG_READ(dspbase_reg);
		}

		/* Next, disable display pipes */
		temp = REG_READ(pipeconf_reg);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			temp &= ~PIPEACONF_ENABLE;
			temp |= PIPECONF_PLANE_OFF | PIPECONF_CURSOR_OFF;
			REG_WRITE(pipeconf_reg, temp);
			REG_READ(pipeconf_reg);

			/* Wait for for the pipe disable to take effect. */
			mdfldWaitForPipeDisable(dev, pipe);
		}

		temp = REG_READ(dpll_reg);
		if (temp & DPLL_VCO_ENABLE) {
			if ((pipe != 1 && !((REG_READ(PIPEACONF)
				| REG_READ(PIPECCONF)) & PIPEACONF_ENABLE))
					|| pipe == 1) {
				temp &= ~(DPLL_VCO_ENABLE);
				REG_WRITE(dpll_reg, temp);
				REG_READ(dpll_reg);
				/* Wait for the clocks to turn off. */
				/* FIXME_MDFLD PO may need more delay */
				udelay(500);
			}
		}
		break;
	}
	gma_power_end(dev);
}


#define MDFLD_LIMT_DPLL_19	    0
#define MDFLD_LIMT_DPLL_25	    1
#define MDFLD_LIMT_DPLL_83	    2
#define MDFLD_LIMT_DPLL_100	    3
#define MDFLD_LIMT_DSIPLL_19	    4
#define MDFLD_LIMT_DSIPLL_25	    5
#define MDFLD_LIMT_DSIPLL_83	    6
#define MDFLD_LIMT_DSIPLL_100	    7

#define MDFLD_DOT_MIN		  19750
#define MDFLD_DOT_MAX		  120000
#define MDFLD_DPLL_M_MIN_19	    113
#define MDFLD_DPLL_M_MAX_19	    155
#define MDFLD_DPLL_P1_MIN_19	    2
#define MDFLD_DPLL_P1_MAX_19	    10
#define MDFLD_DPLL_M_MIN_25	    101
#define MDFLD_DPLL_M_MAX_25	    130
#define MDFLD_DPLL_P1_MIN_25	    2
#define MDFLD_DPLL_P1_MAX_25	    10
#define MDFLD_DPLL_M_MIN_83	    64
#define MDFLD_DPLL_M_MAX_83	    64
#define MDFLD_DPLL_P1_MIN_83	    2
#define MDFLD_DPLL_P1_MAX_83	    2
#define MDFLD_DPLL_M_MIN_100	    64
#define MDFLD_DPLL_M_MAX_100	    64
#define MDFLD_DPLL_P1_MIN_100	    2
#define MDFLD_DPLL_P1_MAX_100	    2
#define MDFLD_DSIPLL_M_MIN_19	    131
#define MDFLD_DSIPLL_M_MAX_19	    175
#define MDFLD_DSIPLL_P1_MIN_19	    3
#define MDFLD_DSIPLL_P1_MAX_19	    8
#define MDFLD_DSIPLL_M_MIN_25	    97
#define MDFLD_DSIPLL_M_MAX_25	    140
#define MDFLD_DSIPLL_P1_MIN_25	    3
#define MDFLD_DSIPLL_P1_MAX_25	    9
#define MDFLD_DSIPLL_M_MIN_83	    33
#define MDFLD_DSIPLL_M_MAX_83	    92
#define MDFLD_DSIPLL_P1_MIN_83	    2
#define MDFLD_DSIPLL_P1_MAX_83	    3
#define MDFLD_DSIPLL_M_MIN_100	    97
#define MDFLD_DSIPLL_M_MAX_100	    140
#define MDFLD_DSIPLL_P1_MIN_100	    3
#define MDFLD_DSIPLL_P1_MAX_100	    9

static const struct mrst_limit_t mdfld_limits[] = {
	{			/* MDFLD_LIMT_DPLL_19 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DPLL_M_MIN_19, .max = MDFLD_DPLL_M_MAX_19},
	 .p1 = {.min = MDFLD_DPLL_P1_MIN_19, .max = MDFLD_DPLL_P1_MAX_19},
	 },
	{			/* MDFLD_LIMT_DPLL_25 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DPLL_M_MIN_25, .max = MDFLD_DPLL_M_MAX_25},
	 .p1 = {.min = MDFLD_DPLL_P1_MIN_25, .max = MDFLD_DPLL_P1_MAX_25},
	 },
	{			/* MDFLD_LIMT_DPLL_83 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DPLL_M_MIN_83, .max = MDFLD_DPLL_M_MAX_83},
	 .p1 = {.min = MDFLD_DPLL_P1_MIN_83, .max = MDFLD_DPLL_P1_MAX_83},
	 },
	{			/* MDFLD_LIMT_DPLL_100 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DPLL_M_MIN_100, .max = MDFLD_DPLL_M_MAX_100},
	 .p1 = {.min = MDFLD_DPLL_P1_MIN_100, .max = MDFLD_DPLL_P1_MAX_100},
	 },
	{			/* MDFLD_LIMT_DSIPLL_19 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DSIPLL_M_MIN_19, .max = MDFLD_DSIPLL_M_MAX_19},
	 .p1 = {.min = MDFLD_DSIPLL_P1_MIN_19, .max = MDFLD_DSIPLL_P1_MAX_19},
	 },
	{			/* MDFLD_LIMT_DSIPLL_25 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DSIPLL_M_MIN_25, .max = MDFLD_DSIPLL_M_MAX_25},
	 .p1 = {.min = MDFLD_DSIPLL_P1_MIN_25, .max = MDFLD_DSIPLL_P1_MAX_25},
	 },
	{			/* MDFLD_LIMT_DSIPLL_83 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DSIPLL_M_MIN_83, .max = MDFLD_DSIPLL_M_MAX_83},
	 .p1 = {.min = MDFLD_DSIPLL_P1_MIN_83, .max = MDFLD_DSIPLL_P1_MAX_83},
	 },
	{			/* MDFLD_LIMT_DSIPLL_100 */
	 .dot = {.min = MDFLD_DOT_MIN, .max = MDFLD_DOT_MAX},
	 .m = {.min = MDFLD_DSIPLL_M_MIN_100, .max = MDFLD_DSIPLL_M_MAX_100},
	 .p1 = {.min = MDFLD_DSIPLL_P1_MIN_100, .max = MDFLD_DSIPLL_P1_MAX_100},
	 },
};

#define MDFLD_M_MIN	    21
#define MDFLD_M_MAX	    180
static const u32 mdfld_m_converts[] = {
/* M configuration table from 9-bit LFSR table */
	224, 368, 440, 220, 366, 439, 219, 365, 182, 347, /* 21 - 30 */
	173, 342, 171, 85, 298, 149, 74, 37, 18, 265,   /* 31 - 40 */
	388, 194, 353, 432, 216, 108, 310, 155, 333, 166, /* 41 - 50 */
	83, 41, 276, 138, 325, 162, 337, 168, 340, 170, /* 51 - 60 */
	341, 426, 469, 234, 373, 442, 221, 110, 311, 411, /* 61 - 70 */
	461, 486, 243, 377, 188, 350, 175, 343, 427, 213, /* 71 - 80 */
	106, 53, 282, 397, 354, 227, 113, 56, 284, 142, /* 81 - 90 */
	71, 35, 273, 136, 324, 418, 465, 488, 500, 506, /* 91 - 100 */
	253, 126, 63, 287, 399, 455, 483, 241, 376, 444, /* 101 - 110 */
	478, 495, 503, 251, 381, 446, 479, 239, 375, 443, /* 111 - 120 */
	477, 238, 119, 315, 157, 78, 295, 147, 329, 420, /* 121 - 130 */
	210, 105, 308, 154, 77, 38, 275, 137, 68, 290, /* 131 - 140 */
	145, 328, 164, 82, 297, 404, 458, 485, 498, 249, /* 141 - 150 */
	380, 190, 351, 431, 471, 235, 117, 314, 413, 206, /* 151 - 160 */
	103, 51, 25, 12, 262, 387, 193, 96, 48, 280, /* 161 - 170 */
	396, 198, 99, 305, 152, 76, 294, 403, 457, 228, /* 171 - 180 */
};

static const struct mrst_limit_t *mdfld_limit(struct drm_crtc *crtc)
{
	const struct mrst_limit_t *limit = NULL;
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (psb_intel_pipe_has_type(crtc, INTEL_OUTPUT_MIPI)
	    || psb_intel_pipe_has_type(crtc, INTEL_OUTPUT_MIPI2)) {
		if ((ksel == KSEL_CRYSTAL_19) || (ksel == KSEL_BYPASS_19))
			limit = &mdfld_limits[MDFLD_LIMT_DSIPLL_19];
		else if (ksel == KSEL_BYPASS_25)
			limit = &mdfld_limits[MDFLD_LIMT_DSIPLL_25];
		else if ((ksel == KSEL_BYPASS_83_100) &&
				(dev_priv->core_freq == 166))
			limit = &mdfld_limits[MDFLD_LIMT_DSIPLL_83];
		else if ((ksel == KSEL_BYPASS_83_100) &&
			 (dev_priv->core_freq == 100 ||
				dev_priv->core_freq == 200))
			limit = &mdfld_limits[MDFLD_LIMT_DSIPLL_100];
	} else if (psb_intel_pipe_has_type(crtc, INTEL_OUTPUT_HDMI)) {
		if ((ksel == KSEL_CRYSTAL_19) || (ksel == KSEL_BYPASS_19))
			limit = &mdfld_limits[MDFLD_LIMT_DPLL_19];
		else if (ksel == KSEL_BYPASS_25)
			limit = &mdfld_limits[MDFLD_LIMT_DPLL_25];
		else if ((ksel == KSEL_BYPASS_83_100) &&
				(dev_priv->core_freq == 166))
			limit = &mdfld_limits[MDFLD_LIMT_DPLL_83];
		else if ((ksel == KSEL_BYPASS_83_100) &&
				 (dev_priv->core_freq == 100 ||
				 dev_priv->core_freq == 200))
			limit = &mdfld_limits[MDFLD_LIMT_DPLL_100];
	} else {
		limit = NULL;
		dev_dbg(dev->dev, "mdfld_limit Wrong display type.\n");
	}

	return limit;
}

/** Derive the pixel clock for the given refclk and divisors for 8xx chips. */
static void mdfld_clock(int refclk, struct mrst_clock_t *clock)
{
	clock->dot = (refclk * clock->m) / clock->p1;
}

/**
 * Returns a set of divisors for the desired target clock with the given refclk,
 * or FALSE.  Divisor values are the actual divisors for
 */
static bool
mdfldFindBestPLL(struct drm_crtc *crtc, int target, int refclk,
		struct mrst_clock_t *best_clock)
{
	struct mrst_clock_t clock;
	const struct mrst_limit_t *limit = mdfld_limit(crtc);
	int err = target;

	memset(best_clock, 0, sizeof(*best_clock));

	for (clock.m = limit->m.min; clock.m <= limit->m.max; clock.m++) {
		for (clock.p1 = limit->p1.min; clock.p1 <= limit->p1.max;
		     clock.p1++) {
			int this_err;

			mdfld_clock(refclk, &clock);

			this_err = abs(clock.dot - target);
			if (this_err < err) {
				*best_clock = clock;
				err = this_err;
			}
		}
	}
	return err != target;
}

static int mdfld_crtc_mode_set(struct drm_crtc *crtc,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode,
			      int x, int y,
			      struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int pipe = psb_intel_crtc->pipe;
	int fp_reg = MRST_FPA0;
	int dpll_reg = MRST_DPLL_A;
	int dspcntr_reg = DSPACNTR;
	int pipeconf_reg = PIPEACONF;
	int htot_reg = HTOTAL_A;
	int hblank_reg = HBLANK_A;
	int hsync_reg = HSYNC_A;
	int vtot_reg = VTOTAL_A;
	int vblank_reg = VBLANK_A;
	int vsync_reg = VSYNC_A;
	int dspsize_reg = DSPASIZE;
	int dsppos_reg = DSPAPOS;
	int pipesrc_reg = PIPEASRC;
	u32 *pipeconf = &dev_priv->pipeconf[pipe];
	u32 *dspcntr = &dev_priv->dspcntr[pipe];
	int refclk = 0;
	int clk_n = 0, clk_p2 = 0, clk_byte = 1, clk = 0, m_conv = 0,
								clk_tmp = 0;
	struct mrst_clock_t clock;
	bool ok;
	u32 dpll = 0, fp = 0;
	bool is_mipi = false, is_mipi2 = false, is_hdmi = false;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct psb_intel_encoder *psb_intel_encoder = NULL;
	uint64_t scalingType = DRM_MODE_SCALE_FULLSCREEN;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int timeout = 0;
	int ret;

	dev_dbg(dev->dev, "pipe = 0x%x\n", pipe);

#if 0
	if (pipe == 1) {
		if (!gma_power_begin(dev, true))
			return 0;
		android_hdmi_crtc_mode_set(crtc, mode, adjusted_mode,
			x, y, old_fb);
		goto mrst_crtc_mode_set_exit;
	}
#endif

	switch (pipe) {
	case 0:
		break;
	case 1:
		fp_reg = FPB0;
		dpll_reg = DPLL_B;
		dspcntr_reg = DSPBCNTR;
		pipeconf_reg = PIPEBCONF;
		htot_reg = HTOTAL_B;
		hblank_reg = HBLANK_B;
		hsync_reg = HSYNC_B;
		vtot_reg = VTOTAL_B;
		vblank_reg = VBLANK_B;
		vsync_reg = VSYNC_B;
		dspsize_reg = DSPBSIZE;
		dsppos_reg = DSPBPOS;
		pipesrc_reg = PIPEBSRC;
		fp_reg = MDFLD_DPLL_DIV0;
		dpll_reg = MDFLD_DPLL_B;
		break;
	case 2:
		dpll_reg = MRST_DPLL_A;
		dspcntr_reg = DSPCCNTR;
		pipeconf_reg = PIPECCONF;
		htot_reg = HTOTAL_C;
		hblank_reg = HBLANK_C;
		hsync_reg = HSYNC_C;
		vtot_reg = VTOTAL_C;
		vblank_reg = VBLANK_C;
		vsync_reg = VSYNC_C;
		dspsize_reg = DSPCSIZE;
		dsppos_reg = DSPCPOS;
		pipesrc_reg = PIPECSRC;
		break;
	default:
		DRM_ERROR("Illegal Pipe Number.\n");
		return 0;
	}

	ret = check_fb(crtc->fb);
	if (ret)
		return ret;

	dev_dbg(dev->dev, "adjusted_hdisplay = %d\n",
		 adjusted_mode->hdisplay);
	dev_dbg(dev->dev, "adjusted_vdisplay = %d\n",
		 adjusted_mode->vdisplay);
	dev_dbg(dev->dev, "adjusted_hsync_start = %d\n",
		 adjusted_mode->hsync_start);
	dev_dbg(dev->dev, "adjusted_hsync_end = %d\n",
		 adjusted_mode->hsync_end);
	dev_dbg(dev->dev, "adjusted_htotal = %d\n",
		 adjusted_mode->htotal);
	dev_dbg(dev->dev, "adjusted_vsync_start = %d\n",
		 adjusted_mode->vsync_start);
	dev_dbg(dev->dev, "adjusted_vsync_end = %d\n",
		 adjusted_mode->vsync_end);
	dev_dbg(dev->dev, "adjusted_vtotal = %d\n",
		 adjusted_mode->vtotal);
	dev_dbg(dev->dev, "adjusted_clock = %d\n",
		 adjusted_mode->clock);
	dev_dbg(dev->dev, "hdisplay = %d\n",
		 mode->hdisplay);
	dev_dbg(dev->dev, "vdisplay = %d\n",
		 mode->vdisplay);

	if (!gma_power_begin(dev, true))
		return 0;

	memcpy(&psb_intel_crtc->saved_mode, mode,
					sizeof(struct drm_display_mode));
	memcpy(&psb_intel_crtc->saved_adjusted_mode, adjusted_mode,
					sizeof(struct drm_display_mode));

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		if (!connector)
			continue;

		encoder = connector->encoder;

		if (!encoder)
			continue;

		if (encoder->crtc != crtc)
			continue;

		psb_intel_encoder = psb_intel_attached_encoder(connector);

		switch (psb_intel_encoder->type) {
		case INTEL_OUTPUT_MIPI:
			is_mipi = true;
			break;
		case INTEL_OUTPUT_MIPI2:
			is_mipi2 = true;
			break;
		case INTEL_OUTPUT_HDMI:
			is_hdmi = true;
			break;
		}
	}

	/* Disable the VGA plane that we never use */
	REG_WRITE(VGACNTRL, VGA_DISP_DISABLE);

	/* Disable the panel fitter if it was on our pipe */
	if (psb_intel_panel_fitter_pipe(dev) == pipe)
		REG_WRITE(PFIT_CONTROL, 0);

	/* pipesrc and dspsize control the size that is scaled from,
	 * which should always be the user's requested size.
	 */
	if (pipe == 1) {
		/* FIXME: To make HDMI display with 864x480 (TPO), 480x864
		 * (PYR) or 480x854 (TMD), set the sprite width/height and
		 * souce image size registers with the adjusted mode for
		 * pipe B.
		 */

		/*
		 * The defined sprite rectangle must always be completely
		 * contained within the displayable area of the screen image
		 * (frame buffer).
		 */
		REG_WRITE(dspsize_reg, ((min(mode->crtc_vdisplay, adjusted_mode->crtc_vdisplay) - 1) << 16)
				| (min(mode->crtc_hdisplay, adjusted_mode->crtc_hdisplay) - 1));
		/* Set the CRTC with encoder mode. */
		REG_WRITE(pipesrc_reg, ((mode->crtc_hdisplay - 1) << 16)
				 | (mode->crtc_vdisplay - 1));
	} else {
		REG_WRITE(dspsize_reg,
				((mode->crtc_vdisplay - 1) << 16) |
						(mode->crtc_hdisplay - 1));
		REG_WRITE(pipesrc_reg,
				((mode->crtc_hdisplay - 1) << 16) |
						(mode->crtc_vdisplay - 1));
	}

	REG_WRITE(dsppos_reg, 0);

	if (psb_intel_encoder)
		drm_connector_property_get_value(connector,
			dev->mode_config.scaling_mode_property, &scalingType);

	if (scalingType == DRM_MODE_SCALE_NO_SCALE) {
		/* Medfield doesn't have register support for centering so we
		 * need to mess with the h/vblank and h/vsync start and ends
		 * to get centering
		 */
		int offsetX = 0, offsetY = 0;

		offsetX = (adjusted_mode->crtc_hdisplay -
					mode->crtc_hdisplay) / 2;
		offsetY = (adjusted_mode->crtc_vdisplay -
					mode->crtc_vdisplay) / 2;

		REG_WRITE(htot_reg, (mode->crtc_hdisplay - 1) |
			((adjusted_mode->crtc_htotal - 1) << 16));
		REG_WRITE(vtot_reg, (mode->crtc_vdisplay - 1) |
			((adjusted_mode->crtc_vtotal - 1) << 16));
		REG_WRITE(hblank_reg, (adjusted_mode->crtc_hblank_start -
								offsetX - 1) |
			((adjusted_mode->crtc_hblank_end - offsetX - 1) << 16));
		REG_WRITE(hsync_reg, (adjusted_mode->crtc_hsync_start -
								offsetX - 1) |
			((adjusted_mode->crtc_hsync_end - offsetX - 1) << 16));
		REG_WRITE(vblank_reg, (adjusted_mode->crtc_vblank_start -
								offsetY - 1) |
			((adjusted_mode->crtc_vblank_end - offsetY - 1) << 16));
		REG_WRITE(vsync_reg, (adjusted_mode->crtc_vsync_start -
								offsetY - 1) |
			((adjusted_mode->crtc_vsync_end - offsetY - 1) << 16));
	} else {
		REG_WRITE(htot_reg, (adjusted_mode->crtc_hdisplay - 1) |
			((adjusted_mode->crtc_htotal - 1) << 16));
		REG_WRITE(vtot_reg, (adjusted_mode->crtc_vdisplay - 1) |
			((adjusted_mode->crtc_vtotal - 1) << 16));
		REG_WRITE(hblank_reg, (adjusted_mode->crtc_hblank_start - 1) |
			((adjusted_mode->crtc_hblank_end - 1) << 16));
		REG_WRITE(hsync_reg, (adjusted_mode->crtc_hsync_start - 1) |
			((adjusted_mode->crtc_hsync_end - 1) << 16));
		REG_WRITE(vblank_reg, (adjusted_mode->crtc_vblank_start - 1) |
			((adjusted_mode->crtc_vblank_end - 1) << 16));
		REG_WRITE(vsync_reg, (adjusted_mode->crtc_vsync_start - 1) |
			((adjusted_mode->crtc_vsync_end - 1) << 16));
	}

	/* Flush the plane changes */
	{
		struct drm_crtc_helper_funcs *crtc_funcs =
		    crtc->helper_private;
		crtc_funcs->mode_set_base(crtc, x, y, old_fb);
	}

	/* setup pipeconf */
	*pipeconf = PIPEACONF_ENABLE; /* FIXME_JLIU7 REG_READ(pipeconf_reg); */

	/* Set up the display plane register */
	*dspcntr = REG_READ(dspcntr_reg);
	*dspcntr |= pipe << DISPPLANE_SEL_PIPE_POS;
	*dspcntr |= DISPLAY_PLANE_ENABLE;

	if (is_mipi2)
		goto mrst_crtc_mode_set_exit;
	clk = adjusted_mode->clock;

	if (is_hdmi) {
		if ((ksel == KSEL_CRYSTAL_19) || (ksel == KSEL_BYPASS_19)) {
			refclk = 19200;

			if (is_mipi || is_mipi2)
				clk_n = 1, clk_p2 = 8;
			else if (is_hdmi)
				clk_n = 1, clk_p2 = 10;
		} else if (ksel == KSEL_BYPASS_25) {
			refclk = 25000;

			if (is_mipi || is_mipi2)
				clk_n = 1, clk_p2 = 8;
			else if (is_hdmi)
				clk_n = 1, clk_p2 = 10;
		} else if ((ksel == KSEL_BYPASS_83_100) &&
					dev_priv->core_freq == 166) {
			refclk = 83000;

			if (is_mipi || is_mipi2)
				clk_n = 4, clk_p2 = 8;
			else if (is_hdmi)
				clk_n = 4, clk_p2 = 10;
		} else if ((ksel == KSEL_BYPASS_83_100) &&
					(dev_priv->core_freq == 100 ||
					dev_priv->core_freq == 200)) {
			refclk = 100000;
			if (is_mipi || is_mipi2)
				clk_n = 4, clk_p2 = 8;
			else if (is_hdmi)
				clk_n = 4, clk_p2 = 10;
		}

		if (is_mipi)
			clk_byte = dev_priv->bpp / 8;
		else if (is_mipi2)
			clk_byte = dev_priv->bpp2 / 8;

		clk_tmp = clk * clk_n * clk_p2 * clk_byte;

		dev_dbg(dev->dev, "clk = %d, clk_n = %d, clk_p2 = %d.\n",
					clk, clk_n, clk_p2);
		dev_dbg(dev->dev, "adjusted_mode->clock = %d, clk_tmp = %d.\n",
					adjusted_mode->clock, clk_tmp);

		ok = mdfldFindBestPLL(crtc, clk_tmp, refclk, &clock);

		if (!ok) {
			DRM_ERROR
			    ("mdfldFindBestPLL fail in mdfld_crtc_mode_set.\n");
		} else {
			m_conv = mdfld_m_converts[(clock.m - MDFLD_M_MIN)];

			dev_dbg(dev->dev, "dot clock = %d,"
				 "m = %d, p1 = %d, m_conv = %d.\n",
					clock.dot, clock.m,
					clock.p1, m_conv);
		}

		dpll = REG_READ(dpll_reg);

		if (dpll & DPLL_VCO_ENABLE) {
			dpll &= ~DPLL_VCO_ENABLE;
			REG_WRITE(dpll_reg, dpll);
			REG_READ(dpll_reg);

			/* FIXME jliu7 check the DPLL lock bit PIPEACONF[29] */
			/* FIXME_MDFLD PO - change 500 to 1 after PO */
			udelay(500);

			/* reset M1, N1 & P1 */
			REG_WRITE(fp_reg, 0);
			dpll &= ~MDFLD_P1_MASK;
			REG_WRITE(dpll_reg, dpll);
			/* FIXME_MDFLD PO - change 500 to 1 after PO */
			udelay(500);
		}

		/* When ungating power of DPLL, needs to wait 0.5us before
		 * enable the VCO */
		if (dpll & MDFLD_PWR_GATE_EN) {
			dpll &= ~MDFLD_PWR_GATE_EN;
			REG_WRITE(dpll_reg, dpll);
			/* FIXME_MDFLD PO - change 500 to 1 after PO */
			udelay(500);
		}
		dpll = 0;

#if 0 /* FIXME revisit later */
		if (ksel == KSEL_CRYSTAL_19 || ksel == KSEL_BYPASS_19 ||
						ksel == KSEL_BYPASS_25)
			dpll &= ~MDFLD_INPUT_REF_SEL;
		else if (ksel == KSEL_BYPASS_83_100)
			dpll |= MDFLD_INPUT_REF_SEL;
#endif /* FIXME revisit later */

		if (is_hdmi)
			dpll |= MDFLD_VCO_SEL;

		fp = (clk_n / 2) << 16;
		fp |= m_conv;

		/* compute bitmask from p1 value */
		dpll |= (1 << (clock.p1 - 2)) << 17;

#if 0 /* 1080p30 & 720p */
		dpll = 0x00050000;
		fp = 0x000001be;
#endif
#if 0 /* 480p */
		dpll = 0x02010000;
		fp = 0x000000d2;
#endif
	} else {
#if 0 /*DBI_TPO_480x864*/
		dpll = 0x00020000;
		fp = 0x00000156;
#endif /* DBI_TPO_480x864 */ /* get from spec. */

		dpll = 0x00800000;
		fp = 0x000000c1;
	}

	REG_WRITE(fp_reg, fp);
	REG_WRITE(dpll_reg, dpll);
	/* FIXME_MDFLD PO - change 500 to 1 after PO */
	udelay(500);

	dpll |= DPLL_VCO_ENABLE;
	REG_WRITE(dpll_reg, dpll);
	REG_READ(dpll_reg);

	/* wait for DSI PLL to lock */
	while (timeout < 20000 &&
			!(REG_READ(pipeconf_reg) & PIPECONF_DSIPLL_LOCK)) {
		udelay(150);
		timeout++;
	}

	if (is_mipi)
		goto mrst_crtc_mode_set_exit;

	dev_dbg(dev->dev, "is_mipi = 0x%x\n", is_mipi);

	REG_WRITE(pipeconf_reg, *pipeconf);
	REG_READ(pipeconf_reg);

	/* Wait for for the pipe enable to take effect. */
	REG_WRITE(dspcntr_reg, *dspcntr);
	psb_intel_wait_for_vblank(dev);

mrst_crtc_mode_set_exit:

	gma_power_end(dev);

	return 0;
}

const struct drm_crtc_helper_funcs mdfld_helper_funcs = {
	.dpms = mdfld_crtc_dpms,
	.mode_fixup = psb_intel_crtc_mode_fixup,
	.mode_set = mdfld_crtc_mode_set,
	.mode_set_base = mdfld__intel_pipe_set_base,
	.prepare = psb_intel_crtc_prepare,
	.commit = psb_intel_crtc_commit,
};

