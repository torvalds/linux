/*
 * sh7372 processor support
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2008  Yoshihiro Shimoda
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/serial_sci.h>
#include <linux/sh_intc.h>
#include <linux/sh_timer.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* SCIF */
static struct plat_sci_port scif0_platform_data = {
	.mapbase	= 0xe6c40000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs		= { 80, 80, 80, 80 },
};

static struct platform_device scif0_device = {
	.name		= "sh-sci",
	.id		= 0,
	.dev		= {
		.platform_data	= &scif0_platform_data,
	},
};

static struct plat_sci_port scif1_platform_data = {
	.mapbase	= 0xe6c50000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs           = { 81, 81, 81, 81 },
};

static struct platform_device scif1_device = {
	.name		= "sh-sci",
	.id		= 1,
	.dev		= {
		.platform_data	= &scif1_platform_data,
	},
};

static struct plat_sci_port scif2_platform_data = {
	.mapbase	= 0xe6c60000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs           = { 82, 82, 82, 82 },
};

static struct platform_device scif2_device = {
	.name		= "sh-sci",
	.id		= 2,
	.dev		= {
		.platform_data	= &scif2_platform_data,
	},
};

static struct plat_sci_port scif3_platform_data = {
	.mapbase	= 0xe6c70000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs           = { 83, 83, 83, 83 },
};

static struct platform_device scif3_device = {
	.name		= "sh-sci",
	.id		= 3,
	.dev		= {
		.platform_data	= &scif3_platform_data,
	},
};

static struct plat_sci_port scif4_platform_data = {
	.mapbase	= 0xe6c80000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs           = { 89, 89, 89, 89 },
};

static struct platform_device scif4_device = {
	.name		= "sh-sci",
	.id		= 4,
	.dev		= {
		.platform_data	= &scif4_platform_data,
	},
};

static struct plat_sci_port scif5_platform_data = {
	.mapbase	= 0xe6cb0000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs           = { 90, 90, 90, 90 },
};

static struct platform_device scif5_device = {
	.name		= "sh-sci",
	.id		= 5,
	.dev		= {
		.platform_data	= &scif5_platform_data,
	},
};

static struct plat_sci_port scif6_platform_data = {
	.mapbase	= 0xe6c30000,
	.flags		= UPF_BOOT_AUTOCONF,
	.type		= PORT_SCIF,
	.irqs           = { 91, 91, 91, 91 },
};

static struct platform_device scif6_device = {
	.name		= "sh-sci",
	.id		= 6,
	.dev		= {
		.platform_data	= &scif6_platform_data,
	},
};

/* CMT */
static struct sh_timer_config cmt10_platform_data = {
	.name = "CMT10",
	.channel_offset = 0x10,
	.timer_bit = 0,
	.clk = "r_clk",
	.clockevent_rating = 125,
	.clocksource_rating = 125,
};

static struct resource cmt10_resources[] = {
	[0] = {
		.name	= "CMT10",
		.start	= 0xe6138010,
		.end	= 0xe613801b,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 72,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device cmt10_device = {
	.name		= "sh_cmt",
	.id		= 10,
	.dev = {
		.platform_data	= &cmt10_platform_data,
	},
	.resource	= cmt10_resources,
	.num_resources	= ARRAY_SIZE(cmt10_resources),
};

/* I2C */
static struct resource iic0_resources[] = {
	[0] = {
		.name	= "IIC0",
		.start  = 0xFFF20000,
		.end    = 0xFFF20425 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = intcs_evt2irq(0xe00),
		.end    = intcs_evt2irq(0xe60),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device iic0_device = {
	.name           = "i2c-sh_mobile",
	.id             = 0, /* "i2c0" clock */
	.num_resources  = ARRAY_SIZE(iic0_resources),
	.resource       = iic0_resources,
};

static struct resource iic1_resources[] = {
	[0] = {
		.name	= "IIC1",
		.start  = 0xE6C20000,
		.end    = 0xE6C20425 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 44,
		.end    = 47,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device iic1_device = {
	.name           = "i2c-sh_mobile",
	.id             = 1, /* "i2c1" clock */
	.num_resources  = ARRAY_SIZE(iic1_resources),
	.resource       = iic1_resources,
};

static struct platform_device *sh7372_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&cmt10_device,
	&iic0_device,
	&iic1_device,
};

void __init sh7372_add_standard_devices(void)
{
	platform_add_devices(sh7372_early_devices,
			    ARRAY_SIZE(sh7372_early_devices));
}

#define SMSTPCR3 0xe615013c
#define SMSTPCR3_CMT1 (1 << 29)

void __init sh7372_add_early_devices(void)
{
	/* enable clock to CMT1 */
	__raw_writel(__raw_readl(SMSTPCR3) & ~SMSTPCR3_CMT1, SMSTPCR3);

	early_platform_add_devices(sh7372_early_devices,
				   ARRAY_SIZE(sh7372_early_devices));
}
