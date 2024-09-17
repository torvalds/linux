// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>

#include <asm/switch_to.h>

#include "ptrace-decl.h"

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last checkpointed
 * value of all FPR registers for the current transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	fpr[32];
 *	u64	fpscr;
 * };
 */
int fpr_get(struct task_struct *target, const struct user_regset *regset,
	    struct membuf to)
{
	u64 buf[33];
	int i;

	flush_fp_to_thread(target);

	/* copy to local buffer then write that out */
	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.TS_FPR(i);
	buf[32] = target->thread.fp_state.fpscr;
	return membuf_write(&to, buf, 33 * sizeof(u64));
}

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last checkpointed
 * value of all FPR registers for the current transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	fpr[32];
 *	u64	fpscr;
 * };
 *
 */
int fpr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf)
{
	u64 buf[33];
	int i;

	flush_fp_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.TS_FPR(i);
	buf[32] = target->thread.fp_state.fpscr;

	/* copy to local buffer then write that out */
	i = user_regset_copyin(&pos, &count, &kbuf, &ubuf, buf, 0, -1);
	if (i)
		return i;

	for (i = 0; i < 32 ; i++)
		target->thread.TS_FPR(i) = buf[i];
	target->thread.fp_state.fpscr = buf[32];
	return 0;
}

/*
 * Currently to set and get all the vsx state, you need to call
 * the fp and VMX calls as well.  This only get/sets the lower 32
 * 128bit VSX registers.
 */

int vsr_active(struct task_struct *target, const struct user_regset *regset)
{
	flush_vsx_to_thread(target);
	return target->thread.used_vsr ? regset->n : 0;
}

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last
 * checkpointed value of all FPR registers for the current
 * transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	vsx[32];
 * };
 */
int vsr_get(struct task_struct *target, const struct user_regset *regset,
	    struct membuf to)
{
	u64 buf[32];
	int i;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);
	flush_vsx_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.fp_state.fpr[i][TS_VSRLOWOFFSET];

	return membuf_write(&to, buf, 32 * sizeof(double));
}

/*
 * Regardless of transactions, 'fp_state' holds the current running
 * value of all FPR registers and 'ckfp_state' holds the last
 * checkpointed value of all FPR registers for the current
 * transaction.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	u64	vsx[32];
 * };
 */
int vsr_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf)
{
	u64 buf[32];
	int ret, i;

	flush_tmregs_to_thread(target);
	flush_fp_to_thread(target);
	flush_altivec_to_thread(target);
	flush_vsx_to_thread(target);

	for (i = 0; i < 32 ; i++)
		buf[i] = target->thread.fp_state.fpr[i][TS_VSRLOWOFFSET];

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 buf, 0, 32 * sizeof(double));
	if (!ret)
		for (i = 0; i < 32 ; i++)
			target->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = buf[i];

	return ret;
}
