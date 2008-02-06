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
#include <linux/ata_platform.h>
#include <linux/types.h>
#include <net/ax88796.h>
#include <asm/machvec.h>
#include <asm/r7780rp.h>
#include <asm/clock.h>
#include <asm/heartbeat.h>
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
		.start	= IRQ_EXT1,		/* irq number */
		.end	= IRQ_EXT1,
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
		.start	= IRQ_EXT4,		/* irq number */
		.end	= IRQ_EXT4,
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
		.start	= IRQ_CF,
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

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PA_OBLED,
		.end	= PA_OBLED,
		.flags	= IORESOURCE_MEM,
	},
};

#ifndef CONFIG_SH_R7785RP
static unsigned char heartbeat_bit_pos[] = { 2, 1, 0, 3, 6, 5, 4, 7 };

static struct heartbeat_data heartbeat_data = {
	.bit_pos	= heartbeat_bit_pos,
	.nr_bits	= ARRAY_SIZE(heartbeat_bit_pos),
};
#endif

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,

	/* R7785RP has a slightly more sensible FPGA.. */
#ifndef CONFIG_SH_R7785RP
	.dev	= {
		.platform_data	= &heartbeat_data,
	},
#endif
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct ax_plat_data ax88796_platdata = {
	.flags          = AXFLG_HAS_93CX6,
	.wordlength     = 2,
	.dcr_val        = 0x1,
	.rcr_val        = 0x40,
};

static struct resource ax88796_resources[] = {
	{
#ifdef CONFIG_SH_R7780RP
		.start  = 0xa5800400,
		.end    = 0xa5800400 + (0x20 * 0x2) - 1,
#else
		.start  = 0xa4100400,
		.end    = 0xa4100400 + (0x20 * 0x2) - 1,
#endif
		.flags  = IORESOURCE_MEM,
	},
	{
		.start  = IRQ_AX88796,
		.end    = IRQ_AX88796,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device ax88796_device = {
	.name           = "ax88796",
	.id             = 0,

	.dev    = {
		.platform_data = &ax88796_platdata,
	},

	.num_resources  = ARRAY_SIZE(ax88796_resources),
	.resource       = ax88796_resources,
};


static struct platform_device *r7780rp_devices[] __initdata = {
	&r8a66597_usb_host_device,
	&m66592_usb_peripheral_device,
	&heartbeat_device,
#ifndef CONFIG_SH_R7780RP
	&cf_ide_device,
	&ax88796_device,
#endif
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

static inline unsigned char is_ide_ioaddr(unsigned long addr)
{
	return ((cf_ide_resources[0].start <= addr &&
		 addr <= cf_ide_resources[0].end) ||
		(cf_ide_resources[1].start <= addr &&
		 addr <= cf_ide_resources[1].end));
}

void highlander_writeb(u8 b, void __iomem *addr)
{
	unsigned long tmp = (unsigned long __force)addr;

	if (is_ide_ioaddr(tmp))
		ctrl_outw((u16)b, tmp);
	else
		ctrl_outb(b, tmp);
}

u8 highlander_readb(void __iomem *addr)
{
	unsigned long tmp = (unsigned long __force)addr;

	if (is_ide_ioaddr(tmp))
		return ctrl_inw(tmp) & 0xff;
	else
		return ctrl_inb(tmp);
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

static unsigned char irl2irq[HL_NR_IRL];

int highlander_irq_demux(int irq)
{
	if (irq >= HL_NR_IRL || !irl2irq[irq])
		return irq;

	return irl2irq[irq];
}

void __init highlander_init_irq(void)
{
	unsigned char *ucp = NULL;

	do {
#ifdef CONFIG_SH_R7780MP
		ucp = highlander_init_irq_r7780mp();
		if (ucp)
			break;
#endif
#ifdef CONFIG_SH_R7785RP
		ucp = highlander_init_irq_r7785rp();
		if (ucp)
			break;
#endif
#ifdef CONFIG_SH_R7780RP
		ucp = highlander_init_irq_r7780rp();
		if (ucp)
			break;
#endif
	} while (0);

	if (ucp) {
		plat_irq_setup_pins(IRQ_MODE_IRL3210);
		memcpy(irl2irq, ucp, HL_NR_IRL);
	}
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_highlander __initmv = {
	.mv_name		= "Highlander",
	.mv_setup		= highlander_setup,
	.mv_init_irq		= highlander_init_irq,
	.mv_irq_demux		= highlander_irq_demux,
	.mv_readb		= highlander_readb,
	.mv_writeb		= highlander_writeb,
};
