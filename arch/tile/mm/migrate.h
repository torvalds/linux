/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Structure definitions for migration, exposed here for use by
 * arch/tile/kernel/asm-offsets.c.
 */

#ifndef MM_MIGRATE_H
#define MM_MIGRATE_H

#include <linux/cpumask.h>
#include <hv/hypervisor.h>

/*
 * This function is used as a helper when setting up the initial
 * page table (swapper_pg_dir).
 *
 * You must mask ALL interrupts prior to invoking this code, since
 * you can't legally touch the stack during the cache flush.
 */
extern int flush_and_install_context(HV_PhysAddr page_table, HV_PTE access,
				     HV_ASID asid,
				     const unsigned long *cpumask);

/*
 * This function supports migration as a "helper" as follows:
 *
 *  - Set the stack PTE itself to "migrating".
 *  - Do a global TLB flush for (va,length) and the specified ASIDs.
 *  - Do a cache-evict on all necessary cpus.
 *  - Write the new stack PTE.
 *
 * Note that any non-NULL pointers must not point to the page that
 * is handled by the stack_pte itself.
 *
 * You must mask ALL interrupts prior to invoking this code, since
 * you can't legally touch the stack during the cache flush.
 */
extern int homecache_migrate_stack_and_flush(pte_t stack_pte, unsigned long va,
				     size_t length, pte_t *stack_ptep,
				     const struct cpumask *cache_cpumask,
				     const struct cpumask *tlb_cpumask,
				     HV_Remote_ASID *asids,
				     int asidcount);

#endif /* MM_MIGRATE_H */
