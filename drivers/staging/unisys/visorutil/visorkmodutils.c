/* timskmodutils.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include "uniklog.h"
#include "timskmod.h"

#define MYDRVNAME "timskmodutils"

/* s-Par uses the Intel processor's VT-X features to separate groups of
 * processors into partitions. The firmware sets the hypervisor bit and
 * reports an ID in the HV capabilities leaf so that the partition's OS
 * knows s-Par is present and managing the processors.
 */

#define UNISYS_SPAR_LEAF_ID 0x40000000

/* The s-Par leaf ID returns "UnisysSpar64" encoded across ebx, ecx, edx */
#define UNISYS_SPAR_ID_EBX 0x73696e55
#define UNISYS_SPAR_ID_ECX 0x70537379
#define UNISYS_SPAR_ID_EDX 0x34367261

int unisys_spar_platform;
EXPORT_SYMBOL_GPL(unisys_spar_platform);

/** Callers to interfaces that set __GFP_NORETRY flag below
 *  must check for a NULL (error) result as we are telling the
 *  kernel interface that it is okay to fail.
 */

void *kmalloc_kernel(size_t siz)
{
	return kmalloc(siz, GFP_KERNEL | __GFP_NORETRY);
}

static __init uint32_t
visorutil_spar_detect(void)
{
	unsigned int eax, ebx, ecx, edx;

	if (cpu_has_hypervisor) {
		/* check the ID */
		cpuid(UNISYS_SPAR_LEAF_ID, &eax, &ebx, &ecx, &edx);
		return  (ebx == UNISYS_SPAR_ID_EBX) &&
			(ecx == UNISYS_SPAR_ID_ECX) &&
			(edx == UNISYS_SPAR_ID_EDX);
	} else
		return 0;

}




static __init int
visorutil_mod_init(void)
{
	if (visorutil_spar_detect()) {
		unisys_spar_platform = TRUE;
		return 0;
	} else
		return -ENODEV;
}

static __exit void
visorutil_mod_exit(void)
{
}

module_init(visorutil_mod_init);
module_exit(visorutil_mod_exit);

MODULE_LICENSE("GPL");
