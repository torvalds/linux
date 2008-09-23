/*
 * Suspend and hibernation support for x86-64
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2007 Rafael J. Wysocki <rjw@sisk.pl>
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/smp.h>
#include <linux/suspend.h>
#include <asm/proto.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mtrr.h>
#include <asm/xcr.h>

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

	/*
	 * restore XCR0 for xsave capable cpu's.
	 */
	if (cpu_has_xsave)
		xsetbv(XCR_XFEATURE_ENABLED_MASK, pcntxt_mask);

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

	/*
	 * This just modifies memory; should not be necessary. But... This
	 * is necessary, because 386 hardware has concept of busy TSS or some
	 * similar stupidity.
	 */
	set_tss_desc(cpu, t);

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
