/*
 * Old U-boot compatibility for Walnut
 *
 * Author: Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * Copyright 2007 IBM Corporation
 *   Based on cuboot-83xx.c, which is:
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "stdio.h"
#include "dcr.h"
#include "4xx.h"
#include "io.h"

BSS_STACK(4096);

void ibm405gp_fixup_clocks(unsigned int sysclk, unsigned int ser_clk)
{
	u32 pllmr = mfdcr(DCRN_CPC0_PLLMR);
	u32 cpc0_cr0 = mfdcr(DCRN_405_CPC0_CR0);
	u32 cpc0_cr1 = mfdcr(DCRN_405_CPC0_CR1);
	u32 cpu, plb, opb, ebc, tb, uart0, uart1, m;
	u32 fwdv, fbdv, cbdv, opdv, epdv, udiv;

	fwdv = (8 - ((pllmr & 0xe0000000) >> 29));
	fbdv = (pllmr & 0x1e000000) >> 25;
	cbdv = ((pllmr & 0x00060000) >> 17) + 1;
	opdv = ((pllmr & 0x00018000) >> 15) + 1;
	epdv = ((pllmr & 0x00001800) >> 13) + 2;
	udiv = ((cpc0_cr0 & 0x3e) >> 1) + 1;

	m = fwdv * fbdv * cbdv;

	cpu = sysclk * m / fwdv;
	plb = cpu / cbdv;
	opb = plb / opdv;
	ebc = plb / epdv;

	if (cpc0_cr0 & 0x80) {
		/* uart0 uses the external clock */
		uart0 = ser_clk;
	} else {
		uart0 = cpu / udiv;
	}

	if (cpc0_cr0 & 0x40) {
		/* uart1 uses the external clock */
		uart1 = ser_clk;
	} else {
		uart1 = cpu / udiv;
	}

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

static void walnut_flashsel_fixup(void)
{
	void *devp, *sram;
	u32 reg_flash[3] = {0x0, 0x0, 0x80000};
	u32 reg_sram[3] = {0x0, 0x0, 0x80000};
	u8 *fpga;
	u8 fpga_brds1 = 0x0;

	devp = finddevice("/plb/ebc/fpga");
	if (!devp)
		fatal("Couldn't locate FPGA node\n\r");

	if (getprop(devp, "virtual-reg", &fpga, sizeof(fpga)) != sizeof(fpga))
		fatal("no virtual-reg property\n\r");

	fpga_brds1 = in_8(fpga);

	devp = finddevice("/plb/ebc/flash");
	if (!devp)
		fatal("Couldn't locate flash node\n\r");

	if (getprop(devp, "reg", reg_flash, sizeof(reg_flash)) != sizeof(reg_flash))
		fatal("flash reg property has unexpected size\n\r");

	sram = finddevice("/plb/ebc/sram");
	if (!sram)
		fatal("Couldn't locate sram node\n\r");

	if (getprop(sram, "reg", reg_sram, sizeof(reg_sram)) != sizeof(reg_sram))
		fatal("sram reg property has unexpected size\n\r");

	if (fpga_brds1 & 0x1) {
		reg_flash[1] ^= 0x80000;
		reg_sram[1] ^= 0x80000;
	}

	setprop(devp, "reg", reg_flash, sizeof(reg_flash));
	setprop(sram, "reg", reg_sram, sizeof(reg_sram));
}

#define WALNUT_OPENBIOS_MAC_OFF 0xfffffe0b
static void walnut_fixups(void)
{
	ibm4xx_fixup_memsize();
	ibm405gp_fixup_clocks(33330000, 0xa8c000);
	ibm4xx_quiesce_eth((u32 *)0xef600800, NULL);
	ibm4xx_fixup_ebc_ranges("/plb/ebc");
	walnut_flashsel_fixup();
	dt_fixup_mac_addresses((u8 *) WALNUT_OPENBIOS_MAC_OFF);
}

void platform_init(void)
{
	unsigned long end_of_ram = 0x2000000;
	unsigned long avail_ram = end_of_ram - (unsigned long) _end;

	simple_alloc_init(_end, avail_ram, 32, 32);
	platform_ops.fixups = walnut_fixups;
	platform_ops.exit = ibm40x_dbcr_reset;
	ft_init(_dtb_start, _dtb_end - _dtb_start, 32);
	serial_console_init();
}
