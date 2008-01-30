/*
 * Suspend support specific for i386.
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/smp.h>
#include <linux/suspend.h>
#include <asm/proto.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mtrr.h>

/* References to section boundaries */
extern const void __nosave_begin, __nosave_end;

static void fix_processor_context(void);

struct saved_context saved_context;

/**
 *	__save_processor_state - save CPU registers before creating a
 *		hibernation image and before restoring the memory state from it
 *	@ctxt - structure to store the registers contents in
 *
 *	NOTE: If there is a CPU register the modification of which by the
 *	boot kernel (ie. the kernel used for loading the hibernation image)
 *	might affect the operations of the restored target kernel (ie. the one
 *	saved in the hibernation image), then its contents must be saved by this
 *	function.  In other words, if kernel A is hibernated and different
 *	kernel B is used for loading the hibernation image into memory, the
 *	kernel A's __save_processor_state() function must save all registers
 *	needed by kernel A, so that it can operate correctly after the resume
 *	regardless of what kernel B does in the meantime.
 */
static void __save_processor_state(struct saved_context *ctxt)
{
	kernel_fpu_begin();

	/*
	 * descriptor tables
	 */
	store_gdt((struct desc_ptr *)&ctxt->gdt_limit);
	store_idt((struct desc_ptr *)&ctxt->idt_limit);
	store_tr(ctxt->tr);

	/* XMM0..XMM15 should be handled by kernel_fpu_begin(). */
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
	mtrr_save_fixed_ranges(NULL);

	/*
	 * control registers 
	 */
	rdmsrl(MSR_EFER, ctxt->efer);
	ctxt->cr0 = read_cr0();
	ctxt->cr2 = read_cr2();
	ctxt->cr3 = read_cr3();
	ctxt->cr4 = read_cr4();
	ctxt->cr8 = read_cr8();
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

/**
 *	__restore_processor_state - restore the contents of CPU registers saved
 *		by __save_processor_state()
 *	@ctxt - structure to load the registers contents from
 */
static void __restore_processor_state(struct saved_context *ctxt)
{
	/*
	 * control registers
	 */
	wrmsrl(MSR_EFER, ctxt->efer);
	write_cr8(ctxt->cr8);
	write_cr4(ctxt->cr4);
	write_cr3(ctxt->cr3);
	write_cr2(ctxt->cr2);
	write_cr0(ctxt->cr0);

	/*
	 * now restore the descriptor tables to their proper values
	 * ltr is done i fix_processor_context().
	 */
	load_gdt((const struct desc_ptr *)&ctxt->gdt_limit);
	load_idt((const struct desc_ptr *)&ctxt->idt_limit);


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

static void fix_processor_context(void)
{
	int cpu = smp_processor_id();
	struct tss_struct *t = &per_cpu(init_tss, cpu);

	set_tss_desc(cpu,t);	/* This just modifies memory; should not be necessary. But... This is necessary, because 386 hardware has concept of busy TSS or some similar stupidity. */

	get_cpu_gdt_table(cpu)[GDT_ENTRY_TSS].type = 9;

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

#ifdef CONFIG_HIBERNATION
/* Defined in arch/x86_64/kernel/suspend_asm.S */
extern int restore_image(void);

/*
 * Address to jump to in the last phase of restore in order to get to the image
 * kernel's text (this value is passed in the image header).
 */
unsigned long restore_jump_address;

/*
 * Value of the cr3 register from before the hibernation (this value is passed
 * in the image header).
 */
unsigned long restore_cr3;

pgd_t *temp_level4_pgt;

void *relocated_restore_code;

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
			pe = __PAGE_KERNEL_LARGE_EXEC | paddr;
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

	relocated_restore_code = (void *)get_safe_page(GFP_ATOMIC);
	if (!relocated_restore_code)
		return -ENOMEM;
	memcpy(relocated_restore_code, &core_restore_code,
	       &restore_registers - &core_restore_code);

	restore_image();
	return 0;
}

/*
 *	pfn_is_nosave - check if given pfn is in the 'nosave' section
 */

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa_symbol(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa_symbol(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

struct restore_data_record {
	unsigned long jump_address;
	unsigned long cr3;
	unsigned long magic;
};

#define RESTORE_MAGIC	0x0123456789ABCDEFUL

/**
 *	arch_hibernation_header_save - populate the architecture specific part
 *		of a hibernation image header
 *	@addr: address to save the data at
 */
int arch_hibernation_header_save(void *addr, unsigned int max_size)
{
	struct restore_data_record *rdr = addr;

	if (max_size < sizeof(struct restore_data_record))
		return -EOVERFLOW;
	rdr->jump_address = restore_jump_address;
	rdr->cr3 = restore_cr3;
	rdr->magic = RESTORE_MAGIC;
	return 0;
}

/**
 *	arch_hibernation_header_restore - read the architecture specific data
 *		from the hibernation image header
 *	@addr: address to read the data from
 */
int arch_hibernation_header_restore(void *addr)
{
	struct restore_data_record *rdr = addr;

	restore_jump_address = rdr->jump_address;
	restore_cr3 = rdr->cr3;
	return (rdr->magic == RESTORE_MAGIC) ? 0 : -EINVAL;
}
#endif /* CONFIG_HIBERNATION */
