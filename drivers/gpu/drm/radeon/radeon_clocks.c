/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon_reg.h"
#include "radeon.h"
#include "atom.h"

/* 10 khz */
uint32_t radeon_legacy_get_engine_clock(struct radeon_device *rdev)
{
	struct radeon_pll *spll = &rdev->clock.spll;
	uint32_t fb_div, ref_div, post_div, sclk;

	fb_div = RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV);
	fb_div = (fb_div >> RADEON_SPLL_FB_DIV_SHIFT) & RADEON_SPLL_FB_DIV_MASK;
	fb_div <<= 1;
	fb_div *= spll->reference_freq;

	ref_div =
	    RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV) & RADEON_M_SPLL_REF_DIV_MASK;

	if (ref_div == 0)
		return 0;

	sclk = fb_div / ref_div;

	post_div = RREG32_PLL(RADEON_SCLK_CNTL) & RADEON_SCLK_SRC_SEL_MASK;
	if (post_div == 2)
		sclk >>= 1;
	else if (post_div == 3)
		sclk >>= 2;
	else if (post_div == 4)
		sclk >>= 3;

	return sclk;
}

/* 10 khz */
uint32_t radeon_legacy_get_memory_clock(struct radeon_device *rdev)
{
	struct radeon_pll *mpll = &rdev->clock.mpll;
	uint32_t fb_div, ref_div, post_div, mclk;

	fb_div = RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV);
	fb_div = (fb_div >> RADEON_MPLL_FB_DIV_SHIFT) & RADEON_MPLL_FB_DIV_MASK;
	fb_div <<= 1;
	fb_div *= mpll->reference_freq;

	ref_div =
	    RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV) & RADEON_M_SPLL_REF_DIV_MASK;

	if (ref_div == 0)
		return 0;

	mclk = fb_div / ref_div;

	post_div = RREG32_PLL(RADEON_MCLK_CNTL) & 0x7;
	if (post_div == 2)
		mclk >>= 1;
	else if (post_div == 3)
		mclk >>= 2;
	else if (post_div == 4)
		mclk >>= 3;

	return mclk;
}

#ifdef CONFIG_OF
/*
 * Read XTAL (ref clock), SCLK and MCLK from Open Firmware device
 * tree. Hopefully, ATI OF driver is kind enough to fill these
 */
static bool radeon_read_clocks_OF(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct device_node *dp = rdev->pdev->dev.of_node;
	const u32 *val;
	struct radeon_pll *p1pll = &rdev->clock.p1pll;
	struct radeon_pll *p2pll = &rdev->clock.p2pll;
	struct radeon_pll *spll = &rdev->clock.spll;
	struct radeon_pll *mpll = &rdev->clock.mpll;

	if (dp == NULL)
		return false;
	val = of_get_property(dp, "ATY,RefCLK", NULL);
	if (!val || !*val) {
		printk(KERN_WARNING "radeonfb: No ATY,RefCLK property !\n");
		return false;
	}
	p1pll->reference_freq = p2pll->reference_freq = (*val) / 10;
	p1pll->reference_div = RREG32_PLL(RADEON_PPLL_REF_DIV) & 0x3ff;
	if (p1pll->reference_div < 2)
		p1pll->reference_div = 12;
	p2pll->reference_div = p1pll->reference_div;

	/* These aren't in the device-tree */
	if (rdev->family >= CHIP_R420) {
		p1pll->pll_in_min = 100;
		p1pll->pll_in_max = 1350;
		p1pll->pll_out_min = 20000;
		p1pll->pll_out_max = 50000;
		p2pll->pll_in_min = 100;
		p2pll->pll_in_max = 1350;
		p2pll->pll_out_min = 20000;
		p2pll->pll_out_max = 50000;
	} else {
		p1pll->pll_in_min = 40;
		p1pll->pll_in_max = 500;
		p1pll->pll_out_min = 12500;
		p1pll->pll_out_max = 35000;
		p2pll->pll_in_min = 40;
		p2pll->pll_in_max = 500;
		p2pll->pll_out_min = 12500;
		p2pll->pll_out_max = 35000;
	}
	/* not sure what the max should be in all cases */
	rdev->clock.max_pixel_clock = 35000;

	spll->reference_freq = mpll->reference_freq = p1pll->reference_freq;
	spll->reference_div = mpll->reference_div =
		RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV) &
			    RADEON_M_SPLL_REF_DIV_MASK;

	val = of_get_property(dp, "ATY,SCLK", NULL);
	if (val && *val)
		rdev->clock.default_sclk = (*val) / 10;
	else
		rdev->clock.default_sclk =
			radeon_legacy_get_engine_clock(rdev);

	val = of_get_property(dp, "ATY,MCLK", NULL);
	if (val && *val)
		rdev->clock.default_mclk = (*val) / 10;
	else
		rdev->clock.default_mclk =
			radeon_legacy_get_memory_clock(rdev);

	DRM_INFO("Using device-tree clock info\n");

	return true;
}
#else
static bool radeon_read_clocks_OF(struct drm_device *dev)
{
	return false;
}
#endif /* CONFIG_OF */

void radeon_get_clock_info(struct drm_device *dev)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_pll *p1pll = &rdev->clock.p1pll;
	struct radeon_pll *p2pll = &rdev->clock.p2pll;
	struct radeon_pll *dcpll = &rdev->clock.dcpll;
	struct radeon_pll *spll = &rdev->clock.spll;
	struct radeon_pll *mpll = &rdev->clock.mpll;
	int ret;

	if (rdev->is_atom_bios)
		ret = radeon_atom_get_clock_info(dev);
	else
		ret = radeon_combios_get_clock_info(dev);
	if (!ret)
		ret = radeon_read_clocks_OF(dev);

	if (ret) {
		if (p1pll->reference_div < 2) {
			if (!ASIC_IS_AVIVO(rdev)) {
				u32 tmp = RREG32_PLL(RADEON_PPLL_REF_DIV);
				if (ASIC_IS_R300(rdev))
					p1pll->reference_div =
						(tmp & R300_PPLL_REF_DIV_ACC_MASK) >> R300_PPLL_REF_DIV_ACC_SHIFT;
				else
					p1pll->reference_div = tmp & RADEON_PPLL_REF_DIV_MASK;
				if (p1pll->reference_div < 2)
					p1pll->reference_div = 12;
			} else
				p1pll->reference_div = 12;
		}
		if (p2pll->reference_div < 2)
			p2pll->reference_div = 12;
		if (rdev->family < CHIP_RS600) {
			if (spll->reference_div < 2)
				spll->reference_div =
					RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV) &
					RADEON_M_SPLL_REF_DIV_MASK;
		}
		if (mpll->reference_div < 2)
			mpll->reference_div = spll->reference_div;
	} else {
		if (ASIC_IS_AVIVO(rdev)) {
			/* TODO FALLBACK */
		} else {
			DRM_INFO("Using generic clock info\n");

			if (rdev->flags & RADEON_IS_IGP) {
				p1pll->reference_freq = 1432;
				p2pll->reference_freq = 1432;
				spll->reference_freq = 1432;
				mpll->reference_freq = 1432;
			} else {
				p1pll->reference_freq = 2700;
				p2pll->reference_freq = 2700;
				spll->reference_freq = 2700;
				mpll->reference_freq = 2700;
			}
			p1pll->reference_div =
			    RREG32_PLL(RADEON_PPLL_REF_DIV) & 0x3ff;
			if (p1pll->reference_div < 2)
				p1pll->reference_div = 12;
			p2pll->reference_div = p1pll->reference_div;

			if (rdev->family >= CHIP_R420) {
				p1pll->pll_in_min = 100;
				p1pll->pll_in_max = 1350;
				p1pll->pll_out_min = 20000;
				p1pll->pll_out_max = 50000;
				p2pll->pll_in_min = 100;
				p2pll->pll_in_max = 1350;
				p2pll->pll_out_min = 20000;
				p2pll->pll_out_max = 50000;
			} else {
				p1pll->pll_in_min = 40;
				p1pll->pll_in_max = 500;
				p1pll->pll_out_min = 12500;
				p1pll->pll_out_max = 35000;
				p2pll->pll_in_min = 40;
				p2pll->pll_in_max = 500;
				p2pll->pll_out_min = 12500;
				p2pll->pll_out_max = 35000;
			}

			spll->reference_div =
			    RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV) &
			    RADEON_M_SPLL_REF_DIV_MASK;
			mpll->reference_div = spll->reference_div;
			rdev->clock.default_sclk =
			    radeon_legacy_get_engine_clock(rdev);
			rdev->clock.default_mclk =
			    radeon_legacy_get_memory_clock(rdev);
		}
	}

	/* pixel clocks */
	if (ASIC_IS_AVIVO(rdev)) {
		p1pll->min_post_div = 2;
		p1pll->max_post_div = 0x7f;
		p1pll->min_frac_feedback_div = 0;
		p1pll->max_frac_feedback_div = 9;
		p2pll->min_post_div = 2;
		p2pll->max_post_div = 0x7f;
		p2pll->min_frac_feedback_div = 0;
		p2pll->max_frac_feedback_div = 9;
	} else {
		p1pll->min_post_div = 1;
		p1pll->max_post_div = 16;
		p1pll->min_frac_feedback_div = 0;
		p1pll->max_frac_feedback_div = 0;
		p2pll->min_post_div = 1;
		p2pll->max_post_div = 12;
		p2pll->min_frac_feedback_div = 0;
		p2pll->max_frac_feedback_div = 0;
	}

	/* dcpll is DCE4 only */
	dcpll->min_post_div = 2;
	dcpll->max_post_div = 0x7f;
	dcpll->min_frac_feedback_div = 0;
	dcpll->max_frac_feedback_div = 9;
	dcpll->min_ref_div = 2;
	dcpll->max_ref_div = 0x3ff;
	dcpll->min_feedback_div = 4;
	dcpll->max_feedback_div = 0xfff;
	dcpll->best_vco = 0;

	p1pll->min_ref_div = 2;
	p1pll->max_ref_div = 0x3ff;
	p1pll->min_feedback_div = 4;
	p1pll->max_feedback_div = 0x7ff;
	p1pll->best_vco = 0;

	p2pll->min_ref_div = 2;
	p2pll->max_ref_div = 0x3ff;
	p2pll->min_feedback_div = 4;
	p2pll->max_feedback_div = 0x7ff;
	p2pll->best_vco = 0;

	/* system clock */
	spll->min_post_div = 1;
	spll->max_post_div = 1;
	spll->min_ref_div = 2;
	spll->max_ref_div = 0xff;
	spll->min_feedback_div = 4;
	spll->max_feedback_div = 0xff;
	spll->best_vco = 0;

	/* memory clock */
	mpll->min_post_div = 1;
	mpll->max_post_div = 1;
	mpll->min_ref_div = 2;
	mpll->max_ref_div = 0xff;
	mpll->min_feedback_div = 4;
	mpll->max_feedback_div = 0xff;
	mpll->best_vco = 0;

	if (!rdev->clock.default_sclk)
		rdev->clock.default_sclk = radeon_get_engine_clock(rdev);
	if ((!rdev->clock.default_mclk) && rdev->asic->get_memory_clock)
		rdev->clock.default_mclk = radeon_get_memory_clock(rdev);

	rdev->pm.current_sclk = rdev->clock.default_sclk;
	rdev->pm.current_mclk = rdev->clock.default_mclk;

}

/* 10 khz */
static uint32_t calc_eng_mem_clock(struct radeon_device *rdev,
				   uint32_t req_clock,
				   int *fb_div, int *post_div)
{
	struct radeon_pll *spll = &rdev->clock.spll;
	int ref_div = spll->reference_div;

	if (!ref_div)
		ref_div =
		    RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV) &
		    RADEON_M_SPLL_REF_DIV_MASK;

	if (req_clock < 15000) {
		*post_div = 8;
		req_clock *= 8;
	} else if (req_clock < 30000) {
		*post_div = 4;
		req_clock *= 4;
	} else if (req_clock < 60000) {
		*post_div = 2;
		req_clock *= 2;
	} else
		*post_div = 1;

	req_clock *= ref_div;
	req_clock += spll->reference_freq;
	req_clock /= (2 * spll->reference_freq);

	*fb_div = req_clock & 0xff;

	req_clock = (req_clock & 0xffff) << 1;
	req_clock *= spll->reference_freq;
	req_clock /= ref_div;
	req_clock /= *post_div;

	return req_clock;
}

/* 10 khz */
void radeon_legacy_set_engine_clock(struct radeon_device *rdev,
				    uint32_t eng_clock)
{
	uint32_t tmp;
	int fb_div, post_div;

	/* XXX: wait for idle */

	eng_clock = calc_eng_mem_clock(rdev, eng_clock, &fb_div, &post_div);

	tmp = RREG32_PLL(RADEON_CLK_PIN_CNTL);
	tmp &= ~RADEON_DONT_USE_XTALIN;
	WREG32_PLL(RADEON_CLK_PIN_CNTL, tmp);

	tmp = RREG32_PLL(RADEON_SCLK_CNTL);
	tmp &= ~RADEON_SCLK_SRC_SEL_MASK;
	WREG32_PLL(RADEON_SCLK_CNTL, tmp);

	udelay(10);

	tmp = RREG32_PLL(RADEON_SPLL_CNTL);
	tmp |= RADEON_SPLL_SLEEP;
	WREG32_PLL(RADEON_SPLL_CNTL, tmp);

	udelay(2);

	tmp = RREG32_PLL(RADEON_SPLL_CNTL);
	tmp |= RADEON_SPLL_RESET;
	WREG32_PLL(RADEON_SPLL_CNTL, tmp);

	udelay(200);

	tmp = RREG32_PLL(RADEON_M_SPLL_REF_FB_DIV);
	tmp &= ~(RADEON_SPLL_FB_DIV_MASK << RADEON_SPLL_FB_DIV_SHIFT);
	tmp |= (fb_div & RADEON_SPLL_FB_DIV_MASK) << RADEON_SPLL_FB_DIV_SHIFT;
	WREG32_PLL(RADEON_M_SPLL_REF_FB_DIV, tmp);

	/* XXX: verify on different asics */
	tmp = RREG32_PLL(RADEON_SPLL_CNTL);
	tmp &= ~RADEON_SPLL_PVG_MASK;
	if ((eng_clock * post_div) >= 90000)
		tmp |= (0x7 << RADEON_SPLL_PVG_SHIFT);
	else
		tmp |= (0x4 << RADEON_SPLL_PVG_SHIFT);
	WREG32_PLL(RADEON_SPLL_CNTL, tmp);

	tmp = RREG32_PLL(RADEON_SPLL_CNTL);
	tmp &= ~RADEON_SPLL_SLEEP;
	WREG32_PLL(RADEON_SPLL_CNTL, tmp);

	udelay(2);

	tmp = RREG32_PLL(RADEON_SPLL_CNTL);
	tmp &= ~RADEON_SPLL_RESET;
	WREG32_PLL(RADEON_SPLL_CNTL, tmp);

	udelay(200);

	tmp = RREG32_PLL(RADEON_SCLK_CNTL);
	tmp &= ~RADEON_SCLK_SRC_SEL_MASK;
	switch (post_div) {
	case 1:
	default:
		tmp |= 1;
		break;
	case 2:
		tmp |= 2;
		break;
	case 4:
		tmp |= 3;
		break;
	case 8:
		tmp |= 4;
		break;
	}
	WREG32_PLL(RADEON_SCLK_CNTL, tmp);

	udelay(20);

	tmp = RREG32_PLL(RADEON_CLK_PIN_CNTL);
	tmp |= RADEON_DONT_USE_XTALIN;
	WREG32_PLL(RADEON_CLK_PIN_CNTL, tmp);

	udelay(10);
}

void radeon_legacy_set_clock_gating(struct radeon_device *rdev, int enable)
{
	uint32_t tmp;

	if (enable) {
		if (rdev->flags & RADEON_SINGLE_CRTC) {
			tmp = RREG32_PLL(RADEON_SCLK_CNTL);
			if ((RREG32(RADEON_CONFIG_CNTL) &
			     RADEON_CFG_ATI_REV_ID_MASK) >
			    RADEON_CFG_ATI_REV_A13) {
				tmp &=
				    ~(RADEON_SCLK_FORCE_CP |
				      RADEON_SCLK_FORCE_RB);
			}
			tmp &=
			    ~(RADEON_SCLK_FORCE_HDP | RADEON_SCLK_FORCE_DISP1 |
			      RADEON_SCLK_FORCE_TOP | RADEON_SCLK_FORCE_SE |
			      RADEON_SCLK_FORCE_IDCT | RADEON_SCLK_FORCE_RE |
			      RADEON_SCLK_FORCE_PB | RADEON_SCLK_FORCE_TAM |
			      RADEON_SCLK_FORCE_TDM);
			WREG32_PLL(RADEON_SCLK_CNTL, tmp);
		} else if (ASIC_IS_R300(rdev)) {
			if ((rdev->family == CHIP_RS400) ||
			    (rdev->family == CHIP_RS480)) {
				tmp = RREG32_PLL(RADEON_SCLK_CNTL);
				tmp &=
				    ~(RADEON_SCLK_FORCE_DISP2 |
				      RADEON_SCLK_FORCE_CP |
				      RADEON_SCLK_FORCE_HDP |
				      RADEON_SCLK_FORCE_DISP1 |
				      RADEON_SCLK_FORCE_TOP |
				      RADEON_SCLK_FORCE_E2 | R300_SCLK_FORCE_VAP
				      | RADEON_SCLK_FORCE_IDCT |
				      RADEON_SCLK_FORCE_VIP | R300_SCLK_FORCE_SR
				      | R300_SCLK_FORCE_PX | R300_SCLK_FORCE_TX
				      | R300_SCLK_FORCE_US |
				      RADEON_SCLK_FORCE_TV_SCLK |
				      R300_SCLK_FORCE_SU |
				      RADEON_SCLK_FORCE_OV0);
				tmp |= RADEON_DYN_STOP_LAT_MASK;
				tmp |=
				    RADEON_SCLK_FORCE_TOP |
				    RADEON_SCLK_FORCE_VIP;
				WREG32_PLL(RADEON_SCLK_CNTL, tmp);

				tmp = RREG32_PLL(RADEON_SCLK_MORE_CNTL);
				tmp &= ~RADEON_SCLK_MORE_FORCEON;
				tmp |= RADEON_SCLK_MORE_MAX_DYN_STOP_LAT;
				WREG32_PLL(RADEON_SCLK_MORE_CNTL, tmp);

				tmp = RREG32_PLL(RADEON_VCLK_ECP_CNTL);
				tmp |= (RADEON_PIXCLK_ALWAYS_ONb |
					RADEON_PIXCLK_DAC_ALWAYS_ONb);
				WREG32_PLL(RADEON_VCLK_ECP_CNTL, tmp);

				tmp = RREG32_PLL(RADEON_PIXCLKS_CNTL);
				tmp |= (RADEON_PIX2CLK_ALWAYS_ONb |
					RADEON_PIX2CLK_DAC_ALWAYS_ONb |
					RADEON_DISP_TVOUT_PIXCLK_TV_ALWAYS_ONb |
					R300_DVOCLK_ALWAYS_ONb |
					RADEON_PIXCLK_BLEND_ALWAYS_ONb |
					RADEON_PIXCLK_GV_ALWAYS_ONb |
					R300_PIXCLK_DVO_ALWAYS_ONb |
					RADEON_PIXCLK_LVDS_ALWAYS_ONb |
					RADEON_PIXCLK_TMDS_ALWAYS_ONb |
					R300_PIXCLK_TRANS_ALWAYS_ONb |
					R300_PIXCLK_TVO_ALWAYS_ONb |
					R300_P2G2CLK_ALWAYS_ONb |
					R300_P2G2CLK_DAC_ALWAYS_ONb);
				WREG32_PLL(RADEON_PIXCLKS_CNTL, tmp);
			} else if (rdev->family >= CHIP_RV350) {
				tmp = RREG32_PLL(R300_SCLK_CNTL2);
				tmp &= ~(R300_SCLK_FORCE_TCL |
					 R300_SCLK_FORCE_GA |
					 R300_SCLK_FORCE_CBA);
				tmp |= (R300_SCLK_TCL_MAX_DYN_STOP_LAT |
					R300_SCLK_GA_MAX_DYN_STOP_LAT |
					R300_SCLK_CBA_MAX_DYN_STOP_LAT);
				WREG32_PLL(R300_SCLK_CNTL2, tmp);

				tmp = RREG32_PLL(RADEON_SCLK_CNTL);
				tmp &=
				    ~(RADEON_SCLK_FORCE_DISP2 |
				      RADEON_SCLK_FORCE_CP |
				      RADEON_SCLK_FORCE_HDP |
				      RADEON_SCLK_FORCE_DISP1 |
				      RADEON_SCLK_FORCE_TOP |
				      RADEON_SCLK_FORCE_E2 | R300_SCLK_FORCE_VAP
				      | RADEON_SCLK_FORCE_IDCT |
				      RADEON_SCLK_FORCE_VIP | R300_SCLK_FORCE_SR
				      | R300_SCLK_FORCE_PX | R300_SCLK_FORCE_TX
				      | R300_SCLK_FORCE_US |
				      RADEON_SCLK_FORCE_TV_SCLK |
				      R300_SCLK_FORCE_SU |
				      RADEON_SCLK_FORCE_OV0);
				tmp |= RADEON_DYN_STOP_LAT_MASK;
				WREG32_PLL(RADEON_SCLK_CNTL, tmp);

				tmp = RREG32_PLL(RADEON_SCLK_MORE_CNTL);
				tmp &= ~RADEON_SCLK_MORE_FORCEON;
				tmp |= RADEON_SCLK_MORE_MAX_DYN_STOP_LAT;
				WREG32_PLL(RADEON_SCLK_MORE_CNTL, tmp);

				tmp = RREG32_PLL(RADEON_VCLK_ECP_CNTL);
				tmp |= (RADEON_PIXCLK_ALWAYS_ONb |
					RADEON_PIXCLK_DAC_ALWAYS_ONb);
				WREG32_PLL(RADEON_VCLK_ECP_CNTL, tmp);

				tmp = RREG32_PLL(RADEON_PIXCLKS_CNTL);
				tmp |= (RADEON_PIX2CLK_ALWAYS_ONb |
					RADEON_PIX2CLK_DAC_ALWAYS_ONb |
					RADEON_DISP_TVOUT_PIXCLK_TV_ALWAYS_ONb |
					R300_DVOCLK_ALWAYS_ONb |
					RADEON_PIXCLK_BLEND_ALWAYS_ONb |
					RADEON_PIXCLK_GV_ALWAYS_ONb |
					R300_PIXCLK_DVO_ALWAYS_ONb |
					RADEON_PIXCLK_LVDS_ALWAYS_ONb |
					RADEON_PIXCLK_TMDS_ALWAYS_ONb |
					R300_PIXCLK_TRANS_ALWAYS_ONb |
					R300_PIXCLK_TVO_ALWAYS_ONb |
					R300_P2G2CLK_ALWAYS_ONb |
					R300_P2G2CLK_DAC_ALWAYS_ONb);
				WREG32_PLL(RADEON_PIXCLKS_CNTL, tmp);

				tmp = RREG32_PLL(RADEON_MCLK_MISC);
				tmp |= (RADEON_MC_MCLK_DYN_ENABLE |
					RADEON_IO_MCLK_DYN_ENABLE);
				WREG32_PLL(RADEON_MCLK_MISC, tmp);

				tmp = RREG32_PLL(RADEON_MCLK_CNTL);
				tmp |= (RADEON_FORCEON_MCLKA |
					RADEON_FORCEON_MCLKB);

				tmp &= ~(RADEON_FORCEON_YCLKA |
					 RADEON_FORCEON_YCLKB |
					 RADEON_FORCEON_MC);

				/* Some releases of vbios have set DISABLE_MC_MCLKA
				   and DISABLE_MC_MCLKB bits in the vbios table.  Setting these
				   bits will cause H/W hang when reading video memory with dynamic clocking
				   enabled. */
				if ((tmp & R300_DISABLE_MC_MCLKA) &&
				    (tmp & R300_DISABLE_MC_MCLKB)) {
					/* If both bits are set, then check the active channels */
					tmp = RREG32_PLL(RADEON_MCLK_CNTL);
					if (rdev->mc.vram_width == 64) {
						if (RREG32(RADEON_MEM_CNTL) &
						    R300_MEM_USE_CD_CH_ONLY)
							tmp &=
							    ~R300_DISABLE_MC_MCLKB;
						else
							tmp &=
							    ~R300_DISABLE_MC_MCLKA;
					} else {
						tmp &= ~(R300_DISABLE_MC_MCLKA |
							 R300_DISABLE_MC_MCLKB);
					}
				}

				WREG32_PLL(RADEON_MCLK_CNTL, tmp);
			} else {
				tmp = RREG32_PLL(RADEON_SCLK_CNTL);
				tmp &= ~(R300_SCLK_FORCE_VAP);
				tmp |= RADEON_SCLK_FORCE_CP;
				WREG32_PLL(RADEON_SCLK_CNTL, tmp);
				udelay(15000);

				tmp = RREG32_PLL(R300_SCLK_CNTL2);
				tmp &= ~(R300_SCLK_FORCE_TCL |
					 R300_SCLK_FORCE_GA |
					 R300_SCLK_FORCE_CBA);
				WREG32_PLL(R300_SCLK_CNTL2, tmp);
			}
		} else {
			tmp = RREG32_PLL(RADEON_CLK_PWRMGT_CNTL);

			tmp &= ~(RADEON_ACTIVE_HILO_LAT_MASK |
				 RADEON_DISP_DYN_STOP_LAT_MASK |
				 RADEON_DYN_STOP_MODE_MASK);

			tmp |= (RADEON_ENGIN_DYNCLK_MODE |
				(0x01 << RADEON_ACTIVE_HILO_LAT_SHIFT));
			WREG32_PLL(RADEON_CLK_PWRMGT_CNTL, tmp);
			udelay(15000);

			tmp = RREG32_PLL(RADEON_CLK_PIN_CNTL);
			tmp |= RADEON_SCLK_DYN_START_CNTL;
			WREG32_PLL(RADEON_CLK_PIN_CNTL, tmp);
			udelay(15000);

			/* When DRI is enabled, setting DYN_STOP_LAT to zero can cause some R200
			   to lockup randomly, leave them as set by BIOS.
			 */
			tmp = RREG32_PLL(RADEON_SCLK_CNTL);
			/*tmp &= RADEON_SCLK_SRC_SEL_MASK; */
			tmp &= ~RADEON_SCLK_FORCEON_MASK;

			/*RAGE_6::A11 A12 A12N1 A13, RV250::A11 A12, R300 */
			if (((rdev->family == CHIP_RV250) &&
			     ((RREG32(RADEON_CONFIG_CNTL) &
			       RADEON_CFG_ATI_REV_ID_MASK) <
			      RADEON_CFG_ATI_REV_A13))
			    || ((rdev->family == CHIP_RV100)
				&&
				((RREG32(RADEON_CONFIG_CNTL) &
				  RADEON_CFG_ATI_REV_ID_MASK) <=
				 RADEON_CFG_ATI_REV_A13))) {
				tmp |= RADEON_SCLK_FORCE_CP;
				tmp |= RADEON_SCLK_FORCE_VIP;
			}

			WREG32_PLL(RADEON_SCLK_CNTL, tmp);

			if ((rdev->family == CHIP_RV200) ||
			    (rdev->family == CHIP_RV250) ||
			    (rdev->family == CHIP_RV280)) {
				tmp = RREG32_PLL(RADEON_SCLK_MORE_CNTL);
				tmp &= ~RADEON_SCLK_MORE_FORCEON;

				/* RV200::A11 A12 RV250::A11 A12 */
				if (((rdev->family == CHIP_RV200) ||
				     (rdev->family == CHIP_RV250)) &&
				    ((RREG32(RADEON_CONFIG_CNTL) &
				      RADEON_CFG_ATI_REV_ID_MASK) <
				     RADEON_CFG_ATI_REV_A13)) {
					tmp |= RADEON_SCLK_MORE_FORCEON;
				}
				WREG32_PLL(RADEON_SCLK_MORE_CNTL, tmp);
				udelay(15000);
			}

			/* RV200::A11 A12, RV250::A11 A12 */
			if (((rdev->family == CHIP_RV200) ||
			     (rdev->family == CHIP_RV250)) &&
			    ((RREG32(RADEON_CONFIG_CNTL) &
			      RADEON_CFG_ATI_REV_ID_MASK) <
			     RADEON_CFG_ATI_REV_A13)) {
				tmp = RREG32_PLL(RADEON_PLL_PWRMGT_CNTL);
				tmp |= RADEON_TCL_BYPASS_DISABLE;
				WREG32_PLL(RADEON_PLL_PWRMGT_CNTL, tmp);
			}
			udelay(15000);

			/*enable dynamic mode for display clocks (PIXCLK and PIX2CLK) */
			tmp = RREG32_PLL(RADEON_PIXCLKS_CNTL);
			tmp |= (RADEON_PIX2CLK_ALWAYS_ONb |
				RADEON_PIX2CLK_DAC_ALWAYS_ONb |
				RADEON_PIXCLK_BLEND_ALWAYS_ONb |
				RADEON_PIXCLK_GV_ALWAYS_ONb |
				RADEON_PIXCLK_DIG_TMDS_ALWAYS_ONb |
				RADEON_PIXCLK_LVDS_ALWAYS_ONb |
				RADEON_PIXCLK_TMDS_ALWAYS_ONb);

			WREG32_PLL(RADEON_PIXCLKS_CNTL, tmp);
			udelay(15000);

			tmp = RREG32_PLL(RADEON_VCLK_ECP_CNTL);
			tmp |= (RADEON_PIXCLK_ALWAYS_ONb |
				RADEON_PIXCLK_DAC_ALWAYS_ONb);

			WREG32_PLL(RADEON_VCLK_ECP_CNTL, tmp);
			udelay(15000);
		}
	} else {
		/* Turn everything OFF (ForceON to everything) */
		if (rdev->flags & RADEON_SINGLE_CRTC) {
			tmp = RREG32_PLL(RADEON_SCLK_CNTL);
			tmp |= (RADEON_SCLK_FORCE_CP | RADEON_SCLK_FORCE_HDP |
				RADEON_SCLK_FORCE_DISP1 | RADEON_SCLK_FORCE_TOP
				| RADEON_SCLK_FORCE_E2 | RADEON_SCLK_FORCE_SE |
				RADEON_SCLK_FORCE_IDCT | RADEON_SCLK_FORCE_VIP |
				RADEON_SCLK_FORCE_RE | RADEON_SCLK_FORCE_PB |
				RADEON_SCLK_FORCE_TAM | RADEON_SCLK_FORCE_TDM |
				RADEON_SCLK_FORCE_RB);
			WREG32_PLL(RADEON_SCLK_CNTL, tmp);
		} else if ((rdev->family == CHIP_RS400) ||
			   (rdev->family == CHIP_RS480)) {
			tmp = RREG32_PLL(RADEON_SCLK_CNTL);
			tmp |= (RADEON_SCLK_FORCE_DISP2 | RADEON_SCLK_FORCE_CP |
				RADEON_SCLK_FORCE_HDP | RADEON_SCLK_FORCE_DISP1
				| RADEON_SCLK_FORCE_TOP | RADEON_SCLK_FORCE_E2 |
				R300_SCLK_FORCE_VAP | RADEON_SCLK_FORCE_IDCT |
				RADEON_SCLK_FORCE_VIP | R300_SCLK_FORCE_SR |
				R300_SCLK_FORCE_PX | R300_SCLK_FORCE_TX |
				R300_SCLK_FORCE_US | RADEON_SCLK_FORCE_TV_SCLK |
				R300_SCLK_FORCE_SU | RADEON_SCLK_FORCE_OV0);
			WREG32_PLL(RADEON_SCLK_CNTL, tmp);

			tmp = RREG32_PLL(RADEON_SCLK_MORE_CNTL);
			tmp |= RADEON_SCLK_MORE_FORCEON;
			WREG32_PLL(RADEON_SCLK_MORE_CNTL, tmp);

			tmp = RREG32_PLL(RADEON_VCLK_ECP_CNTL);
			tmp &= ~(RADEON_PIXCLK_ALWAYS_ONb |
				 RADEON_PIXCLK_DAC_ALWAYS_ONb |
				 R300_DISP_DAC_PIXCLK_DAC_BLANK_OFF);
			WREG32_PLL(RADEON_VCLK_ECP_CNTL, tmp);

			tmp = RREG32_PLL(RADEON_PIXCLKS_CNTL);
			tmp &= ~(RADEON_PIX2CLK_ALWAYS_ONb |
				 RADEON_PIX2CLK_DAC_ALWAYS_ONb |
				 RADEON_DISP_TVOUT_PIXCLK_TV_ALWAYS_ONb |
				 R300_DVOCLK_ALWAYS_ONb |
				 RADEON_PIXCLK_BLEND_ALWAYS_ONb |
				 RADEON_PIXCLK_GV_ALWAYS_ONb |
				 R300_PIXCLK_DVO_ALWAYS_ONb |
				 RADEON_PIXCLK_LVDS_ALWAYS_ONb |
				 RADEON_PIXCLK_TMDS_ALWAYS_ONb |
				 R300_PIXCLK_TRANS_ALWAYS_ONb |
				 R300_PIXCLK_TVO_ALWAYS_ONb |
				 R300_P2G2CLK_ALWAYS_ONb |
				 R300_P2G2CLK_DAC_ALWAYS_ONb |
				 R300_DISP_DAC_PIXCLK_DAC2_BLANK_OFF);
			WREG32_PLL(RADEON_PIXCLKS_CNTL, tmp);
		} else if (rdev->family >= CHIP_RV350) {
			/* for RV350/M10, no delays are required. */
			tmp = RREG32_PLL(R300_SCLK_CNTL2);
			tmp |= (R300_SCLK_FORCE_TCL |
				R300_SCLK_FORCE_GA | R300_SCLK_FORCE_CBA);
			WREG32_PLL(R300_SCLK_CNTL2, tmp);

			tmp = RREG32_PLL(RADEON_SCLK_CNTL);
			tmp |= (RADEON_SCLK_FORCE_DISP2 | RADEON_SCLK_FORCE_CP |
				RADEON_SCLK_FORCE_HDP | RADEON_SCLK_FORCE_DISP1
				| RADEON_SCLK_FORCE_TOP | RADEON_SCLK_FORCE_E2 |
				R300_SCLK_FORCE_VAP | RADEON_SCLK_FORCE_IDCT |
				RADEON_SCLK_FORCE_VIP | R300_SCLK_FORCE_SR |
				R300_SCLK_FORCE_PX | R300_SCLK_FORCE_TX |
				R300_SCLK_FORCE_US | RADEON_SCLK_FORCE_TV_SCLK |
				R300_SCLK_FORCE_SU | RADEON_SCLK_FORCE_OV0);
			WREG32_PLL(RADEON_SCLK_CNTL, tmp);

			tmp = RREG32_PLL(RADEON_SCLK_MORE_CNTL);
			tmp |= RADEON_SCLK_MORE_FORCEON;
			WREG32_PLL(RADEON_SCLK_MORE_CNTL, tmp);

			tmp = RREG32_PLL(RADEON_MCLK_CNTL);
			tmp |= (RADEON_FORCEON_MCLKA |
				RADEON_FORCEON_MCLKB |
				RADEON_FORCEON_YCLKA |
				RADEON_FORCEON_YCLKB | RADEON_FORCEON_MC);
			WREG32_PLL(RADEON_MCLK_CNTL, tmp);

			tmp = RREG32_PLL(RADEON_VCLK_ECP_CNTL);
			tmp &= ~(RADEON_PIXCLK_ALWAYS_ONb |
				 RADEON_PIXCLK_DAC_ALWAYS_ONb |
				 R300_DISP_DAC_PIXCLK_DAC_BLANK_OFF);
			WREG32_PLL(RADEON_VCLK_ECP_CNTL, tmp);

			tmp = RREG32_PLL(RADEON_PIXCLKS_CNTL);
			tmp &= ~(RADEON_PIX2CLK_ALWAYS_ONb |
				 RADEON_PIX2CLK_DAC_ALWAYS_ONb |
				 RADEON_DISP_TVOUT_PIXCLK_TV_ALWAYS_ONb |
				 R300_DVOCLK_ALWAYS_ONb |
				 RADEON_PIXCLK_BLEND_ALWAYS_ONb |
				 RADEON_PIXCLK_GV_ALWAYS_ONb |
				 R300_PIXCLK_DVO_ALWAYS_ONb |
				 RADEON_PIXCLK_LVDS_ALWAYS_ONb |
				 RADEON_PIXCLK_TMDS_ALWAYS_ONb |
				 R300_PIXCLK_TRANS_ALWAYS_ONb |
				 R300_PIXCLK_TVO_ALWAYS_ONb |
				 R300_P2G2CLK_ALWAYS_ONb |
				 R300_P2G2CLK_DAC_ALWAYS_ONb |
				 R300_DISP_DAC_PIXCLK_DAC2_BLANK_OFF);
			WREG32_PLL(RADEON_PIXCLKS_CNTL, tmp);
		} else {
			tmp = RREG32_PLL(RADEON_SCLK_CNTL);
			tmp |= (RADEON_SCLK_FORCE_CP | RADEON_SCLK_FORCE_E2);
			tmp |= RADEON_SCLK_FORCE_SE;

			if (rdev->flags & RADEON_SINGLE_CRTC) {
				tmp |= (RADEON_SCLK_FORCE_RB |
					RADEON_SCLK_FORCE_TDM |
					RADEON_SCLK_FORCE_TAM |
					RADEON_SCLK_FORCE_PB |
					RADEON_SCLK_FORCE_RE |
					RADEON_SCLK_FORCE_VIP |
					RADEON_SCLK_FORCE_IDCT |
					RADEON_SCLK_FORCE_TOP |
					RADEON_SCLK_FORCE_DISP1 |
					RADEON_SCLK_FORCE_DISP2 |
					RADEON_SCLK_FORCE_HDP);
			} else if ((rdev->family == CHIP_R300) ||
				   (rdev->family == CHIP_R350)) {
				tmp |= (RADEON_SCLK_FORCE_HDP |
					RADEON_SCLK_FORCE_DISP1 |
					RADEON_SCLK_FORCE_DISP2 |
					RADEON_SCLK_FORCE_TOP |
					RADEON_SCLK_FORCE_IDCT |
					RADEON_SCLK_FORCE_VIP);
			}
			WREG32_PLL(RADEON_SCLK_CNTL, tmp);

			udelay(16000);

			if ((rdev->family == CHIP_R300) ||
			    (rdev->family == CHIP_R350)) {
				tmp = RREG32_PLL(R300_SCLK_CNTL2);
				tmp |= (R300_SCLK_FORCE_TCL |
					R300_SCLK_FORCE_GA |
					R300_SCLK_FORCE_CBA);
				WREG32_PLL(R300_SCLK_CNTL2, tmp);
				udelay(16000);
			}

			if (rdev->flags & RADEON_IS_IGP) {
				tmp = RREG32_PLL(RADEON_MCLK_CNTL);
				tmp &= ~(RADEON_FORCEON_MCLKA |
					 RADEON_FORCEON_YCLKA);
				WREG32_PLL(RADEON_MCLK_CNTL, tmp);
				udelay(16000);
			}

			if ((rdev->family == CHIP_RV200) ||
			    (rdev->family == CHIP_RV250) ||
			    (rdev->family == CHIP_RV280)) {
				tmp = RREG32_PLL(RADEON_SCLK_MORE_CNTL);
				tmp |= RADEON_SCLK_MORE_FORCEON;
				WREG32_PLL(RADEON_SCLK_MORE_CNTL, tmp);
				udelay(16000);
			}

			tmp = RREG32_PLL(RADEON_PIXCLKS_CNTL);
			tmp &= ~(RADEON_PIX2CLK_ALWAYS_ONb |
				 RADEON_PIX2CLK_DAC_ALWAYS_ONb |
				 RADEON_PIXCLK_BLEND_ALWAYS_ONb |
				 RADEON_PIXCLK_GV_ALWAYS_ONb |
				 RADEON_PIXCLK_DIG_TMDS_ALWAYS_ONb |
				 RADEON_PIXCLK_LVDS_ALWAYS_ONb |
				 RADEON_PIXCLK_TMDS_ALWAYS_ONb);

			WREG32_PLL(RADEON_PIXCLKS_CNTL, tmp);
			udelay(16000);

			tmp = RREG32_PLL(RADEON_VCLK_ECP_CNTL);
			tmp &= ~(RADEON_PIXCLK_ALWAYS_ONb |
				 RADEON_PIXCLK_DAC_ALWAYS_ONb);
			WREG32_PLL(RADEON_VCLK_ECP_CNTL, tmp);
		}
	}
}

