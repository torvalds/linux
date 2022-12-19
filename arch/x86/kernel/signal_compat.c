// SPDX-License-Identifier: GPL-2.0
#include <linux/compat.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>

void sigaction_compat_abi(struct k_sigaction *act, struct k_sigaction *oact)
{
	if (!act)
		return;

	if (in_ia32_syscall())
		act->sa.sa_flags |= SA_IA32_ABI;
	if (in_x32_syscall())
		act->sa.sa_flags |= SA_X32_ABI;
}
