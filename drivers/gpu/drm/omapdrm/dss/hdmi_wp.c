/*
 * HDMI wrapper
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define DSS_SUBSYS_NAME "HDMIWP"

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#include "omapdss.h"
#include "dss.h"
#include "hdmi.h"

void hdmi_wp_dump(struct hdmi_wp_data *wp, struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-35s %08x\n", #r, hdmi_read_reg(wp->base, r))

	DUMPREG(HDMI_WP_REVISION);
	DUMPREG(HDMI_WP_SYSCONFIG);
	DUMPREG(HDMI_WP_IRQSTATUS_RAW);
	DUMPREG(HDMI_WP_IRQSTATUS);
	DUMPREG(HDMI_WP_IRQENABLE_SET);
	DUMPREG(HDMI_WP_IRQENABLE_CLR);
	DUMPREG(HDMI_WP_IRQWAKEEN);
	DUMPREG(HDMI_WP_PWR_CTRL);
	DUMPREG(HDMI_WP_DEBOUNCE);
	DUMPREG(HDMI_WP_VIDEO_CFG);
	DUMPREG(HDMI_WP_VIDEO_SIZE);
	DUMPREG(HDMI_WP_VIDEO_TIMING_H);
	DUMPREG(HDMI_WP_VIDEO_TIMING_V);
	DUMPREG(HDMI_WP_CLK);
	DUMPREG(HDMI_WP_AUDIO_CFG);
	DUMPREG(HDMI_WP_AUDIO_CFG2);
	DUMPREG(HDMI_WP_AUDIO_CTRL);
	DUMPREG(HDMI_WP_AUDIO_DATA);
}

u32 hdmi_wp_get_irqstatus(struct hdmi_wp_data *wp)
{
	return hdmi_read_reg(wp->base, HDMI_WP_IRQSTATUS);
}

void hdmi_wp_set_irqstatus(struct hdmi_wp_data *wp, u32 irqstatus)
{
	hdmi_write_reg(wp->base, HDMI_WP_IRQSTATUS, irqstatus);
	/* flush posted write */
	hdmi_read_reg(wp->base, HDMI_WP_IRQSTATUS);
}

void hdmi_wp_set_irqenable(struct hdmi_wp_data *wp, u32 mask)
{
	hdmi_write_reg(wp->base, HDMI_WP_IRQENABLE_SET, mask);
}

void hdmi_wp_clear_irqenable(struct hdmi_wp_data *wp, u32 mask)
{
	hdmi_write_reg(wp->base, HDMI_WP_IRQENABLE_CLR, mask);
}

/* PHY_PWR_CMD */
int hdmi_wp_set_phy_pwr(struct hdmi_wp_data *wp, enum hdmi_phy_pwr val)
{
	/* Return if already the state */
	if (REG_GET(wp->base, HDMI_WP_PWR_CTRL, 5, 4) == val)
		return 0;

	/* Command for power control of HDMI PHY */
	REG_FLD_MOD(wp->base, HDMI_WP_PWR_CTRL, val, 7, 6);

	/* Status of the power control of HDMI PHY */
	if (hdmi_wait_for_bit_change(wp->base, HDMI_WP_PWR_CTRL, 5, 4, val)
			!= val) {
		DSSERR("Failed to set PHY power mode to %d\n", val);
		return -ETIMEDOUT;
	}

	return 0;
}

/* PLL_PWR_CMD */
int hdmi_wp_set_pll_pwr(struct hdmi_wp_data *wp, enum hdmi_pll_pwr val)
{
	/* Command for power control of HDMI PLL */
	REG_FLD_MOD(wp->base, HDMI_WP_PWR_CTRL, val, 3, 2);

	/* wait till PHY_PWR_STATUS is set */
	if (hdmi_wait_for_bit_change(wp->base, HDMI_WP_PWR_CTRL, 1, 0, val)
			!= val) {
		DSSERR("Failed to set PLL_PWR_STATUS\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int hdmi_wp_video_start(struct hdmi_wp_data *wp)
{
	REG_FLD_MOD(wp->base, HDMI_WP_VIDEO_CFG, true, 31, 31);

	return 0;
}

void hdmi_wp_video_stop(struct hdmi_wp_data *wp)
{
	int i;

	hdmi_write_reg(wp->base, HDMI_WP_IRQSTATUS, HDMI_IRQ_VIDEO_FRAME_DONE);

	REG_FLD_MOD(wp->base, HDMI_WP_VIDEO_CFG, false, 31, 31);

	for (i = 0; i < 50; ++i) {
		u32 v;

		msleep(20);

		v = hdmi_read_reg(wp->base, HDMI_WP_IRQSTATUS_RAW);
		if (v & HDMI_IRQ_VIDEO_FRAME_DONE)
			return;
	}

	DSSERR("no HDMI FRAMEDONE when disabling output\n");
}

void hdmi_wp_video_config_format(struct hdmi_wp_data *wp,
		struct hdmi_video_format *video_fmt)
{
	u32 l = 0;

	REG_FLD_MOD(wp->base, HDMI_WP_VIDEO_CFG, video_fmt->packing_mode,
		10, 8);

	l |= FLD_VAL(video_fmt->y_res, 31, 16);
	l |= FLD_VAL(video_fmt->x_res, 15, 0);
	hdmi_write_reg(wp->base, HDMI_WP_VIDEO_SIZE, l);
}

void hdmi_wp_video_config_interface(struct hdmi_wp_data *wp,
				    struct videomode *vm)
{
	u32 r;
	bool vsync_inv, hsync_inv;
	DSSDBG("Enter hdmi_wp_video_config_interface\n");

	vsync_inv = !!(vm->flags & DISPLAY_FLAGS_VSYNC_LOW);
	hsync_inv = !!(vm->flags & DISPLAY_FLAGS_HSYNC_LOW);

	r = hdmi_read_reg(wp->base, HDMI_WP_VIDEO_CFG);
	r = FLD_MOD(r, 1, 7, 7);	/* VSYNC_POL to dispc active high */
	r = FLD_MOD(r, 1, 6, 6);	/* HSYNC_POL to dispc active high */
	r = FLD_MOD(r, vsync_inv, 5, 5);	/* CORE_VSYNC_INV */
	r = FLD_MOD(r, hsync_inv, 4, 4);	/* CORE_HSYNC_INV */
	r = FLD_MOD(r, !!(vm->flags & DISPLAY_FLAGS_INTERLACED), 3, 3);
	r = FLD_MOD(r, 1, 1, 0); /* HDMI_TIMING_MASTER_24BIT */
	hdmi_write_reg(wp->base, HDMI_WP_VIDEO_CFG, r);
}

void hdmi_wp_video_config_timing(struct hdmi_wp_data *wp,
				 struct videomode *vm)
{
	u32 timing_h = 0;
	u32 timing_v = 0;
	unsigned int hsync_len_offset = 1;

	DSSDBG("Enter hdmi_wp_video_config_timing\n");

	/*
	 * On OMAP4 and OMAP5 ES1 the HSW field is programmed as is. On OMAP5
	 * ES2+ (including DRA7/AM5 SoCs) HSW field is programmed to hsync_len-1.
	 * However, we don't support OMAP5 ES1 at all, so we can just check for
	 * OMAP4 here.
	 */
	if (wp->version == 4)
		hsync_len_offset = 0;

	timing_h |= FLD_VAL(vm->hback_porch, 31, 20);
	timing_h |= FLD_VAL(vm->hfront_porch, 19, 8);
	timing_h |= FLD_VAL(vm->hsync_len - hsync_len_offset, 7, 0);
	hdmi_write_reg(wp->base, HDMI_WP_VIDEO_TIMING_H, timing_h);

	timing_v |= FLD_VAL(vm->vback_porch, 31, 20);
	timing_v |= FLD_VAL(vm->vfront_porch, 19, 8);
	timing_v |= FLD_VAL(vm->vsync_len, 7, 0);
	hdmi_write_reg(wp->base, HDMI_WP_VIDEO_TIMING_V, timing_v);
}

void hdmi_wp_init_vid_fmt_timings(struct hdmi_video_format *video_fmt,
		struct videomode *vm, struct hdmi_config *param)
{
	DSSDBG("Enter hdmi_wp_video_init_format\n");

	video_fmt->packing_mode = HDMI_PACK_10b_RGB_YUV444;
	video_fmt->y_res = param->vm.vactive;
	video_fmt->x_res = param->vm.hactive;

	vm->hback_porch = param->vm.hback_porch;
	vm->hfront_porch = param->vm.hfront_porch;
	vm->hsync_len = param->vm.hsync_len;
	vm->vback_porch = param->vm.vback_porch;
	vm->vfront_porch = param->vm.vfront_porch;
	vm->vsync_len = param->vm.vsync_len;

	vm->flags = param->vm.flags;

	if (param->vm.flags & DISPLAY_FLAGS_INTERLACED) {
		video_fmt->y_res /= 2;
		vm->vback_porch /= 2;
		vm->vfront_porch /= 2;
		vm->vsync_len /= 2;
	}

	if (param->vm.flags & DISPLAY_FLAGS_DOUBLECLK) {
		video_fmt->x_res *= 2;
		vm->hfront_porch *= 2;
		vm->hsync_len *= 2;
		vm->hback_porch *= 2;
	}
}

void hdmi_wp_audio_config_format(struct hdmi_wp_data *wp,
		struct hdmi_audio_format *aud_fmt)
{
	u32 r;

	DSSDBG("Enter hdmi_wp_audio_config_format\n");

	r = hdmi_read_reg(wp->base, HDMI_WP_AUDIO_CFG);
	if (wp->version == 4) {
		r = FLD_MOD(r, aud_fmt->stereo_channels, 26, 24);
		r = FLD_MOD(r, aud_fmt->active_chnnls_msk, 23, 16);
	}
	r = FLD_MOD(r, aud_fmt->en_sig_blk_strt_end, 5, 5);
	r = FLD_MOD(r, aud_fmt->type, 4, 4);
	r = FLD_MOD(r, aud_fmt->justification, 3, 3);
	r = FLD_MOD(r, aud_fmt->sample_order, 2, 2);
	r = FLD_MOD(r, aud_fmt->samples_per_word, 1, 1);
	r = FLD_MOD(r, aud_fmt->sample_size, 0, 0);
	hdmi_write_reg(wp->base, HDMI_WP_AUDIO_CFG, r);
}

void hdmi_wp_audio_config_dma(struct hdmi_wp_data *wp,
		struct hdmi_audio_dma *aud_dma)
{
	u32 r;

	DSSDBG("Enter hdmi_wp_audio_config_dma\n");

	r = hdmi_read_reg(wp->base, HDMI_WP_AUDIO_CFG2);
	r = FLD_MOD(r, aud_dma->transfer_size, 15, 8);
	r = FLD_MOD(r, aud_dma->block_size, 7, 0);
	hdmi_write_reg(wp->base, HDMI_WP_AUDIO_CFG2, r);

	r = hdmi_read_reg(wp->base, HDMI_WP_AUDIO_CTRL);
	r = FLD_MOD(r, aud_dma->mode, 9, 9);
	r = FLD_MOD(r, aud_dma->fifo_threshold, 8, 0);
	hdmi_write_reg(wp->base, HDMI_WP_AUDIO_CTRL, r);
}

int hdmi_wp_audio_enable(struct hdmi_wp_data *wp, bool enable)
{
	REG_FLD_MOD(wp->base, HDMI_WP_AUDIO_CTRL, enable, 31, 31);

	return 0;
}

int hdmi_wp_audio_core_req_enable(struct hdmi_wp_data *wp, bool enable)
{
	REG_FLD_MOD(wp->base, HDMI_WP_AUDIO_CTRL, enable, 30, 30);

	return 0;
}

int hdmi_wp_init(struct platform_device *pdev, struct hdmi_wp_data *wp,
		 unsigned int version)
{
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "wp");
	wp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wp->base))
		return PTR_ERR(wp->base);

	wp->phys_base = res->start;
	wp->version = version;

	return 0;
}

phys_addr_t hdmi_wp_get_audio_dma_addr(struct hdmi_wp_data *wp)
{
	return wp->phys_base + HDMI_WP_AUDIO_DATA;
}
