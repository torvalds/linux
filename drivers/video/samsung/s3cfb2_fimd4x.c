/* linux/drivers/video/samsung/s3cfb2_fimd4x.c
 *
 * Register interface file for Samsung Display Controller (FIMD) driver
 *
 * Jinsung Yang, Copyright (c) 2009 Samsung Electronics
 *	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <mach/map.h>
#include <plat/clock.h>
#include <plat/fb.h>
#include <plat/regs-fb.h>

#include "s3cfb2.h"

void s3cfb_check_line_count(struct s3cfb_global *ctrl)
{
	int timeout = 30 * 5300;
	int i = 0;

	do {
		if (!(readl(ctrl->regs + S3C_VIDCON1) & 0x7ff0000))
			break;
		i++;
	} while (i < timeout);

	if (i == timeout) {
		dev_err(ctrl->dev, "line count mismatch\n");
		s3cfb_display_on(ctrl);
	}
}

int s3cfb_set_output(struct s3cfb_global *ctrl)
{
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_VIDCON0);
	cfg &= ~S3C_VIDCON0_VIDOUT_MASK;

	if (ctrl->output == OUTPUT_RGB)
		cfg |= S3C_VIDCON0_VIDOUT_RGB;
	else if (ctrl->output == OUTPUT_ITU)
		cfg |= S3C_VIDCON0_VIDOUT_ITU;
	else if (ctrl->output == OUTPUT_I80LDI0)
		cfg |= S3C_VIDCON0_VIDOUT_I80LDI0;
	else if (ctrl->output == OUTPUT_I80LDI1)
		cfg |= S3C_VIDCON0_VIDOUT_I80LDI1;
	else {
		dev_err(ctrl->dev, "invalid output type: %d\n", ctrl->output);
		return -EINVAL;
	}

	writel(cfg, ctrl->regs + S3C_VIDCON0);

	return 0;
}

int s3cfb_set_display_mode(struct s3cfb_global *ctrl)
{
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_VIDCON0);
	cfg &= ~S3C_VIDCON0_PNRMODE_MASK;
	cfg |= (ctrl->rgb_mode << S3C_VIDCON0_PNRMODE_SHIFT);
	writel(cfg, ctrl->regs + S3C_VIDCON0);

	return 0;
}

int s3cfb_display_on(struct s3cfb_global *ctrl)
{
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_VIDCON0);
	cfg |= (S3C_VIDCON0_ENVID_ENABLE | S3C_VIDCON0_ENVID_F_ENABLE);
	writel(cfg, ctrl->regs + S3C_VIDCON0);

	dev_dbg(ctrl->dev, "global display is on\n");

	return 0;
}

int s3cfb_display_off(struct s3cfb_global *ctrl)
{
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_VIDCON0);
	cfg &= ~S3C_VIDCON0_ENVID_ENABLE;
	writel(cfg, ctrl->regs + S3C_VIDCON0);

	cfg &= ~S3C_VIDCON0_ENVID_F_ENABLE;
	writel(cfg, ctrl->regs + S3C_VIDCON0);

	dev_dbg(ctrl->dev, "global display is off\n");

	return 0;
}

int s3cfb_frame_off(struct s3cfb_global *ctrl)
{
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_VIDCON0);
	cfg &= ~S3C_VIDCON0_ENVID_F_ENABLE;
	writel(cfg, ctrl->regs + S3C_VIDCON0);

	dev_dbg(ctrl->dev, "current frame display is off\n");

	return 0;
}

int s3cfb_set_clock(struct s3cfb_global *ctrl)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	unsigned int cfg, maxclk, src_clk, vclk, div;

	maxclk = 66 * 1000000;

	/* fixed clock source: hclk */
	cfg = readl(ctrl->regs + S3C_VIDCON0);
	cfg &= ~(S3C_VIDCON0_CLKSEL_MASK | S3C_VIDCON0_CLKVALUP_MASK |
		S3C_VIDCON0_VCLKEN_MASK | S3C_VIDCON0_CLKDIR_MASK);
	cfg |= (S3C_VIDCON0_CLKSEL_HCLK | S3C_VIDCON0_CLKVALUP_ALWAYS |
		S3C_VIDCON0_VCLKEN_NORMAL | S3C_VIDCON0_CLKDIR_DIVIDED);

	src_clk = ctrl->clock->parent->rate;
	vclk = ctrl->fb[pdata->default_win]->var.pixclock;

	if (vclk > maxclk)
		vclk = maxclk;

	div = src_clk / vclk;
	if (src_clk % vclk)
		div++;

	cfg |= S3C_VIDCON0_CLKVAL_F(div - 1);
	writel(cfg, ctrl->regs + S3C_VIDCON0);

	dev_dbg(ctrl->dev, "parent clock: %d, vclk: %d, vclk div: %d\n",
			src_clk, vclk, div);

	return 0;
}

int s3cfb_set_polarity(struct s3cfb_global *ctrl)
{
	struct s3cfb_lcd_polarity *pol;
	unsigned int cfg;

	pol = &ctrl->lcd->polarity;
	cfg = 0;

	if (pol->rise_vclk)
		cfg |= S3C_VIDCON1_IVCLK_RISING_EDGE;

	if (pol->inv_hsync)
		cfg |= S3C_VIDCON1_IHSYNC_INVERT;

	if (pol->inv_vsync)
		cfg |= S3C_VIDCON1_IVSYNC_INVERT;

	if (pol->inv_vden)
		cfg |= S3C_VIDCON1_IVDEN_INVERT;

	writel(cfg, ctrl->regs + S3C_VIDCON1);

	return 0;
}

int s3cfb_set_timing(struct s3cfb_global *ctrl)
{
	struct s3cfb_lcd_timing *time;
	unsigned int cfg;

	time = &ctrl->lcd->timing;
	cfg = 0;

	cfg |= S3C_VIDTCON0_VBPDE(time->v_bpe - 1);
	cfg |= S3C_VIDTCON0_VBPD(time->v_bp - 1);
	cfg |= S3C_VIDTCON0_VFPD(time->v_fp - 1);
	cfg |= S3C_VIDTCON0_VSPW(time->v_sw - 1);

	writel(cfg, ctrl->regs + S3C_VIDTCON0);

	cfg = 0;

	cfg |= S3C_VIDTCON1_VFPDE(time->v_fpe - 1);
	cfg |= S3C_VIDTCON1_HBPD(time->h_bp - 1);
	cfg |= S3C_VIDTCON1_HFPD(time->h_fp - 1);
	cfg |= S3C_VIDTCON1_HSPW(time->h_sw - 1);

	writel(cfg, ctrl->regs + S3C_VIDTCON1);

	return 0;
}

int s3cfb_set_lcd_size(struct s3cfb_global *ctrl)
{
	unsigned int cfg = 0;

	cfg |= S3C_VIDTCON2_HOZVAL(ctrl->lcd->width - 1);
	cfg |= S3C_VIDTCON2_LINEVAL(ctrl->lcd->height - 1);

	writel(cfg, ctrl->regs + S3C_VIDTCON2);

	return 0;
}

int s3cfb_set_global_interrupt(struct s3cfb_global *ctrl, int enable)
{
	unsigned int cfg = 0;

	cfg = readl(ctrl->regs + S3C_VIDINTCON0);
	cfg &= ~(S3C_VIDINTCON0_INTFRMEN_ENABLE |
		S3C_VIDINTCON0_INT_ENABLE);

	if (enable) {
		dev_dbg(ctrl->dev, "video interrupt is on\n");
		cfg |= (S3C_VIDINTCON0_INTFRMEN_ENABLE |
			S3C_VIDINTCON0_INT_ENABLE);
	} else {
		dev_dbg(ctrl->dev, "video interrupt is off\n");
		cfg |= (S3C_VIDINTCON0_INTFRMEN_DISABLE |
			S3C_VIDINTCON0_INT_DISABLE);
	}

	writel(cfg, ctrl->regs + S3C_VIDINTCON0);

	return 0;
}

int s3cfb_set_vsync_interrupt(struct s3cfb_global *ctrl, int enable)
{
	unsigned int cfg = 0;

	cfg = readl(ctrl->regs + S3C_VIDINTCON0);
	cfg &= ~S3C_VIDINTCON0_FRAMESEL0_MASK;

	if (enable) {
		dev_dbg(ctrl->dev, "vsync interrupt is on\n");
		cfg |= S3C_VIDINTCON0_FRAMESEL0_VSYNC;
	} else {
		dev_dbg(ctrl->dev, "vsync interrupt is off\n");
		cfg &= ~S3C_VIDINTCON0_FRAMESEL0_VSYNC;
	}

	writel(cfg, ctrl->regs + S3C_VIDINTCON0);

	return 0;
}

#ifdef CONFIG_FB_S3C_V2_TRACE_UNDERRUN
int s3cfb_set_fifo_interrupt(struct s3cfb_global *ctrl, int enable)
{
	unsigned int cfg = 0;

	cfg = readl(ctrl->regs + S3C_VIDINTCON0);

	cfg &= ~(S3C_VIDINTCON0_FIFOSEL_MASK | S3C_VIDINTCON0_FIFOLEVEL_MASK);
	cfg |= (S3C_VIDINTCON0_FIFOSEL_ALL | S3C_VIDINTCON0_FIFOLEVEL_EMPTY);

	if (enable) {
		dev_dbg(ctrl->dev, "fifo interrupt is on\n");
		cfg |= (S3C_VIDINTCON0_INTFIFO_ENABLE |
			S3C_VIDINTCON0_INT_ENABLE);
	} else {
		dev_dbg(ctrl->dev, "fifo interrupt is off\n");
		cfg &= ~(S3C_VIDINTCON0_INTFIFO_ENABLE |
			S3C_VIDINTCON0_INT_ENABLE);
	}

	writel(cfg, ctrl->regs + S3C_VIDINTCON0);

	return 0;
}
#endif

int s3cfb_clear_interrupt(struct s3cfb_global *ctrl)
{
	unsigned int cfg = 0;

	cfg = readl(ctrl->regs + S3C_VIDINTCON1);

	if (cfg & S3C_VIDINTCON1_INTFIFOPEND)
		info("fifo underrun occur\n");

	cfg |= (S3C_VIDINTCON1_INTVPPEND | S3C_VIDINTCON1_INTI80PEND |
		S3C_VIDINTCON1_INTFRMPEND | S3C_VIDINTCON1_INTFIFOPEND);

	writel(cfg, ctrl->regs + S3C_VIDINTCON1);

	return 0;
}

int s3cfb_window_on(struct s3cfb_global *ctrl, int id)
{
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_WINCON(id));
	cfg |= S3C_WINCON_ENWIN_ENABLE;
	writel(cfg, ctrl->regs + S3C_WINCON(id));

	dev_dbg(ctrl->dev, "[fb%d] turn on\n", id);

	return 0;
}

int s3cfb_window_off(struct s3cfb_global *ctrl, int id)
{
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_WINCON(id));
	cfg &= ~S3C_WINCON_ENWIN_ENABLE;
	writel(cfg, ctrl->regs + S3C_WINCON(id));

	dev_dbg(ctrl->dev, "[fb%d] turn off\n", id);

	return 0;
}

int s3cfb_set_window_control(struct s3cfb_global *ctrl, int id)
{
	struct s3c_platform_fb *pdata = to_fb_plat(ctrl->dev);
	struct fb_info *fb = ctrl->fb[id];
	struct fb_var_screeninfo *var = &fb->var;
	struct s3cfb_window *win = fb->par;
	unsigned int cfg;

	cfg = readl(ctrl->regs + S3C_WINCON(id));

	cfg &= ~(S3C_WINCON_BITSWP_ENABLE | S3C_WINCON_BYTESWP_ENABLE |
		S3C_WINCON_HAWSWP_ENABLE | S3C_WINCON_WSWP_ENABLE |
		S3C_WINCON_BURSTLEN_MASK | S3C_WINCON_BPPMODE_MASK |
		S3C_WINCON_INRGB_MASK | S3C_WINCON_DATAPATH_MASK);

	if (win->path == DATA_PATH_FIFO) {
		dev_dbg(ctrl->dev, "[fb%d] data path: fifo\n", id);

		cfg |= S3C_WINCON_DATAPATH_LOCAL;
		cfg |= S3C_WINCON_INRGB_RGB;
		cfg |= S3C_WINCON_BPPMODE_24BPP_888;

		if (id == 1) {
			cfg &= ~S3C_WINCON1_LOCALSEL_MASK;

			if (win->local_channel == 0)
				cfg |= S3C_WINCON1_LOCALSEL_TV;
			else
				cfg |= S3C_WINCON1_LOCALSEL_FIMC1;
		}
	} else {
		dev_dbg(ctrl->dev, "[fb%d] data path: dma\n", id);

		cfg |= S3C_WINCON_DATAPATH_DMA;

		if (fb->var.bits_per_pixel == 16 && pdata->swap & FB_SWAP_HWORD)
			cfg |= S3C_WINCON_HAWSWP_ENABLE;

		if (fb->var.bits_per_pixel == 32 && pdata->swap & FB_SWAP_WORD)
			cfg |= S3C_WINCON_WSWP_ENABLE;

		/* dma burst */
		if (win->dma_burst == 4)
			cfg |= S3C_WINCON_BURSTLEN_4WORD;
		else if (win->dma_burst == 8)
			cfg |= S3C_WINCON_BURSTLEN_8WORD;
		else
			cfg |= S3C_WINCON_BURSTLEN_16WORD;

		/* bpp mode set */
		switch (fb->var.bits_per_pixel) {
		case 16:
			if (var->transp.length == 1) {
				dev_dbg(ctrl->dev,
					"[fb%d] bpp mode: A1-R5-G5-B5\n", id);
				cfg |= S3C_WINCON_BPPMODE_16BPP_A555;
			} else if (var->transp.length == 4) {
				dev_dbg(ctrl->dev,
					"[fb%d] bpp mode: A4-R4-G4-B4\n", id);
				cfg |= S3C_WINCON_BPPMODE_16BPP_A444;
			} else {
				dev_dbg(ctrl->dev,
					"[fb%d] bpp mode: R5-G6-B5\n", id);
				cfg |= S3C_WINCON_BPPMODE_16BPP_565;
			}
			break;

		case 24: /* packed 24 bpp: nothing to do for 4.x fimd */
			break;

		case 32:
			if (var->transp.length == 0) {
				dev_dbg(ctrl->dev,
					"[fb%d] bpp mode: R8-G8-B8\n", id);
				cfg |= S3C_WINCON_BPPMODE_24BPP_888;
			} else {
				dev_dbg(ctrl->dev,
					"[fb%d] bpp mode: A4-R8-G8-B8\n", id);
				cfg |= S3C_WINCON_BPPMODE_28BPP_A888;
			}
			break;
		}
	}

	writel(cfg, ctrl->regs + S3C_WINCON(id));

	return 0;
}

int s3cfb_set_buffer_address(struct s3cfb_global *ctrl, int id)
{
	struct fb_fix_screeninfo *fix = &ctrl->fb[id]->fix;
	struct fb_var_screeninfo *var = &ctrl->fb[id]->var;
	dma_addr_t start_addr = 0, end_addr = 0;

	if (fix->smem_start) {
		start_addr = fix->smem_start + (var->xres_virtual *
				(var->bits_per_pixel / 8) * var->yoffset);

		end_addr = start_addr + (var->xres_virtual *
				(var->bits_per_pixel / 8) * var->yres);
	}

	writel(start_addr, ctrl->regs + S3C_VIDADDR_START0(id));
	writel(end_addr, ctrl->regs + S3C_VIDADDR_END0(id));

	dev_dbg(ctrl->dev, "[fb%d] start_addr: 0x%08x, end_addr: 0x%08x\n",
		id, start_addr, end_addr);

	return 0;
}

int s3cfb_set_alpha_blending(struct s3cfb_global *ctrl, int id)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;
	struct s3cfb_alpha *alpha = &win->alpha;
	unsigned int avalue = 0, cfg;

	if (id == 0) {
		dev_err(ctrl->dev, "[fb%d] does not support alpha blending\n", id);
		return -EINVAL;
	}

	cfg = readl(ctrl->regs + S3C_WINCON(id));
	cfg &= ~(S3C_WINCON_BLD_MASK | S3C_WINCON_ALPHA_SEL_MASK);

	if (alpha->mode == PIXEL_BLENDING) {
		dev_dbg(ctrl->dev, "[fb%d] alpha mode: pixel blending\n", id);

		/* fixing to DATA[31:24] for alpha value */
		cfg |= (S3C_WINCON_BLD_PIXEL | S3C_WINCON_ALPHA1_SEL);
	} else {
		dev_dbg(ctrl->dev, "[fb%d] alpha mode: plane %d blending\n",
			id, alpha->channel);

		cfg |= S3C_WINCON_BLD_PLANE;

		if (alpha->channel == 0) {
			cfg |= S3C_WINCON_ALPHA0_SEL;
			avalue = (alpha->value << S3C_VIDOSD_ALPHA0_SHIFT);
		} else {
			cfg |= S3C_WINCON_ALPHA1_SEL;
			avalue = (alpha->value << S3C_VIDOSD_ALPHA1_SHIFT);
		}
	}

	writel(cfg, ctrl->regs + S3C_WINCON(id));
	writel(avalue, ctrl->regs + S3C_VIDOSD_C(id));

	return 0;
}

int s3cfb_set_window_position(struct s3cfb_global *ctrl, int id)
{
	struct fb_var_screeninfo *var = &ctrl->fb[id]->var;
	struct s3cfb_window *win = ctrl->fb[id]->par;
	unsigned int cfg;

	cfg = S3C_VIDOSD_LEFT_X(win->x) | S3C_VIDOSD_TOP_Y(win->y);
	writel(cfg, ctrl->regs + S3C_VIDOSD_A(id));

	cfg = S3C_VIDOSD_RIGHT_X(win->x + var->xres - 1) |
		S3C_VIDOSD_BOTTOM_Y(win->y + var->yres - 1);

	writel(cfg, ctrl->regs + S3C_VIDOSD_B(id));

	dev_dbg(ctrl->dev, "[fb%d] offset: (%d, %d, %d, %d)\n", id,
		win->x, win->y, win->x + var->xres - 1, win->y + var->yres - 1);

	return 0;
}

int s3cfb_set_window_size(struct s3cfb_global *ctrl, int id)
{
	struct fb_var_screeninfo *var = &ctrl->fb[id]->var;
	unsigned int cfg;

	if (id > 2)
		return 0;

	cfg = S3C_VIDOSD_SIZE(var->xres * var->yres);

	if (id == 0)
		writel(cfg, ctrl->regs + S3C_VIDOSD_C(id));
	else
		writel(cfg, ctrl->regs + S3C_VIDOSD_D(id));

	dev_dbg(ctrl->dev, "[fb%d] resolution: %d x %d\n", id,
		var->xres, var->yres);

	return 0;
}

int s3cfb_set_buffer_size(struct s3cfb_global *ctrl, int id)
{
	struct fb_fix_screeninfo *fix = &ctrl->fb[id]->fix;
	unsigned int cfg = 0;

	cfg = S3C_VIDADDR_PAGEWIDTH(fix->line_length);
	writel(cfg, ctrl->regs + S3C_VIDADDR_SIZE(id));

	return 0;
}

int s3cfb_set_chroma_key(struct s3cfb_global *ctrl, int id)
{
	struct s3cfb_window *win = ctrl->fb[id]->par;
	struct s3cfb_chroma *chroma = &win->chroma;
	unsigned int cfg = 0;

	if (id == 0) {
		dev_err(ctrl->dev, "[fb%d] does not support chroma key\n", id);
		return -EINVAL;
	}

	cfg = (S3C_KEYCON0_KEYBLEN_DISABLE | S3C_KEYCON0_DIRCON_MATCH_FG);

	if (chroma->enabled)
		cfg |= S3C_KEYCON0_KEY_ENABLE;

	writel(cfg, ctrl->regs + S3C_KEYCON(id));

	cfg = S3C_KEYCON1_COLVAL(chroma->key);
	writel(cfg, ctrl->regs + S3C_KEYVAL(id));

	dev_dbg(ctrl->dev, "[fb%d] chroma key: 0x%08x, %s\n", id, cfg,
		chroma->enabled ? "enabled" : "disabled");

	return 0;
}
