/*
 * drivers/video/tegra/dc/dc_priv.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DRIVERS_VIDEO_TEGRA_DC_DC_PRIV_H
#define __DRIVERS_VIDEO_TEGRA_DC_DC_PRIV_H

#include <linux/io.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include "../host/dev.h"

struct tegra_dc;

struct tegra_dc_blend {
	unsigned z[DC_N_WINDOWS];
	unsigned flags[DC_N_WINDOWS];
};

struct tegra_dc_out_ops {
	/* initialize output.  dc clocks are not on at this point */
	int (*init)(struct tegra_dc *dc);
	/* destroy output.  dc clocks are not on at this point */
	void (*destroy)(struct tegra_dc *dc);
	/* detect connected display.  can sleep.*/
	bool (*detect)(struct tegra_dc *dc);
	/* enable output.  dc clocks are on at this point */
	void (*enable)(struct tegra_dc *dc);
	/* disable output.  dc clocks are on at this point */
	void (*disable)(struct tegra_dc *dc);

	/* suspend output.  dc clocks are on at this point */
	void (*suspend)(struct tegra_dc *dc);
	/* resume output.  dc clocks are on at this point */
	void (*resume)(struct tegra_dc *dc);
};

struct tegra_dc {
	struct list_head		list;

	struct nvhost_device		*ndev;
	struct tegra_dc_platform_data	*pdata;

	struct resource			*base_res;
	void __iomem			*base;
	int				irq;

	struct clk			*clk;
	struct clk			*emc_clk;

	bool				enabled;

	struct tegra_dc_out		*out;
	struct tegra_dc_out_ops		*out_ops;
	void				*out_data;

	struct tegra_dc_mode		mode;

	struct tegra_dc_win		windows[DC_N_WINDOWS];
	struct tegra_dc_blend		blend;
	int				n_windows;

	wait_queue_head_t		wq;

	struct mutex			lock;

	struct resource			*fb_mem;
	struct tegra_fb_info		*fb;

	u32				syncpt_id;
	u32				syncpt_min;
	u32				syncpt_max;

	unsigned long			underflow_mask;
	struct work_struct		reset_work;
};

static inline void tegra_dc_io_start(struct tegra_dc *dc)
{
	nvhost_module_busy(&dc->ndev->host->mod);
}

static inline void tegra_dc_io_end(struct tegra_dc *dc)
{
	nvhost_module_idle(&dc->ndev->host->mod);
}

static inline unsigned long tegra_dc_readl(struct tegra_dc *dc,
					   unsigned long reg)
{
	BUG_ON(!nvhost_module_powered(&dc->ndev->host->mod));
	return readl(dc->base + reg * 4);
}

static inline void tegra_dc_writel(struct tegra_dc *dc, unsigned long val,
				   unsigned long reg)
{
	BUG_ON(!nvhost_module_powered(&dc->ndev->host->mod));
	writel(val, dc->base + reg * 4);
}

static inline void _tegra_dc_write_table(struct tegra_dc *dc, const u32 *table,
					 unsigned len)
{
	int i;

	for (i = 0; i < len; i++)
		tegra_dc_writel(dc, table[i * 2 + 1], table[i * 2]);
}

#define tegra_dc_write_table(dc, table)		\
	_tegra_dc_write_table(dc, table, ARRAY_SIZE(table) / 2)

static inline void tegra_dc_set_outdata(struct tegra_dc *dc, void *data)
{
	dc->out_data = data;
}

static inline void *tegra_dc_get_outdata(struct tegra_dc *dc)
{
	return dc->out_data;
}

void tegra_dc_setup_clk(struct tegra_dc *dc, struct clk *clk);

extern struct tegra_dc_out_ops tegra_dc_rgb_ops;
extern struct tegra_dc_out_ops tegra_dc_hdmi_ops;

#endif
