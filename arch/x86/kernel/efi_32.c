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
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/kexec.h>

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>

#define PFX 		"EFI: "

/*
 * To make EFI call EFI runtime service in physical addressing mode we need
 * prelog/epilog before/after the invocation to disable interrupt, to
 * claim EFI runtime service handler exclusively and to duplicate a memory in
 * low memory space say 0 - 3G.
 */

static unsigned long efi_rt_eflags;
static DEFINE_SPINLOCK(efi_rt_lock);
static pgd_t efi_bak_pg_dir_pointer[2];

void efi_call_phys_prelog(void) __acquires(efi_rt_lock)
{
	unsigned long cr4;
	unsigned long temp;
	struct desc_ptr gdt_descr;

	spin_lock(&efi_rt_lock);
	local_irq_save(efi_rt_eflags);

	/*
	 * If I don't have PSE, I should just duplicate two entries in page
	 * directory. If I have PSE, I just need to duplicate one entry in
	 * page directory.
	 */
	cr4 = read_cr4();

	if (cr4 & X86_CR4_PSE) {
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
	local_flush_tlb();

	gdt_descr.address = __pa(get_cpu_gdt_table(0));
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

void efi_call_phys_epilog(void) __releases(efi_rt_lock)
{
	unsigned long cr4;
	struct desc_ptr gdt_descr;

	gdt_descr.address = (unsigned long)get_cpu_gdt_table(0);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);

	cr4 = read_cr4();

	if (cr4 & X86_CR4_PSE) {
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
	local_flush_tlb();

	local_irq_restore(efi_rt_eflags);
	spin_unlock(&efi_rt_lock);
}

/*
 * We need to map the EFI memory map again after paging_init().
 */
void __init efi_map_memmap(void)
{
	memmap.map = NULL;

	memmap.map = bt_ioremap((unsigned long) memmap.phys_map,
			(memmap.nr_map * memmap.desc_size));
	if (memmap.map == NULL)
		printk(KERN_ERR PFX "Could not remap the EFI memmap!\n");

	memmap.map_end = memmap.map + (memmap.nr_map * memmap.desc_size);
}
