/*
 * devtree.c - convenience functions for device tree manipulation
 * Copyright 2007 David Gibson, IBM Corporation.
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * Authors: David Gibson <david@gibson.dropbear.id.au>
 *	    Scott Wood <scottwood@freescale.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "ops.h"

void dt_fixup_memory(u64 start, u64 size)
{
	void *root, *memory;
	int naddr, nsize, i;
	u32 memreg[4];

	root = finddevice("/");
	if (getprop(root, "#address-cells", &naddr, sizeof(naddr)) < 0)
		naddr = 2;
	if (naddr < 1 || naddr > 2)
		fatal("Can't cope with #address-cells == %d in /\n\r", naddr);

	if (getprop(root, "#size-cells", &nsize, sizeof(nsize)) < 0)
		nsize = 1;
	if (nsize < 1 || nsize > 2)
		fatal("Can't cope with #size-cells == %d in /\n\r", nsize);

	i = 0;
	if (naddr == 2)
		memreg[i++] = start >> 32;
	memreg[i++] = start & 0xffffffff;
	if (nsize == 2)
		memreg[i++] = size >> 32;
	memreg[i++] = size & 0xffffffff;

	memory = finddevice("/memory");
	if (! memory) {
		memory = create_node(NULL, "memory");
		setprop_str(memory, "device_type", "memory");
	}

	printf("Memory <- <0x%x", memreg[0]);
	for (i = 1; i < (naddr + nsize); i++)
		printf(" 0x%x", memreg[i]);
	printf("> (%ldMB)\n\r", (unsigned long)(size >> 20));

	setprop(memory, "reg", memreg, (naddr + nsize)*sizeof(u32));
}

#define MHZ(x)	((x + 500000) / 1000000)

void dt_fixup_cpu_clocks(u32 cpu, u32 tb, u32 bus)
{
	void *devp = NULL;

	printf("CPU clock-frequency <- 0x%x (%dMHz)\n\r", cpu, MHZ(cpu));
	printf("CPU timebase-frequency <- 0x%x (%dMHz)\n\r", tb, MHZ(tb));
	if (bus > 0)
		printf("CPU bus-frequency <- 0x%x (%dMHz)\n\r", bus, MHZ(bus));

	while ((devp = find_node_by_devtype(devp, "cpu"))) {
		setprop_val(devp, "clock-frequency", cpu);
		setprop_val(devp, "timebase-frequency", tb);
		if (bus > 0)
			setprop_val(devp, "bus-frequency", bus);
	}
}

void dt_fixup_clock(const char *path, u32 freq)
{
	void *devp = finddevice(path);

	if (devp) {
		printf("%s: clock-frequency <- %x (%dMHz)\n\r", path, freq, MHZ(freq));
		setprop_val(devp, "clock-frequency", freq);
	}
}

void __dt_fixup_mac_addresses(u32 startindex, ...)
{
	va_list ap;
	u32 index = startindex;
	void *devp;
	const u8 *addr;

	va_start(ap, startindex);
	while ((addr = va_arg(ap, const u8 *))) {
		devp = find_node_by_prop_value(NULL, "linux,network-index",
					       (void*)&index, sizeof(index));

		printf("ENET%d: local-mac-address <-"
		       " %02x:%02x:%02x:%02x:%02x:%02x\n\r", index,
		       addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

		if (devp)
			setprop(devp, "local-mac-address", addr, 6);

		index++;
	}
	va_end(ap);
}
