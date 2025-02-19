// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC ioremap.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/bug.h>
#include <linux/sched.h>
#include <asm/tlbflush.h>

extern int mem_init_done;

/*
 * OK, this one's a bit tricky... ioremap can get called before memory is
 * initialized (early serial console does this) and will want to alloc a page
 * for its mapping.  No userspace pages will ever get allocated before memory
 * is initialized so this applies only to kernel pages.  In the event that
 * this is called before memory is initialized we allocate the page using
 * the memblock infrastructure.
 */

pte_t __ref *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte;

	if (likely(mem_init_done)) {
		pte = (pte_t *)get_zeroed_page(GFP_KERNEL);
	} else {
		pte = memblock_alloc_or_panic(PAGE_SIZE, PAGE_SIZE);
	}

	return pte;
}
