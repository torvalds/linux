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
	ibm4xx_sdram_fixup_memsize();
	ibm405gp_fixup_clocks(33330000, 0xa8c000);
	ibm4xx_quiesce_eth((u32 *)0xef600800, NULL);
	ibm4xx_fixup_ebc_ranges("/plb/ebc");
	walnut_flashsel_fixup();
	dt_fixup_mac_address_by_alias("ethernet0", (u8 *) WALNUT_OPENBIOS_MAC_OFF);
}

void platform_init(void)
{
	unsigned long end_of_ram = 0x2000000;
	unsigned long avail_ram = end_of_ram - (unsigned long) _end;

	simple_alloc_init(_end, avail_ram, 32, 32);
	platform_ops.fixups = walnut_fixups;
	platform_ops.exit = ibm40x_dbcr_reset;
	fdt_init(_dtb_start);
	serial_console_init();
}
