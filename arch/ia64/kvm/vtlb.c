/*
 * vtlb.c: guest virtual tlb handling module.
 * Copyright (c) 2004, Intel Corporation.
 *  Yaozu Dong (Eddie Dong) <Eddie.dong@intel.com>
 *  Xuefei Xu (Anthony Xu) <anthony.xu@intel.com>
 *
 * Copyright (c) 2007, Intel Corporation.
 *  Xuefei Xu (Anthony Xu) <anthony.xu@intel.com>
 *  Xiantao Zhang <xiantao.zhang@intel.com>
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

#include "vcpu.h"

#include <linux/rwsem.h>

#include <asm/tlb.h>

/*
 * Check to see if the address rid:va is translated by the TLB
 */

static int __is_tr_translated(struct thash_data *trp, u64 rid, u64 va)
{
	return ((trp->p) && (trp->rid == rid)
				&& ((va-trp->vadr) < PSIZE(trp->ps)));
}

/*
 * Only for GUEST TR format.
 */
static int __is_tr_overlap(struct thash_data *trp, u64 rid, u64 sva, u64 eva)
{
	u64 sa1, ea1;

	if (!trp->p || trp->rid != rid)
		return 0;

	sa1 = trp->vadr;
	ea1 = sa1 + PSIZE(trp->ps) - 1;
	eva -= 1;
	if ((sva > ea1) || (sa1 > eva))
		return 0;
	else
		return 1;

}

void machine_tlb_purge(u64 va, u64 ps)
{
	ia64_ptcl(va, ps << 2);
}

void local_flush_tlb_all(void)
{
	int i, j;
	unsigned long flags, count0, count1;
	unsigned long stride0, stride1, addr;

	addr    = current_vcpu->arch.ptce_base;
	count0  = current_vcpu->arch.ptce_count[0];
	count1  = current_vcpu->arch.ptce_count[1];
	stride0 = current_vcpu->arch.ptce_stride[0];
	stride1 = current_vcpu->arch.ptce_stride[1];

	local_irq_save(flags);
	for (i = 0; i < count0; ++i) {
		for (j = 0; j < count1; ++j) {
			ia64_ptce(addr);
			addr += stride1;
		}
		addr += stride0;
	}
	local_irq_restore(flags);
	ia64_srlz_i();          /* srlz.i implies srlz.d */
}

int vhpt_enabled(struct kvm_vcpu *vcpu, u64 vadr, enum vhpt_ref ref)
{
	union ia64_rr    vrr;
	union ia64_pta   vpta;
	struct  ia64_psr   vpsr;

	vpsr = *(struct ia64_psr *)&VCPU(vcpu, vpsr);
	vrr.val = vcpu_get_rr(vcpu, vadr);
	vpta.val = vcpu_get_pta(vcpu);

	if (vrr.ve & vpta.ve) {
		switch (ref) {
		case DATA_REF:
		case NA_REF:
			return vpsr.dt;
		case INST_REF:
			return vpsr.dt && vpsr.it && vpsr.ic;
		case RSE_REF:
			return vpsr.dt && vpsr.rt;

		}
	}
	return 0;
}

struct thash_data *vsa_thash(union ia64_pta vpta, u64 va, u64 vrr, u64 *tag)
{
	u64 index, pfn, rid, pfn_bits;

	pfn_bits = vpta.size - 5 - 8;
	pfn = REGION_OFFSET(va) >> _REGION_PAGE_SIZE(vrr);
	rid = _REGION_ID(vrr);
	index = ((rid & 0xff) << pfn_bits)|(pfn & ((1UL << pfn_bits) - 1));
	*tag = ((rid >> 8) & 0xffff) | ((pfn >> pfn_bits) << 16);

	return (struct thash_data *)((vpta.base << PTA_BASE_SHIFT) +
				(index << 5));
}

struct thash_data *__vtr_lookup(struct kvm_vcpu *vcpu, u64 va, int type)
{

	struct thash_data *trp;
	int  i;
	u64 rid;

	rid = vcpu_get_rr(vcpu, va);
	rid = rid & RR_RID_MASK;
	if (type == D_TLB) {
		if (vcpu_quick_region_check(vcpu->arch.dtr_regions, va)) {
			for (trp = (struct thash_data *)&vcpu->arch.dtrs, i = 0;
						i < NDTRS; i++, trp++) {
				if (__is_tr_translated(trp, rid, va))
					return trp;
			}
		}
	} else {
		if (vcpu_quick_region_check(vcpu->arch.itr_regions, va)) {
			for (trp = (struct thash_data *)&vcpu->arch.itrs, i = 0;
					i < NITRS; i++, trp++) {
				if (__is_tr_translated(trp, rid, va))
					return trp;
			}
		}
	}

	return NULL;
}

static void vhpt_insert(u64 pte, u64 itir, u64 ifa, u64 gpte)
{
	union ia64_rr rr;
	struct thash_data *head;
	unsigned long ps, gpaddr;

	ps = itir_ps(itir);
	rr.val = ia64_get_rr(ifa);

	 gpaddr = ((gpte & _PAGE_PPN_MASK) >> ps << ps) |
					(ifa & ((1UL << ps) - 1));

	head = (struct thash_data *)ia64_thash(ifa);
	head->etag = INVALID_TI_TAG;
	ia64_mf();
	head->page_flags = pte & ~PAGE_FLAGS_RV_MASK;
	head->itir = rr.ps << 2;
	head->etag = ia64_ttag(ifa);
	head->gpaddr = gpaddr;
}

void mark_pages_dirty(struct kvm_vcpu *v, u64 pte, u64 ps)
{
	u64 i, dirty_pages = 1;
	u64 base_gfn = (pte&_PAGE_PPN_MASK) >> PAGE_SHIFT;
	spinlock_t *lock = __kvm_va(v->arch.dirty_log_lock_pa);
	void *dirty_bitmap = (void *)KVM_MEM_DIRTY_LOG_BASE;

	dirty_pages <<= ps <= PAGE_SHIFT ? 0 : ps - PAGE_SHIFT;

	vmm_spin_lock(lock);
	for (i = 0; i < dirty_pages; i++) {
		/* avoid RMW */
		if (!test_bit(base_gfn + i, dirty_bitmap))
			set_bit(base_gfn + i , dirty_bitmap);
	}
	vmm_spin_unlock(lock);
}

void thash_vhpt_insert(struct kvm_vcpu *v, u64 pte, u64 itir, u64 va, int type)
{
	u64 phy_pte, psr;
	union ia64_rr mrr;

	mrr.val = ia64_get_rr(va);
	phy_pte = translate_phy_pte(&pte, itir, va);

	if (itir_ps(itir) >= mrr.ps) {
		vhpt_insert(phy_pte, itir, va, pte);
	} else {
		phy_pte  &= ~PAGE_FLAGS_RV_MASK;
		psr = ia64_clear_ic();
		ia64_itc(type, va, phy_pte, itir_ps(itir));
		paravirt_dv_serialize_data();
		ia64_set_psr(psr);
	}

	if (!(pte&VTLB_PTE_IO))
		mark_pages_dirty(v, pte, itir_ps(itir));
}

/*
 *   vhpt lookup
 */
struct thash_data *vhpt_lookup(u64 va)
{
	struct thash_data *head;
	u64 tag;

	head = (struct thash_data *)ia64_thash(va);
	tag = ia64_ttag(va);
	if (head->etag == tag)
		return head;
	return NULL;
}

u64 guest_vhpt_lookup(u64 iha, u64 *pte)
{
	u64 ret;
	struct thash_data *data;

	data = __vtr_lookup(current_vcpu, iha, D_TLB);
	if (data != NULL)
		thash_vhpt_insert(current_vcpu, data->page_flags,
			data->itir, iha, D_TLB);

	asm volatile ("rsm psr.ic|psr.i;;"
			"srlz.d;;"
			"ld8.s r9=[%1];;"
			"tnat.nz p6,p7=r9;;"
			"(p6) mov %0=1;"
			"(p6) mov r9=r0;"
			"(p7) extr.u r9=r9,0,53;;"
			"(p7) mov %0=r0;"
			"(p7) st8 [%2]=r9;;"
			"ssm psr.ic;;"
			"srlz.d;;"
			"ssm psr.i;;"
			"srlz.d;;"
			: "=r"(ret) : "r"(iha), "r"(pte):"memory");

	return ret;
}

/*
 *  purge software guest tlb
 */

static void vtlb_purge(struct kvm_vcpu *v, u64 va, u64 ps)
{
	struct thash_data *cur;
	u64 start, curadr, size, psbits, tag, rr_ps, num;
	union ia64_rr vrr;
	struct thash_cb *hcb = &v->arch.vtlb;

	vrr.val = vcpu_get_rr(v, va);
	psbits = VMX(v, psbits[(va >> 61)]);
	start = va & ~((1UL << ps) - 1);
	while (psbits) {
		curadr = start;
		rr_ps = __ffs(psbits);
		psbits &= ~(1UL << rr_ps);
		num = 1UL << ((ps < rr_ps) ? 0 : (ps - rr_ps));
		size = PSIZE(rr_ps);
		vrr.ps = rr_ps;
		while (num) {
			cur = vsa_thash(hcb->pta, curadr, vrr.val, &tag);
			if (cur->etag == tag && cur->ps == rr_ps)
				cur->etag = INVALID_TI_TAG;
			curadr += size;
			num--;
		}
	}
}


/*
 *  purge VHPT and machine TLB
 */
static void vhpt_purge(struct kvm_vcpu *v, u64 va, u64 ps)
{
	struct thash_data *cur;
	u64 start, size, tag, num;
	union ia64_rr rr;

	start = va & ~((1UL << ps) - 1);
	rr.val = ia64_get_rr(va);
	size = PSIZE(rr.ps);
	num = 1UL << ((ps < rr.ps) ? 0 : (ps - rr.ps));
	while (num) {
		cur = (struct thash_data *)ia64_thash(start);
		tag = ia64_ttag(start);
		if (cur->etag == tag)
			cur->etag = INVALID_TI_TAG;
		start += size;
		num--;
	}
	machine_tlb_purge(va, ps);
}

/*
 * Insert an entry into hash TLB or VHPT.
 * NOTES:
 *  1: When inserting VHPT to thash, "va" is a must covered
 *  address by the inserted machine VHPT entry.
 *  2: The format of entry is always in TLB.
 *  3: The caller need to make sure the new entry will not overlap
 *     with any existed entry.
 */
void vtlb_insert(struct kvm_vcpu *v, u64 pte, u64 itir, u64 va)
{
	struct thash_data *head;
	union ia64_rr vrr;
	u64 tag;
	struct thash_cb *hcb = &v->arch.vtlb;

	vrr.val = vcpu_get_rr(v, va);
	vrr.ps = itir_ps(itir);
	VMX(v, psbits[va >> 61]) |= (1UL << vrr.ps);
	head = vsa_thash(hcb->pta, va, vrr.val, &tag);
	head->page_flags = pte;
	head->itir = itir;
	head->etag = tag;
}

int vtr_find_overlap(struct kvm_vcpu *vcpu, u64 va, u64 ps, int type)
{
	struct thash_data  *trp;
	int  i;
	u64 end, rid;

	rid = vcpu_get_rr(vcpu, va);
	rid = rid & RR_RID_MASK;
	end = va + PSIZE(ps);
	if (type == D_TLB) {
		if (vcpu_quick_region_check(vcpu->arch.dtr_regions, va)) {
			for (trp = (struct thash_data *)&vcpu->arch.dtrs, i = 0;
					i < NDTRS; i++, trp++) {
				if (__is_tr_overlap(trp, rid, va, end))
					return i;
			}
		}
	} else {
		if (vcpu_quick_region_check(vcpu->arch.itr_regions, va)) {
			for (trp = (struct thash_data *)&vcpu->arch.itrs, i = 0;
					i < NITRS; i++, trp++) {
				if (__is_tr_overlap(trp, rid, va, end))
					return i;
			}
		}
	}
	return -1;
}

/*
 * Purge entries in VTLB and VHPT
 */
void thash_purge_entries(struct kvm_vcpu *v, u64 va, u64 ps)
{
	if (vcpu_quick_region_check(v->arch.tc_regions, va))
		vtlb_purge(v, va, ps);
	vhpt_purge(v, va, ps);
}

void thash_purge_entries_remote(struct kvm_vcpu *v, u64 va, u64 ps)
{
	u64 old_va = va;
	va = REGION_OFFSET(va);
	if (vcpu_quick_region_check(v->arch.tc_regions, old_va))
		vtlb_purge(v, va, ps);
	vhpt_purge(v, va, ps);
}

u64 translate_phy_pte(u64 *pte, u64 itir, u64 va)
{
	u64 ps, ps_mask, paddr, maddr, io_mask;
	union pte_flags phy_pte;

	ps = itir_ps(itir);
	ps_mask = ~((1UL << ps) - 1);
	phy_pte.val = *pte;
	paddr = *pte;
	paddr = ((paddr & _PAGE_PPN_MASK) & ps_mask) | (va & ~ps_mask);
	maddr = kvm_get_mpt_entry(paddr >> PAGE_SHIFT);
	io_mask = maddr & GPFN_IO_MASK;
	if (io_mask && (io_mask != GPFN_PHYS_MMIO)) {
		*pte |= VTLB_PTE_IO;
		return -1;
	}
	maddr = ((maddr & _PAGE_PPN_MASK) & PAGE_MASK) |
					(paddr & ~PAGE_MASK);
	phy_pte.ppn = maddr >> ARCH_PAGE_SHIFT;
	return phy_pte.val;
}

/*
 * Purge overlap TCs and then insert the new entry to emulate itc ops.
 * Notes: Only TC entry can purge and insert.
 */
void  thash_purge_and_insert(struct kvm_vcpu *v, u64 pte, u64 itir,
						u64 ifa, int type)
{
	u64 ps;
	u64 phy_pte, io_mask, index;
	union ia64_rr vrr, mrr;

	ps = itir_ps(itir);
	vrr.val = vcpu_get_rr(v, ifa);
	mrr.val = ia64_get_rr(ifa);

	index = (pte & _PAGE_PPN_MASK) >> PAGE_SHIFT;
	io_mask = kvm_get_mpt_entry(index) & GPFN_IO_MASK;
	phy_pte = translate_phy_pte(&pte, itir, ifa);

	/* Ensure WB attribute if pte is related to a normal mem page,
	 * which is required by vga acceleration since qemu maps shared
	 * vram buffer with WB.
	 */
	if (!(pte & VTLB_PTE_IO) && ((pte & _PAGE_MA_MASK) != _PAGE_MA_NAT) &&
			io_mask != GPFN_PHYS_MMIO) {
		pte &= ~_PAGE_MA_MASK;
		phy_pte &= ~_PAGE_MA_MASK;
	}

	vtlb_purge(v, ifa, ps);
	vhpt_purge(v, ifa, ps);

	if ((ps != mrr.ps) || (pte & VTLB_PTE_IO)) {
		vtlb_insert(v, pte, itir, ifa);
		vcpu_quick_region_set(VMX(v, tc_regions), ifa);
	}
	if (pte & VTLB_PTE_IO)
		return;

	if (ps >= mrr.ps)
		vhpt_insert(phy_pte, itir, ifa, pte);
	else {
		u64 psr;
		phy_pte  &= ~PAGE_FLAGS_RV_MASK;
		psr = ia64_clear_ic();
		ia64_itc(type, ifa, phy_pte, ps);
		paravirt_dv_serialize_data();
		ia64_set_psr(psr);
	}
	if (!(pte&VTLB_PTE_IO))
		mark_pages_dirty(v, pte, ps);

}

/*
 * Purge all TCs or VHPT entries including those in Hash table.
 *
 */

void thash_purge_all(struct kvm_vcpu *v)
{
	int i;
	struct thash_data *head;
	struct thash_cb  *vtlb, *vhpt;
	vtlb = &v->arch.vtlb;
	vhpt = &v->arch.vhpt;

	for (i = 0; i < 8; i++)
		VMX(v, psbits[i]) = 0;

	head = vtlb->hash;
	for (i = 0; i < vtlb->num; i++) {
		head->page_flags = 0;
		head->etag = INVALID_TI_TAG;
		head->itir = 0;
		head->next = 0;
		head++;
	};

	head = vhpt->hash;
	for (i = 0; i < vhpt->num; i++) {
		head->page_flags = 0;
		head->etag = INVALID_TI_TAG;
		head->itir = 0;
		head->next = 0;
		head++;
	};

	local_flush_tlb_all();
}

/*
 * Lookup the hash table and its collision chain to find an entry
 * covering this address rid:va or the entry.
 *
 * INPUT:
 *  in: TLB format for both VHPT & TLB.
 */
struct thash_data *vtlb_lookup(struct kvm_vcpu *v, u64 va, int is_data)
{
	struct thash_data  *cch;
	u64    psbits, ps, tag;
	union ia64_rr vrr;

	struct thash_cb *hcb = &v->arch.vtlb;

	cch = __vtr_lookup(v, va, is_data);
	if (cch)
		return cch;

	if (vcpu_quick_region_check(v->arch.tc_regions, va) == 0)
		return NULL;

	psbits = VMX(v, psbits[(va >> 61)]);
	vrr.val = vcpu_get_rr(v, va);
	while (psbits) {
		ps = __ffs(psbits);
		psbits &= ~(1UL << ps);
		vrr.ps = ps;
		cch = vsa_thash(hcb->pta, va, vrr.val, &tag);
		if (cch->etag == tag && cch->ps == ps)
			return cch;
	}

	return NULL;
}

/*
 * Initialize internal control data before service.
 */
void thash_init(struct thash_cb *hcb, u64 sz)
{
	int i;
	struct thash_data *head;

	hcb->pta.val = (unsigned long)hcb->hash;
	hcb->pta.vf = 1;
	hcb->pta.ve = 1;
	hcb->pta.size = sz;
	head = hcb->hash;
	for (i = 0; i < hcb->num; i++) {
		head->page_flags = 0;
		head->itir = 0;
		head->etag = INVALID_TI_TAG;
		head->next = 0;
		head++;
	}
}

u64 kvm_get_mpt_entry(u64 gpfn)
{
	u64 *base = (u64 *) KVM_P2M_BASE;

	if (gpfn >= (KVM_P2M_SIZE >> 3))
		panic_vm(current_vcpu, "Invalid gpfn =%lx\n", gpfn);

	return *(base + gpfn);
}

u64 kvm_lookup_mpa(u64 gpfn)
{
	u64 maddr;
	maddr = kvm_get_mpt_entry(gpfn);
	return maddr&_PAGE_PPN_MASK;
}

u64 kvm_gpa_to_mpa(u64 gpa)
{
	u64 pte = kvm_lookup_mpa(gpa >> PAGE_SHIFT);
	return (pte >> PAGE_SHIFT << PAGE_SHIFT) | (gpa & ~PAGE_MASK);
}

/*
 * Fetch guest bundle code.
 * INPUT:
 *  gip: guest ip
 *  pbundle: used to return fetched bundle.
 */
int fetch_code(struct kvm_vcpu *vcpu, u64 gip, IA64_BUNDLE *pbundle)
{
	u64     gpip = 0;   /* guest physical IP*/
	u64     *vpa;
	struct thash_data    *tlb;
	u64     maddr;

	if (!(VCPU(vcpu, vpsr) & IA64_PSR_IT)) {
		/* I-side physical mode */
		gpip = gip;
	} else {
		tlb = vtlb_lookup(vcpu, gip, I_TLB);
		if (tlb)
			gpip = (tlb->ppn >> (tlb->ps - 12) << tlb->ps) |
				(gip & (PSIZE(tlb->ps) - 1));
	}
	if (gpip) {
		maddr = kvm_gpa_to_mpa(gpip);
	} else {
		tlb = vhpt_lookup(gip);
		if (tlb == NULL) {
			ia64_ptcl(gip, ARCH_PAGE_SHIFT << 2);
			return IA64_FAULT;
		}
		maddr = (tlb->ppn >> (tlb->ps - 12) << tlb->ps)
					| (gip & (PSIZE(tlb->ps) - 1));
	}
	vpa = (u64 *)__kvm_va(maddr);

	pbundle->i64[0] = *vpa++;
	pbundle->i64[1] = *vpa;

	return IA64_NO_FAULT;
}

void kvm_init_vhpt(struct kvm_vcpu *v)
{
	v->arch.vhpt.num = VHPT_NUM_ENTRIES;
	thash_init(&v->arch.vhpt, VHPT_SHIFT);
	ia64_set_pta(v->arch.vhpt.pta.val);
	/*Enable VHPT here?*/
}

void kvm_init_vtlb(struct kvm_vcpu *v)
{
	v->arch.vtlb.num = VTLB_NUM_ENTRIES;
	thash_init(&v->arch.vtlb, VTLB_SHIFT);
}
