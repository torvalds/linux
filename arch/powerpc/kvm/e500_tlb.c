/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, yu.liu@freescale.com
 *
 * Description:
 * This file is based on arch/powerpc/kvm/44x_tlb.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_e500.h>

#include "../mm/mmu_decl.h"
#include "e500_tlb.h"
#include "trace.h"

#define to_htlb1_esel(esel) (tlb1_entry_num - (esel) - 1)

static unsigned int tlb1_entry_num;

void kvmppc_dump_tlbs(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct tlbe *tlbe;
	int i, tlbsel;

	printk("| %8s | %8s | %8s | %8s | %8s |\n",
			"nr", "mas1", "mas2", "mas3", "mas7");

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		printk("Guest TLB%d:\n", tlbsel);
		for (i = 0; i < vcpu_e500->guest_tlb_size[tlbsel]; i++) {
			tlbe = &vcpu_e500->guest_tlb[tlbsel][i];
			if (tlbe->mas1 & MAS1_VALID)
				printk(" G[%d][%3d] |  %08X | %08X | %08X | %08X |\n",
					tlbsel, i, tlbe->mas1, tlbe->mas2,
					tlbe->mas3, tlbe->mas7);
		}
	}

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		printk("Shadow TLB%d:\n", tlbsel);
		for (i = 0; i < vcpu_e500->shadow_tlb_size[tlbsel]; i++) {
			tlbe = &vcpu_e500->shadow_tlb[tlbsel][i];
			if (tlbe->mas1 & MAS1_VALID)
				printk(" S[%d][%3d] |  %08X | %08X | %08X | %08X |\n",
					tlbsel, i, tlbe->mas1, tlbe->mas2,
					tlbe->mas3, tlbe->mas7);
		}
	}
}

static inline unsigned int tlb0_get_next_victim(
		struct kvmppc_vcpu_e500 *vcpu_e500)
{
	unsigned int victim;

	victim = vcpu_e500->guest_tlb_nv[0]++;
	if (unlikely(vcpu_e500->guest_tlb_nv[0] >= KVM_E500_TLB0_WAY_NUM))
		vcpu_e500->guest_tlb_nv[0] = 0;

	return victim;
}

static inline unsigned int tlb1_max_shadow_size(void)
{
	return tlb1_entry_num - tlbcam_index;
}

static inline int tlbe_is_writable(struct tlbe *tlbe)
{
	return tlbe->mas3 & (MAS3_SW|MAS3_UW);
}

static inline u32 e500_shadow_mas3_attrib(u32 mas3, int usermode)
{
	/* Mask off reserved bits. */
	mas3 &= MAS3_ATTRIB_MASK;

	if (!usermode) {
		/* Guest is in supervisor mode,
		 * so we need to translate guest
		 * supervisor permissions into user permissions. */
		mas3 &= ~E500_TLB_USER_PERM_MASK;
		mas3 |= (mas3 & E500_TLB_SUPER_PERM_MASK) << 1;
	}

	return mas3 | E500_TLB_SUPER_PERM_MASK;
}

static inline u32 e500_shadow_mas2_attrib(u32 mas2, int usermode)
{
#ifdef CONFIG_SMP
	return (mas2 & MAS2_ATTRIB_MASK) | MAS2_M;
#else
	return mas2 & MAS2_ATTRIB_MASK;
#endif
}

/*
 * writing shadow tlb entry to host TLB
 */
static inline void __write_host_tlbe(struct tlbe *stlbe)
{
	mtspr(SPRN_MAS1, stlbe->mas1);
	mtspr(SPRN_MAS2, stlbe->mas2);
	mtspr(SPRN_MAS3, stlbe->mas3);
	mtspr(SPRN_MAS7, stlbe->mas7);
	__asm__ __volatile__ ("tlbwe\n" : : );
}

static inline void write_host_tlbe(struct kvmppc_vcpu_e500 *vcpu_e500,
		int tlbsel, int esel)
{
	struct tlbe *stlbe = &vcpu_e500->shadow_tlb[tlbsel][esel];

	local_irq_disable();
	if (tlbsel == 0) {
		__write_host_tlbe(stlbe);
	} else {
		unsigned register mas0;

		mas0 = mfspr(SPRN_MAS0);

		mtspr(SPRN_MAS0, MAS0_TLBSEL(1) | MAS0_ESEL(to_htlb1_esel(esel)));
		__write_host_tlbe(stlbe);

		mtspr(SPRN_MAS0, mas0);
	}
	local_irq_enable();
}

void kvmppc_e500_tlb_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int i;
	unsigned register mas0;

	/* Load all valid TLB1 entries to reduce guest tlb miss fault */
	local_irq_disable();
	mas0 = mfspr(SPRN_MAS0);
	for (i = 0; i < tlb1_max_shadow_size(); i++) {
		struct tlbe *stlbe = &vcpu_e500->shadow_tlb[1][i];

		if (get_tlb_v(stlbe)) {
			mtspr(SPRN_MAS0, MAS0_TLBSEL(1)
					| MAS0_ESEL(to_htlb1_esel(i)));
			__write_host_tlbe(stlbe);
		}
	}
	mtspr(SPRN_MAS0, mas0);
	local_irq_enable();
}

void kvmppc_e500_tlb_put(struct kvm_vcpu *vcpu)
{
	_tlbil_all();
}

/* Search the guest TLB for a matching entry. */
static int kvmppc_e500_tlb_index(struct kvmppc_vcpu_e500 *vcpu_e500,
		gva_t eaddr, int tlbsel, unsigned int pid, int as)
{
	int i;

	/* XXX Replace loop with fancy data structures. */
	for (i = 0; i < vcpu_e500->guest_tlb_size[tlbsel]; i++) {
		struct tlbe *tlbe = &vcpu_e500->guest_tlb[tlbsel][i];
		unsigned int tid;

		if (eaddr < get_tlb_eaddr(tlbe))
			continue;

		if (eaddr > get_tlb_end(tlbe))
			continue;

		tid = get_tlb_tid(tlbe);
		if (tid && (tid != pid))
			continue;

		if (!get_tlb_v(tlbe))
			continue;

		if (get_tlb_ts(tlbe) != as && as != -1)
			continue;

		return i;
	}

	return -1;
}

static void kvmppc_e500_shadow_release(struct kvmppc_vcpu_e500 *vcpu_e500,
		int tlbsel, int esel)
{
	struct tlbe *stlbe = &vcpu_e500->shadow_tlb[tlbsel][esel];
	struct page *page = vcpu_e500->shadow_pages[tlbsel][esel];

	if (page) {
		vcpu_e500->shadow_pages[tlbsel][esel] = NULL;

		if (get_tlb_v(stlbe)) {
			if (tlbe_is_writable(stlbe))
				kvm_release_page_dirty(page);
			else
				kvm_release_page_clean(page);
		}
	}
}

static void kvmppc_e500_stlbe_invalidate(struct kvmppc_vcpu_e500 *vcpu_e500,
		int tlbsel, int esel)
{
	struct tlbe *stlbe = &vcpu_e500->shadow_tlb[tlbsel][esel];

	kvmppc_e500_shadow_release(vcpu_e500, tlbsel, esel);
	stlbe->mas1 = 0;
	trace_kvm_stlb_inval(index_of(tlbsel, esel), stlbe->mas1, stlbe->mas2,
			     stlbe->mas3, stlbe->mas7);
}

static void kvmppc_e500_tlb1_invalidate(struct kvmppc_vcpu_e500 *vcpu_e500,
		gva_t eaddr, gva_t eend, u32 tid)
{
	unsigned int pid = tid & 0xff;
	unsigned int i;

	/* XXX Replace loop with fancy data structures. */
	for (i = 0; i < vcpu_e500->guest_tlb_size[1]; i++) {
		struct tlbe *stlbe = &vcpu_e500->shadow_tlb[1][i];
		unsigned int tid;

		if (!get_tlb_v(stlbe))
			continue;

		if (eend < get_tlb_eaddr(stlbe))
			continue;

		if (eaddr > get_tlb_end(stlbe))
			continue;

		tid = get_tlb_tid(stlbe);
		if (tid && (tid != pid))
			continue;

		kvmppc_e500_stlbe_invalidate(vcpu_e500, 1, i);
		write_host_tlbe(vcpu_e500, 1, i);
	}
}

static inline void kvmppc_e500_deliver_tlb_miss(struct kvm_vcpu *vcpu,
		unsigned int eaddr, int as)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	unsigned int victim, pidsel, tsized;
	int tlbsel;

	/* since we only have two TLBs, only lower bit is used. */
	tlbsel = (vcpu_e500->mas4 >> 28) & 0x1;
	victim = (tlbsel == 0) ? tlb0_get_next_victim(vcpu_e500) : 0;
	pidsel = (vcpu_e500->mas4 >> 16) & 0xf;
	tsized = (vcpu_e500->mas4 >> 7) & 0x1f;

	vcpu_e500->mas0 = MAS0_TLBSEL(tlbsel) | MAS0_ESEL(victim)
		| MAS0_NV(vcpu_e500->guest_tlb_nv[tlbsel]);
	vcpu_e500->mas1 = MAS1_VALID | (as ? MAS1_TS : 0)
		| MAS1_TID(vcpu_e500->pid[pidsel])
		| MAS1_TSIZE(tsized);
	vcpu_e500->mas2 = (eaddr & MAS2_EPN)
		| (vcpu_e500->mas4 & MAS2_ATTRIB_MASK);
	vcpu_e500->mas3 &= MAS3_U0 | MAS3_U1 | MAS3_U2 | MAS3_U3;
	vcpu_e500->mas6 = (vcpu_e500->mas6 & MAS6_SPID1)
		| (get_cur_pid(vcpu) << 16)
		| (as ? MAS6_SAS : 0);
	vcpu_e500->mas7 = 0;
}

static inline void kvmppc_e500_shadow_map(struct kvmppc_vcpu_e500 *vcpu_e500,
	u64 gvaddr, gfn_t gfn, struct tlbe *gtlbe, int tlbsel, int esel)
{
	struct page *new_page;
	struct tlbe *stlbe;
	hpa_t hpaddr;

	stlbe = &vcpu_e500->shadow_tlb[tlbsel][esel];

	/* Get reference to new page. */
	new_page = gfn_to_page(vcpu_e500->vcpu.kvm, gfn);
	if (is_error_page(new_page)) {
		printk(KERN_ERR "Couldn't get guest page for gfn %lx!\n", gfn);
		kvm_release_page_clean(new_page);
		return;
	}
	hpaddr = page_to_phys(new_page);

	/* Drop reference to old page. */
	kvmppc_e500_shadow_release(vcpu_e500, tlbsel, esel);

	vcpu_e500->shadow_pages[tlbsel][esel] = new_page;

	/* Force TS=1 IPROT=0 TSIZE=4KB for all guest mappings. */
	stlbe->mas1 = MAS1_TSIZE(BOOK3E_PAGESZ_4K)
		| MAS1_TID(get_tlb_tid(gtlbe)) | MAS1_TS | MAS1_VALID;
	stlbe->mas2 = (gvaddr & MAS2_EPN)
		| e500_shadow_mas2_attrib(gtlbe->mas2,
				vcpu_e500->vcpu.arch.msr & MSR_PR);
	stlbe->mas3 = (hpaddr & MAS3_RPN)
		| e500_shadow_mas3_attrib(gtlbe->mas3,
				vcpu_e500->vcpu.arch.msr & MSR_PR);
	stlbe->mas7 = (hpaddr >> 32) & MAS7_RPN;

	trace_kvm_stlb_write(index_of(tlbsel, esel), stlbe->mas1, stlbe->mas2,
			     stlbe->mas3, stlbe->mas7);
}

/* XXX only map the one-one case, for now use TLB0 */
static int kvmppc_e500_stlbe_map(struct kvmppc_vcpu_e500 *vcpu_e500,
		int tlbsel, int esel)
{
	struct tlbe *gtlbe;

	gtlbe = &vcpu_e500->guest_tlb[tlbsel][esel];

	kvmppc_e500_shadow_map(vcpu_e500, get_tlb_eaddr(gtlbe),
			get_tlb_raddr(gtlbe) >> PAGE_SHIFT,
			gtlbe, tlbsel, esel);

	return esel;
}

/* Caller must ensure that the specified guest TLB entry is safe to insert into
 * the shadow TLB. */
/* XXX for both one-one and one-to-many , for now use TLB1 */
static int kvmppc_e500_tlb1_map(struct kvmppc_vcpu_e500 *vcpu_e500,
		u64 gvaddr, gfn_t gfn, struct tlbe *gtlbe)
{
	unsigned int victim;

	victim = vcpu_e500->guest_tlb_nv[1]++;

	if (unlikely(vcpu_e500->guest_tlb_nv[1] >= tlb1_max_shadow_size()))
		vcpu_e500->guest_tlb_nv[1] = 0;

	kvmppc_e500_shadow_map(vcpu_e500, gvaddr, gfn, gtlbe, 1, victim);

	return victim;
}

/* Invalidate all guest kernel mappings when enter usermode,
 * so that when they fault back in they will get the
 * proper permission bits. */
void kvmppc_mmu_priv_switch(struct kvm_vcpu *vcpu, int usermode)
{
	if (usermode) {
		struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
		int i;

		/* XXX Replace loop with fancy data structures. */
		for (i = 0; i < tlb1_max_shadow_size(); i++)
			kvmppc_e500_stlbe_invalidate(vcpu_e500, 1, i);

		_tlbil_all();
	}
}

static int kvmppc_e500_gtlbe_invalidate(struct kvmppc_vcpu_e500 *vcpu_e500,
		int tlbsel, int esel)
{
	struct tlbe *gtlbe = &vcpu_e500->guest_tlb[tlbsel][esel];

	if (unlikely(get_tlb_iprot(gtlbe)))
		return -1;

	if (tlbsel == 1) {
		kvmppc_e500_tlb1_invalidate(vcpu_e500, get_tlb_eaddr(gtlbe),
				get_tlb_end(gtlbe),
				get_tlb_tid(gtlbe));
	} else {
		kvmppc_e500_stlbe_invalidate(vcpu_e500, tlbsel, esel);
	}

	gtlbe->mas1 = 0;

	return 0;
}

int kvmppc_e500_emul_mt_mmucsr0(struct kvmppc_vcpu_e500 *vcpu_e500, ulong value)
{
	int esel;

	if (value & MMUCSR0_TLB0FI)
		for (esel = 0; esel < vcpu_e500->guest_tlb_size[0]; esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, 0, esel);
	if (value & MMUCSR0_TLB1FI)
		for (esel = 0; esel < vcpu_e500->guest_tlb_size[1]; esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, 1, esel);

	_tlbil_all();

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbivax(struct kvm_vcpu *vcpu, int ra, int rb)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	unsigned int ia;
	int esel, tlbsel;
	gva_t ea;

	ea = ((ra) ? vcpu->arch.gpr[ra] : 0) + vcpu->arch.gpr[rb];

	ia = (ea >> 2) & 0x1;

	/* since we only have two TLBs, only lower bit is used. */
	tlbsel = (ea >> 3) & 0x1;

	if (ia) {
		/* invalidate all entries */
		for (esel = 0; esel < vcpu_e500->guest_tlb_size[tlbsel]; esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
	} else {
		ea &= 0xfffff000;
		esel = kvmppc_e500_tlb_index(vcpu_e500, ea, tlbsel,
				get_cur_pid(vcpu), -1);
		if (esel >= 0)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
	}

	_tlbil_all();

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbre(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int tlbsel, esel;
	struct tlbe *gtlbe;

	tlbsel = get_tlb_tlbsel(vcpu_e500);
	esel = get_tlb_esel(vcpu_e500, tlbsel);

	gtlbe = &vcpu_e500->guest_tlb[tlbsel][esel];
	vcpu_e500->mas0 &= ~MAS0_NV(~0);
	vcpu_e500->mas0 |= MAS0_NV(vcpu_e500->guest_tlb_nv[tlbsel]);
	vcpu_e500->mas1 = gtlbe->mas1;
	vcpu_e500->mas2 = gtlbe->mas2;
	vcpu_e500->mas3 = gtlbe->mas3;
	vcpu_e500->mas7 = gtlbe->mas7;

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbsx(struct kvm_vcpu *vcpu, int rb)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int as = !!get_cur_sas(vcpu_e500);
	unsigned int pid = get_cur_spid(vcpu_e500);
	int esel, tlbsel;
	struct tlbe *gtlbe = NULL;
	gva_t ea;

	ea = vcpu->arch.gpr[rb];

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		esel = kvmppc_e500_tlb_index(vcpu_e500, ea, tlbsel, pid, as);
		if (esel >= 0) {
			gtlbe = &vcpu_e500->guest_tlb[tlbsel][esel];
			break;
		}
	}

	if (gtlbe) {
		vcpu_e500->mas0 = MAS0_TLBSEL(tlbsel) | MAS0_ESEL(esel)
			| MAS0_NV(vcpu_e500->guest_tlb_nv[tlbsel]);
		vcpu_e500->mas1 = gtlbe->mas1;
		vcpu_e500->mas2 = gtlbe->mas2;
		vcpu_e500->mas3 = gtlbe->mas3;
		vcpu_e500->mas7 = gtlbe->mas7;
	} else {
		int victim;

		/* since we only have two TLBs, only lower bit is used. */
		tlbsel = vcpu_e500->mas4 >> 28 & 0x1;
		victim = (tlbsel == 0) ? tlb0_get_next_victim(vcpu_e500) : 0;

		vcpu_e500->mas0 = MAS0_TLBSEL(tlbsel) | MAS0_ESEL(victim)
			| MAS0_NV(vcpu_e500->guest_tlb_nv[tlbsel]);
		vcpu_e500->mas1 = (vcpu_e500->mas6 & MAS6_SPID0)
			| (vcpu_e500->mas6 & (MAS6_SAS ? MAS1_TS : 0))
			| (vcpu_e500->mas4 & MAS4_TSIZED(~0));
		vcpu_e500->mas2 &= MAS2_EPN;
		vcpu_e500->mas2 |= vcpu_e500->mas4 & MAS2_ATTRIB_MASK;
		vcpu_e500->mas3 &= MAS3_U0 | MAS3_U1 | MAS3_U2 | MAS3_U3;
		vcpu_e500->mas7 = 0;
	}

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbwe(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	u64 eaddr;
	u64 raddr;
	u32 tid;
	struct tlbe *gtlbe;
	int tlbsel, esel, stlbsel, sesel;

	tlbsel = get_tlb_tlbsel(vcpu_e500);
	esel = get_tlb_esel(vcpu_e500, tlbsel);

	gtlbe = &vcpu_e500->guest_tlb[tlbsel][esel];

	if (get_tlb_v(gtlbe) && tlbsel == 1) {
		eaddr = get_tlb_eaddr(gtlbe);
		tid = get_tlb_tid(gtlbe);
		kvmppc_e500_tlb1_invalidate(vcpu_e500, eaddr,
				get_tlb_end(gtlbe), tid);
	}

	gtlbe->mas1 = vcpu_e500->mas1;
	gtlbe->mas2 = vcpu_e500->mas2;
	gtlbe->mas3 = vcpu_e500->mas3;
	gtlbe->mas7 = vcpu_e500->mas7;

	trace_kvm_gtlb_write(vcpu_e500->mas0, gtlbe->mas1, gtlbe->mas2,
			     gtlbe->mas3, gtlbe->mas7);

	/* Invalidate shadow mappings for the about-to-be-clobbered TLBE. */
	if (tlbe_is_host_safe(vcpu, gtlbe)) {
		switch (tlbsel) {
		case 0:
			/* TLB0 */
			gtlbe->mas1 &= ~MAS1_TSIZE(~0);
			gtlbe->mas1 |= MAS1_TSIZE(BOOK3E_PAGESZ_4K);

			stlbsel = 0;
			sesel = kvmppc_e500_stlbe_map(vcpu_e500, 0, esel);

			break;

		case 1:
			/* TLB1 */
			eaddr = get_tlb_eaddr(gtlbe);
			raddr = get_tlb_raddr(gtlbe);

			/* Create a 4KB mapping on the host.
			 * If the guest wanted a large page,
			 * only the first 4KB is mapped here and the rest
			 * are mapped on the fly. */
			stlbsel = 1;
			sesel = kvmppc_e500_tlb1_map(vcpu_e500, eaddr,
					raddr >> PAGE_SHIFT, gtlbe);
			break;

		default:
			BUG();
		}
		write_host_tlbe(vcpu_e500, stlbsel, sesel);
	}

	return EMULATE_DONE;
}

int kvmppc_mmu_itlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.msr & MSR_IS);

	return kvmppc_e500_tlb_search(vcpu, eaddr, get_cur_pid(vcpu), as);
}

int kvmppc_mmu_dtlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.msr & MSR_DS);

	return kvmppc_e500_tlb_search(vcpu, eaddr, get_cur_pid(vcpu), as);
}

void kvmppc_mmu_itlb_miss(struct kvm_vcpu *vcpu)
{
	unsigned int as = !!(vcpu->arch.msr & MSR_IS);

	kvmppc_e500_deliver_tlb_miss(vcpu, vcpu->arch.pc, as);
}

void kvmppc_mmu_dtlb_miss(struct kvm_vcpu *vcpu)
{
	unsigned int as = !!(vcpu->arch.msr & MSR_DS);

	kvmppc_e500_deliver_tlb_miss(vcpu, vcpu->arch.fault_dear, as);
}

gpa_t kvmppc_mmu_xlate(struct kvm_vcpu *vcpu, unsigned int index,
			gva_t eaddr)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct tlbe *gtlbe =
		&vcpu_e500->guest_tlb[tlbsel_of(index)][esel_of(index)];
	u64 pgmask = get_tlb_bytes(gtlbe) - 1;

	return get_tlb_raddr(gtlbe) | (eaddr & pgmask);
}

void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int tlbsel, i;

	for (tlbsel = 0; tlbsel < 2; tlbsel++)
		for (i = 0; i < vcpu_e500->guest_tlb_size[tlbsel]; i++)
			kvmppc_e500_shadow_release(vcpu_e500, tlbsel, i);

	/* discard all guest mapping */
	_tlbil_all();
}

void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 eaddr, gpa_t gpaddr,
			unsigned int index)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int tlbsel = tlbsel_of(index);
	int esel = esel_of(index);
	int stlbsel, sesel;

	switch (tlbsel) {
	case 0:
		stlbsel = 0;
		sesel = esel;
		break;

	case 1: {
		gfn_t gfn = gpaddr >> PAGE_SHIFT;
		struct tlbe *gtlbe
			= &vcpu_e500->guest_tlb[tlbsel][esel];

		stlbsel = 1;
		sesel = kvmppc_e500_tlb1_map(vcpu_e500, eaddr, gfn, gtlbe);
		break;
	}

	default:
		BUG();
		break;
	}
	write_host_tlbe(vcpu_e500, stlbsel, sesel);
}

int kvmppc_e500_tlb_search(struct kvm_vcpu *vcpu,
				gva_t eaddr, unsigned int pid, int as)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int esel, tlbsel;

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		esel = kvmppc_e500_tlb_index(vcpu_e500, eaddr, tlbsel, pid, as);
		if (esel >= 0)
			return index_of(tlbsel, esel);
	}

	return -1;
}

void kvmppc_e500_tlb_setup(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	struct tlbe *tlbe;

	/* Insert large initial mapping for guest. */
	tlbe = &vcpu_e500->guest_tlb[1][0];
	tlbe->mas1 = MAS1_VALID | MAS1_TSIZE(BOOK3E_PAGESZ_256M);
	tlbe->mas2 = 0;
	tlbe->mas3 = E500_TLB_SUPER_PERM_MASK;
	tlbe->mas7 = 0;

	/* 4K map for serial output. Used by kernel wrapper. */
	tlbe = &vcpu_e500->guest_tlb[1][1];
	tlbe->mas1 = MAS1_VALID | MAS1_TSIZE(BOOK3E_PAGESZ_4K);
	tlbe->mas2 = (0xe0004500 & 0xFFFFF000) | MAS2_I | MAS2_G;
	tlbe->mas3 = (0xe0004500 & 0xFFFFF000) | E500_TLB_SUPER_PERM_MASK;
	tlbe->mas7 = 0;
}

int kvmppc_e500_tlb_init(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	tlb1_entry_num = mfspr(SPRN_TLB1CFG) & 0xFFF;

	vcpu_e500->guest_tlb_size[0] = KVM_E500_TLB0_SIZE;
	vcpu_e500->guest_tlb[0] =
		kzalloc(sizeof(struct tlbe) * KVM_E500_TLB0_SIZE, GFP_KERNEL);
	if (vcpu_e500->guest_tlb[0] == NULL)
		goto err_out;

	vcpu_e500->shadow_tlb_size[0] = KVM_E500_TLB0_SIZE;
	vcpu_e500->shadow_tlb[0] =
		kzalloc(sizeof(struct tlbe) * KVM_E500_TLB0_SIZE, GFP_KERNEL);
	if (vcpu_e500->shadow_tlb[0] == NULL)
		goto err_out_guest0;

	vcpu_e500->guest_tlb_size[1] = KVM_E500_TLB1_SIZE;
	vcpu_e500->guest_tlb[1] =
		kzalloc(sizeof(struct tlbe) * KVM_E500_TLB1_SIZE, GFP_KERNEL);
	if (vcpu_e500->guest_tlb[1] == NULL)
		goto err_out_shadow0;

	vcpu_e500->shadow_tlb_size[1] = tlb1_entry_num;
	vcpu_e500->shadow_tlb[1] =
		kzalloc(sizeof(struct tlbe) * tlb1_entry_num, GFP_KERNEL);
	if (vcpu_e500->shadow_tlb[1] == NULL)
		goto err_out_guest1;

	vcpu_e500->shadow_pages[0] = (struct page **)
		kzalloc(sizeof(struct page *) * KVM_E500_TLB0_SIZE, GFP_KERNEL);
	if (vcpu_e500->shadow_pages[0] == NULL)
		goto err_out_shadow1;

	vcpu_e500->shadow_pages[1] = (struct page **)
		kzalloc(sizeof(struct page *) * tlb1_entry_num, GFP_KERNEL);
	if (vcpu_e500->shadow_pages[1] == NULL)
		goto err_out_page0;

	return 0;

err_out_page0:
	kfree(vcpu_e500->shadow_pages[0]);
err_out_shadow1:
	kfree(vcpu_e500->shadow_tlb[1]);
err_out_guest1:
	kfree(vcpu_e500->guest_tlb[1]);
err_out_shadow0:
	kfree(vcpu_e500->shadow_tlb[0]);
err_out_guest0:
	kfree(vcpu_e500->guest_tlb[0]);
err_out:
	return -1;
}

void kvmppc_e500_tlb_uninit(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	kfree(vcpu_e500->shadow_pages[1]);
	kfree(vcpu_e500->shadow_pages[0]);
	kfree(vcpu_e500->shadow_tlb[1]);
	kfree(vcpu_e500->guest_tlb[1]);
	kfree(vcpu_e500->shadow_tlb[0]);
	kfree(vcpu_e500->guest_tlb[0]);
}
