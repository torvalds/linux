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
#include <asm/mmu-hash64.h>

/* #define DEBUG_MMU */

#ifdef DEBUG_MMU
#define dprintk(X...) printk(KERN_INFO X)
#else
#define dprintk(X...) do { } while(0)
#endif

static void kvmppc_mmu_book3s_64_reset_msr(struct kvm_vcpu *vcpu)
{
	kvmppc_set_msr(vcpu, MSR_SF);
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

static int kvmppc_mmu_book3s_64_get_pagesize(struct kvmppc_slb *slbe)
{
	return slbe->large ? 24 : 12;
}

static u32 kvmppc_mmu_book3s_64_get_page(struct kvmppc_slb *slbe, gva_t eaddr)
{
	int p = kvmppc_mmu_book3s_64_get_pagesize(slbe);

	return ((eaddr & kvmppc_slb_offset_mask(slbe)) >> p);
}

static hva_t kvmppc_mmu_book3s_64_get_pteg(
				struct kvmppc_vcpu_book3s *vcpu_book3s,
				struct kvmppc_slb *slbe, gva_t eaddr,
				bool second)
{
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
	if (vcpu_book3s->vcpu.arch.papr_enabled)
		r = pteg;
	else
		r = gfn_to_hva(vcpu_book3s->vcpu.kvm, pteg >> PAGE_SHIFT);

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

	if (p < 24)
		avpn >>= ((80 - p) - 56) - 8;
	else
		avpn <<= 8;

	return avpn;
}

static int kvmppc_mmu_book3s_64_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
				struct kvmppc_pte *gpte, bool data)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	struct kvmppc_slb *slbe;
	hva_t ptegp;
	u64 pteg[16];
	u64 avpn = 0;
	int i;
	u8 key = 0;
	bool found = false;
	int second = 0;
	ulong mp_ea = vcpu->arch.magic_page_ea;

	/* Magic page override */
	if (unlikely(mp_ea) &&
	    unlikely((eaddr & ~0xfffULL) == (mp_ea & ~0xfffULL)) &&
	    !(vcpu->arch.shared->msr & MSR_PR)) {
		gpte->eaddr = eaddr;
		gpte->vpage = kvmppc_mmu_book3s_64_ea_to_vp(vcpu, eaddr, data);
		gpte->raddr = vcpu->arch.magic_page_pa | (gpte->raddr & 0xfff);
		gpte->raddr &= KVM_PAM;
		gpte->may_execute = true;
		gpte->may_read = true;
		gpte->may_write = true;

		return 0;
	}

	slbe = kvmppc_mmu_book3s_64_find_slbe(vcpu, eaddr);
	if (!slbe)
		goto no_seg_found;

	avpn = kvmppc_mmu_book3s_64_get_avpn(slbe, eaddr);
	if (slbe->tb)
		avpn |= SLB_VSID_B_1T;

do_second:
	ptegp = kvmppc_mmu_book3s_64_get_pteg(vcpu_book3s, slbe, eaddr, second);
	if (kvm_is_error_hva(ptegp))
		goto no_page_found;

	if(copy_from_user(pteg, (void __user *)ptegp, sizeof(pteg))) {
		printk(KERN_ERR "KVM can't copy data from 0x%lx!\n", ptegp);
		goto no_page_found;
	}

	if ((vcpu->arch.shared->msr & MSR_PR) && slbe->Kp)
		key = 4;
	else if (!(vcpu->arch.shared->msr & MSR_PR) && slbe->Ks)
		key = 4;

	for (i=0; i<16; i+=2) {
		u64 v = pteg[i];
		u64 r = pteg[i+1];

		/* Valid check */
		if (!(v & HPTE_V_VALID))
			continue;
		/* Hash check */
		if ((v & HPTE_V_SECONDARY) != second)
			continue;

		/* AVPN compare */
		if (HPTE_V_COMPARE(avpn, v)) {
			u8 pp = (r & HPTE_R_PP) | key;
			int eaddr_mask = 0xFFF;

			gpte->eaddr = eaddr;
			gpte->vpage = kvmppc_mmu_book3s_64_ea_to_vp(vcpu,
								    eaddr,
								    data);
			if (slbe->large)
				eaddr_mask = 0xFFFFFF;
			gpte->raddr = (r & HPTE_R_RPN) | (eaddr & eaddr_mask);
			gpte->may_execute = ((r & HPTE_R_N) ? false : true);
			gpte->may_read = false;
			gpte->may_write = false;

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
				gpte->may_read = true;
				break;
			}

			dprintk("KVM MMU: Translated 0x%lx [0x%llx] -> 0x%llx "
				"-> 0x%lx\n",
				eaddr, avpn, gpte->vpage, gpte->raddr);
			found = true;
			break;
		}
	}

	/* Update PTE R and C bits, so the guest's swapper knows we used the
	 * page */
	if (found) {
		u32 oldr = pteg[i+1];

		if (gpte->may_read) {
			/* Set the accessed flag */
			pteg[i+1] |= HPTE_R_R;
		}
		if (gpte->may_write) {
			/* Set the dirty flag */
			pteg[i+1] |= HPTE_R_C;
		} else {
			dprintk("KVM: Mapping read-only page!\n");
		}

		/* Write back into the PTEG */
		if (pteg[i+1] != oldr)
			copy_to_user((void __user *)ptegp, pteg, sizeof(pteg));

		if (!gpte->may_read)
			return -EPERM;
		return 0;
	} else {
		dprintk("KVM MMU: No PTE found (ea=0x%lx sdr1=0x%llx "
			"ptegp=0x%lx)\n",
			eaddr, to_book3s(vcpu)->sdr1, ptegp);
		for (i = 0; i < 16; i += 2)
			dprintk("   %02d: 0x%llx - 0x%llx (0x%llx)\n",
				i, pteg[i], pteg[i+1], avpn);

		if (!second) {
			second = HPTE_V_SECONDARY;
			goto do_second;
		}
	}

no_page_found:
	return -ENOENT;

no_seg_found:

	dprintk("KVM MMU: Trigger segment fault\n");
	return -EINVAL;
}

static void kvmppc_mmu_book3s_64_slbmte(struct kvm_vcpu *vcpu, u64 rs, u64 rb)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s;
	u64 esid, esid_1t;
	int slb_nr;
	struct kvmppc_slb *slbe;

	dprintk("KVM MMU: slbmte(0x%llx, 0x%llx)\n", rs, rb);

	vcpu_book3s = to_book3s(vcpu);

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

	seg_size = 1ull << kvmppc_slb_sid_shift(slbe);
	kvmppc_mmu_flush_segment(vcpu, ea & ~(seg_size - 1), seg_size);
}

static void kvmppc_mmu_book3s_64_slbia(struct kvm_vcpu *vcpu)
{
	int i;

	dprintk("KVM MMU: slbia()\n");

	for (i = 1; i < vcpu->arch.slb_nr; i++)
		vcpu->arch.slb[i].valid = false;

	if (vcpu->arch.shared->msr & MSR_IR) {
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

	dprintk("KVM MMU: tlbie(0x%lx)\n", va);

	if (large)
		mask = 0xFFFFFF000ULL;
	kvmppc_mmu_pte_vflush(vcpu, va >> 12, mask);
}

static int kvmppc_mmu_book3s_64_esid_to_vsid(struct kvm_vcpu *vcpu, ulong esid,
					     u64 *vsid)
{
	ulong ea = esid << SID_SHIFT;
	struct kvmppc_slb *slb;
	u64 gvsid = esid;
	ulong mp_ea = vcpu->arch.magic_page_ea;

	if (vcpu->arch.shared->msr & (MSR_DR|MSR_IR)) {
		slb = kvmppc_mmu_book3s_64_find_slbe(vcpu, ea);
		if (slb) {
			gvsid = slb->vsid;
			if (slb->tb) {
				gvsid <<= SID_SHIFT_1T - SID_SHIFT;
				gvsid |= esid & ((1ul << (SID_SHIFT_1T - SID_SHIFT)) - 1);
				gvsid |= VSID_1T;
			}
		}
	}

	switch (vcpu->arch.shared->msr & (MSR_DR|MSR_IR)) {
	case 0:
		*vsid = VSID_REAL | esid;
		break;
	case MSR_IR:
		*vsid = VSID_REAL_IR | gvsid;
		break;
	case MSR_DR:
		*vsid = VSID_REAL_DR | gvsid;
		break;
	case MSR_DR|MSR_IR:
		if (!slb)
			goto no_slb;

		*vsid = gvsid;
		break;
	default:
		BUG();
		break;
	}

	if (vcpu->arch.shared->msr & MSR_PR)
		*vsid |= VSID_PR;

	return 0;

no_slb:
	/* Catch magic page case */
	if (unlikely(mp_ea) &&
	    unlikely(esid == (mp_ea >> SID_SHIFT)) &&
	    !(vcpu->arch.shared->msr & MSR_PR)) {
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
