// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-mmp/common.c
 *
 *  Code common to PXA168 processor lines
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include "addr-map.h"
#include <linux/soc/mmp/cputype.h>

#include "common.h"

#define MMP_CHIPID	CIU_REG(0x00)

unsigned int mmp_chip_id;
EXPORT_SYMBOL(mmp_chip_id);

static struct map_desc standard_io_desc[] __initdata = {
	{
		.pfn		= __phys_to_pfn(APB_PHYS_BASE),
		.virtual	= (unsigned long)APB_VIRT_BASE,
		.length		= APB_PHYS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.pfn		= __phys_to_pfn(AXI_PHYS_BASE),
		.virtual	= (unsigned long)AXI_VIRT_BASE,
		.length		= AXI_PHYS_SIZE,
		.type		= MT_DEVICE,
	},
};

static struct map_desc mmp2_io_desc[] __initdata = {
	{
		.pfn		= __phys_to_pfn(PGU_PHYS_BASE),
		.virtual	= (unsigned long)PGU_VIRT_BASE,
		.length		= PGU_PHYS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init mmp_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));

	/* this is early, initialize mmp_chip_id here */
	mmp_chip_id = __raw_readl(MMP_CHIPID);
}

void __init mmp2_map_io(void)
{
	mmp_map_io();
	iotable_init(mmp2_io_desc, ARRAY_SIZE(mmp2_io_desc));
}

void mmp_restart(enum reboot_mode mode, const char *cmd)
{
	soft_restart(0);
}
