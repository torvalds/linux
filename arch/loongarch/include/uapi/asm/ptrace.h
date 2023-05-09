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

#ifndef __KERNEL__
#include <stdint.h>
#endif

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
	uint64_t    fpr[32];
	uint64_t    fcc;
	uint32_t    fcsr;
};

struct user_watch_state {
	uint64_t dbg_info;
	struct {
		uint64_t    addr;
		uint64_t    mask;
		uint32_t    ctrl;
		uint32_t    pad;
	} dbg_regs[8];
};

#define PTRACE_SYSEMU			0x1f
#define PTRACE_SYSEMU_SINGLESTEP	0x20

#endif /* _UAPI_ASM_PTRACE_H */
