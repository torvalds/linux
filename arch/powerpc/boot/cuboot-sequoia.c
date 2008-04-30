/*
 * Old U-boot compatibility for Sequoia
 *
 * Valentine Barshak <vbarshak@ru.mvista.com>
 * Copyright 2007 MontaVista Software, Inc
 *
 * Based on Ebony code by David Gibson <david@gibson.dropbear.id.au>
 * Copyright IBM Corporation, 2007
 *
 * Based on Bamboo code by Josh Boyer <jwboyer@linux.vnet.ibm.com>
 * Copyright IBM Corporation, 2007
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
#include "cuboot.h"

#define TARGET_4xx
#define TARGET_44x
#include "ppcboot.h"

static bd_t bd;


static void sequoia_fixups(void)
{
	unsigned long sysclk = 33333333;

	ibm440ep_fixup_clocks(sysclk, 11059200, 50000000);
	ibm4xx_fixup_ebc_ranges("/plb/opb/ebc");
	ibm4xx_denali_fixup_memsize();
	dt_fixup_mac_address_by_alias("ethernet0", bd.bi_enetaddr);
	dt_fixup_mac_address_by_alias("ethernet1", bd.bi_enet1addr);
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();
	platform_ops.fixups = sequoia_fixups;
	platform_ops.exit = ibm44x_dbcr_reset;
	fdt_init(_dtb_start);
	serial_console_init();
}
