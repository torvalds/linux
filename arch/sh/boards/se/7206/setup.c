/*
 *
 * linux/arch/sh/boards/se/7206/setup.c
 *
 * Copyright (C) 2006  Yoshinori Sato
 *
 * Hitachi 7206 SolutionEngine Support.
 *
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/se7206.h>
#include <asm/machvec.h>

static struct resource smc91x_resources[] = {
	[0] = {
		.start		= 0x300,
		.end		= 0x300 + 0x020 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= 64,
		.end		= 64,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static int __init se7206_devices_setup(void)
{
	return platform_device_register(&smc91x_device);
}

__initcall(se7206_devices_setup);

void heartbeat_se(void);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_se __initmv = {
	.mv_name		= "SolutionEngine",
	.mv_nr_irqs		= 256,
	.mv_inb			= se7206_inb,
	.mv_inw			= se7206_inw,
	.mv_inl			= se7206_inl,
	.mv_outb		= se7206_outb,
	.mv_outw		= se7206_outw,
	.mv_outl		= se7206_outl,

	.mv_inb_p		= se7206_inb_p,
	.mv_inw_p		= se7206_inw,
	.mv_inl_p		= se7206_inl,
	.mv_outb_p		= se7206_outb_p,
	.mv_outw_p		= se7206_outw,
	.mv_outl_p		= se7206_outl,

	.mv_insb		= se7206_insb,
	.mv_insw		= se7206_insw,
	.mv_insl		= se7206_insl,
	.mv_outsb		= se7206_outsb,
	.mv_outsw		= se7206_outsw,
	.mv_outsl		= se7206_outsl,

	.mv_init_irq		= init_se7206_IRQ,
	.mv_irq_demux		= se7206_irq_demux,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_se,
#endif
};
ALIAS_MV(se)
