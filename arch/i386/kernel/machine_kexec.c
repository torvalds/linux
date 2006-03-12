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
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/cpufeature.h>
#include <asm/desc.h>
#include <asm/system.h>

#define PAGE_ALIGNED __attribute__ ((__aligned__(PAGE_SIZE)))

#define L0_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define L1_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define L2_ATTR (_PAGE_PRESENT)

#define LEVEL0_SIZE (1UL << 12UL)

#ifndef CONFIG_X86_PAE
#define LEVEL1_SIZE (1UL << 22UL)
static u32 pgtable_level1[1024] PAGE_ALIGNED;

static void identity_map_page(unsigned long address)
{
	unsigned long level1_index, level2_index;
	u32 *pgtable_level2;

	/* Find the current page table */
	pgtable_level2 = __va(read_cr3());

	/* Find the indexes of the physical address to identity map */
	level1_index = (address % LEVEL1_SIZE)/LEVEL0_SIZE;
	level2_index = address / LEVEL1_SIZE;

	/* Identity map the page table entry */
	pgtable_level1[level1_index] = address | L0_ATTR;
	pgtable_level2[level2_index] = __pa(pgtable_level1) | L1_ATTR;

	/* Flush the tlb so the new mapping takes effect.
	 * Global tlb entries are not flushed but that is not an issue.
	 */
	load_cr3(pgtable_level2);
}

#else
#define LEVEL1_SIZE (1UL << 21UL)
#define LEVEL2_SIZE (1UL << 30UL)
static u64 pgtable_level1[512] PAGE_ALIGNED;
static u64 pgtable_level2[512] PAGE_ALIGNED;

static void identity_map_page(unsigned long address)
{
	unsigned long level1_index, level2_index, level3_index;
	u64 *pgtable_level3;

	/* Find the current page table */
	pgtable_level3 = __va(read_cr3());

	/* Find the indexes of the physical address to identity map */
	level1_index = (address % LEVEL1_SIZE)/LEVEL0_SIZE;
	level2_index = (address % LEVEL2_SIZE)/LEVEL1_SIZE;
	level3_index = address / LEVEL2_SIZE;

	/* Identity map the page table entry */
	pgtable_level1[level1_index] = address | L0_ATTR;
	pgtable_level2[level2_index] = __pa(pgtable_level1) | L1_ATTR;
	set_64bit(&pgtable_level3[level3_index],
					       __pa(pgtable_level2) | L2_ATTR);

	/* Flush the tlb so the new mapping takes effect.
	 * Global tlb entries are not flushed but that is not an issue.
	 */
	load_cr3(pgtable_level3);
}
#endif

static void set_idt(void *newidt, __u16 limit)
{
	struct Xgt_desc_struct curidt;

	/* ia32 supports unaliged loads & stores */
	curidt.size    = limit;
	curidt.address = (unsigned long)newidt;

	load_idt(&curidt);
};


static void set_gdt(void *newgdt, __u16 limit)
{
	struct Xgt_desc_struct curgdt;

	/* ia32 supports unaligned loads & stores */
	curgdt.size    = limit;
	curgdt.address = (unsigned long)newgdt;

	load_gdt(&curgdt);
};

static void load_segments(void)
{
#define __STR(X) #X
#define STR(X) __STR(X)

	__asm__ __volatile__ (
		"\tljmp $"STR(__KERNEL_CS)",$1f\n"
		"\t1:\n"
		"\tmovl $"STR(__KERNEL_DS)",%%eax\n"
		"\tmovl %%eax,%%ds\n"
		"\tmovl %%eax,%%es\n"
		"\tmovl %%eax,%%fs\n"
		"\tmovl %%eax,%%gs\n"
		"\tmovl %%eax,%%ss\n"
		::: "eax", "memory");
#undef STR
#undef __STR
}

typedef asmlinkage NORET_TYPE void (*relocate_new_kernel_t)(
					unsigned long indirection_page,
					unsigned long reboot_code_buffer,
					unsigned long start_address,
					unsigned int has_pae) ATTRIB_NORET;

const extern unsigned char relocate_new_kernel[];
extern void relocate_new_kernel_end(void);
const extern unsigned int relocate_new_kernel_size;

/*
 * A architecture hook called to validate the
 * proposed image and prepare the control pages
 * as needed.  The pages for KEXEC_CONTROL_CODE_SIZE
 * have been allocated, but the segments have yet
 * been copied into the kernel.
 *
 * Do what every setup is needed on image and the
 * reboot code buffer to allow us to avoid allocations
 * later.
 *
 * Currently nothing.
 */
int machine_kexec_prepare(struct kimage *image)
{
	return 0;
}

/*
 * Undo anything leftover by machine_kexec_prepare
 * when an image is freed.
 */
void machine_kexec_cleanup(struct kimage *image)
{
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
NORET_TYPE void machine_kexec(struct kimage *image)
{
	unsigned long page_list;
	unsigned long reboot_code_buffer;

	relocate_new_kernel_t rnk;

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();

	/* Compute some offsets */
	reboot_code_buffer = page_to_pfn(image->control_code_page)
								<< PAGE_SHIFT;
	page_list = image->head;

	/* Set up an identity mapping for the reboot_code_buffer */
	identity_map_page(reboot_code_buffer);

	/* copy it out */
	memcpy((void *)reboot_code_buffer, relocate_new_kernel,
						relocate_new_kernel_size);

	/* The segment registers are funny things, they are
	 * automatically loaded from a table, in memory wherever you
	 * set them to a specific selector, but this table is never
	 * accessed again you set the segment to a different selector.
	 *
	 * The more common model is are caches where the behide
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
	rnk = (relocate_new_kernel_t) reboot_code_buffer;
	(*rnk)(page_list, reboot_code_buffer, image->start, cpu_has_pae);
}
