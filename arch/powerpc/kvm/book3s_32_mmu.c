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
	return vcpu->arch.regs.nip == DEBUG_MMU_PTE_IP;
#else
	return true;
#endif
}

static inline u32 sr_vsid(u32 sr_raw)
{
	return sr_raw & 0x0fffffff;
}

static inline bool sr_valid(u32 sr_raw)
{
	return (sr_raw & 0x80000000) ? false : true;
}

static inline bool sr_ks(u32 sr_raw)
{
	return (sr_raw & 0x40000000) ? true: false;
}

static inline bool sr_kp(u32 sr_raw)
{
	return (sr_raw & 0x20000000) ? true: false;
}

static int kvmppc_mmu_book3s_32_xlate_bat(struct kvm_vcpu *vcpu, gva_t eaddr,
					  struct kvmppc_pte *pte, bool data,
					  bool iswrite);
static int kvmppc_mmu_book3s_32_esid_to_vsid(struct kvm_vcpu *vcpu, ulong esid,
					     u64 *vsid);

static u32 find_sr(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	return kvmppc_get_sr(vcpu, (eaddr >> 28) & 0xf);
}

static u64 kvmppc_mmu_book3s_32_ea_to_vp(struct kvm_vcpu *vcpu, gva_t eaddr,
					 bool data)
{
	u64 vsid;
	struct kvmppc_pte pte;

	if (!kvmppc_mmu_book3s_32_xlate_bat(vcpu, eaddr, &pte, data, false))
		return pte.vpage;

	kvmppc_mmu_book3s_32_esid_to_vsid(vcpu, eaddr >> SID_SHIFT, &vsid);
	return (((u64)eaddr >> 12) & 0xffff) | (vsid << 16);
}

static void kvmppc_mmu_book3s_32_reset_msr(struct kvm_vcpu *vcpu)
{
	kvmppc_set_msr(vcpu, 0);
}

static hva_t kvmppc_mmu_book3s_32_get_pteg(struct kvm_vcpu *vcpu,
				      u32 sre, gva_t eaddr,
				      bool primary)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	u32 page, hash, pteg, htabmask;
	hva_t r;

	page = (eaddr & 0x0FFFFFFF) >> 12;
	htabmask = ((vcpu_book3s->sdr1 & 0x1FF) << 16) | 0xFFC0;

	hash = ((sr_vsid(sre) ^ page) << 6);
	if (!primary)
		hash = ~hash;
	hash &= htabmask;

	pteg = (vcpu_book3s->sdr1 & 0xffff0000) | hash;

	dprintk("MMU: pc=0x%lx eaddr=0x%lx sdr1=0x%llx pteg=0x%x vsid=0x%x\n",
		kvmppc_get_pc(vcpu), eaddr, vcpu_book3s->sdr1, pteg,
		sr_vsid(sre));

	r = gfn_to_hva(vcpu->kvm, pteg >> PAGE_SHIFT);
	if (kvm_is_error_hva(r))
		return r;
	return r | (pteg & ~PAGE_MASK);
}

static u32 kvmppc_mmu_book3s_32_get_ptem(u32 sre, gva_t eaddr, bool primary)
{
	return ((eaddr & 0x0fffffff) >> 22) | (sr_vsid(sre) << 7) |
	       (primary ? 0 : 0x40) | 0x80000000;
}

static int kvmppc_mmu_book3s_32_xlate_bat(struct kvm_vcpu *vcpu, gva_t eaddr,
					  struct kvmppc_pte *pte, bool data,
					  bool iswrite)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	struct kvmppc_bat *bat;
	int i;

	for (i = 0; i < 8; i++) {
		if (data)
			bat = &vcpu_book3s->dbat[i];
		else
			bat = &vcpu_book3s->ibat[i];

		if (kvmppc_get_msr(vcpu) & MSR_PR) {
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
			if (iswrite && !pte->may_write) {
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
				     bool iswrite, bool primary)
{
	u32 sre;
	hva_t ptegp;
	u32 pteg[16];
	u32 pte0, pte1;
	u32 ptem = 0;
	int i;
	int found = 0;

	sre = find_sr(vcpu, eaddr);

	dprintk_pte("SR 0x%lx: vsid=0x%x, raw=0x%x\n", eaddr >> 28,
		    sr_vsid(sre), sre);

	pte->vpage = kvmppc_mmu_book3s_32_ea_to_vp(vcpu, eaddr, data);

	ptegp = kvmppc_mmu_book3s_32_get_pteg(vcpu, sre, eaddr, primary);
	if (kvm_is_error_hva(ptegp)) {
		printk(KERN_INFO "KVM: Invalid PTEG!\n");
		goto no_page_found;
	}

	ptem = kvmppc_mmu_book3s_32_get_ptem(sre, eaddr, primary);

	if(copy_from_user(pteg, (void __user *)ptegp, sizeof(pteg))) {
		printk_ratelimited(KERN_ERR
			"KVM: Can't copy data from 0x%lx!\n", ptegp);
		goto no_page_found;
	}

	for (i=0; i<16; i+=2) {
		pte0 = be32_to_cpu(pteg[i]);
		pte1 = be32_to_cpu(pteg[i + 1]);
		if (ptem == pte0) {
			u8 pp;

			pte->raddr = (pte1 & ~(0xFFFULL)) | (eaddr & 0xFFF);
			pp = pte1 & 3;

			if ((sr_kp(sre) &&  (kvmppc_get_msr(vcpu) & MSR_PR)) ||
			    (sr_ks(sre) && !(kvmppc_get_msr(vcpu) & MSR_PR)))
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

			dprintk_pte("MMU: Found PTE -> %x %x - %x\n",
				    pte0, pte1, pp);
			found = 1;
			break;
		}
	}

	/* Update PTE C and A bits, so the guest's swapper knows we used the
	   page */
	if (found) {
		u32 pte_r = pte1;
		char __user *addr = (char __user *) (ptegp + (i+1) * sizeof(u32));

		/*
		 * Use single-byte writes to update the HPTE, to
		 * conform to what real hardware does.
		 */
		if (pte->may_read && !(pte_r & PTEG_FLAG_ACCESSED)) {
			pte_r |= PTEG_FLAG_ACCESSED;
			put_user(pte_r >> 8, addr + 2);
		}
		if (iswrite && pte->may_write && !(pte_r & PTEG_FLAG_DIRTY)) {
			pte_r |= PTEG_FLAG_DIRTY;
			put_user(pte_r, addr + 3);
		}
		if (!pte->may_read || (iswrite && !pte->may_write))
			return -EPERM;
		return 0;
	}

no_page_found:

	if (check_debug_ip(vcpu)) {
		dprintk_pte("KVM MMU: No PTE found (sdr1=0x%llx ptegp=0x%lx)\n",
			    to_book3s(vcpu)->sdr1, ptegp);
		for (i=0; i<16; i+=2) {
			dprintk_pte("   %02d: 0x%x - 0x%x (0x%x)\n",
				    i, be32_to_cpu(pteg[i]),
				    be32_to_cpu(pteg[i+1]), ptem);
		}
	}

	return -ENOENT;
}

static int kvmppc_mmu_book3s_32_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
				      struct kvmppc_pte *pte, bool data,
				      bool iswrite)
{
	int r;
	ulong mp_ea = vcpu->arch.magic_page_ea;

	pte->eaddr = eaddr;
	pte->page_size = MMU_PAGE_4K;

	/* Magic page override */
	if (unlikely(mp_ea) &&
	    unlikely((eaddr & ~0xfffULL) == (mp_ea & ~0xfffULL)) &&
	    !(kvmppc_get_msr(vcpu) & MSR_PR)) {
		pte->vpage = kvmppc_mmu_book3s_32_ea_to_vp(vcpu, eaddr, data);
		pte->raddr = vcpu->arch.magic_page_pa | (pte->raddr & 0xfff);
		pte->raddr &= KVM_PAM;
		pte->may_execute = true;
		pte->may_read = true;
		pte->may_write = true;

		return 0;
	}

	r = kvmppc_mmu_book3s_32_xlate_bat(vcpu, eaddr, pte, data, iswrite);
	if (r < 0)
		r = kvmppc_mmu_book3s_32_xlate_pte(vcpu, eaddr, pte,
						   data, iswrite, true);
	if (r == -ENOENT)
		r = kvmppc_mmu_book3s_32_xlate_pte(vcpu, eaddr, pte,
						   data, iswrite, false);

	return r;
}


static u32 kvmppc_mmu_book3s_32_mfsrin(struct kvm_vcpu *vcpu, u32 srnum)
{
	return kvmppc_get_sr(vcpu, srnum);
}

static void kvmppc_mmu_book3s_32_mtsrin(struct kvm_vcpu *vcpu, u32 srnum,
					ulong value)
{
	kvmppc_set_sr(vcpu, srnum, value);
	kvmppc_mmu_map_segment(vcpu, srnum << SID_SHIFT);
}

static void kvmppc_mmu_book3s_32_tlbie(struct kvm_vcpu *vcpu, ulong ea, bool large)
{
	int i;
	struct kvm_vcpu *v;

	/* flush this VA on all cpus */
	kvm_for_each_vcpu(i, v, vcpu->kvm)
		kvmppc_mmu_pte_flush(v, ea, 0x0FFFF000);
}

static int kvmppc_mmu_book3s_32_esid_to_vsid(struct kvm_vcpu *vcpu, ulong esid,
					     u64 *vsid)
{
	ulong ea = esid << SID_SHIFT;
	u32 sr;
	u64 gvsid = esid;
	u64 msr = kvmppc_get_msr(vcpu);

	if (msr & (MSR_DR|MSR_IR)) {
		sr = find_sr(vcpu, ea);
		if (sr_valid(sr))
			gvsid = sr_vsid(sr);
	}

	/* In case we only have one of MSR_IR or MSR_DR set, let's put
	   that in the real-mode context (and hope RM doesn't access
	   high memory) */
	switch (msr & (MSR_DR|MSR_IR)) {
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
		if (sr_valid(sr))
			*vsid = sr_vsid(sr);
		else
			*vsid = VSID_BAT | gvsid;
		break;
	default:
		BUG();
	}

	if (msr & MSR_PR)
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
