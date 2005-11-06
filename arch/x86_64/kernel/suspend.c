/*
 * Suspend support specific for i386.
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/config.h>
#include <linux/smp.h>
#include <linux/suspend.h>
#include <asm/proto.h>
#include <asm/page.h>
#include <asm/pgtable.h>

struct saved_context saved_context;

unsigned long saved_context_eax, saved_context_ebx, saved_context_ecx, saved_context_edx;
unsigned long saved_context_esp, saved_context_ebp, saved_context_esi, saved_context_edi;
unsigned long saved_context_r08, saved_context_r09, saved_context_r10, saved_context_r11;
unsigned long saved_context_r12, saved_context_r13, saved_context_r14, saved_context_r15;
unsigned long saved_context_eflags;

void __save_processor_state(struct saved_context *ctxt)
{
	kernel_fpu_begin();

	/*
	 * descriptor tables
	 */
	asm volatile ("sgdt %0" : "=m" (ctxt->gdt_limit));
	asm volatile ("sidt %0" : "=m" (ctxt->idt_limit));
	asm volatile ("str %0"  : "=m" (ctxt->tr));

	/* XMM0..XMM15 should be handled by kernel_fpu_begin(). */
	/* EFER should be constant for kernel version, no need to handle it. */
	/*
	 * segment registers
	 */
	asm volatile ("movw %%ds, %0" : "=m" (ctxt->ds));
	asm volatile ("movw %%es, %0" : "=m" (ctxt->es));
	asm volatile ("movw %%fs, %0" : "=m" (ctxt->fs));
	asm volatile ("movw %%gs, %0" : "=m" (ctxt->gs));
	asm volatile ("movw %%ss, %0" : "=m" (ctxt->ss));

	rdmsrl(MSR_FS_BASE, ctxt->fs_base);
	rdmsrl(MSR_GS_BASE, ctxt->gs_base);
	rdmsrl(MSR_KERNEL_GS_BASE, ctxt->gs_kernel_base);

	/*
	 * control registers 
	 */
	asm volatile ("movq %%cr0, %0" : "=r" (ctxt->cr0));
	asm volatile ("movq %%cr2, %0" : "=r" (ctxt->cr2));
	asm volatile ("movq %%cr3, %0" : "=r" (ctxt->cr3));
	asm volatile ("movq %%cr4, %0" : "=r" (ctxt->cr4));
	asm volatile ("movq %%cr8, %0" : "=r" (ctxt->cr8));
}

void save_processor_state(void)
{
	__save_processor_state(&saved_context);
}

static void do_fpu_end(void)
{
	/*
	 * Restore FPU regs if necessary
	 */
	kernel_fpu_end();
}

void __restore_processor_state(struct saved_context *ctxt)
{
	/*
	 * control registers
	 */
	asm volatile ("movq %0, %%cr8" :: "r" (ctxt->cr8));
	asm volatile ("movq %0, %%cr4" :: "r" (ctxt->cr4));
	asm volatile ("movq %0, %%cr3" :: "r" (ctxt->cr3));
	asm volatile ("movq %0, %%cr2" :: "r" (ctxt->cr2));
	asm volatile ("movq %0, %%cr0" :: "r" (ctxt->cr0));

	/*
	 * now restore the descriptor tables to their proper values
	 * ltr is done i fix_processor_context().
	 */
	asm volatile ("lgdt %0" :: "m" (ctxt->gdt_limit));
	asm volatile ("lidt %0" :: "m" (ctxt->idt_limit));

	/*
	 * segment registers
	 */
	asm volatile ("movw %0, %%ds" :: "r" (ctxt->ds));
	asm volatile ("movw %0, %%es" :: "r" (ctxt->es));
	asm volatile ("movw %0, %%fs" :: "r" (ctxt->fs));
	load_gs_index(ctxt->gs);
	asm volatile ("movw %0, %%ss" :: "r" (ctxt->ss));

	wrmsrl(MSR_FS_BASE, ctxt->fs_base);
	wrmsrl(MSR_GS_BASE, ctxt->gs_base);
	wrmsrl(MSR_KERNEL_GS_BASE, ctxt->gs_kernel_base);

	fix_processor_context();

	do_fpu_end();
	mtrr_ap_init();
}

void restore_processor_state(void)
{
	__restore_processor_state(&saved_context);
}

void fix_processor_context(void)
{
	int cpu = smp_processor_id();
	struct tss_struct *t = &per_cpu(init_tss, cpu);

	set_tss_desc(cpu,t);	/* This just modifies memory; should not be neccessary. But... This is neccessary, because 386 hardware has concept of busy TSS or some similar stupidity. */

	cpu_gdt_table[cpu][GDT_ENTRY_TSS].type = 9;

	syscall_init();                         /* This sets MSR_*STAR and related */
	load_TR_desc();				/* This does ltr */
	load_LDT(&current->active_mm->context);	/* This does lldt */

	/*
	 * Now maybe reload the debug registers
	 */
	if (current->thread.debugreg7){
                loaddebug(&current->thread, 0);
                loaddebug(&current->thread, 1);
                loaddebug(&current->thread, 2);
                loaddebug(&current->thread, 3);
                /* no 4 and 5 */
                loaddebug(&current->thread, 6);
                loaddebug(&current->thread, 7);
	}

}

#ifdef CONFIG_SOFTWARE_SUSPEND
/* Defined in arch/x86_64/kernel/suspend_asm.S */
extern int restore_image(void);

pgd_t *temp_level4_pgt;

static int res_phys_pud_init(pud_t *pud, unsigned long address, unsigned long end)
{
	long i, j;

	i = pud_index(address);
	pud = pud + i;
	for (; i < PTRS_PER_PUD; pud++, i++) {
		unsigned long paddr;
		pmd_t *pmd;

		paddr = address + i*PUD_SIZE;
		if (paddr >= end)
			break;

		pmd = (pmd_t *)get_safe_page(GFP_ATOMIC);
		if (!pmd)
			return -ENOMEM;
		set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE));
		for (j = 0; j < PTRS_PER_PMD; pmd++, j++, paddr += PMD_SIZE) {
			unsigned long pe;

			if (paddr >= end)
				break;
			pe = _PAGE_NX | _PAGE_PSE | _KERNPG_TABLE | paddr;
			pe &= __supported_pte_mask;
			set_pmd(pmd, __pmd(pe));
		}
	}
	return 0;
}

static int set_up_temporary_mappings(void)
{
	unsigned long start, end, next;
	int error;

	temp_level4_pgt = (pgd_t *)get_safe_page(GFP_ATOMIC);
	if (!temp_level4_pgt)
		return -ENOMEM;

	/* It is safe to reuse the original kernel mapping */
	set_pgd(temp_level4_pgt + pgd_index(__START_KERNEL_map),
		init_level4_pgt[pgd_index(__START_KERNEL_map)]);

	/* Set up the direct mapping from scratch */
	start = (unsigned long)pfn_to_kaddr(0);
	end = (unsigned long)pfn_to_kaddr(end_pfn);

	for (; start < end; start = next) {
		pud_t *pud = (pud_t *)get_safe_page(GFP_ATOMIC);
		if (!pud)
			return -ENOMEM;
		next = start + PGDIR_SIZE;
		if (next > end)
			next = end;
		if ((error = res_phys_pud_init(pud, __pa(start), __pa(next))))
			return error;
		set_pgd(temp_level4_pgt + pgd_index(start),
			mk_kernel_pgd(__pa(pud)));
	}
	return 0;
}

int swsusp_arch_resume(void)
{
	int error;

	/* We have got enough memory and from now on we cannot recover */
	if ((error = set_up_temporary_mappings()))
		return error;
	restore_image();
	return 0;
}
#endif /* CONFIG_SOFTWARE_SUSPEND */
