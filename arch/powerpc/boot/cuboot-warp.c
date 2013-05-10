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

static void warp_fixups(void)
{
	ibm440ep_fixup_clocks(66000000, 11059200, 50000000);
	ibm4xx_sdram_fixup_memsize();
	ibm4xx_fixup_ebc_ranges("/plb/opb/ebc");
	dt_fixup_mac_address_by_alias("ethernet0", bd.bi_enetaddr);
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
