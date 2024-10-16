/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2022-2024 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * Authors
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 *	Nick Kossifidis <mick@ics.forth.gr>
 */

#ifndef _RISCV_IOMMU_H_
#define _RISCV_IOMMU_H_

#include <linux/iommu.h>
#include <linux/types.h>
#include <linux/iopoll.h>

#include "iommu-bits.h"

struct riscv_iommu_device {
	/* iommu core interface */
	struct iommu_device iommu;

	/* iommu hardware */
	struct device *dev;

	/* hardware control register space */
	void __iomem *reg;

	/* supported and enabled hardware capabilities */
	u64 caps;
	u32 fctl;

	/* available interrupt numbers, MSI or WSI */
	unsigned int irqs[RISCV_IOMMU_INTR_COUNT];
	unsigned int irqs_count;
};

int riscv_iommu_init(struct riscv_iommu_device *iommu);
void riscv_iommu_remove(struct riscv_iommu_device *iommu);

#define riscv_iommu_readl(iommu, addr) \
	readl_relaxed((iommu)->reg + (addr))

#define riscv_iommu_readq(iommu, addr) \
	readq_relaxed((iommu)->reg + (addr))

#define riscv_iommu_writel(iommu, addr, val) \
	writel_relaxed((val), (iommu)->reg + (addr))

#define riscv_iommu_writeq(iommu, addr, val) \
	writeq_relaxed((val), (iommu)->reg + (addr))

#define riscv_iommu_readq_timeout(iommu, addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readq_relaxed, (iommu)->reg + (addr), val, cond, \
			   delay_us, timeout_us)

#define riscv_iommu_readl_timeout(iommu, addr, val, cond, delay_us, timeout_us) \
	readx_poll_timeout(readl_relaxed, (iommu)->reg + (addr), val, cond, \
			   delay_us, timeout_us)

#endif
