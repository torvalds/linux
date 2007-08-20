/*
 * Copyright 2007 David Gibson, IBM Corporation.
 *
 * Based on earlier code:
 *   Matt Porter <mporter@kernel.crashing.org>
 *   Copyright 2002-2005 MontaVista Software Inc.
 *
 *   Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *   Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stddef.h>
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "ops.h"
#include "reg.h"
#include "dcr.h"

/* Read the 4xx SDRAM controller to get size of system memory. */
void ibm4xx_fixup_memsize(void)
{
	int i;
	unsigned long memsize, bank_config;

	memsize = 0;
	for (i = 0; i < ARRAY_SIZE(sdram_bxcr); i++) {
		mtdcr(DCRN_SDRAM0_CFGADDR, sdram_bxcr[i]);
		bank_config = mfdcr(DCRN_SDRAM0_CFGDATA);

		if (bank_config & SDRAM_CONFIG_BANK_ENABLE)
			memsize += SDRAM_CONFIG_BANK_SIZE(bank_config);
	}

	dt_fixup_memory(0, memsize);
}

#define SPRN_DBCR0_40X 0x3F2
#define SPRN_DBCR0_44X 0x134
#define DBCR0_RST_SYSTEM 0x30000000

void ibm44x_dbcr_reset(void)
{
	unsigned long tmp;

	asm volatile (
		"mfspr	%0,%1\n"
		"oris	%0,%0,%2@h\n"
		"mtspr	%1,%0"
		: "=&r"(tmp) : "i"(SPRN_DBCR0_44X), "i"(DBCR0_RST_SYSTEM)
		);

}

void ibm40x_dbcr_reset(void)
{
	unsigned long tmp;

	asm volatile (
		"mfspr	%0,%1\n"
		"oris	%0,%0,%2@h\n"
		"mtspr	%1,%0"
		: "=&r"(tmp) : "i"(SPRN_DBCR0_40X), "i"(DBCR0_RST_SYSTEM)
		);
}

#define EMAC_RESET 0x20000000
void ibm4xx_quiesce_eth(u32 *emac0, u32 *emac1)
{
	/* Quiesce the MAL and EMAC(s) since PIBS/OpenBIOS don't do this for us */
	if (emac0)
		*emac0 = EMAC_RESET;
	if (emac1)
		*emac1 = EMAC_RESET;

	mtdcr(DCRN_MAL0_CFG, MAL_RESET);
}

/* Read 4xx EBC bus bridge registers to get mappings of the peripheral
 * banks into the OPB address space */
void ibm4xx_fixup_ebc_ranges(const char *ebc)
{
	void *devp;
	u32 bxcr;
	u32 ranges[EBC_NUM_BANKS*4];
	u32 *p = ranges;
	int i;

	for (i = 0; i < EBC_NUM_BANKS; i++) {
		mtdcr(DCRN_EBC0_CFGADDR, EBC_BXCR(i));
		bxcr = mfdcr(DCRN_EBC0_CFGDATA);

		if ((bxcr & EBC_BXCR_BU) != EBC_BXCR_BU_OFF) {
			*p++ = i;
			*p++ = 0;
			*p++ = bxcr & EBC_BXCR_BAS;
			*p++ = EBC_BXCR_BANK_SIZE(bxcr);
		}
	}

	devp = finddevice(ebc);
	if (! devp)
		fatal("Couldn't locate EBC node %s\n\r", ebc);

	setprop(devp, "ranges", ranges, (p - ranges) * sizeof(u32));
}
