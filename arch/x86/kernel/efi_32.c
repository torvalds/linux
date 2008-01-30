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

int is_available_memory(efi_memory_desc_t * md)
{
	if (!(md->attribute & EFI_MEMORY_WB))
		return 0;

	switch (md->type) {
		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
		case EFI_CONVENTIONAL_MEMORY:
			return 1;
	}
	return 0;
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

/*
 * Walks the EFI memory map and calls CALLBACK once for each EFI
 * memory descriptor that has memory that is available for kernel use.
 */
void efi_memmap_walk(efi_freemem_callback_t callback, void *arg)
{
	int prev_valid = 0;
	struct range {
		unsigned long start;
		unsigned long end;
	} uninitialized_var(prev), curr;
	efi_memory_desc_t *md;
	unsigned long start, end;
	void *p;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;

		if ((md->num_pages == 0) || (!is_available_memory(md)))
			continue;

		curr.start = md->phys_addr;
		curr.end = curr.start + (md->num_pages << EFI_PAGE_SHIFT);

		if (!prev_valid) {
			prev = curr;
			prev_valid = 1;
		} else {
			if (curr.start < prev.start)
				printk(KERN_INFO PFX "Unordered memory map\n");
			if (prev.end == curr.start)
				prev.end = curr.end;
			else {
				start =
				    (unsigned long) (PAGE_ALIGN(prev.start));
				end = (unsigned long) (prev.end & PAGE_MASK);
				if ((end > start)
				    && (*callback) (start, end, arg) < 0)
					return;
				prev = curr;
			}
		}
	}
	if (prev_valid) {
		start = (unsigned long) PAGE_ALIGN(prev.start);
		end = (unsigned long) (prev.end & PAGE_MASK);
		if (end > start)
			(*callback) (start, end, arg);
	}
}

void __init
efi_initialize_iomem_resources(struct resource *code_resource,
			       struct resource *data_resource,
			       struct resource *bss_resource)
{
	struct resource *res;
	efi_memory_desc_t *md;
	void *p;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;

		if ((md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT)) >
		    0x100000000ULL)
			continue;
		res = kzalloc(sizeof(struct resource), GFP_ATOMIC);
		switch (md->type) {
		case EFI_RESERVED_TYPE:
			res->name = "Reserved Memory";
			break;
		case EFI_LOADER_CODE:
			res->name = "Loader Code";
			break;
		case EFI_LOADER_DATA:
			res->name = "Loader Data";
			break;
		case EFI_BOOT_SERVICES_DATA:
			res->name = "BootServices Data";
			break;
		case EFI_BOOT_SERVICES_CODE:
			res->name = "BootServices Code";
			break;
		case EFI_RUNTIME_SERVICES_CODE:
			res->name = "Runtime Service Code";
			break;
		case EFI_RUNTIME_SERVICES_DATA:
			res->name = "Runtime Service Data";
			break;
		case EFI_CONVENTIONAL_MEMORY:
			res->name = "Conventional Memory";
			break;
		case EFI_UNUSABLE_MEMORY:
			res->name = "Unusable Memory";
			break;
		case EFI_ACPI_RECLAIM_MEMORY:
			res->name = "ACPI Reclaim";
			break;
		case EFI_ACPI_MEMORY_NVS:
			res->name = "ACPI NVS";
			break;
		case EFI_MEMORY_MAPPED_IO:
			res->name = "Memory Mapped IO";
			break;
		case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
			res->name = "Memory Mapped IO Port Space";
			break;
		default:
			res->name = "Reserved";
			break;
		}
		res->start = md->phys_addr;
		res->end = res->start + ((md->num_pages << EFI_PAGE_SHIFT) - 1);
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		if (request_resource(&iomem_resource, res) < 0)
			printk(KERN_ERR PFX "Failed to allocate res %s : "
				"0x%llx-0x%llx\n", res->name,
				(unsigned long long)res->start,
				(unsigned long long)res->end);
		/*
		 * We don't know which region contains kernel data so we try
		 * it repeatedly and let the resource manager test it.
		 */
		if (md->type == EFI_CONVENTIONAL_MEMORY) {
			request_resource(res, code_resource);
			request_resource(res, data_resource);
			request_resource(res, bss_resource);
#ifdef CONFIG_KEXEC
			request_resource(res, &crashk_res);
#endif
		}
	}
}
