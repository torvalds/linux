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

#include <asm/types.h>

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
};

struct kvm_fpu {
	__u64 fpr[32];
};

#endif /* __LINUX_KVM_POWERPC_H */
