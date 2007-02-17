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

static struct ipr_data sh7619_ipr_map[] = {
	{ 86, 0,  4, 2 },	/* CMI0 */
	{ 88, 1, 12, 3 },	/* SCIF0_ERI */
	{ 89, 1, 12, 3 },	/* SCIF0_RXI */
	{ 90, 1, 12, 3 },	/* SCIF0_BRI */
	{ 91, 1, 12, 3 },	/* SCIF0_TXI */
	{ 92, 1,  8, 3 },	/* SCIF1_ERI */
	{ 93, 1,  8, 3 },	/* SCIF1_RXI */
	{ 94, 1,  8, 3 },	/* SCIF1_BRI */
	{ 95, 1,  8, 3 },	/* SCIF1_TXI */
	{ 96, 1,  4, 3 },	/* SCIF2_ERI */
	{ 97, 1,  4, 3 },	/* SCIF2_RXI */
	{ 98, 1,  4, 3 },	/* SCIF2_BRI */
	{ 99, 1,  4, 3 },	/* SCIF2_TXI */
};

static unsigned int ipr_offsets[] = {
	0xf8080000,	/* IPRC */
	0xf8080002,	/* IPRD */
	0xf8080004,	/* IPRE */
	0xf8080006,	/* IPRF */
	0xf8080008,	/* IPRG */
};

/* given the IPR index return the address of the IPR register */
unsigned int map_ipridx_to_addr(int idx)
{
	if (unlikely(idx >= ARRAY_SIZE(ipr_offsets)))
		return 0;
	return ipr_offsets[idx];
}

void __init init_IRQ_ipr(void)
{
	make_ipr_irq(sh7619_ipr_map, ARRAY_SIZE(sh7619_ipr_map));
}
