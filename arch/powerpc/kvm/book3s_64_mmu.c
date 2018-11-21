/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/book3s/64/mmu-hash.h>

/* #define DEBUG_MMU */

#ifdef DEBUG_MMU
#define dprintk(X...) printk(KERN_INFO X)
#else
#define dprintk(X...) do { } while(0)
#endif

static void kvmppc_mmu_book3s_64_reset_msr(struct kvm_vcpu *vcpu)
{
	unsigned long msr = vcpu->arch.intr_msr;
	unsigned long cur_msr = kvmppc_get_msr(vcpu);

	/* If transactional, change to suspend mode on IRQ delivery */
	if (MSR_TM_TRANSACTIONAL(cur_msr))
		msr |= MSR_TS_S;
	else
		msr |= cur_msr & MSR_TS_MASK;

	kvmppc_set_msr(vcpu, msr);
}

static struct kvmppc_slb *kvmppc_mmu_book3s_64_find_slbe(
				struct kvm_vcpu *vcpu,
				gva_t eaddr)
{
	int i;
	u64 esid = GET_ESID(eaddr);
	u64 esid_1t = GET_ESID_1T(eaddr);

	for (i = 0; i < vcpu->arch.slb_nr; i++) {
		u64 cmp_esid = esid;

		if (!vcpu->arch.slb[i].valid)
			continue;

		if (vcpu->arch.slb[i].tb)
			cmp_esid = esid_1t;

		if (vcpu->arch.slb[i].esid == cmp_esid)
			return &vcpu->arch.slb[i];
	}

	dprintk("KVM: No SLB entry found for 0x%lx [%llx | %llx]\n",
		eaddr, esid, esid_1t);
	for (i = 0; i < vcpu->arch.slb_nr; i++) {
	    if (vcpu->arch.slb[i].vsid)
		dprintk("  %d: %c%c%c %llx %llx\n", i,
			vcpu->arch.slb[i].valid ? 'v' : ' ',
			vcpu->arch.slb[i].large ? 'l' : ' ',
			vcpu->arch.slb[i].tb    ? 't' : ' ',
			vcpu->arch.slb[i].esid,
			vcpu->arch.slb[i].vsid);
	}

	return NULL;
}

static int kvmppc_slb_sid_shift(struct kvmppc_slb *slbe)
{
	return slbe->tb ? SID_SHIFT_1T : SID_SHIFT;
}

static u64 kvmppc_slb_offset_mask(struct kvmppc_slb *slbe)
{
	return (1ul << kvmppc_slb_sid_shift(slbe)) - 1;
}

static u64 kvmppc_slb_calc_vpn(struct kvmppc_slb *slb, gva_t eaddr)
{
	eaddr &= kvmppc_slb_offset_mask(slb);

	return (eaddr >> VPN_SHIFT) |
		((slb->vsid) << (kvmppc_slb_sid_shift(slb) - VPN_SHIFT));
}

static u64 kvmppc_mmu_book3s_64_ea_to_vp(struct kvm_vcpu *vcpu, gva_t eaddr,
					 bool data)
{
	struct kvmppc_slb *slb;

	slb = kvmppc_mmu_book3s_64_find_slbe(vcpu, eaddr);
	if (!slb)
		return 0;

	return kvmppc_slb_calc_vpn(slb, eaddr);
}

static int mmu_pagesize(int mmu_pg)
{
	switch (mmu_pg) {
	case MMU_PAGE_64K:
		return 16;
	case MMU_PAGE_16M:
		return 24;
	}
	return 12;
}

static int kvmppc_mmu_book3s_64_get_pagesize(struct kvmppc_slb *slbe)
{
	return mmu_pagesize(slbe->base_page_size);
}

static u32 kvmppc_mmu_book3s_64_get_page(struct kvmppc_slb *slbe, gva_t eaddr)
{
	int p = kvmppc_mmu_book3s_64_get_pagesize(slbe);

	return ((eaddr & kvmppc_slb_offset_mask(slbe)) >> p);
}

static hva_t kvmppc_mmu_book3s_64_get_pteg(struct kvm_vcpu *vcpu,
				struct kvmppc_slb *slbe, gva_t eaddr,
				bool second)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	u64 hash, pteg, htabsize;
	u32 ssize;
	hva_t r;
	u64 vpn;

	htabsize = ((1 << ((vcpu_book3s->sdr1 & 0x1f) + 11)) - 1);

	vpn = kvmppc_slb_calc_vpn(slbe, eaddr);
	ssize = slbe->tb ? MMU_SEGSIZE_1T : MMU_SEGSIZE_256M;
	hash = hpt_hash(vpn, kvmppc_mmu_book3s_64_get_pagesize(slbe), ssize);
	if (second)
		hash = ~hash;
	hash &= ((1ULL << 39ULL) - 1ULL);
	hash &= htabsize;
	hash <<= 7ULL;

	pteg = vcpu_book3s->sdr1 & 0xfffffffffffc0000ULL;
	pteg |= hash;

	dprintk("MMU: page=0x%x sdr1=0x%llx pteg=0x%llx vsid=0x%llx\n",
		page, vcpu_book3s->sdr1, pteg, slbe->vsid);

	/* When running a PAPR guest, SDR1 contains a HVA address instead
           of a GPA */
	if (vcpu->arch.papr_enabled)
		r = pteg;
	else
		r = gfn_to_hva(vcpu->kvm, pteg >> PAGE_SHIFT);

	if (kvm_is_error_hva(r))
		return r;
	return r | (pteg & ~PAGE_MASK);
}

static u64 kvmppc_mmu_book3s_64_get_avpn(struct kvmppc_slb *slbe, gva_t eaddr)
{
	int p = kvmppc_mmu_book3s_64_get_pagesize(slbe);
	u64 avpn;

	avpn = kvmppc_mmu_book3s_64_get_page(slbe, eaddr);
	avpn |= slbe->vsid << (kvmppc_slb_sid_shift(slbe) - p);

	if (p < 16)
		avpn >>= ((80 - p) - 56) - 8;	/* 16 - p */
	else
		avpn <<= p - 16;

	return avpn;
}

/*
 * Return page size encoded in the second word of a HPTE, or
 * -1 for an invalid encoding for the base page size indicated by
 * the SLB entry.  This doesn't handle mixed pagesize segments yet.
 */
static int decode_pagesize(struct kvmppc_slb *slbe, u64 r)
{
	switch (slbe->base_page_size) {
	case MMU_PAGE_64K:
		if ((r & 0xf000) == 0x1000)
			return MMU_PAGE_64K;
		break;
	case MMU_PAGE_16M:
		if ((r & 0xff000) == 0)
			return MMU_PAGE_16M;
		break;
	}
	return -1;
}

static int kvmppc_mmu_book3s_64_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
				      struct kvmppc_pte *gpte, bool data,
				      bool iswrite)
{
	struct kvmppc_slb *slbe;
	hva_t ptegp;
	u64 pteg[16];
	u64 avpn = 0;
	u64 v, r;
	u64 v_val, v_mask;
	u64 eaddr_mask;
	int i;
	u8 pp, key = 0;
	bool found = false;
	bool second = false;
	int pgsize;
	ulong mp_ea = vcpu->arch.magic_page_ea;

	/* Magic page override */
	if (unlikely(mp_ea) &&
	    unlikely((eaddr & ~0xfffULL) == (mp_ea & ~0xfffULL)) &&
	    !(kvmppc_get_msr(vcpu) & MSR_PR)) {
		gpte->eaddr = eaddr;
		gpte->vpage = kvmppc_mmu_book3s_64_ea_to_vp(vcpu, eaddr, data);
		gpte->raddr = vcpu->arch.magic_page_pa | (gpte->raddr & 0xfff);
		gpte->raddr &= KVM_PAM;
		gpte->may_execute = true;
		gpte->may_read = true;
		gpte->may_write = true;
		gpte->page_size = MMU_PAGE_4K;
		gpte->wimg = HPTE_R_M;

		return 0;
	}

	slbe = kvmppc_mmu_book3s_64_find_slbe(vcpu, eaddr);
	if (!slbe)
		goto no_seg_found;

	avpn = kvmppc_mmu_book3s_64_get_avpn(slbe, eaddr);
	v_val = avpn & HPTE_V_AVPN;

	if (slbe->tb)
		v_val |= SLB_VSID_B_1T;
	if (slbe->large)
		v_val |= HPTE_V_LARGE;
	v_val |= HPTE_V_VALID;

	v_mask = SLB_VSID_B | HPTE_V_AVPN | HPTE_V_LARGE | HPTE_V_VALID |
		HPTE_V_SECONDARY;

	pgsize = slbe->large ? MMU_PAGE_16M : MMU_PAGE_4K;

	mutex_lock(&vcpu->kvm->arch.hpt_mutex);

do_second:
	ptegp = kvmppc_mmu_book3s_64_get_pteg(vcpu, slbe, eaddr, second);
	if (kvm_is_error_hva(ptegp))
		goto no_page_found;

	if(copy_from_user(pteg, (void __user *)ptegp, sizeof(pteg))) {
		printk_ratelimited(KERN_ERR
			"KVM: Can't copy data from 0x%lx!\n", ptegp);
		goto no_page_found;
	}

	if ((kvmppc_get_msr(vcpu) & MSR_PR) && slbe->Kp)
		key = 4;
	else if (!(kvmppc_get_msr(vcpu) & MSR_PR) && slbe->Ks)
		key = 4;

	for (i=0; i<16; i+=2) {
		u64 pte0 = be64_to_cpu(pteg[i]);
		u64 pte1 = be64_to_cpu(pteg[i + 1]);

		/* Check all relevant fields of 1st dword */
		if ((pte0 & v_mask) == v_val) {
			/* If large page bit is set, check pgsize encoding */
			if (slbe->large &&
			    (vcpu->arch.hflags & BOOK3S_HFLAG_MULTI_PGSIZE)) {
				pgsize = decode_pagesize(slbe, pte1);
				if (pgsize < 0)
					continue;
			}
			found = true;
			break;
		}
	}

	if (!found) {
		if (second)
			goto no_page_found;
		v_val |= HPTE_V_SECONDARY;
		second = true;
		goto do_second;
	}

	v = be64_to_cpu(pteg[i]);
	r = be64_to_cpu(pteg[i+1]);
	pp = (r & HPTE_R_PP) | key;
	if (r & HPTE_R_PP0)
		pp |= 8;

	gpte->eaddr = eaddr;
	gpte->vpage = kvmppc_mmu_book3s_64_ea_to_vp(vcpu, eaddr, data);

	eaddr_mask = (1ull << mmu_pagesize(pgsize)) - 1;
	gpte->raddr = (r & HPTE_R_RPN & ~eaddr_mask) | (eaddr & eaddr_mask);
	gpte->page_size = pgsize;
	gpte->may_execute = ((r & HPTE_R_N) ? false : true);
	if (unlikely(vcpu->arch.disable_kernel_nx) &&
	    !(kvmppc_get_msr(vcpu) & MSR_PR))
		gpte->may_execute = true;
	gpte->may_read = false;
	gpte->may_write = false;
	gpte->wimg = r & HPTE_R_WIMG;

	switch (pp) {
	case 0:
	case 1:
	case 2:
	case 6:
		gpte->may_write = true;
		/* fall through */
	case 3:
	case 5:
	case 7:
	case 10:
		gpte->may_read = true;
		break;
	}

	dprintk("KVM MMU: Translated 0x%lx [0x%llx] -> 0x%llx "
		"-> 0x%lx\n",
		eaddr, avpn, gpte->vpage, gpte->raddr);

	/* Update PTE R and C bits, so the guest's swapper knows we used the
	 * page */
	if (gpte->may_read && !(r & HPTE_R_R)) {
		/*
		 * Set the accessed flag.
		 * We have to write this back with a single byte write
		 * because another vcpu may be accessing this on
		 * non-PAPR platforms such as mac99, and this is
		 * what real hardware does.
		 */
                char __user *addr = (char __user *) (ptegp + (i + 1) * sizeof(u64));
		r |= HPTE_R_R;
		put_user(r >> 8, addr + 6);
	}
	if (iswrite && gpte->may_write && !(r & HPTE_R_C)) {
		/* Set the dirty flag */
		/* Use a single byte write */
                char __user *addr = (char __user *) (ptegp + (i + 1) * sizeof(u64));
		r |= HPTE_R_C;
		put_user(r, addr + 7);
	}

	mutex_unlock(&vcpu->kvm->arch.hpt_mutex);

	if (!gpte->may_read || (iswrite && !gpte->may_write))
		return -EPERM;
	return 0;

no_page_found:
	mutex_unlock(&vcpu->kvm->arch.hpt_mutex);
	return -ENOENT;

no_seg_found:
	dprintk("KVM MMU: Trigger segment fault\n");
	return -EINVAL;
}

static void kvmppc_mmu_book3s_64_slbmte(struct kvm_vcpu *vcpu, u64 rs, u64 rb)
{
	u64 esid, esid_1t;
	int slb_nr;
	struct kvmppc_slb *slbe;

	dprintk("KVM MMU: slbmte(0x%llx, 0x%llx)\n", rs, rb);

	esid = GET_ESID(rb);
	esid_1t = GET_ESID_1T(rb);
	slb_nr = rb & 0xfff;

	if (slb_nr > vcpu->arch.slb_nr)
		return;

	slbe = &vcpu->arch.slb[slb_nr];

	slbe->large = (rs & SLB_VSID_L) ? 1 : 0;
	slbe->tb    = (rs & SLB_VSID_B_1T) ? 1 : 0;
	slbe->esid  = slbe->tb ? esid_1t : esid;
	slbe->vsid  = (rs & ~SLB_VSID_B) >> (kvmppc_slb_sid_shift(slbe) - 16);
	slbe->valid = (rb & SLB_ESID_V) ? 1 : 0;
	slbe->Ks    = (rs & SLB_VSID_KS) ? 1 : 0;
	slbe->Kp    = (rs & SLB_VSID_KP) ? 1 : 0;
	slbe->nx    = (rs & SLB_VSID_N) ? 1 : 0;
	slbe->class = (rs & SLB_VSID_C) ? 1 : 0;

	slbe->base_page_size = MMU_PAGE_4K;
	if (slbe->large) {
		if (vcpu->arch.hflags & BOOK3S_HFLAG_MULTI_PGSIZE) {
			switch (rs & SLB_VSID_LP) {
			case SLB_VSID_LP_00:
				slbe->base_page_size = MMU_PAGE_16M;
				break;
			case SLB_VSID_LP_01:
				slbe->base_page_size = MMU_PAGE_64K;
				break;
			}
		} else
			slbe->base_page_size = MMU_PAGE_16M;
	}

	slbe->orige = rb & (ESID_MASK | SLB_ESID_V);
	slbe->origv = rs;

	/* Map the new segment */
	kvmppc_mmu_map_segment(vcpu, esid << SID_SHIFT);
}

static u64 kvmppc_mmu_book3s_64_slbmfee(struct kvm_vcpu *vcpu, u64 slb_nr)
{
	struct kvmppc_slb *slbe;

	if (slb_nr > vcpu->arch.slb_nr)
		return 0;

	slbe = &vcpu->arch.slb[slb_nr];

	return slbe->orige;
}

static u64 kvmppc_mmu_book3s_64_slbmfev(struct kvm_vcpu *vcpu, u64 slb_nr)
{
	struct kvmppc_slb *slbe;

	if (slb_nr > vcpu->arch.slb_nr)
		return 0;

	slbe = &vcpu->arch.slb[slb_nr];

	return slbe->origv;
}

static void kvmppc_mmu_book3s_64_slbie(struct kvm_vcpu *vcpu, u64 ea)
{
	struct kvmppc_slb *slbe;
	u64 seg_size;

	dprintk("KVM MMU: slbie(0x%llx)\n", ea);

	slbe = kvmppc_mmu_book3s_64_find_slbe(vcpu, ea);

	if (!slbe)
		return;

	dprintk("KVM MMU: slbie(0x%llx, 0x%llx)\n", ea, slbe->esid);

	slbe->valid = false;
	slbe->orige = 0;
	slbe->origv = 0;

	seg_size = 1ull << kvmppc_slb_sid_shift(slbe);
	kvmppc_mmu_flush_segment(vcpu, ea & ~(seg_size - 1), seg_size);
}

static void kvmppc_mmu_book3s_64_slbia(struct kvm_vcpu *vcpu)
{
	int i;

	dprintk("KVM MMU: slbia()\n");

	for (i = 1; i < vcpu->arch.slb_nr; i++) {
		vcpu->arch.slb[i].valid = false;
		vcpu->arch.slb[i].orige = 0;
		vcpu->arch.slb[i].origv = 0;
	}

	if (kvmppc_get_msr(vcpu) & MSR_IR) {
		kvmppc_mmu_flush_segments(vcpu);
		kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu));
	}
}

static void kvmppc_mmu_book3s_64_mtsrin(struct kvm_vcpu *vcpu, u32 srnum,
					ulong value)
{
	u64 rb = 0, rs = 0;

	/*
	 * According to Book3 2.01 mtsrin is implemented as:
	 *
	 * The SLB entry specified by (RB)32:35 is loaded from register
	 * RS, as follows.
	 *
	 * SLBE Bit	Source			SLB Field
	 *
	 * 0:31		0x0000_0000		ESID-0:31
	 * 32:35	(RB)32:35		ESID-32:35
	 * 36		0b1			V
	 * 37:61	0x00_0000|| 0b0		VSID-0:24
	 * 62:88	(RS)37:63		VSID-25:51
	 * 89:91	(RS)33:35		Ks Kp N
	 * 92		(RS)36			L ((RS)36 must be 0b0)
	 * 93		0b0			C
	 */

	dprintk("KVM MMU: mtsrin(0x%x, 0x%lx)\n", srnum, value);

	/* ESID = srnum */
	rb |= (srnum & 0xf) << 28;
	/* Set the valid bit */
	rb |= 1 << 27;
	/* Index = ESID */
	rb |= srnum;

	/* VSID = VSID */
	rs |= (value & 0xfffffff) << 12;
	/* flags = flags */
	rs |= ((value >> 28) & 0x7) << 9;

	kvmppc_mmu_book3s_64_slbmte(vcpu, rs, rb);
}

static void kvmppc_mmu_book3s_64_tlbie(struct kvm_vcpu *vcpu, ulong va,
				       bool large)
{
	u64 mask = 0xFFFFFFFFFULL;
	long i;
	struct kvm_vcpu *v;

	dprintk("KVM MMU: tlbie(0x%lx)\n", va);

	/*
	 * The tlbie instruction changed behaviour starting with
	 * POWER6.  POWER6 and later don't have the large page flag
	 * in the instruction but in the RB value, along with bits
	 * indicating page and segment sizes.
	 */
	if (vcpu->arch.hflags & BOOK3S_HFLAG_NEW_TLBIE) {
		/* POWER6 or later */
		if (va & 1) {		/* L bit */
			if ((va & 0xf000) == 0x1000)
				mask = 0xFFFFFFFF0ULL;	/* 64k page */
			else
				mask = 0xFFFFFF000ULL;	/* 16M page */
		}
	} else {
		/* older processors, e.g. PPC970 */
		if (large)
			mask = 0xFFFFFF000ULL;
	}
	/* flush this VA on all vcpus */
	kvm_for_each_vcpu(i, v, vcpu->kvm)
		kvmppc_mmu_pte_vflush(v, va >> 12, mask);
}

#ifdef CONFIG_PPC_64K_PAGES
static int segment_contains_magic_page(struct kvm_vcpu *vcpu, ulong esid)
{
	ulong mp_ea = vcpu->arch.magic_page_ea;

	return mp_ea && !(kvmppc_get_msr(vcpu) & MSR_PR) &&
		(mp_ea >> SID_SHIFT) == esid;
}
#endif

static int kvmppc_mmu_book3s_64_esid_to_vsid(struct kvm_vcpu *vcpu, ulong esid,
					     u64 *vsid)
{
	ulong ea = esid << SID_SHIFT;
	struct kvmppc_slb *slb;
	u64 gvsid = esid;
	ulong mp_ea = vcpu->arch.magic_page_ea;
	int pagesize = MMU_PAGE_64K;
	u64 msr = kvmppc_get_msr(vcpu);

	if (msr & (MSR_DR|MSR_IR)) {
		slb = kvmppc_mmu_book3s_64_find_slbe(vcpu, ea);
		if (slb) {
			gvsid = slb->vsid;
			pagesize = slb->base_page_size;
			if (slb->tb) {
				gvsid <<= SID_SHIFT_1T - SID_SHIFT;
				gvsid |= esid & ((1ul << (SID_SHIFT_1T - SID_SHIFT)) - 1);
				gvsid |= VSID_1T;
			}
		}
	}

	switch (msr & (MSR_DR|MSR_IR)) {
	case 0:
		gvsid = VSID_REAL | esid;
		break;
	case MSR_IR:
		gvsid |= VSID_REAL_IR;
		break;
	case MSR_DR:
		gvsid |= VSID_REAL_DR;
		break;
	case MSR_DR|MSR_IR:
		if (!slb)
			goto no_slb;

		break;
	default:
		BUG();
		break;
	}

#ifdef CONFIG_PPC_64K_PAGES
	/*
	 * Mark this as a 64k segment if the host is using
	 * 64k pages, the host MMU supports 64k pages and
	 * the guest segment page size is >= 64k,
	 * but not if this segment contains the magic page.
	 */
	if (pagesize >= MMU_PAGE_64K &&
	    mmu_psize_defs[MMU_PAGE_64K].shift &&
	    !segment_contains_magic_page(vcpu, esid))
		gvsid |= VSID_64K;
#endif

	if (kvmppc_get_msr(vcpu) & MSR_PR)
		gvsid |= VSID_PR;

	*vsid = gvsid;
	return 0;

no_slb:
	/* Catch magic page case */
	if (unlikely(mp_ea) &&
	    unlikely(esid == (mp_ea >> SID_SHIFT)) &&
	    !(kvmppc_get_msr(vcpu) & MSR_PR)) {
		*vsid = VSID_REAL | esid;
		return 0;
	}

	return -EINVAL;
}

static bool kvmppc_mmu_book3s_64_is_dcbz32(struct kvm_vcpu *vcpu)
{
	return (to_book3s(vcpu)->hid[5] & 0x80);
}

void kvmppc_mmu_book3s_64_init(struct kvm_vcpu *vcpu)
{
	struct kvmppc_mmu *mmu = &vcpu->arch.mmu;

	mmu->mfsrin = NULL;
	mmu->mtsrin = kvmppc_mmu_book3s_64_mtsrin;
	mmu->slbmte = kvmppc_mmu_book3s_64_slbmte;
	mmu->slbmfee = kvmppc_mmu_book3s_64_slbmfee;
	mmu->slbmfev = kvmppc_mmu_book3s_64_slbmfev;
	mmu->slbie = kvmppc_mmu_book3s_64_slbie;
	mmu->slbia = kvmppc_mmu_book3s_64_slbia;
	mmu->xlate = kvmppc_mmu_book3s_64_xlate;
	mmu->reset_msr = kvmppc_mmu_book3s_64_reset_msr;
	mmu->tlbie = kvmppc_mmu_book3s_64_tlbie;
	mmu->esid_to_vsid = kvmppc_mmu_book3s_64_esid_to_vsid;
	mmu->ea_to_vp = kvmppc_mmu_book3s_64_ea_to_vp;
	mmu->is_dcbz32 = kvmppc_mmu_book3s_64_is_dcbz32;

	vcpu->arch.hflags |= BOOK3S_HFLAG_SLB;
}
