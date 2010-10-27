/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __LINUX_KVM_POWERPC_H
#define __LINUX_KVM_POWERPC_H

#include <linux/types.h>

struct kvm_regs {
	__u64 pc;
	__u64 cr;
	__u64 ctr;
	__u64 lr;
	__u64 xer;
	__u64 msr;
	__u64 srr0;
	__u64 srr1;
	__u64 pid;

	__u64 sprg0;
	__u64 sprg1;
	__u64 sprg2;
	__u64 sprg3;
	__u64 sprg4;
	__u64 sprg5;
	__u64 sprg6;
	__u64 sprg7;

	__u64 gpr[32];
};

struct kvm_sregs {
	__u32 pvr;
	union {
		struct {
			__u64 sdr1;
			struct {
				struct {
					__u64 slbe;
					__u64 slbv;
				} slb[64];
			} ppc64;
			struct {
				__u32 sr[16];
				__u64 ibat[8]; 
				__u64 dbat[8]; 
			} ppc32;
		} s;
		__u8 pad[1020];
	} u;
};

struct kvm_fpu {
	__u64 fpr[32];
};

struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

#define KVM_REG_MASK		0x001f
#define KVM_REG_EXT_MASK	0xffe0
#define KVM_REG_GPR		0x0000
#define KVM_REG_FPR		0x0020
#define KVM_REG_QPR		0x0040
#define KVM_REG_FQPR		0x0060

#define KVM_INTERRUPT_SET	-1U
#define KVM_INTERRUPT_UNSET	-2U

#endif /* __LINUX_KVM_POWERPC_H */
