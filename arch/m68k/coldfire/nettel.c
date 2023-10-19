// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	nettel.c -- startup code support for the NETtel boards
 *
 *	Copyright (C) 2009, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/nettel.h>

/***************************************************************************/

/*
 * Define the IO and interrupt resources of the 2 SMC9196 interfaces.
 */
#define	NETTEL_SMC0_ADDR	0x30600300
#define	NETTEL_SMC0_IRQ		29

#define	NETTEL_SMC1_ADDR	0x30600000
#define	NETTEL_SMC1_IRQ		27

/*
 * We need some access into the SMC9196 registers. Define those registers
 * we will need here (including the smc91x.h doesn't seem to give us these
 * in a simple form).
 */
#define	SMC91xx_BANKSELECT	14
#define	SMC91xx_BASEADDR	2
#define	SMC91xx_BASEMAC		4

/***************************************************************************/

static struct resource nettel_smc91x_0_resources[] = {
	{
		.start		= NETTEL_SMC0_ADDR,
		.end		= NETTEL_SMC0_ADDR + 0x20,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= NETTEL_SMC0_IRQ,
		.end		= NETTEL_SMC0_IRQ,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct resource nettel_smc91x_1_resources[] = {
	{
		.start		= NETTEL_SMC1_ADDR,
		.end		= NETTEL_SMC1_ADDR + 0x20,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= NETTEL_SMC1_IRQ,
		.end		= NETTEL_SMC1_IRQ,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device nettel_smc91x[] = {
	{
		.name			= "smc91x",
		.id			= 0,
		.num_resources		= ARRAY_SIZE(nettel_smc91x_0_resources),
		.resource		= nettel_smc91x_0_resources,
	},
	{
		.name			= "smc91x",
		.id			= 1,
		.num_resources		= ARRAY_SIZE(nettel_smc91x_1_resources),
		.resource		= nettel_smc91x_1_resources,
	},
};

static struct platform_device *nettel_devices[] __initdata = {
	&nettel_smc91x[0],
	&nettel_smc91x[1],
};

/***************************************************************************/

static u8 nettel_macdefault[] __initdata = {
	0x00, 0xd0, 0xcf, 0x00, 0x00, 0x01,
};

/*
 * Set flash contained MAC address into SMC9196 core. Make sure the flash
 * MAC address is sane, and not an empty flash. If no good use the Moreton
 * Bay default MAC address instead.
 */

static void __init nettel_smc91x_setmac(unsigned int ioaddr, unsigned int flashaddr)
{
	u16 *macp;

	macp = (u16 *) flashaddr;
	if ((macp[0] == 0xffff) && (macp[1] == 0xffff) && (macp[2] == 0xffff))
		macp = (u16 *) &nettel_macdefault[0];

	writew(1, NETTEL_SMC0_ADDR + SMC91xx_BANKSELECT);
	writew(macp[0], ioaddr + SMC91xx_BASEMAC);
	writew(macp[1], ioaddr + SMC91xx_BASEMAC + 2);
	writew(macp[2], ioaddr + SMC91xx_BASEMAC + 4);
}

/***************************************************************************/

/*
 * Re-map the address space of at least one of the SMC ethernet
 * parts. Both parts power up decoding the same address, so we
 * need to move one of them first, before doing anything else.
 */

static void __init nettel_smc91x_init(void)
{
	writew(0x00ec, MCFSIM_PADDR);
	mcf_setppdata(0, 0x0080);
	writew(1, NETTEL_SMC0_ADDR + SMC91xx_BANKSELECT);
	writew(0x0067, NETTEL_SMC0_ADDR + SMC91xx_BASEADDR);
	mcf_setppdata(0x0080, 0);

	/* Set correct chip select timing for SMC9196 accesses */
	writew(0x1180, MCFSIM_CSCR3);

	/* Set the SMC interrupts to be auto-vectored */
	mcf_autovector(NETTEL_SMC0_IRQ);
	mcf_autovector(NETTEL_SMC1_IRQ);

	/* Set MAC addresses from flash for both interfaces */
	nettel_smc91x_setmac(NETTEL_SMC0_ADDR, 0xf0006000);
	nettel_smc91x_setmac(NETTEL_SMC1_ADDR, 0xf0006006);
}

/***************************************************************************/

static int __init init_nettel(void)
{
	nettel_smc91x_init();
	platform_add_devices(nettel_devices, ARRAY_SIZE(nettel_devices));
	return 0;
}

arch_initcall(init_nettel);

/***************************************************************************/
