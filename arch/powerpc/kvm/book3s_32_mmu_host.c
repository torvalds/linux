/*
 * Copyright (C) 2010 SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *     Alexander Graf <agraf@suse.de>
 *
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
 */

#include <linux/kvm_host.h>

#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu-hash32.h>
#include <asm/machdep.h>
#include <asm/mmu_context.h>
#include <asm/hw_irq.h>

/* #define DEBUG_MMU */
/* #define DEBUG_SR */

#ifdef DEBUG_MMU
#define dprintk_mmu(a, ...) printk(KERN_INFO a, __VA_ARGS__)
#else
#define dprintk_mmu(a, ...) do { } while(0)
#endif

#ifdef DEBUG_SR
#define dprintk_sr(a, ...) printk(KERN_INFO a, __VA_ARGS__)
#else
#define dprintk_sr(a, ...) do { } while(0)
#endif

#if PAGE_SHIFT != 12
#error Unknown page size
#endif

#ifdef CONFIG_SMP
#error XXX need to grab mmu_hash_lock
#endif

#ifdef CONFIG_PTE_64BIT
#error Only 32 bit pages are supported for now
#endif

static ulong htab;
static u32 htabmask;

static void invalidate_pte(struct kvm_vcpu *vcpu, struct hpte_cache *pte)
{
	volatile u32 *pteg;

	dprintk_mmu("KVM: Flushing SPTE: 0x%llx (0x%llx) -> 0x%llx\n",
		    pte->pte.eaddr, pte->pte.vpage, pte->host_va);

	pteg = (u32*)pte->slot;

	pteg[0] = 0;
	asm volatile ("sync");
	asm volatile ("tlbie %0" : : "r" (pte->pte.eaddr) : "memory");
	asm volatile ("sync");
	asm volatile ("tlbsync");

	pte->host_va = 0;

	if (pte->pte.may_write)
		kvm_release_pfn_dirty(pte->pfn);
	else
		kvm_release_pfn_clean(pte->pfn);
}

void kvmppc_mmu_pte_flush(struct kvm_vcpu *vcpu, ulong guest_ea, ulong ea_mask)
{
	int i;

	dprintk_mmu("KVM: Flushing %d Shadow PTEs: 0x%x & 0x%x\n",
		    vcpu->arch.hpte_cache_offset, guest_ea, ea_mask);
	BUG_ON(vcpu->arch.hpte_cache_offset > HPTEG_CACHE_NUM);

	guest_ea &= ea_mask;
	for (i = 0; i < vcpu->arch.hpte_cache_offset; i++) {
		struct hpte_cache *pte;

		pte = &vcpu->arch.hpte_cache[i];
		if (!pte->host_va)
			continue;

		if ((pte->pte.eaddr & ea_mask) == guest_ea) {
			invalidate_pte(vcpu, pte);
		}
	}

	/* Doing a complete flush -> start from scratch */
	if (!ea_mask)
		vcpu->arch.hpte_cache_offset = 0;
}

void kvmppc_mmu_pte_vflush(struct kvm_vcpu *vcpu, u64 guest_vp, u64 vp_mask)
{
	int i;

	dprintk_mmu("KVM: Flushing %d Shadow vPTEs: 0x%llx & 0x%llx\n",
		    vcpu->arch.hpte_cache_offset, guest_vp, vp_mask);
	BUG_ON(vcpu->arch.hpte_cache_offset > HPTEG_CACHE_NUM);

	guest_vp &= vp_mask;
	for (i = 0; i < vcpu->arch.hpte_cache_offset; i++) {
		struct hpte_cache *pte;

		pte = &vcpu->arch.hpte_cache[i];
		if (!pte->host_va)
			continue;

		if ((pte->pte.vpage & vp_mask) == guest_vp) {
			invalidate_pte(vcpu, pte);
		}
	}
}

void kvmppc_mmu_pte_pflush(struct kvm_vcpu *vcpu, ulong pa_start, ulong pa_end)
{
	int i;

	dprintk_mmu("KVM: Flushing %d Shadow pPTEs: 0x%llx & 0x%llx\n",
		    vcpu->arch.hpte_cache_offset, pa_start, pa_end);
	BUG_ON(vcpu->arch.hpte_cache_offset > HPTEG_CACHE_NUM);

	for (i = 0; i < vcpu->arch.hpte_cache_offset; i++) {
		struct hpte_cache *pte;

		pte = &vcpu->arch.hpte_cache[i];
		if (!pte->host_va)
			continue;

		if ((pte->pte.raddr >= pa_start) &&
		    (pte->pte.raddr < pa_end)) {
			invalidate_pte(vcpu, pte);
		}
	}
}

struct kvmppc_pte *kvmppc_mmu_find_pte(struct kvm_vcpu *vcpu, u64 ea, bool data)
{
	int i;
	u64 guest_vp;

	guest_vp = vcpu->arch.mmu.ea_to_vp(vcpu, ea, false);
	for (i=0; i<vcpu->arch.hpte_cache_offset; i++) {
		struct hpte_cache *pte;

		pte = &vcpu->arch.hpte_cache[i];
		if (!pte->host_va)
			continue;

		if (pte->pte.vpage == guest_vp)
			return &pte->pte;
	}

	return NULL;
}

static int kvmppc_mmu_hpte_cache_next(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.hpte_cache_offset == HPTEG_CACHE_NUM)
		kvmppc_mmu_pte_flush(vcpu, 0, 0);

	return vcpu->arch.hpte_cache_offset++;
}

/* We keep 512 gvsid->hvsid entries, mapping the guest ones to the array using
 * a hash, so we don't waste cycles on looping */
static u16 kvmppc_sid_hash(struct kvm_vcpu *vcpu, u64 gvsid)
{
	return (u16)(((gvsid >> (SID_MAP_BITS * 7)) & SID_MAP_MASK) ^
		     ((gvsid >> (SID_MAP_BITS * 6)) & SID_MAP_MASK) ^
		     ((gvsid >> (SID_MAP_BITS * 5)) & SID_MAP_MASK) ^
		     ((gvsid >> (SID_MAP_BITS * 4)) & SID_MAP_MASK) ^
		     ((gvsid >> (SID_MAP_BITS * 3)) & SID_MAP_MASK) ^
		     ((gvsid >> (SID_MAP_BITS * 2)) & SID_MAP_MASK) ^
		     ((gvsid >> (SID_MAP_BITS * 1)) & SID_MAP_MASK) ^
		     ((gvsid >> (SID_MAP_BITS * 0)) & SID_MAP_MASK));
}


static struct kvmppc_sid_map *find_sid_vsid(struct kvm_vcpu *vcpu, u64 gvsid)
{
	struct kvmppc_sid_map *map;
	u16 sid_map_mask;

	if (vcpu->arch.msr & MSR_PR)
		gvsid |= VSID_PR;

	sid_map_mask = kvmppc_sid_hash(vcpu, gvsid);
	map = &to_book3s(vcpu)->sid_map[sid_map_mask];
	if (map->guest_vsid == gvsid) {
		dprintk_sr("SR: Searching 0x%llx -> 0x%llx\n",
			    gvsid, map->host_vsid);
		return map;
	}

	map = &to_book3s(vcpu)->sid_map[SID_MAP_MASK - sid_map_mask];
	if (map->guest_vsid == gvsid) {
		dprintk_sr("SR: Searching 0x%llx -> 0x%llx\n",
			    gvsid, map->host_vsid);
		return map;
	}

	dprintk_sr("SR: Searching 0x%llx -> not found\n", gvsid);
	return NULL;
}

static u32 *kvmppc_mmu_get_pteg(struct kvm_vcpu *vcpu, u32 vsid, u32 eaddr,
				bool primary)
{
	u32 page, hash;
	ulong pteg = htab;

	page = (eaddr & ~ESID_MASK) >> 12;

	hash = ((vsid ^ page) << 6);
	if (!primary)
		hash = ~hash;

	hash &= htabmask;

	pteg |= hash;

	dprintk_mmu("htab: %lx | hash: %x | htabmask: %x | pteg: %lx\n",
		htab, hash, htabmask, pteg);

	return (u32*)pteg;
}

extern char etext[];

int kvmppc_mmu_map_page(struct kvm_vcpu *vcpu, struct kvmppc_pte *orig_pte)
{
	pfn_t hpaddr;
	u64 va;
	u64 vsid;
	struct kvmppc_sid_map *map;
	volatile u32 *pteg;
	u32 eaddr = orig_pte->eaddr;
	u32 pteg0, pteg1;
	register int rr = 0;
	bool primary = false;
	bool evict = false;
	int hpte_id;
	struct hpte_cache *pte;

	/* Get host physical address for gpa */
	hpaddr = gfn_to_pfn(vcpu->kvm, orig_pte->raddr >> PAGE_SHIFT);
	if (kvm_is_error_hva(hpaddr)) {
		printk(KERN_INFO "Couldn't get guest page for gfn %lx!\n",
				 orig_pte->eaddr);
		return -EINVAL;
	}
	hpaddr <<= PAGE_SHIFT;

	/* and write the mapping ea -> hpa into the pt */
	vcpu->arch.mmu.esid_to_vsid(vcpu, orig_pte->eaddr >> SID_SHIFT, &vsid);
	map = find_sid_vsid(vcpu, vsid);
	if (!map) {
		kvmppc_mmu_map_segment(vcpu, eaddr);
		map = find_sid_vsid(vcpu, vsid);
	}
	BUG_ON(!map);

	vsid = map->host_vsid;
	va = (vsid << SID_SHIFT) | (eaddr & ~ESID_MASK);

next_pteg:
	if (rr == 16) {
		primary = !primary;
		evict = true;
		rr = 0;
	}

	pteg = kvmppc_mmu_get_pteg(vcpu, vsid, eaddr, primary);

	/* not evicting yet */
	if (!evict && (pteg[rr] & PTE_V)) {
		rr += 2;
		goto next_pteg;
	}

	dprintk_mmu("KVM: old PTEG: %p (%d)\n", pteg, rr);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[0], pteg[1]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[2], pteg[3]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[4], pteg[5]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[6], pteg[7]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[8], pteg[9]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[10], pteg[11]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[12], pteg[13]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[14], pteg[15]);

	pteg0 = ((eaddr & 0x0fffffff) >> 22) | (vsid << 7) | PTE_V |
		(primary ? 0 : PTE_SEC);
	pteg1 = hpaddr | PTE_M | PTE_R | PTE_C;

	if (orig_pte->may_write) {
		pteg1 |= PP_RWRW;
		mark_page_dirty(vcpu->kvm, orig_pte->raddr >> PAGE_SHIFT);
	} else {
		pteg1 |= PP_RWRX;
	}

	local_irq_disable();

	if (pteg[rr]) {
		pteg[rr] = 0;
		asm volatile ("sync");
	}
	pteg[rr + 1] = pteg1;
	pteg[rr] = pteg0;
	asm volatile ("sync");

	local_irq_enable();

	dprintk_mmu("KVM: new PTEG: %p\n", pteg);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[0], pteg[1]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[2], pteg[3]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[4], pteg[5]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[6], pteg[7]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[8], pteg[9]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[10], pteg[11]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[12], pteg[13]);
	dprintk_mmu("KVM:   %08x - %08x\n", pteg[14], pteg[15]);


	/* Now tell our Shadow PTE code about the new page */

	hpte_id = kvmppc_mmu_hpte_cache_next(vcpu);
	pte = &vcpu->arch.hpte_cache[hpte_id];

	dprintk_mmu("KVM: %c%c Map 0x%llx: [%lx] 0x%llx (0x%llx) -> %lx\n",
		    orig_pte->may_write ? 'w' : '-',
		    orig_pte->may_execute ? 'x' : '-',
		    orig_pte->eaddr, (ulong)pteg, va,
		    orig_pte->vpage, hpaddr);

	pte->slot = (ulong)&pteg[rr];
	pte->host_va = va;
	pte->pte = *orig_pte;
	pte->pfn = hpaddr >> PAGE_SHIFT;

	return 0;
}

static struct kvmppc_sid_map *create_sid_map(struct kvm_vcpu *vcpu, u64 gvsid)
{
	struct kvmppc_sid_map *map;
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	u16 sid_map_mask;
	static int backwards_map = 0;

	if (vcpu->arch.msr & MSR_PR)
		gvsid |= VSID_PR;

	/* We might get collisions that trap in preceding order, so let's
	   map them differently */

	sid_map_mask = kvmppc_sid_hash(vcpu, gvsid);
	if (backwards_map)
		sid_map_mask = SID_MAP_MASK - sid_map_mask;

	map = &to_book3s(vcpu)->sid_map[sid_map_mask];

	/* Make sure we're taking the other map next time */
	backwards_map = !backwards_map;

	/* Uh-oh ... out of mappings. Let's flush! */
	if (vcpu_book3s->vsid_next >= vcpu_book3s->vsid_max) {
		vcpu_book3s->vsid_next = vcpu_book3s->vsid_first;
		memset(vcpu_book3s->sid_map, 0,
		       sizeof(struct kvmppc_sid_map) * SID_MAP_NUM);
		kvmppc_mmu_pte_flush(vcpu, 0, 0);
		kvmppc_mmu_flush_segments(vcpu);
	}
	map->host_vsid = vcpu_book3s->vsid_next;

	/* Would have to be 111 to be completely aligned with the rest of
	   Linux, but that is just way too little space! */
	vcpu_book3s->vsid_next+=1;

	map->guest_vsid = gvsid;
	map->valid = true;

	return map;
}

int kvmppc_mmu_map_segment(struct kvm_vcpu *vcpu, ulong eaddr)
{
	u32 esid = eaddr >> SID_SHIFT;
	u64 gvsid;
	u32 sr;
	struct kvmppc_sid_map *map;
	struct kvmppc_book3s_shadow_vcpu *svcpu = to_svcpu(vcpu);

	if (vcpu->arch.mmu.esid_to_vsid(vcpu, esid, &gvsid)) {
		/* Invalidate an entry */
		svcpu->sr[esid] = SR_INVALID;
		return -ENOENT;
	}

	map = find_sid_vsid(vcpu, gvsid);
	if (!map)
		map = create_sid_map(vcpu, gvsid);

	map->guest_esid = esid;
	sr = map->host_vsid | SR_KP;
	svcpu->sr[esid] = sr;

	dprintk_sr("MMU: mtsr %d, 0x%x\n", esid, sr);

	return 0;
}

void kvmppc_mmu_flush_segments(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvmppc_book3s_shadow_vcpu *svcpu = to_svcpu(vcpu);

	dprintk_sr("MMU: flushing all segments (%d)\n", ARRAY_SIZE(svcpu->sr));
	for (i = 0; i < ARRAY_SIZE(svcpu->sr); i++)
		svcpu->sr[i] = SR_INVALID;
}

void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu)
{
	kvmppc_mmu_pte_flush(vcpu, 0, 0);
	preempt_disable();
	__destroy_context(to_book3s(vcpu)->context_id);
	preempt_enable();
}

/* From mm/mmu_context_hash32.c */
#define CTX_TO_VSID(ctx) (((ctx) * (897 * 16)) & 0xffffff)

int kvmppc_mmu_init(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_book3s *vcpu3s = to_book3s(vcpu);
	int err;
	ulong sdr1;

	err = __init_new_context();
	if (err < 0)
		return -1;
	vcpu3s->context_id = err;

	vcpu3s->vsid_max = CTX_TO_VSID(vcpu3s->context_id + 1) - 1;
	vcpu3s->vsid_first = CTX_TO_VSID(vcpu3s->context_id);

#if 0 /* XXX still doesn't guarantee uniqueness */
	/* We could collide with the Linux vsid space because the vsid
	 * wraps around at 24 bits. We're safe if we do our own space
	 * though, so let's always set the highest bit. */

	vcpu3s->vsid_max |= 0x00800000;
	vcpu3s->vsid_first |= 0x00800000;
#endif
	BUG_ON(vcpu3s->vsid_max < vcpu3s->vsid_first);

	vcpu3s->vsid_next = vcpu3s->vsid_first;

	/* Remember where the HTAB is */
	asm ( "mfsdr1 %0" : "=r"(sdr1) );
	htabmask = ((sdr1 & 0x1FF) << 16) | 0xFFC0;
	htab = (ulong)__va(sdr1 & 0xffff0000);

	return 0;
}
