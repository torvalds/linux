/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

#include <linux/fb.h>

#include "mdp.h"
#include "msm_fb.h"
#include "mdp4.h"

#ifdef CONFIG_FB_MSM_MDP40
#define LCDC_BASE	0xC0000
#define DTV_BASE	0xD0000
#define DMA_E_BASE      0xB0000
#else
#define LCDC_BASE	0xE0000
#endif

#define DMA_P_BASE      0x90000

extern spinlock_t mdp_spin_lock;
#ifndef CONFIG_FB_MSM_MDP40
extern uint32 mdp_intr_mask;
#endif

int first_pixel_start_x;
int first_pixel_start_y;

int mdp_lcdc_on(struct platform_device *pdev)
{
	int lcdc_width;
	int lcdc_height;
	int lcdc_bpp;
	int lcdc_border_clr;
	int lcdc_underflow_clr;
	int lcdc_hsync_skew;

	int hsync_period;
	int hsync_ctrl;
	int vsync_period;
	int display_hctl;
	int display_v_start;
	int display_v_end;
	int active_hctl;
	int active_h_start;
	int active_h_end;
	int active_v_start;
	int active_v_end;
	int ctrl_polarity;
	int h_back_porch;
	int h_front_porch;
	int v_back_porch;
	int v_front_porch;
	int hsync_pulse_width;
	int vsync_pulse_width;
	int hsync_polarity;
	int vsync_polarity;
	int data_en_polarity;
	int hsync_start_x;
	int hsync_end_x;
	uint8 *buf;
	int bpp;
	uint32 dma2_cfg_reg;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;
	struct msm_fb_data_type *mfd;
	uint32 dma_base;
	uint32 timer_base = LCDC_BASE;
	uint32 block = MDP_DMA2_BLOCK;
	int ret;

	mfd = (struct msm_fb_data_type *)platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	fbi = mfd->fbi;
	var = &fbi->var;

	/* MDP cmd block enable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

	bpp = fbi->var.bits_per_pixel / 8;
	buf = (uint8 *) fbi->fix.smem_start;
	buf += fbi->var.xoffset * bpp + fbi->var.yoffset * fbi->fix.line_length;

	dma2_cfg_reg = DMA_PACK_ALIGN_LSB | DMA_DITHER_EN | DMA_OUT_SEL_LCDC;

	if (mfd->fb_imgType == MDP_BGR_565)
		dma2_cfg_reg |= DMA_PACK_PATTERN_BGR;
	else
		dma2_cfg_reg |= DMA_PACK_PATTERN_RGB;

	if (bpp == 2)
		dma2_cfg_reg |= DMA_IBUF_FORMAT_RGB565;
	else if (bpp == 3)
		dma2_cfg_reg |= DMA_IBUF_FORMAT_RGB888;
	else
		dma2_cfg_reg |= DMA_IBUF_FORMAT_xRGB8888_OR_ARGB8888;

	switch (mfd->panel_info.bpp) {
	case 24:
		dma2_cfg_reg |= DMA_DSTC0G_8BITS |
		    DMA_DSTC1B_8BITS | DMA_DSTC2R_8BITS;
		break;

	case 18:
		dma2_cfg_reg |= DMA_DSTC0G_6BITS |
		    DMA_DSTC1B_6BITS | DMA_DSTC2R_6BITS;
		break;

	case 16:
		dma2_cfg_reg |= DMA_DSTC0G_6BITS |
		    DMA_DSTC1B_5BITS | DMA_DSTC2R_5BITS;
		break;

	default:
		printk(KERN_ERR "mdp lcdc can't support format %d bpp!\n",
		       mfd->panel_info.bpp);
		return -ENODEV;
	}

	/* DMA register config */

	dma_base = DMA_P_BASE;

#ifdef CONFIG_FB_MSM_MDP40
	if (mfd->panel.type == HDMI_PANEL)
		dma_base = DMA_E_BASE;
#endif

	/* starting address */
	MDP_OUTP(MDP_BASE + dma_base + 0x8, (uint32) buf);
	/* active window width and height */
	MDP_OUTP(MDP_BASE + dma_base + 0x4, ((fbi->var.yres) << 16) |
						(fbi->var.xres));
	/* buffer ystride */
	MDP_OUTP(MDP_BASE + dma_base + 0xc, fbi->fix.line_length);
	/* x/y coordinate = always 0 for lcdc */
	MDP_OUTP(MDP_BASE + dma_base + 0x10, 0);
	/* dma config */
	MDP_OUTP(MDP_BASE + dma_base, dma2_cfg_reg);

	/*
	 * LCDC timing setting
	 */
	h_back_porch = var->left_margin;
	h_front_porch = var->right_margin;
	v_back_porch = var->upper_margin;
	v_front_porch = var->lower_margin;
	hsync_pulse_width = var->hsync_len;
	vsync_pulse_width = var->vsync_len;
	lcdc_border_clr = mfd->panel_info.lcdc.border_clr;
	lcdc_underflow_clr = mfd->panel_info.lcdc.underflow_clr;
	lcdc_hsync_skew = mfd->panel_info.lcdc.hsync_skew;

	lcdc_width = mfd->panel_info.xres;
	lcdc_height = mfd->panel_info.yres;
	lcdc_bpp = mfd->panel_info.bpp;

	hsync_period =
	    hsync_pulse_width + h_back_porch + lcdc_width + h_front_porch;
	hsync_ctrl = (hsync_period << 16) | hsync_pulse_width;
	hsync_start_x = hsync_pulse_width + h_back_porch;
	hsync_end_x = hsync_period - h_front_porch - 1;
	display_hctl = (hsync_end_x << 16) | hsync_start_x;

	vsync_period =
	    (vsync_pulse_width + v_back_porch + lcdc_height +
	     v_front_porch) * hsync_period;
	display_v_start =
	    (vsync_pulse_width + v_back_porch) * hsync_period + lcdc_hsync_skew;
	display_v_end =
	    vsync_period - (v_front_porch * hsync_period) + lcdc_hsync_skew - 1;

	if (lcdc_width != var->xres) {
		active_h_start = hsync_start_x + first_pixel_start_x;
		active_h_end = active_h_start + var->xres - 1;
		active_hctl =
		    ACTIVE_START_X_EN | (active_h_end << 16) | active_h_start;
	} else {
		active_hctl = 0;
	}

	if (lcdc_height != var->yres) {
		active_v_start =
		    display_v_start + first_pixel_start_y * hsync_period;
		active_v_end = active_v_start + (var->yres) * hsync_period - 1;
		active_v_start |= ACTIVE_START_Y_EN;
	} else {
		active_v_start = 0;
		active_v_end = 0;
	}


#ifdef CONFIG_FB_MSM_MDP40
	if (mfd->panel.type == HDMI_PANEL) {
		block = MDP_DMA_E_BLOCK;
		timer_base = DTV_BASE;
		hsync_polarity = 0;
		vsync_polarity = 0;
	} else {
		hsync_polarity = 1;
		vsync_polarity = 1;
	}

	lcdc_underflow_clr |= 0x80000000;	/* enable recovery */
#else
	hsync_polarity = 0;
	vsync_polarity = 0;
#endif
	data_en_polarity = 0;

	ctrl_polarity =
	    (data_en_polarity << 2) | (vsync_polarity << 1) | (hsync_polarity);

	MDP_OUTP(MDP_BASE + timer_base + 0x4, hsync_ctrl);
	MDP_OUTP(MDP_BASE + timer_base + 0x8, vsync_period);
	MDP_OUTP(MDP_BASE + timer_base + 0xc, vsync_pulse_width * hsync_period);
	if (timer_base == LCDC_BASE) {
		MDP_OUTP(MDP_BASE + timer_base + 0x10, display_hctl);
		MDP_OUTP(MDP_BASE + timer_base + 0x14, display_v_start);
		MDP_OUTP(MDP_BASE + timer_base + 0x18, display_v_end);
		MDP_OUTP(MDP_BASE + timer_base + 0x28, lcdc_border_clr);
		MDP_OUTP(MDP_BASE + timer_base + 0x2c, lcdc_underflow_clr);
		MDP_OUTP(MDP_BASE + timer_base + 0x30, lcdc_hsync_skew);
		MDP_OUTP(MDP_BASE + timer_base + 0x38, ctrl_polarity);
		MDP_OUTP(MDP_BASE + timer_base + 0x1c, active_hctl);
		MDP_OUTP(MDP_BASE + timer_base + 0x20, active_v_start);
		MDP_OUTP(MDP_BASE + timer_base + 0x24, active_v_end);
	} else {
		MDP_OUTP(MDP_BASE + timer_base + 0x18, display_hctl);
		MDP_OUTP(MDP_BASE + timer_base + 0x1c, display_v_start);
		MDP_OUTP(MDP_BASE + timer_base + 0x20, display_v_end);
		MDP_OUTP(MDP_BASE + timer_base + 0x40, lcdc_border_clr);
		MDP_OUTP(MDP_BASE + timer_base + 0x44, lcdc_underflow_clr);
		MDP_OUTP(MDP_BASE + timer_base + 0x48, lcdc_hsync_skew);
		MDP_OUTP(MDP_BASE + timer_base + 0x50, ctrl_polarity);
		MDP_OUTP(MDP_BASE + timer_base + 0x2c, active_hctl);
		MDP_OUTP(MDP_BASE + timer_base + 0x30, active_v_start);
		MDP_OUTP(MDP_BASE + timer_base + 0x38, active_v_end);
	}

	ret = panel_next_on(pdev);
	if (ret == 0) {
		/* enable LCDC block */
		MDP_OUTP(MDP_BASE + timer_base, 1);
		mdp_pipe_ctrl(block, MDP_BLOCK_POWER_ON, FALSE);
	}
	/* MDP cmd block disable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	return ret;
}

int mdp_lcdc_off(struct platform_device *pdev)
{
	int ret = 0;
	struct msm_fb_data_type *mfd;
	uint32 timer_base = LCDC_BASE;
	uint32 block = MDP_DMA2_BLOCK;

	mfd = (struct msm_fb_data_type *)platform_get_drvdata(pdev);

#ifdef CONFIG_FB_MSM_MDP40
	if (mfd->panel.type == HDMI_PANEL) {
		block = MDP_DMA_E_BLOCK;
		timer_base = DTV_BASE;
	}
#endif

	/* MDP cmd block enable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	MDP_OUTP(MDP_BASE + timer_base, 0);
	/* MDP cmd block disable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	mdp_pipe_ctrl(block, MDP_BLOCK_POWER_OFF, FALSE);

	ret = panel_next_off(pdev);

	/* delay to make sure the last frame finishes */
	mdelay(100);

	return ret;
}

void mdp_lcdc_update(struct msm_fb_data_type *mfd)
{
	struct fb_info *fbi = mfd->fbi;
	uint8 *buf;
	int bpp;
	unsigned long flag;
	uint32 dma_base;
	int irq_block = MDP_DMA2_TERM;
#ifdef CONFIG_FB_MSM_MDP40
	int intr = INTR_DMA_P_DONE;
#endif

	if (!mfd->panel_power_on)
		return;

	/* no need to power on cmd block since it's lcdc mode */

	if (!mfd->ibuf.visible_swapped) {
		bpp = fbi->var.bits_per_pixel / 8;
		buf = (uint8 *) fbi->fix.smem_start;
		buf += fbi->var.xoffset * bpp +
		fbi->var.yoffset * fbi->fix.line_length;
	} else {
		/* we've done something to update the pointer. */
		bpp =  mfd->ibuf.bpp;
		buf = mfd->ibuf.buf;
	}

	dma_base = DMA_P_BASE;

#ifdef CONFIG_FB_MSM_MDP40
	if (mfd->panel.type == HDMI_PANEL) {
		intr = INTR_DMA_E_DONE;
		irq_block = MDP_DMA_E_TERM;
		dma_base = DMA_E_BASE;
	}
#endif

	/* starting address */
	MDP_OUTP(MDP_BASE + dma_base + 0x8, (uint32) buf);

	/* enable LCDC irq */
	spin_lock_irqsave(&mdp_spin_lock, flag);
	mdp_enable_irq(irq_block);
	INIT_COMPLETION(mfd->dma->comp);
	mfd->dma->waiting = TRUE;
#ifdef CONFIG_FB_MSM_MDP40
	outp32(MDP_INTR_CLEAR, intr);
	mdp_intr_mask |= intr;
	outp32(MDP_INTR_ENABLE, mdp_intr_mask);
#else
	outp32(MDP_INTR_CLEAR, LCDC_FRAME_START);
	mdp_intr_mask |= LCDC_FRAME_START;
	outp32(MDP_INTR_ENABLE, mdp_intr_mask);
#endif
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	if (mfd->ibuf.vsync_enable)
		wait_for_completion_killable(&mfd->dma->comp);
	mdp_disable_irq(irq_block);
}
