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
#include "time_user.h"
#include "signal_user.h"
#include "skas.h"
#include "os.h"
#include "user_util.h"
#include "tlb.h"
#include "kern.h"
#include "mode.h"
#include "proc_mm.h"
#include "registers.h"

void *switch_to_skas(void *prev, void *next)
{
	struct task_struct *from, *to;

	from = prev;
	to = next;

	/* XXX need to check runqueues[cpu].idle */
	if(current->pid == 0)
		switch_timers(0);

	to->thread.prev_sched = from;
	set_current(to);

	switch_threads(&from->thread.mode.skas.switch_buf, 
		       to->thread.mode.skas.switch_buf);

	if(current->pid == 0)
		switch_timers(1);

	return(current->thread.prev_sched);
}

extern void schedule_tail(struct task_struct *prev);

void new_thread_handler(int sig)
{
	int (*fn)(void *), n;
	void *arg;

	fn = current->thread.request.u.thread.proc;
	arg = current->thread.request.u.thread.arg;
	change_sig(SIGUSR1, 1);
	thread_wait(&current->thread.mode.skas.switch_buf, 
		    current->thread.mode.skas.fork_buf);

	if(current->thread.prev_sched != NULL)
		schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	/* The return value is 1 if the kernel thread execs a process,
	 * 0 if it just exits
	 */
	n = run_kernel_thread(fn, arg, &current->thread.exec_buf);
	if(n == 1)
		userspace(&current->thread.regs.regs);
	else do_exit(0);
}

void new_thread_proc(void *stack, void (*handler)(int sig))
{
	init_new_thread_stack(stack, handler);
	os_usr1_process(os_getpid());
}

void release_thread_skas(struct task_struct *task)
{
}

void exit_thread_skas(void)
{
}

void fork_handler(int sig)
{
        change_sig(SIGUSR1, 1);
 	thread_wait(&current->thread.mode.skas.switch_buf, 
		    current->thread.mode.skas.fork_buf);
  	
	force_flush_all();
	if(current->thread.prev_sched == NULL)
		panic("blech");

	schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	userspace(&current->thread.regs.regs);
}

int copy_thread_skas(int nr, unsigned long clone_flags, unsigned long sp,
		     unsigned long stack_top, struct task_struct * p, 
		     struct pt_regs *regs)
{
  	void (*handler)(int);

	if(current->thread.forking){
	  	memcpy(&p->thread.regs.regs.skas, 
		       &current->thread.regs.regs.skas, 
		       sizeof(p->thread.regs.regs.skas));
		REGS_SET_SYSCALL_RETURN(p->thread.regs.regs.skas.regs, 0);
		if(sp != 0) REGS_SP(p->thread.regs.regs.skas.regs) = sp;

		handler = fork_handler;
	}
	else {
		init_thread_registers(&p->thread.regs.regs);
                p->thread.request.u.thread = current->thread.request.u.thread;
		handler = new_thread_handler;
	}

	new_thread(p->thread_info, &p->thread.mode.skas.switch_buf,
		   &p->thread.mode.skas.fork_buf, handler);
	return(0);
}

int new_mm(int from)
{
	struct proc_mm_op copy;
	int n, fd;

	fd = os_open_file("/proc/mm", of_cloexec(of_write(OPENFLAGS())), 0);
	if(fd < 0)
		return(fd);

	if(from != -1){
		copy = ((struct proc_mm_op) { .op 	= MM_COPY_SEGMENTS,
					      .u 	=
					      { .copy_segments	= from } } );
		n = os_write_file(fd, &copy, sizeof(copy));
		if(n != sizeof(copy))
			printk("new_mm : /proc/mm copy_segments failed, "
			       "err = %d\n", -n);
	}

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

int start_uml_skas(void)
{
	start_userspace(0);

	init_new_thread_signals(1);
	uml_idle_timer();

	init_task.thread.request.u.thread.proc = start_kernel_proc;
	init_task.thread.request.u.thread.arg = NULL;
	return(start_idle_thread(init_task.thread_info,
				 &init_task.thread.mode.skas.switch_buf,
				 &init_task.thread.mode.skas.fork_buf));
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

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
