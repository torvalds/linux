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
};

extern struct platform_driver dc_platform_driver;
#endif /* __VS_DC_H__ */
