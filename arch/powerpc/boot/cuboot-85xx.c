/*
 * Old U-boot compatibility for 85xx
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

#define TARGET_85xx
#include "ppcboot.h"

static bd_t bd;
extern char _end[];
extern char _dtb_start[], _dtb_end[];

static void platform_fixups(void)
{
	void *soc;

	dt_fixup_memory(bd.bi_memstart, bd.bi_memsize);
	dt_fixup_mac_addresses(bd.bi_enetaddr, bd.bi_enet1addr,
	                       bd.bi_enet2addr);
	dt_fixup_cpu_clocks(bd.bi_intfreq, bd.bi_busfreq / 8, bd.bi_busfreq);

	/* Unfortunately, the specific model number is encoded in the
	 * soc node name in existing dts files -- once that is fixed,
	 * this can do a simple path lookup.
	 */
	soc = find_node_by_devtype(NULL, "soc");
	if (soc) {
		void *serial = NULL;

		setprop(soc, "bus-frequency", &bd.bi_busfreq,
		        sizeof(bd.bi_busfreq));

		while ((serial = find_node_by_devtype(serial, "serial"))) {
			if (get_parent(serial) != soc)
				continue;

			setprop(serial, "clock-frequency", &bd.bi_busfreq,
			        sizeof(bd.bi_busfreq));
		}
	}
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	unsigned long end_of_ram = bd.bi_memstart + bd.bi_memsize;
	unsigned long avail_ram = end_of_ram - (unsigned long)_end;

	memcpy(&bd, (bd_t *)r3, sizeof(bd));
	loader_info.initrd_addr = r4;
	loader_info.initrd_size = r4 ? r5 : 0;
	loader_info.cmdline = (char *)r6;
	loader_info.cmdline_len = r7 - r6;

	simple_alloc_init(_end, avail_ram - 1024*1024, 32, 64);
	ft_init(_dtb_start, _dtb_end - _dtb_start, 32);
	serial_console_init();
	platform_ops.fixups = platform_fixups;
}
