/*
 * arch/arm/mach-tegra/fuse.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
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

enum tegra_revision {
	TEGRA_REVISION_UNKNOWN = 0,
	TEGRA_REVISION_A02,
	TEGRA_REVISION_A03,
	TEGRA_REVISION_A03p,
	TEGRA_REVISION_MAX,
};

unsigned long long tegra_chip_uid(void);
int tegra_sku_id(void);
int tegra_cpu_process_id(void);
int tegra_core_process_id(void);
void tegra_init_fuse(void);
void tegra_init_fuse_dma(void);
u32 tegra_fuse_readl(unsigned long offset);
void tegra_fuse_writel(u32 value, unsigned long offset);
enum tegra_revision tegra_get_revision(void);
