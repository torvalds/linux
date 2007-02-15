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

static struct ipr_data sh7206_ipr_map[] = {
	{ 140,  7, 12, 2 },	/* CMI0 */
	{ 164,  8,  4, 2 },	/* MTU2_TGI1A */
	{ 240, 13, 12, 3 },	/* SCIF0_BRI */
	{ 241, 13, 12, 3 },	/* SCIF0_ERI */
	{ 242, 13, 12, 3 },	/* SCIF0_RXI */
	{ 243, 13, 12, 3 },	/* SCIF0_TXI */
	{ 244, 13,  8, 3 },	/* SCIF1_BRI */
	{ 245, 13,  8, 3 },	/* SCIF1_ERI */
	{ 246, 13,  8, 3 },	/* SCIF1_RXI */
	{ 247, 13,  8, 3 },	/* SCIF1_TXI */
	{ 248, 13,  4, 3 },	/* SCIF2_BRI */
	{ 249, 13,  4, 3 },	/* SCIF2_ERI */
	{ 250, 13,  4, 3 },	/* SCIF2_RXI */
	{ 251, 13,  4, 3 },	/* SCIF2_TXI */
	{ 252, 13,  0, 3 },	/* SCIF3_BRI */
	{ 253, 13,  0, 3 },	/* SCIF3_ERI */
	{ 254, 13,  0, 3 },	/* SCIF3_RXI */
	{ 255, 13,  0, 3 },	/* SCIF3_TXI */
};

static unsigned int ipr_offsets[] = {
	0xfffe0818,	/* IPR01 */
	0xfffe081a,	/* IPR02 */
	0,		/* unused */
	0,		/* unused */
	0xfffe0820,	/* IPR05 */
	0xfffe0c00,	/* IPR06 */
	0xfffe0c02,	/* IPR07 */
	0xfffe0c04,	/* IPR08 */
	0xfffe0c06,	/* IPR09 */
	0xfffe0c08,	/* IPR10 */
	0xfffe0c0a,	/* IPR11 */
	0xfffe0c0c,	/* IPR12 */
	0xfffe0c0e,	/* IPR13 */
	0xfffe0c10,	/* IPR14 */
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
	make_ipr_irq(sh7206_ipr_map, ARRAY_SIZE(sh7206_ipr_map));
}
