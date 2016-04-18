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
#include <linux/kernel.h>
#include <linux/tracehook.h>
#include <linux/signal.h>
#include <linux/delay.h>
#include <linux/context_tracking.h>
#include <asm/stack.h>
#include <asm/switch_to.h>
#include <asm/homecache.h>
#include <asm/syscalls.h>
#include <asm/traps.h>
#include <asm/setup.h>
#include <asm/uaccess.h>
#ifdef CONFIG_HARDWALL
#include <asm/hardwall.h>
#endif
#include <arch/chip.h>
#include <arch/abi.h>
#include <arch/sim_def.h>

/*
 * Use the (x86) "idle=poll" option to prefer low latency when leaving the
 * idle loop over low power while in the idle loop, e.g. if we have
 * one thread per core and we want to get threads out of futex waits fast.
 */
static int __init idle_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "poll")) {
		pr_info("using polling idle threads\n");
		cpu_idle_poll_ctrl(true);
		return 0;
	} else if (!strcmp(str, "halt")) {
		return 0;
	}
	return -1;
}
early_param("idle", idle_setup);

void arch_cpu_idle(void)
{
	__this_cpu_write(irq_stat.idle_timestamp, jiffies);
	_cpu_idle();
}

/*
 * Release a thread_info structure
 */
void arch_release_thread_info(struct thread_info *info)
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
}

static void save_arch_state(struct thread_struct *t);

int copy_thread(unsigned long clone_flags, unsigned long sp,
		unsigned long arg, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);
	unsigned long ksp;
	unsigned long *callee_regs;

	/*
	 * Set up the stack and stack pointer appropriately for the
	 * new child to find itself woken up in __switch_to().
	 * The callee-saved registers must be on the stack to be read;
	 * the new task will then jump to assembly support to handle
	 * calling schedule_tail(), etc., and (for userspace tasks)
	 * returning to the context set up in the pt_regs.
	 */
	ksp = (unsigned long) childregs;
	ksp -= C_ABI_SAVE_AREA_SIZE;   /* interrupt-entry save area */
	((long *)ksp)[0] = ((long *)ksp)[1] = 0;
	ksp -= CALLEE_SAVED_REGS_COUNT * sizeof(unsigned long);
	callee_regs = (unsigned long *)ksp;
	ksp -= C_ABI_SAVE_AREA_SIZE;   /* __switch_to() save area */
	((long *)ksp)[0] = ((long *)ksp)[1] = 0;
	p->thread.ksp = ksp;

	/* Record the pid of the task that created this one. */
	p->thread.creator_pid = current->pid;

	if (unlikely(p->flags & PF_KTHREAD)) {
		/* kernel thread */
		memset(childregs, 0, sizeof(struct pt_regs));
		memset(&callee_regs[2], 0,
		       (CALLEE_SAVED_REGS_COUNT - 2) * sizeof(unsigned long));
		callee_regs[0] = sp;   /* r30 = function */
		callee_regs[1] = arg;  /* r31 = arg */
		p->thread.pc = (unsigned long) ret_from_kernel_thread;
		return 0;
	}

	/*
	 * Start new thread in ret_from_fork so it schedules properly
	 * and then return from interrupt like the parent.
	 */
	p->thread.pc = (unsigned long) ret_from_fork;

	/*
	 * Do not clone step state from the parent; each thread
	 * must make its own lazily.
	 */
	task_thread_info(p)->step_state = NULL;

#ifdef __tilegx__
	/*
	 * Do not clone unalign jit fixup from the parent; each thread
	 * must allocate its own on demand.
	 */
	task_thread_info(p)->unalign_jit_base = NULL;
#endif

	/*
	 * Copy the registers onto the kernel stack so the
	 * return-from-interrupt code will reload it into registers.
	 */
	*childregs = *current_pt_regs();
	childregs->regs[0] = 0;         /* return value is zero */
	if (sp)
		childregs->sp = sp;  /* override with new user stack pointer */
	memcpy(callee_regs, &childregs->regs[CALLEE_SAVED_FIRST_REG],
	       CALLEE_SAVED_REGS_COUNT * sizeof(unsigned long));

	/* Save user stack top pointer so we can ID the stack vm area later. */
	p->thread.usp0 = childregs->sp;

	/*
	 * If CLONE_SETTLS is set, set "tp" in the new task to "r4",
	 * which is passed in as arg #5 to sys_clone().
	 */
	if (clone_flags & CLONE_SETTLS)
		childregs->tp = childregs->regs[4];


#if CHIP_HAS_TILE_DMA()
	/*
	 * No DMA in the new thread.  We model this on the fact that
	 * fork() clears the pending signals, alarms, and aio for the child.
	 */
	memset(&p->thread.tile_dma_state, 0, sizeof(struct tile_dma_state));
	memset(&p->thread.dma_async_tlb, 0, sizeof(struct async_tlb));
#endif

	/* New thread has its miscellaneous processor state bits clear. */
	p->thread.proc_status = 0;

#ifdef CONFIG_HARDWALL
	/* New thread does not own any networks. */
	memset(&p->thread.hardwall[0], 0,
	       sizeof(struct hardwall_task) * HARDWALL_TYPES);
#endif


	/*
	 * Start the new thread with the current architecture state
	 * (user interrupt masks, etc.).
	 */
	save_arch_state(&p->thread);

	return 0;
}

int set_unalign_ctl(struct task_struct *tsk, unsigned int val)
{
	task_thread_info(tsk)->align_ctl = val;
	return 0;
}

int get_unalign_ctl(struct task_struct *tsk, unsigned long adr)
{
	return put_user(task_thread_info(tsk)->align_ctl,
			(unsigned int __user *)adr);
}

static struct task_struct corrupt_current = { .comm = "<corrupt>" };

/*
 * Return "current" if it looks plausible, or else a pointer to a dummy.
 * This can be helpful if we are just trying to emit a clean panic.
 */
struct task_struct *validate_current(void)
{
	struct task_struct *tsk = current;
	if (unlikely((unsigned long)tsk < PAGE_OFFSET ||
		     (high_memory && (void *)tsk > high_memory) ||
		     ((unsigned long)tsk & (__alignof__(*tsk) - 1)) != 0)) {
		pr_err("Corrupt 'current' %p (sp %#lx)\n", tsk, stack_pointer);
		tsk = &corrupt_current;
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
#if CONFIG_KERNEL_PL == 2
	__insn_mtspr(SPR_MPL_DMA_CPL_SET_1, 1);
	__insn_mtspr(SPR_MPL_DMA_NOTIFY_SET_1, 1);
#else
	__insn_mtspr(SPR_MPL_DMA_CPL_SET_0, 1);
	__insn_mtspr(SPR_MPL_DMA_NOTIFY_SET_0, 1);
#endif
}

/* Forbid user processes from accessing the DMA SPRs */
void restrict_dma_mpls(void)
{
#if CONFIG_KERNEL_PL == 2
	__insn_mtspr(SPR_MPL_DMA_CPL_SET_2, 1);
	__insn_mtspr(SPR_MPL_DMA_NOTIFY_SET_2, 1);
#else
	__insn_mtspr(SPR_MPL_DMA_CPL_SET_1, 1);
	__insn_mtspr(SPR_MPL_DMA_NOTIFY_SET_1, 1);
#endif
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
	t->proc_status = __insn_mfspr(SPR_PROC_STATUS);
#if !CHIP_HAS_FIXED_INTVEC_BASE()
	t->interrupt_vector_base = __insn_mfspr(SPR_INTERRUPT_VECTOR_BASE_0);
#endif
	t->tile_rtf_hwm = __insn_mfspr(SPR_TILE_RTF_HWM);
#if CHIP_HAS_DSTREAM_PF()
	t->dstream_pf = __insn_mfspr(SPR_DSTREAM_PF);
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
	__insn_mtspr(SPR_PROC_STATUS, t->proc_status);
#if !CHIP_HAS_FIXED_INTVEC_BASE()
	__insn_mtspr(SPR_INTERRUPT_VECTOR_BASE_0, t->interrupt_vector_base);
#endif
	__insn_mtspr(SPR_TILE_RTF_HWM, t->tile_rtf_hwm);
#if CHIP_HAS_DSTREAM_PF()
	__insn_mtspr(SPR_DSTREAM_PF, t->dstream_pf);
#endif
}


void _prepare_arch_switch(struct task_struct *next)
{
#if CHIP_HAS_TILE_DMA()
	struct tile_dma_state *dma = &current->thread.tile_dma_state;
	if (dma->enabled)
		save_tile_dma_state(dma);
#endif
}


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

#ifdef CONFIG_HARDWALL
	/* Enable or disable access to the network registers appropriately. */
	hardwall_switch_tasks(prev, next);
#endif

	/* Notify the simulator of task exit. */
	if (unlikely(prev->state == TASK_DEAD))
		__insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_OS_EXIT |
			     (prev->pid << _SIM_CONTROL_OPERATOR_BITS));

	/*
	 * Switch kernel SP, PC, and callee-saved registers.
	 * In the context of the new task, return the old task pointer
	 * (i.e. the task that actually called __switch_to).
	 * Pass the value to use for SYSTEM_SAVE_K_0 when we reset our sp.
	 */
	return __switch_to(prev, next, next_current_ksp0(next));
}

/*
 * This routine is called on return from interrupt if any of the
 * TIF_ALLWORK_MASK flags are set in thread_info->flags.  It is
 * entered with interrupts disabled so we don't miss an event that
 * modified the thread_info flags.  We loop until all the tested flags
 * are clear.  Note that the function is called on certain conditions
 * that are not listed in the loop condition here (e.g. SINGLESTEP)
 * which guarantees we will do those things once, and redo them if any
 * of the other work items is re-done, but won't continue looping if
 * all the other work is done.
 */
void prepare_exit_to_usermode(struct pt_regs *regs, u32 thread_info_flags)
{
	if (WARN_ON(!user_mode(regs)))
		return;

	do {
		local_irq_enable();

		if (thread_info_flags & _TIF_NEED_RESCHED)
			schedule();

#if CHIP_HAS_TILE_DMA()
		if (thread_info_flags & _TIF_ASYNC_TLB)
			do_async_page_fault(regs);
#endif

		if (thread_info_flags & _TIF_SIGPENDING)
			do_signal(regs);

		if (thread_info_flags & _TIF_NOTIFY_RESUME) {
			clear_thread_flag(TIF_NOTIFY_RESUME);
			tracehook_notify_resume(regs);
		}

		local_irq_disable();
		thread_info_flags = READ_ONCE(current_thread_info()->flags);

	} while (thread_info_flags & _TIF_WORK_MASK);

	if (thread_info_flags & _TIF_SINGLESTEP) {
		single_step_once(regs);
#ifndef __tilegx__
		/*
		 * FIXME: on tilepro, since we enable interrupts in
		 * this routine, it's possible that we miss a signal
		 * or other asynchronous event.
		 */
		local_irq_disable();
#endif
	}

	user_enter();
}

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
#ifdef CONFIG_HARDWALL
	/*
	 * Remove the task from the list of tasks that are associated
	 * with any live hardwalls.  (If the task that is exiting held
	 * the last reference to a hardwall fd, it would already have
	 * been released and deactivated at this point.)
	 */
	hardwall_deactivate_all(current);
#endif
}

void tile_show_regs(struct pt_regs *regs)
{
	int i;
#ifdef __tilegx__
	for (i = 0; i < 17; i++)
		pr_err(" r%-2d: "REGFMT" r%-2d: "REGFMT" r%-2d: "REGFMT"\n",
		       i, regs->regs[i], i+18, regs->regs[i+18],
		       i+36, regs->regs[i+36]);
	pr_err(" r17: "REGFMT" r35: "REGFMT" tp : "REGFMT"\n",
	       regs->regs[17], regs->regs[35], regs->tp);
	pr_err(" sp : "REGFMT" lr : "REGFMT"\n", regs->sp, regs->lr);
#else
	for (i = 0; i < 13; i++)
		pr_err(" r%-2d: "REGFMT" r%-2d: "REGFMT
		       " r%-2d: "REGFMT" r%-2d: "REGFMT"\n",
		       i, regs->regs[i], i+14, regs->regs[i+14],
		       i+27, regs->regs[i+27], i+40, regs->regs[i+40]);
	pr_err(" r13: "REGFMT" tp : "REGFMT" sp : "REGFMT" lr : "REGFMT"\n",
	       regs->regs[13], regs->tp, regs->sp, regs->lr);
#endif
	pr_err(" pc : "REGFMT" ex1: %ld     faultnum: %ld flags:%s%s%s%s\n",
	       regs->pc, regs->ex1, regs->faultnum,
	       is_compat_task() ? " compat" : "",
	       (regs->flags & PT_FLAGS_DISABLE_IRQ) ? " noirq" : "",
	       !(regs->flags & PT_FLAGS_CALLER_SAVES) ? " nocallersave" : "",
	       (regs->flags & PT_FLAGS_RESTORE_REGS) ? " restoreregs" : "");
}

void show_regs(struct pt_regs *regs)
{
	struct KBacktraceIterator kbt;

	show_regs_print_info(KERN_DEFAULT);
	tile_show_regs(regs);

	KBacktraceIterator_init(&kbt, NULL, regs);
	tile_show_stack(&kbt);
}

/* To ensure stack dump on tiles occurs one by one. */
static DEFINE_SPINLOCK(backtrace_lock);
/* To ensure no backtrace occurs before all of the stack dump are done. */
static atomic_t backtrace_cpus;
/* The cpu mask to avoid reentrance. */
static struct cpumask backtrace_mask;

void do_nmi_dump_stack(struct pt_regs *regs)
{
	int is_idle = is_idle_task(current) && !in_interrupt();
	int cpu;

	nmi_enter();
	cpu = smp_processor_id();
	if (WARN_ON_ONCE(!cpumask_test_and_clear_cpu(cpu, &backtrace_mask)))
		goto done;

	spin_lock(&backtrace_lock);
	if (is_idle)
		pr_info("CPU: %d idle\n", cpu);
	else
		show_regs(regs);
	spin_unlock(&backtrace_lock);
	atomic_dec(&backtrace_cpus);
done:
	nmi_exit();
}

#ifdef __tilegx__
void arch_trigger_all_cpu_backtrace(bool self)
{
	struct cpumask mask;
	HV_Coord tile;
	unsigned int timeout;
	int cpu;
	int ongoing;
	HV_NMI_Info info[NR_CPUS];

	ongoing = atomic_cmpxchg(&backtrace_cpus, 0, num_online_cpus() - 1);
	if (ongoing != 0) {
		pr_err("Trying to do all-cpu backtrace.\n");
		pr_err("But another all-cpu backtrace is ongoing (%d cpus left)\n",
		       ongoing);
		if (self) {
			pr_err("Reporting the stack on this cpu only.\n");
			dump_stack();
		}
		return;
	}

	cpumask_copy(&mask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &mask);
	cpumask_copy(&backtrace_mask, &mask);

	/* Backtrace for myself first. */
	if (self)
		dump_stack();

	/* Tentatively dump stack on remote tiles via NMI. */
	timeout = 100;
	while (!cpumask_empty(&mask) && timeout) {
		for_each_cpu(cpu, &mask) {
			tile.x = cpu_x(cpu);
			tile.y = cpu_y(cpu);
			info[cpu] = hv_send_nmi(tile, TILE_NMI_DUMP_STACK, 0);
			if (info[cpu].result == HV_NMI_RESULT_OK)
				cpumask_clear_cpu(cpu, &mask);
		}

		mdelay(10);
		timeout--;
	}

	/* Warn about cpus stuck in ICS and decrement their counts here. */
	if (!cpumask_empty(&mask)) {
		for_each_cpu(cpu, &mask) {
			switch (info[cpu].result) {
			case HV_NMI_RESULT_FAIL_ICS:
				pr_warn("Skipping stack dump of cpu %d in ICS at pc %#llx\n",
					cpu, info[cpu].pc);
				break;
			case HV_NMI_RESULT_FAIL_HV:
				pr_warn("Skipping stack dump of cpu %d in hypervisor\n",
					cpu);
				break;
			case HV_ENOSYS:
				pr_warn("Hypervisor too old to allow remote stack dumps.\n");
				goto skip_for_each;
			default:  /* should not happen */
				pr_warn("Skipping stack dump of cpu %d [%d,%#llx]\n",
					cpu, info[cpu].result, info[cpu].pc);
				break;
			}
		}
skip_for_each:
		atomic_sub(cpumask_weight(&mask), &backtrace_cpus);
	}
}
#endif /* __tilegx_ */
