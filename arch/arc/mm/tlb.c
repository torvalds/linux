/*
 * TLB Management (flush/create/diagnostics) for ARC700
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <asm/arcregs.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

/* A copy of the ASID from the PID reg is kept in asid_cache */
int asid_cache = FIRST_ASID;

/* ASID to mm struct mapping. We have one extra entry corresponding to
 * NO_ASID to save us a compare when clearing the mm entry for old asid
 * see get_new_mmu_context (asm-arc/mmu_context.h)
 */
struct mm_struct *asid_mm_map[NUM_ASID + 1];
