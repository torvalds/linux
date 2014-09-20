/*
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __DRIVERS_MISC_TEGRA_FUSE_H
#define __DRIVERS_MISC_TEGRA_FUSE_H

#define TEGRA_FUSE_BASE	0x7000f800
#define TEGRA_FUSE_SIZE	0x400

int tegra_fuse_create_sysfs(struct device *dev, int size,
		     u32 (*readl)(const unsigned int offset));

bool tegra30_spare_fuse(int bit);
u32 tegra30_fuse_readl(const unsigned int offset);
void tegra30_init_fuse_early(void);
void tegra_init_revision(void);
void tegra_init_apbmisc(void);

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
void tegra20_init_speedo_data(struct tegra_sku_info *sku_info);
bool tegra20_spare_fuse_early(int spare_bit);
void tegra20_init_fuse_early(void);
u32 tegra20_fuse_early(const unsigned int offset);
#else
static inline void tegra20_init_speedo_data(struct tegra_sku_info *sku_info) {}
static inline bool tegra20_spare_fuse_early(int spare_bit)
{
	return false;
}
static inline void tegra20_init_fuse_early(void) {}
static inline u32 tegra20_fuse_early(const unsigned int offset)
{
	return 0;
}
#endif


#ifdef CONFIG_ARCH_TEGRA_3x_SOC
void tegra30_init_speedo_data(struct tegra_sku_info *sku_info);
#else
static inline void tegra30_init_speedo_data(struct tegra_sku_info *sku_info) {}
#endif

#ifdef CONFIG_ARCH_TEGRA_114_SOC
void tegra114_init_speedo_data(struct tegra_sku_info *sku_info);
#else
static inline void tegra114_init_speedo_data(struct tegra_sku_info *sku_info) {}
#endif

#ifdef CONFIG_ARCH_TEGRA_124_SOC
void tegra124_init_speedo_data(struct tegra_sku_info *sku_info);
#else
static inline void tegra124_init_speedo_data(struct tegra_sku_info *sku_info) {}
#endif

#endif
