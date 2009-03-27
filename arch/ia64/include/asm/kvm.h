#ifndef __ASM_IA64_KVM_H
#define __ASM_IA64_KVM_H

/*
 * kvm structure definitions  for ia64
 *
 * Copyright (C) 2007 Xiantao Zhang <xiantao.zhang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/types.h>
#include <linux/ioctl.h>

/* Select x86 specific features in <linux/kvm.h> */
#define __KVM_HAVE_IOAPIC
#define __KVM_HAVE_DEVICE_ASSIGNMENT

/* Architectural interrupt line count. */
#define KVM_NR_INTERRUPTS 256

#define KVM_IOAPIC_NUM_PINS  48

struct kvm_ioapic_state {
	__u64 base_address;
	__u32 ioregsel;
	__u32 id;
	__u32 irr;
	__u32 pad;
	union {
		__u64 bits;
		struct {
			__u8 vector;
			__u8 delivery_mode:3;
			__u8 dest_mode:1;
			__u8 delivery_status:1;
			__u8 polarity:1;
			__u8 remote_irr:1;
			__u8 trig_mode:1;
			__u8 mask:1;
			__u8 reserve:7;
			__u8 reserved[4];
			__u8 dest_id;
		} fields;
	} redirtbl[KVM_IOAPIC_NUM_PINS];
};

#define KVM_IRQCHIP_PIC_MASTER   0
#define KVM_IRQCHIP_PIC_SLAVE    1
#define KVM_IRQCHIP_IOAPIC       2

#define KVM_CONTEXT_SIZE	8*1024

struct kvm_fpreg {
	union {
		unsigned long bits[2];
		long double __dummy;	/* force 16-byte alignment */
	} u;
};

union context {
	/* 8K size */
	char	dummy[KVM_CONTEXT_SIZE];
	struct {
		unsigned long       psr;
		unsigned long       pr;
		unsigned long       caller_unat;
		unsigned long       pad;
		unsigned long       gr[32];
		unsigned long       ar[128];
		unsigned long       br[8];
		unsigned long       cr[128];
		unsigned long       rr[8];
		unsigned long       ibr[8];
		unsigned long       dbr[8];
		unsigned long       pkr[8];
		struct kvm_fpreg   fr[128];
	};
};

struct thash_data {
	union {
		struct {
			unsigned long p    :  1; /* 0 */
			unsigned long rv1  :  1; /* 1 */
			unsigned long ma   :  3; /* 2-4 */
			unsigned long a    :  1; /* 5 */
			unsigned long d    :  1; /* 6 */
			unsigned long pl   :  2; /* 7-8 */
			unsigned long ar   :  3; /* 9-11 */
			unsigned long ppn  : 38; /* 12-49 */
			unsigned long rv2  :  2; /* 50-51 */
			unsigned long ed   :  1; /* 52 */
			unsigned long ig1  : 11; /* 53-63 */
		};
		struct {
			unsigned long __rv1 : 53;     /* 0-52 */
			unsigned long contiguous : 1; /*53 */
			unsigned long tc : 1;         /* 54 TR or TC */
			unsigned long cl : 1;
			/* 55 I side or D side cache line */
			unsigned long len  :  4;      /* 56-59 */
			unsigned long io  : 1;	/* 60 entry is for io or not */
			unsigned long nomap : 1;
			/* 61 entry cann't be inserted into machine TLB.*/
			unsigned long checked : 1;
			/* 62 for VTLB/VHPT sanity check */
			unsigned long invalid : 1;
			/* 63 invalid entry */
		};
		unsigned long page_flags;
	};                  /* same for VHPT and TLB */

	union {
		struct {
			unsigned long rv3  :  2;
			unsigned long ps   :  6;
			unsigned long key  : 24;
			unsigned long rv4  : 32;
		};
		unsigned long itir;
	};
	union {
		struct {
			unsigned long ig2  :  12;
			unsigned long vpn  :  49;
			unsigned long vrn  :   3;
		};
		unsigned long ifa;
		unsigned long vadr;
		struct {
			unsigned long tag  :  63;
			unsigned long ti   :  1;
		};
		unsigned long etag;
	};
	union {
		struct thash_data *next;
		unsigned long rid;
		unsigned long gpaddr;
	};
};

#define	NITRS	8
#define NDTRS	8

struct saved_vpd {
	unsigned long  vhpi;
	unsigned long  vgr[16];
	unsigned long  vbgr[16];
	unsigned long  vnat;
	unsigned long  vbnat;
	unsigned long  vcpuid[5];
	unsigned long  vpsr;
	unsigned long  vpr;
	union {
		unsigned long  vcr[128];
		struct {
			unsigned long dcr;
			unsigned long itm;
			unsigned long iva;
			unsigned long rsv1[5];
			unsigned long pta;
			unsigned long rsv2[7];
			unsigned long ipsr;
			unsigned long isr;
			unsigned long rsv3;
			unsigned long iip;
			unsigned long ifa;
			unsigned long itir;
			unsigned long iipa;
			unsigned long ifs;
			unsigned long iim;
			unsigned long iha;
			unsigned long rsv4[38];
			unsigned long lid;
			unsigned long ivr;
			unsigned long tpr;
			unsigned long eoi;
			unsigned long irr[4];
			unsigned long itv;
			unsigned long pmv;
			unsigned long cmcv;
			unsigned long rsv5[5];
			unsigned long lrr0;
			unsigned long lrr1;
			unsigned long rsv6[46];
		};
	};
};

struct kvm_regs {
	struct saved_vpd vpd;
	/*Arch-regs*/
	int mp_state;
	unsigned long vmm_rr;
	/* TR and TC.  */
	struct thash_data itrs[NITRS];
	struct thash_data dtrs[NDTRS];
	/* Bit is set if there is a tr/tc for the region.  */
	unsigned char itr_regions;
	unsigned char dtr_regions;
	unsigned char tc_regions;

	char irq_check;
	unsigned long saved_itc;
	unsigned long itc_check;
	unsigned long timer_check;
	unsigned long timer_pending;
	unsigned long last_itc;

	unsigned long vrr[8];
	unsigned long ibr[8];
	unsigned long dbr[8];
	unsigned long insvc[4];		/* Interrupt in service.  */
	unsigned long xtp;

	unsigned long metaphysical_rr0; /* from kvm_arch (so is pinned) */
	unsigned long metaphysical_rr4;	/* from kvm_arch (so is pinned) */
	unsigned long metaphysical_saved_rr0; /* from kvm_arch          */
	unsigned long metaphysical_saved_rr4; /* from kvm_arch          */
	unsigned long fp_psr;       /*used for lazy float register */
	unsigned long saved_gp;
	/*for phycial  emulation */

	union context saved_guest;

	unsigned long reserved[64];	/* for future use */
};

struct kvm_sregs {
};

struct kvm_fpu {
};

#define KVM_IA64_VCPU_STACK_SHIFT	16
#define KVM_IA64_VCPU_STACK_SIZE	(1UL << KVM_IA64_VCPU_STACK_SHIFT)

struct kvm_ia64_vcpu_stack {
	unsigned char stack[KVM_IA64_VCPU_STACK_SIZE];
};

struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

#endif
