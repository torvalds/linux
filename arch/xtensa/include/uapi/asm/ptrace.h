/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/asm-xtensa/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _UAPI_XTENSA_PTRACE_H
#define _UAPI_XTENSA_PTRACE_H

#include <linux/types.h>

/* Registers used by strace */

#define REG_A_BASE	0x0000
#define REG_AR_BASE	0x0100
#define REG_PC		0x0020
#define REG_PS		0x02e6
#define REG_WB		0x0248
#define REG_WS		0x0249
#define REG_LBEG	0x0200
#define REG_LEND	0x0201
#define REG_LCOUNT	0x0202
#define REG_SAR		0x0203

#define SYSCALL_NR	0x00ff

/* Other PTRACE_ values defined in <linux/ptrace.h> using values 0-9,16,17,24 */

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETXTREGS	18
#define PTRACE_SETXTREGS	19
#define PTRACE_GETHBPREGS	20
#define PTRACE_SETHBPREGS	21
#define PTRACE_GETFDPIC		22

#define PTRACE_GETFDPIC_EXEC	0
#define PTRACE_GETFDPIC_INTERP	1

#ifndef __ASSEMBLY__

struct user_pt_regs {
	__u32 pc;
	__u32 ps;
	__u32 lbeg;
	__u32 lend;
	__u32 lcount;
	__u32 sar;
	__u32 windowstart;
	__u32 windowbase;
	__u32 threadptr;
	__u32 syscall;
	__u32 reserved[6 + 48];
	__u32 a[64];
};

#endif
#endif /* _UAPI_XTENSA_PTRACE_H */
