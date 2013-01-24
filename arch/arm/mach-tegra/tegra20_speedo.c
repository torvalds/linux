/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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

#define CPU_SPEEDO_LSBIT		20
#define CPU_SPEEDO_MSBIT		29
#define CPU_SPEEDO_REDUND_LSBIT		30
#define CPU_SPEEDO_REDUND_MSBIT		39
#define CPU_SPEEDO_REDUND_OFFS	(CPU_SPEEDO_REDUND_MSBIT - CPU_SPEEDO_MSBIT)

#define CORE_SPEEDO_LSBIT		40
#define CORE_SPEEDO_MSBIT		47
#define CORE_SPEEDO_REDUND_LSBIT	48
#define CORE_SPEEDO_REDUND_MSBIT	55
#define CORE_SPEEDO_REDUND_OFFS	(CORE_SPEEDO_REDUND_MSBIT - CORE_SPEEDO_MSBIT)

#define SPEEDO_MULT			4

#define PROCESS_CORNERS_NUM		4

#define SPEEDO_ID_SELECT_0(rev)		((rev) <= 2)
#define SPEEDO_ID_SELECT_1(sku)		\
	(((sku) != 20) && ((sku) != 23) && ((sku) != 24) && \
	 ((sku) != 27) && ((sku) != 28))

enum {
	SPEEDO_ID_0,
	SPEEDO_ID_1,
	SPEEDO_ID_2,
	SPEEDO_ID_COUNT,
};

static const u32 cpu_process_speedos[][PROCESS_CORNERS_NUM] = {
	{315, 366, 420, UINT_MAX},
	{303, 368, 419, UINT_MAX},
	{316, 331, 383, UINT_MAX},
};

static const u32 core_process_speedos[][PROCESS_CORNERS_NUM] = {
	{165, 195, 224, UINT_MAX},
	{165, 195, 224, UINT_MAX},
	{165, 195, 224, UINT_MAX},
};

void tegra20_init_speedo_data(void)
{
	u32 reg;
	u32 val;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(cpu_process_speedos) != SPEEDO_ID_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(core_process_speedos) != SPEEDO_ID_COUNT);

	if (SPEEDO_ID_SELECT_0(tegra_revision))
		tegra_soc_speedo_id = SPEEDO_ID_0;
	else if (SPEEDO_ID_SELECT_1(tegra_sku_id))
		tegra_soc_speedo_id = SPEEDO_ID_1;
	else
		tegra_soc_speedo_id = SPEEDO_ID_2;

	val = 0;
	for (i = CPU_SPEEDO_MSBIT; i >= CPU_SPEEDO_LSBIT; i--) {
		reg = tegra_spare_fuse(i) |
			tegra_spare_fuse(i + CPU_SPEEDO_REDUND_OFFS);
		val = (val << 1) | (reg & 0x1);
	}
	val = val * SPEEDO_MULT;
	pr_debug("%s CPU speedo value %u\n", __func__, val);

	for (i = 0; i < (PROCESS_CORNERS_NUM - 1); i++) {
		if (val <= cpu_process_speedos[tegra_soc_speedo_id][i])
			break;
	}
	tegra_cpu_process_id = i;

	val = 0;
	for (i = CORE_SPEEDO_MSBIT; i >= CORE_SPEEDO_LSBIT; i--) {
		reg = tegra_spare_fuse(i) |
			tegra_spare_fuse(i + CORE_SPEEDO_REDUND_OFFS);
		val = (val << 1) | (reg & 0x1);
	}
	val = val * SPEEDO_MULT;
	pr_debug("%s Core speedo value %u\n", __func__, val);

	for (i = 0; i < (PROCESS_CORNERS_NUM - 1); i++) {
		if (val <= core_process_speedos[tegra_soc_speedo_id][i])
			break;
	}
	tegra_core_process_id = i;

	pr_info("Tegra20 Soc Speedo ID %d", tegra_soc_speedo_id);
}
