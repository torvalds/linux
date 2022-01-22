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
#include <linux/llist.h>
#include <linux/pgtable.h>

#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu.h>
#include <asm/pgalloc.h>
#include <asm/pte-walk.h>
#include <asm/reg.h>
#include <asm/plpar_wrappers.h>

static struct patb_entry *pseries_partition_tb;

static void kvmhv_update_ptbl_cache(struct kvm_nested_guest *gp);
static void kvmhv_free_memslot_nest_rmap(struct kvm_memory_slot *free);

void kvmhv_save_hv_regs(struct kvm_vcpu *vcpu, struct hv_guest_state *hr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	hr->pcr = vc->pcr | PCR_MASK;
	hr->dpdes = vc->dpdes;
	hr->hfscr = vcpu->arch.hfscr;
	hr->tb_offset = vc->tb_offset;
	hr->dawr0 = vcpu->arch.dawr0;
	hr->dawrx0 = vcpu->arch.dawrx0;
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
	hr->dawr1 = vcpu->arch.dawr1;
	hr->dawrx1 = vcpu->arch.dawrx1;
}

/* Use noinline_for_stack due to https://bugs.llvm.org/show_bug.cgi?id=49610 */
static noinline_for_stack void byteswap_pt_regs(struct pt_regs *regs)
{
	unsigned long *addr = (unsigned long *) regs;

	for (; addr < ((unsigned long *) (regs + 1)); addr++)
		*addr = swab64(*addr);
}

static void byteswap_hv_regs(struct hv_guest_state *hr)
{
	hr->version = swab64(hr->version);
	hr->lpid = swab32(hr->lpid);
	hr->vcpu_token = swab32(hr->vcpu_token);
	hr->lpcr = swab64(hr->lpcr);
	hr->pcr = swab64(hr->pcr) | PCR_MASK;
	hr->amor = swab64(hr->amor);
	hr->dpdes = swab64(hr->dpdes);
	hr->hfscr = swab64(hr->hfscr);
	hr->tb_offset = swab64(hr->tb_offset);
	hr->dawr0 = swab64(hr->dawr0);
	hr->dawrx0 = swab64(hr->dawrx0);
	hr->ciabr = swab64(hr->ciabr);
	hr->hdec_expiry = swab64(hr->hdec_expiry);
	hr->purr = swab64(hr->purr);
	hr->spurr = swab64(hr->spurr);
	hr->ic = swab64(hr->ic);
	hr->vtb = swab64(hr->vtb);
	hr->hdar = swab64(hr->hdar);
	hr->hdsisr = swab64(hr->hdsisr);
	hr->heir = swab64(hr->heir);
	hr->asdr = swab64(hr->asdr);
	hr->srr0 = swab64(hr->srr0);
	hr->srr1 = swab64(hr->srr1);
	hr->sprg[0] = swab64(hr->sprg[0]);
	hr->sprg[1] = swab64(hr->sprg[1]);
	hr->sprg[2] = swab64(hr->sprg[2]);
	hr->sprg[3] = swab64(hr->sprg[3]);
	hr->pidr = swab64(hr->pidr);
	hr->cfar = swab64(hr->cfar);
	hr->ppr = swab64(hr->ppr);
	hr->dawr1 = swab64(hr->dawr1);
	hr->dawrx1 = swab64(hr->dawrx1);
}

static void save_hv_return_state(struct kvm_vcpu *vcpu,
				 struct hv_guest_state *hr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	hr->dpdes = vc->dpdes;
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
	switch (vcpu->arch.trap) {
	case BOOK3S_INTERRUPT_H_DATA_STORAGE:
		hr->hdar = vcpu->arch.fault_dar;
		hr->hdsisr = vcpu->arch.fault_dsisr;
		hr->asdr = vcpu->arch.fault_gpa;
		break;
	case BOOK3S_INTERRUPT_H_INST_STORAGE:
		hr->asdr = vcpu->arch.fault_gpa;
		break;
	case BOOK3S_INTERRUPT_H_FAC_UNAVAIL:
		hr->hfscr = ((~HFSCR_INTR_CAUSE & hr->hfscr) |
			     (HFSCR_INTR_CAUSE & vcpu->arch.hfscr));
		break;
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
		hr->heir = vcpu->arch.emul_inst;
		break;
	}
}

static void restore_hv_regs(struct kvm_vcpu *vcpu, const struct hv_guest_state *hr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;

	vc->pcr = hr->pcr | PCR_MASK;
	vc->dpdes = hr->dpdes;
	vcpu->arch.hfscr = hr->hfscr;
	vcpu->arch.dawr0 = hr->dawr0;
	vcpu->arch.dawrx0 = hr->dawrx0;
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
	vcpu->arch.dawr1 = hr->dawr1;
	vcpu->arch.dawrx1 = hr->dawrx1;
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

static void kvmhv_nested_mmio_needed(struct kvm_vcpu *vcpu, u64 regs_ptr)
{
	/* No need to reflect the page fault to L1, we've handled it */
	vcpu->arch.trap = 0;

	/*
	 * Since the L2 gprs have already been written back into L1 memory when
	 * we complete the mmio, store the L1 memory location of the L2 gpr
	 * being loaded into by the mmio so that the loaded value can be
	 * written there in kvmppc_complete_mmio_load()
	 */
	if (((vcpu->arch.io_gpr & KVM_MMIO_REG_EXT_MASK) == KVM_MMIO_REG_GPR)
	    && (vcpu->mmio_is_write == 0)) {
		vcpu->arch.nested_io_gpr = (gpa_t) regs_ptr +
					   offsetof(struct pt_regs,
						    gpr[vcpu->arch.io_gpr]);
		vcpu->arch.io_gpr = KVM_MMIO_REG_NESTED_GPR;
	}
}

static int kvmhv_read_guest_state_and_regs(struct kvm_vcpu *vcpu,
					   struct hv_guest_state *l2_hv,
					   struct pt_regs *l2_regs,
					   u64 hv_ptr, u64 regs_ptr)
{
	int size;

	if (kvm_vcpu_read_guest(vcpu, hv_ptr, &l2_hv->version,
				sizeof(l2_hv->version)))
		return -1;

	if (kvmppc_need_byteswap(vcpu))
		l2_hv->version = swab64(l2_hv->version);

	size = hv_guest_state_size(l2_hv->version);
	if (size < 0)
		return -1;

	return kvm_vcpu_read_guest(vcpu, hv_ptr, l2_hv, size) ||
		kvm_vcpu_read_guest(vcpu, regs_ptr, l2_regs,
				    sizeof(struct pt_regs));
}

static int kvmhv_write_guest_state_and_regs(struct kvm_vcpu *vcpu,
					    struct hv_guest_state *l2_hv,
					    struct pt_regs *l2_regs,
					    u64 hv_ptr, u64 regs_ptr)
{
	int size;

	size = hv_guest_state_size(l2_hv->version);
	if (size < 0)
		return -1;

	return kvm_vcpu_write_guest(vcpu, hv_ptr, l2_hv, size) ||
		kvm_vcpu_write_guest(vcpu, regs_ptr, l2_regs,
				     sizeof(struct pt_regs));
}

static void load_l2_hv_regs(struct kvm_vcpu *vcpu,
			    const struct hv_guest_state *l2_hv,
			    const struct hv_guest_state *l1_hv, u64 *lpcr)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	u64 mask;

	restore_hv_regs(vcpu, l2_hv);

	/*
	 * Don't let L1 change LPCR bits for the L2 except these:
	 */
	mask = LPCR_DPFD | LPCR_ILE | LPCR_TC | LPCR_AIL | LPCR_LD |
		LPCR_LPES | LPCR_MER;

	/*
	 * Additional filtering is required depending on hardware
	 * and configuration.
	 */
	*lpcr = kvmppc_filter_lpcr_hv(vcpu->kvm,
				      (vc->lpcr & ~mask) | (*lpcr & mask));

	/*
	 * Don't let L1 enable features for L2 which we don't allow for L1,
	 * but preserve the interrupt cause field.
	 */
	vcpu->arch.hfscr = l2_hv->hfscr & (HFSCR_INTR_CAUSE | vcpu->arch.hfscr_permitted);

	/* Don't let data address watchpoint match in hypervisor state */
	vcpu->arch.dawrx0 = l2_hv->dawrx0 & ~DAWRX_HYP;
	vcpu->arch.dawrx1 = l2_hv->dawrx1 & ~DAWRX_HYP;

	/* Don't let completed instruction address breakpt match in HV state */
	if ((l2_hv->ciabr & CIABR_PRIV) == CIABR_PRIV_HYPER)
		vcpu->arch.ciabr = l2_hv->ciabr & ~CIABR_PRIV;
}

long kvmhv_enter_nested_guest(struct kvm_vcpu *vcpu)
{
	long int err, r;
	struct kvm_nested_guest *l2;
	struct pt_regs l2_regs, saved_l1_regs;
	struct hv_guest_state l2_hv = {0}, saved_l1_hv;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	u64 hv_ptr, regs_ptr;
	u64 hdec_exp, lpcr;
	s64 delta_purr, delta_spurr, delta_ic, delta_vtb;

	if (vcpu->kvm->arch.l1_ptcr == 0)
		return H_NOT_AVAILABLE;

	if (MSR_TM_TRANSACTIONAL(vcpu->arch.shregs.msr))
		return H_BAD_MODE;

	/* copy parameters in */
	hv_ptr = kvmppc_get_gpr(vcpu, 4);
	regs_ptr = kvmppc_get_gpr(vcpu, 5);
	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
	err = kvmhv_read_guest_state_and_regs(vcpu, &l2_hv, &l2_regs,
					      hv_ptr, regs_ptr);
	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	if (err)
		return H_PARAMETER;

	if (kvmppc_need_byteswap(vcpu))
		byteswap_hv_regs(&l2_hv);
	if (l2_hv.version > HV_GUEST_STATE_VERSION)
		return H_P2;

	if (kvmppc_need_byteswap(vcpu))
		byteswap_pt_regs(&l2_regs);
	if (l2_hv.vcpu_token >= NR_CPUS)
		return H_PARAMETER;

	/*
	 * L1 must have set up a suspended state to enter the L2 in a
	 * transactional state, and only in that case. These have to be
	 * filtered out here to prevent causing a TM Bad Thing in the
	 * host HRFID. We could synthesize a TM Bad Thing back to the L1
	 * here but there doesn't seem like much point.
	 */
	if (MSR_TM_SUSPENDED(vcpu->arch.shregs.msr)) {
		if (!MSR_TM_ACTIVE(l2_regs.msr))
			return H_BAD_MODE;
	} else {
		if (l2_regs.msr & MSR_TS_MASK)
			return H_BAD_MODE;
		if (WARN_ON_ONCE(vcpu->arch.shregs.msr & MSR_TS_MASK))
			return H_BAD_MODE;
	}

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
	vcpu->arch.nested_hfscr = l2_hv.hfscr;
	vcpu->arch.regs = l2_regs;

	/* Guest must always run with ME enabled, HV disabled. */
	vcpu->arch.shregs.msr = (vcpu->arch.regs.msr | MSR_ME) & ~MSR_HV;

	lpcr = l2_hv.lpcr;
	load_l2_hv_regs(vcpu, &l2_hv, &saved_l1_hv, &lpcr);

	vcpu->arch.ret = RESUME_GUEST;
	vcpu->arch.trap = 0;
	do {
		if (mftb() >= hdec_exp) {
			vcpu->arch.trap = BOOK3S_INTERRUPT_HV_DECREMENTER;
			r = RESUME_HOST;
			break;
		}
		r = kvmhv_run_single_vcpu(vcpu, hdec_exp, lpcr);
	} while (is_kvmppc_resume_guest(r));

	/* save L2 state for return */
	l2_regs = vcpu->arch.regs;
	l2_regs.msr = vcpu->arch.shregs.msr;
	delta_purr = vcpu->arch.purr - l2_hv.purr;
	delta_spurr = vcpu->arch.spurr - l2_hv.spurr;
	delta_ic = vcpu->arch.ic - l2_hv.ic;
	delta_vtb = vc->vtb - l2_hv.vtb;
	save_hv_return_state(vcpu, &l2_hv);

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
	if (kvmppc_need_byteswap(vcpu)) {
		byteswap_hv_regs(&l2_hv);
		byteswap_pt_regs(&l2_regs);
	}
	vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
	err = kvmhv_write_guest_state_and_regs(vcpu, &l2_hv, &l2_regs,
					       hv_ptr, regs_ptr);
	srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
	if (err)
		return H_AUTHORITY;

	if (r == -EINTR)
		return H_INTERRUPT;

	if (vcpu->mmio_needed) {
		kvmhv_nested_mmio_needed(vcpu, regs_ptr);
		return H_TOO_HARD;
	}

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

static void kvmhv_flush_lpid(unsigned int lpid)
{
	long rc;

	if (!kvmhv_on_pseries()) {
		radix__flush_all_lpid(lpid);
		return;
	}

	if (!firmware_has_feature(FW_FEATURE_RPT_INVALIDATE))
		rc = plpar_hcall_norets(H_TLB_INVALIDATE, H_TLBIE_P1_ENC(2, 0, 1),
					lpid, TLBIEL_INVAL_SET_LPID);
	else
		rc = pseries_rpt_invalidate(lpid, H_RPTI_TARGET_CMMU,
					    H_RPTI_TYPE_NESTED |
					    H_RPTI_TYPE_TLB | H_RPTI_TYPE_PWC |
					    H_RPTI_TYPE_PAT,
					    H_RPTI_PAGE_ALL, 0, -1UL);
	if (rc)
		pr_err("KVM: TLB LPID invalidation hcall failed, rc=%ld\n", rc);
}

void kvmhv_set_ptbl_entry(unsigned int lpid, u64 dw0, u64 dw1)
{
	if (!kvmhv_on_pseries()) {
		mmu_partition_table_set_entry(lpid, dw0, dw1, true);
		return;
	}

	pseries_partition_tb[lpid].patb0 = cpu_to_be64(dw0);
	pseries_partition_tb[lpid].patb1 = cpu_to_be64(dw1);
	/* L0 will do the necessary barriers */
	kvmhv_flush_lpid(lpid);
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
 * Handle the H_COPY_TOFROM_GUEST hcall.
 * r4 = L1 lpid of nested guest
 * r5 = pid
 * r6 = eaddr to access
 * r7 = to buffer (L1 gpa)
 * r8 = from buffer (L1 gpa)
 * r9 = n bytes to copy
 */
long kvmhv_copy_tofrom_guest_nested(struct kvm_vcpu *vcpu)
{
	struct kvm_nested_guest *gp;
	int l1_lpid = kvmppc_get_gpr(vcpu, 4);
	int pid = kvmppc_get_gpr(vcpu, 5);
	gva_t eaddr = kvmppc_get_gpr(vcpu, 6);
	gpa_t gp_to = (gpa_t) kvmppc_get_gpr(vcpu, 7);
	gpa_t gp_from = (gpa_t) kvmppc_get_gpr(vcpu, 8);
	void *buf;
	unsigned long n = kvmppc_get_gpr(vcpu, 9);
	bool is_load = !!gp_to;
	long rc;

	if (gp_to && gp_from) /* One must be NULL to determine the direction */
		return H_PARAMETER;

	if (eaddr & (0xFFFUL << 52))
		return H_PARAMETER;

	buf = kzalloc(n, GFP_KERNEL | __GFP_NOWARN);
	if (!buf)
		return H_NO_MEM;

	gp = kvmhv_get_nested(vcpu->kvm, l1_lpid, false);
	if (!gp) {
		rc = H_PARAMETER;
		goto out_free;
	}

	mutex_lock(&gp->tlb_lock);

	if (is_load) {
		/* Load from the nested guest into our buffer */
		rc = __kvmhv_copy_tofrom_guest_radix(gp->shadow_lpid, pid,
						     eaddr, buf, NULL, n);
		if (rc)
			goto not_found;

		/* Write what was loaded into our buffer back to the L1 guest */
		vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		rc = kvm_vcpu_write_guest(vcpu, gp_to, buf, n);
		srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
		if (rc)
			goto not_found;
	} else {
		/* Load the data to be stored from the L1 guest into our buf */
		vcpu->srcu_idx = srcu_read_lock(&vcpu->kvm->srcu);
		rc = kvm_vcpu_read_guest(vcpu, gp_from, buf, n);
		srcu_read_unlock(&vcpu->kvm->srcu, vcpu->srcu_idx);
		if (rc)
			goto not_found;

		/* Store from our buffer into the nested guest */
		rc = __kvmhv_copy_tofrom_guest_radix(gp->shadow_lpid, pid,
						     eaddr, NULL, buf, n);
		if (rc)
			goto not_found;
	}

out_unlock:
	mutex_unlock(&gp->tlb_lock);
	kvmhv_put_nested(gp);
out_free:
	kfree(buf);
	return rc;
not_found:
	rc = H_NOT_FOUND;
	goto out_unlock;
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
	if (gp->l1_lpid < (1ul << ((kvm->arch.l1_ptcr & PRTS_MASK) + 8))) {
		int srcu_idx = srcu_read_lock(&kvm->srcu);
		ret = kvm_read_guest(kvm, ptbl_addr,
				     &ptbl_entry, sizeof(ptbl_entry));
		srcu_read_unlock(&kvm->srcu, srcu_idx);
	}
	if (ret) {
		gp->l1_gr_to_hr = 0;
		gp->process_table = 0;
	} else {
		gp->l1_gr_to_hr = be64_to_cpu(ptbl_entry.patb0);
		gp->process_table = be64_to_cpu(ptbl_entry.patb1);
	}
	kvmhv_set_nested_ptbl(gp);
}

static struct kvm_nested_guest *kvmhv_alloc_nested(struct kvm *kvm, unsigned int lpid)
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
	gp->radix = 1;

	memset(gp->prev_cpu, -1, sizeof(gp->prev_cpu));

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
	struct kvm *kvm = gp->l1_host;

	if (gp->shadow_pgtable) {
		/*
		 * No vcpu is using this struct and no call to
		 * kvmhv_get_nested can find this struct,
		 * so we don't need to hold kvm->mmu_lock.
		 */
		kvmppc_free_pgtable_radix(kvm, gp->shadow_pgtable,
					  gp->shadow_lpid);
		pgd_free(kvm->mm, gp->shadow_pgtable);
	}
	kvmhv_set_ptbl_entry(gp->shadow_lpid, 0, 0);
	kvmppc_free_lpid(gp->shadow_lpid);
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
	struct kvm_memory_slot *memslot;
	int srcu_idx;

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

	srcu_idx = srcu_read_lock(&kvm->srcu);
	kvm_for_each_memslot(memslot, kvm_memslots(kvm))
		kvmhv_free_memslot_nest_rmap(memslot);
	srcu_read_unlock(&kvm->srcu, srcu_idx);
}

/* caller must hold gp->tlb_lock */
static void kvmhv_flush_nested(struct kvm_nested_guest *gp)
{
	struct kvm *kvm = gp->l1_host;

	spin_lock(&kvm->mmu_lock);
	kvmppc_free_pgtable_radix(kvm, gp->shadow_pgtable, gp->shadow_lpid);
	spin_unlock(&kvm->mmu_lock);
	kvmhv_flush_lpid(gp->shadow_lpid);
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

static struct kvm_nested_guest *kvmhv_find_nested(struct kvm *kvm, int lpid)
{
	if (lpid > kvm->arch.max_nested_lpid)
		return NULL;
	return kvm->arch.nested_guests[lpid];
}

pte_t *find_kvm_nested_guest_pte(struct kvm *kvm, unsigned long lpid,
				 unsigned long ea, unsigned *hshift)
{
	struct kvm_nested_guest *gp;
	pte_t *pte;

	gp = kvmhv_find_nested(kvm, lpid);
	if (!gp)
		return NULL;

	VM_WARN(!spin_is_locked(&kvm->mmu_lock),
		"%s called with kvm mmu_lock not held \n", __func__);
	pte = __find_linux_pte(gp->shadow_pgtable, ea, NULL, hshift);

	return pte;
}

static inline bool kvmhv_n_rmap_is_equal(u64 rmap_1, u64 rmap_2)
{
	return !((rmap_1 ^ rmap_2) & (RMAP_NESTED_LPID_MASK |
				       RMAP_NESTED_GPA_MASK));
}

void kvmhv_insert_nest_rmap(struct kvm *kvm, unsigned long *rmapp,
			    struct rmap_nested **n_rmap)
{
	struct llist_node *entry = ((struct llist_head *) rmapp)->first;
	struct rmap_nested *cursor;
	u64 rmap, new_rmap = (*n_rmap)->rmap;

	/* Are there any existing entries? */
	if (!(*rmapp)) {
		/* No -> use the rmap as a single entry */
		*rmapp = new_rmap | RMAP_NESTED_IS_SINGLE_ENTRY;
		return;
	}

	/* Do any entries match what we're trying to insert? */
	for_each_nest_rmap_safe(cursor, entry, &rmap) {
		if (kvmhv_n_rmap_is_equal(rmap, new_rmap))
			return;
	}

	/* Do we need to create a list or just add the new entry? */
	rmap = *rmapp;
	if (rmap & RMAP_NESTED_IS_SINGLE_ENTRY) /* Not previously a list */
		*rmapp = 0UL;
	llist_add(&((*n_rmap)->list), (struct llist_head *) rmapp);
	if (rmap & RMAP_NESTED_IS_SINGLE_ENTRY) /* Not previously a list */
		(*n_rmap)->list.next = (struct llist_node *) rmap;

	/* Set NULL so not freed by caller */
	*n_rmap = NULL;
}

static void kvmhv_update_nest_rmap_rc(struct kvm *kvm, u64 n_rmap,
				      unsigned long clr, unsigned long set,
				      unsigned long hpa, unsigned long mask)
{
	unsigned long gpa;
	unsigned int shift, lpid;
	pte_t *ptep;

	gpa = n_rmap & RMAP_NESTED_GPA_MASK;
	lpid = (n_rmap & RMAP_NESTED_LPID_MASK) >> RMAP_NESTED_LPID_SHIFT;

	/* Find the pte */
	ptep = find_kvm_nested_guest_pte(kvm, lpid, gpa, &shift);
	/*
	 * If the pte is present and the pfn is still the same, update the pte.
	 * If the pfn has changed then this is a stale rmap entry, the nested
	 * gpa actually points somewhere else now, and there is nothing to do.
	 * XXX A future optimisation would be to remove the rmap entry here.
	 */
	if (ptep && pte_present(*ptep) && ((pte_val(*ptep) & mask) == hpa)) {
		__radix_pte_update(ptep, clr, set);
		kvmppc_radix_tlbie_page(kvm, gpa, shift, lpid);
	}
}

/*
 * For a given list of rmap entries, update the rc bits in all ptes in shadow
 * page tables for nested guests which are referenced by the rmap list.
 */
void kvmhv_update_nest_rmap_rc_list(struct kvm *kvm, unsigned long *rmapp,
				    unsigned long clr, unsigned long set,
				    unsigned long hpa, unsigned long nbytes)
{
	struct llist_node *entry = ((struct llist_head *) rmapp)->first;
	struct rmap_nested *cursor;
	unsigned long rmap, mask;

	if ((clr | set) & ~(_PAGE_DIRTY | _PAGE_ACCESSED))
		return;

	mask = PTE_RPN_MASK & ~(nbytes - 1);
	hpa &= mask;

	for_each_nest_rmap_safe(cursor, entry, &rmap)
		kvmhv_update_nest_rmap_rc(kvm, rmap, clr, set, hpa, mask);
}

static void kvmhv_remove_nest_rmap(struct kvm *kvm, u64 n_rmap,
				   unsigned long hpa, unsigned long mask)
{
	struct kvm_nested_guest *gp;
	unsigned long gpa;
	unsigned int shift, lpid;
	pte_t *ptep;

	gpa = n_rmap & RMAP_NESTED_GPA_MASK;
	lpid = (n_rmap & RMAP_NESTED_LPID_MASK) >> RMAP_NESTED_LPID_SHIFT;
	gp = kvmhv_find_nested(kvm, lpid);
	if (!gp)
		return;

	/* Find and invalidate the pte */
	ptep = find_kvm_nested_guest_pte(kvm, lpid, gpa, &shift);
	/* Don't spuriously invalidate ptes if the pfn has changed */
	if (ptep && pte_present(*ptep) && ((pte_val(*ptep) & mask) == hpa))
		kvmppc_unmap_pte(kvm, ptep, gpa, shift, NULL, gp->shadow_lpid);
}

static void kvmhv_remove_nest_rmap_list(struct kvm *kvm, unsigned long *rmapp,
					unsigned long hpa, unsigned long mask)
{
	struct llist_node *entry = llist_del_all((struct llist_head *) rmapp);
	struct rmap_nested *cursor;
	unsigned long rmap;

	for_each_nest_rmap_safe(cursor, entry, &rmap) {
		kvmhv_remove_nest_rmap(kvm, rmap, hpa, mask);
		kfree(cursor);
	}
}

/* called with kvm->mmu_lock held */
void kvmhv_remove_nest_rmap_range(struct kvm *kvm,
				  const struct kvm_memory_slot *memslot,
				  unsigned long gpa, unsigned long hpa,
				  unsigned long nbytes)
{
	unsigned long gfn, end_gfn;
	unsigned long addr_mask;

	if (!memslot)
		return;
	gfn = (gpa >> PAGE_SHIFT) - memslot->base_gfn;
	end_gfn = gfn + (nbytes >> PAGE_SHIFT);

	addr_mask = PTE_RPN_MASK & ~(nbytes - 1);
	hpa &= addr_mask;

	for (; gfn < end_gfn; gfn++) {
		unsigned long *rmap = &memslot->arch.rmap[gfn];
		kvmhv_remove_nest_rmap_list(kvm, rmap, hpa, addr_mask);
	}
}

static void kvmhv_free_memslot_nest_rmap(struct kvm_memory_slot *free)
{
	unsigned long page;

	for (page = 0; page < free->npages; page++) {
		unsigned long rmap, *rmapp = &free->arch.rmap[page];
		struct rmap_nested *cursor;
		struct llist_node *entry;

		entry = llist_del_all((struct llist_head *) rmapp);
		for_each_nest_rmap_safe(cursor, entry, &rmap)
			kfree(cursor);
	}
}

static bool kvmhv_invalidate_shadow_pte(struct kvm_vcpu *vcpu,
					struct kvm_nested_guest *gp,
					long gpa, int *shift_ret)
{
	struct kvm *kvm = vcpu->kvm;
	bool ret = false;
	pte_t *ptep;
	int shift;

	spin_lock(&kvm->mmu_lock);
	ptep = find_kvm_nested_guest_pte(kvm, gp->l1_lpid, gpa, &shift);
	if (!shift)
		shift = PAGE_SHIFT;
	if (ptep && pte_present(*ptep)) {
		kvmppc_unmap_pte(kvm, ptep, gpa, shift, NULL, gp->shadow_lpid);
		ret = true;
	}
	spin_unlock(&kvm->mmu_lock);

	if (shift_ret)
		*shift_ret = shift;
	return ret;
}

static inline int get_ric(unsigned int instr)
{
	return (instr >> 18) & 0x3;
}

static inline int get_prs(unsigned int instr)
{
	return (instr >> 17) & 0x1;
}

static inline int get_r(unsigned int instr)
{
	return (instr >> 16) & 0x1;
}

static inline int get_lpid(unsigned long r_val)
{
	return r_val & 0xffffffff;
}

static inline int get_is(unsigned long r_val)
{
	return (r_val >> 10) & 0x3;
}

static inline int get_ap(unsigned long r_val)
{
	return (r_val >> 5) & 0x7;
}

static inline long get_epn(unsigned long r_val)
{
	return r_val >> 12;
}

static int kvmhv_emulate_tlbie_tlb_addr(struct kvm_vcpu *vcpu, int lpid,
					int ap, long epn)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_nested_guest *gp;
	long npages;
	int shift, shadow_shift;
	unsigned long addr;

	shift = ap_to_shift(ap);
	addr = epn << 12;
	if (shift < 0)
		/* Invalid ap encoding */
		return -EINVAL;

	addr &= ~((1UL << shift) - 1);
	npages = 1UL << (shift - PAGE_SHIFT);

	gp = kvmhv_get_nested(kvm, lpid, false);
	if (!gp) /* No such guest -> nothing to do */
		return 0;
	mutex_lock(&gp->tlb_lock);

	/* There may be more than one host page backing this single guest pte */
	do {
		kvmhv_invalidate_shadow_pte(vcpu, gp, addr, &shadow_shift);

		npages -= 1UL << (shadow_shift - PAGE_SHIFT);
		addr += 1UL << shadow_shift;
	} while (npages > 0);

	mutex_unlock(&gp->tlb_lock);
	kvmhv_put_nested(gp);
	return 0;
}

static void kvmhv_emulate_tlbie_lpid(struct kvm_vcpu *vcpu,
				     struct kvm_nested_guest *gp, int ric)
{
	struct kvm *kvm = vcpu->kvm;

	mutex_lock(&gp->tlb_lock);
	switch (ric) {
	case 0:
		/* Invalidate TLB */
		spin_lock(&kvm->mmu_lock);
		kvmppc_free_pgtable_radix(kvm, gp->shadow_pgtable,
					  gp->shadow_lpid);
		kvmhv_flush_lpid(gp->shadow_lpid);
		spin_unlock(&kvm->mmu_lock);
		break;
	case 1:
		/*
		 * Invalidate PWC
		 * We don't cache this -> nothing to do
		 */
		break;
	case 2:
		/* Invalidate TLB, PWC and caching of partition table entries */
		kvmhv_flush_nested(gp);
		break;
	default:
		break;
	}
	mutex_unlock(&gp->tlb_lock);
}

static void kvmhv_emulate_tlbie_all_lpid(struct kvm_vcpu *vcpu, int ric)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_nested_guest *gp;
	int i;

	spin_lock(&kvm->mmu_lock);
	for (i = 0; i <= kvm->arch.max_nested_lpid; i++) {
		gp = kvm->arch.nested_guests[i];
		if (gp) {
			spin_unlock(&kvm->mmu_lock);
			kvmhv_emulate_tlbie_lpid(vcpu, gp, ric);
			spin_lock(&kvm->mmu_lock);
		}
	}
	spin_unlock(&kvm->mmu_lock);
}

static int kvmhv_emulate_priv_tlbie(struct kvm_vcpu *vcpu, unsigned int instr,
				    unsigned long rsval, unsigned long rbval)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_nested_guest *gp;
	int r, ric, prs, is, ap;
	int lpid;
	long epn;
	int ret = 0;

	ric = get_ric(instr);
	prs = get_prs(instr);
	r = get_r(instr);
	lpid = get_lpid(rsval);
	is = get_is(rbval);

	/*
	 * These cases are invalid and are not handled:
	 * r   != 1 -> Only radix supported
	 * prs == 1 -> Not HV privileged
	 * ric == 3 -> No cluster bombs for radix
	 * is  == 1 -> Partition scoped translations not associated with pid
	 * (!is) && (ric == 1 || ric == 2) -> Not supported by ISA
	 */
	if ((!r) || (prs) || (ric == 3) || (is == 1) ||
	    ((!is) && (ric == 1 || ric == 2)))
		return -EINVAL;

	switch (is) {
	case 0:
		/*
		 * We know ric == 0
		 * Invalidate TLB for a given target address
		 */
		epn = get_epn(rbval);
		ap = get_ap(rbval);
		ret = kvmhv_emulate_tlbie_tlb_addr(vcpu, lpid, ap, epn);
		break;
	case 2:
		/* Invalidate matching LPID */
		gp = kvmhv_get_nested(kvm, lpid, false);
		if (gp) {
			kvmhv_emulate_tlbie_lpid(vcpu, gp, ric);
			kvmhv_put_nested(gp);
		}
		break;
	case 3:
		/* Invalidate ALL LPIDs */
		kvmhv_emulate_tlbie_all_lpid(vcpu, ric);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * This handles the H_TLB_INVALIDATE hcall.
 * Parameters are (r4) tlbie instruction code, (r5) rS contents,
 * (r6) rB contents.
 */
long kvmhv_do_nested_tlbie(struct kvm_vcpu *vcpu)
{
	int ret;

	ret = kvmhv_emulate_priv_tlbie(vcpu, kvmppc_get_gpr(vcpu, 4),
			kvmppc_get_gpr(vcpu, 5), kvmppc_get_gpr(vcpu, 6));
	if (ret)
		return H_PARAMETER;
	return H_SUCCESS;
}

static long do_tlb_invalidate_nested_all(struct kvm_vcpu *vcpu,
					 unsigned long lpid, unsigned long ric)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_nested_guest *gp;

	gp = kvmhv_get_nested(kvm, lpid, false);
	if (gp) {
		kvmhv_emulate_tlbie_lpid(vcpu, gp, ric);
		kvmhv_put_nested(gp);
	}
	return H_SUCCESS;
}

/*
 * Number of pages above which we invalidate the entire LPID rather than
 * flush individual pages.
 */
static unsigned long tlb_range_flush_page_ceiling __read_mostly = 33;

static long do_tlb_invalidate_nested_tlb(struct kvm_vcpu *vcpu,
					 unsigned long lpid,
					 unsigned long pg_sizes,
					 unsigned long start,
					 unsigned long end)
{
	int ret = H_P4;
	unsigned long addr, nr_pages;
	struct mmu_psize_def *def;
	unsigned long psize, ap, page_size;
	bool flush_lpid;

	for (psize = 0; psize < MMU_PAGE_COUNT; psize++) {
		def = &mmu_psize_defs[psize];
		if (!(pg_sizes & def->h_rpt_pgsize))
			continue;

		nr_pages = (end - start) >> def->shift;
		flush_lpid = nr_pages > tlb_range_flush_page_ceiling;
		if (flush_lpid)
			return do_tlb_invalidate_nested_all(vcpu, lpid,
							RIC_FLUSH_TLB);
		addr = start;
		ap = mmu_get_ap(psize);
		page_size = 1UL << def->shift;
		do {
			ret = kvmhv_emulate_tlbie_tlb_addr(vcpu, lpid, ap,
						   get_epn(addr));
			if (ret)
				return H_P4;
			addr += page_size;
		} while (addr < end);
	}
	return ret;
}

/*
 * Performs partition-scoped invalidations for nested guests
 * as part of H_RPT_INVALIDATE hcall.
 */
long do_h_rpt_invalidate_pat(struct kvm_vcpu *vcpu, unsigned long lpid,
			     unsigned long type, unsigned long pg_sizes,
			     unsigned long start, unsigned long end)
{
	/*
	 * If L2 lpid isn't valid, we need to return H_PARAMETER.
	 *
	 * However, nested KVM issues a L2 lpid flush call when creating
	 * partition table entries for L2. This happens even before the
	 * corresponding shadow lpid is created in HV which happens in
	 * H_ENTER_NESTED call. Since we can't differentiate this case from
	 * the invalid case, we ignore such flush requests and return success.
	 */
	if (!kvmhv_find_nested(vcpu->kvm, lpid))
		return H_SUCCESS;

	/*
	 * A flush all request can be handled by a full lpid flush only.
	 */
	if ((type & H_RPTI_TYPE_NESTED_ALL) == H_RPTI_TYPE_NESTED_ALL)
		return do_tlb_invalidate_nested_all(vcpu, lpid, RIC_FLUSH_ALL);

	/*
	 * We don't need to handle a PWC flush like process table here,
	 * because intermediate partition scoped table in nested guest doesn't
	 * really have PWC. Only level we have PWC is in L0 and for nested
	 * invalidate at L0 we always do kvm_flush_lpid() which does
	 * radix__flush_all_lpid(). For range invalidate at any level, we
	 * are not removing the higher level page tables and hence there is
	 * no PWC invalidate needed.
	 *
	 * if (type & H_RPTI_TYPE_PWC) {
	 *	ret = do_tlb_invalidate_nested_all(vcpu, lpid, RIC_FLUSH_PWC);
	 *	if (ret)
	 *		return H_P4;
	 * }
	 */

	if (start == 0 && end == -1)
		return do_tlb_invalidate_nested_all(vcpu, lpid, RIC_FLUSH_TLB);

	if (type & H_RPTI_TYPE_TLB)
		return do_tlb_invalidate_nested_tlb(vcpu, lpid, pg_sizes,
						    start, end);
	return H_SUCCESS;
}

/* Used to convert a nested guest real address to a L1 guest real address */
static int kvmhv_translate_addr_nested(struct kvm_vcpu *vcpu,
				       struct kvm_nested_guest *gp,
				       unsigned long n_gpa, unsigned long dsisr,
				       struct kvmppc_pte *gpte_p)
{
	u64 fault_addr, flags = dsisr & DSISR_ISSTORE;
	int ret;

	ret = kvmppc_mmu_walk_radix_tree(vcpu, n_gpa, gpte_p, gp->l1_gr_to_hr,
					 &fault_addr);

	if (ret) {
		/* We didn't find a pte */
		if (ret == -EINVAL) {
			/* Unsupported mmu config */
			flags |= DSISR_UNSUPP_MMU;
		} else if (ret == -ENOENT) {
			/* No translation found */
			flags |= DSISR_NOHPTE;
		} else if (ret == -EFAULT) {
			/* Couldn't access L1 real address */
			flags |= DSISR_PRTABLE_FAULT;
			vcpu->arch.fault_gpa = fault_addr;
		} else {
			/* Unknown error */
			return ret;
		}
		goto forward_to_l1;
	} else {
		/* We found a pte -> check permissions */
		if (dsisr & DSISR_ISSTORE) {
			/* Can we write? */
			if (!gpte_p->may_write) {
				flags |= DSISR_PROTFAULT;
				goto forward_to_l1;
			}
		} else if (vcpu->arch.trap == BOOK3S_INTERRUPT_H_INST_STORAGE) {
			/* Can we execute? */
			if (!gpte_p->may_execute) {
				flags |= SRR1_ISI_N_G_OR_CIP;
				goto forward_to_l1;
			}
		} else {
			/* Can we read? */
			if (!gpte_p->may_read && !gpte_p->may_write) {
				flags |= DSISR_PROTFAULT;
				goto forward_to_l1;
			}
		}
	}

	return 0;

forward_to_l1:
	vcpu->arch.fault_dsisr = flags;
	if (vcpu->arch.trap == BOOK3S_INTERRUPT_H_INST_STORAGE) {
		vcpu->arch.shregs.msr &= SRR1_MSR_BITS;
		vcpu->arch.shregs.msr |= flags;
	}
	return RESUME_HOST;
}

static long kvmhv_handle_nested_set_rc(struct kvm_vcpu *vcpu,
				       struct kvm_nested_guest *gp,
				       unsigned long n_gpa,
				       struct kvmppc_pte gpte,
				       unsigned long dsisr)
{
	struct kvm *kvm = vcpu->kvm;
	bool writing = !!(dsisr & DSISR_ISSTORE);
	u64 pgflags;
	long ret;

	/* Are the rc bits set in the L1 partition scoped pte? */
	pgflags = _PAGE_ACCESSED;
	if (writing)
		pgflags |= _PAGE_DIRTY;
	if (pgflags & ~gpte.rc)
		return RESUME_HOST;

	spin_lock(&kvm->mmu_lock);
	/* Set the rc bit in the pte of our (L0) pgtable for the L1 guest */
	ret = kvmppc_hv_handle_set_rc(kvm, false, writing,
				      gpte.raddr, kvm->arch.lpid);
	if (!ret) {
		ret = -EINVAL;
		goto out_unlock;
	}

	/* Set the rc bit in the pte of the shadow_pgtable for the nest guest */
	ret = kvmppc_hv_handle_set_rc(kvm, true, writing,
				      n_gpa, gp->l1_lpid);
	if (!ret)
		ret = -EINVAL;
	else
		ret = 0;

out_unlock:
	spin_unlock(&kvm->mmu_lock);
	return ret;
}

static inline int kvmppc_radix_level_to_shift(int level)
{
	switch (level) {
	case 2:
		return PUD_SHIFT;
	case 1:
		return PMD_SHIFT;
	default:
		return PAGE_SHIFT;
	}
}

static inline int kvmppc_radix_shift_to_level(int shift)
{
	if (shift == PUD_SHIFT)
		return 2;
	if (shift == PMD_SHIFT)
		return 1;
	if (shift == PAGE_SHIFT)
		return 0;
	WARN_ON_ONCE(1);
	return 0;
}

/* called with gp->tlb_lock held */
static long int __kvmhv_nested_page_fault(struct kvm_vcpu *vcpu,
					  struct kvm_nested_guest *gp)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_memory_slot *memslot;
	struct rmap_nested *n_rmap;
	struct kvmppc_pte gpte;
	pte_t pte, *pte_p;
	unsigned long mmu_seq;
	unsigned long dsisr = vcpu->arch.fault_dsisr;
	unsigned long ea = vcpu->arch.fault_dar;
	unsigned long *rmapp;
	unsigned long n_gpa, gpa, gfn, perm = 0UL;
	unsigned int shift, l1_shift, level;
	bool writing = !!(dsisr & DSISR_ISSTORE);
	bool kvm_ro = false;
	long int ret;

	if (!gp->l1_gr_to_hr) {
		kvmhv_update_ptbl_cache(gp);
		if (!gp->l1_gr_to_hr)
			return RESUME_HOST;
	}

	/* Convert the nested guest real address into a L1 guest real address */

	n_gpa = vcpu->arch.fault_gpa & ~0xF000000000000FFFULL;
	if (!(dsisr & DSISR_PRTABLE_FAULT))
		n_gpa |= ea & 0xFFF;
	ret = kvmhv_translate_addr_nested(vcpu, gp, n_gpa, dsisr, &gpte);

	/*
	 * If the hardware found a translation but we don't now have a usable
	 * translation in the l1 partition-scoped tree, remove the shadow pte
	 * and let the guest retry.
	 */
	if (ret == RESUME_HOST &&
	    (dsisr & (DSISR_PROTFAULT | DSISR_BADACCESS | DSISR_NOEXEC_OR_G |
		      DSISR_BAD_COPYPASTE)))
		goto inval;
	if (ret)
		return ret;

	/* Failed to set the reference/change bits */
	if (dsisr & DSISR_SET_RC) {
		ret = kvmhv_handle_nested_set_rc(vcpu, gp, n_gpa, gpte, dsisr);
		if (ret == RESUME_HOST)
			return ret;
		if (ret)
			goto inval;
		dsisr &= ~DSISR_SET_RC;
		if (!(dsisr & (DSISR_BAD_FAULT_64S | DSISR_NOHPTE |
			       DSISR_PROTFAULT)))
			return RESUME_GUEST;
	}

	/*
	 * We took an HISI or HDSI while we were running a nested guest which
	 * means we have no partition scoped translation for that. This means
	 * we need to insert a pte for the mapping into our shadow_pgtable.
	 */

	l1_shift = gpte.page_shift;
	if (l1_shift < PAGE_SHIFT) {
		/* We don't support l1 using a page size smaller than our own */
		pr_err("KVM: L1 guest page shift (%d) less than our own (%d)\n",
			l1_shift, PAGE_SHIFT);
		return -EINVAL;
	}
	gpa = gpte.raddr;
	gfn = gpa >> PAGE_SHIFT;

	/* 1. Get the corresponding host memslot */

	memslot = gfn_to_memslot(kvm, gfn);
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID)) {
		if (dsisr & (DSISR_PRTABLE_FAULT | DSISR_BADACCESS)) {
			/* unusual error -> reflect to the guest as a DSI */
			kvmppc_core_queue_data_storage(vcpu, ea, dsisr);
			return RESUME_GUEST;
		}

		/* passthrough of emulated MMIO case */
		return kvmppc_hv_emulate_mmio(vcpu, gpa, ea, writing);
	}
	if (memslot->flags & KVM_MEM_READONLY) {
		if (writing) {
			/* Give the guest a DSI */
			kvmppc_core_queue_data_storage(vcpu, ea,
					DSISR_ISSTORE | DSISR_PROTFAULT);
			return RESUME_GUEST;
		}
		kvm_ro = true;
	}

	/* 2. Find the host pte for this L1 guest real address */

	/* Used to check for invalidations in progress */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	/* See if can find translation in our partition scoped tables for L1 */
	pte = __pte(0);
	spin_lock(&kvm->mmu_lock);
	pte_p = find_kvm_secondary_pte(kvm, gpa, &shift);
	if (!shift)
		shift = PAGE_SHIFT;
	if (pte_p)
		pte = *pte_p;
	spin_unlock(&kvm->mmu_lock);

	if (!pte_present(pte) || (writing && !(pte_val(pte) & _PAGE_WRITE))) {
		/* No suitable pte found -> try to insert a mapping */
		ret = kvmppc_book3s_instantiate_page(vcpu, gpa, memslot,
					writing, kvm_ro, &pte, &level);
		if (ret == -EAGAIN)
			return RESUME_GUEST;
		else if (ret)
			return ret;
		shift = kvmppc_radix_level_to_shift(level);
	}
	/* Align gfn to the start of the page */
	gfn = (gpa & ~((1UL << shift) - 1)) >> PAGE_SHIFT;

	/* 3. Compute the pte we need to insert for nest_gpa -> host r_addr */

	/* The permissions is the combination of the host and l1 guest ptes */
	perm |= gpte.may_read ? 0UL : _PAGE_READ;
	perm |= gpte.may_write ? 0UL : _PAGE_WRITE;
	perm |= gpte.may_execute ? 0UL : _PAGE_EXEC;
	/* Only set accessed/dirty (rc) bits if set in host and l1 guest ptes */
	perm |= (gpte.rc & _PAGE_ACCESSED) ? 0UL : _PAGE_ACCESSED;
	perm |= ((gpte.rc & _PAGE_DIRTY) && writing) ? 0UL : _PAGE_DIRTY;
	pte = __pte(pte_val(pte) & ~perm);

	/* What size pte can we insert? */
	if (shift > l1_shift) {
		u64 mask;
		unsigned int actual_shift = PAGE_SHIFT;
		if (PMD_SHIFT < l1_shift)
			actual_shift = PMD_SHIFT;
		mask = (1UL << shift) - (1UL << actual_shift);
		pte = __pte(pte_val(pte) | (gpa & mask));
		shift = actual_shift;
	}
	level = kvmppc_radix_shift_to_level(shift);
	n_gpa &= ~((1UL << shift) - 1);

	/* 4. Insert the pte into our shadow_pgtable */

	n_rmap = kzalloc(sizeof(*n_rmap), GFP_KERNEL);
	if (!n_rmap)
		return RESUME_GUEST; /* Let the guest try again */
	n_rmap->rmap = (n_gpa & RMAP_NESTED_GPA_MASK) |
		(((unsigned long) gp->l1_lpid) << RMAP_NESTED_LPID_SHIFT);
	rmapp = &memslot->arch.rmap[gfn - memslot->base_gfn];
	ret = kvmppc_create_pte(kvm, gp->shadow_pgtable, pte, n_gpa, level,
				mmu_seq, gp->shadow_lpid, rmapp, &n_rmap);
	kfree(n_rmap);
	if (ret == -EAGAIN)
		ret = RESUME_GUEST;	/* Let the guest try again */

	return ret;

 inval:
	kvmhv_invalidate_shadow_pte(vcpu, gp, n_gpa, NULL);
	return RESUME_GUEST;
}

long int kvmhv_nested_page_fault(struct kvm_vcpu *vcpu)
{
	struct kvm_nested_guest *gp = vcpu->arch.nested;
	long int ret;

	mutex_lock(&gp->tlb_lock);
	ret = __kvmhv_nested_page_fault(vcpu, gp);
	mutex_unlock(&gp->tlb_lock);
	return ret;
}

int kvmhv_nested_next_lpid(struct kvm *kvm, int lpid)
{
	int ret = -1;

	spin_lock(&kvm->mmu_lock);
	while (++lpid <= kvm->arch.max_nested_lpid) {
		if (kvm->arch.nested_guests[lpid]) {
			ret = lpid;
			break;
		}
	}
	spin_unlock(&kvm->mmu_lock);
	return ret;
}
