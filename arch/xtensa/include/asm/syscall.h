/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007 Tensilica Inc.
 * Copyright (C) 2018 Cadence Design Systems Inc.
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H

struct pt_regs;

asmlinkage long xtensa_rt_sigreturn(struct pt_regs*);
asmlinkage long xtensa_shmat(int, char __user *, int);
asmlinkage long xtensa_fadvise64_64(int, int,
				    unsigned long long, unsigned long long);

#endif
