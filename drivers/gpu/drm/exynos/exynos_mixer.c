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

#include <drm/drmP.h>

#include "regs-mixer.h"
#include "regs-vp.h"

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/component.h>

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_iommu.h"
#include "exynos_mixer.h"

#define MIXER_WIN_NR		3
#define MIXER_DEFAULT_WIN	0

struct hdmi_win_data {
	dma_addr_t		dma_addr;
	dma_addr_t		chroma_dma_addr;
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
	unsigned int		src_width;
	unsigned int		src_height;
	unsigned int		mode_width;
	unsigned int		mode_height;
	unsigned int		scan_flags;
	bool			enabled;
	bool			resume;
};

struct mixer_resources {
	int			irq;
	void __iomem		*mixer_regs;
	void __iomem		*vp_regs;
	spinlock_t		reg_slock;
	struct clk		*mixer;
	struct clk		*vp;
	struct clk		*hdmi;
	struct clk		*sclk_mixer;
	struct clk		*sclk_hdmi;
	struct clk		*mout_mixer;
};

enum mixer_version_id {
	MXR_VER_0_0_0_16,
	MXR_VER_16_0_33_0,
	MXR_VER_128_0_0_184,
};

struct mixer_context {
	struct platform_device *pdev;
	struct device		*dev;
	struct drm_device	*drm_dev;
	struct exynos_drm_crtc	*crtc;
	int			pipe;
	bool			interlace;
	bool			powered;
	bool			vp_enabled;
	bool			has_sclk;
	u32			int_en;

	struct mutex		mixer_mutex;
	struct mixer_resources	mixer_res;
	struct hdmi_win_data	win_data[MIXER_WIN_NR];
	enum mixer_version_id	mxr_ver;
	wait_queue_head_t	wait_vsync_queue;
	atomic_t		wait_vsync_event;
};

struct mixer_drv_data {
	enum mixer_version_id	version;
	bool					is_vp_enabled;
	bool					has_sclk;
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
		filter_y_horiz_tap8, sizeof(filter_y_horiz_tap8));
	vp_filter_set(res, VP_POLY4_Y0_LL,
		filter_y_vert_tap4, sizeof(filter_y_vert_tap4));
	vp_filter_set(res, VP_POLY4_C0_LL,
		filter_cr_horiz_tap4, sizeof(filter_cr_horiz_tap4));
}

static void mixer_vsync_set_update(struct mixer_context *ctx, bool enable)
{
	struct mixer_resources *res = &ctx->mixer_res;

	/* block update on vsync */
	mixer_reg_writemask(res, MXR_STATUS, enable ?
			MXR_STATUS_SYNC_ENABLE : 0, MXR_STATUS_SYNC_ENABLE);

	if (ctx->vp_enabled)
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

	if (ctx->mxr_ver != MXR_VER_128_0_0_184) {
		/* choosing between proper HD and SD mode */
		if (height <= 480)
			val |= MXR_CFG_SCAN_NTSC | MXR_CFG_SCAN_SD;
		else if (height <= 576)
			val |= MXR_CFG_SCAN_PAL | MXR_CFG_SCAN_SD;
		else if (height <= 720)
			val |= MXR_CFG_SCAN_HD_720 | MXR_CFG_SCAN_HD;
		else if (height <= 1080)
			val |= MXR_CFG_SCAN_HD_1080 | MXR_CFG_SCAN_HD;
		else
			val |= MXR_CFG_SCAN_HD_720 | MXR_CFG_SCAN_HD;
	}

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
		if (ctx->vp_enabled) {
			vp_reg_writemask(res, VP_ENABLE, val, VP_ENABLE_ON);
			mixer_reg_writemask(res, MXR_CFG, val,
				MXR_CFG_VP_ENABLE);

			/* control blending of graphic layer 0 */
			mixer_reg_writemask(res, MXR_GRAPHIC_CFG(0), val,
					MXR_GRP_CFG_BLEND_PRE_MUL |
					MXR_GRP_CFG_PIXEL_BLEND_EN);
		}
		break;
	}
}

static void mixer_run(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	mixer_reg_writemask(res, MXR_STATUS, ~0, MXR_STATUS_REG_RUN);

	mixer_regs_dump(ctx);
}

static void mixer_stop(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;
	int timeout = 20;

	mixer_reg_writemask(res, MXR_STATUS, 0, MXR_STATUS_REG_RUN);

	while (!(mixer_reg_read(res, MXR_STATUS) & MXR_STATUS_REG_IDLE) &&
			--timeout)
		usleep_range(10000, 12000);

	mixer_regs_dump(ctx);
}

static void vp_video_buffer(struct mixer_context *ctx, int win)
{
	struct mixer_resources *res = &ctx->mixer_res;
	unsigned long flags;
	struct hdmi_win_data *win_data;
	unsigned int x_ratio, y_ratio;
	unsigned int buf_num = 1;
	dma_addr_t luma_addr[2], chroma_addr[2];
	bool tiled_mode = false;
	bool crcb_mode = false;
	u32 val;

	win_data = &ctx->win_data[win];

	switch (win_data->pixel_format) {
	case DRM_FORMAT_NV12:
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

	/* scaling feature: (src << 16) / dst */
	x_ratio = (win_data->src_width << 16) / win_data->crtc_width;
	y_ratio = (win_data->src_height << 16) / win_data->crtc_height;

	if (buf_num == 2) {
		luma_addr[0] = win_data->dma_addr;
		chroma_addr[0] = win_data->chroma_dma_addr;
	} else {
		luma_addr[0] = win_data->dma_addr;
		chroma_addr[0] = win_data->dma_addr
			+ (win_data->fb_width * win_data->fb_height);
	}

	if (win_data->scan_flags & DRM_MODE_FLAG_INTERLACE) {
		ctx->interlace = true;
		if (tiled_mode) {
			luma_addr[1] = luma_addr[0] + 0x40;
			chroma_addr[1] = chroma_addr[0] + 0x40;
		} else {
			luma_addr[1] = luma_addr[0] + win_data->fb_width;
			chroma_addr[1] = chroma_addr[0] + win_data->fb_width;
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
	vp_reg_write(res, VP_IMG_SIZE_Y, VP_IMG_HSIZE(win_data->fb_width) |
		VP_IMG_VSIZE(win_data->fb_height));
	/* chroma height has to reduced by 2 to avoid chroma distorions */
	vp_reg_write(res, VP_IMG_SIZE_C, VP_IMG_HSIZE(win_data->fb_width) |
		VP_IMG_VSIZE(win_data->fb_height / 2));

	vp_reg_write(res, VP_SRC_WIDTH, win_data->src_width);
	vp_reg_write(res, VP_SRC_HEIGHT, win_data->src_height);
	vp_reg_write(res, VP_SRC_H_POSITION,
			VP_SRC_H_POSITION_VAL(win_data->fb_x));
	vp_reg_write(res, VP_SRC_V_POSITION, win_data->fb_y);

	vp_reg_write(res, VP_DST_WIDTH, win_data->crtc_width);
	vp_reg_write(res, VP_DST_H_POSITION, win_data->crtc_x);
	if (ctx->interlace) {
		vp_reg_write(res, VP_DST_HEIGHT, win_data->crtc_height / 2);
		vp_reg_write(res, VP_DST_V_POSITION, win_data->crtc_y / 2);
	} else {
		vp_reg_write(res, VP_DST_HEIGHT, win_data->crtc_height);
		vp_reg_write(res, VP_DST_V_POSITION, win_data->crtc_y);
	}

	vp_reg_write(res, VP_H_RATIO, x_ratio);
	vp_reg_write(res, VP_V_RATIO, y_ratio);

	vp_reg_write(res, VP_ENDIAN_MODE, VP_ENDIAN_MODE_LITTLE);

	/* set buffer address to vp */
	vp_reg_write(res, VP_TOP_Y_PTR, luma_addr[0]);
	vp_reg_write(res, VP_BOT_Y_PTR, luma_addr[1]);
	vp_reg_write(res, VP_TOP_C_PTR, chroma_addr[0]);
	vp_reg_write(res, VP_BOT_C_PTR, chroma_addr[1]);

	mixer_cfg_scan(ctx, win_data->mode_height);
	mixer_cfg_rgb_fmt(ctx, win_data->mode_height);
	mixer_cfg_layer(ctx, win, true);
	mixer_run(ctx);

	mixer_vsync_set_update(ctx, true);
	spin_unlock_irqrestore(&res->reg_slock, flags);

	vp_regs_dump(ctx);
}

static void mixer_layer_update(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	mixer_reg_writemask(res, MXR_CFG, ~0, MXR_CFG_LAYER_UPDATE);
}

static void mixer_graph_buffer(struct mixer_context *ctx, int win)
{
	struct mixer_resources *res = &ctx->mixer_res;
	unsigned long flags;
	struct hdmi_win_data *win_data;
	unsigned int x_ratio, y_ratio;
	unsigned int src_x_offset, src_y_offset, dst_x_offset, dst_y_offset;
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

	/* 2x scaling feature */
	x_ratio = 0;
	y_ratio = 0;

	dst_x_offset = win_data->crtc_x;
	dst_y_offset = win_data->crtc_y;

	/* converting dma address base and source offset */
	dma_addr = win_data->dma_addr
		+ (win_data->fb_x * win_data->bpp >> 3)
		+ (win_data->fb_y * win_data->fb_width * win_data->bpp >> 3);
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
	mixer_reg_write(res, MXR_GRAPHIC_SPAN(win), win_data->fb_width);

	/* setup display size */
	if (ctx->mxr_ver == MXR_VER_128_0_0_184 &&
		win == MIXER_DEFAULT_WIN) {
		val  = MXR_MXR_RES_HEIGHT(win_data->mode_height);
		val |= MXR_MXR_RES_WIDTH(win_data->mode_width);
		mixer_reg_write(res, MXR_RESOLUTION, val);
	}

	val  = MXR_GRP_WH_WIDTH(win_data->crtc_width);
	val |= MXR_GRP_WH_HEIGHT(win_data->crtc_height);
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

	mixer_cfg_scan(ctx, win_data->mode_height);
	mixer_cfg_rgb_fmt(ctx, win_data->mode_height);
	mixer_cfg_layer(ctx, win, true);

	/* layer update mandatory for mixer 16.0.33.0 */
	if (ctx->mxr_ver == MXR_VER_16_0_33_0 ||
		ctx->mxr_ver == MXR_VER_128_0_0_184)
		mixer_layer_update(ctx);

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
		usleep_range(10000, 12000);
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
	if (ctx->vp_enabled)
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

	/* Don't blend layer 0 onto the mixer background */
	mixer_reg_write(res, MXR_GRAPHIC_CFG(0), val);

	/* Blend layer 1 into layer 0 */
	val |= MXR_GRP_CFG_BLEND_PRE_MUL;
	val |= MXR_GRP_CFG_PIXEL_BLEND_EN;
	mixer_reg_write(res, MXR_GRAPHIC_CFG(1), val);

	/* setting video layers */
	val = MXR_GRP_CFG_ALPHA_VAL(0);
	mixer_reg_write(res, MXR_VIDEO_CFG, val);

	if (ctx->vp_enabled) {
		/* configuration of Video Processor Registers */
		vp_win_reset(ctx);
		vp_default_filter(res);
	}

	/* disable all layers */
	mixer_reg_writemask(res, MXR_CFG, 0, MXR_CFG_GRP0_ENABLE);
	mixer_reg_writemask(res, MXR_CFG, 0, MXR_CFG_GRP1_ENABLE);
	if (ctx->vp_enabled)
		mixer_reg_writemask(res, MXR_CFG, 0, MXR_CFG_VP_ENABLE);

	mixer_vsync_set_update(ctx, true);
	spin_unlock_irqrestore(&res->reg_slock, flags);
}

static irqreturn_t mixer_irq_handler(int irq, void *arg)
{
	struct mixer_context *ctx = arg;
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

		drm_handle_vblank(ctx->drm_dev, ctx->pipe);
		exynos_drm_crtc_finish_pageflip(ctx->drm_dev, ctx->pipe);

		/* set wait vsync event to zero and wake up queue. */
		if (atomic_read(&ctx->wait_vsync_event)) {
			atomic_set(&ctx->wait_vsync_event, 0);
			wake_up(&ctx->wait_vsync_queue);
		}
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

static int mixer_resources_init(struct mixer_context *mixer_ctx)
{
	struct device *dev = &mixer_ctx->pdev->dev;
	struct mixer_resources *mixer_res = &mixer_ctx->mixer_res;
	struct resource *res;
	int ret;

	spin_lock_init(&mixer_res->reg_slock);

	mixer_res->mixer = devm_clk_get(dev, "mixer");
	if (IS_ERR(mixer_res->mixer)) {
		dev_err(dev, "failed to get clock 'mixer'\n");
		return -ENODEV;
	}

	mixer_res->hdmi = devm_clk_get(dev, "hdmi");
	if (IS_ERR(mixer_res->hdmi)) {
		dev_err(dev, "failed to get clock 'hdmi'\n");
		return PTR_ERR(mixer_res->hdmi);
	}

	mixer_res->sclk_hdmi = devm_clk_get(dev, "sclk_hdmi");
	if (IS_ERR(mixer_res->sclk_hdmi)) {
		dev_err(dev, "failed to get clock 'sclk_hdmi'\n");
		return -ENODEV;
	}
	res = platform_get_resource(mixer_ctx->pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "get memory resource failed.\n");
		return -ENXIO;
	}

	mixer_res->mixer_regs = devm_ioremap(dev, res->start,
							resource_size(res));
	if (mixer_res->mixer_regs == NULL) {
		dev_err(dev, "register mapping failed.\n");
		return -ENXIO;
	}

	res = platform_get_resource(mixer_ctx->pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(dev, "get interrupt resource failed.\n");
		return -ENXIO;
	}

	ret = devm_request_irq(dev, res->start, mixer_irq_handler,
						0, "drm_mixer", mixer_ctx);
	if (ret) {
		dev_err(dev, "request interrupt failed.\n");
		return ret;
	}
	mixer_res->irq = res->start;

	return 0;
}

static int vp_resources_init(struct mixer_context *mixer_ctx)
{
	struct device *dev = &mixer_ctx->pdev->dev;
	struct mixer_resources *mixer_res = &mixer_ctx->mixer_res;
	struct resource *res;

	mixer_res->vp = devm_clk_get(dev, "vp");
	if (IS_ERR(mixer_res->vp)) {
		dev_err(dev, "failed to get clock 'vp'\n");
		return -ENODEV;
	}

	if (mixer_ctx->has_sclk) {
		mixer_res->sclk_mixer = devm_clk_get(dev, "sclk_mixer");
		if (IS_ERR(mixer_res->sclk_mixer)) {
			dev_err(dev, "failed to get clock 'sclk_mixer'\n");
			return -ENODEV;
		}
		mixer_res->mout_mixer = devm_clk_get(dev, "mout_mixer");
		if (IS_ERR(mixer_res->mout_mixer)) {
			dev_err(dev, "failed to get clock 'mout_mixer'\n");
			return -ENODEV;
		}

		if (mixer_res->sclk_hdmi && mixer_res->mout_mixer)
			clk_set_parent(mixer_res->mout_mixer,
				       mixer_res->sclk_hdmi);
	}

	res = platform_get_resource(mixer_ctx->pdev, IORESOURCE_MEM, 1);
	if (res == NULL) {
		dev_err(dev, "get memory resource failed.\n");
		return -ENXIO;
	}

	mixer_res->vp_regs = devm_ioremap(dev, res->start,
							resource_size(res));
	if (mixer_res->vp_regs == NULL) {
		dev_err(dev, "register mapping failed.\n");
		return -ENXIO;
	}

	return 0;
}

static int mixer_initialize(struct mixer_context *mixer_ctx,
			struct drm_device *drm_dev)
{
	int ret;
	struct exynos_drm_private *priv;
	priv = drm_dev->dev_private;

	mixer_ctx->drm_dev = drm_dev;
	mixer_ctx->pipe = priv->pipe++;

	/* acquire resources: regs, irqs, clocks */
	ret = mixer_resources_init(mixer_ctx);
	if (ret) {
		DRM_ERROR("mixer_resources_init failed ret=%d\n", ret);
		return ret;
	}

	if (mixer_ctx->vp_enabled) {
		/* acquire vp resources: regs, irqs, clocks */
		ret = vp_resources_init(mixer_ctx);
		if (ret) {
			DRM_ERROR("vp_resources_init failed ret=%d\n", ret);
			return ret;
		}
	}

	if (!is_drm_iommu_supported(mixer_ctx->drm_dev))
		return 0;

	return drm_iommu_attach_device(mixer_ctx->drm_dev, mixer_ctx->dev);
}

static void mixer_ctx_remove(struct mixer_context *mixer_ctx)
{
	if (is_drm_iommu_supported(mixer_ctx->drm_dev))
		drm_iommu_detach_device(mixer_ctx->drm_dev, mixer_ctx->dev);
}

static int mixer_enable_vblank(struct exynos_drm_crtc *crtc)
{
	struct mixer_context *mixer_ctx = crtc->ctx;
	struct mixer_resources *res = &mixer_ctx->mixer_res;

	if (!mixer_ctx->powered) {
		mixer_ctx->int_en |= MXR_INT_EN_VSYNC;
		return 0;
	}

	/* enable vsync interrupt */
	mixer_reg_writemask(res, MXR_INT_EN, MXR_INT_EN_VSYNC,
			MXR_INT_EN_VSYNC);

	return 0;
}

static void mixer_disable_vblank(struct exynos_drm_crtc *crtc)
{
	struct mixer_context *mixer_ctx = crtc->ctx;
	struct mixer_resources *res = &mixer_ctx->mixer_res;

	/* disable vsync interrupt */
	mixer_reg_writemask(res, MXR_INT_EN, 0, MXR_INT_EN_VSYNC);
}

static void mixer_win_mode_set(struct exynos_drm_crtc *crtc,
			struct exynos_drm_plane *plane)
{
	struct mixer_context *mixer_ctx = crtc->ctx;
	struct hdmi_win_data *win_data;
	int win;

	if (!plane) {
		DRM_ERROR("plane is NULL\n");
		return;
	}

	DRM_DEBUG_KMS("set [%d]x[%d] at (%d,%d) to [%d]x[%d] at (%d,%d)\n",
				 plane->fb_width, plane->fb_height,
				 plane->fb_x, plane->fb_y,
				 plane->crtc_width, plane->crtc_height,
				 plane->crtc_x, plane->crtc_y);

	win = plane->zpos;
	if (win == DEFAULT_ZPOS)
		win = MIXER_DEFAULT_WIN;

	if (win < 0 || win >= MIXER_WIN_NR) {
		DRM_ERROR("mixer window[%d] is wrong\n", win);
		return;
	}

	win_data = &mixer_ctx->win_data[win];

	win_data->dma_addr = plane->dma_addr[0];
	win_data->chroma_dma_addr = plane->dma_addr[1];
	win_data->pixel_format = plane->pixel_format;
	win_data->bpp = plane->bpp;

	win_data->crtc_x = plane->crtc_x;
	win_data->crtc_y = plane->crtc_y;
	win_data->crtc_width = plane->crtc_width;
	win_data->crtc_height = plane->crtc_height;

	win_data->fb_x = plane->fb_x;
	win_data->fb_y = plane->fb_y;
	win_data->fb_width = plane->fb_width;
	win_data->fb_height = plane->fb_height;
	win_data->src_width = plane->src_width;
	win_data->src_height = plane->src_height;

	win_data->mode_width = plane->mode_width;
	win_data->mode_height = plane->mode_height;

	win_data->scan_flags = plane->scan_flag;
}

static void mixer_win_commit(struct exynos_drm_crtc *crtc, int zpos)
{
	struct mixer_context *mixer_ctx = crtc->ctx;
	int win = zpos == DEFAULT_ZPOS ? MIXER_DEFAULT_WIN : zpos;

	DRM_DEBUG_KMS("win: %d\n", win);

	mutex_lock(&mixer_ctx->mixer_mutex);
	if (!mixer_ctx->powered) {
		mutex_unlock(&mixer_ctx->mixer_mutex);
		return;
	}
	mutex_unlock(&mixer_ctx->mixer_mutex);

	if (win > 1 && mixer_ctx->vp_enabled)
		vp_video_buffer(mixer_ctx, win);
	else
		mixer_graph_buffer(mixer_ctx, win);

	mixer_ctx->win_data[win].enabled = true;
}

static void mixer_win_disable(struct exynos_drm_crtc *crtc, int zpos)
{
	struct mixer_context *mixer_ctx = crtc->ctx;
	struct mixer_resources *res = &mixer_ctx->mixer_res;
	int win = zpos == DEFAULT_ZPOS ? MIXER_DEFAULT_WIN : zpos;
	unsigned long flags;

	DRM_DEBUG_KMS("win: %d\n", win);

	mutex_lock(&mixer_ctx->mixer_mutex);
	if (!mixer_ctx->powered) {
		mutex_unlock(&mixer_ctx->mixer_mutex);
		mixer_ctx->win_data[win].resume = false;
		return;
	}
	mutex_unlock(&mixer_ctx->mixer_mutex);

	spin_lock_irqsave(&res->reg_slock, flags);
	mixer_vsync_set_update(mixer_ctx, false);

	mixer_cfg_layer(mixer_ctx, win, false);

	mixer_vsync_set_update(mixer_ctx, true);
	spin_unlock_irqrestore(&res->reg_slock, flags);

	mixer_ctx->win_data[win].enabled = false;
}

static void mixer_wait_for_vblank(struct exynos_drm_crtc *crtc)
{
	struct mixer_context *mixer_ctx = crtc->ctx;
	int err;

	mutex_lock(&mixer_ctx->mixer_mutex);
	if (!mixer_ctx->powered) {
		mutex_unlock(&mixer_ctx->mixer_mutex);
		return;
	}
	mutex_unlock(&mixer_ctx->mixer_mutex);

	err = drm_vblank_get(mixer_ctx->drm_dev, mixer_ctx->pipe);
	if (err < 0) {
		DRM_DEBUG_KMS("failed to acquire vblank counter\n");
		return;
	}

	atomic_set(&mixer_ctx->wait_vsync_event, 1);

	/*
	 * wait for MIXER to signal VSYNC interrupt or return after
	 * timeout which is set to 50ms (refresh rate of 20).
	 */
	if (!wait_event_timeout(mixer_ctx->wait_vsync_queue,
				!atomic_read(&mixer_ctx->wait_vsync_event),
				HZ/20))
		DRM_DEBUG_KMS("vblank wait timed out.\n");

	drm_vblank_put(mixer_ctx->drm_dev, mixer_ctx->pipe);
}

static void mixer_window_suspend(struct mixer_context *ctx)
{
	struct hdmi_win_data *win_data;
	int i;

	for (i = 0; i < MIXER_WIN_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->resume = win_data->enabled;
		mixer_win_disable(ctx->crtc, i);
	}
	mixer_wait_for_vblank(ctx->crtc);
}

static void mixer_window_resume(struct mixer_context *ctx)
{
	struct hdmi_win_data *win_data;
	int i;

	for (i = 0; i < MIXER_WIN_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->enabled = win_data->resume;
		win_data->resume = false;
		if (win_data->enabled)
			mixer_win_commit(ctx->crtc, i);
	}
}

static void mixer_poweron(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	mutex_lock(&ctx->mixer_mutex);
	if (ctx->powered) {
		mutex_unlock(&ctx->mixer_mutex);
		return;
	}

	mutex_unlock(&ctx->mixer_mutex);

	pm_runtime_get_sync(ctx->dev);

	clk_prepare_enable(res->mixer);
	clk_prepare_enable(res->hdmi);
	if (ctx->vp_enabled) {
		clk_prepare_enable(res->vp);
		if (ctx->has_sclk)
			clk_prepare_enable(res->sclk_mixer);
	}

	mutex_lock(&ctx->mixer_mutex);
	ctx->powered = true;
	mutex_unlock(&ctx->mixer_mutex);

	mixer_reg_writemask(res, MXR_STATUS, ~0, MXR_STATUS_SOFT_RESET);

	mixer_reg_write(res, MXR_INT_EN, ctx->int_en);
	mixer_win_reset(ctx);

	mixer_window_resume(ctx);
}

static void mixer_poweroff(struct mixer_context *ctx)
{
	struct mixer_resources *res = &ctx->mixer_res;

	mutex_lock(&ctx->mixer_mutex);
	if (!ctx->powered) {
		mutex_unlock(&ctx->mixer_mutex);
		return;
	}
	mutex_unlock(&ctx->mixer_mutex);

	mixer_stop(ctx);
	mixer_window_suspend(ctx);

	ctx->int_en = mixer_reg_read(res, MXR_INT_EN);

	mutex_lock(&ctx->mixer_mutex);
	ctx->powered = false;
	mutex_unlock(&ctx->mixer_mutex);

	clk_disable_unprepare(res->hdmi);
	clk_disable_unprepare(res->mixer);
	if (ctx->vp_enabled) {
		clk_disable_unprepare(res->vp);
		if (ctx->has_sclk)
			clk_disable_unprepare(res->sclk_mixer);
	}

	pm_runtime_put_sync(ctx->dev);
}

static void mixer_dpms(struct exynos_drm_crtc *crtc, int mode)
{
	switch (mode) {
	case DRM_MODE_DPMS_ON:
		mixer_poweron(crtc->ctx);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		mixer_poweroff(crtc->ctx);
		break;
	default:
		DRM_DEBUG_KMS("unknown dpms mode: %d\n", mode);
		break;
	}
}

/* Only valid for Mixer version 16.0.33.0 */
int mixer_check_mode(struct drm_display_mode *mode)
{
	u32 w, h;

	w = mode->hdisplay;
	h = mode->vdisplay;

	DRM_DEBUG_KMS("xres=%d, yres=%d, refresh=%d, intl=%d\n",
		mode->hdisplay, mode->vdisplay, mode->vrefresh,
		(mode->flags & DRM_MODE_FLAG_INTERLACE) ? 1 : 0);

	if ((w >= 464 && w <= 720 && h >= 261 && h <= 576) ||
		(w >= 1024 && w <= 1280 && h >= 576 && h <= 720) ||
		(w >= 1664 && w <= 1920 && h >= 936 && h <= 1080))
		return 0;

	return -EINVAL;
}

static struct exynos_drm_crtc_ops mixer_crtc_ops = {
	.dpms			= mixer_dpms,
	.enable_vblank		= mixer_enable_vblank,
	.disable_vblank		= mixer_disable_vblank,
	.wait_for_vblank	= mixer_wait_for_vblank,
	.win_mode_set		= mixer_win_mode_set,
	.win_commit		= mixer_win_commit,
	.win_disable		= mixer_win_disable,
};

static struct mixer_drv_data exynos5420_mxr_drv_data = {
	.version = MXR_VER_128_0_0_184,
	.is_vp_enabled = 0,
};

static struct mixer_drv_data exynos5250_mxr_drv_data = {
	.version = MXR_VER_16_0_33_0,
	.is_vp_enabled = 0,
};

static struct mixer_drv_data exynos4212_mxr_drv_data = {
	.version = MXR_VER_0_0_0_16,
	.is_vp_enabled = 1,
};

static struct mixer_drv_data exynos4210_mxr_drv_data = {
	.version = MXR_VER_0_0_0_16,
	.is_vp_enabled = 1,
	.has_sclk = 1,
};

static struct platform_device_id mixer_driver_types[] = {
	{
		.name		= "s5p-mixer",
		.driver_data	= (unsigned long)&exynos4210_mxr_drv_data,
	}, {
		.name		= "exynos5-mixer",
		.driver_data	= (unsigned long)&exynos5250_mxr_drv_data,
	}, {
		/* end node */
	}
};

static struct of_device_id mixer_match_types[] = {
	{
		.compatible = "samsung,exynos4210-mixer",
		.data	= &exynos4210_mxr_drv_data,
	}, {
		.compatible = "samsung,exynos4212-mixer",
		.data	= &exynos4212_mxr_drv_data,
	}, {
		.compatible = "samsung,exynos5-mixer",
		.data	= &exynos5250_mxr_drv_data,
	}, {
		.compatible = "samsung,exynos5250-mixer",
		.data	= &exynos5250_mxr_drv_data,
	}, {
		.compatible = "samsung,exynos5420-mixer",
		.data	= &exynos5420_mxr_drv_data,
	}, {
		/* end node */
	}
};
MODULE_DEVICE_TABLE(of, mixer_match_types);

static int mixer_bind(struct device *dev, struct device *manager, void *data)
{
	struct mixer_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	int ret;

	ret = mixer_initialize(ctx, drm_dev);
	if (ret)
		return ret;

	ctx->crtc = exynos_drm_crtc_create(drm_dev, ctx->pipe,
				     EXYNOS_DISPLAY_TYPE_HDMI,
				     &mixer_crtc_ops, ctx);
	if (IS_ERR(ctx->crtc)) {
		mixer_ctx_remove(ctx);
		ret = PTR_ERR(ctx->crtc);
		goto free_ctx;
	}

	return 0;

free_ctx:
	devm_kfree(dev, ctx);
	return ret;
}

static void mixer_unbind(struct device *dev, struct device *master, void *data)
{
	struct mixer_context *ctx = dev_get_drvdata(dev);

	mixer_ctx_remove(ctx);
}

static const struct component_ops mixer_component_ops = {
	.bind	= mixer_bind,
	.unbind	= mixer_unbind,
};

static int mixer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mixer_drv_data *drv;
	struct mixer_context *ctx;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		DRM_ERROR("failed to alloc mixer context.\n");
		return -ENOMEM;
	}

	mutex_init(&ctx->mixer_mutex);

	if (dev->of_node) {
		const struct of_device_id *match;

		match = of_match_node(mixer_match_types, dev->of_node);
		drv = (struct mixer_drv_data *)match->data;
	} else {
		drv = (struct mixer_drv_data *)
			platform_get_device_id(pdev)->driver_data;
	}

	ctx->pdev = pdev;
	ctx->dev = dev;
	ctx->vp_enabled = drv->is_vp_enabled;
	ctx->has_sclk = drv->has_sclk;
	ctx->mxr_ver = drv->version;
	init_waitqueue_head(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	platform_set_drvdata(pdev, ctx);

	ret = exynos_drm_component_add(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC,
					EXYNOS_DISPLAY_TYPE_HDMI);
	if (ret)
		return ret;

	ret = component_add(&pdev->dev, &mixer_component_ops);
	if (ret) {
		exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC);
		return ret;
	}

	pm_runtime_enable(dev);

	return ret;
}

static int mixer_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	component_del(&pdev->dev, &mixer_component_ops);
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC);

	return 0;
}

struct platform_driver mixer_driver = {
	.driver = {
		.name = "exynos-mixer",
		.owner = THIS_MODULE,
		.of_match_table = mixer_match_types,
	},
	.probe = mixer_probe,
	.remove = mixer_remove,
	.id_table	= mixer_driver_types,
};
