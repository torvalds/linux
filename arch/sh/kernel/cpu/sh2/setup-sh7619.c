/*
 * SH7619 Setup
 *
 *  Copyright (C) 2006  Yoshinori Sato
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <asm/sci.h>

static struct plat_sci_port sci_platform_data[] = {
	{
		.mapbase	= 0xf8400000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 88, 89, 91, 90},
	}, {
		.mapbase	= 0xf8410000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 92, 93, 95, 94},
	}, {
		.mapbase	= 0xf8420000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 96, 97, 99, 98},
	}, {
		.flags = 0,
	}
};

static struct platform_device sci_device = {
	.name		= "sh-sci",
	.id		= -1,
	.dev		= {
		.platform_data	= sci_platform_data,
	},
};

static struct platform_device *sh7619_devices[] __initdata = {
	&sci_device,
};

static int __init sh7619_devices_setup(void)
{
	return platform_add_devices(sh7619_devices,
				    ARRAY_SIZE(sh7619_devices));
}
__initcall(sh7619_devices_setup);

#define INTC_IPRC      0xf8080000UL
#define INTC_IPRD      0xf8080002UL

#define CMI0_IRQ       86

#define SCIF0_ERI_IRQ  88
#define SCIF0_RXI_IRQ  89
#define SCIF0_BRI_IRQ  90
#define SCIF0_TXI_IRQ  91

#define SCIF1_ERI_IRQ  92
#define SCIF1_RXI_IRQ  93
#define SCIF1_BRI_IRQ  94
#define SCIF1_TXI_IRQ  95

#define SCIF2_BRI_IRQ  96
#define SCIF2_ERI_IRQ  97
#define SCIF2_RXI_IRQ  98
#define SCIF2_TXI_IRQ  99

static struct ipr_data sh7619_ipr_map[] = {
	{ CMI0_IRQ,      INTC_IPRC, 1, 2 },
	{ SCIF0_ERI_IRQ, INTC_IPRD, 3, 3 },
	{ SCIF0_RXI_IRQ, INTC_IPRD, 3, 3 },
	{ SCIF0_BRI_IRQ, INTC_IPRD, 3, 3 },
	{ SCIF0_TXI_IRQ, INTC_IPRD, 3, 3 },
	{ SCIF1_ERI_IRQ, INTC_IPRD, 2, 3 },
	{ SCIF1_RXI_IRQ, INTC_IPRD, 2, 3 },
	{ SCIF1_BRI_IRQ, INTC_IPRD, 2, 3 },
	{ SCIF1_TXI_IRQ, INTC_IPRD, 2, 3 },
	{ SCIF2_ERI_IRQ, INTC_IPRD, 1, 3 },
	{ SCIF2_RXI_IRQ, INTC_IPRD, 1, 3 },
	{ SCIF2_BRI_IRQ, INTC_IPRD, 1, 3 },
	{ SCIF2_TXI_IRQ, INTC_IPRD, 1, 3 },
};

void __init init_IRQ_ipr(void)
{
	make_ipr_irq(sh7619_ipr_map, ARRAY_SIZE(sh7619_ipr_map));
}
