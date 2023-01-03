/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright: 2017 Cadence Design Systems, Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef __CDNS_DSI_H__
#define __CDNS_DSI_H__

#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/phy/phy.h>

struct clk;
struct reset_control;

struct cdns_dsi_output {
	struct mipi_dsi_device *dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	union phy_configure_opts phy_opts;
};

enum cdns_dsi_input_id {
	CDNS_SDI_INPUT,
	CDNS_DPI_INPUT,
	CDNS_DSC_INPUT,
};

struct cdns_dsi_cfg {
	unsigned int hfp;
	unsigned int hsa;
	unsigned int hbp;
	unsigned int hact;
	unsigned int htotal;
};

struct cdns_dsi_input {
	enum cdns_dsi_input_id id;
	struct drm_bridge bridge;
};

struct cdns_dsi {
	struct mipi_dsi_host base;
	void __iomem *regs;
	struct cdns_dsi_input input;
	struct cdns_dsi_output output;
	unsigned int direct_cmd_fifo_depth;
	unsigned int rx_fifo_depth;
	struct completion direct_cmd_comp;
	struct clk *dsi_p_clk;
	struct reset_control *dsi_p_rst;
	struct clk *dsi_sys_clk;
	bool link_initialized;
	bool phy_initialized;
	struct phy *dphy;
};

#endif /* !__CDNS_DSI_H__ */
