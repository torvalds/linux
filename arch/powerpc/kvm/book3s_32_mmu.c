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

/* #define DEBUG_MMU */
/* #define DEBUG_MMU_PTE */
/* #define DEBUG_MMU_PTE_IP 0xfff14c40 */

#ifdef DEBUG_MMU
#define dprintk(X...) printk(KERN_INFO X)
#else
#define dprintk(X...) do { } while(0)
#endif

#ifdef DEBUG_MMU_PTE
#define dprintk_pte(X...) printk(KERN_INFO X)
#else
#define dprintk_pte(X...) do { } while(0)
#endif

#define PTEG_FLAG_ACCESSED	0x00000100
#define PTEG_FLAG_DIRTY		0x00000080
#ifndef SID_SHIFT
#define SID_SHIFT		28
#endif

static inline bool check_debug_ip(struct kvm_vcpu *vcpu)
{
#ifdef DEBUG_MMU_PTE_IP
	return vcpu->arch.pc == DEBUG_MMU_PTE_IP;
#else
	return true;
#endif
}

static int kvmppc_mmu_book3s_32_xlate_bat(struct kvm_vcpu *vcpu, gva_t eaddr,
					  struct kvmppc_pte *pte, bool data);
static int kvmppc_mmu_book3s_32_esid_to_vsid(struct kvm_vcpu *vcpu, ulong esid,
					     u64 *vsid);

static struct kvmppc_sr *find_sr(struct kvmppc_vcpu_book3s *vcpu_book3s, gva_t eaddr)
{
	return &vcpu_book3s->sr[(eaddr >> 28) & 0xf];
}

static u64 kvmppc_mmu_book3s_32_ea_to_vp(struct kvm_vcpu *vcpu, gva_t eaddr,
					 bool data)
{
	u64 vsid;
	struct kvmppc_pte pte;

	if (!kvmppc_mmu_book3s_32_xlate_bat(vcpu, eaddr, &pte, data))
		return pte.vpage;

	kvmppc_mmu_book3s_32_esid_to_vsid(vcpu, eaddr >> SID_SHIFT, &vsid);
	return (((u64)eaddr >> 12) & 0xffff) | (vsid << 16);
}

static void kvmppc_mmu_book3s_32_reset_msr(struct kvm_vcpu *vcpu)
{
	kvmppc_set_msr(vcpu, 0);
}

static hva_t kvmppc_mmu_book3s_32_get_pteg(struct kvmppc_vcpu_book3s *vcpu_book3s,
				      struct kvmppc_sr *sre, gva_t eaddr,
				      bool primary)
{
	u32 page, hash, pteg, htabmask;
	hva_t r;

	page = (eaddr & 0x0FFFFFFF) >> 12;
	htabmask = ((vcpu_book3s->sdr1 & 0x1FF) << 16) | 0xFFC0;

	hash = ((sre->vsid ^ page) << 6);
	if (!primary)
		hash = ~hash;
	hash &= htabmask;

	pteg = (vcpu_book3s->sdr1 & 0xffff0000) | hash;

	dprintk("MMU: pc=0x%lx eaddr=0x%lx sdr1=0x%llx pteg=0x%x vsid=0x%x\n",
		vcpu_book3s->vcpu.arch.pc, eaddr, vcpu_book3s->sdr1, pteg,
		sre->vsid);

	r = gfn_to_hva(vcpu_book3s->vcpu.kvm, pteg >> PAGE_SHIFT);
	if (kvm_is_error_hva(r))
		return r;
	return r | (pteg & ~PAGE_MASK);
}

static u32 kvmppc_mmu_book3s_32_get_ptem(struct kvmppc_sr *sre, gva_t eaddr,
				    bool primary)
{
	return ((eaddr & 0x0fffffff) >> 22) | (sre->vsid << 7) |
	       (primary ? 0 : 0x40) | 0x80000000;
}

static int kvmppc_mmu_book3s_32_xlate_bat(struct kvm_vcpu *vcpu, gva_t eaddr,
					  struct kvmppc_pte *pte, bool data)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	struct kvmppc_bat *bat;
	int i;

	for (i = 0; i < 8; i++) {
		if (data)
			bat = &vcpu_book3s->dbat[i];
		else
			bat = &vcpu_book3s->ibat[i];

		if (vcpu->arch.msr & MSR_PR) {
			if (!bat->vp)
				continue;
		} else {
			if (!bat->vs)
				continue;
		}

		if (check_debug_ip(vcpu))
		{
			dprintk_pte("%cBAT %02d: 0x%lx - 0x%x (0x%x)\n",
				    data ? 'd' : 'i', i, eaddr, bat->bepi,
				    bat->bepi_mask);
		}
		if ((eaddr & bat->bepi_mask) == bat->bepi) {
			u64 vsid;
			kvmppc_mmu_book3s_32_esid_to_vsid(vcpu,
				eaddr >> SID_SHIFT, &vsid);
			vsid <<= 16;
			pte->vpage = (((u64)eaddr >> 12) & 0xffff) | vsid;

			pte->raddr = bat->brpn | (eaddr & ~bat->bepi_mask);
			pte->may_read = bat->pp;
			pte->may_write = bat->pp > 1;
			pte->may_execute = true;
			if (!pte->may_read) {
				printk(KERN_INFO "BAT is not readable!\n");
				continue;
			}
			if (!pte->may_write) {
				/* let's treat r/o BATs as not-readable for now */
				dprintk_pte("BAT is read-only!\n");
				continue;
			}

			return 0;
		}
	}

	return -ENOENT;
}

static int kvmppc_mmu_book3s_32_xlate_pte(struct kvm_vcpu *vcpu, gva_t eaddr,
				     struct kvmppc_pte *pte, bool data,
				     bool primary)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	struct kvmppc_sr *sre;
	hva_t ptegp;
	u32 pteg[16];
	u32 ptem = 0;
	int i;
	int found = 0;

	sre = find_sr(vcpu_book3s, eaddr);

	dprintk_pte("SR 0x%lx: vsid=0x%x, raw=0x%x\n", eaddr >> 28,
		    sre->vsid, sre->raw);

	pte->vpage = kvmppc_mmu_book3s_32_ea_to_vp(vcpu, eaddr, data);

	ptegp = kvmppc_mmu_book3s_32_get_pteg(vcpu_book3s, sre, eaddr, primary);
	if (kvm_is_error_hva(ptegp)) {
		printk(KERN_INFO "KVM: Invalid PTEG!\n");
		goto no_page_found;
	}

	ptem = kvmppc_mmu_book3s_32_get_ptem(sre, eaddr, primary);

	if(copy_from_user(pteg, (void __user *)ptegp, sizeof(pteg))) {
		printk(KERN_ERR "KVM: Can't copy data from 0x%lx!\n", ptegp);
		goto no_page_found;
	}

	for (i=0; i<16; i+=2) {
		if (ptem == pteg[i]) {
			u8 pp;

			pte->raddr = (pteg[i+1] & ~(0xFFFULL)) | (eaddr & 0xFFF);
			pp = pteg[i+1] & 3;

			if ((sre->Kp &&  (vcpu->arch.msr & MSR_PR)) ||
			    (sre->Ks && !(vcpu->arch.msr & MSR_PR)))
				pp |= 4;

			pte->may_write = false;
			pte->may_read = false;
			pte->may_execute = true;
			switch (pp) {
				case 0:
				case 1:
				case 2:
				case 6:
					pte->may_write = true;
				case 3:
				case 5:
				case 7:
					pte->may_read = true;
					break;
			}

			if ( !pte->may_read )
				continue;

			dprintk_pte("MMU: Found PTE -> %x %x - %x\n",
				    pteg[i], pteg[i+1], pp);
			found = 1;
			break;
		}
	}

	/* Update PTE C and A bits, so the guest's swapper knows we used the
	   page */
	if (found) {
		u32 oldpte = pteg[i+1];

		if (pte->may_read)
			pteg[i+1] |= PTEG_FLAG_ACCESSED;
		if (pte->may_write)
			pteg[i+1] |= PTEG_FLAG_DIRTY;
		else
			dprintk_pte("KVM: Mapping read-only page!\n");

		/* Write back into the PTEG */
		if (pteg[i+1] != oldpte)
			copy_to_user((void __user *)ptegp, pteg, sizeof(pteg));

		return 0;
	}

no_page_found:

	if (check_debug_ip(vcpu)) {
		dprintk_pte("KVM MMU: No PTE found (sdr1=0x%llx ptegp=0x%lx)\n",
			    to_book3s(vcpu)->sdr1, ptegp);
		for (i=0; i<16; i+=2) {
			dprintk_pte("   %02d: 0x%x - 0x%x (0x%llx)\n",
				    i, pteg[i], pteg[i+1], ptem);
		}
	}

	return -ENOENT;
}

static int kvmppc_mmu_book3s_32_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
				      struct kvmppc_pte *pte, bool data)
{
	int r;

	pte->eaddr = eaddr;
	r = kvmppc_mmu_book3s_32_xlate_bat(vcpu, eaddr, pte, data);
	if (r < 0)
	       r = kvmppc_mmu_book3s_32_xlate_pte(vcpu, eaddr, pte, data, true);
	if (r < 0)
	       r = kvmppc_mmu_book3s_32_xlate_pte(vcpu, eaddr, pte, data, false);

	return r;
}


static u32 kvmppc_mmu_book3s_32_mfsrin(struct kvm_vcpu *vcpu, u32 srnum)
{
	return to_book3s(vcpu)->sr[srnum].raw;
}

static void kvmppc_mmu_book3s_32_mtsrin(struct kvm_vcpu *vcpu, u32 srnum,
					ulong value)
{
	struct kvmppc_sr *sre;

	sre = &to_book3s(vcpu)->sr[srnum];

	/* Flush any left-over shadows from the previous SR */

	/* XXX Not necessary? */
	/* kvmppc_mmu_pte_flush(vcpu, ((u64)sre->vsid) << 28, 0xf0000000ULL); */

	/* And then put in the new SR */
	sre->raw = value;
	sre->vsid = (value & 0x0fffffff);
	sre->valid = (value & 0x80000000) ? false : true;
	sre->Ks = (value & 0x40000000) ? true : false;
	sre->Kp = (value & 0x20000000) ? true : false;
	sre->nx = (value & 0x10000000) ? true : false;

	/* Map the new segment */
	kvmppc_mmu_map_segment(vcpu, srnum << SID_SHIFT);
}

static void kvmppc_mmu_book3s_32_tlbie(struct kvm_vcpu *vcpu, ulong ea, bool large)
{
	kvmppc_mmu_pte_flush(vcpu, ea, 0x0FFFF000);
}

static int kvmppc_mmu_book3s_32_esid_to_vsid(struct kvm_vcpu *vcpu, ulong esid,
					     u64 *vsid)
{
	ulong ea = esid << SID_SHIFT;
	struct kvmppc_sr *sr;
	u64 gvsid = esid;

	if (vcpu->arch.msr & (MSR_DR|MSR_IR)) {
		sr = find_sr(to_book3s(vcpu), ea);
		if (sr->valid)
			gvsid = sr->vsid;
	}

	/* In case we only have one of MSR_IR or MSR_DR set, let's put
	   that in the real-mode context (and hope RM doesn't access
	   high memory) */
	switch (vcpu->arch.msr & (MSR_DR|MSR_IR)) {
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
		if (sr->valid)
			*vsid = sr->vsid;
		else
			*vsid = VSID_BAT | gvsid;
		break;
	default:
		BUG();
	}

	if (vcpu->arch.msr & MSR_PR)
		*vsid |= VSID_PR;

	return 0;
}

static bool kvmppc_mmu_book3s_32_is_dcbz32(struct kvm_vcpu *vcpu)
{
	return true;
}


void kvmppc_mmu_book3s_32_init(struct kvm_vcpu *vcpu)
{
	struct kvmppc_mmu *mmu = &vcpu->arch.mmu;

	mmu->mtsrin = kvmppc_mmu_book3s_32_mtsrin;
	mmu->mfsrin = kvmppc_mmu_book3s_32_mfsrin;
	mmu->xlate = kvmppc_mmu_book3s_32_xlate;
	mmu->reset_msr = kvmppc_mmu_book3s_32_reset_msr;
	mmu->tlbie = kvmppc_mmu_book3s_32_tlbie;
	mmu->esid_to_vsid = kvmppc_mmu_book3s_32_esid_to_vsid;
	mmu->ea_to_vp = kvmppc_mmu_book3s_32_ea_to_vp;
	mmu->is_dcbz32 = kvmppc_mmu_book3s_32_is_dcbz32;

	mmu->slbmte = NULL;
	mmu->slbmfee = NULL;
	mmu->slbmfev = NULL;
	mmu->slbie = NULL;
	mmu->slbia = NULL;
}
