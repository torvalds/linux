/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu <yu.liu@freescale.com>
 *         Scott Wood <scottwood@freescale.com>
 *         Ashish Kalra <ashish.kalra@freescale.com>
 *         Varun Sethi <varun.sethi@freescale.com>
 *
 * Description:
 * This file is based on arch/powerpc/kvm/44x_tlb.h and
 * arch/powerpc/include/asm/kvm_44x.h by Hollis Blanchard <hollisb@us.ibm.com>,
 * Copyright IBM Corp. 2007-2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#ifndef KVM_E500_H
#define KVM_E500_H

#include <linux/kvm_host.h>
#include <asm/mmu-book3e.h>
#include <asm/tlb.h>

enum vcpu_ftr {
	VCPU_FTR_MMU_V2
};

#define E500_PID_NUM   3
#define E500_TLB_NUM   2

/* entry is mapped somewhere in host TLB */
#define E500_TLB_VALID		(1 << 31)
/* TLB1 entry is mapped by host TLB1, tracked by bitmaps */
#define E500_TLB_BITMAP		(1 << 30)
/* TLB1 entry is mapped by host TLB0 */
#define E500_TLB_TLB0		(1 << 29)
/* bits [6-5] MAS2_X1 and MAS2_X0 and [4-0] bits for WIMGE */
#define E500_TLB_MAS2_ATTR	(0x7f)

struct tlbe_ref {
	pfn_t pfn;		/* valid only for TLB0, except briefly */
	unsigned int flags;	/* E500_TLB_* */
};

struct tlbe_priv {
	struct tlbe_ref ref;
};

#ifdef CONFIG_KVM_E500V2
struct vcpu_id_table;
#endif

struct kvmppc_e500_tlb_params {
	int entries, ways, sets;
};

struct kvmppc_vcpu_e500 {
	struct kvm_vcpu vcpu;

	/* Unmodified copy of the guest's TLB -- shared with host userspace. */
	struct kvm_book3e_206_tlb_entry *gtlb_arch;

	/* Starting entry number in gtlb_arch[] */
	int gtlb_offset[E500_TLB_NUM];

	/* KVM internal information associated with each guest TLB entry */
	struct tlbe_priv *gtlb_priv[E500_TLB_NUM];

	struct kvmppc_e500_tlb_params gtlb_params[E500_TLB_NUM];

	unsigned int gtlb_nv[E500_TLB_NUM];

	unsigned int host_tlb1_nv;

	u32 svr;
	u32 l1csr0;
	u32 l1csr1;
	u32 hid0;
	u32 hid1;
	u64 mcar;

	struct page **shared_tlb_pages;
	int num_shared_tlb_pages;

	u64 *g2h_tlb1_map;
	unsigned int *h2g_tlb1_rmap;

	/* Minimum and maximum address mapped my TLB1 */
	unsigned long tlb1_min_eaddr;
	unsigned long tlb1_max_eaddr;

#ifdef CONFIG_KVM_E500V2
	u32 pid[E500_PID_NUM];

	/* vcpu id table */
	struct vcpu_id_table *idt;
#endif
};

static inline struct kvmppc_vcpu_e500 *to_e500(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct kvmppc_vcpu_e500, vcpu);
}


/* This geometry is the legacy default -- can be overridden by userspace */
#define KVM_E500_TLB0_WAY_SIZE		128
#define KVM_E500_TLB0_WAY_NUM		2

#define KVM_E500_TLB0_SIZE  (KVM_E500_TLB0_WAY_SIZE * KVM_E500_TLB0_WAY_NUM)
#define KVM_E500_TLB1_SIZE  16

#define index_of(tlbsel, esel)	(((tlbsel) << 16) | ((esel) & 0xFFFF))
#define tlbsel_of(index)	((index) >> 16)
#define esel_of(index)		((index) & 0xFFFF)

#define E500_TLB_USER_PERM_MASK (MAS3_UX|MAS3_UR|MAS3_UW)
#define E500_TLB_SUPER_PERM_MASK (MAS3_SX|MAS3_SR|MAS3_SW)
#define MAS2_ATTRIB_MASK \
	  (MAS2_X0 | MAS2_X1 | MAS2_E | MAS2_G)
#define MAS3_ATTRIB_MASK \
	  (MAS3_U0 | MAS3_U1 | MAS3_U2 | MAS3_U3 \
	   | E500_TLB_USER_PERM_MASK | E500_TLB_SUPER_PERM_MASK)

int kvmppc_e500_emul_mt_mmucsr0(struct kvmppc_vcpu_e500 *vcpu_e500,
				ulong value);
int kvmppc_e500_emul_tlbwe(struct kvm_vcpu *vcpu);
int kvmppc_e500_emul_tlbre(struct kvm_vcpu *vcpu);
int kvmppc_e500_emul_tlbivax(struct kvm_vcpu *vcpu, gva_t ea);
int kvmppc_e500_emul_tlbilx(struct kvm_vcpu *vcpu, int type, gva_t ea);
int kvmppc_e500_emul_tlbsx(struct kvm_vcpu *vcpu, gva_t ea);
int kvmppc_e500_tlb_init(struct kvmppc_vcpu_e500 *vcpu_e500);
void kvmppc_e500_tlb_uninit(struct kvmppc_vcpu_e500 *vcpu_e500);

void kvmppc_get_sregs_e500_tlb(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
int kvmppc_set_sregs_e500_tlb(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);

int kvmppc_get_one_reg_e500_tlb(struct kvm_vcpu *vcpu, u64 id,
				union kvmppc_one_reg *val);
int kvmppc_set_one_reg_e500_tlb(struct kvm_vcpu *vcpu, u64 id,
			       union kvmppc_one_reg *val);

#ifdef CONFIG_KVM_E500V2
unsigned int kvmppc_e500_get_sid(struct kvmppc_vcpu_e500 *vcpu_e500,
				 unsigned int as, unsigned int gid,
				 unsigned int pr, int avoid_recursion);
#endif

/* TLB helper functions */
static inline unsigned int
get_tlb_size(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return (tlbe->mas1 >> 7) & 0x1f;
}

static inline gva_t get_tlb_eaddr(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return tlbe->mas2 & MAS2_EPN;
}

static inline u64 get_tlb_bytes(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	unsigned int pgsize = get_tlb_size(tlbe);
	return 1ULL << 10 << pgsize;
}

static inline gva_t get_tlb_end(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	u64 bytes = get_tlb_bytes(tlbe);
	return get_tlb_eaddr(tlbe) + bytes - 1;
}

static inline u64 get_tlb_raddr(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return tlbe->mas7_3 & ~0xfffULL;
}

static inline unsigned int
get_tlb_tid(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return (tlbe->mas1 >> 16) & 0xff;
}

static inline unsigned int
get_tlb_ts(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return (tlbe->mas1 >> 12) & 0x1;
}

static inline unsigned int
get_tlb_v(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return (tlbe->mas1 >> 31) & 0x1;
}

static inline unsigned int
get_tlb_iprot(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return (tlbe->mas1 >> 30) & 0x1;
}

static inline unsigned int
get_tlb_tsize(const struct kvm_book3e_206_tlb_entry *tlbe)
{
	return (tlbe->mas1 & MAS1_TSIZE_MASK) >> MAS1_TSIZE_SHIFT;
}

static inline unsigned int get_cur_pid(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.pid & 0xff;
}

static inline unsigned int get_cur_as(struct kvm_vcpu *vcpu)
{
	return !!(vcpu->arch.shared->msr & (MSR_IS | MSR_DS));
}

static inline unsigned int get_cur_pr(struct kvm_vcpu *vcpu)
{
	return !!(vcpu->arch.shared->msr & MSR_PR);
}

static inline unsigned int get_cur_spid(const struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.shared->mas6 >> 16) & 0xff;
}

static inline unsigned int get_cur_sas(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.shared->mas6 & 0x1;
}

static inline unsigned int get_tlb_tlbsel(const struct kvm_vcpu *vcpu)
{
	/*
	 * Manual says that tlbsel has 2 bits wide.
	 * Since we only have two TLBs, only lower bit is used.
	 */
	return (vcpu->arch.shared->mas0 >> 28) & 0x1;
}

static inline unsigned int get_tlb_nv_bit(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.shared->mas0 & 0xfff;
}

static inline unsigned int get_tlb_esel_bit(const struct kvm_vcpu *vcpu)
{
	return (vcpu->arch.shared->mas0 >> 16) & 0xfff;
}

static inline int tlbe_is_host_safe(const struct kvm_vcpu *vcpu,
			const struct kvm_book3e_206_tlb_entry *tlbe)
{
	gpa_t gpa;

	if (!get_tlb_v(tlbe))
		return 0;

#ifndef CONFIG_KVM_BOOKE_HV
	/* Does it match current guest AS? */
	/* XXX what about IS != DS? */
	if (get_tlb_ts(tlbe) != !!(vcpu->arch.shared->msr & MSR_IS))
		return 0;
#endif

	gpa = get_tlb_raddr(tlbe);
	if (!gfn_to_memslot(vcpu->kvm, gpa >> PAGE_SHIFT))
		/* Mapping is not for RAM. */
		return 0;

	return 1;
}

static inline struct kvm_book3e_206_tlb_entry *get_entry(
	struct kvmppc_vcpu_e500 *vcpu_e500, int tlbsel, int entry)
{
	int offset = vcpu_e500->gtlb_offset[tlbsel];
	return &vcpu_e500->gtlb_arch[offset + entry];
}

void kvmppc_e500_tlbil_one(struct kvmppc_vcpu_e500 *vcpu_e500,
			   struct kvm_book3e_206_tlb_entry *gtlbe);
void kvmppc_e500_tlbil_all(struct kvmppc_vcpu_e500 *vcpu_e500);

#ifdef CONFIG_KVM_BOOKE_HV
#define kvmppc_e500_get_tlb_stid(vcpu, gtlbe)       get_tlb_tid(gtlbe)
#define get_tlbmiss_tid(vcpu)           get_cur_pid(vcpu)
#define get_tlb_sts(gtlbe)              (gtlbe->mas1 & MAS1_TS)
#else
unsigned int kvmppc_e500_get_tlb_stid(struct kvm_vcpu *vcpu,
				      struct kvm_book3e_206_tlb_entry *gtlbe);

static inline unsigned int get_tlbmiss_tid(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	unsigned int tidseld = (vcpu->arch.shared->mas4 >> 16) & 0xf;

	return vcpu_e500->pid[tidseld];
}

/* Force TS=1 for all guest mappings. */
#define get_tlb_sts(gtlbe)              (MAS1_TS)
#endif /* !BOOKE_HV */

static inline bool has_feature(const struct kvm_vcpu *vcpu,
			       enum vcpu_ftr ftr)
{
	bool has_ftr;
	switch (ftr) {
	case VCPU_FTR_MMU_V2:
		has_ftr = ((vcpu->arch.mmucfg & MMUCFG_MAVN) == MMUCFG_MAVN_V2);
		break;
	default:
		return false;
	}
	return has_ftr;
}

#endif /* KVM_E500_H */
