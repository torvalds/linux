/*
 * Copyright IBM Corporation, 2007
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * Based on ebony wrapper:
 * Copyright 2007 David Gibson, IBM Corporation.
 *
 * Clocking code based on code by:
 * Stefan Roese <sr@denx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the License
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"
#include "dcr.h"
#include "4xx.h"
#include "44x.h"

static u8 *bamboo_mac0, *bamboo_mac1;

static void bamboo_fixups(void)
{
	unsigned long sysclk = 33333333;

	ibm440ep_fixup_clocks(sysclk, 11059200, 25000000);
	ibm4xx_sdram_fixup_memsize();
	ibm4xx_quiesce_eth((u32 *)0xef600e00, (u32 *)0xef600f00);
	dt_fixup_mac_address_by_alias("ethernet0", bamboo_mac0);
	dt_fixup_mac_address_by_alias("ethernet1", bamboo_mac1);
}

void bamboo_init(void *mac0, void *mac1)
{
	platform_ops.fixups = bamboo_fixups;
	platform_ops.exit = ibm44x_dbcr_reset;
	bamboo_mac0 = mac0;
	bamboo_mac1 = mac1;
	fdt_init(_dtb_start);
	serial_console_init();
}
