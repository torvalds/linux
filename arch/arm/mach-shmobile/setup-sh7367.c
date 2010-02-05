/*
 * sh7367 processor support
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

static struct platform_device *sh7367_early_devices[] __initdata = {
	&scif0_device,
	&scif1_device,
	&scif2_device,
	&scif3_device,
	&scif4_device,
	&scif5_device,
	&scif6_device,
	&cmt10_device,
};

void __init sh7367_add_standard_devices(void)
{
	platform_add_devices(sh7367_early_devices,
			     ARRAY_SIZE(sh7367_early_devices));
}

#define SYMSTPCR2 0xe6158048
#define SYMSTPCR2_CMT1 (1 << 29)

void __init sh7367_add_early_devices(void)
{
	/* enable clock to CMT1 */
	__raw_writel(__raw_readl(SYMSTPCR2) & ~SYMSTPCR2_CMT1, SYMSTPCR2);

	early_platform_add_devices(sh7367_early_devices,
				   ARRAY_SIZE(sh7367_early_devices));
}

enum {
	UNUSED = 0,

	/* interrupt sources INTCA */

	SCIFA0, SCIFA1, SCIFA2, SCIFA3, SCIFA4, SCIFA5, SCIFB,
	CMT10,
};

static struct intc_vect vectors[] = {
	INTC_VECT(CMT10, 0xb00),
	INTC_VECT(SCIFA0, 0xc00), INTC_VECT(SCIFA1, 0xc20),
	INTC_VECT(SCIFA2, 0xc40), INTC_VECT(SCIFA3, 0xc60),
	INTC_VECT(SCIFA4, 0xd20), INTC_VECT(SCIFA5, 0xd40),
	INTC_VECT(SCIFB, 0xd60),
};

static struct intc_mask_reg mask_registers[] = {
	{ 0xe6940094, 0xe69400d4, 8, /* IMR5A / IMCR5A */
	  { 0, 0, 0, 0, SCIFA3, SCIFA2, SCIFA1, SCIFA0 } },
	{ 0xe6940098, 0xe69400d8, 8, /* IMR6A / IMCR6A */
	  { SCIFB, SCIFA5, SCIFA4, 0, 0, 0, 0, 0 } },
	{ 0xe69400a4, 0xe69400e4, 8, /* IMR9A / IMCR9A */
	  { 0, 0, 0, CMT10, 0, 0, 0, 0 } },
};

static struct intc_prio_reg prio_registers[] = {
	{ 0xe6940014, 0, 16, 4, /* IPRFA */ { 0, 0, 0, CMT10 } },
	{ 0xe6940018, 0, 16, 4, /* IPRGA */ { SCIFA0, SCIFA1,
					      SCIFA2, SCIFA3 } },
	{ 0xe6940020, 0, 16, 4, /* IPRIA */ { 0, SCIFA4, 0, 0 } },
	{ 0xe6940034, 0, 16, 4, /* IPRNA */ { SCIFB, SCIFA5, 0, 0 } },
};

static DECLARE_INTC_DESC(intc_desc, "sh7367", vectors, NULL, mask_registers,
			 prio_registers, NULL);

void __init sh7367_init_irq(void)
{
	register_intc_controller(&intc_desc);
}
