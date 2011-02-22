/*
 *
 * linux/arch/sh/boards/se/7206/setup.c
 *
 * Copyright (C) 2006  Yoshinori Sato
 * Copyright (C) 2007 - 2008  Paul Mundt
 *
 * Hitachi 7206 SolutionEngine Support.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/smc91x.h>
#include <mach-se/mach/se7206.h>
#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/heartbeat.h>

static struct resource smc91x_resources[] = {
	[0] = {
		.name		= "smc91x-regs",
		.start		= PA_SMSC + 0x300,
		.end		= PA_SMSC + 0x300 + 0x020 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= 64,
		.end		= 64,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smc91x_platdata smc91x_info = {
	.flags	= SMC91X_USE_16BIT,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.dev		= {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static unsigned char heartbeat_bit_pos[] = { 8, 9, 10, 11, 12, 13, 14, 15 };

static struct heartbeat_data heartbeat_data = {
	.bit_pos	= heartbeat_bit_pos,
	.nr_bits	= ARRAY_SIZE(heartbeat_bit_pos),
};

static struct resource heartbeat_resource = {
	.start	= PA_LED,
	.end	= PA_LED,
	.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.dev	= {
		.platform_data	= &heartbeat_data,
	},
	.num_resources	= 1,
	.resource	= &heartbeat_resource,
};

static struct platform_device *se7206_devices[] __initdata = {
	&smc91x_device,
	&heartbeat_device,
};

static int __init se7206_devices_setup(void)
{
	return platform_add_devices(se7206_devices, ARRAY_SIZE(se7206_devices));
}
device_initcall(se7206_devices_setup);

static int se7206_mode_pins(void)
{
	return MODE_PIN1 | MODE_PIN2;
}

/*
 * The Machine Vector
 */

static struct sh_machine_vector mv_se __initmv = {
	.mv_name		= "SolutionEngine",
	.mv_nr_irqs		= 256,
	.mv_init_irq		= init_se7206_IRQ,
	.mv_mode_pins		= se7206_mode_pins,
};
