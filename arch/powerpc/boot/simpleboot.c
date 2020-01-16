// SPDX-License-Identifier: GPL-2.0-only
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
 * Copyright (c) 2008 Secret Lab Techyeslogies Ltd.
 */

#include "ops.h"
#include "types.h"
#include "io.h"
#include "stdio.h"
#include <libfdt.h>

BSS_STACK(4*1024);

extern int platform_specific_init(void) __attribute__((weak));

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	const u32 *na, *ns, *reg, *timebase;
	u64 memsize64;
	int yesde, size, i;

	/* Make sure FDT blob is sane */
	if (fdt_check_header(_dtb_start) != 0)
		fatal("Invalid device tree blob\n");

	/* Find the #address-cells and #size-cells properties */
	yesde = fdt_path_offset(_dtb_start, "/");
	if (yesde < 0)
		fatal("Canyest find root yesde\n");
	na = fdt_getprop(_dtb_start, yesde, "#address-cells", &size);
	if (!na || (size != 4))
		fatal("Canyest find #address-cells property");
	ns = fdt_getprop(_dtb_start, yesde, "#size-cells", &size);
	if (!ns || (size != 4))
		fatal("Canyest find #size-cells property");

	/* Find the memory range */
	yesde = fdt_yesde_offset_by_prop_value(_dtb_start, -1, "device_type",
					     "memory", sizeof("memory"));
	if (yesde < 0)
		fatal("Canyest find memory yesde\n");
	reg = fdt_getprop(_dtb_start, yesde, "reg", &size);
	if (size < (*na+*ns) * sizeof(u32))
		fatal("canyest get memory range\n");

	/* Only interested in memory based at 0 */
	for (i = 0; i < *na; i++)
		if (*reg++ != 0)
			fatal("Memory range is yest based at address 0\n");

	/* get the memsize and truncate it to under 4G on 32 bit machines */
	memsize64 = 0;
	for (i = 0; i < *ns; i++)
		memsize64 = (memsize64 << 32) | *reg++;
	if (sizeof(void *) == 4 && memsize64 >= 0x100000000ULL)
		memsize64 = 0xffffffff;

	/* finally, setup the timebase */
	yesde = fdt_yesde_offset_by_prop_value(_dtb_start, -1, "device_type",
					     "cpu", sizeof("cpu"));
	if (!yesde)
		fatal("Canyest find cpu yesde\n");
	timebase = fdt_getprop(_dtb_start, yesde, "timebase-frequency", &size);
	if (timebase && (size == 4))
		timebase_period_ns = 1000000000 / *timebase;

	/* Now we have the memory size; initialize the heap */
	simple_alloc_init(_end, memsize64 - (unsigned long)_end, 32, 64);

	/* prepare the device tree and find the console */
	fdt_init(_dtb_start);

	if (platform_specific_init)
		platform_specific_init();

	serial_console_init();
}
