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

#ifndef __ASSEMBLY__

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
 *	0x8a0		sve_context (vl <= 64) (optional)
 *	 0x20		extra_context (optional)
 *	 0x10		terminator (null _aarch64_ctx)
 *
 *	0x510		(reserved for future allocation)
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

#define SVE_MAGIC	0x53564501

struct sve_context {
	struct _aarch64_ctx head;
	__u16 vl;
	__u16 __reserved[3];
};

#endif /* !__ASSEMBLY__ */

#include <asm/sve_context.h>

/*
 * The SVE architecture leaves space for future expansion of the
 * vector length beyond its initial architectural limit of 2048 bits
 * (16 quadwords).
 *
 * See linux/Documentation/arm64/sve.rst for a description of the VL/VQ
 * terminology.
 */
#define SVE_VQ_BYTES		__SVE_VQ_BYTES	/* bytes per quadword */

#define SVE_VQ_MIN		__SVE_VQ_MIN
#define SVE_VQ_MAX		__SVE_VQ_MAX

#define SVE_VL_MIN		__SVE_VL_MIN
#define SVE_VL_MAX		__SVE_VL_MAX

#define SVE_NUM_ZREGS		__SVE_NUM_ZREGS
#define SVE_NUM_PREGS		__SVE_NUM_PREGS

#define sve_vl_valid(vl)	__sve_vl_valid(vl)
#define sve_vq_from_vl(vl)	__sve_vq_from_vl(vl)
#define sve_vl_from_vq(vq)	__sve_vl_from_vq(vq)

/*
 * If the SVE registers are currently live for the thread at signal delivery,
 * sve_context.head.size >=
 *	SVE_SIG_CONTEXT_SIZE(sve_vq_from_vl(sve_context.vl))
 * and the register data may be accessed using the SVE_SIG_*() macros.
 *
 * If sve_context.head.size <
 *	SVE_SIG_CONTEXT_SIZE(sve_vq_from_vl(sve_context.vl)),
 * the SVE registers were not live for the thread and no register data
 * is included: in this case, the SVE_SIG_*() macros should not be
 * used except for this check.
 *
 * The same convention applies when returning from a signal: a caller
 * will need to remove or resize the sve_context block if it wants to
 * make the SVE registers live when they were previously non-live or
 * vice-versa.  This may require the the caller to allocate fresh
 * memory and/or move other context blocks in the signal frame.
 *
 * Changing the vector length during signal return is not permitted:
 * sve_context.vl must equal the thread's current vector length when
 * doing a sigreturn.
 *
 *
 * Note: for all these macros, the "vq" argument denotes the SVE
 * vector length in quadwords (i.e., units of 128 bits).
 *
 * The correct way to obtain vq is to use sve_vq_from_vl(vl).  The
 * result is valid if and only if sve_vl_valid(vl) is true.  This is
 * guaranteed for a struct sve_context written by the kernel.
 *
 *
 * Additional macros describe the contents and layout of the payload.
 * For each, SVE_SIG_x_OFFSET(args) is the start offset relative to
 * the start of struct sve_context, and SVE_SIG_x_SIZE(args) is the
 * size in bytes:
 *
 *	x	type				description
 *	-	----				-----------
 *	REGS					the entire SVE context
 *
 *	ZREGS	__uint128_t[SVE_NUM_ZREGS][vq]	all Z-registers
 *	ZREG	__uint128_t[vq]			individual Z-register Zn
 *
 *	PREGS	uint16_t[SVE_NUM_PREGS][vq]	all P-registers
 *	PREG	uint16_t[vq]			individual P-register Pn
 *
 *	FFR	uint16_t[vq]			first-fault status register
 *
 * Additional data might be appended in the future.
 */

#define SVE_SIG_ZREG_SIZE(vq)	__SVE_ZREG_SIZE(vq)
#define SVE_SIG_PREG_SIZE(vq)	__SVE_PREG_SIZE(vq)
#define SVE_SIG_FFR_SIZE(vq)	__SVE_FFR_SIZE(vq)

#define SVE_SIG_REGS_OFFSET					\
	((sizeof(struct sve_context) + (__SVE_VQ_BYTES - 1))	\
		/ __SVE_VQ_BYTES * __SVE_VQ_BYTES)

#define SVE_SIG_ZREGS_OFFSET \
		(SVE_SIG_REGS_OFFSET + __SVE_ZREGS_OFFSET)
#define SVE_SIG_ZREG_OFFSET(vq, n) \
		(SVE_SIG_REGS_OFFSET + __SVE_ZREG_OFFSET(vq, n))
#define SVE_SIG_ZREGS_SIZE(vq) __SVE_ZREGS_SIZE(vq)

#define SVE_SIG_PREGS_OFFSET(vq) \
		(SVE_SIG_REGS_OFFSET + __SVE_PREGS_OFFSET(vq))
#define SVE_SIG_PREG_OFFSET(vq, n) \
		(SVE_SIG_REGS_OFFSET + __SVE_PREG_OFFSET(vq, n))
#define SVE_SIG_PREGS_SIZE(vq) __SVE_PREGS_SIZE(vq)

#define SVE_SIG_FFR_OFFSET(vq) \
		(SVE_SIG_REGS_OFFSET + __SVE_FFR_OFFSET(vq))

#define SVE_SIG_REGS_SIZE(vq) \
		(__SVE_FFR_OFFSET(vq) + __SVE_FFR_SIZE(vq))

#define SVE_SIG_CONTEXT_SIZE(vq) \
		(SVE_SIG_REGS_OFFSET + SVE_SIG_REGS_SIZE(vq))

#endif /* _UAPI__ASM_SIGCONTEXT_H */
