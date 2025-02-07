// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>

#include "iris_core.h"
#include "iris_vpu_common.h"

#define CPU_BASE_OFFS				0x000A0000

#define CPU_CS_BASE_OFFS			(CPU_BASE_OFFS)

#define CTRL_INIT				(CPU_CS_BASE_OFFS + 0x48)
#define CTRL_STATUS				(CPU_CS_BASE_OFFS + 0x4C)

#define CTRL_ERROR_STATUS__M			0xfe

#define QTBL_INFO				(CPU_CS_BASE_OFFS + 0x50)
#define QTBL_ENABLE				BIT(0)

#define QTBL_ADDR				(CPU_CS_BASE_OFFS + 0x54)
#define CPU_CS_SCIACMDARG3			(CPU_CS_BASE_OFFS + 0x58)
#define SFR_ADDR				(CPU_CS_BASE_OFFS + 0x5C)
#define UC_REGION_ADDR				(CPU_CS_BASE_OFFS + 0x64)
#define UC_REGION_SIZE				(CPU_CS_BASE_OFFS + 0x68)

#define CPU_CS_H2XSOFTINTEN			(CPU_CS_BASE_OFFS + 0x148)
#define HOST2XTENSA_INTR_ENABLE			BIT(0)

#define CPU_CS_X2RPMH				(CPU_CS_BASE_OFFS + 0x168)

static void iris_vpu_setup_ucregion_memory_map(struct iris_core *core)
{
	u32 queue_size, value;

	/* Iris hardware requires 4K queue alignment */
	queue_size = ALIGN(sizeof(struct iris_hfi_queue_table_header) +
		(IFACEQ_QUEUE_SIZE * IFACEQ_NUMQ), SZ_4K);

	value = (u32)core->iface_q_table_daddr;
	writel(value, core->reg_base + UC_REGION_ADDR);

	/* Iris hardware requires 1M queue alignment */
	value = ALIGN(SFR_SIZE + queue_size, SZ_1M);
	writel(value, core->reg_base + UC_REGION_SIZE);

	value = (u32)core->iface_q_table_daddr;
	writel(value, core->reg_base + QTBL_ADDR);

	writel(QTBL_ENABLE, core->reg_base + QTBL_INFO);

	if (core->sfr_daddr) {
		value = (u32)core->sfr_daddr + core->iris_platform_data->core_arch;
		writel(value, core->reg_base + SFR_ADDR);
	}
}

int iris_vpu_boot_firmware(struct iris_core *core)
{
	u32 ctrl_init = BIT(0), ctrl_status = 0, count = 0, max_tries = 1000;

	iris_vpu_setup_ucregion_memory_map(core);

	writel(ctrl_init, core->reg_base + CTRL_INIT);
	writel(0x1, core->reg_base + CPU_CS_SCIACMDARG3);

	while (!ctrl_status && count < max_tries) {
		ctrl_status = readl(core->reg_base + CTRL_STATUS);
		if ((ctrl_status & CTRL_ERROR_STATUS__M) == 0x4) {
			dev_err(core->dev, "invalid setting for uc_region\n");
			break;
		}

		usleep_range(50, 100);
		count++;
	}

	if (count >= max_tries) {
		dev_err(core->dev, "error booting up iris firmware\n");
		return -ETIME;
	}

	writel(HOST2XTENSA_INTR_ENABLE, core->reg_base + CPU_CS_H2XSOFTINTEN);
	writel(0x0, core->reg_base + CPU_CS_X2RPMH);

	return 0;
}
