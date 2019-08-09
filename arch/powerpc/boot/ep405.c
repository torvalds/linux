// SPDX-License-Identifier: GPL-2.0-only
/*
 * Embedded Planet EP405 with PlanetCore firmware
 *
 * (c) Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp,\
 *
 * Based on ep88xc.c by
 *
 * Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */

#include "ops.h"
#include "stdio.h"
#include "planetcore.h"
#include "dcr.h"
#include "4xx.h"
#include "io.h"

static char *table;
static u64 mem_size;

static void platform_fixups(void)
{
	u64 val;
	void *nvrtc;

	dt_fixup_memory(0, mem_size);
	planetcore_set_mac_addrs(table);

	if (!planetcore_get_decimal(table, PLANETCORE_KEY_CRYSTAL_HZ, &val)) {
		printf("No PlanetCore crystal frequency key.\r\n");
		return;
	}
	ibm405gp_fixup_clocks(val, 0xa8c000);
	ibm4xx_quiesce_eth((u32 *)0xef600800, NULL);
	ibm4xx_fixup_ebc_ranges("/plb/ebc");

	if (!planetcore_get_decimal(table, PLANETCORE_KEY_KB_NVRAM, &val)) {
		printf("No PlanetCore NVRAM size key.\r\n");
		return;
	}
	nvrtc = finddevice("/plb/ebc/nvrtc@4,200000");
	if (nvrtc != NULL) {
		u32 reg[3] = { 4, 0x200000, 0};
		getprop(nvrtc, "reg", reg, 3);
		reg[2] = (val << 10) & 0xffffffff;
		setprop(nvrtc, "reg", reg, 3);
	}
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	table = (char *)r3;
	planetcore_prepare_table(table);

	if (!planetcore_get_decimal(table, PLANETCORE_KEY_MB_RAM, &mem_size))
		return;

	mem_size *= 1024 * 1024;
	simple_alloc_init(_end, mem_size - (unsigned long)_end, 32, 64);

	fdt_init(_dtb_start);

	planetcore_set_stdout_path(table);

	serial_console_init();
	platform_ops.fixups = platform_fixups;
}
