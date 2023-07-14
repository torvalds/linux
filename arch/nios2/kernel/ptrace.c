/*
 * Copyright (C) 2014 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>
#include <linux/user.h>

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	const struct pt_regs *regs = task_pt_regs(target);
	const struct switch_stack *sw = (struct switch_stack *)regs - 1;

	membuf_zero(&to, 4); // R0
	membuf_write(&to, &regs->r1, 7 * 4); // R1..R7
	membuf_write(&to, &regs->r8, 8 * 4); // R8..R15
	membuf_write(&to, sw, 8 * 4); // R16..R23
	membuf_zero(&to, 2 * 4); /* et and bt */
	membuf_store(&to, regs->gp);
	membuf_store(&to, regs->sp);
	membuf_store(&to, regs->fp);
	membuf_store(&to, regs->ea);
	membuf_zero(&to, 4); // PTR_BA
	membuf_store(&to, regs->ra);
	membuf_store(&to, regs->ea); /* use ea for PC */
	return membuf_zero(&to, (NUM_PTRACE_REG - PTR_PC) * 4);
}

/*
 * Set the thread state from a regset passed in via ptrace
 */
static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	const struct switch_stack *sw = (struct switch_stack *)regs - 1;
	int ret = 0;

#define REG_IGNORE_RANGE(START, END)		\
	if (!ret)					\
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, \
			START * 4, (END * 4) + 4);

#define REG_IN_ONE(PTR, LOC)	\
	if (!ret)			\
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			(void *)(PTR), LOC * 4, (LOC * 4) + 4);

#define REG_IN_RANGE(PTR, START, END)	\
	if (!ret)				\
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			(void *)(PTR), START * 4, (END * 4) + 4);

	REG_IGNORE_RANGE(PTR_R0, PTR_R0);
	REG_IN_RANGE(&regs->r1, PTR_R1, PTR_R7);
	REG_IN_RANGE(&regs->r8, PTR_R8, PTR_R15);
	REG_IN_RANGE(sw, PTR_R16, PTR_R23);
	REG_IGNORE_RANGE(PTR_R24, PTR_R25); /* et and bt */
	REG_IN_ONE(&regs->gp, PTR_GP);
	REG_IN_ONE(&regs->sp, PTR_SP);
	REG_IN_ONE(&regs->fp, PTR_FP);
	REG_IN_ONE(&regs->ea, PTR_EA);
	REG_IGNORE_RANGE(PTR_BA, PTR_BA);
	REG_IN_ONE(&regs->ra, PTR_RA);
	REG_IN_ONE(&regs->ea, PTR_PC); /* use ea for PC */
	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  PTR_STATUS * 4, -1);

	return ret;
}

/*
 * Define the register sets available on Nios2 under Linux
 */
enum nios2_regset {
	REGSET_GENERAL,
};

static const struct user_regset nios2_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = NUM_PTRACE_REG,
		.size = sizeof(unsigned long),
		.align = sizeof(unsigned long),
		.regset_get = genregs_get,
		.set = genregs_set,
	}
};

static const struct user_regset_view nios2_user_view = {
	.name = "nios2",
	.e_machine = ELF_ARCH,
	.ei_osabi = ELF_OSABI,
	.regsets = nios2_regsets,
	.n = ARRAY_SIZE(nios2_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &nios2_user_view;
}

void ptrace_disable(struct task_struct *child)
{

}

long arch_ptrace(struct task_struct *child, long request, unsigned long addr,
		 unsigned long data)
{
	return ptrace_request(child, request, addr, data);
}

asmlinkage int do_syscall_trace_enter(void)
{
	int ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ret = ptrace_report_syscall_entry(task_pt_regs(current));

	return ret;
}

asmlinkage void do_syscall_trace_exit(void)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ptrace_report_syscall_exit(task_pt_regs(current), 0);
}
