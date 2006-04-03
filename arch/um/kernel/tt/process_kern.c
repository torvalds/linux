/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/signal.h"
#include "linux/kernel.h"
#include "linux/interrupt.h"
#include "linux/ptrace.h"
#include "asm/system.h"
#include "asm/pgalloc.h"
#include "asm/ptrace.h"
#include "asm/tlbflush.h"
#include "irq_user.h"
#include "kern_util.h"
#include "user_util.h"
#include "os.h"
#include "kern.h"
#include "sigcontext.h"
#include "mem_user.h"
#include "tlb.h"
#include "mode.h"
#include "mode_kern.h"
#include "init.h"
#include "tt.h"

void switch_to_tt(void *prev, void *next)
{
	struct task_struct *from, *to, *prev_sched;
	unsigned long flags;
	int err, vtalrm, alrm, prof, cpu;
	char c;

	from = prev;
	to = next;

	cpu = task_thread_info(from)->cpu;
	if(cpu == 0)
		forward_interrupts(to->thread.mode.tt.extern_pid);
#ifdef CONFIG_SMP
	forward_ipi(cpu_data[cpu].ipi_pipe[0], to->thread.mode.tt.extern_pid);
#endif
	local_irq_save(flags);

	vtalrm = change_sig(SIGVTALRM, 0);
	alrm = change_sig(SIGALRM, 0);
	prof = change_sig(SIGPROF, 0);

	forward_pending_sigio(to->thread.mode.tt.extern_pid);

	c = 0;

	/* Notice that here we "up" the semaphore on which "to" is waiting, and
	 * below (the read) we wait on this semaphore (which is implemented by
	 * switch_pipe) and go sleeping. Thus, after that, we have resumed in
	 * "to", and can't use any more the value of "from" (which is outdated),
	 * nor the value in "to" (since it was the task which stole us the CPU,
	 * which we don't care about). */

	err = os_write_file(to->thread.mode.tt.switch_pipe[1], &c, sizeof(c));
	if(err != sizeof(c))
		panic("write of switch_pipe failed, err = %d", -err);

	if(from->thread.mode.tt.switch_pipe[0] == -1)
		os_kill_process(os_getpid(), 0);

	err = os_read_file(from->thread.mode.tt.switch_pipe[0], &c, sizeof(c));
	if(err != sizeof(c))
		panic("read of switch_pipe failed, errno = %d", -err);

	/* If the process that we have just scheduled away from has exited,
	 * then it needs to be killed here.  The reason is that, even though
	 * it will kill itself when it next runs, that may be too late.  Its
	 * stack will be freed, possibly before then, and if that happens,
	 * we have a use-after-free situation.  So, it gets killed here
	 * in case it has not already killed itself.
	 */
	prev_sched = current->thread.prev_sched;
        if(prev_sched->thread.mode.tt.switch_pipe[0] == -1)
		os_kill_process(prev_sched->thread.mode.tt.extern_pid, 1);

	change_sig(SIGVTALRM, vtalrm);
	change_sig(SIGALRM, alrm);
	change_sig(SIGPROF, prof);

	arch_switch_to_tt(prev_sched, current);

	flush_tlb_all();
	local_irq_restore(flags);
}

void release_thread_tt(struct task_struct *task)
{
	int pid = task->thread.mode.tt.extern_pid;

	/*
         * We first have to kill the other process, before
         * closing its switch_pipe. Else it might wake up
         * and receive "EOF" before we could kill it.
         */
	if(os_getpid() != pid)
		os_kill_process(pid, 0);

        os_close_file(task->thread.mode.tt.switch_pipe[0]);
        os_close_file(task->thread.mode.tt.switch_pipe[1]);
	/* use switch_pipe as flag: thread is released */
        task->thread.mode.tt.switch_pipe[0] = -1;
}

void suspend_new_thread(int fd)
{
	int err;
	char c;

	os_stop_process(os_getpid());
	err = os_read_file(fd, &c, sizeof(c));
	if(err != sizeof(c))
		panic("read failed in suspend_new_thread, err = %d", -err);
}

void schedule_tail(task_t *prev);

static void new_thread_handler(int sig)
{
	unsigned long disable;
	int (*fn)(void *);
	void *arg;

	fn = current->thread.request.u.thread.proc;
	arg = current->thread.request.u.thread.arg;

	UPT_SC(&current->thread.regs.regs) = (void *) (&sig + 1);
	disable = (1 << (SIGVTALRM - 1)) | (1 << (SIGALRM - 1)) |
		(1 << (SIGIO - 1)) | (1 << (SIGPROF - 1));
	SC_SIGMASK(UPT_SC(&current->thread.regs.regs)) &= ~disable;

	suspend_new_thread(current->thread.mode.tt.switch_pipe[0]);

	force_flush_all();
	if(current->thread.prev_sched != NULL)
		schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	init_new_thread_signals(1);
	enable_timer();
	free_page(current->thread.temp_stack);
	set_cmdline("(kernel thread)");

	change_sig(SIGUSR1, 1);
	change_sig(SIGPROF, 1);
	local_irq_enable();
	if(!run_kernel_thread(fn, arg, &current->thread.exec_buf))
		do_exit(0);

	/* XXX No set_user_mode here because a newly execed process will
	 * immediately segfault on its non-existent IP, coming straight back
	 * to the signal handler, which will call set_user_mode on its way
	 * out.  This should probably change since it's confusing.
	 */
}

static int new_thread_proc(void *stack)
{
	/* local_irq_disable is needed to block out signals until this thread is
	 * properly scheduled.  Otherwise, the tracing thread will get mighty
	 * upset about any signals that arrive before that.
	 * This has the complication that it sets the saved signal mask in
	 * the sigcontext to block signals.  This gets restored when this
	 * thread (or a descendant, since they get a copy of this sigcontext)
	 * returns to userspace.
	 * So, this is compensated for elsewhere.
	 * XXX There is still a small window until local_irq_disable() actually
	 * finishes where signals are possible - shouldn't be a problem in
	 * practice since SIGIO hasn't been forwarded here yet, and the
	 * local_irq_disable should finish before a SIGVTALRM has time to be
	 * delivered.
	 */

	local_irq_disable();
	init_new_thread_stack(stack, new_thread_handler);
	os_usr1_process(os_getpid());
	change_sig(SIGUSR1, 1);
	return(0);
}

/* Signal masking - signals are blocked at the start of fork_tramp.  They
 * are re-enabled when finish_fork_handler is entered by fork_tramp hitting
 * itself with a SIGUSR1.  set_user_mode has to be run with SIGUSR1 off,
 * so it is blocked before it's called.  They are re-enabled on sigreturn
 * despite the fact that they were blocked when the SIGUSR1 was issued because
 * copy_thread copies the parent's sigcontext, including the signal mask
 * onto the signal frame.
 */

void finish_fork_handler(int sig)
{
 	UPT_SC(&current->thread.regs.regs) = (void *) (&sig + 1);
	suspend_new_thread(current->thread.mode.tt.switch_pipe[0]);

	force_flush_all();
	if(current->thread.prev_sched != NULL)
		schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	enable_timer();
	change_sig(SIGVTALRM, 1);
	local_irq_enable();
	if(current->mm != current->parent->mm)
		protect_memory(uml_reserved, high_physmem - uml_reserved, 1, 
			       1, 0, 1);
	task_protections((unsigned long) current_thread);

	free_page(current->thread.temp_stack);
	local_irq_disable();
	change_sig(SIGUSR1, 0);
	set_user_mode(current);
}

int fork_tramp(void *stack)
{
	local_irq_disable();
	arch_init_thread();
	init_new_thread_stack(stack, finish_fork_handler);

	os_usr1_process(os_getpid());
	change_sig(SIGUSR1, 1);
	return(0);
}

int copy_thread_tt(int nr, unsigned long clone_flags, unsigned long sp,
		   unsigned long stack_top, struct task_struct * p, 
		   struct pt_regs *regs)
{
	int (*tramp)(void *);
	int new_pid, err;
	unsigned long stack;
	
	if(current->thread.forking)
		tramp = fork_tramp;
	else {
		tramp = new_thread_proc;
		p->thread.request.u.thread = current->thread.request.u.thread;
	}

	err = os_pipe(p->thread.mode.tt.switch_pipe, 1, 1);
	if(err < 0){
		printk("copy_thread : pipe failed, err = %d\n", -err);
		return(err);
	}

	stack = alloc_stack(0, 0);
	if(stack == 0){
		printk(KERN_ERR "copy_thread : failed to allocate "
		       "temporary stack\n");
		return(-ENOMEM);
	}

	clone_flags &= CLONE_VM;
	p->thread.temp_stack = stack;
	new_pid = start_fork_tramp(task_stack_page(p), stack, clone_flags, tramp);
	if(new_pid < 0){
		printk(KERN_ERR "copy_thread : clone failed - errno = %d\n", 
		       -new_pid);
		return(new_pid);
	}

	if(current->thread.forking){
		sc_to_sc(UPT_SC(&p->thread.regs.regs), UPT_SC(&regs->regs));
		SC_SET_SYSCALL_RETURN(UPT_SC(&p->thread.regs.regs), 0);
		if(sp != 0)
			SC_SP(UPT_SC(&p->thread.regs.regs)) = sp;
	}
	p->thread.mode.tt.extern_pid = new_pid;

	current->thread.request.op = OP_FORK;
	current->thread.request.u.fork.pid = new_pid;
	os_usr1_process(os_getpid());

	/* Enable the signal and then disable it to ensure that it is handled
	 * here, and nowhere else.
	 */
	change_sig(SIGUSR1, 1);

	change_sig(SIGUSR1, 0);
	err = 0;
	return(err);
}

void reboot_tt(void)
{
	current->thread.request.op = OP_REBOOT;
	os_usr1_process(os_getpid());
	change_sig(SIGUSR1, 1);
}

void halt_tt(void)
{
	current->thread.request.op = OP_HALT;
	os_usr1_process(os_getpid());
	change_sig(SIGUSR1, 1);
}

void kill_off_processes_tt(void)
{
	struct task_struct *p;
	int me;

	me = os_getpid();
        for_each_process(p){
		if(p->thread.mode.tt.extern_pid != me) 
			os_kill_process(p->thread.mode.tt.extern_pid, 0);
	}
	if(init_task.thread.mode.tt.extern_pid != me) 
		os_kill_process(init_task.thread.mode.tt.extern_pid, 0);
}

void initial_thread_cb_tt(void (*proc)(void *), void *arg)
{
	if(os_getpid() == tracing_pid){
		(*proc)(arg);
	}
	else {
		current->thread.request.op = OP_CB;
		current->thread.request.u.cb.proc = proc;
		current->thread.request.u.cb.arg = arg;
		os_usr1_process(os_getpid());
		change_sig(SIGUSR1, 1);

		change_sig(SIGUSR1, 0);
	}
}

int do_proc_op(void *t, int proc_id)
{
	struct task_struct *task;
	struct thread_struct *thread;
	int op, pid;

	task = t;
	thread = &task->thread;
	op = thread->request.op;
	switch(op){
	case OP_NONE:
	case OP_TRACE_ON:
		break;
	case OP_EXEC:
		pid = thread->request.u.exec.pid;
		do_exec(thread->mode.tt.extern_pid, pid);
		thread->mode.tt.extern_pid = pid;
		cpu_tasks[task_thread_info(task)->cpu].pid = pid;
		break;
	case OP_FORK:
		attach_process(thread->request.u.fork.pid);
		break;
	case OP_CB:
		(*thread->request.u.cb.proc)(thread->request.u.cb.arg);
		break;
	case OP_REBOOT:
	case OP_HALT:
		break;
	default:
		tracer_panic("Bad op in do_proc_op");
		break;
	}
	thread->request.op = OP_NONE;
	return(op);
}

void init_idle_tt(void)
{
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
	if(debug) os_stop_process(pid);
	start_kernel();
	return(0);
}

void set_tracing(void *task, int tracing)
{
	((struct task_struct *) task)->thread.mode.tt.tracing = tracing;
}

int is_tracing(void *t)
{
	return (((struct task_struct *) t)->thread.mode.tt.tracing);
}

int set_user_mode(void *t)
{
	struct task_struct *task;

	task = t ? t : current;
	if(task->thread.mode.tt.tracing) 
		return(1);
	task->thread.request.op = OP_TRACE_ON;
	os_usr1_process(os_getpid());
	return(0);
}

void set_init_pid(int pid)
{
	int err;

	init_task.thread.mode.tt.extern_pid = pid;
	err = os_pipe(init_task.thread.mode.tt.switch_pipe, 1, 1);
	if(err)
		panic("Can't create switch pipe for init_task, errno = %d",
		      -err);
}

int start_uml_tt(void)
{
	void *sp;
	int pages;

	pages = (1 << CONFIG_KERNEL_STACK_ORDER);
	sp = task_stack_page(&init_task) +
		pages * PAGE_SIZE - sizeof(unsigned long);
	return(tracer(start_kernel_proc, sp));
}

int external_pid_tt(struct task_struct *task)
{
	return(task->thread.mode.tt.extern_pid);
}

int thread_pid_tt(struct task_struct *task)
{
	return(task->thread.mode.tt.extern_pid);
}

int is_valid_pid(int pid)
{
	struct task_struct *task;

        read_lock(&tasklist_lock);
        for_each_process(task){
                if(task->thread.mode.tt.extern_pid == pid){
			read_unlock(&tasklist_lock);
			return(1);
                }
        }
	read_unlock(&tasklist_lock);
	return(0);
}
