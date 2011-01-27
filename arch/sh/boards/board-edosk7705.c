/*
 * arch/sh/boards/renesas/edosk7705/setup.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 * Modified for edosk7705 development
 * board by S. Dunn, 2003.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/smc91x.h>
#include <asm/machvec.h>
#include <asm/sizes.h>

#define SMC_IOBASE	0xA2000000
#define SMC_IO_OFFSET	0x300
#define SMC_IOADDR	(SMC_IOBASE + SMC_IO_OFFSET)

#define ETHERNET_IRQ	0x09

static void __init sh_edosk7705_init_irq(void)
{
	make_imask_irq(ETHERNET_IRQ);
}

/* eth initialization functions */
static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_IO_SHIFT_1 | IORESOURCE_IRQ_LOWLEVEL,
};

static struct resource smc91x_res[] = {
	[0] = {
		.start	= SMC_IOADDR,
		.end	= SMC_IOADDR + SZ_32 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= ETHERNET_IRQ,
		.end	= ETHERNET_IRQ,
		.flags	= IORESOURCE_IRQ ,
	}
};

static struct platform_device smc91x_dev = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_res),
	.resource	= smc91x_res,

	.dev	= {
		.platform_data	= &smc91x_info,
	},
};

/* platform init code */
static struct platform_device *edosk7705_devices[] __initdata = {
	&smc91x_dev,
};

static int __init init_edosk7705_devices(void)
{
	return platform_add_devices(edosk7705_devices,
				    ARRAY_SIZE(edosk7705_devices));
}
device_initcall(init_edosk7705_devices);

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_edosk7705 __initmv = {
	.mv_name		= "EDOSK7705",
	.mv_nr_irqs		= 80,
	.mv_init_irq		= sh_edosk7705_init_irq,
};
