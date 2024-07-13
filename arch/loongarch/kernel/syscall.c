// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/capability.h>
#include <linux/entry-common.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>

#include <asm/asm.h>
#include <asm/exception.h>
#include <asm/signal.h>
#include <asm/switch_to.h>
#include <asm-generic/syscalls.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)	[nr] = (call),

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len, unsigned long,
		prot, unsigned long, flags, unsigned long, fd, unsigned long, offset)
{
	if (offset & ~PAGE_MASK)
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
}

void *sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};

typedef long (*sys_call_fn)(unsigned long, unsigned long,
	unsigned long, unsigned long, unsigned long, unsigned long);

void noinstr do_syscall(struct pt_regs *regs)
{
	unsigned long nr;
	sys_call_fn syscall_fn;

	nr = regs->regs[11];
	/* Set for syscall restarting */
	if (nr < NR_syscalls)
		regs->regs[0] = nr + 1;

	regs->csr_era += 4;
	regs->orig_a0 = regs->regs[4];
	regs->regs[4] = -ENOSYS;

	nr = syscall_enter_from_user_mode(regs, nr);

	if (nr < NR_syscalls) {
		syscall_fn = sys_call_table[nr];
		regs->regs[4] = syscall_fn(regs->orig_a0, regs->regs[5], regs->regs[6],
					   regs->regs[7], regs->regs[8], regs->regs[9]);
	}

	syscall_exit_to_user_mode(regs);
}
