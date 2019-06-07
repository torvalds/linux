// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/sh/boards/se/7780/setup.c
 *
 * Copyright (C) 2006,2007  Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7780 Support.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/machvec.h>
#include <mach-se/mach/se7780.h>
#include <asm/io.h>
#include <asm/heartbeat.h>

/* Heartbeat */
static struct resource heartbeat_resource = {
	.start  = PA_LED,
	.end    = PA_LED,
	.flags  = IORESOURCE_MEM | IORESOURCE_MEM_16BIT,
};

static struct platform_device heartbeat_device = {
	.name           = "heartbeat",
	.id             = -1,
	.num_resources  = 1,
	.resource       = &heartbeat_resource,
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

static struct platform_device *se7780_devices[] __initdata = {
	&heartbeat_device,
	&smc91x_eth_device,
};

static int __init se7780_devices_setup(void)
{
	return platform_add_devices(se7780_devices,
		ARRAY_SIZE(se7780_devices));
}
device_initcall(se7780_devices_setup);

#define GPIO_PHCR        0xFFEA000E
#define GPIO_PMSELR      0xFFEA0080
#define GPIO_PECR        0xFFEA0008

static void __init se7780_setup(char **cmdline_p)
{
	/* "SH-Linux" on LED Display */
	__raw_writew( 'S' , PA_LED_DISP + (DISP_SEL0_ADDR << 1) );
	__raw_writew( 'H' , PA_LED_DISP + (DISP_SEL1_ADDR << 1) );
	__raw_writew( '-' , PA_LED_DISP + (DISP_SEL2_ADDR << 1) );
	__raw_writew( 'L' , PA_LED_DISP + (DISP_SEL3_ADDR << 1) );
	__raw_writew( 'i' , PA_LED_DISP + (DISP_SEL4_ADDR << 1) );
	__raw_writew( 'n' , PA_LED_DISP + (DISP_SEL5_ADDR << 1) );
	__raw_writew( 'u' , PA_LED_DISP + (DISP_SEL6_ADDR << 1) );
	__raw_writew( 'x' , PA_LED_DISP + (DISP_SEL7_ADDR << 1) );

	printk(KERN_INFO "Hitachi UL Solutions Engine 7780SE03 support.\n");

	/*
	 * PCI REQ/GNT setting
	 *   REQ0/GNT0 -> USB
	 *   REQ1/GNT1 -> PC Card
	 *   REQ2/GNT2 -> Serial ATA
	 *   REQ3/GNT3 -> PCI slot
	 */
	__raw_writew(0x0213, FPGA_REQSEL);

	/* GPIO setting */
	__raw_writew(0x0000, GPIO_PECR);
	__raw_writew(__raw_readw(GPIO_PHCR)&0xfff3, GPIO_PHCR);
	__raw_writew(0x0c00, GPIO_PMSELR);

	/* iVDR Power ON */
	__raw_writew(0x0001, FPGA_IVDRPW);
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_se7780 __initmv = {
	.mv_name                = "Solution Engine 7780" ,
	.mv_setup               = se7780_setup ,
	.mv_init_irq		= init_se7780_IRQ,
};
