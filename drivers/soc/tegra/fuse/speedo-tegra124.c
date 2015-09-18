/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/bug.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define CPU_PROCESS_CORNERS	2
#define GPU_PROCESS_CORNERS	2
#define SOC_PROCESS_CORNERS	2

#define FUSE_CPU_SPEEDO_0	0x14
#define FUSE_CPU_SPEEDO_1	0x2c
#define FUSE_CPU_SPEEDO_2	0x30
#define FUSE_SOC_SPEEDO_0	0x34
#define FUSE_SOC_SPEEDO_1	0x38
#define FUSE_SOC_SPEEDO_2	0x3c
#define FUSE_CPU_IDDQ		0x18
#define FUSE_SOC_IDDQ		0x40
#define FUSE_GPU_IDDQ		0x128
#define FUSE_FT_REV		0x28

enum {
	THRESHOLD_INDEX_0,
	THRESHOLD_INDEX_1,
	THRESHOLD_INDEX_COUNT,
};

static const u32 __initconst cpu_process_speedos[][CPU_PROCESS_CORNERS] = {
	{2190,	UINT_MAX},
	{0,	UINT_MAX},
};

static const u32 __initconst gpu_process_speedos[][GPU_PROCESS_CORNERS] = {
	{1965,	UINT_MAX},
	{0,	UINT_MAX},
};

static const u32 __initconst soc_process_speedos[][SOC_PROCESS_CORNERS] = {
	{2101,	UINT_MAX},
	{0,	UINT_MAX},
};

static void __init rev_sku_to_speedo_ids(struct tegra_sku_info *sku_info,
					 int *threshold)
{
	int sku = sku_info->sku_id;

	/* Assign to default */
	sku_info->cpu_speedo_id = 0;
	sku_info->soc_speedo_id = 0;
	sku_info->gpu_speedo_id = 0;
	*threshold = THRESHOLD_INDEX_0;

	switch (sku) {
	case 0x00: /* Eng sku */
	case 0x0F:
	case 0x23:
		/* Using the default */
		break;
	case 0x83:
		sku_info->cpu_speedo_id = 2;
		break;

	case 0x1F:
	case 0x87:
	case 0x27:
		sku_info->cpu_speedo_id = 2;
		sku_info->soc_speedo_id = 0;
		sku_info->gpu_speedo_id = 1;
		*threshold = THRESHOLD_INDEX_0;
		break;
	case 0x81:
	case 0x21:
	case 0x07:
		sku_info->cpu_speedo_id = 1;
		sku_info->soc_speedo_id = 1;
		sku_info->gpu_speedo_id = 1;
		*threshold = THRESHOLD_INDEX_1;
		break;
	case 0x49:
	case 0x4A:
	case 0x48:
		sku_info->cpu_speedo_id = 4;
		sku_info->soc_speedo_id = 2;
		sku_info->gpu_speedo_id = 3;
		*threshold = THRESHOLD_INDEX_1;
		break;
	default:
		pr_err("Tegra Unknown SKU %d\n", sku);
		/* Using the default for the error case */
		break;
	}
}

void __init tegra124_init_speedo_data(struct tegra_sku_info *sku_info)
{
	int i, threshold, cpu_speedo_0_value, soc_speedo_0_value;
	int cpu_iddq_value, gpu_iddq_value, soc_iddq_value;

	BUILD_BUG_ON(ARRAY_SIZE(cpu_process_speedos) !=
			THRESHOLD_INDEX_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(gpu_process_speedos) !=
			THRESHOLD_INDEX_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(soc_process_speedos) !=
			THRESHOLD_INDEX_COUNT);

	cpu_speedo_0_value = tegra_fuse_read_early(FUSE_CPU_SPEEDO_0);

	/* GPU Speedo is stored in CPU_SPEEDO_2 */
	sku_info->gpu_speedo_value = tegra_fuse_read_early(FUSE_CPU_SPEEDO_2);

	soc_speedo_0_value = tegra_fuse_read_early(FUSE_SOC_SPEEDO_0);

	cpu_iddq_value = tegra_fuse_read_early(FUSE_CPU_IDDQ);
	soc_iddq_value = tegra_fuse_read_early(FUSE_SOC_IDDQ);
	gpu_iddq_value = tegra_fuse_read_early(FUSE_GPU_IDDQ);

	sku_info->cpu_speedo_value = cpu_speedo_0_value;

	if (sku_info->cpu_speedo_value == 0) {
		pr_warn("Tegra Warning: Speedo value not fused.\n");
		WARN_ON(1);
		return;
	}

	rev_sku_to_speedo_ids(sku_info, &threshold);

	sku_info->cpu_iddq_value = tegra_fuse_read_early(FUSE_CPU_IDDQ);

	for (i = 0; i < GPU_PROCESS_CORNERS; i++)
		if (sku_info->gpu_speedo_value <
			gpu_process_speedos[threshold][i])
			break;
	sku_info->gpu_process_id = i;

	for (i = 0; i < CPU_PROCESS_CORNERS; i++)
		if (sku_info->cpu_speedo_value <
			cpu_process_speedos[threshold][i])
				break;
	sku_info->cpu_process_id = i;

	for (i = 0; i < SOC_PROCESS_CORNERS; i++)
		if (soc_speedo_0_value <
			soc_process_speedos[threshold][i])
			break;
	sku_info->soc_process_id = i;

	pr_debug("Tegra GPU Speedo ID=%d, Speedo Value=%d\n",
		 sku_info->gpu_speedo_id, sku_info->gpu_speedo_value);
}
