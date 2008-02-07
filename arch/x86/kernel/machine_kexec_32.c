/*
 * handle transition of Linux booting another kernel
 * Copyright (C) 2002-2005 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/numa.h>
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
static u32 kexec_pgd[1024] PAGE_ALIGNED;
#ifdef CONFIG_X86_PAE
static u32 kexec_pmd0[1024] PAGE_ALIGNED;
static u32 kexec_pmd1[1024] PAGE_ALIGNED;
#endif
static u32 kexec_pte0[1024] PAGE_ALIGNED;
static u32 kexec_pte1[1024] PAGE_ALIGNED;

static void set_idt(void *newidt, __u16 limit)
{
	struct desc_ptr curidt;

	/* ia32 supports unaliged loads & stores */
	curidt.size    = limit;
	curidt.address = (unsigned long)newidt;

	load_idt(&curidt);
};


static void set_gdt(void *newgdt, __u16 limit)
{
	struct desc_ptr curgdt;

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
	unsigned long page_list[PAGES_NR];
	void *control_page;

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();

	control_page = page_address(image->control_code_page);
	memcpy(control_page, relocate_kernel, PAGE_SIZE);

	page_list[PA_CONTROL_PAGE] = __pa(control_page);
	page_list[VA_CONTROL_PAGE] = (unsigned long)relocate_kernel;
	page_list[PA_PGD] = __pa(kexec_pgd);
	page_list[VA_PGD] = (unsigned long)kexec_pgd;
#ifdef CONFIG_X86_PAE
	page_list[PA_PMD_0] = __pa(kexec_pmd0);
	page_list[VA_PMD_0] = (unsigned long)kexec_pmd0;
	page_list[PA_PMD_1] = __pa(kexec_pmd1);
	page_list[VA_PMD_1] = (unsigned long)kexec_pmd1;
#endif
	page_list[PA_PTE_0] = __pa(kexec_pte0);
	page_list[VA_PTE_0] = (unsigned long)kexec_pte0;
	page_list[PA_PTE_1] = __pa(kexec_pte1);
	page_list[VA_PTE_1] = (unsigned long)kexec_pte1;

	/* The segment registers are funny things, they have both a
	 * visible and an invisible part.  Whenever the visible part is
	 * set to a specific selector, the invisible part is loaded
	 * with from a table in memory.  At no other time is the
	 * descriptor table in memory accessed.
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
	relocate_kernel((unsigned long)image->head, (unsigned long)page_list,
			image->start, cpu_has_pae);
}

void arch_crash_save_vmcoreinfo(void)
{
#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(node_data);
	VMCOREINFO_LENGTH(node_data, MAX_NUMNODES);
#endif
#ifdef CONFIG_X86_PAE
	VMCOREINFO_CONFIG(X86_PAE);
#endif
}

