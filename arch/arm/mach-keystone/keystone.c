// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keystone2 based boards and SOC related code.
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Cyril Chemparathy <cyril@ti.com>
 *	Santosh Shilimkar <santosh.shillimkar@ti.com>
 */
#include <linux/io.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/memblock.h>

#include <asm/setup.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/smp_plat.h>
#include <asm/memory.h>

#include "memory.h"

#include "keystone.h"

#ifdef CONFIG_ARM_LPAE
static int keystone_platform_notifier(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	struct device *dev = data;

	if (event != BUS_NOTIFY_ADD_DEVICE)
		return NOTIFY_DONE;

	if (!dev)
		return NOTIFY_BAD;

	if (!dev->of_node) {
		int ret = dma_direct_set_offset(dev, KEYSTONE_HIGH_PHYS_START,
						KEYSTONE_LOW_PHYS_START,
						KEYSTONE_HIGH_PHYS_SIZE);
		dev_err(dev, "set dma_offset%08llx%s\n",
			KEYSTONE_HIGH_PHYS_START - KEYSTONE_LOW_PHYS_START,
			ret ? " failed" : "");
	}
	return NOTIFY_OK;
}

static struct notifier_block platform_nb = {
	.notifier_call = keystone_platform_notifier,
};
#endif /* CONFIG_ARM_LPAE */

static void __init keystone_init(void)
{
#ifdef CONFIG_ARM_LPAE
	if (PHYS_OFFSET >= KEYSTONE_HIGH_PHYS_START)
		bus_register_notifier(&platform_bus_type, &platform_nb);
#endif
	keystone_pm_runtime_init();
}

static long long __init keystone_pv_fixup(void)
{
	long long offset;
	u64 mem_start, mem_end;

	mem_start = memblock_start_of_DRAM();
	mem_end = memblock_end_of_DRAM();

	/* nothing to do if we are running out of the <32-bit space */
	if (mem_start >= KEYSTONE_LOW_PHYS_START &&
	    mem_end   <= KEYSTONE_LOW_PHYS_END)
		return 0;

	if (mem_start < KEYSTONE_HIGH_PHYS_START ||
	    mem_end   > KEYSTONE_HIGH_PHYS_END) {
		pr_crit("Invalid address space for memory (%08llx-%08llx)\n",
		        mem_start, mem_end);
		return 0;
	}

	offset = KEYSTONE_HIGH_PHYS_START - KEYSTONE_LOW_PHYS_START;

	/* Populate the arch idmap hook */
	arch_phys_to_idmap_offset = -offset;

	return offset;
}

static const char *const keystone_match[] __initconst = {
	"ti,k2hk",
	"ti,k2e",
	"ti,k2l",
	"ti,k2g",
	"ti,keystone",
	NULL,
};

DT_MACHINE_START(KEYSTONE, "Keystone")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.smp		= smp_ops(keystone_smp_ops),
	.init_machine	= keystone_init,
	.dt_compat	= keystone_match,
	.pv_fixup	= keystone_pv_fixup,
MACHINE_END
