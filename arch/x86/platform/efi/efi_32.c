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
 * prolog/epilog before/after the invocation to claim the EFI runtime service
 * handler exclusively and to duplicate a memory mapping in low memory space,
 * say 0 - 3G.
 */


void efi_call_phys_prelog(void)
{
	struct desc_ptr gdt_descr;

	load_cr3(initial_page_table);
	__flush_tlb_all();

	gdt_descr.address = __pa(get_cpu_gdt_table(0));
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

void efi_call_phys_epilog(void)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (unsigned long)get_cpu_gdt_table(0);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();
}
