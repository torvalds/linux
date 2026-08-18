// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/compat.h>
#include <linux/context_tracking.h>
#include <linux/randomize_kstack.h>
#include <linux/entry-common.h>

#include <asm/interrupt.h>
#include <asm/kup.h>
#include <asm/syscall.h>
#include <asm/time.h>
#include <asm/tm.h>
#include <asm/unistd.h>


/* Has to run notrace because it is entered not completely "reconciled" */
notrace long system_call_exception(struct pt_regs *regs, unsigned long r0)
{
	long ret;
	syscall_fn f;

	add_random_kstack_offset();
	r0 = syscall_enter_from_user_mode(regs, r0);

	if (unlikely(r0 >= NR_syscalls)) {
		if (unlikely(trap_is_unsupported_scv(regs))) {
			/* Unsupported scv vector */
			_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
			return regs->gpr[3];
		}
		return -ENOSYS;
	}

	/* May be faster to do array_index_nospec? */
	barrier_nospec();

#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
	// No COMPAT if we have SYSCALL_WRAPPER, see Kconfig
	f = (void *)sys_call_table[r0];
	ret = f(regs);
#else
	if (unlikely(is_compat_task())) {
		unsigned long r3, r4, r5, r6, r7, r8;

		f = (void *)compat_sys_call_table[r0];

		r3 = regs->gpr[3] & 0x00000000ffffffffULL;
		r4 = regs->gpr[4] & 0x00000000ffffffffULL;
		r5 = regs->gpr[5] & 0x00000000ffffffffULL;
		r6 = regs->gpr[6] & 0x00000000ffffffffULL;
		r7 = regs->gpr[7] & 0x00000000ffffffffULL;
		r8 = regs->gpr[8] & 0x00000000ffffffffULL;

		ret = f(r3, r4, r5, r6, r7, r8);
	} else {
		f = (void *)sys_call_table[r0];

		ret = f(regs->gpr[3], regs->gpr[4], regs->gpr[5],
			regs->gpr[6], regs->gpr[7], regs->gpr[8]);
	}
#endif

	return ret;
}
