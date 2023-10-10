/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Zhang Yubing <yubing.zhang@rock-chips.com>
 */

#ifndef _RKX110_X120_H
#define _RKX110_X120_H

#include <drm/drm_panel.h>
#include <dt-bindings/mfd/rockchip-serdes.h>
#include <linux/i2c.h>
#include <video/videomode.h>

#define MAX_PANEL 2
#define RK_SERDES_PASSTHROUGH_CNT	11

#define SERDES_VERSION_V0(type)		0x2201
#define SERDES_VERSION_V1(type)		(type ? 0x1200001 : 0x1100001)

#define SER_GRF_CHIP_ID			0x10800
#define DES_GRF_CHIP_ID			0x1010400
#define HIWORD_MASK(h, l)		(GENMASK(h, l) | GENMASK(h, l) << 16)

enum {
	SERDES_V0 = 0,
	SERDES_V1,
};

enum {
	LOCAL_MODE = 0,
	REMOTE_MODE,
};

enum {
	STREAM_DISPLAY = 0,
	STREAM_CAMERA,
};

enum {
	DEVICE_LOCAL = 0,
	DEVICE_REMOTE0,
	DEVICE_REMOTE1,
	DEVICE_MAX,
};

enum {
	PORT_REMOTE0,
	PORT_REMOTE1,
	PORT_REMOTE_MAX,
};

enum {
	LINK_LANE0,
	LINK_LANE1,
	LINK_LANE_DUAL,
};

enum combtx_phy_mode {
	COMBTX_PHY_MODE_GPIO,
	COMBTX_PHY_MODE_VIDEO_LVDS,
	COMBTX_PHY_MODE_VIDEO_MIPI,
	COMBTX_PHY_MODE_VIDEO_MINI_LVDS,
};

enum comb_phy_id {
	COMBPHY_0,
	COMBPHY_1,
	COMBPHY_MAX,
};

enum combrx_phy_mode {
	COMBRX_PHY_MODE_RGB,
	COMBRX_PHY_MODE_VIDEO_LVDS,
	COMBRX_PHY_MODE_VIDEO_MIPI,
	COMBRX_PHY_MODE_LVDS_CAMERA,
};

enum serdes_dsi_mode_flags {
	SERDES_MIPI_DSI_MODE_VIDEO = 1,
	SERDES_MIPI_DSI_MODE_VIDEO_BURST = 2,
	SERDES_MIPI_DSI_MODE_VIDEO_SYNC_PULSE = 4,
	SERDES_MIPI_DSI_MODE_VIDEO_HFP = 8,
	SERDES_MIPI_DSI_MODE_VIDEO_HBP = 16,
	SERDES_MIPI_DSI_MODE_EOT_PACKET = 32,
	SERDES_MIPI_DSI_CLOCK_NON_CONTINUOUS = 64,
	SERDES_MIPI_DSI_MODE_LPM = 128,
};

enum serdes_dsi_bus_format {
	SERDES_MIPI_DSI_FMT_RGB888,
	SERDES_MIPI_DSI_FMT_RGB666,
	SERDES_MIPI_DSI_FMT_RGB666_PACKED,
	SERDES_MIPI_DSI_FMT_RGB565,
};

enum serdes_frame_mode {
	SERDES_FRAME_NORMAL_MODE,
	SERDES_SP_PIXEL_INTERLEAVED,
	SERDES_SP_LEFT_RIGHT_SPLIT,
	SERDES_SP_LINE_INTERLEAVED,
};

struct configure_opts_combphy {
	unsigned int clk_miss;
	unsigned int clk_post;
	unsigned int clk_pre;
	unsigned int clk_prepare;
	unsigned int clk_settle;
	unsigned int clk_term_en;
	unsigned int clk_trail;
	unsigned int clk_zero;
	unsigned int d_term_en;
	unsigned int eot;
	unsigned int hs_exit;
	unsigned int hs_prepare;
	unsigned int hs_settle;
	unsigned int hs_skip;
	unsigned int hs_trail;
	unsigned int hs_zero;
	unsigned int init;
	unsigned int lpx;
	unsigned int ta_get;
	unsigned int ta_go;
	unsigned int ta_sure;
	unsigned int wakeup;
	unsigned long hs_clk_rate;
	unsigned long lp_clk_rate;
	unsigned char lanes;
};

struct rkx120_combtxphy {
	enum combtx_phy_mode mode;
	unsigned int flags;
	u8 ref_div;
	u16 fb_div;
	u8 rate_factor;
	u64 rate;
	struct configure_opts_combphy mipi_dphy_cfg;
};

struct rkx110_combrxphy {
	enum combrx_phy_mode mode;
	u64 rate;
	struct configure_opts_combphy mipi_dphy_cfg;
};

struct rkx120_dsi_tx {
	int bpp; /* 24/18/16*/
	enum serdes_dsi_bus_format bus_format;
	enum serdes_dsi_mode_flags mode_flags;
	struct videomode *vm;
	uint8_t channel;
	uint8_t lanes;
};

struct rkx110_dsi_rx {
	enum serdes_dsi_mode_flags mode_flags;
	struct videomode *vm;
	uint8_t channel;
	uint8_t lanes;
};

enum {
	OUTPUT,
	INPUT,
};

enum rk_serdes_rate {
	RATE_2GBPS_83M,
	RATE_4GBPS_83M,
	RATE_4GBPS_125M,
	RATE_4GBPS_250M,
	RATE_4_5GBPS_140M,
	RATE_4_8GBPS_150M,
	RATE_5GBPS_156M,
	RATE_6GBPS_187M,
};

enum {
	FDR_RATE_MODE,
	HDR_RATE_MODE,
	QDR_RATE_MODE,
};

enum rk_serdes_route_type {
	ROUTE_MULTI_SOURCE = BIT(0),
	ROUTE_MULTI_LANE = BIT(1),
	ROUTE_MULTI_CHANNEL = BIT(2),
	ROUTE_MULTI_REMOTE = BIT(3),
	ROUTE_MULTI_DSI_INPUT = BIT(20),
	ROUTE_MULTI_LVDS_INPUT = BIT(21),
	ROUTE_MULTI_MIRROR = BIT(22),
	ROUTE_MULTI_SPLIT = BIT(23),
};

struct rk_serdes_pma_pll {
	uint32_t rate_mode;
	uint32_t pll_refclk_div;
	uint32_t pll_div;
	uint32_t clk_div;
	bool pll_div4;
	bool pll_fck_vco_div2;
	bool force_init_en;
};

struct rk_serdes_reg {
	const char *name;
	uint32_t reg_base;
	uint32_t reg_len;
};

struct rk_serdes_route {
	u32 stream_type;
	struct videomode vm;
	enum serdes_frame_mode frame_mode;
	u32 local_port0;
	u32 local_port1;
	u32 remote0_port0;
	u32 remote0_port1;
	u32 remote1_port0;
	u32 remote1_port1;
};

struct rk_serdes_chip {
	bool is_remote;
	struct i2c_client *client;
	struct hwclk *hwclk;
	struct rk_serdes *serdes;
};

struct pattern_gen {
	const char *name;
	struct rk_serdes_chip *chip;
	u32 base;
	u32 link_src_reg;
	u8 link_src_offset;
};

struct rk_serdes_pt_pin {
	u32 bank;
	u32 pin;
	u32 incfgs;
	u32 outcfgs;
};

struct rk_serdes_pt {
	u32 en_reg;
	u32 en_mask;
	u32 en_val;
	u32 dir_reg;
	u32 dir_mask;
	u32 dir_val;
	int configs;
	struct rk_serdes_pt_pin pt_pins[4];
};

struct rk_serdes {
	struct device *dev;
	struct rk_serdes_chip chip[DEVICE_MAX];
	struct regulator *supply;
	struct gpio_desc *reset;
	struct gpio_desc *enable;

	/*
	 * Control by I2C-Debug
	 */
	bool rkx110_debug;
	bool rkx120_debug;

	enum rk_serdes_rate rate;

	struct dentry *debugfs_root;
	struct dentry *debugfs_local;
	struct dentry *debugfs_remote0;
	struct dentry *debugfs_remote1;
	struct dentry *debugfs_rate;

	struct videomode *vm;
	u32 stream_type;
	u32 version;
	u32 route_flag;
	u8 remote_nr;
	struct rkx110_combrxphy combrxphy;
	struct rkx110_dsi_rx dsi_rx;
	struct rkx120_combtxphy combtxphy;
	struct rkx120_dsi_tx dsi_tx;

	int (*i2c_read_reg)(struct i2c_client *client, u32 addr, u32 *value);
	int (*i2c_write_reg)(struct i2c_client *client, u32 addr, u32 value);
	int (*i2c_update_bits)(struct i2c_client *client, u32 reg, u32 mask, u32 val);
	int (*route_prepare)(struct rk_serdes *serdes, struct rk_serdes_route *route);
	int (*route_enable)(struct rk_serdes *serdes, struct rk_serdes_route *route);
	int (*route_disable)(struct rk_serdes *serdes, struct rk_serdes_route *route);
	int (*route_unprepare)(struct rk_serdes *serdes, struct rk_serdes_route *route);
	int (*set_hwpin)(struct rk_serdes *serdes, struct i2c_client *client,
			 int pintype, int bank, uint32_t mpins, uint32_t param);
};

struct cmd_ctrl_hdr {
	u8 dtype;       /* data type */
	u8 wait;        /* ms */
	u8 dlen;        /* payload len */
} __packed;

struct cmd_desc {
	struct cmd_ctrl_hdr dchdr;
	u8 *payload;
};

struct panel_cmds {
	u8 *buf;
	int blen;
	struct cmd_desc *cmds;
	int cmd_cnt;
};

struct rk_serdes_panel {
	struct drm_panel panel;
	struct device *dev;
	struct rk_serdes *parent;
	struct rk_serdes_route route;
	unsigned int bus_format;
	int link_mode;

	struct panel_cmds *on_cmds;
	struct panel_cmds *off_cmds;

	struct regulator *supply;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
};

int rkx110_linktx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route);
void rkx110_linktx_video_enable(struct rk_serdes *serdes, u8 dev_id, bool enable);
void rkx110_linktx_channel_enable(struct rk_serdes *serdes, u8 ch_id, u8 dev_id, bool enable);
void rkx120_linkrx_engine_enable(struct rk_serdes *serdes, u8 en_id, u8 dev_id, bool enable);
void rkx110_set_stream_source(struct rk_serdes *serdes, int local_port, u8 dev_id);
int rkx120_linkrx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 remote_id);
int rkx120_rgb_tx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 remote_id);
int rkx120_lvds_tx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 remote_id,
			  u8 phy_id);
void rkx120_linkrx_gpi_gpo_mux_cfg(struct rk_serdes *serdes, u32 mux, u8 remote_id);
void rkx110_linktx_gpi_gpo_mux_cfg(struct rk_serdes *serdes, u32 mux, u8 remote_id);
int rkx110_rgb_rx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route);
int rkx110_lvds_rx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route, int id);
void rkx110_debugfs_init(struct rk_serdes_chip *chip, struct dentry *dentry);
void rkx120_debugfs_init(struct rk_serdes_chip *chip, struct dentry *dentry);
void rkx110_pma_set_rate(struct rk_serdes *serdes, struct rk_serdes_pma_pll *pll,
			 u8 pcs_id, u8 dev_id);
void rkx120_pma_set_rate(struct rk_serdes *serdes, struct rk_serdes_pma_pll *pll,
			 u8 pcs_id, u8 dev_id);
void rkx110_pcs_enable(struct rk_serdes *serdes, bool enable, u8 pcs_id, u8 dev_id);
void rkx120_pcs_enable(struct rk_serdes *serdes, bool enable, u8 pcs_id, u8 dev_id);
void rkx110_ser_pma_enable(struct rk_serdes *serdes, bool enable, u8 pma_id, u8 remote_id);
void rkx120_des_pma_enable(struct rk_serdes *serdes, bool enable, u8 pma_id, u8 remote_id);
void rkx110_linktx_wait_link_ready(struct rk_serdes *serdes, u8 id);
void rkx120_linkrx_wait_link_ready(struct rk_serdes *serdes, u8 id);
void rkx110_x120_pattern_gen_debugfs_create_file(struct pattern_gen *pattern_gen,
						 struct rk_serdes_chip *chip,
						 struct dentry *dentry);
void rkx110_linktx_passthrough_cfg(struct rk_serdes *serdes, u32 client_id, u32 func_id,
				   bool is_rx);
void rkx120_linkrx_passthrough_cfg(struct rk_serdes *serdes, u32 client_id, u32 func_id,
				   bool is_rx);
#endif
