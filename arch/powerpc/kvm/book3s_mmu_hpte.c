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
#include <linux/hash.h>
#include <linux/slab.h>

#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/machdep.h>
#include <asm/mmu_context.h>
#include <asm/hw_irq.h>

#define PTE_SIZE	12

/* #define DEBUG_MMU */

#ifdef DEBUG_MMU
#define dprintk_mmu(a, ...) printk(KERN_INFO a, __VA_ARGS__)
#else
#define dprintk_mmu(a, ...) do { } while(0)
#endif

static struct kmem_cache *hpte_cache;

static inline u64 kvmppc_mmu_hash_pte(u64 eaddr)
{
	return hash_64(eaddr >> PTE_SIZE, HPTEG_HASH_BITS_PTE);
}

static inline u64 kvmppc_mmu_hash_vpte(u64 vpage)
{
	return hash_64(vpage & 0xfffffffffULL, HPTEG_HASH_BITS_VPTE);
}

static inline u64 kvmppc_mmu_hash_vpte_long(u64 vpage)
{
	return hash_64((vpage & 0xffffff000ULL) >> 12,
		       HPTEG_HASH_BITS_VPTE_LONG);
}

void kvmppc_mmu_hpte_cache_map(struct kvm_vcpu *vcpu, struct hpte_cache *pte)
{
	u64 index;

	/* Add to ePTE list */
	index = kvmppc_mmu_hash_pte(pte->pte.eaddr);
	hlist_add_head(&pte->list_pte, &vcpu->arch.hpte_hash_pte[index]);

	/* Add to vPTE list */
	index = kvmppc_mmu_hash_vpte(pte->pte.vpage);
	hlist_add_head(&pte->list_vpte, &vcpu->arch.hpte_hash_vpte[index]);

	/* Add to vPTE_long list */
	index = kvmppc_mmu_hash_vpte_long(pte->pte.vpage);
	hlist_add_head(&pte->list_vpte_long,
		       &vcpu->arch.hpte_hash_vpte_long[index]);
}

static void invalidate_pte(struct kvm_vcpu *vcpu, struct hpte_cache *pte)
{
	dprintk_mmu("KVM: Flushing SPT: 0x%lx (0x%llx) -> 0x%llx\n",
		    pte->pte.eaddr, pte->pte.vpage, pte->host_va);

	/* Different for 32 and 64 bit */
	kvmppc_mmu_invalidate_pte(vcpu, pte);

	if (pte->pte.may_write)
		kvm_release_pfn_dirty(pte->pfn);
	else
		kvm_release_pfn_clean(pte->pfn);

	hlist_del(&pte->list_pte);
	hlist_del(&pte->list_vpte);
	hlist_del(&pte->list_vpte_long);

	vcpu->arch.hpte_cache_count--;
	kmem_cache_free(hpte_cache, pte);
}

static void kvmppc_mmu_pte_flush_all(struct kvm_vcpu *vcpu)
{
	struct hpte_cache *pte;
	struct hlist_node *node, *tmp;
	int i;

	for (i = 0; i < HPTEG_HASH_NUM_VPTE_LONG; i++) {
		struct hlist_head *list = &vcpu->arch.hpte_hash_vpte_long[i];

		hlist_for_each_entry_safe(pte, node, tmp, list, list_vpte_long)
			invalidate_pte(vcpu, pte);
	}
}

static void kvmppc_mmu_pte_flush_page(struct kvm_vcpu *vcpu, ulong guest_ea)
{
	struct hlist_head *list;
	struct hlist_node *node, *tmp;
	struct hpte_cache *pte;

	/* Find the list of entries in the map */
	list = &vcpu->arch.hpte_hash_pte[kvmppc_mmu_hash_pte(guest_ea)];

	/* Check the list for matching entries and invalidate */
	hlist_for_each_entry_safe(pte, node, tmp, list, list_pte)
		if ((pte->pte.eaddr & ~0xfffUL) == guest_ea)
			invalidate_pte(vcpu, pte);
}

void kvmppc_mmu_pte_flush(struct kvm_vcpu *vcpu, ulong guest_ea, ulong ea_mask)
{
	u64 i;

	dprintk_mmu("KVM: Flushing %d Shadow PTEs: 0x%lx & 0x%lx\n",
		    vcpu->arch.hpte_cache_count, guest_ea, ea_mask);

	guest_ea &= ea_mask;

	switch (ea_mask) {
	case ~0xfffUL:
		kvmppc_mmu_pte_flush_page(vcpu, guest_ea);
		break;
	case 0x0ffff000:
		/* 32-bit flush w/o segment, go through all possible segments */
		for (i = 0; i < 0x100000000ULL; i += 0x10000000ULL)
			kvmppc_mmu_pte_flush(vcpu, guest_ea | i, ~0xfffUL);
		break;
	case 0:
		/* Doing a complete flush -> start from scratch */
		kvmppc_mmu_pte_flush_all(vcpu);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

/* Flush with mask 0xfffffffff */
static void kvmppc_mmu_pte_vflush_short(struct kvm_vcpu *vcpu, u64 guest_vp)
{
	struct hlist_head *list;
	struct hlist_node *node, *tmp;
	struct hpte_cache *pte;
	u64 vp_mask = 0xfffffffffULL;

	list = &vcpu->arch.hpte_hash_vpte[kvmppc_mmu_hash_vpte(guest_vp)];

	/* Check the list for matching entries and invalidate */
	hlist_for_each_entry_safe(pte, node, tmp, list, list_vpte)
		if ((pte->pte.vpage & vp_mask) == guest_vp)
			invalidate_pte(vcpu, pte);
}

/* Flush with mask 0xffffff000 */
static void kvmppc_mmu_pte_vflush_long(struct kvm_vcpu *vcpu, u64 guest_vp)
{
	struct hlist_head *list;
	struct hlist_node *node, *tmp;
	struct hpte_cache *pte;
	u64 vp_mask = 0xffffff000ULL;

	list = &vcpu->arch.hpte_hash_vpte_long[
		kvmppc_mmu_hash_vpte_long(guest_vp)];

	/* Check the list for matching entries and invalidate */
	hlist_for_each_entry_safe(pte, node, tmp, list, list_vpte_long)
		if ((pte->pte.vpage & vp_mask) == guest_vp)
			invalidate_pte(vcpu, pte);
}

void kvmppc_mmu_pte_vflush(struct kvm_vcpu *vcpu, u64 guest_vp, u64 vp_mask)
{
	dprintk_mmu("KVM: Flushing %d Shadow vPTEs: 0x%llx & 0x%llx\n",
		    vcpu->arch.hpte_cache_count, guest_vp, vp_mask);
	guest_vp &= vp_mask;

	switch(vp_mask) {
	case 0xfffffffffULL:
		kvmppc_mmu_pte_vflush_short(vcpu, guest_vp);
		break;
	case 0xffffff000ULL:
		kvmppc_mmu_pte_vflush_long(vcpu, guest_vp);
		break;
	default:
		WARN_ON(1);
		return;
	}
}

void kvmppc_mmu_pte_pflush(struct kvm_vcpu *vcpu, ulong pa_start, ulong pa_end)
{
	struct hlist_node *node, *tmp;
	struct hpte_cache *pte;
	int i;

	dprintk_mmu("KVM: Flushing %d Shadow pPTEs: 0x%lx - 0x%lx\n",
		    vcpu->arch.hpte_cache_count, pa_start, pa_end);

	for (i = 0; i < HPTEG_HASH_NUM_VPTE_LONG; i++) {
		struct hlist_head *list = &vcpu->arch.hpte_hash_vpte_long[i];

		hlist_for_each_entry_safe(pte, node, tmp, list, list_vpte_long)
			if ((pte->pte.raddr >= pa_start) &&
			    (pte->pte.raddr < pa_end))
				invalidate_pte(vcpu, pte);
	}
}

struct hpte_cache *kvmppc_mmu_hpte_cache_next(struct kvm_vcpu *vcpu)
{
	struct hpte_cache *pte;

	pte = kmem_cache_zalloc(hpte_cache, GFP_KERNEL);
	vcpu->arch.hpte_cache_count++;

	if (vcpu->arch.hpte_cache_count == HPTEG_CACHE_NUM)
		kvmppc_mmu_pte_flush_all(vcpu);

	return pte;
}

void kvmppc_mmu_hpte_destroy(struct kvm_vcpu *vcpu)
{
	kvmppc_mmu_pte_flush(vcpu, 0, 0);
}

static void kvmppc_mmu_hpte_init_hash(struct hlist_head *hash_list, int len)
{
	int i;

	for (i = 0; i < len; i++)
		INIT_HLIST_HEAD(&hash_list[i]);
}

int kvmppc_mmu_hpte_init(struct kvm_vcpu *vcpu)
{
	/* init hpte lookup hashes */
	kvmppc_mmu_hpte_init_hash(vcpu->arch.hpte_hash_pte,
				  ARRAY_SIZE(vcpu->arch.hpte_hash_pte));
	kvmppc_mmu_hpte_init_hash(vcpu->arch.hpte_hash_vpte,
				  ARRAY_SIZE(vcpu->arch.hpte_hash_vpte));
	kvmppc_mmu_hpte_init_hash(vcpu->arch.hpte_hash_vpte_long,
				  ARRAY_SIZE(vcpu->arch.hpte_hash_vpte_long));

	return 0;
}

int kvmppc_mmu_hpte_sysinit(void)
{
	/* init hpte slab cache */
	hpte_cache = kmem_cache_create("kvm-spt", sizeof(struct hpte_cache),
				       sizeof(struct hpte_cache), 0, NULL);

	return 0;
}

void kvmppc_mmu_hpte_sysexit(void)
{
	kmem_cache_destroy(hpte_cache);
}
