/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _UAPI__ASM_SIGCONTEXT_H
#define _UAPI__ASM_SIGCONTEXT_H

#include <linux/types.h>

/*
 * Signal context structure - contains all info to do with the state
 * before the signal handler was invoked.
 */
struct sigcontext {
	__u64 fault_address;
	/* AArch64 registers */
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
	/* 4K reserved for FP/SIMD state and future expansion */
	__u8 __reserved[4096] __attribute__((__aligned__(16)));
};

/*
 * Allocation of __reserved[]:
 * (Note: records do not necessarily occur in the order shown here.)
 *
 *	size		description
 *
 *	0x210		fpsimd_context
 *	 0x10		esr_context
 *	 0x20		extra_context (optional)
 *	 0x10		terminator (null _aarch64_ctx)
 *
 *	0xdb0		(reserved for future allocation)
 *
 * New records that can exceed this space need to be opt-in for userspace, so
 * that an expanded signal frame is not generated unexpectedly.  The mechanism
 * for opting in will depend on the extension that generates each new record.
 * The above table documents the maximum set and sizes of records than can be
 * generated when userspace does not opt in for any such extension.
 */

/*
 * Header to be used at the beginning of structures extending the user
 * context. Such structures must be placed after the rt_sigframe on the stack
 * and be 16-byte aligned. The last structure must be a dummy one with the
 * magic and size set to 0.
 */
struct _aarch64_ctx {
	__u32 magic;
	__u32 size;
};

#define FPSIMD_MAGIC	0x46508001

struct fpsimd_context {
	struct _aarch64_ctx head;
	__u32 fpsr;
	__u32 fpcr;
	__uint128_t vregs[32];
};

/* ESR_EL1 context */
#define ESR_MAGIC	0x45535201

struct esr_context {
	struct _aarch64_ctx head;
	__u64 esr;
};

/*
 * extra_context: describes extra space in the signal frame for
 * additional structures that don't fit in sigcontext.__reserved[].
 *
 * Note:
 *
 * 1) fpsimd_context, esr_context and extra_context must be placed in
 * sigcontext.__reserved[] if present.  They cannot be placed in the
 * extra space.  Any other record can be placed either in the extra
 * space or in sigcontext.__reserved[], unless otherwise specified in
 * this file.
 *
 * 2) There must not be more than one extra_context.
 *
 * 3) If extra_context is present, it must be followed immediately in
 * sigcontext.__reserved[] by the terminating null _aarch64_ctx.
 *
 * 4) The extra space to which datap points must start at the first
 * 16-byte aligned address immediately after the terminating null
 * _aarch64_ctx that follows the extra_context structure in
 * __reserved[].  The extra space may overrun the end of __reserved[],
 * as indicated by a sufficiently large value for the size field.
 *
 * 5) The extra space must itself be terminated with a null
 * _aarch64_ctx.
 */
#define EXTRA_MAGIC	0x45585401

struct extra_context {
	struct _aarch64_ctx head;
	__u64 datap; /* 16-byte aligned pointer to extra space cast to __u64 */
	__u32 size; /* size in bytes of the extra space */
	__u32 __reserved[3];
};

#endif /* _UAPI__ASM_SIGCONTEXT_H */
