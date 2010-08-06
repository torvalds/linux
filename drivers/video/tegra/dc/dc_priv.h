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

#include <linux/wait.h>
#include <linux/list.h>
#include <linux/io.h>

struct tegra_dc;

struct tegra_dc_out_ops {
	void (*init)(struct tegra_dc *dc);
};

struct tegra_dc {
	struct list_head		list;

	struct nvhost_device		*ndev;
	struct tegra_dc_platform_data	*pdata;

	struct resource			*base_res;
	void __iomem			*base;
	int				irq;

	struct clk			*clk;
	struct clk			*host1x_clk;

	struct tegra_dc_out		*out;
	struct tegra_dc_out_ops		*out_ops;

	struct tegra_dc_mode		*mode;

	struct tegra_dc_win		windows[DC_N_WINDOWS];
	int				n_windows;

	wait_queue_head_t		wq;

	spinlock_t			lock;

	struct resource			*fb_mem;
	struct tegra_fb_info		*fb;
};

static inline unsigned long tegra_dc_readl(struct tegra_dc *dc,
					   unsigned long reg)
{
	return readl(dc->base + reg * 4);
}

static inline void tegra_dc_writel(struct tegra_dc *dc, unsigned long val,
				   unsigned long reg)
{
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

extern struct tegra_dc_out_ops tegra_dc_rgb_ops;

#endif
