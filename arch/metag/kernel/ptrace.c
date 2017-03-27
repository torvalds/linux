/*
 *  Copyright (C) 2005-2012 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/uaccess.h>
#include <trace/syscall.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/*
 * user_regset definitions.
 */

static unsigned long user_txstatus(const struct pt_regs *regs)
{
	unsigned long data = (unsigned long)regs->ctx.Flags;

	if (regs->ctx.SaveMask & TBICTX_CBUF_BIT)
		data |= USER_GP_REGS_STATUS_CATCH_BIT;

	return data;
}

int metag_gp_regs_copyout(const struct pt_regs *regs,
			  unsigned int pos, unsigned int count,
			  void *kbuf, void __user *ubuf)
{
	const void *ptr;
	unsigned long data;
	int ret;

	/* D{0-1}.{0-7} */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  regs->ctx.DX, 0, 4*16);
	if (ret)
		goto out;
	/* A{0-1}.{0-1} */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  regs->ctx.AX, 4*16, 4*20);
	if (ret)
		goto out;
	/* A{0-1}.2 */
	if (regs->ctx.SaveMask & TBICTX_XEXT_BIT)
		ptr = regs->ctx.Ext.Ctx.pExt;
	else
		ptr = &regs->ctx.Ext.AX2;
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  ptr, 4*20, 4*22);
	if (ret)
		goto out;
	/* A{0-1}.3 */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &regs->ctx.AX3, 4*22, 4*24);
	if (ret)
		goto out;
	/* PC */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &regs->ctx.CurrPC, 4*24, 4*25);
	if (ret)
		goto out;
	/* TXSTATUS */
	data = user_txstatus(regs);
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &data, 4*25, 4*26);
	if (ret)
		goto out;
	/* TXRPT, TXBPOBITS, TXMODE */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &regs->ctx.CurrRPT, 4*26, 4*29);
	if (ret)
		goto out;
	/* Padding */
	ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
				       4*29, 4*30);
out:
	return ret;
}

int metag_gp_regs_copyin(struct pt_regs *regs,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	void *ptr;
	unsigned long data;
	int ret;

	/* D{0-1}.{0-7} */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs->ctx.DX, 0, 4*16);
	if (ret)
		goto out;
	/* A{0-1}.{0-1} */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs->ctx.AX, 4*16, 4*20);
	if (ret)
		goto out;
	/* A{0-1}.2 */
	if (regs->ctx.SaveMask & TBICTX_XEXT_BIT)
		ptr = regs->ctx.Ext.Ctx.pExt;
	else
		ptr = &regs->ctx.Ext.AX2;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 ptr, 4*20, 4*22);
	if (ret)
		goto out;
	/* A{0-1}.3 */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->ctx.AX3, 4*22, 4*24);
	if (ret)
		goto out;
	/* PC */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->ctx.CurrPC, 4*24, 4*25);
	if (ret)
		goto out;
	/* TXSTATUS */
	data = user_txstatus(regs);
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &data, 4*25, 4*26);
	if (ret)
		goto out;
	regs->ctx.Flags = data & 0xffff;
	if (data & USER_GP_REGS_STATUS_CATCH_BIT)
		regs->ctx.SaveMask |= TBICTX_XCBF_BIT | TBICTX_CBUF_BIT;
	else
		regs->ctx.SaveMask &= ~TBICTX_CBUF_BIT;
	/* TXRPT, TXBPOBITS, TXMODE */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->ctx.CurrRPT, 4*26, 4*29);
out:
	return ret;
}

static int metag_gp_regs_get(struct task_struct *target,
			     const struct user_regset *regset,
			     unsigned int pos, unsigned int count,
			     void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	return metag_gp_regs_copyout(regs, pos, count, kbuf, ubuf);
}

static int metag_gp_regs_set(struct task_struct *target,
			     const struct user_regset *regset,
			     unsigned int pos, unsigned int count,
			     const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	return metag_gp_regs_copyin(regs, pos, count, kbuf, ubuf);
}

int metag_cb_regs_copyout(const struct pt_regs *regs,
			  unsigned int pos, unsigned int count,
			  void *kbuf, void __user *ubuf)
{
	int ret;

	/* TXCATCH{0-3} */
	if (regs->ctx.SaveMask & TBICTX_XCBF_BIT)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  regs->extcb0, 0, 4*4);
	else
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       0, 4*4);
	return ret;
}

int metag_cb_regs_copyin(struct pt_regs *regs,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	int ret;

	/* TXCATCH{0-3} */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs->extcb0, 0, 4*4);
	return ret;
}

static int metag_cb_regs_get(struct task_struct *target,
			     const struct user_regset *regset,
			     unsigned int pos, unsigned int count,
			     void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	return metag_cb_regs_copyout(regs, pos, count, kbuf, ubuf);
}

static int metag_cb_regs_set(struct task_struct *target,
			     const struct user_regset *regset,
			     unsigned int pos, unsigned int count,
			     const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	return metag_cb_regs_copyin(regs, pos, count, kbuf, ubuf);
}

int metag_rp_state_copyout(const struct pt_regs *regs,
			   unsigned int pos, unsigned int count,
			   void *kbuf, void __user *ubuf)
{
	unsigned long mask;
	u64 *ptr;
	int ret, i;

	/* Empty read pipeline */
	if (!(regs->ctx.SaveMask & TBICTX_CBRP_BIT)) {
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       0, 4*13);
		goto out;
	}

	mask = (regs->ctx.CurrDIVTIME & TXDIVTIME_RPMASK_BITS) >>
		TXDIVTIME_RPMASK_S;

	/* Read pipeline entries */
	ptr = (void *)&regs->extcb0[1];
	for (i = 0; i < 6; ++i, ++ptr) {
		if (mask & (1 << i))
			ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
						  ptr, 8*i, 8*(i + 1));
		else
			ret = user_regset_copyout_zero(&pos, &count, &kbuf,
						       &ubuf, 8*i, 8*(i + 1));
		if (ret)
			goto out;
	}
	/* Mask of entries */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &mask, 4*12, 4*13);
out:
	return ret;
}

int metag_rp_state_copyin(struct pt_regs *regs,
			  unsigned int pos, unsigned int count,
			  const void *kbuf, const void __user *ubuf)
{
	struct user_rp_state rp;
	unsigned long long *ptr;
	int ret, i;

	/* Read the entire pipeline before making any changes */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &rp, 0, 4*13);
	if (ret)
		goto out;

	/* Write pipeline entries */
	ptr = (void *)&regs->extcb0[1];
	for (i = 0; i < 6; ++i, ++ptr)
		if (rp.mask & (1 << i))
			*ptr = rp.entries[i];

	/* Update RPMask in TXDIVTIME */
	regs->ctx.CurrDIVTIME &= ~TXDIVTIME_RPMASK_BITS;
	regs->ctx.CurrDIVTIME |= (rp.mask << TXDIVTIME_RPMASK_S)
				 & TXDIVTIME_RPMASK_BITS;

	/* Set/clear flags to indicate catch/read pipeline state */
	if (rp.mask)
		regs->ctx.SaveMask |= TBICTX_XCBF_BIT | TBICTX_CBRP_BIT;
	else
		regs->ctx.SaveMask &= ~TBICTX_CBRP_BIT;
out:
	return ret;
}

static int metag_rp_state_get(struct task_struct *target,
			      const struct user_regset *regset,
			      unsigned int pos, unsigned int count,
			      void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	return metag_rp_state_copyout(regs, pos, count, kbuf, ubuf);
}

static int metag_rp_state_set(struct task_struct *target,
			      const struct user_regset *regset,
			      unsigned int pos, unsigned int count,
			      const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	return metag_rp_state_copyin(regs, pos, count, kbuf, ubuf);
}

static int metag_tls_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	void __user *tls = target->thread.tls_ptr;
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, &tls, 0, -1);
}

static int metag_tls_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	int ret;
	void __user *tls = target->thread.tls_ptr;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &tls, 0, -1);
	if (ret)
		return ret;

	target->thread.tls_ptr = tls;
	return ret;
}

enum metag_regset {
	REGSET_GENERAL,
	REGSET_CBUF,
	REGSET_READPIPE,
	REGSET_TLS,
};

static const struct user_regset metag_regsets[] = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(long),
		.align = sizeof(long long),
		.get = metag_gp_regs_get,
		.set = metag_gp_regs_set,
	},
	[REGSET_CBUF] = {
		.core_note_type = NT_METAG_CBUF,
		.n = sizeof(struct user_cb_regs) / sizeof(long),
		.size = sizeof(long),
		.align = sizeof(long long),
		.get = metag_cb_regs_get,
		.set = metag_cb_regs_set,
	},
	[REGSET_READPIPE] = {
		.core_note_type = NT_METAG_RPIPE,
		.n = sizeof(struct user_rp_state) / sizeof(long),
		.size = sizeof(long),
		.align = sizeof(long long),
		.get = metag_rp_state_get,
		.set = metag_rp_state_set,
	},
	[REGSET_TLS] = {
		.core_note_type = NT_METAG_TLS,
		.n = 1,
		.size = sizeof(void *),
		.align = sizeof(void *),
		.get = metag_tls_get,
		.set = metag_tls_set,
	},
};

static const struct user_regset_view user_metag_view = {
	.name = "metag",
	.e_machine = EM_METAG,
	.regsets = metag_regsets,
	.n = ARRAY_SIZE(metag_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_metag_view;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* nothing to do.. */
}

long arch_ptrace(struct task_struct *child, long request, unsigned long addr,
		 unsigned long data)
{
	int ret;

	switch (request) {
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

int syscall_trace_enter(struct pt_regs *regs)
{
	int ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ret = tracehook_report_syscall_entry(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->ctx.DX[0].U1);

	return ret ? -1 : regs->ctx.DX[0].U1;
}

void syscall_trace_leave(struct pt_regs *regs)
{
	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->ctx.DX[0].U1);

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, 0);
}
