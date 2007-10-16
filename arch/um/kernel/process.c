/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/err.h"
#include "linux/hardirq.h"
#include "linux/mm.h"
#include "linux/personality.h"
#include "linux/proc_fs.h"
#include "linux/ptrace.h"
#include "linux/random.h"
#include "linux/sched.h"
#include "linux/tick.h"
#include "linux/threads.h"
#include "asm/pgtable.h"
#include "asm/uaccess.h"
#include "as-layout.h"
#include "kern_util.h"
#include "os.h"
#include "skas.h"
#include "tlb.h"

/*
 * This is a per-cpu array.  A processor only modifies its entry and it only
 * cares about its entry, so it's OK if another processor is modifying its
 * entry.
 */
struct cpu_task cpu_tasks[NR_CPUS] = { [0 ... NR_CPUS - 1] = { -1, NULL } };

static inline int external_pid(struct task_struct *task)
{
	/* FIXME: Need to look up userspace_pid by cpu */
	return userspace_pid[0];
}

int pid_to_processor_id(int pid)
{
	int i;

	for(i = 0; i < ncpus; i++) {
		if (cpu_tasks[i].pid == pid)
			return i;
	}
	return -1;
}

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
	if (page == 0)
		return 0;

	return page;
}

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	int pid;

	current->thread.request.u.thread.proc = fn;
	current->thread.request.u.thread.arg = arg;
	pid = do_fork(CLONE_VM | CLONE_UNTRACED | flags, 0,
		      &current->thread.regs, 0, NULL, NULL);
	return pid;
}

static inline void set_current(struct task_struct *task)
{
	cpu_tasks[task_thread_info(task)->cpu] = ((struct cpu_task)
		{ external_pid(task), task });
}

extern void arch_switch_to(struct task_struct *from, struct task_struct *to);

void *_switch_to(void *prev, void *next, void *last)
{
	struct task_struct *from = prev;
	struct task_struct *to= next;

	to->thread.prev_sched = from;
	set_current(to);

	do {
		current->thread.saved_task = NULL;

		switch_threads(&from->thread.switch_buf,
			       &to->thread.switch_buf);

		arch_switch_to(current->thread.prev_sched, current);

		if (current->thread.saved_task)
			show_regs(&(current->thread.regs));
		next= current->thread.saved_task;
		prev= current;
	} while(current->thread.saved_task);

	return current->thread.prev_sched;

}

void interrupt_end(void)
{
	if (need_resched())
		schedule();
	if (test_tsk_thread_flag(current, TIF_SIGPENDING))
		do_signal();
}

void exit_thread(void)
{
}

void *get_current(void)
{
	return current;
}

extern void schedule_tail(struct task_struct *prev);

/*
 * This is called magically, by its address being stuffed in a jmp_buf
 * and being longjmp-d to.
 */
void new_thread_handler(void)
{
	int (*fn)(void *), n;
	void *arg;

	if (current->thread.prev_sched != NULL)
		schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	fn = current->thread.request.u.thread.proc;
	arg = current->thread.request.u.thread.arg;

	/*
	 * The return value is 1 if the kernel thread execs a process,
	 * 0 if it just exits
	 */
	n = run_kernel_thread(fn, arg, &current->thread.exec_buf);
	if (n == 1) {
		/* Handle any immediate reschedules or signals */
		interrupt_end();
		userspace(&current->thread.regs.regs);
	}
	else do_exit(0);
}

/* Called magically, see new_thread_handler above */
void fork_handler(void)
{
	force_flush_all();
	if (current->thread.prev_sched == NULL)
		panic("blech");

	schedule_tail(current->thread.prev_sched);

	/*
	 * XXX: if interrupt_end() calls schedule, this call to
	 * arch_switch_to isn't needed. We could want to apply this to
	 * improve performance. -bb
	 */
	arch_switch_to(current->thread.prev_sched, current);

	current->thread.prev_sched = NULL;

	/* Handle any immediate reschedules or signals */
	interrupt_end();

	userspace(&current->thread.regs.regs);
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		unsigned long stack_top, struct task_struct * p,
		struct pt_regs *regs)
{
	void (*handler)(void);
	int ret = 0;

	p->thread = (struct thread_struct) INIT_THREAD;

	if (current->thread.forking) {
	  	memcpy(&p->thread.regs.regs, &regs->regs,
		       sizeof(p->thread.regs.regs));
		REGS_SET_SYSCALL_RETURN(p->thread.regs.regs.gp, 0);
		if (sp != 0)
			REGS_SP(p->thread.regs.regs.gp) = sp;

		handler = fork_handler;

		arch_copy_thread(&current->thread.arch, &p->thread.arch);
	}
	else {
		init_thread_registers(&p->thread.regs.regs);
		p->thread.request.u.thread = current->thread.request.u.thread;
		handler = new_thread_handler;
	}

	new_thread(task_stack_page(p), &p->thread.switch_buf, handler);

	if (current->thread.forking) {
		clear_flushed_tls(p);

		/*
		 * Set a new TLS for the child thread?
		 */
		if (clone_flags & CLONE_SETTLS)
			ret = arch_copy_tls(p);
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

void default_idle(void)
{
	unsigned long long nsecs;

	while(1) {
		/* endless idle loop with no priority at all */

		/*
		 * although we are an idle CPU, we do not want to
		 * get into the scheduler unnecessarily.
		 */
		if (need_resched())
			schedule();

		tick_nohz_stop_sched_tick();
		nsecs = disable_timer();
		idle_sleep(nsecs);
		tick_nohz_restart_sched_tick();
	}
}

void cpu_idle(void)
{
	cpu_tasks[current_thread->cpu].pid = os_getpid();
	default_idle();
}

void *um_virt_to_phys(struct task_struct *task, unsigned long addr,
		      pte_t *pte_out)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t ptent;

	if (task->mm == NULL)
		return ERR_PTR(-EINVAL);
	pgd = pgd_offset(task->mm, addr);
	if (!pgd_present(*pgd))
		return ERR_PTR(-EINVAL);

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
		return ERR_PTR(-EINVAL);

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return ERR_PTR(-EINVAL);

	pte = pte_offset_kernel(pmd, addr);
	ptent = *pte;
	if (!pte_present(ptent))
		return ERR_PTR(-EINVAL);

	if (pte_out != NULL)
		*pte_out = ptent;
	return (void *) (pte_val(ptent) & PAGE_MASK) + (addr & ~PAGE_MASK);
}

char *current_cmd(void)
{
#if defined(CONFIG_SMP) || defined(CONFIG_HIGHMEM)
	return "(Unknown)";
#else
	void *addr = um_virt_to_phys(current, current->mm->arg_start, NULL);
	return IS_ERR(addr) ? "(Unknown)": __va((unsigned long) addr);
#endif
}

void dump_thread(struct pt_regs *regs, struct user *u)
{
}

int __cant_sleep(void) {
	return in_atomic() || irqs_disabled() || in_interrupt();
	/* Is in_interrupt() really needed? */
}

int user_context(unsigned long sp)
{
	unsigned long stack;

	stack = sp & (PAGE_MASK << CONFIG_KERNEL_STACK_ORDER);
	return stack != (unsigned long) current_thread;
}

extern exitcall_t __uml_exitcall_begin, __uml_exitcall_end;

void do_uml_exitcalls(void)
{
	exitcall_t *call;

	call = &__uml_exitcall_end;
	while (--call >= &__uml_exitcall_begin)
		(*call)();
}

char *uml_strdup(char *string)
{
	return kstrdup(string, GFP_KERNEL);
}

int copy_to_user_proc(void __user *to, void *from, int size)
{
	return copy_to_user(to, from, size);
}

int copy_from_user_proc(void *to, void __user *from, int size)
{
	return copy_from_user(to, from, size);
}

int clear_user_proc(void __user *buf, int size)
{
	return clear_user(buf, size);
}

int strlen_user_proc(char __user *str)
{
	return strlen_user(str);
}

int smp_sigio_handler(void)
{
#ifdef CONFIG_SMP
	int cpu = current_thread->cpu;
	IPI_handler(cpu);
	if (cpu != 0)
		return 1;
#endif
	return 0;
}

int cpu(void)
{
	return current_thread->cpu;
}

static atomic_t using_sysemu = ATOMIC_INIT(0);
int sysemu_supported;

void set_using_sysemu(int value)
{
	if (value > sysemu_supported)
		return;
	atomic_set(&using_sysemu, value);
}

int get_using_sysemu(void)
{
	return atomic_read(&using_sysemu);
}

static int proc_read_sysemu(char *buf, char **start, off_t offset, int size,int *eof, void *data)
{
	if (snprintf(buf, size, "%d\n", get_using_sysemu()) < size)
		/* No overflow */
		*eof = 1;

	return strlen(buf);
}

static int proc_write_sysemu(struct file *file,const char __user *buf, unsigned long count,void *data)
{
	char tmp[2];

	if (copy_from_user(tmp, buf, 1))
		return -EFAULT;

	if (tmp[0] >= '0' && tmp[0] <= '2')
		set_using_sysemu(tmp[0] - '0');
	/* We use the first char, but pretend to write everything */
	return count;
}

int __init make_proc_sysemu(void)
{
	struct proc_dir_entry *ent;
	if (!sysemu_supported)
		return 0;

	ent = create_proc_entry("sysemu", 0600, &proc_root);

	if (ent == NULL)
	{
		printk(KERN_WARNING "Failed to register /proc/sysemu\n");
		return 0;
	}

	ent->read_proc  = proc_read_sysemu;
	ent->write_proc = proc_write_sysemu;

	return 0;
}

late_initcall(make_proc_sysemu);

int singlestepping(void * t)
{
	struct task_struct *task = t ? t : current;

	if ( ! (task->ptrace & PT_DTRACE) )
		return 0;

	if (task->thread.singlestep_syscall)
		return 1;

	return 2;
}

/*
 * Only x86 and x86_64 have an arch_align_stack().
 * All other arches have "#define arch_align_stack(x) (x)"
 * in their asm/system.h
 * As this is included in UML from asm-um/system-generic.h,
 * we can use it to behave as the subarch does.
 */
#ifndef arch_align_stack
unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() % 8192;
	return sp & ~0xf;
}
#endif
