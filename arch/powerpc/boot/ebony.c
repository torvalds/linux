/*
 * Copyright 2007 David Gibson, IBM Corporation.
 *
 * Based on earlier code:
 *   Copyright (C) Paul Mackerras 1997.
 *
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
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"
#include "reg.h"
#include "io.h"
#include "dcr.h"
#include "4xx.h"
#include "44x.h"

static u8 *ebony_mac0, *ebony_mac1;

/* Calculate 440GP clocks */
void ibm440gp_fixup_clocks(unsigned int sysclk, unsigned int ser_clk)
{
	u32 sys0 = mfdcr(DCRN_CPC0_SYS0);
	u32 cr0 = mfdcr(DCRN_CPC0_CR0);
	u32 cpu, plb, opb, ebc, tb, uart0, uart1, m;
	u32 opdv = CPC0_SYS0_OPDV(sys0);
	u32 epdv = CPC0_SYS0_EPDV(sys0);

	if (sys0 & CPC0_SYS0_BYPASS) {
		/* Bypass system PLL */
		cpu = plb = sysclk;
	} else {
		if (sys0 & CPC0_SYS0_EXTSL)
			/* PerClk */
			m = CPC0_SYS0_FWDVB(sys0) * opdv * epdv;
		else
			/* CPU clock */
			m = CPC0_SYS0_FBDV(sys0) * CPC0_SYS0_FWDVA(sys0);
		cpu = sysclk * m / CPC0_SYS0_FWDVA(sys0);
		plb = sysclk * m / CPC0_SYS0_FWDVB(sys0);
	}

	opb = plb / opdv;
	ebc = opb / epdv;

	/* FIXME: Check if this is for all 440GP, or just Ebony */
	if ((mfpvr() & 0xf0000fff) == 0x40000440)
		/* Rev. B 440GP, use external system clock */
		tb = sysclk;
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
	       (sysclk + 500000) / 1000000, sysclk);

	dt_fixup_cpu_clocks(cpu, tb, 0);

	dt_fixup_clock("/plb", plb);
	dt_fixup_clock("/plb/opb", opb);
	dt_fixup_clock("/plb/opb/ebc", ebc);
	dt_fixup_clock("/plb/opb/serial@40000200", uart0);
	dt_fixup_clock("/plb/opb/serial@40000300", uart1);
}

#define EBONY_FPGA_PATH		"/plb/opb/ebc/fpga"
#define	EBONY_FPGA_FLASH_SEL	0x01
#define EBONY_SMALL_FLASH_PATH	"/plb/opb/ebc/small-flash"

static void ebony_flashsel_fixup(void)
{
	void *devp;
	u32 reg[3] = {0x0, 0x0, 0x80000};
	u8 *fpga;
	u8 fpga_reg0 = 0x0;

	devp = finddevice(EBONY_FPGA_PATH);
	if (!devp)
		fatal("Couldn't locate FPGA node %s\n\r", EBONY_FPGA_PATH);

	if (getprop(devp, "virtual-reg", &fpga, sizeof(fpga)) != sizeof(fpga))
		fatal("%s has missing or invalid virtual-reg property\n\r",
		      EBONY_FPGA_PATH);

	fpga_reg0 = in_8(fpga);

	devp = finddevice(EBONY_SMALL_FLASH_PATH);
	if (!devp)
		fatal("Couldn't locate small flash node %s\n\r",
		      EBONY_SMALL_FLASH_PATH);

	if (getprop(devp, "reg", reg, sizeof(reg)) != sizeof(reg))
		fatal("%s has reg property of unexpected size\n\r",
		      EBONY_SMALL_FLASH_PATH);

	/* Invert address bit 14 (IBM-endian) if FLASH_SEL fpga bit is set */
	if (fpga_reg0 & EBONY_FPGA_FLASH_SEL)
		reg[1] ^= 0x80000;

	setprop(devp, "reg", reg, sizeof(reg));
}

static void ebony_fixups(void)
{
	// FIXME: sysclk should be derived by reading the FPGA registers
	unsigned long sysclk = 33000000;

	ibm440gp_fixup_clocks(sysclk, 6 * 1843200);
	ibm4xx_fixup_memsize();
	dt_fixup_mac_addresses(ebony_mac0, ebony_mac1);
	ibm4xx_fixup_ebc_ranges("/plb/opb/ebc");
	ebony_flashsel_fixup();
}

void ebony_init(void *mac0, void *mac1)
{
	platform_ops.fixups = ebony_fixups;
	platform_ops.exit = ibm44x_dbcr_reset;
	ebony_mac0 = mac0;
	ebony_mac1 = mac1;
	ft_init(_dtb_start, _dtb_end - _dtb_start, 32);
	serial_console_init();
}
