/*
 * Copyright 2010 Matt Turner.
 * Copyright 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *	    Matt Turner
 *	    Dave Airlie
 */

#include <linux/delay.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "mgag200_drv.h"

#define MGAG200_LUT_SIZE 256

/*
 * This file contains setup code for the CRTC.
 */

static void mga_crtc_load_lut(struct drm_crtc *crtc)
{
	struct mga_crtc *mga_crtc = to_mga_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = dev->dev_private;
	struct drm_framebuffer *fb = crtc->fb;
	int i;

	if (!crtc->enabled)
		return;

	WREG8(DAC_INDEX + MGA1064_INDEX, 0);

	if (fb && fb->bits_per_pixel == 16) {
		int inc = (fb->depth == 15) ? 8 : 4;
		u8 r, b;
		for (i = 0; i < MGAG200_LUT_SIZE; i += inc) {
			if (fb->depth == 16) {
				if (i > (MGAG200_LUT_SIZE >> 1)) {
					r = b = 0;
				} else {
					r = mga_crtc->lut_r[i << 1];
					b = mga_crtc->lut_b[i << 1];
				}
			} else {
				r = mga_crtc->lut_r[i];
				b = mga_crtc->lut_b[i];
			}
			/* VGA registers */
			WREG8(DAC_INDEX + MGA1064_COL_PAL, r);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, mga_crtc->lut_g[i]);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, b);
		}
		return;
	}
	for (i = 0; i < MGAG200_LUT_SIZE; i++) {
		/* VGA registers */
		WREG8(DAC_INDEX + MGA1064_COL_PAL, mga_crtc->lut_r[i]);
		WREG8(DAC_INDEX + MGA1064_COL_PAL, mga_crtc->lut_g[i]);
		WREG8(DAC_INDEX + MGA1064_COL_PAL, mga_crtc->lut_b[i]);
	}
}

static inline void mga_wait_vsync(struct mga_device *mdev)
{
	unsigned long timeout = jiffies + HZ/10;
	unsigned int status = 0;

	do {
		status = RREG32(MGAREG_Status);
	} while ((status & 0x08) && time_before(jiffies, timeout));
	timeout = jiffies + HZ/10;
	status = 0;
	do {
		status = RREG32(MGAREG_Status);
	} while (!(status & 0x08) && time_before(jiffies, timeout));
}

static inline void mga_wait_busy(struct mga_device *mdev)
{
	unsigned long timeout = jiffies + HZ;
	unsigned int status = 0;
	do {
		status = RREG8(MGAREG_Status + 2);
	} while ((status & 0x01) && time_before(jiffies, timeout));
}

/*
 * The core passes the desired mode to the CRTC code to see whether any
 * CRTC-specific modifications need to be made to it. We're in a position
 * to just pass that straight through, so this does nothing
 */
static bool mga_crtc_mode_fixup(struct drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int mga_g200se_set_plls(struct mga_device *mdev, long clock)
{
	unsigned int vcomax, vcomin, pllreffreq;
	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n;
	unsigned int computed;

	m = n = p = 0;
	vcomax = 320000;
	vcomin = 160000;
	pllreffreq = 25000;

	delta = 0xffffffff;
	permitteddelta = clock * 5 / 1000;

	for (testp = 8; testp > 0; testp /= 2) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testn = 17; testn < 256; testn++) {
			for (testm = 1; testm < 32; testm++) {
				computed = (pllreffreq * testn) /
					(testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					m = testm - 1;
					n = testn - 1;
					p = testp - 1;
				}
			}
		}
	}

	if (delta > permitteddelta) {
		printk(KERN_WARNING "PLL delta too large\n");
		return 1;
	}

	WREG_DAC(MGA1064_PIX_PLLC_M, m);
	WREG_DAC(MGA1064_PIX_PLLC_N, n);
	WREG_DAC(MGA1064_PIX_PLLC_P, p);
	return 0;
}

static int mga_g200wb_set_plls(struct mga_device *mdev, long clock)
{
	unsigned int vcomax, vcomin, pllreffreq;
	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n;
	unsigned int computed;
	int i, j, tmpcount, vcount;
	bool pll_locked = false;
	u8 tmp;

	m = n = p = 0;
	vcomax = 550000;
	vcomin = 150000;
	pllreffreq = 48000;

	delta = 0xffffffff;
	permitteddelta = clock * 5 / 1000;

	for (testp = 1; testp < 9; testp++) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testm = 1; testm < 17; testm++) {
			for (testn = 1; testn < 151; testn++) {
				computed = (pllreffreq * testn) /
					(testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					n = testn - 1;
					m = (testm - 1) | ((n >> 1) & 0x80);
					p = testp - 1;
				}
			}
		}
	}

	for (i = 0; i <= 32 && pll_locked == false; i++) {
		if (i > 0) {
			WREG8(MGAREG_CRTC_INDEX, 0x1e);
			tmp = RREG8(MGAREG_CRTC_DATA);
			if (tmp < 0xff)
				WREG8(MGAREG_CRTC_DATA, tmp+1);
		}

		/* set pixclkdis to 1 */
		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_PIX_CLK_CTL_CLK_DIS;
		WREG8(DAC_DATA, tmp);

		WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_REMHEADCTL_CLKDIS;
		WREG8(DAC_DATA, tmp);

		/* select PLL Set C */
		tmp = RREG8(MGAREG_MEM_MISC_READ);
		tmp |= 0x3 << 2;
		WREG8(MGAREG_MEM_MISC_WRITE, tmp);

		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_PIX_CLK_CTL_CLK_POW_DOWN | 0x80;
		WREG8(DAC_DATA, tmp);

		udelay(500);

		/* reset the PLL */
		WREG8(DAC_INDEX, MGA1064_VREF_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~0x04;
		WREG8(DAC_DATA, tmp);

		udelay(50);

		/* program pixel pll register */
		WREG_DAC(MGA1064_WB_PIX_PLLC_N, n);
		WREG_DAC(MGA1064_WB_PIX_PLLC_M, m);
		WREG_DAC(MGA1064_WB_PIX_PLLC_P, p);

		udelay(50);

		/* turn pll on */
		WREG8(DAC_INDEX, MGA1064_VREF_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= 0x04;
		WREG_DAC(MGA1064_VREF_CTL, tmp);

		udelay(500);

		/* select the pixel pll */
		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_PIX_CLK_CTL_SEL_MSK;
		tmp |= MGA1064_PIX_CLK_CTL_SEL_PLL;
		WREG8(DAC_DATA, tmp);

		WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_REMHEADCTL_CLKSL_MSK;
		tmp |= MGA1064_REMHEADCTL_CLKSL_PLL;
		WREG8(DAC_DATA, tmp);

		/* reset dotclock rate bit */
		WREG8(MGAREG_SEQ_INDEX, 1);
		tmp = RREG8(MGAREG_SEQ_DATA);
		tmp &= ~0x8;
		WREG8(MGAREG_SEQ_DATA, tmp);

		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_PIX_CLK_CTL_CLK_DIS;
		WREG8(DAC_DATA, tmp);

		vcount = RREG8(MGAREG_VCOUNT);

		for (j = 0; j < 30 && pll_locked == false; j++) {
			tmpcount = RREG8(MGAREG_VCOUNT);
			if (tmpcount < vcount)
				vcount = 0;
			if ((tmpcount - vcount) > 2)
				pll_locked = true;
			else
				udelay(5);
		}
	}
	WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
	tmp = RREG8(DAC_DATA);
	tmp &= ~MGA1064_REMHEADCTL_CLKDIS;
	WREG_DAC(MGA1064_REMHEADCTL, tmp);
	return 0;
}

static int mga_g200ev_set_plls(struct mga_device *mdev, long clock)
{
	unsigned int vcomax, vcomin, pllreffreq;
	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n;
	unsigned int computed;
	u8 tmp;

	m = n = p = 0;
	vcomax = 550000;
	vcomin = 150000;
	pllreffreq = 50000;

	delta = 0xffffffff;
	permitteddelta = clock * 5 / 1000;

	for (testp = 16; testp > 0; testp--) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testn = 1; testn < 257; testn++) {
			for (testm = 1; testm < 17; testm++) {
				computed = (pllreffreq * testn) /
					(testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					n = testn - 1;
					m = testm - 1;
					p = testp - 1;
				}
			}
		}
	}

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp |= MGA1064_PIX_CLK_CTL_CLK_DIS;
	WREG8(DAC_DATA, tmp);

	tmp = RREG8(MGAREG_MEM_MISC_READ);
	tmp |= 0x3 << 2;
	WREG8(MGAREG_MEM_MISC_WRITE, tmp);

	WREG8(DAC_INDEX, MGA1064_PIX_PLL_STAT);
	tmp = RREG8(DAC_DATA);
	WREG8(DAC_DATA, tmp & ~0x40);

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp |= MGA1064_PIX_CLK_CTL_CLK_POW_DOWN;
	WREG8(DAC_DATA, tmp);

	WREG_DAC(MGA1064_EV_PIX_PLLC_M, m);
	WREG_DAC(MGA1064_EV_PIX_PLLC_N, n);
	WREG_DAC(MGA1064_EV_PIX_PLLC_P, p);

	udelay(50);

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp &= ~MGA1064_PIX_CLK_CTL_CLK_POW_DOWN;
	WREG8(DAC_DATA, tmp);

	udelay(500);

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp &= ~MGA1064_PIX_CLK_CTL_SEL_MSK;
	tmp |= MGA1064_PIX_CLK_CTL_SEL_PLL;
	WREG8(DAC_DATA, tmp);

	WREG8(DAC_INDEX, MGA1064_PIX_PLL_STAT);
	tmp = RREG8(DAC_DATA);
	WREG8(DAC_DATA, tmp | 0x40);

	tmp = RREG8(MGAREG_MEM_MISC_READ);
	tmp |= (0x3 << 2);
	WREG8(MGAREG_MEM_MISC_WRITE, tmp);

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp &= ~MGA1064_PIX_CLK_CTL_CLK_DIS;
	WREG8(DAC_DATA, tmp);

	return 0;
}

static int mga_g200eh_set_plls(struct mga_device *mdev, long clock)
{
	unsigned int vcomax, vcomin, pllreffreq;
	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n;
	unsigned int computed;
	int i, j, tmpcount, vcount;
	u8 tmp;
	bool pll_locked = false;

	m = n = p = 0;
	vcomax = 800000;
	vcomin = 400000;
	pllreffreq = 33333;

	delta = 0xffffffff;
	permitteddelta = clock * 5 / 1000;

	for (testp = 16; testp > 0; testp >>= 1) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testm = 1; testm < 33; testm++) {
			for (testn = 17; testn < 257; testn++) {
				computed = (pllreffreq * testn) /
					(testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					n = testn - 1;
					m = (testm - 1);
					p = testp - 1;
				}
				if ((clock * testp) >= 600000)
					p |= 0x80;
			}
		}
	}
	for (i = 0; i <= 32 && pll_locked == false; i++) {
		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_PIX_CLK_CTL_CLK_DIS;
		WREG8(DAC_DATA, tmp);

		tmp = RREG8(MGAREG_MEM_MISC_READ);
		tmp |= 0x3 << 2;
		WREG8(MGAREG_MEM_MISC_WRITE, tmp);

		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_PIX_CLK_CTL_CLK_POW_DOWN;
		WREG8(DAC_DATA, tmp);

		udelay(500);

		WREG_DAC(MGA1064_EH_PIX_PLLC_M, m);
		WREG_DAC(MGA1064_EH_PIX_PLLC_N, n);
		WREG_DAC(MGA1064_EH_PIX_PLLC_P, p);

		udelay(500);

		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_PIX_CLK_CTL_SEL_MSK;
		tmp |= MGA1064_PIX_CLK_CTL_SEL_PLL;
		WREG8(DAC_DATA, tmp);

		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_PIX_CLK_CTL_CLK_DIS;
		tmp &= ~MGA1064_PIX_CLK_CTL_CLK_POW_DOWN;
		WREG8(DAC_DATA, tmp);

		vcount = RREG8(MGAREG_VCOUNT);

		for (j = 0; j < 30 && pll_locked == false; j++) {
			tmpcount = RREG8(MGAREG_VCOUNT);
			if (tmpcount < vcount)
				vcount = 0;
			if ((tmpcount - vcount) > 2)
				pll_locked = true;
			else
				udelay(5);
		}
	}

	return 0;
}

static int mga_g200er_set_plls(struct mga_device *mdev, long clock)
{
	unsigned int vcomax, vcomin, pllreffreq;
	unsigned int delta, tmpdelta;
	int testr, testn, testm, testo;
	unsigned int p, m, n;
	unsigned int computed, vco;
	int tmp;
	const unsigned int m_div_val[] = { 1, 2, 4, 8 };

	m = n = p = 0;
	vcomax = 1488000;
	vcomin = 1056000;
	pllreffreq = 48000;

	delta = 0xffffffff;

	for (testr = 0; testr < 4; testr++) {
		if (delta == 0)
			break;
		for (testn = 5; testn < 129; testn++) {
			if (delta == 0)
				break;
			for (testm = 3; testm >= 0; testm--) {
				if (delta == 0)
					break;
				for (testo = 5; testo < 33; testo++) {
					vco = pllreffreq * (testn + 1) /
						(testr + 1);
					if (vco < vcomin)
						continue;
					if (vco > vcomax)
						continue;
					computed = vco / (m_div_val[testm] * (testo + 1));
					if (computed > clock)
						tmpdelta = computed - clock;
					else
						tmpdelta = clock - computed;
					if (tmpdelta < delta) {
						delta = tmpdelta;
						m = testm | (testo << 3);
						n = testn;
						p = testr | (testr << 3);
					}
				}
			}
		}
	}

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp |= MGA1064_PIX_CLK_CTL_CLK_DIS;
	WREG8(DAC_DATA, tmp);

	WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
	tmp = RREG8(DAC_DATA);
	tmp |= MGA1064_REMHEADCTL_CLKDIS;
	WREG8(DAC_DATA, tmp);

	tmp = RREG8(MGAREG_MEM_MISC_READ);
	tmp |= (0x3<<2) | 0xc0;
	WREG8(MGAREG_MEM_MISC_WRITE, tmp);

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp &= ~MGA1064_PIX_CLK_CTL_CLK_DIS;
	tmp |= MGA1064_PIX_CLK_CTL_CLK_POW_DOWN;
	WREG8(DAC_DATA, tmp);

	udelay(500);

	WREG_DAC(MGA1064_ER_PIX_PLLC_N, n);
	WREG_DAC(MGA1064_ER_PIX_PLLC_M, m);
	WREG_DAC(MGA1064_ER_PIX_PLLC_P, p);

	udelay(50);

	return 0;
}

static int mga_crtc_set_plls(struct mga_device *mdev, long clock)
{
	switch(mdev->type) {
	case G200_SE_A:
	case G200_SE_B:
		return mga_g200se_set_plls(mdev, clock);
		break;
	case G200_WB:
		return mga_g200wb_set_plls(mdev, clock);
		break;
	case G200_EV:
		return mga_g200ev_set_plls(mdev, clock);
		break;
	case G200_EH:
		return mga_g200eh_set_plls(mdev, clock);
		break;
	case G200_ER:
		return mga_g200er_set_plls(mdev, clock);
		break;
	}
	return 0;
}

static void mga_g200wb_prepare(struct drm_crtc *crtc)
{
	struct mga_device *mdev = crtc->dev->dev_private;
	u8 tmp;
	int iter_max;

	/* 1- The first step is to warn the BMC of an upcoming mode change.
	 * We are putting the misc<0> to output.*/

	WREG8(DAC_INDEX, MGA1064_GEN_IO_CTL);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x10;
	WREG_DAC(MGA1064_GEN_IO_CTL, tmp);

	/* we are putting a 1 on the misc<0> line */
	WREG8(DAC_INDEX, MGA1064_GEN_IO_DATA);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x10;
	WREG_DAC(MGA1064_GEN_IO_DATA, tmp);

	/* 2- Second step to mask and further scan request
	 * This will be done by asserting the remfreqmsk bit (XSPAREREG<7>)
	 */
	WREG8(DAC_INDEX, MGA1064_SPAREREG);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x80;
	WREG_DAC(MGA1064_SPAREREG, tmp);

	/* 3a- the third step is to verifu if there is an active scan
	 * We are searching for a 0 on remhsyncsts <XSPAREREG<0>)
	 */
	iter_max = 300;
	while (!(tmp & 0x1) && iter_max) {
		WREG8(DAC_INDEX, MGA1064_SPAREREG);
		tmp = RREG8(DAC_DATA);
		udelay(1000);
		iter_max--;
	}

	/* 3b- this step occurs only if the remove is actually scanning
	 * we are waiting for the end of the frame which is a 1 on
	 * remvsyncsts (XSPAREREG<1>)
	 */
	if (iter_max) {
		iter_max = 300;
		while ((tmp & 0x2) && iter_max) {
			WREG8(DAC_INDEX, MGA1064_SPAREREG);
			tmp = RREG8(DAC_DATA);
			udelay(1000);
			iter_max--;
		}
	}
}

static void mga_g200wb_commit(struct drm_crtc *crtc)
{
	u8 tmp;
	struct mga_device *mdev = crtc->dev->dev_private;

	/* 1- The first step is to ensure that the vrsten and hrsten are set */
	WREG8(MGAREG_CRTCEXT_INDEX, 1);
	tmp = RREG8(MGAREG_CRTCEXT_DATA);
	WREG8(MGAREG_CRTCEXT_DATA, tmp | 0x88);

	/* 2- second step is to assert the rstlvl2 */
	WREG8(DAC_INDEX, MGA1064_REMHEADCTL2);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x8;
	WREG8(DAC_DATA, tmp);

	/* wait 10 us */
	udelay(10);

	/* 3- deassert rstlvl2 */
	tmp &= ~0x08;
	WREG8(DAC_INDEX, MGA1064_REMHEADCTL2);
	WREG8(DAC_DATA, tmp);

	/* 4- remove mask of scan request */
	WREG8(DAC_INDEX, MGA1064_SPAREREG);
	tmp = RREG8(DAC_DATA);
	tmp &= ~0x80;
	WREG8(DAC_DATA, tmp);

	/* 5- put back a 0 on the misc<0> line */
	WREG8(DAC_INDEX, MGA1064_GEN_IO_DATA);
	tmp = RREG8(DAC_DATA);
	tmp &= ~0x10;
	WREG_DAC(MGA1064_GEN_IO_DATA, tmp);
}

/*
   This is how the framebuffer base address is stored in g200 cards:
   * Assume @offset is the gpu_addr variable of the framebuffer object
   * Then addr is the number of _pixels_ (not bytes) from the start of
     VRAM to the first pixel we want to display. (divided by 2 for 32bit
     framebuffers)
   * addr is stored in the CRTCEXT0, CRTCC and CRTCD registers
   addr<20> -> CRTCEXT0<6>
   addr<19-16> -> CRTCEXT0<3-0>
   addr<15-8> -> CRTCC<7-0>
   addr<7-0> -> CRTCD<7-0>
   CRTCEXT0 has to be programmed last to trigger an update and make the
   new addr variable take effect.
 */
void mga_set_start_address(struct drm_crtc *crtc, unsigned offset)
{
	struct mga_device *mdev = crtc->dev->dev_private;
	u32 addr;
	int count;
	u8 crtcext0;

	while (RREG8(0x1fda) & 0x08);
	while (!(RREG8(0x1fda) & 0x08));

	count = RREG8(MGAREG_VCOUNT) + 2;
	while (RREG8(MGAREG_VCOUNT) < count);

	WREG8(MGAREG_CRTCEXT_INDEX, 0);
	crtcext0 = RREG8(MGAREG_CRTCEXT_DATA);
	crtcext0 &= 0xB0;
	addr = offset / 8;
	/* Can't store addresses any higher than that...
	   but we also don't have more than 16MB of memory, so it should be fine. */
	WARN_ON(addr > 0x1fffff);
	crtcext0 |= (!!(addr & (1<<20)))<<6;
	WREG_CRT(0x0d, (u8)(addr & 0xff));
	WREG_CRT(0x0c, (u8)(addr >> 8) & 0xff);
	WREG_ECRT(0x0, ((u8)(addr >> 16) & 0xf) | crtcext0);
}


/* ast is different - we will force move buffers out of VRAM */
static int mga_crtc_do_set_base(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				int x, int y, int atomic)
{
	struct mga_device *mdev = crtc->dev->dev_private;
	struct drm_gem_object *obj;
	struct mga_framebuffer *mga_fb;
	struct mgag200_bo *bo;
	int ret;
	u64 gpu_addr;

	/* push the previous fb to system ram */
	if (!atomic && fb) {
		mga_fb = to_mga_framebuffer(fb);
		obj = mga_fb->obj;
		bo = gem_to_mga_bo(obj);
		ret = mgag200_bo_reserve(bo, false);
		if (ret)
			return ret;
		mgag200_bo_push_sysram(bo);
		mgag200_bo_unreserve(bo);
	}

	mga_fb = to_mga_framebuffer(crtc->fb);
	obj = mga_fb->obj;
	bo = gem_to_mga_bo(obj);

	ret = mgag200_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = mgag200_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
	if (ret) {
		mgag200_bo_unreserve(bo);
		return ret;
	}

	if (&mdev->mfbdev->mfb == mga_fb) {
		/* if pushing console in kmap it */
		ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
		if (ret)
			DRM_ERROR("failed to kmap fbcon\n");

	}
	mgag200_bo_unreserve(bo);

	mga_set_start_address(crtc, (u32)gpu_addr);

	return 0;
}

static int mga_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
				  struct drm_framebuffer *old_fb)
{
	return mga_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

static int mga_crtc_mode_set(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode,
				int x, int y, struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = dev->dev_private;
	int hdisplay, hsyncstart, hsyncend, htotal;
	int vdisplay, vsyncstart, vsyncend, vtotal;
	int pitch;
	int option = 0, option2 = 0;
	int i;
	unsigned char misc = 0;
	unsigned char ext_vga[6];
	u8 bppshift;

	static unsigned char dacvalue[] = {
		/* 0x00: */        0,    0,    0,    0,    0,    0, 0x00,    0,
		/* 0x08: */        0,    0,    0,    0,    0,    0,    0,    0,
		/* 0x10: */        0,    0,    0,    0,    0,    0,    0,    0,
		/* 0x18: */     0x00,    0, 0xC9, 0xFF, 0xBF, 0x20, 0x1F, 0x20,
		/* 0x20: */     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* 0x28: */     0x00, 0x00, 0x00, 0x00,    0,    0,    0, 0x40,
		/* 0x30: */     0x00, 0xB0, 0x00, 0xC2, 0x34, 0x14, 0x02, 0x83,
		/* 0x38: */     0x00, 0x93, 0x00, 0x77, 0x00, 0x00, 0x00, 0x3A,
		/* 0x40: */        0,    0,    0,    0,    0,    0,    0,    0,
		/* 0x48: */        0,    0,    0,    0,    0,    0,    0,    0
	};

	bppshift = mdev->bpp_shifts[(crtc->fb->bits_per_pixel >> 3) - 1];

	switch (mdev->type) {
	case G200_SE_A:
	case G200_SE_B:
		dacvalue[MGA1064_VREF_CTL] = 0x03;
		dacvalue[MGA1064_PIX_CLK_CTL] = MGA1064_PIX_CLK_CTL_SEL_PLL;
		dacvalue[MGA1064_MISC_CTL] = MGA1064_MISC_CTL_DAC_EN |
					     MGA1064_MISC_CTL_VGA8 |
					     MGA1064_MISC_CTL_DAC_RAM_CS;
		if (mdev->has_sdram)
			option = 0x40049120;
		else
			option = 0x4004d120;
		option2 = 0x00008000;
		break;
	case G200_WB:
		dacvalue[MGA1064_VREF_CTL] = 0x07;
		option = 0x41049120;
		option2 = 0x0000b000;
		break;
	case G200_EV:
		dacvalue[MGA1064_PIX_CLK_CTL] = MGA1064_PIX_CLK_CTL_SEL_PLL;
		dacvalue[MGA1064_MISC_CTL] = MGA1064_MISC_CTL_VGA8 |
					     MGA1064_MISC_CTL_DAC_RAM_CS;
		option = 0x00000120;
		option2 = 0x0000b000;
		break;
	case G200_EH:
		dacvalue[MGA1064_MISC_CTL] = MGA1064_MISC_CTL_VGA8 |
					     MGA1064_MISC_CTL_DAC_RAM_CS;
		option = 0x00000120;
		option2 = 0x0000b000;
		break;
	case G200_ER:
		break;
	}

	switch (crtc->fb->bits_per_pixel) {
	case 8:
		dacvalue[MGA1064_MUL_CTL] = MGA1064_MUL_CTL_8bits;
		break;
	case 16:
		if (crtc->fb->depth == 15)
			dacvalue[MGA1064_MUL_CTL] = MGA1064_MUL_CTL_15bits;
		else
			dacvalue[MGA1064_MUL_CTL] = MGA1064_MUL_CTL_16bits;
		break;
	case 24:
		dacvalue[MGA1064_MUL_CTL] = MGA1064_MUL_CTL_24bits;
		break;
	case 32:
		dacvalue[MGA1064_MUL_CTL] = MGA1064_MUL_CTL_32_24bits;
		break;
	}

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		misc |= 0x40;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		misc |= 0x80;


	for (i = 0; i < sizeof(dacvalue); i++) {
		if ((i <= 0x17) ||
		    (i == 0x1b) ||
		    (i == 0x1c) ||
		    ((i >= 0x1f) && (i <= 0x29)) ||
		    ((i >= 0x30) && (i <= 0x37)))
			continue;
		if (IS_G200_SE(mdev) &&
		    ((i == 0x2c) || (i == 0x2d) || (i == 0x2e)))
			continue;
		if ((mdev->type == G200_EV || mdev->type == G200_WB || mdev->type == G200_EH) &&
		    (i >= 0x44) && (i <= 0x4e))
			continue;

		WREG_DAC(i, dacvalue[i]);
	}

	if (mdev->type == G200_ER)
		WREG_DAC(0x90, 0);

	if (option)
		pci_write_config_dword(dev->pdev, PCI_MGA_OPTION, option);
	if (option2)
		pci_write_config_dword(dev->pdev, PCI_MGA_OPTION2, option2);

	WREG_SEQ(2, 0xf);
	WREG_SEQ(3, 0);
	WREG_SEQ(4, 0xe);

	pitch = crtc->fb->pitches[0] / (crtc->fb->bits_per_pixel / 8);
	if (crtc->fb->bits_per_pixel == 24)
		pitch = (pitch * 3) >> (4 - bppshift);
	else
		pitch = pitch >> (4 - bppshift);

	hdisplay = mode->hdisplay / 8 - 1;
	hsyncstart = mode->hsync_start / 8 - 1;
	hsyncend = mode->hsync_end / 8 - 1;
	htotal = mode->htotal / 8 - 1;

	/* Work around hardware quirk */
	if ((htotal & 0x07) == 0x06 || (htotal & 0x07) == 0x04)
		htotal++;

	vdisplay = mode->vdisplay - 1;
	vsyncstart = mode->vsync_start - 1;
	vsyncend = mode->vsync_end - 1;
	vtotal = mode->vtotal - 2;

	WREG_GFX(0, 0);
	WREG_GFX(1, 0);
	WREG_GFX(2, 0);
	WREG_GFX(3, 0);
	WREG_GFX(4, 0);
	WREG_GFX(5, 0x40);
	WREG_GFX(6, 0x5);
	WREG_GFX(7, 0xf);
	WREG_GFX(8, 0xf);

	WREG_CRT(0, htotal - 4);
	WREG_CRT(1, hdisplay);
	WREG_CRT(2, hdisplay);
	WREG_CRT(3, (htotal & 0x1F) | 0x80);
	WREG_CRT(4, hsyncstart);
	WREG_CRT(5, ((htotal & 0x20) << 2) | (hsyncend & 0x1F));
	WREG_CRT(6, vtotal & 0xFF);
	WREG_CRT(7, ((vtotal & 0x100) >> 8) |
		 ((vdisplay & 0x100) >> 7) |
		 ((vsyncstart & 0x100) >> 6) |
		 ((vdisplay & 0x100) >> 5) |
		 ((vdisplay & 0x100) >> 4) | /* linecomp */
		 ((vtotal & 0x200) >> 4)|
		 ((vdisplay & 0x200) >> 3) |
		 ((vsyncstart & 0x200) >> 2));
	WREG_CRT(9, ((vdisplay & 0x200) >> 4) |
		 ((vdisplay & 0x200) >> 3));
	WREG_CRT(10, 0);
	WREG_CRT(11, 0);
	WREG_CRT(12, 0);
	WREG_CRT(13, 0);
	WREG_CRT(14, 0);
	WREG_CRT(15, 0);
	WREG_CRT(16, vsyncstart & 0xFF);
	WREG_CRT(17, (vsyncend & 0x0F) | 0x20);
	WREG_CRT(18, vdisplay & 0xFF);
	WREG_CRT(19, pitch & 0xFF);
	WREG_CRT(20, 0);
	WREG_CRT(21, vdisplay & 0xFF);
	WREG_CRT(22, (vtotal + 1) & 0xFF);
	WREG_CRT(23, 0xc3);
	WREG_CRT(24, vdisplay & 0xFF);

	ext_vga[0] = 0;
	ext_vga[5] = 0;

	/* TODO interlace */

	ext_vga[0] |= (pitch & 0x300) >> 4;
	ext_vga[1] = (((htotal - 4) & 0x100) >> 8) |
		((hdisplay & 0x100) >> 7) |
		((hsyncstart & 0x100) >> 6) |
		(htotal & 0x40);
	ext_vga[2] = ((vtotal & 0xc00) >> 10) |
		((vdisplay & 0x400) >> 8) |
		((vdisplay & 0xc00) >> 7) |
		((vsyncstart & 0xc00) >> 5) |
		((vdisplay & 0x400) >> 3);
	if (crtc->fb->bits_per_pixel == 24)
		ext_vga[3] = (((1 << bppshift) * 3) - 1) | 0x80;
	else
		ext_vga[3] = ((1 << bppshift) - 1) | 0x80;
	ext_vga[4] = 0;
	if (mdev->type == G200_WB)
		ext_vga[1] |= 0x88;

	/* Set pixel clocks */
	misc = 0x2d;
	WREG8(MGA_MISC_OUT, misc);

	mga_crtc_set_plls(mdev, mode->clock);

	for (i = 0; i < 6; i++) {
		WREG_ECRT(i, ext_vga[i]);
	}

	if (mdev->type == G200_ER)
		WREG_ECRT(0x24, 0x5);

	if (mdev->type == G200_EV) {
		WREG_ECRT(6, 0);
	}

	WREG_ECRT(0, ext_vga[0]);
	/* Enable mga pixel clock */
	misc = 0x2d;

	WREG8(MGA_MISC_OUT, misc);

	if (adjusted_mode)
		memcpy(&mdev->mode, mode, sizeof(struct drm_display_mode));

	mga_crtc_do_set_base(crtc, old_fb, x, y, 0);

	/* reset tagfifo */
	if (mdev->type == G200_ER) {
		u32 mem_ctl = RREG32(MGAREG_MEMCTL);
		u8 seq1;

		/* screen off */
		WREG8(MGAREG_SEQ_INDEX, 0x01);
		seq1 = RREG8(MGAREG_SEQ_DATA) | 0x20;
		WREG8(MGAREG_SEQ_DATA, seq1);

		WREG32(MGAREG_MEMCTL, mem_ctl | 0x00200000);
		udelay(1000);
		WREG32(MGAREG_MEMCTL, mem_ctl & ~0x00200000);

		WREG8(MGAREG_SEQ_DATA, seq1 & ~0x20);
	}


	if (IS_G200_SE(mdev)) {
		if (mdev->unique_rev_id >= 0x02) {
			u8 hi_pri_lvl;
			u32 bpp;
			u32 mb;

			if (crtc->fb->bits_per_pixel > 16)
				bpp = 32;
			else if (crtc->fb->bits_per_pixel > 8)
				bpp = 16;
			else
				bpp = 8;

			mb = (mode->clock * bpp) / 1000;
			if (mb > 3100)
				hi_pri_lvl = 0;
			else if (mb > 2600)
				hi_pri_lvl = 1;
			else if (mb > 1900)
				hi_pri_lvl = 2;
			else if (mb > 1160)
				hi_pri_lvl = 3;
			else if (mb > 440)
				hi_pri_lvl = 4;
			else
				hi_pri_lvl = 5;

			WREG8(MGAREG_CRTCEXT_INDEX, 0x06);
			WREG8(MGAREG_CRTCEXT_DATA, hi_pri_lvl);
		} else {
			WREG8(MGAREG_CRTCEXT_INDEX, 0x06);
			if (mdev->unique_rev_id >= 0x01)
				WREG8(MGAREG_CRTCEXT_DATA, 0x03);
			else
				WREG8(MGAREG_CRTCEXT_DATA, 0x04);
		}
	}
	return 0;
}

#if 0 /* code from mjg to attempt D3 on crtc dpms off - revisit later */
static int mga_suspend(struct drm_crtc *crtc)
{
	struct mga_crtc *mga_crtc = to_mga_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = dev->dev_private;
	struct pci_dev *pdev = dev->pdev;
	int option;

	if (mdev->suspended)
		return 0;

	WREG_SEQ(1, 0x20);
	WREG_ECRT(1, 0x30);
	/* Disable the pixel clock */
	WREG_DAC(0x1a, 0x05);
	/* Power down the DAC */
	WREG_DAC(0x1e, 0x18);
	/* Power down the pixel PLL */
	WREG_DAC(0x1a, 0x0d);

	/* Disable PLLs and clocks */
	pci_read_config_dword(pdev, PCI_MGA_OPTION, &option);
	option &= ~(0x1F8024);
	pci_write_config_dword(pdev, PCI_MGA_OPTION, option);
	pci_set_power_state(pdev, PCI_D3hot);
	pci_disable_device(pdev);

	mdev->suspended = true;

	return 0;
}

static int mga_resume(struct drm_crtc *crtc)
{
	struct mga_crtc *mga_crtc = to_mga_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = dev->dev_private;
	struct pci_dev *pdev = dev->pdev;
	int option;

	if (!mdev->suspended)
		return 0;

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_device(pdev);

	/* Disable sysclk */
	pci_read_config_dword(pdev, PCI_MGA_OPTION, &option);
	option &= ~(0x4);
	pci_write_config_dword(pdev, PCI_MGA_OPTION, option);

	mdev->suspended = false;

	return 0;
}

#endif

static void mga_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = dev->dev_private;
	u8 seq1 = 0, crtcext1 = 0;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		seq1 = 0;
		crtcext1 = 0;
		mga_crtc_load_lut(crtc);
		break;
	case DRM_MODE_DPMS_STANDBY:
		seq1 = 0x20;
		crtcext1 = 0x10;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		seq1 = 0x20;
		crtcext1 = 0x20;
		break;
	case DRM_MODE_DPMS_OFF:
		seq1 = 0x20;
		crtcext1 = 0x30;
		break;
	}

#if 0
	if (mode == DRM_MODE_DPMS_OFF) {
		mga_suspend(crtc);
	}
#endif
	WREG8(MGAREG_SEQ_INDEX, 0x01);
	seq1 |= RREG8(MGAREG_SEQ_DATA) & ~0x20;
	mga_wait_vsync(mdev);
	mga_wait_busy(mdev);
	WREG8(MGAREG_SEQ_DATA, seq1);
	msleep(20);
	WREG8(MGAREG_CRTCEXT_INDEX, 0x01);
	crtcext1 |= RREG8(MGAREG_CRTCEXT_DATA) & ~0x30;
	WREG8(MGAREG_CRTCEXT_DATA, crtcext1);

#if 0
	if (mode == DRM_MODE_DPMS_ON && mdev->suspended == true) {
		mga_resume(crtc);
		drm_helper_resume_force_mode(dev);
	}
#endif
}

/*
 * This is called before a mode is programmed. A typical use might be to
 * enable DPMS during the programming to avoid seeing intermediate stages,
 * but that's not relevant to us
 */
static void mga_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = dev->dev_private;
	u8 tmp;

	/*	mga_resume(crtc);*/

	WREG8(MGAREG_CRTC_INDEX, 0x11);
	tmp = RREG8(MGAREG_CRTC_DATA);
	WREG_CRT(0x11, tmp | 0x80);

	if (mdev->type == G200_SE_A || mdev->type == G200_SE_B) {
		WREG_SEQ(0, 1);
		msleep(50);
		WREG_SEQ(1, 0x20);
		msleep(20);
	} else {
		WREG8(MGAREG_SEQ_INDEX, 0x1);
		tmp = RREG8(MGAREG_SEQ_DATA);

		/* start sync reset */
		WREG_SEQ(0, 1);
		WREG_SEQ(1, tmp | 0x20);
	}

	if (mdev->type == G200_WB)
		mga_g200wb_prepare(crtc);

	WREG_CRT(17, 0);
}

/*
 * This is called after a mode is programmed. It should reverse anything done
 * by the prepare function
 */
static void mga_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = dev->dev_private;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	u8 tmp;

	if (mdev->type == G200_WB)
		mga_g200wb_commit(crtc);

	if (mdev->type == G200_SE_A || mdev->type == G200_SE_B) {
		msleep(50);
		WREG_SEQ(1, 0x0);
		msleep(20);
		WREG_SEQ(0, 0x3);
	} else {
		WREG8(MGAREG_SEQ_INDEX, 0x1);
		tmp = RREG8(MGAREG_SEQ_DATA);

		tmp &= ~0x20;
		WREG_SEQ(0x1, tmp);
		WREG_SEQ(0, 3);
	}
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
}

/*
 * The core can pass us a set of gamma values to program. We actually only
 * use this for 8-bit mode so can't perform smooth fades on deeper modes,
 * but it's a requirement that we provide the function
 */
static void mga_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				  u16 *blue, uint32_t start, uint32_t size)
{
	struct mga_crtc *mga_crtc = to_mga_crtc(crtc);
	int end = (start + size > MGAG200_LUT_SIZE) ? MGAG200_LUT_SIZE : start + size;
	int i;

	for (i = start; i < end; i++) {
		mga_crtc->lut_r[i] = red[i] >> 8;
		mga_crtc->lut_g[i] = green[i] >> 8;
		mga_crtc->lut_b[i] = blue[i] >> 8;
	}
	mga_crtc_load_lut(crtc);
}

/* Simple cleanup function */
static void mga_crtc_destroy(struct drm_crtc *crtc)
{
	struct mga_crtc *mga_crtc = to_mga_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(mga_crtc);
}

static void mga_crtc_disable(struct drm_crtc *crtc)
{
	int ret;
	DRM_DEBUG_KMS("\n");
	mga_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	if (crtc->fb) {
		struct mga_framebuffer *mga_fb = to_mga_framebuffer(crtc->fb);
		struct drm_gem_object *obj = mga_fb->obj;
		struct mgag200_bo *bo = gem_to_mga_bo(obj);
		ret = mgag200_bo_reserve(bo, false);
		if (ret)
			return;
		mgag200_bo_push_sysram(bo);
		mgag200_bo_unreserve(bo);
	}
	crtc->fb = NULL;
}

/* These provide the minimum set of functions required to handle a CRTC */
static const struct drm_crtc_funcs mga_crtc_funcs = {
	.cursor_set = mga_crtc_cursor_set,
	.cursor_move = mga_crtc_cursor_move,
	.gamma_set = mga_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = mga_crtc_destroy,
};

static const struct drm_crtc_helper_funcs mga_helper_funcs = {
	.disable = mga_crtc_disable,
	.dpms = mga_crtc_dpms,
	.mode_fixup = mga_crtc_mode_fixup,
	.mode_set = mga_crtc_mode_set,
	.mode_set_base = mga_crtc_mode_set_base,
	.prepare = mga_crtc_prepare,
	.commit = mga_crtc_commit,
	.load_lut = mga_crtc_load_lut,
};

/* CRTC setup */
static void mga_crtc_init(struct mga_device *mdev)
{
	struct mga_crtc *mga_crtc;
	int i;

	mga_crtc = kzalloc(sizeof(struct mga_crtc) +
			      (MGAG200FB_CONN_LIMIT * sizeof(struct drm_connector *)),
			      GFP_KERNEL);

	if (mga_crtc == NULL)
		return;

	drm_crtc_init(mdev->dev, &mga_crtc->base, &mga_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&mga_crtc->base, MGAG200_LUT_SIZE);
	mdev->mode_info.crtc = mga_crtc;

	for (i = 0; i < MGAG200_LUT_SIZE; i++) {
		mga_crtc->lut_r[i] = i;
		mga_crtc->lut_g[i] = i;
		mga_crtc->lut_b[i] = i;
	}

	drm_crtc_helper_add(&mga_crtc->base, &mga_helper_funcs);
}

/** Sets the color ramps on behalf of fbcon */
void mga_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			      u16 blue, int regno)
{
	struct mga_crtc *mga_crtc = to_mga_crtc(crtc);

	mga_crtc->lut_r[regno] = red >> 8;
	mga_crtc->lut_g[regno] = green >> 8;
	mga_crtc->lut_b[regno] = blue >> 8;
}

/** Gets the color ramps on behalf of fbcon */
void mga_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			      u16 *blue, int regno)
{
	struct mga_crtc *mga_crtc = to_mga_crtc(crtc);

	*red = (u16)mga_crtc->lut_r[regno] << 8;
	*green = (u16)mga_crtc->lut_g[regno] << 8;
	*blue = (u16)mga_crtc->lut_b[regno] << 8;
}

/*
 * The encoder comes after the CRTC in the output pipeline, but before
 * the connector. It's responsible for ensuring that the digital
 * stream is appropriately converted into the output format. Setup is
 * very simple in this case - all we have to do is inform qemu of the
 * colour depth in order to ensure that it displays appropriately
 */

/*
 * These functions are analagous to those in the CRTC code, but are intended
 * to handle any encoder-specific limitations
 */
static bool mga_encoder_mode_fixup(struct drm_encoder *encoder,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mga_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{

}

static void mga_encoder_dpms(struct drm_encoder *encoder, int state)
{
	return;
}

static void mga_encoder_prepare(struct drm_encoder *encoder)
{
}

static void mga_encoder_commit(struct drm_encoder *encoder)
{
}

void mga_encoder_destroy(struct drm_encoder *encoder)
{
	struct mga_encoder *mga_encoder = to_mga_encoder(encoder);
	drm_encoder_cleanup(encoder);
	kfree(mga_encoder);
}

static const struct drm_encoder_helper_funcs mga_encoder_helper_funcs = {
	.dpms = mga_encoder_dpms,
	.mode_fixup = mga_encoder_mode_fixup,
	.mode_set = mga_encoder_mode_set,
	.prepare = mga_encoder_prepare,
	.commit = mga_encoder_commit,
};

static const struct drm_encoder_funcs mga_encoder_encoder_funcs = {
	.destroy = mga_encoder_destroy,
};

static struct drm_encoder *mga_encoder_init(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	struct mga_encoder *mga_encoder;

	mga_encoder = kzalloc(sizeof(struct mga_encoder), GFP_KERNEL);
	if (!mga_encoder)
		return NULL;

	encoder = &mga_encoder->base;
	encoder->possible_crtcs = 0x1;

	drm_encoder_init(dev, encoder, &mga_encoder_encoder_funcs,
			 DRM_MODE_ENCODER_DAC);
	drm_encoder_helper_add(encoder, &mga_encoder_helper_funcs);

	return encoder;
}


static int mga_vga_get_modes(struct drm_connector *connector)
{
	struct mga_connector *mga_connector = to_mga_connector(connector);
	struct edid *edid;
	int ret = 0;

	edid = drm_get_edid(connector, &mga_connector->i2c->adapter);
	if (edid) {
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}
	return ret;
}

static uint32_t mga_vga_calculate_mode_bandwidth(struct drm_display_mode *mode,
							int bits_per_pixel)
{
	uint32_t total_area, divisor;
	int64_t active_area, pixels_per_second, bandwidth;
	uint64_t bytes_per_pixel = (bits_per_pixel + 7) / 8;

	divisor = 1024;

	if (!mode->htotal || !mode->vtotal || !mode->clock)
		return 0;

	active_area = mode->hdisplay * mode->vdisplay;
	total_area = mode->htotal * mode->vtotal;

	pixels_per_second = active_area * mode->clock * 1000;
	do_div(pixels_per_second, total_area);

	bandwidth = pixels_per_second * bytes_per_pixel * 100;
	do_div(bandwidth, divisor);

	return (uint32_t)(bandwidth);
}

#define MODE_BANDWIDTH	MODE_BAD

static int mga_vga_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct mga_device *mdev = (struct mga_device*)dev->dev_private;
	struct mga_fbdev *mfbdev = mdev->mfbdev;
	struct drm_fb_helper *fb_helper = &mfbdev->helper;
	struct drm_fb_helper_connector *fb_helper_conn = NULL;
	int bpp = 32;
	int i = 0;

	if (IS_G200_SE(mdev)) {
		if (mdev->unique_rev_id == 0x01) {
			if (mode->hdisplay > 1600)
				return MODE_VIRTUAL_X;
			if (mode->vdisplay > 1200)
				return MODE_VIRTUAL_Y;
			if (mga_vga_calculate_mode_bandwidth(mode, bpp)
				> (24400 * 1024))
				return MODE_BANDWIDTH;
		} else if (mdev->unique_rev_id >= 0x02) {
			if (mode->hdisplay > 1920)
				return MODE_VIRTUAL_X;
			if (mode->vdisplay > 1200)
				return MODE_VIRTUAL_Y;
			if (mga_vga_calculate_mode_bandwidth(mode, bpp)
				> (30100 * 1024))
				return MODE_BANDWIDTH;
		}
	} else if (mdev->type == G200_WB) {
		if (mode->hdisplay > 1280)
			return MODE_VIRTUAL_X;
		if (mode->vdisplay > 1024)
			return MODE_VIRTUAL_Y;
		if (mga_vga_calculate_mode_bandwidth(mode,
			bpp > (31877 * 1024)))
			return MODE_BANDWIDTH;
	} else if (mdev->type == G200_EV &&
		(mga_vga_calculate_mode_bandwidth(mode, bpp)
			> (32700 * 1024))) {
		return MODE_BANDWIDTH;
	} else if (mode->type == G200_EH &&
		(mga_vga_calculate_mode_bandwidth(mode, bpp)
			> (37500 * 1024))) {
		return MODE_BANDWIDTH;
	} else if (mode->type == G200_ER &&
		(mga_vga_calculate_mode_bandwidth(mode,
			bpp) > (55000 * 1024))) {
		return MODE_BANDWIDTH;
	}

	if (mode->crtc_hdisplay > 2048 || mode->crtc_hsync_start > 4096 ||
	    mode->crtc_hsync_end > 4096 || mode->crtc_htotal > 4096 ||
	    mode->crtc_vdisplay > 2048 || mode->crtc_vsync_start > 4096 ||
	    mode->crtc_vsync_end > 4096 || mode->crtc_vtotal > 4096) {
		return MODE_BAD;
	}

	/* Validate the mode input by the user */
	for (i = 0; i < fb_helper->connector_count; i++) {
		if (fb_helper->connector_info[i]->connector == connector) {
			/* Found the helper for this connector */
			fb_helper_conn = fb_helper->connector_info[i];
			if (fb_helper_conn->cmdline_mode.specified) {
				if (fb_helper_conn->cmdline_mode.bpp_specified) {
					bpp = fb_helper_conn->cmdline_mode.bpp;
				}
			}
		}
	}

	if ((mode->hdisplay * mode->vdisplay * (bpp/8)) > mdev->mc.vram_size) {
		if (fb_helper_conn)
			fb_helper_conn->cmdline_mode.specified = false;
		return MODE_BAD;
	}

	return MODE_OK;
}

struct drm_encoder *mga_connector_best_encoder(struct drm_connector
						  *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	/* pick the encoder ids */
	if (enc_id) {
		obj =
		    drm_mode_object_find(connector->dev, enc_id,
					 DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			return NULL;
		encoder = obj_to_encoder(obj);
		return encoder;
	}
	return NULL;
}

static enum drm_connector_status mga_vga_detect(struct drm_connector
						   *connector, bool force)
{
	return connector_status_connected;
}

static void mga_connector_destroy(struct drm_connector *connector)
{
	struct mga_connector *mga_connector = to_mga_connector(connector);
	mgag200_i2c_destroy(mga_connector->i2c);
	drm_connector_cleanup(connector);
	kfree(connector);
}

struct drm_connector_helper_funcs mga_vga_connector_helper_funcs = {
	.get_modes = mga_vga_get_modes,
	.mode_valid = mga_vga_mode_valid,
	.best_encoder = mga_connector_best_encoder,
};

struct drm_connector_funcs mga_vga_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = mga_vga_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = mga_connector_destroy,
};

static struct drm_connector *mga_vga_init(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct mga_connector *mga_connector;

	mga_connector = kzalloc(sizeof(struct mga_connector), GFP_KERNEL);
	if (!mga_connector)
		return NULL;

	connector = &mga_connector->base;

	drm_connector_init(dev, connector,
			   &mga_vga_connector_funcs, DRM_MODE_CONNECTOR_VGA);

	drm_connector_helper_add(connector, &mga_vga_connector_helper_funcs);

	drm_sysfs_connector_add(connector);

	mga_connector->i2c = mgag200_i2c_create(dev);
	if (!mga_connector->i2c)
		DRM_ERROR("failed to add ddc bus\n");

	return connector;
}


int mgag200_modeset_init(struct mga_device *mdev)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	mdev->mode_info.mode_config_initialized = true;

	mdev->dev->mode_config.max_width = MGAG200_MAX_FB_WIDTH;
	mdev->dev->mode_config.max_height = MGAG200_MAX_FB_HEIGHT;

	mdev->dev->mode_config.fb_base = mdev->mc.vram_base;

	mga_crtc_init(mdev);

	encoder = mga_encoder_init(mdev->dev);
	if (!encoder) {
		DRM_ERROR("mga_encoder_init failed\n");
		return -1;
	}

	connector = mga_vga_init(mdev->dev);
	if (!connector) {
		DRM_ERROR("mga_vga_init failed\n");
		return -1;
	}

	drm_mode_connector_attach_encoder(connector, encoder);

	ret = mgag200_fbdev_init(mdev);
	if (ret) {
		DRM_ERROR("mga_fbdev_init failed\n");
		return ret;
	}

	return 0;
}

void mgag200_modeset_fini(struct mga_device *mdev)
{

}
