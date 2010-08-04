/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <linux/proc_fs.h>
#include <linux/sysctl.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/time.h>
#include <asm/pmu.h>

#include <asm/mach/arch.h>
#include <mach/dma.h>
#include <mach/hardware.h>
#include <mach/csp/mm_io.h>
#include <mach/csp/chipcHw_def.h>
#include <mach/csp/chipcHw_inline.h>

#include <cfg_global.h>

#include "core.h"

HW_DECLARE_SPINLOCK(arch)
HW_DECLARE_SPINLOCK(gpio)
#if defined(CONFIG_DEBUG_SPINLOCK)
    EXPORT_SYMBOL(bcmring_gpio_reg_lock);
#endif

/* sysctl */
int bcmring_arch_warm_reboot;	/* do a warm reboot on hard reset */

static struct ctl_table_header *bcmring_sysctl_header;

static struct ctl_table bcmring_sysctl_warm_reboot[] = {
	{
	 .procname = "warm",
	 .data = &bcmring_arch_warm_reboot,
	 .maxlen = sizeof(int),
	 .mode = 0644,
	 .proc_handler = proc_dointvec},
	{}
};

static struct ctl_table bcmring_sysctl_reboot[] = {
	{
	 .procname = "reboot",
	 .mode = 0555,
	 .child = bcmring_sysctl_warm_reboot},
	{}
};

static struct resource nand_resource[] = {
	[0] = {
		.start = MM_ADDR_IO_NAND,
		.end = MM_ADDR_IO_NAND + 0x1000 - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device nand_device = {
	.name = "bcm-nand",
	.id = -1,
	.resource = nand_resource,
	.num_resources	= ARRAY_SIZE(nand_resource),
};

static struct resource pmu_resource = {
	.start	= IRQ_PMUIRQ,
	.end	= IRQ_PMUIRQ,
	.flags	= IORESOURCE_IRQ,
};

static struct platform_device pmu_device = {
	.name		= "arm-pmu",
	.id		= ARM_PMU_DEVICE_CPU,
	.resource	= &pmu_resource,
	.num_resources	= 1,
};


static struct platform_device *devices[] __initdata = {
	&nand_device,
	&pmu_device,
};

/****************************************************************************
*
*   Called from the customize_machine function in arch/arm/kernel/setup.c
*
*   The customize_machine function is tagged as an arch_initcall
*   (see include/linux/init.h for the order that the various init sections
*   are called in.
*
*****************************************************************************/
static void __init bcmring_init_machine(void)
{

	bcmring_sysctl_header = register_sysctl_table(bcmring_sysctl_reboot);

	/* Enable spread spectrum */
	chipcHw_enableSpreadSpectrum();

	platform_add_devices(devices, ARRAY_SIZE(devices));

	bcmring_amba_init();

	dma_init();
}

/****************************************************************************
*
*   Called from setup_arch (in arch/arm/kernel/setup.c) to fixup any tags
*   passed in by the boot loader.
*
*****************************************************************************/

static void __init bcmring_fixup(struct machine_desc *desc,
     struct tag *t, char **cmdline, struct meminfo *mi) {
#ifdef CONFIG_BLK_DEV_INITRD
	printk(KERN_NOTICE "bcmring_fixup\n");
	t->hdr.tag = ATAG_CORE;
	t->hdr.size = tag_size(tag_core);
	t->u.core.flags = 0;
	t->u.core.pagesize = PAGE_SIZE;
	t->u.core.rootdev = 31 << 8 | 0;
	t = tag_next(t);

	t->hdr.tag = ATAG_MEM;
	t->hdr.size = tag_size(tag_mem32);
	t->u.mem.start = CFG_GLOBAL_RAM_BASE;
	t->u.mem.size = CFG_GLOBAL_RAM_SIZE;

	t = tag_next(t);

	t->hdr.tag = ATAG_NONE;
	t->hdr.size = 0;
#endif
}

/****************************************************************************
*
*   Machine Description
*
*****************************************************************************/

MACHINE_START(BCMRING, "BCMRING")
	/* Maintainer: Broadcom Corporation */
	.phys_io = MM_IO_START,
	.io_pg_offst = (MM_IO_BASE >> 18) & 0xfffc,
	.fixup = bcmring_fixup,
	.map_io = bcmring_map_io,
	.init_irq = bcmring_init_irq,
	.timer = &bcmring_timer,
	.init_machine = bcmring_init_machine
MACHINE_END
