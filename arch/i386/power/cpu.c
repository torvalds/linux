/*
 * Suspend support specific for i386.
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/sysrq.h>
#include <linux/proc_fs.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <linux/acpi.h>

#include <asm/uaccess.h>
#include <asm/acpi.h>
#include <asm/tlbflush.h>
#include <asm/processor.h>

static struct saved_context saved_context;

unsigned long saved_context_ebx;
unsigned long saved_context_esp, saved_context_ebp;
unsigned long saved_context_esi, saved_context_edi;
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

	/*
	 * segment registers
	 */
	asm volatile ("movw %%es, %0" : "=m" (ctxt->es));
	asm volatile ("movw %%fs, %0" : "=m" (ctxt->fs));
	asm volatile ("movw %%gs, %0" : "=m" (ctxt->gs));
	asm volatile ("movw %%ss, %0" : "=m" (ctxt->ss));

	/*
	 * control registers 
	 */
	asm volatile ("movl %%cr0, %0" : "=r" (ctxt->cr0));
	asm volatile ("movl %%cr2, %0" : "=r" (ctxt->cr2));
	asm volatile ("movl %%cr3, %0" : "=r" (ctxt->cr3));
	asm volatile ("movl %%cr4, %0" : "=r" (ctxt->cr4));
}

void save_processor_state(void)
{
	__save_processor_state(&saved_context);
}

static void
do_fpu_end(void)
{
        /* restore FPU regs if necessary */
	/* Do it out of line so that gcc does not move cr0 load to some stupid place */
        kernel_fpu_end();
	mxcsr_feature_mask_init();
}


static void fix_processor_context(void)
{
	int cpu = smp_processor_id();
	struct tss_struct * t = &per_cpu(init_tss, cpu);

	set_tss_desc(cpu,t);	/* This just modifies memory; should not be necessary. But... This is necessary, because 386 hardware has concept of busy TSS or some similar stupidity. */
        per_cpu(cpu_gdt_table, cpu)[GDT_ENTRY_TSS].b &= 0xfffffdff;

	load_TR_desc();				/* This does ltr */
	load_LDT(&current->active_mm->context);	/* This does lldt */

	/*
	 * Now maybe reload the debug registers
	 */
	if (current->thread.debugreg[7]){
		set_debugreg(current->thread.debugreg[0], 0);
		set_debugreg(current->thread.debugreg[1], 1);
		set_debugreg(current->thread.debugreg[2], 2);
		set_debugreg(current->thread.debugreg[3], 3);
		/* no 4 and 5 */
		set_debugreg(current->thread.debugreg[6], 6);
		set_debugreg(current->thread.debugreg[7], 7);
	}

}

void __restore_processor_state(struct saved_context *ctxt)
{
	/*
	 * control registers
	 */
	asm volatile ("movl %0, %%cr4" :: "r" (ctxt->cr4));
	asm volatile ("movl %0, %%cr3" :: "r" (ctxt->cr3));
	asm volatile ("movl %0, %%cr2" :: "r" (ctxt->cr2));
	asm volatile ("movl %0, %%cr0" :: "r" (ctxt->cr0));

	/*
	 * now restore the descriptor tables to their proper values
	 * ltr is done i fix_processor_context().
	 */
	asm volatile ("lgdt %0" :: "m" (ctxt->gdt_limit));
	asm volatile ("lidt %0" :: "m" (ctxt->idt_limit));

	/*
	 * segment registers
	 */
	asm volatile ("movw %0, %%es" :: "r" (ctxt->es));
	asm volatile ("movw %0, %%fs" :: "r" (ctxt->fs));
	asm volatile ("movw %0, %%gs" :: "r" (ctxt->gs));
	asm volatile ("movw %0, %%ss" :: "r" (ctxt->ss));

	/*
	 * sysenter MSRs
	 */
	if (boot_cpu_has(X86_FEATURE_SEP))
		enable_sep_cpu();

	fix_processor_context();
	do_fpu_end();
	mtrr_ap_init();
}

void restore_processor_state(void)
{
	__restore_processor_state(&saved_context);
}

/* Needed by apm.c */
EXPORT_SYMBOL(save_processor_state);
EXPORT_SYMBOL(restore_processor_state);
