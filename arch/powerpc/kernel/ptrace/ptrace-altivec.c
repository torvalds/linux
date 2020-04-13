// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>
#include <linux/elf.h>

#include <asm/switch_to.h>

#include "ptrace-decl.h"

/*
 * Get/set all the altivec registers vr0..vr31, vscr, vrsave, in one go.
 * The transfer totals 34 quadword.  Quadwords 0-31 contain the
 * corresponding vector registers.  Quadword 32 contains the vscr as the
 * last word (offset 12) within that quadword.  Quadword 33 contains the
 * vrsave as the first word (offset 0) within the quadword.
 *
 * This definition of the VMX state is compatible with the current PPC32
 * ptrace interface.  This allows signal handling and ptrace to use the
 * same structures.  This also simplifies the implementation of a bi-arch
 * (combined (32- and 64-bit) gdb.
 */

int vr_active(struct task_struct *target, const struct user_regset *regset)
{
	flush_altivec_to_thread(target);
	return target->thread.used_vr ? regset->n : 0;
}

/*
 * Regardless of transactions, 'vr_state' holds the current running
 * value of all the VMX registers and 'ckvr_state' holds the last
 * checkpointed value of all the VMX registers for the current
 * transaction to fall back on in case it aborts.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	vector128	vr[32];
 *	vector128	vscr;
 *	vector128	vrsave;
 * };
 */
int vr_get(struct task_struct *target, const struct user_regset *regset,
	   unsigned int pos, unsigned int count, void *kbuf, void __user *ubuf)
{
	int ret;

	flush_altivec_to_thread(target);

	BUILD_BUG_ON(offsetof(struct thread_vr_state, vscr) !=
		     offsetof(struct thread_vr_state, vr[32]));

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &target->thread.vr_state, 0,
				  33 * sizeof(vector128));
	if (!ret) {
		/*
		 * Copy out only the low-order word of vrsave.
		 */
		int start, end;
		union {
			elf_vrreg_t reg;
			u32 word;
		} vrsave;
		memset(&vrsave, 0, sizeof(vrsave));

		vrsave.word = target->thread.vrsave;

		start = 33 * sizeof(vector128);
		end = start + sizeof(vrsave);
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, &vrsave,
					  start, end);
	}

	return ret;
}

/*
 * Regardless of transactions, 'vr_state' holds the current running
 * value of all the VMX registers and 'ckvr_state' holds the last
 * checkpointed value of all the VMX registers for the current
 * transaction to fall back on in case it aborts.
 *
 * Userspace interface buffer layout:
 *
 * struct data {
 *	vector128	vr[32];
 *	vector128	vscr;
 *	vector128	vrsave;
 * };
 */
int vr_set(struct task_struct *target, const struct user_regset *regset,
	   unsigned int pos, unsigned int count,
	   const void *kbuf, const void __user *ubuf)
{
	int ret;

	flush_altivec_to_thread(target);

	BUILD_BUG_ON(offsetof(struct thread_vr_state, vscr) !=
		     offsetof(struct thread_vr_state, vr[32]));

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.vr_state, 0,
				 33 * sizeof(vector128));
	if (!ret && count > 0) {
		/*
		 * We use only the first word of vrsave.
		 */
		int start, end;
		union {
			elf_vrreg_t reg;
			u32 word;
		} vrsave;
		memset(&vrsave, 0, sizeof(vrsave));

		vrsave.word = target->thread.vrsave;

		start = 33 * sizeof(vector128);
		end = start + sizeof(vrsave);
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &vrsave,
					 start, end);
		if (!ret)
			target->thread.vrsave = vrsave.word;
	}

	return ret;
}
