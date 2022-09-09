// SPDX-License-Identifier: GPL-2.0-or-later

#include <stddef.h>
#include "stdio.h"
#include "types.h"
#include "io.h"
#include "ops.h"

BSS_STACK(8192);

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5)
{
	unsigned long heapsize = 16*1024*1024 - (unsigned long)_end;

	/*
	 * Disable interrupts and turn off MSR_RI, since we'll
	 * shortly be overwriting the interrupt vectors.
	 */
	__asm__ volatile("mtmsrd %0,1" : : "r" (0));

	simple_alloc_init(_end, heapsize, 32, 64);
	fdt_init(_dtb_start);
	serial_console_init();
}
