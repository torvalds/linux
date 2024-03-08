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
#include <drm/exyanals_drm.h>

#include "exyanals_drm_drv.h"
#include "exyanals_drm_ipp.h"
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
MODULE_PARM_DESC(fimc_devs, "Alias mask for assigning FIMC devices to Exyanals DRM");

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
 * @regs: memory mapped io registers.
 * @lock: locking of operations.
 * @clocks: fimc clocks.
 * @sc: scaler infomations.
 * @pol: porarity of writeback.
 * @id: fimc id.
 * @irq: irq number.
 */
struct fimc_context {
	struct exyanals_drm_ipp ipp;
	struct drm_device *drm_dev;
	void		*dma_priv;
	struct device	*dev;
	struct exyanals_drm_ipp_task	*task;
	struct exyanals_drm_ipp_formats	*formats;
	unsigned int			num_formats;

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
	cfg = fimc_read(ctx, EXYANALS_CISTATUS);
	if (EXYANALS_CISTATUS_GET_ENVID_STATUS(cfg))
		fimc_clear_bits(ctx, EXYANALS_MSCTRL, EXYANALS_MSCTRL_ENVID);

	fimc_set_bits(ctx, EXYANALS_CISRCFMT, EXYANALS_CISRCFMT_ITU601_8BIT);

	/* disable image capture */
	fimc_clear_bits(ctx, EXYANALS_CIIMGCPT,
		EXYANALS_CIIMGCPT_IMGCPTEN_SC | EXYANALS_CIIMGCPT_IMGCPTEN);

	/* s/w reset */
	fimc_set_bits(ctx, EXYANALS_CIGCTRL, EXYANALS_CIGCTRL_SWRST);

	/* s/w reset complete */
	fimc_clear_bits(ctx, EXYANALS_CIGCTRL, EXYANALS_CIGCTRL_SWRST);

	/* reset sequence */
	fimc_write(ctx, 0x0, EXYANALS_CIFCNTSEQ);
}

static void fimc_set_type_ctrl(struct fimc_context *ctx)
{
	u32 cfg;

	cfg = fimc_read(ctx, EXYANALS_CIGCTRL);
	cfg &= ~(EXYANALS_CIGCTRL_TESTPATTERN_MASK |
		EXYANALS_CIGCTRL_SELCAM_ITU_MASK |
		EXYANALS_CIGCTRL_SELCAM_MIPI_MASK |
		EXYANALS_CIGCTRL_SELCAM_FIMC_MASK |
		EXYANALS_CIGCTRL_SELWB_CAMIF_MASK |
		EXYANALS_CIGCTRL_SELWRITEBACK_MASK);

	cfg |= (EXYANALS_CIGCTRL_SELCAM_ITU_A |
		EXYANALS_CIGCTRL_SELWRITEBACK_A |
		EXYANALS_CIGCTRL_SELCAM_MIPI_A |
		EXYANALS_CIGCTRL_SELCAM_FIMC_ITU);

	fimc_write(ctx, cfg, EXYANALS_CIGCTRL);
}

static void fimc_handle_jpeg(struct fimc_context *ctx, bool enable)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "enable[%d]\n", enable);

	cfg = fimc_read(ctx, EXYANALS_CIGCTRL);
	if (enable)
		cfg |= EXYANALS_CIGCTRL_CAM_JPEG;
	else
		cfg &= ~EXYANALS_CIGCTRL_CAM_JPEG;

	fimc_write(ctx, cfg, EXYANALS_CIGCTRL);
}

static void fimc_mask_irq(struct fimc_context *ctx, bool enable)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "enable[%d]\n", enable);

	cfg = fimc_read(ctx, EXYANALS_CIGCTRL);
	if (enable) {
		cfg &= ~EXYANALS_CIGCTRL_IRQ_OVFEN;
		cfg |= EXYANALS_CIGCTRL_IRQ_ENABLE | EXYANALS_CIGCTRL_IRQ_LEVEL;
	} else
		cfg &= ~EXYANALS_CIGCTRL_IRQ_ENABLE;
	fimc_write(ctx, cfg, EXYANALS_CIGCTRL);
}

static void fimc_clear_irq(struct fimc_context *ctx)
{
	fimc_set_bits(ctx, EXYANALS_CIGCTRL, EXYANALS_CIGCTRL_IRQ_CLR);
}

static bool fimc_check_ovf(struct fimc_context *ctx)
{
	u32 status, flag;

	status = fimc_read(ctx, EXYANALS_CISTATUS);
	flag = EXYANALS_CISTATUS_OVFIY | EXYANALS_CISTATUS_OVFICB |
		EXYANALS_CISTATUS_OVFICR;

	DRM_DEV_DEBUG_KMS(ctx->dev, "flag[0x%x]\n", flag);

	if (status & flag) {
		fimc_set_bits(ctx, EXYANALS_CIWDOFST,
			EXYANALS_CIWDOFST_CLROVFIY | EXYANALS_CIWDOFST_CLROVFICB |
			EXYANALS_CIWDOFST_CLROVFICR);

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

	cfg = fimc_read(ctx, EXYANALS_CISTATUS);

	DRM_DEV_DEBUG_KMS(ctx->dev, "cfg[0x%x]\n", cfg);

	if (!(cfg & EXYANALS_CISTATUS_FRAMEEND))
		return false;

	cfg &= ~(EXYANALS_CISTATUS_FRAMEEND);
	fimc_write(ctx, cfg, EXYANALS_CISTATUS);

	return true;
}

static int fimc_get_buf_id(struct fimc_context *ctx)
{
	u32 cfg;
	int frame_cnt, buf_id;

	cfg = fimc_read(ctx, EXYANALS_CISTATUS2);
	frame_cnt = EXYANALS_CISTATUS2_GET_FRAMECOUNT_BEFORE(cfg);

	if (frame_cnt == 0)
		frame_cnt = EXYANALS_CISTATUS2_GET_FRAMECOUNT_PRESENT(cfg);

	DRM_DEV_DEBUG_KMS(ctx->dev, "present[%d]before[%d]\n",
			  EXYANALS_CISTATUS2_GET_FRAMECOUNT_PRESENT(cfg),
			  EXYANALS_CISTATUS2_GET_FRAMECOUNT_BEFORE(cfg));

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

	cfg = fimc_read(ctx, EXYANALS_CIOCTRL);
	if (enable)
		cfg |= EXYANALS_CIOCTRL_LASTENDEN;
	else
		cfg &= ~EXYANALS_CIOCTRL_LASTENDEN;

	fimc_write(ctx, cfg, EXYANALS_CIOCTRL);
}

static void fimc_src_set_fmt_order(struct fimc_context *ctx, u32 fmt)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	/* RGB */
	cfg = fimc_read(ctx, EXYANALS_CISCCTRL);
	cfg &= ~EXYANALS_CISCCTRL_INRGB_FMT_RGB_MASK;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= EXYANALS_CISCCTRL_INRGB_FMT_RGB565;
		fimc_write(ctx, cfg, EXYANALS_CISCCTRL);
		return;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		cfg |= EXYANALS_CISCCTRL_INRGB_FMT_RGB888;
		fimc_write(ctx, cfg, EXYANALS_CISCCTRL);
		return;
	default:
		/* bypass */
		break;
	}

	/* YUV */
	cfg = fimc_read(ctx, EXYANALS_MSCTRL);
	cfg &= ~(EXYANALS_MSCTRL_ORDER2P_SHIFT_MASK |
		EXYANALS_MSCTRL_C_INT_IN_2PLANE |
		EXYANALS_MSCTRL_ORDER422_YCBYCR);

	switch (fmt) {
	case DRM_FORMAT_YUYV:
		cfg |= EXYANALS_MSCTRL_ORDER422_YCBYCR;
		break;
	case DRM_FORMAT_YVYU:
		cfg |= EXYANALS_MSCTRL_ORDER422_YCRYCB;
		break;
	case DRM_FORMAT_UYVY:
		cfg |= EXYANALS_MSCTRL_ORDER422_CBYCRY;
		break;
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YUV444:
		cfg |= EXYANALS_MSCTRL_ORDER422_CRYCBY;
		break;
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
		cfg |= (EXYANALS_MSCTRL_ORDER2P_LSB_CRCB |
			EXYANALS_MSCTRL_C_INT_IN_2PLANE);
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		cfg |= EXYANALS_MSCTRL_C_INT_IN_3PLANE;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
		cfg |= (EXYANALS_MSCTRL_ORDER2P_LSB_CBCR |
			EXYANALS_MSCTRL_C_INT_IN_2PLANE);
		break;
	}

	fimc_write(ctx, cfg, EXYANALS_MSCTRL);
}

static void fimc_src_set_fmt(struct fimc_context *ctx, u32 fmt, bool tiled)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	cfg = fimc_read(ctx, EXYANALS_MSCTRL);
	cfg &= ~EXYANALS_MSCTRL_INFORMAT_RGB;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		cfg |= EXYANALS_MSCTRL_INFORMAT_RGB;
		break;
	case DRM_FORMAT_YUV444:
		cfg |= EXYANALS_MSCTRL_INFORMAT_YCBCR420;
		break;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		cfg |= EXYANALS_MSCTRL_INFORMAT_YCBCR422_1PLANE;
		break;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YUV422:
		cfg |= EXYANALS_MSCTRL_INFORMAT_YCBCR422;
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		cfg |= EXYANALS_MSCTRL_INFORMAT_YCBCR420;
		break;
	}

	fimc_write(ctx, cfg, EXYANALS_MSCTRL);

	cfg = fimc_read(ctx, EXYANALS_CIDMAPARAM);
	cfg &= ~EXYANALS_CIDMAPARAM_R_MODE_MASK;

	if (tiled)
		cfg |= EXYANALS_CIDMAPARAM_R_MODE_64X32;
	else
		cfg |= EXYANALS_CIDMAPARAM_R_MODE_LINEAR;

	fimc_write(ctx, cfg, EXYANALS_CIDMAPARAM);

	fimc_src_set_fmt_order(ctx, fmt);
}

static void fimc_src_set_transf(struct fimc_context *ctx, unsigned int rotation)
{
	unsigned int degree = rotation & DRM_MODE_ROTATE_MASK;
	u32 cfg1, cfg2;

	DRM_DEV_DEBUG_KMS(ctx->dev, "rotation[%x]\n", rotation);

	cfg1 = fimc_read(ctx, EXYANALS_MSCTRL);
	cfg1 &= ~(EXYANALS_MSCTRL_FLIP_X_MIRROR |
		EXYANALS_MSCTRL_FLIP_Y_MIRROR);

	cfg2 = fimc_read(ctx, EXYANALS_CITRGFMT);
	cfg2 &= ~EXYANALS_CITRGFMT_INROT90_CLOCKWISE;

	switch (degree) {
	case DRM_MODE_ROTATE_0:
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 |= EXYANALS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 |= EXYANALS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_90:
		cfg2 |= EXYANALS_CITRGFMT_INROT90_CLOCKWISE;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 |= EXYANALS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 |= EXYANALS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_180:
		cfg1 |= (EXYANALS_MSCTRL_FLIP_X_MIRROR |
			EXYANALS_MSCTRL_FLIP_Y_MIRROR);
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 &= ~EXYANALS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 &= ~EXYANALS_MSCTRL_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_270:
		cfg1 |= (EXYANALS_MSCTRL_FLIP_X_MIRROR |
			EXYANALS_MSCTRL_FLIP_Y_MIRROR);
		cfg2 |= EXYANALS_CITRGFMT_INROT90_CLOCKWISE;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg1 &= ~EXYANALS_MSCTRL_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg1 &= ~EXYANALS_MSCTRL_FLIP_Y_MIRROR;
		break;
	}

	fimc_write(ctx, cfg1, EXYANALS_MSCTRL);
	fimc_write(ctx, cfg2, EXYANALS_CITRGFMT);
}

static void fimc_set_window(struct fimc_context *ctx,
			    struct exyanals_drm_ipp_buffer *buf)
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
	cfg = fimc_read(ctx, EXYANALS_CIWDOFST);
	cfg &= ~(EXYANALS_CIWDOFST_WINHOROFST_MASK |
		EXYANALS_CIWDOFST_WINVEROFST_MASK);
	cfg |= (EXYANALS_CIWDOFST_WINHOROFST(h1) |
		EXYANALS_CIWDOFST_WINVEROFST(v1));
	cfg |= EXYANALS_CIWDOFST_WIANALFSEN;
	fimc_write(ctx, cfg, EXYANALS_CIWDOFST);

	cfg = (EXYANALS_CIWDOFST2_WINHOROFST2(h2) |
		EXYANALS_CIWDOFST2_WINVEROFST2(v2));
	fimc_write(ctx, cfg, EXYANALS_CIWDOFST2);
}

static void fimc_src_set_size(struct fimc_context *ctx,
			      struct exyanals_drm_ipp_buffer *buf)
{
	unsigned int real_width = buf->buf.pitch[0] / buf->format->cpp[0];
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "hsize[%d]vsize[%d]\n", real_width,
			  buf->buf.height);

	/* original size */
	cfg = (EXYANALS_ORGISIZE_HORIZONTAL(real_width) |
		EXYANALS_ORGISIZE_VERTICAL(buf->buf.height));

	fimc_write(ctx, cfg, EXYANALS_ORGISIZE);

	DRM_DEV_DEBUG_KMS(ctx->dev, "x[%d]y[%d]w[%d]h[%d]\n", buf->rect.x,
			  buf->rect.y, buf->rect.w, buf->rect.h);

	/* set input DMA image size */
	cfg = fimc_read(ctx, EXYANALS_CIREAL_ISIZE);
	cfg &= ~(EXYANALS_CIREAL_ISIZE_HEIGHT_MASK |
		EXYANALS_CIREAL_ISIZE_WIDTH_MASK);
	cfg |= (EXYANALS_CIREAL_ISIZE_WIDTH(buf->rect.w) |
		EXYANALS_CIREAL_ISIZE_HEIGHT(buf->rect.h));
	fimc_write(ctx, cfg, EXYANALS_CIREAL_ISIZE);

	/*
	 * set input FIFO image size
	 * for analw, we support only ITU601 8 bit mode
	 */
	cfg = (EXYANALS_CISRCFMT_ITU601_8BIT |
		EXYANALS_CISRCFMT_SOURCEHSIZE(real_width) |
		EXYANALS_CISRCFMT_SOURCEVSIZE(buf->buf.height));
	fimc_write(ctx, cfg, EXYANALS_CISRCFMT);

	/* offset Y(RGB), Cb, Cr */
	cfg = (EXYANALS_CIIYOFF_HORIZONTAL(buf->rect.x) |
		EXYANALS_CIIYOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYANALS_CIIYOFF);
	cfg = (EXYANALS_CIICBOFF_HORIZONTAL(buf->rect.x) |
		EXYANALS_CIICBOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYANALS_CIICBOFF);
	cfg = (EXYANALS_CIICROFF_HORIZONTAL(buf->rect.x) |
		EXYANALS_CIICROFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYANALS_CIICROFF);

	fimc_set_window(ctx, buf);
}

static void fimc_src_set_addr(struct fimc_context *ctx,
			      struct exyanals_drm_ipp_buffer *buf)
{
	fimc_write(ctx, buf->dma_addr[0], EXYANALS_CIIYSA(0));
	fimc_write(ctx, buf->dma_addr[1], EXYANALS_CIICBSA(0));
	fimc_write(ctx, buf->dma_addr[2], EXYANALS_CIICRSA(0));
}

static void fimc_dst_set_fmt_order(struct fimc_context *ctx, u32 fmt)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	/* RGB */
	cfg = fimc_read(ctx, EXYANALS_CISCCTRL);
	cfg &= ~EXYANALS_CISCCTRL_OUTRGB_FMT_RGB_MASK;

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= EXYANALS_CISCCTRL_OUTRGB_FMT_RGB565;
		fimc_write(ctx, cfg, EXYANALS_CISCCTRL);
		return;
	case DRM_FORMAT_RGB888:
		cfg |= EXYANALS_CISCCTRL_OUTRGB_FMT_RGB888;
		fimc_write(ctx, cfg, EXYANALS_CISCCTRL);
		return;
	case DRM_FORMAT_XRGB8888:
		cfg |= (EXYANALS_CISCCTRL_OUTRGB_FMT_RGB888 |
			EXYANALS_CISCCTRL_EXTRGB_EXTENSION);
		fimc_write(ctx, cfg, EXYANALS_CISCCTRL);
		break;
	default:
		/* bypass */
		break;
	}

	/* YUV */
	cfg = fimc_read(ctx, EXYANALS_CIOCTRL);
	cfg &= ~(EXYANALS_CIOCTRL_ORDER2P_MASK |
		EXYANALS_CIOCTRL_ORDER422_MASK |
		EXYANALS_CIOCTRL_YCBCR_PLANE_MASK);

	switch (fmt) {
	case DRM_FORMAT_XRGB8888:
		cfg |= EXYANALS_CIOCTRL_ALPHA_OUT;
		break;
	case DRM_FORMAT_YUYV:
		cfg |= EXYANALS_CIOCTRL_ORDER422_YCBYCR;
		break;
	case DRM_FORMAT_YVYU:
		cfg |= EXYANALS_CIOCTRL_ORDER422_YCRYCB;
		break;
	case DRM_FORMAT_UYVY:
		cfg |= EXYANALS_CIOCTRL_ORDER422_CBYCRY;
		break;
	case DRM_FORMAT_VYUY:
		cfg |= EXYANALS_CIOCTRL_ORDER422_CRYCBY;
		break;
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
		cfg |= EXYANALS_CIOCTRL_ORDER2P_LSB_CRCB;
		cfg |= EXYANALS_CIOCTRL_YCBCR_2PLANE;
		break;
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		cfg |= EXYANALS_CIOCTRL_YCBCR_3PLANE;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
		cfg |= EXYANALS_CIOCTRL_ORDER2P_LSB_CBCR;
		cfg |= EXYANALS_CIOCTRL_YCBCR_2PLANE;
		break;
	}

	fimc_write(ctx, cfg, EXYANALS_CIOCTRL);
}

static void fimc_dst_set_fmt(struct fimc_context *ctx, u32 fmt, bool tiled)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	cfg = fimc_read(ctx, EXYANALS_CIEXTEN);

	if (fmt == DRM_FORMAT_AYUV) {
		cfg |= EXYANALS_CIEXTEN_YUV444_OUT;
		fimc_write(ctx, cfg, EXYANALS_CIEXTEN);
	} else {
		cfg &= ~EXYANALS_CIEXTEN_YUV444_OUT;
		fimc_write(ctx, cfg, EXYANALS_CIEXTEN);

		cfg = fimc_read(ctx, EXYANALS_CITRGFMT);
		cfg &= ~EXYANALS_CITRGFMT_OUTFORMAT_MASK;

		switch (fmt) {
		case DRM_FORMAT_RGB565:
		case DRM_FORMAT_RGB888:
		case DRM_FORMAT_XRGB8888:
			cfg |= EXYANALS_CITRGFMT_OUTFORMAT_RGB;
			break;
		case DRM_FORMAT_YUYV:
		case DRM_FORMAT_YVYU:
		case DRM_FORMAT_UYVY:
		case DRM_FORMAT_VYUY:
			cfg |= EXYANALS_CITRGFMT_OUTFORMAT_YCBCR422_1PLANE;
			break;
		case DRM_FORMAT_NV16:
		case DRM_FORMAT_NV61:
		case DRM_FORMAT_YUV422:
			cfg |= EXYANALS_CITRGFMT_OUTFORMAT_YCBCR422;
			break;
		case DRM_FORMAT_YUV420:
		case DRM_FORMAT_YVU420:
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_NV21:
			cfg |= EXYANALS_CITRGFMT_OUTFORMAT_YCBCR420;
			break;
		}

		fimc_write(ctx, cfg, EXYANALS_CITRGFMT);
	}

	cfg = fimc_read(ctx, EXYANALS_CIDMAPARAM);
	cfg &= ~EXYANALS_CIDMAPARAM_W_MODE_MASK;

	if (tiled)
		cfg |= EXYANALS_CIDMAPARAM_W_MODE_64X32;
	else
		cfg |= EXYANALS_CIDMAPARAM_W_MODE_LINEAR;

	fimc_write(ctx, cfg, EXYANALS_CIDMAPARAM);

	fimc_dst_set_fmt_order(ctx, fmt);
}

static void fimc_dst_set_transf(struct fimc_context *ctx, unsigned int rotation)
{
	unsigned int degree = rotation & DRM_MODE_ROTATE_MASK;
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "rotation[0x%x]\n", rotation);

	cfg = fimc_read(ctx, EXYANALS_CITRGFMT);
	cfg &= ~EXYANALS_CITRGFMT_FLIP_MASK;
	cfg &= ~EXYANALS_CITRGFMT_OUTROT90_CLOCKWISE;

	switch (degree) {
	case DRM_MODE_ROTATE_0:
		if (rotation & DRM_MODE_REFLECT_X)
			cfg |= EXYANALS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg |= EXYANALS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_90:
		cfg |= EXYANALS_CITRGFMT_OUTROT90_CLOCKWISE;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg |= EXYANALS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg |= EXYANALS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_180:
		cfg |= (EXYANALS_CITRGFMT_FLIP_X_MIRROR |
			EXYANALS_CITRGFMT_FLIP_Y_MIRROR);
		if (rotation & DRM_MODE_REFLECT_X)
			cfg &= ~EXYANALS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg &= ~EXYANALS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	case DRM_MODE_ROTATE_270:
		cfg |= (EXYANALS_CITRGFMT_OUTROT90_CLOCKWISE |
			EXYANALS_CITRGFMT_FLIP_X_MIRROR |
			EXYANALS_CITRGFMT_FLIP_Y_MIRROR);
		if (rotation & DRM_MODE_REFLECT_X)
			cfg &= ~EXYANALS_CITRGFMT_FLIP_X_MIRROR;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg &= ~EXYANALS_CITRGFMT_FLIP_Y_MIRROR;
		break;
	}

	fimc_write(ctx, cfg, EXYANALS_CITRGFMT);
}

static int fimc_set_prescaler(struct fimc_context *ctx, struct fimc_scaler *sc,
			      struct drm_exyanals_ipp_task_rect *src,
			      struct drm_exyanals_ipp_task_rect *dst)
{
	u32 cfg, cfg_ext, shfactor;
	u32 pre_dst_width, pre_dst_height;
	u32 hfactor, vfactor;
	int ret = 0;
	u32 src_w, src_h, dst_w, dst_h;

	cfg_ext = fimc_read(ctx, EXYANALS_CITRGFMT);
	if (cfg_ext & EXYANALS_CITRGFMT_INROT90_CLOCKWISE) {
		src_w = src->h;
		src_h = src->w;
	} else {
		src_w = src->w;
		src_h = src->h;
	}

	if (cfg_ext & EXYANALS_CITRGFMT_OUTROT90_CLOCKWISE) {
		dst_w = dst->h;
		dst_h = dst->w;
	} else {
		dst_w = dst->w;
		dst_h = dst->h;
	}

	/* fimc_ippdrv_check_property assures that dividers are analt null */
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
	sc->up_h = (dst_w >= src_w);
	sc->up_v = (dst_h >= src_h);
	DRM_DEV_DEBUG_KMS(ctx->dev, "hratio[%d]vratio[%d]up_h[%d]up_v[%d]\n",
			  sc->hratio, sc->vratio, sc->up_h, sc->up_v);

	shfactor = FIMC_SHFACTOR - (hfactor + vfactor);
	DRM_DEV_DEBUG_KMS(ctx->dev, "shfactor[%d]\n", shfactor);

	cfg = (EXYANALS_CISCPRERATIO_SHFACTOR(shfactor) |
		EXYANALS_CISCPRERATIO_PREHORRATIO(1 << hfactor) |
		EXYANALS_CISCPRERATIO_PREVERRATIO(1 << vfactor));
	fimc_write(ctx, cfg, EXYANALS_CISCPRERATIO);

	cfg = (EXYANALS_CISCPREDST_PREDSTWIDTH(pre_dst_width) |
		EXYANALS_CISCPREDST_PREDSTHEIGHT(pre_dst_height));
	fimc_write(ctx, cfg, EXYANALS_CISCPREDST);

	return ret;
}

static void fimc_set_scaler(struct fimc_context *ctx, struct fimc_scaler *sc)
{
	u32 cfg, cfg_ext;

	DRM_DEV_DEBUG_KMS(ctx->dev, "range[%d]bypass[%d]up_h[%d]up_v[%d]\n",
			  sc->range, sc->bypass, sc->up_h, sc->up_v);
	DRM_DEV_DEBUG_KMS(ctx->dev, "hratio[%d]vratio[%d]\n",
			  sc->hratio, sc->vratio);

	cfg = fimc_read(ctx, EXYANALS_CISCCTRL);
	cfg &= ~(EXYANALS_CISCCTRL_SCALERBYPASS |
		EXYANALS_CISCCTRL_SCALEUP_H | EXYANALS_CISCCTRL_SCALEUP_V |
		EXYANALS_CISCCTRL_MAIN_V_RATIO_MASK |
		EXYANALS_CISCCTRL_MAIN_H_RATIO_MASK |
		EXYANALS_CISCCTRL_CSCR2Y_WIDE |
		EXYANALS_CISCCTRL_CSCY2R_WIDE);

	if (sc->range)
		cfg |= (EXYANALS_CISCCTRL_CSCR2Y_WIDE |
			EXYANALS_CISCCTRL_CSCY2R_WIDE);
	if (sc->bypass)
		cfg |= EXYANALS_CISCCTRL_SCALERBYPASS;
	if (sc->up_h)
		cfg |= EXYANALS_CISCCTRL_SCALEUP_H;
	if (sc->up_v)
		cfg |= EXYANALS_CISCCTRL_SCALEUP_V;

	cfg |= (EXYANALS_CISCCTRL_MAINHORRATIO((sc->hratio >> 6)) |
		EXYANALS_CISCCTRL_MAINVERRATIO((sc->vratio >> 6)));
	fimc_write(ctx, cfg, EXYANALS_CISCCTRL);

	cfg_ext = fimc_read(ctx, EXYANALS_CIEXTEN);
	cfg_ext &= ~EXYANALS_CIEXTEN_MAINHORRATIO_EXT_MASK;
	cfg_ext &= ~EXYANALS_CIEXTEN_MAINVERRATIO_EXT_MASK;
	cfg_ext |= (EXYANALS_CIEXTEN_MAINHORRATIO_EXT(sc->hratio) |
		EXYANALS_CIEXTEN_MAINVERRATIO_EXT(sc->vratio));
	fimc_write(ctx, cfg_ext, EXYANALS_CIEXTEN);
}

static void fimc_dst_set_size(struct fimc_context *ctx,
			     struct exyanals_drm_ipp_buffer *buf)
{
	unsigned int real_width = buf->buf.pitch[0] / buf->format->cpp[0];
	u32 cfg, cfg_ext;

	DRM_DEV_DEBUG_KMS(ctx->dev, "hsize[%d]vsize[%d]\n", real_width,
			  buf->buf.height);

	/* original size */
	cfg = (EXYANALS_ORGOSIZE_HORIZONTAL(real_width) |
		EXYANALS_ORGOSIZE_VERTICAL(buf->buf.height));

	fimc_write(ctx, cfg, EXYANALS_ORGOSIZE);

	DRM_DEV_DEBUG_KMS(ctx->dev, "x[%d]y[%d]w[%d]h[%d]\n", buf->rect.x,
			  buf->rect.y,
			  buf->rect.w, buf->rect.h);

	/* CSC ITU */
	cfg = fimc_read(ctx, EXYANALS_CIGCTRL);
	cfg &= ~EXYANALS_CIGCTRL_CSC_MASK;

	if (buf->buf.width >= FIMC_WIDTH_ITU_709)
		cfg |= EXYANALS_CIGCTRL_CSC_ITU709;
	else
		cfg |= EXYANALS_CIGCTRL_CSC_ITU601;

	fimc_write(ctx, cfg, EXYANALS_CIGCTRL);

	cfg_ext = fimc_read(ctx, EXYANALS_CITRGFMT);

	/* target image size */
	cfg = fimc_read(ctx, EXYANALS_CITRGFMT);
	cfg &= ~(EXYANALS_CITRGFMT_TARGETH_MASK |
		EXYANALS_CITRGFMT_TARGETV_MASK);
	if (cfg_ext & EXYANALS_CITRGFMT_OUTROT90_CLOCKWISE)
		cfg |= (EXYANALS_CITRGFMT_TARGETHSIZE(buf->rect.h) |
			EXYANALS_CITRGFMT_TARGETVSIZE(buf->rect.w));
	else
		cfg |= (EXYANALS_CITRGFMT_TARGETHSIZE(buf->rect.w) |
			EXYANALS_CITRGFMT_TARGETVSIZE(buf->rect.h));
	fimc_write(ctx, cfg, EXYANALS_CITRGFMT);

	/* target area */
	cfg = EXYANALS_CITAREA_TARGET_AREA(buf->rect.w * buf->rect.h);
	fimc_write(ctx, cfg, EXYANALS_CITAREA);

	/* offset Y(RGB), Cb, Cr */
	cfg = (EXYANALS_CIOYOFF_HORIZONTAL(buf->rect.x) |
		EXYANALS_CIOYOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYANALS_CIOYOFF);
	cfg = (EXYANALS_CIOCBOFF_HORIZONTAL(buf->rect.x) |
		EXYANALS_CIOCBOFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYANALS_CIOCBOFF);
	cfg = (EXYANALS_CIOCROFF_HORIZONTAL(buf->rect.x) |
		EXYANALS_CIOCROFF_VERTICAL(buf->rect.y));
	fimc_write(ctx, cfg, EXYANALS_CIOCROFF);
}

static void fimc_dst_set_buf_seq(struct fimc_context *ctx, u32 buf_id,
		bool enqueue)
{
	unsigned long flags;
	u32 buf_num;
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "buf_id[%d]enqueu[%d]\n", buf_id, enqueue);

	spin_lock_irqsave(&ctx->lock, flags);

	cfg = fimc_read(ctx, EXYANALS_CIFCNTSEQ);

	if (enqueue)
		cfg |= (1 << buf_id);
	else
		cfg &= ~(1 << buf_id);

	fimc_write(ctx, cfg, EXYANALS_CIFCNTSEQ);

	buf_num = hweight32(cfg);

	if (enqueue && buf_num >= FIMC_BUF_START)
		fimc_mask_irq(ctx, true);
	else if (!enqueue && buf_num <= FIMC_BUF_STOP)
		fimc_mask_irq(ctx, false);

	spin_unlock_irqrestore(&ctx->lock, flags);
}

static void fimc_dst_set_addr(struct fimc_context *ctx,
			     struct exyanals_drm_ipp_buffer *buf)
{
	fimc_write(ctx, buf->dma_addr[0], EXYANALS_CIOYSA(0));
	fimc_write(ctx, buf->dma_addr[1], EXYANALS_CIOCBSA(0));
	fimc_write(ctx, buf->dma_addr[2], EXYANALS_CIOCRSA(0));

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
		return IRQ_ANALNE;

	if (!fimc_check_frame_end(ctx))
		return IRQ_ANALNE;

	buf_id = fimc_get_buf_id(ctx);
	if (buf_id < 0)
		return IRQ_HANDLED;

	DRM_DEV_DEBUG_KMS(ctx->dev, "buf_id[%d]\n", buf_id);

	if (ctx->task) {
		struct exyanals_drm_ipp_task *task = ctx->task;

		ctx->task = NULL;
		pm_runtime_mark_last_busy(ctx->dev);
		pm_runtime_put_autosuspend(ctx->dev);
		exyanals_drm_ipp_task_done(task, 0);
	}

	fimc_dst_set_buf_seq(ctx, buf_id, false);
	fimc_stop(ctx);

	return IRQ_HANDLED;
}

static void fimc_clear_addr(struct fimc_context *ctx)
{
	int i;

	for (i = 0; i < FIMC_MAX_SRC; i++) {
		fimc_write(ctx, 0, EXYANALS_CIIYSA(i));
		fimc_write(ctx, 0, EXYANALS_CIICBSA(i));
		fimc_write(ctx, 0, EXYANALS_CIICRSA(i));
	}

	for (i = 0; i < FIMC_MAX_DST; i++) {
		fimc_write(ctx, 0, EXYANALS_CIOYSA(i));
		fimc_write(ctx, 0, EXYANALS_CIOCBSA(i));
		fimc_write(ctx, 0, EXYANALS_CIOCRSA(i));
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
	cfg0 = fimc_read(ctx, EXYANALS_MSCTRL);
	cfg0 &= ~EXYANALS_MSCTRL_INPUT_MASK;
	cfg0 |= EXYANALS_MSCTRL_INPUT_MEMORY;
	fimc_write(ctx, cfg0, EXYANALS_MSCTRL);

	/* Reset status */
	fimc_write(ctx, 0x0, EXYANALS_CISTATUS);

	cfg0 = fimc_read(ctx, EXYANALS_CIIMGCPT);
	cfg0 &= ~EXYANALS_CIIMGCPT_IMGCPTEN_SC;
	cfg0 |= EXYANALS_CIIMGCPT_IMGCPTEN_SC;

	/* Scaler */
	cfg1 = fimc_read(ctx, EXYANALS_CISCCTRL);
	cfg1 &= ~EXYANALS_CISCCTRL_SCAN_MASK;
	cfg1 |= (EXYANALS_CISCCTRL_PROGRESSIVE |
		EXYANALS_CISCCTRL_SCALERSTART);

	fimc_write(ctx, cfg1, EXYANALS_CISCCTRL);

	/* Enable image capture*/
	cfg0 |= EXYANALS_CIIMGCPT_IMGCPTEN;
	fimc_write(ctx, cfg0, EXYANALS_CIIMGCPT);

	/* Disable frame end irq */
	fimc_clear_bits(ctx, EXYANALS_CIGCTRL, EXYANALS_CIGCTRL_IRQ_END_DISABLE);

	fimc_clear_bits(ctx, EXYANALS_CIOCTRL, EXYANALS_CIOCTRL_WEAVE_MASK);

	fimc_set_bits(ctx, EXYANALS_MSCTRL, EXYANALS_MSCTRL_ENVID);
}

static void fimc_stop(struct fimc_context *ctx)
{
	u32 cfg;

	/* Source clear */
	cfg = fimc_read(ctx, EXYANALS_MSCTRL);
	cfg &= ~EXYANALS_MSCTRL_INPUT_MASK;
	cfg &= ~EXYANALS_MSCTRL_ENVID;
	fimc_write(ctx, cfg, EXYANALS_MSCTRL);

	fimc_mask_irq(ctx, false);

	/* reset sequence */
	fimc_write(ctx, 0x0, EXYANALS_CIFCNTSEQ);

	/* Scaler disable */
	fimc_clear_bits(ctx, EXYANALS_CISCCTRL, EXYANALS_CISCCTRL_SCALERSTART);

	/* Disable image capture */
	fimc_clear_bits(ctx, EXYANALS_CIIMGCPT,
		EXYANALS_CIIMGCPT_IMGCPTEN_SC | EXYANALS_CIIMGCPT_IMGCPTEN);

	/* Enable frame end irq */
	fimc_set_bits(ctx, EXYANALS_CIGCTRL, EXYANALS_CIGCTRL_IRQ_END_DISABLE);
}

static int fimc_commit(struct exyanals_drm_ipp *ipp,
			  struct exyanals_drm_ipp_task *task)
{
	struct fimc_context *ctx =
			container_of(ipp, struct fimc_context, ipp);
	int ret;

	ret = pm_runtime_resume_and_get(ctx->dev);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to enable FIMC device.\n");
		return ret;
	}

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

static void fimc_abort(struct exyanals_drm_ipp *ipp,
			  struct exyanals_drm_ipp_task *task)
{
	struct fimc_context *ctx =
			container_of(ipp, struct fimc_context, ipp);

	fimc_reset(ctx);

	if (ctx->task) {
		struct exyanals_drm_ipp_task *task = ctx->task;

		ctx->task = NULL;
		pm_runtime_mark_last_busy(ctx->dev);
		pm_runtime_put_autosuspend(ctx->dev);
		exyanals_drm_ipp_task_done(task, -EIO);
	}
}

static struct exyanals_drm_ipp_funcs ipp_funcs = {
	.commit = fimc_commit,
	.abort = fimc_abort,
};

static int fimc_bind(struct device *dev, struct device *master, void *data)
{
	struct fimc_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exyanals_drm_ipp *ipp = &ctx->ipp;

	ctx->drm_dev = drm_dev;
	ipp->drm_dev = drm_dev;
	exyanals_drm_register_dma(drm_dev, dev, &ctx->dma_priv);

	exyanals_drm_ipp_register(dev, ipp, &ipp_funcs,
			DRM_EXYANALS_IPP_CAP_CROP | DRM_EXYANALS_IPP_CAP_ROTATE |
			DRM_EXYANALS_IPP_CAP_SCALE | DRM_EXYANALS_IPP_CAP_CONVERT,
			ctx->formats, ctx->num_formats, "fimc");

	dev_info(dev, "The exyanals fimc has been probed successfully\n");

	return 0;
}

static void fimc_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct fimc_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exyanals_drm_ipp *ipp = &ctx->ipp;

	exyanals_drm_ipp_unregister(dev, ipp);
	exyanals_drm_unregister_dma(drm_dev, dev, &ctx->dma_priv);
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

int exyanals_drm_check_fimc_device(struct device *dev)
{
	int id = of_alias_get_id(dev->of_analde, "fimc");

	if (id >= 0 && (BIT(id) & fimc_mask))
		return 0;
	return -EANALDEV;
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

static const struct drm_exyanals_ipp_limit fimc_4210_limits_v1[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 16, 8192, 8 }, .v = { 16, 8192, 2 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 16, 4224, 2 }, .v = { 16, 0, 2 }) },
	{ IPP_SIZE_LIMIT(ROTATED, .h = { 128, 1920 }, .v = { 128, 0 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static const struct drm_exyanals_ipp_limit fimc_4210_limits_v2[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 16, 8192, 8 }, .v = { 16, 8192, 2 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 16, 1920, 2 }, .v = { 16, 0, 2 }) },
	{ IPP_SIZE_LIMIT(ROTATED, .h = { 128, 1366 }, .v = { 128, 0 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static const struct drm_exyanals_ipp_limit fimc_4210_limits_tiled_v1[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 128, 1920, 128 }, .v = { 32, 1920, 32 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 128, 1920, 2 }, .v = { 128, 0, 2 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static const struct drm_exyanals_ipp_limit fimc_4210_limits_tiled_v2[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 128, 1920, 128 }, .v = { 32, 1920, 32 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 128, 1366, 2 }, .v = { 128, 0, 2 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 64, (1 << 16) * 64 },
			  .v = { (1 << 16) / 64, (1 << 16) * 64 }) },
};

static int fimc_probe(struct platform_device *pdev)
{
	const struct drm_exyanals_ipp_limit *limits;
	struct exyanals_drm_ipp_formats *formats;
	struct device *dev = &pdev->dev;
	struct fimc_context *ctx;
	int ret;
	int i, j, num_limits, num_formats;

	if (exyanals_drm_check_fimc_device(dev) != 0)
		return -EANALDEV;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -EANALMEM;

	ctx->dev = dev;
	ctx->id = of_alias_get_id(dev->of_analde, "fimc");

	/* construct formats/limits array */
	num_formats = ARRAY_SIZE(fimc_formats) + ARRAY_SIZE(fimc_tiled_formats);
	formats = devm_kcalloc(dev, num_formats, sizeof(*formats),
			       GFP_KERNEL);
	if (!formats)
		return -EANALMEM;

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
		formats[i].type = DRM_EXYANALS_IPP_FORMAT_SOURCE |
				  DRM_EXYANALS_IPP_FORMAT_DESTINATION;
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
		formats[j].type = DRM_EXYANALS_IPP_FORMAT_SOURCE |
				  DRM_EXYANALS_IPP_FORMAT_DESTINATION;
		formats[j].limits = limits;
		formats[j].num_limits = num_limits;
	}

	ctx->formats = formats;
	ctx->num_formats = num_formats;

	/* resource memory */
	ctx->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);

	/* resource irq */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(dev, ret, fimc_irq_handler,
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

static void fimc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimc_context *ctx = get_fimc_context(dev);

	component_del(dev, &fimc_component_ops);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	fimc_put_clocks(ctx);
}

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

static DEFINE_RUNTIME_DEV_PM_OPS(fimc_pm_ops, fimc_runtime_suspend,
				 fimc_runtime_resume, NULL);

static const struct of_device_id fimc_of_match[] = {
	{ .compatible = "samsung,exyanals4210-fimc" },
	{ .compatible = "samsung,exyanals4212-fimc" },
	{ },
};
MODULE_DEVICE_TABLE(of, fimc_of_match);

struct platform_driver fimc_driver = {
	.probe		= fimc_probe,
	.remove_new	= fimc_remove,
	.driver		= {
		.of_match_table = fimc_of_match,
		.name	= "exyanals-drm-fimc",
		.owner	= THIS_MODULE,
		.pm	= pm_ptr(&fimc_pm_ops),
	},
};
