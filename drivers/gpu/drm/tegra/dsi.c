/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/host1x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

#include "dc.h"
#include "drm.h"
#include "dsi.h"
#include "mipi-phy.h"

struct tegra_dsi_state {
	struct drm_connector_state base;

	struct mipi_dphy_timing timing;
	unsigned long period;

	unsigned int vrefresh;
	unsigned int lanes;
	unsigned long pclk;
	unsigned long bclk;

	enum tegra_dsi_format format;
	unsigned int mul;
	unsigned int div;
};

static inline struct tegra_dsi_state *
to_dsi_state(struct drm_connector_state *state)
{
	return container_of(state, struct tegra_dsi_state, base);
}

struct tegra_dsi {
	struct host1x_client client;
	struct tegra_output output;
	struct device *dev;

	void __iomem *regs;

	struct reset_control *rst;
	struct clk *clk_parent;
	struct clk *clk_lp;
	struct clk *clk;

	struct drm_info_list *debugfs_files;
	struct drm_minor *minor;
	struct dentry *debugfs;

	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;

	struct tegra_mipi_device *mipi;
	struct mipi_dsi_host host;

	struct regulator *vdd;

	unsigned int video_fifo_depth;
	unsigned int host_fifo_depth;

	/* for ganged-mode support */
	struct tegra_dsi *master;
	struct tegra_dsi *slave;
};

static inline struct tegra_dsi *
host1x_client_to_dsi(struct host1x_client *client)
{
	return container_of(client, struct tegra_dsi, client);
}

static inline struct tegra_dsi *host_to_tegra(struct mipi_dsi_host *host)
{
	return container_of(host, struct tegra_dsi, host);
}

static inline struct tegra_dsi *to_dsi(struct tegra_output *output)
{
	return container_of(output, struct tegra_dsi, output);
}

static struct tegra_dsi_state *tegra_dsi_get_state(struct tegra_dsi *dsi)
{
	return to_dsi_state(dsi->output.connector.state);
}

static inline u32 tegra_dsi_readl(struct tegra_dsi *dsi, unsigned long reg)
{
	return readl(dsi->regs + (reg << 2));
}

static inline void tegra_dsi_writel(struct tegra_dsi *dsi, u32 value,
				    unsigned long reg)
{
	writel(value, dsi->regs + (reg << 2));
}

static int tegra_dsi_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_dsi *dsi = node->info_ent->data;
	struct drm_crtc *crtc = dsi->output.encoder.crtc;
	struct drm_device *drm = node->minor->dev;
	int err = 0;

	drm_modeset_lock_all(drm);

	if (!crtc || !crtc->state->active) {
		err = -EBUSY;
		goto unlock;
	}

#define DUMP_REG(name)						\
	seq_printf(s, "%-32s %#05x %08x\n", #name, name,	\
		   tegra_dsi_readl(dsi, name))

	DUMP_REG(DSI_INCR_SYNCPT);
	DUMP_REG(DSI_INCR_SYNCPT_CONTROL);
	DUMP_REG(DSI_INCR_SYNCPT_ERROR);
	DUMP_REG(DSI_CTXSW);
	DUMP_REG(DSI_RD_DATA);
	DUMP_REG(DSI_WR_DATA);
	DUMP_REG(DSI_POWER_CONTROL);
	DUMP_REG(DSI_INT_ENABLE);
	DUMP_REG(DSI_INT_STATUS);
	DUMP_REG(DSI_INT_MASK);
	DUMP_REG(DSI_HOST_CONTROL);
	DUMP_REG(DSI_CONTROL);
	DUMP_REG(DSI_SOL_DELAY);
	DUMP_REG(DSI_MAX_THRESHOLD);
	DUMP_REG(DSI_TRIGGER);
	DUMP_REG(DSI_TX_CRC);
	DUMP_REG(DSI_STATUS);

	DUMP_REG(DSI_INIT_SEQ_CONTROL);
	DUMP_REG(DSI_INIT_SEQ_DATA_0);
	DUMP_REG(DSI_INIT_SEQ_DATA_1);
	DUMP_REG(DSI_INIT_SEQ_DATA_2);
	DUMP_REG(DSI_INIT_SEQ_DATA_3);
	DUMP_REG(DSI_INIT_SEQ_DATA_4);
	DUMP_REG(DSI_INIT_SEQ_DATA_5);
	DUMP_REG(DSI_INIT_SEQ_DATA_6);
	DUMP_REG(DSI_INIT_SEQ_DATA_7);

	DUMP_REG(DSI_PKT_SEQ_0_LO);
	DUMP_REG(DSI_PKT_SEQ_0_HI);
	DUMP_REG(DSI_PKT_SEQ_1_LO);
	DUMP_REG(DSI_PKT_SEQ_1_HI);
	DUMP_REG(DSI_PKT_SEQ_2_LO);
	DUMP_REG(DSI_PKT_SEQ_2_HI);
	DUMP_REG(DSI_PKT_SEQ_3_LO);
	DUMP_REG(DSI_PKT_SEQ_3_HI);
	DUMP_REG(DSI_PKT_SEQ_4_LO);
	DUMP_REG(DSI_PKT_SEQ_4_HI);
	DUMP_REG(DSI_PKT_SEQ_5_LO);
	DUMP_REG(DSI_PKT_SEQ_5_HI);

	DUMP_REG(DSI_DCS_CMDS);

	DUMP_REG(DSI_PKT_LEN_0_1);
	DUMP_REG(DSI_PKT_LEN_2_3);
	DUMP_REG(DSI_PKT_LEN_4_5);
	DUMP_REG(DSI_PKT_LEN_6_7);

	DUMP_REG(DSI_PHY_TIMING_0);
	DUMP_REG(DSI_PHY_TIMING_1);
	DUMP_REG(DSI_PHY_TIMING_2);
	DUMP_REG(DSI_BTA_TIMING);

	DUMP_REG(DSI_TIMEOUT_0);
	DUMP_REG(DSI_TIMEOUT_1);
	DUMP_REG(DSI_TO_TALLY);

	DUMP_REG(DSI_PAD_CONTROL_0);
	DUMP_REG(DSI_PAD_CONTROL_CD);
	DUMP_REG(DSI_PAD_CD_STATUS);
	DUMP_REG(DSI_VIDEO_MODE_CONTROL);
	DUMP_REG(DSI_PAD_CONTROL_1);
	DUMP_REG(DSI_PAD_CONTROL_2);
	DUMP_REG(DSI_PAD_CONTROL_3);
	DUMP_REG(DSI_PAD_CONTROL_4);

	DUMP_REG(DSI_GANGED_MODE_CONTROL);
	DUMP_REG(DSI_GANGED_MODE_START);
	DUMP_REG(DSI_GANGED_MODE_SIZE);

	DUMP_REG(DSI_RAW_DATA_BYTE_COUNT);
	DUMP_REG(DSI_ULTRA_LOW_POWER_CONTROL);

	DUMP_REG(DSI_INIT_SEQ_DATA_8);
	DUMP_REG(DSI_INIT_SEQ_DATA_9);
	DUMP_REG(DSI_INIT_SEQ_DATA_10);
	DUMP_REG(DSI_INIT_SEQ_DATA_11);
	DUMP_REG(DSI_INIT_SEQ_DATA_12);
	DUMP_REG(DSI_INIT_SEQ_DATA_13);
	DUMP_REG(DSI_INIT_SEQ_DATA_14);
	DUMP_REG(DSI_INIT_SEQ_DATA_15);

#undef DUMP_REG

unlock:
	drm_modeset_unlock_all(drm);
	return err;
}

static struct drm_info_list debugfs_files[] = {
	{ "regs", tegra_dsi_show_regs, 0, NULL },
};

static int tegra_dsi_debugfs_init(struct tegra_dsi *dsi,
				  struct drm_minor *minor)
{
	const char *name = dev_name(dsi->dev);
	unsigned int i;
	int err;

	dsi->debugfs = debugfs_create_dir(name, minor->debugfs_root);
	if (!dsi->debugfs)
		return -ENOMEM;

	dsi->debugfs_files = kmemdup(debugfs_files, sizeof(debugfs_files),
				     GFP_KERNEL);
	if (!dsi->debugfs_files) {
		err = -ENOMEM;
		goto remove;
	}

	for (i = 0; i < ARRAY_SIZE(debugfs_files); i++)
		dsi->debugfs_files[i].data = dsi;

	err = drm_debugfs_create_files(dsi->debugfs_files,
				       ARRAY_SIZE(debugfs_files),
				       dsi->debugfs, minor);
	if (err < 0)
		goto free;

	dsi->minor = minor;

	return 0;

free:
	kfree(dsi->debugfs_files);
	dsi->debugfs_files = NULL;
remove:
	debugfs_remove(dsi->debugfs);
	dsi->debugfs = NULL;

	return err;
}

static void tegra_dsi_debugfs_exit(struct tegra_dsi *dsi)
{
	drm_debugfs_remove_files(dsi->debugfs_files, ARRAY_SIZE(debugfs_files),
				 dsi->minor);
	dsi->minor = NULL;

	kfree(dsi->debugfs_files);
	dsi->debugfs_files = NULL;

	debugfs_remove(dsi->debugfs);
	dsi->debugfs = NULL;
}

#define PKT_ID0(id)	((((id) & 0x3f) <<  3) | (1 <<  9))
#define PKT_LEN0(len)	(((len) & 0x07) <<  0)
#define PKT_ID1(id)	((((id) & 0x3f) << 13) | (1 << 19))
#define PKT_LEN1(len)	(((len) & 0x07) << 10)
#define PKT_ID2(id)	((((id) & 0x3f) << 23) | (1 << 29))
#define PKT_LEN2(len)	(((len) & 0x07) << 20)

#define PKT_LP		(1 << 30)
#define NUM_PKT_SEQ	12

/*
 * non-burst mode with sync pulses
 */
static const u32 pkt_seq_video_non_burst_sync_pulses[NUM_PKT_SEQ] = {
	[ 0] = PKT_ID0(MIPI_DSI_V_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(1) |
	       PKT_ID2(MIPI_DSI_H_SYNC_END) | PKT_LEN2(0) |
	       PKT_LP,
	[ 1] = 0,
	[ 2] = PKT_ID0(MIPI_DSI_V_SYNC_END) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(1) |
	       PKT_ID2(MIPI_DSI_H_SYNC_END) | PKT_LEN2(0) |
	       PKT_LP,
	[ 3] = 0,
	[ 4] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(1) |
	       PKT_ID2(MIPI_DSI_H_SYNC_END) | PKT_LEN2(0) |
	       PKT_LP,
	[ 5] = 0,
	[ 6] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(1) |
	       PKT_ID2(MIPI_DSI_H_SYNC_END) | PKT_LEN2(0),
	[ 7] = PKT_ID0(MIPI_DSI_BLANKING_PACKET) | PKT_LEN0(2) |
	       PKT_ID1(MIPI_DSI_PACKED_PIXEL_STREAM_24) | PKT_LEN1(3) |
	       PKT_ID2(MIPI_DSI_BLANKING_PACKET) | PKT_LEN2(4),
	[ 8] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(1) |
	       PKT_ID2(MIPI_DSI_H_SYNC_END) | PKT_LEN2(0) |
	       PKT_LP,
	[ 9] = 0,
	[10] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(1) |
	       PKT_ID2(MIPI_DSI_H_SYNC_END) | PKT_LEN2(0),
	[11] = PKT_ID0(MIPI_DSI_BLANKING_PACKET) | PKT_LEN0(2) |
	       PKT_ID1(MIPI_DSI_PACKED_PIXEL_STREAM_24) | PKT_LEN1(3) |
	       PKT_ID2(MIPI_DSI_BLANKING_PACKET) | PKT_LEN2(4),
};

/*
 * non-burst mode with sync events
 */
static const u32 pkt_seq_video_non_burst_sync_events[NUM_PKT_SEQ] = {
	[ 0] = PKT_ID0(MIPI_DSI_V_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_END_OF_TRANSMISSION) | PKT_LEN1(7) |
	       PKT_LP,
	[ 1] = 0,
	[ 2] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_END_OF_TRANSMISSION) | PKT_LEN1(7) |
	       PKT_LP,
	[ 3] = 0,
	[ 4] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_END_OF_TRANSMISSION) | PKT_LEN1(7) |
	       PKT_LP,
	[ 5] = 0,
	[ 6] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(2) |
	       PKT_ID2(MIPI_DSI_PACKED_PIXEL_STREAM_24) | PKT_LEN2(3),
	[ 7] = PKT_ID0(MIPI_DSI_BLANKING_PACKET) | PKT_LEN0(4),
	[ 8] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_END_OF_TRANSMISSION) | PKT_LEN1(7) |
	       PKT_LP,
	[ 9] = 0,
	[10] = PKT_ID0(MIPI_DSI_H_SYNC_START) | PKT_LEN0(0) |
	       PKT_ID1(MIPI_DSI_BLANKING_PACKET) | PKT_LEN1(2) |
	       PKT_ID2(MIPI_DSI_PACKED_PIXEL_STREAM_24) | PKT_LEN2(3),
	[11] = PKT_ID0(MIPI_DSI_BLANKING_PACKET) | PKT_LEN0(4),
};

static const u32 pkt_seq_command_mode[NUM_PKT_SEQ] = {
	[ 0] = 0,
	[ 1] = 0,
	[ 2] = 0,
	[ 3] = 0,
	[ 4] = 0,
	[ 5] = 0,
	[ 6] = PKT_ID0(MIPI_DSI_DCS_LONG_WRITE) | PKT_LEN0(3) | PKT_LP,
	[ 7] = 0,
	[ 8] = 0,
	[ 9] = 0,
	[10] = PKT_ID0(MIPI_DSI_DCS_LONG_WRITE) | PKT_LEN0(5) | PKT_LP,
	[11] = 0,
};

static void tegra_dsi_set_phy_timing(struct tegra_dsi *dsi,
				     unsigned long period,
				     const struct mipi_dphy_timing *timing)
{
	u32 value;

	value = DSI_TIMING_FIELD(timing->hsexit, period, 1) << 24 |
		DSI_TIMING_FIELD(timing->hstrail, period, 0) << 16 |
		DSI_TIMING_FIELD(timing->hszero, period, 3) << 8 |
		DSI_TIMING_FIELD(timing->hsprepare, period, 1);
	tegra_dsi_writel(dsi, value, DSI_PHY_TIMING_0);

	value = DSI_TIMING_FIELD(timing->clktrail, period, 1) << 24 |
		DSI_TIMING_FIELD(timing->clkpost, period, 1) << 16 |
		DSI_TIMING_FIELD(timing->clkzero, period, 1) << 8 |
		DSI_TIMING_FIELD(timing->lpx, period, 1);
	tegra_dsi_writel(dsi, value, DSI_PHY_TIMING_1);

	value = DSI_TIMING_FIELD(timing->clkprepare, period, 1) << 16 |
		DSI_TIMING_FIELD(timing->clkpre, period, 1) << 8 |
		DSI_TIMING_FIELD(0xff * period, period, 0) << 0;
	tegra_dsi_writel(dsi, value, DSI_PHY_TIMING_2);

	value = DSI_TIMING_FIELD(timing->taget, period, 1) << 16 |
		DSI_TIMING_FIELD(timing->tasure, period, 1) << 8 |
		DSI_TIMING_FIELD(timing->tago, period, 1);
	tegra_dsi_writel(dsi, value, DSI_BTA_TIMING);

	if (dsi->slave)
		tegra_dsi_set_phy_timing(dsi->slave, period, timing);
}

static int tegra_dsi_get_muldiv(enum mipi_dsi_pixel_format format,
				unsigned int *mulp, unsigned int *divp)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB666_PACKED:
	case MIPI_DSI_FMT_RGB888:
		*mulp = 3;
		*divp = 1;
		break;

	case MIPI_DSI_FMT_RGB565:
		*mulp = 2;
		*divp = 1;
		break;

	case MIPI_DSI_FMT_RGB666:
		*mulp = 9;
		*divp = 4;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra_dsi_get_format(enum mipi_dsi_pixel_format format,
				enum tegra_dsi_format *fmt)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB888:
		*fmt = TEGRA_DSI_FORMAT_24P;
		break;

	case MIPI_DSI_FMT_RGB666:
		*fmt = TEGRA_DSI_FORMAT_18NP;
		break;

	case MIPI_DSI_FMT_RGB666_PACKED:
		*fmt = TEGRA_DSI_FORMAT_18P;
		break;

	case MIPI_DSI_FMT_RGB565:
		*fmt = TEGRA_DSI_FORMAT_16P;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void tegra_dsi_ganged_enable(struct tegra_dsi *dsi, unsigned int start,
				    unsigned int size)
{
	u32 value;

	tegra_dsi_writel(dsi, start, DSI_GANGED_MODE_START);
	tegra_dsi_writel(dsi, size << 16 | size, DSI_GANGED_MODE_SIZE);

	value = DSI_GANGED_MODE_CONTROL_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_GANGED_MODE_CONTROL);
}

static void tegra_dsi_enable(struct tegra_dsi *dsi)
{
	u32 value;

	value = tegra_dsi_readl(dsi, DSI_POWER_CONTROL);
	value |= DSI_POWER_CONTROL_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_POWER_CONTROL);

	if (dsi->slave)
		tegra_dsi_enable(dsi->slave);
}

static unsigned int tegra_dsi_get_lanes(struct tegra_dsi *dsi)
{
	if (dsi->master)
		return dsi->master->lanes + dsi->lanes;

	if (dsi->slave)
		return dsi->lanes + dsi->slave->lanes;

	return dsi->lanes;
}

static void tegra_dsi_configure(struct tegra_dsi *dsi, unsigned int pipe,
				const struct drm_display_mode *mode)
{
	unsigned int hact, hsw, hbp, hfp, i, mul, div;
	struct tegra_dsi_state *state;
	const u32 *pkt_seq;
	u32 value;

	/* XXX: pass in state into this function? */
	if (dsi->master)
		state = tegra_dsi_get_state(dsi->master);
	else
		state = tegra_dsi_get_state(dsi);

	mul = state->mul;
	div = state->div;

	if (dsi->flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
		DRM_DEBUG_KMS("Non-burst video mode with sync pulses\n");
		pkt_seq = pkt_seq_video_non_burst_sync_pulses;
	} else if (dsi->flags & MIPI_DSI_MODE_VIDEO) {
		DRM_DEBUG_KMS("Non-burst video mode with sync events\n");
		pkt_seq = pkt_seq_video_non_burst_sync_events;
	} else {
		DRM_DEBUG_KMS("Command mode\n");
		pkt_seq = pkt_seq_command_mode;
	}

	value = DSI_CONTROL_CHANNEL(0) |
		DSI_CONTROL_FORMAT(state->format) |
		DSI_CONTROL_LANES(dsi->lanes - 1) |
		DSI_CONTROL_SOURCE(pipe);
	tegra_dsi_writel(dsi, value, DSI_CONTROL);

	tegra_dsi_writel(dsi, dsi->video_fifo_depth, DSI_MAX_THRESHOLD);

	value = DSI_HOST_CONTROL_HS;
	tegra_dsi_writel(dsi, value, DSI_HOST_CONTROL);

	value = tegra_dsi_readl(dsi, DSI_CONTROL);

	if (dsi->flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		value |= DSI_CONTROL_HS_CLK_CTRL;

	value &= ~DSI_CONTROL_TX_TRIG(3);

	/* enable DCS commands for command mode */
	if (dsi->flags & MIPI_DSI_MODE_VIDEO)
		value &= ~DSI_CONTROL_DCS_ENABLE;
	else
		value |= DSI_CONTROL_DCS_ENABLE;

	value |= DSI_CONTROL_VIDEO_ENABLE;
	value &= ~DSI_CONTROL_HOST_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_CONTROL);

	for (i = 0; i < NUM_PKT_SEQ; i++)
		tegra_dsi_writel(dsi, pkt_seq[i], DSI_PKT_SEQ_0_LO + i);

	if (dsi->flags & MIPI_DSI_MODE_VIDEO) {
		/* horizontal active pixels */
		hact = mode->hdisplay * mul / div;

		/* horizontal sync width */
		hsw = (mode->hsync_end - mode->hsync_start) * mul / div;

		/* horizontal back porch */
		hbp = (mode->htotal - mode->hsync_end) * mul / div;

		if ((dsi->flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) == 0)
			hbp += hsw;

		/* horizontal front porch */
		hfp = (mode->hsync_start - mode->hdisplay) * mul / div;

		/* subtract packet overhead */
		hsw -= 10;
		hbp -= 14;
		hfp -= 8;

		tegra_dsi_writel(dsi, hsw << 16 | 0, DSI_PKT_LEN_0_1);
		tegra_dsi_writel(dsi, hact << 16 | hbp, DSI_PKT_LEN_2_3);
		tegra_dsi_writel(dsi, hfp, DSI_PKT_LEN_4_5);
		tegra_dsi_writel(dsi, 0x0f0f << 16, DSI_PKT_LEN_6_7);

		/* set SOL delay (for non-burst mode only) */
		tegra_dsi_writel(dsi, 8 * mul / div, DSI_SOL_DELAY);

		/* TODO: implement ganged mode */
	} else {
		u16 bytes;

		if (dsi->master || dsi->slave) {
			/*
			 * For ganged mode, assume symmetric left-right mode.
			 */
			bytes = 1 + (mode->hdisplay / 2) * mul / div;
		} else {
			/* 1 byte (DCS command) + pixel data */
			bytes = 1 + mode->hdisplay * mul / div;
		}

		tegra_dsi_writel(dsi, 0, DSI_PKT_LEN_0_1);
		tegra_dsi_writel(dsi, bytes << 16, DSI_PKT_LEN_2_3);
		tegra_dsi_writel(dsi, bytes << 16, DSI_PKT_LEN_4_5);
		tegra_dsi_writel(dsi, 0, DSI_PKT_LEN_6_7);

		value = MIPI_DCS_WRITE_MEMORY_START << 8 |
			MIPI_DCS_WRITE_MEMORY_CONTINUE;
		tegra_dsi_writel(dsi, value, DSI_DCS_CMDS);

		/* set SOL delay */
		if (dsi->master || dsi->slave) {
			unsigned long delay, bclk, bclk_ganged;
			unsigned int lanes = state->lanes;

			/* SOL to valid, valid to FIFO and FIFO write delay */
			delay = 4 + 4 + 2;
			delay = DIV_ROUND_UP(delay * mul, div * lanes);
			/* FIFO read delay */
			delay = delay + 6;

			bclk = DIV_ROUND_UP(mode->htotal * mul, div * lanes);
			bclk_ganged = DIV_ROUND_UP(bclk * lanes / 2, lanes);
			value = bclk - bclk_ganged + delay + 20;
		} else {
			/* TODO: revisit for non-ganged mode */
			value = 8 * mul / div;
		}

		tegra_dsi_writel(dsi, value, DSI_SOL_DELAY);
	}

	if (dsi->slave) {
		tegra_dsi_configure(dsi->slave, pipe, mode);

		/*
		 * TODO: Support modes other than symmetrical left-right
		 * split.
		 */
		tegra_dsi_ganged_enable(dsi, 0, mode->hdisplay / 2);
		tegra_dsi_ganged_enable(dsi->slave, mode->hdisplay / 2,
					mode->hdisplay / 2);
	}
}

static int tegra_dsi_wait_idle(struct tegra_dsi *dsi, unsigned long timeout)
{
	u32 value;

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		value = tegra_dsi_readl(dsi, DSI_STATUS);
		if (value & DSI_STATUS_IDLE)
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static void tegra_dsi_video_disable(struct tegra_dsi *dsi)
{
	u32 value;

	value = tegra_dsi_readl(dsi, DSI_CONTROL);
	value &= ~DSI_CONTROL_VIDEO_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_CONTROL);

	if (dsi->slave)
		tegra_dsi_video_disable(dsi->slave);
}

static void tegra_dsi_ganged_disable(struct tegra_dsi *dsi)
{
	tegra_dsi_writel(dsi, 0, DSI_GANGED_MODE_START);
	tegra_dsi_writel(dsi, 0, DSI_GANGED_MODE_SIZE);
	tegra_dsi_writel(dsi, 0, DSI_GANGED_MODE_CONTROL);
}

static void tegra_dsi_set_timeout(struct tegra_dsi *dsi, unsigned long bclk,
				  unsigned int vrefresh)
{
	unsigned int timeout;
	u32 value;

	/* one frame high-speed transmission timeout */
	timeout = (bclk / vrefresh) / 512;
	value = DSI_TIMEOUT_LRX(0x2000) | DSI_TIMEOUT_HTX(timeout);
	tegra_dsi_writel(dsi, value, DSI_TIMEOUT_0);

	/* 2 ms peripheral timeout for panel */
	timeout = 2 * bclk / 512 * 1000;
	value = DSI_TIMEOUT_PR(timeout) | DSI_TIMEOUT_TA(0x2000);
	tegra_dsi_writel(dsi, value, DSI_TIMEOUT_1);

	value = DSI_TALLY_TA(0) | DSI_TALLY_LRX(0) | DSI_TALLY_HTX(0);
	tegra_dsi_writel(dsi, value, DSI_TO_TALLY);

	if (dsi->slave)
		tegra_dsi_set_timeout(dsi->slave, bclk, vrefresh);
}

static void tegra_dsi_disable(struct tegra_dsi *dsi)
{
	u32 value;

	if (dsi->slave) {
		tegra_dsi_ganged_disable(dsi->slave);
		tegra_dsi_ganged_disable(dsi);
	}

	value = tegra_dsi_readl(dsi, DSI_POWER_CONTROL);
	value &= ~DSI_POWER_CONTROL_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_POWER_CONTROL);

	if (dsi->slave)
		tegra_dsi_disable(dsi->slave);

	usleep_range(5000, 10000);
}

static void tegra_dsi_soft_reset(struct tegra_dsi *dsi)
{
	u32 value;

	value = tegra_dsi_readl(dsi, DSI_POWER_CONTROL);
	value &= ~DSI_POWER_CONTROL_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_POWER_CONTROL);

	usleep_range(300, 1000);

	value = tegra_dsi_readl(dsi, DSI_POWER_CONTROL);
	value |= DSI_POWER_CONTROL_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_POWER_CONTROL);

	usleep_range(300, 1000);

	value = tegra_dsi_readl(dsi, DSI_TRIGGER);
	if (value)
		tegra_dsi_writel(dsi, 0, DSI_TRIGGER);

	if (dsi->slave)
		tegra_dsi_soft_reset(dsi->slave);
}

static void tegra_dsi_connector_reset(struct drm_connector *connector)
{
	struct tegra_dsi_state *state;

	kfree(connector->state);
	connector->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		connector->state = &state->base;
}

static struct drm_connector_state *
tegra_dsi_connector_duplicate_state(struct drm_connector *connector)
{
	struct tegra_dsi_state *state = to_dsi_state(connector->state);
	struct tegra_dsi_state *copy;

	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (!copy)
		return NULL;

	return &copy->base;
}

static const struct drm_connector_funcs tegra_dsi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = tegra_dsi_connector_reset,
	.detect = tegra_output_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = tegra_output_connector_destroy,
	.atomic_duplicate_state = tegra_dsi_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status
tegra_dsi_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_connector_helper_funcs tegra_dsi_connector_helper_funcs = {
	.get_modes = tegra_output_connector_get_modes,
	.mode_valid = tegra_dsi_connector_mode_valid,
	.best_encoder = tegra_output_connector_best_encoder,
};

static const struct drm_encoder_funcs tegra_dsi_encoder_funcs = {
	.destroy = tegra_output_encoder_destroy,
};

static void tegra_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_dsi *dsi = to_dsi(output);
	u32 value;
	int err;

	if (output->panel)
		drm_panel_disable(output->panel);

	tegra_dsi_video_disable(dsi);

	/*
	 * The following accesses registers of the display controller, so make
	 * sure it's only executed when the output is attached to one.
	 */
	if (dc) {
		value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
		value &= ~DSI_ENABLE;
		tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

		tegra_dc_commit(dc);
	}

	err = tegra_dsi_wait_idle(dsi, 100);
	if (err < 0)
		dev_dbg(dsi->dev, "failed to idle DSI: %d\n", err);

	tegra_dsi_soft_reset(dsi);

	if (output->panel)
		drm_panel_unprepare(output->panel);

	tegra_dsi_disable(dsi);

	return;
}

static void tegra_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dc *dc = to_tegra_dc(encoder->crtc);
	struct tegra_dsi *dsi = to_dsi(output);
	struct tegra_dsi_state *state;
	u32 value;

	state = tegra_dsi_get_state(dsi);

	tegra_dsi_set_timeout(dsi, state->bclk, state->vrefresh);

	/*
	 * The D-PHY timing fields are expressed in byte-clock cycles, so
	 * multiply the period by 8.
	 */
	tegra_dsi_set_phy_timing(dsi, state->period * 8, &state->timing);

	if (output->panel)
		drm_panel_prepare(output->panel);

	tegra_dsi_configure(dsi, dc->pipe, mode);

	/* enable display controller */
	value = tegra_dc_readl(dc, DC_DISP_DISP_WIN_OPTIONS);
	value |= DSI_ENABLE;
	tegra_dc_writel(dc, value, DC_DISP_DISP_WIN_OPTIONS);

	tegra_dc_commit(dc);

	/* enable DSI controller */
	tegra_dsi_enable(dsi);

	if (output->panel)
		drm_panel_enable(output->panel);

	return;
}

static int
tegra_dsi_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct tegra_output *output = encoder_to_output(encoder);
	struct tegra_dsi_state *state = to_dsi_state(conn_state);
	struct tegra_dc *dc = to_tegra_dc(conn_state->crtc);
	struct tegra_dsi *dsi = to_dsi(output);
	unsigned int scdiv;
	unsigned long plld;
	int err;

	state->pclk = crtc_state->mode.clock * 1000;

	err = tegra_dsi_get_muldiv(dsi->format, &state->mul, &state->div);
	if (err < 0)
		return err;

	state->lanes = tegra_dsi_get_lanes(dsi);

	err = tegra_dsi_get_format(dsi->format, &state->format);
	if (err < 0)
		return err;

	state->vrefresh = drm_mode_vrefresh(&crtc_state->mode);

	/* compute byte clock */
	state->bclk = (state->pclk * state->mul) / (state->div * state->lanes);

	DRM_DEBUG_KMS("mul: %u, div: %u, lanes: %u\n", state->mul, state->div,
		      state->lanes);
	DRM_DEBUG_KMS("format: %u, vrefresh: %u\n", state->format,
		      state->vrefresh);
	DRM_DEBUG_KMS("bclk: %lu\n", state->bclk);

	/*
	 * Compute bit clock and round up to the next MHz.
	 */
	plld = DIV_ROUND_UP(state->bclk * 8, USEC_PER_SEC) * USEC_PER_SEC;
	state->period = DIV_ROUND_CLOSEST(NSEC_PER_SEC, plld);

	err = mipi_dphy_timing_get_default(&state->timing, state->period);
	if (err < 0)
		return err;

	err = mipi_dphy_timing_validate(&state->timing, state->period);
	if (err < 0) {
		dev_err(dsi->dev, "failed to validate D-PHY timing: %d\n", err);
		return err;
	}

	/*
	 * We divide the frequency by two here, but we make up for that by
	 * setting the shift clock divider (further below) to half of the
	 * correct value.
	 */
	plld /= 2;

	/*
	 * Derive pixel clock from bit clock using the shift clock divider.
	 * Note that this is only half of what we would expect, but we need
	 * that to make up for the fact that we divided the bit clock by a
	 * factor of two above.
	 *
	 * It's not clear exactly why this is necessary, but the display is
	 * not working properly otherwise. Perhaps the PLLs cannot generate
	 * frequencies sufficiently high.
	 */
	scdiv = ((8 * state->mul) / (state->div * state->lanes)) - 2;

	err = tegra_dc_state_setup_clock(dc, crtc_state, dsi->clk_parent,
					 plld, scdiv);
	if (err < 0) {
		dev_err(output->dev, "failed to setup CRTC state: %d\n", err);
		return err;
	}

	return err;
}

static const struct drm_encoder_helper_funcs tegra_dsi_encoder_helper_funcs = {
	.disable = tegra_dsi_encoder_disable,
	.enable = tegra_dsi_encoder_enable,
	.atomic_check = tegra_dsi_encoder_atomic_check,
};

static int tegra_dsi_pad_enable(struct tegra_dsi *dsi)
{
	u32 value;

	value = DSI_PAD_CONTROL_VS1_PULLDN(0) | DSI_PAD_CONTROL_VS1_PDIO(0);
	tegra_dsi_writel(dsi, value, DSI_PAD_CONTROL_0);

	return 0;
}

static int tegra_dsi_pad_calibrate(struct tegra_dsi *dsi)
{
	u32 value;

	tegra_dsi_writel(dsi, 0, DSI_PAD_CONTROL_0);
	tegra_dsi_writel(dsi, 0, DSI_PAD_CONTROL_1);
	tegra_dsi_writel(dsi, 0, DSI_PAD_CONTROL_2);
	tegra_dsi_writel(dsi, 0, DSI_PAD_CONTROL_3);
	tegra_dsi_writel(dsi, 0, DSI_PAD_CONTROL_4);

	/* start calibration */
	tegra_dsi_pad_enable(dsi);

	value = DSI_PAD_SLEW_UP(0x7) | DSI_PAD_SLEW_DN(0x7) |
		DSI_PAD_LP_UP(0x1) | DSI_PAD_LP_DN(0x1) |
		DSI_PAD_OUT_CLK(0x0);
	tegra_dsi_writel(dsi, value, DSI_PAD_CONTROL_2);

	value = DSI_PAD_PREEMP_PD_CLK(0x3) | DSI_PAD_PREEMP_PU_CLK(0x3) |
		DSI_PAD_PREEMP_PD(0x03) | DSI_PAD_PREEMP_PU(0x3);
	tegra_dsi_writel(dsi, value, DSI_PAD_CONTROL_3);

	return tegra_mipi_calibrate(dsi->mipi);
}

static int tegra_dsi_init(struct host1x_client *client)
{
	struct drm_device *drm = dev_get_drvdata(client->parent);
	struct tegra_dsi *dsi = host1x_client_to_dsi(client);
	int err;

	reset_control_deassert(dsi->rst);

	err = tegra_dsi_pad_calibrate(dsi);
	if (err < 0) {
		dev_err(dsi->dev, "MIPI calibration failed: %d\n", err);
		goto reset;
	}

	/* Gangsters must not register their own outputs. */
	if (!dsi->master) {
		dsi->output.dev = client->dev;

		drm_connector_init(drm, &dsi->output.connector,
				   &tegra_dsi_connector_funcs,
				   DRM_MODE_CONNECTOR_DSI);
		drm_connector_helper_add(&dsi->output.connector,
					 &tegra_dsi_connector_helper_funcs);
		dsi->output.connector.dpms = DRM_MODE_DPMS_OFF;

		drm_encoder_init(drm, &dsi->output.encoder,
				 &tegra_dsi_encoder_funcs,
				 DRM_MODE_ENCODER_DSI);
		drm_encoder_helper_add(&dsi->output.encoder,
				       &tegra_dsi_encoder_helper_funcs);

		drm_mode_connector_attach_encoder(&dsi->output.connector,
						  &dsi->output.encoder);
		drm_connector_register(&dsi->output.connector);

		err = tegra_output_init(drm, &dsi->output);
		if (err < 0) {
			dev_err(client->dev,
				"failed to initialize output: %d\n",
				err);
			goto reset;
		}

		dsi->output.encoder.possible_crtcs = 0x3;
	}

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_dsi_debugfs_init(dsi, drm->primary);
		if (err < 0)
			dev_err(dsi->dev, "debugfs setup failed: %d\n", err);
	}

	return 0;

reset:
	reset_control_assert(dsi->rst);
	return err;
}

static int tegra_dsi_exit(struct host1x_client *client)
{
	struct tegra_dsi *dsi = host1x_client_to_dsi(client);

	tegra_output_exit(&dsi->output);

	if (IS_ENABLED(CONFIG_DEBUG_FS))
		tegra_dsi_debugfs_exit(dsi);

	reset_control_assert(dsi->rst);

	return 0;
}

static const struct host1x_client_ops dsi_client_ops = {
	.init = tegra_dsi_init,
	.exit = tegra_dsi_exit,
};

static int tegra_dsi_setup_clocks(struct tegra_dsi *dsi)
{
	struct clk *parent;
	int err;

	parent = clk_get_parent(dsi->clk);
	if (!parent)
		return -EINVAL;

	err = clk_set_parent(parent, dsi->clk_parent);
	if (err < 0)
		return err;

	return 0;
}

static const char * const error_report[16] = {
	"SoT Error",
	"SoT Sync Error",
	"EoT Sync Error",
	"Escape Mode Entry Command Error",
	"Low-Power Transmit Sync Error",
	"Peripheral Timeout Error",
	"False Control Error",
	"Contention Detected",
	"ECC Error, single-bit",
	"ECC Error, multi-bit",
	"Checksum Error",
	"DSI Data Type Not Recognized",
	"DSI VC ID Invalid",
	"Invalid Transmission Length",
	"Reserved",
	"DSI Protocol Violation",
};

static ssize_t tegra_dsi_read_response(struct tegra_dsi *dsi,
				       const struct mipi_dsi_msg *msg,
				       size_t count)
{
	u8 *rx = msg->rx_buf;
	unsigned int i, j, k;
	size_t size = 0;
	u16 errors;
	u32 value;

	/* read and parse packet header */
	value = tegra_dsi_readl(dsi, DSI_RD_DATA);

	switch (value & 0x3f) {
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		errors = (value >> 8) & 0xffff;
		dev_dbg(dsi->dev, "Acknowledge and error report: %04x\n",
			errors);
		for (i = 0; i < ARRAY_SIZE(error_report); i++)
			if (errors & BIT(i))
				dev_dbg(dsi->dev, "  %2u: %s\n", i,
					error_report[i]);
		break;

	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
		rx[0] = (value >> 8) & 0xff;
		size = 1;
		break;

	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		rx[0] = (value >>  8) & 0xff;
		rx[1] = (value >> 16) & 0xff;
		size = 2;
		break;

	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		size = ((value >> 8) & 0xff00) | ((value >> 8) & 0xff);
		break;

	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
		size = ((value >> 8) & 0xff00) | ((value >> 8) & 0xff);
		break;

	default:
		dev_err(dsi->dev, "unhandled response type: %02x\n",
			value & 0x3f);
		return -EPROTO;
	}

	size = min(size, msg->rx_len);

	if (msg->rx_buf && size > 0) {
		for (i = 0, j = 0; i < count - 1; i++, j += 4) {
			u8 *rx = msg->rx_buf + j;

			value = tegra_dsi_readl(dsi, DSI_RD_DATA);

			for (k = 0; k < 4 && (j + k) < msg->rx_len; k++)
				rx[j + k] = (value >> (k << 3)) & 0xff;
		}
	}

	return size;
}

static int tegra_dsi_transmit(struct tegra_dsi *dsi, unsigned long timeout)
{
	tegra_dsi_writel(dsi, DSI_TRIGGER_HOST, DSI_TRIGGER);

	timeout = jiffies + msecs_to_jiffies(timeout);

	while (time_before(jiffies, timeout)) {
		u32 value = tegra_dsi_readl(dsi, DSI_TRIGGER);
		if ((value & DSI_TRIGGER_HOST) == 0)
			return 0;

		usleep_range(1000, 2000);
	}

	DRM_DEBUG_KMS("timeout waiting for transmission to complete\n");
	return -ETIMEDOUT;
}

static int tegra_dsi_wait_for_response(struct tegra_dsi *dsi,
				       unsigned long timeout)
{
	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		u32 value = tegra_dsi_readl(dsi, DSI_STATUS);
		u8 count = value & 0x1f;

		if (count > 0)
			return count;

		usleep_range(1000, 2000);
	}

	DRM_DEBUG_KMS("peripheral returned no data\n");
	return -ETIMEDOUT;
}

static void tegra_dsi_writesl(struct tegra_dsi *dsi, unsigned long offset,
			      const void *buffer, size_t size)
{
	const u8 *buf = buffer;
	size_t i, j;
	u32 value;

	for (j = 0; j < size; j += 4) {
		value = 0;

		for (i = 0; i < 4 && j + i < size; i++)
			value |= buf[j + i] << (i << 3);

		tegra_dsi_writel(dsi, value, DSI_WR_DATA);
	}
}

static ssize_t tegra_dsi_host_transfer(struct mipi_dsi_host *host,
				       const struct mipi_dsi_msg *msg)
{
	struct tegra_dsi *dsi = host_to_tegra(host);
	struct mipi_dsi_packet packet;
	const u8 *header;
	size_t count;
	ssize_t err;
	u32 value;

	err = mipi_dsi_create_packet(&packet, msg);
	if (err < 0)
		return err;

	header = packet.header;

	/* maximum FIFO depth is 1920 words */
	if (packet.size > dsi->video_fifo_depth * 4)
		return -ENOSPC;

	/* reset underflow/overflow flags */
	value = tegra_dsi_readl(dsi, DSI_STATUS);
	if (value & (DSI_STATUS_UNDERFLOW | DSI_STATUS_OVERFLOW)) {
		value = DSI_HOST_CONTROL_FIFO_RESET;
		tegra_dsi_writel(dsi, value, DSI_HOST_CONTROL);
		usleep_range(10, 20);
	}

	value = tegra_dsi_readl(dsi, DSI_POWER_CONTROL);
	value |= DSI_POWER_CONTROL_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_POWER_CONTROL);

	usleep_range(5000, 10000);

	value = DSI_HOST_CONTROL_CRC_RESET | DSI_HOST_CONTROL_TX_TRIG_HOST |
		DSI_HOST_CONTROL_CS | DSI_HOST_CONTROL_ECC;

	if ((msg->flags & MIPI_DSI_MSG_USE_LPM) == 0)
		value |= DSI_HOST_CONTROL_HS;

	/*
	 * The host FIFO has a maximum of 64 words, so larger transmissions
	 * need to use the video FIFO.
	 */
	if (packet.size > dsi->host_fifo_depth * 4)
		value |= DSI_HOST_CONTROL_FIFO_SEL;

	tegra_dsi_writel(dsi, value, DSI_HOST_CONTROL);

	/*
	 * For reads and messages with explicitly requested ACK, generate a
	 * BTA sequence after the transmission of the packet.
	 */
	if ((msg->flags & MIPI_DSI_MSG_REQ_ACK) ||
	    (msg->rx_buf && msg->rx_len > 0)) {
		value = tegra_dsi_readl(dsi, DSI_HOST_CONTROL);
		value |= DSI_HOST_CONTROL_PKT_BTA;
		tegra_dsi_writel(dsi, value, DSI_HOST_CONTROL);
	}

	value = DSI_CONTROL_LANES(0) | DSI_CONTROL_HOST_ENABLE;
	tegra_dsi_writel(dsi, value, DSI_CONTROL);

	/* write packet header, ECC is generated by hardware */
	value = header[2] << 16 | header[1] << 8 | header[0];
	tegra_dsi_writel(dsi, value, DSI_WR_DATA);

	/* write payload (if any) */
	if (packet.payload_length > 0)
		tegra_dsi_writesl(dsi, DSI_WR_DATA, packet.payload,
				  packet.payload_length);

	err = tegra_dsi_transmit(dsi, 250);
	if (err < 0)
		return err;

	if ((msg->flags & MIPI_DSI_MSG_REQ_ACK) ||
	    (msg->rx_buf && msg->rx_len > 0)) {
		err = tegra_dsi_wait_for_response(dsi, 250);
		if (err < 0)
			return err;

		count = err;

		value = tegra_dsi_readl(dsi, DSI_RD_DATA);
		switch (value) {
		case 0x84:
			/*
			dev_dbg(dsi->dev, "ACK\n");
			*/
			break;

		case 0x87:
			/*
			dev_dbg(dsi->dev, "ESCAPE\n");
			*/
			break;

		default:
			dev_err(dsi->dev, "unknown status: %08x\n", value);
			break;
		}

		if (count > 1) {
			err = tegra_dsi_read_response(dsi, msg, count);
			if (err < 0)
				dev_err(dsi->dev,
					"failed to parse response: %zd\n",
					err);
			else {
				/*
				 * For read commands, return the number of
				 * bytes returned by the peripheral.
				 */
				count = err;
			}
		}
	} else {
		/*
		 * For write commands, we have transmitted the 4-byte header
		 * plus the variable-length payload.
		 */
		count = 4 + packet.payload_length;
	}

	return count;
}

static int tegra_dsi_ganged_setup(struct tegra_dsi *dsi)
{
	struct clk *parent;
	int err;

	/* make sure both DSI controllers share the same PLL */
	parent = clk_get_parent(dsi->slave->clk);
	if (!parent)
		return -EINVAL;

	err = clk_set_parent(parent, dsi->clk_parent);
	if (err < 0)
		return err;

	return 0;
}

static int tegra_dsi_host_attach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *device)
{
	struct tegra_dsi *dsi = host_to_tegra(host);

	dsi->flags = device->mode_flags;
	dsi->format = device->format;
	dsi->lanes = device->lanes;

	if (dsi->slave) {
		int err;

		dev_dbg(dsi->dev, "attaching dual-channel device %s\n",
			dev_name(&device->dev));

		err = tegra_dsi_ganged_setup(dsi);
		if (err < 0) {
			dev_err(dsi->dev, "failed to set up ganged mode: %d\n",
				err);
			return err;
		}
	}

	/*
	 * Slaves don't have a panel associated with them, so they provide
	 * merely the second channel.
	 */
	if (!dsi->master) {
		struct tegra_output *output = &dsi->output;

		output->panel = of_drm_find_panel(device->dev.of_node);
		if (output->panel && output->connector.dev) {
			drm_panel_attach(output->panel, &output->connector);
			drm_helper_hpd_irq_event(output->connector.dev);
		}
	}

	return 0;
}

static int tegra_dsi_host_detach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *device)
{
	struct tegra_dsi *dsi = host_to_tegra(host);
	struct tegra_output *output = &dsi->output;

	if (output->panel && &device->dev == output->panel->dev) {
		output->panel = NULL;

		if (output->connector.dev)
			drm_helper_hpd_irq_event(output->connector.dev);
	}

	return 0;
}

static const struct mipi_dsi_host_ops tegra_dsi_host_ops = {
	.attach = tegra_dsi_host_attach,
	.detach = tegra_dsi_host_detach,
	.transfer = tegra_dsi_host_transfer,
};

static int tegra_dsi_ganged_probe(struct tegra_dsi *dsi)
{
	struct device_node *np;

	np = of_parse_phandle(dsi->dev->of_node, "nvidia,ganged-mode", 0);
	if (np) {
		struct platform_device *gangster = of_find_device_by_node(np);

		dsi->slave = platform_get_drvdata(gangster);
		of_node_put(np);

		if (!dsi->slave)
			return -EPROBE_DEFER;

		dsi->slave->master = dsi;
	}

	return 0;
}

static int tegra_dsi_probe(struct platform_device *pdev)
{
	struct tegra_dsi *dsi;
	struct resource *regs;
	int err;

	dsi = devm_kzalloc(&pdev->dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->output.dev = dsi->dev = &pdev->dev;
	dsi->video_fifo_depth = 1920;
	dsi->host_fifo_depth = 64;

	err = tegra_dsi_ganged_probe(dsi);
	if (err < 0)
		return err;

	err = tegra_output_probe(&dsi->output);
	if (err < 0)
		return err;

	dsi->output.connector.polled = DRM_CONNECTOR_POLL_HPD;

	/*
	 * Assume these values by default. When a DSI peripheral driver
	 * attaches to the DSI host, the parameters will be taken from
	 * the attached device.
	 */
	dsi->flags = MIPI_DSI_MODE_VIDEO;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	dsi->rst = devm_reset_control_get(&pdev->dev, "dsi");
	if (IS_ERR(dsi->rst))
		return PTR_ERR(dsi->rst);

	dsi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dsi->clk)) {
		dev_err(&pdev->dev, "cannot get DSI clock\n");
		err = PTR_ERR(dsi->clk);
		goto reset;
	}

	err = clk_prepare_enable(dsi->clk);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot enable DSI clock\n");
		goto reset;
	}

	dsi->clk_lp = devm_clk_get(&pdev->dev, "lp");
	if (IS_ERR(dsi->clk_lp)) {
		dev_err(&pdev->dev, "cannot get low-power clock\n");
		err = PTR_ERR(dsi->clk_lp);
		goto disable_clk;
	}

	err = clk_prepare_enable(dsi->clk_lp);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot enable low-power clock\n");
		goto disable_clk;
	}

	dsi->clk_parent = devm_clk_get(&pdev->dev, "parent");
	if (IS_ERR(dsi->clk_parent)) {
		dev_err(&pdev->dev, "cannot get parent clock\n");
		err = PTR_ERR(dsi->clk_parent);
		goto disable_clk_lp;
	}

	dsi->vdd = devm_regulator_get(&pdev->dev, "avdd-dsi-csi");
	if (IS_ERR(dsi->vdd)) {
		dev_err(&pdev->dev, "cannot get VDD supply\n");
		err = PTR_ERR(dsi->vdd);
		goto disable_clk_lp;
	}

	err = regulator_enable(dsi->vdd);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot enable VDD supply\n");
		goto disable_clk_lp;
	}

	err = tegra_dsi_setup_clocks(dsi);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot setup clocks\n");
		goto disable_vdd;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsi->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(dsi->regs)) {
		err = PTR_ERR(dsi->regs);
		goto disable_vdd;
	}

	dsi->mipi = tegra_mipi_request(&pdev->dev);
	if (IS_ERR(dsi->mipi)) {
		err = PTR_ERR(dsi->mipi);
		goto disable_vdd;
	}

	dsi->host.ops = &tegra_dsi_host_ops;
	dsi->host.dev = &pdev->dev;

	err = mipi_dsi_host_register(&dsi->host);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register DSI host: %d\n", err);
		goto mipi_free;
	}

	INIT_LIST_HEAD(&dsi->client.list);
	dsi->client.ops = &dsi_client_ops;
	dsi->client.dev = &pdev->dev;

	err = host1x_client_register(&dsi->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		goto unregister;
	}

	platform_set_drvdata(pdev, dsi);

	return 0;

unregister:
	mipi_dsi_host_unregister(&dsi->host);
mipi_free:
	tegra_mipi_free(dsi->mipi);
disable_vdd:
	regulator_disable(dsi->vdd);
disable_clk_lp:
	clk_disable_unprepare(dsi->clk_lp);
disable_clk:
	clk_disable_unprepare(dsi->clk);
reset:
	reset_control_assert(dsi->rst);
	return err;
}

static int tegra_dsi_remove(struct platform_device *pdev)
{
	struct tegra_dsi *dsi = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&dsi->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	tegra_output_remove(&dsi->output);

	mipi_dsi_host_unregister(&dsi->host);
	tegra_mipi_free(dsi->mipi);

	regulator_disable(dsi->vdd);
	clk_disable_unprepare(dsi->clk_lp);
	clk_disable_unprepare(dsi->clk);
	reset_control_assert(dsi->rst);

	return 0;
}

static const struct of_device_id tegra_dsi_of_match[] = {
	{ .compatible = "nvidia,tegra210-dsi", },
	{ .compatible = "nvidia,tegra132-dsi", },
	{ .compatible = "nvidia,tegra124-dsi", },
	{ .compatible = "nvidia,tegra114-dsi", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_dsi_of_match);

struct platform_driver tegra_dsi_driver = {
	.driver = {
		.name = "tegra-dsi",
		.of_match_table = tegra_dsi_of_match,
	},
	.probe = tegra_dsi_probe,
	.remove = tegra_dsi_remove,
};
