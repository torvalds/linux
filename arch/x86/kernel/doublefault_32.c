// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/init_task.h>
#include <linux/fs.h>

#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/traps.h>

#define ptr_ok(x) ((x) > PAGE_OFFSET && (x) < PAGE_OFFSET + MAXMEM)

#define TSS(x) this_cpu_read(cpu_tss_rw.x86_tss.x)

static void set_df_gdt_entry(unsigned int cpu);

/*
 * Called by double_fault with CR0.TS and EFLAGS.NT cleared.  The CPU thinks
 * we're running the doublefault task.  Cannot return.
 */
asmlinkage noinstr void __noreturn doublefault_shim(void)
{
	unsigned long cr2;
	struct pt_regs regs;

	BUILD_BUG_ON(sizeof(struct doublefault_stack) != PAGE_SIZE);

	cr2 = native_read_cr2();

	/* Reset back to the normal kernel task. */
	force_reload_TR();
	set_df_gdt_entry(smp_processor_id());

	trace_hardirqs_off();

	/*
	 * Fill in pt_regs.  A downside of doing this in C is that the unwinder
	 * won't see it (no ENCODE_FRAME_POINTER), so a nested stack dump
	 * won't successfully unwind to the source of the double fault.
	 * The main dump from exc_double_fault() is fine, though, since it
	 * uses these regs directly.
	 *
	 * If anyone ever cares, this could be moved to asm.
	 */
	regs.ss		= TSS(ss);
	regs.__ssh	= 0;
	regs.sp		= TSS(sp);
	regs.flags	= TSS(flags);
	regs.cs		= TSS(cs);
	/* We won't go through the entry asm, so we can leave __csh as 0. */
	regs.__csh	= 0;
	regs.ip		= TSS(ip);
	regs.orig_ax	= 0;
	regs.gs		= TSS(gs);
	regs.__gsh	= 0;
	regs.fs		= TSS(fs);
	regs.__fsh	= 0;
	regs.es		= TSS(es);
	regs.__esh	= 0;
	regs.ds		= TSS(ds);
	regs.__dsh	= 0;
	regs.ax		= TSS(ax);
	regs.bp		= TSS(bp);
	regs.di		= TSS(di);
	regs.si		= TSS(si);
	regs.dx		= TSS(dx);
	regs.cx		= TSS(cx);
	regs.bx		= TSS(bx);

	exc_double_fault(&regs, 0, cr2);

	/*
	 * x86_32 does not save the original CR3 anywhere on a task switch.
	 * This means that, even if we wanted to return, we would need to find
	 * some way to reconstruct CR3.  We could make a credible guess based
	 * on cpu_tlbstate, but that would be racy and would not account for
	 * PTI.
	 *
	 * Instead, don't bother.  We can return through
	 * rewind_stack_do_exit() instead.
	 */
	panic("cannot return from double fault\n");
}

DEFINE_PER_CPU_PAGE_ALIGNED(struct doublefault_stack, doublefault_stack) = {
	.tss = {
                /*
                 * No sp0 or ss0 -- we never run CPL != 0 with this TSS
                 * active.  sp is filled in later.
                 */
		.ldt		= 0,
	.io_bitmap_base	= IO_BITMAP_OFFSET_INVALID,

		.ip		= (unsigned long) asm_exc_double_fault,
		.flags		= X86_EFLAGS_FIXED,
		.es		= __USER_DS,
		.cs		= __KERNEL_CS,
		.ss		= __KERNEL_DS,
		.ds		= __USER_DS,
		.fs		= __KERNEL_PERCPU,
#ifndef CONFIG_X86_32_LAZY_GS
		.gs		= __KERNEL_STACK_CANARY,
#endif

		.__cr3		= __pa_nodebug(swapper_pg_dir),
	},
};

static void set_df_gdt_entry(unsigned int cpu)
{
	/* Set up doublefault TSS pointer in the GDT */
	__set_tss_desc(cpu, GDT_ENTRY_DOUBLEFAULT_TSS,
		       &get_cpu_entry_area(cpu)->doublefault_stack.tss);

}

void doublefault_init_cpu_tss(void)
{
	unsigned int cpu = smp_processor_id();
	struct cpu_entry_area *cea = get_cpu_entry_area(cpu);

	/*
	 * The linker isn't smart enough to initialize percpu variables that
	 * point to other places in percpu space.
	 */
        this_cpu_write(doublefault_stack.tss.sp,
                       (unsigned long)&cea->doublefault_stack.stack +
                       sizeof(doublefault_stack.stack));

	set_df_gdt_entry(cpu);
}
