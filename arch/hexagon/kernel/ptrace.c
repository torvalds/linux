/*
 * Ptrace support for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <generated/compile.h>

#include <linux/kernel.h>
#include <linux/sched.h>
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
		   unsigned int pos, unsigned int count,
		   void *kbuf, void __user *ubuf)
{
	int ret;
	unsigned int dummy;
	struct pt_regs *regs = task_pt_regs(target);


	if (!regs)
		return -EIO;

	/* The general idea here is that the copyout must happen in
	 * exactly the same order in which the userspace expects these
	 * regs. Now, the sequence in userspace does not match the
	 * sequence in the kernel, so everything past the 32 gprs
	 * happens one at a time.
	 */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &regs->r00, 0, 32*sizeof(unsigned long));

#define ONEXT(KPT_REG, USR_REG) \
	if (!ret) \
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, \
			KPT_REG, offsetof(struct user_regs_struct, USR_REG), \
			offsetof(struct user_regs_struct, USR_REG) + \
				 sizeof(unsigned long));

	/* Must be exactly same sequence as struct user_regs_struct */
	ONEXT(&regs->sa0, sa0);
	ONEXT(&regs->lc0, lc0);
	ONEXT(&regs->sa1, sa1);
	ONEXT(&regs->lc1, lc1);
	ONEXT(&regs->m0, m0);
	ONEXT(&regs->m1, m1);
	ONEXT(&regs->usr, usr);
	ONEXT(&regs->preds, p3_0);
	ONEXT(&regs->gp, gp);
	ONEXT(&regs->ugp, ugp);
	ONEXT(&pt_elr(regs), pc);
	dummy = pt_cause(regs);
	ONEXT(&dummy, cause);
	ONEXT(&pt_badva(regs), badva);
#if CONFIG_HEXAGON_ARCH_VERSION >=4
	ONEXT(&regs->cs0, cs0);
	ONEXT(&regs->cs1, cs1);
#endif

	/* Pad the rest with zeros, if needed */
	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					offsetof(struct user_regs_struct, pad1), -1);
	return ret;
}

static int genregs_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret;
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
#endif

	/* Ignore the rest, if needed */
	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					offsetof(struct user_regs_struct, pad1), -1);

	if (ret)
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
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(unsigned long),
		.align = sizeof(unsigned long),
		.get = genregs_get,
		.set = genregs_set,
	},
};

static const struct user_regset_view hexagon_user_view = {
	.name = UTS_MACHINE,
	.e_machine = ELF_ARCH,
	.ei_osabi = ELF_OSABI,
	.regsets = hexagon_regsets,
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
