/*
 * RedBoot firmware support
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "stdio.h"
#include "redboot.h"
#include "fsl-soc.h"
#include "io.h"

static bd_t bd;
BSS_STACK(4096);

#define MHZ(x)	((x + 500000) / 1000000)

static void platform_fixups(void)
{
	void *node;

	dt_fixup_memory(bd.bi_memstart, bd.bi_memsize);
	dt_fixup_mac_addresses(bd.bi_enetaddr);
	dt_fixup_cpu_clocks(bd.bi_intfreq, bd.bi_busfreq / 16, bd.bi_busfreq);

	node = finddevice("/soc/cpm/brg");
	if (node) {
		printf("BRG clock-frequency <- 0x%x (%dMHz)\r\n",
		       bd.bi_busfreq, MHZ(bd.bi_busfreq));
		setprop(node, "clock-frequency",  &bd.bi_busfreq, 4);
	}
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	memcpy(&bd, (char *)r3, sizeof(bd));

	if (bd.bi_tag != 0x42444944)
		return;

	simple_alloc_init(_end,
	                  bd.bi_memstart + bd.bi_memsize - (unsigned long)_end,
	                  32, 64);

	fdt_init(_dtb_start);
	serial_console_init();
	platform_ops.fixups = platform_fixups;

	loader_info.cmdline = (char *)bd.bi_cmdline;
	loader_info.cmdline_len = strlen((char *)bd.bi_cmdline);
}
