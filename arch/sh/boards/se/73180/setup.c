/*
 * arch/sh/boards/se/73180/setup.c
 *
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 * Based on arch/sh/setup_shmse.c
 *
 * Modified for 73180 SolutionEngine
 *           by YOSHII Takashi <yoshii-takashi@hitachi-ul.co.jp>
 *
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/machvec.h>
#include <asm/se73180.h>
#include <asm/irq.h>

void init_73180se_IRQ(void);

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PA_LED,
		.end	= PA_LED + 8 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct platform_device *se73180_devices[] __initdata = {
	&heartbeat_device,
};

static int __init se73180_devices_setup(void)
{
	return platform_add_devices(se73180_devices,
				    ARRAY_SIZE(se73180_devices));
}
__initcall(se73180_devices_setup);

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_73180se __initmv = {
	.mv_name = "SolutionEngine 73180",
	.mv_nr_irqs = 108,
	.mv_inb = sh73180se_inb,
	.mv_inw = sh73180se_inw,
	.mv_inl = sh73180se_inl,
	.mv_outb = sh73180se_outb,
	.mv_outw = sh73180se_outw,
	.mv_outl = sh73180se_outl,

	.mv_inb_p = sh73180se_inb_p,
	.mv_inw_p = sh73180se_inw,
	.mv_inl_p = sh73180se_inl,
	.mv_outb_p = sh73180se_outb_p,
	.mv_outw_p = sh73180se_outw,
	.mv_outl_p = sh73180se_outl,

	.mv_insb = sh73180se_insb,
	.mv_insw = sh73180se_insw,
	.mv_insl = sh73180se_insl,
	.mv_outsb = sh73180se_outsb,
	.mv_outsw = sh73180se_outsw,
	.mv_outsl = sh73180se_outsl,

	.mv_init_irq = init_73180se_IRQ,
	.mv_irq_demux = shmse_irq_demux,
};
ALIAS_MV(73180se)
