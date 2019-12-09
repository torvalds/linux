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
 * Copyright (C) 2009 Wind River Systems, Inc.
 *   Updated for supporting PPC405EX on Kilauea.
 *   Tiejun Chen <tiejun.chen@windriver.com>
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

static unsigned long chip_11_errata(unsigned long memsize)
{
	unsigned long pvr;

	pvr = mfpvr();

	switch (pvr & 0xf0000ff0) {
		case 0x40000850:
		case 0x400008d0:
		case 0x200008d0:
			memsize -= 4096;
			break;
		default:
			break;
	}

	return memsize;
}

/* Read the 4xx SDRAM controller to get size of system memory. */
void ibm4xx_sdram_fixup_memsize(void)
{
	int i;
	unsigned long memsize, bank_config;

	memsize = 0;
	for (i = 0; i < ARRAY_SIZE(sdram_bxcr); i++) {
		bank_config = SDRAM0_READ(sdram_bxcr[i]);
		if (bank_config & SDRAM_CONFIG_BANK_ENABLE)
			memsize += SDRAM_CONFIG_BANK_SIZE(bank_config);
	}

	memsize = chip_11_errata(memsize);
	dt_fixup_memory(0, memsize);
}

/* Read the 440SPe MQ controller to get size of system memory. */
#define DCRN_MQ0_B0BAS		0x40
#define DCRN_MQ0_B1BAS		0x41
#define DCRN_MQ0_B2BAS		0x42
#define DCRN_MQ0_B3BAS		0x43

static u64 ibm440spe_decode_bas(u32 bas)
{
	u64 base = ((u64)(bas & 0xFFE00000u)) << 2;

	/* open coded because I'm paranoid about invalid values */
	switch ((bas >> 4) & 0xFFF) {
	case 0:
		return 0;
	case 0xffc:
		return base + 0x000800000ull;
	case 0xff8:
		return base + 0x001000000ull;
	case 0xff0:
		return base + 0x002000000ull;
	case 0xfe0:
		return base + 0x004000000ull;
	case 0xfc0:
		return base + 0x008000000ull;
	case 0xf80:
		return base + 0x010000000ull;
	case 0xf00:
		return base + 0x020000000ull;
	case 0xe00:
		return base + 0x040000000ull;
	case 0xc00:
		return base + 0x080000000ull;
	case 0x800:
		return base + 0x100000000ull;
	}
	printf("Memory BAS value 0x%08x unsupported !\n", bas);
	return 0;
}

void ibm440spe_fixup_memsize(void)
{
	u64 banktop, memsize = 0;

	/* Ultimately, we should directly construct the memory node
	 * so we are able to handle holes in the memory address space
	 */
	banktop = ibm440spe_decode_bas(mfdcr(DCRN_MQ0_B0BAS));
	if (banktop > memsize)
		memsize = banktop;
	banktop = ibm440spe_decode_bas(mfdcr(DCRN_MQ0_B1BAS));
	if (banktop > memsize)
		memsize = banktop;
	banktop = ibm440spe_decode_bas(mfdcr(DCRN_MQ0_B2BAS));
	if (banktop > memsize)
		memsize = banktop;
	banktop = ibm440spe_decode_bas(mfdcr(DCRN_MQ0_B3BAS));
	if (banktop > memsize)
		memsize = banktop;

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

/*
 * Some U-Boot versions set the number of chipselects to two
 * for Sequoia/Rainier boards while they only have one chipselect
 * hardwired. Hardcode the number of chipselects to one
 * for sequioa/rainer board models or read the actual value
 * from the memory controller register DDR0_10 otherwise.
 */
static inline u32 ibm4xx_denali_get_cs(void)
{
	void *devp;
	char model[64];
	u32 val, cs;

	devp = finddevice("/");
	if (!devp)
		goto read_cs;

	if (getprop(devp, "model", model, sizeof(model)) <= 0)
		goto read_cs;

	model[sizeof(model)-1] = 0;

	if (!strcmp(model, "amcc,sequoia") ||
	    !strcmp(model, "amcc,rainier"))
		return 1;

read_cs:
	/* get CS value */
	val = SDRAM0_READ(DDR0_10);

	val = DDR_GET_VAL(val, DDR_CS_MAP, DDR_CS_MAP_SHIFT);
	cs = 0;
	while (val) {
		if (val & 0x1)
			cs++;
		val = val >> 1;
	}
	return cs;
}

void ibm4xx_denali_fixup_memsize(void)
{
	u32 val, max_cs, max_col, max_row;
	u32 cs, col, row, bank, dpath;
	unsigned long memsize;

	val = SDRAM0_READ(DDR0_02);
	if (!DDR_GET_VAL(val, DDR_START, DDR_START_SHIFT))
		fatal("DDR controller is not initialized\n");

	/* get maximum cs col and row values */
	max_cs  = DDR_GET_VAL(val, DDR_MAX_CS_REG, DDR_MAX_CS_REG_SHIFT);
	max_col = DDR_GET_VAL(val, DDR_MAX_COL_REG, DDR_MAX_COL_REG_SHIFT);
	max_row = DDR_GET_VAL(val, DDR_MAX_ROW_REG, DDR_MAX_ROW_REG_SHIFT);

	cs = ibm4xx_denali_get_cs();
	if (!cs)
		fatal("No memory installed\n");
	if (cs > max_cs)
		fatal("DDR wrong CS configuration\n");

	/* get data path bytes */
	val = SDRAM0_READ(DDR0_14);

	if (DDR_GET_VAL(val, DDR_REDUC, DDR_REDUC_SHIFT))
		dpath = 4; /* 32 bits */
	else
		dpath = 8; /* 64 bits */

	/* get address pins (rows) */
	val = SDRAM0_READ(DDR0_42);

	row = DDR_GET_VAL(val, DDR_APIN, DDR_APIN_SHIFT);
	if (row > max_row)
		fatal("DDR wrong APIN configuration\n");
	row = max_row - row;

	/* get collomn size and banks */
	val = SDRAM0_READ(DDR0_43);

	col = DDR_GET_VAL(val, DDR_COL_SZ, DDR_COL_SZ_SHIFT);
	if (col > max_col)
		fatal("DDR wrong COL configuration\n");
	col = max_col - col;

	if (DDR_GET_VAL(val, DDR_BANK8, DDR_BANK8_SHIFT))
		bank = 8; /* 8 banks */
	else
		bank = 4; /* 4 banks */

	memsize = cs * (1 << (col+row)) * bank * dpath;
	memsize = chip_11_errata(memsize);
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
	/* Quiesce the MAL and EMAC(s) since PIBS/OpenBIOS don't
	 * do this for us
	 */
	if (emac0)
		*emac0 = EMAC_RESET;
	if (emac1)
		*emac1 = EMAC_RESET;

	mtdcr(DCRN_MAL0_CFG, MAL_RESET);
	while (mfdcr(DCRN_MAL0_CFG) & MAL_RESET)
		; /* loop until reset takes effect */
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

/* Calculate 440GP clocks */
void ibm440gp_fixup_clocks(unsigned int sys_clk, unsigned int ser_clk)
{
	u32 sys0 = mfdcr(DCRN_CPC0_SYS0);
	u32 cr0 = mfdcr(DCRN_CPC0_CR0);
	u32 cpu, plb, opb, ebc, tb, uart0, uart1, m;
	u32 opdv = CPC0_SYS0_OPDV(sys0);
	u32 epdv = CPC0_SYS0_EPDV(sys0);

	if (sys0 & CPC0_SYS0_BYPASS) {
		/* Bypass system PLL */
		cpu = plb = sys_clk;
	} else {
		if (sys0 & CPC0_SYS0_EXTSL)
			/* PerClk */
			m = CPC0_SYS0_FWDVB(sys0) * opdv * epdv;
		else
			/* CPU clock */
			m = CPC0_SYS0_FBDV(sys0) * CPC0_SYS0_FWDVA(sys0);
		cpu = sys_clk * m / CPC0_SYS0_FWDVA(sys0);
		plb = sys_clk * m / CPC0_SYS0_FWDVB(sys0);
	}

	opb = plb / opdv;
	ebc = opb / epdv;

	/* FIXME: Check if this is for all 440GP, or just Ebony */
	if ((mfpvr() & 0xf0000fff) == 0x40000440)
		/* Rev. B 440GP, use external system clock */
		tb = sys_clk;
	else
		/* Rev. C 440GP, errata force us to use internal clock */
		tb = cpu;

	if (cr0 & CPC0_CR0_U0EC)
		/* External UART clock */
		uart0 = ser_clk;
	else
		/* Internal UART clock */
		uart0 = plb / CPC0_CR0_UDIV(cr0);

	if (cr0 & CPC0_CR0_U1EC)
		/* External UART clock */
		uart1 = ser_clk;
	else
		/* Internal UART clock */
		uart1 = plb / CPC0_CR0_UDIV(cr0);

	printf("PPC440GP: SysClk = %dMHz (%x)\n\r",
	       (sys_clk + 500000) / 1000000, sys_clk);

	dt_fixup_cpu_clocks(cpu, tb, 0);

	dt_fixup_clock("/plb", plb);
	dt_fixup_clock("/plb/opb", opb);
	dt_fixup_clock("/plb/opb/ebc", ebc);
	dt_fixup_clock("/plb/opb/serial@40000200", uart0);
	dt_fixup_clock("/plb/opb/serial@40000300", uart1);
}

#define SPRN_CCR1 0x378

static inline u32 __fix_zero(u32 v, u32 def)
{
	return v ? v : def;
}

static unsigned int __ibm440eplike_fixup_clocks(unsigned int sys_clk,
						unsigned int tmr_clk,
						int per_clk_from_opb)
{
	/* PLL config */
	u32 pllc  = CPR0_READ(DCRN_CPR0_PLLC);
	u32 plld  = CPR0_READ(DCRN_CPR0_PLLD);

	/* Dividers */
	u32 fbdv   = __fix_zero((plld >> 24) & 0x1f, 32);
	u32 fwdva  = __fix_zero((plld >> 16) & 0xf, 16);
	u32 fwdvb  = __fix_zero((plld >> 8) & 7, 8);
	u32 lfbdv  = __fix_zero(plld & 0x3f, 64);
	u32 pradv0 = __fix_zero((CPR0_READ(DCRN_CPR0_PRIMAD) >> 24) & 7, 8);
	u32 prbdv0 = __fix_zero((CPR0_READ(DCRN_CPR0_PRIMBD) >> 24) & 7, 8);
	u32 opbdv0 = __fix_zero((CPR0_READ(DCRN_CPR0_OPBD) >> 24) & 3, 4);
	u32 perdv0 = __fix_zero((CPR0_READ(DCRN_CPR0_PERD) >> 24) & 3, 4);

	/* Input clocks for primary dividers */
	u32 clk_a, clk_b;

	/* Resulting clocks */
	u32 cpu, plb, opb, ebc, vco;

	/* Timebase */
	u32 ccr1, tb = tmr_clk;

	if (pllc & 0x40000000) {
		u32 m;

		/* Feedback path */
		switch ((pllc >> 24) & 7) {
		case 0:
			/* PLLOUTx */
			m = ((pllc & 0x20000000) ? fwdvb : fwdva) * lfbdv;
			break;
		case 1:
			/* CPU */
			m = fwdva * pradv0;
			break;
		case 5:
			/* PERClk */
			m = fwdvb * prbdv0 * opbdv0 * perdv0;
			break;
		default:
			printf("WARNING ! Invalid PLL feedback source !\n");
			goto bypass;
		}
		m *= fbdv;
		vco = sys_clk * m;
		clk_a = vco / fwdva;
		clk_b = vco / fwdvb;
	} else {
bypass:
		/* Bypass system PLL */
		vco = 0;
		clk_a = clk_b = sys_clk;
	}

	cpu = clk_a / pradv0;
	plb = clk_b / prbdv0;
	opb = plb / opbdv0;
	ebc = (per_clk_from_opb ? opb : plb) / perdv0;

	/* Figure out timebase.  Either CPU or default TmrClk */
	ccr1 = mfspr(SPRN_CCR1);

	/* If passed a 0 tmr_clk, force CPU clock */
	if (tb == 0) {
		ccr1 &= ~0x80u;
		mtspr(SPRN_CCR1, ccr1);
	}
	if ((ccr1 & 0x0080) == 0)
		tb = cpu;

	dt_fixup_cpu_clocks(cpu, tb, 0);
	dt_fixup_clock("/plb", plb);
	dt_fixup_clock("/plb/opb", opb);
	dt_fixup_clock("/plb/opb/ebc", ebc);

	return plb;
}

static void eplike_fixup_uart_clk(int index, const char *path,
				  unsigned int ser_clk,
				  unsigned int plb_clk)
{
	unsigned int sdr;
	unsigned int clock;

	switch (index) {
	case 0:
		sdr = SDR0_READ(DCRN_SDR0_UART0);
		break;
	case 1:
		sdr = SDR0_READ(DCRN_SDR0_UART1);
		break;
	case 2:
		sdr = SDR0_READ(DCRN_SDR0_UART2);
		break;
	case 3:
		sdr = SDR0_READ(DCRN_SDR0_UART3);
		break;
	default:
		return;
	}

	if (sdr & 0x00800000u)
		clock = ser_clk;
	else
		clock = plb_clk / __fix_zero(sdr & 0xff, 256);

	dt_fixup_clock(path, clock);
}

void ibm440ep_fixup_clocks(unsigned int sys_clk,
			   unsigned int ser_clk,
			   unsigned int tmr_clk)
{
	unsigned int plb_clk = __ibm440eplike_fixup_clocks(sys_clk, tmr_clk, 0);

	/* serial clocks need fixup based on int/ext */
	eplike_fixup_uart_clk(0, "/plb/opb/serial@ef600300", ser_clk, plb_clk);
	eplike_fixup_uart_clk(1, "/plb/opb/serial@ef600400", ser_clk, plb_clk);
	eplike_fixup_uart_clk(2, "/plb/opb/serial@ef600500", ser_clk, plb_clk);
	eplike_fixup_uart_clk(3, "/plb/opb/serial@ef600600", ser_clk, plb_clk);
}

void ibm440gx_fixup_clocks(unsigned int sys_clk,
			   unsigned int ser_clk,
			   unsigned int tmr_clk)
{
	unsigned int plb_clk = __ibm440eplike_fixup_clocks(sys_clk, tmr_clk, 1);

	/* serial clocks need fixup based on int/ext */
	eplike_fixup_uart_clk(0, "/plb/opb/serial@40000200", ser_clk, plb_clk);
	eplike_fixup_uart_clk(1, "/plb/opb/serial@40000300", ser_clk, plb_clk);
}

void ibm440spe_fixup_clocks(unsigned int sys_clk,
			    unsigned int ser_clk,
			    unsigned int tmr_clk)
{
	unsigned int plb_clk = __ibm440eplike_fixup_clocks(sys_clk, tmr_clk, 1);

	/* serial clocks need fixup based on int/ext */
	eplike_fixup_uart_clk(0, "/plb/opb/serial@f0000200", ser_clk, plb_clk);
	eplike_fixup_uart_clk(1, "/plb/opb/serial@f0000300", ser_clk, plb_clk);
	eplike_fixup_uart_clk(2, "/plb/opb/serial@f0000600", ser_clk, plb_clk);
}

void ibm405gp_fixup_clocks(unsigned int sys_clk, unsigned int ser_clk)
{
	u32 pllmr = mfdcr(DCRN_CPC0_PLLMR);
	u32 cpc0_cr0 = mfdcr(DCRN_405_CPC0_CR0);
	u32 cpc0_cr1 = mfdcr(DCRN_405_CPC0_CR1);
	u32 psr = mfdcr(DCRN_405_CPC0_PSR);
	u32 cpu, plb, opb, ebc, tb, uart0, uart1, m;
	u32 fwdv, fwdvb, fbdv, cbdv, opdv, epdv, ppdv, udiv;

	fwdv = (8 - ((pllmr & 0xe0000000) >> 29));
	fbdv = (pllmr & 0x1e000000) >> 25;
	if (fbdv == 0)
		fbdv = 16;
	cbdv = ((pllmr & 0x00060000) >> 17) + 1; /* CPU:PLB */
	opdv = ((pllmr & 0x00018000) >> 15) + 1; /* PLB:OPB */
	ppdv = ((pllmr & 0x00006000) >> 13) + 1; /* PLB:PCI */
	epdv = ((pllmr & 0x00001800) >> 11) + 2; /* PLB:EBC */
	udiv = ((cpc0_cr0 & 0x3e) >> 1) + 1;

	/* check for 405GPr */
	if ((mfpvr() & 0xfffffff0) == (0x50910951 & 0xfffffff0)) {
		fwdvb = 8 - (pllmr & 0x00000007);
		if (!(psr & 0x00001000)) /* PCI async mode enable == 0 */
			if (psr & 0x00000020) /* New mode enable */
				m = fwdvb * 2 * ppdv;
			else
				m = fwdvb * cbdv * ppdv;
		else if (psr & 0x00000020) /* New mode enable */
			if (psr & 0x00000800) /* PerClk synch mode */
				m = fwdvb * 2 * epdv;
			else
				m = fbdv * fwdv;
		else if (epdv == fbdv)
			m = fbdv * cbdv * epdv;
		else
			m = fbdv * fwdvb * cbdv;

		cpu = sys_clk * m / fwdv;
		plb = sys_clk * m / (fwdvb * cbdv);
	} else {
		m = fwdv * fbdv * cbdv;
		cpu = sys_clk * m / fwdv;
		plb = cpu / cbdv;
	}
	opb = plb / opdv;
	ebc = plb / epdv;

	if (cpc0_cr0 & 0x80)
		/* uart0 uses the external clock */
		uart0 = ser_clk;
	else
		uart0 = cpu / udiv;

	if (cpc0_cr0 & 0x40)
		/* uart1 uses the external clock */
		uart1 = ser_clk;
	else
		uart1 = cpu / udiv;

	/* setup the timebase clock to tick at the cpu frequency */
	cpc0_cr1 = cpc0_cr1 & ~0x00800000;
	mtdcr(DCRN_405_CPC0_CR1, cpc0_cr1);
	tb = cpu;

	dt_fixup_cpu_clocks(cpu, tb, 0);
	dt_fixup_clock("/plb", plb);
	dt_fixup_clock("/plb/opb", opb);
	dt_fixup_clock("/plb/ebc", ebc);
	dt_fixup_clock("/plb/opb/serial@ef600300", uart0);
	dt_fixup_clock("/plb/opb/serial@ef600400", uart1);
}


void ibm405ep_fixup_clocks(unsigned int sys_clk)
{
	u32 pllmr0 = mfdcr(DCRN_CPC0_PLLMR0);
	u32 pllmr1 = mfdcr(DCRN_CPC0_PLLMR1);
	u32 cpc0_ucr = mfdcr(DCRN_CPC0_UCR);
	u32 cpu, plb, opb, ebc, uart0, uart1;
	u32 fwdva, fwdvb, fbdv, cbdv, opdv, epdv;
	u32 pllmr0_ccdv, tb, m;

	fwdva = 8 - ((pllmr1 & 0x00070000) >> 16);
	fwdvb = 8 - ((pllmr1 & 0x00007000) >> 12);
	fbdv = (pllmr1 & 0x00f00000) >> 20;
	if (fbdv == 0)
		fbdv = 16;

	cbdv = ((pllmr0 & 0x00030000) >> 16) + 1; /* CPU:PLB */
	epdv = ((pllmr0 & 0x00000300) >> 8) + 2;  /* PLB:EBC */
	opdv = ((pllmr0 & 0x00003000) >> 12) + 1; /* PLB:OPB */

	m = fbdv * fwdvb;

	pllmr0_ccdv = ((pllmr0 & 0x00300000) >> 20) + 1;
	if (pllmr1 & 0x80000000)
		cpu = sys_clk * m / (fwdva * pllmr0_ccdv);
	else
		cpu = sys_clk / pllmr0_ccdv;

	plb = cpu / cbdv;
	opb = plb / opdv;
	ebc = plb / epdv;
	tb = cpu;
	uart0 = cpu / (cpc0_ucr & 0x0000007f);
	uart1 = cpu / ((cpc0_ucr & 0x00007f00) >> 8);

	dt_fixup_cpu_clocks(cpu, tb, 0);
	dt_fixup_clock("/plb", plb);
	dt_fixup_clock("/plb/opb", opb);
	dt_fixup_clock("/plb/ebc", ebc);
	dt_fixup_clock("/plb/opb/serial@ef600300", uart0);
	dt_fixup_clock("/plb/opb/serial@ef600400", uart1);
}

static u8 ibm405ex_fwdv_multi_bits[] = {
	/* values for:  1 - 16 */
	0x01, 0x02, 0x0e, 0x09, 0x04, 0x0b, 0x10, 0x0d, 0x0c, 0x05,
	0x06, 0x0f, 0x0a, 0x07, 0x08, 0x03
};

u32 ibm405ex_get_fwdva(unsigned long cpr_fwdv)
{
	u32 index;

	for (index = 0; index < ARRAY_SIZE(ibm405ex_fwdv_multi_bits); index++)
		if (cpr_fwdv == (u32)ibm405ex_fwdv_multi_bits[index])
			return index + 1;

	return 0;
}

static u8 ibm405ex_fbdv_multi_bits[] = {
	/* values for:  1 - 100 */
	0x00, 0xff, 0x7e, 0xfd, 0x7a, 0xf5, 0x6a, 0xd5, 0x2a, 0xd4,
	0x29, 0xd3, 0x26, 0xcc, 0x19, 0xb3, 0x67, 0xce, 0x1d, 0xbb,
	0x77, 0xee, 0x5d, 0xba, 0x74, 0xe9, 0x52, 0xa5, 0x4b, 0x96,
	0x2c, 0xd8, 0x31, 0xe3, 0x46, 0x8d, 0x1b, 0xb7, 0x6f, 0xde,
	0x3d, 0xfb, 0x76, 0xed, 0x5a, 0xb5, 0x6b, 0xd6, 0x2d, 0xdb,
	0x36, 0xec, 0x59, 0xb2, 0x64, 0xc9, 0x12, 0xa4, 0x48, 0x91,
	0x23, 0xc7, 0x0e, 0x9c, 0x38, 0xf0, 0x61, 0xc2, 0x05, 0x8b,
	0x17, 0xaf, 0x5f, 0xbe, 0x7c, 0xf9, 0x72, 0xe5, 0x4a, 0x95,
	0x2b, 0xd7, 0x2e, 0xdc, 0x39, 0xf3, 0x66, 0xcd, 0x1a, 0xb4,
	0x68, 0xd1, 0x22, 0xc4, 0x09, 0x93, 0x27, 0xcf, 0x1e, 0xbc,
	/* values for:  101 - 200 */
	0x78, 0xf1, 0x62, 0xc5, 0x0a, 0x94, 0x28, 0xd0, 0x21, 0xc3,
	0x06, 0x8c, 0x18, 0xb0, 0x60, 0xc1, 0x02, 0x84, 0x08, 0x90,
	0x20, 0xc0, 0x01, 0x83, 0x07, 0x8f, 0x1f, 0xbf, 0x7f, 0xfe,
	0x7d, 0xfa, 0x75, 0xea, 0x55, 0xaa, 0x54, 0xa9, 0x53, 0xa6,
	0x4c, 0x99, 0x33, 0xe7, 0x4e, 0x9d, 0x3b, 0xf7, 0x6e, 0xdd,
	0x3a, 0xf4, 0x69, 0xd2, 0x25, 0xcb, 0x16, 0xac, 0x58, 0xb1,
	0x63, 0xc6, 0x0d, 0x9b, 0x37, 0xef, 0x5e, 0xbd, 0x7b, 0xf6,
	0x6d, 0xda, 0x35, 0xeb, 0x56, 0xad, 0x5b, 0xb6, 0x6c, 0xd9,
	0x32, 0xe4, 0x49, 0x92, 0x24, 0xc8, 0x11, 0xa3, 0x47, 0x8e,
	0x1c, 0xb8, 0x70, 0xe1, 0x42, 0x85, 0x0b, 0x97, 0x2f, 0xdf,
	/* values for:  201 - 255 */
	0x3e, 0xfc, 0x79, 0xf2, 0x65, 0xca, 0x15, 0xab, 0x57, 0xae,
	0x5c, 0xb9, 0x73, 0xe6, 0x4d, 0x9a, 0x34, 0xe8, 0x51, 0xa2,
	0x44, 0x89, 0x13, 0xa7, 0x4f, 0x9e, 0x3c, 0xf8, 0x71, 0xe2,
	0x45, 0x8a, 0x14, 0xa8, 0x50, 0xa1, 0x43, 0x86, 0x0c, 0x98,
	0x30, 0xe0, 0x41, 0x82, 0x04, 0x88, 0x10, 0xa0, 0x40, 0x81,
	0x03, 0x87, 0x0f, 0x9f, 0x3f  /* END */
};

u32 ibm405ex_get_fbdv(unsigned long cpr_fbdv)
{
	u32 index;

	for (index = 0; index < ARRAY_SIZE(ibm405ex_fbdv_multi_bits); index++)
		if (cpr_fbdv == (u32)ibm405ex_fbdv_multi_bits[index])
			return index + 1;

	return 0;
}

void ibm405ex_fixup_clocks(unsigned int sys_clk, unsigned int uart_clk)
{
	/* PLL config */
	u32 pllc  = CPR0_READ(DCRN_CPR0_PLLC);
	u32 plld  = CPR0_READ(DCRN_CPR0_PLLD);
	u32 cpud  = CPR0_READ(DCRN_CPR0_PRIMAD);
	u32 plbd  = CPR0_READ(DCRN_CPR0_PRIMBD);
	u32 opbd  = CPR0_READ(DCRN_CPR0_OPBD);
	u32 perd  = CPR0_READ(DCRN_CPR0_PERD);

	/* Dividers */
	u32 fbdv   = ibm405ex_get_fbdv(__fix_zero((plld >> 24) & 0xff, 1));

	u32 fwdva  = ibm405ex_get_fwdva(__fix_zero((plld >> 16) & 0x0f, 1));

	u32 cpudv0 = __fix_zero((cpud >> 24) & 7, 8);

	/* PLBDV0 is hardwared to 010. */
	u32 plbdv0 = 2;
	u32 plb2xdv0 = __fix_zero((plbd >> 16) & 7, 8);

	u32 opbdv0 = __fix_zero((opbd >> 24) & 3, 4);

	u32 perdv0 = __fix_zero((perd >> 24) & 3, 4);

	/* Resulting clocks */
	u32 cpu, plb, opb, ebc, vco, tb, uart0, uart1;

	/* PLL's VCO is the source for primary forward ? */
	if (pllc & 0x40000000) {
		u32 m;

		/* Feedback path */
		switch ((pllc >> 24) & 7) {
		case 0:
			/* PLLOUTx */
			m = fbdv;
			break;
		case 1:
			/* CPU */
			m = fbdv * fwdva * cpudv0;
			break;
		case 5:
			/* PERClk */
			m = fbdv * fwdva * plb2xdv0 * plbdv0 * opbdv0 * perdv0;
			break;
		default:
			printf("WARNING ! Invalid PLL feedback source !\n");
			goto bypass;
		}

		vco = (unsigned int)(sys_clk * m);
	} else {
bypass:
		/* Bypass system PLL */
		vco = 0;
	}

	/* CPU = VCO / ( FWDVA x CPUDV0) */
	cpu = vco / (fwdva * cpudv0);
	/* PLB = VCO / ( FWDVA x PLB2XDV0 x PLBDV0) */
	plb = vco / (fwdva * plb2xdv0 * plbdv0);
	/* OPB = PLB / OPBDV0 */
	opb = plb / opbdv0;
	/* EBC = OPB / PERDV0 */
	ebc = opb / perdv0;

	tb = cpu;
	uart0 = uart1 = uart_clk;

	dt_fixup_cpu_clocks(cpu, tb, 0);
	dt_fixup_clock("/plb", plb);
	dt_fixup_clock("/plb/opb", opb);
	dt_fixup_clock("/plb/opb/ebc", ebc);
	dt_fixup_clock("/plb/opb/serial@ef600200", uart0);
	dt_fixup_clock("/plb/opb/serial@ef600300", uart1);
}
