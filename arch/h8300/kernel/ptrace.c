/*
 *  linux/arch/h8300/kernel/ptrace.c
 *
 *  Copyright 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/audit.h>
#include <linux/tracehook.h>
#include <linux/regset.h>
#include <linux/elf.h>

#define CCR_MASK 0x6f    /* mode/imask not set */
#define EXR_MASK 0x80    /* modify only T */

#define PT_REG(r) offsetof(struct pt_regs, r)

extern void user_disable_single_step(struct task_struct *child);

/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static const int register_offset[] = {
	PT_REG(er1), PT_REG(er2), PT_REG(er3), PT_REG(er4),
	PT_REG(er5), PT_REG(er6), PT_REG(er0), -1,
	PT_REG(orig_er0), PT_REG(ccr), PT_REG(pc),
#if defined(CONFIG_CPU_H8S)
	PT_REG(exr),
#endif
};

/* read register */
long h8300_get_reg(struct task_struct *task, int regno)
{
	switch (regno) {
	case PT_USP:
		return task->thread.usp + sizeof(long)*2;
	case PT_CCR:
	case PT_EXR:
	    return *(unsigned short *)(task->thread.esp0 +
				       register_offset[regno]);
	default:
	    return *(unsigned long *)(task->thread.esp0 +
				      register_offset[regno]);
	}
}

int h8300_put_reg(struct task_struct *task, int regno, unsigned long data)
{
	unsigned short oldccr;
	unsigned short oldexr;

	switch (regno) {
	case PT_USP:
		task->thread.usp = data - sizeof(long)*2;
	case PT_CCR:
		oldccr = *(unsigned short *)(task->thread.esp0 +
					     register_offset[regno]);
		oldccr &= ~CCR_MASK;
		data &= CCR_MASK;
		data |= oldccr;
		*(unsigned short *)(task->thread.esp0 +
				    register_offset[regno]) = data;
		break;
	case PT_EXR:
		oldexr = *(unsigned short *)(task->thread.esp0 +
					     register_offset[regno]);
		oldccr &= ~EXR_MASK;
		data &= EXR_MASK;
		data |= oldexr;
		*(unsigned short *)(task->thread.esp0 +
				    register_offset[regno]) = data;
		break;
	default:
		*(unsigned long *)(task->thread.esp0 +
				   register_offset[regno]) = data;
		break;
	}
	return 0;
}

static int regs_get(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    void *kbuf, void __user *ubuf)
{
	int r;
	struct user_regs_struct regs;
	long *reg = (long *)&regs;

	/* build user regs in buffer */
	BUILD_BUG_ON(sizeof(regs) % sizeof(long) != 0);
	for (r = 0; r < sizeof(regs) / sizeof(long); r++)
		*reg++ = h8300_get_reg(target, r);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &regs, 0, sizeof(regs));
}

static int regs_set(struct task_struct *target,
		    const struct user_regset *regset,
		    unsigned int pos, unsigned int count,
		    const void *kbuf, const void __user *ubuf)
{
	int r;
	int ret;
	struct user_regs_struct regs;
	long *reg;

	/* build user regs in buffer */
	BUILD_BUG_ON(sizeof(regs) % sizeof(long) != 0);
	for (reg = (long *)&regs, r = 0; r < sizeof(regs) / sizeof(long); r++)
		*reg++ = h8300_get_reg(target, r);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs, 0, sizeof(regs));
	if (ret)
		return ret;

	/* write back to pt_regs */
	for (reg = (long *)&regs, r = 0; r < sizeof(regs) / sizeof(long); r++)
		h8300_put_reg(target, r, *reg++);
	return 0;
}

enum h8300_regset {
	REGSET_GENERAL,
};

static const struct user_regset h8300_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= regs_get,
		.set		= regs_set,
	},
};

static const struct user_regset_view user_h8300_native_view = {
	.name = "h8300",
	.e_machine = EM_H8_300,
	.regsets = h8300_regsets,
	.n = ARRAY_SIZE(h8300_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_h8300_native_view;
}

void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;

	switch (request) {
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

asmlinkage long do_syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in regs->regs[0].
		 */
		ret = -1L;

	audit_syscall_entry(regs->er1, regs->er2, regs->er3,
			    regs->er4, regs->er5);

	return ret ?: regs->er0;
}

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
