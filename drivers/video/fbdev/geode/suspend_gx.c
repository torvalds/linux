// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2007 Advanced Micro Devices, Inc.
 *   Copyright (C) 2008 Andres Salomon <dilinger@debian.org>
 */
#include <linux/fb.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <linux/cs5535.h>
#include <asm/delay.h>

#include "gxfb.h"

#ifdef CONFIG_PM

static void gx_save_regs(struct gxfb_par *par)
{
	int i;

	/* wait for the BLT engine to stop being busy */
	do {
		i = read_gp(par, GP_BLT_STATUS);
	} while (i & (GP_BLT_STATUS_BLT_PENDING | GP_BLT_STATUS_BLT_BUSY));

	/* save MSRs */
	rdmsrl(MSR_GX_MSR_PADSEL, par->msr.padsel);
	rdmsrl(MSR_GLCP_DOTPLL, par->msr.dotpll);

	write_dc(par, DC_UNLOCK, DC_UNLOCK_UNLOCK);

	/* save registers */
	memcpy(par->gp, par->gp_regs, sizeof(par->gp));
	memcpy(par->dc, par->dc_regs, sizeof(par->dc));
	memcpy(par->vp, par->vid_regs, sizeof(par->vp));
	memcpy(par->fp, par->vid_regs + VP_FP_START, sizeof(par->fp));

	/* save the palette */
	write_dc(par, DC_PAL_ADDRESS, 0);
	for (i = 0; i < ARRAY_SIZE(par->pal); i++)
		par->pal[i] = read_dc(par, DC_PAL_DATA);
}

static void gx_set_dotpll(uint32_t dotpll_hi)
{
	uint32_t dotpll_lo;
	int i;

	rdmsrl(MSR_GLCP_DOTPLL, dotpll_lo);
	dotpll_lo |= MSR_GLCP_DOTPLL_DOTRESET;
	dotpll_lo &= ~MSR_GLCP_DOTPLL_BYPASS;
	wrmsr(MSR_GLCP_DOTPLL, dotpll_lo, dotpll_hi);

	/* wait for the PLL to lock */
	for (i = 0; i < 200; i++) {
		rdmsrl(MSR_GLCP_DOTPLL, dotpll_lo);
		if (dotpll_lo & MSR_GLCP_DOTPLL_LOCK)
			break;
		udelay(1);
	}

	/* PLL set, unlock */
	dotpll_lo &= ~MSR_GLCP_DOTPLL_DOTRESET;
	wrmsr(MSR_GLCP_DOTPLL, dotpll_lo, dotpll_hi);
}

static void gx_restore_gfx_proc(struct gxfb_par *par)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(par->gp); i++) {
		switch (i) {
		case GP_VECTOR_MODE:
		case GP_BLT_MODE:
		case GP_BLT_STATUS:
		case GP_HST_SRC:
			/* don't restore these registers */
			break;
		default:
			write_gp(par, i, par->gp[i]);
		}
	}
}

static void gx_restore_display_ctlr(struct gxfb_par *par)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(par->dc); i++) {
		switch (i) {
		case DC_UNLOCK:
			/* unlock the DC; runs first */
			write_dc(par, DC_UNLOCK, DC_UNLOCK_UNLOCK);
			break;

		case DC_GENERAL_CFG:
			/* write without the enables */
			write_dc(par, i, par->dc[i] & ~(DC_GENERAL_CFG_VIDE |
					DC_GENERAL_CFG_ICNE |
					DC_GENERAL_CFG_CURE |
					DC_GENERAL_CFG_DFLE));
			break;

		case DC_DISPLAY_CFG:
			/* write without the enables */
			write_dc(par, i, par->dc[i] & ~(DC_DISPLAY_CFG_VDEN |
					DC_DISPLAY_CFG_GDEN |
					DC_DISPLAY_CFG_TGEN));
			break;

		case DC_RSVD_0:
		case DC_RSVD_1:
		case DC_RSVD_2:
		case DC_RSVD_3:
		case DC_RSVD_4:
		case DC_LINE_CNT:
		case DC_PAL_ADDRESS:
		case DC_PAL_DATA:
		case DC_DFIFO_DIAG:
		case DC_CFIFO_DIAG:
		case DC_RSVD_5:
			/* don't restore these registers */
			break;
		default:
			write_dc(par, i, par->dc[i]);
		}
	}

	/* restore the palette */
	write_dc(par, DC_PAL_ADDRESS, 0);
	for (i = 0; i < ARRAY_SIZE(par->pal); i++)
		write_dc(par, DC_PAL_DATA, par->pal[i]);
}

static void gx_restore_video_proc(struct gxfb_par *par)
{
	int i;

	wrmsrl(MSR_GX_MSR_PADSEL, par->msr.padsel);

	for (i = 0; i < ARRAY_SIZE(par->vp); i++) {
		switch (i) {
		case VP_VCFG:
			/* don't enable video yet */
			write_vp(par, i, par->vp[i] & ~VP_VCFG_VID_EN);
			break;

		case VP_DCFG:
			/* don't enable CRT yet */
			write_vp(par, i, par->vp[i] &
					~(VP_DCFG_DAC_BL_EN | VP_DCFG_VSYNC_EN |
					VP_DCFG_HSYNC_EN | VP_DCFG_CRT_EN));
			break;

		case VP_GAR:
		case VP_GDR:
		case VP_RSVD_0:
		case VP_RSVD_1:
		case VP_RSVD_2:
		case VP_RSVD_3:
		case VP_CRC32:
		case VP_AWT:
		case VP_VTM:
			/* don't restore these registers */
			break;
		default:
			write_vp(par, i, par->vp[i]);
		}
	}
}

static void gx_restore_regs(struct gxfb_par *par)
{
	int i;

	gx_set_dotpll((uint32_t) (par->msr.dotpll >> 32));
	gx_restore_gfx_proc(par);
	gx_restore_display_ctlr(par);
	gx_restore_video_proc(par);

	/* Flat Panel */
	for (i = 0; i < ARRAY_SIZE(par->fp); i++) {
		if (i != FP_PM && i != FP_RSVD_0)
			write_fp(par, i, par->fp[i]);
	}
}

static void gx_disable_graphics(struct gxfb_par *par)
{
	/* shut down the engine */
	write_vp(par, VP_VCFG, par->vp[VP_VCFG] & ~VP_VCFG_VID_EN);
	write_vp(par, VP_DCFG, par->vp[VP_DCFG] & ~(VP_DCFG_DAC_BL_EN |
			VP_DCFG_VSYNC_EN | VP_DCFG_HSYNC_EN | VP_DCFG_CRT_EN));

	/* turn off the flat panel */
	write_fp(par, FP_PM, par->fp[FP_PM] & ~FP_PM_P);


	/* turn off display */
	write_dc(par, DC_UNLOCK, DC_UNLOCK_UNLOCK);
	write_dc(par, DC_GENERAL_CFG, par->dc[DC_GENERAL_CFG] &
			~(DC_GENERAL_CFG_VIDE | DC_GENERAL_CFG_ICNE |
			DC_GENERAL_CFG_CURE | DC_GENERAL_CFG_DFLE));
	write_dc(par, DC_DISPLAY_CFG, par->dc[DC_DISPLAY_CFG] &
			~(DC_DISPLAY_CFG_VDEN | DC_DISPLAY_CFG_GDEN |
			DC_DISPLAY_CFG_TGEN));
	write_dc(par, DC_UNLOCK, DC_UNLOCK_LOCK);
}

static void gx_enable_graphics(struct gxfb_par *par)
{
	uint32_t fp;

	fp = read_fp(par, FP_PM);
	if (par->fp[FP_PM] & FP_PM_P) {
		/* power on the panel if not already power{ed,ing} on */
		if (!(fp & (FP_PM_PANEL_ON|FP_PM_PANEL_PWR_UP)))
			write_fp(par, FP_PM, par->fp[FP_PM]);
	} else {
		/* power down the panel if not already power{ed,ing} down */
		if (!(fp & (FP_PM_PANEL_OFF|FP_PM_PANEL_PWR_DOWN)))
			write_fp(par, FP_PM, par->fp[FP_PM]);
	}

	/* turn everything on */
	write_vp(par, VP_VCFG, par->vp[VP_VCFG]);
	write_vp(par, VP_DCFG, par->vp[VP_DCFG]);
	write_dc(par, DC_DISPLAY_CFG, par->dc[DC_DISPLAY_CFG]);
	/* do this last; it will enable the FIFO load */
	write_dc(par, DC_GENERAL_CFG, par->dc[DC_GENERAL_CFG]);

	/* lock the door behind us */
	write_dc(par, DC_UNLOCK, DC_UNLOCK_LOCK);
}

int gx_powerdown(struct fb_info *info)
{
	struct gxfb_par *par = info->par;

	if (par->powered_down)
		return 0;

	gx_save_regs(par);
	gx_disable_graphics(par);

	par->powered_down = 1;
	return 0;
}

int gx_powerup(struct fb_info *info)
{
	struct gxfb_par *par = info->par;

	if (!par->powered_down)
		return 0;

	gx_restore_regs(par);
	gx_enable_graphics(par);

	par->powered_down  = 0;
	return 0;
}

#endif
