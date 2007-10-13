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

/* 4xx DDR1/2 Denali memory controller support */
/* DDR0 registers */
#define DDR0_02			2
#define DDR0_08			8
#define DDR0_10			10
#define DDR0_14			14
#define DDR0_42			42
#define DDR0_43			43

/* DDR0_02 */
#define DDR_START		0x1
#define DDR_START_SHIFT		0
#define DDR_MAX_CS_REG		0x3
#define DDR_MAX_CS_REG_SHIFT	24
#define DDR_MAX_COL_REG		0xf
#define DDR_MAX_COL_REG_SHIFT	16
#define DDR_MAX_ROW_REG		0xf
#define DDR_MAX_ROW_REG_SHIFT	8
/* DDR0_08 */
#define DDR_DDR2_MODE		0x1
#define DDR_DDR2_MODE_SHIFT	0
/* DDR0_10 */
#define DDR_CS_MAP		0x3
#define DDR_CS_MAP_SHIFT	8
/* DDR0_14 */
#define DDR_REDUC		0x1
#define DDR_REDUC_SHIFT		16
/* DDR0_42 */
#define DDR_APIN		0x7
#define DDR_APIN_SHIFT		24
/* DDR0_43 */
#define DDR_COL_SZ		0x7
#define DDR_COL_SZ_SHIFT	8
#define DDR_BANK8		0x1
#define DDR_BANK8_SHIFT		0

#define DDR_GET_VAL(val, mask, shift)	(((val) >> (shift)) & (mask))

static inline u32 mfdcr_sdram0(u32 reg)
{
        mtdcr(DCRN_SDRAM0_CFGADDR, reg);
        return mfdcr(DCRN_SDRAM0_CFGDATA);
}

void ibm4xx_denali_fixup_memsize(void)
{
	u32 val, max_cs, max_col, max_row;
	u32 cs, col, row, bank, dpath;
	unsigned long memsize;

	val = mfdcr_sdram0(DDR0_02);
	if (!DDR_GET_VAL(val, DDR_START, DDR_START_SHIFT))
		fatal("DDR controller is not initialized\n");

	/* get maximum cs col and row values */
	max_cs  = DDR_GET_VAL(val, DDR_MAX_CS_REG, DDR_MAX_CS_REG_SHIFT);
	max_col = DDR_GET_VAL(val, DDR_MAX_COL_REG, DDR_MAX_COL_REG_SHIFT);
	max_row = DDR_GET_VAL(val, DDR_MAX_ROW_REG, DDR_MAX_ROW_REG_SHIFT);

	/* get CS value */
	val = mfdcr_sdram0(DDR0_10);

	val = DDR_GET_VAL(val, DDR_CS_MAP, DDR_CS_MAP_SHIFT);
	cs = 0;
	while (val) {
		if (val && 0x1)
			cs++;
		val = val >> 1;
	}

	if (!cs)
		fatal("No memory installed\n");
	if (cs > max_cs)
		fatal("DDR wrong CS configuration\n");

	/* get data path bytes */
	val = mfdcr_sdram0(DDR0_14);

	if (DDR_GET_VAL(val, DDR_REDUC, DDR_REDUC_SHIFT))
		dpath = 8; /* 64 bits */
	else
		dpath = 4; /* 32 bits */

	/* get adress pins (rows) */
	val = mfdcr_sdram0(DDR0_42);

	row = DDR_GET_VAL(val, DDR_APIN, DDR_APIN_SHIFT);
	if (row > max_row)
		fatal("DDR wrong APIN configuration\n");
	row = max_row - row;

	/* get collomn size and banks */
	val = mfdcr_sdram0(DDR0_43);

	col = DDR_GET_VAL(val, DDR_COL_SZ, DDR_COL_SZ_SHIFT);
	if (col > max_col)
		fatal("DDR wrong COL configuration\n");
	col = max_col - col;

	if (DDR_GET_VAL(val, DDR_BANK8, DDR_BANK8_SHIFT))
		bank = 8; /* 8 banks */
	else
		bank = 4; /* 4 banks */

	memsize = cs * (1 << (col+row)) * bank * dpath;
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

#define SPRN_CCR1 0x378
void ibm440ep_fixup_clocks(unsigned int sysclk, unsigned int ser_clk)
{
	u32 cpu, plb, opb, ebc, tb, uart0, m, vco;
	u32 reg;
	u32 fwdva, fwdvb, fbdv, lfbdv, opbdv0, perdv0, spcid0, prbdv0, tmp;

	mtdcr(DCRN_CPR0_ADDR, CPR0_PLLD0);
	reg = mfdcr(DCRN_CPR0_DATA);
	tmp = (reg & 0x000F0000) >> 16;
	fwdva = tmp ? tmp : 16;
	tmp = (reg & 0x00000700) >> 8;
	fwdvb = tmp ? tmp : 8;
	tmp = (reg & 0x1F000000) >> 24;
	fbdv = tmp ? tmp : 32;
	lfbdv = (reg & 0x0000007F);

	mtdcr(DCRN_CPR0_ADDR, CPR0_OPBD0);
	reg = mfdcr(DCRN_CPR0_DATA);
	tmp = (reg & 0x03000000) >> 24;
	opbdv0 = tmp ? tmp : 4;

	mtdcr(DCRN_CPR0_ADDR, CPR0_PERD0);
	reg = mfdcr(DCRN_CPR0_DATA);
	tmp = (reg & 0x07000000) >> 24;
	perdv0 = tmp ? tmp : 8;

	mtdcr(DCRN_CPR0_ADDR, CPR0_PRIMBD0);
	reg = mfdcr(DCRN_CPR0_DATA);
	tmp = (reg & 0x07000000) >> 24;
	prbdv0 = tmp ? tmp : 8;

	mtdcr(DCRN_CPR0_ADDR, CPR0_SCPID);
	reg = mfdcr(DCRN_CPR0_DATA);
	tmp = (reg & 0x03000000) >> 24;
	spcid0 = tmp ? tmp : 4;

	/* Calculate M */
	mtdcr(DCRN_CPR0_ADDR, CPR0_PLLC0);
	reg = mfdcr(DCRN_CPR0_DATA);
	tmp = (reg & 0x03000000) >> 24;
	if (tmp == 0) { /* PLL output */
		tmp = (reg & 0x20000000) >> 29;
		if (!tmp) /* PLLOUTA */
			m = fbdv * lfbdv * fwdva;
		else
			m = fbdv * lfbdv * fwdvb;
	}
	else if (tmp == 1) /* CPU output */
		m = fbdv * fwdva;
	else
		m = perdv0 * opbdv0 * fwdvb;

	vco = (m * sysclk) + (m >> 1);
	cpu = vco / fwdva;
	plb = vco / fwdvb / prbdv0;
	opb = plb / opbdv0;
	ebc = plb / perdv0;

	/* FIXME */
	uart0 = ser_clk;

	/* Figure out timebase.  Either CPU or default TmrClk */
	asm volatile (
			"mfspr	%0,%1\n"
			:
			"=&r"(reg) : "i"(SPRN_CCR1));
	if (reg & 0x0080)
		tb = 25000000; /* TmrClk is 25MHz */
	else
		tb = cpu;

	dt_fixup_cpu_clocks(cpu, tb, 0);
	dt_fixup_clock("/plb", plb);
	dt_fixup_clock("/plb/opb", opb);
	dt_fixup_clock("/plb/opb/ebc", ebc);
	dt_fixup_clock("/plb/opb/serial@ef600300", uart0);
	dt_fixup_clock("/plb/opb/serial@ef600400", uart0);
	dt_fixup_clock("/plb/opb/serial@ef600500", uart0);
	dt_fixup_clock("/plb/opb/serial@ef600600", uart0);
}
