/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Tomeu Vizoso <tomeu@tomeuvizoso.net> */

#ifndef __ROCKET_CORE_H__
#define __ROCKET_CORE_H__

#include <drm/gpu_scheduler.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mutex_types.h>
#include <linux/reset.h>

#include "rocket_registers.h"

#define rocket_pc_readl(core, reg) \
	readl((core)->pc_iomem + (REG_PC_##reg))
#define rocket_pc_writel(core, reg, value) \
	writel(value, (core)->pc_iomem + (REG_PC_##reg))

#define rocket_cna_readl(core, reg) \
	readl((core)->cna_iomem + (REG_CNA_##reg) - REG_CNA_S_STATUS)
#define rocket_cna_writel(core, reg, value) \
	writel(value, (core)->cna_iomem + (REG_CNA_##reg) - REG_CNA_S_STATUS)

#define rocket_core_readl(core, reg) \
	readl((core)->core_iomem + (REG_CORE_##reg) - REG_CORE_S_STATUS)
#define rocket_core_writel(core, reg, value) \
	writel(value, (core)->core_iomem + (REG_CORE_##reg) - REG_CORE_S_STATUS)

struct rocket_core {
	struct device *dev;
	struct rocket_device *rdev;
	unsigned int index;

	int irq;
	void __iomem *pc_iomem;
	void __iomem *cna_iomem;
	void __iomem *core_iomem;
	struct clk_bulk_data clks[4];
	struct reset_control_bulk_data resets[2];

	struct iommu_group *iommu_group;
};

int rocket_core_init(struct rocket_core *core);
void rocket_core_fini(struct rocket_core *core);
void rocket_core_reset(struct rocket_core *core);

#endif
