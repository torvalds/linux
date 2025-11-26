/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _UAPI_ASM_PTRACE_H
#define _UAPI_ASM_PTRACE_H

#include <linux/types.h>

/*
 * For PTRACE_{POKE,PEEK}USR. 0 - 31 are GPRs,
 * 32 is syscall's original ARG0, 33 is PC, 34 is BADVADDR.
 */
#define GPR_BASE	0
#define GPR_NUM		32
#define GPR_END		(GPR_BASE + GPR_NUM - 1)
#define ARG0		(GPR_END + 1)
#define PC		(GPR_END + 2)
#define BADVADDR	(GPR_END + 3)

#define NUM_FPU_REGS	32

struct user_pt_regs {
	/* Main processor registers. */
	unsigned long regs[32];

	/* Original syscall arg0. */
	unsigned long orig_a0;

	/* Special CSR registers. */
	unsigned long csr_era;
	unsigned long csr_badv;
	unsigned long reserved[10];
} __attribute__((aligned(8)));

struct user_fp_state {
	__u64 fpr[32];
	__u64 fcc;
	__u32 fcsr;
};

struct user_lsx_state {
	/* 32 registers, 128 bits width per register. */
	__u64 vregs[32*2];
};

struct user_lasx_state {
	/* 32 registers, 256 bits width per register. */
	__u64 vregs[32*4];
};

struct user_lbt_state {
	__u64 scr[4];
	__u32 eflags;
	__u32 ftop;
};

struct user_watch_state {
	__u64 dbg_info;
	struct {
		__u64    addr;
		__u64    mask;
		__u32    ctrl;
		__u32    pad;
	} dbg_regs[8];
};

struct user_watch_state_v2 {
	__u64 dbg_info;
	struct {
		__u64    addr;
		__u64    mask;
		__u32    ctrl;
		__u32    pad;
	} dbg_regs[14];
};

#define PTRACE_SYSEMU			0x1f
#define PTRACE_SYSEMU_SINGLESTEP	0x20

#endif /* _UAPI_ASM_PTRACE_H */
