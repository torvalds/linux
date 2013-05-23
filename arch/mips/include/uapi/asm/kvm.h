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

#define __KVM_MIPS

#define N_MIPS_COPROC_REGS      32
#define N_MIPS_COPROC_SEL   	8

/* for KVM_GET_REGS and KVM_SET_REGS */
struct kvm_regs {
	__u32 gprs[32];
	__u32 hi;
	__u32 lo;
	__u32 pc;

	__u32 cp0reg[N_MIPS_COPROC_REGS][N_MIPS_COPROC_SEL];
};

/* for KVM_GET_SREGS and KVM_SET_SREGS */
struct kvm_sregs {
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

struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

struct kvm_mips_interrupt {
	/* in */
	__u32 cpu;
	__u32 irq;
};

/* definition of registers in kvm_run */
struct kvm_sync_regs {
};

#endif /* __LINUX_KVM_MIPS_H */
