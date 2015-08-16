/*
 * x86 single-step support code, common to 32-bit and 64-bit.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>

unsigned long convert_ip_to_linear(struct task_struct *child, struct pt_regs *regs)
{
	unsigned long addr, seg;

	addr = regs->ip;
	seg = regs->cs & 0xffff;
	if (v8086_mode(regs)) {
		addr = (addr & 0xffff) + (seg << 4);
		return addr;
	}

	/*
	 * We'll assume that the code segments in the GDT
	 * are all zero-based. That is largely true: the
	 * TLS segments are used for data, and the PNPBIOS
	 * and APM bios ones we just ignore here.
	 */
	if ((seg & SEGMENT_TI_MASK) == SEGMENT_LDT) {
		struct desc_struct *desc;
		unsigned long base;

		seg >>= 3;

		mutex_lock(&child->mm->context.lock);
		if (unlikely(!child->mm->context.ldt ||
			     seg >= child->mm->context.ldt->size))
			addr = -1L; /* bogus selector, access would fault */
		else {
			desc = &child->mm->context.ldt->entries[seg];
			base = get_desc_base(desc);

			/* 16-bit code segment? */
			if (!desc->d)
				addr &= 0xffff;
			addr += base;
		}
		mutex_unlock(&child->mm->context.lock);
	}

	return addr;
}

static int is_setting_trap_flag(struct task_struct *child, struct pt_regs *regs)
{
	int i, copied;
	unsigned char opcode[15];
	unsigned long addr = convert_ip_to_linear(child, regs);

	copied = access_process_vm(child, addr, opcode, sizeof(opcode), 0);
	for (i = 0; i < copied; i++) {
		switch (opcode[i]) {
		/* popf and iret */
		case 0x9d: case 0xcf:
			return 1;

			/* CHECKME: 64 65 */

		/* opcode and address size prefixes */
		case 0x66: case 0x67:
			continue;
		/* irrelevant prefixes (segment overrides and repeats) */
		case 0x26: case 0x2e:
		case 0x36: case 0x3e:
		case 0x64: case 0x65:
		case 0xf0: case 0xf2: case 0xf3:
			continue;

#ifdef CONFIG_X86_64
		case 0x40 ... 0x4f:
			if (!user_64bit_mode(regs))
				/* 32-bit mode: register increment */
				return 0;
			/* 64-bit mode: REX prefix */
			continue;
#endif

			/* CHECKME: f2, f3 */

		/*
		 * pushf: NOTE! We should probably not let
		 * the user see the TF bit being set. But
		 * it's more pain than it's worth to avoid
		 * it, and a debugger could emulate this
		 * all in user space if it _really_ cares.
		 */
		case 0x9c:
		default:
			return 0;
		}
	}
	return 0;
}

/*
 * Enable single-stepping.  Return nonzero if user mode is not using TF itself.
 */
static int enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);
	unsigned long oflags;

	/*
	 * If we stepped into a sysenter/syscall insn, it trapped in
	 * kernel mode; do_debug() cleared TF and set TIF_SINGLESTEP.
	 * If user-mode had set TF itself, then it's still clear from
	 * do_debug() and we need to set it again to restore the user
	 * state so we don't wrongly set TIF_FORCED_TF below.
	 * If enable_single_step() was used last and that is what
	 * set TIF_SINGLESTEP, then both TF and TIF_FORCED_TF are
	 * already set and our bookkeeping is fine.
	 */
	if (unlikely(test_tsk_thread_flag(child, TIF_SINGLESTEP)))
		regs->flags |= X86_EFLAGS_TF;

	/*
	 * Always set TIF_SINGLESTEP - this guarantees that
	 * we single-step system calls etc..  This will also
	 * cause us to set TF when returning to user mode.
	 */
	set_tsk_thread_flag(child, TIF_SINGLESTEP);

	oflags = regs->flags;

	/* Set TF on the kernel stack.. */
	regs->flags |= X86_EFLAGS_TF;

	/*
	 * ..but if TF is changed by the instruction we will trace,
	 * don't mark it as being "us" that set it, so that we
	 * won't clear it by hand later.
	 *
	 * Note that if we don't actually execute the popf because
	 * of a signal arriving right now or suchlike, we will lose
	 * track of the fact that it really was "us" that set it.
	 */
	if (is_setting_trap_flag(child, regs)) {
		clear_tsk_thread_flag(child, TIF_FORCED_TF);
		return 0;
	}

	/*
	 * If TF was already set, check whether it was us who set it.
	 * If not, we should never attempt a block step.
	 */
	if (oflags & X86_EFLAGS_TF)
		return test_tsk_thread_flag(child, TIF_FORCED_TF);

	set_tsk_thread_flag(child, TIF_FORCED_TF);

	return 1;
}

void set_task_blockstep(struct task_struct *task, bool on)
{
	unsigned long debugctl;

	/*
	 * Ensure irq/preemption can't change debugctl in between.
	 * Note also that both TIF_BLOCKSTEP and debugctl should
	 * be changed atomically wrt preemption.
	 *
	 * NOTE: this means that set/clear TIF_BLOCKSTEP is only safe if
	 * task is current or it can't be running, otherwise we can race
	 * with __switch_to_xtra(). We rely on ptrace_freeze_traced() but
	 * PTRACE_KILL is not safe.
	 */
	local_irq_disable();
	debugctl = get_debugctlmsr();
	if (on) {
		debugctl |= DEBUGCTLMSR_BTF;
		set_tsk_thread_flag(task, TIF_BLOCKSTEP);
	} else {
		debugctl &= ~DEBUGCTLMSR_BTF;
		clear_tsk_thread_flag(task, TIF_BLOCKSTEP);
	}
	if (task == current)
		update_debugctlmsr(debugctl);
	local_irq_enable();
}

/*
 * Enable single or block step.
 */
static void enable_step(struct task_struct *child, bool block)
{
	/*
	 * Make sure block stepping (BTF) is not enabled unless it should be.
	 * Note that we don't try to worry about any is_setting_trap_flag()
	 * instructions after the first when using block stepping.
	 * So no one should try to use debugger block stepping in a program
	 * that uses user-mode single stepping itself.
	 */
	if (enable_single_step(child) && block)
		set_task_blockstep(child, true);
	else if (test_tsk_thread_flag(child, TIF_BLOCKSTEP))
		set_task_blockstep(child, false);
}

void user_enable_single_step(struct task_struct *child)
{
	enable_step(child, 0);
}

void user_enable_block_step(struct task_struct *child)
{
	enable_step(child, 1);
}

void user_disable_single_step(struct task_struct *child)
{
	/*
	 * Make sure block stepping (BTF) is disabled.
	 */
	if (test_tsk_thread_flag(child, TIF_BLOCKSTEP))
		set_task_blockstep(child, false);

	/* Always clear TIF_SINGLESTEP... */
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);

	/* But touch TF only if it was set by us.. */
	if (test_and_clear_tsk_thread_flag(child, TIF_FORCED_TF))
		task_pt_regs(child)->flags &= ~X86_EFLAGS_TF;
}
