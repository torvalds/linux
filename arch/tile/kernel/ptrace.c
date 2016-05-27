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
 *
 * Copied from i386: Ross Biro 1/23/92
 */

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/kprobes.h>
#include <linux/compat.h>
#include <linux/uaccess.h>
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/tracehook.h>
#include <linux/context_tracking.h>
#include <asm/traps.h>
#include <arch/chip.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

void user_enable_single_step(struct task_struct *child)
{
	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);

	/*
	 * These two are currently unused, but will be set by arch_ptrace()
	 * and used in the syscall assembly when we do support them.
	 */
	clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
}

/*
 * Get registers from task and ready the result for userspace.
 * Note that we localize the API issues to getregs() and putregs() at
 * some cost in performance, e.g. we need a full pt_regs copy for
 * PEEKUSR, and two copies for POKEUSR.  But in general we expect
 * GETREGS/PUTREGS to be the API of choice anyway.
 */
static char *getregs(struct task_struct *child, struct pt_regs *uregs)
{
	*uregs = *task_pt_regs(child);

	/* Set up flags ABI bits. */
	uregs->flags = 0;
#ifdef CONFIG_COMPAT
	if (task_thread_info(child)->status & TS_COMPAT)
		uregs->flags |= PT_FLAGS_COMPAT;
#endif

	return (char *)uregs;
}

/* Put registers back to task. */
static void putregs(struct task_struct *child, struct pt_regs *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);

	/* Don't allow overwriting the kernel-internal flags word. */
	uregs->flags = regs->flags;

	/* Only allow setting the ICS bit in the ex1 word. */
	uregs->ex1 = PL_ICS_EX1(USER_PL, EX1_ICS(uregs->ex1));

	*regs = *uregs;
}

enum tile_regset {
	REGSET_GPR,
};

static int tile_gpr_get(struct task_struct *target,
			  const struct user_regset *regset,
			  unsigned int pos, unsigned int count,
			  void *kbuf, void __user *ubuf)
{
	struct pt_regs regs;

	getregs(target, &regs);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, &regs, 0,
				   sizeof(regs));
}

static int tile_gpr_set(struct task_struct *target,
			  const struct user_regset *regset,
			  unsigned int pos, unsigned int count,
			  const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct pt_regs regs = *task_pt_regs(target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &regs, 0,
				 sizeof(regs));
	if (ret)
		return ret;

	putregs(target, &regs);

	return 0;
}

static const struct user_regset tile_user_regset[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.get = tile_gpr_get,
		.set = tile_gpr_set,
	},
};

static const struct user_regset_view tile_user_regset_view = {
	.name = CHIP_ARCH_NAME,
	.e_machine = ELF_ARCH,
	.ei_osabi = ELF_OSABI,
	.regsets = tile_user_regset,
	.n = ARRAY_SIZE(tile_user_regset),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &tile_user_regset_view;
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long __user *datap = (long __user __force *)data;
	unsigned long tmp;
	long ret = -EIO;
	char *childreg;
	struct pt_regs copyregs;

	switch (request) {

	case PTRACE_PEEKUSR:  /* Read register from pt_regs. */
		if (addr >= PTREGS_SIZE)
			break;
		childreg = getregs(child, &copyregs) + addr;
#ifdef CONFIG_COMPAT
		if (is_compat_task()) {
			if (addr & (sizeof(compat_long_t)-1))
				break;
			ret = put_user(*(compat_long_t *)childreg,
				       (compat_long_t __user *)datap);
		} else
#endif
		{
			if (addr & (sizeof(long)-1))
				break;
			ret = put_user(*(long *)childreg, datap);
		}
		break;

	case PTRACE_POKEUSR:  /* Write register in pt_regs. */
		if (addr >= PTREGS_SIZE)
			break;
		childreg = getregs(child, &copyregs) + addr;
#ifdef CONFIG_COMPAT
		if (is_compat_task()) {
			if (addr & (sizeof(compat_long_t)-1))
				break;
			*(compat_long_t *)childreg = data;
		} else
#endif
		{
			if (addr & (sizeof(long)-1))
				break;
			*(long *)childreg = data;
		}
		putregs(child, &copyregs);
		ret = 0;
		break;

	case PTRACE_GETREGS:  /* Get all registers from the child. */
		ret = copy_regset_to_user(child, &tile_user_regset_view,
					  REGSET_GPR, 0,
					  sizeof(struct pt_regs), datap);
		break;

	case PTRACE_SETREGS:  /* Set all registers in the child. */
		ret = copy_regset_from_user(child, &tile_user_regset_view,
					    REGSET_GPR, 0,
					    sizeof(struct pt_regs), datap);
		break;

	case PTRACE_GETFPREGS:  /* Get the child FPU state. */
	case PTRACE_SETFPREGS:  /* Set the child FPU state. */
		break;

	case PTRACE_SETOPTIONS:
		/* Support TILE-specific ptrace options. */
		BUILD_BUG_ON(PTRACE_O_MASK_TILE & PTRACE_O_MASK);
		tmp = data & PTRACE_O_MASK_TILE;
		data &= ~PTRACE_O_MASK_TILE;
		ret = ptrace_request(child, request, addr, data);
		if (ret == 0) {
			unsigned int flags = child->ptrace;
			flags &= ~(PTRACE_O_MASK_TILE << PT_OPT_FLAG_SHIFT);
			flags |= (tmp << PT_OPT_FLAG_SHIFT);
			child->ptrace = flags;
		}
		break;

	default:
#ifdef CONFIG_COMPAT
		if (task_thread_info(current)->status & TS_COMPAT) {
			ret = compat_ptrace_request(child, request,
						    addr, data);
			break;
		}
#endif
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
/* Not used; we handle compat issues in arch_ptrace() directly. */
long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			       compat_ulong_t addr, compat_ulong_t data)
{
	BUG();
}
#endif

int do_syscall_trace_enter(struct pt_regs *regs)
{
	u32 work = ACCESS_ONCE(current_thread_info()->flags);

	/*
	 * If TIF_NOHZ is set, we are required to call user_exit() before
	 * doing anything that could touch RCU.
	 */
	if (work & _TIF_NOHZ)
		user_exit();

	if (secure_computing(NULL) == -1)
		return -1;

	if (work & _TIF_SYSCALL_TRACE) {
		if (tracehook_report_syscall_entry(regs))
			regs->regs[TREG_SYSCALL_NR] = -1;
	}

	if (work & _TIF_SYSCALL_TRACEPOINT)
		trace_sys_enter(regs, regs->regs[TREG_SYSCALL_NR]);

	return regs->regs[TREG_SYSCALL_NR];
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	long errno;

	/*
	 * We may come here right after calling schedule_user()
	 * in which case we can be in RCU user mode.
	 */
	user_exit();

	/*
	 * The standard tile calling convention returns the value (or negative
	 * errno) in r0, and zero (or positive errno) in r1.
	 * It saves a couple of cycles on the hot path to do this work in
	 * registers only as we return, rather than updating the in-memory
	 * struct ptregs.
	 */
	errno = (long) regs->regs[0];
	if (errno < 0 && errno > -4096)
		regs->regs[1] = -errno;
	else
		regs->regs[1] = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);

	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, regs->regs[0]);
}

void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs)
{
	struct siginfo info;

	memset(&info, 0, sizeof(info));
	info.si_signo = SIGTRAP;
	info.si_code  = TRAP_BRKPT;
	info.si_addr  = (void __user *) regs->pc;

	/* Send us the fakey SIGTRAP */
	force_sig_info(SIGTRAP, &info, tsk);
}

/* Handle synthetic interrupt delivered only by the simulator. */
void __kprobes do_breakpoint(struct pt_regs* regs, int fault_num)
{
	enum ctx_state prev_state = exception_enter();
	send_sigtrap(current, regs);
	exception_exit(prev_state);
}
