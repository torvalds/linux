/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/regset.h>
#include <linux/unistd.h>
#include <linux/elf.h>

static struct callee_regs *task_callee_regs(struct task_struct *tsk)
{
	struct callee_regs *tmp = (struct callee_regs *)tsk->thread.callee_reg;
	return tmp;
}

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct pt_regs *ptregs = task_pt_regs(target);
	const struct callee_regs *cregs = task_callee_regs(target);
	int ret = 0;
	unsigned int stop_pc_val;

#define REG_O_CHUNK(START, END, PTR)	\
	if (!ret)	\
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, PTR, \
			offsetof(struct user_regs_struct, START), \
			offsetof(struct user_regs_struct, END));

#define REG_O_ONE(LOC, PTR)	\
	if (!ret)		\
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, PTR, \
			offsetof(struct user_regs_struct, LOC), \
			offsetof(struct user_regs_struct, LOC) + 4);

#define REG_O_ZERO(LOC)		\
	if (!ret)		\
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf, \
			offsetof(struct user_regs_struct, LOC), \
			offsetof(struct user_regs_struct, LOC) + 4);

	REG_O_ZERO(pad);
	REG_O_CHUNK(scratch, callee, ptregs);
	REG_O_ZERO(pad2);
	REG_O_CHUNK(callee, efa, cregs);
	REG_O_CHUNK(efa, stop_pc, &target->thread.fault_address);

	if (!ret) {
		if (in_brkpt_trap(ptregs)) {
			stop_pc_val = target->thread.fault_address;
			pr_debug("\t\tstop_pc (brk-pt)\n");
		} else {
			stop_pc_val = ptregs->ret;
			pr_debug("\t\tstop_pc (others)\n");
		}

		REG_O_ONE(stop_pc, &stop_pc_val);
	}

	return ret;
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	const struct pt_regs *ptregs = task_pt_regs(target);
	const struct callee_regs *cregs = task_callee_regs(target);
	int ret = 0;

#define REG_IN_CHUNK(FIRST, NEXT, PTR)	\
	if (!ret)			\
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			(void *)(PTR), \
			offsetof(struct user_regs_struct, FIRST), \
			offsetof(struct user_regs_struct, NEXT));

#define REG_IN_ONE(LOC, PTR)		\
	if (!ret)			\
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			(void *)(PTR), \
			offsetof(struct user_regs_struct, LOC), \
			offsetof(struct user_regs_struct, LOC) + 4);

#define REG_IGNORE_ONE(LOC)		\
	if (!ret)			\
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, \
			offsetof(struct user_regs_struct, LOC), \
			offsetof(struct user_regs_struct, LOC) + 4);

	REG_IGNORE_ONE(pad);
	/* TBD: disallow updates to STATUS32 etc*/
	REG_IN_CHUNK(scratch, pad2, ptregs);	/* pt_regs[bta..sp] */
	REG_IGNORE_ONE(pad2);
	REG_IN_CHUNK(callee, efa, cregs);	/* callee_regs[r25..r13] */
	REG_IGNORE_ONE(efa);			/* efa update invalid */
	REG_IN_ONE(stop_pc, &ptregs->ret);	/* stop_pc: PC update */

	return ret;
}

enum arc_getset {
	REGSET_GENERAL,
};

static const struct user_regset arc_regsets[] = {
	[REGSET_GENERAL] = {
	       .core_note_type = NT_PRSTATUS,
	       .n = ELF_NGREG,
	       .size = sizeof(unsigned long),
	       .align = sizeof(unsigned long),
	       .get = genregs_get,
	       .set = genregs_set,
	}
};

static const struct user_regset_view user_arc_view = {
	.name		= UTS_MACHINE,
	.e_machine	= EM_ARCOMPACT,
	.regsets	= arc_regsets,
	.n		= ARRAY_SIZE(arc_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_arc_view;
}

void ptrace_disable(struct task_struct *child)
{
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret = -EIO;

	pr_debug("REQ=%ld: ADDR =0x%lx, DATA=0x%lx)\n", request, addr, data);

	switch (request) {
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage int syscall_trace_entry(struct pt_regs *regs)
{
	if (tracehook_report_syscall_entry(regs))
		return ULONG_MAX;

	return regs->r8;
}

asmlinkage void syscall_trace_exit(struct pt_regs *regs)
{
	tracehook_report_syscall_exit(regs, 0);
}
