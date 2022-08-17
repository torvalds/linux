// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>

#include "mgag200_drv.h"

/*
 * G200
 */

static int mgag200_pixpll_compute_g200(struct mgag200_pll *pixpll, long clock,
				       struct mgag200_pll_values *pixpllc)
{
	struct mga_device *mdev = pixpll->mdev;
	struct drm_device *dev = &mdev->base;
	struct mgag200_g200_device *g200 = to_mgag200_g200_device(dev);
	const int post_div_max = 7;
	const int in_div_min = 1;
	const int in_div_max = 6;
	const int feed_div_min = 7;
	const int feed_div_max = 127;
	u8 testp, testm, testn;
	u8 n = 0, m = 0, p, s;
	long f_vco;
	long computed;
	long delta, tmp_delta;
	long ref_clk = g200->ref_clk;
	long p_clk_min = g200->pclk_min;
	long p_clk_max = g200->pclk_max;

	if (clock > p_clk_max) {
		drm_err(dev, "Pixel Clock %ld too high\n", clock);
		return -EINVAL;
	}

	if (clock < p_clk_min >> 3)
		clock = p_clk_min >> 3;

	f_vco = clock;
	for (testp = 0;
	     testp <= post_div_max && f_vco < p_clk_min;
	     testp = (testp << 1) + 1, f_vco <<= 1)
		;
	p = testp + 1;

	delta = clock;

	for (testm = in_div_min; testm <= in_div_max; testm++) {
		for (testn = feed_div_min; testn <= feed_div_max; testn++) {
			computed = ref_clk * (testn + 1) / (testm + 1);
			if (computed < f_vco)
				tmp_delta = f_vco - computed;
			else
				tmp_delta = computed - f_vco;
			if (tmp_delta < delta) {
				delta = tmp_delta;
				m = testm + 1;
				n = testn + 1;
			}
		}
	}
	f_vco = ref_clk * n / m;
	if (f_vco < 100000)
		s = 0;
	else if (f_vco < 140000)
		s = 1;
	else if (f_vco < 180000)
		s = 2;
	else
		s = 3;

	drm_dbg_kms(dev, "clock: %ld vco: %ld m: %d n: %d p: %d s: %d\n",
		    clock, f_vco, m, n, p, s);

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void
mgag200_pixpll_update_g200(struct mgag200_pll *pixpll, const struct mgag200_pll_values *pixpllc)
{
	struct mga_device *mdev = pixpll->mdev;
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	WREG_DAC(MGA1064_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_PIX_PLLC_P, xpixpllcp);
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200 = {
	.compute = mgag200_pixpll_compute_g200,
	.update = mgag200_pixpll_update_g200,
};

/*
 * G200SE
 */

static int mgag200_pixpll_compute_g200se_00(struct mgag200_pll *pixpll, long clock,
					    struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 320000;
	static const unsigned int vcomin = 160000;
	static const unsigned int pllreffreq = 25000;

	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;
	permitteddelta = clock * 5 / 1000;

	for (testp = 8; testp > 0; testp /= 2) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testn = 17; testn < 256; testn++) {
			for (testm = 1; testm < 32; testm++) {
				computed = (pllreffreq * testn) / (testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					m = testm;
					n = testn;
					p = testp;
				}
			}
		}
	}

	if (delta > permitteddelta) {
		pr_warn("PLL delta too large\n");
		return -EINVAL;
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void mgag200_pixpll_update_g200se_00(struct mgag200_pll *pixpll,
					    const struct mgag200_pll_values *pixpllc)
{
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp;
	struct mga_device *mdev = pixpll->mdev;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = pixpllcm | ((pixpllcn & BIT(8)) >> 1);
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	WREG_DAC(MGA1064_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_PIX_PLLC_P, xpixpllcp);
}

static int mgag200_pixpll_compute_g200se_04(struct mgag200_pll *pixpll, long clock,
					    struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 1600000;
	static const unsigned int vcomin = 800000;
	static const unsigned int pllreffreq = 25000;
	static const unsigned int pvalues_e4[] = {16, 14, 12, 10, 8, 6, 4, 2, 1};

	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;
	unsigned int fvv;
	unsigned int i;

	m = n = p = s = 0;
	delta = 0xffffffff;

	if (clock < 25000)
		clock = 25000;
	clock = clock * 2;

	/* Permited delta is 0.5% as VESA Specification */
	permitteddelta = clock * 5 / 1000;

	for (i = 0 ; i < ARRAY_SIZE(pvalues_e4); i++) {
		testp = pvalues_e4[i];

		if ((clock * testp) > vcomax)
			continue;
		if ((clock * testp) < vcomin)
			continue;

		for (testn = 50; testn <= 256; testn++) {
			for (testm = 1; testm <= 32; testm++) {
				computed = (pllreffreq * testn) / (testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;

				if (tmpdelta < delta) {
					delta = tmpdelta;
					m = testm;
					n = testn;
					p = testp;
				}
			}
		}
	}

	fvv = pllreffreq * n / m;
	fvv = (fvv - 800000) / 50000;
	if (fvv > 15)
		fvv = 15;
	s = fvv << 1;

	if (delta > permitteddelta) {
		pr_warn("PLL delta too large\n");
		return -EINVAL;
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void mgag200_pixpll_update_g200se_04(struct mgag200_pll *pixpll,
					    const struct mgag200_pll_values *pixpllc)
{
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp;
	struct mga_device *mdev = pixpll->mdev;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = pixpllcm | ((pixpllcn & BIT(8)) >> 1);
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	WREG_DAC(MGA1064_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_PIX_PLLC_P, xpixpllcp);

	WREG_DAC(0x1a, 0x09);
	msleep(20);
	WREG_DAC(0x1a, 0x01);
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200se_00 = {
	.compute = mgag200_pixpll_compute_g200se_00,
	.update = mgag200_pixpll_update_g200se_00,
};

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200se_04 = {
	.compute = mgag200_pixpll_compute_g200se_04,
	.update = mgag200_pixpll_update_g200se_04,
};

/*
 * G200WB
 */

static int mgag200_pixpll_compute_g200wb(struct mgag200_pll *pixpll, long clock,
					 struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 550000;
	static const unsigned int vcomin = 150000;
	static const unsigned int pllreffreq = 48000;

	unsigned int delta, tmpdelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;

	for (testp = 1; testp < 9; testp++) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testm = 1; testm < 17; testm++) {
			for (testn = 1; testn < 151; testn++) {
				computed = (pllreffreq * testn) / (testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					n = testn;
					m = testm;
					p = testp;
					s = 0;
				}
			}
		}
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void
mgag200_pixpll_update_g200wb(struct mgag200_pll *pixpll, const struct mgag200_pll_values *pixpllc)
{
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp, tmp;
	int i, j, tmpcount, vcount;
	struct mga_device *mdev = pixpll->mdev;
	bool pll_locked = false;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = ((pixpllcn & BIT(8)) >> 1) | pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = ((pixpllcn & GENMASK(10, 9)) >> 3) | (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

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
		WREG_DAC(MGA1064_WB_PIX_PLLC_N, xpixpllcn);
		WREG_DAC(MGA1064_WB_PIX_PLLC_M, xpixpllcm);
		WREG_DAC(MGA1064_WB_PIX_PLLC_P, xpixpllcp);

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
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200wb = {
	.compute = mgag200_pixpll_compute_g200wb,
	.update = mgag200_pixpll_update_g200wb,
};

/*
 * G200EV
 */

static int mgag200_pixpll_compute_g200ev(struct mgag200_pll *pixpll, long clock,
					 struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 550000;
	static const unsigned int vcomin = 150000;
	static const unsigned int pllreffreq = 50000;

	unsigned int delta, tmpdelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;

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
					n = testn;
					m = testm;
					p = testp;
				}
			}
		}
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void
mgag200_pixpll_update_g200ev(struct mgag200_pll *pixpll, const struct mgag200_pll_values *pixpllc)
{
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp, tmp;
	struct mga_device *mdev = pixpll->mdev;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

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

	WREG_DAC(MGA1064_EV_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_EV_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_EV_PIX_PLLC_P, xpixpllcp);

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
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200ev = {
	.compute = mgag200_pixpll_compute_g200ev,
	.update = mgag200_pixpll_update_g200ev,
};

/*
 * G200EH
 */

static int mgag200_pixpll_compute_g200eh(struct mgag200_pll *pixpll, long clock,
					 struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 800000;
	static const unsigned int vcomin = 400000;
	static const unsigned int pllreffreq = 33333;

	unsigned int delta, tmpdelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;

	for (testp = 16; testp > 0; testp >>= 1) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testm = 1; testm < 33; testm++) {
			for (testn = 17; testn < 257; testn++) {
				computed = (pllreffreq * testn) / (testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					n = testn;
					m = testm;
					p = testp;
				}
			}
		}
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void
mgag200_pixpll_update_g200eh(struct mgag200_pll *pixpll, const struct mgag200_pll_values *pixpllc)
{
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp, tmp;
	int i, j, tmpcount, vcount;
	struct mga_device *mdev = pixpll->mdev;
	bool pll_locked = false;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = ((pixpllcn & BIT(8)) >> 1) | pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

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

		WREG_DAC(MGA1064_EH_PIX_PLLC_M, xpixpllcm);
		WREG_DAC(MGA1064_EH_PIX_PLLC_N, xpixpllcn);
		WREG_DAC(MGA1064_EH_PIX_PLLC_P, xpixpllcp);

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
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200eh = {
	.compute = mgag200_pixpll_compute_g200eh,
	.update = mgag200_pixpll_update_g200eh,
};

/*
 * G200EH3
 */

static int mgag200_pixpll_compute_g200eh3(struct mgag200_pll *pixpll, long clock,
					  struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 3000000;
	static const unsigned int vcomin = 1500000;
	static const unsigned int pllreffreq = 25000;

	unsigned int delta, tmpdelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;
	testp = 0;

	for (testm = 150; testm >= 6; testm--) {
		if (clock * testm > vcomax)
			continue;
		if (clock * testm < vcomin)
			continue;
		for (testn = 120; testn >= 60; testn--) {
			computed = (pllreffreq * testn) / testm;
			if (computed > clock)
				tmpdelta = computed - clock;
			else
				tmpdelta = clock - computed;
			if (tmpdelta < delta) {
				delta = tmpdelta;
				n = testn + 1;
				m = testm + 1;
				p = testp + 1;
			}
			if (delta == 0)
				break;
		}
		if (delta == 0)
			break;
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200eh3 = {
	.compute = mgag200_pixpll_compute_g200eh3,
	.update = mgag200_pixpll_update_g200eh, // same as G200EH
};

/*
 * G200ER
 */

static int mgag200_pixpll_compute_g200er(struct mgag200_pll *pixpll, long clock,
					 struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 1488000;
	static const unsigned int vcomin = 1056000;
	static const unsigned int pllreffreq = 48000;
	static const unsigned int m_div_val[] = { 1, 2, 4, 8 };

	unsigned int delta, tmpdelta;
	int testr, testn, testm, testo;
	unsigned int p, m, n, s;
	unsigned int computed, vco;

	m = n = p = s = 0;
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
						m = (testm | (testo << 3)) + 1;
						n = testn + 1;
						p = testr + 1;
						s = testr;
					}
				}
			}
		}
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void
mgag200_pixpll_update_g200er(struct mgag200_pll *pixpll, const struct mgag200_pll_values *pixpllc)
{
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp, tmp;
	struct mga_device *mdev = pixpll->mdev;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

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

	WREG_DAC(MGA1064_ER_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_ER_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_ER_PIX_PLLC_P, xpixpllcp);

	udelay(50);
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200er = {
	.compute = mgag200_pixpll_compute_g200er,
	.update = mgag200_pixpll_update_g200er,
};

/*
 * G200EW3
 */

static int mgag200_pixpll_compute_g200ew3(struct mgag200_pll *pixpll, long clock,
					  struct mgag200_pll_values *pixpllc)
{
	static const unsigned int vcomax = 800000;
	static const unsigned int vcomin = 400000;
	static const unsigned int pllreffreq = 25000;

	unsigned int delta, tmpdelta;
	unsigned int testp, testm, testn, testp2;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;

	for (testp = 1; testp < 8; testp++) {
		for (testp2 = 1; testp2 < 8; testp2++) {
			if (testp < testp2)
				continue;
			if ((clock * testp * testp2) > vcomax)
				continue;
			if ((clock * testp * testp2) < vcomin)
				continue;
			for (testm = 1; testm < 26; testm++) {
				for (testn = 32; testn < 2048 ; testn++) {
					computed = (pllreffreq * testn) / (testm * testp * testp2);
					if (computed > clock)
						tmpdelta = computed - clock;
					else
						tmpdelta = clock - computed;
					if (tmpdelta < delta) {
						delta = tmpdelta;
						m = testm + 1;
						n = testn + 1;
						p = testp + 1;
						s = testp2;
					}
				}
			}
		}
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static const struct mgag200_pll_funcs mgag200_pixpll_funcs_g200ew3 = {
	.compute = mgag200_pixpll_compute_g200ew3,
	.update = mgag200_pixpll_update_g200wb, // same as G200WB
};

/*
 * PLL initialization
 */

int mgag200_pixpll_init(struct mgag200_pll *pixpll, struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct mgag200_g200se_device *g200se;

	pixpll->mdev = mdev;

	switch (mdev->type) {
	case G200_PCI:
	case G200_AGP:
		pixpll->funcs = &mgag200_pixpll_funcs_g200;
		break;
	case G200_SE_A:
	case G200_SE_B:
		g200se = to_mgag200_g200se_device(dev);

		if (g200se->unique_rev_id >= 0x04)
			pixpll->funcs = &mgag200_pixpll_funcs_g200se_04;
		else
			pixpll->funcs = &mgag200_pixpll_funcs_g200se_00;
		break;
	case G200_WB:
		pixpll->funcs = &mgag200_pixpll_funcs_g200wb;
		break;
	case G200_EV:
		pixpll->funcs = &mgag200_pixpll_funcs_g200ev;
		break;
	case G200_EH:
		pixpll->funcs = &mgag200_pixpll_funcs_g200eh;
		break;
	case G200_EH3:
		pixpll->funcs = &mgag200_pixpll_funcs_g200eh3;
		break;
	case G200_ER:
		pixpll->funcs = &mgag200_pixpll_funcs_g200er;
		break;
	case G200_EW3:
		pixpll->funcs = &mgag200_pixpll_funcs_g200ew3;
		break;
	default:
		drm_err(dev, "unknown device type %d\n", mdev->type);
		return -ENODEV;
	}

	return 0;
}
