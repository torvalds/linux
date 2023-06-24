/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Based on arch/arm/include/asm/ptrace.h
 *
 * Copyright (C) 1996-2003 Russell King
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
#ifndef _UAPI__ASM_PTRACE_H
#define _UAPI__ASM_PTRACE_H

#include <linux/types.h>

#include <asm/hwcap.h>
#include <asm/sve_context.h>


/*
 * PSR bits
 */
#define PSR_MODE_EL0t	0x00000000
#define PSR_MODE_EL1t	0x00000004
#define PSR_MODE_EL1h	0x00000005
#define PSR_MODE_EL2t	0x00000008
#define PSR_MODE_EL2h	0x00000009
#define PSR_MODE_EL3t	0x0000000c
#define PSR_MODE_EL3h	0x0000000d
#define PSR_MODE_MASK	0x0000000f

/* AArch32 CPSR bits */
#define PSR_MODE32_BIT		0x00000010

/* AArch64 SPSR bits */
#define PSR_F_BIT	0x00000040
#define PSR_I_BIT	0x00000080
#define PSR_A_BIT	0x00000100
#define PSR_D_BIT	0x00000200
#define PSR_BTYPE_MASK	0x00000c00
#define PSR_SSBS_BIT	0x00001000
#define PSR_PAN_BIT	0x00400000
#define PSR_UAO_BIT	0x00800000
#define PSR_DIT_BIT	0x01000000
#define PSR_TCO_BIT	0x02000000
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000

#define PSR_BTYPE_SHIFT		10

/*
 * Groups of PSR bits
 */
#define PSR_f		0xff000000	/* Flags		*/
#define PSR_s		0x00ff0000	/* Status		*/
#define PSR_x		0x0000ff00	/* Extension		*/
#define PSR_c		0x000000ff	/* Control		*/

/* Convenience names for the values of PSTATE.BTYPE */
#define PSR_BTYPE_NONE		(0b00 << PSR_BTYPE_SHIFT)
#define PSR_BTYPE_JC		(0b01 << PSR_BTYPE_SHIFT)
#define PSR_BTYPE_C		(0b10 << PSR_BTYPE_SHIFT)
#define PSR_BTYPE_J		(0b11 << PSR_BTYPE_SHIFT)

/* syscall emulation path in ptrace */
#define PTRACE_SYSEMU		  31
#define PTRACE_SYSEMU_SINGLESTEP  32
/* MTE allocation tag access */
#define PTRACE_PEEKMTETAGS	  33
#define PTRACE_POKEMTETAGS	  34

#ifndef __ASSEMBLY__

/*
 * User structures for general purpose, floating point and debug registers.
 */
struct user_pt_regs {
	__u64		regs[31];
	__u64		sp;
	__u64		pc;
	__u64		pstate;
};

struct user_fpsimd_state {
	__uint128_t	vregs[32];
	__u32		fpsr;
	__u32		fpcr;
	__u32		__reserved[2];
};

struct user_hwdebug_state {
	__u32		dbg_info;
	__u32		pad;
	struct {
		__u64	addr;
		__u32	ctrl;
		__u32	pad;
	}		dbg_regs[16];
};

/* SVE/FP/SIMD state (NT_ARM_SVE & NT_ARM_SSVE) */

struct user_sve_header {
	__u32 size; /* total meaningful regset content in bytes */
	__u32 max_size; /* maxmium possible size for this thread */
	__u16 vl; /* current vector length */
	__u16 max_vl; /* maximum possible vector length */
	__u16 flags;
	__u16 __reserved;
};

/* Definitions for user_sve_header.flags: */
#define SVE_PT_REGS_MASK		(1 << 0)

#define SVE_PT_REGS_FPSIMD		0
#define SVE_PT_REGS_SVE			SVE_PT_REGS_MASK

/*
 * Common SVE_PT_* flags:
 * These must be kept in sync with prctl interface in <linux/prctl.h>
 */
#define SVE_PT_VL_INHERIT		((1 << 17) /* PR_SVE_VL_INHERIT */ >> 16)
#define SVE_PT_VL_ONEXEC		((1 << 18) /* PR_SVE_SET_VL_ONEXEC */ >> 16)


/*
 * The remainder of the SVE state follows struct user_sve_header.  The
 * total size of the SVE state (including header) depends on the
 * metadata in the header:  SVE_PT_SIZE(vq, flags) gives the total size
 * of the state in bytes, including the header.
 *
 * Refer to <asm/sigcontext.h> for details of how to pass the correct
 * "vq" argument to these macros.
 */

/* Offset from the start of struct user_sve_header to the register data */
#define SVE_PT_REGS_OFFSET						\
	((sizeof(struct user_sve_header) + (__SVE_VQ_BYTES - 1))	\
		/ __SVE_VQ_BYTES * __SVE_VQ_BYTES)

/*
 * The register data content and layout depends on the value of the
 * flags field.
 */

/*
 * (flags & SVE_PT_REGS_MASK) == SVE_PT_REGS_FPSIMD case:
 *
 * The payload starts at offset SVE_PT_FPSIMD_OFFSET, and is of type
 * struct user_fpsimd_state.  Additional data might be appended in the
 * future: use SVE_PT_FPSIMD_SIZE(vq, flags) to compute the total size.
 * SVE_PT_FPSIMD_SIZE(vq, flags) will never be less than
 * sizeof(struct user_fpsimd_state).
 */

#define SVE_PT_FPSIMD_OFFSET		SVE_PT_REGS_OFFSET

#define SVE_PT_FPSIMD_SIZE(vq, flags)	(sizeof(struct user_fpsimd_state))

/*
 * (flags & SVE_PT_REGS_MASK) == SVE_PT_REGS_SVE case:
 *
 * The payload starts at offset SVE_PT_SVE_OFFSET, and is of size
 * SVE_PT_SVE_SIZE(vq, flags).
 *
 * Additional macros describe the contents and layout of the payload.
 * For each, SVE_PT_SVE_x_OFFSET(args) is the start offset relative to
 * the start of struct user_sve_header, and SVE_PT_SVE_x_SIZE(args) is
 * the size in bytes:
 *
 *	x	type				description
 *	-	----				-----------
 *	ZREGS		\
 *	ZREG		|
 *	PREGS		| refer to <asm/sigcontext.h>
 *	PREG		|
 *	FFR		/
 *
 *	FPSR	uint32_t			FPSR
 *	FPCR	uint32_t			FPCR
 *
 * Additional data might be appended in the future.
 *
 * The Z-, P- and FFR registers are represented in memory in an endianness-
 * invariant layout which differs from the layout used for the FPSIMD
 * V-registers on big-endian systems: see sigcontext.h for more explanation.
 */

#define SVE_PT_SVE_ZREG_SIZE(vq)	__SVE_ZREG_SIZE(vq)
#define SVE_PT_SVE_PREG_SIZE(vq)	__SVE_PREG_SIZE(vq)
#define SVE_PT_SVE_FFR_SIZE(vq)		__SVE_FFR_SIZE(vq)
#define SVE_PT_SVE_FPSR_SIZE		sizeof(__u32)
#define SVE_PT_SVE_FPCR_SIZE		sizeof(__u32)

#define SVE_PT_SVE_OFFSET		SVE_PT_REGS_OFFSET

#define SVE_PT_SVE_ZREGS_OFFSET \
	(SVE_PT_REGS_OFFSET + __SVE_ZREGS_OFFSET)
#define SVE_PT_SVE_ZREG_OFFSET(vq, n) \
	(SVE_PT_REGS_OFFSET + __SVE_ZREG_OFFSET(vq, n))
#define SVE_PT_SVE_ZREGS_SIZE(vq) \
	(SVE_PT_SVE_ZREG_OFFSET(vq, __SVE_NUM_ZREGS) - SVE_PT_SVE_ZREGS_OFFSET)

#define SVE_PT_SVE_PREGS_OFFSET(vq) \
	(SVE_PT_REGS_OFFSET + __SVE_PREGS_OFFSET(vq))
#define SVE_PT_SVE_PREG_OFFSET(vq, n) \
	(SVE_PT_REGS_OFFSET + __SVE_PREG_OFFSET(vq, n))
#define SVE_PT_SVE_PREGS_SIZE(vq) \
	(SVE_PT_SVE_PREG_OFFSET(vq, __SVE_NUM_PREGS) - \
		SVE_PT_SVE_PREGS_OFFSET(vq))

/* For streaming mode SVE (SSVE) FFR must be read and written as zero */
#define SVE_PT_SVE_FFR_OFFSET(vq) \
	(SVE_PT_REGS_OFFSET + __SVE_FFR_OFFSET(vq))

#define SVE_PT_SVE_FPSR_OFFSET(vq)				\
	((SVE_PT_SVE_FFR_OFFSET(vq) + SVE_PT_SVE_FFR_SIZE(vq) +	\
			(__SVE_VQ_BYTES - 1))			\
		/ __SVE_VQ_BYTES * __SVE_VQ_BYTES)
#define SVE_PT_SVE_FPCR_OFFSET(vq) \
	(SVE_PT_SVE_FPSR_OFFSET(vq) + SVE_PT_SVE_FPSR_SIZE)

/*
 * Any future extension appended after FPCR must be aligned to the next
 * 128-bit boundary.
 */

#define SVE_PT_SVE_SIZE(vq, flags)					\
	((SVE_PT_SVE_FPCR_OFFSET(vq) + SVE_PT_SVE_FPCR_SIZE		\
			- SVE_PT_SVE_OFFSET + (__SVE_VQ_BYTES - 1))	\
		/ __SVE_VQ_BYTES * __SVE_VQ_BYTES)

#define SVE_PT_SIZE(vq, flags)						  \
	 (((flags) & SVE_PT_REGS_MASK) == SVE_PT_REGS_SVE ?		  \
		  SVE_PT_SVE_OFFSET + SVE_PT_SVE_SIZE(vq, flags)	  \
		: ((((flags) & SVE_PT_REGS_MASK) == SVE_PT_REGS_FPSIMD ?  \
		    SVE_PT_FPSIMD_OFFSET + SVE_PT_FPSIMD_SIZE(vq, flags) \
		  : SVE_PT_REGS_OFFSET)))

/* pointer authentication masks (NT_ARM_PAC_MASK) */

struct user_pac_mask {
	__u64		data_mask;
	__u64		insn_mask;
};

/* pointer authentication keys (NT_ARM_PACA_KEYS, NT_ARM_PACG_KEYS) */

struct user_pac_address_keys {
	__uint128_t	apiakey;
	__uint128_t	apibkey;
	__uint128_t	apdakey;
	__uint128_t	apdbkey;
};

struct user_pac_generic_keys {
	__uint128_t	apgakey;
};

/* ZA state (NT_ARM_ZA) */

struct user_za_header {
	__u32 size; /* total meaningful regset content in bytes */
	__u32 max_size; /* maxmium possible size for this thread */
	__u16 vl; /* current vector length */
	__u16 max_vl; /* maximum possible vector length */
	__u16 flags;
	__u16 __reserved;
};

/*
 * Common ZA_PT_* flags:
 * These must be kept in sync with prctl interface in <linux/prctl.h>
 */
#define ZA_PT_VL_INHERIT		((1 << 17) /* PR_SME_VL_INHERIT */ >> 16)
#define ZA_PT_VL_ONEXEC			((1 << 18) /* PR_SME_SET_VL_ONEXEC */ >> 16)


/*
 * The remainder of the ZA state follows struct user_za_header.  The
 * total size of the ZA state (including header) depends on the
 * metadata in the header:  ZA_PT_SIZE(vq, flags) gives the total size
 * of the state in bytes, including the header.
 *
 * Refer to <asm/sigcontext.h> for details of how to pass the correct
 * "vq" argument to these macros.
 */

/* Offset from the start of struct user_za_header to the register data */
#define ZA_PT_ZA_OFFSET						\
	((sizeof(struct user_za_header) + (__SVE_VQ_BYTES - 1))	\
		/ __SVE_VQ_BYTES * __SVE_VQ_BYTES)

/*
 * The payload starts at offset ZA_PT_ZA_OFFSET, and is of size
 * ZA_PT_ZA_SIZE(vq, flags).
 *
 * The ZA array is stored as a sequence of horizontal vectors ZAV of SVL/8
 * bytes each, starting from vector 0.
 *
 * Additional data might be appended in the future.
 *
 * The ZA matrix is represented in memory in an endianness-invariant layout
 * which differs from the layout used for the FPSIMD V-registers on big-endian
 * systems: see sigcontext.h for more explanation.
 */

#define ZA_PT_ZAV_OFFSET(vq, n) \
	(ZA_PT_ZA_OFFSET + ((vq * __SVE_VQ_BYTES) * n))

#define ZA_PT_ZA_SIZE(vq) ((vq * __SVE_VQ_BYTES) * (vq * __SVE_VQ_BYTES))

#define ZA_PT_SIZE(vq)						\
	(ZA_PT_ZA_OFFSET + ZA_PT_ZA_SIZE(vq))

#endif /* __ASSEMBLY__ */

#endif /* _UAPI__ASM_PTRACE_H */
