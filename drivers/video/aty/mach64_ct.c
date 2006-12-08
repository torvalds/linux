
/*
 *  ATI Mach64 CT/VT/GT/LT Support
 */

#include <linux/fb.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <video/mach64.h>
#include "atyfb.h"

#undef DEBUG

static int aty_valid_pll_ct (const struct fb_info *info, u32 vclk_per, struct pll_ct *pll);
static int aty_dsp_gt       (const struct fb_info *info, u32 bpp, struct pll_ct *pll);
static int aty_var_to_pll_ct(const struct fb_info *info, u32 vclk_per, u32 bpp, union aty_pll *pll);
static u32 aty_pll_to_var_ct(const struct fb_info *info, const union aty_pll *pll);

u8 aty_ld_pll_ct(int offset, const struct atyfb_par *par)
{
	u8 res;

	/* write addr byte */
	aty_st_8(CLOCK_CNTL_ADDR, (offset << 2) & PLL_ADDR, par);
	/* read the register value */
	res = aty_ld_8(CLOCK_CNTL_DATA, par);
	return res;
}

static void aty_st_pll_ct(int offset, u8 val, const struct atyfb_par *par)
{
	/* write addr byte */
	aty_st_8(CLOCK_CNTL_ADDR, ((offset << 2) & PLL_ADDR) | PLL_WR_EN, par);
	/* write the register value */
	aty_st_8(CLOCK_CNTL_DATA, val & PLL_DATA, par);
	aty_st_8(CLOCK_CNTL_ADDR, ((offset << 2) & PLL_ADDR) & ~PLL_WR_EN, par);
}

/*
 * by Daniel Mantione
 *                                  <daniel.mantione@freepascal.org>
 *
 *
 * ATI Mach64 CT clock synthesis description.
 *
 * All clocks on the Mach64 can be calculated using the same principle:
 *
 *       XTALIN * x * FB_DIV
 * CLK = ----------------------
 *       PLL_REF_DIV * POST_DIV
 *
 * XTALIN is a fixed speed clock. Common speeds are 14.31 MHz and 29.50 MHz.
 * PLL_REF_DIV can be set by the user, but is the same for all clocks.
 * FB_DIV can be set by the user for each clock individually, it should be set
 * between 128 and 255, the chip will generate a bad clock signal for too low
 * values.
 * x depends on the type of clock; usually it is 2, but for the MCLK it can also
 * be set to 4.
 * POST_DIV can be set by the user for each clock individually, Possible values
 * are 1,2,4,8 and for some clocks other values are available too.
 * CLK is of course the clock speed that is generated.
 *
 * The Mach64 has these clocks:
 *
 * MCLK			The clock rate of the chip
 * XCLK			The clock rate of the on-chip memory
 * VCLK0		First pixel clock of first CRT controller
 * VCLK1    Second pixel clock of first CRT controller
 * VCLK2		Third pixel clock of first CRT controller
 * VCLK3    Fourth pixel clock of first CRT controller
 * VCLK			Selected pixel clock, one of VCLK0, VCLK1, VCLK2, VCLK3
 * V2CLK		Pixel clock of the second CRT controller.
 * SCLK			Multi-purpose clock
 *
 * - MCLK and XCLK use the same FB_DIV
 * - VCLK0 .. VCLK3 use the same FB_DIV
 * - V2CLK is needed when the second CRTC is used (can be used for dualhead);
 *   i.e. CRT monitor connected to laptop has different resolution than built
 *   in LCD monitor.
 * - SCLK is not available on all cards; it is know to exist on the Rage LT-PRO,
 *   Rage XL and Rage Mobility. It is know not to exist on the Mach64 VT.
 * - V2CLK is not available on all cards, most likely only the Rage LT-PRO,
 *   the Rage XL and the Rage Mobility
 *
 * SCLK can be used to:
 * - Clock the chip instead of MCLK
 * - Replace XTALIN with a user defined frequency
 * - Generate the pixel clock for the LCD monitor (instead of VCLK)
 */

 /*
  * It can be quite hard to calculate XCLK and MCLK if they don't run at the
  * same frequency. Luckily, until now all cards that need asynchrone clock
  * speeds seem to have SCLK.
  * So this driver uses SCLK to clock the chip and XCLK to clock the memory.
  */

/* ------------------------------------------------------------------------- */

/*
 *  PLL programming (Mach64 CT family)
 *
 *
 * This procedure sets the display fifo. The display fifo is a buffer that
 * contains data read from the video memory that waits to be processed by
 * the CRT controller.
 *
 * On the more modern Mach64 variants, the chip doesn't calculate the
 * interval after which the display fifo has to be reloaded from memory
 * automatically, the driver has to do it instead.
 */

#define Maximum_DSP_PRECISION 7
static u8 postdividers[] = {1,2,4,8,3};

static int aty_dsp_gt(const struct fb_info *info, u32 bpp, struct pll_ct *pll)
{
	u32 dsp_off, dsp_on, dsp_xclks;
	u32 multiplier, divider, ras_multiplier, ras_divider, tmp;
	u8 vshift, xshift;
	s8 dsp_precision;

	multiplier = ((u32)pll->mclk_fb_div) * pll->vclk_post_div_real;
	divider = ((u32)pll->vclk_fb_div) * pll->xclk_ref_div;

	ras_multiplier = pll->xclkmaxrasdelay;
	ras_divider = 1;

	if (bpp>=8)
		divider = divider * (bpp >> 2);

	vshift = (6 - 2) - pll->xclk_post_div;	/* FIFO is 64 bits wide in accelerator mode ... */

	if (bpp == 0)
		vshift--;	/* ... but only 32 bits in VGA mode. */

#ifdef CONFIG_FB_ATY_GENERIC_LCD
	if (pll->xres != 0) {
		struct atyfb_par *par = (struct atyfb_par *) info->par;

		multiplier = multiplier * par->lcd_width;
		divider = divider * pll->xres & ~7;

		ras_multiplier = ras_multiplier * par->lcd_width;
		ras_divider = ras_divider * pll->xres & ~7;
	}
#endif
	/* If we don't do this, 32 bits for multiplier & divider won't be
	enough in certain situations! */
	while (((multiplier | divider) & 1) == 0) {
		multiplier = multiplier >> 1;
		divider = divider >> 1;
	}

	/* Determine DSP precision first */
	tmp = ((multiplier * pll->fifo_size) << vshift) / divider;

	for (dsp_precision = -5;  tmp;  dsp_precision++)
		tmp >>= 1;
	if (dsp_precision < 0)
		dsp_precision = 0;
	else if (dsp_precision > Maximum_DSP_PRECISION)
		dsp_precision = Maximum_DSP_PRECISION;

	xshift = 6 - dsp_precision;
	vshift += xshift;

	/* Move on to dsp_off */
	dsp_off = ((multiplier * (pll->fifo_size - 1)) << vshift) / divider -
		(1 << (vshift - xshift));

/*    if (bpp == 0)
        dsp_on = ((multiplier * 20 << vshift) + divider) / divider;
    else */
	{
		dsp_on = ((multiplier << vshift) + divider) / divider;
		tmp = ((ras_multiplier << xshift) + ras_divider) / ras_divider;
		if (dsp_on < tmp)
		dsp_on = tmp;
		dsp_on = dsp_on + (tmp * 2) + (pll->xclkpagefaultdelay << xshift);
	}

	/* Calculate rounding factor and apply it to dsp_on */
	tmp = ((1 << (Maximum_DSP_PRECISION - dsp_precision)) - 1) >> 1;
	dsp_on = ((dsp_on + tmp) / (tmp + 1)) * (tmp + 1);

	if (dsp_on >= ((dsp_off / (tmp + 1)) * (tmp + 1))) {
		dsp_on = dsp_off - (multiplier << vshift) / divider;
		dsp_on = (dsp_on / (tmp + 1)) * (tmp + 1);
	}

	/* Last but not least:  dsp_xclks */
	dsp_xclks = ((multiplier << (vshift + 5)) + divider) / divider;

	/* Get register values. */
	pll->dsp_on_off = (dsp_on << 16) + dsp_off;
	pll->dsp_config = (dsp_precision << 20) | (pll->dsp_loop_latency << 16) | dsp_xclks;
#ifdef DEBUG
	printk("atyfb(%s): dsp_config 0x%08x, dsp_on_off 0x%08x\n",
		__FUNCTION__, pll->dsp_config, pll->dsp_on_off);
#endif
	return 0;
}

static int aty_valid_pll_ct(const struct fb_info *info, u32 vclk_per, struct pll_ct *pll)
{
	u32 q;
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	int pllvclk;

	/* FIXME: use the VTB/GTB /{3,6,12} post dividers if they're better suited */
	q = par->ref_clk_per * pll->pll_ref_div * 4 / vclk_per;
	if (q < 16*8 || q > 255*8) {
		printk(KERN_CRIT "atyfb: vclk out of range\n");
		return -EINVAL;
	} else {
		pll->vclk_post_div  = (q < 128*8);
		pll->vclk_post_div += (q <  64*8);
		pll->vclk_post_div += (q <  32*8);
	}
	pll->vclk_post_div_real = postdividers[pll->vclk_post_div];
	//    pll->vclk_post_div <<= 6;
	pll->vclk_fb_div = q * pll->vclk_post_div_real / 8;
	pllvclk = (1000000 * 2 * pll->vclk_fb_div) /
		(par->ref_clk_per * pll->pll_ref_div);
#ifdef DEBUG
	printk("atyfb(%s): pllvclk=%d MHz, vclk=%d MHz\n",
		__FUNCTION__, pllvclk, pllvclk / pll->vclk_post_div_real);
#endif
	pll->pll_vclk_cntl = 0x03; /* VCLK = PLL_VCLK/VCLKx_POST */

	/* Set ECP (scaler/overlay clock) divider */
	if (par->pll_limits.ecp_max) {
		int ecp = pllvclk / pll->vclk_post_div_real;
		int ecp_div = 0;

		while (ecp > par->pll_limits.ecp_max && ecp_div < 2) {
			ecp >>= 1;
			ecp_div++;
		}
		pll->pll_vclk_cntl |= ecp_div << 4;
	}

	return 0;
}

static int aty_var_to_pll_ct(const struct fb_info *info, u32 vclk_per, u32 bpp, union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	int err;

	if ((err = aty_valid_pll_ct(info, vclk_per, &pll->ct)))
		return err;
	if (M64_HAS(GTB_DSP) && (err = aty_dsp_gt(info, bpp, &pll->ct)))
		return err;
	/*aty_calc_pll_ct(info, &pll->ct);*/
	return 0;
}

static u32 aty_pll_to_var_ct(const struct fb_info *info, const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 ret;
	ret = par->ref_clk_per * pll->ct.pll_ref_div * pll->ct.vclk_post_div_real / pll->ct.vclk_fb_div / 2;
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	if(pll->ct.xres > 0) {
		ret *= par->lcd_width;
		ret /= pll->ct.xres;
	}
#endif
#ifdef DEBUG
	printk("atyfb(%s): calculated 0x%08X(%i)\n", __FUNCTION__, ret, ret);
#endif
	return ret;
}

void aty_set_pll_ct(const struct fb_info *info, const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 crtc_gen_cntl, lcd_gen_cntrl;
	u8 tmp, tmp2;

	lcd_gen_cntrl = 0;
#ifdef DEBUG
	printk("atyfb(%s): about to program:\n"
		"pll_ext_cntl=0x%02x pll_gen_cntl=0x%02x pll_vclk_cntl=0x%02x\n",
		__FUNCTION__,
		pll->ct.pll_ext_cntl, pll->ct.pll_gen_cntl, pll->ct.pll_vclk_cntl);

	printk("atyfb(%s): setting clock %lu for FeedBackDivider %i, ReferenceDivider %i, PostDivider %i(%i)\n",
		__FUNCTION__,
		par->clk_wr_offset, pll->ct.vclk_fb_div,
		pll->ct.pll_ref_div, pll->ct.vclk_post_div, pll->ct.vclk_post_div_real);
#endif
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	if (par->lcd_table != 0) {
		/* turn off LCD */
		lcd_gen_cntrl = aty_ld_lcd(LCD_GEN_CNTL, par);
		aty_st_lcd(LCD_GEN_CNTL, lcd_gen_cntrl & ~LCD_ON, par);
	}
#endif
	aty_st_8(CLOCK_CNTL, par->clk_wr_offset | CLOCK_STROBE, par);

	/* Temporarily switch to accelerator mode */
	crtc_gen_cntl = aty_ld_le32(CRTC_GEN_CNTL, par);
	if (!(crtc_gen_cntl & CRTC_EXT_DISP_EN))
		aty_st_le32(CRTC_GEN_CNTL, crtc_gen_cntl | CRTC_EXT_DISP_EN, par);

	/* Reset VCLK generator */
	aty_st_pll_ct(PLL_VCLK_CNTL, pll->ct.pll_vclk_cntl, par);

	/* Set post-divider */
	tmp2 = par->clk_wr_offset << 1;
	tmp = aty_ld_pll_ct(VCLK_POST_DIV, par);
	tmp &= ~(0x03U << tmp2);
	tmp |= ((pll->ct.vclk_post_div & 0x03U) << tmp2);
	aty_st_pll_ct(VCLK_POST_DIV, tmp, par);

	/* Set extended post-divider */
	tmp = aty_ld_pll_ct(PLL_EXT_CNTL, par);
	tmp &= ~(0x10U << par->clk_wr_offset);
	tmp &= 0xF0U;
	tmp |= pll->ct.pll_ext_cntl;
	aty_st_pll_ct(PLL_EXT_CNTL, tmp, par);

	/* Set feedback divider */
	tmp = VCLK0_FB_DIV + par->clk_wr_offset;
	aty_st_pll_ct(tmp, (pll->ct.vclk_fb_div & 0xFFU), par);

	aty_st_pll_ct(PLL_GEN_CNTL, (pll->ct.pll_gen_cntl & (~(PLL_OVERRIDE | PLL_MCLK_RST))) | OSC_EN, par);

	/* End VCLK generator reset */
	aty_st_pll_ct(PLL_VCLK_CNTL, pll->ct.pll_vclk_cntl & ~(PLL_VCLK_RST), par);
	mdelay(5);

	aty_st_pll_ct(PLL_GEN_CNTL, pll->ct.pll_gen_cntl, par);
	aty_st_pll_ct(PLL_VCLK_CNTL, pll->ct.pll_vclk_cntl, par);
	mdelay(1);

	/* Restore mode register */
	if (!(crtc_gen_cntl & CRTC_EXT_DISP_EN))
		aty_st_le32(CRTC_GEN_CNTL, crtc_gen_cntl, par);

	if (M64_HAS(GTB_DSP)) {
		u8 dll_cntl;

		if (M64_HAS(XL_DLL))
			dll_cntl = 0x80;
		else if (par->ram_type >= SDRAM)
			dll_cntl = 0xa6;
		else
			dll_cntl = 0xa0;
		aty_st_pll_ct(DLL_CNTL, dll_cntl, par);
		aty_st_pll_ct(VFC_CNTL, 0x1b, par);
		aty_st_le32(DSP_CONFIG, pll->ct.dsp_config, par);
		aty_st_le32(DSP_ON_OFF, pll->ct.dsp_on_off, par);

		mdelay(10);
		aty_st_pll_ct(DLL_CNTL, dll_cntl, par);
		mdelay(10);
		aty_st_pll_ct(DLL_CNTL, dll_cntl | 0x40, par);
		mdelay(10);
		aty_st_pll_ct(DLL_CNTL, dll_cntl & ~0x40, par);
	}
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	if (par->lcd_table != 0) {
		/* restore LCD */
		aty_st_lcd(LCD_GEN_CNTL, lcd_gen_cntrl, par);
	}
#endif
}

static void __devinit aty_get_pll_ct(const struct fb_info *info,
				     union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u8 tmp, clock;

	clock = aty_ld_8(CLOCK_CNTL, par) & 0x03U;
	tmp = clock << 1;
	pll->ct.vclk_post_div = (aty_ld_pll_ct(VCLK_POST_DIV, par) >> tmp) & 0x03U;

	pll->ct.pll_ext_cntl = aty_ld_pll_ct(PLL_EXT_CNTL, par) & 0x0FU;
	pll->ct.vclk_fb_div = aty_ld_pll_ct(VCLK0_FB_DIV + clock, par) & 0xFFU;
	pll->ct.pll_ref_div = aty_ld_pll_ct(PLL_REF_DIV, par);
	pll->ct.mclk_fb_div = aty_ld_pll_ct(MCLK_FB_DIV, par);

	pll->ct.pll_gen_cntl = aty_ld_pll_ct(PLL_GEN_CNTL, par);
	pll->ct.pll_vclk_cntl = aty_ld_pll_ct(PLL_VCLK_CNTL, par);

	if (M64_HAS(GTB_DSP)) {
		pll->ct.dsp_config = aty_ld_le32(DSP_CONFIG, par);
		pll->ct.dsp_on_off = aty_ld_le32(DSP_ON_OFF, par);
	}
}

static int __devinit aty_init_pll_ct(const struct fb_info *info,
				     union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u8 mpost_div, xpost_div, sclk_post_div_real;
	u32 q, memcntl, trp;
	u32 dsp_config, dsp_on_off, vga_dsp_config, vga_dsp_on_off;
#ifdef DEBUG
	int pllmclk, pllsclk;
#endif
	pll->ct.pll_ext_cntl = aty_ld_pll_ct(PLL_EXT_CNTL, par);
	pll->ct.xclk_post_div = pll->ct.pll_ext_cntl & 0x07;
	pll->ct.xclk_ref_div = 1;
	switch (pll->ct.xclk_post_div) {
	case 0:  case 1:  case 2:  case 3:
		break;

	case 4:
		pll->ct.xclk_ref_div = 3;
		pll->ct.xclk_post_div = 0;
		break;

	default:
		printk(KERN_CRIT "atyfb: Unsupported xclk source:  %d.\n", pll->ct.xclk_post_div);
		return -EINVAL;
	}
	pll->ct.mclk_fb_mult = 2;
	if(pll->ct.pll_ext_cntl & PLL_MFB_TIMES_4_2B) {
		pll->ct.mclk_fb_mult = 4;
		pll->ct.xclk_post_div -= 1;
	}

#ifdef DEBUG
	printk("atyfb(%s): mclk_fb_mult=%d, xclk_post_div=%d\n",
		__FUNCTION__, pll->ct.mclk_fb_mult, pll->ct.xclk_post_div);
#endif

	memcntl = aty_ld_le32(MEM_CNTL, par);
	trp = (memcntl & 0x300) >> 8;

	pll->ct.xclkpagefaultdelay = ((memcntl & 0xc00) >> 10) + ((memcntl & 0x1000) >> 12) + trp + 2;
	pll->ct.xclkmaxrasdelay = ((memcntl & 0x70000) >> 16) + trp + 2;

	if (M64_HAS(FIFO_32)) {
		pll->ct.fifo_size = 32;
	} else {
		pll->ct.fifo_size = 24;
		pll->ct.xclkpagefaultdelay += 2;
		pll->ct.xclkmaxrasdelay += 3;
	}

	switch (par->ram_type) {
	case DRAM:
		if (info->fix.smem_len<=ONE_MB) {
			pll->ct.dsp_loop_latency = 10;
		} else {
			pll->ct.dsp_loop_latency = 8;
			pll->ct.xclkpagefaultdelay += 2;
		}
		break;
	case EDO:
	case PSEUDO_EDO:
		if (info->fix.smem_len<=ONE_MB) {
			pll->ct.dsp_loop_latency = 9;
		} else {
			pll->ct.dsp_loop_latency = 8;
			pll->ct.xclkpagefaultdelay += 1;
		}
		break;
	case SDRAM:
		if (info->fix.smem_len<=ONE_MB) {
			pll->ct.dsp_loop_latency = 11;
		} else {
			pll->ct.dsp_loop_latency = 10;
			pll->ct.xclkpagefaultdelay += 1;
		}
		break;
	case SGRAM:
		pll->ct.dsp_loop_latency = 8;
		pll->ct.xclkpagefaultdelay += 3;
		break;
	default:
		pll->ct.dsp_loop_latency = 11;
		pll->ct.xclkpagefaultdelay += 3;
		break;
	}

	if (pll->ct.xclkmaxrasdelay <= pll->ct.xclkpagefaultdelay)
		pll->ct.xclkmaxrasdelay = pll->ct.xclkpagefaultdelay + 1;

	/* Allow BIOS to override */
	dsp_config = aty_ld_le32(DSP_CONFIG, par);
	dsp_on_off = aty_ld_le32(DSP_ON_OFF, par);
	vga_dsp_config = aty_ld_le32(VGA_DSP_CONFIG, par);
	vga_dsp_on_off = aty_ld_le32(VGA_DSP_ON_OFF, par);

	if (dsp_config)
		pll->ct.dsp_loop_latency = (dsp_config & DSP_LOOP_LATENCY) >> 16;
#if 0
	FIXME: is it relevant for us?
	if ((!dsp_on_off && !M64_HAS(RESET_3D)) ||
		((dsp_on_off == vga_dsp_on_off) &&
		(!dsp_config || !((dsp_config ^ vga_dsp_config) & DSP_XCLKS_PER_QW)))) {
		vga_dsp_on_off &= VGA_DSP_OFF;
		vga_dsp_config &= VGA_DSP_XCLKS_PER_QW;
		if (ATIDivide(vga_dsp_on_off, vga_dsp_config, 5, 1) > 24)
			pll->ct.fifo_size = 32;
		else
			pll->ct.fifo_size = 24;
	}
#endif
	/* Exit if the user does not want us to tamper with the clock
	rates of her chip. */
	if (par->mclk_per == 0) {
		u8 mclk_fb_div, pll_ext_cntl;
		pll->ct.pll_ref_div = aty_ld_pll_ct(PLL_REF_DIV, par);
		pll_ext_cntl = aty_ld_pll_ct(PLL_EXT_CNTL, par);
		pll->ct.xclk_post_div_real = postdividers[pll_ext_cntl & 0x07];
		mclk_fb_div = aty_ld_pll_ct(MCLK_FB_DIV, par);
		if (pll_ext_cntl & PLL_MFB_TIMES_4_2B)
			mclk_fb_div <<= 1;
		pll->ct.mclk_fb_div = mclk_fb_div;
		return 0;
	}

	pll->ct.pll_ref_div = par->pll_per * 2 * 255 / par->ref_clk_per;

	/* FIXME: use the VTB/GTB /3 post divider if it's better suited */
	q = par->ref_clk_per * pll->ct.pll_ref_div * 8 /
		(pll->ct.mclk_fb_mult * par->xclk_per);

	if (q < 16*8 || q > 255*8) {
		printk(KERN_CRIT "atxfb: xclk out of range\n");
		return -EINVAL;
	} else {
		xpost_div  = (q < 128*8);
		xpost_div += (q <  64*8);
		xpost_div += (q <  32*8);
	}
	pll->ct.xclk_post_div_real = postdividers[xpost_div];
	pll->ct.mclk_fb_div = q * pll->ct.xclk_post_div_real / 8;

#ifdef DEBUG
	pllmclk = (1000000 * pll->ct.mclk_fb_mult * pll->ct.mclk_fb_div) /
			(par->ref_clk_per * pll->ct.pll_ref_div);
	printk("atyfb(%s): pllmclk=%d MHz, xclk=%d MHz\n",
		__FUNCTION__, pllmclk, pllmclk / pll->ct.xclk_post_div_real);
#endif

	if (M64_HAS(SDRAM_MAGIC_PLL) && (par->ram_type >= SDRAM))
		pll->ct.pll_gen_cntl = OSC_EN;
	else
		pll->ct.pll_gen_cntl = OSC_EN | DLL_PWDN /* | FORCE_DCLK_TRI_STATE */;

	if (M64_HAS(MAGIC_POSTDIV))
		pll->ct.pll_ext_cntl = 0;
	else
		pll->ct.pll_ext_cntl = xpost_div;

	if (pll->ct.mclk_fb_mult == 4)
		pll->ct.pll_ext_cntl |= PLL_MFB_TIMES_4_2B;

	if (par->mclk_per == par->xclk_per) {
		pll->ct.pll_gen_cntl |= (xpost_div << 4); /* mclk == xclk */
	} else {
		/*
		* The chip clock is not equal to the memory clock.
		* Therefore we will use sclk to clock the chip.
		*/
		pll->ct.pll_gen_cntl |= (6 << 4); /* mclk == sclk */

		q = par->ref_clk_per * pll->ct.pll_ref_div * 4 / par->mclk_per;
		if (q < 16*8 || q > 255*8) {
			printk(KERN_CRIT "atyfb: mclk out of range\n");
			return -EINVAL;
		} else {
			mpost_div  = (q < 128*8);
			mpost_div += (q <  64*8);
			mpost_div += (q <  32*8);
		}
		sclk_post_div_real = postdividers[mpost_div];
		pll->ct.sclk_fb_div = q * sclk_post_div_real / 8;
		pll->ct.spll_cntl2 = mpost_div << 4;
#ifdef DEBUG
		pllsclk = (1000000 * 2 * pll->ct.sclk_fb_div) /
			(par->ref_clk_per * pll->ct.pll_ref_div);
		printk("atyfb(%s): use sclk, pllsclk=%d MHz, sclk=mclk=%d MHz\n",
			__FUNCTION__, pllsclk, pllsclk / sclk_post_div_real);
#endif
	}

	/* Disable the extra precision pixel clock controls since we do not use them. */
	pll->ct.ext_vpll_cntl = aty_ld_pll_ct(EXT_VPLL_CNTL, par);
	pll->ct.ext_vpll_cntl &= ~(EXT_VPLL_EN | EXT_VPLL_VGA_EN | EXT_VPLL_INSYNC);

	return 0;
}

static void aty_resume_pll_ct(const struct fb_info *info,
			      union aty_pll *pll)
{
	struct atyfb_par *par = info->par;

	if (par->mclk_per != par->xclk_per) {
		int i;
		/*
		* This disables the sclk, crashes the computer as reported:
		* aty_st_pll_ct(SPLL_CNTL2, 3, info);
		*
		* So it seems the sclk must be enabled before it is used;
		* so PLL_GEN_CNTL must be programmed *after* the sclk.
		*/
		aty_st_pll_ct(SCLK_FB_DIV, pll->ct.sclk_fb_div, par);
		aty_st_pll_ct(SPLL_CNTL2, pll->ct.spll_cntl2, par);
		/*
		 * The sclk has been started. However, I believe the first clock
		 * ticks it generates are not very stable. Hope this primitive loop
		 * helps for Rage Mobilities that sometimes crash when
		 * we switch to sclk. (Daniel Mantione, 13-05-2003)
		 */
		for (i=0;i<=0x1ffff;i++);
	}

	aty_st_pll_ct(PLL_REF_DIV, pll->ct.pll_ref_div, par);
	aty_st_pll_ct(PLL_GEN_CNTL, pll->ct.pll_gen_cntl, par);
	aty_st_pll_ct(MCLK_FB_DIV, pll->ct.mclk_fb_div, par);
	aty_st_pll_ct(PLL_EXT_CNTL, pll->ct.pll_ext_cntl, par);
	aty_st_pll_ct(EXT_VPLL_CNTL, pll->ct.ext_vpll_cntl, par);
}

static int dummy(void)
{
	return 0;
}

const struct aty_dac_ops aty_dac_ct = {
	.set_dac	= (void *) dummy,
};

const struct aty_pll_ops aty_pll_ct = {
	.var_to_pll	= aty_var_to_pll_ct,
	.pll_to_var	= aty_pll_to_var_ct,
	.set_pll	= aty_set_pll_ct,
	.get_pll	= aty_get_pll_ct,
	.init_pll	= aty_init_pll_ct,
	.resume_pll	= aty_resume_pll_ct,
};
