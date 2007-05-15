/*
 * linux/arch/sh/boards/se/7300/setup.c
 *
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 *
 * SH-Mobile SolutionEngine 7300 Support.
 *
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/machvec.h>
#include <asm/se7300.h>

void init_7300se_IRQ(void);

static unsigned char heartbeat_bit_pos[] = { 8, 9, 10, 11, 12, 13, 14, 15 };

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PA_LED,
		.end	= PA_LED + ARRAY_SIZE(heartbeat_bit_pos) - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.dev	= {
		.platform_data	= heartbeat_bit_pos,
	},
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct platform_device *se7300_devices[] __initdata = {
	&heartbeat_device,
};

static int __init se7300_devices_setup(void)
{
	return platform_add_devices(se7300_devices, ARRAY_SIZE(se7300_devices));
}
__initcall(se7300_devices_setup);

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_7300se __initmv = {
	.mv_name = "SolutionEngine 7300",
	.mv_nr_irqs = 109,
	.mv_inb = sh7300se_inb,
	.mv_inw = sh7300se_inw,
	.mv_inl = sh7300se_inl,
	.mv_outb = sh7300se_outb,
	.mv_outw = sh7300se_outw,
	.mv_outl = sh7300se_outl,

	.mv_inb_p = sh7300se_inb_p,
	.mv_inw_p = sh7300se_inw,
	.mv_inl_p = sh7300se_inl,
	.mv_outb_p = sh7300se_outb_p,
	.mv_outw_p = sh7300se_outw,
	.mv_outl_p = sh7300se_outl,

	.mv_insb = sh7300se_insb,
	.mv_insw = sh7300se_insw,
	.mv_insl = sh7300se_insl,
	.mv_outsb = sh7300se_outsb,
	.mv_outsw = sh7300se_outsw,
	.mv_outsl = sh7300se_outsl,

	.mv_init_irq = init_7300se_IRQ,
};
