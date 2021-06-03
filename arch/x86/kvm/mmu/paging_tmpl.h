/* SPDX-License-Identifier: GPL-2.0-only */
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
	#define PT_GUEST_DIRTY_SHIFT PT_DIRTY_SHIFT
	#define PT_GUEST_ACCESSED_SHIFT PT_ACCESSED_SHIFT
	#define PT_HAVE_ACCESSED_DIRTY(mmu) true
	#ifdef CONFIG_X86_64
	#define PT_MAX_FULL_LEVELS PT64_ROOT_MAX_LEVEL
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
	#define PT_GUEST_DIRTY_SHIFT PT_DIRTY_SHIFT
	#define PT_GUEST_ACCESSED_SHIFT PT_ACCESSED_SHIFT
	#define PT_HAVE_ACCESSED_DIRTY(mmu) true
	#define CMPXCHG cmpxchg
#elif PTTYPE == PTTYPE_EPT
	#define pt_element_t u64
	#define guest_walker guest_walkerEPT
	#define FNAME(name) ept_##name
	#define PT_BASE_ADDR_MASK PT64_BASE_ADDR_MASK
	#define PT_LVL_ADDR_MASK(lvl) PT64_LVL_ADDR_MASK(lvl)
	#define PT_LVL_OFFSET_MASK(lvl) PT64_LVL_OFFSET_MASK(lvl)
	#define PT_INDEX(addr, level) PT64_INDEX(addr, level)
	#define PT_LEVEL_BITS PT64_LEVEL_BITS
	#define PT_GUEST_DIRTY_SHIFT 9
	#define PT_GUEST_ACCESSED_SHIFT 8
	#define PT_HAVE_ACCESSED_DIRTY(mmu) ((mmu)->ept_ad)
	#define CMPXCHG cmpxchg64
	#define PT_MAX_FULL_LEVELS PT64_ROOT_MAX_LEVEL
#else
	#error Invalid PTTYPE value
#endif

#define PT_GUEST_DIRTY_MASK    (1 << PT_GUEST_DIRTY_SHIFT)
#define PT_GUEST_ACCESSED_MASK (1 << PT_GUEST_ACCESSED_SHIFT)

#define gpte_to_gfn_lvl FNAME(gpte_to_gfn_lvl)
#define gpte_to_gfn(pte) gpte_to_gfn_lvl((pte), PG_LEVEL_4K)

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
	bool pte_writable[PT_MAX_FULL_LEVELS];
	unsigned int pt_access[PT_MAX_FULL_LEVELS];
	unsigned int pte_access;
	gfn_t gfn;
	struct x86_exception fault;
};

static gfn_t gpte_to_gfn_lvl(pt_element_t gpte, int lvl)
{
	return (gpte & PT_LVL_ADDR_MASK(lvl)) >> PAGE_SHIFT;
}

static inline void FNAME(protect_clean_gpte)(struct kvm_mmu *mmu, unsigned *access,
					     unsigned gpte)
{
	unsigned mask;

	/* dirty bit is not supported, so no need to track it */
	if (!PT_HAVE_ACCESSED_DIRTY(mmu))
		return;

	BUILD_BUG_ON(PT_WRITABLE_MASK != ACC_WRITE_MASK);

	mask = (unsigned)~ACC_WRITE_MASK;
	/* Allow write access to dirty gptes */
	mask |= (gpte >> (PT_GUEST_DIRTY_SHIFT - PT_WRITABLE_SHIFT)) &
		PT_WRITABLE_MASK;
	*access &= mask;
}

static inline int FNAME(is_present_gpte)(unsigned long pte)
{
#if PTTYPE != PTTYPE_EPT
	return pte & PT_PRESENT_MASK;
#else
	return pte & 7;
#endif
}

static bool FNAME(is_bad_mt_xwr)(struct rsvd_bits_validate *rsvd_check, u64 gpte)
{
#if PTTYPE != PTTYPE_EPT
	return false;
#else
	return __is_bad_mt_xwr(rsvd_check, gpte);
#endif
}

static bool FNAME(is_rsvd_bits_set)(struct kvm_mmu *mmu, u64 gpte, int level)
{
	return __is_rsvd_bits_set(&mmu->guest_rsvd_check, gpte, level) ||
	       FNAME(is_bad_mt_xwr)(&mmu->guest_rsvd_check, gpte);
}

static int FNAME(cmpxchg_gpte)(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			       pt_element_t __user *ptep_user, unsigned index,
			       pt_element_t orig_pte, pt_element_t new_pte)
{
	int npages;
	pt_element_t ret;
	pt_element_t *table;
	struct page *page;

	npages = get_user_pages_fast((unsigned long)ptep_user, 1, FOLL_WRITE, &page);
	if (likely(npages == 1)) {
		table = kmap_atomic(page);
		ret = CMPXCHG(&table[index], orig_pte, new_pte);
		kunmap_atomic(table);

		kvm_release_page_dirty(page);
	} else {
		struct vm_area_struct *vma;
		unsigned long vaddr = (unsigned long)ptep_user & PAGE_MASK;
		unsigned long pfn;
		unsigned long paddr;

		mmap_read_lock(current->mm);
		vma = find_vma_intersection(current->mm, vaddr, vaddr + PAGE_SIZE);
		if (!vma || !(vma->vm_flags & VM_PFNMAP)) {
			mmap_read_unlock(current->mm);
			return -EFAULT;
		}
		pfn = ((vaddr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
		paddr = pfn << PAGE_SHIFT;
		table = memremap(paddr, PAGE_SIZE, MEMREMAP_WB);
		if (!table) {
			mmap_read_unlock(current->mm);
			return -EFAULT;
		}
		ret = CMPXCHG(&table[index], orig_pte, new_pte);
		memunmap(table);
		mmap_read_unlock(current->mm);
	}

	return (ret != orig_pte);
}

static bool FNAME(prefetch_invalid_gpte)(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *spte,
				  u64 gpte)
{
	if (!FNAME(is_present_gpte)(gpte))
		goto no_present;

	/* if accessed bit is not supported prefetch non accessed gpte */
	if (PT_HAVE_ACCESSED_DIRTY(vcpu->arch.mmu) &&
	    !(gpte & PT_GUEST_ACCESSED_MASK))
		goto no_present;

	if (FNAME(is_rsvd_bits_set)(vcpu->arch.mmu, gpte, PG_LEVEL_4K))
		goto no_present;

	return false;

no_present:
	drop_spte(vcpu->kvm, spte);
	return true;
}

/*
 * For PTTYPE_EPT, a page table can be executable but not readable
 * on supported processors. Therefore, set_spte does not automatically
 * set bit 0 if execute only is supported. Here, we repurpose ACC_USER_MASK
 * to signify readability since it isn't used in the EPT case
 */
static inline unsigned FNAME(gpte_access)(u64 gpte)
{
	unsigned access;
#if PTTYPE == PTTYPE_EPT
	access = ((gpte & VMX_EPT_WRITABLE_MASK) ? ACC_WRITE_MASK : 0) |
		((gpte & VMX_EPT_EXECUTABLE_MASK) ? ACC_EXEC_MASK : 0) |
		((gpte & VMX_EPT_READABLE_MASK) ? ACC_USER_MASK : 0);
#else
	BUILD_BUG_ON(ACC_EXEC_MASK != PT_PRESENT_MASK);
	BUILD_BUG_ON(ACC_EXEC_MASK != 1);
	access = gpte & (PT_WRITABLE_MASK | PT_USER_MASK | PT_PRESENT_MASK);
	/* Combine NX with P (which is set here) to get ACC_EXEC_MASK.  */
	access ^= (gpte >> PT64_NX_SHIFT);
#endif

	return access;
}

static int FNAME(update_accessed_dirty_bits)(struct kvm_vcpu *vcpu,
					     struct kvm_mmu *mmu,
					     struct guest_walker *walker,
					     gpa_t addr, int write_fault)
{
	unsigned level, index;
	pt_element_t pte, orig_pte;
	pt_element_t __user *ptep_user;
	gfn_t table_gfn;
	int ret;

	/* dirty/accessed bits are not supported, so no need to update them */
	if (!PT_HAVE_ACCESSED_DIRTY(mmu))
		return 0;

	for (level = walker->max_level; level >= walker->level; --level) {
		pte = orig_pte = walker->ptes[level - 1];
		table_gfn = walker->table_gfn[level - 1];
		ptep_user = walker->ptep_user[level - 1];
		index = offset_in_page(ptep_user) / sizeof(pt_element_t);
		if (!(pte & PT_GUEST_ACCESSED_MASK)) {
			trace_kvm_mmu_set_accessed_bit(table_gfn, index, sizeof(pte));
			pte |= PT_GUEST_ACCESSED_MASK;
		}
		if (level == walker->level && write_fault &&
				!(pte & PT_GUEST_DIRTY_MASK)) {
			trace_kvm_mmu_set_dirty_bit(table_gfn, index, sizeof(pte));
#if PTTYPE == PTTYPE_EPT
			if (kvm_x86_ops.nested_ops->write_log_dirty(vcpu, addr))
				return -EINVAL;
#endif
			pte |= PT_GUEST_DIRTY_MASK;
		}
		if (pte == orig_pte)
			continue;

		/*
		 * If the slot is read-only, simply do not process the accessed
		 * and dirty bits.  This is the correct thing to do if the slot
		 * is ROM, and page tables in read-as-ROM/write-as-MMIO slots
		 * are only supported if the accessed and dirty bits are already
		 * set in the ROM (so that MMIO writes are never needed).
		 *
		 * Note that NPT does not allow this at all and faults, since
		 * it always wants nested page table entries for the guest
		 * page tables to be writable.  And EPT works but will simply
		 * overwrite the read-only memory to set the accessed and dirty
		 * bits.
		 */
		if (unlikely(!walker->pte_writable[level - 1]))
			continue;

		ret = FNAME(cmpxchg_gpte)(vcpu, mmu, ptep_user, index, orig_pte, pte);
		if (ret)
			return ret;

		kvm_vcpu_mark_page_dirty(vcpu, table_gfn);
		walker->ptes[level - 1] = pte;
	}
	return 0;
}

static inline unsigned FNAME(gpte_pkeys)(struct kvm_vcpu *vcpu, u64 gpte)
{
	unsigned pkeys = 0;
#if PTTYPE == 64
	pte_t pte = {.pte = gpte};

	pkeys = pte_flags_pkey(pte_flags(pte));
#endif
	return pkeys;
}

/*
 * Fetch a guest pte for a guest virtual address, or for an L2's GPA.
 */
static int FNAME(walk_addr_generic)(struct guest_walker *walker,
				    struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				    gpa_t addr, u32 access)
{
	int ret;
	pt_element_t pte;
	pt_element_t __user *ptep_user;
	gfn_t table_gfn;
	u64 pt_access, pte_access;
	unsigned index, accessed_dirty, pte_pkey;
	unsigned nested_access;
	gpa_t pte_gpa;
	bool have_ad;
	int offset;
	u64 walk_nx_mask = 0;
	const int write_fault = access & PFERR_WRITE_MASK;
	const int user_fault  = access & PFERR_USER_MASK;
	const int fetch_fault = access & PFERR_FETCH_MASK;
	u16 errcode = 0;
	gpa_t real_gpa;
	gfn_t gfn;

	trace_kvm_mmu_pagetable_walk(addr, access);
retry_walk:
	walker->level = mmu->root_level;
	pte           = mmu->get_guest_pgd(vcpu);
	have_ad       = PT_HAVE_ACCESSED_DIRTY(mmu);

#if PTTYPE == 64
	walk_nx_mask = 1ULL << PT64_NX_SHIFT;
	if (walker->level == PT32E_ROOT_LEVEL) {
		pte = mmu->get_pdptr(vcpu, (addr >> 30) & 3);
		trace_kvm_mmu_paging_element(pte, walker->level);
		if (!FNAME(is_present_gpte)(pte))
			goto error;
		--walker->level;
	}
#endif
	walker->max_level = walker->level;
	ASSERT(!(is_long_mode(vcpu) && !is_pae(vcpu)));

	/*
	 * FIXME: on Intel processors, loads of the PDPTE registers for PAE paging
	 * by the MOV to CR instruction are treated as reads and do not cause the
	 * processor to set the dirty flag in any EPT paging-structure entry.
	 */
	nested_access = (have_ad ? PFERR_WRITE_MASK : 0) | PFERR_USER_MASK;

	pte_access = ~0;
	++walker->level;

	do {
		unsigned long host_addr;

		pt_access = pte_access;
		--walker->level;

		index = PT_INDEX(addr, walker->level);
		table_gfn = gpte_to_gfn(pte);
		offset    = index * sizeof(pt_element_t);
		pte_gpa   = gfn_to_gpa(table_gfn) + offset;

		BUG_ON(walker->level < 1);
		walker->table_gfn[walker->level - 1] = table_gfn;
		walker->pte_gpa[walker->level - 1] = pte_gpa;

		real_gpa = mmu->translate_gpa(vcpu, gfn_to_gpa(table_gfn),
					      nested_access,
					      &walker->fault);

		/*
		 * FIXME: This can happen if emulation (for of an INS/OUTS
		 * instruction) triggers a nested page fault.  The exit
		 * qualification / exit info field will incorrectly have
		 * "guest page access" as the nested page fault's cause,
		 * instead of "guest page structure access".  To fix this,
		 * the x86_exception struct should be augmented with enough
		 * information to fix the exit_qualification or exit_info_1
		 * fields.
		 */
		if (unlikely(real_gpa == UNMAPPED_GVA))
			return 0;

		host_addr = kvm_vcpu_gfn_to_hva_prot(vcpu, gpa_to_gfn(real_gpa),
					    &walker->pte_writable[walker->level - 1]);
		if (unlikely(kvm_is_error_hva(host_addr)))
			goto error;

		ptep_user = (pt_element_t __user *)((void *)host_addr + offset);
		if (unlikely(__get_user(pte, ptep_user)))
			goto error;
		walker->ptep_user[walker->level - 1] = ptep_user;

		trace_kvm_mmu_paging_element(pte, walker->level);

		/*
		 * Inverting the NX it lets us AND it like other
		 * permission bits.
		 */
		pte_access = pt_access & (pte ^ walk_nx_mask);

		if (unlikely(!FNAME(is_present_gpte)(pte)))
			goto error;

		if (unlikely(FNAME(is_rsvd_bits_set)(mmu, pte, walker->level))) {
			errcode = PFERR_RSVD_MASK | PFERR_PRESENT_MASK;
			goto error;
		}

		walker->ptes[walker->level - 1] = pte;

		/* Convert to ACC_*_MASK flags for struct guest_walker.  */
		walker->pt_access[walker->level - 1] = FNAME(gpte_access)(pt_access ^ walk_nx_mask);
	} while (!is_last_gpte(mmu, walker->level, pte));

	pte_pkey = FNAME(gpte_pkeys)(vcpu, pte);
	accessed_dirty = have_ad ? pte_access & PT_GUEST_ACCESSED_MASK : 0;

	/* Convert to ACC_*_MASK flags for struct guest_walker.  */
	walker->pte_access = FNAME(gpte_access)(pte_access ^ walk_nx_mask);
	errcode = permission_fault(vcpu, mmu, walker->pte_access, pte_pkey, access);
	if (unlikely(errcode))
		goto error;

	gfn = gpte_to_gfn_lvl(pte, walker->level);
	gfn += (addr & PT_LVL_OFFSET_MASK(walker->level)) >> PAGE_SHIFT;

	if (PTTYPE == 32 && walker->level > PG_LEVEL_4K && is_cpuid_PSE36())
		gfn += pse36_gfn_delta(pte);

	real_gpa = mmu->translate_gpa(vcpu, gfn_to_gpa(gfn), access, &walker->fault);
	if (real_gpa == UNMAPPED_GVA)
		return 0;

	walker->gfn = real_gpa >> PAGE_SHIFT;

	if (!write_fault)
		FNAME(protect_clean_gpte)(mmu, &walker->pte_access, pte);
	else
		/*
		 * On a write fault, fold the dirty bit into accessed_dirty.
		 * For modes without A/D bits support accessed_dirty will be
		 * always clear.
		 */
		accessed_dirty &= pte >>
			(PT_GUEST_DIRTY_SHIFT - PT_GUEST_ACCESSED_SHIFT);

	if (unlikely(!accessed_dirty)) {
		ret = FNAME(update_accessed_dirty_bits)(vcpu, mmu, walker,
							addr, write_fault);
		if (unlikely(ret < 0))
			goto error;
		else if (ret)
			goto retry_walk;
	}

	pgprintk("%s: pte %llx pte_access %x pt_access %x\n",
		 __func__, (u64)pte, walker->pte_access,
		 walker->pt_access[walker->level - 1]);
	return 1;

error:
	errcode |= write_fault | user_fault;
	if (fetch_fault && (mmu->nx ||
			    kvm_read_cr4_bits(vcpu, X86_CR4_SMEP)))
		errcode |= PFERR_FETCH_MASK;

	walker->fault.vector = PF_VECTOR;
	walker->fault.error_code_valid = true;
	walker->fault.error_code = errcode;

#if PTTYPE == PTTYPE_EPT
	/*
	 * Use PFERR_RSVD_MASK in error_code to to tell if EPT
	 * misconfiguration requires to be injected. The detection is
	 * done by is_rsvd_bits_set() above.
	 *
	 * We set up the value of exit_qualification to inject:
	 * [2:0] - Derive from the access bits. The exit_qualification might be
	 *         out of date if it is serving an EPT misconfiguration.
	 * [5:3] - Calculated by the page walk of the guest EPT page tables
	 * [7:8] - Derived from [7:8] of real exit_qualification
	 *
	 * The other bits are set to 0.
	 */
	if (!(errcode & PFERR_RSVD_MASK)) {
		vcpu->arch.exit_qualification &= 0x180;
		if (write_fault)
			vcpu->arch.exit_qualification |= EPT_VIOLATION_ACC_WRITE;
		if (user_fault)
			vcpu->arch.exit_qualification |= EPT_VIOLATION_ACC_READ;
		if (fetch_fault)
			vcpu->arch.exit_qualification |= EPT_VIOLATION_ACC_INSTR;
		vcpu->arch.exit_qualification |= (pte_access & 0x7) << 3;
	}
#endif
	walker->fault.address = addr;
	walker->fault.nested_page_fault = mmu != vcpu->arch.walk_mmu;

	trace_kvm_mmu_walker_error(walker->fault.error_code);
	return 0;
}

static int FNAME(walk_addr)(struct guest_walker *walker,
			    struct kvm_vcpu *vcpu, gpa_t addr, u32 access)
{
	return FNAME(walk_addr_generic)(walker, vcpu, vcpu->arch.mmu, addr,
					access);
}

#if PTTYPE != PTTYPE_EPT
static int FNAME(walk_addr_nested)(struct guest_walker *walker,
				   struct kvm_vcpu *vcpu, gva_t addr,
				   u32 access)
{
	return FNAME(walk_addr_generic)(walker, vcpu, &vcpu->arch.nested_mmu,
					addr, access);
}
#endif

static bool
FNAME(prefetch_gpte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
		     u64 *spte, pt_element_t gpte, bool no_dirty_log)
{
	unsigned pte_access;
	gfn_t gfn;
	kvm_pfn_t pfn;

	if (FNAME(prefetch_invalid_gpte)(vcpu, sp, spte, gpte))
		return false;

	pgprintk("%s: gpte %llx spte %p\n", __func__, (u64)gpte, spte);

	gfn = gpte_to_gfn(gpte);
	pte_access = sp->role.access & FNAME(gpte_access)(gpte);
	FNAME(protect_clean_gpte)(vcpu->arch.mmu, &pte_access, gpte);
	pfn = pte_prefetch_gfn_to_pfn(vcpu, gfn,
			no_dirty_log && (pte_access & ACC_WRITE_MASK));
	if (is_error_pfn(pfn))
		return false;

	/*
	 * we call mmu_set_spte() with host_writable = true because
	 * pte_prefetch_gfn_to_pfn always gets a writable pfn.
	 */
	mmu_set_spte(vcpu, spte, pte_access, false, PG_LEVEL_4K, gfn, pfn,
		     true, true);

	kvm_release_pfn_clean(pfn);
	return true;
}

static void FNAME(update_pte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			      u64 *spte, const void *pte)
{
	pt_element_t gpte = *(const pt_element_t *)pte;

	FNAME(prefetch_gpte)(vcpu, sp, spte, gpte, false);
}

static bool FNAME(gpte_changed)(struct kvm_vcpu *vcpu,
				struct guest_walker *gw, int level)
{
	pt_element_t curr_pte;
	gpa_t base_gpa, pte_gpa = gw->pte_gpa[level - 1];
	u64 mask;
	int r, index;

	if (level == PG_LEVEL_4K) {
		mask = PTE_PREFETCH_NUM * sizeof(pt_element_t) - 1;
		base_gpa = pte_gpa & ~mask;
		index = (pte_gpa - base_gpa) / sizeof(pt_element_t);

		r = kvm_vcpu_read_guest_atomic(vcpu, base_gpa,
				gw->prefetch_ptes, sizeof(gw->prefetch_ptes));
		curr_pte = gw->prefetch_ptes[index];
	} else
		r = kvm_vcpu_read_guest_atomic(vcpu, pte_gpa,
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

	sp = sptep_to_sp(sptep);

	if (sp->role.level > PG_LEVEL_4K)
		return;

	if (sp->role.direct)
		return __direct_pte_prefetch(vcpu, sp, sptep);

	i = (sptep - sp->spt) & ~(PTE_PREFETCH_NUM - 1);
	spte = sp->spt + i;

	for (i = 0; i < PTE_PREFETCH_NUM; i++, spte++) {
		if (spte == sptep)
			continue;

		if (is_shadow_present_pte(*spte))
			continue;

		if (!FNAME(prefetch_gpte)(vcpu, sp, spte, gptep[i], true))
			break;
	}
}

/*
 * Fetch a shadow pte for a specific level in the paging hierarchy.
 * If the guest tries to write a write-protected page, we need to
 * emulate this operation, return 1 to indicate this case.
 */
static int FNAME(fetch)(struct kvm_vcpu *vcpu, gpa_t addr,
			 struct guest_walker *gw, u32 error_code,
			 int max_level, kvm_pfn_t pfn, bool map_writable,
			 bool prefault)
{
	bool nx_huge_page_workaround_enabled = is_nx_huge_page_enabled();
	bool write_fault = error_code & PFERR_WRITE_MASK;
	bool exec = error_code & PFERR_FETCH_MASK;
	bool huge_page_disallowed = exec && nx_huge_page_workaround_enabled;
	struct kvm_mmu_page *sp = NULL;
	struct kvm_shadow_walk_iterator it;
	unsigned int direct_access, access;
	int top_level, level, req_level, ret;
	gfn_t base_gfn = gw->gfn;

	direct_access = gw->pte_access;

	top_level = vcpu->arch.mmu->root_level;
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

	if (WARN_ON(!VALID_PAGE(vcpu->arch.mmu->root_hpa)))
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
			access = gw->pt_access[it.level - 2];
			sp = kvm_mmu_get_page(vcpu, table_gfn, addr, it.level-1,
					      false, access);
		}

		/*
		 * Verify that the gpte in the page we've just write
		 * protected is still there.
		 */
		if (FNAME(gpte_changed)(vcpu, gw, it.level - 1))
			goto out_gpte_changed;

		if (sp)
			link_shadow_page(vcpu, it.sptep, sp);
	}

	level = kvm_mmu_hugepage_adjust(vcpu, gw->gfn, max_level, &pfn,
					huge_page_disallowed, &req_level);

	trace_kvm_mmu_spte_requested(addr, gw->level, pfn);

	for (; shadow_walk_okay(&it); shadow_walk_next(&it)) {
		clear_sp_write_flooding_count(it.sptep);

		/*
		 * We cannot overwrite existing page tables with an NX
		 * large page, as the leaf could be executable.
		 */
		if (nx_huge_page_workaround_enabled)
			disallowed_hugepage_adjust(*it.sptep, gw->gfn, it.level,
						   &pfn, &level);

		base_gfn = gw->gfn & ~(KVM_PAGES_PER_HPAGE(it.level) - 1);
		if (it.level == level)
			break;

		validate_direct_spte(vcpu, it.sptep, direct_access);

		drop_large_spte(vcpu, it.sptep);

		if (!is_shadow_present_pte(*it.sptep)) {
			sp = kvm_mmu_get_page(vcpu, base_gfn, addr,
					      it.level - 1, true, direct_access);
			link_shadow_page(vcpu, it.sptep, sp);
			if (huge_page_disallowed && req_level >= it.level)
				account_huge_nx_page(vcpu->kvm, sp);
		}
	}

	ret = mmu_set_spte(vcpu, it.sptep, gw->pte_access, write_fault,
			   it.level, base_gfn, pfn, prefault, map_writable);
	if (ret == RET_PF_SPURIOUS)
		return ret;

	FNAME(pte_prefetch)(vcpu, gw, it.sptep);
	++vcpu->stat.pf_fixed;
	return ret;

out_gpte_changed:
	return RET_PF_RETRY;
}

 /*
 * To see whether the mapped gfn can write its page table in the current
 * mapping.
 *
 * It is the helper function of FNAME(page_fault). When guest uses large page
 * size to map the writable gfn which is used as current page table, we should
 * force kvm to use small page size to map it because new shadow page will be
 * created when kvm establishes shadow page table that stop kvm using large
 * page size. Do it early can avoid unnecessary #PF and emulation.
 *
 * @write_fault_to_shadow_pgtable will return true if the fault gfn is
 * currently used as its page table.
 *
 * Note: the PDPT page table is not checked for PAE-32 bit guest. It is ok
 * since the PDPT is always shadowed, that means, we can not use large page
 * size to map the gfn which is used as PDPT.
 */
static bool
FNAME(is_self_change_mapping)(struct kvm_vcpu *vcpu,
			      struct guest_walker *walker, bool user_fault,
			      bool *write_fault_to_shadow_pgtable)
{
	int level;
	gfn_t mask = ~(KVM_PAGES_PER_HPAGE(walker->level) - 1);
	bool self_changed = false;

	if (!(walker->pte_access & ACC_WRITE_MASK ||
	      (!is_write_protection(vcpu) && !user_fault)))
		return false;

	for (level = walker->level; level <= walker->max_level; level++) {
		gfn_t gfn = walker->gfn ^ walker->table_gfn[level - 1];

		self_changed |= !(gfn & mask);
		*write_fault_to_shadow_pgtable |= !gfn;
	}

	return self_changed;
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
static int FNAME(page_fault)(struct kvm_vcpu *vcpu, gpa_t addr, u32 error_code,
			     bool prefault)
{
	bool write_fault = error_code & PFERR_WRITE_MASK;
	bool user_fault = error_code & PFERR_USER_MASK;
	struct guest_walker walker;
	int r;
	kvm_pfn_t pfn;
	unsigned long mmu_seq;
	bool map_writable, is_self_change_mapping;
	int max_level;

	pgprintk("%s: addr %lx err %x\n", __func__, addr, error_code);

	/*
	 * If PFEC.RSVD is set, this is a shadow page fault.
	 * The bit needs to be cleared before walking guest page tables.
	 */
	error_code &= ~PFERR_RSVD_MASK;

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
			kvm_inject_emulated_page_fault(vcpu, &walker.fault);

		return RET_PF_RETRY;
	}

	if (page_fault_handle_page_track(vcpu, error_code, walker.gfn)) {
		shadow_page_table_clear_flood(vcpu, addr);
		return RET_PF_EMULATE;
	}

	r = mmu_topup_memory_caches(vcpu, true);
	if (r)
		return r;

	vcpu->arch.write_fault_to_shadow_pgtable = false;

	is_self_change_mapping = FNAME(is_self_change_mapping)(vcpu,
	      &walker, user_fault, &vcpu->arch.write_fault_to_shadow_pgtable);

	if (is_self_change_mapping)
		max_level = PG_LEVEL_4K;
	else
		max_level = walker.level;

	mmu_seq = vcpu->kvm->mmu_notifier_seq;
	smp_rmb();

	if (try_async_pf(vcpu, prefault, walker.gfn, addr, &pfn, write_fault,
			 &map_writable))
		return RET_PF_RETRY;

	if (handle_abnormal_pfn(vcpu, addr, walker.gfn, pfn, walker.pte_access, &r))
		return r;

	/*
	 * Do not change pte_access if the pfn is a mmio page, otherwise
	 * we will cache the incorrect access into mmio spte.
	 */
	if (write_fault && !(walker.pte_access & ACC_WRITE_MASK) &&
	     !is_write_protection(vcpu) && !user_fault &&
	      !is_noslot_pfn(pfn)) {
		walker.pte_access |= ACC_WRITE_MASK;
		walker.pte_access &= ~ACC_USER_MASK;

		/*
		 * If we converted a user page to a kernel page,
		 * so that the kernel can write to it when cr0.wp=0,
		 * then we should prevent the kernel from executing it
		 * if SMEP is enabled.
		 */
		if (kvm_read_cr4_bits(vcpu, X86_CR4_SMEP))
			walker.pte_access &= ~ACC_EXEC_MASK;
	}

	r = RET_PF_RETRY;
	spin_lock(&vcpu->kvm->mmu_lock);
	if (mmu_notifier_retry(vcpu->kvm, mmu_seq))
		goto out_unlock;

	kvm_mmu_audit(vcpu, AUDIT_PRE_PAGE_FAULT);
	r = make_mmu_pages_available(vcpu);
	if (r)
		goto out_unlock;
	r = FNAME(fetch)(vcpu, addr, &walker, error_code, max_level, pfn,
			 map_writable, prefault);
	kvm_mmu_audit(vcpu, AUDIT_POST_PAGE_FAULT);

out_unlock:
	spin_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	return r;
}

static gpa_t FNAME(get_level1_sp_gpa)(struct kvm_mmu_page *sp)
{
	int offset = 0;

	WARN_ON(sp->role.level != PG_LEVEL_4K);

	if (PTTYPE == 32)
		offset = sp->role.quadrant << PT64_LEVEL_BITS;

	return gfn_to_gpa(sp->gfn) + offset * sizeof(pt_element_t);
}

static void FNAME(invlpg)(struct kvm_vcpu *vcpu, gva_t gva, hpa_t root_hpa)
{
	struct kvm_shadow_walk_iterator iterator;
	struct kvm_mmu_page *sp;
	u64 old_spte;
	int level;
	u64 *sptep;

	vcpu_clear_mmio_info(vcpu, gva);

	/*
	 * No need to check return value here, rmap_can_add() can
	 * help us to skip pte prefetch later.
	 */
	mmu_topup_memory_caches(vcpu, true);

	if (!VALID_PAGE(root_hpa)) {
		WARN_ON(1);
		return;
	}

	spin_lock(&vcpu->kvm->mmu_lock);
	for_each_shadow_entry_using_root(vcpu, root_hpa, gva, iterator) {
		level = iterator.level;
		sptep = iterator.sptep;

		sp = sptep_to_sp(sptep);
		old_spte = *sptep;
		if (is_last_spte(old_spte, level)) {
			pt_element_t gpte;
			gpa_t pte_gpa;

			if (!sp->unsync)
				break;

			pte_gpa = FNAME(get_level1_sp_gpa)(sp);
			pte_gpa += (sptep - sp->spt) * sizeof(pt_element_t);

			mmu_page_zap_pte(vcpu->kvm, sp, sptep, NULL);
			if (is_shadow_present_pte(old_spte))
				kvm_flush_remote_tlbs_with_address(vcpu->kvm,
					sp->gfn, KVM_PAGES_PER_HPAGE(sp->role.level));

			if (!rmap_can_add(vcpu))
				break;

			if (kvm_vcpu_read_guest_atomic(vcpu, pte_gpa, &gpte,
						       sizeof(pt_element_t)))
				break;

			FNAME(update_pte)(vcpu, sp, sptep, &gpte);
		}

		if (!is_shadow_present_pte(*sptep) || !sp->unsync_children)
			break;
	}
	spin_unlock(&vcpu->kvm->mmu_lock);
}

/* Note, @addr is a GPA when gva_to_gpa() translates an L2 GPA to an L1 GPA. */
static gpa_t FNAME(gva_to_gpa)(struct kvm_vcpu *vcpu, gpa_t addr, u32 access,
			       struct x86_exception *exception)
{
	struct guest_walker walker;
	gpa_t gpa = UNMAPPED_GVA;
	int r;

	r = FNAME(walk_addr)(&walker, vcpu, addr, access);

	if (r) {
		gpa = gfn_to_gpa(walker.gfn);
		gpa |= addr & ~PAGE_MASK;
	} else if (exception)
		*exception = walker.fault;

	return gpa;
}

#if PTTYPE != PTTYPE_EPT
/* Note, gva_to_gpa_nested() is only used to translate L2 GVAs. */
static gpa_t FNAME(gva_to_gpa_nested)(struct kvm_vcpu *vcpu, gpa_t vaddr,
				      u32 access,
				      struct x86_exception *exception)
{
	struct guest_walker walker;
	gpa_t gpa = UNMAPPED_GVA;
	int r;

#ifndef CONFIG_X86_64
	/* A 64-bit GVA should be impossible on 32-bit KVM. */
	WARN_ON_ONCE(vaddr >> 32);
#endif

	r = FNAME(walk_addr_nested)(&walker, vcpu, vaddr, access);

	if (r) {
		gpa = gfn_to_gpa(walker.gfn);
		gpa |= vaddr & ~PAGE_MASK;
	} else if (exception)
		*exception = walker.fault;

	return gpa;
}
#endif

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
	int set_spte_ret = 0;

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

		if (kvm_vcpu_read_guest_atomic(vcpu, pte_gpa, &gpte,
					       sizeof(pt_element_t)))
			return 0;

		if (FNAME(prefetch_invalid_gpte)(vcpu, sp, &sp->spt[i], gpte)) {
			/*
			 * Update spte before increasing tlbs_dirty to make
			 * sure no tlb flush is lost after spte is zapped; see
			 * the comments in kvm_flush_remote_tlbs().
			 */
			smp_wmb();
			vcpu->kvm->tlbs_dirty++;
			continue;
		}

		gfn = gpte_to_gfn(gpte);
		pte_access = sp->role.access;
		pte_access &= FNAME(gpte_access)(gpte);
		FNAME(protect_clean_gpte)(vcpu->arch.mmu, &pte_access, gpte);

		if (sync_mmio_spte(vcpu, &sp->spt[i], gfn, pte_access,
		      &nr_present))
			continue;

		if (gfn != sp->gfns[i]) {
			drop_spte(vcpu->kvm, &sp->spt[i]);
			/*
			 * The same as above where we are doing
			 * prefetch_invalid_gpte().
			 */
			smp_wmb();
			vcpu->kvm->tlbs_dirty++;
			continue;
		}

		nr_present++;

		host_writable = sp->spt[i] & SPTE_HOST_WRITEABLE;

		set_spte_ret |= set_spte(vcpu, &sp->spt[i],
					 pte_access, PG_LEVEL_4K,
					 gfn, spte_to_pfn(sp->spt[i]),
					 true, false, host_writable);
	}

	if (set_spte_ret & SET_SPTE_NEED_REMOTE_TLB_FLUSH)
		kvm_flush_remote_tlbs(vcpu->kvm);

	return nr_present;
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
#undef PT_GUEST_ACCESSED_MASK
#undef PT_GUEST_DIRTY_MASK
#undef PT_GUEST_DIRTY_SHIFT
#undef PT_GUEST_ACCESSED_SHIFT
#undef PT_HAVE_ACCESSED_DIRTY
