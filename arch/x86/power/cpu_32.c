/*
 * Suspend support specific for i386.
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/suspend.h>
#include <linux/smp.h>

#include <asm/pgtable.h>
#include <asm/proto.h>
#include <asm/mtrr.h>
#include <asm/page.h>
#include <asm/mce.h>
#include <asm/xcr.h>
#include <asm/suspend.h>

#ifdef CONFIG_X86_32
static struct saved_context saved_context;

unsigned long saved_context_ebx;
unsigned long saved_context_esp, saved_context_ebp;
unsigned long saved_context_esi, saved_context_edi;
unsigned long saved_context_eflags;
#else
/* CONFIG_X86_64 */
static void fix_processor_context(void);

struct saved_context saved_context;
#endif

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
#ifdef CONFIG_X86_32
	mtrr_save_fixed_ranges(NULL);
#endif
	kernel_fpu_begin();

	/*
	 * descriptor tables
	 */
#ifdef CONFIG_X86_32
	store_gdt(&ctxt->gdt);
	store_idt(&ctxt->idt);
#else
/* CONFIG_X86_64 */
	store_gdt((struct desc_ptr *)&ctxt->gdt_limit);
	store_idt((struct desc_ptr *)&ctxt->idt_limit);
#endif
	store_tr(ctxt->tr);

	/* XMM0..XMM15 should be handled by kernel_fpu_begin(). */
	/*
	 * segment registers
	 */
#ifdef CONFIG_X86_32
	savesegment(es, ctxt->es);
	savesegment(fs, ctxt->fs);
	savesegment(gs, ctxt->gs);
	savesegment(ss, ctxt->ss);
#else
/* CONFIG_X86_64 */
	asm volatile ("movw %%ds, %0" : "=m" (ctxt->ds));
	asm volatile ("movw %%es, %0" : "=m" (ctxt->es));
	asm volatile ("movw %%fs, %0" : "=m" (ctxt->fs));
	asm volatile ("movw %%gs, %0" : "=m" (ctxt->gs));
	asm volatile ("movw %%ss, %0" : "=m" (ctxt->ss));

	rdmsrl(MSR_FS_BASE, ctxt->fs_base);
	rdmsrl(MSR_GS_BASE, ctxt->gs_base);
	rdmsrl(MSR_KERNEL_GS_BASE, ctxt->gs_kernel_base);
	mtrr_save_fixed_ranges(NULL);

	rdmsrl(MSR_EFER, ctxt->efer);
#endif

	/*
	 * control registers
	 */
	ctxt->cr0 = read_cr0();
	ctxt->cr2 = read_cr2();
	ctxt->cr3 = read_cr3();
#ifdef CONFIG_X86_32
	ctxt->cr4 = read_cr4_safe();
#else
/* CONFIG_X86_64 */
	ctxt->cr4 = read_cr4();
	ctxt->cr8 = read_cr8();
#endif
}

/* Needed by apm.c */
void save_processor_state(void)
{
	__save_processor_state(&saved_context);
}
#ifdef CONFIG_X86_32
EXPORT_SYMBOL(save_processor_state);
#endif

static void do_fpu_end(void)
{
	/*
	 * Restore FPU regs if necessary.
	 */
	kernel_fpu_end();
}

static void fix_processor_context(void)
{
	int cpu = smp_processor_id();
	struct tss_struct *t = &per_cpu(init_tss, cpu);

	set_tss_desc(cpu, t);	/*
				 * This just modifies memory; should not be
				 * necessary. But... This is necessary, because
				 * 386 hardware has concept of busy TSS or some
				 * similar stupidity.
				 */

	load_TR_desc();				/* This does ltr */
	load_LDT(&current->active_mm->context);	/* This does lldt */

	/*
	 * Now maybe reload the debug registers
	 */
	if (current->thread.debugreg7) {
		set_debugreg(current->thread.debugreg0, 0);
		set_debugreg(current->thread.debugreg1, 1);
		set_debugreg(current->thread.debugreg2, 2);
		set_debugreg(current->thread.debugreg3, 3);
		/* no 4 and 5 */
		set_debugreg(current->thread.debugreg6, 6);
		set_debugreg(current->thread.debugreg7, 7);
	}

}

static void __restore_processor_state(struct saved_context *ctxt)
{
	/*
	 * control registers
	 */
	/* cr4 was introduced in the Pentium CPU */
	if (ctxt->cr4)
		write_cr4(ctxt->cr4);
	write_cr3(ctxt->cr3);
	write_cr2(ctxt->cr2);
	write_cr0(ctxt->cr0);

	/*
	 * now restore the descriptor tables to their proper values
	 * ltr is done i fix_processor_context().
	 */
	load_gdt(&ctxt->gdt);
	load_idt(&ctxt->idt);

	/*
	 * segment registers
	 */
	loadsegment(es, ctxt->es);
	loadsegment(fs, ctxt->fs);
	loadsegment(gs, ctxt->gs);
	loadsegment(ss, ctxt->ss);

	/*
	 * sysenter MSRs
	 */
	if (boot_cpu_has(X86_FEATURE_SEP))
		enable_sep_cpu();

	/*
	 * restore XCR0 for xsave capable cpu's.
	 */
	if (cpu_has_xsave)
		xsetbv(XCR_XFEATURE_ENABLED_MASK, pcntxt_mask);

	fix_processor_context();
	do_fpu_end();
	mtrr_ap_init();
	mcheck_init(&boot_cpu_data);
}

/* Needed by apm.c */
void restore_processor_state(void)
{
	__restore_processor_state(&saved_context);
}
EXPORT_SYMBOL(restore_processor_state);
