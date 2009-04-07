/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <stdarg.h>

#include <linux/stackprotector.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
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
#include <linux/utsname.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/ptrace.h>
#include <linux/random.h>
#include <linux/personality.h>
#include <linux/tick.h>
#include <linux/percpu.h>
#include <linux/prctl.h>
#include <linux/dmi.h>
#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/kdebug.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/desc.h>
#ifdef CONFIG_MATH_EMULATION
#include <asm/math_emu.h>
#endif

#include <linux/err.h>

#include <asm/tlbflush.h>
#include <asm/cpu.h>
#include <asm/idle.h>
#include <asm/syscalls.h>
#include <asm/ds.h>

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

DEFINE_PER_CPU(struct task_struct *, current_task) = &init_task;
EXPORT_PER_CPU_SYMBOL(current_task);

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return ((unsigned long *)tsk->thread.sp)[3];
}

#ifndef CONFIG_SMP
static inline void play_dead(void)
{
	BUG();
}
#endif

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	int cpu = smp_processor_id();

	/*
	 * If we're the non-boot CPU, nothing set the stack canary up
	 * for us.  CPU0 already has it initialized but no harm in
	 * doing it again.  This is a good place for updating it, as
	 * we wont ever return from this function (so the invalid
	 * canaries already on the stack wont ever trigger).
	 */
	boot_init_stack_canary();

	current_thread_info()->status |= TS_POLLING;

	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_stop_sched_tick(1);
		while (!need_resched()) {

			check_pgt_cache();
			rmb();

			if (cpu_is_offline(cpu))
				play_dead();

			local_irq_disable();
			/* Don't trace irqs off for idle */
			stop_critical_timings();
			pm_idle();
			start_critical_timings();
		}
		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void __show_regs(struct pt_regs *regs, int all)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L;
	unsigned long d0, d1, d2, d3, d6, d7;
	unsigned long sp;
	unsigned short ss, gs;
	const char *board;

	if (user_mode_vm(regs)) {
		sp = regs->sp;
		ss = regs->ss & 0xffff;
		gs = get_user_gs(regs);
	} else {
		sp = (unsigned long) (&regs->sp);
		savesegment(ss, ss);
		savesegment(gs, gs);
	}

	printk("\n");

	board = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!board)
		board = "";
	printk("Pid: %d, comm: %s %s (%s %.*s) %s\n",
			task_pid_nr(current), current->comm,
			print_tainted(), init_utsname()->release,
			(int)strcspn(init_utsname()->version, " "),
			init_utsname()->version, board);

	printk("EIP: %04x:[<%08lx>] EFLAGS: %08lx CPU: %d\n",
			(u16)regs->cs, regs->ip, regs->flags,
			smp_processor_id());
	print_symbol("EIP is at %s\n", regs->ip);

	printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		regs->ax, regs->bx, regs->cx, regs->dx);
	printk("ESI: %08lx EDI: %08lx EBP: %08lx ESP: %08lx\n",
		regs->si, regs->di, regs->bp, sp);
	printk(" DS: %04x ES: %04x FS: %04x GS: %04x SS: %04x\n",
	       (u16)regs->ds, (u16)regs->es, (u16)regs->fs, gs, ss);

	if (!all)
		return;

	cr0 = read_cr0();
	cr2 = read_cr2();
	cr3 = read_cr3();
	cr4 = read_cr4_safe();
	printk("CR0: %08lx CR2: %08lx CR3: %08lx CR4: %08lx\n",
			cr0, cr2, cr3, cr4);

	get_debugreg(d0, 0);
	get_debugreg(d1, 1);
	get_debugreg(d2, 2);
	get_debugreg(d3, 3);
	printk("DR0: %08lx DR1: %08lx DR2: %08lx DR3: %08lx\n",
			d0, d1, d2, d3);

	get_debugreg(d6, 6);
	get_debugreg(d7, 7);
	printk("DR6: %08lx DR7: %08lx\n",
			d6, d7);
}

void show_regs(struct pt_regs *regs)
{
	__show_regs(regs, 1);
	show_trace(NULL, regs, &regs->sp, regs->bp);
}

/*
 * This gets run with %bx containing the
 * function to call, and %dx containing
 * the "args".
 */
extern void kernel_thread_helper(void);

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.bx = (unsigned long) fn;
	regs.dx = (unsigned long) arg;

	regs.ds = __USER_DS;
	regs.es = __USER_DS;
	regs.fs = __KERNEL_PERCPU;
	regs.gs = __KERNEL_STACK_CANARY;
	regs.orig_ax = -1;
	regs.ip = (unsigned long) kernel_thread_helper;
	regs.cs = __KERNEL_CS | get_kernel_rpl();
	regs.flags = X86_EFLAGS_IF | X86_EFLAGS_SF | X86_EFLAGS_PF | 0x2;

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);

void release_thread(struct task_struct *dead_task)
{
	BUG_ON(dead_task->mm);
	release_vm86_irqs(dead_task);
}

/*
 * This gets called before we allocate a new thread and copy
 * the current task into it.
 */
void prepare_to_copy(struct task_struct *tsk)
{
	unlazy_fpu(tsk);
}

int copy_thread(unsigned long clone_flags, unsigned long sp,
	unsigned long unused,
	struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	struct task_struct *tsk;
	int err;

	childregs = task_pt_regs(p);
	*childregs = *regs;
	childregs->ax = 0;
	childregs->sp = sp;

	p->thread.sp = (unsigned long) childregs;
	p->thread.sp0 = (unsigned long) (childregs+1);

	p->thread.ip = (unsigned long) ret_from_fork;

	task_user_gs(p) = get_user_gs(regs);

	tsk = current;
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
			(struct user_desc __user *)childregs->si, 0);

	if (err && p->thread.io_bitmap_ptr) {
		kfree(p->thread.io_bitmap_ptr);
		p->thread.io_bitmap_max = 0;
	}

	ds_copy_thread(p, current);

	clear_tsk_thread_flag(p, TIF_DEBUGCTLMSR);
	p->thread.debugctlmsr = 0;

	return err;
}

void
start_thread(struct pt_regs *regs, unsigned long new_ip, unsigned long new_sp)
{
	set_user_gs(regs, 0);
	regs->fs		= 0;
	set_fs(USER_DS);
	regs->ds		= __USER_DS;
	regs->es		= __USER_DS;
	regs->ss		= __USER_DS;
	regs->cs		= __USER_CS;
	regs->ip		= new_ip;
	regs->sp		= new_sp;
	/*
	 * Free the old FP and other extended state
	 */
	free_thread_xstate(current);
}
EXPORT_SYMBOL_GPL(start_thread);


/*
 *	switch_to(x,yn) should switch tasks from x to y.
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
__notrace_funcgraph struct task_struct *
__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
				 *next = &next_p->thread;
	int cpu = smp_processor_id();
	struct tss_struct *tss = &per_cpu(init_tss, cpu);

	/* never put a printk in __switch_to... printk() calls wake_up*() indirectly */

	__unlazy_fpu(prev_p);


	/* we're going to use this soon, after a few expensive things */
	if (next_p->fpu_counter > 5)
		prefetch(next->xstate);

	/*
	 * Reload esp0.
	 */
	load_sp0(tss, next);

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
	 * done before math_state_restore, so the TS bit is up
	 * to date.
	 */
	arch_end_context_switch(next_p);

	/* If the task has used fpu the last 5 timeslices, just do a full
	 * restore of the math state immediately to avoid the trap; the
	 * chances of needing FPU soon are obviously high now
	 *
	 * tsk_used_math() checks prevent calling math_state_restore(),
	 * which can sleep in the case of !tsk_used_math()
	 */
	if (tsk_used_math(next_p) && next_p->fpu_counter > 5)
		math_state_restore();

	/*
	 * Restore %gs if needed (which is common)
	 */
	if (prev->gs | next->gs)
		lazy_load_gs(next->gs);

	percpu_write(current_task, next_p);

	return prev_p;
}

int sys_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	clone_flags = regs->bx;
	newsp = regs->cx;
	parent_tidptr = (int __user *)regs->dx;
	child_tidptr = (int __user *)regs->di;
	if (!newsp)
		newsp = regs->sp;
	return do_fork(clone_flags, newsp, regs, 0, parent_tidptr, child_tidptr);
}

/*
 * sys_execve() executes a new program.
 */
int sys_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname((char __user *) regs->bx);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename,
			(char __user * __user *) regs->cx,
			(char __user * __user *) regs->dx,
			regs);
	if (error == 0) {
		/* Make sure we don't return using sysenter.. */
		set_thread_flag(TIF_IRET);
	}
	putname(filename);
out:
	return error;
}

#define top_esp                (THREAD_SIZE - sizeof(unsigned long))
#define top_ebp                (THREAD_SIZE - 2*sizeof(unsigned long))

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long bp, sp, ip;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
	stack_page = (unsigned long)task_stack_page(p);
	sp = p->thread.sp;
	if (!stack_page || sp < stack_page || sp > top_esp+stack_page)
		return 0;
	/* include/asm-i386/system.h:switch_to() pushes bp last. */
	bp = *(unsigned long *) sp;
	do {
		if (bp < stack_page || bp > top_ebp+stack_page)
			return 0;
		ip = *(unsigned long *) (bp+4);
		if (!in_sched_functions(ip))
			return ip;
		bp = *(unsigned long *) bp;
	} while (count++ < 16);
	return 0;
}

unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() % 8192;
	return sp & ~0xf;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long range_end = mm->brk + 0x02000000;
	return randomize_range(mm->brk, range_end, 0) ? : mm->brk;
}
