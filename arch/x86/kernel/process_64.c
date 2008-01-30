/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 *
 *  X86-64 port
 *	Andi Kleen.
 *
 *	CPU hotplug support - ashok.raj@intel.com
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <stdarg.h>

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/utsname.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/random.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/tick.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/mmu_context.h>
#include <asm/pda.h>
#include <asm/prctl.h>
#include <asm/desc.h>
#include <asm/proto.h>
#include <asm/ia32.h>
#include <asm/idle.h>

asmlinkage extern void ret_from_fork(void);

unsigned long kernel_thread_flags = CLONE_VM | CLONE_UNTRACED;

unsigned long boot_option_idle_override = 0;
EXPORT_SYMBOL(boot_option_idle_override);

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void);
EXPORT_SYMBOL(pm_idle);
static DEFINE_PER_CPU(unsigned int, cpu_idle_state);

static ATOMIC_NOTIFIER_HEAD(idle_notifier);

void idle_notifier_register(struct notifier_block *n)
{
	atomic_notifier_chain_register(&idle_notifier, n);
}

void enter_idle(void)
{
	write_pda(isidle, 1);
	atomic_notifier_call_chain(&idle_notifier, IDLE_START, NULL);
}

static void __exit_idle(void)
{
	if (test_and_clear_bit_pda(0, isidle) == 0)
		return;
	atomic_notifier_call_chain(&idle_notifier, IDLE_END, NULL);
}

/* Called from interrupts to signify idle end */
void exit_idle(void)
{
	/* idle loop has pid 0 */
	if (current->pid)
		return;
	__exit_idle();
}

/*
 * We use this if we don't have any better
 * idle routine..
 */
void default_idle(void)
{
	current_thread_info()->status &= ~TS_POLLING;
	/*
	 * TS_POLLING-cleared state must be visible before we
	 * test NEED_RESCHED:
	 */
	smp_mb();
	local_irq_disable();
	if (!need_resched()) {
		ktime_t t0, t1;
		u64 t0n, t1n;

		t0 = ktime_get();
		t0n = ktime_to_ns(t0);
		safe_halt();	/* enables interrupts racelessly */
		local_irq_disable();
		t1 = ktime_get();
		t1n = ktime_to_ns(t1);
		sched_clock_idle_wakeup_event(t1n - t0n);
	}
	local_irq_enable();
	current_thread_info()->status |= TS_POLLING;
}

/*
 * On SMP it's slightly faster (but much more power-consuming!)
 * to poll the ->need_resched flag instead of waiting for the
 * cross-CPU IPI to arrive. Use this option with caution.
 */
static void poll_idle(void)
{
	local_irq_enable();
	cpu_relax();
}

#ifdef CONFIG_HOTPLUG_CPU
DECLARE_PER_CPU(int, cpu_state);

#include <asm/nmi.h>
/* We halt the CPU with physical CPU hotplug */
static inline void play_dead(void)
{
	idle_task_exit();
	wbinvd();
	mb();
	/* Ack it */
	__get_cpu_var(cpu_state) = CPU_DEAD;

	local_irq_disable();
	while (1)
		halt();
}
#else
static inline void play_dead(void)
{
	BUG();
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	current_thread_info()->status |= TS_POLLING;
	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_stop_sched_tick();
		while (!need_resched()) {
			void (*idle)(void);

			if (__get_cpu_var(cpu_idle_state))
				__get_cpu_var(cpu_idle_state) = 0;

			rmb();
			idle = pm_idle;
			if (!idle)
				idle = default_idle;
			if (cpu_is_offline(smp_processor_id()))
				play_dead();
			/*
			 * Idle routines should keep interrupts disabled
			 * from here on, until they go to idle.
			 * Otherwise, idle callbacks can misfire.
			 */
			local_irq_disable();
			enter_idle();
			idle();
			/* In many cases the interrupt that ended idle
			   has already called exit_idle. But some idle
			   loops can be woken up without interrupt. */
			__exit_idle();
		}

		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

static void do_nothing(void *unused)
{
}

void cpu_idle_wait(void)
{
	unsigned int cpu, this_cpu = get_cpu();
	cpumask_t map, tmp = current->cpus_allowed;

	set_cpus_allowed(current, cpumask_of_cpu(this_cpu));
	put_cpu();

	cpus_clear(map);
	for_each_online_cpu(cpu) {
		per_cpu(cpu_idle_state, cpu) = 1;
		cpu_set(cpu, map);
	}

	__get_cpu_var(cpu_idle_state) = 0;

	wmb();
	do {
		ssleep(1);
		for_each_online_cpu(cpu) {
			if (cpu_isset(cpu, map) && !per_cpu(cpu_idle_state, cpu))
				cpu_clear(cpu, map);
		}
		cpus_and(map, map, cpu_online_map);
		/*
		 * We waited 1 sec, if a CPU still did not call idle
		 * it may be because it is in idle and not waking up
		 * because it has nothing to do.
		 * Give all the remaining CPUS a kick.
		 */
		smp_call_function_mask(map, do_nothing, 0, 0);
	} while (!cpus_empty(map));

	set_cpus_allowed(current, tmp);
}
EXPORT_SYMBOL_GPL(cpu_idle_wait);

/*
 * This uses new MONITOR/MWAIT instructions on P4 processors with PNI,
 * which can obviate IPI to trigger checking of need_resched.
 * We execute MONITOR against need_resched and enter optimized wait state
 * through MWAIT. Whenever someone changes need_resched, we would be woken
 * up from MWAIT (without an IPI).
 *
 * New with Core Duo processors, MWAIT can take some hints based on CPU
 * capability.
 */
void mwait_idle_with_hints(unsigned long ax, unsigned long cx)
{
	if (!need_resched()) {
		__monitor((void *)&current_thread_info()->flags, 0, 0);
		smp_mb();
		if (!need_resched())
			__mwait(ax, cx);
	}
}

/* Default MONITOR/MWAIT with no hints, used for default C1 state */
static void mwait_idle(void)
{
	if (!need_resched()) {
		__monitor((void *)&current_thread_info()->flags, 0, 0);
		smp_mb();
		if (!need_resched())
			__sti_mwait(0, 0);
		else
			local_irq_enable();
	} else {
		local_irq_enable();
	}
}


static int __cpuinit mwait_usable(const struct cpuinfo_x86 *c)
{
	if (force_mwait)
		return 1;
	/* Any C1 states supported? */
	return c->cpuid_level >= 5 && ((cpuid_edx(5) >> 4) & 0xf) > 0;
}

void __cpuinit select_idle_routine(const struct cpuinfo_x86 *c)
{
	static int selected;

	if (selected)
		return;
#ifdef CONFIG_X86_SMP
	if (pm_idle == poll_idle && smp_num_siblings > 1) {
		printk(KERN_WARNING "WARNING: polling idle and HT enabled,"
			" performance may degrade.\n");
	}
#endif
	if (cpu_has(c, X86_FEATURE_MWAIT) && mwait_usable(c)) {
		/*
		 * Skip, if setup has overridden idle.
		 * One CPU supports mwait => All CPUs supports mwait
		 */
		if (!pm_idle) {
			printk(KERN_INFO "using mwait in idle threads.\n");
			pm_idle = mwait_idle;
		}
	}
	selected = 1;
}

static int __init idle_setup(char *str)
{
	if (!strcmp(str, "poll")) {
		printk("using polling idle threads.\n");
		pm_idle = poll_idle;
	} else if (!strcmp(str, "mwait"))
		force_mwait = 1;
	else
		return -1;

	boot_option_idle_override = 1;
	return 0;
}
early_param("idle", idle_setup);

/* Prints also some state that isn't saved in the pt_regs */
void __show_regs(struct pt_regs * regs)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
	unsigned long d0, d1, d2, d3, d6, d7;
	unsigned int fsindex, gsindex;
	unsigned int ds, cs, es;

	printk("\n");
	print_modules();
	printk("Pid: %d, comm: %.20s %s %s %.*s\n",
		current->pid, current->comm, print_tainted(),
		init_utsname()->release,
		(int)strcspn(init_utsname()->version, " "),
		init_utsname()->version);
	printk("RIP: %04lx:[<%016lx>] ", regs->cs & 0xffff, regs->ip);
	printk_address(regs->ip, 1);
	printk("RSP: %04lx:%016lx  EFLAGS: %08lx\n", regs->ss, regs->sp,
		regs->flags);
	printk("RAX: %016lx RBX: %016lx RCX: %016lx\n",
	       regs->ax, regs->bx, regs->cx);
	printk("RDX: %016lx RSI: %016lx RDI: %016lx\n",
	       regs->dx, regs->si, regs->di);
	printk("RBP: %016lx R08: %016lx R09: %016lx\n",
	       regs->bp, regs->r8, regs->r9);
	printk("R10: %016lx R11: %016lx R12: %016lx\n",
	       regs->r10, regs->r11, regs->r12); 
	printk("R13: %016lx R14: %016lx R15: %016lx\n",
	       regs->r13, regs->r14, regs->r15); 

	asm("movl %%ds,%0" : "=r" (ds)); 
	asm("movl %%cs,%0" : "=r" (cs)); 
	asm("movl %%es,%0" : "=r" (es)); 
	asm("movl %%fs,%0" : "=r" (fsindex));
	asm("movl %%gs,%0" : "=r" (gsindex));

	rdmsrl(MSR_FS_BASE, fs);
	rdmsrl(MSR_GS_BASE, gs); 
	rdmsrl(MSR_KERNEL_GS_BASE, shadowgs); 

	cr0 = read_cr0();
	cr2 = read_cr2();
	cr3 = read_cr3();
	cr4 = read_cr4();

	printk("FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n", 
	       fs,fsindex,gs,gsindex,shadowgs); 
	printk("CS:  %04x DS: %04x ES: %04x CR0: %016lx\n", cs, ds, es, cr0); 
	printk("CR2: %016lx CR3: %016lx CR4: %016lx\n", cr2, cr3, cr4);

	get_debugreg(d0, 0);
	get_debugreg(d1, 1);
	get_debugreg(d2, 2);
	printk("DR0: %016lx DR1: %016lx DR2: %016lx\n", d0, d1, d2);
	get_debugreg(d3, 3);
	get_debugreg(d6, 6);
	get_debugreg(d7, 7);
	printk("DR3: %016lx DR6: %016lx DR7: %016lx\n", d3, d6, d7);
}

void show_regs(struct pt_regs *regs)
{
	printk("CPU %d:", smp_processor_id());
	__show_regs(regs);
	show_trace(NULL, regs, (void *)(regs + 1), regs->bp);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	struct task_struct *me = current;
	struct thread_struct *t = &me->thread;

	if (me->thread.io_bitmap_ptr) {
		struct tss_struct *tss = &per_cpu(init_tss, get_cpu());

		kfree(t->io_bitmap_ptr);
		t->io_bitmap_ptr = NULL;
		clear_thread_flag(TIF_IO_BITMAP);
		/*
		 * Careful, clear this in the TSS too:
		 */
		memset(tss->io_bitmap, 0xff, t->io_bitmap_max);
		t->io_bitmap_max = 0;
		put_cpu();
	}
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	if (test_tsk_thread_flag(tsk, TIF_ABI_PENDING)) {
		clear_tsk_thread_flag(tsk, TIF_ABI_PENDING);
		if (test_tsk_thread_flag(tsk, TIF_IA32)) {
			clear_tsk_thread_flag(tsk, TIF_IA32);
		} else {
			set_tsk_thread_flag(tsk, TIF_IA32);
			current_thread_info()->status |= TS_COMPAT;
		}
	}
	clear_tsk_thread_flag(tsk, TIF_DEBUG);

	tsk->thread.debugreg0 = 0;
	tsk->thread.debugreg1 = 0;
	tsk->thread.debugreg2 = 0;
	tsk->thread.debugreg3 = 0;
	tsk->thread.debugreg6 = 0;
	tsk->thread.debugreg7 = 0;
	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));
	/*
	 * Forget coprocessor state..
	 */
	clear_fpu(tsk);
	clear_used_math();
}

void release_thread(struct task_struct *dead_task)
{
	if (dead_task->mm) {
		if (dead_task->mm->context.size) {
			printk("WARNING: dead process %8s still has LDT? <%p/%d>\n",
					dead_task->comm,
					dead_task->mm->context.ldt,
					dead_task->mm->context.size);
			BUG();
		}
	}
}

static inline void set_32bit_tls(struct task_struct *t, int tls, u32 addr)
{
	struct user_desc ud = {
		.base_addr = addr,
		.limit = 0xfffff,
		.seg_32bit = 1,
		.limit_in_pages = 1,
		.useable = 1,
	};
	struct desc_struct *desc = t->thread.tls_array;
	desc += tls;
	fill_ldt(desc, &ud);
}

static inline u32 read_32bit_tls(struct task_struct *t, int tls)
{
	return get_desc_base(&t->thread.tls_array[tls]);
}

/*
 * This gets called before we allocate a new thread and copy
 * the current task into it.
 */
void prepare_to_copy(struct task_struct *tsk)
{
	unlazy_fpu(tsk);
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		unsigned long unused,
	struct task_struct * p, struct pt_regs * regs)
{
	int err;
	struct pt_regs * childregs;
	struct task_struct *me = current;

	childregs = ((struct pt_regs *)
			(THREAD_SIZE + task_stack_page(p))) - 1;
	*childregs = *regs;

	childregs->ax = 0;
	childregs->sp = sp;
	if (sp == ~0UL)
		childregs->sp = (unsigned long)childregs;

	p->thread.sp = (unsigned long) childregs;
	p->thread.sp0 = (unsigned long) (childregs+1);
	p->thread.usersp = me->thread.usersp;

	set_tsk_thread_flag(p, TIF_FORK);

	p->thread.fs = me->thread.fs;
	p->thread.gs = me->thread.gs;

	asm("mov %%gs,%0" : "=m" (p->thread.gsindex));
	asm("mov %%fs,%0" : "=m" (p->thread.fsindex));
	asm("mov %%es,%0" : "=m" (p->thread.es));
	asm("mov %%ds,%0" : "=m" (p->thread.ds));

	if (unlikely(test_tsk_thread_flag(me, TIF_IO_BITMAP))) {
		p->thread.io_bitmap_ptr = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
		if (!p->thread.io_bitmap_ptr) {
			p->thread.io_bitmap_max = 0;
			return -ENOMEM;
		}
		memcpy(p->thread.io_bitmap_ptr, me->thread.io_bitmap_ptr,
				IO_BITMAP_BYTES);
		set_tsk_thread_flag(p, TIF_IO_BITMAP);
	}

	/*
	 * Set a new TLS for the child thread?
	 */
	if (clone_flags & CLONE_SETTLS) {
#ifdef CONFIG_IA32_EMULATION
		if (test_thread_flag(TIF_IA32))
			err = do_set_thread_area(p, -1,
				(struct user_desc __user *)childregs->si, 0);
		else 			
#endif	 
			err = do_arch_prctl(p, ARCH_SET_FS, childregs->r8); 
		if (err) 
			goto out;
	}
	err = 0;
out:
	if (err && p->thread.io_bitmap_ptr) {
		kfree(p->thread.io_bitmap_ptr);
		p->thread.io_bitmap_max = 0;
	}
	return err;
}

/*
 * This special macro can be used to load a debugging register
 */
#define loaddebug(thread, r) set_debugreg(thread->debugreg ## r, r)

static inline void __switch_to_xtra(struct task_struct *prev_p,
				    struct task_struct *next_p,
				    struct tss_struct *tss)
{
	struct thread_struct *prev, *next;
	unsigned long debugctl;

	prev = &prev_p->thread,
	next = &next_p->thread;

	debugctl = prev->debugctlmsr;
	if (next->ds_area_msr != prev->ds_area_msr) {
		/* we clear debugctl to make sure DS
		 * is not in use when we change it */
		debugctl = 0;
		wrmsrl(MSR_IA32_DEBUGCTLMSR, 0);
		wrmsrl(MSR_IA32_DS_AREA, next->ds_area_msr);
	}

	if (next->debugctlmsr != debugctl)
		wrmsrl(MSR_IA32_DEBUGCTLMSR, next->debugctlmsr);

	if (test_tsk_thread_flag(next_p, TIF_DEBUG)) {
		loaddebug(next, 0);
		loaddebug(next, 1);
		loaddebug(next, 2);
		loaddebug(next, 3);
		/* no 4 and 5 */
		loaddebug(next, 6);
		loaddebug(next, 7);
	}

	if (test_tsk_thread_flag(next_p, TIF_IO_BITMAP)) {
		/*
		 * Copy the relevant range of the IO bitmap.
		 * Normally this is 128 bytes or less:
		 */
		memcpy(tss->io_bitmap, next->io_bitmap_ptr,
		       max(prev->io_bitmap_max, next->io_bitmap_max));
	} else if (test_tsk_thread_flag(prev_p, TIF_IO_BITMAP)) {
		/*
		 * Clear any possible leftover bits:
		 */
		memset(tss->io_bitmap, 0xff, prev->io_bitmap_max);
	}

	if (test_tsk_thread_flag(prev_p, TIF_BTS_TRACE_TS))
		ptrace_bts_take_timestamp(prev_p, BTS_TASK_DEPARTS);

	if (test_tsk_thread_flag(next_p, TIF_BTS_TRACE_TS))
		ptrace_bts_take_timestamp(next_p, BTS_TASK_ARRIVES);
}

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 * This could still be optimized:
 * - fold all the options into a flag word and test it with a single test.
 * - could test fs/gs bitsliced
 *
 * Kprobes not supported here. Set the probe on schedule instead.
 */
struct task_struct *
__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
				 *next = &next_p->thread;
	int cpu = smp_processor_id();
	struct tss_struct *tss = &per_cpu(init_tss, cpu);

	/* we're going to use this soon, after a few expensive things */
	if (next_p->fpu_counter>5)
		prefetch(&next->i387.fxsave);

	/*
	 * Reload esp0, LDT and the page table pointer:
	 */
	load_sp0(tss, next);

	/* 
	 * Switch DS and ES.
	 * This won't pick up thread selector changes, but I guess that is ok.
	 */
	asm volatile("mov %%es,%0" : "=m" (prev->es));
	if (unlikely(next->es | prev->es))
		loadsegment(es, next->es); 
	
	asm volatile ("mov %%ds,%0" : "=m" (prev->ds));
	if (unlikely(next->ds | prev->ds))
		loadsegment(ds, next->ds);

	load_TLS(next, cpu);

	/* 
	 * Switch FS and GS.
	 */
	{ 
		unsigned fsindex;
		asm volatile("movl %%fs,%0" : "=r" (fsindex)); 
		/* segment register != 0 always requires a reload. 
		   also reload when it has changed. 
		   when prev process used 64bit base always reload
		   to avoid an information leak. */
		if (unlikely(fsindex | next->fsindex | prev->fs)) {
			loadsegment(fs, next->fsindex);
			/* check if the user used a selector != 0
	                 * if yes clear 64bit base, since overloaded base
                         * is always mapped to the Null selector
                         */
			if (fsindex)
			prev->fs = 0;				
		}
		/* when next process has a 64bit base use it */
		if (next->fs) 
			wrmsrl(MSR_FS_BASE, next->fs); 
		prev->fsindex = fsindex;
	}
	{ 
		unsigned gsindex;
		asm volatile("movl %%gs,%0" : "=r" (gsindex)); 
		if (unlikely(gsindex | next->gsindex | prev->gs)) {
			load_gs_index(next->gsindex);
			if (gsindex)
			prev->gs = 0;				
		}
		if (next->gs)
			wrmsrl(MSR_KERNEL_GS_BASE, next->gs); 
		prev->gsindex = gsindex;
	}

	/* Must be after DS reload */
	unlazy_fpu(prev_p);

	/* 
	 * Switch the PDA and FPU contexts.
	 */
	prev->usersp = read_pda(oldrsp);
	write_pda(oldrsp, next->usersp);
	write_pda(pcurrent, next_p); 

	write_pda(kernelstack,
	(unsigned long)task_stack_page(next_p) + THREAD_SIZE - PDA_STACKOFFSET);
#ifdef CONFIG_CC_STACKPROTECTOR
	write_pda(stack_canary, next_p->stack_canary);
	/*
	 * Build time only check to make sure the stack_canary is at
	 * offset 40 in the pda; this is a gcc ABI requirement
	 */
	BUILD_BUG_ON(offsetof(struct x8664_pda, stack_canary) != 40);
#endif

	/*
	 * Now maybe reload the debug registers and handle I/O bitmaps
	 */
	if (unlikely(task_thread_info(next_p)->flags & _TIF_WORK_CTXSW_NEXT ||
		     task_thread_info(prev_p)->flags & _TIF_WORK_CTXSW_PREV))
		__switch_to_xtra(prev_p, next_p, tss);

	/* If the task has used fpu the last 5 timeslices, just do a full
	 * restore of the math state immediately to avoid the trap; the
	 * chances of needing FPU soon are obviously high now
	 */
	if (next_p->fpu_counter>5)
		math_state_restore();
	return prev_p;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage
long sys_execve(char __user *name, char __user * __user *argv,
		char __user * __user *envp, struct pt_regs regs)
{
	long error;
	char * filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename)) 
		return error;
	error = do_execve(filename, argv, envp, &regs); 
	putname(filename);
	return error;
}

void set_personality_64bit(void)
{
	/* inherit personality from parent */

	/* Make sure to be in 64bit mode */
	clear_thread_flag(TIF_IA32);

	/* TBD: overwrites user setup. Should have two bits.
	   But 64bit processes have always behaved this way,
	   so it's not too bad. The main problem is just that
	   32bit childs are affected again. */
	current->personality &= ~READ_IMPLIES_EXEC;
}

asmlinkage long sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->sp, regs, 0, NULL, NULL);
}

asmlinkage long
sys_clone(unsigned long clone_flags, unsigned long newsp,
	  void __user *parent_tid, void __user *child_tid, struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->sp;
	return do_fork(clone_flags, newsp, regs, 0, parent_tid, child_tid);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage long sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->sp, regs, 0,
		    NULL, NULL);
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long stack;
	u64 fp,ip;
	int count = 0;

	if (!p || p == current || p->state==TASK_RUNNING)
		return 0; 
	stack = (unsigned long)task_stack_page(p);
	if (p->thread.sp < stack || p->thread.sp > stack+THREAD_SIZE)
		return 0;
	fp = *(u64 *)(p->thread.sp);
	do { 
		if (fp < (unsigned long)stack ||
		    fp > (unsigned long)stack+THREAD_SIZE)
			return 0; 
		ip = *(u64 *)(fp+8);
		if (!in_sched_functions(ip))
			return ip;
		fp = *(u64 *)fp; 
	} while (count++ < 16); 
	return 0;
}

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr)
{ 
	int ret = 0; 
	int doit = task == current;
	int cpu;

	switch (code) { 
	case ARCH_SET_GS:
		if (addr >= TASK_SIZE_OF(task))
			return -EPERM; 
		cpu = get_cpu();
		/* handle small bases via the GDT because that's faster to 
		   switch. */
		if (addr <= 0xffffffff) {  
			set_32bit_tls(task, GS_TLS, addr); 
			if (doit) { 
				load_TLS(&task->thread, cpu);
				load_gs_index(GS_TLS_SEL); 
			}
			task->thread.gsindex = GS_TLS_SEL; 
			task->thread.gs = 0;
		} else { 
			task->thread.gsindex = 0;
			task->thread.gs = addr;
			if (doit) {
				load_gs_index(0);
				ret = checking_wrmsrl(MSR_KERNEL_GS_BASE, addr);
			} 
		}
		put_cpu();
		break;
	case ARCH_SET_FS:
		/* Not strictly needed for fs, but do it for symmetry
		   with gs */
		if (addr >= TASK_SIZE_OF(task))
			return -EPERM;
		cpu = get_cpu();
		/* handle small bases via the GDT because that's faster to
		   switch. */
		if (addr <= 0xffffffff) {
			set_32bit_tls(task, FS_TLS, addr);
			if (doit) {
				load_TLS(&task->thread, cpu);
				asm volatile("movl %0,%%fs" :: "r"(FS_TLS_SEL));
			}
			task->thread.fsindex = FS_TLS_SEL;
			task->thread.fs = 0;
		} else {
			task->thread.fsindex = 0;
			task->thread.fs = addr;
			if (doit) {
				/* set the selector to 0 to not confuse
				   __switch_to */
				asm volatile("movl %0,%%fs" :: "r" (0));
				ret = checking_wrmsrl(MSR_FS_BASE, addr);
			}
		}
		put_cpu();
		break;
	case ARCH_GET_FS: {
		unsigned long base;
		if (task->thread.fsindex == FS_TLS_SEL)
			base = read_32bit_tls(task, FS_TLS);
		else if (doit)
			rdmsrl(MSR_FS_BASE, base);
		else
			base = task->thread.fs;
		ret = put_user(base, (unsigned long __user *)addr);
		break;
	}
	case ARCH_GET_GS: {
		unsigned long base;
		unsigned gsindex;
		if (task->thread.gsindex == GS_TLS_SEL)
			base = read_32bit_tls(task, GS_TLS);
		else if (doit) {
			asm("movl %%gs,%0" : "=r" (gsindex));
			if (gsindex)
				rdmsrl(MSR_KERNEL_GS_BASE, base);
			else
				base = task->thread.gs;
		}
		else
			base = task->thread.gs;
		ret = put_user(base, (unsigned long __user *)addr);
		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

long sys_arch_prctl(int code, unsigned long addr)
{
	return do_arch_prctl(current, code, addr);
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
