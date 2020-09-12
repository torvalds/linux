/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_ASM_H__
#define __ASM_KVM_BOOK3S_ASM_H__

/* XICS ICP register offsets */
#define XICS_XIRR		4
#define XICS_MFRR		0xc
#define XICS_IPI		2	/* interrupt source # for IPIs */

/* LPIDs we support with this build -- runtime limit may be lower */
#define KVMPPC_NR_LPIDS			(LPID_RSVD + 1)

/* Maximum number of threads per physical core */
#define MAX_SMT_THREADS		8

/* Maximum number of subcores per physical core */
#define MAX_SUBCORES		4

#ifdef __ASSEMBLY__

#ifdef CONFIG_KVM_BOOK3S_HANDLER

#include <asm/kvm_asm.h>

.macro DO_KVM intno
	.if (\intno == BOOK3S_INTERRUPT_SYSTEM_RESET) || \
	    (\intno == BOOK3S_INTERRUPT_MACHINE_CHECK) || \
	    (\intno == BOOK3S_INTERRUPT_DATA_STORAGE) || \
	    (\intno == BOOK3S_INTERRUPT_INST_STORAGE) || \
	    (\intno == BOOK3S_INTERRUPT_DATA_SEGMENT) || \
	    (\intno == BOOK3S_INTERRUPT_INST_SEGMENT) || \
	    (\intno == BOOK3S_INTERRUPT_EXTERNAL) || \
	    (\intno == BOOK3S_INTERRUPT_EXTERNAL_HV) || \
	    (\intno == BOOK3S_INTERRUPT_ALIGNMENT) || \
	    (\intno == BOOK3S_INTERRUPT_PROGRAM) || \
	    (\intno == BOOK3S_INTERRUPT_FP_UNAVAIL) || \
	    (\intno == BOOK3S_INTERRUPT_DECREMENTER) || \
	    (\intno == BOOK3S_INTERRUPT_SYSCALL) || \
	    (\intno == BOOK3S_INTERRUPT_TRACE) || \
	    (\intno == BOOK3S_INTERRUPT_PERFMON) || \
	    (\intno == BOOK3S_INTERRUPT_ALTIVEC) || \
	    (\intno == BOOK3S_INTERRUPT_VSX)

	b	kvmppc_trampoline_\intno
kvmppc_resume_\intno:

	.endif
.endm

#else

.macro DO_KVM intno
.endm

#endif /* CONFIG_KVM_BOOK3S_HANDLER */

#else  /*__ASSEMBLY__ */

struct kvmppc_vcore;

/* Struct used for coordinating micro-threading (split-core) mode changes */
struct kvm_split_mode {
	unsigned long	rpr;
	unsigned long	pmmar;
	unsigned long	ldbar;
	u8		subcore_size;
	u8		do_nap;
	u8		napped[MAX_SMT_THREADS];
	struct kvmppc_vcore *vc[MAX_SUBCORES];
	/* Bits for changing lpcr on P9 */
	unsigned long	lpcr_req;
	unsigned long	lpidr_req;
	unsigned long	host_lpcr;
	u32		do_set;
	u32		do_restore;
	union {
		u32	allphases;
		u8	phase[4];
	} lpcr_sync;
};

/*
 * This struct goes in the PACA on 64-bit processors.  It is used
 * to store host state that needs to be saved when we enter a guest
 * and restored when we exit, but isn't specific to any particular
 * guest or vcpu.  It also has some scratch fields used by the guest
 * exit code.
 */
struct kvmppc_host_state {
	ulong host_r1;
	ulong host_r2;
	ulong host_msr;
	ulong vmhandler;
	ulong scratch0;
	ulong scratch1;
	ulong scratch2;
	u8 in_guest;
	u8 restore_hid5;
	u8 napping;

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	u8 hwthread_req;
	u8 hwthread_state;
	u8 host_ipi;
	u8 ptid;		/* thread number within subcore when split */
	u8 tid;			/* thread number within whole core */
	u8 fake_suspend;
	struct kvm_vcpu *kvm_vcpu;
	struct kvmppc_vcore *kvm_vcore;
	void __iomem *xics_phys;
	void __iomem *xive_tima_phys;
	void __iomem *xive_tima_virt;
	u32 saved_xirr;
	u64 dabr;
	u64 host_mmcr[10];	/* MMCR 0,1,A, SIAR, SDAR, MMCR2, SIER, MMCR3, SIER2/3 */
	u32 host_pmc[8];
	u64 host_purr;
	u64 host_spurr;
	u64 host_dscr;
	u64 dec_expires;
	struct kvm_split_mode *kvm_split_mode;
#endif
#ifdef CONFIG_PPC_BOOK3S_64
	u64 cfar;
	u64 ppr;
	u64 host_fscr;
#endif
};

struct kvmppc_book3s_shadow_vcpu {
	bool in_use;
	ulong gpr[14];
	u32 cr;
	ulong xer;
	ulong ctr;
	ulong lr;
	ulong pc;

	ulong shadow_srr1;
	ulong fault_dar;
	u32 fault_dsisr;
	u32 last_inst;

#ifdef CONFIG_PPC_BOOK3S_32
	u32     sr[16];			/* Guest SRs */

	struct kvmppc_host_state hstate;
#endif

#ifdef CONFIG_PPC_BOOK3S_64
	u8 slb_max;			/* highest used guest slb entry */
	struct  {
		u64     esid;
		u64     vsid;
	} slb[64];			/* guest SLB */
	u64 shadow_fscr;
#endif
};

#endif /*__ASSEMBLY__ */

/* Values for kvm_state */
#define KVM_HWTHREAD_IN_KERNEL	0
#define KVM_HWTHREAD_IN_IDLE	1
#define KVM_HWTHREAD_IN_KVM	2

#endif /* __ASM_KVM_BOOK3S_ASM_H__ */
