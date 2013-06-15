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

#ifndef __MACH_TEGRA_FUSE_H
#define __MACH_TEGRA_FUSE_H

#define SKU_ID_T20	8
#define SKU_ID_T25SE	20
#define SKU_ID_AP25	23
#define SKU_ID_T25	24
#define SKU_ID_AP25E	27
#define SKU_ID_T25E	28

#define TEGRA20		0x20
#define TEGRA30		0x30
#define TEGRA114	0x35

#ifndef __ASSEMBLY__
enum tegra_revision {
	TEGRA_REVISION_UNKNOWN = 0,
	TEGRA_REVISION_A01,
	TEGRA_REVISION_A02,
	TEGRA_REVISION_A03,
	TEGRA_REVISION_A03p,
	TEGRA_REVISION_A04,
	TEGRA_REVISION_MAX,
};

extern int tegra_sku_id;
extern int tegra_cpu_process_id;
extern int tegra_core_process_id;
extern int tegra_chip_id;
extern int tegra_cpu_speedo_id;		/* only exist in Tegra30 and later */
extern int tegra_soc_speedo_id;
extern enum tegra_revision tegra_revision;

extern int tegra_bct_strapping;

unsigned long long tegra_chip_uid(void);
void tegra_init_fuse(void);
bool tegra_spare_fuse(int bit);
u32 tegra_fuse_readl(unsigned long offset);

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
void tegra20_init_speedo_data(void);
#else
static inline void tegra20_init_speedo_data(void) {}
#endif

#ifdef CONFIG_ARCH_TEGRA_3x_SOC
void tegra30_init_speedo_data(void);
#else
static inline void tegra30_init_speedo_data(void) {}
#endif

#ifdef CONFIG_ARCH_TEGRA_114_SOC
void tegra114_init_speedo_data(void);
#else
static inline void tegra114_init_speedo_data(void) {}
#endif
#endif /* __ASSEMBLY__ */

#endif
