/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors:
 * Seung-Woo Kim <sw0312.kim@samsung.com>
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * Based on drivers/media/video/s5p-tv/mixer_reg.c
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include "drmP.h"

#include "regs-mixer.h"
#include "regs-vp.h"

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_hdmi.h"

#define get_mixer_context(dev)	platform_get_drvdata(to_platform_device(dev))

struct hdmi_win_data {
	dma_addr_t		dma_addr;
	void __iomem		*vaddr;
	dma_addr_t		chroma_dma_addr;
	void __iomem		*chroma_vaddr;
	uint32_t		pixel_format;
	unsigned int		bpp;
	unsigned int		crtc_x;
	unsigned int		crtc_y;
	unsigned int		crtc_width;
	unsigned int		crtc_height;
	unsigned int		fb_x;
	unsigned int		fb_y;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		mode_width;
	unsigned int		mode_height;
	unsigned int		scan_flags;
};

struct mixer_resources {
	int			irq;
	void __iomem		*mixer_regs;
	void __iomem		*vp_regs;
	spinlock_t		reg_slock;
	struct clk		*mixer;
	struct clk		*vp;
	struct clk		*sclk_mixer;
	struct clk		*sclk_hdmi;
	struct clk		*sclk_dac;
};

struct mixer_context {
	struct device		*dev;
	int			pipe;
	bool			interlace;
	bool			powered;
	u32			int_en;

	struct mutex		mixer_mutex;
	struct mixer_resources	mixer_res;
	struct hdmi_win_data	win_data[MIXER_WIN_NR];
};

static const u8 filter_y_horiz_tap8[] = {
	0,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	-1,	-1,	-1,	-1,	-1,	0,	0,	0,
	0,	2,	4,	5,	6,	6,	6,	6,
	6,	5,	5,	4,	3,	2,	1,	1,
	0,	-6,	-12,	-16,	-18,	-20,	-21,	-20,
	-20,	-18,	-16,	-13,	-10,	-8,	-5,	-2,
	127,	126,	125,	121,	114,	107,	99,	89,
	79,	68,	57,	46,	35,	25,	16,	8,
};

static const u8 filter_y_vert_tap4[] = {
	0,	-3,	-6,	-8,	-8,	-8,	-8,	-7,
	-6,	-5,	-4,	-3,	-2,	-1,	-1,	0,
	127,	126,	124,	118,	111,	102,	92,	81,
	70,	59,	48,	37,	27,	19,	11,	5,
	0,	5,	11,	19,	27,	37,	48,	59,
	70,	81,	92,	102,	111,	118,	124,	126,
	0,	0,	-1,	-1,	-2,	-3,	-4,	-5,
	-6,	-7,	-8,	-8,	-8,	-8,	-6,	-3,
};

static const u8 filter_cr_horiz_tap4[] = {
	0,	-3,	-6,	-8,	-8,	-8,	-8,	-7,
	-6,	-5,	-4,	-3,	-2,	-1,	-1,	0,
	127,	126,	124,	118,	111,	102,	92,	81,
	70,	59,	48,	37,	27,	19,	11,	5,
};

static inline u32 vp_reg_read(struct mixer_resources *res, u32 reg_id)
{
	return readl(res->vp_regs + reg_id);
}

static inline void vp_reg_write(struct mixer_resources *res, u32 reg_id,
				 u32 val)
{
	writel(val, res->vp_regs + reg_id);
}

static inline void vp_reg_writemask(struct mixer_resources *res, u32 reg_id,
				 u32 val, u32 mask)
{
	u32 old = vp_reg_read(res, reg_id);

	val = (val & mask) | (old & ~mask);
	writel(val, res->vp_regs + reg_id);
}

static inline u32 mixer_reg_read(struct mixer_resources *res, u32 reg_id)
{
	return readl(res->mixer_regs + reg_id);
}

static inline void mixer_reg_write(struct mixer_resources *res, u32 reg_id,
				 u32 val)
{
	writel(val, res->mixer_regs + reg_id);
}

static inline void mixer_reg_writemask(struct mixer_resources *res,
				 u32 reg_id, u32 val, u32 mask)
{
	u32 old = mixer_reg_read(res, reg_id);

	val = (val & mask) | (old & ~mask);
	writel(val, res->mixer_regs + reg_id);
}

static void mixer_regs_dump(struct mixer_context *ctx)
{
#define DUMPREG(reg_id) \
do { \
	DRM_DEBUG_KMS(#reg_id " = %08x\n", \
		(u32)readl(ctx->mixer_res.mixer_regs + reg_id)); \
} while (0)

	DUMPREG(MXR_STATUS);
	DUMPREG(MXR_CFG);
	DUMPREG(MXR_INT_EN);
	DUMPREG(MXR_INT_STATUS);

	DUMPREG(MXR_LAYER_CFG);
	DUMPREG(MXR_VIDEO_CFG);

	DUMPREG(MXR_GRAPHIC0_CFG);
	DUMPREG(MXR_GRAPHIC0_BASE);
	DUMPREG(MXR_GRAPHIC0_SPAN);
	DUMPREG(MXR_GRAPHIC0_WH);
	DUMPREG(MXR_GRAPHIC0_SXY);
	DUMPREG(MXR_GRAPHIC0_DXY);

	DUMPREG(MXR_GRAPHIC1_CFG);
	DUMPREG(MXR_GRAPHIC1_BASE);
	DUMPREG(MXR_GRAPHIC1_SPAN);
	DUMPREG(MXR_GRAPHIC1_WH);
	DUMPREG(MXR_GRAPHIC1_SXY);
	DUMPREG(MXR_GRAPHIC1_DXY);
#undef DUMPREG
}

static void vp_regs_dump(struct mixer_context *ctx)
{
#define DUMPREG(reg_id) \
do { \
	DRM_DEBUG_KMS(#reg_id " = %08x\n", \
		(u32) readl(ctx->mixer_res.vp_regs + reg_id)); \
} while (0)

	DUMPREG(VP_ENABLE);
	DUMPREG(VP_SRESET);
	DUMPREG(VP_SHADOW_UPDATE);
	DUMPREG(VP_FIELD_ID);
	DUMPREG(VP_MODE);
	DUMPREG(VP_IMG_SIZE_Y);
	DUMPREG(VP_IMG_SIZE_C);
	DUMPREG(VP_PER_RATE_CTRL);
	DUMPREG(VP_TOP_Y_PTR);
	DUMPREG(VP_BOT_Y_PTR);
	DUMPREG(VP_TOP_C_PTR);
	DUMPREG(VP_BOT_C_PTR);
	DUMPREG(VP_ENDIAN_MODE);
	DUMPREG(VP_SRC_H_POSITION);
	DUMPREG(VP_SRC_V_POSITION);
	DUMPREG(VP_SRC_WIDTH);
	DUMPREG(VP_SRC_HEIGHT);
	DUMPREG(VP_DST_H_POSITION);
	DUMPREG(VP_DST_V_POSITION);
	DUMPREG(VP_DST_WIDTH);
	DUMPREG(VP_DST_HEIGHT);
	DUMPREG(VP_H_RATIO);
	DUMPREG(VP_V_RATIO);

#undef DUMPREG
}

static inline void vp_filter_set(struct mixer_resources *res,
		int reg_id, const u8 *data, unsigned int size)
{
	/* assure 4-byte align */
	BUG_ON(size & 3);
	for (; size; size -= 4, reg_id += 4, data += 4) {
		u32 val = (data[0] << 24) |  (data[1] << 16) |
			(data[2] << 8) | data[3];
		vp_reg_write(res, reg_id, val);
	}
}

static void vp_default_filter(struct mixer_resources *res)
{
	vp_filter_set(res, VP_POLY8_Y0_LL,
		filter_y_horiz_tap8, sizeof filter_y_horiz_tap8);
	vp_filter_set(res, VP_POLY4_Y0_LL,
		filter_y_vert_tap4, sizeof filter_y_vert_tap4);
	vp_filter_set(res, VP_POLY4_C0_LL,
		filter_cr_horiz_tap4, sizeof filter_cr_horiz_tap4);
}

static void mixer_vsync_set_update(struct mixer_context *ctx, bool enable)
{
	struct mixer_resources *res = &ctx->mixer_res;

	/* block update on vsync */
	mixer_reg_writemask(res, MXR_STATUS, enable ?
			MXR_STATUS_SYNC_ENABLE : 0, MXR_STATUS_SYNC_ENABLE);

	vp_reg_write(res, VP_SHADOW_UPDATE, enable ?
			VP_SHADOW_UPDATE_ENABLE : 0);
}

static void mixer_cfg_scan(struct mixer_context *ctx, unsigned int height)
{
	struct mixer_resources *res = &ctx->mixer_res;
	u32 val;

	/* choosing between interlace and progressive mode */
	val = (ctx->interlace ? MXR_CFG_SCAN_INTERLACE :
				MXR_CFG_SCAN_PROGRASSIVE);

	/* choosing between porper HD and SD mode */
	if (height == 480)
		val |= MXR_CFG_SCAN_NTSC | MXR_CFG_SCAN_SD;
	else if (height == 576)
		val |= MXR_CFG_SCAN_PAL | MXR_CFG_SCAN_SD;
	else if (height == 720)
		val |= MXR_CFG_SCAN_HD_720 | MXR_CFG_SCAN_HD;
	else if (height == 1080)
		val |= MXR_CFG_SCAN_HD_1080 | MXR_CFG_SCAN_HD;
	else
		val |= MXR_CFG_SCAN_HD_720 | MXR_CFG_SCAN_HD;

	mixer_reg_writemask(res, MXR_CFG, val, MXR_CFG_SCAN_MASK);
}

static void mixer_cfg_rgb_fmt(struct mixer_context *ctx, unsigned int height)
{
	struct mixer_resources *res = &ctx->mixer_res;
	u32 val;

	if (height == 480) {
		val = MXR_CFG_RGB601_0_255;
	} else if (height == 576) {
		val = MXR_CFG_RGB601_0_255;
	} else if (height == 720) {
		val = MXR_CFG_RGB709_16_235;
		mixer_reg_write(res, MXR_CM_COEFF_Y,
				(1 << 30) | (94 << 20) | (314 << 10) |
				(32 << 0));
		mixer_reg_write(res, MXR_CM_COEFF_CB,
				(972 << 20) | (851 << 10) | (225 << 0));
		mixer_reg_write(res, MXR_CM_COEFF_CR,
				(225 << 20) | (820 << 10) | (1004 << 0));
	} else if (height == 1080) {
		val = MXR_CFG_RGB709_16_235;
		mixer_reg_write(res, MXR_CM_COEFF_Y,
				(1 << 30) | (94 << 20) | (314 << 10) |
				(32 << 0));
		mixer_reg_write(res, MXR_CM_COEFF_CB,
				(972 << 20) | (851 << 10) | (225 << 0));
		mixer_reg_write(res, MXR_CM_COEFF_CR,
				(225 << 20) | (820 << 10) | (1004 << 0));
	} else {
		val = MXR_CFG_RGB709_16_235;
		mixer_reg_write(res, MXR_CM_COEFF_Y,
				(1 << 30) | (94 << 20) | (314 << 10) |
				(32 << 0));
		mixer_reg_write(res, MXR_CM_COEFF_CB,
				(972 << 20) | (851 << 10) | (225 << 0));
		mixer_reg_write(res, MXR_CM_COEFF_CR,
				(225 << 20) | (820 << 10) | (1004 << 0));
	}

	mixer_reg_writemask(res, MXR_CFG, val, MXR_CFG_RGB_FMT_MASK);
}

static void mixer_cfg_layer(struct mixer_context *ctx, int win, bool enable)
{
	struct mixer_resources *res = &ctx->mixer_res;
	u32 val = enable ? ~0 : 0;

	switch (win) {
	case 0:
		mixer_reg_writemask(res, MXR_CFG, val, MXR_CFG_GRP0_ENABLE);
		break;
	case 1:
		mixer_reg_writemask(res, MXR_CFG, val, MXR_CFG_GRP1_ENABLE);
		break;
	case 2:
		vp_reg_writemask(res, VP_ENABLE, val, VP_ENABLE_ON);
		mixer_reg_writemask(res, MXR_CFG, val, MXR_CFG_VP_ENABLE);
		break;
	}
}

static void mixer_run(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	mixer_reg_writemask(res, MXR_STATUS, ~0, MXR_STATUS_REG_RUN);

	mixer_regs_dump(ctx);
}

static void vp_video_buffer(struct mixer_context *ctx, int win)
{
	struct mixer_resources *res = &ctx->mixer_res;
	unsigned long flags;
	struct hdmi_win_data *win_data;
	unsigned int full_width, full_height, width, height;
	unsigned int x_ratio, y_ratio;
	unsigned int src_x_offset, src_y_offset, dst_x_offset, dst_y_offset;
	unsigned int mode_width, mode_height;
	unsigned int buf_num;
	dma_addr_t luma_addr[2], chroma_addr[2];
	bool tiled_mode = false;
	bool crcb_mode = false;
	u32 val;

	win_data = &ctx->win_data[win];

	switch (win_data->pixel_format) {
	case DRM_FORMAT_NV12MT:
		tiled_mode = true;
	case DRM_FORMAT_NV12M:
		crcb_mode = false;
		buf_num = 2;
		break;
	/* TODO: single buffer format NV12, NV21 */
	default:
		/* ignore pixel format at disable time */
		if (!win_data->dma_addr)
			break;

		DRM_ERROR("pixel format for vp is wrong [%d].\n",
				win_data->pixel_format);
		return;
	}

	full_width = win_data->fb_width;
	full_height = win_data->fb_height;
	width = win_data->crtc_width;
	height = win_data->crtc_height;
	mode_width = win_data->mode_width;
	mode_height = win_data->mode_height;

	/* scaling feature: (src << 16) / dst */
	x_ratio = (width << 16) / width;
	y_ratio = (height << 16) / height;

	src_x_offset = win_data->fb_x;
	src_y_offset = win_data->fb_y;
	dst_x_offset = win_data->crtc_x;
	dst_y_offset = win_data->crtc_y;

	if (buf_num == 2) {
		luma_addr[0] = win_data->dma_addr;
		chroma_addr[0] = win_data->chroma_dma_addr;
	} else {
		luma_addr[0] = win_data->dma_addr;
		chroma_addr[0] = win_data->dma_addr
			+ (full_width * full_height);
	}

	if (win_data->scan_flags & DRM_MODE_FLAG_INTERLACE) {
		ctx->interlace = true;
		if (tiled_mode) {
			luma_addr[1] = luma_addr[0] + 0x40;
			chroma_addr[1] = chroma_addr[0] + 0x40;
		} else {
			luma_addr[1] = luma_addr[0] + full_width;
			chroma_addr[1] = chroma_addr[0] + full_width;
		}
	} else {
		ctx->interlace = false;
		luma_addr[1] = 0;
		chroma_addr[1] = 0;
	}

	spin_lock_irqsave(&res->reg_slock, flags);
	mixer_vsync_set_update(ctx, false);

	/* interlace or progressive scan mode */
	val = (ctx->interlace ? ~0 : 0);
	vp_reg_writemask(res, VP_MODE, val, VP_MODE_LINE_SKIP);

	/* setup format */
	val = (crcb_mode ? VP_MODE_NV21 : VP_MODE_NV12);
	val |= (tiled_mode ? VP_MODE_MEM_TILED : VP_MODE_MEM_LINEAR);
	vp_reg_writemask(res, VP_MODE, val, VP_MODE_FMT_MASK);

	/* setting size of input image */
	vp_reg_write(res, VP_IMG_SIZE_Y, VP_IMG_HSIZE(full_width) |
		VP_IMG_VSIZE(full_height));
	/* chroma height has to reduced by 2 to avoid chroma distorions */
	vp_reg_write(res, VP_IMG_SIZE_C, VP_IMG_HSIZE(full_width) |
		VP_IMG_VSIZE(full_height / 2));

	vp_reg_write(res, VP_SRC_WIDTH, width);
	vp_reg_write(res, VP_SRC_HEIGHT, height);
	vp_reg_write(res, VP_SRC_H_POSITION,
			VP_SRC_H_POSITION_VAL(src_x_offset));
	vp_reg_write(res, VP_SRC_V_POSITION, src_y_offset);

	vp_reg_write(res, VP_DST_WIDTH, width);
	vp_reg_write(res, VP_DST_H_POSITION, dst_x_offset);
	if (ctx->interlace) {
		vp_reg_write(res, VP_DST_HEIGHT, height / 2);
		vp_reg_write(res, VP_DST_V_POSITION, dst_y_offset / 2);
	} else {
		vp_reg_write(res, VP_DST_HEIGHT, height);
		vp_reg_write(res, VP_DST_V_POSITION, dst_y_offset);
	}

	vp_reg_write(res, VP_H_RATIO, x_ratio);
	vp_reg_write(res, VP_V_RATIO, y_ratio);

	vp_reg_write(res, VP_ENDIAN_MODE, VP_ENDIAN_MODE_LITTLE);

	/* set buffer address to vp */
	vp_reg_write(res, VP_TOP_Y_PTR, luma_addr[0]);
	vp_reg_write(res, VP_BOT_Y_PTR, luma_addr[1]);
	vp_reg_write(res, VP_TOP_C_PTR, chroma_addr[0]);
	vp_reg_write(res, VP_BOT_C_PTR, chroma_addr[1]);

	mixer_cfg_scan(ctx, mode_height);
	mixer_cfg_rgb_fmt(ctx, mode_height);
	mixer_cfg_layer(ctx, win, true);
	mixer_run(ctx);

	mixer_vsync_set_update(ctx, true);
	spin_unlock_irqrestore(&res->reg_slock, flags);

	vp_regs_dump(ctx);
}

static void mixer_graph_buffer(struct mixer_context *ctx, int win)
{
	struct mixer_resources *res = &ctx->mixer_res;
	unsigned long flags;
	struct hdmi_win_data *win_data;
	unsigned int full_width, width, height;
	unsigned int x_ratio, y_ratio;
	unsigned int src_x_offset, src_y_offset, dst_x_offset, dst_y_offset;
	unsigned int mode_width, mode_height;
	dma_addr_t dma_addr;
	unsigned int fmt;
	u32 val;

	win_data = &ctx->win_data[win];

	#define RGB565 4
	#define ARGB1555 5
	#define ARGB4444 6
	#define ARGB8888 7

	switch (win_data->bpp) {
	case 16:
		fmt = ARGB4444;
		break;
	case 32:
		fmt = ARGB8888;
		break;
	default:
		fmt = ARGB8888;
	}

	dma_addr = win_data->dma_addr;
	full_width = win_data->fb_width;
	width = win_data->crtc_width;
	height = win_data->crtc_height;
	mode_width = win_data->mode_width;
	mode_height = win_data->mode_height;

	/* 2x scaling feature */
	x_ratio = 0;
	y_ratio = 0;

	src_x_offset = win_data->fb_x;
	src_y_offset = win_data->fb_y;
	dst_x_offset = win_data->crtc_x;
	dst_y_offset = win_data->crtc_y;

	/* converting dma address base and source offset */
	dma_addr = dma_addr
		+ (src_x_offset * win_data->bpp >> 3)
		+ (src_y_offset * full_width * win_data->bpp >> 3);
	src_x_offset = 0;
	src_y_offset = 0;

	if (win_data->scan_flags & DRM_MODE_FLAG_INTERLACE)
		ctx->interlace = true;
	else
		ctx->interlace = false;

	spin_lock_irqsave(&res->reg_slock, flags);
	mixer_vsync_set_update(ctx, false);

	/* setup format */
	mixer_reg_writemask(res, MXR_GRAPHIC_CFG(win),
		MXR_GRP_CFG_FORMAT_VAL(fmt), MXR_GRP_CFG_FORMAT_MASK);

	/* setup geometry */
	mixer_reg_write(res, MXR_GRAPHIC_SPAN(win), full_width);

	val  = MXR_GRP_WH_WIDTH(width);
	val |= MXR_GRP_WH_HEIGHT(height);
	val |= MXR_GRP_WH_H_SCALE(x_ratio);
	val |= MXR_GRP_WH_V_SCALE(y_ratio);
	mixer_reg_write(res, MXR_GRAPHIC_WH(win), val);

	/* setup offsets in source image */
	val  = MXR_GRP_SXY_SX(src_x_offset);
	val |= MXR_GRP_SXY_SY(src_y_offset);
	mixer_reg_write(res, MXR_GRAPHIC_SXY(win), val);

	/* setup offsets in display image */
	val  = MXR_GRP_DXY_DX(dst_x_offset);
	val |= MXR_GRP_DXY_DY(dst_y_offset);
	mixer_reg_write(res, MXR_GRAPHIC_DXY(win), val);

	/* set buffer address to mixer */
	mixer_reg_write(res, MXR_GRAPHIC_BASE(win), dma_addr);

	mixer_cfg_scan(ctx, mode_height);
	mixer_cfg_rgb_fmt(ctx, mode_height);
	mixer_cfg_layer(ctx, win, true);
	mixer_run(ctx);

	mixer_vsync_set_update(ctx, true);
	spin_unlock_irqrestore(&res->reg_slock, flags);
}

static void vp_win_reset(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;
	int tries = 100;

	vp_reg_write(res, VP_SRESET, VP_SRESET_PROCESSING);
	for (tries = 100; tries; --tries) {
		/* waiting until VP_SRESET_PROCESSING is 0 */
		if (~vp_reg_read(res, VP_SRESET) & VP_SRESET_PROCESSING)
			break;
		mdelay(10);
	}
	WARN(tries == 0, "failed to reset Video Processor\n");
}

static void mixer_win_reset(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;
	unsigned long flags;
	u32 val; /* value stored to register */

	spin_lock_irqsave(&res->reg_slock, flags);
	mixer_vsync_set_update(ctx, false);

	mixer_reg_writemask(res, MXR_CFG, MXR_CFG_DST_HDMI, MXR_CFG_DST_MASK);

	/* set output in RGB888 mode */
	mixer_reg_writemask(res, MXR_CFG, MXR_CFG_OUT_RGB888, MXR_CFG_OUT_MASK);

	/* 16 beat burst in DMA */
	mixer_reg_writemask(res, MXR_STATUS, MXR_STATUS_16_BURST,
		MXR_STATUS_BURST_MASK);

	/* setting default layer priority: layer1 > layer0 > video
	 * because typical usage scenario would be
	 * layer1 - OSD
	 * layer0 - framebuffer
	 * video - video overlay
	 */
	val = MXR_LAYER_CFG_GRP1_VAL(3);
	val |= MXR_LAYER_CFG_GRP0_VAL(2);
	val |= MXR_LAYER_CFG_VP_VAL(1);
	mixer_reg_write(res, MXR_LAYER_CFG, val);

	/* setting background color */
	mixer_reg_write(res, MXR_BG_COLOR0, 0x008080);
	mixer_reg_write(res, MXR_BG_COLOR1, 0x008080);
	mixer_reg_write(res, MXR_BG_COLOR2, 0x008080);

	/* setting graphical layers */

	val  = MXR_GRP_CFG_COLOR_KEY_DISABLE; /* no blank key */
	val |= MXR_GRP_CFG_WIN_BLEND_EN;
	val |= MXR_GRP_CFG_ALPHA_VAL(0xff); /* non-transparent alpha */

	/* the same configuration for both layers */
	mixer_reg_write(res, MXR_GRAPHIC_CFG(0), val);

	val |= MXR_GRP_CFG_BLEND_PRE_MUL;
	val |= MXR_GRP_CFG_PIXEL_BLEND_EN;
	mixer_reg_write(res, MXR_GRAPHIC_CFG(1), val);

	/* configuration of Video Processor Registers */
	vp_win_reset(ctx);
	vp_default_filter(res);

	/* disable all layers */
	mixer_reg_writemask(res, MXR_CFG, 0, MXR_CFG_GRP0_ENABLE);
	mixer_reg_writemask(res, MXR_CFG, 0, MXR_CFG_GRP1_ENABLE);
	mixer_reg_writemask(res, MXR_CFG, 0, MXR_CFG_VP_ENABLE);

	mixer_vsync_set_update(ctx, true);
	spin_unlock_irqrestore(&res->reg_slock, flags);
}

static void mixer_poweron(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	mutex_lock(&ctx->mixer_mutex);
	if (ctx->powered) {
		mutex_unlock(&ctx->mixer_mutex);
		return;
	}
	ctx->powered = true;
	mutex_unlock(&ctx->mixer_mutex);

	pm_runtime_get_sync(ctx->dev);

	clk_enable(res->mixer);
	clk_enable(res->vp);
	clk_enable(res->sclk_mixer);

	mixer_reg_write(res, MXR_INT_EN, ctx->int_en);
	mixer_win_reset(ctx);
}

static void mixer_poweroff(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	mutex_lock(&ctx->mixer_mutex);
	if (!ctx->powered)
		goto out;
	mutex_unlock(&ctx->mixer_mutex);

	ctx->int_en = mixer_reg_read(res, MXR_INT_EN);

	clk_disable(res->mixer);
	clk_disable(res->vp);
	clk_disable(res->sclk_mixer);

	pm_runtime_put_sync(ctx->dev);

	mutex_lock(&ctx->mixer_mutex);
	ctx->powered = false;

out:
	mutex_unlock(&ctx->mixer_mutex);
}

static int mixer_enable_vblank(void *ctx, int pipe)
{
	struct mixer_context *mixer_ctx = ctx;
	struct mixer_resources *res = &mixer_ctx->mixer_res;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	mixer_ctx->pipe = pipe;

	/* enable vsync interrupt */
	mixer_reg_writemask(res, MXR_INT_EN, MXR_INT_EN_VSYNC,
			MXR_INT_EN_VSYNC);

	return 0;
}

static void mixer_disable_vblank(void *ctx)
{
	struct mixer_context *mixer_ctx = ctx;
	struct mixer_resources *res = &mixer_ctx->mixer_res;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	/* disable vsync interrupt */
	mixer_reg_writemask(res, MXR_INT_EN, 0, MXR_INT_EN_VSYNC);
}

static void mixer_dpms(void *ctx, int mode)
{
	struct mixer_context *mixer_ctx = ctx;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		mixer_poweron(mixer_ctx);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		mixer_poweroff(mixer_ctx);
		break;
	default:
		DRM_DEBUG_KMS("unknown dpms mode: %d\n", mode);
		break;
	}
}

static void mixer_win_mode_set(void *ctx,
			      struct exynos_drm_overlay *overlay)
{
	struct mixer_context *mixer_ctx = ctx;
	struct hdmi_win_data *win_data;
	int win;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (!overlay) {
		DRM_ERROR("overlay is NULL\n");
		return;
	}

	DRM_DEBUG_KMS("set [%d]x[%d] at (%d,%d) to [%d]x[%d] at (%d,%d)\n",
				 overlay->fb_width, overlay->fb_height,
				 overlay->fb_x, overlay->fb_y,
				 overlay->crtc_width, overlay->crtc_height,
				 overlay->crtc_x, overlay->crtc_y);

	win = overlay->zpos;
	if (win == DEFAULT_ZPOS)
		win = MIXER_DEFAULT_WIN;

	if (win < 0 || win > MIXER_WIN_NR) {
		DRM_ERROR("mixer window[%d] is wrong\n", win);
		return;
	}

	win_data = &mixer_ctx->win_data[win];

	win_data->dma_addr = overlay->dma_addr[0];
	win_data->vaddr = overlay->vaddr[0];
	win_data->chroma_dma_addr = overlay->dma_addr[1];
	win_data->chroma_vaddr = overlay->vaddr[1];
	win_data->pixel_format = overlay->pixel_format;
	win_data->bpp = overlay->bpp;

	win_data->crtc_x = overlay->crtc_x;
	win_data->crtc_y = overlay->crtc_y;
	win_data->crtc_width = overlay->crtc_width;
	win_data->crtc_height = overlay->crtc_height;

	win_data->fb_x = overlay->fb_x;
	win_data->fb_y = overlay->fb_y;
	win_data->fb_width = overlay->fb_width;
	win_data->fb_height = overlay->fb_height;

	win_data->mode_width = overlay->mode_width;
	win_data->mode_height = overlay->mode_height;

	win_data->scan_flags = overlay->scan_flag;
}

static void mixer_win_commit(void *ctx, int win)
{
	struct mixer_context *mixer_ctx = ctx;

	DRM_DEBUG_KMS("[%d] %s, win: %d\n", __LINE__, __func__, win);

	if (win > 1)
		vp_video_buffer(mixer_ctx, win);
	else
		mixer_graph_buffer(mixer_ctx, win);
}

static void mixer_win_disable(void *ctx, int win)
{
	struct mixer_context *mixer_ctx = ctx;
	struct mixer_resources *res = &mixer_ctx->mixer_res;
	unsigned long flags;

	DRM_DEBUG_KMS("[%d] %s, win: %d\n", __LINE__, __func__, win);

	spin_lock_irqsave(&res->reg_slock, flags);
	mixer_vsync_set_update(mixer_ctx, false);

	mixer_cfg_layer(mixer_ctx, win, false);

	mixer_vsync_set_update(mixer_ctx, true);
	spin_unlock_irqrestore(&res->reg_slock, flags);
}

static struct exynos_mixer_ops mixer_ops = {
	/* manager */
	.enable_vblank		= mixer_enable_vblank,
	.disable_vblank		= mixer_disable_vblank,
	.dpms			= mixer_dpms,

	/* overlay */
	.win_mode_set		= mixer_win_mode_set,
	.win_commit		= mixer_win_commit,
	.win_disable		= mixer_win_disable,
};

/* for pageflip event */
static void mixer_finish_pageflip(struct drm_device *drm_dev, int crtc)
{
	struct exynos_drm_private *dev_priv = drm_dev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	struct timeval now;
	unsigned long flags;
	bool is_checked = false;

	spin_lock_irqsave(&drm_dev->event_lock, flags);

	list_for_each_entry_safe(e, t, &dev_priv->pageflip_event_list,
			base.link) {
		/* if event's pipe isn't same as crtc then ignore it. */
		if (crtc != e->pipe)
			continue;

		is_checked = true;
		do_gettimeofday(&now);
		e->event.sequence = 0;
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;

		list_move_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}

	if (is_checked)
		/*
		 * call drm_vblank_put only in case that drm_vblank_get was
		 * called.
		 */
		if (atomic_read(&drm_dev->vblank_refcount[crtc]) > 0)
			drm_vblank_put(drm_dev, crtc);

	spin_unlock_irqrestore(&drm_dev->event_lock, flags);
}

static irqreturn_t mixer_irq_handler(int irq, void *arg)
{
	struct exynos_drm_hdmi_context *drm_hdmi_ctx = arg;
	struct mixer_context *ctx = drm_hdmi_ctx->ctx;
	struct mixer_resources *res = &ctx->mixer_res;
	u32 val, base, shadow;

	spin_lock(&res->reg_slock);

	/* read interrupt status for handling and clearing flags for VSYNC */
	val = mixer_reg_read(res, MXR_INT_STATUS);

	/* handling VSYNC */
	if (val & MXR_INT_STATUS_VSYNC) {
		/* interlace scan need to check shadow register */
		if (ctx->interlace) {
			base = mixer_reg_read(res, MXR_GRAPHIC_BASE(0));
			shadow = mixer_reg_read(res, MXR_GRAPHIC_BASE_S(0));
			if (base != shadow)
				goto out;

			base = mixer_reg_read(res, MXR_GRAPHIC_BASE(1));
			shadow = mixer_reg_read(res, MXR_GRAPHIC_BASE_S(1));
			if (base != shadow)
				goto out;
		}

		drm_handle_vblank(drm_hdmi_ctx->drm_dev, ctx->pipe);
		mixer_finish_pageflip(drm_hdmi_ctx->drm_dev, ctx->pipe);
	}

out:
	/* clear interrupts */
	if (~val & MXR_INT_EN_VSYNC) {
		/* vsync interrupt use different bit for read and clear */
		val &= ~MXR_INT_EN_VSYNC;
		val |= MXR_INT_CLEAR_VSYNC;
	}
	mixer_reg_write(res, MXR_INT_STATUS, val);

	spin_unlock(&res->reg_slock);

	return IRQ_HANDLED;
}

static int __devinit mixer_resources_init(struct exynos_drm_hdmi_context *ctx,
				 struct platform_device *pdev)
{
	struct mixer_context *mixer_ctx = ctx->ctx;
	struct device *dev = &pdev->dev;
	struct mixer_resources *mixer_res = &mixer_ctx->mixer_res;
	struct resource *res;
	int ret;

	spin_lock_init(&mixer_res->reg_slock);

	mixer_res->mixer = clk_get(dev, "mixer");
	if (IS_ERR_OR_NULL(mixer_res->mixer)) {
		dev_err(dev, "failed to get clock 'mixer'\n");
		ret = -ENODEV;
		goto fail;
	}
	mixer_res->vp = clk_get(dev, "vp");
	if (IS_ERR_OR_NULL(mixer_res->vp)) {
		dev_err(dev, "failed to get clock 'vp'\n");
		ret = -ENODEV;
		goto fail;
	}
	mixer_res->sclk_mixer = clk_get(dev, "sclk_mixer");
	if (IS_ERR_OR_NULL(mixer_res->sclk_mixer)) {
		dev_err(dev, "failed to get clock 'sclk_mixer'\n");
		ret = -ENODEV;
		goto fail;
	}
	mixer_res->sclk_hdmi = clk_get(dev, "sclk_hdmi");
	if (IS_ERR_OR_NULL(mixer_res->sclk_hdmi)) {
		dev_err(dev, "failed to get clock 'sclk_hdmi'\n");
		ret = -ENODEV;
		goto fail;
	}
	mixer_res->sclk_dac = clk_get(dev, "sclk_dac");
	if (IS_ERR_OR_NULL(mixer_res->sclk_dac)) {
		dev_err(dev, "failed to get clock 'sclk_dac'\n");
		ret = -ENODEV;
		goto fail;
	}
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mxr");
	if (res == NULL) {
		dev_err(dev, "get memory resource failed.\n");
		ret = -ENXIO;
		goto fail;
	}

	clk_set_parent(mixer_res->sclk_mixer, mixer_res->sclk_hdmi);

	mixer_res->mixer_regs = ioremap(res->start, resource_size(res));
	if (mixer_res->mixer_regs == NULL) {
		dev_err(dev, "register mapping failed.\n");
		ret = -ENXIO;
		goto fail;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vp");
	if (res == NULL) {
		dev_err(dev, "get memory resource failed.\n");
		ret = -ENXIO;
		goto fail_mixer_regs;
	}

	mixer_res->vp_regs = ioremap(res->start, resource_size(res));
	if (mixer_res->vp_regs == NULL) {
		dev_err(dev, "register mapping failed.\n");
		ret = -ENXIO;
		goto fail_mixer_regs;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "irq");
	if (res == NULL) {
		dev_err(dev, "get interrupt resource failed.\n");
		ret = -ENXIO;
		goto fail_vp_regs;
	}

	ret = request_irq(res->start, mixer_irq_handler, 0, "drm_mixer", ctx);
	if (ret) {
		dev_err(dev, "request interrupt failed.\n");
		goto fail_vp_regs;
	}
	mixer_res->irq = res->start;

	return 0;

fail_vp_regs:
	iounmap(mixer_res->vp_regs);

fail_mixer_regs:
	iounmap(mixer_res->mixer_regs);

fail:
	if (!IS_ERR_OR_NULL(mixer_res->sclk_dac))
		clk_put(mixer_res->sclk_dac);
	if (!IS_ERR_OR_NULL(mixer_res->sclk_hdmi))
		clk_put(mixer_res->sclk_hdmi);
	if (!IS_ERR_OR_NULL(mixer_res->sclk_mixer))
		clk_put(mixer_res->sclk_mixer);
	if (!IS_ERR_OR_NULL(mixer_res->vp))
		clk_put(mixer_res->vp);
	if (!IS_ERR_OR_NULL(mixer_res->mixer))
		clk_put(mixer_res->mixer);
	return ret;
}

static void mixer_resources_cleanup(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	free_irq(res->irq, ctx);

	iounmap(res->vp_regs);
	iounmap(res->mixer_regs);
}

static int __devinit mixer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_drm_hdmi_context *drm_hdmi_ctx;
	struct mixer_context *ctx;
	int ret;

	dev_info(dev, "probe start\n");

	drm_hdmi_ctx = kzalloc(sizeof(*drm_hdmi_ctx), GFP_KERNEL);
	if (!drm_hdmi_ctx) {
		DRM_ERROR("failed to allocate common hdmi context.\n");
		return -ENOMEM;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		DRM_ERROR("failed to alloc mixer context.\n");
		kfree(drm_hdmi_ctx);
		return -ENOMEM;
	}

	mutex_init(&ctx->mixer_mutex);

	ctx->dev = &pdev->dev;
	drm_hdmi_ctx->ctx = (void *)ctx;

	platform_set_drvdata(pdev, drm_hdmi_ctx);

	/* acquire resources: regs, irqs, clocks */
	ret = mixer_resources_init(drm_hdmi_ctx, pdev);
	if (ret)
		goto fail;

	/* register specific callback point to common hdmi. */
	exynos_mixer_ops_register(&mixer_ops);

	pm_runtime_enable(dev);

	return 0;


fail:
	dev_info(dev, "probe failed\n");
	return ret;
}

static int mixer_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_drm_hdmi_context *drm_hdmi_ctx =
					platform_get_drvdata(pdev);
	struct mixer_context *ctx = drm_hdmi_ctx->ctx;

	dev_info(dev, "remove successful\n");

	pm_runtime_disable(&pdev->dev);

	mixer_resources_cleanup(ctx);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mixer_suspend(struct device *dev)
{
	struct exynos_drm_hdmi_context *drm_hdmi_ctx = get_mixer_context(dev);
	struct mixer_context *ctx = drm_hdmi_ctx->ctx;

	mixer_poweroff(ctx);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mixer_pm_ops, mixer_suspend, NULL);

struct platform_driver mixer_driver = {
	.driver = {
		.name = "s5p-mixer",
		.owner = THIS_MODULE,
		.pm = &mixer_pm_ops,
	},
	.probe = mixer_probe,
	.remove = __devexit_p(mixer_remove),
};
