// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ptrace support for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/user.h>
#include <linux/elf.h>

#include <asm/user.h>

#if arch_has_single_step()
/*  Both called from ptrace_resume  */
void user_enable_single_step(struct task_struct *child)
{
	pt_set_singlestep(task_pt_regs(child));
	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	pt_clr_singlestep(task_pt_regs(child));
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}
#endif

static int genregs_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	struct pt_regs *regs = task_pt_regs(target);

	/* The general idea here is that the copyout must happen in
	 * exactly the same order in which the userspace expects these
	 * regs. Now, the sequence in userspace does not match the
	 * sequence in the kernel, so everything past the 32 gprs
	 * happens one at a time.
	 */
	membuf_write(&to, &regs->r00, 32*sizeof(unsigned long));
	/* Must be exactly same sequence as struct user_regs_struct */
	membuf_store(&to, regs->sa0);
	membuf_store(&to, regs->lc0);
	membuf_store(&to, regs->sa1);
	membuf_store(&to, regs->lc1);
	membuf_store(&to, regs->m0);
	membuf_store(&to, regs->m1);
	membuf_store(&to, regs->usr);
	membuf_store(&to, regs->preds);
	membuf_store(&to, regs->gp);
	membuf_store(&to, regs->ugp);
	membuf_store(&to, pt_elr(regs)); // pc
	membuf_store(&to, (unsigned long)pt_cause(regs)); // cause
	membuf_store(&to, pt_badva(regs)); // badva
#if CONFIG_HEXAGON_ARCH_VERSION >=4
	membuf_store(&to, regs->cs0);
	membuf_store(&to, regs->cs1);
	return membuf_zero(&to, sizeof(unsigned long));
#else
	return membuf_zero(&to, 3 * sizeof(unsigned long));
#endif
}

static int genregs_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret, ignore_offset;
	unsigned long bucket;
	struct pt_regs *regs = task_pt_regs(target);

	if (!regs)
		return -EIO;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->r00, 0, 32*sizeof(unsigned long));

#define INEXT(KPT_REG, USR_REG) \
	if (!ret) \
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			KPT_REG, offsetof(struct user_regs_struct, USR_REG), \
			offsetof(struct user_regs_struct, USR_REG) + \
				sizeof(unsigned long));

	/* Must be exactly same sequence as struct user_regs_struct */
	INEXT(&regs->sa0, sa0);
	INEXT(&regs->lc0, lc0);
	INEXT(&regs->sa1, sa1);
	INEXT(&regs->lc1, lc1);
	INEXT(&regs->m0, m0);
	INEXT(&regs->m1, m1);
	INEXT(&regs->usr, usr);
	INEXT(&regs->preds, p3_0);
	INEXT(&regs->gp, gp);
	INEXT(&regs->ugp, ugp);
	INEXT(&pt_elr(regs), pc);

	/* CAUSE and BADVA aren't writeable. */
	INEXT(&bucket, cause);
	INEXT(&bucket, badva);

#if CONFIG_HEXAGON_ARCH_VERSION >=4
	INEXT(&regs->cs0, cs0);
	INEXT(&regs->cs1, cs1);
	ignore_offset = offsetof(struct user_regs_struct, pad1);
#else
	ignore_offset = offsetof(struct user_regs_struct, cs0);
#endif

	/* Ignore the rest, if needed */
	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  ignore_offset, -1);
	else
		return ret;

	/*
	 * This is special; SP is actually restored by the VM via the
	 * special event record which is set by the special trap.
	 */
	regs->hvmer.vmpsp = regs->r29;
	return 0;
}

enum hexagon_regset {
	REGSET_GENERAL,
};

static const struct user_regset hexagon_regsets[] = {
	[REGSET_GENERAL] = {
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n = ELF_NGREG,
		.size = sizeof(unsigned long),
		.align = sizeof(unsigned long),
		.regset_get = genregs_get,
		.set = genregs_set,
	},
};

static const struct user_regset_view hexagon_user_view = {
	.name = "hexagon",
	.e_machine = ELF_ARCH,
	.ei_osabi = ELF_OSABI,
	.regsets = hexagon_regsets,
	.e_flags = ELF_CORE_EFLAGS,
	.n = ARRAY_SIZE(hexagon_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &hexagon_user_view;
}

void ptrace_disable(struct task_struct *child)
{
	/* Boilerplate - resolves to null inline if no HW single-step */
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	return ptrace_request(child, request, addr, data);
}
