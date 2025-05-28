// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright 2003 PathScale, Inc.
 */

#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/hardirq.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/personality.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/random.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/seq_file.h>
#include <linux/tick.h>
#include <linux/threads.h>
#include <linux/resume_user_mode.h>
#include <asm/current.h>
#include <asm/mmu_context.h>
#include <asm/switch_to.h>
#include <asm/exec.h>
#include <linux/uaccess.h>
#include <as-layout.h>
#include <kern_util.h>
#include <os.h>
#include <skas.h>
#include <registers.h>
#include <linux/time-internal.h>
#include <linux/elfcore.h>

/*
 * This is a per-cpu array.  A processor only modifies its entry and it only
 * cares about its entry, so it's OK if another processor is modifying its
 * entry.
 */
struct task_struct *cpu_tasks[NR_CPUS];
EXPORT_SYMBOL(cpu_tasks);

void free_stack(unsigned long stack, int order)
{
	free_pages(stack, order);
}

unsigned long alloc_stack(int order, int atomic)
{
	unsigned long page;
	gfp_t flags = GFP_KERNEL;

	if (atomic)
		flags = GFP_ATOMIC;
	page = __get_free_pages(flags, order);

	return page;
}

static inline void set_current(struct task_struct *task)
{
	cpu_tasks[task_thread_info(task)->cpu] = task;
}

struct task_struct *__switch_to(struct task_struct *from, struct task_struct *to)
{
	to->thread.prev_sched = from;
	set_current(to);

	switch_threads(&from->thread.switch_buf, &to->thread.switch_buf);
	arch_switch_to(current);

	return current->thread.prev_sched;
}

void interrupt_end(void)
{
	struct pt_regs *regs = &current->thread.regs;

	if (need_resched())
		schedule();
	if (test_thread_flag(TIF_SIGPENDING) ||
	    test_thread_flag(TIF_NOTIFY_SIGNAL))
		do_signal(regs);
	if (test_thread_flag(TIF_NOTIFY_RESUME))
		resume_user_mode_work(regs);
}

int get_current_pid(void)
{
	return task_pid_nr(current);
}

/*
 * This is called magically, by its address being stuffed in a jmp_buf
 * and being longjmp-d to.
 */
void new_thread_handler(void)
{
	int (*fn)(void *);
	void *arg;

	if (current->thread.prev_sched != NULL)
		schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	fn = current->thread.request.thread.proc;
	arg = current->thread.request.thread.arg;

	/*
	 * callback returns only if the kernel thread execs a process
	 */
	fn(arg);
	userspace(&current->thread.regs.regs);
}

/* Called magically, see new_thread_handler above */
static void fork_handler(void)
{
	schedule_tail(current->thread.prev_sched);

	/*
	 * XXX: if interrupt_end() calls schedule, this call to
	 * arch_switch_to isn't needed. We could want to apply this to
	 * improve performance. -bb
	 */
	arch_switch_to(current);

	current->thread.prev_sched = NULL;

	userspace(&current->thread.regs.regs);
}

int copy_thread(struct task_struct * p, const struct kernel_clone_args *args)
{
	unsigned long clone_flags = args->flags;
	unsigned long sp = args->stack;
	unsigned long tls = args->tls;
	void (*handler)(void);
	int ret = 0;

	p->thread = (struct thread_struct) INIT_THREAD;

	if (!args->fn) {
	  	memcpy(&p->thread.regs.regs, current_pt_regs(),
		       sizeof(p->thread.regs.regs));
		PT_REGS_SET_SYSCALL_RETURN(&p->thread.regs, 0);
		if (sp != 0)
			REGS_SP(p->thread.regs.regs.gp) = sp;

		handler = fork_handler;

		arch_copy_thread(&current->thread.arch, &p->thread.arch);
	} else {
		get_safe_registers(p->thread.regs.regs.gp, p->thread.regs.regs.fp);
		p->thread.request.thread.proc = args->fn;
		p->thread.request.thread.arg = args->fn_arg;
		handler = new_thread_handler;
	}

	new_thread(task_stack_page(p), &p->thread.switch_buf, handler);

	if (!args->fn) {
		clear_flushed_tls(p);

		/*
		 * Set a new TLS for the child thread?
		 */
		if (clone_flags & CLONE_SETTLS)
			ret = arch_set_tls(p, tls);
	}

	return ret;
}

void initial_thread_cb(void (*proc)(void *), void *arg)
{
	int save_kmalloc_ok = kmalloc_ok;

	kmalloc_ok = 0;
	initial_thread_cb_skas(proc, arg);
	kmalloc_ok = save_kmalloc_ok;
}

int arch_dup_task_struct(struct task_struct *dst,
			 struct task_struct *src)
{
	/* init_task is not dynamically sized (missing FPU state) */
	if (unlikely(src == &init_task)) {
		memcpy(dst, src, sizeof(init_task));
		memset((void *)dst + sizeof(init_task), 0,
		       arch_task_struct_size - sizeof(init_task));
	} else {
		memcpy(dst, src, arch_task_struct_size);
	}

	return 0;
}

void um_idle_sleep(void)
{
	if (time_travel_mode != TT_MODE_OFF)
		time_travel_sleep();
	else
		os_idle_sleep();
}

void arch_cpu_idle(void)
{
	um_idle_sleep();
}

int __uml_cant_sleep(void) {
	return in_atomic() || irqs_disabled() || in_interrupt();
	/* Is in_interrupt() really needed? */
}

extern exitcall_t __uml_exitcall_begin, __uml_exitcall_end;

void do_uml_exitcalls(void)
{
	exitcall_t *call;

	call = &__uml_exitcall_end;
	while (--call >= &__uml_exitcall_begin)
		(*call)();
}

char *uml_strdup(const char *string)
{
	return kstrdup(string, GFP_KERNEL);
}
EXPORT_SYMBOL(uml_strdup);

int copy_from_user_proc(void *to, void __user *from, int size)
{
	return copy_from_user(to, from, size);
}

int singlestepping(void)
{
	return test_thread_flag(TIF_SINGLESTEP);
}

/*
 * Only x86 and x86_64 have an arch_align_stack().
 * All other arches have "#define arch_align_stack(x) (x)"
 * in their asm/exec.h
 * As this is included in UML from asm-um/system-generic.h,
 * we can use it to behave as the subarch does.
 */
#ifndef arch_align_stack
unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_u32_below(8192);
	return sp & ~0xf;
}
#endif

unsigned long __get_wchan(struct task_struct *p)
{
	unsigned long stack_page, sp, ip;
	bool seen_sched = 0;

	stack_page = (unsigned long) task_stack_page(p);
	/* Bail if the process has no kernel stack for some reason */
	if (stack_page == 0)
		return 0;

	sp = p->thread.switch_buf->JB_SP;
	/*
	 * Bail if the stack pointer is below the bottom of the kernel
	 * stack for some reason
	 */
	if (sp < stack_page)
		return 0;

	while (sp < stack_page + THREAD_SIZE) {
		ip = *((unsigned long *) sp);
		if (in_sched_functions(ip))
			/* Ignore everything until we're above the scheduler */
			seen_sched = 1;
		else if (kernel_text_address(ip) && seen_sched)
			return ip;

		sp += sizeof(unsigned long);
	}

	return 0;
}
