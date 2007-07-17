/*
 * arch/sh/boards/renesas/r7780rp/setup.c
 *
 * Renesas Solutions Highlander Support.
 *
 * Copyright (C) 2002 Atom Create Engineering Co., Ltd.
 * Copyright (C) 2005 - 2007 Paul Mundt
 *
 * This contains support for the R7780RP-1, R7780MP, and R7785RP
 * Highlander modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pata_platform.h>
#include <asm/machvec.h>
#include <asm/r7780rp.h>
#include <asm/clock.h>
#include <asm/io.h>

static struct resource r8a66597_usb_host_resources[] = {
	[0] = {
		.name	= "r8a66597_hcd",
		.start	= 0xA4200000,
		.end	= 0xA42000FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "r8a66597_hcd",
		.start	= 11,		/* irq number */
		.end	= 11,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device r8a66597_usb_host_device = {
	.name		= "r8a66597_hcd",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,		/* don't use dma */
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(r8a66597_usb_host_resources),
	.resource	= r8a66597_usb_host_resources,
};

static struct resource m66592_usb_peripheral_resources[] = {
	[0] = {
		.name	= "m66592_udc",
		.start	= 0xb0000000,
		.end	= 0xb00000FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "m66592_udc",
		.start	= 9,		/* irq number */
		.end	= 9,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device m66592_usb_peripheral_device = {
	.name		= "m66592_udc",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,		/* don't use dma */
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(m66592_usb_peripheral_resources),
	.resource	= m66592_usb_peripheral_resources,
};

static struct resource cf_ide_resources[] = {
	[0] = {
		.start	= PA_AREA5_IO + 0x1000,
		.end	= PA_AREA5_IO + 0x1000 + 0x08 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PA_AREA5_IO + 0x80c,
		.end	= PA_AREA5_IO + 0x80c + 0x16 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
#ifdef CONFIG_SH_R7780RP
		.start	= 4,
#else
		.start	= 1,
#endif
		.flags	= IORESOURCE_IRQ,
	},
};

static struct pata_platform_info pata_info = {
	.ioport_shift	= 1,
};

static struct platform_device cf_ide_device  = {
	.name		= "pata_platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(cf_ide_resources),
	.resource	= cf_ide_resources,
	.dev	= {
		.platform_data	= &pata_info,
	},
};

static unsigned char heartbeat_bit_pos[] = { 2, 1, 0, 3, 6, 5, 4, 7 };

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PA_OBLED,
		.end	= PA_OBLED + ARRAY_SIZE(heartbeat_bit_pos) - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,

	/* R7785RP has a slightly more sensible FPGA.. */
#ifndef CONFIG_SH_R7785RP
	.dev	= {
		.platform_data	= heartbeat_bit_pos,
	},
#endif
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct platform_device *r7780rp_devices[] __initdata = {
	&r8a66597_usb_host_device,
	&m66592_usb_peripheral_device,
	&cf_ide_device,
	&heartbeat_device,
};

static int __init r7780rp_devices_setup(void)
{
	return platform_add_devices(r7780rp_devices,
				    ARRAY_SIZE(r7780rp_devices));
}
device_initcall(r7780rp_devices_setup);

/*
 * Platform specific clocks
 */
static void ivdr_clk_enable(struct clk *clk)
{
	ctrl_outw(ctrl_inw(PA_IVDRCTL) | (1 << IVDR_CK_ON), PA_IVDRCTL);
}

static void ivdr_clk_disable(struct clk *clk)
{
	ctrl_outw(ctrl_inw(PA_IVDRCTL) & ~(1 << IVDR_CK_ON), PA_IVDRCTL);
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
	if (mach_is_r7780mp() || mach_is_r7785rp())
		ctrl_outw(0x0001, PA_POFF);
}

/*
 * Initialize the board
 */
static void __init highlander_setup(char **cmdline_p)
{
	u16 ver = ctrl_inw(PA_VERREG);
	int i;

	printk(KERN_INFO "Renesas Solutions Highlander %s support.\n",
			 mach_is_r7780rp() ? "R7780RP-1" :
			 mach_is_r7780mp() ? "R7780MP"	 :
					     "R7785RP");

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

	if (mach_is_r7780rp())
		ctrl_outw(0x0001, PA_SDPOW);	/* SD Power ON */

	ctrl_outw(ctrl_inw(PA_IVDRCTL) | 0x01, PA_IVDRCTL);	/* Si13112 */

	pm_power_off = r7780rp_power_off;
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_highlander __initmv = {
	.mv_name		= "Highlander",
	.mv_nr_irqs		= 109,
	.mv_setup		= highlander_setup,
	.mv_init_irq		= highlander_init_irq,
};
