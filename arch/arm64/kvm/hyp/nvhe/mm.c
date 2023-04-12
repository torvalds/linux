// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pgtable.h>
#include <asm/kvm_pkvm.h>
#include <asm/spectre.h>

#include <nvhe/early_alloc.h>
#include <nvhe/gfp.h>
#include <nvhe/memory.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/modules.h>
#include <nvhe/spinlock.h>

struct kvm_pgtable pkvm_pgtable;
hyp_spinlock_t pkvm_pgd_lock;

struct memblock_region hyp_memory[HYP_MEMBLOCK_REGIONS];
unsigned int hyp_memblock_nr;

static u64 __private_range_base;
static u64 __private_range_cur;

struct hyp_fixmap_slot {
	u64 addr;
	kvm_pte_t *ptep;
};
static DEFINE_PER_CPU(struct hyp_fixmap_slot, fixmap_slots);

static int __pkvm_create_mappings(unsigned long start, unsigned long size,
				  unsigned long phys, enum kvm_pgtable_prot prot)
{
	int err;

	hyp_spin_lock(&pkvm_pgd_lock);
	err = kvm_pgtable_hyp_map(&pkvm_pgtable, start, size, phys, prot);
	hyp_spin_unlock(&pkvm_pgd_lock);

	return err;
}

/**
 * pkvm_alloc_private_va_range - Allocates a private VA range.
 * @size:	The size of the VA range to reserve.
 * @haddr:	The hypervisor virtual start address of the allocation.
 *
 * The private virtual address (VA) range is allocated above __private_range_base
 * and aligned based on the order of @size.
 *
 * Return: 0 on success or negative error code on failure.
 */
int pkvm_alloc_private_va_range(size_t size, unsigned long *haddr)
{
	unsigned long cur, addr;
	int ret = 0;

	hyp_spin_lock(&pkvm_pgd_lock);

	/* Align the allocation based on the order of its size */
	addr = ALIGN(__private_range_cur, PAGE_SIZE << get_order(size));

	/* The allocated size is always a multiple of PAGE_SIZE */
	cur = addr + PAGE_ALIGN(size);

	/* Has the private range grown too large ? */
	if (!addr || cur > __hyp_vmemmap || (cur - __private_range_base) > __PKVM_PRIVATE_SZ) {
		ret = -ENOMEM;
	} else {
		__private_range_cur = cur;
		*haddr = addr;
	}

	hyp_spin_unlock(&pkvm_pgd_lock);

	return ret;
}

int __pkvm_create_private_mapping(phys_addr_t phys, size_t size,
				  enum kvm_pgtable_prot prot,
				  unsigned long *haddr)
{
	unsigned long addr;
	int err;

	size = PAGE_ALIGN(size + offset_in_page(phys));
	err = pkvm_alloc_private_va_range(size, &addr);
	if (err)
		return err;

	err = __pkvm_create_mappings(addr, size, phys, prot);
	if (err)
		return err;

	*haddr = addr + offset_in_page(phys);
	return err;
}

#ifdef CONFIG_NVHE_EL2_DEBUG
static unsigned long mod_range_start = ULONG_MAX;
static unsigned long mod_range_end;
static DEFINE_HYP_SPINLOCK(mod_range_lock);

static void update_mod_range(unsigned long addr, size_t size)
{
	hyp_spin_lock(&mod_range_lock);
	mod_range_start = min(mod_range_start, addr);
	mod_range_end = max(mod_range_end, addr + size);
	hyp_spin_unlock(&mod_range_lock);
}

void assert_in_mod_range(unsigned long addr)
{
	/*
	 * This is not entirely watertight if there are private range
	 * allocations between modules being loaded, but in practice that is
	 * probably going to be allocation initiated by the modules themselves.
	 */
	hyp_spin_lock(&mod_range_lock);
	WARN_ON(addr < mod_range_start || mod_range_end <= addr);
	hyp_spin_unlock(&mod_range_lock);
}
#else
static inline void update_mod_range(unsigned long addr, size_t size) { }
#endif

void *__pkvm_alloc_module_va(u64 nr_pages)
{
	size_t size = nr_pages << PAGE_SHIFT;
	unsigned long addr = 0;

	if (!pkvm_alloc_private_va_range(size, &addr))
		update_mod_range(addr, size);

	return (void *)addr;
}

int __pkvm_map_module_page(u64 pfn, void *va, enum kvm_pgtable_prot prot, bool is_protected)
{
	unsigned long addr = (unsigned long)va;
	int ret;

	assert_in_mod_range(addr);

	if (!is_protected) {
		ret = __pkvm_host_donate_hyp(pfn, 1);
		if (ret)
			return ret;
	}

	ret = __pkvm_create_mappings(addr, PAGE_SIZE, hyp_pfn_to_phys(pfn), prot);
	if (ret && !is_protected)
		WARN_ON(__pkvm_hyp_donate_host(pfn, 1));

	return ret;
}

void __pkvm_unmap_module_page(u64 pfn, void *va)
{
	WARN_ON(__pkvm_hyp_donate_host(pfn, 1));
	pkvm_remove_mappings(va, va + PAGE_SIZE);
}

int pkvm_create_mappings_locked(void *from, void *to, enum kvm_pgtable_prot prot)
{
	unsigned long start = (unsigned long)from;
	unsigned long end = (unsigned long)to;
	unsigned long virt_addr;
	phys_addr_t phys;

	hyp_assert_lock_held(&pkvm_pgd_lock);

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (virt_addr = start; virt_addr < end; virt_addr += PAGE_SIZE) {
		int err;

		phys = hyp_virt_to_phys((void *)virt_addr);
		err = kvm_pgtable_hyp_map(&pkvm_pgtable, virt_addr, PAGE_SIZE,
					  phys, prot);
		if (err)
			return err;
	}

	return 0;
}

int pkvm_create_mappings(void *from, void *to, enum kvm_pgtable_prot prot)
{
	int ret;

	hyp_spin_lock(&pkvm_pgd_lock);
	ret = pkvm_create_mappings_locked(from, to, prot);
	hyp_spin_unlock(&pkvm_pgd_lock);

	return ret;
}

void pkvm_remove_mappings(void *from, void *to)
{
	unsigned long size = (unsigned long)to - (unsigned long)from;

	hyp_spin_lock(&pkvm_pgd_lock);
	WARN_ON(kvm_pgtable_hyp_unmap(&pkvm_pgtable, (u64)from, size) != size);
	hyp_spin_unlock(&pkvm_pgd_lock);
}

int hyp_back_vmemmap(phys_addr_t back)
{
	unsigned long i, start, size, end = 0;
	int ret;

	for (i = 0; i < hyp_memblock_nr; i++) {
		start = hyp_memory[i].base;
		start = ALIGN_DOWN((u64)hyp_phys_to_page(start), PAGE_SIZE);
		/*
		 * The begining of the hyp_vmemmap region for the current
		 * memblock may already be backed by the page backing the end
		 * the previous region, so avoid mapping it twice.
		 */
		start = max(start, end);

		end = hyp_memory[i].base + hyp_memory[i].size;
		end = PAGE_ALIGN((u64)hyp_phys_to_page(end));
		if (start >= end)
			continue;

		size = end - start;
		ret = __pkvm_create_mappings(start, size, back, PAGE_HYP);
		if (ret)
			return ret;

		memset(hyp_phys_to_virt(back), 0, size);
		back += size;
	}

	return 0;
}

static void *__hyp_bp_vect_base;
int pkvm_cpu_set_vector(enum arm64_hyp_spectre_vector slot)
{
	void *vector;

	switch (slot) {
	case HYP_VECTOR_DIRECT: {
		vector = __kvm_hyp_vector;
		break;
	}
	case HYP_VECTOR_SPECTRE_DIRECT: {
		vector = __bp_harden_hyp_vecs;
		break;
	}
	case HYP_VECTOR_INDIRECT:
	case HYP_VECTOR_SPECTRE_INDIRECT: {
		vector = (void *)__hyp_bp_vect_base;
		break;
	}
	default:
		return -EINVAL;
	}

	vector = __kvm_vector_slot2addr(vector, slot);
	*this_cpu_ptr(&kvm_hyp_vector) = (unsigned long)vector;

	return 0;
}

int hyp_map_vectors(void)
{
	phys_addr_t phys;
	unsigned long bp_base;
	int ret;

	if (!kvm_system_needs_idmapped_vectors()) {
		__hyp_bp_vect_base = __bp_harden_hyp_vecs;
		return 0;
	}

	phys = __hyp_pa(__bp_harden_hyp_vecs);
	ret = __pkvm_create_private_mapping(phys, __BP_HARDEN_HYP_VECS_SZ,
					    PAGE_HYP_EXEC, &bp_base);
	if (ret)
		return ret;

	__hyp_bp_vect_base = (void *)bp_base;

	return 0;
}

void *hyp_fixmap_map(phys_addr_t phys)
{
	struct hyp_fixmap_slot *slot = this_cpu_ptr(&fixmap_slots);
	kvm_pte_t pte, *ptep = slot->ptep;

	pte = *ptep;
	pte &= ~kvm_phys_to_pte(KVM_PHYS_INVALID);
	pte |= kvm_phys_to_pte(phys) | KVM_PTE_VALID;
	WRITE_ONCE(*ptep, pte);
	dsb(ishst);

	return (void *)slot->addr + offset_in_page(phys);
}

#define KVM_PTE_LEAF_ATTR_LO_S1_ATTRIDX	GENMASK(4, 2)
void *hyp_fixmap_map_nc(phys_addr_t phys)
{
	struct hyp_fixmap_slot *slot = this_cpu_ptr(&fixmap_slots);
	kvm_pte_t pte, *ptep = slot->ptep;

	pte = *ptep;
	pte &= ~kvm_phys_to_pte(KVM_PHYS_INVALID);
	pte |= kvm_phys_to_pte(phys) | KVM_PTE_VALID |
	       FIELD_PREP(KVM_PTE_LEAF_ATTR_LO_S1_ATTRIDX, MT_NORMAL_NC);
	WRITE_ONCE(*ptep, pte);
	dsb(ishst);

	return (void *)slot->addr;
}

static void fixmap_clear_slot(struct hyp_fixmap_slot *slot)
{
	kvm_pte_t *ptep = slot->ptep;
	u64 addr = slot->addr;

	/* Zap the memory type too. MT_NORMAL is 0 so the fixmap is cacheable by default */
	WRITE_ONCE(*ptep, *ptep & ~(KVM_PTE_VALID | KVM_PTE_LEAF_ATTR_LO_S1_ATTRIDX));

	/*
	 * Irritatingly, the architecture requires that we use inner-shareable
	 * broadcast TLB invalidation here in case another CPU speculates
	 * through our fixmap and decides to create an "amalagamation of the
	 * values held in the TLB" due to the apparent lack of a
	 * break-before-make sequence.
	 *
	 * https://lore.kernel.org/kvm/20221017115209.2099-1-will@kernel.org/T/#mf10dfbaf1eaef9274c581b81c53758918c1d0f03
	 */
	dsb(ishst);
	__tlbi_level(vale2is, __TLBI_VADDR(addr, 0), (KVM_PGTABLE_MAX_LEVELS - 1));
	dsb(ish);
	isb();
}

void hyp_fixmap_unmap(void)
{
	fixmap_clear_slot(this_cpu_ptr(&fixmap_slots));
}

static int __create_fixmap_slot_cb(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
				   enum kvm_pgtable_walk_flags flag,
				   void * const arg)
{
	struct hyp_fixmap_slot *slot = per_cpu_ptr(&fixmap_slots, (u64)arg);

	if (!kvm_pte_valid(*ptep) || level != KVM_PGTABLE_MAX_LEVELS - 1)
		return -EINVAL;

	slot->addr = addr;
	slot->ptep = ptep;

	/*
	 * Clear the PTE, but keep the page-table page refcount elevated to
	 * prevent it from ever being freed. This lets us manipulate the PTEs
	 * by hand safely without ever needing to allocate memory.
	 */
	fixmap_clear_slot(slot);

	return 0;
}

static int create_fixmap_slot(u64 addr, u64 cpu)
{
	struct kvm_pgtable_walker walker = {
		.cb	= __create_fixmap_slot_cb,
		.flags	= KVM_PGTABLE_WALK_LEAF,
		.arg = (void *)cpu,
	};

	return kvm_pgtable_walk(&pkvm_pgtable, addr, PAGE_SIZE, &walker);
}

int hyp_create_pcpu_fixmap(void)
{
	unsigned long addr, i;
	int ret;

	for (i = 0; i < hyp_nr_cpus; i++) {
		ret = pkvm_alloc_private_va_range(PAGE_SIZE, &addr);
		if (ret)
			return ret;

		ret = kvm_pgtable_hyp_map(&pkvm_pgtable, addr, PAGE_SIZE,
					  __hyp_pa(__hyp_bss_start), PAGE_HYP);
		if (ret)
			return ret;

		ret = create_fixmap_slot(addr, i);
		if (ret)
			return ret;
	}

	return 0;
}

int hyp_create_idmap(u32 hyp_va_bits)
{
	unsigned long start, end;

	start = hyp_virt_to_phys((void *)__hyp_idmap_text_start);
	start = ALIGN_DOWN(start, PAGE_SIZE);

	end = hyp_virt_to_phys((void *)__hyp_idmap_text_end);
	end = ALIGN(end, PAGE_SIZE);

	/*
	 * One half of the VA space is reserved to linearly map portions of
	 * memory -- see va_layout.c for more details. The other half of the VA
	 * space contains the trampoline page, and needs some care. Split that
	 * second half in two and find the quarter of VA space not conflicting
	 * with the idmap to place the IOs and the vmemmap. IOs use the lower
	 * half of the quarter and the vmemmap the upper half.
	 */
	__private_range_base = start & BIT(hyp_va_bits - 2);
	__private_range_base ^= BIT(hyp_va_bits - 2);
	__private_range_cur = __private_range_base;
	__hyp_vmemmap = __private_range_base | BIT(hyp_va_bits - 3);

	return __pkvm_create_mappings(start, end - start, start, PAGE_HYP_EXEC);
}

static void *admit_host_page(void *arg)
{
	struct kvm_hyp_memcache *host_mc = arg;

	if (!host_mc->nr_pages)
		return NULL;

	/*
	 * The host still owns the pages in its memcache, so we need to go
	 * through a full host-to-hyp donation cycle to change it. Fortunately,
	 * __pkvm_host_donate_hyp() takes care of races for us, so if it
	 * succeeds we're good to go.
	 */
	if (__pkvm_host_donate_hyp(hyp_phys_to_pfn(host_mc->head), 1))
		return NULL;

	return pop_hyp_memcache(host_mc, hyp_phys_to_virt);
}

/* Refill our local memcache by poping pages from the one provided by the host. */
int refill_memcache(struct kvm_hyp_memcache *mc, unsigned long min_pages,
		    struct kvm_hyp_memcache *host_mc)
{
	struct kvm_hyp_memcache tmp = *host_mc;
	int ret;

	ret =  __topup_hyp_memcache(mc, min_pages, admit_host_page,
				    hyp_virt_to_phys, &tmp);
	*host_mc = tmp;

	return ret;
}
