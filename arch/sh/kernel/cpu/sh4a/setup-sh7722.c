/*
 * SH7722 Setup
 *
 *  Copyright (C) 2006  Paul Mundt
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
		.mapbase	= 0xffe00000,
		.flags		= UPF_BOOT_AUTOCONF,
		.type		= PORT_SCIF,
		.irqs		= { 80, 81, 83, 82 },
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

static struct platform_device *sh7722_devices[] __initdata = {
	&sci_device,
};

static int __init sh7722_devices_setup(void)
{
	return platform_add_devices(sh7722_devices,
				    ARRAY_SIZE(sh7722_devices));
}
__initcall(sh7722_devices_setup);

static struct ipr_data sh7722_ipr_map[] = {
	/* IRQ, IPR-idx, shift, prio */
	{ 16, 0, 12, 2 }, /* TMU0 */
	{ 17, 0,  8, 2 }, /* TMU1 */
	{ 80, 6, 12, 3 }, /* SCIF ERI */
	{ 81, 6, 12, 3 }, /* SCIF RXI */
	{ 82, 6, 12, 3 }, /* SCIF BRI */
	{ 83, 6, 12, 3 }, /* SCIF TXI */
};

static unsigned long ipr_offsets[] = {
	0xa4080000, /*  0: IPRA */
	0xa4080004, /*  1: IPRB */
	0xa4080008, /*  2: IPRC */
	0xa408000c, /*  3: IPRD */
	0xa4080010, /*  4: IPRE */
	0xa4080014, /*  5: IPRF */
	0xa4080018, /*  6: IPRG */
	0xa408001c, /*  7: IPRH */
	0xa4080020, /*  8: IPRI */
	0xa4080024, /*  9: IPRJ */
	0xa4080028, /* 10: IPRK */
	0xa408002c, /* 11: IPRL */
};

unsigned int map_ipridx_to_addr(int idx)
{
	if (unlikely(idx >= ARRAY_SIZE(ipr_offsets)))
		return 0;
	return ipr_offsets[idx];
}

void __init init_IRQ_ipr(void)
{
	make_ipr_irq(sh7722_ipr_map, ARRAY_SIZE(sh7722_ipr_map));
}
