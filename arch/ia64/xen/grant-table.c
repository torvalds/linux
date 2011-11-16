/******************************************************************************
 * arch/ia64/xen/grant-table.c
 *
 * Copyright (c) 2006 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/grant_table.h>

#include <asm/xen/hypervisor.h>

/****************************************************************************
 * grant table hack
 * cmd: GNTTABOP_xxx
 */

int arch_gnttab_map_shared(unsigned long *frames, unsigned long nr_gframes,
			   unsigned long max_nr_gframes,
			   struct grant_entry **__shared)
{
	*__shared = __va(frames[0] << PAGE_SHIFT);
	return 0;
}

void arch_gnttab_unmap_shared(struct grant_entry *shared,
			      unsigned long nr_gframes)
{
	/* nothing */
}

static void
gnttab_map_grant_ref_pre(struct gnttab_map_grant_ref *uop)
{
	uint32_t flags;

	flags = uop->flags;

	if (flags & GNTMAP_host_map) {
		if (flags & GNTMAP_application_map) {
			printk(KERN_DEBUG
			       "GNTMAP_application_map is not supported yet: "
			       "flags 0x%x\n", flags);
			BUG();
		}
		if (flags & GNTMAP_contains_pte) {
			printk(KERN_DEBUG
			       "GNTMAP_contains_pte is not supported yet: "
			       "flags 0x%x\n", flags);
			BUG();
		}
	} else if (flags & GNTMAP_device_map) {
		printk("GNTMAP_device_map is not supported yet 0x%x\n", flags);
		BUG();	/* not yet. actually this flag is not used. */
	} else {
		BUG();
	}
}

int
HYPERVISOR_grant_table_op(unsigned int cmd, void *uop, unsigned int count)
{
	if (cmd == GNTTABOP_map_grant_ref) {
		unsigned int i;
		for (i = 0; i < count; i++) {
			gnttab_map_grant_ref_pre(
				(struct gnttab_map_grant_ref *)uop + i);
		}
	}
	return xencomm_hypercall_grant_table_op(cmd, uop, count);
}

EXPORT_SYMBOL(HYPERVISOR_grant_table_op);
