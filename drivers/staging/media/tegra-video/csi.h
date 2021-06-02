/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __TEGRA_CSI_H__
#define __TEGRA_CSI_H__

#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

/*
 * Each CSI brick supports max of 4 lanes that can be used as either
 * one x4 port using both CILA and CILB partitions of a CSI brick or can
 * be used as two x2 ports with one x2 from CILA and the other x2 from
 * CILB.
 */
#define CSI_PORTS_PER_BRICK	2
#define CSI_LANES_PER_BRICK	4

/* Maximum 2 CSI x4 ports can be ganged up for streaming */
#define GANG_PORTS_MAX	2

/* each CSI channel can have one sink and one source pads */
#define TEGRA_CSI_PADS_NUM	2

enum tegra_csi_cil_port {
	PORT_A = 0,
	PORT_B,
};

enum tegra_csi_block {
	CSI_CIL_AB = 0,
	CSI_CIL_CD,
	CSI_CIL_EF,
};

struct tegra_csi;

/**
 * struct tegra_csi_channel - Tegra CSI channel
 *
 * @list: list head for this entry
 * @subdev: V4L2 subdevice associated with this channel
 * @pads: media pads for the subdevice entity
 * @numpads: number of pads.
 * @csi: Tegra CSI device structure
 * @of_node: csi device tree node
 * @numgangports: number of immediate ports ganged up to meet the
 *             channel bus-width
 * @numlanes: number of lanes used per port
 * @csi_port_nums: CSI channel port numbers
 * @pg_mode: test pattern generator mode for channel
 * @format: active format of the channel
 * @framerate: active framerate for TPG
 * @h_blank: horizontal blanking for TPG active format
 * @v_blank: vertical blanking for TPG active format
 * @mipi: mipi device for corresponding csi channel pads
 * @pixel_rate: active pixel rate from the sensor on this channel
 */
struct tegra_csi_channel {
	struct list_head list;
	struct v4l2_subdev subdev;
	struct media_pad pads[TEGRA_CSI_PADS_NUM];
	unsigned int numpads;
	struct tegra_csi *csi;
	struct device_node *of_node;
	u8 numgangports;
	unsigned int numlanes;
	u8 csi_port_nums[GANG_PORTS_MAX];
	u8 pg_mode;
	struct v4l2_mbus_framefmt format;
	unsigned int framerate;
	unsigned int h_blank;
	unsigned int v_blank;
	struct tegra_mipi_device *mipi;
	unsigned int pixel_rate;
};

/**
 * struct tpg_framerate - Tegra CSI TPG framerate configuration
 *
 * @frmsize: frame resolution
 * @code: media bus format code
 * @h_blank: horizontal blanking used for TPG
 * @v_blank: vertical blanking interval used for TPG
 * @framerate: framerate achieved with the corresponding blanking intervals,
 *		format and resolution.
 */
struct tpg_framerate {
	struct v4l2_frmsize_discrete frmsize;
	u32 code;
	unsigned int h_blank;
	unsigned int v_blank;
	unsigned int framerate;
};

/**
 * struct tegra_csi_ops - Tegra CSI operations
 *
 * @csi_start_streaming: programs csi hardware to enable streaming.
 * @csi_stop_streaming: programs csi hardware to disable streaming.
 * @csi_err_recover: csi hardware block recovery in case of any capture errors
 *		due to missing source stream or due to improper csi input from
 *		the external source.
 */
struct tegra_csi_ops {
	int (*csi_start_streaming)(struct tegra_csi_channel *csi_chan);
	void (*csi_stop_streaming)(struct tegra_csi_channel *csi_chan);
	void (*csi_err_recover)(struct tegra_csi_channel *csi_chan);
};

/**
 * struct tegra_csi_soc - NVIDIA Tegra CSI SoC structure
 *
 * @ops: csi hardware operations
 * @csi_max_channels: supported max streaming channels
 * @clk_names: csi and cil clock names
 * @num_clks: total clocks count
 * @tpg_frmrate_table: csi tpg frame rate table with blanking intervals
 * @tpg_frmrate_table_size: size of frame rate table
 */
struct tegra_csi_soc {
	const struct tegra_csi_ops *ops;
	unsigned int csi_max_channels;
	const char * const *clk_names;
	unsigned int num_clks;
	const struct tpg_framerate *tpg_frmrate_table;
	unsigned int tpg_frmrate_table_size;
};

/**
 * struct tegra_csi - NVIDIA Tegra CSI device structure
 *
 * @dev: device struct
 * @client: host1x_client struct
 * @iomem: register base
 * @clks: clock for CSI and CIL
 * @soc: pointer to SoC data structure
 * @ops: csi operations
 * @csi_chans: list head for CSI channels
 */
struct tegra_csi {
	struct device *dev;
	struct host1x_client client;
	void __iomem *iomem;
	struct clk_bulk_data *clks;
	const struct tegra_csi_soc *soc;
	const struct tegra_csi_ops *ops;
	struct list_head csi_chans;
};

#if defined(CONFIG_ARCH_TEGRA_210_SOC)
extern const struct tegra_csi_soc tegra210_csi_soc;
#endif

void tegra_csi_error_recover(struct v4l2_subdev *subdev);
void tegra_csi_calc_settle_time(struct tegra_csi_channel *csi_chan,
				u8 csi_port_num,
				u8 *clk_settle_time,
				u8 *ths_settle_time);
#endif
