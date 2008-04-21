/*
 * Old U-boot compatibility for Yosemite
 *
 * Author: Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * Copyright 2008 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "stdio.h"
#include "4xx.h"
#include "44x.h"
#include "cuboot.h"

#define TARGET_4xx
#define TARGET_44x
#include "ppcboot.h"

static bd_t bd;

static void yosemite_fixups(void)
{
	unsigned long sysclk = 66666666;

	ibm440ep_fixup_clocks(sysclk, 11059200, 50000000);
	ibm4xx_sdram_fixup_memsize();
	ibm4xx_quiesce_eth((u32 *)0xef600e00, (u32 *)0xef600f00);
	dt_fixup_mac_address_by_alias("ethernet0", bd.bi_enetaddr);
	dt_fixup_mac_address_by_alias("ethernet1", bd.bi_enet1addr);
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();
	platform_ops.fixups = yosemite_fixups;
	platform_ops.exit = ibm44x_dbcr_reset;
	fdt_init(_dtb_start);
	serial_console_init();
}
