/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/mc146818rtc.h>
#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/ptrace.h>
#include <linux/personality.h>
#include <linux/percpu.h>
#include <linux/prctl.h>
#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/kdebug.h>
#include <linux/syscalls.h>

#include <asm/pgtable.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/fpu/internal.h>
#include <asm/desc.h>
#ifdef CONFIG_MATH_EMULATION
#include <asm/math_emu.h>
#endif

#include <linux/err.h>

#include <asm/tlbflush.h>
#include <asm/cpu.h>
#include <asm/syscalls.h>
#include <asm/debugreg.h>
#include <asm/switch_to.h>
#include <asm/vm86.h>
#include <asm/intel_rdt.h>
#include <asm/proto.h>

void __show_regs(struct pt_regs *regs, int all)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L;
	unsigned long d0, d1, d2, d3, d6, d7;
	unsigned long sp;
	unsigned short ss, gs;

	if (user_mode(regs)) {
		sp = regs->sp;
		ss = regs->ss & 0xffff;
		gs = get_user_gs(regs);
	} else {
		sp = kernel_stack_pointer(regs);
		savesegment(ss, ss);
		savesegment(gs, gs);
	}

	printk(KERN_DEFAULT "EIP: %pS\n", (void *)regs->ip);
	printk(KERN_DEFAULT "EFLAGS: %08lx CPU: %d\n", regs->flags,
		raw_smp_processor_id());

	printk(KERN_DEFAULT "EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		regs->ax, regs->bx, regs->cx, regs->dx);
	printk(KERN_DEFAULT "ESI: %08lx EDI: %08lx EBP: %08lx ESP: %08lx\n",
		regs->si, regs->di, regs->bp, sp);
	printk(KERN_DEFAULT " DS: %04x ES: %04x FS: %04x GS: %04x SS: %04x\n",
	       (u16)regs->ds, (u16)regs->es, (u16)regs->fs, gs, ss);

	if (!all)
		return;

	cr0 = read_cr0();
	cr2 = read_cr2();
	cr3 = read_cr3();
	cr4 = __read_cr4();
	printk(KERN_DEFAULT "CR0: %08lx CR2: %08lx CR3: %08lx CR4: %08lx\n",
			cr0, cr2, cr3, cr4);

	get_debugreg(d0, 0);
	get_debugreg(d1, 1);
	get_debugreg(d2, 2);
	get_debugreg(d3, 3);
	get_debugreg(d6, 6);
	get_debugreg(d7, 7);

	/* Only print out debug registers if they are in their non-default state. */
	if ((d0 == 0) && (d1 == 0) && (d2 == 0) && (d3 == 0) &&
	    (d6 == DR6_RESERVED) && (d7 == 0x400))
		return;

	printk(KERN_DEFAULT "DR0: %08lx DR1: %08lx DR2: %08lx DR3: %08lx\n",
			d0, d1, d2, d3);
	printk(KERN_DEFAULT "DR6: %08lx DR7: %08lx\n",
			d6, d7);
}

void release_thread(struct task_struct *dead_task)
{
	BUG_ON(dead_task->mm);
	release_vm86_irqs(dead_task);
}

int copy_thread_tls(unsigned long clone_flags, unsigned long sp,
	unsigned long arg, struct task_struct *p, unsigned long tls)
{
	struct pt_regs *childregs = task_pt_regs(p);
	struct fork_frame *fork_frame = container_of(childregs, struct fork_frame, regs);
	struct inactive_task_frame *frame = &fork_frame->frame;
	struct task_struct *tsk;
	int err;

	frame->bp = 0;
	frame->ret_addr = (unsigned long) ret_from_fork;
	p->thread.sp = (unsigned long) fork_frame;
	p->thread.sp0 = (unsigned long) (childregs+1);
	memset(p->thread.ptrace_bps, 0, sizeof(p->thread.ptrace_bps));

	if (unlikely(p->flags & PF_KTHREAD)) {
		/* kernel thread */
		memset(childregs, 0, sizeof(struct pt_regs));
		frame->bx = sp;		/* function */
		frame->di = arg;
		p->thread.io_bitmap_ptr = NULL;
		return 0;
	}
	frame->bx = 0;
	*childregs = *current_pt_regs();
	childregs->ax = 0;
	if (sp)
		childregs->sp = sp;

	task_user_gs(p) = get_user_gs(current_pt_regs());

	p->thread.io_bitmap_ptr = NULL;
	tsk = current;
	err = -ENOMEM;

	if (unlikely(test_tsk_thread_flag(tsk, TIF_IO_BITMAP))) {
		p->thread.io_bitmap_ptr = kmemdup(tsk->thread.io_bitmap_ptr,
						IO_BITMAP_BYTES, GFP_KERNEL);
		if (!p->thread.io_bitmap_ptr) {
			p->thread.io_bitmap_max = 0;
			return -ENOMEM;
		}
		set_tsk_thread_flag(p, TIF_IO_BITMAP);
	}

	err = 0;

	/*
	 * Set a new TLS for the child thread?
	 */
	if (clone_flags & CLONE_SETTLS)
		err = do_set_thread_area(p, -1,
			(struct user_desc __user *)tls, 0);

	if (err && p->thread.io_bitmap_ptr) {
		kfree(p->thread.io_bitmap_ptr);
		p->thread.io_bitmap_max = 0;
	}
	return err;
}

void
start_thread(struct pt_regs *regs, unsigned long new_ip, unsigned long new_sp)
{
	set_user_gs(regs, 0);
	regs->fs		= 0;
	regs->ds		= __USER_DS;
	regs->es		= __USER_DS;
	regs->ss		= __USER_DS;
	regs->cs		= __USER_CS;
	regs->ip		= new_ip;
	regs->sp		= new_sp;
	regs->flags		= X86_EFLAGS_IF;
	force_iret();
}
EXPORT_SYMBOL_GPL(start_thread);


/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 * We fsave/fwait so that an exception goes off at the right time
 * (as a call from the fsave or fwait in effect) rather than to
 * the wrong process. Lazy FP saving no longer makes any sense
 * with modern CPU's, and this simplifies a lot of things (SMP
 * and UP become the same).
 *
 * NOTE! We used to use the x86 hardware context switching. The
 * reason for not using it any more becomes apparent when you
 * try to recover gracefully from saved state that is no longer
 * valid (stale segment register values in particular). With the
 * hardware task-switch, there is no way to fix up bad state in
 * a reasonable manner.
 *
 * The fact that Intel documents the hardware task-switching to
 * be slow is a fairly red herring - this code is not noticeably
 * faster. However, there _is_ some room for improvement here,
 * so the performance issues may eventually be a valid point.
 * More important, however, is the fact that this allows us much
 * more flexibility.
 *
 * The return value (in %ax) will be the "prev" task after
 * the task-switch, and shows up in ret_from_fork in entry.S,
 * for example.
 */
__visible __notrace_funcgraph struct task_struct *
__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
			     *next = &next_p->thread;
	struct fpu *prev_fpu = &prev->fpu;
	struct fpu *next_fpu = &next->fpu;
	int cpu = smp_processor_id();
	struct tss_struct *tss = &per_cpu(cpu_tss, cpu);

	/* never put a printk in __switch_to... printk() calls wake_up*() indirectly */

	switch_fpu_prepare(prev_fpu, cpu);

	/*
	 * Save away %gs. No need to save %fs, as it was saved on the
	 * stack on entry.  No need to save %es and %ds, as those are
	 * always kernel segments while inside the kernel.  Doing this
	 * before setting the new TLS descriptors avoids the situation
	 * where we temporarily have non-reloadable segments in %fs
	 * and %gs.  This could be an issue if the NMI handler ever
	 * used %fs or %gs (it does not today), or if the kernel is
	 * running inside of a hypervisor layer.
	 */
	lazy_save_gs(prev->gs);

	/*
	 * Load the per-thread Thread-Local Storage descriptor.
	 */
	load_TLS(next, cpu);

	/*
	 * Restore IOPL if needed.  In normal use, the flags restore
	 * in the switch assembly will handle this.  But if the kernel
	 * is running virtualized at a non-zero CPL, the popf will
	 * not restore flags, so it must be done in a separate step.
	 */
	if (get_kernel_rpl() && unlikely(prev->iopl != next->iopl))
		set_iopl_mask(next->iopl);

	/*
	 * Now maybe handle debug registers and/or IO bitmaps
	 */
	if (unlikely(task_thread_info(prev_p)->flags & _TIF_WORK_CTXSW_PREV ||
		     task_thread_info(next_p)->flags & _TIF_WORK_CTXSW_NEXT))
		__switch_to_xtra(prev_p, next_p, tss);

	/*
	 * Leave lazy mode, flushing any hypercalls made here.
	 * This must be done before restoring TLS segments so
	 * the GDT and LDT are properly updated, and must be
	 * done before fpu__restore(), so the TS bit is up
	 * to date.
	 */
	arch_end_context_switch(next_p);

	/*
	 * Reload esp0 and cpu_current_top_of_stack.  This changes
	 * current_thread_info().
	 */
	load_sp0(tss, next);
	this_cpu_write(cpu_current_top_of_stack,
		       (unsigned long)task_stack_page(next_p) +
		       THREAD_SIZE);

	/*
	 * Restore %gs if needed (which is common)
	 */
	if (prev->gs | next->gs)
		lazy_load_gs(next->gs);

	switch_fpu_finish(next_fpu, cpu);

	this_cpu_write(current_task, next_p);

	/* Load the Intel cache allocation PQR MSR. */
	intel_rdt_sched_in();

	return prev_p;
}

SYSCALL_DEFINE2(arch_prctl, int, option, unsigned long, arg2)
{
	return do_arch_prctl_common(current, option, arg2);
}
