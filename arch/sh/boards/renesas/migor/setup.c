/*
 * Renesas System Solutions Asia Pte. Ltd - Migo-R
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/sh_keysc.h>

/* Address     IRQ  Size  Bus  Description
 * 0x00000000       64MB  16   NOR Flash (SP29PL256N)
 * 0x0c000000       64MB  64   SDRAM (2xK4M563233G)
 * 0x10000000  IRQ0       16   Ethernet (SMC91C111)
 * 0x14000000  IRQ4       16   USB 2.0 Host Controller (M66596)
 * 0x18000000       8GB    8   NAND Flash (K9K8G08U0A)
 */

static struct resource smc91x_eth_resources[] = {
	[0] = {
		.name   = "smc91x-regs" ,
		.start  = P2SEGADDR(0x10000300),
		.end    = P2SEGADDR(0x1000030f),
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 32, /* IRQ0 */
		.flags  = IORESOURCE_IRQ | IRQF_TRIGGER_HIGH,
	},
};

static struct platform_device smc91x_eth_device = {
	.name           = "smc91x",
	.num_resources  = ARRAY_SIZE(smc91x_eth_resources),
	.resource       = smc91x_eth_resources,
};

static struct sh_keysc_info sh_keysc_info = {
	.mode = SH_KEYSC_MODE_2, /* KEYOUT0->4, KEYIN1->5 */
	.scan_timing = 3,
	.delay = 5,
	.keycodes = {
		0, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ENTER,
		0, KEY_F, KEY_C, KEY_D,	KEY_H, KEY_1,
		0, KEY_2, KEY_3, KEY_4,	KEY_5, KEY_6,
		0, KEY_7, KEY_8, KEY_9, KEY_S, KEY_0,
		0, KEY_P, KEY_STOP, KEY_REWIND, KEY_PLAY, KEY_FASTFORWARD,
	},
};

static struct resource sh_keysc_resources[] = {
	[0] = {
		.start  = 0x044b0000,
		.end    = 0x044b000f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 79,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sh_keysc_device = {
	.name           = "sh_keysc",
	.num_resources  = ARRAY_SIZE(sh_keysc_resources),
	.resource       = sh_keysc_resources,
	.dev	= {
		.platform_data	= &sh_keysc_info,
	},
};

static struct platform_device *migor_devices[] __initdata = {
	&smc91x_eth_device,
	&sh_keysc_device,
};

static int __init migor_devices_setup(void)
{
	return platform_add_devices(migor_devices, ARRAY_SIZE(migor_devices));
}
__initcall(migor_devices_setup);

#define PORT_PJCR       0xA4050110UL
#define PORT_PSELA      0xA405014EUL
#define PORT_PYCR       0xA405014AUL
#define PORT_PZCR       0xA405014CUL
#define PORT_HIZCRA     0xA4050158UL
#define PORT_HIZCRC     0xA405015CUL
#define MSTPCR2         0xA4150038UL

static void __init migor_setup(char **cmdline_p)
{
	/* SMC91C111 - Enable IRQ0 */
	ctrl_outw(ctrl_inw(PORT_PJCR) & ~0x0003, PORT_PJCR);

	/* KEYSC */
	ctrl_outw(ctrl_inw(PORT_PYCR) & ~0x0fff, PORT_PYCR);
	ctrl_outw(ctrl_inw(PORT_PZCR) & ~0x0ff0, PORT_PZCR);
	ctrl_outw(ctrl_inw(PORT_PSELA) & ~0x4100, PORT_PSELA);
	ctrl_outw(ctrl_inw(PORT_HIZCRA) & ~0x4000, PORT_HIZCRA);
	ctrl_outw(ctrl_inw(PORT_HIZCRC) & ~0xc000, PORT_HIZCRC);
	ctrl_outl(ctrl_inl(MSTPCR2) & ~0x00004000, MSTPCR2);
}

static struct sh_machine_vector mv_migor __initmv = {
	.mv_name		= "Migo-R",
	.mv_setup		= migor_setup,
};
