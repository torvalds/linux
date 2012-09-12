/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * MMU support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
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
	unsigned max_level;
	gfn_t table_gfn[PT_MAX_FULL_LEVELS];
	pt_element_t ptes[PT_MAX_FULL_LEVELS];
	pt_element_t prefetch_ptes[PTE_PREFETCH_NUM];
	gpa_t pte_gpa[PT_MAX_FULL_LEVELS];
	pt_element_t __user *ptep_user[PT_MAX_FULL_LEVELS];
	unsigned pt_access;
	unsigned pte_access;
	gfn_t gfn;
	struct x86_exception fault;
};

static gfn_t gpte_to_gfn_lvl(pt_element_t gpte, int lvl)
{
	return (gpte & PT_LVL_ADDR_MASK(lvl)) >> PAGE_SHIFT;
}

static int FNAME(cmpxchg_gpte)(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			       pt_element_t __user *ptep_user, unsigned index,
			       pt_element_t orig_pte, pt_element_t new_pte)
{
	int npages;
	pt_element_t ret;
	pt_element_t *table;
	struct page *page;

	npages = get_user_pages_fast((unsigned long)ptep_user, 1, 1, &page);
	/* Check if the user is doing something meaningless. */
	if (unlikely(npages != 1))
		return -EFAULT;

	table = kmap_atomic(page);
	ret = CMPXCHG(&table[index], orig_pte, new_pte);
	kunmap_atomic(table);

	kvm_release_page_dirty(page);

	return (ret != orig_pte);
}

static int FNAME(update_accessed_dirty_bits)(struct kvm_vcpu *vcpu,
					     struct kvm_mmu *mmu,
					     struct guest_walker *walker,
					     int write_fault)
{
	unsigned level, index;
	pt_element_t pte, orig_pte;
	pt_element_t __user *ptep_user;
	gfn_t table_gfn;
	int ret;

	for (level = walker->max_level; level >= walker->level; --level) {
		pte = orig_pte = walker->ptes[level - 1];
		table_gfn = walker->table_gfn[level - 1];
		ptep_user = walker->ptep_user[level - 1];
		index = offset_in_page(ptep_user) / sizeof(pt_element_t);
		if (!(pte & PT_ACCESSED_MASK)) {
			trace_kvm_mmu_set_accessed_bit(table_gfn, index, sizeof(pte));
			pte |= PT_ACCESSED_MASK;
		}
		if (level == walker->level && write_fault && !is_dirty_gpte(pte)) {
			trace_kvm_mmu_set_dirty_bit(table_gfn, index, sizeof(pte));
			pte |= PT_DIRTY_MASK;
		}
		if (pte == orig_pte)
			continue;

		ret = FNAME(cmpxchg_gpte)(vcpu, mmu, ptep_user, index, orig_pte, pte);
		if (ret)
			return ret;

		mark_page_dirty(vcpu->kvm, table_gfn);
		walker->ptes[level] = pte;
	}
	return 0;
}

/*
 * Fetch a guest pte for a guest virtual address
 */
static int FNAME(walk_addr_generic)(struct guest_walker *walker,
				    struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				    gva_t addr, u32 access)
{
	int ret;
	pt_element_t pte;
	pt_element_t __user *uninitialized_var(ptep_user);
	gfn_t table_gfn;
	unsigned index, pt_access, pte_access;
	gpa_t pte_gpa;
	bool eperm;
	int offset;
	const int write_fault = access & PFERR_WRITE_MASK;
	const int user_fault  = access & PFERR_USER_MASK;
	const int fetch_fault = access & PFERR_FETCH_MASK;
	u16 errcode = 0;
	gpa_t real_gpa;
	gfn_t gfn;
	u32 ac;

	trace_kvm_mmu_pagetable_walk(addr, access);
retry_walk:
	eperm = false;
	walker->level = mmu->root_level;
	pte           = mmu->get_cr3(vcpu);

#if PTTYPE == 64
	if (walker->level == PT32E_ROOT_LEVEL) {
		pte = mmu->get_pdptr(vcpu, (addr >> 30) & 3);
		trace_kvm_mmu_paging_element(pte, walker->level);
		if (!is_present_gpte(pte))
			goto error;
		--walker->level;
	}
#endif
	walker->max_level = walker->level;
	ASSERT((!is_long_mode(vcpu) && is_pae(vcpu)) ||
	       (mmu->get_cr3(vcpu) & CR3_NONPAE_RESERVED_BITS) == 0);

	pt_access = pte_access = ACC_ALL;
	++walker->level;

	do {
		gfn_t real_gfn;
		unsigned long host_addr;

		pt_access &= pte_access;
		--walker->level;

		index = PT_INDEX(addr, walker->level);

		table_gfn = gpte_to_gfn(pte);
		offset    = index * sizeof(pt_element_t);
		pte_gpa   = gfn_to_gpa(table_gfn) + offset;
		walker->table_gfn[walker->level - 1] = table_gfn;
		walker->pte_gpa[walker->level - 1] = pte_gpa;

		real_gfn = mmu->translate_gpa(vcpu, gfn_to_gpa(table_gfn),
					      PFERR_USER_MASK|PFERR_WRITE_MASK);
		if (unlikely(real_gfn == UNMAPPED_GVA))
			goto error;
		real_gfn = gpa_to_gfn(real_gfn);

		host_addr = gfn_to_hva(vcpu->kvm, real_gfn);
		if (unlikely(kvm_is_error_hva(host_addr)))
			goto error;

		ptep_user = (pt_element_t __user *)((void *)host_addr + offset);
		if (unlikely(__copy_from_user(&pte, ptep_user, sizeof(pte))))
			goto error;
		walker->ptep_user[walker->level - 1] = ptep_user;

		trace_kvm_mmu_paging_element(pte, walker->level);

		if (unlikely(!is_present_gpte(pte)))
			goto error;

		if (unlikely(is_rsvd_bits_set(&vcpu->arch.mmu, pte,
					      walker->level))) {
			errcode |= PFERR_RSVD_MASK | PFERR_PRESENT_MASK;
			goto error;
		}

		pte_access = pt_access & gpte_access(vcpu, pte);

		walker->ptes[walker->level - 1] = pte;
	} while (!is_last_gpte(mmu, walker->level, pte));

	eperm |= permission_fault(mmu, pte_access, access);
	if (unlikely(eperm)) {
		errcode |= PFERR_PRESENT_MASK;
		goto error;
	}

	gfn = gpte_to_gfn_lvl(pte, walker->level);
	gfn += (addr & PT_LVL_OFFSET_MASK(walker->level)) >> PAGE_SHIFT;

	if (PTTYPE == 32 && walker->level == PT_DIRECTORY_LEVEL && is_cpuid_PSE36())
		gfn += pse36_gfn_delta(pte);

	ac = write_fault | fetch_fault | user_fault;

	real_gpa = mmu->translate_gpa(vcpu, gfn_to_gpa(gfn), ac);
	if (real_gpa == UNMAPPED_GVA)
		return 0;

	walker->gfn = real_gpa >> PAGE_SHIFT;

	if (!write_fault)
		protect_clean_gpte(&pte_access, pte);

	ret = FNAME(update_accessed_dirty_bits)(vcpu, mmu, walker, write_fault);
	if (unlikely(ret < 0))
		goto error;
	else if (ret)
		goto retry_walk;

	walker->pt_access = pt_access;
	walker->pte_access = pte_access;
	pgprintk("%s: pte %llx pte_access %x pt_access %x\n",
		 __func__, (u64)pte, pte_access, pt_access);
	return 1;

error:
	errcode |= write_fault | user_fault;
	if (fetch_fault && (mmu->nx ||
			    kvm_read_cr4_bits(vcpu, X86_CR4_SMEP)))
		errcode |= PFERR_FETCH_MASK;

	walker->fault.vector = PF_VECTOR;
	walker->fault.error_code_valid = true;
	walker->fault.error_code = errcode;
	walker->fault.address = addr;
	walker->fault.nested_page_fault = mmu != vcpu->arch.walk_mmu;

	trace_kvm_mmu_walker_error(walker->fault.error_code);
	return 0;
}

static int FNAME(walk_addr)(struct guest_walker *walker,
			    struct kvm_vcpu *vcpu, gva_t addr, u32 access)
{
	return FNAME(walk_addr_generic)(walker, vcpu, &vcpu->arch.mmu, addr,
					access);
}

static int FNAME(walk_addr_nested)(struct guest_walker *walker,
				   struct kvm_vcpu *vcpu, gva_t addr,
				   u32 access)
{
	return FNAME(walk_addr_generic)(walker, vcpu, &vcpu->arch.nested_mmu,
					addr, access);
}

static bool FNAME(prefetch_invalid_gpte)(struct kvm_vcpu *vcpu,
				    struct kvm_mmu_page *sp, u64 *spte,
				    pt_element_t gpte)
{
	if (is_rsvd_bits_set(&vcpu->arch.mmu, gpte, PT_PAGE_TABLE_LEVEL))
		goto no_present;

	if (!is_present_gpte(gpte))
		goto no_present;

	if (!(gpte & PT_ACCESSED_MASK))
		goto no_present;

	return false;

no_present:
	drop_spte(vcpu->kvm, spte);
	return true;
}

static void FNAME(update_pte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			      u64 *spte, const void *pte)
{
	pt_element_t gpte;
	unsigned pte_access;
	pfn_t pfn;

	gpte = *(const pt_element_t *)pte;
	if (FNAME(prefetch_invalid_gpte)(vcpu, sp, spte, gpte))
		return;

	pgprintk("%s: gpte %llx spte %p\n", __func__, (u64)gpte, spte);
	pte_access = sp->role.access & gpte_access(vcpu, gpte);
	protect_clean_gpte(&pte_access, gpte);
	pfn = gfn_to_pfn_atomic(vcpu->kvm, gpte_to_gfn(gpte));
	if (mmu_invalid_pfn(pfn))
		return;

	/*
	 * we call mmu_set_spte() with host_writable = true because that
	 * vcpu->arch.update_pte.pfn was fetched from get_user_pages(write = 1).
	 */
	mmu_set_spte(vcpu, spte, sp->role.access, pte_access, 0, 0,
		     NULL, PT_PAGE_TABLE_LEVEL,
		     gpte_to_gfn(gpte), pfn, true, true);
}

static bool FNAME(gpte_changed)(struct kvm_vcpu *vcpu,
				struct guest_walker *gw, int level)
{
	pt_element_t curr_pte;
	gpa_t base_gpa, pte_gpa = gw->pte_gpa[level - 1];
	u64 mask;
	int r, index;

	if (level == PT_PAGE_TABLE_LEVEL) {
		mask = PTE_PREFETCH_NUM * sizeof(pt_element_t) - 1;
		base_gpa = pte_gpa & ~mask;
		index = (pte_gpa - base_gpa) / sizeof(pt_element_t);

		r = kvm_read_guest_atomic(vcpu->kvm, base_gpa,
				gw->prefetch_ptes, sizeof(gw->prefetch_ptes));
		curr_pte = gw->prefetch_ptes[index];
	} else
		r = kvm_read_guest_atomic(vcpu->kvm, pte_gpa,
				  &curr_pte, sizeof(curr_pte));

	return r || curr_pte != gw->ptes[level - 1];
}

static void FNAME(pte_prefetch)(struct kvm_vcpu *vcpu, struct guest_walker *gw,
				u64 *sptep)
{
	struct kvm_mmu_page *sp;
	pt_element_t *gptep = gw->prefetch_ptes;
	u64 *spte;
	int i;

	sp = page_header(__pa(sptep));

	if (sp->role.level > PT_PAGE_TABLE_LEVEL)
		return;

	if (sp->role.direct)
		return __direct_pte_prefetch(vcpu, sp, sptep);

	i = (sptep - sp->spt) & ~(PTE_PREFETCH_NUM - 1);
	spte = sp->spt + i;

	for (i = 0; i < PTE_PREFETCH_NUM; i++, spte++) {
		pt_element_t gpte;
		unsigned pte_access;
		gfn_t gfn;
		pfn_t pfn;

		if (spte == sptep)
			continue;

		if (is_shadow_present_pte(*spte))
			continue;

		gpte = gptep[i];

		if (FNAME(prefetch_invalid_gpte)(vcpu, sp, spte, gpte))
			continue;

		pte_access = sp->role.access & gpte_access(vcpu, gpte);
		protect_clean_gpte(&pte_access, gpte);
		gfn = gpte_to_gfn(gpte);
		pfn = pte_prefetch_gfn_to_pfn(vcpu, gfn,
				      pte_access & ACC_WRITE_MASK);
		if (mmu_invalid_pfn(pfn))
			break;

		mmu_set_spte(vcpu, spte, sp->role.access, pte_access, 0, 0,
			     NULL, PT_PAGE_TABLE_LEVEL, gfn,
			     pfn, true, true);
	}
}

/*
 * Fetch a shadow pte for a specific level in the paging hierarchy.
 */
static u64 *FNAME(fetch)(struct kvm_vcpu *vcpu, gva_t addr,
			 struct guest_walker *gw,
			 int user_fault, int write_fault, int hlevel,
			 int *emulate, pfn_t pfn, bool map_writable,
			 bool prefault)
{
	unsigned access = gw->pt_access;
	struct kvm_mmu_page *sp = NULL;
	int top_level;
	unsigned direct_access;
	struct kvm_shadow_walk_iterator it;

	if (!is_present_gpte(gw->ptes[gw->level - 1]))
		return NULL;

	direct_access = gw->pte_access;

	top_level = vcpu->arch.mmu.root_level;
	if (top_level == PT32E_ROOT_LEVEL)
		top_level = PT32_ROOT_LEVEL;
	/*
	 * Verify that the top-level gpte is still there.  Since the page
	 * is a root page, it is either write protected (and cannot be
	 * changed from now on) or it is invalid (in which case, we don't
	 * really care if it changes underneath us after this point).
	 */
	if (FNAME(gpte_changed)(vcpu, gw, top_level))
		goto out_gpte_changed;

	for (shadow_walk_init(&it, vcpu, addr);
	     shadow_walk_okay(&it) && it.level > gw->level;
	     shadow_walk_next(&it)) {
		gfn_t table_gfn;

		clear_sp_write_flooding_count(it.sptep);
		drop_large_spte(vcpu, it.sptep);

		sp = NULL;
		if (!is_shadow_present_pte(*it.sptep)) {
			table_gfn = gw->table_gfn[it.level - 2];
			sp = kvm_mmu_get_page(vcpu, table_gfn, addr, it.level-1,
					      false, access, it.sptep);
		}

		/*
		 * Verify that the gpte in the page we've just write
		 * protected is still there.
		 */
		if (FNAME(gpte_changed)(vcpu, gw, it.level - 1))
			goto out_gpte_changed;

		if (sp)
			link_shadow_page(it.sptep, sp);
	}

	for (;
	     shadow_walk_okay(&it) && it.level > hlevel;
	     shadow_walk_next(&it)) {
		gfn_t direct_gfn;

		clear_sp_write_flooding_count(it.sptep);
		validate_direct_spte(vcpu, it.sptep, direct_access);

		drop_large_spte(vcpu, it.sptep);

		if (is_shadow_present_pte(*it.sptep))
			continue;

		direct_gfn = gw->gfn & ~(KVM_PAGES_PER_HPAGE(it.level) - 1);

		sp = kvm_mmu_get_page(vcpu, direct_gfn, addr, it.level-1,
				      true, direct_access, it.sptep);
		link_shadow_page(it.sptep, sp);
	}

	clear_sp_write_flooding_count(it.sptep);
	mmu_set_spte(vcpu, it.sptep, access, gw->pte_access,
		     user_fault, write_fault, emulate, it.level,
		     gw->gfn, pfn, prefault, map_writable);
	FNAME(pte_prefetch)(vcpu, gw, it.sptep);

	return it.sptep;

out_gpte_changed:
	if (sp)
		kvm_mmu_put_page(sp, it.sptep);
	kvm_release_pfn_clean(pfn);
	return NULL;
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
static int FNAME(page_fault)(struct kvm_vcpu *vcpu, gva_t addr, u32 error_code,
			     bool prefault)
{
	int write_fault = error_code & PFERR_WRITE_MASK;
	int user_fault = error_code & PFERR_USER_MASK;
	struct guest_walker walker;
	u64 *sptep;
	int emulate = 0;
	int r;
	pfn_t pfn;
	int level = PT_PAGE_TABLE_LEVEL;
	int force_pt_level;
	unsigned long mmu_seq;
	bool map_writable;

	pgprintk("%s: addr %lx err %x\n", __func__, addr, error_code);

	if (unlikely(error_code & PFERR_RSVD_MASK))
		return handle_mmio_page_fault(vcpu, addr, error_code,
					      mmu_is_nested(vcpu));

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	/*
	 * Look up the guest pte for the faulting address.
	 */
	r = FNAME(walk_addr)(&walker, vcpu, addr, error_code);

	/*
	 * The page is not mapped by the guest.  Let the guest handle it.
	 */
	if (!r) {
		pgprintk("%s: guest page fault\n", __func__);
		if (!prefault)
			inject_page_fault(vcpu, &walker.fault);

		return 0;
	}

	if (walker.level >= PT_DIRECTORY_LEVEL)
		force_pt_level = mapping_level_dirty_bitmap(vcpu, walker.gfn);
	else
		force_pt_level = 1;
	if (!force_pt_level) {
		level = min(walker.level, mapping_level(vcpu, walker.gfn));
		walker.gfn = walker.gfn & ~(KVM_PAGES_PER_HPAGE(level) - 1);
	}

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (try_async_pf(vcpu, prefault, walker.gfn, addr, &pfn, write_fault,
			 &map_writable))
		return 0;

	if (handle_abnormal_pfn(vcpu, mmu_is_nested(vcpu) ? 0 : addr,
				walker.gfn, pfn, walker.pte_access, &r))
		return r;

	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu, mmu_seq))
		goto out_unlock;

	kvm_mmu_audit(vcpu, AUDIT_PRE_PAGE_FAULT);
	kvm_mmu_free_some_pages(vcpu);
	if (!force_pt_level)
		transparent_hugepage_adjust(vcpu, &walker.gfn, &pfn, &level);
	sptep = FNAME(fetch)(vcpu, addr, &walker, user_fault, write_fault,
			     level, &emulate, pfn, map_writable, prefault);
	(void)sptep;
	pgprintk("%s: shadow pte %p %llx emulate %d\n", __func__,
		 sptep, *sptep, emulate);

	++vcpu->stat.pf_fixed;
	kvm_mmu_audit(vcpu, AUDIT_POST_PAGE_FAULT);
	spin_unlock(&vcpu->kvm->mmu_lock);

	return emulate;

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return 0;
}

static gpa_t FNAME(get_level1_sp_gpa)(struct kvm_mmu_page *sp)
{
	int offset = 0;

	WARN_ON(sp->role.level != PT_PAGE_TABLE_LEVEL);

	if (PTTYPE == 32)
		offset = sp->role.quadrant << PT64_LEVEL_BITS;

	return gfn_to_gpa(sp->gfn) + offset * sizeof(pt_element_t);
}

static void FNAME(invlpg)(struct kvm_vcpu *vcpu, gva_t gva)
{
	struct kvm_shadow_walk_iterator iterator;
	struct kvm_mmu_page *sp;
	int level;
	u64 *sptep;

	vcpu_clear_mmio_info(vcpu, gva);

	/*
	 * No need to check return value here, rmap_can_add() can
	 * help us to skip pte prefetch later.
	 */
	mmu_topup_memory_caches(vcpu);

	spin_lock(&vcpu->kvm->mmu_lock);
	for_each_shadow_entry(vcpu, gva, iterator) {
		level = iterator.level;
		sptep = iterator.sptep;

		sp = page_header(__pa(sptep));
		if (is_last_spte(*sptep, level)) {
			pt_element_t gpte;
			gpa_t pte_gpa;

			if (!sp->unsync)
				break;

			pte_gpa = FNAME(get_level1_sp_gpa)(sp);
			pte_gpa += (sptep - sp->spt) * sizeof(pt_element_t);

			if (mmu_page_zap_pte(vcpu->kvm, sp, sptep))
				kvm_flush_remote_tlbs(vcpu->kvm);

			if (!rmap_can_add(vcpu))
				break;

			if (kvm_read_guest_atomic(vcpu->kvm, pte_gpa, &gpte,
						  sizeof(pt_element_t)))
				break;

			FNAME(update_pte)(vcpu, sp, sptep, &gpte);
		}

		if (!is_shadow_present_pte(*sptep) || !sp->unsync_children)
			break;
	}
	spin_unlock(&vcpu->kvm->mmu_lock);
}

static gpa_t FNAME(gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t vaddr, u32 access,
			       struct x86_exception *exception)
{
	struct guest_walker walker;
	gpa_t gpa = UNMAPPED_GVA;
	int r;

	r = FNAME(walk_addr)(&walker, vcpu, vaddr, access);

	if (r) {
		gpa = gfn_to_gpa(walker.gfn);
		gpa |= vaddr & ~PAGE_MASK;
	} else if (exception)
		*exception = walker.fault;

	return gpa;
}

static gpa_t FNAME(gva_to_gpa_nested)(struct kvm_vcpu *vcpu, gva_t vaddr,
				      u32 access,
				      struct x86_exception *exception)
{
	struct guest_walker walker;
	gpa_t gpa = UNMAPPED_GVA;
	int r;

	r = FNAME(walk_addr_nested)(&walker, vcpu, vaddr, access);

	if (r) {
		gpa = gfn_to_gpa(walker.gfn);
		gpa |= vaddr & ~PAGE_MASK;
	} else if (exception)
		*exception = walker.fault;

	return gpa;
}

/*
 * Using the cached information from sp->gfns is safe because:
 * - The spte has a reference to the struct page, so the pfn for a given gfn
 *   can't change unless all sptes pointing to it are nuked first.
 *
 * Note:
 *   We should flush all tlbs if spte is dropped even though guest is
 *   responsible for it. Since if we don't, kvm_mmu_notifier_invalidate_page
 *   and kvm_mmu_notifier_invalidate_range_start detect the mapping page isn't
 *   used by guest then tlbs are not flushed, so guest is allowed to access the
 *   freed pages.
 *   And we increase kvm->tlbs_dirty to delay tlbs flush in this case.
 */
static int FNAME(sync_page)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	int i, nr_present = 0;
	bool host_writable;
	gpa_t first_pte_gpa;

	/* direct kvm_mmu_page can not be unsync. */
	BUG_ON(sp->role.direct);

	first_pte_gpa = FNAME(get_level1_sp_gpa)(sp);

	for (i = 0; i < PT64_ENT_PER_PAGE; i++) {
		unsigned pte_access;
		pt_element_t gpte;
		gpa_t pte_gpa;
		gfn_t gfn;

		if (!sp->spt[i])
			continue;

		pte_gpa = first_pte_gpa + i * sizeof(pt_element_t);

		if (kvm_read_guest_atomic(vcpu->kvm, pte_gpa, &gpte,
					  sizeof(pt_element_t)))
			return -EINVAL;

		if (FNAME(prefetch_invalid_gpte)(vcpu, sp, &sp->spt[i], gpte)) {
			vcpu->kvm->tlbs_dirty++;
			continue;
		}

		gfn = gpte_to_gfn(gpte);
		pte_access = sp->role.access;
		pte_access &= gpte_access(vcpu, gpte);
		protect_clean_gpte(&pte_access, gpte);

		if (sync_mmio_spte(&sp->spt[i], gfn, pte_access, &nr_present))
			continue;

		if (gfn != sp->gfns[i]) {
			drop_spte(vcpu->kvm, &sp->spt[i]);
			vcpu->kvm->tlbs_dirty++;
			continue;
		}

		nr_present++;

		host_writable = sp->spt[i] & SPTE_HOST_WRITEABLE;

		set_spte(vcpu, &sp->spt[i], pte_access, 0, 0,
			 PT_PAGE_TABLE_LEVEL, gfn,
			 spte_to_pfn(sp->spt[i]), true, false,
			 host_writable);
	}

	return !nr_present;
}

#undef pt_element_t
#undef guest_walker
#undef FNAME
#undef PT_BASE_ADDR_MASK
#undef PT_INDEX
#undef PT_LVL_ADDR_MASK
#undef PT_LVL_OFFSET_MASK
#undef PT_LEVEL_BITS
#undef PT_MAX_FULL_LEVELS
#undef gpte_to_gfn
#undef gpte_to_gfn_lvl
#undef CMPXCHG
