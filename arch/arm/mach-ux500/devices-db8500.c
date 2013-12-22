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
