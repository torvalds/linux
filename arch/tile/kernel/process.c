/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/sched.h>
#include <linux/preempt.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/elfcore.h>
#include <linux/tick.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/compat.h>
#include <linux/hardirq.h>
#include <linux/syscalls.h>
#include <asm/system.h>
#include <asm/stack.h>
#include <asm/homecache.h>
#include <arch/chip.h>
#include <arch/abi.h>


/*
 * Use the (x86) "idle=poll" option to prefer low latency when leaving the
 * idle loop over low power while in the idle loop, e.g. if we have
 * one thread per core and we want to get threads out of futex waits fast.
 */
static int no_idle_nap;
static int __init idle_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "poll")) {
		printk("using polling idle threads.\n");
		no_idle_nap = 1;
	} else if (!strcmp(str, "halt"))
		no_idle_nap = 0;
	else
		return -1;

	return 0;
}
early_param("idle", idle_setup);

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	extern void _cpu_idle(void);
	int cpu = smp_processor_id();


	current_thread_info()->status |= TS_POLLING;

	if (no_idle_nap) {
		while (1) {
			while (!need_resched())
				cpu_relax();
			schedule();
		}
	}

	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_stop_sched_tick(1);
		while (!need_resched()) {
			if (cpu_is_offline(cpu))
				BUG();  /* no HOTPLUG_CPU */

			local_irq_disable();
			__get_cpu_var(irq_stat).idle_timestamp = jiffies;
			current_thread_info()->status &= ~TS_POLLING;
			/*
			 * TS_POLLING-cleared state must be visible before we
			 * test NEED_RESCHED:
			 */
			smp_mb();

			if (!need_resched())
				_cpu_idle();
			else
				local_irq_enable();
			current_thread_info()->status |= TS_POLLING;
		}
		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

struct thread_info *alloc_thread_info(struct task_struct *task)
{
	struct page *page;
	int flags = GFP_KERNEL;

#ifdef CONFIG_DEBUG_STACK_USAGE
	flags |= __GFP_ZERO;
#endif

	page = alloc_pages(flags, THREAD_SIZE_ORDER);
	if (!page)
		return 0;

	return (struct thread_info *)page_address(page);
}

/*
 * Free a thread_info node, and all of its derivative
 * data structures.
 */
void free_thread_info(struct thread_info *info)
{
	struct single_step_state *step_state = info->step_state;


	if (step_state) {

		/*
		 * FIXME: we don't munmap step_state->buffer
		 * because the mm_struct for this process (info->task->mm)
		 * has already been zeroed in exit_mm().  Keeping a
		 * reference to it here seems like a bad move, so this
		 * means we can't munmap() the buffer, and therefore if we
		 * ptrace multiple threads in a process, we will slowly
		 * leak user memory.  (Note that as soon as the last
		 * thread in a process dies, we will reclaim all user
		 * memory including single-step buffers in the usual way.)
		 * We should either assign a kernel VA to this buffer
		 * somehow, or we should associate the buffer(s) with the
		 * mm itself so we can clean them up that way.
		 */
		kfree(step_state);
	}

	free_page((unsigned long)info);
}

static void save_arch_state(struct thread_struct *t);

extern void ret_from_fork(void);

int copy_thread(unsigned long clone_flags, unsigned long sp,
		unsigned long stack_size,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	unsigned long ksp;

	/*
	 * When creating a new kernel thread we pass sp as zero.
	 * Assign it to a reasonable value now that we have the stack.
	 */
	if (sp == 0 && regs->ex1 == PL_ICS_EX1(KERNEL_PL, 0))
		sp = KSTK_TOP(p);

	/*
	 * Do not clone step state from the parent; each thread
	 * must make its own lazily.
	 */
	task_thread_info(p)->step_state = NULL;

	/*
	 * Start new thread in ret_from_fork so it schedules properly
	 * and then return from interrupt like the parent.
	 */
	p->thread.pc = (unsigned long) ret_from_fork;

	/* Save user stack top pointer so we can ID the stack vm area later. */
	p->thread.usp0 = sp;

	/* Record the pid of the process that created this one. */
	p->thread.creator_pid = current->pid;

	/*
	 * Copy the registers onto the kernel stack so the
	 * return-from-interrupt code will reload it into registers.
	 */
	childregs = task_pt_regs(p);
	*childregs = *regs;
	childregs->regs[0] = 0;         /* return value is zero */
	childregs->sp = sp;  /* override with new user stack pointer */

	/*
	 * Copy the callee-saved registers from the passed pt_regs struct
	 * into the context-switch callee-saved registers area.
	 * We have to restore the callee-saved registers since we may
	 * be cloning a userspace task with userspace register state,
	 * and we won't be unwinding the same kernel frames to restore them.
	 * Zero out the C ABI save area to mark the top of the stack.
	 */
	ksp = (unsigned long) childregs;
	ksp -= C_ABI_SAVE_AREA_SIZE;   /* interrupt-entry save area */
	((long *)ksp)[0] = ((long *)ksp)[1] = 0;
	ksp -= CALLEE_SAVED_REGS_COUNT * sizeof(unsigned long);
	memcpy((void *)ksp, &regs->regs[CALLEE_SAVED_FIRST_REG],
	       CALLEE_SAVED_REGS_COUNT * sizeof(unsigned long));
	ksp -= C_ABI_SAVE_AREA_SIZE;   /* __switch_to() save area */
	((long *)ksp)[0] = ((long *)ksp)[1] = 0;
	p->thread.ksp = ksp;

#if CHIP_HAS_TILE_DMA()
	/*
	 * No DMA in the new thread.  We model this on the fact that
	 * fork() clears the pending signals, alarms, and aio for the child.
	 */
	memset(&p->thread.tile_dma_state, 0, sizeof(struct tile_dma_state));
	memset(&p->thread.dma_async_tlb, 0, sizeof(struct async_tlb));
#endif

#if CHIP_HAS_SN_PROC()
	/* Likewise, the new thread is not running static processor code. */
	p->thread.sn_proc_running = 0;
	memset(&p->thread.sn_async_tlb, 0, sizeof(struct async_tlb));
#endif

#if CHIP_HAS_PROC_STATUS_SPR()
	/* New thread has its miscellaneous processor state bits clear. */
	p->thread.proc_status = 0;
#endif



	/*
	 * Start the new thread with the current architecture state
	 * (user interrupt masks, etc.).
	 */
	save_arch_state(&p->thread);

	return 0;
}

/*
 * Return "current" if it looks plausible, or else a pointer to a dummy.
 * This can be helpful if we are just trying to emit a clean panic.
 */
struct task_struct *validate_current(void)
{
	static struct task_struct corrupt = { .comm = "<corrupt>" };
	struct task_struct *tsk = current;
	if (unlikely((unsigned long)tsk < PAGE_OFFSET ||
		     (void *)tsk > high_memory ||
		     ((unsigned long)tsk & (__alignof__(*tsk) - 1)) != 0)) {
		printk("Corrupt 'current' %p (sp %#lx)\n", tsk, stack_pointer);
		tsk = &corrupt;
	}
	return tsk;
}

/* Take and return the pointer to the previous task, for schedule_tail(). */
struct task_struct *sim_notify_fork(struct task_struct *prev)
{
	struct task_struct *tsk = current;
	__insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_FORK_PARENT |
		     (tsk->thread.creator_pid << _SIM_CONTROL_OPERATOR_BITS));
	__insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_FORK |
		     (tsk->pid << _SIM_CONTROL_OPERATOR_BITS));
	return prev;
}

int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs *ptregs = task_pt_regs(tsk);
	elf_core_copy_regs(regs, ptregs);
	return 1;
}

#if CHIP_HAS_TILE_DMA()

/* Allow user processes to access the DMA SPRs */
void grant_dma_mpls(void)
{
	__insn_mtspr(SPR_MPL_DMA_CPL_SET_0, 1);
	__insn_mtspr(SPR_MPL_DMA_NOTIFY_SET_0, 1);
}

/* Forbid user processes from accessing the DMA SPRs */
void restrict_dma_mpls(void)
{
	__insn_mtspr(SPR_MPL_DMA_CPL_SET_1, 1);
	__insn_mtspr(SPR_MPL_DMA_NOTIFY_SET_1, 1);
}

/* Pause the DMA engine, then save off its state registers. */
static void save_tile_dma_state(struct tile_dma_state *dma)
{
	unsigned long state = __insn_mfspr(SPR_DMA_USER_STATUS);
	unsigned long post_suspend_state;

	/* If we're running, suspend the engine. */
	if ((state & DMA_STATUS_MASK) == SPR_DMA_STATUS__RUNNING_MASK)
		__insn_mtspr(SPR_DMA_CTR, SPR_DMA_CTR__SUSPEND_MASK);

	/*
	 * Wait for the engine to idle, then save regs.  Note that we
	 * want to record the "running" bit from before suspension,
	 * and the "done" bit from after, so that we can properly
	 * distinguish a case where the user suspended the engine from
	 * the case where the kernel suspended as part of the context
	 * swap.
	 */
	do {
		post_suspend_state = __insn_mfspr(SPR_DMA_USER_STATUS);
	} while (post_suspend_state & SPR_DMA_STATUS__BUSY_MASK);

	dma->src = __insn_mfspr(SPR_DMA_SRC_ADDR);
	dma->src_chunk = __insn_mfspr(SPR_DMA_SRC_CHUNK_ADDR);
	dma->dest = __insn_mfspr(SPR_DMA_DST_ADDR);
	dma->dest_chunk = __insn_mfspr(SPR_DMA_DST_CHUNK_ADDR);
	dma->strides = __insn_mfspr(SPR_DMA_STRIDE);
	dma->chunk_size = __insn_mfspr(SPR_DMA_CHUNK_SIZE);
	dma->byte = __insn_mfspr(SPR_DMA_BYTE);
	dma->status = (state & SPR_DMA_STATUS__RUNNING_MASK) |
		(post_suspend_state & SPR_DMA_STATUS__DONE_MASK);
}

/* Restart a DMA that was running before we were context-switched out. */
static void restore_tile_dma_state(struct thread_struct *t)
{
	const struct tile_dma_state *dma = &t->tile_dma_state;

	/*
	 * The only way to restore the done bit is to run a zero
	 * length transaction.
	 */
	if ((dma->status & SPR_DMA_STATUS__DONE_MASK) &&
	    !(__insn_mfspr(SPR_DMA_USER_STATUS) & SPR_DMA_STATUS__DONE_MASK)) {
		__insn_mtspr(SPR_DMA_BYTE, 0);
		__insn_mtspr(SPR_DMA_CTR, SPR_DMA_CTR__REQUEST_MASK);
		while (__insn_mfspr(SPR_DMA_USER_STATUS) &
		       SPR_DMA_STATUS__BUSY_MASK)
			;
	}

	__insn_mtspr(SPR_DMA_SRC_ADDR, dma->src);
	__insn_mtspr(SPR_DMA_SRC_CHUNK_ADDR, dma->src_chunk);
	__insn_mtspr(SPR_DMA_DST_ADDR, dma->dest);
	__insn_mtspr(SPR_DMA_DST_CHUNK_ADDR, dma->dest_chunk);
	__insn_mtspr(SPR_DMA_STRIDE, dma->strides);
	__insn_mtspr(SPR_DMA_CHUNK_SIZE, dma->chunk_size);
	__insn_mtspr(SPR_DMA_BYTE, dma->byte);

	/*
	 * Restart the engine if we were running and not done.
	 * Clear a pending async DMA fault that we were waiting on return
	 * to user space to execute, since we expect the DMA engine
	 * to regenerate those faults for us now.  Note that we don't
	 * try to clear the TIF_ASYNC_TLB flag, since it's relatively
	 * harmless if set, and it covers both DMA and the SN processor.
	 */
	if ((dma->status & DMA_STATUS_MASK) == SPR_DMA_STATUS__RUNNING_MASK) {
		t->dma_async_tlb.fault_num = 0;
		__insn_mtspr(SPR_DMA_CTR, SPR_DMA_CTR__REQUEST_MASK);
	}
}

#endif

static void save_arch_state(struct thread_struct *t)
{
#if CHIP_HAS_SPLIT_INTR_MASK()
	t->interrupt_mask = __insn_mfspr(SPR_INTERRUPT_MASK_0_0) |
		((u64)__insn_mfspr(SPR_INTERRUPT_MASK_0_1) << 32);
#else
	t->interrupt_mask = __insn_mfspr(SPR_INTERRUPT_MASK_0);
#endif
	t->ex_context[0] = __insn_mfspr(SPR_EX_CONTEXT_0_0);
	t->ex_context[1] = __insn_mfspr(SPR_EX_CONTEXT_0_1);
	t->system_save[0] = __insn_mfspr(SPR_SYSTEM_SAVE_0_0);
	t->system_save[1] = __insn_mfspr(SPR_SYSTEM_SAVE_0_1);
	t->system_save[2] = __insn_mfspr(SPR_SYSTEM_SAVE_0_2);
	t->system_save[3] = __insn_mfspr(SPR_SYSTEM_SAVE_0_3);
	t->intctrl_0 = __insn_mfspr(SPR_INTCTRL_0_STATUS);
#if CHIP_HAS_PROC_STATUS_SPR()
	t->proc_status = __insn_mfspr(SPR_PROC_STATUS);
#endif
}

static void restore_arch_state(const struct thread_struct *t)
{
#if CHIP_HAS_SPLIT_INTR_MASK()
	__insn_mtspr(SPR_INTERRUPT_MASK_0_0, (u32) t->interrupt_mask);
	__insn_mtspr(SPR_INTERRUPT_MASK_0_1, t->interrupt_mask >> 32);
#else
	__insn_mtspr(SPR_INTERRUPT_MASK_0, t->interrupt_mask);
#endif
	__insn_mtspr(SPR_EX_CONTEXT_0_0, t->ex_context[0]);
	__insn_mtspr(SPR_EX_CONTEXT_0_1, t->ex_context[1]);
	__insn_mtspr(SPR_SYSTEM_SAVE_0_0, t->system_save[0]);
	__insn_mtspr(SPR_SYSTEM_SAVE_0_1, t->system_save[1]);
	__insn_mtspr(SPR_SYSTEM_SAVE_0_2, t->system_save[2]);
	__insn_mtspr(SPR_SYSTEM_SAVE_0_3, t->system_save[3]);
	__insn_mtspr(SPR_INTCTRL_0_STATUS, t->intctrl_0);
#if CHIP_HAS_PROC_STATUS_SPR()
	__insn_mtspr(SPR_PROC_STATUS, t->proc_status);
#endif
#if CHIP_HAS_TILE_RTF_HWM()
	/*
	 * Clear this whenever we switch back to a process in case
	 * the previous process was monkeying with it.  Even if enabled
	 * in CBOX_MSR1 via TILE_RTF_HWM_MIN, it's still just a
	 * performance hint, so isn't worth a full save/restore.
	 */
	__insn_mtspr(SPR_TILE_RTF_HWM, 0);
#endif
}


void _prepare_arch_switch(struct task_struct *next)
{
#if CHIP_HAS_SN_PROC()
	int snctl;
#endif
#if CHIP_HAS_TILE_DMA()
	struct tile_dma_state *dma = &current->thread.tile_dma_state;
	if (dma->enabled)
		save_tile_dma_state(dma);
#endif
#if CHIP_HAS_SN_PROC()
	/*
	 * Suspend the static network processor if it was running.
	 * We do not suspend the fabric itself, just like we don't
	 * try to suspend the UDN.
	 */
	snctl = __insn_mfspr(SPR_SNCTL);
	current->thread.sn_proc_running =
		(snctl & SPR_SNCTL__FRZPROC_MASK) == 0;
	if (current->thread.sn_proc_running)
		__insn_mtspr(SPR_SNCTL, snctl | SPR_SNCTL__FRZPROC_MASK);
#endif
}


extern struct task_struct *__switch_to(struct task_struct *prev,
				       struct task_struct *next,
				       unsigned long new_system_save_1_0);

struct task_struct *__sched _switch_to(struct task_struct *prev,
				       struct task_struct *next)
{
	/* DMA state is already saved; save off other arch state. */
	save_arch_state(&prev->thread);

#if CHIP_HAS_TILE_DMA()
	/*
	 * Restore DMA in new task if desired.
	 * Note that it is only safe to restart here since interrupts
	 * are disabled, so we can't take any DMATLB miss or access
	 * interrupts before we have finished switching stacks.
	 */
	if (next->thread.tile_dma_state.enabled) {
		restore_tile_dma_state(&next->thread);
		grant_dma_mpls();
	} else {
		restrict_dma_mpls();
	}
#endif

	/* Restore other arch state. */
	restore_arch_state(&next->thread);

#if CHIP_HAS_SN_PROC()
	/*
	 * Restart static network processor in the new process
	 * if it was running before.
	 */
	if (next->thread.sn_proc_running) {
		int snctl = __insn_mfspr(SPR_SNCTL);
		__insn_mtspr(SPR_SNCTL, snctl & ~SPR_SNCTL__FRZPROC_MASK);
	}
#endif


	/*
	 * Switch kernel SP, PC, and callee-saved registers.
	 * In the context of the new task, return the old task pointer
	 * (i.e. the task that actually called __switch_to).
	 * Pass the value to use for SYSTEM_SAVE_1_0 when we reset our sp.
	 */
	return __switch_to(prev, next, next_current_ksp0(next));
}

int _sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->sp, regs, 0, NULL, NULL);
}

int _sys_clone(unsigned long clone_flags, unsigned long newsp,
	       int __user *parent_tidptr, int __user *child_tidptr,
	       struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->sp;
	return do_fork(clone_flags, newsp, regs, 0,
		       parent_tidptr, child_tidptr);
}

int _sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->sp,
		       regs, 0, NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 */
int _sys_execve(char __user *path, char __user *__user *argv,
		char __user *__user *envp, struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname(path);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

#ifdef CONFIG_COMPAT
int _compat_sys_execve(char __user *path, compat_uptr_t __user *argv,
		       compat_uptr_t __user *envp, struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname(path);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = compat_do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}
#endif

unsigned long get_wchan(struct task_struct *p)
{
	struct KBacktraceIterator kbt;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	for (KBacktraceIterator_init(&kbt, p, NULL);
	     !KBacktraceIterator_end(&kbt);
	     KBacktraceIterator_next(&kbt)) {
		if (!in_sched_functions(kbt.it.pc))
			return kbt.it.pc;
	}

	return 0;
}

/*
 * We pass in lr as zero (cleared in kernel_thread) and the caller
 * part of the backtrace ABI on the stack also zeroed (in copy_thread)
 * so that backtraces will stop with this function.
 * Note that we don't use r0, since copy_thread() clears it.
 */
static void start_kernel_thread(int dummy, int (*fn)(int), int arg)
{
	do_exit(fn(arg));
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.ex1 = PL_ICS_EX1(KERNEL_PL, 0);  /* run at kernel PL, no ICS */
	regs.pc = (long) start_kernel_thread;
	regs.flags = PT_FLAGS_CALLER_SAVES;   /* need to restore r1 and r2 */
	regs.regs[1] = (long) fn;             /* function pointer */
	regs.regs[2] = (long) arg;            /* parameter register */

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs,
		       0, NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);

/* Flush thread state. */
void flush_thread(void)
{
	/* Nothing */
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* Nothing */
}

#ifdef __tilegx__
# define LINECOUNT 3
# define EXTRA_NL "\n"
#else
# define LINECOUNT 4
# define EXTRA_NL ""
#endif

void show_regs(struct pt_regs *regs)
{
	struct task_struct *tsk = validate_current();
	int i, linebreak;
	printk("\n");
	printk(" Pid: %d, comm: %20s, CPU: %d\n",
	       tsk->pid, tsk->comm, smp_processor_id());
	for (i = linebreak = 0; i < 53; ++i) {
		printk(" r%-2d: "REGFMT, i, regs->regs[i]);
		if (++linebreak == LINECOUNT) {
			linebreak = 0;
			printk("\n");
		}
	}
	printk(" tp : "REGFMT EXTRA_NL " sp : "REGFMT" lr : "REGFMT"\n",
	       regs->tp, regs->sp, regs->lr);
	printk(" pc : "REGFMT" ex1: %ld     faultnum: %ld\n",
	       regs->pc, regs->ex1, regs->faultnum);

	dump_stack_regs(regs);
}
