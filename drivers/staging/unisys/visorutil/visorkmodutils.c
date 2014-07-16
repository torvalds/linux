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

/*  Use these handy-dandy seq_file_xxx functions if you want to call some
 *  functions that write stuff into a seq_file, but you actually just want
 *  to dump that output into a buffer.  Use them as follows:
 *  - call visor_seq_file_new_buffer to create the seq_file (you supply the buf)
 *  - call whatever functions you want that take a seq_file as an argument
 *    (the buf you supplied will get the output data)
 *  - call visor_seq_file_done_buffer to dispose of your seq_file
 */
struct seq_file *visor_seq_file_new_buffer(void *buf, size_t buf_size)
{
	struct seq_file *rc = NULL;
	struct seq_file *m = kmalloc_kernel(sizeof(struct seq_file));

	if (m == NULL) {
		rc = NULL;
		goto Away;
	}
	memset(m, 0, sizeof(struct seq_file));
	m->buf = buf;
	m->size = buf_size;
	rc = m;
Away:
	if (rc == NULL) {
		visor_seq_file_done_buffer(m);
		m = NULL;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(visor_seq_file_new_buffer);



void visor_seq_file_done_buffer(struct seq_file *m)
{
	if (!m)
		return;
	kfree(m);
}
EXPORT_SYMBOL_GPL(visor_seq_file_done_buffer);

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
