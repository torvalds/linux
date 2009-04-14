/*
 * Davinci CPU identification code
 *
 * Copyright (C) 2006 Komal Shah <komal_shah802003@yahoo.com>
 *
 * Derived from OMAP1 CPU identification code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#define JTAG_ID_BASE		IO_ADDRESS(0x01c40028)

static unsigned int davinci_revision;

struct davinci_id {
	u8	variant;	/* JTAG ID bits 31:28 */
	u16	part_no;	/* JTAG ID bits 27:12 */
	u32	manufacturer;	/* JTAG ID bits 11:1 */
	u32	type;		/* Cpu id bits [31:8], cpu class bits [7:0] */
};

/* Register values to detect the DaVinci version */
static struct davinci_id davinci_ids[] __initdata = {
	{
		/* DM6446 */
		.part_no      = 0xb700,
		.variant      = 0x0,
		.manufacturer = 0x017,
		.type	      = 0x64460000,
	},
	{
		/* DM646X */
		.part_no      = 0xb770,
		.variant      = 0x0,
		.manufacturer = 0x017,
		.type         = 0x64670000,
	},
	{
		/* DM355 */
		.part_no	= 0xb73b,
		.variant	= 0x0,
		.manufacturer	= 0x00f,
		.type		= 0x03550000,
	},
};

/*
 * Get Device Part No. from JTAG ID register
 */
static u16 __init davinci_get_part_no(void)
{
	u32 dev_id, part_no;

	dev_id = __raw_readl(JTAG_ID_BASE);

	part_no = ((dev_id >> 12) & 0xffff);

	return part_no;
}

/*
 * Get Device Revision from JTAG ID register
 */
static u8 __init davinci_get_variant(void)
{
	u32 variant;

	variant = __raw_readl(JTAG_ID_BASE);

	variant = (variant >> 28) & 0xf;

	return variant;
}

unsigned int davinci_rev(void)
{
	return davinci_revision >> 16;
}
EXPORT_SYMBOL(davinci_rev);

void __init davinci_check_revision(void)
{
	int i;
	u16 part_no;
	u8 variant;

	part_no = davinci_get_part_no();
	variant = davinci_get_variant();

	/* First check only the major version in a safe way */
	for (i = 0; i < ARRAY_SIZE(davinci_ids); i++) {
		if (part_no == (davinci_ids[i].part_no)) {
			davinci_revision = davinci_ids[i].type;
			break;
		}
	}

	/* Check if we can find the dev revision */
	for (i = 0; i < ARRAY_SIZE(davinci_ids); i++) {
		if (part_no == davinci_ids[i].part_no &&
		    variant == davinci_ids[i].variant) {
			davinci_revision = davinci_ids[i].type;
			break;
		}
	}

	printk(KERN_INFO "DaVinci DM%04x variant 0x%x\n",
	       davinci_rev(), variant);
}
