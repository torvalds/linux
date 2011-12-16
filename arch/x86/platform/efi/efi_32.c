/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 1.0
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * All EFI Runtime Services are not implemented yet as EFI only
 * supports physical mode addressing on SoftSDV. This is to be fixed
 * in a future version.  --drummond 1999-07-20
 *
 * Implemented EFI runtime services and virtual mode calls.  --davidm
 *
 * Goutham Rao: <goutham.rao@intel.com>
 *	Skip non-WB memory and ignore empty memory ranges.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/efi.h>

#include <asm/io.h>
#include <asm/desc.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/efi.h>

/*
 * To make EFI call EFI runtime service in physical addressing mode we need
 * prelog/epilog before/after the invocation to disable interrupt, to
 * claim EFI runtime service handler exclusively and to duplicate a memory in
 * low memory space say 0 - 3G.
 */

static unsigned long efi_rt_eflags;
static pgd_t efi_bak_pg_dir_pointer[2];

void efi_call_phys_prelog(void)
{
	unsigned long cr4;
	unsigned long temp;
	struct desc_ptr gdt_descr;

	local_irq_save(efi_rt_eflags);

	/*
	 * If I don't have PAE, I should just duplicate two entries in page
	 * directory. If I have PAE, I just need to duplicate one entry in
	 * page directory.
	 */
	cr4 = read_cr4_safe();

	if (cr4 & X86_CR4_PAE) {
		efi_bak_pg_dir_pointer[0].pgd =
		    swapper_pg_dir[pgd_index(0)].pgd;
		swapper_pg_dir[0].pgd =
		    swapper_pg_dir[pgd_index(PAGE_OFFSET)].pgd;
	} else {
		efi_bak_pg_dir_pointer[0].pgd =
		    swapper_pg_dir[pgd_index(0)].pgd;
		efi_bak_pg_dir_pointer[1].pgd =
		    swapper_pg_dir[pgd_index(0x400000)].pgd;
		swapper_pg_dir[pgd_index(0)].pgd =
		    swapper_pg_dir[pgd_index(PAGE_OFFSET)].pgd;
		temp = PAGE_OFFSET + 0x400000;
		swapper_pg_dir[pgd_index(0x400000)].pgd =
		    swapper_pg_dir[pgd_index(temp)].pgd;
	}

	/*
	 * After the lock is released, the original page table is restored.
	 */
	__flush_tlb_all();

	gdt_descr.address = __pa(get_cpu_gdt_table(0));
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

void efi_call_phys_epilog(void)
{
	unsigned long cr4;
	struct desc_ptr gdt_descr;

	gdt_descr.address = (unsigned long)get_cpu_gdt_table(0);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);

	cr4 = read_cr4_safe();

	if (cr4 & X86_CR4_PAE) {
		swapper_pg_dir[pgd_index(0)].pgd =
		    efi_bak_pg_dir_pointer[0].pgd;
	} else {
		swapper_pg_dir[pgd_index(0)].pgd =
		    efi_bak_pg_dir_pointer[0].pgd;
		swapper_pg_dir[pgd_index(0x400000)].pgd =
		    efi_bak_pg_dir_pointer[1].pgd;
	}

	/*
	 * After the lock is released, the original page table is restored.
	 */
	__flush_tlb_all();

	local_irq_restore(efi_rt_eflags);
}
