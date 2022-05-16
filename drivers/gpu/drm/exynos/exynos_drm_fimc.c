// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Authors:
 *	Eunchul Kim <chulspro.kim@samsung.com>
 *	Jinyoung Jeon <jy0.jeon@samsung.com>
 *	Sangmin Lee <lsmin.lee@samsung.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_ipp.h"
#include "regs-fimc.h"

/*
 * FIMC stands for Fully Interactive Mobile Camera and
 * supports image scaler/rotator and input/output DMA operations.
 * input DMA reads image data from the memory.
 * output DMA writes image data to memory.
 * FIMC supports image rotation and image effect functions.
 */

#define FIMC_MAX_DEVS	4
#define FIMC_MAX_SRC	2
#define FIMC_MAX_DST	32
#define FIMC_SHFACTOR	10
#define FIMC_BUF_STOP	1
#define FIMC_BUF_START	2
#define FIMC_WIDTH_ITU_709	1280
#define FIMC_AUTOSUSPEND_DELAY	2000

static unsigned int fimc_mask = 0xc;
module_param_named(fimc_devs, fimc_mask, uint, 0644);
MODULE_PARM_DESC(fimc_devs, "Alias mask for assigning FIMC devices to Exynos DRM");

#define get_fimc_context(dev)	dev_get_drvdata(dev)

enum {
	FIMC_CLK_LCLK,
	FIMC_CLK_GATE,
	FIMC_CLK_WB_A,
	FIMC_CLK_WB_B,
	FIMC_CLKS_MAX
};

static const char * const fimc_clock_names[] = {
	[FIMC_CLK_LCLK]   = "sclk_fimc",
	[FIMC_CLK_GATE]   = "fimc",
	[FIMC_CLK_WB_A]   = "pxl_async0",
	[FIMC_CLK_WB_B]   = "pxl_async1",
};

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
	bool range;
	bool bypass;
	bool up_h;
	bool up_v;
	u32 hratio;
	u32 vratio;
};

/*
 * A structure of fimc context.
 *
 * @regs_res: register resources.
 * @regs: memory mapped io registers.
 * @lock: locking of operations.
 * @clocks: fimc clocks.
 * @sc: scaler infomations.
 * @pol: porarity of writeback.
 * @id: fimc id.
 * @irq: irq number.
 */
struct fimc_context {
	struct exynos_drm_ipp ipp;
	struct drm_device *drm_dev;
	void		*dma_priv;
	struct device	*dev;
	struct exynos_drm_ipp_task	*task;
	struct exynos_drm_ipp_formats	*formats;
	unsigned int			num_formats;

	struct resource	*regs_res;
	void __iomem	*regs;
	spinlock_t	lock;
	struct clk	*clocks[FIMC_CLKS_MAX];
	struct fimc_scaler	sc;
	int	id;
	int	irq;
};

static u32 fimc_read(struct fimc_context *ctx, u32 reg)
{
	return readl(ctx->regs + reg);
}

static void fimc_write(struct fimc_context *ctx, u32 val, u32 reg)
{
	writel(val, ctx->regs + reg);
}

static void fimc_set_bits(struct fimc_context *ctx, u32 reg, u32 bits)
{
	void __iomem *r = ctx->regs + reg;

	writel(readl(r) | bits, r);
}

static void fimc_clear_bits(struct fimc_context *ctx, u32 reg, u32 bits)
{
	void __iomem *r = ctx->regs + reg;

	writel(readl(r) & ~bits, r);
}

static void fimc_sw_reset(struct fimc_context *ctx)
{
	u32 cfg;

	/* stop dma operation */
	cfg = fimc_read(ctx, EXYNOS_CISTATUS);
	if (EXYNOS_CISTATUS_GET_ENVID_STATUS(cfg))
		fimc_clear_bits(ctx, EXYNOS_MSCTRL, EXYNOS_MSCTRL_ENVID);

	fimc_set_bits(ctx, EXYNOS_CISRCFMT, EXYNOS_CISRCFMT_ITU601_8BIT);

	/* disable image capture */
	fimc_clear_bits(ctx, EXYNOS_CIIMGCPT,
		EXYNOS_CIIMGCPT_IMGCPTEN_SC | EXYNOS_CIIMGCPT_IMGCPTEN);

	/* s/w reset */
	fimc_set_bits(ctx, EXYNOS_CIGCTRL, EXYNOS_CIGCTRL_SWRST);

	/* s/w reset complete */
	fimc_clear_bits(ctx, EXYNOS_CIGCTRL, EXYNOS_CIGCTRL_SWRST);

	/* reset sequence */
	fimc_write(ctx, 0x0, EXYNOS_CIFCNTSEQ);
}

static void fimc_set_type_ctrl(struct fimc_context *ctx)
{
	u32 cfg;

	cfg = fimc_read(ctx, EXYNOS_CIGCTRL);
	cfg &= ~(EXYNOS_CIGCTRL_TESTPATTERN_MASK |
		EXYNOS_CIGCTRL_SELCAM_ITU_MASK |
		EXYNOS_CIGCTRL_SELCAM_MIPI_MASK |
		EXYNOS_CIGCTRL_SELCAM_FIMC_MASK |
		EXYNOS_CIGCTRL_SELWB_CAMIF_MASK |
		EXYNOS_CIGCTRL_SELWRITEBACK_MASK);

	cfg |= (EXYNOS_CIGCTRL_SELCAM_ITU_A |
		EXYNOS_CIGCTRL_SELWRITEBACK_A |
		EXYNOS_CIGCTRL_SELCAM_MIPI_A |
		EXYNOS_CIGCTRL_SELCAM_FIMC_ITU);

	fimc_write(ctx, cfg, EXYNOS_CIGCTRL);
}

static void fimc_handle_jpeg(struct fimc_context *ctx, bool enable)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "enable[%d]\n", enable);

	cfg = fimc_read(ctx, EXYNOS_CIGCTRL);
	if (enable)
		cfg |= EXYNOS_CIGCTRL_CAM_JPEG;
	else
		cfg &= ~EXYNOS_CIGCTRL_CAM_JPEG;

	fimc_write(ctx, cfg, EXYNOS_CIGCTRL);
}

static void fimc_mask_irq(struct fimc_context *ctx, bool enable)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "enable[%d]\n", enable);

	cfg = fimc_read(ctx, EXYNOS_CIGCTRL);
	if (enable) {
		cfg &= ~EXYNOS_CIGCTRL_IRQ_OVFEN;
		cfg |= EXYNOS_CIGCTRL_IRQ_ENABLE | EXYNOS_CIGCTRL_IRQ_LEVEL;
	} else
		cfg &= ~EXYNOS_CIGCTRL_IRQ_ENABLE;
	fimc_write(ctx, cfg, EXYNOS_CIGCTRL);
}

static void fimc_clear_irq(struct fimc_context *ctx)
{
	fimc_set_bits(ctx, EXYNOS_CIGCTRL, EXYNOS_CIGCTRL_IRQ_CLR);
}

static bool fimc_check_ovf(struct fimc_context *ctx)
{
	u32 status, flag;

	status = fimc_read(ctx, EXYNOS_CISTATUS);
	flag = EXYNOS_CISTATUS_OVFIY | EXYNOS_CISTATUS_OVFICB |
		EXYNOS_CISTATUS_OVFICR;

	DRM_DEV_DEBUG_KMS(ctx->dev, "flag[0x%x]\n", flag);

	if (status & flag) {
		fimc_set_bits(ctx, EXYNOS_CIWDOFST,
			EXYNOS_CIWDOFST_CLROVFIY | EXYNOS_CIWDOFST_CLROVFICB |
			EXYNOS_CIWDOFST_CLROVFICR);

		DRM_DEV_ERROR(ctx->dev,
			      "occurred overflow at %d, status 0x%x.\n",
			      ctx->id, status);
		return true;
	}

	return false;
}

static bool fimc_check_frame_end(struct fimc_context *ctx)
{
	u32 cfg;

	cfg = fimc_read(ctx, EXYNOS_CISTATUS);

	DRM_DEV_DEBUG_KMS(ctx->dev, "cfg[0x%x]\n", cfg);

	if (!(cfg & EXYNOS_CISTATUS_FRAMEEND))
		return false;

	cfg &= ~(EXYNOS_CISTATUS_FRAMEEND);
	fimc_write(ctx, cfg, EXYNOS_CISTATUS);

	return true;
}

static int fimc_get_buf_id(struct fimc_context *ctx)
{
	u32 cfg;
	int frame_cnt, buf_id;

	cfg = fimc_read(ctx, EXYNOS_CISTATUS2);
	frame_cnt = EXYNOS_CISTATUS2_GET_FRAMECOUNT_BEFORE(cfg);

	if (frame_cnt == 0)
		frame_cnt = EXYNOS_CISTATUS2_GET_FRAMECOUNT_PRESENT(cfg);

	DRM_DEV_DEBUG_KMS(ctx->dev, "present[%d]before[%d]\n",
			  EXYNOS_CISTATUS2_GET_FRAMECOUNT_PRESENT(cfg),
			  EXYNOS_CISTATUS2_GET_FRAMECOUNT_BEFORE(cfg));

	if (frame_cnt == 0) {
		DRM_DEV_ERROR(ctx->dev, "failed to get frame count.\n");
		return -EIO;
	}

	buf_id = frame_cnt - 1;
	DRM_DEV_DEBUG_KMS(ctx->dev, "buf_id[%d]\n", buf_id);

	return buf_id;
}

static void fimc_handle_lastend(struct fimc_context *ctx, bool enable)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "enable[%d]\n", enable);

	cfg = fimc_read(ctx, EXYNOS_CIOCTRL);
	if (enable)
		cfg |= EXYNOS_CIOCTRL_LASTENDEN;
	else
		cfg &= ~EXYNOS_CIOCTRL_LASTENDEN;

	fimc_write(ctx, cfg, EXYNOS_CIOCTRL);
}

static void fimc_src_set_fmt_order(struct fimc_context *ctx, u32 fmt)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	/* RGB */
	cfg = fimc_read(ctx, EXYNOS_CISCCTRL);
	cfg &= ~EXYNOS_CISCCTRL_INRGB_FMT_RGB_MASK;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= EXYNOS_CISCCTRL_INRGB_FMT_RGB565;
		fimc_write(ctx, cfg, EXYNOS_CISCCTRL);
		return;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		cfg |= EXYNOS_CISCCTRL_INRGB_FMT_RGB888;
		fimc_write(ctx, cfg, EXYNOS_CISCCTRL);
		return;
	default:
		/* bypass */
		break;
	}

	/* YUV */
	cfg = fimc_read(ctx, EXYNOS_MSCTRL);
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
	case DRM_FORMAT_NV16:
		cfg |= (EXYNOS_MSCTRL_ORDER2P_LSB_CBCR |
			EXYNOS_MSCTRL_C_INT_IN_2PLANE);
		break;
	}

	fimc_write(ctx, cfg, EXYNOS_MSCTRL);
}

static void fimc_src_set_fmt(struct fimc_context *ctx, u32 fmt, bool tiled)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	cfg = fimc_read(ctx, EXYNOS_MSCTRL);
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
		cfg |= EXYNOS_MSCTRL_INFORMAT_YCBCR420;
		break;
	}

	fimc_write(ctx, cfg, EXYNOS_MSCTRL);

	cfg = fimc_read(ctx, EXYNOS_CIDMAPARAM);
	cfg &= ~EXYNOS_CIDMAPARAM_R_MODE_MASK;

	if (tiled)
		cfg |= EXYNOS_CIDMAPARAM_R_MODE_64X32;
	else
		cfg |= EXYNOS_CIDMAPARAM_R_MODE_LINEAR;

	fimc_write(ctx, cfg, EXYNOS_CIDMAPARAM);

	fimc_src_set_fmt_order(ctx, fmt);
}

static void fimc_src_set_transf(struct fimc_context *ctx, unsigned int rotation)
{
	unsigned int degree = rotation & DRM_MODE_ROTATE_MASK;
	u32 cfg1, cfg2;

	DRM_DEV_DEBUG_KMS(ctx->dev, "rotation[%x]\n", rotation);

	cfg1 = fimc_read(ctx, EXYNOS_MSCTRL);
	cfg1 &= ~(EXYNOS_MSCTRL_FLIP_X_MIRROR |
		EXYNOS_MSCTRL_FLIP_Y_MIRROR);

	cfg2 = fimc_read(ctx, EXYNOS_CITRGFMT);
	cfg2 &= ~EXYNOS_CITRGFMT_INROT90_CLOCKWISE;

	switch (degree) {
	case DRM_MODE_ROTATE_0:
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 |= EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 |= EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_90:
		cfg2 |= EXYNOS_CITRGFMT_INROT90_CLOCKWISE;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 |= EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 |= EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_180:
		cfg1 |= (EXYNOS_MSCTRL_FLIP_X_MIRROR |
			EXYNOS_MSCTRL_FLIP_Y_MIRROR);
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_270:
		cfg1 |= (EXYNOS_MSCTRL_FLIP_X_MIRROR |
			EXYNOS_MSCTRL_FLIP_Y_MIRROR);
		cfg2 |= EXYNOS_CITRGFMT_INROT90_CLOCKWISE;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 &= ~EXYNOS_MSCTRL_FLIP_Y_MIRROR;
		break;
	}

	fimc_write(ctx, cfg1, EXYNOS_MSCTRL);
	fimc_write(ctx, cfg2, EXYNOS_CITRGFMT);
}

static void fimc_set_window(struct fimc_context *ctx,
			    struct exynos_drm_ipp_buffer *buf)
{
	unsigned int real_width = buf->buf.pitch[0] / buf->format->cpp[0];
	u32 cfg, h1, h2, v1, v2;

	/* cropped image */
	h1 = buf->rect.x;
	h2 = real_width - buf->rect.w - buf->rect.x;
	v1 = buf->rect.y;
	v2 = buf->buf.height - buf->rect.h - buf->rect.y;

	DRM_DEV_DEBUG_KMS(ctx->dev, "x[%d]y[%d]w[%d]h[%d]hsize[%d]vsize[%d]\n",
			  buf->rect.x, buf->rect.y, buf->rect.w, buf->rect.h,
			  real_width, buf->buf.height);
	DRM_DEV_DEBUG_KMS(ctx->dev, "h1[%d]h2[%d]v1[%d]v2[%d]\n", h1, h2, v1,
			  v2);

	/*
	 * set window offset 1, 2 size
	 * check figure 43-21 in user manual
	 */
	cfg = fimc_read(ctx, EXYNOS_CIWDOFST);
	cfg &= ~(EXYNOS_CIWDOFST_WINHOROFST_MASK |
		EXYNOS_CIWDOFST_WINVEROFST_MASK);
	cfg |= (EXYNOS_CIWDOFST_WINHOROFST(h1) |
		EXYNOS_CIWDOFST_WINVEROFST(v1));
	cfg |= EXYNOS_CIWDOFST_WINOFSEN;
	fimc_write(ctx, cfg, EXYNOS_CIWDOFST);

	cfg = (EXYNOS_CIWDOFST2_WINHOROFST2(h2) |
		EXYNOS_CIWDOFST2_WINVEROFST2(v2));
	fimc_write(ctx, cfg, EXYNOS_CIWDOFST2);
}

static void fimc_src_set_size(struct fimc_context *ctx,
			      struct exynos_drm_ipp_buffer *buf)
{
	unsigned int real_width = buf->buf.pitch[0] / buf->format->cpp[0];
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "hsize[%d]vsize[%d]\n", real_width,
			  buf->buf.height);

	/* original size */
	cfg = (EXYNOS_ORGISIZE_HORIZONTAL(real_width) |
		EXYNOS_ORGISIZE_VERTICAL(buf->buf.height));

	fimc_write(ctx, cfg, EXYNOS_ORGISIZE);

	DRM_DEV_DEBUG_KMS(ctx->dev, "x[%d]y[%d]w[%d]h[%d]\n", buf->rect.x,
			  buf->rect.y, buf->rect.w, buf->rect.h);

	/* set input DMA image size */
	cfg = fimc_read(ctx, EXYNOS_CIREAL_ISIZE);
	cfg &= ~(EXYNOS_CIREAL_ISIZE_HEIGHT_MASK |
		EXYNOS_CIREAL_ISIZE_WIDTH_MASK);
	cfg |= (EXYNOS_CIREAL_ISIZE_WIDTH(buf->rect.w) |
		EXYNOS_CIREAL_ISIZE_HEIGHT(buf->rect.h));
	fimc_write(ctx, cfg, EXYNOS_CIREAL_ISIZE);

	/*
	 * set input FIFO image size
	 * for now, we support only ITU601 8 bit mode
	 */
	cfg = (EXYNOS_CISRCFMT_ITU601_8BIT |
		EXYNOS_CISRCFMT_SOURCEHSIZE(real_width) |
		EXYNOS_CISRCFMT_SOURCEVSIZE(buf->buf.height));
	fimc_write(ctx, cfg, EXYNOS_CISRCFMT);

	/* offset Y(RGB), Cb, Cr */
	cfg = (EXYNOS_CIIYOFF_HORIZONTAL(buf->rect.x) |
		EXYNOS_CIIYOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYNOS_CIIYOFF);
	cfg = (EXYNOS_CIICBOFF_HORIZONTAL(buf->rect.x) |
		EXYNOS_CIICBOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYNOS_CIICBOFF);
	cfg = (EXYNOS_CIICROFF_HORIZONTAL(buf->rect.x) |
		EXYNOS_CIICROFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYNOS_CIICROFF);

	fimc_set_window(ctx, buf);
}

static void fimc_src_set_addr(struct fimc_context *ctx,
			      struct exynos_drm_ipp_buffer *buf)
{
	fimc_write(ctx, buf->dma_addr[0], EXYNOS_CIIYSA(0));
	fimc_write(ctx, buf->dma_addr[1], EXYNOS_CIICBSA(0));
	fimc_write(ctx, buf->dma_addr[2], EXYNOS_CIICRSA(0));
}

static void fimc_dst_set_fmt_order(struct fimc_context *ctx, u32 fmt)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	/* RGB */
	cfg = fimc_read(ctx, EXYNOS_CISCCTRL);
	cfg &= ~EXYNOS_CISCCTRL_OUTRGB_FMT_RGB_MASK;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= EXYNOS_CISCCTRL_OUTRGB_FMT_RGB565;
		fimc_write(ctx, cfg, EXYNOS_CISCCTRL);
		return;
	case DRM_FORMAT_RGB888:
		cfg |= EXYNOS_CISCCTRL_OUTRGB_FMT_RGB888;
		fimc_write(ctx, cfg, EXYNOS_CISCCTRL);
		return;
	case DRM_FORMAT_XRGB8888:
		cfg |= (EXYNOS_CISCCTRL_OUTRGB_FMT_RGB888 |
			EXYNOS_CISCCTRL_EXTRGB_EXTENSION);
		fimc_write(ctx, cfg, EXYNOS_CISCCTRL);
		break;
	default:
		/* bypass */
		break;
	}

	/* YUV */
	cfg = fimc_read(ctx, EXYNOS_CIOCTRL);
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
	case DRM_FORMAT_NV16:
		cfg |= EXYNOS_CIOCTRL_ORDER2P_LSB_CBCR;
		cfg |= EXYNOS_CIOCTRL_YCBCR_2PLANE;
		break;
	}

	fimc_write(ctx, cfg, EXYNOS_CIOCTRL);
}

static void fimc_dst_set_fmt(struct fimc_context *ctx, u32 fmt, bool tiled)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	cfg = fimc_read(ctx, EXYNOS_CIEXTEN);

	if (fmt == DRM_FORMAT_AYUV) {
		cfg |= EXYNOS_CIEXTEN_YUV444_OUT;
		fimc_write(ctx, cfg, EXYNOS_CIEXTEN);
	} else {
		cfg &= ~EXYNOS_CIEXTEN_YUV444_OUT;
		fimc_write(ctx, cfg, EXYNOS_CIEXTEN);

		cfg = fimc_read(ctx, EXYNOS_CITRGFMT);
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
		case DRM_FORMAT_NV21:
			cfg |= EXYNOS_CITRGFMT_OUTFORMAT_YCBCR420;
			break;
		}

		fimc_write(ctx, cfg, EXYNOS_CITRGFMT);
	}

	cfg = fimc_read(ctx, EXYNOS_CIDMAPARAM);
	cfg &= ~EXYNOS_CIDMAPARAM_W_MODE_MASK;

	if (tiled)
		cfg |= EXYNOS_CIDMAPARAM_W_MODE_64X32;
	else
		cfg |= EXYNOS_CIDMAPARAM_W_MODE_LINEAR;

	fimc_write(ctx, cfg, EXYNOS_CIDMAPARAM);

	fimc_dst_set_fmt_order(ctx, fmt);
}

static void fimc_dst_set_transf(struct fimc_context *ctx, unsigned int rotation)
{
	unsigned int degree = rotation & DRM_MODE_ROTATE_MASK;
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "rotation[0x%x]\n", rotation);

	cfg = fimc_read(ctx, EXYNOS_CITRGFMT);
	cfg &= ~EXYNOS_CITRGFMT_FLIP_MASK;
	cfg &= ~EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE;

	switch (degree) {
	case DRM_MODE_ROTATE_0:
		if (rotation & DRM_MODE_REFLECT_X)
			cfg |= EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg |= EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_90:
		cfg |= EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg |= EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg |= EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_180:
		cfg |= (EXYNOS_CITRGFMT_FLIP_X_MIRROR |
			EXYNOS_CITRGFMT_FLIP_Y_MIRROR);
		if (rotation & DRM_MODE_REFLECT_X)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_270:
		cfg |= (EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE |
			EXYNOS_CITRGFMT_FLIP_X_MIRROR |
			EXYNOS_CITRGFMT_FLIP_Y_MIRROR);
		if (rotation & DRM_MODE_REFLECT_X)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg &= ~EXYNOS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	}

	fimc_write(ctx, cfg, EXYNOS_CITRGFMT);
}

static int fimc_set_prescaler(struct fimc_context *ctx, struct fimc_scaler *sc,
			      struct drm_exynos_ipp_task_rect *src,
			      struct drm_exynos_ipp_task_rect *dst)
{
	u32 cfg, cfg_ext, shfactor;
	u32 pre_dst_width, pre_dst_height;
	u32 hfactor, vfactor;
	int ret = 0;
	u32 src_w, src_h, dst_w, dst_h;

	cfg_ext = fimc_read(ctx, EXYNOS_CITRGFMT);
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

	/* fimc_ippdrv_check_property assures that dividers are not null */
	hfactor = fls(src_w / dst_w / 2);
	if (hfactor > FIMC_SHFACTOR / 2) {
		dev_err(ctx->dev, "failed to get ratio horizontal.\n");
		return -EINVAL;
	}

	vfactor = fls(src_h / dst_h / 2);
	if (vfactor > FIMC_SHFACTOR / 2) {
		dev_err(ctx->dev, "failed to get ratio vertical.\n");
		return -EINVAL;
	}

	pre_dst_width = src_w >> hfactor;
	pre_dst_height = src_h >> vfactor;
	DRM_DEV_DEBUG_KMS(ctx->dev, "pre_dst_width[%d]pre_dst_height[%d]\n",
			  pre_dst_width, pre_dst_height);
	DRM_DEV_DEBUG_KMS(ctx->dev, "hfactor[%d]vfactor[%d]\n", hfactor,
			  vfactor);

	sc->hratio = (src_w << 14) / (dst_w << hfactor);
	sc->vratio = (src_h << 14) / (dst_h << vfactor);
	sc->up_h = (dst_w >= src_w) ? true : false;
	sc->up_v = (dst_h >= src_h) ? true : false;
	DRM_DEV_DEBUG_KMS(ctx->dev, "hratio[%d]vratio[%d]up_h[%d]up_v[%d]\n",
			  sc->hratio, sc->vratio, sc->up_h, sc->up_v);

	shfactor = FIMC_SHFACTOR - (hfactor + vfactor);
	DRM_DEV_DEBUG_KMS(ctx->dev, "shfactor[%d]\n", shfactor);

	cfg = (EXYNOS_CISCPRERATIO_SHFACTOR(shfactor) |
		EXYNOS_CISCPRERATIO_PREHORRATIO(1 << hfactor) |
		EXYNOS_CISCPRERATIO_PREVERRATIO(1 << vfactor));
	fimc_write(ctx, cfg, EXYNOS_CISCPRERATIO);

	cfg = (EXYNOS_CISCPREDST_PREDSTWIDTH(pre_dst_width) |
		EXYNOS_CISCPREDST_PREDSTHEIGHT(pre_dst_height));
	fimc_write(ctx, cfg, EXYNOS_CISCPREDST);

	return ret;
}

static void fimc_set_scaler(struct fimc_context *ctx, struct fimc_scaler *sc)
{
	u32 cfg, cfg_ext;

	DRM_DEV_DEBUG_KMS(ctx->dev, "range[%d]bypass[%d]up_h[%d]up_v[%d]\n",
			  sc->range, sc->bypass, sc->up_h, sc->up_v);
	DRM_DEV_DEBUG_KMS(ctx->dev, "hratio[%d]vratio[%d]\n",
			  sc->hratio, sc->vratio);

	cfg = fimc_read(ctx, EXYNOS_CISCCTRL);
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
	fimc_write(ctx, cfg, EXYNOS_CISCCTRL);

	cfg_ext = fimc_read(ctx, EXYNOS_CIEXTEN);
	cfg_ext &= ~EXYNOS_CIEXTEN_MAINHORRATIO_EXT_MASK;
	cfg_ext &= ~EXYNOS_CIEXTEN_MAINVERRATIO_EXT_MASK;
	cfg_ext |= (EXYNOS_CIEXTEN_MAINHORRATIO_EXT(sc->hratio) |
		EXYNOS_CIEXTEN_MAINVERRATIO_EXT(sc->vratio));
	fimc_write(ctx, cfg_ext, EXYNOS_CIEXTEN);
}

static void fimc_dst_set_size(struct fimc_context *ctx,
			     struct exynos_drm_ipp_buffer *buf)
{
	unsigned int real_width = buf->buf.pitch[0] / buf->format->cpp[0];
	u32 cfg, cfg_ext;

	DRM_DEV_DEBUG_KMS(ctx->dev, "hsize[%d]vsize[%d]\n", real_width,
			  buf->buf.height);

	/* original size */
	cfg = (EXYNOS_ORGOSIZE_HORIZONTAL(real_width) |
		EXYNOS_ORGOSIZE_VERTICAL(buf->buf.height));

	fimc_write(ctx, cfg, EXYNOS_ORGOSIZE);

	DRM_DEV_DEBUG_KMS(ctx->dev, "x[%d]y[%d]w[%d]h[%d]\n", buf->rect.x,
			  buf->rect.y,
			  buf->rect.w, buf->rect.h);

	/* CSC ITU */
	cfg = fimc_read(ctx, EXYNOS_CIGCTRL);
	cfg &= ~EXYNOS_CIGCTRL_CSC_MASK;

	if (buf->buf.width >= FIMC_WIDTH_ITU_709)
		cfg |= EXYNOS_CIGCTRL_CSC_ITU709;
	else
		cfg |= EXYNOS_CIGCTRL_CSC_ITU601;

	fimc_write(ctx, cfg, EXYNOS_CIGCTRL);

	cfg_ext = fimc_read(ctx, EXYNOS_CITRGFMT);

	/* target image size */
	cfg = fimc_read(ctx, EXYNOS_CITRGFMT);
	cfg &= ~(EXYNOS_CITRGFMT_TARGETH_MASK |
		EXYNOS_CITRGFMT_TARGETV_MASK);
	if (cfg_ext & EXYNOS_CITRGFMT_OUTROT90_CLOCKWISE)
		cfg |= (EXYNOS_CITRGFMT_TARGETHSIZE(buf->rect.h) |
			EXYNOS_CITRGFMT_TARGETVSIZE(buf->rect.w));
	else
		cfg |= (EXYNOS_CITRGFMT_TARGETHSIZE(buf->rect.w) |
			EXYNOS_CITRGFMT_TARGETVSIZE(buf->rect.h));
	fimc_write(ctx, cfg, EXYNOS_CITRGFMT);

	/* target area */
	cfg = EXYNOS_CITAREA_TARGET_AREA(buf->rect.w * buf->rect.h);
	fimc_write(ctx, cfg, EXYNOS_CITAREA);

	/* offset Y(RGB), Cb, Cr */
	cfg = (EXYNOS_CIOYOFF_HORIZONTAL(buf->rect.x) |
		EXYNOS_CIOYOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYNOS_CIOYOFF);
	cfg = (EXYNOS_CIOCBOFF_HORIZONTAL(buf->rect.x) |
		EXYNOS_CIOCBOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYNOS_CIOCBOFF);
	cfg = (EXYNOS_CIOCROFF_HORIZONTAL(buf->rect.x) |
		EXYNOS_CIOCROFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYNOS_CIOCROFF);
}

static void fimc_dst_set_buf_seq(struct fimc_context *ctx, u32 buf_id,
		bool enqueue)
{
	unsigned long flags;
	u32 buf_num;
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "buf_id[%d]enqueu[%d]\n", buf_id, enqueue);

	spin_lock_irqsave(&ctx->lock, flags);

	cfg = fimc_read(ctx, EXYNOS_CIFCNTSEQ);

	if (enqueue)
		cfg |= (1 << buf_id);
	else
		cfg &= ~(1 << buf_id);

	fimc_write(ctx, cfg, EXYNOS_CIFCNTSEQ);

	buf_num = hweight32(cfg);

	if (enqueue && buf_num >= FIMC_BUF_START)
		fimc_mask_irq(ctx, true);
	else if (!enqueue && buf_num <= FIMC_BUF_STOP)
		fimc_mask_irq(ctx, false);

	spin_unlock_irqrestore(&ctx->lock, flags);
}

static void fimc_dst_set_addr(struct fimc_context *ctx,
			     struct exynos_drm_ipp_buffer *buf)
{
	fimc_write(ctx, buf->dma_addr[0], EXYNOS_CIOYSA(0));
	fimc_write(ctx, buf->dma_addr[1], EXYNOS_CIOCBSA(0));
	fimc_write(ctx, buf->dma_addr[2], EXYNOS_CIOCRSA(0));

	fimc_dst_set_buf_seq(ctx, 0, true);
}

static void fimc_stop(struct fimc_context *ctx);

static irqreturn_t fimc_irq_handler(int irq, void *dev_id)
{
	struct fimc_context *ctx = dev_id;
	int buf_id;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fimc id[%d]\n", ctx->id);

	fimc_clear_irq(ctx);
	if (fimc_check_ovf(ctx))
		return IRQ_NONE;

	if (!fimc_check_frame_end(ctx))
		return IRQ_NONE;

	buf_id = fimc_get_buf_id(ctx);
	if (buf_id < 0)
		return IRQ_HANDLED;

	DRM_DEV_DEBUG_KMS(ctx->dev, "buf_id[%d]\n", buf_id);

	if (ctx->task) {
		struct exynos_drm_ipp_task *task = ctx->task;

		ctx->task = NULL;
		pm_runtime_mark_last_busy(ctx->dev);
		pm_runtime_put_autosuspend(ctx->dev);
		exynos_drm_ipp_task_done(task, 0);
	}

	fimc_dst_set_buf_seq(ctx, buf_id, false);
	fimc_stop(ctx);

	return IRQ_HANDLED;
}

static void fimc_clear_addr(struct fimc_context *ctx)
{
	int i;

	for (i = 0; i < FIMC_MAX_SRC; i++) {
		fimc_write(ctx, 0, EXYNOS_CIIYSA(i));
		fimc_write(ctx, 0, EXYNOS_CIICBSA(i));
		fimc_write(ctx, 0, EXYNOS_CIICRSA(i));
	}

	for (i = 0; i < FIMC_MAX_DST; i++) {
		fimc_write(ctx, 0, EXYNOS_CIOYSA(i));
		fimc_write(ctx, 0, EXYNOS_CIOCBSA(i));
		fimc_write(ctx, 0, EXYNOS_CIOCRSA(i));
	}
}

static void fimc_reset(struct fimc_context *ctx)
{
	/* reset h/w block */
	fimc_sw_reset(ctx);

	/* reset scaler capability */
	memset(&ctx->sc, 0x0, sizeof(ctx->sc));

	fimc_clear_addr(ctx);
}

static void fimc_start(struct fimc_context *ctx)
{
	u32 cfg0, cfg1;

	fimc_mask_irq(ctx, true);

	/* If set true, we can save jpeg about screen */
	fimc_handle_jpeg(ctx, false);
	fimc_set_scaler(ctx, &ctx->sc);

	fimc_set_type_ctrl(ctx);
	fimc_handle_lastend(ctx, false);

	/* setup dma */
	cfg0 = fimc_read(ctx, EXYNOS_MSCTRL);
	cfg0 &= ~EXYNOS_MSCTRL_INPUT_MASK;
	cfg0 |= EXYNOS_MSCTRL_INPUT_MEMORY;
	fimc_write(ctx, cfg0, EXYNOS_MSCTRL);

	/* Reset status */
	fimc_write(ctx, 0x0, EXYNOS_CISTATUS);

	cfg0 = fimc_read(ctx, EXYNOS_CIIMGCPT);
	cfg0 &= ~EXYNOS_CIIMGCPT_IMGCPTEN_SC;
	cfg0 |= EXYNOS_CIIMGCPT_IMGCPTEN_SC;

	/* Scaler */
	cfg1 = fimc_read(ctx, EXYNOS_CISCCTRL);
	cfg1 &= ~EXYNOS_CISCCTRL_SCAN_MASK;
	cfg1 |= (EXYNOS_CISCCTRL_PROGRESSIVE |
		EXYNOS_CISCCTRL_SCALERSTART);

	fimc_write(ctx, cfg1, EXYNOS_CISCCTRL);

	/* Enable image capture*/
	cfg0 |= EXYNOS_CIIMGCPT_IMGCPTEN;
	fimc_write(ctx, cfg0, EXYNOS_CIIMGCPT);

	/* Disable frame end irq */
	fimc_clear_bits(ctx, EXYNOS_CIGCTRL, EXYNOS_CIGCTRL_IRQ_END_DISABLE);

	fimc_clear_bits(ctx, EXYNOS_CIOCTRL, EXYNOS_CIOCTRL_WEAVE_MASK);

	fimc_set_bits(ctx, EXYNOS_MSCTRL, EXYNOS_MSCTRL_ENVID);
}

static void fimc_stop(struct fimc_context *ctx)
{
	u32 cfg;

	/* Source clear */
	cfg = fimc_read(ctx, EXYNOS_MSCTRL);
	cfg &= ~EXYNOS_MSCTRL_INPUT_MASK;
	cfg &= ~EXYNOS_MSCTRL_ENVID;
	fimc_write(ctx, cfg, EXYNOS_MSCTRL);

	fimc_mask_irq(ctx, false);

	/* reset sequence */
	fimc_write(ctx, 0x0, EXYNOS_CIFCNTSEQ);

	/* Scaler disable */
	fimc_clear_bits(ctx, EXYNOS_CISCCTRL, EXYNOS_CISCCTRL_SCALERSTART);

	/* Disable image capture */
	fimc_clear_bits(ctx, EXYNOS_CIIMGCPT,
		EXYNOS_CIIMGCPT_IMGCPTEN_SC | EXYNOS_CIIMGCPT_IMGCPTEN);

	/* Enable frame end irq */
	fimc_set_bits(ctx, EXYNOS_CIGCTRL, EXYNOS_CIGCTRL_IRQ_END_DISABLE);
}

static int fimc_commit(struct exynos_drm_ipp *ipp,
			  struct exynos_drm_ipp_task *task)
{
	struct fimc_context *ctx =
			container_of(ipp, struct fimc_context, ipp);

	pm_runtime_get_sync(ctx->dev);
	ctx->task = task;

	fimc_src_set_fmt(ctx, task->src.buf.fourcc, task->src.buf.modifier);
	fimc_src_set_size(ctx, &task->src);
	fimc_src_set_transf(ctx, DRM_MODE_ROTATE_0);
	fimc_src_set_addr(ctx, &task->src);
	fimc_dst_set_fmt(ctx, task->dst.buf.fourcc, task->dst.buf.modifier);
	fimc_dst_set_transf(ctx, task->transform.rotation);
	fimc_dst_set_size(ctx, &task->dst);
	fimc_dst_set_addr(ctx, &task->dst);
	fimc_set_prescaler(ctx, &ctx->sc, &task->src.rect, &task->dst.rect);
	fimc_start(ctx);

	return 0;
}

static void fimc_abort(struct exynos_drm_ipp *ipp,
			  struct exynos_drm_ipp_task *task)
{
	struct fimc_context *ctx =
			container_of(ipp, struct fimc_context, ipp);

	fimc_reset(ctx);

	if (ctx->task) {
		struct exynos_drm_ipp_task *task = ctx->task;

		ctx->task = NULL;
		pm_runtime_mark_last_busy(ctx->dev);
		pm_runtime_put_autosuspend(ctx->dev);
		exynos_drm_ipp_task_done(task, -EIO);
	}
}

static struct exynos_drm_ipp_funcs ipp_funcs = {
	.commit = fimc_commit,
	.abort = fimc_abort,
};

static int fimc_bind(struct device *dev, struct device *master, void *data)
{
	struct fimc_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_ipp *ipp = &ctx->ipp;

	ctx->drm_dev = drm_dev;
	ipp->drm_dev = drm_dev;
	exynos_drm_register_dma(drm_dev, dev, &ctx->dma_priv);

	exynos_drm_ipp_register(dev, ipp, &ipp_funcs,
			DRM_EXYNOS_IPP_CAP_CROP | DRM_EXYNOS_IPP_CAP_ROTATE |
			DRM_EXYNOS_IPP_CAP_SCALE | DRM_EXYNOS_IPP_CAP_CONVERT,
			ctx->formats, ctx->num_formats, "fimc");

	dev_info(dev, "The exynos fimc has been probed successfully\n");

	return 0;
}

static void fimc_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct fimc_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_ipp *ipp = &ctx->ipp;

	exynos_drm_ipp_unregister(dev, ipp);
	exynos_drm_unregister_dma(drm_dev, dev, &ctx->dma_priv);
}

static const struct component_ops fimc_component_ops = {
	.bind	= fimc_bind,
	.unbind = fimc_unbind,
};

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
	struct device *fimc_dev = ctx->dev;
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
			ret = PTR_ERR(ctx->clocks[i]);
			dev_err(fimc_dev, "failed to get clock: %s\n",
						fimc_clock_names[i]);
			goto e_clk_free;
		}
	}

	ret = clk_prepare_enable(ctx->clocks[FIMC_CLK_LCLK]);
	if (!ret)
		return ret;
e_clk_free:
	fimc_put_clocks(ctx);
	return ret;
}

int exynos_drm_check_fimc_device(struct device *dev)
{
	int id = of_alias_get_id(dev->of_node, "fimc");

	if (id >= 0 && (BIT(id) & fimc_mask))
		return 0;
	return -ENODEV;
}

static const unsigned int fimc_formats[] = {
	DRM_FORMAT_XRGB8888, DRM_FORMAT_RGB565,
	DRM_FORMAT_NV12, DRM_FORMAT_NV16, DRM_FORMAT_NV21, DRM_FORMAT_NV61,
	DRM_FORMAT_UYVY, DRM_FORMAT_VYUY, DRM_FORMAT_YUYV, DRM_FORMAT_YVYU,
	DRM_FORMAT_YUV420, DRM_FORMAT_YVU420, DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV444,
};

static const unsigned int fimc_tiled_formats[] = {
	DRM_FORMAT_NV12, DRM_FORMAT_NV21,
};

static const struct drm_exynos_ipp_limit fimc_4210_limits_v1[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 16, 8192, 8 }, .v = { 16, 8192, 2 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 16, 4224, 2 }, .v = { 16, 0, 2 }) },
	{ IPP_SIZE_LIMIT(ROTATED, .h = { 128, 1920 }, .v = { 128, 0 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static const struct drm_exynos_ipp_limit fimc_4210_limits_v2[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 16, 8192, 8 }, .v = { 16, 8192, 2 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 16, 1920, 2 }, .v = { 16, 0, 2 }) },
	{ IPP_SIZE_LIMIT(ROTATED, .h = { 128, 1366 }, .v = { 128, 0 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static const struct drm_exynos_ipp_limit fimc_4210_limits_tiled_v1[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 128, 1920, 128 }, .v = { 32, 1920, 32 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 128, 1920, 2 }, .v = { 128, 0, 2 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static const struct drm_exynos_ipp_limit fimc_4210_limits_tiled_v2[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 128, 1920, 128 }, .v = { 32, 1920, 32 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 128, 1366, 2 }, .v = { 128, 0, 2 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static int fimc_probe(struct platform_device *pdev)
{
	const struct drm_exynos_ipp_limit *limits;
	struct exynos_drm_ipp_formats *formats;
	struct device *dev = &pdev->dev;
	struct fimc_context *ctx;
	struct resource *res;
	int ret;
	int i, j, num_limits, num_formats;

	if (exynos_drm_check_fimc_device(dev) != 0)
		return -ENODEV;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	ctx->id = of_alias_get_id(dev->of_node, "fimc");

	/* construct formats/limits array */
	num_formats = ARRAY_SIZE(fimc_formats) + ARRAY_SIZE(fimc_tiled_formats);
	formats = devm_kcalloc(dev, num_formats, sizeof(*formats),
			       GFP_KERNEL);
	if (!formats)
		return -ENOMEM;

	/* linear formats */
	if (ctx->id < 3) {
		limits = fimc_4210_limits_v1;
		num_limits = ARRAY_SIZE(fimc_4210_limits_v1);
	} else {
		limits = fimc_4210_limits_v2;
		num_limits = ARRAY_SIZE(fimc_4210_limits_v2);
	}
	for (i = 0; i < ARRAY_SIZE(fimc_formats); i++) {
		formats[i].fourcc = fimc_formats[i];
		formats[i].type = DRM_EXYNOS_IPP_FORMAT_SOURCE |
				  DRM_EXYNOS_IPP_FORMAT_DESTINATION;
		formats[i].limits = limits;
		formats[i].num_limits = num_limits;
	}

	/* tiled formats */
	if (ctx->id < 3) {
		limits = fimc_4210_limits_tiled_v1;
		num_limits = ARRAY_SIZE(fimc_4210_limits_tiled_v1);
	} else {
		limits = fimc_4210_limits_tiled_v2;
		num_limits = ARRAY_SIZE(fimc_4210_limits_tiled_v2);
	}
	for (j = i, i = 0; i < ARRAY_SIZE(fimc_tiled_formats); j++, i++) {
		formats[j].fourcc = fimc_tiled_formats[i];
		formats[j].modifier = DRM_FORMAT_MOD_SAMSUNG_64_32_TILE;
		formats[j].type = DRM_EXYNOS_IPP_FORMAT_SOURCE |
				  DRM_EXYNOS_IPP_FORMAT_DESTINATION;
		formats[j].limits = limits;
		formats[j].num_limits = num_limits;
	}

	ctx->formats = formats;
	ctx->num_formats = num_formats;

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

	ret = devm_request_irq(dev, res->start, fimc_irq_handler,
		0, dev_name(dev), ctx);
	if (ret < 0) {
		dev_err(dev, "failed to request irq.\n");
		return ret;
	}

	ret = fimc_setup_clocks(ctx);
	if (ret < 0)
		return ret;

	spin_lock_init(&ctx->lock);
	platform_set_drvdata(pdev, ctx);

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, FIMC_AUTOSUSPEND_DELAY);
	pm_runtime_enable(dev);

	ret = component_add(dev, &fimc_component_ops);
	if (ret)
		goto err_pm_dis;

	dev_info(dev, "drm fimc registered successfully.\n");

	return 0;

err_pm_dis:
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	fimc_put_clocks(ctx);

	return ret;
}

static int fimc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimc_context *ctx = get_fimc_context(dev);

	component_del(dev, &fimc_component_ops);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	fimc_put_clocks(ctx);

	return 0;
}

#ifdef CONFIG_PM
static int fimc_runtime_suspend(struct device *dev)
{
	struct fimc_context *ctx = get_fimc_context(dev);

	DRM_DEV_DEBUG_KMS(dev, "id[%d]\n", ctx->id);
	clk_disable_unprepare(ctx->clocks[FIMC_CLK_GATE]);
	return 0;
}

static int fimc_runtime_resume(struct device *dev)
{
	struct fimc_context *ctx = get_fimc_context(dev);

	DRM_DEV_DEBUG_KMS(dev, "id[%d]\n", ctx->id);
	return clk_prepare_enable(ctx->clocks[FIMC_CLK_GATE]);
}
#endif

static const struct dev_pm_ops fimc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(fimc_runtime_suspend, fimc_runtime_resume, NULL)
};

static const struct of_device_id fimc_of_match[] = {
	{ .compatible = "samsung,exynos4210-fimc" },
	{ .compatible = "samsung,exynos4212-fimc" },
	{ },
};
MODULE_DEVICE_TABLE(of, fimc_of_match);

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
