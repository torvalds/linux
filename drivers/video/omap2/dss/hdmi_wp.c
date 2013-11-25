/*
 * HDMI wrapper
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <video/omapdss.h>

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
	DUMPREG(HDMI_WP_WP_CLK);
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
		pr_err("Failed to set PHY power mode to %d\n", val);
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
		pr_err("Failed to set PLL_PWR_STATUS\n");
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
	REG_FLD_MOD(wp->base, HDMI_WP_VIDEO_CFG, false, 31, 31);
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
		struct omap_video_timings *timings)
{
	u32 r;
	bool vsync_pol, hsync_pol;
	pr_debug("Enter hdmi_wp_video_config_interface\n");

	vsync_pol = timings->vsync_level == OMAPDSS_SIG_ACTIVE_HIGH;
	hsync_pol = timings->hsync_level == OMAPDSS_SIG_ACTIVE_HIGH;

	r = hdmi_read_reg(wp->base, HDMI_WP_VIDEO_CFG);
	r = FLD_MOD(r, vsync_pol, 7, 7);
	r = FLD_MOD(r, hsync_pol, 6, 6);
	r = FLD_MOD(r, timings->interlace, 3, 3);
	r = FLD_MOD(r, 1, 1, 0); /* HDMI_TIMING_MASTER_24BIT */
	hdmi_write_reg(wp->base, HDMI_WP_VIDEO_CFG, r);
}

void hdmi_wp_video_config_timing(struct hdmi_wp_data *wp,
		struct omap_video_timings *timings)
{
	u32 timing_h = 0;
	u32 timing_v = 0;

	pr_debug("Enter hdmi_wp_video_config_timing\n");

	timing_h |= FLD_VAL(timings->hbp, 31, 20);
	timing_h |= FLD_VAL(timings->hfp, 19, 8);
	timing_h |= FLD_VAL(timings->hsw, 7, 0);
	hdmi_write_reg(wp->base, HDMI_WP_VIDEO_TIMING_H, timing_h);

	timing_v |= FLD_VAL(timings->vbp, 31, 20);
	timing_v |= FLD_VAL(timings->vfp, 19, 8);
	timing_v |= FLD_VAL(timings->vsw, 7, 0);
	hdmi_write_reg(wp->base, HDMI_WP_VIDEO_TIMING_V, timing_v);
}

void hdmi_wp_init_vid_fmt_timings(struct hdmi_video_format *video_fmt,
		struct omap_video_timings *timings, struct hdmi_config *param)
{
	pr_debug("Enter hdmi_wp_video_init_format\n");

	video_fmt->packing_mode = HDMI_PACK_10b_RGB_YUV444;
	video_fmt->y_res = param->timings.y_res;
	video_fmt->x_res = param->timings.x_res;

	timings->hbp = param->timings.hbp;
	timings->hfp = param->timings.hfp;
	timings->hsw = param->timings.hsw;
	timings->vbp = param->timings.vbp;
	timings->vfp = param->timings.vfp;
	timings->vsw = param->timings.vsw;
	timings->vsync_level = param->timings.vsync_level;
	timings->hsync_level = param->timings.hsync_level;
	timings->interlace = param->timings.interlace;
}

#if defined(CONFIG_OMAP4_DSS_HDMI_AUDIO)
void hdmi_wp_audio_config_format(struct hdmi_wp_data *wp,
		struct hdmi_audio_format *aud_fmt)
{
	u32 r;

	DSSDBG("Enter hdmi_wp_audio_config_format\n");

	r = hdmi_read_reg(wp->base, HDMI_WP_AUDIO_CFG);
	r = FLD_MOD(r, aud_fmt->stereo_channels, 26, 24);
	r = FLD_MOD(r, aud_fmt->active_chnnls_msk, 23, 16);
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
#endif

#define WP_SIZE	0x200

int hdmi_wp_init(struct platform_device *pdev, struct hdmi_wp_data *wp)
{
	struct resource *res;
	struct resource temp_res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdmi_wp");
	if (!res) {
		DSSDBG("can't get WP mem resource by name\n");
		/*
		 * if hwmod/DT doesn't have the memory resource information
		 * split into HDMI sub blocks by name, we try again by getting
		 * the platform's first resource. this code will be removed when
		 * the driver can get the mem resources by name
		 */
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			DSSERR("can't get WP mem resource\n");
			return -EINVAL;
		}

		temp_res.start = res->start;
		temp_res.end = temp_res.start + WP_SIZE - 1;
		res = &temp_res;
	}

	wp->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!wp->base) {
		DSSERR("can't ioremap HDMI WP\n");
		return -ENOMEM;
	}

	return 0;
}
