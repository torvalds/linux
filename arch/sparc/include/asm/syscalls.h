/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC64_SYSCALLS_H
#define _SPARC64_SYSCALLS_H

struct pt_regs;

asmlinkage long sparc_do_fork(unsigned long clone_flags,
			      unsigned long stack_start,
			      struct pt_regs *regs,
			      unsigned long stack_size);

#endif /* _SPARC64_SYSCALLS_H */
