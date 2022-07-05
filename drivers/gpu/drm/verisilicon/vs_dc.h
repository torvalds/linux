/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DC_H__
#define __VS_DC_H__

#include <linux/version.h>
#include <linux/mm_types.h>

#include <drm/drm_modes.h>
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
#include <drm/drmP.h>
#endif

#include "vs_plane.h"
#include "vs_crtc.h"
#include "vs_dc_hw.h"
#include "vs_dc_dec.h"
#ifdef CONFIG_VERISILICON_MMU
#include "vs_dc_mmu.h"
#endif

struct vs_dc_funcs {
	void (*dump_enable)(struct device *dev, dma_addr_t addr,
				unsigned int pitch);
	void (*dump_disable)(struct device *dev);
};

struct vs_dc_plane {
	enum dc_hw_plane_id id;
};

struct vs_dc {
	struct vs_crtc		*crtc[DC_DISPLAY_NUM];
	struct dc_hw		hw;
#ifdef CONFIG_VERISILICON_DEC
	struct dc_dec400l	dec400l;
#endif

	void __iomem	*pmu_base;

	unsigned int	 pix_clk_rate; /* in KHz */

	struct reset_control *resets;
	struct clk_bulk_data *clks;
	int num_clks;


	bool			first_frame;

	struct vs_dc_plane planes[PLANE_NUM];

	const struct vs_dc_funcs *funcs;

	struct clk *cpu_axi;
	struct clk *axicfg0_axi;
	struct clk *disp_axi;
	struct clk *stg_axi;

	struct reset_control *cpu_axi_n;
	struct reset_control *axicfg0_axi_n;
	struct reset_control *apb_bus_n;
	struct reset_control *disp_axi_n;
	struct reset_control *stg_axi_n;

	struct clk *vout_src;
	struct clk *vout_axi;
	struct clk *ahb1;
	struct clk *vout_ahb;
	struct clk *hdmitx0_mclk;
	struct clk *bclk_mst;

	struct reset_control *rstn_vout_src;

	struct clk *dc8200_clk_pix0;
	struct clk *dc8200_clk_pix1;
	struct clk *dc8200_axi;
	struct clk *dc8200_core;
	struct clk *dc8200_ahb;

	struct reset_control *rstn_dc8200_axi;
	struct reset_control *rstn_dc8200_core;
	struct reset_control *rstn_dc8200_ahb;

	struct clk *vout_top_axi;
	struct clk *vout_top_lcd;

	struct clk *hdmitx0_pixelclk;
	struct clk *dc8200_pix0;
	struct clk *dc8200_clk_pix0_out;
	struct clk *dc8200_clk_pix1_out;

	struct regmap *dss_regmap;

};

extern struct platform_driver dc_platform_driver;
extern struct platform_driver starfive_dsi_platform_driver;
extern int init_seeed_panel(void);
extern void exit_seeed_panel(void);

#endif /* __VS_DC_H__ */
