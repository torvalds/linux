/*
 * Copyright © 2006-2011 Intel Corporation
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
#include "framebuffer.h"
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "psb_intel_display.h"
#include "power.h"
#include "cdv_device.h"

static bool cdv_intel_find_dp_pll(const struct gma_limit_t *limit,
				  struct drm_crtc *crtc, int target,
				  int refclk, struct gma_clock_t *best_clock);


#define CDV_LIMIT_SINGLE_LVDS_96	0
#define CDV_LIMIT_SINGLE_LVDS_100	1
#define CDV_LIMIT_DAC_HDMI_27		2
#define CDV_LIMIT_DAC_HDMI_96		3
#define CDV_LIMIT_DP_27			4
#define CDV_LIMIT_DP_100		5

static const struct gma_limit_t cdv_intel_limits[] = {
	{			/* CDV_SINGLE_LVDS_96MHz */
	 .dot = {.min = 20000, .max = 115500},
	 .vco = {.min = 1800000, .max = 3600000},
	 .n = {.min = 2, .max = 6},
	 .m = {.min = 60, .max = 160},
	 .m1 = {.min = 0, .max = 0},
	 .m2 = {.min = 58, .max = 158},
	 .p = {.min = 28, .max = 140},
	 .p1 = {.min = 2, .max = 10},
	 .p2 = {.dot_limit = 200000, .p2_slow = 14, .p2_fast = 14},
	 .find_pll = gma_find_best_pll,
	 },
	{			/* CDV_SINGLE_LVDS_100MHz */
	 .dot = {.min = 20000, .max = 115500},
	 .vco = {.min = 1800000, .max = 3600000},
	 .n = {.min = 2, .max = 6},
	 .m = {.min = 60, .max = 160},
	 .m1 = {.min = 0, .max = 0},
	 .m2 = {.min = 58, .max = 158},
	 .p = {.min = 28, .max = 140},
	 .p1 = {.min = 2, .max = 10},
	 /* The single-channel range is 25-112Mhz, and dual-channel
	  * is 80-224Mhz.  Prefer single channel as much as possible.
	  */
	 .p2 = {.dot_limit = 200000, .p2_slow = 14, .p2_fast = 14},
	 .find_pll = gma_find_best_pll,
	 },
	{			/* CDV_DAC_HDMI_27MHz */
	 .dot = {.min = 20000, .max = 400000},
	 .vco = {.min = 1809000, .max = 3564000},
	 .n = {.min = 1, .max = 1},
	 .m = {.min = 67, .max = 132},
	 .m1 = {.min = 0, .max = 0},
	 .m2 = {.min = 65, .max = 130},
	 .p = {.min = 5, .max = 90},
	 .p1 = {.min = 1, .max = 9},
	 .p2 = {.dot_limit = 225000, .p2_slow = 10, .p2_fast = 5},
	 .find_pll = gma_find_best_pll,
	 },
	{			/* CDV_DAC_HDMI_96MHz */
	 .dot = {.min = 20000, .max = 400000},
	 .vco = {.min = 1800000, .max = 3600000},
	 .n = {.min = 2, .max = 6},
	 .m = {.min = 60, .max = 160},
	 .m1 = {.min = 0, .max = 0},
	 .m2 = {.min = 58, .max = 158},
	 .p = {.min = 5, .max = 100},
	 .p1 = {.min = 1, .max = 10},
	 .p2 = {.dot_limit = 225000, .p2_slow = 10, .p2_fast = 5},
	 .find_pll = gma_find_best_pll,
	 },
	{			/* CDV_DP_27MHz */
	 .dot = {.min = 160000, .max = 272000},
	 .vco = {.min = 1809000, .max = 3564000},
	 .n = {.min = 1, .max = 1},
	 .m = {.min = 67, .max = 132},
	 .m1 = {.min = 0, .max = 0},
	 .m2 = {.min = 65, .max = 130},
	 .p = {.min = 5, .max = 90},
	 .p1 = {.min = 1, .max = 9},
	 .p2 = {.dot_limit = 225000, .p2_slow = 10, .p2_fast = 10},
	 .find_pll = cdv_intel_find_dp_pll,
	 },
	{			/* CDV_DP_100MHz */
	 .dot = {.min = 160000, .max = 272000},
	 .vco = {.min = 1800000, .max = 3600000},
	 .n = {.min = 2, .max = 6},
	 .m = {.min = 60, .max = 164},
	 .m1 = {.min = 0, .max = 0},
	 .m2 = {.min = 58, .max = 162},
	 .p = {.min = 5, .max = 100},
	 .p1 = {.min = 1, .max = 10},
	 .p2 = {.dot_limit = 225000, .p2_slow = 10, .p2_fast = 10},
	 .find_pll = cdv_intel_find_dp_pll,
	 }	
};

#define _wait_for(COND, MS, W) ({ \
	unsigned long timeout__ = jiffies + msecs_to_jiffies(MS);	\
	int ret__ = 0;							\
	while (!(COND)) {						\
		if (time_after(jiffies, timeout__)) {			\
			ret__ = -ETIMEDOUT;				\
			break;						\
		}							\
		if (W && !in_dbg_master())				\
			msleep(W);					\
	}								\
	ret__;								\
})

#define wait_for(COND, MS) _wait_for(COND, MS, 1)


int cdv_sb_read(struct drm_device *dev, u32 reg, u32 *val)
{
	int ret;

	ret = wait_for((REG_READ(SB_PCKT) & SB_BUSY) == 0, 1000);
	if (ret) {
		DRM_ERROR("timeout waiting for SB to idle before read\n");
		return ret;
	}

	REG_WRITE(SB_ADDR, reg);
	REG_WRITE(SB_PCKT,
		   SET_FIELD(SB_OPCODE_READ, SB_OPCODE) |
		   SET_FIELD(SB_DEST_DPLL, SB_DEST) |
		   SET_FIELD(0xf, SB_BYTE_ENABLE));

	ret = wait_for((REG_READ(SB_PCKT) & SB_BUSY) == 0, 1000);
	if (ret) {
		DRM_ERROR("timeout waiting for SB to idle after read\n");
		return ret;
	}

	*val = REG_READ(SB_DATA);

	return 0;
}

int cdv_sb_write(struct drm_device *dev, u32 reg, u32 val)
{
	int ret;
	static bool dpio_debug = true;
	u32 temp;

	if (dpio_debug) {
		if (cdv_sb_read(dev, reg, &temp) == 0)
			DRM_DEBUG_KMS("0x%08x: 0x%08x (before)\n", reg, temp);
		DRM_DEBUG_KMS("0x%08x: 0x%08x\n", reg, val);
	}

	ret = wait_for((REG_READ(SB_PCKT) & SB_BUSY) == 0, 1000);
	if (ret) {
		DRM_ERROR("timeout waiting for SB to idle before write\n");
		return ret;
	}

	REG_WRITE(SB_ADDR, reg);
	REG_WRITE(SB_DATA, val);
	REG_WRITE(SB_PCKT,
		   SET_FIELD(SB_OPCODE_WRITE, SB_OPCODE) |
		   SET_FIELD(SB_DEST_DPLL, SB_DEST) |
		   SET_FIELD(0xf, SB_BYTE_ENABLE));

	ret = wait_for((REG_READ(SB_PCKT) & SB_BUSY) == 0, 1000);
	if (ret) {
		DRM_ERROR("timeout waiting for SB to idle after write\n");
		return ret;
	}

	if (dpio_debug) {
		if (cdv_sb_read(dev, reg, &temp) == 0)
			DRM_DEBUG_KMS("0x%08x: 0x%08x (after)\n", reg, temp);
	}

	return 0;
}

/* Reset the DPIO configuration register.  The BIOS does this at every
 * mode set.
 */
void cdv_sb_reset(struct drm_device *dev)
{

	REG_WRITE(DPIO_CFG, 0);
	REG_READ(DPIO_CFG);
	REG_WRITE(DPIO_CFG, DPIO_MODE_SELECT_0 | DPIO_CMN_RESET_N);
}

/* Unlike most Intel display engines, on Cedarview the DPLL registers
 * are behind this sideband bus.  They must be programmed while the
 * DPLL reference clock is on in the DPLL control register, but before
 * the DPLL is enabled in the DPLL control register.
 */
static int
cdv_dpll_set_clock_cdv(struct drm_device *dev, struct drm_crtc *crtc,
		       struct gma_clock_t *clock, bool is_lvds, u32 ddi_select)
{
	struct psb_intel_crtc *psb_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_crtc->pipe;
	u32 m, n_vco, p;
	int ret = 0;
	int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;
	int ref_sfr = (pipe == 0) ? SB_REF_DPLLA : SB_REF_DPLLB;
	u32 ref_value;
	u32 lane_reg, lane_value;

	cdv_sb_reset(dev);

	REG_WRITE(dpll_reg, DPLL_SYNCLOCK_ENABLE | DPLL_VGA_MODE_DIS);

	udelay(100);

	/* Follow the BIOS and write the REF/SFR Register. Hardcoded value */
	ref_value = 0x68A701;

	cdv_sb_write(dev, SB_REF_SFR(pipe), ref_value);

	/* We don't know what the other fields of these regs are, so
	 * leave them in place.
	 */
	/* 
	 * The BIT 14:13 of 0x8010/0x8030 is used to select the ref clk
	 * for the pipe A/B. Display spec 1.06 has wrong definition.
	 * Correct definition is like below:
	 *
	 * refclka mean use clock from same PLL
	 *
	 * if DPLLA sets 01 and DPLLB sets 01, they use clock from their pll
	 *
	 * if DPLLA sets 01 and DPLLB sets 02, both use clk from DPLLA
	 *
	 */  
	ret = cdv_sb_read(dev, ref_sfr, &ref_value);
	if (ret)
		return ret;
	ref_value &= ~(REF_CLK_MASK);

	/* use DPLL_A for pipeB on CRT/HDMI */
	if (pipe == 1 && !is_lvds && !(ddi_select & DP_MASK)) {
		DRM_DEBUG_KMS("use DPLLA for pipe B\n");
		ref_value |= REF_CLK_DPLLA;
	} else {
		DRM_DEBUG_KMS("use their DPLL for pipe A/B\n");
		ref_value |= REF_CLK_DPLL;
	}
	ret = cdv_sb_write(dev, ref_sfr, ref_value);
	if (ret)
		return ret;

	ret = cdv_sb_read(dev, SB_M(pipe), &m);
	if (ret)
		return ret;
	m &= ~SB_M_DIVIDER_MASK;
	m |= ((clock->m2) << SB_M_DIVIDER_SHIFT);
	ret = cdv_sb_write(dev, SB_M(pipe), m);
	if (ret)
		return ret;

	ret = cdv_sb_read(dev, SB_N_VCO(pipe), &n_vco);
	if (ret)
		return ret;

	/* Follow the BIOS to program the N_DIVIDER REG */
	n_vco &= 0xFFFF;
	n_vco |= 0x107;
	n_vco &= ~(SB_N_VCO_SEL_MASK |
		   SB_N_DIVIDER_MASK |
		   SB_N_CB_TUNE_MASK);

	n_vco |= ((clock->n) << SB_N_DIVIDER_SHIFT);

	if (clock->vco < 2250000) {
		n_vco |= (2 << SB_N_CB_TUNE_SHIFT);
		n_vco |= (0 << SB_N_VCO_SEL_SHIFT);
	} else if (clock->vco < 2750000) {
		n_vco |= (1 << SB_N_CB_TUNE_SHIFT);
		n_vco |= (1 << SB_N_VCO_SEL_SHIFT);
	} else if (clock->vco < 3300000) {
		n_vco |= (0 << SB_N_CB_TUNE_SHIFT);
		n_vco |= (2 << SB_N_VCO_SEL_SHIFT);
	} else {
		n_vco |= (0 << SB_N_CB_TUNE_SHIFT);
		n_vco |= (3 << SB_N_VCO_SEL_SHIFT);
	}

	ret = cdv_sb_write(dev, SB_N_VCO(pipe), n_vco);
	if (ret)
		return ret;

	ret = cdv_sb_read(dev, SB_P(pipe), &p);
	if (ret)
		return ret;
	p &= ~(SB_P2_DIVIDER_MASK | SB_P1_DIVIDER_MASK);
	p |= SET_FIELD(clock->p1, SB_P1_DIVIDER);
	switch (clock->p2) {
	case 5:
		p |= SET_FIELD(SB_P2_5, SB_P2_DIVIDER);
		break;
	case 10:
		p |= SET_FIELD(SB_P2_10, SB_P2_DIVIDER);
		break;
	case 14:
		p |= SET_FIELD(SB_P2_14, SB_P2_DIVIDER);
		break;
	case 7:
		p |= SET_FIELD(SB_P2_7, SB_P2_DIVIDER);
		break;
	default:
		DRM_ERROR("Bad P2 clock: %d\n", clock->p2);
		return -EINVAL;
	}
	ret = cdv_sb_write(dev, SB_P(pipe), p);
	if (ret)
		return ret;

	if (ddi_select) {
		if ((ddi_select & DDI_MASK) == DDI0_SELECT) {
			lane_reg = PSB_LANE0;
			cdv_sb_read(dev, lane_reg, &lane_value);
			lane_value &= ~(LANE_PLL_MASK);
			lane_value |= LANE_PLL_ENABLE | LANE_PLL_PIPE(pipe);
			cdv_sb_write(dev, lane_reg, lane_value);

			lane_reg = PSB_LANE1;
			cdv_sb_read(dev, lane_reg, &lane_value);
			lane_value &= ~(LANE_PLL_MASK);
			lane_value |= LANE_PLL_ENABLE | LANE_PLL_PIPE(pipe);
			cdv_sb_write(dev, lane_reg, lane_value);
		} else {
			lane_reg = PSB_LANE2;
			cdv_sb_read(dev, lane_reg, &lane_value);
			lane_value &= ~(LANE_PLL_MASK);
			lane_value |= LANE_PLL_ENABLE | LANE_PLL_PIPE(pipe);
			cdv_sb_write(dev, lane_reg, lane_value);

			lane_reg = PSB_LANE3;
			cdv_sb_read(dev, lane_reg, &lane_value);
			lane_value &= ~(LANE_PLL_MASK);
			lane_value |= LANE_PLL_ENABLE | LANE_PLL_PIPE(pipe);
			cdv_sb_write(dev, lane_reg, lane_value);
		}
	}
	return 0;
}

static const struct gma_limit_t *cdv_intel_limit(struct drm_crtc *crtc,
						 int refclk)
{
	const struct gma_limit_t *limit;
	if (psb_intel_pipe_has_type(crtc, INTEL_OUTPUT_LVDS)) {
		/*
		 * Now only single-channel LVDS is supported on CDV. If it is
		 * incorrect, please add the dual-channel LVDS.
		 */
		if (refclk == 96000)
			limit = &cdv_intel_limits[CDV_LIMIT_SINGLE_LVDS_96];
		else
			limit = &cdv_intel_limits[CDV_LIMIT_SINGLE_LVDS_100];
	} else if (psb_intel_pipe_has_type(crtc, INTEL_OUTPUT_DISPLAYPORT) ||
			psb_intel_pipe_has_type(crtc, INTEL_OUTPUT_EDP)) {
		if (refclk == 27000)
			limit = &cdv_intel_limits[CDV_LIMIT_DP_27];
		else
			limit = &cdv_intel_limits[CDV_LIMIT_DP_100];
	} else {
		if (refclk == 27000)
			limit = &cdv_intel_limits[CDV_LIMIT_DAC_HDMI_27];
		else
			limit = &cdv_intel_limits[CDV_LIMIT_DAC_HDMI_96];
	}
	return limit;
}

/* m1 is reserved as 0 in CDV, n is a ring counter */
static void cdv_intel_clock(int refclk, struct gma_clock_t *clock)
{
	clock->m = clock->m2 + 2;
	clock->p = clock->p1 * clock->p2;
	clock->vco = (refclk * clock->m) / clock->n;
	clock->dot = clock->vco / clock->p;
}

static bool cdv_intel_find_dp_pll(const struct gma_limit_t *limit,
				  struct drm_crtc *crtc, int target,
				  int refclk,
				  struct gma_clock_t *best_clock)
{
	struct gma_clock_t clock;
	if (refclk == 27000) {
		if (target < 200000) {
			clock.p1 = 2;
			clock.p2 = 10;
			clock.n = 1;
			clock.m1 = 0;
			clock.m2 = 118;
		} else {
			clock.p1 = 1;
			clock.p2 = 10;
			clock.n = 1;
			clock.m1 = 0;
			clock.m2 = 98;
		}
	} else if (refclk == 100000) {
		if (target < 200000) {
			clock.p1 = 2;
			clock.p2 = 10;
			clock.n = 5;
			clock.m1 = 0;
			clock.m2 = 160;
		} else {
			clock.p1 = 1;
			clock.p2 = 10;
			clock.n = 5;
			clock.m1 = 0;
			clock.m2 = 133;
		}
	} else
		return false;
	clock.m = clock.m2 + 2;
	clock.p = clock.p1 * clock.p2;
	clock.vco = (refclk * clock.m) / clock.n;
	clock.dot = clock.vco / clock.p;
	memcpy(best_clock, &clock, sizeof(struct gma_clock_t));
	return true;
}

static int cdv_intel_pipe_set_base(struct drm_crtc *crtc,
			    int x, int y, struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	struct psb_framebuffer *psbfb = to_psb_fb(crtc->fb);
	int pipe = psb_intel_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	unsigned long start, offset;
	u32 dspcntr;
	int ret = 0;

	if (!gma_power_begin(dev, true))
		return 0;

	/* no fb bound */
	if (!crtc->fb) {
		dev_err(dev->dev, "No FB bound\n");
		goto psb_intel_pipe_cleaner;
	}


	/* We are displaying this buffer, make sure it is actually loaded
	   into the GTT */
	ret = psb_gtt_pin(psbfb->gtt);
	if (ret < 0)
		goto psb_intel_pipe_set_base_exit;
	start = psbfb->gtt->offset;
	offset = y * crtc->fb->pitches[0] + x * (crtc->fb->bits_per_pixel / 8);

	REG_WRITE(map->stride, crtc->fb->pitches[0]);

	dspcntr = REG_READ(map->cntr);
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
	default:
		dev_err(dev->dev, "Unknown color depth\n");
		ret = -EINVAL;
		goto psb_intel_pipe_set_base_exit;
	}
	REG_WRITE(map->cntr, dspcntr);

	dev_dbg(dev->dev,
		"Writing base %08lX %08lX %d %d\n", start, offset, x, y);

	REG_WRITE(map->base, offset);
	REG_READ(map->base);
	REG_WRITE(map->surf, start);
	REG_READ(map->surf);

psb_intel_pipe_cleaner:
	/* If there was a previous display we can now unpin it */
	if (old_fb)
		psb_gtt_unpin(to_psb_fb(old_fb)->gtt);

psb_intel_pipe_set_base_exit:
	gma_power_end(dev);
	return ret;
}

#define		FIFO_PIPEA		(1 << 0)
#define		FIFO_PIPEB		(1 << 1)

static bool cdv_intel_pipe_enabled(struct drm_device *dev, int pipe)
{
	struct drm_crtc *crtc;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = NULL;

	crtc = dev_priv->pipe_to_crtc_mapping[pipe];
	psb_intel_crtc = to_psb_intel_crtc(crtc);

	if (crtc->fb == NULL || !psb_intel_crtc->active)
		return false;
	return true;
}

static bool cdv_intel_single_pipe_active (struct drm_device *dev)
{
	uint32_t pipe_enabled = 0;

	if (cdv_intel_pipe_enabled(dev, 0))
		pipe_enabled |= FIFO_PIPEA;

	if (cdv_intel_pipe_enabled(dev, 1))
		pipe_enabled |= FIFO_PIPEB;


	DRM_DEBUG_KMS("pipe enabled %x\n", pipe_enabled);

	if (pipe_enabled == FIFO_PIPEA || pipe_enabled == FIFO_PIPEB)
		return true;
	else
		return false;
}

static bool is_pipeb_lvds(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;

	if (psb_intel_crtc->pipe != 1)
		return false;

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct psb_intel_encoder *psb_intel_encoder =
					psb_intel_attached_encoder(connector);

		if (!connector->encoder
		    || connector->encoder->crtc != crtc)
			continue;

		if (psb_intel_encoder->type == INTEL_OUTPUT_LVDS)
			return true;
	}

	return false;
}

static void cdv_intel_disable_self_refresh (struct drm_device *dev)
{
	if (REG_READ(FW_BLC_SELF) & FW_BLC_SELF_EN) {

		/* Disable self-refresh before adjust WM */
		REG_WRITE(FW_BLC_SELF, (REG_READ(FW_BLC_SELF) & ~FW_BLC_SELF_EN));
		REG_READ(FW_BLC_SELF);

		cdv_intel_wait_for_vblank(dev);

		/* Cedarview workaround to write ovelay plane, which force to leave
		 * MAX_FIFO state.
		 */
		REG_WRITE(OV_OVADD, 0/*dev_priv->ovl_offset*/);
		REG_READ(OV_OVADD);

		cdv_intel_wait_for_vblank(dev);
	}

}

static void cdv_intel_update_watermark (struct drm_device *dev, struct drm_crtc *crtc)
{

	if (cdv_intel_single_pipe_active(dev)) {
		u32 fw;

		fw = REG_READ(DSPFW1);
		fw &= ~DSP_FIFO_SR_WM_MASK;
		fw |= (0x7e << DSP_FIFO_SR_WM_SHIFT);
		fw &= ~CURSOR_B_FIFO_WM_MASK;
		fw |= (0x4 << CURSOR_B_FIFO_WM_SHIFT);
		REG_WRITE(DSPFW1, fw);

		fw = REG_READ(DSPFW2);
		fw &= ~CURSOR_A_FIFO_WM_MASK;
		fw |= (0x6 << CURSOR_A_FIFO_WM_SHIFT);
		fw &= ~DSP_PLANE_C_FIFO_WM_MASK;
		fw |= (0x8 << DSP_PLANE_C_FIFO_WM_SHIFT);
		REG_WRITE(DSPFW2, fw);

		REG_WRITE(DSPFW3, 0x36000000);

		/* ignore FW4 */

		if (is_pipeb_lvds(dev, crtc)) {
			REG_WRITE(DSPFW5, 0x00040330);
		} else {
			fw = (3 << DSP_PLANE_B_FIFO_WM1_SHIFT) |
			     (4 << DSP_PLANE_A_FIFO_WM1_SHIFT) |
			     (3 << CURSOR_B_FIFO_WM1_SHIFT) |
			     (4 << CURSOR_FIFO_SR_WM1_SHIFT);
			REG_WRITE(DSPFW5, fw);
		}

		REG_WRITE(DSPFW6, 0x10);

		cdv_intel_wait_for_vblank(dev);

		/* enable self-refresh for single pipe active */
		REG_WRITE(FW_BLC_SELF, FW_BLC_SELF_EN);
		REG_READ(FW_BLC_SELF);
		cdv_intel_wait_for_vblank(dev);

	} else {

		/* HW team suggested values... */
		REG_WRITE(DSPFW1, 0x3f880808);
		REG_WRITE(DSPFW2, 0x0b020202);
		REG_WRITE(DSPFW3, 0x24000000);
		REG_WRITE(DSPFW4, 0x08030202);
		REG_WRITE(DSPFW5, 0x01010101);
		REG_WRITE(DSPFW6, 0x1d0);

		cdv_intel_wait_for_vblank(dev);

		cdv_intel_disable_self_refresh(dev);
	
	}
}

/** Loads the palette/gamma unit for the CRTC with the prepared values */
static void cdv_intel_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int palreg = PALETTE_A;
	int i;

	/* The clocks have to be on to load the palette. */
	if (!crtc->enabled)
		return;

	switch (psb_intel_crtc->pipe) {
	case 0:
		break;
	case 1:
		palreg = PALETTE_B;
		break;
	case 2:
		palreg = PALETTE_C;
		break;
	default:
		dev_err(dev->dev, "Illegal Pipe Number.\n");
		return;
	}

	if (gma_power_begin(dev, false)) {
		for (i = 0; i < 256; i++) {
			REG_WRITE(palreg + 4 * i,
				  ((psb_intel_crtc->lut_r[i] +
				  psb_intel_crtc->lut_adj[i]) << 16) |
				  ((psb_intel_crtc->lut_g[i] +
				  psb_intel_crtc->lut_adj[i]) << 8) |
				  (psb_intel_crtc->lut_b[i] +
				  psb_intel_crtc->lut_adj[i]));
		}
		gma_power_end(dev);
	} else {
		for (i = 0; i < 256; i++) {
			dev_priv->regs.pipe[0].palette[i] =
				  ((psb_intel_crtc->lut_r[i] +
				  psb_intel_crtc->lut_adj[i]) << 16) |
				  ((psb_intel_crtc->lut_g[i] +
				  psb_intel_crtc->lut_adj[i]) << 8) |
				  (psb_intel_crtc->lut_b[i] +
				  psb_intel_crtc->lut_adj[i]);
		}

	}
}

/**
 * Sets the power management mode of the pipe and plane.
 *
 * This code should probably grow support for turning the cursor off and back
 * on appropriately at the same time as we're turning the pipe off/on.
 */
static void cdv_intel_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	u32 temp;

	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */
	cdv_intel_disable_self_refresh(dev);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		if (psb_intel_crtc->active)
			break;

		psb_intel_crtc->active = true;

		/* Enable the DPLL */
		temp = REG_READ(map->dpll);
		if ((temp & DPLL_VCO_ENABLE) == 0) {
			REG_WRITE(map->dpll, temp);
			REG_READ(map->dpll);
			/* Wait for the clocks to stabilize. */
			udelay(150);
			REG_WRITE(map->dpll, temp | DPLL_VCO_ENABLE);
			REG_READ(map->dpll);
			/* Wait for the clocks to stabilize. */
			udelay(150);
			REG_WRITE(map->dpll, temp | DPLL_VCO_ENABLE);
			REG_READ(map->dpll);
			/* Wait for the clocks to stabilize. */
			udelay(150);
		}

		/* Jim Bish - switch plan and pipe per scott */
		/* Enable the plane */
		temp = REG_READ(map->cntr);
		if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
			REG_WRITE(map->cntr,
				  temp | DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			REG_WRITE(map->base, REG_READ(map->base));
		}

		udelay(150);

		/* Enable the pipe */
		temp = REG_READ(map->conf);
		if ((temp & PIPEACONF_ENABLE) == 0)
			REG_WRITE(map->conf, temp | PIPEACONF_ENABLE);

		temp = REG_READ(map->status);
		temp &= ~(0xFFFF);
		temp |= PIPE_FIFO_UNDERRUN;
		REG_WRITE(map->status, temp);
		REG_READ(map->status);

		cdv_intel_crtc_load_lut(crtc);

		/* Give the overlay scaler a chance to enable
		 * if it's on this pipe */
		/* psb_intel_crtc_dpms_video(crtc, true); TODO */
		break;
	case DRM_MODE_DPMS_OFF:
		if (!psb_intel_crtc->active)
			break;

		psb_intel_crtc->active = false;

		/* Give the overlay scaler a chance to disable
		 * if it's on this pipe */
		/* psb_intel_crtc_dpms_video(crtc, FALSE); TODO */

		/* Disable the VGA plane that we never use */
		REG_WRITE(VGACNTRL, VGA_DISP_DISABLE);

		/* Jim Bish - changed pipe/plane here as well. */

		drm_vblank_off(dev, pipe);
		/* Wait for vblank for the disable to take effect */
		cdv_intel_wait_for_vblank(dev);

		/* Next, disable display pipes */
		temp = REG_READ(map->conf);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			REG_WRITE(map->conf, temp & ~PIPEACONF_ENABLE);
			REG_READ(map->conf);
		}

		/* Wait for vblank for the disable to take effect. */
		cdv_intel_wait_for_vblank(dev);

		udelay(150);

		/* Disable display plane */
		temp = REG_READ(map->cntr);
		if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
			REG_WRITE(map->cntr,
				  temp & ~DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			REG_WRITE(map->base, REG_READ(map->base));
			REG_READ(map->base);
		}

		temp = REG_READ(map->dpll);
		if ((temp & DPLL_VCO_ENABLE) != 0) {
			REG_WRITE(map->dpll, temp & ~DPLL_VCO_ENABLE);
			REG_READ(map->dpll);
		}

		/* Wait for the clocks to turn off. */
		udelay(150);
		break;
	}
	cdv_intel_update_watermark(dev, crtc);
	/*Set FIFO Watermarks*/
	REG_WRITE(DSPARB, 0x3F3E);
}

static void cdv_intel_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void cdv_intel_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
}

static bool cdv_intel_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}


/**
 * Return the pipe currently connected to the panel fitter,
 * or -1 if the panel fitter is not present or not in use
 */
static int cdv_intel_panel_fitter_pipe(struct drm_device *dev)
{
	u32 pfit_control;

	pfit_control = REG_READ(PFIT_CONTROL);

	/* See if the panel fitter is in use */
	if ((pfit_control & PFIT_ENABLE) == 0)
		return -1;
	return (pfit_control >> 29) & 0x3;
}

static int cdv_intel_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	int refclk;
	struct gma_clock_t clock;
	u32 dpll = 0, dspcntr, pipeconf;
	bool ok;
	bool is_crt = false, is_lvds = false, is_tv = false;
	bool is_hdmi = false, is_dp = false;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;
	const struct gma_limit_t *limit;
	u32 ddi_select = 0;
	bool is_edp = false;

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct psb_intel_encoder *psb_intel_encoder =
					psb_intel_attached_encoder(connector);

		if (!connector->encoder
		    || connector->encoder->crtc != crtc)
			continue;

		ddi_select = psb_intel_encoder->ddi_select;
		switch (psb_intel_encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_TVOUT:
			is_tv = true;
			break;
		case INTEL_OUTPUT_ANALOG:
			is_crt = true;
			break;
		case INTEL_OUTPUT_HDMI:
			is_hdmi = true;
			break;
		case INTEL_OUTPUT_DISPLAYPORT:
			is_dp = true;
			break;
		case INTEL_OUTPUT_EDP:
			is_edp = true;
			break;
		default:
			DRM_ERROR("invalid output type.\n");
			return 0;
		}
	}

	if (dev_priv->dplla_96mhz)
		/* low-end sku, 96/100 mhz */
		refclk = 96000;
	else
		/* high-end sku, 27/100 mhz */
		refclk = 27000;
	if (is_dp || is_edp) {
		/*
		 * Based on the spec the low-end SKU has only CRT/LVDS. So it is
		 * unnecessary to consider it for DP/eDP.
		 * On the high-end SKU, it will use the 27/100M reference clk
		 * for DP/eDP. When using SSC clock, the ref clk is 100MHz.Otherwise
		 * it will be 27MHz. From the VBIOS code it seems that the pipe A choose
		 * 27MHz for DP/eDP while the Pipe B chooses the 100MHz.
		 */ 
		if (pipe == 0)
			refclk = 27000;
		else
			refclk = 100000;
	}

	if (is_lvds && dev_priv->lvds_use_ssc) {
		refclk = dev_priv->lvds_ssc_freq * 1000;
		DRM_DEBUG_KMS("Use SSC reference clock %d Mhz\n", dev_priv->lvds_ssc_freq);
	}

	drm_mode_debug_printmodeline(adjusted_mode);
	
	limit = psb_intel_crtc->clock_funcs->limit(crtc, refclk);

	ok = limit->find_pll(limit, crtc, adjusted_mode->clock, refclk,
				 &clock);
	if (!ok) {
		DRM_ERROR("Couldn't find PLL settings for mode! target: %d, actual: %d",
			  adjusted_mode->clock, clock.dot);
		return 0;
	}

	dpll = DPLL_VGA_MODE_DIS;
	if (is_tv) {
		/* XXX: just matching BIOS for now */
/*	dpll |= PLL_REF_INPUT_TVCLKINBC; */
		dpll |= 3;
	}
/*		dpll |= PLL_REF_INPUT_DREFCLK; */

	if (is_dp || is_edp) {
		cdv_intel_dp_set_m_n(crtc, mode, adjusted_mode);
	} else {
		REG_WRITE(PIPE_GMCH_DATA_M(pipe), 0);
		REG_WRITE(PIPE_GMCH_DATA_N(pipe), 0);
		REG_WRITE(PIPE_DP_LINK_M(pipe), 0);
		REG_WRITE(PIPE_DP_LINK_N(pipe), 0);
	}

	dpll |= DPLL_SYNCLOCK_ENABLE;
/*	if (is_lvds)
		dpll |= DPLLB_MODE_LVDS;
	else
		dpll |= DPLLB_MODE_DAC_SERIAL; */
	/* dpll |= (2 << 11); */

	/* setup pipeconf */
	pipeconf = REG_READ(map->conf);

	pipeconf &= ~(PIPE_BPC_MASK);
	if (is_edp) {
		switch (dev_priv->edp.bpp) {
		case 24:
			pipeconf |= PIPE_8BPC;
			break;
		case 18:
			pipeconf |= PIPE_6BPC;
			break;
		case 30:
			pipeconf |= PIPE_10BPC;
			break;
		default:
			pipeconf |= PIPE_8BPC;
			break;
		}
	} else if (is_lvds) {
		/* the BPC will be 6 if it is 18-bit LVDS panel */
		if ((REG_READ(LVDS) & LVDS_A3_POWER_MASK) == LVDS_A3_POWER_UP)
			pipeconf |= PIPE_8BPC;
		else
			pipeconf |= PIPE_6BPC;
	} else
		pipeconf |= PIPE_8BPC;
			
	/* Set up the display plane register */
	dspcntr = DISPPLANE_GAMMA_ENABLE;

	if (pipe == 0)
		dspcntr |= DISPPLANE_SEL_PIPE_A;
	else
		dspcntr |= DISPPLANE_SEL_PIPE_B;

	dspcntr |= DISPLAY_PLANE_ENABLE;
	pipeconf |= PIPEACONF_ENABLE;

	REG_WRITE(map->dpll, dpll | DPLL_VGA_MODE_DIS | DPLL_SYNCLOCK_ENABLE);
	REG_READ(map->dpll);

	cdv_dpll_set_clock_cdv(dev, crtc, &clock, is_lvds, ddi_select);

	udelay(150);


	/* The LVDS pin pair needs to be on before the DPLLs are enabled.
	 * This is an exception to the general rule that mode_set doesn't turn
	 * things on.
	 */
	if (is_lvds) {
		u32 lvds = REG_READ(LVDS);

		lvds |=
		    LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP |
		    LVDS_PIPEB_SELECT;
		/* Set the B0-B3 data pairs corresponding to
		 * whether we're going to
		 * set the DPLLs for dual-channel mode or not.
		 */
		if (clock.p2 == 7)
			lvds |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;
		else
			lvds &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);

		/* It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
		 * appropriately here, but we need to look more
		 * thoroughly into how panels behave in the two modes.
		 */

		REG_WRITE(LVDS, lvds);
		REG_READ(LVDS);
	}

	dpll |= DPLL_VCO_ENABLE;

	/* Disable the panel fitter if it was on our pipe */
	if (cdv_intel_panel_fitter_pipe(dev) == pipe)
		REG_WRITE(PFIT_CONTROL, 0);

	DRM_DEBUG_KMS("Mode for pipe %c:\n", pipe == 0 ? 'A' : 'B');
	drm_mode_debug_printmodeline(mode);

	REG_WRITE(map->dpll,
		(REG_READ(map->dpll) & ~DPLL_LOCK) | DPLL_VCO_ENABLE);
	REG_READ(map->dpll);
	/* Wait for the clocks to stabilize. */
	udelay(150); /* 42 usec w/o calibration, 110 with.  rounded up. */

	if (!(REG_READ(map->dpll) & DPLL_LOCK)) {
		dev_err(dev->dev, "Failed to get DPLL lock\n");
		return -EBUSY;
	}

	{
		int sdvo_pixel_multiply = adjusted_mode->clock / mode->clock;
		REG_WRITE(map->dpll_md, (0 << DPLL_MD_UDI_DIVIDER_SHIFT) | ((sdvo_pixel_multiply - 1) << DPLL_MD_UDI_MULTIPLIER_SHIFT));
	}

	REG_WRITE(map->htotal, (adjusted_mode->crtc_hdisplay - 1) |
		  ((adjusted_mode->crtc_htotal - 1) << 16));
	REG_WRITE(map->hblank, (adjusted_mode->crtc_hblank_start - 1) |
		  ((adjusted_mode->crtc_hblank_end - 1) << 16));
	REG_WRITE(map->hsync, (adjusted_mode->crtc_hsync_start - 1) |
		  ((adjusted_mode->crtc_hsync_end - 1) << 16));
	REG_WRITE(map->vtotal, (adjusted_mode->crtc_vdisplay - 1) |
		  ((adjusted_mode->crtc_vtotal - 1) << 16));
	REG_WRITE(map->vblank, (adjusted_mode->crtc_vblank_start - 1) |
		  ((adjusted_mode->crtc_vblank_end - 1) << 16));
	REG_WRITE(map->vsync, (adjusted_mode->crtc_vsync_start - 1) |
		  ((adjusted_mode->crtc_vsync_end - 1) << 16));
	/* pipesrc and dspsize control the size that is scaled from,
	 * which should always be the user's requested size.
	 */
	REG_WRITE(map->size,
		  ((mode->vdisplay - 1) << 16) | (mode->hdisplay - 1));
	REG_WRITE(map->pos, 0);
	REG_WRITE(map->src,
		  ((mode->hdisplay - 1) << 16) | (mode->vdisplay - 1));
	REG_WRITE(map->conf, pipeconf);
	REG_READ(map->conf);

	cdv_intel_wait_for_vblank(dev);

	REG_WRITE(map->cntr, dspcntr);

	/* Flush the plane changes */
	{
		struct drm_crtc_helper_funcs *crtc_funcs =
		    crtc->helper_private;
		crtc_funcs->mode_set_base(crtc, x, y, old_fb);
	}

	cdv_intel_wait_for_vblank(dev);

	return 0;
}


/**
 * Save HW states of giving crtc
 */
static void cdv_intel_crtc_save(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	struct psb_intel_crtc_state *crtc_state = psb_intel_crtc->crtc_state;
	const struct psb_offset *map = &dev_priv->regmap[psb_intel_crtc->pipe];
	uint32_t paletteReg;
	int i;

	if (!crtc_state) {
		dev_dbg(dev->dev, "No CRTC state found\n");
		return;
	}

	crtc_state->saveDSPCNTR = REG_READ(map->cntr);
	crtc_state->savePIPECONF = REG_READ(map->conf);
	crtc_state->savePIPESRC = REG_READ(map->src);
	crtc_state->saveFP0 = REG_READ(map->fp0);
	crtc_state->saveFP1 = REG_READ(map->fp1);
	crtc_state->saveDPLL = REG_READ(map->dpll);
	crtc_state->saveHTOTAL = REG_READ(map->htotal);
	crtc_state->saveHBLANK = REG_READ(map->hblank);
	crtc_state->saveHSYNC = REG_READ(map->hsync);
	crtc_state->saveVTOTAL = REG_READ(map->vtotal);
	crtc_state->saveVBLANK = REG_READ(map->vblank);
	crtc_state->saveVSYNC = REG_READ(map->vsync);
	crtc_state->saveDSPSTRIDE = REG_READ(map->stride);

	/*NOTE: DSPSIZE DSPPOS only for psb*/
	crtc_state->saveDSPSIZE = REG_READ(map->size);
	crtc_state->saveDSPPOS = REG_READ(map->pos);

	crtc_state->saveDSPBASE = REG_READ(map->base);

	DRM_DEBUG("(%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x)\n",
			crtc_state->saveDSPCNTR,
			crtc_state->savePIPECONF,
			crtc_state->savePIPESRC,
			crtc_state->saveFP0,
			crtc_state->saveFP1,
			crtc_state->saveDPLL,
			crtc_state->saveHTOTAL,
			crtc_state->saveHBLANK,
			crtc_state->saveHSYNC,
			crtc_state->saveVTOTAL,
			crtc_state->saveVBLANK,
			crtc_state->saveVSYNC,
			crtc_state->saveDSPSTRIDE,
			crtc_state->saveDSPSIZE,
			crtc_state->saveDSPPOS,
			crtc_state->saveDSPBASE
		);

	paletteReg = map->palette;
	for (i = 0; i < 256; ++i)
		crtc_state->savePalette[i] = REG_READ(paletteReg + (i << 2));
}

/**
 * Restore HW states of giving crtc
 */
static void cdv_intel_crtc_restore(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc =  to_psb_intel_crtc(crtc);
	struct psb_intel_crtc_state *crtc_state = psb_intel_crtc->crtc_state;
	const struct psb_offset *map = &dev_priv->regmap[psb_intel_crtc->pipe];
	uint32_t paletteReg;
	int i;

	if (!crtc_state) {
		dev_dbg(dev->dev, "No crtc state\n");
		return;
	}

	DRM_DEBUG(
		"current:(%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x)\n",
		REG_READ(map->cntr),
		REG_READ(map->conf),
		REG_READ(map->src),
		REG_READ(map->fp0),
		REG_READ(map->fp1),
		REG_READ(map->dpll),
		REG_READ(map->htotal),
		REG_READ(map->hblank),
		REG_READ(map->hsync),
		REG_READ(map->vtotal),
		REG_READ(map->vblank),
		REG_READ(map->vsync),
		REG_READ(map->stride),
		REG_READ(map->size),
		REG_READ(map->pos),
		REG_READ(map->base)
	);

	DRM_DEBUG(
		"saved: (%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x)\n",
		crtc_state->saveDSPCNTR,
		crtc_state->savePIPECONF,
		crtc_state->savePIPESRC,
		crtc_state->saveFP0,
		crtc_state->saveFP1,
		crtc_state->saveDPLL,
		crtc_state->saveHTOTAL,
		crtc_state->saveHBLANK,
		crtc_state->saveHSYNC,
		crtc_state->saveVTOTAL,
		crtc_state->saveVBLANK,
		crtc_state->saveVSYNC,
		crtc_state->saveDSPSTRIDE,
		crtc_state->saveDSPSIZE,
		crtc_state->saveDSPPOS,
		crtc_state->saveDSPBASE
	);


	if (crtc_state->saveDPLL & DPLL_VCO_ENABLE) {
		REG_WRITE(map->dpll,
				crtc_state->saveDPLL & ~DPLL_VCO_ENABLE);
		REG_READ(map->dpll);
		DRM_DEBUG("write dpll: %x\n",
				REG_READ(map->dpll));
		udelay(150);
	}

	REG_WRITE(map->fp0, crtc_state->saveFP0);
	REG_READ(map->fp0);

	REG_WRITE(map->fp1, crtc_state->saveFP1);
	REG_READ(map->fp1);

	REG_WRITE(map->dpll, crtc_state->saveDPLL);
	REG_READ(map->dpll);
	udelay(150);

	REG_WRITE(map->htotal, crtc_state->saveHTOTAL);
	REG_WRITE(map->hblank, crtc_state->saveHBLANK);
	REG_WRITE(map->hsync, crtc_state->saveHSYNC);
	REG_WRITE(map->vtotal, crtc_state->saveVTOTAL);
	REG_WRITE(map->vblank, crtc_state->saveVBLANK);
	REG_WRITE(map->vsync, crtc_state->saveVSYNC);
	REG_WRITE(map->stride, crtc_state->saveDSPSTRIDE);

	REG_WRITE(map->size, crtc_state->saveDSPSIZE);
	REG_WRITE(map->pos, crtc_state->saveDSPPOS);

	REG_WRITE(map->src, crtc_state->savePIPESRC);
	REG_WRITE(map->base, crtc_state->saveDSPBASE);
	REG_WRITE(map->conf, crtc_state->savePIPECONF);

	cdv_intel_wait_for_vblank(dev);

	REG_WRITE(map->cntr, crtc_state->saveDSPCNTR);
	REG_WRITE(map->base, crtc_state->saveDSPBASE);

	cdv_intel_wait_for_vblank(dev);

	paletteReg = map->palette;
	for (i = 0; i < 256; ++i)
		REG_WRITE(paletteReg + (i << 2), crtc_state->savePalette[i]);
}

static int cdv_intel_crtc_cursor_set(struct drm_crtc *crtc,
				 struct drm_file *file_priv,
				 uint32_t handle,
				 uint32_t width, uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	uint32_t control = (pipe == 0) ? CURACNTR : CURBCNTR;
	uint32_t base = (pipe == 0) ? CURABASE : CURBBASE;
	uint32_t temp;
	size_t addr = 0;
	struct gtt_range *gt;
	struct drm_gem_object *obj;
	int ret = 0;

	/* if we want to turn of the cursor ignore width and height */
	if (!handle) {
		/* turn off the cursor */
		temp = CURSOR_MODE_DISABLE;

		if (gma_power_begin(dev, false)) {
			REG_WRITE(control, temp);
			REG_WRITE(base, 0);
			gma_power_end(dev);
		}

		/* unpin the old GEM object */
		if (psb_intel_crtc->cursor_obj) {
			gt = container_of(psb_intel_crtc->cursor_obj,
							struct gtt_range, gem);
			psb_gtt_unpin(gt);
			drm_gem_object_unreference(psb_intel_crtc->cursor_obj);
			psb_intel_crtc->cursor_obj = NULL;
		}

		return 0;
	}

	/* Currently we only support 64x64 cursors */
	if (width != 64 || height != 64) {
		dev_dbg(dev->dev, "we currently only support 64x64 cursors\n");
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj)
		return -ENOENT;

	if (obj->size < width * height * 4) {
		dev_dbg(dev->dev, "buffer is to small\n");
		ret = -ENOMEM;
		goto unref_cursor;
	}

	gt = container_of(obj, struct gtt_range, gem);

	/* Pin the memory into the GTT */
	ret = psb_gtt_pin(gt);
	if (ret) {
		dev_err(dev->dev, "Can not pin down handle 0x%x\n", handle);
		goto unref_cursor;
	}

	addr = gt->offset;	/* Or resource.start ??? */

	psb_intel_crtc->cursor_addr = addr;

	temp = 0;
	/* set the pipe for the cursor */
	temp |= (pipe << 28);
	temp |= CURSOR_MODE_64_ARGB_AX | MCURSOR_GAMMA_ENABLE;

	if (gma_power_begin(dev, false)) {
		REG_WRITE(control, temp);
		REG_WRITE(base, addr);
		gma_power_end(dev);
	}

	/* unpin the old GEM object */
	if (psb_intel_crtc->cursor_obj) {
		gt = container_of(psb_intel_crtc->cursor_obj,
							struct gtt_range, gem);
		psb_gtt_unpin(gt);
		drm_gem_object_unreference(psb_intel_crtc->cursor_obj);
	}

	psb_intel_crtc->cursor_obj = obj;
	return ret;

unref_cursor:
	drm_gem_object_unreference(obj);
	return ret;
}

static int cdv_intel_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	uint32_t temp = 0;
	uint32_t adder;


	if (x < 0) {
		temp |= (CURSOR_POS_SIGN << CURSOR_X_SHIFT);
		x = -x;
	}
	if (y < 0) {
		temp |= (CURSOR_POS_SIGN << CURSOR_Y_SHIFT);
		y = -y;
	}

	temp |= ((x & CURSOR_POS_MASK) << CURSOR_X_SHIFT);
	temp |= ((y & CURSOR_POS_MASK) << CURSOR_Y_SHIFT);

	adder = psb_intel_crtc->cursor_addr;

	if (gma_power_begin(dev, false)) {
		REG_WRITE((pipe == 0) ? CURAPOS : CURBPOS, temp);
		REG_WRITE((pipe == 0) ? CURABASE : CURBBASE, adder);
		gma_power_end(dev);
	}
	return 0;
}

static void cdv_intel_crtc_gamma_set(struct drm_crtc *crtc, u16 *red,
			 u16 *green, u16 *blue, uint32_t start, uint32_t size)
{
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int i;
	int end = (start + size > 256) ? 256 : start + size;

	for (i = start; i < end; i++) {
		psb_intel_crtc->lut_r[i] = red[i] >> 8;
		psb_intel_crtc->lut_g[i] = green[i] >> 8;
		psb_intel_crtc->lut_b[i] = blue[i] >> 8;
	}

	cdv_intel_crtc_load_lut(crtc);
}

static int cdv_crtc_set_config(struct drm_mode_set *set)
{
	int ret = 0;
	struct drm_device *dev = set->crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (!dev_priv->rpm_enabled)
		return drm_crtc_helper_set_config(set);

	pm_runtime_forbid(&dev->pdev->dev);

	ret = drm_crtc_helper_set_config(set);

	pm_runtime_allow(&dev->pdev->dev);

	return ret;
}

/** Derive the pixel clock for the given refclk and divisors for 8xx chips. */

/* FIXME: why are we using this, should it be cdv_ in this tree ? */

static void i8xx_clock(int refclk, struct gma_clock_t *clock)
{
	clock->m = 5 * (clock->m1 + 2) + (clock->m2 + 2);
	clock->p = clock->p1 * clock->p2;
	clock->vco = refclk * clock->m / (clock->n + 2);
	clock->dot = clock->vco / clock->p;
}

/* Returns the clock of the currently programmed mode of the given pipe. */
static int cdv_intel_crtc_clock_get(struct drm_device *dev,
				struct drm_crtc *crtc)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	u32 dpll;
	u32 fp;
	struct gma_clock_t clock;
	bool is_lvds;
	struct psb_pipe *p = &dev_priv->regs.pipe[pipe];

	if (gma_power_begin(dev, false)) {
		dpll = REG_READ(map->dpll);
		if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
			fp = REG_READ(map->fp0);
		else
			fp = REG_READ(map->fp1);
		is_lvds = (pipe == 1) && (REG_READ(LVDS) & LVDS_PORT_EN);
		gma_power_end(dev);
	} else {
		dpll = p->dpll;
		if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
			fp = p->fp0;
		else
			fp = p->fp1;

		is_lvds = (pipe == 1) &&
				(dev_priv->regs.psb.saveLVDS & LVDS_PORT_EN);
	}

	clock.m1 = (fp & FP_M1_DIV_MASK) >> FP_M1_DIV_SHIFT;
	clock.m2 = (fp & FP_M2_DIV_MASK) >> FP_M2_DIV_SHIFT;
	clock.n = (fp & FP_N_DIV_MASK) >> FP_N_DIV_SHIFT;

	if (is_lvds) {
		clock.p1 =
		    ffs((dpll &
			 DPLL_FPA01_P1_POST_DIV_MASK_I830_LVDS) >>
			DPLL_FPA01_P1_POST_DIV_SHIFT);
		if (clock.p1 == 0) {
			clock.p1 = 4;
			dev_err(dev->dev, "PLL %d\n", dpll);
		}
		clock.p2 = 14;

		if ((dpll & PLL_REF_INPUT_MASK) ==
		    PLLB_REF_INPUT_SPREADSPECTRUMIN) {
			/* XXX: might not be 66MHz */
			i8xx_clock(66000, &clock);
		} else
			i8xx_clock(48000, &clock);
	} else {
		if (dpll & PLL_P1_DIVIDE_BY_TWO)
			clock.p1 = 2;
		else {
			clock.p1 =
			    ((dpll &
			      DPLL_FPA01_P1_POST_DIV_MASK_I830) >>
			     DPLL_FPA01_P1_POST_DIV_SHIFT) + 2;
		}
		if (dpll & PLL_P2_DIVIDE_BY_4)
			clock.p2 = 4;
		else
			clock.p2 = 2;

		i8xx_clock(48000, &clock);
	}

	/* XXX: It would be nice to validate the clocks, but we can't reuse
	 * i830PllIsValid() because it relies on the xf86_config connector
	 * configuration being accurate, which it isn't necessarily.
	 */

	return clock.dot;
}

/** Returns the currently programmed mode of the given pipe. */
struct drm_display_mode *cdv_intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc)
{
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_pipe *p = &dev_priv->regs.pipe[pipe];
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	struct drm_display_mode *mode;
	int htot;
	int hsync;
	int vtot;
	int vsync;

	if (gma_power_begin(dev, false)) {
		htot = REG_READ(map->htotal);
		hsync = REG_READ(map->hsync);
		vtot = REG_READ(map->vtotal);
		vsync = REG_READ(map->vsync);
		gma_power_end(dev);
	} else {
		htot = p->htotal;
		hsync = p->hsync;
		vtot = p->vtotal;
		vsync = p->vsync;
	}

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	mode->clock = cdv_intel_crtc_clock_get(dev, crtc);
	mode->hdisplay = (htot & 0xffff) + 1;
	mode->htotal = ((htot & 0xffff0000) >> 16) + 1;
	mode->hsync_start = (hsync & 0xffff) + 1;
	mode->hsync_end = ((hsync & 0xffff0000) >> 16) + 1;
	mode->vdisplay = (vtot & 0xffff) + 1;
	mode->vtotal = ((vtot & 0xffff0000) >> 16) + 1;
	mode->vsync_start = (vsync & 0xffff) + 1;
	mode->vsync_end = ((vsync & 0xffff0000) >> 16) + 1;

	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);

	return mode;
}

static void cdv_intel_crtc_destroy(struct drm_crtc *crtc)
{
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);

	kfree(psb_intel_crtc->crtc_state);
	drm_crtc_cleanup(crtc);
	kfree(psb_intel_crtc);
}

static void cdv_intel_crtc_disable(struct drm_crtc *crtc)
{
	struct gtt_range *gt;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;

	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);

	if (crtc->fb) {
		gt = to_psb_fb(crtc->fb)->gtt;
		psb_gtt_unpin(gt);
	}
}

const struct drm_crtc_helper_funcs cdv_intel_helper_funcs = {
	.dpms = cdv_intel_crtc_dpms,
	.mode_fixup = cdv_intel_crtc_mode_fixup,
	.mode_set = cdv_intel_crtc_mode_set,
	.mode_set_base = cdv_intel_pipe_set_base,
	.prepare = cdv_intel_crtc_prepare,
	.commit = cdv_intel_crtc_commit,
	.disable = cdv_intel_crtc_disable,
};

const struct drm_crtc_funcs cdv_intel_crtc_funcs = {
	.save = cdv_intel_crtc_save,
	.restore = cdv_intel_crtc_restore,
	.cursor_set = cdv_intel_crtc_cursor_set,
	.cursor_move = cdv_intel_crtc_cursor_move,
	.gamma_set = cdv_intel_crtc_gamma_set,
	.set_config = cdv_crtc_set_config,
	.destroy = cdv_intel_crtc_destroy,
};

const struct gma_clock_funcs cdv_clock_funcs = {
	.clock = cdv_intel_clock,
	.limit = cdv_intel_limit,
	.pll_is_valid = gma_pll_is_valid,
};
