/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 * Copyright (C) 2002-2005 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/reboot.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/cpufeature.h>
#include <asm/hw_irq.h>

#define LEVEL0_SIZE (1UL << 12UL)
#define LEVEL1_SIZE (1UL << 21UL)
#define LEVEL2_SIZE (1UL << 30UL)
#define LEVEL3_SIZE (1UL << 39UL)
#define LEVEL4_SIZE (1UL << 48UL)

#define L0_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define L1_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_PSE)
#define L2_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define L3_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)

static void init_level2_page(
	u64 *level2p, unsigned long addr)
{
	unsigned long end_addr;
	addr &= PAGE_MASK;
	end_addr = addr + LEVEL2_SIZE;
	while(addr < end_addr) {
		*(level2p++) = addr | L1_ATTR;
		addr += LEVEL1_SIZE;
	}
}

static int init_level3_page(struct kimage *image,
	u64 *level3p, unsigned long addr, unsigned long last_addr)
{
	unsigned long end_addr;
	int result;
	result = 0;
	addr &= PAGE_MASK;
	end_addr = addr + LEVEL3_SIZE;
	while((addr < last_addr) && (addr < end_addr)) {
		struct page *page;
		u64 *level2p;
		page = kimage_alloc_control_pages(image, 0);
		if (!page) {
			result = -ENOMEM;
			goto out;
		}
		level2p = (u64 *)page_address(page);
		init_level2_page(level2p, addr);
		*(level3p++) = __pa(level2p) | L2_ATTR;
		addr += LEVEL2_SIZE;
	}
	/* clear the unused entries */
	while(addr < end_addr) {
		*(level3p++) = 0;
		addr += LEVEL2_SIZE;
	}
out:
	return result;
}


static int init_level4_page(struct kimage *image,
	u64 *level4p, unsigned long addr, unsigned long last_addr)
{
	unsigned long end_addr;
	int result;
	result = 0;
	addr &= PAGE_MASK;
	end_addr = addr + LEVEL4_SIZE;
	while((addr < last_addr) && (addr < end_addr)) {
		struct page *page;
		u64 *level3p;
		page = kimage_alloc_control_pages(image, 0);
		if (!page) {
			result = -ENOMEM;
			goto out;
		}
		level3p = (u64 *)page_address(page);
		result = init_level3_page(image, level3p, addr, last_addr);
		if (result) {
			goto out;
		}
		*(level4p++) = __pa(level3p) | L3_ATTR;
		addr += LEVEL3_SIZE;
	}
	/* clear the unused entries */
	while(addr < end_addr) {
		*(level4p++) = 0;
		addr += LEVEL3_SIZE;
	}
 out:
	return result;
}


static int init_pgtable(struct kimage *image, unsigned long start_pgtable)
{
	u64 *level4p;
	level4p = (u64 *)__va(start_pgtable);
	return init_level4_page(image, level4p, 0, end_pfn << PAGE_SHIFT);
}

static void set_idt(void *newidt, u16 limit)
{
	unsigned char curidt[10];

	/* x86-64 supports unaliged loads & stores */
	(*(u16 *)(curidt)) = limit;
	(*(u64 *)(curidt +2)) = (unsigned long)(newidt);

	__asm__ __volatile__ (
		"lidt %0\n"
		: "=m" (curidt)
		);
};


static void set_gdt(void *newgdt, u16 limit)
{
	unsigned char curgdt[10];

	/* x86-64 supports unaligned loads & stores */
	(*(u16 *)(curgdt)) = limit;
	(*(u64 *)(curgdt +2)) = (unsigned long)(newgdt);

	__asm__ __volatile__ (
		"lgdt %0\n"
		: "=m" (curgdt)
		);
};

static void load_segments(void)
{
	__asm__ __volatile__ (
		"\tmovl $"STR(__KERNEL_DS)",%eax\n"
		"\tmovl %eax,%ds\n"
		"\tmovl %eax,%es\n"
		"\tmovl %eax,%ss\n"
		"\tmovl %eax,%fs\n"
		"\tmovl %eax,%gs\n"
		);
#undef STR
#undef __STR
}

typedef NORET_TYPE void (*relocate_new_kernel_t)(
	unsigned long indirection_page, unsigned long control_code_buffer,
	unsigned long start_address, unsigned long pgtable) ATTRIB_NORET;

const extern unsigned char relocate_new_kernel[];
const extern unsigned long relocate_new_kernel_size;

int machine_kexec_prepare(struct kimage *image)
{
	unsigned long start_pgtable, control_code_buffer;
	int result;

	/* Calculate the offsets */
	start_pgtable       = page_to_pfn(image->control_code_page) << PAGE_SHIFT;
	control_code_buffer = start_pgtable + 4096UL;

	/* Setup the identity mapped 64bit page table */
	result = init_pgtable(image, start_pgtable);
	if (result) {
		return result;
	}

	/* Place the code in the reboot code buffer */
	memcpy(__va(control_code_buffer), relocate_new_kernel, relocate_new_kernel_size);

	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
	return;
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
NORET_TYPE void machine_kexec(struct kimage *image)
{
	unsigned long page_list;
	unsigned long control_code_buffer;
	unsigned long start_pgtable;
	relocate_new_kernel_t rnk;

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();

	/* Calculate the offsets */
	page_list           = image->head;
	start_pgtable       = page_to_pfn(image->control_code_page) << PAGE_SHIFT;
	control_code_buffer = start_pgtable + 4096UL;

	/* Set the low half of the page table to my identity mapped
	 * page table for kexec.  Leave the high half pointing at the
	 * kernel pages.   Don't bother to flush the global pages
	 * as that will happen when I fully switch to my identity mapped
	 * page table anyway.
	 */
	memcpy(__va(read_cr3()), __va(start_pgtable), PAGE_SIZE/2);
	__flush_tlb();


	/* The segment registers are funny things, they are
	 * automatically loaded from a table, in memory wherever you
	 * set them to a specific selector, but this table is never
	 * accessed again unless you set the segment to a different selector.
	 *
	 * The more common model are caches where the behide
	 * the scenes work is done, but is also dropped at arbitrary
	 * times.
	 *
	 * I take advantage of this here by force loading the
	 * segments, before I zap the gdt with an invalid value.
	 */
	load_segments();
	/* The gdt & idt are now invalid.
	 * If you want to load them you must set up your own idt & gdt.
	 */
	set_gdt(phys_to_virt(0),0);
	set_idt(phys_to_virt(0),0);
	/* now call it */
	rnk = (relocate_new_kernel_t) control_code_buffer;
	(*rnk)(page_list, control_code_buffer, image->start, start_pgtable);
}
