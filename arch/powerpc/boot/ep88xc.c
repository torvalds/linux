// SPDX-License-Identifier: GPL-2.0-only
/*
 * Embedded Planet EP88xC with PlanetCore firmware
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */

#include "ops.h"
#include "stdio.h"
#include "planetcore.h"
#include "mpc8xx.h"

static char *table;
static u64 mem_size;

static void platform_fixups(void)
{
	u64 val;

	dt_fixup_memory(0, mem_size);
	planetcore_set_mac_addrs(table);

	if (!planetcore_get_decimal(table, PLANETCORE_KEY_CRYSTAL_HZ, &val)) {
		printf("No PlanetCore crystal frequency key.\r\n");
		return;
	}

	mpc885_fixup_clocks(val);
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
