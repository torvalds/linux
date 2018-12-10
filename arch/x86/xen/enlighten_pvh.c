// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>

/*
 * PVH variables.
 *
 * The variable xen_pvh needs to live in the data segment since it is used
 * after startup_{32|64} is invoked, which will clear the .bss segment.
 */
bool xen_pvh __attribute__((section(".data"))) = 0;
