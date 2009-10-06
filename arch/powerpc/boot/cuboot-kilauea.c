/*
 * Old U-boot compatibility for PPC405EX. This image is already included
 * a dtb.
 *
 * Author: Tiejun Chen <tiejun.chen@windriver.com>
 *
 * Copyright (C) 2009 Wind River Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "io.h"
#include "dcr.h"
#include "stdio.h"
#include "4xx.h"
#include "44x.h"
#include "cuboot.h"

#define TARGET_4xx
#define TARGET_44x
#include "ppcboot.h"

#define KILAUEA_SYS_EXT_SERIAL_CLOCK     11059200        /* ext. 11.059MHz clk */

static bd_t bd;

static void kilauea_fixups(void)
{
	unsigned long sysclk = 33333333;

	ibm405ex_fixup_clocks(sysclk, KILAUEA_SYS_EXT_SERIAL_CLOCK);
	dt_fixup_memory(bd.bi_memstart, bd.bi_memsize);
	ibm4xx_fixup_ebc_ranges("/plb/opb/ebc");
	dt_fixup_mac_address_by_alias("ethernet0", bd.bi_enetaddr);
	dt_fixup_mac_address_by_alias("ethernet1", bd.bi_enet1addr);
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();
	platform_ops.fixups = kilauea_fixups;
	platform_ops.exit = ibm40x_dbcr_reset;
	fdt_init(_dtb_start);
	serial_console_init();
}
