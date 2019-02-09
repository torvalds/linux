// SPDX-License-Identifier: GPL-2.0
#include <linux/syscalls.h>
#include <os.h>

SYSCALL_DEFINE2(arch_prctl, int, option, unsigned long, arg2)
{
	return -EINVAL;
}
