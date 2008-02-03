/*
 * linux/arch/sh/boards/se/7722/setup.c
 *
 * Copyright (C) 2007 Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7722 Support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pata_platform.h>
#include <asm/machvec.h>
#include <asm/se7722.h>
#include <asm/io.h>
#include <asm/heartbeat.h>

/* Heartbeat */
static struct heartbeat_data heartbeat_data = {
	.regsize = 16,
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start  = PA_LED,
		.end    = PA_LED,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name           = "heartbeat",
	.id             = -1,
	.dev = {
		.platform_data = &heartbeat_data,
	},
	.num_resources  = ARRAY_SIZE(heartbeat_resources),
	.resource       = heartbeat_resources,
};

/* SMC91x */
static struct resource smc91x_eth_resources[] = {
	[0] = {
		.name   = "smc91x-regs" ,
		.start  = PA_LAN + 0x300,
		.end    = PA_LAN + 0x300 + 0x10 ,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = SMC_IRQ,
		.end    = SMC_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_eth_device = {
	.name           = "smc91x",
	.id             = 0,
	.dev = {
		.dma_mask               = NULL,         /* don't use dma */
		.coherent_dma_mask      = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(smc91x_eth_resources),
	.resource       = smc91x_eth_resources,
};

static struct resource cf_ide_resources[] = {
	[0] = {
		.start  = PA_MRSHPC_IO + 0x1f0,
		.end    = PA_MRSHPC_IO + 0x1f0 + 8 ,
		.flags  = IORESOURCE_IO,
	},
	[1] = {
		.start  = PA_MRSHPC_IO + 0x1f0 + 0x206,
		.end    = PA_MRSHPC_IO + 0x1f0 +8 + 0x206 + 8,
		.flags  = IORESOURCE_IO,
	},
	[2] = {
		.start  = MRSHPC_IRQ0,
		.end    = MRSHPC_IRQ0,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device cf_ide_device  = {
	.name           = "pata_platform",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(cf_ide_resources),
	.resource       = cf_ide_resources,
};

static struct platform_device *se7722_devices[] __initdata = {
	&heartbeat_device,
	&smc91x_eth_device,
	&cf_ide_device,
};

static int __init se7722_devices_setup(void)
{
	return platform_add_devices(se7722_devices,
		ARRAY_SIZE(se7722_devices));
}
device_initcall(se7722_devices_setup);

static void __init se7722_setup(char **cmdline_p)
{
	ctrl_outw(0x010D, FPGA_OUT);    /* FPGA */

	ctrl_outl(0x00051001, MSTPCR0);
	ctrl_outl(0x00000000, MSTPCR1);
	/* KEYSC, VOU, BEU, CEU, VEU, VPU, LCDC, USB */
	ctrl_outl(0xffffb7c0, MSTPCR2);

	ctrl_outw(0x0000, PORT_PECR);   /* PORT E 1 = IRQ5 ,E 0 = BS */
	ctrl_outw(0x1000, PORT_PJCR);   /* PORT J 1 = IRQ1,J 0 =IRQ0 */

	/* LCDC I/O */
	ctrl_outw(0x0020, PORT_PSELD);

	/* SIOF1*/
	ctrl_outw(0x0003, PORT_PSELB);
	ctrl_outw(0xe000, PORT_PSELC);
	ctrl_outw(0x0000, PORT_PKCR);

	/* LCDC */
	ctrl_outw(0x4020, PORT_PHCR);
	ctrl_outw(0x0000, PORT_PLCR);
	ctrl_outw(0x0000, PORT_PMCR);
	ctrl_outw(0x0002, PORT_PRCR);
	ctrl_outw(0x0000, PORT_PXCR);   /* LCDC,CS6A */

	/* KEYSC */
	ctrl_outw(0x0A10, PORT_PSELA); /* BS,SHHID2 */
	ctrl_outw(0x0000, PORT_PYCR);
	ctrl_outw(0x0000, PORT_PZCR);
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_se7722 __initmv = {
	.mv_name                = "Solution Engine 7722" ,
	.mv_setup               = se7722_setup ,
	.mv_nr_irqs		= SE7722_FPGA_IRQ_BASE + SE7722_FPGA_IRQ_NR,
	.mv_init_irq		= init_se7722_IRQ,
};
