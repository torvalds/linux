// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU API for RISC-V IOMMU implementations.
 *
 * Copyright © 2022-2024 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * Authors
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 *	Nick Kossifidis <mick@ics.forth.gr>
 */

#define pr_fmt(fmt) "riscv-iommu: " fmt

#include <linux/compiler.h>
#include <linux/crash_dump.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/kernel.h>

#include "iommu-bits.h"
#include "iommu.h"

/* Timeouts in [us] */
#define RISCV_IOMMU_DDTP_TIMEOUT	50000

/*
 * This is best effort IOMMU translation shutdown flow.
 * Disable IOMMU without waiting for hardware response.
 */
static void riscv_iommu_disable(struct riscv_iommu_device *iommu)
{
	riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_CQCSR, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FQCSR, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_PQCSR, 0);
}

static int riscv_iommu_init_check(struct riscv_iommu_device *iommu)
{
	u64 ddtp;

	/*
	 * Make sure the IOMMU is switched off or in pass-through mode during
	 * regular boot flow and disable translation when we boot into a kexec
	 * kernel and the previous kernel left them enabled.
	 */
	ddtp = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_DDTP);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	if (FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp) >
	     RISCV_IOMMU_DDTP_IOMMU_MODE_BARE) {
		if (!is_kdump_kernel())
			return -EBUSY;
		riscv_iommu_disable(iommu);
	}

	/* Configure accesses to in-memory data structures for CPU-native byte order. */
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) !=
	    !!(iommu->fctl & RISCV_IOMMU_FCTL_BE)) {
		if (!(iommu->caps & RISCV_IOMMU_CAPABILITIES_END))
			return -EINVAL;
		riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL,
				   iommu->fctl ^ RISCV_IOMMU_FCTL_BE);
		iommu->fctl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL);
		if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) !=
		    !!(iommu->fctl & RISCV_IOMMU_FCTL_BE))
			return -EINVAL;
	}

	return 0;
}

void riscv_iommu_remove(struct riscv_iommu_device *iommu)
{
	iommu_device_sysfs_remove(&iommu->iommu);
}

int riscv_iommu_init(struct riscv_iommu_device *iommu)
{
	int rc;

	rc = riscv_iommu_init_check(iommu);
	if (rc)
		return dev_err_probe(iommu->dev, rc, "unexpected device state\n");

	/*
	 * Placeholder for a complete IOMMU device initialization.  For now,
	 * only bare minimum: enable global identity mapping mode and register sysfs.
	 */
	riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP,
			   FIELD_PREP(RISCV_IOMMU_DDTP_IOMMU_MODE,
				      RISCV_IOMMU_DDTP_IOMMU_MODE_BARE));

	rc = iommu_device_sysfs_add(&iommu->iommu, NULL, NULL, "riscv-iommu@%s",
				    dev_name(iommu->dev));
	if (rc)
		return dev_err_probe(iommu->dev, rc,
				     "cannot register sysfs interface\n");

	return 0;
}
