/*
 *  acpi.c - Architecture-Specific Low-Level ACPI Support
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *  Copyright (C) 2001 Patrick Mochel <mochel@osdl.org>
 *  Copyright (C) 2002 Andi Kleen, SuSE Labs (x86-64 port)
 *  Copyright (C) 2003 Pavel Machek, SuSE Labs
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/cpumask.h>

#include <asm/mpspec.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/io_apic.h>
#include <asm/proto.h>
#include <asm/tlbflush.h>

/* --------------------------------------------------------------------------
                              Low-Level Sleep Support
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_SLEEP

/* address in low memory of the wakeup routine. */
unsigned long acpi_wakeup_address = 0;
unsigned long acpi_video_flags;
extern char wakeup_start, wakeup_end;

extern unsigned long acpi_copy_wakeup_routine(unsigned long);

static pgd_t low_ptr;

static void init_low_mapping(void)
{
	pgd_t *slot0 = pgd_offset(current->mm, 0UL);
	low_ptr = *slot0;
	/* FIXME: We're playing with the current task's page tables here, which
	 * is potentially dangerous on SMP systems.
	 */
	set_pgd(slot0, *pgd_offset(current->mm, PAGE_OFFSET));
	local_flush_tlb();
}

/**
 * acpi_save_state_mem - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int acpi_save_state_mem(void)
{
	init_low_mapping();

	memcpy((void *)acpi_wakeup_address, &wakeup_start,
	       &wakeup_end - &wakeup_start);
	acpi_copy_wakeup_routine(acpi_wakeup_address);

	return 0;
}

/*
 * acpi_restore_state
 */
void acpi_restore_state_mem(void)
{
	set_pgd(pgd_offset(current->mm, 0UL), low_ptr);
	local_flush_tlb();
}

/**
 * acpi_reserve_bootmem - do _very_ early ACPI initialisation
 *
 * We allocate a page in low memory for the wakeup
 * routine for when we come back from a sleep state. The
 * runtime allocator allows specification of <16M pages, but not
 * <1M pages.
 */
void __init acpi_reserve_bootmem(void)
{
	acpi_wakeup_address = (unsigned long)alloc_bootmem_low(PAGE_SIZE);
	if ((&wakeup_end - &wakeup_start) > PAGE_SIZE)
		printk(KERN_CRIT
		       "ACPI: Wakeup code way too big, will crash on attempt to suspend\n");
}

static int __init acpi_sleep_setup(char *str)
{
	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "s3_bios", 7) == 0)
			acpi_video_flags = 1;
		if (strncmp(str, "s3_mode", 7) == 0)
			acpi_video_flags |= 2;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("acpi_sleep=", acpi_sleep_setup);

#endif				/*CONFIG_ACPI_SLEEP */

void acpi_pci_link_exit(void)
{
}
