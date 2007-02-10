/*
 * SH7206 Setup
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
		.mapbase	= 0xfffe8000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 241, 242, 243, 240},
	}, {
		.mapbase	= 0xfffe8800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 247, 244, 245, 246},
	}, {
		.mapbase	= 0xfffe9000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 249, 250, 251, 248},
	}, {
		.mapbase	= 0xfffe9800,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		=  { 253, 254, 255, 252},
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

static struct platform_device *sh7206_devices[] __initdata = {
	&sci_device,
};

static int __init sh7206_devices_setup(void)
{
	return platform_add_devices(sh7206_devices,
				    ARRAY_SIZE(sh7206_devices));
}
__initcall(sh7206_devices_setup);

#define INTC_IPR08     0xfffe0c04UL
#define INTC_IPR09     0xfffe0c06UL
#define INTC_IPR14     0xfffe0c10UL

#define CMI0_IRQ       140

#define MTU1_TGI1A     164

#define SCIF0_BRI_IRQ  240
#define SCIF0_ERI_IRQ  241
#define SCIF0_RXI_IRQ  242
#define SCIF0_TXI_IRQ  243

#define SCIF1_BRI_IRQ  244
#define SCIF1_ERI_IRQ  245
#define SCIF1_RXI_IRQ  246
#define SCIF1_TXI_IRQ  247

#define SCIF2_BRI_IRQ  248
#define SCIF2_ERI_IRQ  249
#define SCIF2_RXI_IRQ  250
#define SCIF2_TXI_IRQ  251

#define SCIF3_BRI_IRQ  252
#define SCIF3_ERI_IRQ  253
#define SCIF3_RXI_IRQ  254
#define SCIF3_TXI_IRQ  255

static struct ipr_data sh7206_ipr_map[] = {
	{ CMI0_IRQ,      INTC_IPR08, 3, 2 },
	{ MTU2_TGI1A,    INTC_IPR09, 1, 2 },
	{ SCIF0_ERI_IRQ, INTC_IPR14, 3, 3 },
	{ SCIF0_RXI_IRQ, INTC_IPR14, 3, 3 },
	{ SCIF0_BRI_IRQ, INTC_IPR14, 3, 3 },
	{ SCIF0_TXI_IRQ, INTC_IPR14, 3, 3 },
	{ SCIF1_ERI_IRQ, INTC_IPR14, 2, 3 },
	{ SCIF1_RXI_IRQ, INTC_IPR14, 2, 3 },
	{ SCIF1_BRI_IRQ, INTC_IPR14, 2, 3 },
	{ SCIF1_TXI_IRQ, INTC_IPR14, 2, 3 },
	{ SCIF2_ERI_IRQ, INTC_IPR14, 1, 3 },
	{ SCIF2_RXI_IRQ, INTC_IPR14, 1, 3 },
	{ SCIF2_BRI_IRQ, INTC_IPR14, 1, 3 },
	{ SCIF2_TXI_IRQ, INTC_IPR14, 1, 3 },
	{ SCIF3_ERI_IRQ, INTC_IPR14, 0, 3 },
	{ SCIF3_RXI_IRQ, INTC_IPR14, 0, 3 },
	{ SCIF3_BRI_IRQ, INTC_IPR14, 0, 3 },
	{ SCIF3_TXI_IRQ, INTC_IPR14, 0, 3 },
};

void __init init_IRQ_ipr(void)
{
	make_ipr_irq(sh7206_ipr_map, ARRAY_SIZE(sh7206_ipr_map));
}
