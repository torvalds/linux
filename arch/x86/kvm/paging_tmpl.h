/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * MMU support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

/*
 * We need the mmu code to access both 32-bit and 64-bit guest ptes,
 * so the code in this file is compiled twice, once per pte size.
 */

#if PTTYPE == 64
	#define pt_element_t u64
	#define guest_walker guest_walker64
	#define FNAME(name) paging##64_##name
	#define PT_BASE_ADDR_MASK PT64_BASE_ADDR_MASK
	#define PT_LVL_ADDR_MASK(lvl) PT64_LVL_ADDR_MASK(lvl)
	#define PT_LVL_OFFSET_MASK(lvl) PT64_LVL_OFFSET_MASK(lvl)
	#define PT_INDEX(addr, level) PT64_INDEX(addr, level)
	#define PT_LEVEL_MASK(level) PT64_LEVEL_MASK(level)
	#define PT_LEVEL_BITS PT64_LEVEL_BITS
	#ifdef CONFIG_X86_64
	#define PT_MAX_FULL_LEVELS 4
	#define CMPXCHG cmpxchg
	#else
	#define CMPXCHG cmpxchg64
	#define PT_MAX_FULL_LEVELS 2
	#endif
#elif PTTYPE == 32
	#define pt_element_t u32
	#define guest_walker guest_walker32
	#define FNAME(name) paging##32_##name
	#define PT_BASE_ADDR_MASK PT32_BASE_ADDR_MASK
	#define PT_LVL_ADDR_MASK(lvl) PT32_LVL_ADDR_MASK(lvl)
	#define PT_LVL_OFFSET_MASK(lvl) PT32_LVL_OFFSET_MASK(lvl)
	#define PT_INDEX(addr, level) PT32_INDEX(addr, level)
	#define PT_LEVEL_MASK(level) PT32_LEVEL_MASK(level)
	#define PT_LEVEL_BITS PT32_LEVEL_BITS
	#define PT_MAX_FULL_LEVELS 2
	#define CMPXCHG cmpxchg
#else
	#error Invalid PTTYPE value
#endif

#define gpte_to_gfn_lvl FNAME(gpte_to_gfn_lvl)
#define gpte_to_gfn(pte) gpte_to_gfn_lvl((pte), PT_PAGE_TABLE_LEVEL)

/*
 * The guest_walker structure emulates the behavior of the hardware page
 * table walker.
 */
struct guest_walker {
	int level;
	gfn_t table_gfn[PT_MAX_FULL_LEVELS];
	pt_element_t ptes[PT_MAX_FULL_LEVELS];
	gpa_t pte_gpa[PT_MAX_FULL_LEVELS];
	unsigned pt_access;
	unsigned pte_access;
	gfn_t gfn;
	u32 error_code;
};

static gfn_t gpte_to_gfn_lvl(pt_element_t gpte, int lvl)
{
	return (gpte & PT_LVL_ADDR_MASK(lvl)) >> PAGE_SHIFT;
}

static bool FNAME(cmpxchg_gpte)(struct kvm *kvm,
			 gfn_t table_gfn, unsigned index,
			 pt_element_t orig_pte, pt_element_t new_pte)
{
	pt_element_t ret;
	pt_element_t *table;
	struct page *page;

	page = gfn_to_page(kvm, table_gfn);

	table = kmap_atomic(page, KM_USER0);
	ret = CMPXCHG(&table[index], orig_pte, new_pte);
	kunmap_atomic(table, KM_USER0);

	kvm_release_page_dirty(page);

	return (ret != orig_pte);
}

static unsigned FNAME(gpte_access)(struct kvm_vcpu *vcpu, pt_element_t gpte)
{
	unsigned access;

	access = (gpte & (PT_WRITABLE_MASK | PT_USER_MASK)) | ACC_EXEC_MASK;
#if PTTYPE == 64
	if (is_nx(vcpu))
		access &= ~(gpte >> PT64_NX_SHIFT);
#endif
	return access;
}

/*
 * Fetch a guest pte for a guest virtual address
 */
static int FNAME(walk_addr)(struct guest_walker *walker,
			    struct kvm_vcpu *vcpu, gva_t addr,
			    int write_fault, int user_fault, int fetch_fault)
{
	pt_element_t pte;
	gfn_t table_gfn;
	unsigned index, pt_access, pte_access;
	gpa_t pte_gpa;
	int rsvd_fault = 0;

	trace_kvm_mmu_pagetable_walk(addr, write_fault, user_fault,
				     fetch_fault);
walk:
	walker->level = vcpu->arch.mmu.root_level;
	pte = vcpu->arch.cr3;
#if PTTYPE == 64
	if (!is_long_mode(vcpu)) {
		pte = kvm_pdptr_read(vcpu, (addr >> 30) & 3);
		trace_kvm_mmu_paging_element(pte, walker->level);
		if (!is_present_gpte(pte))
			goto not_present;
		--walker->level;
	}
#endif
	ASSERT((!is_long_mode(vcpu) && is_pae(vcpu)) ||
	       (vcpu->arch.cr3 & CR3_NONPAE_RESERVED_BITS) == 0);

	pt_access = ACC_ALL;

	for (;;) {
		index = PT_INDEX(addr, walker->level);

		table_gfn = gpte_to_gfn(pte);
		pte_gpa = gfn_to_gpa(table_gfn);
		pte_gpa += index * sizeof(pt_element_t);
		walker->table_gfn[walker->level - 1] = table_gfn;
		walker->pte_gpa[walker->level - 1] = pte_gpa;

		if (kvm_read_guest(vcpu->kvm, pte_gpa, &pte, sizeof(pte)))
			goto not_present;

		trace_kvm_mmu_paging_element(pte, walker->level);

		if (!is_present_gpte(pte))
			goto not_present;

		rsvd_fault = is_rsvd_bits_set(vcpu, pte, walker->level);
		if (rsvd_fault)
			goto access_error;

		if (write_fault && !is_writeble_pte(pte))
			if (user_fault || is_write_protection(vcpu))
				goto access_error;

		if (user_fault && !(pte & PT_USER_MASK))
			goto access_error;

#if PTTYPE == 64
		if (fetch_fault && is_nx(vcpu) && (pte & PT64_NX_MASK))
			goto access_error;
#endif

		if (!(pte & PT_ACCESSED_MASK)) {
			trace_kvm_mmu_set_accessed_bit(table_gfn, index,
						       sizeof(pte));
			mark_page_dirty(vcpu->kvm, table_gfn);
			if (FNAME(cmpxchg_gpte)(vcpu->kvm, table_gfn,
			    index, pte, pte|PT_ACCESSED_MASK))
				goto walk;
			pte |= PT_ACCESSED_MASK;
		}

		pte_access = pt_access & FNAME(gpte_access)(vcpu, pte);

		walker->ptes[walker->level - 1] = pte;

		if ((walker->level == PT_PAGE_TABLE_LEVEL) ||
		    ((walker->level == PT_DIRECTORY_LEVEL) &&
				(pte & PT_PAGE_SIZE_MASK)  &&
				(PTTYPE == 64 || is_pse(vcpu))) ||
		    ((walker->level == PT_PDPE_LEVEL) &&
				(pte & PT_PAGE_SIZE_MASK)  &&
				is_long_mode(vcpu))) {
			int lvl = walker->level;

			walker->gfn = gpte_to_gfn_lvl(pte, lvl);
			walker->gfn += (addr & PT_LVL_OFFSET_MASK(lvl))
					>> PAGE_SHIFT;

			if (PTTYPE == 32 &&
			    walker->level == PT_DIRECTORY_LEVEL &&
			    is_cpuid_PSE36())
				walker->gfn += pse36_gfn_delta(pte);

			break;
		}

		pt_access = pte_access;
		--walker->level;
	}

	if (write_fault && !is_dirty_gpte(pte)) {
		bool ret;

		trace_kvm_mmu_set_dirty_bit(table_gfn, index, sizeof(pte));
		mark_page_dirty(vcpu->kvm, table_gfn);
		ret = FNAME(cmpxchg_gpte)(vcpu->kvm, table_gfn, index, pte,
			    pte|PT_DIRTY_MASK);
		if (ret)
			goto walk;
		pte |= PT_DIRTY_MASK;
		walker->ptes[walker->level - 1] = pte;
	}

	walker->pt_access = pt_access;
	walker->pte_access = pte_access;
	pgprintk("%s: pte %llx pte_access %x pt_access %x\n",
		 __func__, (u64)pte, pt_access, pte_access);
	return 1;

not_present:
	walker->error_code = 0;
	goto err;

access_error:
	walker->error_code = PFERR_PRESENT_MASK;

err:
	if (write_fault)
		walker->error_code |= PFERR_WRITE_MASK;
	if (user_fault)
		walker->error_code |= PFERR_USER_MASK;
	if (fetch_fault)
		walker->error_code |= PFERR_FETCH_MASK;
	if (rsvd_fault)
		walker->error_code |= PFERR_RSVD_MASK;
	trace_kvm_mmu_walker_error(walker->error_code);
	return 0;
}

static void FNAME(update_pte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *page,
			      u64 *spte, const void *pte)
{
	pt_element_t gpte;
	unsigned pte_access;
	pfn_t pfn;

	gpte = *(const pt_element_t *)pte;
	if (~gpte & (PT_PRESENT_MASK | PT_ACCESSED_MASK)) {
		if (!is_present_gpte(gpte))
			__set_spte(spte, shadow_notrap_nonpresent_pte);
		return;
	}
	pgprintk("%s: gpte %llx spte %p\n", __func__, (u64)gpte, spte);
	pte_access = page->role.access & FNAME(gpte_access)(vcpu, gpte);
	if (gpte_to_gfn(gpte) != vcpu->arch.update_pte.gfn)
		return;
	pfn = vcpu->arch.update_pte.pfn;
	if (is_error_pfn(pfn))
		return;
	if (mmu_notifier_retry(vcpu, vcpu->arch.update_pte.mmu_seq))
		return;
	kvm_get_pfn(pfn);
	/*
	 * we call mmu_set_spte() with reset_host_protection = true beacuse that
	 * vcpu->arch.update_pte.pfn was fetched from get_user_pages(write = 1).
	 */
	mmu_set_spte(vcpu, spte, page->role.access, pte_access, 0, 0,
		     gpte & PT_DIRTY_MASK, NULL, PT_PAGE_TABLE_LEVEL,
		     gpte_to_gfn(gpte), pfn, true, true);
}

/*
 * Fetch a shadow pte for a specific level in the paging hierarchy.
 */
static u64 *FNAME(fetch)(struct kvm_vcpu *vcpu, gva_t addr,
			 struct guest_walker *gw,
			 int user_fault, int write_fault, int hlevel,
			 int *ptwrite, pfn_t pfn)
{
	unsigned access = gw->pt_access;
	struct kvm_mmu_page *shadow_page;
	u64 spte, *sptep = NULL;
	int direct;
	gfn_t table_gfn;
	int r;
	int level;
	pt_element_t curr_pte;
	struct kvm_shadow_walk_iterator iterator;

	if (!is_present_gpte(gw->ptes[gw->level - 1]))
		return NULL;

	for_each_shadow_entry(vcpu, addr, iterator) {
		level = iterator.level;
		sptep = iterator.sptep;
		if (iterator.level == hlevel) {
			mmu_set_spte(vcpu, sptep, access,
				     gw->pte_access & access,
				     user_fault, write_fault,
				     gw->ptes[gw->level-1] & PT_DIRTY_MASK,
				     ptwrite, level,
				     gw->gfn, pfn, false, true);
			break;
		}

		if (is_shadow_present_pte(*sptep) && !is_large_pte(*sptep))
			continue;

		if (is_large_pte(*sptep)) {
			rmap_remove(vcpu->kvm, sptep);
			__set_spte(sptep, shadow_trap_nonpresent_pte);
			kvm_flush_remote_tlbs(vcpu->kvm);
		}

		if (level <= gw->level) {
			int delta = level - gw->level + 1;
			direct = 1;
			if (!is_dirty_gpte(gw->ptes[level - delta]))
				access &= ~ACC_WRITE_MASK;
			table_gfn = gpte_to_gfn(gw->ptes[level - delta]);
			/* advance table_gfn when emulating 1gb pages with 4k */
			if (delta == 0)
				table_gfn += PT_INDEX(addr, level);
		} else {
			direct = 0;
			table_gfn = gw->table_gfn[level - 2];
		}
		shadow_page = kvm_mmu_get_page(vcpu, table_gfn, addr, level-1,
					       direct, access, sptep);
		if (!direct) {
			r = kvm_read_guest_atomic(vcpu->kvm,
						  gw->pte_gpa[level - 2],
						  &curr_pte, sizeof(curr_pte));
			if (r || curr_pte != gw->ptes[level - 2]) {
				kvm_mmu_put_page(shadow_page, sptep);
				kvm_release_pfn_clean(pfn);
				sptep = NULL;
				break;
			}
		}

		spte = __pa(shadow_page->spt)
			| PT_PRESENT_MASK | PT_ACCESSED_MASK
			| PT_WRITABLE_MASK | PT_USER_MASK;
		*sptep = spte;
	}

	return sptep;
}

/*
 * Page fault handler.  There are several causes for a page fault:
 *   - there is no shadow pte for the guest pte
 *   - write access through a shadow pte marked read only so that we can set
 *     the dirty bit
 *   - write access to a shadow pte marked read only so we can update the page
 *     dirty bitmap, when userspace requests it
 *   - mmio access; in this case we will never install a present shadow pte
 *   - normal guest page fault due to the guest pte marked not present, not
 *     writable, or not executable
 *
 *  Returns: 1 if we need to emulate the instruction, 0 otherwise, or
 *           a negative value on error.
 */
static int FNAME(page_fault)(struct kvm_vcpu *vcpu, gva_t addr,
			       u32 error_code)
{
	int write_fault = error_code & PFERR_WRITE_MASK;
	int user_fault = error_code & PFERR_USER_MASK;
	int fetch_fault = error_code & PFERR_FETCH_MASK;
	struct guest_walker walker;
	u64 *sptep;
	int write_pt = 0;
	int r;
	pfn_t pfn;
	int level = PT_PAGE_TABLE_LEVEL;
	unsigned long mmu_seq;

	pgprintk("%s: addr %lx err %x\n", __func__, addr, error_code);
	kvm_mmu_audit(vcpu, "pre page fault");

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	/*
	 * Look up the guest pte for the faulting address.
	 */
	r = FNAME(walk_addr)(&walker, vcpu, addr, write_fault, user_fault,
			     fetch_fault);

	/*
	 * The page is not mapped by the guest.  Let the guest handle it.
	 */
	if (!r) {
		pgprintk("%s: guest page fault\n", __func__);
		inject_page_fault(vcpu, addr, walker.error_code);
		vcpu->arch.last_pt_write_count = 0; /* reset fork detector */
		return 0;
	}

	if (walker.level >= PT_DIRECTORY_LEVEL) {
		level = min(walker.level, mapping_level(vcpu, walker.gfn));
		walker.gfn = walker.gfn & ~(KVM_PAGES_PER_HPAGE(level) - 1);
	}

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();
	pfn = gfn_to_pfn(vcpu->kvm, walker.gfn);

	/* mmio */
	if (is_error_pfn(pfn)) {
		pgprintk("gfn %lx is mmio\n", walker.gfn);
		kvm_release_pfn_clean(pfn);
		return 1;
	}

	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu, mmu_seq))
		goto out_unlock;
	kvm_mmu_free_some_pages(vcpu);
	sptep = FNAME(fetch)(vcpu, addr, &walker, user_fault, write_fault,
			     level, &write_pt, pfn);
	pgprintk("%s: shadow pte %p %llx ptwrite %d\n", __func__,
		 sptep, *sptep, write_pt);

	if (!write_pt)
		vcpu->arch.last_pt_write_count = 0; /* reset fork detector */

	++vcpu->stat.pf_fixed;
	kvm_mmu_audit(vcpu, "post page fault (fixed)");
	spin_unlock(&vcpu->kvm->mmu_lock);

	return write_pt;

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return 0;
}

static void FNAME(invlpg)(struct kvm_vcpu *vcpu, gva_t gva)
{
	struct kvm_shadow_walk_iterator iterator;
	int level;
	u64 *sptep;
	int need_flush = 0;

	spin_lock(&vcpu->kvm->mmu_lock);

	for_each_shadow_entry(vcpu, gva, iterator) {
		level = iterator.level;
		sptep = iterator.sptep;

		if (level == PT_PAGE_TABLE_LEVEL  ||
		    ((level == PT_DIRECTORY_LEVEL && is_large_pte(*sptep))) ||
		    ((level == PT_PDPE_LEVEL && is_large_pte(*sptep)))) {

			if (is_shadow_present_pte(*sptep)) {
				rmap_remove(vcpu->kvm, sptep);
				if (is_large_pte(*sptep))
					--vcpu->kvm->stat.lpages;
				need_flush = 1;
			}
			__set_spte(sptep, shadow_trap_nonpresent_pte);
			break;
		}

		if (!is_shadow_present_pte(*sptep))
			break;
	}

	if (need_flush)
		kvm_flush_remote_tlbs(vcpu->kvm);
	spin_unlock(&vcpu->kvm->mmu_lock);
}

static gpa_t FNAME(gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t vaddr)
{
	struct guest_walker walker;
	gpa_t gpa = UNMAPPED_GVA;
	int r;

	r = FNAME(walk_addr)(&walker, vcpu, vaddr, 0, 0, 0);

	if (r) {
		gpa = gfn_to_gpa(walker.gfn);
		gpa |= vaddr & ~PAGE_MASK;
	}

	return gpa;
}

static void FNAME(prefetch_page)(struct kvm_vcpu *vcpu,
				 struct kvm_mmu_page *sp)
{
	int i, j, offset, r;
	pt_element_t pt[256 / sizeof(pt_element_t)];
	gpa_t pte_gpa;

	if (sp->role.direct
	    || (PTTYPE == 32 && sp->role.level > PT_PAGE_TABLE_LEVEL)) {
		nonpaging_prefetch_page(vcpu, sp);
		return;
	}

	pte_gpa = gfn_to_gpa(sp->gfn);
	if (PTTYPE == 32) {
		offset = sp->role.quadrant << PT64_LEVEL_BITS;
		pte_gpa += offset * sizeof(pt_element_t);
	}

	for (i = 0; i < PT64_ENT_PER_PAGE; i += ARRAY_SIZE(pt)) {
		r = kvm_read_guest_atomic(vcpu->kvm, pte_gpa, pt, sizeof pt);
		pte_gpa += ARRAY_SIZE(pt) * sizeof(pt_element_t);
		for (j = 0; j < ARRAY_SIZE(pt); ++j)
			if (r || is_present_gpte(pt[j]))
				sp->spt[i+j] = shadow_trap_nonpresent_pte;
			else
				sp->spt[i+j] = shadow_notrap_nonpresent_pte;
	}
}

/*
 * Using the cached information from sp->gfns is safe because:
 * - The spte has a reference to the struct page, so the pfn for a given gfn
 *   can't change unless all sptes pointing to it are nuked first.
 * - Alias changes zap the entire shadow cache.
 */
static int FNAME(sync_page)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	int i, offset, nr_present;
	bool reset_host_protection;

	offset = nr_present = 0;

	if (PTTYPE == 32)
		offset = sp->role.quadrant << PT64_LEVEL_BITS;

	for (i = 0; i < PT64_ENT_PER_PAGE; i++) {
		unsigned pte_access;
		pt_element_t gpte;
		gpa_t pte_gpa;
		gfn_t gfn = sp->gfns[i];

		if (!is_shadow_present_pte(sp->spt[i]))
			continue;

		pte_gpa = gfn_to_gpa(sp->gfn);
		pte_gpa += (i+offset) * sizeof(pt_element_t);

		if (kvm_read_guest_atomic(vcpu->kvm, pte_gpa, &gpte,
					  sizeof(pt_element_t)))
			return -EINVAL;

		if (gpte_to_gfn(gpte) != gfn || !is_present_gpte(gpte) ||
		    !(gpte & PT_ACCESSED_MASK)) {
			u64 nonpresent;

			rmap_remove(vcpu->kvm, &sp->spt[i]);
			if (is_present_gpte(gpte))
				nonpresent = shadow_trap_nonpresent_pte;
			else
				nonpresent = shadow_notrap_nonpresent_pte;
			__set_spte(&sp->spt[i], nonpresent);
			continue;
		}

		nr_present++;
		pte_access = sp->role.access & FNAME(gpte_access)(vcpu, gpte);
		if (!(sp->spt[i] & SPTE_HOST_WRITEABLE)) {
			pte_access &= ~ACC_WRITE_MASK;
			reset_host_protection = 0;
		} else {
			reset_host_protection = 1;
		}
		set_spte(vcpu, &sp->spt[i], pte_access, 0, 0,
			 is_dirty_gpte(gpte), PT_PAGE_TABLE_LEVEL, gfn,
			 spte_to_pfn(sp->spt[i]), true, false,
			 reset_host_protection);
	}

	return !nr_present;
}

#undef pt_element_t
#undef guest_walker
#undef FNAME
#undef PT_BASE_ADDR_MASK
#undef PT_INDEX
#undef PT_LEVEL_MASK
#undef PT_LVL_ADDR_MASK
#undef PT_LVL_OFFSET_MASK
#undef PT_LEVEL_BITS
#undef PT_MAX_FULL_LEVELS
#undef gpte_to_gfn
#undef gpte_to_gfn_lvl
#undef CMPXCHG
