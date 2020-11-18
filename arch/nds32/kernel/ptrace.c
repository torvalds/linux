// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/sched/task_stack.h>

enum nds32_regset {
	REGSET_GPR,
};

static int gpr_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	return membuf_write(&to, &task_pt_regs(target)->user_regs,
				sizeof(struct user_pt_regs));
}

static int gpr_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user * ubuf)
{
	int err;
	struct user_pt_regs newregs = task_pt_regs(target)->user_regs;

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &newregs, 0, -1);
	if (err)
		return err;

	task_pt_regs(target)->user_regs = newregs;
	return 0;
}

static const struct user_regset nds32_regsets[] = {
	[REGSET_GPR] = {
			.core_note_type = NT_PRSTATUS,
			.n = sizeof(struct user_pt_regs) / sizeof(u32),
			.size = sizeof(elf_greg_t),
			.align = sizeof(elf_greg_t),
			.regset_get = gpr_get,
			.set = gpr_set}
};

static const struct user_regset_view nds32_user_view = {
	.name = "nds32",
	.e_machine = EM_NDS32,
	.regsets = nds32_regsets,
	.n = ARRAY_SIZE(nds32_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &nds32_user_view;
}

void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

/* do_ptrace()
 *
 * Provide ptrace defined service.
 */
long arch_ptrace(struct task_struct *child, long request, unsigned long addr,
		 unsigned long data)
{
	int ret = -EIO;

	switch (request) {
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

void user_enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs;
	regs = task_pt_regs(child);
	regs->ipsw |= PSW_mskHSS;
	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	struct pt_regs *regs;
	regs = task_pt_regs(child);
	regs->ipsw &= ~PSW_mskHSS;
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}

/* sys_trace()
 *
 * syscall trace handler.
 */

asmlinkage int syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE)) {
		if (tracehook_report_syscall_entry(regs))
			forget_syscall(regs);
	}
	return regs->syscallno;
}

asmlinkage void syscall_trace_leave(struct pt_regs *regs)
{
	int step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);

}
