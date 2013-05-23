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
 *
 * If Status[FR] is zero (32-bit FPU), the upper 32-bits of the FPRs
 * are zero filled.
 */
struct kvm_fpu {
	__u64 fpr[32];
	__u32 fir;
	__u32 fccr;
	__u32 fexr;
	__u32 fenr;
	__u32 fcsr;
	__u32 pad;
};


/*
 * For MIPS, we use KVM_SET_ONE_REG and KVM_GET_ONE_REG to access CP0
 * registers.  The id field is broken down as follows:
 *
 *  bits[2..0]   - Register 'sel' index.
 *  bits[7..3]   - Register 'rd'  index.
 *  bits[15..8]  - Must be zero.
 *  bits[63..16] - 1 -> CP0 registers.
 *
 * Other sets registers may be added in the future.  Each set would
 * have its own identifier in bits[63..16].
 *
 * The addr field of struct kvm_one_reg must point to an aligned
 * 64-bit wide location.  For registers that are narrower than
 * 64-bits, the value is stored in the low order bits of the location,
 * and sign extended to 64-bits.
 *
 * The registers defined in struct kvm_regs are also accessible, the
 * id values for these are below.
 */

#define KVM_REG_MIPS_R0 0
#define KVM_REG_MIPS_R1 1
#define KVM_REG_MIPS_R2 2
#define KVM_REG_MIPS_R3 3
#define KVM_REG_MIPS_R4 4
#define KVM_REG_MIPS_R5 5
#define KVM_REG_MIPS_R6 6
#define KVM_REG_MIPS_R7 7
#define KVM_REG_MIPS_R8 8
#define KVM_REG_MIPS_R9 9
#define KVM_REG_MIPS_R10 10
#define KVM_REG_MIPS_R11 11
#define KVM_REG_MIPS_R12 12
#define KVM_REG_MIPS_R13 13
#define KVM_REG_MIPS_R14 14
#define KVM_REG_MIPS_R15 15
#define KVM_REG_MIPS_R16 16
#define KVM_REG_MIPS_R17 17
#define KVM_REG_MIPS_R18 18
#define KVM_REG_MIPS_R19 19
#define KVM_REG_MIPS_R20 20
#define KVM_REG_MIPS_R21 21
#define KVM_REG_MIPS_R22 22
#define KVM_REG_MIPS_R23 23
#define KVM_REG_MIPS_R24 24
#define KVM_REG_MIPS_R25 25
#define KVM_REG_MIPS_R26 26
#define KVM_REG_MIPS_R27 27
#define KVM_REG_MIPS_R28 28
#define KVM_REG_MIPS_R29 29
#define KVM_REG_MIPS_R30 30
#define KVM_REG_MIPS_R31 31

#define KVM_REG_MIPS_HI 32
#define KVM_REG_MIPS_LO 33
#define KVM_REG_MIPS_PC 34

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
