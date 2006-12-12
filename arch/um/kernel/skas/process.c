/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/ptrace.h"
#include "linux/proc_fs.h"
#include "linux/file.h"
#include "linux/errno.h"
#include "linux/init.h"
#include "asm/uaccess.h"
#include "asm/atomic.h"
#include "kern_util.h"
#include "skas.h"
#include "os.h"
#include "user_util.h"
#include "tlb.h"
#include "kern.h"
#include "mode.h"
#include "registers.h"

void switch_to_skas(void *prev, void *next)
{
	struct task_struct *from, *to;

	from = prev;
	to = next;

	/* XXX need to check runqueues[cpu].idle */
	if(current->pid == 0)
		switch_timers(0);

	switch_threads(&from->thread.mode.skas.switch_buf,
		       &to->thread.mode.skas.switch_buf);

	arch_switch_to_skas(current->thread.prev_sched, current);

	if(current->pid == 0)
		switch_timers(1);
}

extern void schedule_tail(struct task_struct *prev);

/* This is called magically, by its address being stuffed in a jmp_buf
 * and being longjmp-d to.
 */
void new_thread_handler(void)
{
	int (*fn)(void *), n;
	void *arg;

	if(current->thread.prev_sched != NULL)
		schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	fn = current->thread.request.u.thread.proc;
	arg = current->thread.request.u.thread.arg;

	/* The return value is 1 if the kernel thread execs a process,
	 * 0 if it just exits
	 */
	n = run_kernel_thread(fn, arg, &current->thread.exec_buf);
	if(n == 1){
		/* Handle any immediate reschedules or signals */
		interrupt_end();
		userspace(&current->thread.regs.regs);
	}
	else do_exit(0);
}

void release_thread_skas(struct task_struct *task)
{
}

/* Called magically, see new_thread_handler above */
void fork_handler(void)
{
	force_flush_all();
	if(current->thread.prev_sched == NULL)
		panic("blech");

	schedule_tail(current->thread.prev_sched);

	/* XXX: if interrupt_end() calls schedule, this call to
	 * arch_switch_to_skas isn't needed. We could want to apply this to
	 * improve performance. -bb */
	arch_switch_to_skas(current->thread.prev_sched, current);

	current->thread.prev_sched = NULL;

/* Handle any immediate reschedules or signals */
	interrupt_end();

	userspace(&current->thread.regs.regs);
}

int copy_thread_skas(int nr, unsigned long clone_flags, unsigned long sp,
		     unsigned long stack_top, struct task_struct * p,
		     struct pt_regs *regs)
{
	void (*handler)(void);

	if(current->thread.forking){
	  	memcpy(&p->thread.regs.regs.skas, &regs->regs.skas,
		       sizeof(p->thread.regs.regs.skas));
		REGS_SET_SYSCALL_RETURN(p->thread.regs.regs.skas.regs, 0);
		if(sp != 0) REGS_SP(p->thread.regs.regs.skas.regs) = sp;

		handler = fork_handler;

		arch_copy_thread(&current->thread.arch, &p->thread.arch);
	}
	else {
		init_thread_registers(&p->thread.regs.regs);
		p->thread.request.u.thread = current->thread.request.u.thread;
		handler = new_thread_handler;
	}

	new_thread(task_stack_page(p), &p->thread.mode.skas.switch_buf,
		   handler);
	return(0);
}

int new_mm(unsigned long stack)
{
	int fd;

	fd = os_open_file("/proc/mm", of_cloexec(of_write(OPENFLAGS())), 0);
	if(fd < 0)
		return(fd);

	if(skas_needs_stub)
		map_stub_pages(fd, CONFIG_STUB_CODE, CONFIG_STUB_DATA, stack);

	return(fd);
}

void init_idle_skas(void)
{
	cpu_tasks[current_thread->cpu].pid = os_getpid();
	default_idle();
}

extern void start_kernel(void);

static int start_kernel_proc(void *unused)
{
	int pid;

	block_signals();
	pid = os_getpid();

	cpu_tasks[0].pid = pid;
	cpu_tasks[0].task = current;
#ifdef CONFIG_SMP
	cpu_online_map = cpumask_of_cpu(0);
#endif
	start_kernel();
	return(0);
}

extern int userspace_pid[];

int start_uml_skas(void)
{
	if(proc_mm)
		userspace_pid[0] = start_userspace(0);

	init_new_thread_signals();

	init_task.thread.request.u.thread.proc = start_kernel_proc;
	init_task.thread.request.u.thread.arg = NULL;
	return(start_idle_thread(task_stack_page(&init_task),
				 &init_task.thread.mode.skas.switch_buf));
}

int external_pid_skas(struct task_struct *task)
{
#warning Need to look up userspace_pid by cpu
	return(userspace_pid[0]);
}

int thread_pid_skas(struct task_struct *task)
{
#warning Need to look up userspace_pid by cpu
	return(userspace_pid[0]);
}

void kill_off_processes_skas(void)
{
	if(proc_mm)
#warning need to loop over userspace_pids in kill_off_processes_skas
		os_kill_ptraced_process(userspace_pid[0], 1);
	else {
		struct task_struct *p;
		int pid, me;

		me = os_getpid();
		for_each_process(p){
			if(p->mm == NULL)
				continue;

			pid = p->mm->context.skas.id.u.pid;
			os_kill_ptraced_process(pid, 1);
		}
	}
}

unsigned long current_stub_stack(void)
{
	if(current->mm == NULL)
		return(0);

	return(current->mm->context.skas.id.stack);
}
