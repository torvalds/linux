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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_ipp.h"
#include "regs-gsc.h"

/*
 * GSC stands for General SCaler and
 * supports image scaler/rotator and input/output DMA operations.
 * input DMA reads image data from the memory.
 * output DMA writes image data to memory.
 * GSC supports image rotation and image effect functions.
 */


#define GSC_MAX_CLOCKS	8
#define GSC_MAX_SRC		4
#define GSC_MAX_DST		16
#define GSC_RESET_TIMEOUT	50
#define GSC_BUF_STOP	1
#define GSC_BUF_START	2
#define GSC_REG_SZ		16
#define GSC_WIDTH_ITU_709	1280
#define GSC_SC_UP_MAX_RATIO		65536
#define GSC_SC_DOWN_RATIO_7_8		74898
#define GSC_SC_DOWN_RATIO_6_8		87381
#define GSC_SC_DOWN_RATIO_5_8		104857
#define GSC_SC_DOWN_RATIO_4_8		131072
#define GSC_SC_DOWN_RATIO_3_8		174762
#define GSC_SC_DOWN_RATIO_2_8		262144
#define GSC_CROP_MAX	8192
#define GSC_CROP_MIN	32
#define GSC_SCALE_MAX	4224
#define GSC_SCALE_MIN	32
#define GSC_COEF_RATIO	7
#define GSC_COEF_PHASE	9
#define GSC_COEF_ATTR	16
#define GSC_COEF_H_8T	8
#define GSC_COEF_V_4T	4
#define GSC_COEF_DEPTH	3
#define GSC_AUTOSUSPEND_DELAY		2000

#define get_gsc_context(dev)	dev_get_drvdata(dev)
#define gsc_read(offset)		readl(ctx->regs + (offset))
#define gsc_write(cfg, offset)	writel(cfg, ctx->regs + (offset))

/*
 * A structure of scaler.
 *
 * @range: narrow, wide.
 * @pre_shfactor: pre sclaer shift factor.
 * @pre_hratio: horizontal ratio of the prescaler.
 * @pre_vratio: vertical ratio of the prescaler.
 * @main_hratio: the main scaler's horizontal ratio.
 * @main_vratio: the main scaler's vertical ratio.
 */
struct gsc_scaler {
	bool	range;
	u32	pre_shfactor;
	u32	pre_hratio;
	u32	pre_vratio;
	unsigned long main_hratio;
	unsigned long main_vratio;
};

/*
 * A structure of gsc context.
 *
 * @regs_res: register resources.
 * @regs: memory mapped io registers.
 * @gsc_clk: gsc gate clock.
 * @sc: scaler infomations.
 * @id: gsc id.
 * @irq: irq number.
 * @rotation: supports rotation of src.
 */
struct gsc_context {
	struct exynos_drm_ipp ipp;
	struct drm_device *drm_dev;
	void		*dma_priv;
	struct device	*dev;
	struct exynos_drm_ipp_task	*task;
	struct exynos_drm_ipp_formats	*formats;
	unsigned int			num_formats;

	struct resource	*regs_res;
	void __iomem	*regs;
	const char	**clk_names;
	struct clk	*clocks[GSC_MAX_CLOCKS];
	int		num_clocks;
	struct gsc_scaler	sc;
	int	id;
	int	irq;
	bool	rotation;
};

/**
 * struct gsc_driverdata - per device type driver data for init time.
 *
 * @limits: picture size limits array
 * @num_limits: number of items in the aforementioned array
 * @clk_names: names of clocks needed by this variant
 * @num_clocks: the number of clocks needed by this variant
 */
struct gsc_driverdata {
	const struct drm_exynos_ipp_limit *limits;
	int		num_limits;
	const char	*clk_names[GSC_MAX_CLOCKS];
	int		num_clocks;
};

/* 8-tap Filter Coefficient */
static const int h_coef_8t[GSC_COEF_RATIO][GSC_COEF_ATTR][GSC_COEF_H_8T] = {
	{	/* Ratio <= 65536 (~8:8) */
		{  0,  0,   0, 128,   0,   0,  0,  0 },
		{ -1,  2,  -6, 127,   7,  -2,  1,  0 },
		{ -1,  4, -12, 125,  16,  -5,  1,  0 },
		{ -1,  5, -15, 120,  25,  -8,  2,  0 },
		{ -1,  6, -18, 114,  35, -10,  3, -1 },
		{ -1,  6, -20, 107,  46, -13,  4, -1 },
		{ -2,  7, -21,  99,  57, -16,  5, -1 },
		{ -1,  6, -20,  89,  68, -18,  5, -1 },
		{ -1,  6, -20,  79,  79, -20,  6, -1 },
		{ -1,  5, -18,  68,  89, -20,  6, -1 },
		{ -1,  5, -16,  57,  99, -21,  7, -2 },
		{ -1,  4, -13,  46, 107, -20,  6, -1 },
		{ -1,  3, -10,  35, 114, -18,  6, -1 },
		{  0,  2,  -8,  25, 120, -15,  5, -1 },
		{  0,  1,  -5,  16, 125, -12,  4, -1 },
		{  0,  1,  -2,   7, 127,  -6,  2, -1 }
	}, {	/* 65536 < Ratio <= 74898 (~8:7) */
		{  3, -8,  14, 111,  13,  -8,  3,  0 },
		{  2, -6,   7, 112,  21, -10,  3, -1 },
		{  2, -4,   1, 110,  28, -12,  4, -1 },
		{  1, -2,  -3, 106,  36, -13,  4, -1 },
		{  1, -1,  -7, 103,  44, -15,  4, -1 },
		{  1,  1, -11,  97,  53, -16,  4, -1 },
		{  0,  2, -13,  91,  61, -16,  4, -1 },
		{  0,  3, -15,  85,  69, -17,  4, -1 },
		{  0,  3, -16,  77,  77, -16,  3,  0 },
		{ -1,  4, -17,  69,  85, -15,  3,  0 },
		{ -1,  4, -16,  61,  91, -13,  2,  0 },
		{ -1,  4, -16,  53,  97, -11,  1,  1 },
		{ -1,  4, -15,  44, 103,  -7, -1,  1 },
		{ -1,  4, -13,  36, 106,  -3, -2,  1 },
		{ -1,  4, -12,  28, 110,   1, -4,  2 },
		{ -1,  3, -10,  21, 112,   7, -6,  2 }
	}, {	/* 74898 < Ratio <= 87381 (~8:6) */
		{ 2, -11,  25,  96, 25, -11,   2,  0 },
		{ 2, -10,  19,  96, 31, -12,   2,  0 },
		{ 2,  -9,  14,  94, 37, -12,   2,  0 },
		{ 2,  -8,  10,  92, 43, -12,   1,  0 },
		{ 2,  -7,   5,  90, 49, -12,   1,  0 },
		{ 2,  -5,   1,  86, 55, -12,   0,  1 },
		{ 2,  -4,  -2,  82, 61, -11,  -1,  1 },
		{ 1,  -3,  -5,  77, 67,  -9,  -1,  1 },
		{ 1,  -2,  -7,  72, 72,  -7,  -2,  1 },
		{ 1,  -1,  -9,  67, 77,  -5,  -3,  1 },
		{ 1,  -1, -11,  61, 82,  -2,  -4,  2 },
		{ 1,   0, -12,  55, 86,   1,  -5,  2 },
		{ 0,   1, -12,  49, 90,   5,  -7,  2 },
		{ 0,   1, -12,  43, 92,  10,  -8,  2 },
		{ 0,   2, -12,  37, 94,  14,  -9,  2 },
		{ 0,   2, -12,  31, 96,  19, -10,  2 }
	}, {	/* 87381 < Ratio <= 104857 (~8:5) */
		{ -1,  -8, 33,  80, 33,  -8,  -1,  0 },
		{ -1,  -8, 28,  80, 37,  -7,  -2,  1 },
		{  0,  -8, 24,  79, 41,  -7,  -2,  1 },
		{  0,  -8, 20,  78, 46,  -6,  -3,  1 },
		{  0,  -8, 16,  76, 50,  -4,  -3,  1 },
		{  0,  -7, 13,  74, 54,  -3,  -4,  1 },
		{  1,  -7, 10,  71, 58,  -1,  -5,  1 },
		{  1,  -6,  6,  68, 62,   1,  -5,  1 },
		{  1,  -6,  4,  65, 65,   4,  -6,  1 },
		{  1,  -5,  1,  62, 68,   6,  -6,  1 },
		{  1,  -5, -1,  58, 71,  10,  -7,  1 },
		{  1,  -4, -3,  54, 74,  13,  -7,  0 },
		{  1,  -3, -4,  50, 76,  16,  -8,  0 },
		{  1,  -3, -6,  46, 78,  20,  -8,  0 },
		{  1,  -2, -7,  41, 79,  24,  -8,  0 },
		{  1,  -2, -7,  37, 80,  28,  -8, -1 }
	}, {	/* 104857 < Ratio <= 131072 (~8:4) */
		{ -3,   0, 35,  64, 35,   0,  -3,  0 },
		{ -3,  -1, 32,  64, 38,   1,  -3,  0 },
		{ -2,  -2, 29,  63, 41,   2,  -3,  0 },
		{ -2,  -3, 27,  63, 43,   4,  -4,  0 },
		{ -2,  -3, 24,  61, 46,   6,  -4,  0 },
		{ -2,  -3, 21,  60, 49,   7,  -4,  0 },
		{ -1,  -4, 19,  59, 51,   9,  -4, -1 },
		{ -1,  -4, 16,  57, 53,  12,  -4, -1 },
		{ -1,  -4, 14,  55, 55,  14,  -4, -1 },
		{ -1,  -4, 12,  53, 57,  16,  -4, -1 },
		{ -1,  -4,  9,  51, 59,  19,  -4, -1 },
		{  0,  -4,  7,  49, 60,  21,  -3, -2 },
		{  0,  -4,  6,  46, 61,  24,  -3, -2 },
		{  0,  -4,  4,  43, 63,  27,  -3, -2 },
		{  0,  -3,  2,  41, 63,  29,  -2, -2 },
		{  0,  -3,  1,  38, 64,  32,  -1, -3 }
	}, {	/* 131072 < Ratio <= 174762 (~8:3) */
		{ -1,   8, 33,  48, 33,   8,  -1,  0 },
		{ -1,   7, 31,  49, 35,   9,  -1, -1 },
		{ -1,   6, 30,  49, 36,  10,  -1, -1 },
		{ -1,   5, 28,  48, 38,  12,  -1, -1 },
		{ -1,   4, 26,  48, 39,  13,   0, -1 },
		{ -1,   3, 24,  47, 41,  15,   0, -1 },
		{ -1,   2, 23,  47, 42,  16,   0, -1 },
		{ -1,   2, 21,  45, 43,  18,   1, -1 },
		{ -1,   1, 19,  45, 45,  19,   1, -1 },
		{ -1,   1, 18,  43, 45,  21,   2, -1 },
		{ -1,   0, 16,  42, 47,  23,   2, -1 },
		{ -1,   0, 15,  41, 47,  24,   3, -1 },
		{ -1,   0, 13,  39, 48,  26,   4, -1 },
		{ -1,  -1, 12,  38, 48,  28,   5, -1 },
		{ -1,  -1, 10,  36, 49,  30,   6, -1 },
		{ -1,  -1,  9,  35, 49,  31,   7, -1 }
	}, {	/* 174762 < Ratio <= 262144 (~8:2) */
		{  2,  13, 30,  38, 30,  13,   2,  0 },
		{  2,  12, 29,  38, 30,  14,   3,  0 },
		{  2,  11, 28,  38, 31,  15,   3,  0 },
		{  2,  10, 26,  38, 32,  16,   4,  0 },
		{  1,  10, 26,  37, 33,  17,   4,  0 },
		{  1,   9, 24,  37, 34,  18,   5,  0 },
		{  1,   8, 24,  37, 34,  19,   5,  0 },
		{  1,   7, 22,  36, 35,  20,   6,  1 },
		{  1,   6, 21,  36, 36,  21,   6,  1 },
		{  1,   6, 20,  35, 36,  22,   7,  1 },
		{  0,   5, 19,  34, 37,  24,   8,  1 },
		{  0,   5, 18,  34, 37,  24,   9,  1 },
		{  0,   4, 17,  33, 37,  26,  10,  1 },
		{  0,   4, 16,  32, 38,  26,  10,  2 },
		{  0,   3, 15,  31, 38,  28,  11,  2 },
		{  0,   3, 14,  30, 38,  29,  12,  2 }
	}
};

/* 4-tap Filter Coefficient */
static const int v_coef_4t[GSC_COEF_RATIO][GSC_COEF_ATTR][GSC_COEF_V_4T] = {
	{	/* Ratio <= 65536 (~8:8) */
		{  0, 128,   0,  0 },
		{ -4, 127,   5,  0 },
		{ -6, 124,  11, -1 },
		{ -8, 118,  19, -1 },
		{ -8, 111,  27, -2 },
		{ -8, 102,  37, -3 },
		{ -8,  92,  48, -4 },
		{ -7,  81,  59, -5 },
		{ -6,  70,  70, -6 },
		{ -5,  59,  81, -7 },
		{ -4,  48,  92, -8 },
		{ -3,  37, 102, -8 },
		{ -2,  27, 111, -8 },
		{ -1,  19, 118, -8 },
		{ -1,  11, 124, -6 },
		{  0,   5, 127, -4 }
	}, {	/* 65536 < Ratio <= 74898 (~8:7) */
		{  8, 112,   8,  0 },
		{  4, 111,  14, -1 },
		{  1, 109,  20, -2 },
		{ -2, 105,  27, -2 },
		{ -3, 100,  34, -3 },
		{ -5,  93,  43, -3 },
		{ -5,  86,  51, -4 },
		{ -5,  77,  60, -4 },
		{ -5,  69,  69, -5 },
		{ -4,  60,  77, -5 },
		{ -4,  51,  86, -5 },
		{ -3,  43,  93, -5 },
		{ -3,  34, 100, -3 },
		{ -2,  27, 105, -2 },
		{ -2,  20, 109,  1 },
		{ -1,  14, 111,  4 }
	}, {	/* 74898 < Ratio <= 87381 (~8:6) */
		{ 16,  96,  16,  0 },
		{ 12,  97,  21, -2 },
		{  8,  96,  26, -2 },
		{  5,  93,  32, -2 },
		{  2,  89,  39, -2 },
		{  0,  84,  46, -2 },
		{ -1,  79,  53, -3 },
		{ -2,  73,  59, -2 },
		{ -2,  66,  66, -2 },
		{ -2,  59,  73, -2 },
		{ -3,  53,  79, -1 },
		{ -2,  46,  84,  0 },
		{ -2,  39,  89,  2 },
		{ -2,  32,  93,  5 },
		{ -2,  26,  96,  8 },
		{ -2,  21,  97, 12 }
	}, {	/* 87381 < Ratio <= 104857 (~8:5) */
		{ 22,  84,  22,  0 },
		{ 18,  85,  26, -1 },
		{ 14,  84,  31, -1 },
		{ 11,  82,  36, -1 },
		{  8,  79,  42, -1 },
		{  6,  76,  47, -1 },
		{  4,  72,  52,  0 },
		{  2,  68,  58,  0 },
		{  1,  63,  63,  1 },
		{  0,  58,  68,  2 },
		{  0,  52,  72,  4 },
		{ -1,  47,  76,  6 },
		{ -1,  42,  79,  8 },
		{ -1,  36,  82, 11 },
		{ -1,  31,  84, 14 },
		{ -1,  26,  85, 18 }
	}, {	/* 104857 < Ratio <= 131072 (~8:4) */
		{ 26,  76,  26,  0 },
		{ 22,  76,  30,  0 },
		{ 19,  75,  34,  0 },
		{ 16,  73,  38,  1 },
		{ 13,  71,  43,  1 },
		{ 10,  69,  47,  2 },
		{  8,  66,  51,  3 },
		{  6,  63,  55,  4 },
		{  5,  59,  59,  5 },
		{  4,  55,  63,  6 },
		{  3,  51,  66,  8 },
		{  2,  47,  69, 10 },
		{  1,  43,  71, 13 },
		{  1,  38,  73, 16 },
		{  0,  34,  75, 19 },
		{  0,  30,  76, 22 }
	}, {	/* 131072 < Ratio <= 174762 (~8:3) */
		{ 29,  70,  29,  0 },
		{ 26,  68,  32,  2 },
		{ 23,  67,  36,  2 },
		{ 20,  66,  39,  3 },
		{ 17,  65,  43,  3 },
		{ 15,  63,  46,  4 },
		{ 12,  61,  50,  5 },
		{ 10,  58,  53,  7 },
		{  8,  56,  56,  8 },
		{  7,  53,  58, 10 },
		{  5,  50,  61, 12 },
		{  4,  46,  63, 15 },
		{  3,  43,  65, 17 },
		{  3,  39,  66, 20 },
		{  2,  36,  67, 23 },
		{  2,  32,  68, 26 }
	}, {	/* 174762 < Ratio <= 262144 (~8:2) */
		{ 32,  64,  32,  0 },
		{ 28,  63,  34,  3 },
		{ 25,  62,  37,  4 },
		{ 22,  62,  40,  4 },
		{ 19,  61,  43,  5 },
		{ 17,  59,  46,  6 },
		{ 15,  58,  48,  7 },
		{ 13,  55,  51,  9 },
		{ 11,  53,  53, 11 },
		{  9,  51,  55, 13 },
		{  7,  48,  58, 15 },
		{  6,  46,  59, 17 },
		{  5,  43,  61, 19 },
		{  4,  40,  62, 22 },
		{  4,  37,  62, 25 },
		{  3,  34,  63, 28 }
	}
};

static int gsc_sw_reset(struct gsc_context *ctx)
{
	u32 cfg;
	int count = GSC_RESET_TIMEOUT;

	/* s/w reset */
	cfg = (GSC_SW_RESET_SRESET);
	gsc_write(cfg, GSC_SW_RESET);

	/* wait s/w reset complete */
	while (count--) {
		cfg = gsc_read(GSC_SW_RESET);
		if (!cfg)
			break;
		usleep_range(1000, 2000);
	}

	if (cfg) {
		DRM_DEV_ERROR(ctx->dev, "failed to reset gsc h/w.\n");
		return -EBUSY;
	}

	/* reset sequence */
	cfg = gsc_read(GSC_IN_BASE_ADDR_Y_MASK);
	cfg |= (GSC_IN_BASE_ADDR_MASK |
		GSC_IN_BASE_ADDR_PINGPONG(0));
	gsc_write(cfg, GSC_IN_BASE_ADDR_Y_MASK);
	gsc_write(cfg, GSC_IN_BASE_ADDR_CB_MASK);
	gsc_write(cfg, GSC_IN_BASE_ADDR_CR_MASK);

	cfg = gsc_read(GSC_OUT_BASE_ADDR_Y_MASK);
	cfg |= (GSC_OUT_BASE_ADDR_MASK |
		GSC_OUT_BASE_ADDR_PINGPONG(0));
	gsc_write(cfg, GSC_OUT_BASE_ADDR_Y_MASK);
	gsc_write(cfg, GSC_OUT_BASE_ADDR_CB_MASK);
	gsc_write(cfg, GSC_OUT_BASE_ADDR_CR_MASK);

	return 0;
}

static void gsc_handle_irq(struct gsc_context *ctx, bool enable,
		bool overflow, bool done)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "enable[%d]overflow[%d]level[%d]\n",
			  enable, overflow, done);

	cfg = gsc_read(GSC_IRQ);
	cfg |= (GSC_IRQ_OR_MASK | GSC_IRQ_FRMDONE_MASK);

	if (enable)
		cfg |= GSC_IRQ_ENABLE;
	else
		cfg &= ~GSC_IRQ_ENABLE;

	if (overflow)
		cfg &= ~GSC_IRQ_OR_MASK;
	else
		cfg |= GSC_IRQ_OR_MASK;

	if (done)
		cfg &= ~GSC_IRQ_FRMDONE_MASK;
	else
		cfg |= GSC_IRQ_FRMDONE_MASK;

	gsc_write(cfg, GSC_IRQ);
}


static void gsc_src_set_fmt(struct gsc_context *ctx, u32 fmt, bool tiled)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	cfg = gsc_read(GSC_IN_CON);
	cfg &= ~(GSC_IN_RGB_TYPE_MASK | GSC_IN_YUV422_1P_ORDER_MASK |
		 GSC_IN_CHROMA_ORDER_MASK | GSC_IN_FORMAT_MASK |
		 GSC_IN_TILE_TYPE_MASK | GSC_IN_TILE_MODE |
		 GSC_IN_CHROM_STRIDE_SEL_MASK | GSC_IN_RB_SWAP_MASK);

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= GSC_IN_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		cfg |= GSC_IN_XRGB8888;
		break;
	case DRM_FORMAT_BGRX8888:
		cfg |= (GSC_IN_XRGB8888 | GSC_IN_RB_SWAP);
		break;
	case DRM_FORMAT_YUYV:
		cfg |= (GSC_IN_YUV422_1P |
			GSC_IN_YUV422_1P_ORDER_LSB_Y |
			GSC_IN_CHROMA_ORDER_CBCR);
		break;
	case DRM_FORMAT_YVYU:
		cfg |= (GSC_IN_YUV422_1P |
			GSC_IN_YUV422_1P_ORDER_LSB_Y |
			GSC_IN_CHROMA_ORDER_CRCB);
		break;
	case DRM_FORMAT_UYVY:
		cfg |= (GSC_IN_YUV422_1P |
			GSC_IN_YUV422_1P_OEDER_LSB_C |
			GSC_IN_CHROMA_ORDER_CBCR);
		break;
	case DRM_FORMAT_VYUY:
		cfg |= (GSC_IN_YUV422_1P |
			GSC_IN_YUV422_1P_OEDER_LSB_C |
			GSC_IN_CHROMA_ORDER_CRCB);
		break;
	case DRM_FORMAT_NV21:
		cfg |= (GSC_IN_CHROMA_ORDER_CRCB | GSC_IN_YUV420_2P);
		break;
	case DRM_FORMAT_NV61:
		cfg |= (GSC_IN_CHROMA_ORDER_CRCB | GSC_IN_YUV422_2P);
		break;
	case DRM_FORMAT_YUV422:
		cfg |= GSC_IN_YUV422_3P;
		break;
	case DRM_FORMAT_YUV420:
		cfg |= (GSC_IN_CHROMA_ORDER_CBCR | GSC_IN_YUV420_3P);
		break;
	case DRM_FORMAT_YVU420:
		cfg |= (GSC_IN_CHROMA_ORDER_CRCB | GSC_IN_YUV420_3P);
		break;
	case DRM_FORMAT_NV12:
		cfg |= (GSC_IN_CHROMA_ORDER_CBCR | GSC_IN_YUV420_2P);
		break;
	case DRM_FORMAT_NV16:
		cfg |= (GSC_IN_CHROMA_ORDER_CBCR | GSC_IN_YUV422_2P);
		break;
	}

	if (tiled)
		cfg |= (GSC_IN_TILE_C_16x8 | GSC_IN_TILE_MODE);

	gsc_write(cfg, GSC_IN_CON);
}

static void gsc_src_set_transf(struct gsc_context *ctx, unsigned int rotation)
{
	unsigned int degree = rotation & DRM_MODE_ROTATE_MASK;
	u32 cfg;

	cfg = gsc_read(GSC_IN_CON);
	cfg &= ~GSC_IN_ROT_MASK;

	switch (degree) {
	case DRM_MODE_ROTATE_0:
		if (rotation & DRM_MODE_REFLECT_X)
			cfg |= GSC_IN_ROT_XFLIP;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg |= GSC_IN_ROT_YFLIP;
		break;
	case DRM_MODE_ROTATE_90:
		cfg |= GSC_IN_ROT_90;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg |= GSC_IN_ROT_XFLIP;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg |= GSC_IN_ROT_YFLIP;
		break;
	case DRM_MODE_ROTATE_180:
		cfg |= GSC_IN_ROT_180;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg &= ~GSC_IN_ROT_XFLIP;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg &= ~GSC_IN_ROT_YFLIP;
		break;
	case DRM_MODE_ROTATE_270:
		cfg |= GSC_IN_ROT_270;
		if (rotation & DRM_MODE_REFLECT_X)
			cfg &= ~GSC_IN_ROT_XFLIP;
		if (rotation & DRM_MODE_REFLECT_Y)
			cfg &= ~GSC_IN_ROT_YFLIP;
		break;
	}

	gsc_write(cfg, GSC_IN_CON);

	ctx->rotation = (cfg & GSC_IN_ROT_90) ? 1 : 0;
}

static void gsc_src_set_size(struct gsc_context *ctx,
			     struct exynos_drm_ipp_buffer *buf)
{
	struct gsc_scaler *sc = &ctx->sc;
	u32 cfg;

	/* pixel offset */
	cfg = (GSC_SRCIMG_OFFSET_X(buf->rect.x) |
		GSC_SRCIMG_OFFSET_Y(buf->rect.y));
	gsc_write(cfg, GSC_SRCIMG_OFFSET);

	/* cropped size */
	cfg = (GSC_CROPPED_WIDTH(buf->rect.w) |
		GSC_CROPPED_HEIGHT(buf->rect.h));
	gsc_write(cfg, GSC_CROPPED_SIZE);

	/* original size */
	cfg = gsc_read(GSC_SRCIMG_SIZE);
	cfg &= ~(GSC_SRCIMG_HEIGHT_MASK |
		GSC_SRCIMG_WIDTH_MASK);

	cfg |= (GSC_SRCIMG_WIDTH(buf->buf.pitch[0] / buf->format->cpp[0]) |
		GSC_SRCIMG_HEIGHT(buf->buf.height));

	gsc_write(cfg, GSC_SRCIMG_SIZE);

	cfg = gsc_read(GSC_IN_CON);
	cfg &= ~GSC_IN_RGB_TYPE_MASK;

	if (buf->rect.w >= GSC_WIDTH_ITU_709)
		if (sc->range)
			cfg |= GSC_IN_RGB_HD_WIDE;
		else
			cfg |= GSC_IN_RGB_HD_NARROW;
	else
		if (sc->range)
			cfg |= GSC_IN_RGB_SD_WIDE;
		else
			cfg |= GSC_IN_RGB_SD_NARROW;

	gsc_write(cfg, GSC_IN_CON);
}

static void gsc_src_set_buf_seq(struct gsc_context *ctx, u32 buf_id,
			       bool enqueue)
{
	bool masked = !enqueue;
	u32 cfg;
	u32 mask = 0x00000001 << buf_id;

	/* mask register set */
	cfg = gsc_read(GSC_IN_BASE_ADDR_Y_MASK);

	/* sequence id */
	cfg &= ~mask;
	cfg |= masked << buf_id;
	gsc_write(cfg, GSC_IN_BASE_ADDR_Y_MASK);
	gsc_write(cfg, GSC_IN_BASE_ADDR_CB_MASK);
	gsc_write(cfg, GSC_IN_BASE_ADDR_CR_MASK);
}

static void gsc_src_set_addr(struct gsc_context *ctx, u32 buf_id,
			    struct exynos_drm_ipp_buffer *buf)
{
	/* address register set */
	gsc_write(buf->dma_addr[0], GSC_IN_BASE_ADDR_Y(buf_id));
	gsc_write(buf->dma_addr[1], GSC_IN_BASE_ADDR_CB(buf_id));
	gsc_write(buf->dma_addr[2], GSC_IN_BASE_ADDR_CR(buf_id));

	gsc_src_set_buf_seq(ctx, buf_id, true);
}

static void gsc_dst_set_fmt(struct gsc_context *ctx, u32 fmt, bool tiled)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "fmt[0x%x]\n", fmt);

	cfg = gsc_read(GSC_OUT_CON);
	cfg &= ~(GSC_OUT_RGB_TYPE_MASK | GSC_OUT_YUV422_1P_ORDER_MASK |
		 GSC_OUT_CHROMA_ORDER_MASK | GSC_OUT_FORMAT_MASK |
		 GSC_OUT_CHROM_STRIDE_SEL_MASK | GSC_OUT_RB_SWAP_MASK |
		 GSC_OUT_GLOBAL_ALPHA_MASK);

	switch (fmt) {
	case DRM_FORMAT_RGB565:
		cfg |= GSC_OUT_RGB565;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		cfg |= (GSC_OUT_XRGB8888 | GSC_OUT_GLOBAL_ALPHA(0xff));
		break;
	case DRM_FORMAT_BGRX8888:
		cfg |= (GSC_OUT_XRGB8888 | GSC_OUT_RB_SWAP);
		break;
	case DRM_FORMAT_YUYV:
		cfg |= (GSC_OUT_YUV422_1P |
			GSC_OUT_YUV422_1P_ORDER_LSB_Y |
			GSC_OUT_CHROMA_ORDER_CBCR);
		break;
	case DRM_FORMAT_YVYU:
		cfg |= (GSC_OUT_YUV422_1P |
			GSC_OUT_YUV422_1P_ORDER_LSB_Y |
			GSC_OUT_CHROMA_ORDER_CRCB);
		break;
	case DRM_FORMAT_UYVY:
		cfg |= (GSC_OUT_YUV422_1P |
			GSC_OUT_YUV422_1P_OEDER_LSB_C |
			GSC_OUT_CHROMA_ORDER_CBCR);
		break;
	case DRM_FORMAT_VYUY:
		cfg |= (GSC_OUT_YUV422_1P |
			GSC_OUT_YUV422_1P_OEDER_LSB_C |
			GSC_OUT_CHROMA_ORDER_CRCB);
		break;
	case DRM_FORMAT_NV21:
		cfg |= (GSC_OUT_CHROMA_ORDER_CRCB | GSC_OUT_YUV420_2P);
		break;
	case DRM_FORMAT_NV61:
		cfg |= (GSC_OUT_CHROMA_ORDER_CRCB | GSC_OUT_YUV422_2P);
		break;
	case DRM_FORMAT_YUV422:
		cfg |= GSC_OUT_YUV422_3P;
		break;
	case DRM_FORMAT_YUV420:
		cfg |= (GSC_OUT_CHROMA_ORDER_CBCR | GSC_OUT_YUV420_3P);
		break;
	case DRM_FORMAT_YVU420:
		cfg |= (GSC_OUT_CHROMA_ORDER_CRCB | GSC_OUT_YUV420_3P);
		break;
	case DRM_FORMAT_NV12:
		cfg |= (GSC_OUT_CHROMA_ORDER_CBCR | GSC_OUT_YUV420_2P);
		break;
	case DRM_FORMAT_NV16:
		cfg |= (GSC_OUT_CHROMA_ORDER_CBCR | GSC_OUT_YUV422_2P);
		break;
	}

	if (tiled)
		cfg |= (GSC_IN_TILE_C_16x8 | GSC_OUT_TILE_MODE);

	gsc_write(cfg, GSC_OUT_CON);
}

static int gsc_get_ratio_shift(struct gsc_context *ctx, u32 src, u32 dst,
			       u32 *ratio)
{
	DRM_DEV_DEBUG_KMS(ctx->dev, "src[%d]dst[%d]\n", src, dst);

	if (src >= dst * 8) {
		DRM_DEV_ERROR(ctx->dev, "failed to make ratio and shift.\n");
		return -EINVAL;
	} else if (src >= dst * 4)
		*ratio = 4;
	else if (src >= dst * 2)
		*ratio = 2;
	else
		*ratio = 1;

	return 0;
}

static void gsc_get_prescaler_shfactor(u32 hratio, u32 vratio, u32 *shfactor)
{
	if (hratio == 4 && vratio == 4)
		*shfactor = 4;
	else if ((hratio == 4 && vratio == 2) ||
		 (hratio == 2 && vratio == 4))
		*shfactor = 3;
	else if ((hratio == 4 && vratio == 1) ||
		 (hratio == 1 && vratio == 4) ||
		 (hratio == 2 && vratio == 2))
		*shfactor = 2;
	else if (hratio == 1 && vratio == 1)
		*shfactor = 0;
	else
		*shfactor = 1;
}

static int gsc_set_prescaler(struct gsc_context *ctx, struct gsc_scaler *sc,
			     struct drm_exynos_ipp_task_rect *src,
			     struct drm_exynos_ipp_task_rect *dst)
{
	u32 cfg;
	u32 src_w, src_h, dst_w, dst_h;
	int ret = 0;

	src_w = src->w;
	src_h = src->h;

	if (ctx->rotation) {
		dst_w = dst->h;
		dst_h = dst->w;
	} else {
		dst_w = dst->w;
		dst_h = dst->h;
	}

	ret = gsc_get_ratio_shift(ctx, src_w, dst_w, &sc->pre_hratio);
	if (ret) {
		DRM_DEV_ERROR(ctx->dev, "failed to get ratio horizontal.\n");
		return ret;
	}

	ret = gsc_get_ratio_shift(ctx, src_h, dst_h, &sc->pre_vratio);
	if (ret) {
		DRM_DEV_ERROR(ctx->dev, "failed to get ratio vertical.\n");
		return ret;
	}

	DRM_DEV_DEBUG_KMS(ctx->dev, "pre_hratio[%d]pre_vratio[%d]\n",
			  sc->pre_hratio, sc->pre_vratio);

	sc->main_hratio = (src_w << 16) / dst_w;
	sc->main_vratio = (src_h << 16) / dst_h;

	DRM_DEV_DEBUG_KMS(ctx->dev, "main_hratio[%ld]main_vratio[%ld]\n",
			  sc->main_hratio, sc->main_vratio);

	gsc_get_prescaler_shfactor(sc->pre_hratio, sc->pre_vratio,
		&sc->pre_shfactor);

	DRM_DEV_DEBUG_KMS(ctx->dev, "pre_shfactor[%d]\n", sc->pre_shfactor);

	cfg = (GSC_PRESC_SHFACTOR(sc->pre_shfactor) |
		GSC_PRESC_H_RATIO(sc->pre_hratio) |
		GSC_PRESC_V_RATIO(sc->pre_vratio));
	gsc_write(cfg, GSC_PRE_SCALE_RATIO);

	return ret;
}

static void gsc_set_h_coef(struct gsc_context *ctx, unsigned long main_hratio)
{
	int i, j, k, sc_ratio;

	if (main_hratio <= GSC_SC_UP_MAX_RATIO)
		sc_ratio = 0;
	else if (main_hratio <= GSC_SC_DOWN_RATIO_7_8)
		sc_ratio = 1;
	else if (main_hratio <= GSC_SC_DOWN_RATIO_6_8)
		sc_ratio = 2;
	else if (main_hratio <= GSC_SC_DOWN_RATIO_5_8)
		sc_ratio = 3;
	else if (main_hratio <= GSC_SC_DOWN_RATIO_4_8)
		sc_ratio = 4;
	else if (main_hratio <= GSC_SC_DOWN_RATIO_3_8)
		sc_ratio = 5;
	else
		sc_ratio = 6;

	for (i = 0; i < GSC_COEF_PHASE; i++)
		for (j = 0; j < GSC_COEF_H_8T; j++)
			for (k = 0; k < GSC_COEF_DEPTH; k++)
				gsc_write(h_coef_8t[sc_ratio][i][j],
					GSC_HCOEF(i, j, k));
}

static void gsc_set_v_coef(struct gsc_context *ctx, unsigned long main_vratio)
{
	int i, j, k, sc_ratio;

	if (main_vratio <= GSC_SC_UP_MAX_RATIO)
		sc_ratio = 0;
	else if (main_vratio <= GSC_SC_DOWN_RATIO_7_8)
		sc_ratio = 1;
	else if (main_vratio <= GSC_SC_DOWN_RATIO_6_8)
		sc_ratio = 2;
	else if (main_vratio <= GSC_SC_DOWN_RATIO_5_8)
		sc_ratio = 3;
	else if (main_vratio <= GSC_SC_DOWN_RATIO_4_8)
		sc_ratio = 4;
	else if (main_vratio <= GSC_SC_DOWN_RATIO_3_8)
		sc_ratio = 5;
	else
		sc_ratio = 6;

	for (i = 0; i < GSC_COEF_PHASE; i++)
		for (j = 0; j < GSC_COEF_V_4T; j++)
			for (k = 0; k < GSC_COEF_DEPTH; k++)
				gsc_write(v_coef_4t[sc_ratio][i][j],
					GSC_VCOEF(i, j, k));
}

static void gsc_set_scaler(struct gsc_context *ctx, struct gsc_scaler *sc)
{
	u32 cfg;

	DRM_DEV_DEBUG_KMS(ctx->dev, "main_hratio[%ld]main_vratio[%ld]\n",
			  sc->main_hratio, sc->main_vratio);

	gsc_set_h_coef(ctx, sc->main_hratio);
	cfg = GSC_MAIN_H_RATIO_VALUE(sc->main_hratio);
	gsc_write(cfg, GSC_MAIN_H_RATIO);

	gsc_set_v_coef(ctx, sc->main_vratio);
	cfg = GSC_MAIN_V_RATIO_VALUE(sc->main_vratio);
	gsc_write(cfg, GSC_MAIN_V_RATIO);
}

static void gsc_dst_set_size(struct gsc_context *ctx,
			     struct exynos_drm_ipp_buffer *buf)
{
	struct gsc_scaler *sc = &ctx->sc;
	u32 cfg;

	/* pixel offset */
	cfg = (GSC_DSTIMG_OFFSET_X(buf->rect.x) |
		GSC_DSTIMG_OFFSET_Y(buf->rect.y));
	gsc_write(cfg, GSC_DSTIMG_OFFSET);

	/* scaled size */
	if (ctx->rotation)
		cfg = (GSC_SCALED_WIDTH(buf->rect.h) |
		       GSC_SCALED_HEIGHT(buf->rect.w));
	else
		cfg = (GSC_SCALED_WIDTH(buf->rect.w) |
		       GSC_SCALED_HEIGHT(buf->rect.h));
	gsc_write(cfg, GSC_SCALED_SIZE);

	/* original size */
	cfg = gsc_read(GSC_DSTIMG_SIZE);
	cfg &= ~(GSC_DSTIMG_HEIGHT_MASK | GSC_DSTIMG_WIDTH_MASK);
	cfg |= GSC_DSTIMG_WIDTH(buf->buf.pitch[0] / buf->format->cpp[0]) |
	       GSC_DSTIMG_HEIGHT(buf->buf.height);
	gsc_write(cfg, GSC_DSTIMG_SIZE);

	cfg = gsc_read(GSC_OUT_CON);
	cfg &= ~GSC_OUT_RGB_TYPE_MASK;

	if (buf->rect.w >= GSC_WIDTH_ITU_709)
		if (sc->range)
			cfg |= GSC_OUT_RGB_HD_WIDE;
		else
			cfg |= GSC_OUT_RGB_HD_NARROW;
	else
		if (sc->range)
			cfg |= GSC_OUT_RGB_SD_WIDE;
		else
			cfg |= GSC_OUT_RGB_SD_NARROW;

	gsc_write(cfg, GSC_OUT_CON);
}

static int gsc_dst_get_buf_seq(struct gsc_context *ctx)
{
	u32 cfg, i, buf_num = GSC_REG_SZ;
	u32 mask = 0x00000001;

	cfg = gsc_read(GSC_OUT_BASE_ADDR_Y_MASK);

	for (i = 0; i < GSC_REG_SZ; i++)
		if (cfg & (mask << i))
			buf_num--;

	DRM_DEV_DEBUG_KMS(ctx->dev, "buf_num[%d]\n", buf_num);

	return buf_num;
}

static void gsc_dst_set_buf_seq(struct gsc_context *ctx, u32 buf_id,
				bool enqueue)
{
	bool masked = !enqueue;
	u32 cfg;
	u32 mask = 0x00000001 << buf_id;

	/* mask register set */
	cfg = gsc_read(GSC_OUT_BASE_ADDR_Y_MASK);

	/* sequence id */
	cfg &= ~mask;
	cfg |= masked << buf_id;
	gsc_write(cfg, GSC_OUT_BASE_ADDR_Y_MASK);
	gsc_write(cfg, GSC_OUT_BASE_ADDR_CB_MASK);
	gsc_write(cfg, GSC_OUT_BASE_ADDR_CR_MASK);

	/* interrupt enable */
	if (enqueue && gsc_dst_get_buf_seq(ctx) >= GSC_BUF_START)
		gsc_handle_irq(ctx, true, false, true);

	/* interrupt disable */
	if (!enqueue && gsc_dst_get_buf_seq(ctx) <= GSC_BUF_STOP)
		gsc_handle_irq(ctx, false, false, true);
}

static void gsc_dst_set_addr(struct gsc_context *ctx,
			     u32 buf_id, struct exynos_drm_ipp_buffer *buf)
{
	/* address register set */
	gsc_write(buf->dma_addr[0], GSC_OUT_BASE_ADDR_Y(buf_id));
	gsc_write(buf->dma_addr[1], GSC_OUT_BASE_ADDR_CB(buf_id));
	gsc_write(buf->dma_addr[2], GSC_OUT_BASE_ADDR_CR(buf_id));

	gsc_dst_set_buf_seq(ctx, buf_id, true);
}

static int gsc_get_src_buf_index(struct gsc_context *ctx)
{
	u32 cfg, curr_index, i;
	u32 buf_id = GSC_MAX_SRC;

	DRM_DEV_DEBUG_KMS(ctx->dev, "gsc id[%d]\n", ctx->id);

	cfg = gsc_read(GSC_IN_BASE_ADDR_Y_MASK);
	curr_index = GSC_IN_CURR_GET_INDEX(cfg);

	for (i = curr_index; i < GSC_MAX_SRC; i++) {
		if (!((cfg >> i) & 0x1)) {
			buf_id = i;
			break;
		}
	}

	DRM_DEV_DEBUG_KMS(ctx->dev, "cfg[0x%x]curr_index[%d]buf_id[%d]\n", cfg,
			  curr_index, buf_id);

	if (buf_id == GSC_MAX_SRC) {
		DRM_DEV_ERROR(ctx->dev, "failed to get in buffer index.\n");
		return -EINVAL;
	}

	gsc_src_set_buf_seq(ctx, buf_id, false);

	return buf_id;
}

static int gsc_get_dst_buf_index(struct gsc_context *ctx)
{
	u32 cfg, curr_index, i;
	u32 buf_id = GSC_MAX_DST;

	DRM_DEV_DEBUG_KMS(ctx->dev, "gsc id[%d]\n", ctx->id);

	cfg = gsc_read(GSC_OUT_BASE_ADDR_Y_MASK);
	curr_index = GSC_OUT_CURR_GET_INDEX(cfg);

	for (i = curr_index; i < GSC_MAX_DST; i++) {
		if (!((cfg >> i) & 0x1)) {
			buf_id = i;
			break;
		}
	}

	if (buf_id == GSC_MAX_DST) {
		DRM_DEV_ERROR(ctx->dev, "failed to get out buffer index.\n");
		return -EINVAL;
	}

	gsc_dst_set_buf_seq(ctx, buf_id, false);

	DRM_DEV_DEBUG_KMS(ctx->dev, "cfg[0x%x]curr_index[%d]buf_id[%d]\n", cfg,
			  curr_index, buf_id);

	return buf_id;
}

static irqreturn_t gsc_irq_handler(int irq, void *dev_id)
{
	struct gsc_context *ctx = dev_id;
	u32 status;
	int err = 0;

	DRM_DEV_DEBUG_KMS(ctx->dev, "gsc id[%d]\n", ctx->id);

	status = gsc_read(GSC_IRQ);
	if (status & GSC_IRQ_STATUS_OR_IRQ) {
		dev_err(ctx->dev, "occurred overflow at %d, status 0x%x.\n",
			ctx->id, status);
		err = -EINVAL;
	}

	if (status & GSC_IRQ_STATUS_OR_FRM_DONE) {
		int src_buf_id, dst_buf_id;

		dev_dbg(ctx->dev, "occurred frame done at %d, status 0x%x.\n",
			ctx->id, status);

		src_buf_id = gsc_get_src_buf_index(ctx);
		dst_buf_id = gsc_get_dst_buf_index(ctx);

		DRM_DEV_DEBUG_KMS(ctx->dev, "buf_id_src[%d]buf_id_dst[%d]\n",
				  src_buf_id, dst_buf_id);

		if (src_buf_id < 0 || dst_buf_id < 0)
			err = -EINVAL;
	}

	if (ctx->task) {
		struct exynos_drm_ipp_task *task = ctx->task;

		ctx->task = NULL;
		pm_runtime_mark_last_busy(ctx->dev);
		pm_runtime_put_autosuspend(ctx->dev);
		exynos_drm_ipp_task_done(task, err);
	}

	return IRQ_HANDLED;
}

static int gsc_reset(struct gsc_context *ctx)
{
	struct gsc_scaler *sc = &ctx->sc;
	int ret;

	/* reset h/w block */
	ret = gsc_sw_reset(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to reset hardware.\n");
		return ret;
	}

	/* scaler setting */
	memset(&ctx->sc, 0x0, sizeof(ctx->sc));
	sc->range = true;

	return 0;
}

static void gsc_start(struct gsc_context *ctx)
{
	u32 cfg;

	gsc_handle_irq(ctx, true, false, true);

	/* enable one shot */
	cfg = gsc_read(GSC_ENABLE);
	cfg &= ~(GSC_ENABLE_ON_CLEAR_MASK |
		GSC_ENABLE_CLK_GATE_MODE_MASK);
	cfg |= GSC_ENABLE_ON_CLEAR_ONESHOT;
	gsc_write(cfg, GSC_ENABLE);

	/* src dma memory */
	cfg = gsc_read(GSC_IN_CON);
	cfg &= ~(GSC_IN_PATH_MASK | GSC_IN_LOCAL_SEL_MASK);
	cfg |= GSC_IN_PATH_MEMORY;
	gsc_write(cfg, GSC_IN_CON);

	/* dst dma memory */
	cfg = gsc_read(GSC_OUT_CON);
	cfg |= GSC_OUT_PATH_MEMORY;
	gsc_write(cfg, GSC_OUT_CON);

	gsc_set_scaler(ctx, &ctx->sc);

	cfg = gsc_read(GSC_ENABLE);
	cfg |= GSC_ENABLE_ON;
	gsc_write(cfg, GSC_ENABLE);
}

static int gsc_commit(struct exynos_drm_ipp *ipp,
			  struct exynos_drm_ipp_task *task)
{
	struct gsc_context *ctx = container_of(ipp, struct gsc_context, ipp);
	int ret;

	pm_runtime_get_sync(ctx->dev);
	ctx->task = task;

	ret = gsc_reset(ctx);
	if (ret) {
		pm_runtime_put_autosuspend(ctx->dev);
		ctx->task = NULL;
		return ret;
	}

	gsc_src_set_fmt(ctx, task->src.buf.fourcc, task->src.buf.modifier);
	gsc_src_set_transf(ctx, task->transform.rotation);
	gsc_src_set_size(ctx, &task->src);
	gsc_src_set_addr(ctx, 0, &task->src);
	gsc_dst_set_fmt(ctx, task->dst.buf.fourcc, task->dst.buf.modifier);
	gsc_dst_set_size(ctx, &task->dst);
	gsc_dst_set_addr(ctx, 0, &task->dst);
	gsc_set_prescaler(ctx, &ctx->sc, &task->src.rect, &task->dst.rect);
	gsc_start(ctx);

	return 0;
}

static void gsc_abort(struct exynos_drm_ipp *ipp,
			  struct exynos_drm_ipp_task *task)
{
	struct gsc_context *ctx =
			container_of(ipp, struct gsc_context, ipp);

	gsc_reset(ctx);
	if (ctx->task) {
		struct exynos_drm_ipp_task *task = ctx->task;

		ctx->task = NULL;
		pm_runtime_mark_last_busy(ctx->dev);
		pm_runtime_put_autosuspend(ctx->dev);
		exynos_drm_ipp_task_done(task, -EIO);
	}
}

static struct exynos_drm_ipp_funcs ipp_funcs = {
	.commit = gsc_commit,
	.abort = gsc_abort,
};

static int gsc_bind(struct device *dev, struct device *master, void *data)
{
	struct gsc_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_ipp *ipp = &ctx->ipp;

	ctx->drm_dev = drm_dev;
	ctx->drm_dev = drm_dev;
	exynos_drm_register_dma(drm_dev, dev, &ctx->dma_priv);

	exynos_drm_ipp_register(dev, ipp, &ipp_funcs,
			DRM_EXYNOS_IPP_CAP_CROP | DRM_EXYNOS_IPP_CAP_ROTATE |
			DRM_EXYNOS_IPP_CAP_SCALE | DRM_EXYNOS_IPP_CAP_CONVERT,
			ctx->formats, ctx->num_formats, "gsc");

	dev_info(dev, "The exynos gscaler has been probed successfully\n");

	return 0;
}

static void gsc_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct gsc_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_ipp *ipp = &ctx->ipp;

	exynos_drm_ipp_unregister(dev, ipp);
	exynos_drm_unregister_dma(drm_dev, dev, &ctx->dma_priv);
}

static const struct component_ops gsc_component_ops = {
	.bind	= gsc_bind,
	.unbind = gsc_unbind,
};

static const unsigned int gsc_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888, DRM_FORMAT_RGB565, DRM_FORMAT_BGRX8888,
	DRM_FORMAT_NV12, DRM_FORMAT_NV16, DRM_FORMAT_NV21, DRM_FORMAT_NV61,
	DRM_FORMAT_UYVY, DRM_FORMAT_VYUY, DRM_FORMAT_YUYV, DRM_FORMAT_YVYU,
	DRM_FORMAT_YUV420, DRM_FORMAT_YVU420, DRM_FORMAT_YUV422,
};

static const unsigned int gsc_tiled_formats[] = {
	DRM_FORMAT_NV12, DRM_FORMAT_NV21,
};

static int gsc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gsc_driverdata *driver_data;
	struct exynos_drm_ipp_formats *formats;
	struct gsc_context *ctx;
	struct resource *res;
	int num_formats, ret, i, j;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	driver_data = (struct gsc_driverdata *)of_device_get_match_data(dev);
	ctx->dev = dev;
	ctx->num_clocks = driver_data->num_clocks;
	ctx->clk_names = driver_data->clk_names;

	/* construct formats/limits array */
	num_formats = ARRAY_SIZE(gsc_formats) + ARRAY_SIZE(gsc_tiled_formats);
	formats = devm_kcalloc(dev, num_formats, sizeof(*formats), GFP_KERNEL);
	if (!formats)
		return -ENOMEM;

	/* linear formats */
	for (i = 0; i < ARRAY_SIZE(gsc_formats); i++) {
		formats[i].fourcc = gsc_formats[i];
		formats[i].type = DRM_EXYNOS_IPP_FORMAT_SOURCE |
				  DRM_EXYNOS_IPP_FORMAT_DESTINATION;
		formats[i].limits = driver_data->limits;
		formats[i].num_limits = driver_data->num_limits;
	}

	/* tiled formats */
	for (j = i, i = 0; i < ARRAY_SIZE(gsc_tiled_formats); j++, i++) {
		formats[j].fourcc = gsc_tiled_formats[i];
		formats[j].modifier = DRM_FORMAT_MOD_SAMSUNG_16_16_TILE;
		formats[j].type = DRM_EXYNOS_IPP_FORMAT_SOURCE |
				  DRM_EXYNOS_IPP_FORMAT_DESTINATION;
		formats[j].limits = driver_data->limits;
		formats[j].num_limits = driver_data->num_limits;
	}

	ctx->formats = formats;
	ctx->num_formats = num_formats;

	/* clock control */
	for (i = 0; i < ctx->num_clocks; i++) {
		ctx->clocks[i] = devm_clk_get(dev, ctx->clk_names[i]);
		if (IS_ERR(ctx->clocks[i])) {
			dev_err(dev, "failed to get clock: %s\n",
				ctx->clk_names[i]);
			return PTR_ERR(ctx->clocks[i]);
		}
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
	ret = devm_request_irq(dev, ctx->irq, gsc_irq_handler, 0,
			       dev_name(dev), ctx);
	if (ret < 0) {
		dev_err(dev, "failed to request irq.\n");
		return ret;
	}

	/* context initailization */
	ctx->id = pdev->id;

	platform_set_drvdata(pdev, ctx);

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, GSC_AUTOSUSPEND_DELAY);
	pm_runtime_enable(dev);

	ret = component_add(dev, &gsc_component_ops);
	if (ret)
		goto err_pm_dis;

	dev_info(dev, "drm gsc registered successfully.\n");

	return 0;

err_pm_dis:
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int gsc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_del(dev, &gsc_component_ops);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	return 0;
}

static int __maybe_unused gsc_runtime_suspend(struct device *dev)
{
	struct gsc_context *ctx = get_gsc_context(dev);
	int i;

	DRM_DEV_DEBUG_KMS(dev, "id[%d]\n", ctx->id);

	for (i = ctx->num_clocks - 1; i >= 0; i--)
		clk_disable_unprepare(ctx->clocks[i]);

	return 0;
}

static int __maybe_unused gsc_runtime_resume(struct device *dev)
{
	struct gsc_context *ctx = get_gsc_context(dev);
	int i, ret;

	DRM_DEV_DEBUG_KMS(dev, "id[%d]\n", ctx->id);

	for (i = 0; i < ctx->num_clocks; i++) {
		ret = clk_prepare_enable(ctx->clocks[i]);
		if (ret) {
			while (--i > 0)
				clk_disable_unprepare(ctx->clocks[i]);
			return ret;
		}
	}
	return 0;
}

static const struct dev_pm_ops gsc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(gsc_runtime_suspend, gsc_runtime_resume, NULL)
};

static const struct drm_exynos_ipp_limit gsc_5250_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 32, 4800, 8 }, .v = { 16, 3344, 8 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 16, 4800, 2 }, .v = { 8, 3344, 2 }) },
	{ IPP_SIZE_LIMIT(ROTATED, .h = { 32, 2048 }, .v = { 16, 2048 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 16, (1 << 16) * 8 },
			  .v = { (1 << 16) / 16, (1 << 16) * 8 }) },
};

static const struct drm_exynos_ipp_limit gsc_5420_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 32, 4800, 8 }, .v = { 16, 3344, 8 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 16, 4800, 2 }, .v = { 8, 3344, 2 }) },
	{ IPP_SIZE_LIMIT(ROTATED, .h = { 16, 2016 }, .v = { 8, 2016 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 16, (1 << 16) * 8 },
			  .v = { (1 << 16) / 16, (1 << 16) * 8 }) },
};

static const struct drm_exynos_ipp_limit gsc_5433_limits[] = {
	{ IPP_SIZE_LIMIT(BUFFER, .h = { 32, 8191, 16 }, .v = { 16, 8191, 2 }) },
	{ IPP_SIZE_LIMIT(AREA, .h = { 16, 4800, 1 }, .v = { 8, 3344, 1 }) },
	{ IPP_SIZE_LIMIT(ROTATED, .h = { 32, 2047 }, .v = { 8, 8191 }) },
	{ IPP_SCALE_LIMIT(.h = { (1 << 16) / 16, (1 << 16) * 8 },
			  .v = { (1 << 16) / 16, (1 << 16) * 8 }) },
};

static struct gsc_driverdata gsc_exynos5250_drvdata = {
	.clk_names = {"gscl"},
	.num_clocks = 1,
	.limits = gsc_5250_limits,
	.num_limits = ARRAY_SIZE(gsc_5250_limits),
};

static struct gsc_driverdata gsc_exynos5420_drvdata = {
	.clk_names = {"gscl"},
	.num_clocks = 1,
	.limits = gsc_5420_limits,
	.num_limits = ARRAY_SIZE(gsc_5420_limits),
};

static struct gsc_driverdata gsc_exynos5433_drvdata = {
	.clk_names = {"pclk", "aclk", "aclk_xiu", "aclk_gsclbend"},
	.num_clocks = 4,
	.limits = gsc_5433_limits,
	.num_limits = ARRAY_SIZE(gsc_5433_limits),
};

static const struct of_device_id exynos_drm_gsc_of_match[] = {
	{
		.compatible = "samsung,exynos5-gsc",
		.data = &gsc_exynos5250_drvdata,
	}, {
		.compatible = "samsung,exynos5250-gsc",
		.data = &gsc_exynos5250_drvdata,
	}, {
		.compatible = "samsung,exynos5420-gsc",
		.data = &gsc_exynos5420_drvdata,
	}, {
		.compatible = "samsung,exynos5433-gsc",
		.data = &gsc_exynos5433_drvdata,
	}, {
	},
};
MODULE_DEVICE_TABLE(of, exynos_drm_gsc_of_match);

struct platform_driver gsc_driver = {
	.probe		= gsc_probe,
	.remove		= gsc_remove,
	.driver		= {
		.name	= "exynos-drm-gsc",
		.owner	= THIS_MODULE,
		.pm	= &gsc_pm_ops,
		.of_match_table = of_match_ptr(exynos_drm_gsc_of_match),
	},
};
