/*
 * The simple platform -- for booting when firmware doesn't supply a device
 *                        tree or any platform configuration information.
 *                        All data is extracted from an embedded device tree
 *                        blob.
 *
 * Authors: Scott Wood <scottwood@freescale.com>
 *          Grant Likely <grant.likely@secretlab.ca>
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2008 Secret Lab Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "ops.h"
#include "types.h"
#include "io.h"
#include "stdio.h"
#include "libfdt/libfdt.h"

BSS_STACK(4*1024);

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	const u32 *na, *ns, *reg, *timebase;
	u64 memsize64;
	int node, size, i;

	/* Make sure FDT blob is sane */
	if (fdt_check_header(_dtb_start) != 0)
		fatal("Invalid device tree blob\n");

	/* Find the #address-cells and #size-cells properties */
	node = fdt_path_offset(_dtb_start, "/");
	if (node < 0)
		fatal("Cannot find root node\n");
	na = fdt_getprop(_dtb_start, node, "#address-cells", &size);
	if (!na || (size != 4))
		fatal("Cannot find #address-cells property");
	ns = fdt_getprop(_dtb_start, node, "#size-cells", &size);
	if (!ns || (size != 4))
		fatal("Cannot find #size-cells property");

	/* Find the memory range */
	node = fdt_node_offset_by_prop_value(_dtb_start, -1, "device_type",
					     "memory", sizeof("memory"));
	if (node < 0)
		fatal("Cannot find memory node\n");
	reg = fdt_getprop(_dtb_start, node, "reg", &size);
	if (size < (*na+*ns) * sizeof(u32))
		fatal("cannot get memory range\n");

	/* Only interested in memory based at 0 */
	for (i = 0; i < *na; i++)
		if (*reg++ != 0)
			fatal("Memory range is not based at address 0\n");

	/* get the memsize and trucate it to under 4G on 32 bit machines */
	memsize64 = 0;
	for (i = 0; i < *ns; i++)
		memsize64 = (memsize64 << 32) | *reg++;
	if (sizeof(void *) == 4 && memsize64 >= 0x100000000ULL)
		memsize64 = 0xffffffff;

	/* finally, setup the timebase */
	node = fdt_node_offset_by_prop_value(_dtb_start, -1, "device_type",
					     "cpu", sizeof("cpu"));
	if (!node)
		fatal("Cannot find cpu node\n");
	timebase = fdt_getprop(_dtb_start, node, "timebase-frequency", &size);
	if (timebase && (size == 4))
		timebase_period_ns = 1000000000 / *timebase;

	/* Now we have the memory size; initialize the heap */
	simple_alloc_init(_end, memsize64 - (unsigned long)_end, 32, 64);

	/* prepare the device tree and find the console */
	fdt_init(_dtb_start);
	serial_console_init();
}
