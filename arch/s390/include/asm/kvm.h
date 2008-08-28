#ifndef __LINUX_KVM_S390_H
#define __LINUX_KVM_S390_H

/*
 * asm-s390/kvm.h - KVM s390 specific structures and definitions
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Christian Borntraeger <borntraeger@de.ibm.com>
 */
#include <asm/types.h>

/* for KVM_GET_IRQCHIP and KVM_SET_IRQCHIP */
struct kvm_pic_state {
	/* no PIC for s390 */
};

struct kvm_ioapic_state {
	/* no IOAPIC for s390 */
};

/* for KVM_GET_REGS and KVM_SET_REGS */
struct kvm_regs {
	/* general purpose regs for s390 */
	__u64 gprs[16];
};

/* for KVM_GET_SREGS and KVM_SET_SREGS */
struct kvm_sregs {
	__u32 acrs[16];
	__u64 crs[16];
};

/* for KVM_GET_FPU and KVM_SET_FPU */
struct kvm_fpu {
	__u32 fpc;
	__u64 fprs[16];
};

#endif
