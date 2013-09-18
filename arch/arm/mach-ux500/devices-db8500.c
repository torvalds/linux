/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/mfd/dbx500-prcmu.h>

#include "setup.h"
#include "irqs.h"

#include "db8500-regs.h"
#include "devices-db8500.h"

struct prcmu_pdata db8500_prcmu_pdata = {
	.ab_platdata	= &ab8500_platdata,
	.ab_irq		= IRQ_DB8500_AB8500,
	.irq_base	= IRQ_PRCMU_BASE,
	.version_offset	= DB8500_PRCMU_FW_VERSION_OFFSET,
	.legacy_offset	= DB8500_PRCMU_LEGACY_OFFSET,
};

static struct resource db8500_prcmu_res[] = {
	{
		.name  = "prcmu",
		.start = U8500_PRCMU_BASE,
		.end   = U8500_PRCMU_BASE + SZ_8K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "prcmu-tcdm",
		.start = U8500_PRCMU_TCDM_BASE,
		.end   = U8500_PRCMU_TCDM_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "irq",
		.start = IRQ_DB8500_PRCMU1,
		.end   = IRQ_DB8500_PRCMU1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "prcmu-tcpm",
		.start = U8500_PRCMU_TCPM_BASE,
		.end   = U8500_PRCMU_TCPM_BASE + SZ_32K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device db8500_prcmu_device = {
	.name			= "db8500-prcmu",
	.resource		= db8500_prcmu_res,
	.num_resources		= ARRAY_SIZE(db8500_prcmu_res),
	.dev = {
		.platform_data = &db8500_prcmu_pdata,
	},
};
