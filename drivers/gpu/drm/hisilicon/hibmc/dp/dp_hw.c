// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/io.h>
#include <linux/delay.h>
#include "dp_config.h"
#include "dp_comm.h"
#include "dp_reg.h"
#include "dp_hw.h"

static void hibmc_dp_set_tu(struct hibmc_dp_dev *dp, struct drm_display_mode *mode)
{
	u32 tu_symbol_frac_size;
	u32 tu_symbol_size;
	u32 rate_ks;
	u8 lane_num;
	u32 value;
	u32 bpp;

	lane_num = dp->link.cap.lanes;
	if (lane_num == 0) {
		drm_err(dp->dev, "set tu failed, lane num cannot be 0!\n");
		return;
	}

	bpp = HIBMC_DP_BPP;
	rate_ks = dp->link.cap.link_rate * HIBMC_DP_LINK_RATE_CAL;
	value = (mode->clock * bpp * 5) / (61 * lane_num * rate_ks);

	if (value % 10 == 9) { /* 9 carry */
		tu_symbol_size = value / 10 + 1;
		tu_symbol_frac_size = 0;
	} else {
		tu_symbol_size = value / 10;
		tu_symbol_frac_size = value % 10 + 1;
	}

	drm_dbg_dp(dp->dev, "tu value: %u.%u value: %u\n",
		   tu_symbol_size, tu_symbol_frac_size, value);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_PACKET,
				 HIBMC_DP_CFG_STREAM_TU_SYMBOL_SIZE, tu_symbol_size);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_PACKET,
				 HIBMC_DP_CFG_STREAM_TU_SYMBOL_FRAC_SIZE, tu_symbol_frac_size);
}

static void hibmc_dp_set_sst(struct hibmc_dp_dev *dp, struct drm_display_mode *mode)
{
	u32 hblank_size;
	u32 htotal_size;
	u32 htotal_int;
	u32 hblank_int;
	u32 fclk; /* flink_clock */

	fclk = dp->link.cap.link_rate * HIBMC_DP_LINK_RATE_CAL;

	/* Considering the effect of spread spectrum, the value may be deviated.
	 * The coefficient (0.9947) is used to offset the deviation.
	 */
	htotal_int = mode->htotal * 9947 / 10000;
	htotal_size = htotal_int * fclk / (HIBMC_DP_SYMBOL_PER_FCLK * (mode->clock / 1000));

	hblank_int = mode->htotal - mode->hdisplay - mode->hdisplay * 53 / 10000;
	hblank_size = hblank_int * fclk * 9947 /
		      (mode->clock * 10 * HIBMC_DP_SYMBOL_PER_FCLK);

	drm_dbg_dp(dp->dev, "h_active %u v_active %u htotal_size %u hblank_size %u",
		   mode->hdisplay, mode->vdisplay, htotal_size, hblank_size);
	drm_dbg_dp(dp->dev, "flink_clock %u pixel_clock %d", fclk, mode->clock / 1000);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_HORIZONTAL_SIZE,
				 HIBMC_DP_CFG_STREAM_HTOTAL_SIZE, htotal_size);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_HORIZONTAL_SIZE,
				 HIBMC_DP_CFG_STREAM_HBLANK_SIZE, hblank_size);
}

static void hibmc_dp_link_cfg(struct hibmc_dp_dev *dp, struct drm_display_mode *mode)
{
	u32 timing_delay;
	u32 vblank;
	u32 hstart;
	u32 vstart;

	vblank = mode->vtotal - mode->vdisplay;
	timing_delay = mode->htotal - mode->hsync_start;
	hstart = mode->htotal - mode->hsync_start;
	vstart = mode->vtotal - mode->vsync_start;

	hibmc_dp_reg_write_field(dp, HIBMC_DP_TIMING_GEN_CONFIG0,
				 HIBMC_DP_CFG_TIMING_GEN0_HBLANK, mode->htotal - mode->hdisplay);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_TIMING_GEN_CONFIG0,
				 HIBMC_DP_CFG_TIMING_GEN0_HACTIVE, mode->hdisplay);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_TIMING_GEN_CONFIG2,
				 HIBMC_DP_CFG_TIMING_GEN0_VBLANK, vblank);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_TIMING_GEN_CONFIG2,
				 HIBMC_DP_CFG_TIMING_GEN0_VACTIVE, mode->vdisplay);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_TIMING_GEN_CONFIG3,
				 HIBMC_DP_CFG_TIMING_GEN0_VFRONT_PORCH,
				 mode->vsync_start - mode->vdisplay);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CONFIG0,
				 HIBMC_DP_CFG_STREAM_HACTIVE, mode->hdisplay);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CONFIG0,
				 HIBMC_DP_CFG_STREAM_HBLANK, mode->htotal - mode->hdisplay);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CONFIG2,
				 HIBMC_DP_CFG_STREAM_HSYNC_WIDTH,
				 mode->hsync_end - mode->hsync_start);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CONFIG1,
				 HIBMC_DP_CFG_STREAM_VACTIVE, mode->vdisplay);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CONFIG1,
				 HIBMC_DP_CFG_STREAM_VBLANK, vblank);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CONFIG3,
				 HIBMC_DP_CFG_STREAM_VFRONT_PORCH,
				 mode->vsync_start - mode->vdisplay);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CONFIG3,
				 HIBMC_DP_CFG_STREAM_VSYNC_WIDTH,
				 mode->vsync_end - mode->vsync_start);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_MSA0,
				 HIBMC_DP_CFG_STREAM_VSTART, vstart);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_MSA0,
				 HIBMC_DP_CFG_STREAM_HSTART, hstart);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CTRL, HIBMC_DP_CFG_STREAM_VSYNC_POLARITY,
				 mode->flags & DRM_MODE_FLAG_PVSYNC ? 1 : 0);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CTRL, HIBMC_DP_CFG_STREAM_HSYNC_POLARITY,
				 mode->flags & DRM_MODE_FLAG_PHSYNC ? 1 : 0);

	/* MSA mic 0 and 1 */
	writel(HIBMC_DP_MSA1, dp->base + HIBMC_DP_VIDEO_MSA1);
	writel(HIBMC_DP_MSA2, dp->base + HIBMC_DP_VIDEO_MSA2);

	hibmc_dp_set_tu(dp, mode);

	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CTRL, HIBMC_DP_CFG_STREAM_RGB_ENABLE, 0x1);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_VIDEO_CTRL, HIBMC_DP_CFG_STREAM_VIDEO_MAPPING, 0);

	/* divide 2: up even */
	if (timing_delay % 2)
		timing_delay++;

	hibmc_dp_reg_write_field(dp, HIBMC_DP_TIMING_MODEL_CTRL,
				 HIBMC_DP_CFG_PIXEL_NUM_TIMING_MODE_SEL1, timing_delay);

	hibmc_dp_set_sst(dp, mode);
}

int hibmc_dp_hw_init(struct hibmc_dp *dp)
{
	struct drm_device *drm_dev = dp->drm_dev;
	struct hibmc_dp_dev *dp_dev;

	dp_dev = devm_kzalloc(drm_dev->dev, sizeof(struct hibmc_dp_dev), GFP_KERNEL);
	if (!dp_dev)
		return -ENOMEM;

	mutex_init(&dp_dev->lock);

	dp->dp_dev = dp_dev;

	dp_dev->dev = drm_dev;
	dp_dev->base = dp->mmio + HIBMC_DP_OFFSET;

	hibmc_dp_aux_init(dp_dev);

	dp_dev->link.cap.lanes = 0x2;
	dp_dev->link.cap.link_rate = DP_LINK_BW_2_7;

	/* hdcp data */
	writel(HIBMC_DP_HDCP, dp_dev->base + HIBMC_DP_HDCP_CFG);
	/* int init */
	writel(0, dp_dev->base + HIBMC_DP_INTR_ENABLE);
	writel(HIBMC_DP_INT_RST, dp_dev->base + HIBMC_DP_INTR_ORIGINAL_STATUS);
	/* rst */
	writel(HIBMC_DP_DPTX_RST, dp_dev->base + HIBMC_DP_DPTX_RST_CTRL);
	/* clock enable */
	writel(HIBMC_DP_CLK_EN, dp_dev->base + HIBMC_DP_DPTX_CLK_CTRL);

	return 0;
}

void hibmc_dp_display_en(struct hibmc_dp *dp, bool enable)
{
	struct hibmc_dp_dev *dp_dev = dp->dp_dev;

	if (enable) {
		hibmc_dp_reg_write_field(dp_dev, HIBMC_DP_VIDEO_CTRL, BIT(0), 0x1);
		writel(HIBMC_DP_SYNC_EN_MASK, dp_dev->base + HIBMC_DP_TIMING_SYNC_CTRL);
		hibmc_dp_reg_write_field(dp_dev, HIBMC_DP_DPTX_GCTL0, BIT(10), 0x1);
		writel(HIBMC_DP_SYNC_EN_MASK, dp_dev->base + HIBMC_DP_TIMING_SYNC_CTRL);
	} else {
		hibmc_dp_reg_write_field(dp_dev, HIBMC_DP_DPTX_GCTL0, BIT(10), 0);
		writel(HIBMC_DP_SYNC_EN_MASK, dp_dev->base + HIBMC_DP_TIMING_SYNC_CTRL);
		hibmc_dp_reg_write_field(dp_dev, HIBMC_DP_VIDEO_CTRL, BIT(0), 0);
		writel(HIBMC_DP_SYNC_EN_MASK, dp_dev->base + HIBMC_DP_TIMING_SYNC_CTRL);
	}

	msleep(50);
}

int hibmc_dp_mode_set(struct hibmc_dp *dp, struct drm_display_mode *mode)
{
	struct hibmc_dp_dev *dp_dev = dp->dp_dev;
	int ret;

	if (!dp_dev->link.status.channel_equalized) {
		ret = hibmc_dp_link_training(dp_dev);
		if (ret) {
			drm_err(dp->drm_dev, "dp link training failed, ret: %d\n", ret);
			return ret;
		}
	}

	hibmc_dp_display_en(dp, false);
	hibmc_dp_link_cfg(dp_dev, mode);

	return 0;
}
