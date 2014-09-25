/*
 * Copyright (C) 2008-2013 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, yu.liu@freescale.com
 *         Scott Wood, scottwood@freescale.com
 *         Ashish Kalra, ashish.kalra@freescale.com
 *         Varun Sethi, varun.sethi@freescale.com
 *         Alexander Graf, agraf@suse.de
 *
 * Description:
 * This file is based on arch/powerpc/kvm/44x_tlb.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/rwsem.h>
#include <linux/vmalloc.h>
#include <linux/hugetlb.h>
#include <asm/kvm_ppc.h>

#include "e500.h"
#include "trace_booke.h"
#include "timing.h"
#include "e500_mmu_host.h"

static inline unsigned int gtlb0_get_next_victim(
		struct kvmppc_vcpu_e500 *vcpu_e500)
{
	unsigned int victim;

	victim = vcpu_e500->gtlb_nv[0]++;
	if (unlikely(vcpu_e500->gtlb_nv[0] >= vcpu_e500->gtlb_params[0].ways))
		vcpu_e500->gtlb_nv[0] = 0;

	return victim;
}

static int tlb0_set_base(gva_t addr, int sets, int ways)
{
	int set_base;

	set_base = (addr >> PAGE_SHIFT) & (sets - 1);
	set_base *= ways;

	return set_base;
}

static int gtlb0_set_base(struct kvmppc_vcpu_e500 *vcpu_e500, gva_t addr)
{
	return tlb0_set_base(addr, vcpu_e500->gtlb_params[0].sets,
			     vcpu_e500->gtlb_params[0].ways);
}

static unsigned int get_tlb_esel(struct kvm_vcpu *vcpu, int tlbsel)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int esel = get_tlb_esel_bit(vcpu);

	if (tlbsel == 0) {
		esel &= vcpu_e500->gtlb_params[0].ways - 1;
		esel += gtlb0_set_base(vcpu_e500, vcpu->arch.shared->mas2);
	} else {
		esel &= vcpu_e500->gtlb_params[tlbsel].entries - 1;
	}

	return esel;
}

/* Search the guest TLB for a matching entry. */
static int kvmppc_e500_tlb_index(struct kvmppc_vcpu_e500 *vcpu_e500,
		gva_t eaddr, int tlbsel, unsigned int pid, int as)
{
	int size = vcpu_e500->gtlb_params[tlbsel].entries;
	unsigned int set_base, offset;
	int i;

	if (tlbsel == 0) {
		set_base = gtlb0_set_base(vcpu_e500, eaddr);
		size = vcpu_e500->gtlb_params[0].ways;
	} else {
		if (eaddr < vcpu_e500->tlb1_min_eaddr ||
				eaddr > vcpu_e500->tlb1_max_eaddr)
			return -1;
		set_base = 0;
	}

	offset = vcpu_e500->gtlb_offset[tlbsel];

	for (i = 0; i < size; i++) {
		struct kvm_book3e_206_tlb_entry *tlbe =
			&vcpu_e500->gtlb_arch[offset + set_base + i];
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

		return set_base + i;
	}

	return -1;
}

static inline void kvmppc_e500_deliver_tlb_miss(struct kvm_vcpu *vcpu,
		gva_t eaddr, int as)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	unsigned int victim, tsized;
	int tlbsel;

	/* since we only have two TLBs, only lower bit is used. */
	tlbsel = (vcpu->arch.shared->mas4 >> 28) & 0x1;
	victim = (tlbsel == 0) ? gtlb0_get_next_victim(vcpu_e500) : 0;
	tsized = (vcpu->arch.shared->mas4 >> 7) & 0x1f;

	vcpu->arch.shared->mas0 = MAS0_TLBSEL(tlbsel) | MAS0_ESEL(victim)
		| MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
	vcpu->arch.shared->mas1 = MAS1_VALID | (as ? MAS1_TS : 0)
		| MAS1_TID(get_tlbmiss_tid(vcpu))
		| MAS1_TSIZE(tsized);
	vcpu->arch.shared->mas2 = (eaddr & MAS2_EPN)
		| (vcpu->arch.shared->mas4 & MAS2_ATTRIB_MASK);
	vcpu->arch.shared->mas7_3 &= MAS3_U0 | MAS3_U1 | MAS3_U2 | MAS3_U3;
	vcpu->arch.shared->mas6 = (vcpu->arch.shared->mas6 & MAS6_SPID1)
		| (get_cur_pid(vcpu) << 16)
		| (as ? MAS6_SAS : 0);
}

static void kvmppc_recalc_tlb1map_range(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	int size = vcpu_e500->gtlb_params[1].entries;
	unsigned int offset;
	gva_t eaddr;
	int i;

	vcpu_e500->tlb1_min_eaddr = ~0UL;
	vcpu_e500->tlb1_max_eaddr = 0;
	offset = vcpu_e500->gtlb_offset[1];

	for (i = 0; i < size; i++) {
		struct kvm_book3e_206_tlb_entry *tlbe =
			&vcpu_e500->gtlb_arch[offset + i];

		if (!get_tlb_v(tlbe))
			continue;

		eaddr = get_tlb_eaddr(tlbe);
		vcpu_e500->tlb1_min_eaddr =
				min(vcpu_e500->tlb1_min_eaddr, eaddr);

		eaddr = get_tlb_end(tlbe);
		vcpu_e500->tlb1_max_eaddr =
				max(vcpu_e500->tlb1_max_eaddr, eaddr);
	}
}

static int kvmppc_need_recalc_tlb1map_range(struct kvmppc_vcpu_e500 *vcpu_e500,
				struct kvm_book3e_206_tlb_entry *gtlbe)
{
	unsigned long start, end, size;

	size = get_tlb_bytes(gtlbe);
	start = get_tlb_eaddr(gtlbe) & ~(size - 1);
	end = start + size - 1;

	return vcpu_e500->tlb1_min_eaddr == start ||
			vcpu_e500->tlb1_max_eaddr == end;
}

/* This function is supposed to be called for a adding a new valid tlb entry */
static void kvmppc_set_tlb1map_range(struct kvm_vcpu *vcpu,
				struct kvm_book3e_206_tlb_entry *gtlbe)
{
	unsigned long start, end, size;
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	if (!get_tlb_v(gtlbe))
		return;

	size = get_tlb_bytes(gtlbe);
	start = get_tlb_eaddr(gtlbe) & ~(size - 1);
	end = start + size - 1;

	vcpu_e500->tlb1_min_eaddr = min(vcpu_e500->tlb1_min_eaddr, start);
	vcpu_e500->tlb1_max_eaddr = max(vcpu_e500->tlb1_max_eaddr, end);
}

static inline int kvmppc_e500_gtlbe_invalidate(
				struct kvmppc_vcpu_e500 *vcpu_e500,
				int tlbsel, int esel)
{
	struct kvm_book3e_206_tlb_entry *gtlbe =
		get_entry(vcpu_e500, tlbsel, esel);

	if (unlikely(get_tlb_iprot(gtlbe)))
		return -1;

	if (tlbsel == 1 && kvmppc_need_recalc_tlb1map_range(vcpu_e500, gtlbe))
		kvmppc_recalc_tlb1map_range(vcpu_e500);

	gtlbe->mas1 = 0;

	return 0;
}

int kvmppc_e500_emul_mt_mmucsr0(struct kvmppc_vcpu_e500 *vcpu_e500, ulong value)
{
	int esel;

	if (value & MMUCSR0_TLB0FI)
		for (esel = 0; esel < vcpu_e500->gtlb_params[0].entries; esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, 0, esel);
	if (value & MMUCSR0_TLB1FI)
		for (esel = 0; esel < vcpu_e500->gtlb_params[1].entries; esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, 1, esel);

	/* Invalidate all host shadow mappings */
	kvmppc_core_flush_tlb(&vcpu_e500->vcpu);

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbivax(struct kvm_vcpu *vcpu, gva_t ea)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	unsigned int ia;
	int esel, tlbsel;

	ia = (ea >> 2) & 0x1;

	/* since we only have two TLBs, only lower bit is used. */
	tlbsel = (ea >> 3) & 0x1;

	if (ia) {
		/* invalidate all entries */
		for (esel = 0; esel < vcpu_e500->gtlb_params[tlbsel].entries;
		     esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
	} else {
		ea &= 0xfffff000;
		esel = kvmppc_e500_tlb_index(vcpu_e500, ea, tlbsel,
				get_cur_pid(vcpu), -1);
		if (esel >= 0)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
	}

	/* Invalidate all host shadow mappings */
	kvmppc_core_flush_tlb(&vcpu_e500->vcpu);

	return EMULATE_DONE;
}

static void tlbilx_all(struct kvmppc_vcpu_e500 *vcpu_e500, int tlbsel,
		       int pid, int type)
{
	struct kvm_book3e_206_tlb_entry *tlbe;
	int tid, esel;

	/* invalidate all entries */
	for (esel = 0; esel < vcpu_e500->gtlb_params[tlbsel].entries; esel++) {
		tlbe = get_entry(vcpu_e500, tlbsel, esel);
		tid = get_tlb_tid(tlbe);
		if (type == 0 || tid == pid) {
			inval_gtlbe_on_host(vcpu_e500, tlbsel, esel);
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
		}
	}
}

static void tlbilx_one(struct kvmppc_vcpu_e500 *vcpu_e500, int pid,
		       gva_t ea)
{
	int tlbsel, esel;

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		esel = kvmppc_e500_tlb_index(vcpu_e500, ea, tlbsel, pid, -1);
		if (esel >= 0) {
			inval_gtlbe_on_host(vcpu_e500, tlbsel, esel);
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
			break;
		}
	}
}

int kvmppc_e500_emul_tlbilx(struct kvm_vcpu *vcpu, int type, gva_t ea)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int pid = get_cur_spid(vcpu);

	if (type == 0 || type == 1) {
		tlbilx_all(vcpu_e500, 0, pid, type);
		tlbilx_all(vcpu_e500, 1, pid, type);
	} else if (type == 3) {
		tlbilx_one(vcpu_e500, pid, ea);
	}

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbre(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int tlbsel, esel;
	struct kvm_book3e_206_tlb_entry *gtlbe;

	tlbsel = get_tlb_tlbsel(vcpu);
	esel = get_tlb_esel(vcpu, tlbsel);

	gtlbe = get_entry(vcpu_e500, tlbsel, esel);
	vcpu->arch.shared->mas0 &= ~MAS0_NV(~0);
	vcpu->arch.shared->mas0 |= MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
	vcpu->arch.shared->mas1 = gtlbe->mas1;
	vcpu->arch.shared->mas2 = gtlbe->mas2;
	vcpu->arch.shared->mas7_3 = gtlbe->mas7_3;

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbsx(struct kvm_vcpu *vcpu, gva_t ea)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int as = !!get_cur_sas(vcpu);
	unsigned int pid = get_cur_spid(vcpu);
	int esel, tlbsel;
	struct kvm_book3e_206_tlb_entry *gtlbe = NULL;

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		esel = kvmppc_e500_tlb_index(vcpu_e500, ea, tlbsel, pid, as);
		if (esel >= 0) {
			gtlbe = get_entry(vcpu_e500, tlbsel, esel);
			break;
		}
	}

	if (gtlbe) {
		esel &= vcpu_e500->gtlb_params[tlbsel].ways - 1;

		vcpu->arch.shared->mas0 = MAS0_TLBSEL(tlbsel) | MAS0_ESEL(esel)
			| MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
		vcpu->arch.shared->mas1 = gtlbe->mas1;
		vcpu->arch.shared->mas2 = gtlbe->mas2;
		vcpu->arch.shared->mas7_3 = gtlbe->mas7_3;
	} else {
		int victim;

		/* since we only have two TLBs, only lower bit is used. */
		tlbsel = vcpu->arch.shared->mas4 >> 28 & 0x1;
		victim = (tlbsel == 0) ? gtlb0_get_next_victim(vcpu_e500) : 0;

		vcpu->arch.shared->mas0 = MAS0_TLBSEL(tlbsel)
			| MAS0_ESEL(victim)
			| MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
		vcpu->arch.shared->mas1 =
			  (vcpu->arch.shared->mas6 & MAS6_SPID0)
			| (vcpu->arch.shared->mas6 & (MAS6_SAS ? MAS1_TS : 0))
			| (vcpu->arch.shared->mas4 & MAS4_TSIZED(~0));
		vcpu->arch.shared->mas2 &= MAS2_EPN;
		vcpu->arch.shared->mas2 |= vcpu->arch.shared->mas4 &
					   MAS2_ATTRIB_MASK;
		vcpu->arch.shared->mas7_3 &= MAS3_U0 | MAS3_U1 |
					     MAS3_U2 | MAS3_U3;
	}

	kvmppc_set_exit_type(vcpu, EMULATED_TLBSX_EXITS);
	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbwe(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_entry *gtlbe;
	int tlbsel, esel;
	int recal = 0;
	int idx;

	tlbsel = get_tlb_tlbsel(vcpu);
	esel = get_tlb_esel(vcpu, tlbsel);

	gtlbe = get_entry(vcpu_e500, tlbsel, esel);

	if (get_tlb_v(gtlbe)) {
		inval_gtlbe_on_host(vcpu_e500, tlbsel, esel);
		if ((tlbsel == 1) &&
			kvmppc_need_recalc_tlb1map_range(vcpu_e500, gtlbe))
			recal = 1;
	}

	gtlbe->mas1 = vcpu->arch.shared->mas1;
	gtlbe->mas2 = vcpu->arch.shared->mas2;
	if (!(vcpu->arch.shared->msr & MSR_CM))
		gtlbe->mas2 &= 0xffffffffUL;
	gtlbe->mas7_3 = vcpu->arch.shared->mas7_3;

	trace_kvm_booke206_gtlb_write(vcpu->arch.shared->mas0, gtlbe->mas1,
	                              gtlbe->mas2, gtlbe->mas7_3);

	if (tlbsel == 1) {
		/*
		 * If a valid tlb1 entry is overwritten then recalculate the
		 * min/max TLB1 map address range otherwise no need to look
		 * in tlb1 array.
		 */
		if (recal)
			kvmppc_recalc_tlb1map_range(vcpu_e500);
		else
			kvmppc_set_tlb1map_range(vcpu, gtlbe);
	}

	idx = srcu_read_lock(&vcpu->kvm->srcu);

	/* Invalidate shadow mappings for the about-to-be-clobbered TLBE. */
	if (tlbe_is_host_safe(vcpu, gtlbe)) {
		u64 eaddr = get_tlb_eaddr(gtlbe);
		u64 raddr = get_tlb_raddr(gtlbe);

		if (tlbsel == 0) {
			gtlbe->mas1 &= ~MAS1_TSIZE(~0);
			gtlbe->mas1 |= MAS1_TSIZE(BOOK3E_PAGESZ_4K);
		}

		/* Premap the faulting page */
		kvmppc_mmu_map(vcpu, eaddr, raddr, index_of(tlbsel, esel));
	}

	srcu_read_unlock(&vcpu->kvm->srcu, idx);

	kvmppc_set_exit_type(vcpu, EMULATED_TLBWE_EXITS);
	return EMULATE_DONE;
}

static int kvmppc_e500_tlb_search(struct kvm_vcpu *vcpu,
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

/* 'linear_address' is actually an encoding of AS|PID|EADDR . */
int kvmppc_core_vcpu_translate(struct kvm_vcpu *vcpu,
                               struct kvm_translation *tr)
{
	int index;
	gva_t eaddr;
	u8 pid;
	u8 as;

	eaddr = tr->linear_address;
	pid = (tr->linear_address >> 32) & 0xff;
	as = (tr->linear_address >> 40) & 0x1;

	index = kvmppc_e500_tlb_search(vcpu, eaddr, pid, as);
	if (index < 0) {
		tr->valid = 0;
		return 0;
	}

	tr->physical_address = kvmppc_mmu_xlate(vcpu, index, eaddr);
	/* XXX what does "writeable" and "usermode" even mean? */
	tr->valid = 1;

	return 0;
}


int kvmppc_mmu_itlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_IS);

	return kvmppc_e500_tlb_search(vcpu, eaddr, get_cur_pid(vcpu), as);
}

int kvmppc_mmu_dtlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_DS);

	return kvmppc_e500_tlb_search(vcpu, eaddr, get_cur_pid(vcpu), as);
}

void kvmppc_mmu_itlb_miss(struct kvm_vcpu *vcpu)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_IS);

	kvmppc_e500_deliver_tlb_miss(vcpu, vcpu->arch.pc, as);
}

void kvmppc_mmu_dtlb_miss(struct kvm_vcpu *vcpu)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_DS);

	kvmppc_e500_deliver_tlb_miss(vcpu, vcpu->arch.fault_dear, as);
}

gpa_t kvmppc_mmu_xlate(struct kvm_vcpu *vcpu, unsigned int index,
			gva_t eaddr)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_entry *gtlbe;
	u64 pgmask;

	gtlbe = get_entry(vcpu_e500, tlbsel_of(index), esel_of(index));
	pgmask = get_tlb_bytes(gtlbe) - 1;

	return get_tlb_raddr(gtlbe) | (eaddr & pgmask);
}

void kvmppc_mmu_destroy_e500(struct kvm_vcpu *vcpu)
{
}

/*****************************************/

static void free_gtlb(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	int i;

	kvmppc_core_flush_tlb(&vcpu_e500->vcpu);
	kfree(vcpu_e500->g2h_tlb1_map);
	kfree(vcpu_e500->gtlb_priv[0]);
	kfree(vcpu_e500->gtlb_priv[1]);

	if (vcpu_e500->shared_tlb_pages) {
		vfree((void *)(round_down((uintptr_t)vcpu_e500->gtlb_arch,
					  PAGE_SIZE)));

		for (i = 0; i < vcpu_e500->num_shared_tlb_pages; i++) {
			set_page_dirty_lock(vcpu_e500->shared_tlb_pages[i]);
			put_page(vcpu_e500->shared_tlb_pages[i]);
		}

		vcpu_e500->num_shared_tlb_pages = 0;

		kfree(vcpu_e500->shared_tlb_pages);
		vcpu_e500->shared_tlb_pages = NULL;
	} else {
		kfree(vcpu_e500->gtlb_arch);
	}

	vcpu_e500->gtlb_arch = NULL;
}

void kvmppc_get_sregs_e500_tlb(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	sregs->u.e.mas0 = vcpu->arch.shared->mas0;
	sregs->u.e.mas1 = vcpu->arch.shared->mas1;
	sregs->u.e.mas2 = vcpu->arch.shared->mas2;
	sregs->u.e.mas7_3 = vcpu->arch.shared->mas7_3;
	sregs->u.e.mas4 = vcpu->arch.shared->mas4;
	sregs->u.e.mas6 = vcpu->arch.shared->mas6;

	sregs->u.e.mmucfg = vcpu->arch.mmucfg;
	sregs->u.e.tlbcfg[0] = vcpu->arch.tlbcfg[0];
	sregs->u.e.tlbcfg[1] = vcpu->arch.tlbcfg[1];
	sregs->u.e.tlbcfg[2] = 0;
	sregs->u.e.tlbcfg[3] = 0;
}

int kvmppc_set_sregs_e500_tlb(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	if (sregs->u.e.features & KVM_SREGS_E_ARCH206_MMU) {
		vcpu->arch.shared->mas0 = sregs->u.e.mas0;
		vcpu->arch.shared->mas1 = sregs->u.e.mas1;
		vcpu->arch.shared->mas2 = sregs->u.e.mas2;
		vcpu->arch.shared->mas7_3 = sregs->u.e.mas7_3;
		vcpu->arch.shared->mas4 = sregs->u.e.mas4;
		vcpu->arch.shared->mas6 = sregs->u.e.mas6;
	}

	return 0;
}

int kvmppc_get_one_reg_e500_tlb(struct kvm_vcpu *vcpu, u64 id,
				union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;

	switch (id) {
	case KVM_REG_PPC_MAS0:
		*val = get_reg_val(id, vcpu->arch.shared->mas0);
		break;
	case KVM_REG_PPC_MAS1:
		*val = get_reg_val(id, vcpu->arch.shared->mas1);
		break;
	case KVM_REG_PPC_MAS2:
		*val = get_reg_val(id, vcpu->arch.shared->mas2);
		break;
	case KVM_REG_PPC_MAS7_3:
		*val = get_reg_val(id, vcpu->arch.shared->mas7_3);
		break;
	case KVM_REG_PPC_MAS4:
		*val = get_reg_val(id, vcpu->arch.shared->mas4);
		break;
	case KVM_REG_PPC_MAS6:
		*val = get_reg_val(id, vcpu->arch.shared->mas6);
		break;
	case KVM_REG_PPC_MMUCFG:
		*val = get_reg_val(id, vcpu->arch.mmucfg);
		break;
	case KVM_REG_PPC_EPTCFG:
		*val = get_reg_val(id, vcpu->arch.eptcfg);
		break;
	case KVM_REG_PPC_TLB0CFG:
	case KVM_REG_PPC_TLB1CFG:
	case KVM_REG_PPC_TLB2CFG:
	case KVM_REG_PPC_TLB3CFG:
		i = id - KVM_REG_PPC_TLB0CFG;
		*val = get_reg_val(id, vcpu->arch.tlbcfg[i]);
		break;
	case KVM_REG_PPC_TLB0PS:
	case KVM_REG_PPC_TLB1PS:
	case KVM_REG_PPC_TLB2PS:
	case KVM_REG_PPC_TLB3PS:
		i = id - KVM_REG_PPC_TLB0PS;
		*val = get_reg_val(id, vcpu->arch.tlbps[i]);
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

int kvmppc_set_one_reg_e500_tlb(struct kvm_vcpu *vcpu, u64 id,
			       union kvmppc_one_reg *val)
{
	int r = 0;
	long int i;

	switch (id) {
	case KVM_REG_PPC_MAS0:
		vcpu->arch.shared->mas0 = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_MAS1:
		vcpu->arch.shared->mas1 = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_MAS2:
		vcpu->arch.shared->mas2 = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_MAS7_3:
		vcpu->arch.shared->mas7_3 = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_MAS4:
		vcpu->arch.shared->mas4 = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_MAS6:
		vcpu->arch.shared->mas6 = set_reg_val(id, *val);
		break;
	/* Only allow MMU registers to be set to the config supported by KVM */
	case KVM_REG_PPC_MMUCFG: {
		u32 reg = set_reg_val(id, *val);
		if (reg != vcpu->arch.mmucfg)
			r = -EINVAL;
		break;
	}
	case KVM_REG_PPC_EPTCFG: {
		u32 reg = set_reg_val(id, *val);
		if (reg != vcpu->arch.eptcfg)
			r = -EINVAL;
		break;
	}
	case KVM_REG_PPC_TLB0CFG:
	case KVM_REG_PPC_TLB1CFG:
	case KVM_REG_PPC_TLB2CFG:
	case KVM_REG_PPC_TLB3CFG: {
		/* MMU geometry (N_ENTRY/ASSOC) can be set only using SW_TLB */
		u32 reg = set_reg_val(id, *val);
		i = id - KVM_REG_PPC_TLB0CFG;
		if (reg != vcpu->arch.tlbcfg[i])
			r = -EINVAL;
		break;
	}
	case KVM_REG_PPC_TLB0PS:
	case KVM_REG_PPC_TLB1PS:
	case KVM_REG_PPC_TLB2PS:
	case KVM_REG_PPC_TLB3PS: {
		u32 reg = set_reg_val(id, *val);
		i = id - KVM_REG_PPC_TLB0PS;
		if (reg != vcpu->arch.tlbps[i])
			r = -EINVAL;
		break;
	}
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

static int vcpu_mmu_geometry_update(struct kvm_vcpu *vcpu,
		struct kvm_book3e_206_tlb_params *params)
{
	vcpu->arch.tlbcfg[0] &= ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	if (params->tlb_sizes[0] <= 2048)
		vcpu->arch.tlbcfg[0] |= params->tlb_sizes[0];
	vcpu->arch.tlbcfg[0] |= params->tlb_ways[0] << TLBnCFG_ASSOC_SHIFT;

	vcpu->arch.tlbcfg[1] &= ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	vcpu->arch.tlbcfg[1] |= params->tlb_sizes[1];
	vcpu->arch.tlbcfg[1] |= params->tlb_ways[1] << TLBnCFG_ASSOC_SHIFT;
	return 0;
}

int kvm_vcpu_ioctl_config_tlb(struct kvm_vcpu *vcpu,
			      struct kvm_config_tlb *cfg)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_params params;
	char *virt;
	struct page **pages;
	struct tlbe_priv *privs[2] = {};
	u64 *g2h_bitmap = NULL;
	size_t array_len;
	u32 sets;
	int num_pages, ret, i;

	if (cfg->mmu_type != KVM_MMU_FSL_BOOKE_NOHV)
		return -EINVAL;

	if (copy_from_user(&params, (void __user *)(uintptr_t)cfg->params,
			   sizeof(params)))
		return -EFAULT;

	if (params.tlb_sizes[1] > 64)
		return -EINVAL;
	if (params.tlb_ways[1] != params.tlb_sizes[1])
		return -EINVAL;
	if (params.tlb_sizes[2] != 0 || params.tlb_sizes[3] != 0)
		return -EINVAL;
	if (params.tlb_ways[2] != 0 || params.tlb_ways[3] != 0)
		return -EINVAL;

	if (!is_power_of_2(params.tlb_ways[0]))
		return -EINVAL;

	sets = params.tlb_sizes[0] >> ilog2(params.tlb_ways[0]);
	if (!is_power_of_2(sets))
		return -EINVAL;

	array_len = params.tlb_sizes[0] + params.tlb_sizes[1];
	array_len *= sizeof(struct kvm_book3e_206_tlb_entry);

	if (cfg->array_len < array_len)
		return -EINVAL;

	num_pages = DIV_ROUND_UP(cfg->array + array_len - 1, PAGE_SIZE) -
		    cfg->array / PAGE_SIZE;
	pages = kmalloc(sizeof(struct page *) * num_pages, GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = get_user_pages_fast(cfg->array, num_pages, 1, pages);
	if (ret < 0)
		goto err_pages;

	if (ret != num_pages) {
		num_pages = ret;
		ret = -EFAULT;
		goto err_put_page;
	}

	virt = vmap(pages, num_pages, VM_MAP, PAGE_KERNEL);
	if (!virt) {
		ret = -ENOMEM;
		goto err_put_page;
	}

	privs[0] = kzalloc(sizeof(struct tlbe_priv) * params.tlb_sizes[0],
			   GFP_KERNEL);
	privs[1] = kzalloc(sizeof(struct tlbe_priv) * params.tlb_sizes[1],
			   GFP_KERNEL);

	if (!privs[0] || !privs[1]) {
		ret = -ENOMEM;
		goto err_privs;
	}

	g2h_bitmap = kzalloc(sizeof(u64) * params.tlb_sizes[1],
	                     GFP_KERNEL);
	if (!g2h_bitmap) {
		ret = -ENOMEM;
		goto err_privs;
	}

	free_gtlb(vcpu_e500);

	vcpu_e500->gtlb_priv[0] = privs[0];
	vcpu_e500->gtlb_priv[1] = privs[1];
	vcpu_e500->g2h_tlb1_map = g2h_bitmap;

	vcpu_e500->gtlb_arch = (struct kvm_book3e_206_tlb_entry *)
		(virt + (cfg->array & (PAGE_SIZE - 1)));

	vcpu_e500->gtlb_params[0].entries = params.tlb_sizes[0];
	vcpu_e500->gtlb_params[1].entries = params.tlb_sizes[1];

	vcpu_e500->gtlb_offset[0] = 0;
	vcpu_e500->gtlb_offset[1] = params.tlb_sizes[0];

	/* Update vcpu's MMU geometry based on SW_TLB input */
	vcpu_mmu_geometry_update(vcpu, &params);

	vcpu_e500->shared_tlb_pages = pages;
	vcpu_e500->num_shared_tlb_pages = num_pages;

	vcpu_e500->gtlb_params[0].ways = params.tlb_ways[0];
	vcpu_e500->gtlb_params[0].sets = sets;

	vcpu_e500->gtlb_params[1].ways = params.tlb_sizes[1];
	vcpu_e500->gtlb_params[1].sets = 1;

	kvmppc_recalc_tlb1map_range(vcpu_e500);
	return 0;

err_privs:
	kfree(privs[0]);
	kfree(privs[1]);

err_put_page:
	for (i = 0; i < num_pages; i++)
		put_page(pages[i]);

err_pages:
	kfree(pages);
	return ret;
}

int kvm_vcpu_ioctl_dirty_tlb(struct kvm_vcpu *vcpu,
			     struct kvm_dirty_tlb *dirty)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	kvmppc_recalc_tlb1map_range(vcpu_e500);
	kvmppc_core_flush_tlb(vcpu);
	return 0;
}

/* Vcpu's MMU default configuration */
static int vcpu_mmu_init(struct kvm_vcpu *vcpu,
		       struct kvmppc_e500_tlb_params *params)
{
	/* Initialize RASIZE, PIDSIZE, NTLBS and MAVN fields with host values*/
	vcpu->arch.mmucfg = mfspr(SPRN_MMUCFG) & ~MMUCFG_LPIDSIZE;

	/* Initialize TLBnCFG fields with host values and SW_TLB geometry*/
	vcpu->arch.tlbcfg[0] = mfspr(SPRN_TLB0CFG) &
			     ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	vcpu->arch.tlbcfg[0] |= params[0].entries;
	vcpu->arch.tlbcfg[0] |= params[0].ways << TLBnCFG_ASSOC_SHIFT;

	vcpu->arch.tlbcfg[1] = mfspr(SPRN_TLB1CFG) &
			     ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	vcpu->arch.tlbcfg[1] |= params[1].entries;
	vcpu->arch.tlbcfg[1] |= params[1].ways << TLBnCFG_ASSOC_SHIFT;

	if (has_feature(vcpu, VCPU_FTR_MMU_V2)) {
		vcpu->arch.tlbps[0] = mfspr(SPRN_TLB0PS);
		vcpu->arch.tlbps[1] = mfspr(SPRN_TLB1PS);

		vcpu->arch.mmucfg &= ~MMUCFG_LRAT;

		/* Guest mmu emulation currently doesn't handle E.PT */
		vcpu->arch.eptcfg = 0;
		vcpu->arch.tlbcfg[0] &= ~TLBnCFG_PT;
		vcpu->arch.tlbcfg[1] &= ~TLBnCFG_IND;
	}

	return 0;
}

int kvmppc_e500_tlb_init(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	struct kvm_vcpu *vcpu = &vcpu_e500->vcpu;
	int entry_size = sizeof(struct kvm_book3e_206_tlb_entry);
	int entries = KVM_E500_TLB0_SIZE + KVM_E500_TLB1_SIZE;

	if (e500_mmu_host_init(vcpu_e500))
		goto err;

	vcpu_e500->gtlb_params[0].entries = KVM_E500_TLB0_SIZE;
	vcpu_e500->gtlb_params[1].entries = KVM_E500_TLB1_SIZE;

	vcpu_e500->gtlb_params[0].ways = KVM_E500_TLB0_WAY_NUM;
	vcpu_e500->gtlb_params[0].sets =
		KVM_E500_TLB0_SIZE / KVM_E500_TLB0_WAY_NUM;

	vcpu_e500->gtlb_params[1].ways = KVM_E500_TLB1_SIZE;
	vcpu_e500->gtlb_params[1].sets = 1;

	vcpu_e500->gtlb_arch = kmalloc(entries * entry_size, GFP_KERNEL);
	if (!vcpu_e500->gtlb_arch)
		return -ENOMEM;

	vcpu_e500->gtlb_offset[0] = 0;
	vcpu_e500->gtlb_offset[1] = KVM_E500_TLB0_SIZE;

	vcpu_e500->gtlb_priv[0] = kzalloc(sizeof(struct tlbe_ref) *
					  vcpu_e500->gtlb_params[0].entries,
					  GFP_KERNEL);
	if (!vcpu_e500->gtlb_priv[0])
		goto err;

	vcpu_e500->gtlb_priv[1] = kzalloc(sizeof(struct tlbe_ref) *
					  vcpu_e500->gtlb_params[1].entries,
					  GFP_KERNEL);
	if (!vcpu_e500->gtlb_priv[1])
		goto err;

	vcpu_e500->g2h_tlb1_map = kzalloc(sizeof(u64) *
					  vcpu_e500->gtlb_params[1].entries,
					  GFP_KERNEL);
	if (!vcpu_e500->g2h_tlb1_map)
		goto err;

	vcpu_mmu_init(vcpu, vcpu_e500->gtlb_params);

	kvmppc_recalc_tlb1map_range(vcpu_e500);
	return 0;

err:
	free_gtlb(vcpu_e500);
	return -1;
}

void kvmppc_e500_tlb_uninit(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	free_gtlb(vcpu_e500);
	e500_mmu_host_uninit(vcpu_e500);
}
