/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Authors:
 *	Eunchul Kim <chulspro.kim@samsung.com>
 *	Jinyoung Jeon <jy0.jeon@samsung.com>
 *	Sangmin Lee <lsmin.lee@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include <drm/exynos_drm.h>
#include "regs-fimc.h"
#include "exynos_drm_ipp.h"
#include "exynos_drm_fimc.h"

/*
 * FIMC stands for Fully Interactive Mobile Camera and
 * supports image scaler/rotator and input/output DMA operations.
 * input DMA reads image data from the memory.
 * output DMA writes image data to memory.
 * FIMC supports image rotation and image effect functions.
 *
 * M2M operation : supports crop/scale/rotation/csc so on.
 * Memory ----> FIMC H/W ----> Memory.
 * Writeback operation : supports cloned screen with FIMD.
 * FIMD ----> FIMC H/W ----> Memory.
 * Output operation : supports direct display using local path.
 * Memory ----> FIMC H/W ----> FIMD.
 */

/*
 * TODO
 * 1. check suspend/resume api if needed.
 * 2. need to check use case platform_device_id.
 * 3. check src/dst size with, height.
 * 4. added check_prepare api for right register.
 * 5. need to add supported list in prop_list.
 * 6. check prescaler/scaler optimization.
 */

#define FIMC_MAX_DEVS	4
#define FIMC_MAX_SRC	2
#define FIMC_MAX_DST	32
#define FIMC_SHFACTOR	10
#define FIMC_BUF_STOP	1
#define FIMC_BUF_START	2
#define FIMC_REG_SZ		32
#define FIMC_WIDTH_ITU_709	1280
#define FIMC_REFRESH_MAX	60
#define FIMC_REFRESH_MIN	12
#define FIMC_CROP_MAX	8192
#define FIMC_CROP_MIN	32
#define FIMC_SCALE_MAX	4224
#define FIMC_SCALE_MIN	32

#define get_fimc_context(dev)	platform_get_drvdata(to_platform_device(dev))
#define get_ctx_from_ippdrv(ippdrv)	container_of(ippdrv,\
					struct fimc_context, ippdrv);
#define fimc_read(offset)		readl(ctx->regs + (offset))
#define fimc_write(cfg, offset)	writel(cfg, ctx->regs + (offset))

enum fimc_wb {
	FIMC_WB_NONE,
	FIMC_WB_A,
	FIMC_WB_B,
};

enum {
	FIMC_CLK_LCLK,
	FIMC_CLK_GATE,
	FIMC_CLK_WB_A,
	FIMC_CLK_WB_B,
	FIMC_CLK_MUX,
	FIMC_CLK_PARENT,
	FIMC_CLKS_MAX
};

static const char * const fimc_clock_names[] = {
	[FIMC_CLK_LCLK]   = "sclk_fimc",
	[FIMC_CLK_GATE]   = "fimc",
	[FIMC_CLK_WB_A]   = "pxl_async0",
	[FIMC_CLK_WB_B]   = "pxl_async1",
	[FIMC_CLK_MUX]    = "mux",
	[FIMC_CLK_PARENT] = "parent",
};

#define FIMC_DEFAULT_LCLK_FREQUENCY 133000000UL

/*
 * A structure of scaler.
 *
 * @range: narrow, wide.
 * @bypass: unused scaler path.
 * @up_h: horizontal scale up.
 * @up_v: vertical scale up.
 * @hratio: horizontal ratio.
 * @vratio: vertical ratio.
 */
struct fimc_scaler {
	bool	range;
	bool bypass;
	bool up_h;
	bool up_v;
	u32 hratio;
	u32 vratio;
};

/*
 * A structure of scaler capability.
 *
 * find user manual table 43-1.
 * @in_hori: scaler input horizontal size.
 * @bypass: scaler bypass mode.
 * @dst_h_wo_rot: target horizontal size without output rotation.
 * @dst_h_rot: target horizontal size with output rotation.
 * @rl_w_wo_rot: real width without input rotation.
 * @rl_h_rot: real height without output rotation.
 */
struct fimc_capability {
	/* scaler */
	u32	in_hori;
	u32	bypass;
	/* output rotator */
	u32	dst_h_wo_rot;
	u32	dst_h_rot;
	/* input rotator */
	u32	rl_w_wo_rot;
	u32	rl_h_rot;
};

/*
 * A structure of fimc context.
 *
 * @ippdrv: prepare initialization using ippdrv.
 * @regs_res: register resources.
 * @regs: memory mapped io registers.
 * @lock: locking of operations.
 * @clocks: fimc clocks.
 * @clk_frequency: LCLK clock frequency.
 * @sysreg: handle to SYSREG block regmap.
 * @sc: scaler infomations.
 * @pol: porarity of writeback.
 * @id: fimc id.
 * @irq: irq number.
 * @suspended: qos operations.
 */
struct fimc_context {
	struct exynos_drm_ippdrv	ippdrv;
	struct resource	*regs_res;
	void __iomem	*regs;
	struct mutex	lock;
	struct clk	*clocks[FIMC_CLKS_MAX];
	u32		clk_frequency;
	struct regmap	*sysreg;
	struct fimc_scaler	sc;
	struct exynos_drm_ipp_pol	pol;
	int	id;
	int	irq;
	bool	suspended;
};

static void fimc_sw_reset(struct fimc_context *ctx)
{
	u32 cfg;

	/* stop dma operation */
	cfg = fimc_read(EXYNOS_CISTATUS);
	if (EXYNOS_CISTATUS_GET_ENVID_STATUS(cfg)) {
		cfg = fimc_read(EXYNOS_MSCTRL);
		cfg &= ~EXYNOS_MSCTRL_ENVID;
		fimc_write(cfg, EXYNOS_MSCTRL);
	}

	cfg = fimc_read(EXYNOS_CISRCFMT);
	cfg |= EXYNOS_CISRCFMT_ITU601_8BIT;
	fimc_write(cfg, EXYNOS_CISRCFMT);

	/* disable image capture */
	cfg = fimc_read(EXYNOS_CIIMGCPT);
	cfg &= ~(EXYNOS_CIIMGCPT_IMGCPTEN_SC | EXYNOS_CIIMGCPT_IMGCPTEN);
	fimc_write(cfg, EXYNOS_CIIMGCPT);

	/* s/w reset */
	cfg = fimc_read(EXYNOS_CIGCTRL);
	cfg |= (EXYNOS_CIGCTRL_SWRST);
	fimc_write(cfg, EXYNOS_CIGCTRL);

	/* s/w reset complete */
	cfg = fimc_read(EXYNOS_CIGCTRL);
	cfg &= ~EXYNOS_CIGCTRL_SWRST;
	fimc_write(cfg, EXYNOS_CIGCTRL);

	/* reset sequence */
	fimc_write(0x0, EXYNOS_CIFCNTSEQ);
}

static int fimc_set_camblk_fimd0_wb(struct fimc_context *ctx)
{
	return regmap_update_bits(ctx->sysreg, SYSREG_CAMERA_BLK,
				  SYSREG_FIMD0WB_DEST_MASK,
				  ctx->id << SYSREG_FIMD0WB_DEST_SHIFT);
}

static void fimc_set_type_ctrl(struct fimc_context *ctx, enum fimc_wb wb)
{
	u32 cfg;

	DRM_DEBUG_KMS("wb[%d]\n", wb);

	cfg = fimc_read(EXYNOS_CIGCTRL);
	cfg &= ~(EXYNOS_CIGCTRL_TESTPATTERN_MASK |
		EXYNOS_CIGCTRL_SELCAM_ITU_MASK |
		EXYNOS_CIGCTRL_SELCAM_MIPI_MASK |
		EXYNOS_CIGCTRL_SELCAM_FIMC_MASK |
		EXYNOS_CIGCTRL_SELWB_CAMIF_MASK |
		EXYNOS_CIGCTRL_SELWRITEBACK_MASK);

	switch (wb) {
	case FIMC_WB_A:
		cfg |= (EXYNOS_CIGCTRL_SELWRITEBACK_A |
			EXYNOS_CIGCTRL_SELWB_CAMIF_WRITEBACK);
		break;
	case FIMC_WB_B:
		cfg |= (EXYNOS_CIGCTRL_SELWRITEBACK_B |
			EXYNOS_CIGCTRL_SELWB_CAMIF_WRITEBACK);
		break;
	case FIMC_WB_NONE:
	default:
		cfg |= (EXYNOS_CIGCTRL_SELCAM_ITU_A |
			EXYNOS_CIGCTRL_SELWRITEBACK_A |
			EXYNOS_CIGCTRL_SELCAM_MIPI_A |
			EXYNOS_CIGCTRL_SELCAM_FIMC_ITU);
		break;
	}

	fimc_write(cfg, EXYNOS_CIGCTRL);
}

static void fimc_set_polarity(struct fimc_context *ctx,
		struct exynos_drm_ipp_pol *pol)
{
	u32 cfg;

	DRM_DEBUG_KMS("inv_pclk[%d]inv_vsync[%d]\n",
		pol->inv_pclk, pol->inv_vsync);
	DRM_DEBUG_KMS("inv_href[%d]inv_hsync[%d]\n",
		pol->inv_href, pol->inv_hsync);

	cfg = fimc_read(EXYNOS_CIGCTRL);
	cfg &= ~(EXYNOS_CIGCTRL_INVPOLPCLK | EXYNOS_CIGCTRL_INVPOLVSYNC |
		 EXYNOS_CIGCTRL_INVPOLHREF | EXYNOS_CIGCTRL_INVPOLHSYNC);

	if (pol->inv_pclk)
		cfg |= EXYNOS_CIGCTRL_INVPOLPCLK;
	if (pol->inv_vsync)
		cfg |= EXYNOS_CIGCTRL_INVPOLVSYNC;
	if (pol->inv_href)
		cfg |= EXYNOS_CIGCTRL_INVPOLHREF;
	if (pol->inv_hsync)
		cfg |= EXYNOS_CIGCTRL_INVPOLHSYNC;

	fimc_write(cfg, EXYNOS_CIGCTRL);
}

static void fimc_handle_jpeg(struct fimc_context *ctx, bool enable)
{
	u32 cfg;

	DRM_DEBUG_KMS("enable[%d]\n", enable);

	cfg = fimc_read(EXYNOS_CIGCTRL);
	if (enable)
		cfg |= EXYNOS_CIGCTRL_CAM_JPEG;
	else
		cfg &= ~EXYNOS_CIGCTRL_CAM_JPEG;

	fimc_write(cfg, EXYNOS_CIGCTRL);
}

static void fimc_handle_irq(struct fimc_context *ctx, bool enable,
		bool overflow, bool level)
{
	u32 cfg;

	DRM_DEBUG_KMS("enable[%d]overflow[%d]level[%d]\n",
			enable, overflow, level);

	cfg = fimc_read(EXYNOS_CIGCTRL);
	if (enable) {
		cfg &= ~(EXYNOS_CIGCTRL_IRQ_OVFEN | EXYNOS_CIGCTRL_IRQ_LEVEL);
		cfg |= EXYNOS_CIGCTRL_IRQ_ENABLE;
		if (overflow)
			cfg |= EXYNOS_CIGCTRL_IRQ_OVFEN;
		if (level)
			cfg |= EXYNOS_CIGCTRL_IRQ_LEVEL;
	} else
		cfg &= ~(EXYNOS_CIGCTRL_IRQ_OVFEN | EXYNOS_CIGCTRL_IRQ_ENABLE);

	fimc_write(cfg, EXYNOS_CIGCTRL);
}

static void fimc_clear_irq(struct fimc_context *ctx)
{
	u32 cfg;

	cfg = fimc_read(EXYNOS_CIGCTRL);
	cfg |= EXYNOS_CIGCTRL_IRQ_CLR;
	fimc_write(cfg, EXYNOS_CIGCTRL);
}

static bool fimc_check_ovf(struct fimc_context *ctx)
{
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg, status, flag;

	status = fimc_read(EXYNOS_CISTATUS);
	flag = EXYNOS_CISTATUS_OVFIY | EXYNOS_CISTATUS_OVFICB |
		EXYNOS_CISTATUS_OVFICR;

	DRM_DEBUG_KMS("flag[0x%x]\n", flag);

	if (status & flag) {
		cfg = fimc_read(EXYNOS_CIWDOFST);
		cfg |= (EXYNOS_CIWDOFST_CLROVFIY | EXYNOS_CIWDOFST_CLROVFICB |
			EXYNOS_CIWDOFST_CLROVFICR);

		fimc_write(cfg, EXYNOS_CIWDOFST);

		cfg = fimc_read(EXYNOS_CIWDOFST);
		cfg &= ~(EXYNOS_CIWDOFST_CLROVFIY | EXYNOS_CIWDOFST_CLROVFICB |
			EXYNOS_CIWDOFST_CLROVFICR);

		fimc_write(cfg, EXYNOS_CIWDOFST);

		dev_err(ippdrv->dev, "occured overflow at %d, status 0x%x.\n",
			ctx->id, status);
		return true;
	}

	return false;
}

static bool fimc_check_frame_end(struct fimc_context *ctx)
{
	u32 cfg;

	cfg = fimc_read(EXYNOS_CISTATUS);

	DRM_DEBUG_KMS("cfg[0x%x]\n", cfg);

	if (!(cfg & EXYNOS_CISTATUS_FRAMEEND))
		return false;

	cfg &= ~(EXYNOS_CISTATUS_FRAMEEND);
	fimc_write(cfg, EXYNOS_CISTATUS);

	return true;
}

static int fimc_get_buf_id(struct fimc_context *ctx)
{
	u32 cfg;
	int frame_cnt, buf_id;

	cfg = fimc_read(EXYNOS_CISTATUS2);
	frame_cnt = EXYNOS_CISTATUS2_GET_FRAMECOUNT_BEFORE(cfg);

	if (frame_cnt == 0)
		frame_cnt = EXYNOS_CISTATUS2_GET_FRAMECOUNT_PRESENT(cfg);

	DRM_DEBUG_KMS("present[%d]before[%d]\n",
		EXYNOS_CISTATUS2_GET_FRAMECOUNT_PRESENT(cfg),
		EXYNOS_CISTATUS2_GET_FRAMECOUNT_BEFORE(cfg));

	if (frame_cnt == 0) {
		DRM_ERROR("failed to get frame count.\n");
		return -EIO;
	}

	buf_id = frame_cnt - 1;
	DRM_DEBUG_KMS("buf_id[%d]\n", buf_id);

	return buf_id;
}

static void fimc_handle_lastend(struct fimc_context *ctx, bool enable)
{
	u32 cfg;

	DRM_DEBUG_KMS("enable[%d]\n", enable);

	cfg = fimc_read(EXYNOS_CIOCTRL);
	if (enable)
		cfg |= EXYNOS_CIOCTRL_LASTENDEN;
	else
		cfg &= ~EXYNOS_CIOCTRL_LASTENDEN;

	fimc_write(cfg, EXYNOS_CIOCTRL);
}


static int fimc_src_set_fmt_order(struct fimc_context *ctx, u32 fmt)
{
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg;

	DRM_DEBUG_KMS("fmt[0x%x]\n", fmt);

	/* RGB */
	cfg = fimc_read(EXYNOS_CISCCTRL);
	cfg &= ~EXYNOS_CISCCTRL_INRGB_FMT_RGB_MASK;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= EXYNOS_CISCCTRL_INRGB_FMT_RGB565;
		fimc_write(cfg, EXYNOS_CISCCTRL);
		return 0;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		cfg |= EXYNOS_CISCCTRL_INRGB_FMT_RGB888;
		fimc_write(cfg, EXYNOS_CISCCTRL);
		return 0;
	default:
		/* bypass */
		break;
	}

	/* YUV */
	cfg = fimc_read(EXYNOS_MSCTRL);
	cfg &= ~(EXYNOS_MSCTRL_ORDER2P_SHIFT_MASK |
		EXYNOS_MSCTRL_C_INT_IN_2PLANE |
		EXYNOS_MSCTRL_ORDER422_YCBYCR);

	switch (fmt) {
	case DRM_FORMAT_YUYV:
		cfg |= EXYNOS_MSCTRL_ORDER422_YCBYCR;
		break;
	case DRM_FORMAT_YVYU:
		cfg |= EXYNOS_MSCTRL_ORDER422_YCRYCB;
		break;
	case DRM_FORMAT_UYVY:
		cfg |= EXYNOS_MSCTRL_ORDER422_CBYCRY;
		break;
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YUV444:
		cfg |= EXYNOS_MSCTRL_ORDER422_CRYCBY;
		break;
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
		cfg |= (EXYNOS_MSCTRL_ORDER2P_LSB_CRCB |
			EXYNOS_MSCTRL_C_INT_IN_2PLANE);
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		cfg |= EXYNOS_MSCTRL_C_INT_IN_3PLANE;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV12MT:
	case DRM_FORMAT_NV16:
		cfg |= (EXYNOS_MSCTRL_ORDER2P_LSB_CBCR |
			EXYNOS_MSCTRL_C_INT_IN_2PLANE);
		break;
	default:
		dev_err(ippdrv->dev, "inavlid source yuv order 0x%x.\n", fmt);
		return -EINVAL;
	}

	fimc_write(cfg, EXYNOS_MSCTRL);

	return 0;
}

static int fimc_src_set_fmt(struct device *dev, u32 fmt)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg;

	DRM_DEBUG_KMS("fmt[0x%x]\n", fmt);

	cfg = fimc_read(EXYNOS_MSCTRL);
	cfg &= ~EXYNOS_MSCTRL_INFORMAT_RGB;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		cfg |= EXYNOS_MSCTRL_INFORMAT_RGB;
		break;
	case DRM_FORMAT_YUV444:
		cfg |= EXYNOS_MSCTRL_INFORMAT_YCBCR420;
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		cfg |= EXYNOS_MSCTRL_INFORMAT_YCBCR422_1PLANE;
		break;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YUV422:
		cfg |= EXYNOS_MSCTRL_INFORMAT_YCBCR422;
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV12MT:
		cfg |= EXYNOS_MSCTRL_INFORMAT_YCBCR420;
		break;
	default:
		dev_err(ippdrv->dev, "inavlid source format 0x%x.\n", fmt);
		return -EINVAL;
	}

	fimc_write(cfg, EXYNOS_MSCTRL);

	cfg = fimc_read(EXYNOS_CIDMAPARAM);
	cfg &= ~EXYNOS_CIDMAPARAM_R_MODE_MASK;

	if (fmt == DRM_FORMAT_NV12MT)
		cfg |= EXYNOS_CIDMAPARAM_R_MODE_64X32;
	else
		cfg |= EXYNOS_CIDMAPARAM_R_MODE_LINEAR;

	fimc_write(cfg, EXYNOS_CIDMAPARAM);

	return fimc_src_set_fmt_order(ctx, fmt);
}

static int fimc_src_set_transf(struct device *dev,
		enum drm_exynos_degree degree,
		enum drm_exynos_flip flip, bool *swap)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg1, cfg2;

	DRM_DEBUG_KMS("degree[%d]flip[0x%x]\n", degree, flip);

	cfg1 = fimc_read(EXYNOS_MSCTRL);
	cfg1 &= ~(EXYNOS_MSCTRL_FLIP_X_MIRROR |
		EXYNOS_MSCTRL_FLIP_Y_MIRROR);

	cfg2 = fimc_read(EXYNOS_CITRGFMT);
	cfg2 &= ~EXYNOS_CITRGFMT_INROT90_CLOCKWISE;

	switch (degree) {
	case EXYNOS_DRM_DEGREE_0:
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg1 |= EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg1 |= EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case EXYNOS_DRM_DEGREE_90:
		cfg2 |= EXYNOS_CITRGFMT_INROT90_CLOCKWISE;
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg1 |= EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg1 |= EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case EXYNOS_DRM_DEGREE_180:
		cfg1 |= (EXYNOS_MSCTRL_FLIP_X_MIRROR |
			EXYNOS_MSCTRL_FLIP_Y_MIRROR);
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case EXYNOS_DRM_DEGREE_270:
		cfg1 |= (EXYNOS_MSCTRL_FLIP_X_MIRROR |
			EXYNOS_MSCTRL_FLIP_Y_MIRROR);
		cfg2 |= EXYNOS_CITRGFMT_INROT90_CLOCKWISE;
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	default:
		dev_err(ippdrv->dev, "inavlid degree value %d.\n", degree);
		return -EINVAL;
	}

	fimc_write(cfg1, EXYNOS_MSCTRL);
	fimc_write(cfg2, EXYNOS_CITRGFMT);
	*swap = (cfg2 & EXYNOS_CITRGFMT_INROT90_CLOCKWISE) ? 1 : 0;

	return 0;
}

static int fimc_set_window(struct fimc_context *ctx,
		struct drm_exynos_pos *pos, struct drm_exynos_sz *sz)
{
	u32 cfg, h1, h2, v1, v2;

	/* cropped image */
	h1 = pos->x;
	h2 = sz->hsize - pos->w - pos->x;
	v1 = pos->y;
	v2 = sz->vsize - pos->h - pos->y;

	DRM_DEBUG_KMS("x[%d]y[%d]w[%d]h[%d]hsize[%d]vsize[%d]\n",
		pos->x, pos->y, pos->w, pos->h, sz->hsize, sz->vsize);
	DRM_DEBUG_KMS("h1[%d]h2[%d]v1[%d]v2[%d]\n", h1, h2, v1, v2);

	/*
	 * set window offset 1, 2 size
	 * check figure 43-21 in user manual
	 */
	cfg = fimc_read(EXYNOS_CIWDOFST);
	cfg &= ~(EXYNOS_CIWDOFST_WINHOROFST_MASK |
		EXYNOS_CIWDOFST_WINVEROFST_MASK);
	cfg |= (EXYNOS_CIWDOFST_WINHOROFST(h1) |
		EXYNOS_CIWDOFST_WINVEROFST(v1));
	cfg |= EXYNOS_CIWDOFST_WINOFSEN;
	fimc_write(cfg, EXYNOS_CIWDOFST);

	cfg = (EXYNOS_CIWDOFST2_WINHOROFST2(h2) |
		EXYNOS_CIWDOFST2_WINVEROFST2(v2));
	fimc_write(cfg, EXYNOS_CIWDOFST2);

	return 0;
}

static int fimc_src_set_size(struct device *dev, int swap,
		struct drm_exynos_pos *pos, struct drm_exynos_sz *sz)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct drm_exynos_pos img_pos = *pos;
	struct drm_exynos_sz img_sz = *sz;
	u32 cfg;

	DRM_DEBUG_KMS("swap[%d]hsize[%d]vsize[%d]\n",
		swap, sz->hsize, sz->vsize);

	/* original size */
	cfg = (EXYNOS_ORGISIZE_HORIZONTAL(img_sz.hsize) |
		EXYNOS_ORGISIZE_VERTICAL(img_sz.vsize));

	fimc_write(cfg, EXYNOS_ORGISIZE);

	DRM_DEBUG_KMS("x[%d]y[%d]w[%d]h[%d]\n", pos->x, pos->y, pos->w, pos->h);

	if (swap) {
		img_pos.w = pos->h;
		img_pos.h = pos->w;
		img_sz.hsize = sz->vsize;
		img_sz.vsize = sz->hsize;
	}

	/* set input DMA image size */
	cfg = fimc_read(EXYNOS_CIREAL_ISIZE);
	cfg &= ~(EXYNOS_CIREAL_ISIZE_HEIGHT_MASK |
		EXYNOS_CIREAL_ISIZE_WIDTH_MASK);
	cfg |= (EXYNOS_CIREAL_ISIZE_WIDTH(img_pos.w) |
		EXYNOS_CIREAL_ISIZE_HEIGHT(img_pos.h));
	fimc_write(cfg, EXYNOS_CIREAL_ISIZE);

	/*
	 * set input FIFO image size
	 * for now, we support only ITU601 8 bit mode
	 */
	cfg = (EXYNOS_CISRCFMT_ITU601_8BIT |
		EXYNOS_CISRCFMT_SOURCEHSIZE(img_sz.hsize) |
		EXYNOS_CISRCFMT_SOURCEVSIZE(img_sz.vsize));
	fimc_write(cfg, EXYNOS_CISRCFMT);

	/* offset Y(RGB), Cb, Cr */
	cfg = (EXYNOS_CIIYOFF_HORIZONTAL(img_pos.x) |
		EXYNOS_CIIYOFF_VERTICAL(img_pos.y));
	fimc_write(cfg, EXYNOS_CIIYOFF);
	cfg = (EXYNOS_CIICBOFF_HORIZONTAL(img_pos.x) |
		EXYNOS_CIICBOFF_VERTICAL(img_pos.y));
	fimc_write(cfg, EXYNOS_CIICBOFF);
	cfg = (EXYNOS_CIICROFF_HORIZONTAL(img_pos.x) |
		EXYNOS_CIICROFF_VERTICAL(img_pos.y));
	fimc_write(cfg, EXYNOS_CIICROFF);

	return fimc_set_window(ctx, &img_pos, &img_sz);
}

static int fimc_src_set_addr(struct device *dev,
		struct drm_exynos_ipp_buf_info *buf_info, u32 buf_id,
		enum drm_exynos_ipp_buf_type buf_type)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	struct drm_exynos_ipp_cmd_node *c_node = ippdrv->c_node;
	struct drm_exynos_ipp_property *property;
	struct drm_exynos_ipp_config *config;

	if (!c_node) {
		DRM_ERROR("failed to get c_node.\n");
		return -EINVAL;
	}

	property = &c_node->property;

	DRM_DEBUG_KMS("prop_id[%d]buf_id[%d]buf_type[%d]\n",
		property->prop_id, buf_id, buf_type);

	if (buf_id > FIMC_MAX_SRC) {
		dev_info(ippdrv->dev, "inavlid buf_id %d.\n", buf_id);
		return -ENOMEM;
	}

	/* address register set */
	switch (buf_type) {
	case IPP_BUF_ENQUEUE:
		config = &property->config[EXYNOS_DRM_OPS_SRC];
		fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_Y],
			EXYNOS_CIIYSA(buf_id));

		if (config->fmt == DRM_FORMAT_YVU420) {
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CR],
				EXYNOS_CIICBSA(buf_id));
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CB],
				EXYNOS_CIICRSA(buf_id));
		} else {
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CB],
				EXYNOS_CIICBSA(buf_id));
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CR],
				EXYNOS_CIICRSA(buf_id));
		}
		break;
	case IPP_BUF_DEQUEUE:
		fimc_write(0x0, EXYNOS_CIIYSA(buf_id));
		fimc_write(0x0, EXYNOS_CIICBSA(buf_id));
		fimc_write(0x0, EXYNOS_CIICRSA(buf_id));
		break;
	default:
		/* bypass */
		break;
	}

	return 0;
}

static struct exynos_drm_ipp_ops fimc_src_ops = {
	.set_fmt = fimc_src_set_fmt,
	.set_transf = fimc_src_set_transf,
	.set_size = fimc_src_set_size,
	.set_addr = fimc_src_set_addr,
};

static int fimc_dst_set_fmt_order(struct fimc_context *ctx, u32 fmt)
{
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg;

	DRM_DEBUG_KMS("fmt[0x%x]\n", fmt);

	/* RGB */
	cfg = fimc_read(EXYNOS_CISCCTRL);
	cfg &= ~EXYNOS_CISCCTRL_OUTRGB_FMT_RGB_MASK;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= EXYNOS_CISCCTRL_OUTRGB_FMT_RGB565;
		fimc_write(cfg, EXYNOS_CISCCTRL);
		return 0;
	case DRM_FORMAT_RGB888:
		cfg |= EXYNOS_CISCCTRL_OUTRGB_FMT_RGB888;
		fimc_write(cfg, EXYNOS_CISCCTRL);
		return 0;
	case DRM_FORMAT_XRGB8888:
		cfg |= (EXYNOS_CISCCTRL_OUTRGB_FMT_RGB888 |
			EXYNOS_CISCCTRL_EXTRGB_EXTENSION);
		fimc_write(cfg, EXYNOS_CISCCTRL);
		break;
	default:
		/* bypass */
		break;
	}

	/* YUV */
	cfg = fimc_read(EXYNOS_CIOCTRL);
	cfg &= ~(EXYNOS_CIOCTRL_ORDER2P_MASK |
		EXYNOS_CIOCTRL_ORDER422_MASK |
		EXYNOS_CIOCTRL_YCBCR_PLANE_MASK);

	switch (fmt) {
	case DRM_FORMAT_XRGB8888:
		cfg |= EXYNOS_CIOCTRL_ALPHA_OUT;
		break;
	case DRM_FORMAT_YUYV:
		cfg |= EXYNOS_CIOCTRL_ORDER422_YCBYCR;
		break;
	case DRM_FORMAT_YVYU:
		cfg |= EXYNOS_CIOCTRL_ORDER422_YCRYCB;
		break;
	case DRM_FORMAT_UYVY:
		cfg |= EXYNOS_CIOCTRL_ORDER422_CBYCRY;
		break;
	case DRM_FORMAT_VYUY:
		cfg |= EXYNOS_CIOCTRL_ORDER422_CRYCBY;
		break;
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
		cfg |= EXYNOS_CIOCTRL_ORDER2P_LSB_CRCB;
		cfg |= EXYNOS_CIOCTRL_YCBCR_2PLANE;
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		cfg |= EXYNOS_CIOCTRL_YCBCR_3PLANE;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV12MT:
	case DRM_FORMAT_NV16:
		cfg |= EXYNOS_CIOCTRL_ORDER2P_LSB_CBCR;
		cfg |= EXYNOS_CIOCTRL_YCBCR_2PLANE;
		break;
	default:
		dev_err(ippdrv->dev, "inavlid target yuv order 0x%x.\n", fmt);
		return -EINVAL;
	}

	fimc_write(cfg, EXYNOS_CIOCTRL);

	return 0;
}

static int fimc_dst_set_fmt(struct device *dev, u32 fmt)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg;

	DRM_DEBUG_KMS("fmt[0x%x]\n", fmt);

	cfg = fimc_read(EXYNOS_CIEXTEN);

	if (fmt == DRM_FORMAT_AYUV) {
		cfg |= EXYNOS_CIEXTEN_YUV444_OUT;
		fimc_write(cfg, EXYNOS_CIEXTEN);
	} else {
		cfg &= ~EXYNOS_CIEXTEN_YUV444_OUT;
		fimc_write(cfg, EXYNOS_CIEXTEN);

		cfg = fimc_read(EXYNOS_CITRGFMT);
		cfg &= ~EXYNOS_CITRGFMT_OUTFORMAT_MASK;

		switch (fmt) {
		case DRM_FORMAT_RGB565:
		case DRM_FORMAT_RGB888:
		case DRM_FORMAT_XRGB8888:
			cfg |= EXYNOS_CITRGFMT_OUTFORMAT_RGB;
			break;
		case DRM_FORMAT_YUYV:
		case DRM_FORMAT_YVYU:
		case DRM_FORMAT_UYVY:
		case DRM_FORMAT_VYUY:
			cfg |= EXYNOS_CITRGFMT_OUTFORMAT_YCBCR422_1PLANE;
			break;
		case DRM_FORMAT_NV16:
		case DRM_FORMAT_NV61:
		case DRM_FORMAT_YUV422:
			cfg |= EXYNOS_CITRGFMT_OUTFORMAT_YCBCR422;
			break;
		case DRM_FORMAT_YUV420:
		case DRM_FORMAT_YVU420:
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_NV12MT:
		case DRM_FORMAT_NV21:
			cfg |= EXYNOS_CITRGFMT_OUTFORMAT_YCBCR420;
			break;
		default:
			dev_err(ippdrv->dev, "inavlid target format 0x%x.\n",
				fmt);
			return -EINVAL;
		}

		fimc_write(cfg, EXYNOS_CITRGFMT);
	}

	cfg = fimc_read(EXYNOS_CIDMAPARAM);
	cfg &= ~EXYNOS_CIDMAPARAM_W_MODE_MASK;

	if (fmt == DRM_FORMAT_NV12MT)
		cfg |= EXYNOS_CIDMAPARAM_W_MODE_64X32;
	else
		cfg |= EXYNOS_CIDMAPARAM_W_MODE_LINEAR;

	fimc_write(cfg, EXYNOS_CIDMAPARAM);

	return fimc_dst_set_fmt_order(ctx, fmt);
}

static int fimc_dst_set_transf(struct device *dev,
		enum drm_exynos_degree degree,
		enum drm_exynos_flip flip, bool *swap)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg;

	DRM_DEBUG_KMS("degree[%d]flip[0x%x]\n", degree, flip);

	cfg = fimc_read(EXYNOS_CITRGFMT);
	cfg &= ~EXYNOS_CITRGFMT_FLIP_MASK;
	cfg &= ~EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE;

	switch (degree) {
	case EXYNOS_DRM_DEGREE_0:
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg |= EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg |= EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case EXYNOS_DRM_DEGREE_90:
		cfg |= EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE;
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg |= EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg |= EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case EXYNOS_DRM_DEGREE_180:
		cfg |= (EXYNOS_CITRGFMT_FLIP_X_MIRROR |
			EXYNOS_CITRGFMT_FLIP_Y_MIRROR);
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case EXYNOS_DRM_DEGREE_270:
		cfg |= (EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE |
			EXYNOS_CITRGFMT_FLIP_X_MIRROR |
			EXYNOS_CITRGFMT_FLIP_Y_MIRROR);
		if (flip & EXYNOS_DRM_FLIP_VERTICAL)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (flip & EXYNOS_DRM_FLIP_HORIZONTAL)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	default:
		dev_err(ippdrv->dev, "inavlid degree value %d.\n", degree);
		return -EINVAL;
	}

	fimc_write(cfg, EXYNOS_CITRGFMT);
	*swap = (cfg & EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE) ? 1 : 0;

	return 0;
}

static int fimc_get_ratio_shift(u32 src, u32 dst, u32 *ratio, u32 *shift)
{
	DRM_DEBUG_KMS("src[%d]dst[%d]\n", src, dst);

	if (src >= dst * 64) {
		DRM_ERROR("failed to make ratio and shift.\n");
		return -EINVAL;
	} else if (src >= dst * 32) {
		*ratio = 32;
		*shift = 5;
	} else if (src >= dst * 16) {
		*ratio = 16;
		*shift = 4;
	} else if (src >= dst * 8) {
		*ratio = 8;
		*shift = 3;
	} else if (src >= dst * 4) {
		*ratio = 4;
		*shift = 2;
	} else if (src >= dst * 2) {
		*ratio = 2;
		*shift = 1;
	} else {
		*ratio = 1;
		*shift = 0;
	}

	return 0;
}

static int fimc_set_prescaler(struct fimc_context *ctx, struct fimc_scaler *sc,
		struct drm_exynos_pos *src, struct drm_exynos_pos *dst)
{
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	u32 cfg, cfg_ext, shfactor;
	u32 pre_dst_width, pre_dst_height;
	u32 pre_hratio, hfactor, pre_vratio, vfactor;
	int ret = 0;
	u32 src_w, src_h, dst_w, dst_h;

	cfg_ext = fimc_read(EXYNOS_CITRGFMT);
	if (cfg_ext & EXYNOS_CITRGFMT_INROT90_CLOCKWISE) {
		src_w = src->h;
		src_h = src->w;
	} else {
		src_w = src->w;
		src_h = src->h;
	}

	if (cfg_ext & EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE) {
		dst_w = dst->h;
		dst_h = dst->w;
	} else {
		dst_w = dst->w;
		dst_h = dst->h;
	}

	ret = fimc_get_ratio_shift(src_w, dst_w, &pre_hratio, &hfactor);
	if (ret) {
		dev_err(ippdrv->dev, "failed to get ratio horizontal.\n");
		return ret;
	}

	ret = fimc_get_ratio_shift(src_h, dst_h, &pre_vratio, &vfactor);
	if (ret) {
		dev_err(ippdrv->dev, "failed to get ratio vertical.\n");
		return ret;
	}

	pre_dst_width = src_w / pre_hratio;
	pre_dst_height = src_h / pre_vratio;
	DRM_DEBUG_KMS("pre_dst_width[%d]pre_dst_height[%d]\n",
		pre_dst_width, pre_dst_height);
	DRM_DEBUG_KMS("pre_hratio[%d]hfactor[%d]pre_vratio[%d]vfactor[%d]\n",
		pre_hratio, hfactor, pre_vratio, vfactor);

	sc->hratio = (src_w << 14) / (dst_w << hfactor);
	sc->vratio = (src_h << 14) / (dst_h << vfactor);
	sc->up_h = (dst_w >= src_w) ? true : false;
	sc->up_v = (dst_h >= src_h) ? true : false;
	DRM_DEBUG_KMS("hratio[%d]vratio[%d]up_h[%d]up_v[%d]\n",
		sc->hratio, sc->vratio, sc->up_h, sc->up_v);

	shfactor = FIMC_SHFACTOR - (hfactor + vfactor);
	DRM_DEBUG_KMS("shfactor[%d]\n", shfactor);

	cfg = (EXYNOS_CISCPRERATIO_SHFACTOR(shfactor) |
		EXYNOS_CISCPRERATIO_PREHORRATIO(pre_hratio) |
		EXYNOS_CISCPRERATIO_PREVERRATIO(pre_vratio));
	fimc_write(cfg, EXYNOS_CISCPRERATIO);

	cfg = (EXYNOS_CISCPREDST_PREDSTWIDTH(pre_dst_width) |
		EXYNOS_CISCPREDST_PREDSTHEIGHT(pre_dst_height));
	fimc_write(cfg, EXYNOS_CISCPREDST);

	return ret;
}

static void fimc_set_scaler(struct fimc_context *ctx, struct fimc_scaler *sc)
{
	u32 cfg, cfg_ext;

	DRM_DEBUG_KMS("range[%d]bypass[%d]up_h[%d]up_v[%d]\n",
		sc->range, sc->bypass, sc->up_h, sc->up_v);
	DRM_DEBUG_KMS("hratio[%d]vratio[%d]\n",
		sc->hratio, sc->vratio);

	cfg = fimc_read(EXYNOS_CISCCTRL);
	cfg &= ~(EXYNOS_CISCCTRL_SCALERBYPASS |
		EXYNOS_CISCCTRL_SCALEUP_H | EXYNOS_CISCCTRL_SCALEUP_V |
		EXYNOS_CISCCTRL_MAIN_V_RATIO_MASK |
		EXYNOS_CISCCTRL_MAIN_H_RATIO_MASK |
		EXYNOS_CISCCTRL_CSCR2Y_WIDE |
		EXYNOS_CISCCTRL_CSCY2R_WIDE);

	if (sc->range)
		cfg |= (EXYNOS_CISCCTRL_CSCR2Y_WIDE |
			EXYNOS_CISCCTRL_CSCY2R_WIDE);
	if (sc->bypass)
		cfg |= EXYNOS_CISCCTRL_SCALERBYPASS;
	if (sc->up_h)
		cfg |= EXYNOS_CISCCTRL_SCALEUP_H;
	if (sc->up_v)
		cfg |= EXYNOS_CISCCTRL_SCALEUP_V;

	cfg |= (EXYNOS_CISCCTRL_MAINHORRATIO((sc->hratio >> 6)) |
		EXYNOS_CISCCTRL_MAINVERRATIO((sc->vratio >> 6)));
	fimc_write(cfg, EXYNOS_CISCCTRL);

	cfg_ext = fimc_read(EXYNOS_CIEXTEN);
	cfg_ext &= ~EXYNOS_CIEXTEN_MAINHORRATIO_EXT_MASK;
	cfg_ext &= ~EXYNOS_CIEXTEN_MAINVERRATIO_EXT_MASK;
	cfg_ext |= (EXYNOS_CIEXTEN_MAINHORRATIO_EXT(sc->hratio) |
		EXYNOS_CIEXTEN_MAINVERRATIO_EXT(sc->vratio));
	fimc_write(cfg_ext, EXYNOS_CIEXTEN);
}

static int fimc_dst_set_size(struct device *dev, int swap,
		struct drm_exynos_pos *pos, struct drm_exynos_sz *sz)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct drm_exynos_pos img_pos = *pos;
	struct drm_exynos_sz img_sz = *sz;
	u32 cfg;

	DRM_DEBUG_KMS("swap[%d]hsize[%d]vsize[%d]\n",
		swap, sz->hsize, sz->vsize);

	/* original size */
	cfg = (EXYNOS_ORGOSIZE_HORIZONTAL(img_sz.hsize) |
		EXYNOS_ORGOSIZE_VERTICAL(img_sz.vsize));

	fimc_write(cfg, EXYNOS_ORGOSIZE);

	DRM_DEBUG_KMS("x[%d]y[%d]w[%d]h[%d]\n", pos->x, pos->y, pos->w, pos->h);

	/* CSC ITU */
	cfg = fimc_read(EXYNOS_CIGCTRL);
	cfg &= ~EXYNOS_CIGCTRL_CSC_MASK;

	if (sz->hsize >= FIMC_WIDTH_ITU_709)
		cfg |= EXYNOS_CIGCTRL_CSC_ITU709;
	else
		cfg |= EXYNOS_CIGCTRL_CSC_ITU601;

	fimc_write(cfg, EXYNOS_CIGCTRL);

	if (swap) {
		img_pos.w = pos->h;
		img_pos.h = pos->w;
		img_sz.hsize = sz->vsize;
		img_sz.vsize = sz->hsize;
	}

	/* target image size */
	cfg = fimc_read(EXYNOS_CITRGFMT);
	cfg &= ~(EXYNOS_CITRGFMT_TARGETH_MASK |
		EXYNOS_CITRGFMT_TARGETV_MASK);
	cfg |= (EXYNOS_CITRGFMT_TARGETHSIZE(img_pos.w) |
		EXYNOS_CITRGFMT_TARGETVSIZE(img_pos.h));
	fimc_write(cfg, EXYNOS_CITRGFMT);

	/* target area */
	cfg = EXYNOS_CITAREA_TARGET_AREA(img_pos.w * img_pos.h);
	fimc_write(cfg, EXYNOS_CITAREA);

	/* offset Y(RGB), Cb, Cr */
	cfg = (EXYNOS_CIOYOFF_HORIZONTAL(img_pos.x) |
		EXYNOS_CIOYOFF_VERTICAL(img_pos.y));
	fimc_write(cfg, EXYNOS_CIOYOFF);
	cfg = (EXYNOS_CIOCBOFF_HORIZONTAL(img_pos.x) |
		EXYNOS_CIOCBOFF_VERTICAL(img_pos.y));
	fimc_write(cfg, EXYNOS_CIOCBOFF);
	cfg = (EXYNOS_CIOCROFF_HORIZONTAL(img_pos.x) |
		EXYNOS_CIOCROFF_VERTICAL(img_pos.y));
	fimc_write(cfg, EXYNOS_CIOCROFF);

	return 0;
}

static int fimc_dst_get_buf_seq(struct fimc_context *ctx)
{
	u32 cfg, i, buf_num = 0;
	u32 mask = 0x00000001;

	cfg = fimc_read(EXYNOS_CIFCNTSEQ);

	for (i = 0; i < FIMC_REG_SZ; i++)
		if (cfg & (mask << i))
			buf_num++;

	DRM_DEBUG_KMS("buf_num[%d]\n", buf_num);

	return buf_num;
}

static int fimc_dst_set_buf_seq(struct fimc_context *ctx, u32 buf_id,
		enum drm_exynos_ipp_buf_type buf_type)
{
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	bool enable;
	u32 cfg;
	u32 mask = 0x00000001 << buf_id;
	int ret = 0;

	DRM_DEBUG_KMS("buf_id[%d]buf_type[%d]\n", buf_id, buf_type);

	mutex_lock(&ctx->lock);

	/* mask register set */
	cfg = fimc_read(EXYNOS_CIFCNTSEQ);

	switch (buf_type) {
	case IPP_BUF_ENQUEUE:
		enable = true;
		break;
	case IPP_BUF_DEQUEUE:
		enable = false;
		break;
	default:
		dev_err(ippdrv->dev, "invalid buf ctrl parameter.\n");
		ret =  -EINVAL;
		goto err_unlock;
	}

	/* sequence id */
	cfg &= ~mask;
	cfg |= (enable << buf_id);
	fimc_write(cfg, EXYNOS_CIFCNTSEQ);

	/* interrupt enable */
	if (buf_type == IPP_BUF_ENQUEUE &&
	    fimc_dst_get_buf_seq(ctx) >= FIMC_BUF_START)
		fimc_handle_irq(ctx, true, false, true);

	/* interrupt disable */
	if (buf_type == IPP_BUF_DEQUEUE &&
	    fimc_dst_get_buf_seq(ctx) <= FIMC_BUF_STOP)
		fimc_handle_irq(ctx, false, false, true);

err_unlock:
	mutex_unlock(&ctx->lock);
	return ret;
}

static int fimc_dst_set_addr(struct device *dev,
		struct drm_exynos_ipp_buf_info *buf_info, u32 buf_id,
		enum drm_exynos_ipp_buf_type buf_type)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	struct drm_exynos_ipp_cmd_node *c_node = ippdrv->c_node;
	struct drm_exynos_ipp_property *property;
	struct drm_exynos_ipp_config *config;

	if (!c_node) {
		DRM_ERROR("failed to get c_node.\n");
		return -EINVAL;
	}

	property = &c_node->property;

	DRM_DEBUG_KMS("prop_id[%d]buf_id[%d]buf_type[%d]\n",
		property->prop_id, buf_id, buf_type);

	if (buf_id > FIMC_MAX_DST) {
		dev_info(ippdrv->dev, "inavlid buf_id %d.\n", buf_id);
		return -ENOMEM;
	}

	/* address register set */
	switch (buf_type) {
	case IPP_BUF_ENQUEUE:
		config = &property->config[EXYNOS_DRM_OPS_DST];

		fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_Y],
			EXYNOS_CIOYSA(buf_id));

		if (config->fmt == DRM_FORMAT_YVU420) {
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CR],
				EXYNOS_CIOCBSA(buf_id));
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CB],
				EXYNOS_CIOCRSA(buf_id));
		} else {
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CB],
				EXYNOS_CIOCBSA(buf_id));
			fimc_write(buf_info->base[EXYNOS_DRM_PLANAR_CR],
				EXYNOS_CIOCRSA(buf_id));
		}
		break;
	case IPP_BUF_DEQUEUE:
		fimc_write(0x0, EXYNOS_CIOYSA(buf_id));
		fimc_write(0x0, EXYNOS_CIOCBSA(buf_id));
		fimc_write(0x0, EXYNOS_CIOCRSA(buf_id));
		break;
	default:
		/* bypass */
		break;
	}

	return fimc_dst_set_buf_seq(ctx, buf_id, buf_type);
}

static struct exynos_drm_ipp_ops fimc_dst_ops = {
	.set_fmt = fimc_dst_set_fmt,
	.set_transf = fimc_dst_set_transf,
	.set_size = fimc_dst_set_size,
	.set_addr = fimc_dst_set_addr,
};

static int fimc_clk_ctrl(struct fimc_context *ctx, bool enable)
{
	DRM_DEBUG_KMS("enable[%d]\n", enable);

	if (enable) {
		clk_prepare_enable(ctx->clocks[FIMC_CLK_GATE]);
		clk_prepare_enable(ctx->clocks[FIMC_CLK_WB_A]);
		ctx->suspended = false;
	} else {
		clk_disable_unprepare(ctx->clocks[FIMC_CLK_GATE]);
		clk_disable_unprepare(ctx->clocks[FIMC_CLK_WB_A]);
		ctx->suspended = true;
	}

	return 0;
}

static irqreturn_t fimc_irq_handler(int irq, void *dev_id)
{
	struct fimc_context *ctx = dev_id;
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	struct drm_exynos_ipp_cmd_node *c_node = ippdrv->c_node;
	struct drm_exynos_ipp_event_work *event_work =
		c_node->event_work;
	int buf_id;

	DRM_DEBUG_KMS("fimc id[%d]\n", ctx->id);

	fimc_clear_irq(ctx);
	if (fimc_check_ovf(ctx))
		return IRQ_NONE;

	if (!fimc_check_frame_end(ctx))
		return IRQ_NONE;

	buf_id = fimc_get_buf_id(ctx);
	if (buf_id < 0)
		return IRQ_HANDLED;

	DRM_DEBUG_KMS("buf_id[%d]\n", buf_id);

	if (fimc_dst_set_buf_seq(ctx, buf_id, IPP_BUF_DEQUEUE) < 0) {
		DRM_ERROR("failed to dequeue.\n");
		return IRQ_HANDLED;
	}

	event_work->ippdrv = ippdrv;
	event_work->buf_id[EXYNOS_DRM_OPS_DST] = buf_id;
	queue_work(ippdrv->event_workq, (struct work_struct *)event_work);

	return IRQ_HANDLED;
}

static int fimc_init_prop_list(struct exynos_drm_ippdrv *ippdrv)
{
	struct drm_exynos_ipp_prop_list *prop_list;

	prop_list = devm_kzalloc(ippdrv->dev, sizeof(*prop_list), GFP_KERNEL);
	if (!prop_list) {
		DRM_ERROR("failed to alloc property list.\n");
		return -ENOMEM;
	}

	prop_list->version = 1;
	prop_list->writeback = 1;
	prop_list->refresh_min = FIMC_REFRESH_MIN;
	prop_list->refresh_max = FIMC_REFRESH_MAX;
	prop_list->flip = (1 << EXYNOS_DRM_FLIP_NONE) |
				(1 << EXYNOS_DRM_FLIP_VERTICAL) |
				(1 << EXYNOS_DRM_FLIP_HORIZONTAL);
	prop_list->degree = (1 << EXYNOS_DRM_DEGREE_0) |
				(1 << EXYNOS_DRM_DEGREE_90) |
				(1 << EXYNOS_DRM_DEGREE_180) |
				(1 << EXYNOS_DRM_DEGREE_270);
	prop_list->csc = 1;
	prop_list->crop = 1;
	prop_list->crop_max.hsize = FIMC_CROP_MAX;
	prop_list->crop_max.vsize = FIMC_CROP_MAX;
	prop_list->crop_min.hsize = FIMC_CROP_MIN;
	prop_list->crop_min.vsize = FIMC_CROP_MIN;
	prop_list->scale = 1;
	prop_list->scale_max.hsize = FIMC_SCALE_MAX;
	prop_list->scale_max.vsize = FIMC_SCALE_MAX;
	prop_list->scale_min.hsize = FIMC_SCALE_MIN;
	prop_list->scale_min.vsize = FIMC_SCALE_MIN;

	ippdrv->prop_list = prop_list;

	return 0;
}

static inline bool fimc_check_drm_flip(enum drm_exynos_flip flip)
{
	switch (flip) {
	case EXYNOS_DRM_FLIP_NONE:
	case EXYNOS_DRM_FLIP_VERTICAL:
	case EXYNOS_DRM_FLIP_HORIZONTAL:
	case EXYNOS_DRM_FLIP_BOTH:
		return true;
	default:
		DRM_DEBUG_KMS("invalid flip\n");
		return false;
	}
}

static int fimc_ippdrv_check_property(struct device *dev,
		struct drm_exynos_ipp_property *property)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	struct drm_exynos_ipp_prop_list *pp = ippdrv->prop_list;
	struct drm_exynos_ipp_config *config;
	struct drm_exynos_pos *pos;
	struct drm_exynos_sz *sz;
	bool swap;
	int i;

	for_each_ipp_ops(i) {
		if ((i == EXYNOS_DRM_OPS_SRC) &&
			(property->cmd == IPP_CMD_WB))
			continue;

		config = &property->config[i];
		pos = &config->pos;
		sz = &config->sz;

		/* check for flip */
		if (!fimc_check_drm_flip(config->flip)) {
			DRM_ERROR("invalid flip.\n");
			goto err_property;
		}

		/* check for degree */
		switch (config->degree) {
		case EXYNOS_DRM_DEGREE_90:
		case EXYNOS_DRM_DEGREE_270:
			swap = true;
			break;
		case EXYNOS_DRM_DEGREE_0:
		case EXYNOS_DRM_DEGREE_180:
			swap = false;
			break;
		default:
			DRM_ERROR("invalid degree.\n");
			goto err_property;
		}

		/* check for buffer bound */
		if ((pos->x + pos->w > sz->hsize) ||
			(pos->y + pos->h > sz->vsize)) {
			DRM_ERROR("out of buf bound.\n");
			goto err_property;
		}

		/* check for crop */
		if ((i == EXYNOS_DRM_OPS_SRC) && (pp->crop)) {
			if (swap) {
				if ((pos->h < pp->crop_min.hsize) ||
					(sz->vsize > pp->crop_max.hsize) ||
					(pos->w < pp->crop_min.vsize) ||
					(sz->hsize > pp->crop_max.vsize)) {
					DRM_ERROR("out of crop size.\n");
					goto err_property;
				}
			} else {
				if ((pos->w < pp->crop_min.hsize) ||
					(sz->hsize > pp->crop_max.hsize) ||
					(pos->h < pp->crop_min.vsize) ||
					(sz->vsize > pp->crop_max.vsize)) {
					DRM_ERROR("out of crop size.\n");
					goto err_property;
				}
			}
		}

		/* check for scale */
		if ((i == EXYNOS_DRM_OPS_DST) && (pp->scale)) {
			if (swap) {
				if ((pos->h < pp->scale_min.hsize) ||
					(sz->vsize > pp->scale_max.hsize) ||
					(pos->w < pp->scale_min.vsize) ||
					(sz->hsize > pp->scale_max.vsize)) {
					DRM_ERROR("out of scale size.\n");
					goto err_property;
				}
			} else {
				if ((pos->w < pp->scale_min.hsize) ||
					(sz->hsize > pp->scale_max.hsize) ||
					(pos->h < pp->scale_min.vsize) ||
					(sz->vsize > pp->scale_max.vsize)) {
					DRM_ERROR("out of scale size.\n");
					goto err_property;
				}
			}
		}
	}

	return 0;

err_property:
	for_each_ipp_ops(i) {
		if ((i == EXYNOS_DRM_OPS_SRC) &&
			(property->cmd == IPP_CMD_WB))
			continue;

		config = &property->config[i];
		pos = &config->pos;
		sz = &config->sz;

		DRM_ERROR("[%s]f[%d]r[%d]pos[%d %d %d %d]sz[%d %d]\n",
			i ? "dst" : "src", config->flip, config->degree,
			pos->x, pos->y, pos->w, pos->h,
			sz->hsize, sz->vsize);
	}

	return -EINVAL;
}

static void fimc_clear_addr(struct fimc_context *ctx)
{
	int i;

	for (i = 0; i < FIMC_MAX_SRC; i++) {
		fimc_write(0, EXYNOS_CIIYSA(i));
		fimc_write(0, EXYNOS_CIICBSA(i));
		fimc_write(0, EXYNOS_CIICRSA(i));
	}

	for (i = 0; i < FIMC_MAX_DST; i++) {
		fimc_write(0, EXYNOS_CIOYSA(i));
		fimc_write(0, EXYNOS_CIOCBSA(i));
		fimc_write(0, EXYNOS_CIOCRSA(i));
	}
}

static int fimc_ippdrv_reset(struct device *dev)
{
	struct fimc_context *ctx = get_fimc_context(dev);

	/* reset h/w block */
	fimc_sw_reset(ctx);

	/* reset scaler capability */
	memset(&ctx->sc, 0x0, sizeof(ctx->sc));

	fimc_clear_addr(ctx);

	return 0;
}

static int fimc_ippdrv_start(struct device *dev, enum drm_exynos_ipp_cmd cmd)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;
	struct drm_exynos_ipp_cmd_node *c_node = ippdrv->c_node;
	struct drm_exynos_ipp_property *property;
	struct drm_exynos_ipp_config *config;
	struct drm_exynos_pos	img_pos[EXYNOS_DRM_OPS_MAX];
	struct drm_exynos_ipp_set_wb set_wb;
	int ret, i;
	u32 cfg0, cfg1;

	DRM_DEBUG_KMS("cmd[%d]\n", cmd);

	if (!c_node) {
		DRM_ERROR("failed to get c_node.\n");
		return -EINVAL;
	}

	property = &c_node->property;

	fimc_handle_irq(ctx, true, false, true);

	for_each_ipp_ops(i) {
		config = &property->config[i];
		img_pos[i] = config->pos;
	}

	ret = fimc_set_prescaler(ctx, &ctx->sc,
		&img_pos[EXYNOS_DRM_OPS_SRC],
		&img_pos[EXYNOS_DRM_OPS_DST]);
	if (ret) {
		dev_err(dev, "failed to set precalser.\n");
		return ret;
	}

	/* If set ture, we can save jpeg about screen */
	fimc_handle_jpeg(ctx, false);
	fimc_set_scaler(ctx, &ctx->sc);
	fimc_set_polarity(ctx, &ctx->pol);

	switch (cmd) {
	case IPP_CMD_M2M:
		fimc_set_type_ctrl(ctx, FIMC_WB_NONE);
		fimc_handle_lastend(ctx, false);

		/* setup dma */
		cfg0 = fimc_read(EXYNOS_MSCTRL);
		cfg0 &= ~EXYNOS_MSCTRL_INPUT_MASK;
		cfg0 |= EXYNOS_MSCTRL_INPUT_MEMORY;
		fimc_write(cfg0, EXYNOS_MSCTRL);
		break;
	case IPP_CMD_WB:
		fimc_set_type_ctrl(ctx, FIMC_WB_A);
		fimc_handle_lastend(ctx, true);

		/* setup FIMD */
		ret = fimc_set_camblk_fimd0_wb(ctx);
		if (ret < 0) {
			dev_err(dev, "camblk setup failed.\n");
			return ret;
		}

		set_wb.enable = 1;
		set_wb.refresh = property->refresh_rate;
		exynos_drm_ippnb_send_event(IPP_SET_WRITEBACK, (void *)&set_wb);
		break;
	case IPP_CMD_OUTPUT:
	default:
		ret = -EINVAL;
		dev_err(dev, "invalid operations.\n");
		return ret;
	}

	/* Reset status */
	fimc_write(0x0, EXYNOS_CISTATUS);

	cfg0 = fimc_read(EXYNOS_CIIMGCPT);
	cfg0 &= ~EXYNOS_CIIMGCPT_IMGCPTEN_SC;
	cfg0 |= EXYNOS_CIIMGCPT_IMGCPTEN_SC;

	/* Scaler */
	cfg1 = fimc_read(EXYNOS_CISCCTRL);
	cfg1 &= ~EXYNOS_CISCCTRL_SCAN_MASK;
	cfg1 |= (EXYNOS_CISCCTRL_PROGRESSIVE |
		EXYNOS_CISCCTRL_SCALERSTART);

	fimc_write(cfg1, EXYNOS_CISCCTRL);

	/* Enable image capture*/
	cfg0 |= EXYNOS_CIIMGCPT_IMGCPTEN;
	fimc_write(cfg0, EXYNOS_CIIMGCPT);

	/* Disable frame end irq */
	cfg0 = fimc_read(EXYNOS_CIGCTRL);
	cfg0 &= ~EXYNOS_CIGCTRL_IRQ_END_DISABLE;
	fimc_write(cfg0, EXYNOS_CIGCTRL);

	cfg0 = fimc_read(EXYNOS_CIOCTRL);
	cfg0 &= ~EXYNOS_CIOCTRL_WEAVE_MASK;
	fimc_write(cfg0, EXYNOS_CIOCTRL);

	if (cmd == IPP_CMD_M2M) {
		cfg0 = fimc_read(EXYNOS_MSCTRL);
		cfg0 |= EXYNOS_MSCTRL_ENVID;
		fimc_write(cfg0, EXYNOS_MSCTRL);

		cfg0 = fimc_read(EXYNOS_MSCTRL);
		cfg0 |= EXYNOS_MSCTRL_ENVID;
		fimc_write(cfg0, EXYNOS_MSCTRL);
	}

	return 0;
}

static void fimc_ippdrv_stop(struct device *dev, enum drm_exynos_ipp_cmd cmd)
{
	struct fimc_context *ctx = get_fimc_context(dev);
	struct drm_exynos_ipp_set_wb set_wb = {0, 0};
	u32 cfg;

	DRM_DEBUG_KMS("cmd[%d]\n", cmd);

	switch (cmd) {
	case IPP_CMD_M2M:
		/* Source clear */
		cfg = fimc_read(EXYNOS_MSCTRL);
		cfg &= ~EXYNOS_MSCTRL_INPUT_MASK;
		cfg &= ~EXYNOS_MSCTRL_ENVID;
		fimc_write(cfg, EXYNOS_MSCTRL);
		break;
	case IPP_CMD_WB:
		exynos_drm_ippnb_send_event(IPP_SET_WRITEBACK, (void *)&set_wb);
		break;
	case IPP_CMD_OUTPUT:
	default:
		dev_err(dev, "invalid operations.\n");
		break;
	}

	fimc_handle_irq(ctx, false, false, true);

	/* reset sequence */
	fimc_write(0x0, EXYNOS_CIFCNTSEQ);

	/* Scaler disable */
	cfg = fimc_read(EXYNOS_CISCCTRL);
	cfg &= ~EXYNOS_CISCCTRL_SCALERSTART;
	fimc_write(cfg, EXYNOS_CISCCTRL);

	/* Disable image capture */
	cfg = fimc_read(EXYNOS_CIIMGCPT);
	cfg &= ~(EXYNOS_CIIMGCPT_IMGCPTEN_SC | EXYNOS_CIIMGCPT_IMGCPTEN);
	fimc_write(cfg, EXYNOS_CIIMGCPT);

	/* Enable frame end irq */
	cfg = fimc_read(EXYNOS_CIGCTRL);
	cfg |= EXYNOS_CIGCTRL_IRQ_END_DISABLE;
	fimc_write(cfg, EXYNOS_CIGCTRL);
}

static void fimc_put_clocks(struct fimc_context *ctx)
{
	int i;

	for (i = 0; i < FIMC_CLKS_MAX; i++) {
		if (IS_ERR(ctx->clocks[i]))
			continue;
		clk_put(ctx->clocks[i]);
		ctx->clocks[i] = ERR_PTR(-EINVAL);
	}
}

static int fimc_setup_clocks(struct fimc_context *ctx)
{
	struct device *fimc_dev = ctx->ippdrv.dev;
	struct device *dev;
	int ret, i;

	for (i = 0; i < FIMC_CLKS_MAX; i++)
		ctx->clocks[i] = ERR_PTR(-EINVAL);

	for (i = 0; i < FIMC_CLKS_MAX; i++) {
		if (i == FIMC_CLK_WB_A || i == FIMC_CLK_WB_B)
			dev = fimc_dev->parent;
		else
			dev = fimc_dev;

		ctx->clocks[i] = clk_get(dev, fimc_clock_names[i]);
		if (IS_ERR(ctx->clocks[i])) {
			if (i >= FIMC_CLK_MUX)
				break;
			ret = PTR_ERR(ctx->clocks[i]);
			dev_err(fimc_dev, "failed to get clock: %s\n",
						fimc_clock_names[i]);
			goto e_clk_free;
		}
	}

	/* Optional FIMC LCLK parent clock setting */
	if (!IS_ERR(ctx->clocks[FIMC_CLK_PARENT])) {
		ret = clk_set_parent(ctx->clocks[FIMC_CLK_MUX],
				     ctx->clocks[FIMC_CLK_PARENT]);
		if (ret < 0) {
			dev_err(fimc_dev, "failed to set parent.\n");
			goto e_clk_free;
		}
	}

	ret = clk_set_rate(ctx->clocks[FIMC_CLK_LCLK], ctx->clk_frequency);
	if (ret < 0)
		goto e_clk_free;

	ret = clk_prepare_enable(ctx->clocks[FIMC_CLK_LCLK]);
	if (!ret)
		return ret;
e_clk_free:
	fimc_put_clocks(ctx);
	return ret;
}

static int fimc_parse_dt(struct fimc_context *ctx)
{
	struct device_node *node = ctx->ippdrv.dev->of_node;

	/* Handle only devices that support the LCD Writeback data path */
	if (!of_property_read_bool(node, "samsung,lcd-wb"))
		return -ENODEV;

	if (of_property_read_u32(node, "clock-frequency",
					&ctx->clk_frequency))
		ctx->clk_frequency = FIMC_DEFAULT_LCLK_FREQUENCY;

	ctx->id = of_alias_get_id(node, "fimc");

	if (ctx->id < 0) {
		dev_err(ctx->ippdrv.dev, "failed to get node alias id.\n");
		return -EINVAL;
	}

	return 0;
}

static int fimc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimc_context *ctx;
	struct resource *res;
	struct exynos_drm_ippdrv *ippdrv;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found.\n");
		return -ENODEV;
	}

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->ippdrv.dev = dev;

	ret = fimc_parse_dt(ctx);
	if (ret < 0)
		return ret;

	ctx->sysreg = syscon_regmap_lookup_by_phandle(dev->of_node,
						"samsung,sysreg");
	if (IS_ERR(ctx->sysreg)) {
		dev_err(dev, "syscon regmap lookup failed.\n");
		return PTR_ERR(ctx->sysreg);
	}

	/* resource memory */
	ctx->regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->regs = devm_ioremap_resource(dev, ctx->regs_res);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);

	/* resource irq */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "failed to request irq resource.\n");
		return -ENOENT;
	}

	ctx->irq = res->start;
	ret = devm_request_threaded_irq(dev, ctx->irq, NULL, fimc_irq_handler,
		IRQF_ONESHOT, "drm_fimc", ctx);
	if (ret < 0) {
		dev_err(dev, "failed to request irq.\n");
		return ret;
	}

	ret = fimc_setup_clocks(ctx);
	if (ret < 0)
		return ret;

	ippdrv = &ctx->ippdrv;
	ippdrv->ops[EXYNOS_DRM_OPS_SRC] = &fimc_src_ops;
	ippdrv->ops[EXYNOS_DRM_OPS_DST] = &fimc_dst_ops;
	ippdrv->check_property = fimc_ippdrv_check_property;
	ippdrv->reset = fimc_ippdrv_reset;
	ippdrv->start = fimc_ippdrv_start;
	ippdrv->stop = fimc_ippdrv_stop;
	ret = fimc_init_prop_list(ippdrv);
	if (ret < 0) {
		dev_err(dev, "failed to init property list.\n");
		goto err_put_clk;
	}

	DRM_DEBUG_KMS("id[%d]ippdrv[0x%x]\n", ctx->id, (int)ippdrv);

	mutex_init(&ctx->lock);
	platform_set_drvdata(pdev, ctx);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = exynos_drm_ippdrv_register(ippdrv);
	if (ret < 0) {
		dev_err(dev, "failed to register drm fimc device.\n");
		goto err_pm_dis;
	}

	dev_info(dev, "drm fimc registered successfully.\n");

	return 0;

err_pm_dis:
	pm_runtime_disable(dev);
err_put_clk:
	fimc_put_clocks(ctx);

	return ret;
}

static int fimc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimc_context *ctx = get_fimc_context(dev);
	struct exynos_drm_ippdrv *ippdrv = &ctx->ippdrv;

	exynos_drm_ippdrv_unregister(ippdrv);
	mutex_destroy(&ctx->lock);

	fimc_put_clocks(ctx);
	pm_runtime_set_suspended(dev);
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int fimc_suspend(struct device *dev)
{
	struct fimc_context *ctx = get_fimc_context(dev);

	DRM_DEBUG_KMS("id[%d]\n", ctx->id);

	if (pm_runtime_suspended(dev))
		return 0;

	return fimc_clk_ctrl(ctx, false);
}

static int fimc_resume(struct device *dev)
{
	struct fimc_context *ctx = get_fimc_context(dev);

	DRM_DEBUG_KMS("id[%d]\n", ctx->id);

	if (!pm_runtime_suspended(dev))
		return fimc_clk_ctrl(ctx, true);

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int fimc_runtime_suspend(struct device *dev)
{
	struct fimc_context *ctx = get_fimc_context(dev);

	DRM_DEBUG_KMS("id[%d]\n", ctx->id);

	return  fimc_clk_ctrl(ctx, false);
}

static int fimc_runtime_resume(struct device *dev)
{
	struct fimc_context *ctx = get_fimc_context(dev);

	DRM_DEBUG_KMS("id[%d]\n", ctx->id);

	return  fimc_clk_ctrl(ctx, true);
}
#endif

static const struct dev_pm_ops fimc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimc_suspend, fimc_resume)
	SET_RUNTIME_PM_OPS(fimc_runtime_suspend, fimc_runtime_resume, NULL)
};

static const struct of_device_id fimc_of_match[] = {
	{ .compatible = "samsung,exynos4210-fimc" },
	{ .compatible = "samsung,exynos4212-fimc" },
	{ },
};

struct platform_driver fimc_driver = {
	.probe		= fimc_probe,
	.remove		= fimc_remove,
	.driver		= {
		.of_match_table = fimc_of_match,
		.name	= "exynos-drm-fimc",
		.owner	= THIS_MODULE,
		.pm	= &fimc_pm_ops,
	},
};

