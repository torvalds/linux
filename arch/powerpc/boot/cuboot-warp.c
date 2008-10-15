/*
 * Copyright (c) 2008 PIKA Technologies
 *   Sean MacLennan <smaclennan@pikatech.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "4xx.h"
#include "cuboot.h"
#include "stdio.h"

#define TARGET_4xx
#define TARGET_44x
#include "ppcboot.h"

static bd_t bd;

static void warp_fixup_one_nor(u32 from, u32 to)
{
	void *devp;
	char name[50];
	u32 v[2];

	sprintf(name, "/plb/opb/ebc/nor_flash@0,0/partition@%x", from);

	devp = finddevice(name);
	if (!devp)
		return;

	if (getprop(devp, "reg", v, sizeof(v)) == sizeof(v)) {
		v[0] = to;
		setprop(devp, "reg", v, sizeof(v));

		printf("NOR 64M fixup %x -> %x\r\n", from, to);
	}
}


static void warp_fixups(void)
{
	ibm440ep_fixup_clocks(66000000, 11059200, 50000000);
	ibm4xx_sdram_fixup_memsize();
	ibm4xx_fixup_ebc_ranges("/plb/opb/ebc");
	dt_fixup_mac_address_by_alias("ethernet0", bd.bi_enetaddr);

	/* Fixup for 64M flash on Rev A boards. */
	if (bd.bi_flashsize == 0x4000000) {
		void *devp;
		u32 v[3];

		devp = finddevice("/plb/opb/ebc/nor_flash@0,0");
		if (!devp)
			return;

		/* Fixup the size */
		if (getprop(devp, "reg", v, sizeof(v)) == sizeof(v)) {
			v[2] = bd.bi_flashsize;
			setprop(devp, "reg", v, sizeof(v));
		}

		/* Fixup parition offsets */
		warp_fixup_one_nor(0x300000, 0x3f00000);
		warp_fixup_one_nor(0x340000, 0x3f40000);
		warp_fixup_one_nor(0x380000, 0x3f80000);
	}
}


void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();

	platform_ops.fixups = warp_fixups;
	platform_ops.exit = ibm44x_dbcr_reset;
	fdt_init(_dtb_start);
	serial_console_init();
}
