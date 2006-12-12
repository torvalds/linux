/*
 * arch/sh/boards/renesas/r7780rp/setup.c
 *
 * Copyright (C) 2002 Atom Create Engineering Co., Ltd.
 * Copyright (C) 2005, 2006 Paul Mundt
 *
 * Renesas Solutions Highlander R7780RP-1 Support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/machvec.h>
#include <asm/r7780rp.h>
#include <asm/clock.h>
#include <asm/io.h>

extern void heartbeat_r7780rp(void);
extern void init_r7780rp_IRQ(void);

static struct resource m66596_usb_host_resources[] = {
	[0] = {
		.start	= 0xa4800000,
		.end	= 0xa4ffffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 6,		/* irq number */
		.end	= 6,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device m66596_usb_host_device = {
	.name		= "m66596-hcd",
	.id		= 0,
	.dev = {
		.dma_mask		= NULL,		/* don't use dma */
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(m66596_usb_host_resources),
	.resource	= m66596_usb_host_resources,
};

static struct resource cf_ide_resources[] = {
	[0] = {
		.start	= 0x1f0,
		.end	= 0x1f0 + 8,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		.start	= 0x1f0 + 0x206,
		.end	= 0x1f0 + 8 + 0x206 + 8,
		.flags	= IORESOURCE_IO,
	},
	[2] = {
#ifdef CONFIG_SH_R7780MP
		.start	= 1,
#else
		.start	= 4,
#endif
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cf_ide_device  = {
	.name		= "pata_platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(cf_ide_resources),
	.resource	= cf_ide_resources,
};

static struct platform_device *r7780rp_devices[] __initdata = {
	&m66596_usb_host_device,
	&cf_ide_device,
};

static int __init r7780rp_devices_setup(void)
{
	return platform_add_devices(r7780rp_devices,
				    ARRAY_SIZE(r7780rp_devices));
}

/*
 * Platform specific clocks
 */
static void ivdr_clk_enable(struct clk *clk)
{
	ctrl_outw(ctrl_inw(PA_IVDRCTL) | (1 << 8), PA_IVDRCTL);
}

static void ivdr_clk_disable(struct clk *clk)
{
	ctrl_outw(ctrl_inw(PA_IVDRCTL) & ~(1 << 8), PA_IVDRCTL);
}

static struct clk_ops ivdr_clk_ops = {
	.enable		= ivdr_clk_enable,
	.disable	= ivdr_clk_disable,
};

static struct clk ivdr_clk = {
	.name		= "ivdr_clk",
	.ops		= &ivdr_clk_ops,
};

static struct clk *r7780rp_clocks[] = {
	&ivdr_clk,
};

static void r7780rp_power_off(void)
{
#ifdef CONFIG_SH_R7780MP
	ctrl_outw(0x0001, PA_POFF);
#endif
}

/*
 * Initialize the board
 */
static void __init r7780rp_setup(char **cmdline_p)
{
	u16 ver = ctrl_inw(PA_VERREG);
	int i;

	device_initcall(r7780rp_devices_setup);

	printk(KERN_INFO "Renesas Solutions Highlander R7780RP-1 support.\n");

	printk(KERN_INFO "Board version: %d (revision %d), "
			 "FPGA version: %d (revision %d)\n",
			 (ver >> 12) & 0xf, (ver >> 8) & 0xf,
			 (ver >>  4) & 0xf, ver & 0xf);

	/*
	 * Enable the important clocks right away..
	 */
	for (i = 0; i < ARRAY_SIZE(r7780rp_clocks); i++) {
		struct clk *clk = r7780rp_clocks[i];

		clk_register(clk);
		clk_enable(clk);
	}

	ctrl_outw(0x0000, PA_OBLED);	/* Clear LED. */
#ifndef CONFIG_SH_R7780MP
	ctrl_outw(0x0001, PA_SDPOW);	/* SD Power ON */
#endif
	ctrl_outw(ctrl_inw(PA_IVDRCTL) | 0x0100, PA_IVDRCTL);	/* Si13112 */

	pm_power_off = r7780rp_power_off;
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_r7780rp __initmv = {
	.mv_name		= "Highlander R7780RP-1",
	.mv_setup		= r7780rp_setup,

	.mv_nr_irqs		= 109,

	.mv_inb			= r7780rp_inb,
	.mv_inw			= r7780rp_inw,
	.mv_inl			= r7780rp_inl,
	.mv_outb		= r7780rp_outb,
	.mv_outw		= r7780rp_outw,
	.mv_outl		= r7780rp_outl,

	.mv_inb_p		= r7780rp_inb_p,
	.mv_inw_p		= r7780rp_inw,
	.mv_inl_p		= r7780rp_inl,
	.mv_outb_p		= r7780rp_outb_p,
	.mv_outw_p		= r7780rp_outw,
	.mv_outl_p		= r7780rp_outl,

	.mv_insb		= r7780rp_insb,
	.mv_insw		= r7780rp_insw,
	.mv_insl		= r7780rp_insl,
	.mv_outsb		= r7780rp_outsb,
	.mv_outsw		= r7780rp_outsw,
	.mv_outsl		= r7780rp_outsl,

	.mv_ioport_map		= r7780rp_ioport_map,
	.mv_init_irq		= init_r7780rp_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_r7780rp,
#endif
};
ALIAS_MV(r7780rp)
