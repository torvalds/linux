/*
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Roy Zang <tie-fei.zang@freescale.com>
 *
 * Description:
 * Old U-boot compatibility for mpc7448hpc2 board
 * Based on the code of Scott Wood <scottwood@freescale.com>
 * for 83xx and 85xx.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 *
 */

#include "ops.h"
#include "stdio.h"
#include "cuboot.h"

#define TARGET_HAS_ETH1
#include "ppcboot.h"

static bd_t bd;
extern char _dtb_start[], _dtb_end[];

static void platform_fixups(void)
{
	void *tsi;

	dt_fixup_memory(bd.bi_memstart, bd.bi_memsize);
	dt_fixup_mac_addresses(bd.bi_enetaddr, bd.bi_enet1addr);
	dt_fixup_cpu_clocks(bd.bi_intfreq, bd.bi_busfreq / 4, bd.bi_busfreq);
	tsi = find_node_by_devtype(NULL, "tsi-bridge");
	if (tsi)
		setprop(tsi, "bus-frequency", &bd.bi_busfreq,
			sizeof(bd.bi_busfreq));
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();
	fdt_init(_dtb_start);
	serial_console_init();
	platform_ops.fixups = platform_fixups;
}
