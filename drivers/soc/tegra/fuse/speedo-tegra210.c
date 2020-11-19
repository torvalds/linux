// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2015, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/bug.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define CPU_PROCESS_CORNERS	2
#define GPU_PROCESS_CORNERS	2
#define SOC_PROCESS_CORNERS	3

#define FUSE_CPU_SPEEDO_0	0x014
#define FUSE_CPU_SPEEDO_1	0x02c
#define FUSE_CPU_SPEEDO_2	0x030
#define FUSE_SOC_SPEEDO_0	0x034
#define FUSE_SOC_SPEEDO_1	0x038
#define FUSE_SOC_SPEEDO_2	0x03c
#define FUSE_CPU_IDDQ		0x018
#define FUSE_SOC_IDDQ		0x040
#define FUSE_GPU_IDDQ		0x128
#define FUSE_FT_REV		0x028

enum {
	THRESHOLD_INDEX_0,
	THRESHOLD_INDEX_1,
	THRESHOLD_INDEX_COUNT,
};

static const u32 __initconst cpu_process_speedos[][CPU_PROCESS_CORNERS] = {
	{ 2119, UINT_MAX },
	{ 2119, UINT_MAX },
};

static const u32 __initconst gpu_process_speedos[][GPU_PROCESS_CORNERS] = {
	{ UINT_MAX, UINT_MAX },
	{ UINT_MAX, UINT_MAX },
};

static const u32 __initconst soc_process_speedos[][SOC_PROCESS_CORNERS] = {
	{ 1950, 2100, UINT_MAX },
	{ 1950, 2100, UINT_MAX },
};

static u8 __init get_speedo_revision(void)
{
	return tegra_fuse_read_spare(4) << 2 |
	       tegra_fuse_read_spare(3) << 1 |
	       tegra_fuse_read_spare(2) << 0;
}

static void __init rev_sku_to_speedo_ids(struct tegra_sku_info *sku_info,
					 u8 speedo_rev, int *threshold)
{
	int sku = sku_info->sku_id;

	/* Assign to default */
	sku_info->cpu_speedo_id = 0;
	sku_info->soc_speedo_id = 0;
	sku_info->gpu_speedo_id = 0;
	*threshold = THRESHOLD_INDEX_0;

	switch (sku) {
	case 0x00: /* Engineering SKU */
	case 0x01: /* Engineering SKU */
	case 0x07:
	case 0x17:
	case 0x27:
		if (speedo_rev >= 2)
			sku_info->gpu_speedo_id = 1;
		break;

	case 0x13:
		if (speedo_rev >= 2)
			sku_info->gpu_speedo_id = 1;

		sku_info->cpu_speedo_id = 1;
		break;

	default:
		pr_err("Tegra210: unknown SKU %#04x\n", sku);
		/* Using the default for the error case */
		break;
	}
}

static int get_process_id(int value, const u32 *speedos, unsigned int num)
{
	unsigned int i;

	for (i = 0; i < num; i++)
		if (value < speedos[i])
			return i;

	return -EINVAL;
}

void __init tegra210_init_speedo_data(struct tegra_sku_info *sku_info)
{
	int cpu_speedo[3], soc_speedo[3];
	unsigned int index;
	u8 speedo_revision;

	BUILD_BUG_ON(ARRAY_SIZE(cpu_process_speedos) !=
			THRESHOLD_INDEX_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(gpu_process_speedos) !=
			THRESHOLD_INDEX_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(soc_process_speedos) !=
			THRESHOLD_INDEX_COUNT);

	/* Read speedo/IDDQ fuses */
	cpu_speedo[0] = tegra_fuse_read_early(FUSE_CPU_SPEEDO_0);
	cpu_speedo[1] = tegra_fuse_read_early(FUSE_CPU_SPEEDO_1);
	cpu_speedo[2] = tegra_fuse_read_early(FUSE_CPU_SPEEDO_2);

	soc_speedo[0] = tegra_fuse_read_early(FUSE_SOC_SPEEDO_0);
	soc_speedo[1] = tegra_fuse_read_early(FUSE_SOC_SPEEDO_1);
	soc_speedo[2] = tegra_fuse_read_early(FUSE_SOC_SPEEDO_2);

	/*
	 * Determine CPU, GPU and SoC speedo values depending on speedo fusing
	 * revision. Note that GPU speedo value is fused in CPU_SPEEDO_2.
	 */
	speedo_revision = get_speedo_revision();
	pr_info("Speedo Revision %u\n", speedo_revision);

	if (speedo_revision >= 3) {
		sku_info->cpu_speedo_value = cpu_speedo[0];
		sku_info->gpu_speedo_value = cpu_speedo[2];
		sku_info->soc_speedo_value = soc_speedo[0];
	} else if (speedo_revision == 2) {
		sku_info->cpu_speedo_value = (-1938 + (1095 * cpu_speedo[0] / 100)) / 10;
		sku_info->gpu_speedo_value = (-1662 + (1082 * cpu_speedo[2] / 100)) / 10;
		sku_info->soc_speedo_value = ( -705 + (1037 * soc_speedo[0] / 100)) / 10;
	} else {
		sku_info->cpu_speedo_value = 2100;
		sku_info->gpu_speedo_value = cpu_speedo[2] - 75;
		sku_info->soc_speedo_value = 1900;
	}

	if ((sku_info->cpu_speedo_value <= 0) ||
	    (sku_info->gpu_speedo_value <= 0) ||
	    (sku_info->soc_speedo_value <= 0)) {
		WARN(1, "speedo value not fused\n");
		return;
	}

	rev_sku_to_speedo_ids(sku_info, speedo_revision, &index);

	sku_info->gpu_process_id = get_process_id(sku_info->gpu_speedo_value,
						  gpu_process_speedos[index],
						  GPU_PROCESS_CORNERS);

	sku_info->cpu_process_id = get_process_id(sku_info->cpu_speedo_value,
						  cpu_process_speedos[index],
						  CPU_PROCESS_CORNERS);

	sku_info->soc_process_id = get_process_id(sku_info->soc_speedo_value,
						  soc_process_speedos[index],
						  SOC_PROCESS_CORNERS);

	pr_debug("Tegra GPU Speedo ID=%d, Speedo Value=%d\n",
		 sku_info->gpu_speedo_id, sku_info->gpu_speedo_value);
}
