// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-ks8695/cpu.c
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 CPU support
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "regs-sys.h"
#include <mach/regs-misc.h>


static struct map_desc ks8695_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)KS8695_IO_VA,
		.pfn		= __phys_to_pfn(KS8695_IO_PA),
		.length		= KS8695_IO_SIZE,
		.type		= MT_DEVICE,
	}
};

static void __init ks8695_processor_info(void)
{
	unsigned long id, rev;

	id = __raw_readl(KS8695_MISC_VA + KS8695_DID);
	rev = __raw_readl(KS8695_MISC_VA + KS8695_RID);

	printk("KS8695 ID=%04lx  SubID=%02lx  Revision=%02lx\n", (id & DID_ID), (rev & RID_SUBID), (rev & RID_REVISION));
}

static unsigned int sysclk[8] = { 125000000, 100000000, 62500000, 50000000, 41700000, 33300000, 31300000, 25000000 };
static unsigned int cpuclk[8] = { 166000000, 166000000, 83000000, 83000000, 55300000, 55300000, 41500000, 41500000 };

static void __init ks8695_clock_info(void)
{
	unsigned int scdc = __raw_readl(KS8695_SYS_VA + KS8695_CLKCON) & CLKCON_SCDC;

	printk("Clocks: System %u MHz, CPU %u MHz\n",
			sysclk[scdc] / 1000000, cpuclk[scdc] / 1000000);
}

void __init ks8695_map_io(void)
{
	iotable_init(ks8695_io_desc, ARRAY_SIZE(ks8695_io_desc));

	ks8695_processor_info();
	ks8695_clock_info();
}
