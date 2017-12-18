/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2013 Cavium, Inc.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#ifndef __LINUX_KVM_MIPS_H
#define __LINUX_KVM_MIPS_H

#include <linux/types.h>

/*
 * KVM MIPS specific structures and definitions.
 *
 * Some parts derived from the x86 version of this file.
 */

#define __KVM_HAVE_READONLY_MEM

#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

/*
 * for KVM_GET_REGS and KVM_SET_REGS
 *
 * If Config[AT] is zero (32-bit CPU), the register contents are
 * stored in the lower 32-bits of the struct kvm_regs fields and sign
 * extended to 64-bits.
 */
struct kvm_regs {
	/* out (KVM_GET_REGS) / in (KVM_SET_REGS) */
	__u64 gpr[32];
	__u64 hi;
	__u64 lo;
	__u64 pc;
};

/*
 * for KVM_GET_FPU and KVM_SET_FPU
 */
struct kvm_fpu {
};


/*
 * For MIPS, we use KVM_SET_ONE_REG and KVM_GET_ONE_REG to access various
 * registers.  The id field is broken down as follows:
 *
 *  bits[63..52] - As per linux/kvm.h
 *  bits[51..32] - Must be zero.
 *  bits[31..16] - Register set.
 *
 * Register set = 0: GP registers from kvm_regs (see definitions below).
 *
 * Register set = 1: CP0 registers.
 *  bits[15..8]  - COP0 register set.
 *
 *  COP0 register set = 0: Main CP0 registers.
 *   bits[7..3]   - Register 'rd'  index.
 *   bits[2..0]   - Register 'sel' index.
 *
 *  COP0 register set = 1: MAARs.
 *   bits[7..0]   - MAAR index.
 *
 * Register set = 2: KVM specific registers (see definitions below).
 *
 * Register set = 3: FPU / MSA registers (see definitions below).
 *
 * Other sets registers may be added in the future.  Each set would
 * have its own identifier in bits[31..16].
 */

#define KVM_REG_MIPS_GP		(KVM_REG_MIPS | 0x0000000000000000ULL)
#define KVM_REG_MIPS_CP0	(KVM_REG_MIPS | 0x0000000000010000ULL)
#define KVM_REG_MIPS_KVM	(KVM_REG_MIPS | 0x0000000000020000ULL)
#define KVM_REG_MIPS_FPU	(KVM_REG_MIPS | 0x0000000000030000ULL)


/*
 * KVM_REG_MIPS_GP - General purpose registers from kvm_regs.
 */

#define KVM_REG_MIPS_R0		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  0)
#define KVM_REG_MIPS_R1		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  1)
#define KVM_REG_MIPS_R2		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  2)
#define KVM_REG_MIPS_R3		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  3)
#define KVM_REG_MIPS_R4		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  4)
#define KVM_REG_MIPS_R5		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  5)
#define KVM_REG_MIPS_R6		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  6)
#define KVM_REG_MIPS_R7		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  7)
#define KVM_REG_MIPS_R8		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  8)
#define KVM_REG_MIPS_R9		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 |  9)
#define KVM_REG_MIPS_R10	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 10)
#define KVM_REG_MIPS_R11	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 11)
#define KVM_REG_MIPS_R12	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 12)
#define KVM_REG_MIPS_R13	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 13)
#define KVM_REG_MIPS_R14	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 14)
#define KVM_REG_MIPS_R15	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 15)
#define KVM_REG_MIPS_R16	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 16)
#define KVM_REG_MIPS_R17	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 17)
#define KVM_REG_MIPS_R18	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 18)
#define KVM_REG_MIPS_R19	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 19)
#define KVM_REG_MIPS_R20	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 20)
#define KVM_REG_MIPS_R21	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 21)
#define KVM_REG_MIPS_R22	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 22)
#define KVM_REG_MIPS_R23	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 23)
#define KVM_REG_MIPS_R24	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 24)
#define KVM_REG_MIPS_R25	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 25)
#define KVM_REG_MIPS_R26	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 26)
#define KVM_REG_MIPS_R27	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 27)
#define KVM_REG_MIPS_R28	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 28)
#define KVM_REG_MIPS_R29	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 29)
#define KVM_REG_MIPS_R30	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 30)
#define KVM_REG_MIPS_R31	(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 31)

#define KVM_REG_MIPS_HI		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 32)
#define KVM_REG_MIPS_LO		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 33)
#define KVM_REG_MIPS_PC		(KVM_REG_MIPS_GP | KVM_REG_SIZE_U64 | 34)


/*
 * KVM_REG_MIPS_CP0 - Coprocessor 0 registers.
 */

#define KVM_REG_MIPS_MAAR	(KVM_REG_MIPS_CP0 | (1 << 8))
#define KVM_REG_MIPS_CP0_MAAR(n)	(KVM_REG_MIPS_MAAR | \
					 KVM_REG_SIZE_U64 | (n))


/*
 * KVM_REG_MIPS_KVM - KVM specific control registers.
 */

/*
 * CP0_Count control
 * DC:    Set 0: Master disable CP0_Count and set COUNT_RESUME to now
 *        Set 1: Master re-enable CP0_Count with unchanged bias, handling timer
 *               interrupts since COUNT_RESUME
 *        This can be used to freeze the timer to get a consistent snapshot of
 *        the CP0_Count and timer interrupt pending state, while also resuming
 *        safely without losing time or guest timer interrupts.
 * Other: Reserved, do not change.
 */
#define KVM_REG_MIPS_COUNT_CTL	    (KVM_REG_MIPS_KVM | KVM_REG_SIZE_U64 | 0)
#define KVM_REG_MIPS_COUNT_CTL_DC	0x00000001

/*
 * CP0_Count resume monotonic nanoseconds
 * The monotonic nanosecond time of the last set of COUNT_CTL.DC (master
 * disable). Any reads and writes of Count related registers while
 * COUNT_CTL.DC=1 will appear to occur at this time. When COUNT_CTL.DC is
 * cleared again (master enable) any timer interrupts since this time will be
 * emulated.
 * Modifications to times in the future are rejected.
 */
#define KVM_REG_MIPS_COUNT_RESUME   (KVM_REG_MIPS_KVM | KVM_REG_SIZE_U64 | 1)
/*
 * CP0_Count rate in Hz
 * Specifies the rate of the CP0_Count timer in Hz. Modifications occur without
 * discontinuities in CP0_Count.
 */
#define KVM_REG_MIPS_COUNT_HZ	    (KVM_REG_MIPS_KVM | KVM_REG_SIZE_U64 | 2)


/*
 * KVM_REG_MIPS_FPU - Floating Point and MIPS SIMD Architecture (MSA) registers.
 *
 *  bits[15..8]  - Register subset (see definitions below).
 *  bits[7..5]   - Must be zero.
 *  bits[4..0]   - Register number within register subset.
 */

#define KVM_REG_MIPS_FPR	(KVM_REG_MIPS_FPU | 0x0000000000000000ULL)
#define KVM_REG_MIPS_FCR	(KVM_REG_MIPS_FPU | 0x0000000000000100ULL)
#define KVM_REG_MIPS_MSACR	(KVM_REG_MIPS_FPU | 0x0000000000000200ULL)

/*
 * KVM_REG_MIPS_FPR - Floating point / Vector registers.
 */
#define KVM_REG_MIPS_FPR_32(n)	(KVM_REG_MIPS_FPR | KVM_REG_SIZE_U32  | (n))
#define KVM_REG_MIPS_FPR_64(n)	(KVM_REG_MIPS_FPR | KVM_REG_SIZE_U64  | (n))
#define KVM_REG_MIPS_VEC_128(n)	(KVM_REG_MIPS_FPR | KVM_REG_SIZE_U128 | (n))

/*
 * KVM_REG_MIPS_FCR - Floating point control registers.
 */
#define KVM_REG_MIPS_FCR_IR	(KVM_REG_MIPS_FCR | KVM_REG_SIZE_U32 |  0)
#define KVM_REG_MIPS_FCR_CSR	(KVM_REG_MIPS_FCR | KVM_REG_SIZE_U32 | 31)

/*
 * KVM_REG_MIPS_MSACR - MIPS SIMD Architecture (MSA) control registers.
 */
#define KVM_REG_MIPS_MSA_IR	 (KVM_REG_MIPS_MSACR | KVM_REG_SIZE_U32 |  0)
#define KVM_REG_MIPS_MSA_CSR	 (KVM_REG_MIPS_MSACR | KVM_REG_SIZE_U32 |  1)


/*
 * KVM MIPS specific structures and definitions
 *
 */
struct kvm_debug_exit_arch {
	__u64 epc;
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

/* definition of registers in kvm_run */
struct kvm_sync_regs {
};

/* dummy definition */
struct kvm_sregs {
};

struct kvm_mips_interrupt {
	/* in */
	__u32 cpu;
	__u32 irq;
};

#endif /* __LINUX_KVM_MIPS_H */
