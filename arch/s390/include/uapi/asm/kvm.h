#ifndef __LINUX_KVM_S390_H
#define __LINUX_KVM_S390_H
/*
 * KVM s390 specific structures and definitions
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
#include <linux/types.h>

#define __KVM_S390

/* Device control API: s390-specific devices */
#define KVM_DEV_FLIC_GET_ALL_IRQS	1
#define KVM_DEV_FLIC_ENQUEUE		2
#define KVM_DEV_FLIC_CLEAR_IRQS		3
#define KVM_DEV_FLIC_APF_ENABLE		4
#define KVM_DEV_FLIC_APF_DISABLE_WAIT	5
/*
 * We can have up to 4*64k pending subchannels + 8 adapter interrupts,
 * as well as up  to ASYNC_PF_PER_VCPU*KVM_MAX_VCPUS pfault done interrupts.
 * There are also sclp and machine checks. This gives us
 * sizeof(kvm_s390_irq)*(4*65536+8+64*64+1+1) = 72 * 266250 = 19170000
 * Lets round up to 8192 pages.
 */
#define KVM_S390_MAX_FLOAT_IRQS	266250
#define KVM_S390_FLIC_MAX_BUFFER	0x2000000

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

struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

#define KVM_SYNC_PREFIX (1UL << 0)
#define KVM_SYNC_GPRS   (1UL << 1)
#define KVM_SYNC_ACRS   (1UL << 2)
#define KVM_SYNC_CRS    (1UL << 3)
/* definition of registers in kvm_run */
struct kvm_sync_regs {
	__u64 prefix;	/* prefix register */
	__u64 gprs[16];	/* general purpose registers */
	__u32 acrs[16];	/* access registers */
	__u64 crs[16];	/* control registers */
};

#define KVM_REG_S390_TODPR	(KVM_REG_S390 | KVM_REG_SIZE_U32 | 0x1)
#define KVM_REG_S390_EPOCHDIFF	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x2)
#define KVM_REG_S390_CPU_TIMER  (KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x3)
#define KVM_REG_S390_CLOCK_COMP (KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x4)
#define KVM_REG_S390_PFTOKEN	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x5)
#define KVM_REG_S390_PFCOMPARE	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x6)
#define KVM_REG_S390_PFSELECT	(KVM_REG_S390 | KVM_REG_SIZE_U64 | 0x7)
#endif
