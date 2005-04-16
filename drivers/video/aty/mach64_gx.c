
/*
 *  ATI Mach64 GX Support
 */

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/sched.h>

#include <asm/io.h>

#include <video/mach64.h>
#include "atyfb.h"

/* Definitions for the ICS 2595 == ATI 18818_1 Clockchip */

#define REF_FREQ_2595       1432	/*  14.33 MHz  (exact   14.31818) */
#define REF_DIV_2595          46	/* really 43 on ICS 2595 !!!  */
				  /* ohne Prescaler */
#define MAX_FREQ_2595      15938	/* 159.38 MHz  (really 170.486) */
#define MIN_FREQ_2595       8000	/*  80.00 MHz  (        85.565) */
				  /* mit Prescaler 2, 4, 8 */
#define ABS_MIN_FREQ_2595   1000	/*  10.00 MHz  (really  10.697) */
#define N_ADJ_2595           257

#define STOP_BITS_2595     0x1800


#define MIN_N_408		2

#define MIN_N_1703		6

#define MIN_M		2
#define MAX_M		30
#define MIN_N		35
#define MAX_N		255-8


    /*
     *  Support Functions
     */

static void aty_dac_waste4(const struct atyfb_par *par)
{
	(void) aty_ld_8(DAC_REGS, par);

	(void) aty_ld_8(DAC_REGS + 2, par);
	(void) aty_ld_8(DAC_REGS + 2, par);
	(void) aty_ld_8(DAC_REGS + 2, par);
	(void) aty_ld_8(DAC_REGS + 2, par);
}

static void aty_StrobeClock(const struct atyfb_par *par)
{
	u8 tmp;

	udelay(26);

	tmp = aty_ld_8(CLOCK_CNTL, par);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, tmp | CLOCK_STROBE, par);
	return;
}


    /*
     *  IBM RGB514 DAC and Clock Chip
     */

static void aty_st_514(int offset, u8 val, const struct atyfb_par *par)
{
	aty_st_8(DAC_CNTL, 1, par);
	/* right addr byte */
	aty_st_8(DAC_W_INDEX, offset & 0xff, par);
	/* left addr byte */
	aty_st_8(DAC_DATA, (offset >> 8) & 0xff, par);
	aty_st_8(DAC_MASK, val, par);
	aty_st_8(DAC_CNTL, 0, par);
}

static int aty_set_dac_514(const struct fb_info *info,
			   const union aty_pll *pll, u32 bpp, u32 accel)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	static struct {
		u8 pixel_dly;
		u8 misc2_cntl;
		u8 pixel_rep;
		u8 pixel_cntl_index;
		u8 pixel_cntl_v1;
	} tab[3] = {
		{
		0, 0x41, 0x03, 0x71, 0x45},	/* 8 bpp */
		{
		0, 0x45, 0x04, 0x0c, 0x01},	/* 555 */
		{
		0, 0x45, 0x06, 0x0e, 0x00},	/* XRGB */
	};
	int i;

	switch (bpp) {
	case 8:
	default:
		i = 0;
		break;
	case 16:
		i = 1;
		break;
	case 32:
		i = 2;
		break;
	}
	aty_st_514(0x90, 0x00, par);	/* VRAM Mask Low */
	aty_st_514(0x04, tab[i].pixel_dly, par);	/* Horizontal Sync Control */
	aty_st_514(0x05, 0x00, par);	/* Power Management */
	aty_st_514(0x02, 0x01, par);	/* Misc Clock Control */
	aty_st_514(0x71, tab[i].misc2_cntl, par);	/* Misc Control 2 */
	aty_st_514(0x0a, tab[i].pixel_rep, par);	/* Pixel Format */
	aty_st_514(tab[i].pixel_cntl_index, tab[i].pixel_cntl_v1, par);
	/* Misc Control 2 / 16 BPP Control / 32 BPP Control */
	return 0;
}

static int aty_var_to_pll_514(const struct fb_info *info, u32 vclk_per,
			      u32 bpp, union aty_pll *pll)
{
	/*
	 *  FIXME: use real calculations instead of using fixed values from the old
	 *         driver
	 */
	static struct {
		u32 limit;	/* pixlock rounding limit (arbitrary) */
		u8 m;		/* (df<<6) | vco_div_count */
		u8 n;		/* ref_div_count */
	} RGB514_clocks[7] = {
		{
		8000, (3 << 6) | 20, 9},	/*  7395 ps / 135.2273 MHz */
		{
		10000, (1 << 6) | 19, 3},	/*  9977 ps / 100.2273 MHz */
		{
		13000, (1 << 6) | 2, 3},	/* 12509 ps /  79.9432 MHz */
		{
		14000, (2 << 6) | 8, 7},	/* 13394 ps /  74.6591 MHz */
		{
		16000, (1 << 6) | 44, 6},	/* 15378 ps /  65.0284 MHz */
		{
		25000, (1 << 6) | 15, 5},	/* 17460 ps /  57.2727 MHz */
		{
		50000, (0 << 6) | 53, 7},	/* 33145 ps /  30.1705 MHz */
	};
	int i;

	for (i = 0; i < sizeof(RGB514_clocks) / sizeof(*RGB514_clocks);
	     i++)
		if (vclk_per <= RGB514_clocks[i].limit) {
			pll->ibm514.m = RGB514_clocks[i].m;
			pll->ibm514.n = RGB514_clocks[i].n;
			return 0;
		}
	return -EINVAL;
}

static u32 aty_pll_514_to_var(const struct fb_info *info,
			      const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u8 df, vco_div_count, ref_div_count;

	df = pll->ibm514.m >> 6;
	vco_div_count = pll->ibm514.m & 0x3f;
	ref_div_count = pll->ibm514.n;

	return ((par->ref_clk_per * ref_div_count) << (3 - df))/
	    		(vco_div_count + 65);
}

static void aty_set_pll_514(const struct fb_info *info,
			    const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	aty_st_514(0x06, 0x02, par);	/* DAC Operation */
	aty_st_514(0x10, 0x01, par);	/* PLL Control 1 */
	aty_st_514(0x70, 0x01, par);	/* Misc Control 1 */
	aty_st_514(0x8f, 0x1f, par);	/* PLL Ref. Divider Input */
	aty_st_514(0x03, 0x00, par);	/* Sync Control */
	aty_st_514(0x05, 0x00, par);	/* Power Management */
	aty_st_514(0x20, pll->ibm514.m, par);	/* F0 / M0 */
	aty_st_514(0x21, pll->ibm514.n, par);	/* F1 / N0 */
}

const struct aty_dac_ops aty_dac_ibm514 = {
	.set_dac	= aty_set_dac_514,
};

const struct aty_pll_ops aty_pll_ibm514 = {
	.var_to_pll	= aty_var_to_pll_514,
	.pll_to_var	= aty_pll_514_to_var,
	.set_pll	= aty_set_pll_514,
};


    /*
     *  ATI 68860-B DAC
     */

static int aty_set_dac_ATI68860_B(const struct fb_info *info,
				  const union aty_pll *pll, u32 bpp,
				  u32 accel)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 gModeReg, devSetupRegA, temp, mask;

	gModeReg = 0;
	devSetupRegA = 0;

	switch (bpp) {
	case 8:
		gModeReg = 0x83;
		devSetupRegA =
		    0x60 | 0x00 /*(info->mach64DAC8Bit ? 0x00 : 0x01) */ ;
		break;
	case 15:
		gModeReg = 0xA0;
		devSetupRegA = 0x60;
		break;
	case 16:
		gModeReg = 0xA1;
		devSetupRegA = 0x60;
		break;
	case 24:
		gModeReg = 0xC0;
		devSetupRegA = 0x60;
		break;
	case 32:
		gModeReg = 0xE3;
		devSetupRegA = 0x60;
		break;
	}

	if (!accel) {
		gModeReg = 0x80;
		devSetupRegA = 0x61;
	}

	temp = aty_ld_8(DAC_CNTL, par);
	aty_st_8(DAC_CNTL, (temp & ~DAC_EXT_SEL_RS2) | DAC_EXT_SEL_RS3,
		 par);

	aty_st_8(DAC_REGS + 2, 0x1D, par);
	aty_st_8(DAC_REGS + 3, gModeReg, par);
	aty_st_8(DAC_REGS, 0x02, par);

	temp = aty_ld_8(DAC_CNTL, par);
	aty_st_8(DAC_CNTL, temp | DAC_EXT_SEL_RS2 | DAC_EXT_SEL_RS3, par);

	if (info->fix.smem_len < ONE_MB)
		mask = 0x04;
	else if (info->fix.smem_len == ONE_MB)
		mask = 0x08;
	else
		mask = 0x0C;

	/* The following assumes that the BIOS has correctly set R7 of the
	 * Device Setup Register A at boot time.
	 */
#define A860_DELAY_L	0x80

	temp = aty_ld_8(DAC_REGS, par);
	aty_st_8(DAC_REGS, (devSetupRegA | mask) | (temp & A860_DELAY_L),
		 par);
	temp = aty_ld_8(DAC_CNTL, par);
	aty_st_8(DAC_CNTL, (temp & ~(DAC_EXT_SEL_RS2 | DAC_EXT_SEL_RS3)),
		 par);

	aty_st_le32(BUS_CNTL, 0x890e20f1, par);
	aty_st_le32(DAC_CNTL, 0x47052100, par);
	return 0;
}

const struct aty_dac_ops aty_dac_ati68860b = {
	.set_dac	= aty_set_dac_ATI68860_B,
};


    /*
     *  AT&T 21C498 DAC
     */

static int aty_set_dac_ATT21C498(const struct fb_info *info,
				 const union aty_pll *pll, u32 bpp,
				 u32 accel)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 dotClock;
	int muxmode = 0;
	int DACMask = 0;

	dotClock = 100000000 / pll->ics2595.period_in_ps;

	switch (bpp) {
	case 8:
		if (dotClock > 8000) {
			DACMask = 0x24;
			muxmode = 1;
		} else
			DACMask = 0x04;
		break;
	case 15:
		DACMask = 0x16;
		break;
	case 16:
		DACMask = 0x36;
		break;
	case 24:
		DACMask = 0xE6;
		break;
	case 32:
		DACMask = 0xE6;
		break;
	}

	if (1 /* info->mach64DAC8Bit */ )
		DACMask |= 0x02;

	aty_dac_waste4(par);
	aty_st_8(DAC_REGS + 2, DACMask, par);

	aty_st_le32(BUS_CNTL, 0x890e20f1, par);
	aty_st_le32(DAC_CNTL, 0x00072000, par);
	return muxmode;
}

const struct aty_dac_ops aty_dac_att21c498 = {
	.set_dac	= aty_set_dac_ATT21C498,
};


    /*
     *  ATI 18818 / ICS 2595 Clock Chip
     */

static int aty_var_to_pll_18818(const struct fb_info *info, u32 vclk_per,
				u32 bpp, union aty_pll *pll)
{
	u32 MHz100;		/* in 0.01 MHz */
	u32 program_bits;
	u32 post_divider;

	/* Calculate the programming word */
	MHz100 = 100000000 / vclk_per;

	program_bits = -1;
	post_divider = 1;

	if (MHz100 > MAX_FREQ_2595) {
		MHz100 = MAX_FREQ_2595;
		return -EINVAL;
	} else if (MHz100 < ABS_MIN_FREQ_2595) {
		program_bits = 0;	/* MHz100 = 257 */
		return -EINVAL;
	} else {
		while (MHz100 < MIN_FREQ_2595) {
			MHz100 *= 2;
			post_divider *= 2;
		}
	}
	MHz100 *= 1000;
	MHz100 = (REF_DIV_2595 * MHz100) / REF_FREQ_2595;
 
	MHz100 += 500;		/* + 0.5 round */
	MHz100 /= 1000;

	if (program_bits == -1) {
		program_bits = MHz100 - N_ADJ_2595;
		switch (post_divider) {
		case 1:
			program_bits |= 0x0600;
			break;
		case 2:
			program_bits |= 0x0400;
			break;
		case 4:
			program_bits |= 0x0200;
			break;
		case 8:
		default:
			break;
		}
	}

	program_bits |= STOP_BITS_2595;

	pll->ics2595.program_bits = program_bits;
	pll->ics2595.locationAddr = 0;
	pll->ics2595.post_divider = post_divider;
	pll->ics2595.period_in_ps = vclk_per;

	return 0;
}

static u32 aty_pll_18818_to_var(const struct fb_info *info,
				const union aty_pll *pll)
{
	return (pll->ics2595.period_in_ps);	/* default for now */
}

static void aty_ICS2595_put1bit(u8 data, const struct atyfb_par *par)
{
	u8 tmp;

	data &= 0x01;
	tmp = aty_ld_8(CLOCK_CNTL, par);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset,
		 (tmp & ~0x04) | (data << 2), par);

	tmp = aty_ld_8(CLOCK_CNTL, par);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, (tmp & ~0x08) | (0 << 3),
		 par);

	aty_StrobeClock(par);

	tmp = aty_ld_8(CLOCK_CNTL, par);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, (tmp & ~0x08) | (1 << 3),
		 par);

	aty_StrobeClock(par);
	return;
}

static void aty_set_pll18818(const struct fb_info *info,
			     const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 program_bits;
	u32 locationAddr;

	u32 i;

	u8 old_clock_cntl;
	u8 old_crtc_ext_disp;

	old_clock_cntl = aty_ld_8(CLOCK_CNTL, par);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, 0, par);

	old_crtc_ext_disp = aty_ld_8(CRTC_GEN_CNTL + 3, par);
	aty_st_8(CRTC_GEN_CNTL + 3,
		 old_crtc_ext_disp | (CRTC_EXT_DISP_EN >> 24), par);

	mdelay(15);		/* delay for 50 (15) ms */

	program_bits = pll->ics2595.program_bits;
	locationAddr = pll->ics2595.locationAddr;

	/* Program the clock chip */
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, 0, par);	/* Strobe = 0 */
	aty_StrobeClock(par);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset, 1, par);	/* Strobe = 0 */
	aty_StrobeClock(par);

	aty_ICS2595_put1bit(1, par);	/* Send start bits */
	aty_ICS2595_put1bit(0, par);	/* Start bit */
	aty_ICS2595_put1bit(0, par);	/* Read / ~Write */

	for (i = 0; i < 5; i++) {	/* Location 0..4 */
		aty_ICS2595_put1bit(locationAddr & 1, par);
		locationAddr >>= 1;
	}

	for (i = 0; i < 8 + 1 + 2 + 2; i++) {
		aty_ICS2595_put1bit(program_bits & 1, par);
		program_bits >>= 1;
	}

	mdelay(1);		/* delay for 1 ms */

	(void) aty_ld_8(DAC_REGS, par);	/* Clear DAC Counter */
	aty_st_8(CRTC_GEN_CNTL + 3, old_crtc_ext_disp, par);
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset,
		 old_clock_cntl | CLOCK_STROBE, par);

	mdelay(50);		/* delay for 50 (15) ms */
	aty_st_8(CLOCK_CNTL + par->clk_wr_offset,
		 ((pll->ics2595.locationAddr & 0x0F) | CLOCK_STROBE), par);
	return;
}

const struct aty_pll_ops aty_pll_ati18818_1 = {
	.var_to_pll	= aty_var_to_pll_18818,
	.pll_to_var	= aty_pll_18818_to_var,
	.set_pll	= aty_set_pll18818,
};


    /*
     *  STG 1703 Clock Chip
     */

static int aty_var_to_pll_1703(const struct fb_info *info, u32 vclk_per,
			       u32 bpp, union aty_pll *pll)
{
	u32 mhz100;		/* in 0.01 MHz */
	u32 program_bits;
	/* u32 post_divider; */
	u32 mach64MinFreq, mach64MaxFreq, mach64RefFreq;
	u32 temp, tempB;
	u16 remainder, preRemainder;
	short divider = 0, tempA;

	/* Calculate the programming word */
	mhz100 = 100000000 / vclk_per;
	mach64MinFreq = MIN_FREQ_2595;
	mach64MaxFreq = MAX_FREQ_2595;
	mach64RefFreq = REF_FREQ_2595;	/* 14.32 MHz */

	/* Calculate program word */
	if (mhz100 == 0)
		program_bits = 0xE0;
	else {
		if (mhz100 < mach64MinFreq)
			mhz100 = mach64MinFreq;
		if (mhz100 > mach64MaxFreq)
			mhz100 = mach64MaxFreq;

		divider = 0;
		while (mhz100 < (mach64MinFreq << 3)) {
			mhz100 <<= 1;
			divider += 0x20;
		}

		temp = (unsigned int) (mhz100);
		temp = (unsigned int) (temp * (MIN_N_1703 + 2));
		temp -= (short) (mach64RefFreq << 1);

		tempA = MIN_N_1703;
		preRemainder = 0xffff;

		do {
			tempB = temp;
			remainder = tempB % mach64RefFreq;
			tempB = tempB / mach64RefFreq;

			if ((tempB & 0xffff) <= 127
			    && (remainder <= preRemainder)) {
				preRemainder = remainder;
				divider &= ~0x1f;
				divider |= tempA;
				divider =
				    (divider & 0x00ff) +
				    ((tempB & 0xff) << 8);
			}

			temp += mhz100;
			tempA++;
		} while (tempA <= (MIN_N_1703 << 1));

		program_bits = divider;
	}

	pll->ics2595.program_bits = program_bits;
	pll->ics2595.locationAddr = 0;
	pll->ics2595.post_divider = divider;	/* fuer nix */
	pll->ics2595.period_in_ps = vclk_per;

	return 0;
}

static u32 aty_pll_1703_to_var(const struct fb_info *info,
			       const union aty_pll *pll)
{
	return (pll->ics2595.period_in_ps);	/* default for now */
}

static void aty_set_pll_1703(const struct fb_info *info,
			     const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 program_bits;
	u32 locationAddr;

	char old_crtc_ext_disp;

	old_crtc_ext_disp = aty_ld_8(CRTC_GEN_CNTL + 3, par);
	aty_st_8(CRTC_GEN_CNTL + 3,
		 old_crtc_ext_disp | (CRTC_EXT_DISP_EN >> 24), par);

	program_bits = pll->ics2595.program_bits;
	locationAddr = pll->ics2595.locationAddr;

	/* Program clock */
	aty_dac_waste4(par);

	(void) aty_ld_8(DAC_REGS + 2, par);
	aty_st_8(DAC_REGS + 2, (locationAddr << 1) + 0x20, par);
	aty_st_8(DAC_REGS + 2, 0, par);
	aty_st_8(DAC_REGS + 2, (program_bits & 0xFF00) >> 8, par);
	aty_st_8(DAC_REGS + 2, (program_bits & 0xFF), par);

	(void) aty_ld_8(DAC_REGS, par);	/* Clear DAC Counter */
	aty_st_8(CRTC_GEN_CNTL + 3, old_crtc_ext_disp, par);
	return;
}

const struct aty_pll_ops aty_pll_stg1703 = {
	.var_to_pll	= aty_var_to_pll_1703,
	.pll_to_var	= aty_pll_1703_to_var,
	.set_pll	= aty_set_pll_1703,
};


    /*
     *  Chrontel 8398 Clock Chip
     */

static int aty_var_to_pll_8398(const struct fb_info *info, u32 vclk_per,
			       u32 bpp, union aty_pll *pll)
{
	u32 tempA, tempB, fOut, longMHz100, diff, preDiff;

	u32 mhz100;		/* in 0.01 MHz */
	u32 program_bits;
	/* u32 post_divider; */
	u32 mach64MinFreq, mach64MaxFreq, mach64RefFreq;
	u16 m, n, k = 0, save_m, save_n, twoToKth;

	/* Calculate the programming word */
	mhz100 = 100000000 / vclk_per;
	mach64MinFreq = MIN_FREQ_2595;
	mach64MaxFreq = MAX_FREQ_2595;
	mach64RefFreq = REF_FREQ_2595;	/* 14.32 MHz */

	save_m = 0;
	save_n = 0;

	/* Calculate program word */
	if (mhz100 == 0)
		program_bits = 0xE0;
	else {
		if (mhz100 < mach64MinFreq)
			mhz100 = mach64MinFreq;
		if (mhz100 > mach64MaxFreq)
			mhz100 = mach64MaxFreq;

		longMHz100 = mhz100 * 256 / 100;	/* 8 bit scale this */

		while (mhz100 < (mach64MinFreq << 3)) {
			mhz100 <<= 1;
			k++;
		}

		twoToKth = 1 << k;
		diff = 0;
		preDiff = 0xFFFFFFFF;

		for (m = MIN_M; m <= MAX_M; m++) {
			for (n = MIN_N; n <= MAX_N; n++) {
				tempA = 938356;		/* 14.31818 * 65536 */
				tempA *= (n + 8);	/* 43..256 */
				tempB = twoToKth * 256;
				tempB *= (m + 2);	/* 4..32 */
				fOut = tempA / tempB;	/* 8 bit scale */

				if (longMHz100 > fOut)
					diff = longMHz100 - fOut;
				else
					diff = fOut - longMHz100;

				if (diff < preDiff) {
					save_m = m;
					save_n = n;
					preDiff = diff;
				}
			}
		}

		program_bits = (k << 6) + (save_m) + (save_n << 8);
	}

	pll->ics2595.program_bits = program_bits;
	pll->ics2595.locationAddr = 0;
	pll->ics2595.post_divider = 0;
	pll->ics2595.period_in_ps = vclk_per;

	return 0;
}

static u32 aty_pll_8398_to_var(const struct fb_info *info,
			       const union aty_pll *pll)
{
	return (pll->ics2595.period_in_ps);	/* default for now */
}

static void aty_set_pll_8398(const struct fb_info *info,
			     const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 program_bits;
	u32 locationAddr;

	char old_crtc_ext_disp;
	char tmp;

	old_crtc_ext_disp = aty_ld_8(CRTC_GEN_CNTL + 3, par);
	aty_st_8(CRTC_GEN_CNTL + 3,
		 old_crtc_ext_disp | (CRTC_EXT_DISP_EN >> 24), par);

	program_bits = pll->ics2595.program_bits;
	locationAddr = pll->ics2595.locationAddr;

	/* Program clock */
	tmp = aty_ld_8(DAC_CNTL, par);
	aty_st_8(DAC_CNTL, tmp | DAC_EXT_SEL_RS2 | DAC_EXT_SEL_RS3, par);

	aty_st_8(DAC_REGS, locationAddr, par);
	aty_st_8(DAC_REGS + 1, (program_bits & 0xff00) >> 8, par);
	aty_st_8(DAC_REGS + 1, (program_bits & 0xff), par);

	tmp = aty_ld_8(DAC_CNTL, par);
	aty_st_8(DAC_CNTL, (tmp & ~DAC_EXT_SEL_RS2) | DAC_EXT_SEL_RS3,
		 par);

	(void) aty_ld_8(DAC_REGS, par);	/* Clear DAC Counter */
	aty_st_8(CRTC_GEN_CNTL + 3, old_crtc_ext_disp, par);

	return;
}

const struct aty_pll_ops aty_pll_ch8398 = {
	.var_to_pll	= aty_var_to_pll_8398,
	.pll_to_var	= aty_pll_8398_to_var,
	.set_pll	= aty_set_pll_8398,
};


    /*
     *  AT&T 20C408 Clock Chip
     */

static int aty_var_to_pll_408(const struct fb_info *info, u32 vclk_per,
			      u32 bpp, union aty_pll *pll)
{
	u32 mhz100;		/* in 0.01 MHz */
	u32 program_bits;
	/* u32 post_divider; */
	u32 mach64MinFreq, mach64MaxFreq, mach64RefFreq;
	u32 temp, tempB;
	u16 remainder, preRemainder;
	short divider = 0, tempA;

	/* Calculate the programming word */
	mhz100 = 100000000 / vclk_per;
	mach64MinFreq = MIN_FREQ_2595;
	mach64MaxFreq = MAX_FREQ_2595;
	mach64RefFreq = REF_FREQ_2595;	/* 14.32 MHz */

	/* Calculate program word */
	if (mhz100 == 0)
		program_bits = 0xFF;
	else {
		if (mhz100 < mach64MinFreq)
			mhz100 = mach64MinFreq;
		if (mhz100 > mach64MaxFreq)
			mhz100 = mach64MaxFreq;

		while (mhz100 < (mach64MinFreq << 3)) {
			mhz100 <<= 1;
			divider += 0x40;
		}

		temp = (unsigned int) mhz100;
		temp = (unsigned int) (temp * (MIN_N_408 + 2));
		temp -= ((short) (mach64RefFreq << 1));

		tempA = MIN_N_408;
		preRemainder = 0xFFFF;

		do {
			tempB = temp;
			remainder = tempB % mach64RefFreq;
			tempB = tempB / mach64RefFreq;
			if (((tempB & 0xFFFF) <= 255)
			    && (remainder <= preRemainder)) {
				preRemainder = remainder;
				divider &= ~0x3f;
				divider |= tempA;
				divider =
				    (divider & 0x00FF) +
				    ((tempB & 0xFF) << 8);
			}
			temp += mhz100;
			tempA++;
		} while (tempA <= 32);

		program_bits = divider;
	}

	pll->ics2595.program_bits = program_bits;
	pll->ics2595.locationAddr = 0;
	pll->ics2595.post_divider = divider;	/* fuer nix */
	pll->ics2595.period_in_ps = vclk_per;

	return 0;
}

static u32 aty_pll_408_to_var(const struct fb_info *info,
			      const union aty_pll *pll)
{
	return (pll->ics2595.period_in_ps);	/* default for now */
}

static void aty_set_pll_408(const struct fb_info *info,
			    const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 program_bits;
	u32 locationAddr;

	u8 tmpA, tmpB, tmpC;
	char old_crtc_ext_disp;

	old_crtc_ext_disp = aty_ld_8(CRTC_GEN_CNTL + 3, par);
	aty_st_8(CRTC_GEN_CNTL + 3,
		 old_crtc_ext_disp | (CRTC_EXT_DISP_EN >> 24), par);

	program_bits = pll->ics2595.program_bits;
	locationAddr = pll->ics2595.locationAddr;

	/* Program clock */
	aty_dac_waste4(par);
	tmpB = aty_ld_8(DAC_REGS + 2, par) | 1;
	aty_dac_waste4(par);
	aty_st_8(DAC_REGS + 2, tmpB, par);

	tmpA = tmpB;
	tmpC = tmpA;
	tmpA |= 8;
	tmpB = 1;

	aty_st_8(DAC_REGS, tmpB, par);
	aty_st_8(DAC_REGS + 2, tmpA, par);

	udelay(400);		/* delay for 400 us */

	locationAddr = (locationAddr << 2) + 0x40;
	tmpB = locationAddr;
	tmpA = program_bits >> 8;

	aty_st_8(DAC_REGS, tmpB, par);
	aty_st_8(DAC_REGS + 2, tmpA, par);

	tmpB = locationAddr + 1;
	tmpA = (u8) program_bits;

	aty_st_8(DAC_REGS, tmpB, par);
	aty_st_8(DAC_REGS + 2, tmpA, par);

	tmpB = locationAddr + 2;
	tmpA = 0x77;

	aty_st_8(DAC_REGS, tmpB, par);
	aty_st_8(DAC_REGS + 2, tmpA, par);

	udelay(400);		/* delay for 400 us */
	tmpA = tmpC & (~(1 | 8));
	tmpB = 1;

	aty_st_8(DAC_REGS, tmpB, par);
	aty_st_8(DAC_REGS + 2, tmpA, par);

	(void) aty_ld_8(DAC_REGS, par);	/* Clear DAC Counter */
	aty_st_8(CRTC_GEN_CNTL + 3, old_crtc_ext_disp, par);
	return;
}

const struct aty_pll_ops aty_pll_att20c408 = {
	.var_to_pll	= aty_var_to_pll_408,
	.pll_to_var	= aty_pll_408_to_var,
	.set_pll	= aty_set_pll_408,
};


    /*
     *  Unsupported DAC and Clock Chip
     */

static int aty_set_dac_unsupported(const struct fb_info *info,
				   const union aty_pll *pll, u32 bpp,
				   u32 accel)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	aty_st_le32(BUS_CNTL, 0x890e20f1, par);
	aty_st_le32(DAC_CNTL, 0x47052100, par);
	/* new in 2.2.3p1 from Geert. ???????? */
	aty_st_le32(BUS_CNTL, 0x590e10ff, par);
	aty_st_le32(DAC_CNTL, 0x47012100, par);
	return 0;
}

static int dummy(void)
{
	return 0;
}

const struct aty_dac_ops aty_dac_unsupported = {
	.set_dac	= aty_set_dac_unsupported,
};

const struct aty_pll_ops aty_pll_unsupported = {
	.var_to_pll	= (void *) dummy,
	.pll_to_var	= (void *) dummy,
	.set_pll	= (void *) dummy,
};
