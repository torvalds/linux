// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corporation, 2018
 * Authors Suraj Jitindar Singh <sjitindarsingh@gmail.com>
 *	   Paul Mackerras <paulus@ozlabs.org>
 *
 * Description: KVM functions specific to running nested KVM-HV guests
 * on Book3S processors (specifically POWER9 and later).
 */

#include <linux/kernel.h>
#include <linux/kvm_host.h>

#include <asm/kvm_ppc.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

static struct patb_entry *pseries_partition_tb;

static void kvmhv_update_ptbl_cache(struct kvm_nested_guest *gp);

void kvmhv_save_hv_regs(struct kvm_vcpu *vcpu, struct hv_guest_state *hr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	hr->pcr = vc->pcr;
	hr->dpdes = vc->dpdes;
	hr->hfscr = vcpu->arch.hfscr;
	hr->tb_offset = vc->tb_offset;
	hr->dawr0 = vcpu->arch.dawr;
	hr->dawrx0 = vcpu->arch.dawrx;
	hr->ciabr = vcpu->arch.ciabr;
	hr->purr = vcpu->arch.purr;
	hr->spurr = vcpu->arch.spurr;
	hr->ic = vcpu->arch.ic;
	hr->vtb = vc->vtb;
	hr->srr0 = vcpu->arch.shregs.srr0;
	hr->srr1 = vcpu->arch.shregs.srr1;
	hr->sprg[0] = vcpu->arch.shregs.sprg0;
	hr->sprg[1] = vcpu->arch.shregs.sprg1;
	hr->sprg[2] = vcpu->arch.shregs.sprg2;
	hr->sprg[3] = vcpu->arch.shregs.sprg3;
	hr->pidr = vcpu->arch.pid;
	hr->cfar = vcpu->arch.cfar;
	hr->ppr = vcpu->arch.ppr;
}

static void save_hv_return_state(struct kvm_vcpu *vcpu, int trap,
				 struct hv_guest_state *hr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	hr->dpdes = vc->dpdes;
	hr->hfscr = vcpu->arch.hfscr;
	hr->purr = vcpu->arch.purr;
	hr->spurr = vcpu->arch.spurr;
	hr->ic = vcpu->arch.ic;
	hr->vtb = vc->vtb;
	hr->srr0 = vcpu->arch.shregs.srr0;
	hr->srr1 = vcpu->arch.shregs.srr1;
	hr->sprg[0] = vcpu->arch.shregs.sprg0;
	hr->sprg[1] = vcpu->arch.shregs.sprg1;
	hr->sprg[2] = vcpu->arch.shregs.sprg2;
	hr->sprg[3] = vcpu->arch.shregs.sprg3;
	hr->pidr = vcpu->arch.pid;
	hr->cfar = vcpu->arch.cfar;
	hr->ppr = vcpu->arch.ppr;
	switch (trap) {
	case BOOK3S_INTERRUPT_H_DATA_STORAGE:
		hr->hdar = vcpu->arch.fault_dar;
		hr->hdsisr = vcpu->arch.fault_dsisr;
		hr->asdr = vcpu->arch.fault_gpa;
		break;
	case BOOK3S_INTERRUPT_H_INST_STORAGE:
		hr->asdr = vcpu->arch.fault_gpa;
		break;
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
		hr->heir = vcpu->arch.emul_inst;
		break;
	}
}

static void restore_hv_regs(struct kvm_vcpu *vcpu, struct hv_guest_state *hr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	vc->pcr = hr->pcr;
	vc->dpdes = hr->dpdes;
	vcpu->arch.hfscr = hr->hfscr;
	vcpu->arch.dawr = hr->dawr0;
	vcpu->arch.dawrx = hr->dawrx0;
	vcpu->arch.ciabr = hr->ciabr;
	vcpu->arch.purr = hr->purr;
	vcpu->arch.spurr = hr->spurr;
	vcpu->arch.ic = hr->ic;
	vc->vtb = hr->vtb;
	vcpu->arch.shregs.srr0 = hr->srr0;
	vcpu->arch.shregs.srr1 = hr->srr1;
	vcpu->arch.shregs.sprg0 = hr->sprg[0];
	vcpu->arch.shregs.sprg1 = hr->sprg[1];
	vcpu->arch.shregs.sprg2 = hr->sprg[2];
	vcpu->arch.shregs.sprg3 = hr->sprg[3];
	vcpu->arch.pid = hr->pidr;
	vcpu->arch.cfar = hr->cfar;
	vcpu->arch.ppr = hr->ppr;
}

void kvmhv_restore_hv_return_state(struct kvm_vcpu *vcpu,
				   struct hv_guest_state *hr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	vc->dpdes = hr->dpdes;
	vcpu->arch.hfscr = hr->hfscr;
	vcpu->arch.purr = hr->purr;
	vcpu->arch.spurr = hr->spurr;
	vcpu->arch.ic = hr->ic;
	vc->vtb = hr->vtb;
	vcpu->arch.fault_dar = hr->hdar;
	vcpu->arch.fault_dsisr = hr->hdsisr;
	vcpu->arch.fault_gpa = hr->asdr;
	vcpu->arch.emul_inst = hr->heir;
	vcpu->arch.shregs.srr0 = hr->srr0;
	vcpu->arch.shregs.srr1 = hr->srr1;
	vcpu->arch.shregs.sprg0 = hr->sprg[0];
	vcpu->arch.shregs.sprg1 = hr->sprg[1];
	vcpu->arch.shregs.sprg2 = hr->sprg[2];
	vcpu->arch.shregs.sprg3 = hr->sprg[3];
	vcpu->arch.pid = hr->pidr;
	vcpu->arch.cfar = hr->cfar;
	vcpu->arch.ppr = hr->ppr;
}

long kvmhv_enter_nested_guest(struct kvm_vcpu *vcpu)
{
	long int err, r;
	struct kvm_nested_guest *l2;
	struct pt_regs l2_regs, saved_l1_regs;
	struct hv_guest_state l2_hv, saved_l1_hv;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	u64 hv_ptr, regs_ptr;
	u64 hdec_exp;
	s64 delta_purr, delta_spurr, delta_ic, delta_vtb;
	u64 mask;
	unsigned long lpcr;

	if (vcpu->kvm->arch.l1_ptcr == 0)
		return H_NOT_AVAILABLE;

	/* copy parameters in */
	hv_ptr = kvmppc_get_gpr(vcpu, 4);
	err = kvm_vcpu_read_guest(vcpu, hv_ptr, &l2_hv,
				  sizeof(struct hv_guest_state));
	if (err)
		return H_PARAMETER;
	if (l2_hv.version != HV_GUEST_STATE_VERSION)
		return H_P2;

	regs_ptr = kvmppc_get_gpr(vcpu, 5);
	err = kvm_vcpu_read_guest(vcpu, regs_ptr, &l2_regs,
				  sizeof(struct pt_regs));
	if (err)
		return H_PARAMETER;

	/* translate lpid */
	l2 = kvmhv_get_nested(vcpu->kvm, l2_hv.lpid, true);
	if (!l2)
		return H_PARAMETER;
	if (!l2->l1_gr_to_hr) {
		mutex_lock(&l2->tlb_lock);
		kvmhv_update_ptbl_cache(l2);
		mutex_unlock(&l2->tlb_lock);
	}

	/* save l1 values of things */
	vcpu->arch.regs.msr = vcpu->arch.shregs.msr;
	saved_l1_regs = vcpu->arch.regs;
	kvmhv_save_hv_regs(vcpu, &saved_l1_hv);

	/* convert TB values/offsets to host (L0) values */
	hdec_exp = l2_hv.hdec_expiry - vc->tb_offset;
	vc->tb_offset += l2_hv.tb_offset;

	/* set L1 state to L2 state */
	vcpu->arch.nested = l2;
	vcpu->arch.nested_vcpu_id = l2_hv.vcpu_token;
	vcpu->arch.regs = l2_regs;
	vcpu->arch.shregs.msr = vcpu->arch.regs.msr;
	mask = LPCR_DPFD | LPCR_ILE | LPCR_TC | LPCR_AIL | LPCR_LD |
		LPCR_LPES | LPCR_MER;
	lpcr = (vc->lpcr & ~mask) | (l2_hv.lpcr & mask);
	restore_hv_regs(vcpu, &l2_hv);

	vcpu->arch.ret = RESUME_GUEST;
	vcpu->arch.trap = 0;
	do {
		if (mftb() >= hdec_exp) {
			vcpu->arch.trap = BOOK3S_INTERRUPT_HV_DECREMENTER;
			r = RESUME_HOST;
			break;
		}
		r = kvmhv_run_single_vcpu(vcpu->arch.kvm_run, vcpu, hdec_exp,
					  lpcr);
	} while (is_kvmppc_resume_guest(r));

	/* save L2 state for return */
	l2_regs = vcpu->arch.regs;
	l2_regs.msr = vcpu->arch.shregs.msr;
	delta_purr = vcpu->arch.purr - l2_hv.purr;
	delta_spurr = vcpu->arch.spurr - l2_hv.spurr;
	delta_ic = vcpu->arch.ic - l2_hv.ic;
	delta_vtb = vc->vtb - l2_hv.vtb;
	save_hv_return_state(vcpu, vcpu->arch.trap, &l2_hv);

	/* restore L1 state */
	vcpu->arch.nested = NULL;
	vcpu->arch.regs = saved_l1_regs;
	vcpu->arch.shregs.msr = saved_l1_regs.msr & ~MSR_TS_MASK;
	/* set L1 MSR TS field according to L2 transaction state */
	if (l2_regs.msr & MSR_TS_MASK)
		vcpu->arch.shregs.msr |= MSR_TS_S;
	vc->tb_offset = saved_l1_hv.tb_offset;
	restore_hv_regs(vcpu, &saved_l1_hv);
	vcpu->arch.purr += delta_purr;
	vcpu->arch.spurr += delta_spurr;
	vcpu->arch.ic += delta_ic;
	vc->vtb += delta_vtb;

	kvmhv_put_nested(l2);

	/* copy l2_hv_state and regs back to guest */
	err = kvm_vcpu_write_guest(vcpu, hv_ptr, &l2_hv,
				   sizeof(struct hv_guest_state));
	if (err)
		return H_AUTHORITY;
	err = kvm_vcpu_write_guest(vcpu, regs_ptr, &l2_regs,
				   sizeof(struct pt_regs));
	if (err)
		return H_AUTHORITY;

	if (r == -EINTR)
		return H_INTERRUPT;

	return vcpu->arch.trap;
}

long kvmhv_nested_init(void)
{
	long int ptb_order;
	unsigned long ptcr;
	long rc;

	if (!kvmhv_on_pseries())
		return 0;
	if (!radix_enabled())
		return -ENODEV;

	/* find log base 2 of KVMPPC_NR_LPIDS, rounding up */
	ptb_order = __ilog2(KVMPPC_NR_LPIDS - 1) + 1;
	if (ptb_order < 8)
		ptb_order = 8;
	pseries_partition_tb = kmalloc(sizeof(struct patb_entry) << ptb_order,
				       GFP_KERNEL);
	if (!pseries_partition_tb) {
		pr_err("kvm-hv: failed to allocated nested partition table\n");
		return -ENOMEM;
	}

	ptcr = __pa(pseries_partition_tb) | (ptb_order - 8);
	rc = plpar_hcall_norets(H_SET_PARTITION_TABLE, ptcr);
	if (rc != H_SUCCESS) {
		pr_err("kvm-hv: Parent hypervisor does not support nesting (rc=%ld)\n",
		       rc);
		kfree(pseries_partition_tb);
		pseries_partition_tb = NULL;
		return -ENODEV;
	}

	return 0;
}

void kvmhv_nested_exit(void)
{
	/*
	 * N.B. the kvmhv_on_pseries() test is there because it enables
	 * the compiler to remove the call to plpar_hcall_norets()
	 * when CONFIG_PPC_PSERIES=n.
	 */
	if (kvmhv_on_pseries() && pseries_partition_tb) {
		plpar_hcall_norets(H_SET_PARTITION_TABLE, 0);
		kfree(pseries_partition_tb);
		pseries_partition_tb = NULL;
	}
}

void kvmhv_set_ptbl_entry(unsigned int lpid, u64 dw0, u64 dw1)
{
	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		mmu_partition_table_set_entry(lpid, dw0, dw1);
	} else {
		pseries_partition_tb[lpid].patb0 = cpu_to_be64(dw0);
		pseries_partition_tb[lpid].patb1 = cpu_to_be64(dw1);
	}
}

static void kvmhv_set_nested_ptbl(struct kvm_nested_guest *gp)
{
	unsigned long dw0;

	dw0 = PATB_HR | radix__get_tree_size() |
		__pa(gp->shadow_pgtable) | RADIX_PGD_INDEX_SIZE;
	kvmhv_set_ptbl_entry(gp->shadow_lpid, dw0, gp->process_table);
}

void kvmhv_vm_nested_init(struct kvm *kvm)
{
	kvm->arch.max_nested_lpid = -1;
}

/*
 * Handle the H_SET_PARTITION_TABLE hcall.
 * r4 = guest real address of partition table + log_2(size) - 12
 * (formatted as for the PTCR).
 */
long kvmhv_set_partition_table(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long ptcr = kvmppc_get_gpr(vcpu, 4);
	int srcu_idx;
	long ret = H_SUCCESS;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	/*
	 * Limit the partition table to 4096 entries (because that's what
	 * hardware supports), and check the base address.
	 */
	if ((ptcr & PRTS_MASK) > 12 - 8 ||
	    !kvm_is_visible_gfn(vcpu->kvm, (ptcr & PRTB_MASK) >> PAGE_SHIFT))
		ret = H_PARAMETER;
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	if (ret == H_SUCCESS)
		kvm->arch.l1_ptcr = ptcr;
	return ret;
}

/*
 * Reload the partition table entry for a guest.
 * Caller must hold gp->tlb_lock.
 */
static void kvmhv_update_ptbl_cache(struct kvm_nested_guest *gp)
{
	int ret;
	struct patb_entry ptbl_entry;
	unsigned long ptbl_addr;
	struct kvm *kvm = gp->l1_host;

	ret = -EFAULT;
	ptbl_addr = (kvm->arch.l1_ptcr & PRTB_MASK) + (gp->l1_lpid << 4);
	if (gp->l1_lpid < (1ul << ((kvm->arch.l1_ptcr & PRTS_MASK) + 8)))
		ret = kvm_read_guest(kvm, ptbl_addr,
				     &ptbl_entry, sizeof(ptbl_entry));
	if (ret) {
		gp->l1_gr_to_hr = 0;
		gp->process_table = 0;
	} else {
		gp->l1_gr_to_hr = be64_to_cpu(ptbl_entry.patb0);
		gp->process_table = be64_to_cpu(ptbl_entry.patb1);
	}
	kvmhv_set_nested_ptbl(gp);
}

struct kvm_nested_guest *kvmhv_alloc_nested(struct kvm *kvm, unsigned int lpid)
{
	struct kvm_nested_guest *gp;
	long shadow_lpid;

	gp = kzalloc(sizeof(*gp), GFP_KERNEL);
	if (!gp)
		return NULL;
	gp->l1_host = kvm;
	gp->l1_lpid = lpid;
	mutex_init(&gp->tlb_lock);
	gp->shadow_pgtable = pgd_alloc(kvm->mm);
	if (!gp->shadow_pgtable)
		goto out_free;
	shadow_lpid = kvmppc_alloc_lpid();
	if (shadow_lpid < 0)
		goto out_free2;
	gp->shadow_lpid = shadow_lpid;

	return gp;

 out_free2:
	pgd_free(kvm->mm, gp->shadow_pgtable);
 out_free:
	kfree(gp);
	return NULL;
}

/*
 * Free up any resources allocated for a nested guest.
 */
static void kvmhv_release_nested(struct kvm_nested_guest *gp)
{
	kvmhv_set_ptbl_entry(gp->shadow_lpid, 0, 0);
	kvmppc_free_lpid(gp->shadow_lpid);
	if (gp->shadow_pgtable)
		pgd_free(gp->l1_host->mm, gp->shadow_pgtable);
	kfree(gp);
}

static void kvmhv_remove_nested(struct kvm_nested_guest *gp)
{
	struct kvm *kvm = gp->l1_host;
	int lpid = gp->l1_lpid;
	long ref;

	spin_lock(&kvm->mmu_lock);
	if (gp == kvm->arch.nested_guests[lpid]) {
		kvm->arch.nested_guests[lpid] = NULL;
		if (lpid == kvm->arch.max_nested_lpid) {
			while (--lpid >= 0 && !kvm->arch.nested_guests[lpid])
				;
			kvm->arch.max_nested_lpid = lpid;
		}
		--gp->refcnt;
	}
	ref = gp->refcnt;
	spin_unlock(&kvm->mmu_lock);
	if (ref == 0)
		kvmhv_release_nested(gp);
}

/*
 * Free up all nested resources allocated for this guest.
 * This is called with no vcpus of the guest running, when
 * switching the guest to HPT mode or when destroying the
 * guest.
 */
void kvmhv_release_all_nested(struct kvm *kvm)
{
	int i;
	struct kvm_nested_guest *gp;
	struct kvm_nested_guest *freelist = NULL;

	spin_lock(&kvm->mmu_lock);
	for (i = 0; i <= kvm->arch.max_nested_lpid; i++) {
		gp = kvm->arch.nested_guests[i];
		if (!gp)
			continue;
		kvm->arch.nested_guests[i] = NULL;
		if (--gp->refcnt == 0) {
			gp->next = freelist;
			freelist = gp;
		}
	}
	kvm->arch.max_nested_lpid = -1;
	spin_unlock(&kvm->mmu_lock);
	while ((gp = freelist) != NULL) {
		freelist = gp->next;
		kvmhv_release_nested(gp);
	}
}

/* caller must hold gp->tlb_lock */
void kvmhv_flush_nested(struct kvm_nested_guest *gp)
{
	kvmhv_update_ptbl_cache(gp);
	if (gp->l1_gr_to_hr == 0)
		kvmhv_remove_nested(gp);
}

struct kvm_nested_guest *kvmhv_get_nested(struct kvm *kvm, int l1_lpid,
					  bool create)
{
	struct kvm_nested_guest *gp, *newgp;

	if (l1_lpid >= KVM_MAX_NESTED_GUESTS ||
	    l1_lpid >= (1ul << ((kvm->arch.l1_ptcr & PRTS_MASK) + 12 - 4)))
		return NULL;

	spin_lock(&kvm->mmu_lock);
	gp = kvm->arch.nested_guests[l1_lpid];
	if (gp)
		++gp->refcnt;
	spin_unlock(&kvm->mmu_lock);

	if (gp || !create)
		return gp;

	newgp = kvmhv_alloc_nested(kvm, l1_lpid);
	if (!newgp)
		return NULL;
	spin_lock(&kvm->mmu_lock);
	if (kvm->arch.nested_guests[l1_lpid]) {
		/* someone else beat us to it */
		gp = kvm->arch.nested_guests[l1_lpid];
	} else {
		kvm->arch.nested_guests[l1_lpid] = newgp;
		++newgp->refcnt;
		gp = newgp;
		newgp = NULL;
		if (l1_lpid > kvm->arch.max_nested_lpid)
			kvm->arch.max_nested_lpid = l1_lpid;
	}
	++gp->refcnt;
	spin_unlock(&kvm->mmu_lock);

	if (newgp)
		kvmhv_release_nested(newgp);

	return gp;
}

void kvmhv_put_nested(struct kvm_nested_guest *gp)
{
	struct kvm *kvm = gp->l1_host;
	long ref;

	spin_lock(&kvm->mmu_lock);
	ref = --gp->refcnt;
	spin_unlock(&kvm->mmu_lock);
	if (ref == 0)
		kvmhv_release_nested(gp);
}

long kvmhv_nested_page_fault(struct kvm_vcpu *vcpu)
{
	return RESUME_HOST;
}
