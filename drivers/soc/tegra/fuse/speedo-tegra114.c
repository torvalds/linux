// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/kernel.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define SOC_PROCESS_CORNERS	2
#define CPU_PROCESS_CORNERS	2

enum {
	THRESHOLD_INDEX_0,
	THRESHOLD_INDEX_1,
	THRESHOLD_INDEX_COUNT,
};

static const u32 __initconst soc_process_speedos[][SOC_PROCESS_CORNERS] = {
	{1123,     UINT_MAX},
	{0,        UINT_MAX},
};

static const u32 __initconst cpu_process_speedos[][CPU_PROCESS_CORNERS] = {
	{1695,     UINT_MAX},
	{0,        UINT_MAX},
};

static void __init rev_sku_to_speedo_ids(struct tegra_sku_info *sku_info,
					 int *threshold)
{
	u32 tmp;
	u32 sku = sku_info->sku_id;
	enum tegra_revision rev = sku_info->revision;

	switch (sku) {
	case 0x00:
	case 0x10:
	case 0x05:
	case 0x06:
		sku_info->cpu_speedo_id = 1;
		sku_info->soc_speedo_id = 0;
		*threshold = THRESHOLD_INDEX_0;
		break;

	case 0x03:
	case 0x04:
		sku_info->cpu_speedo_id = 2;
		sku_info->soc_speedo_id = 1;
		*threshold = THRESHOLD_INDEX_1;
		break;

	default:
		pr_err("Tegra Unknown SKU %d\n", sku);
		sku_info->cpu_speedo_id = 0;
		sku_info->soc_speedo_id = 0;
		*threshold = THRESHOLD_INDEX_0;
		break;
	}

	if (rev == TEGRA_REVISION_A01) {
		tmp = tegra_fuse_read_early(0x270) << 1;
		tmp |= tegra_fuse_read_early(0x26c);
		if (!tmp)
			sku_info->cpu_speedo_id = 0;
	}
}

void __init tegra114_init_speedo_data(struct tegra_sku_info *sku_info)
{
	u32 cpu_speedo_val;
	u32 soc_speedo_val;
	int threshold;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(cpu_process_speedos) !=
			THRESHOLD_INDEX_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(soc_process_speedos) !=
			THRESHOLD_INDEX_COUNT);

	rev_sku_to_speedo_ids(sku_info, &threshold);

	cpu_speedo_val = tegra_fuse_read_early(0x12c) + 1024;
	soc_speedo_val = tegra_fuse_read_early(0x134);

	for (i = 0; i < CPU_PROCESS_CORNERS; i++)
		if (cpu_speedo_val < cpu_process_speedos[threshold][i])
			break;
	sku_info->cpu_process_id = i;

	for (i = 0; i < SOC_PROCESS_CORNERS; i++)
		if (soc_speedo_val < soc_process_speedos[threshold][i])
			break;
	sku_info->soc_process_id = i;
}
