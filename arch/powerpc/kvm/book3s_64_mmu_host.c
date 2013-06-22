/*
 * Copyright (C) 2009 SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *     Alexander Graf <agraf@suse.de>
 *     Kevin Wolf <mail@kevin-wolf.de>
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
#include <asm/mmu-hash64.h>
#include <asm/machdep.h>
#include <asm/mmu_context.h>
#include <asm/hw_irq.h>
#include "trace.h"

#define PTE_SIZE 12

void kvmppc_mmu_invalidate_pte(struct kvm_vcpu *vcpu, struct hpte_cache *pte)
{
	ppc_md.hpte_invalidate(pte->slot, pte->host_vpn,
			       MMU_PAGE_4K, MMU_SEGSIZE_256M,
			       false);
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

	if (vcpu->arch.shared->msr & MSR_PR)
		gvsid |= VSID_PR;

	sid_map_mask = kvmppc_sid_hash(vcpu, gvsid);
	map = &to_book3s(vcpu)->sid_map[sid_map_mask];
	if (map->valid && (map->guest_vsid == gvsid)) {
		trace_kvm_book3s_slb_found(gvsid, map->host_vsid);
		return map;
	}

	map = &to_book3s(vcpu)->sid_map[SID_MAP_MASK - sid_map_mask];
	if (map->valid && (map->guest_vsid == gvsid)) {
		trace_kvm_book3s_slb_found(gvsid, map->host_vsid);
		return map;
	}

	trace_kvm_book3s_slb_fail(sid_map_mask, gvsid);
	return NULL;
}

int kvmppc_mmu_map_page(struct kvm_vcpu *vcpu, struct kvmppc_pte *orig_pte)
{
	unsigned long vpn;
	pfn_t hpaddr;
	ulong hash, hpteg;
	u64 vsid;
	int ret;
	int rflags = 0x192;
	int vflags = 0;
	int attempt = 0;
	struct kvmppc_sid_map *map;
	int r = 0;

	/* Get host physical address for gpa */
	hpaddr = kvmppc_gfn_to_pfn(vcpu, orig_pte->raddr >> PAGE_SHIFT);
	if (is_error_noslot_pfn(hpaddr)) {
		printk(KERN_INFO "Couldn't get guest page for gfn %lx!\n", orig_pte->eaddr);
		r = -EINVAL;
		goto out;
	}
	hpaddr <<= PAGE_SHIFT;
	hpaddr |= orig_pte->raddr & (~0xfffULL & ~PAGE_MASK);

	/* and write the mapping ea -> hpa into the pt */
	vcpu->arch.mmu.esid_to_vsid(vcpu, orig_pte->eaddr >> SID_SHIFT, &vsid);
	map = find_sid_vsid(vcpu, vsid);
	if (!map) {
		ret = kvmppc_mmu_map_segment(vcpu, orig_pte->eaddr);
		WARN_ON(ret < 0);
		map = find_sid_vsid(vcpu, vsid);
	}
	if (!map) {
		printk(KERN_ERR "KVM: Segment map for 0x%llx (0x%lx) failed\n",
				vsid, orig_pte->eaddr);
		WARN_ON(true);
		r = -EINVAL;
		goto out;
	}

	vsid = map->host_vsid;
	vpn = hpt_vpn(orig_pte->eaddr, vsid, MMU_SEGSIZE_256M);

	if (!orig_pte->may_write)
		rflags |= HPTE_R_PP;
	else
		mark_page_dirty(vcpu->kvm, orig_pte->raddr >> PAGE_SHIFT);

	if (!orig_pte->may_execute)
		rflags |= HPTE_R_N;
	else
		kvmppc_mmu_flush_icache(hpaddr >> PAGE_SHIFT);

	hash = hpt_hash(vpn, PTE_SIZE, MMU_SEGSIZE_256M);

map_again:
	hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);

	/* In case we tried normal mapping already, let's nuke old entries */
	if (attempt > 1)
		if (ppc_md.hpte_remove(hpteg) < 0) {
			r = -1;
			goto out;
		}

	ret = ppc_md.hpte_insert(hpteg, vpn, hpaddr, rflags, vflags,
				 MMU_PAGE_4K, MMU_PAGE_4K, MMU_SEGSIZE_256M);

	if (ret < 0) {
		/* If we couldn't map a primary PTE, try a secondary */
		hash = ~hash;
		vflags ^= HPTE_V_SECONDARY;
		attempt++;
		goto map_again;
	} else {
		struct hpte_cache *pte = kvmppc_mmu_hpte_cache_next(vcpu);

		trace_kvm_book3s_64_mmu_map(rflags, hpteg,
					    vpn, hpaddr, orig_pte);

		/* The ppc_md code may give us a secondary entry even though we
		   asked for a primary. Fix up. */
		if ((ret & _PTEIDX_SECONDARY) && !(vflags & HPTE_V_SECONDARY)) {
			hash = ~hash;
			hpteg = ((hash & htab_hash_mask) * HPTES_PER_GROUP);
		}

		pte->slot = hpteg + (ret & 7);
		pte->host_vpn = vpn;
		pte->pte = *orig_pte;
		pte->pfn = hpaddr >> PAGE_SHIFT;

		kvmppc_mmu_hpte_cache_map(vcpu, pte);
	}
	kvm_release_pfn_clean(hpaddr >> PAGE_SHIFT);

out:
	return r;
}

static struct kvmppc_sid_map *create_sid_map(struct kvm_vcpu *vcpu, u64 gvsid)
{
	struct kvmppc_sid_map *map;
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);
	u16 sid_map_mask;
	static int backwards_map = 0;

	if (vcpu->arch.shared->msr & MSR_PR)
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
	if (vcpu_book3s->proto_vsid_next == vcpu_book3s->proto_vsid_max) {
		vcpu_book3s->proto_vsid_next = vcpu_book3s->proto_vsid_first;
		memset(vcpu_book3s->sid_map, 0,
		       sizeof(struct kvmppc_sid_map) * SID_MAP_NUM);
		kvmppc_mmu_pte_flush(vcpu, 0, 0);
		kvmppc_mmu_flush_segments(vcpu);
	}
	map->host_vsid = vsid_scramble(vcpu_book3s->proto_vsid_next++, 256M);

	map->guest_vsid = gvsid;
	map->valid = true;

	trace_kvm_book3s_slb_map(sid_map_mask, gvsid, map->host_vsid);

	return map;
}

static int kvmppc_mmu_next_segment(struct kvm_vcpu *vcpu, ulong esid)
{
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
	int i;
	int max_slb_size = 64;
	int found_inval = -1;
	int r;

	if (!svcpu->slb_max)
		svcpu->slb_max = 1;

	/* Are we overwriting? */
	for (i = 1; i < svcpu->slb_max; i++) {
		if (!(svcpu->slb[i].esid & SLB_ESID_V))
			found_inval = i;
		else if ((svcpu->slb[i].esid & ESID_MASK) == esid) {
			r = i;
			goto out;
		}
	}

	/* Found a spare entry that was invalidated before */
	if (found_inval > 0) {
		r = found_inval;
		goto out;
	}

	/* No spare invalid entry, so create one */

	if (mmu_slb_size < 64)
		max_slb_size = mmu_slb_size;

	/* Overflowing -> purge */
	if ((svcpu->slb_max) == max_slb_size)
		kvmppc_mmu_flush_segments(vcpu);

	r = svcpu->slb_max;
	svcpu->slb_max++;

out:
	svcpu_put(svcpu);
	return r;
}

int kvmppc_mmu_map_segment(struct kvm_vcpu *vcpu, ulong eaddr)
{
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
	u64 esid = eaddr >> SID_SHIFT;
	u64 slb_esid = (eaddr & ESID_MASK) | SLB_ESID_V;
	u64 slb_vsid = SLB_VSID_USER;
	u64 gvsid;
	int slb_index;
	struct kvmppc_sid_map *map;
	int r = 0;

	slb_index = kvmppc_mmu_next_segment(vcpu, eaddr & ESID_MASK);

	if (vcpu->arch.mmu.esid_to_vsid(vcpu, esid, &gvsid)) {
		/* Invalidate an entry */
		svcpu->slb[slb_index].esid = 0;
		r = -ENOENT;
		goto out;
	}

	map = find_sid_vsid(vcpu, gvsid);
	if (!map)
		map = create_sid_map(vcpu, gvsid);

	map->guest_esid = esid;

	slb_vsid |= (map->host_vsid << 12);
	slb_vsid &= ~SLB_VSID_KP;
	slb_esid |= slb_index;

	svcpu->slb[slb_index].esid = slb_esid;
	svcpu->slb[slb_index].vsid = slb_vsid;

	trace_kvm_book3s_slbmte(slb_vsid, slb_esid);

out:
	svcpu_put(svcpu);
	return r;
}

void kvmppc_mmu_flush_segments(struct kvm_vcpu *vcpu)
{
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
	svcpu->slb_max = 1;
	svcpu->slb[0].esid = 0;
	svcpu_put(svcpu);
}

void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu)
{
	kvmppc_mmu_hpte_destroy(vcpu);
	__destroy_context(to_book3s(vcpu)->context_id[0]);
}

int kvmppc_mmu_init(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_book3s *vcpu3s = to_book3s(vcpu);
	int err;

	err = __init_new_context();
	if (err < 0)
		return -1;
	vcpu3s->context_id[0] = err;

	vcpu3s->proto_vsid_max = ((u64)(vcpu3s->context_id[0] + 1)
				  << ESID_BITS) - 1;
	vcpu3s->proto_vsid_first = (u64)vcpu3s->context_id[0] << ESID_BITS;
	vcpu3s->proto_vsid_next = vcpu3s->proto_vsid_first;

	kvmppc_mmu_hpte_init(vcpu);

	return 0;
}
