/*
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/kernel.h>
#include <linux/bug.h>

#include "fuse.h"

#define CORE_PROCESS_CORNERS_NUM	2
#define CPU_PROCESS_CORNERS_NUM		2

enum {
	THRESHOLD_INDEX_0,
	THRESHOLD_INDEX_1,
	THRESHOLD_INDEX_COUNT,
};

static const u32 core_process_speedos[][CORE_PROCESS_CORNERS_NUM] = {
	{1123,     UINT_MAX},
	{0,        UINT_MAX},
};

static const u32 cpu_process_speedos[][CPU_PROCESS_CORNERS_NUM] = {
	{1695,     UINT_MAX},
	{0,        UINT_MAX},
};

static void rev_sku_to_speedo_ids(int rev, int sku, int *threshold)
{
	u32 tmp;

	switch (sku) {
	case 0x00:
	case 0x10:
	case 0x05:
	case 0x06:
		tegra_cpu_speedo_id = 1;
		tegra_soc_speedo_id = 0;
		*threshold = THRESHOLD_INDEX_0;
		break;

	case 0x03:
	case 0x04:
		tegra_cpu_speedo_id = 2;
		tegra_soc_speedo_id = 1;
		*threshold = THRESHOLD_INDEX_1;
		break;

	default:
		pr_err("Tegra114 Unknown SKU %d\n", sku);
		tegra_cpu_speedo_id = 0;
		tegra_soc_speedo_id = 0;
		*threshold = THRESHOLD_INDEX_0;
		break;
	}

	if (rev == TEGRA_REVISION_A01) {
		tmp = tegra_fuse_readl(0x270) << 1;
		tmp |= tegra_fuse_readl(0x26c);
		if (!tmp)
			tegra_cpu_speedo_id = 0;
	}
}

void tegra114_init_speedo_data(void)
{
	u32 cpu_speedo_val;
	u32 core_speedo_val;
	int threshold;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(cpu_process_speedos) !=
			THRESHOLD_INDEX_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(core_process_speedos) !=
			THRESHOLD_INDEX_COUNT);

	rev_sku_to_speedo_ids(tegra_revision, tegra_sku_id, &threshold);

	cpu_speedo_val = tegra_fuse_readl(0x12c) + 1024;
	core_speedo_val = tegra_fuse_readl(0x134);

	for (i = 0; i < CPU_PROCESS_CORNERS_NUM; i++)
		if (cpu_speedo_val < cpu_process_speedos[threshold][i])
			break;
	tegra_cpu_process_id = i;

	for (i = 0; i < CORE_PROCESS_CORNERS_NUM; i++)
		if (core_speedo_val < core_process_speedos[threshold][i])
			break;
	tegra_core_process_id = i;
}
