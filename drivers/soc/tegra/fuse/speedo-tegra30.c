/*
 * Copyright (c) 2012-2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/kernel.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define CORE_PROCESS_CORNERS	1
#define CPU_PROCESS_CORNERS	6

#define FUSE_SPEEDO_CALIB_0	0x14
#define FUSE_PACKAGE_INFO	0XFC
#define FUSE_TEST_PROG_VER	0X28

#define G_SPEEDO_BIT_MINUS1	58
#define G_SPEEDO_BIT_MINUS1_R	59
#define G_SPEEDO_BIT_MINUS2	60
#define G_SPEEDO_BIT_MINUS2_R	61
#define LP_SPEEDO_BIT_MINUS1	62
#define LP_SPEEDO_BIT_MINUS1_R	63
#define LP_SPEEDO_BIT_MINUS2	64
#define LP_SPEEDO_BIT_MINUS2_R	65

enum {
	THRESHOLD_INDEX_0,
	THRESHOLD_INDEX_1,
	THRESHOLD_INDEX_2,
	THRESHOLD_INDEX_3,
	THRESHOLD_INDEX_4,
	THRESHOLD_INDEX_5,
	THRESHOLD_INDEX_6,
	THRESHOLD_INDEX_7,
	THRESHOLD_INDEX_8,
	THRESHOLD_INDEX_9,
	THRESHOLD_INDEX_10,
	THRESHOLD_INDEX_11,
	THRESHOLD_INDEX_COUNT,
};

static const u32 __initconst core_process_speedos[][CORE_PROCESS_CORNERS] = {
	{180},
	{170},
	{195},
	{180},
	{168},
	{192},
	{180},
	{170},
	{195},
	{180},
	{180},
	{180},
};

static const u32 __initconst cpu_process_speedos[][CPU_PROCESS_CORNERS] = {
	{306, 338, 360, 376, UINT_MAX},
	{295, 336, 358, 375, UINT_MAX},
	{325, 325, 358, 375, UINT_MAX},
	{325, 325, 358, 375, UINT_MAX},
	{292, 324, 348, 364, UINT_MAX},
	{324, 324, 348, 364, UINT_MAX},
	{324, 324, 348, 364, UINT_MAX},
	{295, 336, 358, 375, UINT_MAX},
	{358, 358, 358, 358, 397, UINT_MAX},
	{364, 364, 364, 364, 397, UINT_MAX},
	{295, 336, 358, 375, 391, UINT_MAX},
	{295, 336, 358, 375, 391, UINT_MAX},
};

static int threshold_index __initdata;

static void __init fuse_speedo_calib(u32 *speedo_g, u32 *speedo_lp)
{
	u32 reg;
	int ate_ver;
	int bit_minus1;
	int bit_minus2;

	reg = tegra30_fuse_readl(FUSE_SPEEDO_CALIB_0);

	*speedo_lp = (reg & 0xFFFF) * 4;
	*speedo_g = ((reg >> 16) & 0xFFFF) * 4;

	ate_ver = tegra30_fuse_readl(FUSE_TEST_PROG_VER);
	pr_debug("Tegra ATE prog ver %d.%d\n", ate_ver/10, ate_ver%10);

	if (ate_ver >= 26) {
		bit_minus1 = tegra30_spare_fuse(LP_SPEEDO_BIT_MINUS1);
		bit_minus1 |= tegra30_spare_fuse(LP_SPEEDO_BIT_MINUS1_R);
		bit_minus2 = tegra30_spare_fuse(LP_SPEEDO_BIT_MINUS2);
		bit_minus2 |= tegra30_spare_fuse(LP_SPEEDO_BIT_MINUS2_R);
		*speedo_lp |= (bit_minus1 << 1) | bit_minus2;

		bit_minus1 = tegra30_spare_fuse(G_SPEEDO_BIT_MINUS1);
		bit_minus1 |= tegra30_spare_fuse(G_SPEEDO_BIT_MINUS1_R);
		bit_minus2 = tegra30_spare_fuse(G_SPEEDO_BIT_MINUS2);
		bit_minus2 |= tegra30_spare_fuse(G_SPEEDO_BIT_MINUS2_R);
		*speedo_g |= (bit_minus1 << 1) | bit_minus2;
	} else {
		*speedo_lp |= 0x3;
		*speedo_g |= 0x3;
	}
}

static void __init rev_sku_to_speedo_ids(struct tegra_sku_info *sku_info)
{
	int package_id = tegra30_fuse_readl(FUSE_PACKAGE_INFO) & 0x0F;

	switch (sku_info->revision) {
	case TEGRA_REVISION_A01:
		sku_info->cpu_speedo_id = 0;
		sku_info->soc_speedo_id = 0;
		threshold_index = THRESHOLD_INDEX_0;
		break;
	case TEGRA_REVISION_A02:
	case TEGRA_REVISION_A03:
		switch (sku_info->sku_id) {
		case 0x87:
		case 0x82:
			sku_info->cpu_speedo_id = 1;
			sku_info->soc_speedo_id = 1;
			threshold_index = THRESHOLD_INDEX_1;
			break;
		case 0x81:
			switch (package_id) {
			case 1:
				sku_info->cpu_speedo_id = 2;
				sku_info->soc_speedo_id = 2;
				threshold_index = THRESHOLD_INDEX_2;
				break;
			case 2:
				sku_info->cpu_speedo_id = 4;
				sku_info->soc_speedo_id = 1;
				threshold_index = THRESHOLD_INDEX_7;
				break;
			default:
				pr_err("Tegra Unknown pkg %d\n", package_id);
				break;
			}
			break;
		case 0x80:
			switch (package_id) {
			case 1:
				sku_info->cpu_speedo_id = 5;
				sku_info->soc_speedo_id = 2;
				threshold_index = THRESHOLD_INDEX_8;
				break;
			case 2:
				sku_info->cpu_speedo_id = 6;
				sku_info->soc_speedo_id = 2;
				threshold_index = THRESHOLD_INDEX_9;
				break;
			default:
				pr_err("Tegra Unknown pkg %d\n", package_id);
				break;
			}
			break;
		case 0x83:
			switch (package_id) {
			case 1:
				sku_info->cpu_speedo_id = 7;
				sku_info->soc_speedo_id = 1;
				threshold_index = THRESHOLD_INDEX_10;
				break;
			case 2:
				sku_info->cpu_speedo_id = 3;
				sku_info->soc_speedo_id = 2;
				threshold_index = THRESHOLD_INDEX_3;
				break;
			default:
				pr_err("Tegra Unknown pkg %d\n", package_id);
				break;
			}
			break;
		case 0x8F:
			sku_info->cpu_speedo_id = 8;
			sku_info->soc_speedo_id = 1;
			threshold_index = THRESHOLD_INDEX_11;
			break;
		case 0x08:
			sku_info->cpu_speedo_id = 1;
			sku_info->soc_speedo_id = 1;
			threshold_index = THRESHOLD_INDEX_4;
			break;
		case 0x02:
			sku_info->cpu_speedo_id = 2;
			sku_info->soc_speedo_id = 2;
			threshold_index = THRESHOLD_INDEX_5;
			break;
		case 0x04:
			sku_info->cpu_speedo_id = 3;
			sku_info->soc_speedo_id = 2;
			threshold_index = THRESHOLD_INDEX_6;
			break;
		case 0:
			switch (package_id) {
			case 1:
				sku_info->cpu_speedo_id = 2;
				sku_info->soc_speedo_id = 2;
				threshold_index = THRESHOLD_INDEX_2;
				break;
			case 2:
				sku_info->cpu_speedo_id = 3;
				sku_info->soc_speedo_id = 2;
				threshold_index = THRESHOLD_INDEX_3;
				break;
			default:
				pr_err("Tegra Unknown pkg %d\n", package_id);
				break;
			}
			break;
		default:
			pr_warn("Tegra Unknown SKU %d\n", sku_info->sku_id);
			sku_info->cpu_speedo_id = 0;
			sku_info->soc_speedo_id = 0;
			threshold_index = THRESHOLD_INDEX_0;
			break;
		}
		break;
	default:
		pr_warn("Tegra Unknown chip rev %d\n", sku_info->revision);
		sku_info->cpu_speedo_id = 0;
		sku_info->soc_speedo_id = 0;
		threshold_index = THRESHOLD_INDEX_0;
		break;
	}
}

void __init tegra30_init_speedo_data(struct tegra_sku_info *sku_info)
{
	u32 cpu_speedo_val;
	u32 core_speedo_val;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(cpu_process_speedos) !=
			THRESHOLD_INDEX_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(core_process_speedos) !=
			THRESHOLD_INDEX_COUNT);


	rev_sku_to_speedo_ids(sku_info);
	fuse_speedo_calib(&cpu_speedo_val, &core_speedo_val);
	pr_debug("Tegra CPU speedo value %u\n", cpu_speedo_val);
	pr_debug("Tegra Core speedo value %u\n", core_speedo_val);

	for (i = 0; i < CPU_PROCESS_CORNERS; i++) {
		if (cpu_speedo_val < cpu_process_speedos[threshold_index][i])
			break;
	}
	sku_info->cpu_process_id = i - 1;

	if (sku_info->cpu_process_id == -1) {
		pr_warn("Tegra CPU speedo value %3d out of range",
			 cpu_speedo_val);
		sku_info->cpu_process_id = 0;
		sku_info->cpu_speedo_id = 1;
	}

	for (i = 0; i < CORE_PROCESS_CORNERS; i++) {
		if (core_speedo_val < core_process_speedos[threshold_index][i])
			break;
	}
	sku_info->core_process_id = i - 1;

	if (sku_info->core_process_id == -1) {
		pr_warn("Tegra CORE speedo value %3d out of range",
				 core_speedo_val);
		sku_info->core_process_id = 0;
		sku_info->soc_speedo_id = 1;
	}
}
