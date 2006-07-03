/*
 * linux/arch/arm/mach-omap2/id.c
 *
 * OMAP2 CPU identification code
 *
 * Copyright (C) 2005 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/io.h>

#define OMAP24XX_TAP_BASE	io_p2v(0x48014000)

#define OMAP_TAP_IDCODE		0x0204
#define OMAP_TAP_PROD_ID	0x0208

#define OMAP_TAP_DIE_ID_0	0x0218
#define OMAP_TAP_DIE_ID_1	0x021C
#define OMAP_TAP_DIE_ID_2	0x0220
#define OMAP_TAP_DIE_ID_3	0x0224

/* system_rev fields for OMAP2 processors:
 *   CPU id bits     [31:16],
 *   CPU device type [15:12], (unprg,normal,POP)
 *   CPU revision    [11:08]
 *   CPU class bits  [07:00]
 */

struct omap_id {
	u16	hawkeye;	/* Silicon type (Hawkeye id) */
	u8	dev;		/* Device type from production_id reg */
	u32	type;		/* combined type id copied to system_rev */
};

/* Register values to detect the OMAP version */
static struct omap_id omap_ids[] __initdata = {
	{ .hawkeye = 0xb5d9, .dev = 0x0, .type = 0x24200000 },
	{ .hawkeye = 0xb5d9, .dev = 0x1, .type = 0x24201000 },
	{ .hawkeye = 0xb5d9, .dev = 0x2, .type = 0x24202000 },
	{ .hawkeye = 0xb5d9, .dev = 0x4, .type = 0x24220000 },
	{ .hawkeye = 0xb5d9, .dev = 0x8, .type = 0x24230000 },
	{ .hawkeye = 0xb68a, .dev = 0x0, .type = 0x24300000 },
};

static u32 __init read_tap_reg(int reg)
{
	return __raw_readl(OMAP24XX_TAP_BASE + reg);
}

void __init omap2_check_revision(void)
{
	int i, j;
	u32 idcode;
	u32 prod_id;
	u16 hawkeye;
	u8  dev_type;
	u8  rev;

	idcode = read_tap_reg(OMAP_TAP_IDCODE);
	prod_id = read_tap_reg(OMAP_TAP_PROD_ID);
	hawkeye = (idcode >> 12) & 0xffff;
	rev = (idcode >> 28) & 0x0f;
	dev_type = (prod_id >> 16) & 0x0f;

#ifdef DEBUG
	printk(KERN_DEBUG "OMAP_TAP_IDCODE 0x%08x REV %i HAWKEYE 0x%04x MANF %03x\n",
		idcode, rev, hawkeye, (idcode >> 1) & 0x7ff);
	printk(KERN_DEBUG "OMAP_TAP_DIE_ID_0: 0x%08x\n",
		read_tap_reg(OMAP_TAP_DIE_ID_0));
	printk(KERN_DEBUG "OMAP_TAP_DIE_ID_1: 0x%08x DEV_REV: %i\n",
		read_tap_reg(OMAP_TAP_DIE_ID_1),
	       (read_tap_reg(OMAP_TAP_DIE_ID_1) >> 28) & 0xf);
	printk(KERN_DEBUG "OMAP_TAP_DIE_ID_2: 0x%08x\n",
		read_tap_reg(OMAP_TAP_DIE_ID_2));
	printk(KERN_DEBUG "OMAP_TAP_DIE_ID_3: 0x%08x\n",
		read_tap_reg(OMAP_TAP_DIE_ID_3));
	printk(KERN_DEBUG "OMAP_TAP_PROD_ID_0: 0x%08x DEV_TYPE: %i\n",
		prod_id, dev_type);
#endif

	/* Check hawkeye ids */
	for (i = 0; i < ARRAY_SIZE(omap_ids); i++) {
		if (hawkeye == omap_ids[i].hawkeye)
			break;
	}

	if (i == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP CPU id\n");
		return;
	}

	for (j = i; j < ARRAY_SIZE(omap_ids); j++) {
		if (dev_type == omap_ids[j].dev)
			break;
	}

	if (j == ARRAY_SIZE(omap_ids)) {
		printk(KERN_ERR "Unknown OMAP device type. "
				"Handling it as OMAP%04x\n",
				omap_ids[i].type >> 16);
		j = i;
	}
	system_rev = omap_ids[j].type;

	system_rev |= rev << 8;

	/* Add the cpu class info (24xx) */
	system_rev |= 0x24;

	pr_info("OMAP%04x", system_rev >> 16);
	if ((system_rev >> 8) & 0x0f)
		printk("%x", (system_rev >> 8) & 0x0f);
	printk("\n");
}

