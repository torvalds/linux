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
 * The MMU needs to be able to access/walk 32-bit and 64-bit guest page tables,
 * as well as guest EPT tables, so the code in this file is compiled thrice,
 * once per guest PTE type.  The per-type defines are #undef'd at the end.
 */

#if PTTYPE == 64
	#define pt_element_t u64
	#define guest_walker guest_walker64
	#define FNAME(name) paging##64_##name
	#define PT_LEVEL_BITS 9
	#define PT_GUEST_DIRTY_SHIFT PT_DIRTY_SHIFT
	#define PT_GUEST_ACCESSED_SHIFT PT_ACCESSED_SHIFT
	#define PT_HAVE_ACCESSED_DIRTY(mmu) true
	#ifdef CONFIG_X86_64
	#define PT_MAX_FULL_LEVELS PT64_ROOT_MAX_LEVEL
	#else
	#define PT_MAX_FULL_LEVELS 2
	#endif
#elif PTTYPE == 32
	#define pt_element_t u32
	#define guest_walker guest_walker32
	#define FNAME(name) paging##32_##name
	#define PT_LEVEL_BITS 10
	#define PT_MAX_FULL_LEVELS 2
	#define PT_GUEST_DIRTY_SHIFT PT_DIRTY_SHIFT
	#define PT_GUEST_ACCESSED_SHIFT PT_ACCESSED_SHIFT
	#define PT_HAVE_ACCESSED_DIRTY(mmu) true

	#define PT32_DIR_PSE36_SIZE 4
	#define PT32_DIR_PSE36_SHIFT 13
	#define PT32_DIR_PSE36_MASK \
		(((1ULL << PT32_DIR_PSE36_SIZE) - 1) << PT32_DIR_PSE36_SHIFT)
#elif PTTYPE == PTTYPE_EPT
	#define pt_element_t u64
	#define guest_walker guest_walkerEPT
	#define FNAME(name) ept_##name
	#define PT_LEVEL_BITS 9
	#define PT_GUEST_DIRTY_SHIFT 9
	#define PT_GUEST_ACCESSED_SHIFT 8
	#define PT_HAVE_ACCESSED_DIRTY(mmu) (!(mmu)->cpu_role.base.ad_disabled)
	#define PT_MAX_FULL_LEVELS PT64_ROOT_MAX_LEVEL
#else
	#error Invalid PTTYPE value
#endif

/* Common logic, but per-type values.  These also need to be undefined. */
#define PT_BASE_ADDR_MASK	((pt_element_t)(((1ULL << 52) - 1) & ~(u64)(PAGE_SIZE-1)))
#define PT_LVL_ADDR_MASK(lvl)	__PT_LVL_ADDR_MASK(PT_BASE_ADDR_MASK, lvl, PT_LEVEL_BITS)
#define PT_LVL_OFFSET_MASK(lvl)	__PT_LVL_OFFSET_MASK(PT_BASE_ADDR_MASK, lvl, PT_LEVEL_BITS)
#define PT_INDEX(addr, lvl)	__PT_INDEX(addr, lvl, PT_LEVEL_BITS)

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

#if PTTYPE == 32
static inline gfn_t pse36_gfn_delta(u32 gpte)
{
	int shift = 32 - PT32_DIR_PSE36_SHIFT - PAGE_SHIFT;

	return (gpte & PT32_DIR_PSE36_MASK) << shift;
}
#endif

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

static bool FNAME(prefetch_invalid_gpte)(struct kvm_vcpu *vcpu,
				  struct kvm_mmu_page *sp, u64 *spte,
				  u64 gpte)
{
	if (!FNAME(is_present_gpte)(gpte))
		goto no_present;

	/* Prefetch only accessed entries (unless A/D bits are disabled). */
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

		ret = __try_cmpxchg_user(ptep_user, &orig_pte, pte, fault);
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

static inline bool FNAME(is_last_gpte)(struct kvm_mmu *mmu,
				       unsigned int level, unsigned int gpte)
{
	/*
	 * For EPT and PAE paging (both variants), bit 7 is either reserved at
	 * all level or indicates a huge page (ignoring CR3/EPTP).  In either
	 * case, bit 7 being set terminates the walk.
	 */
#if PTTYPE == 32
	/*
	 * 32-bit paging requires special handling because bit 7 is ignored if
	 * CR4.PSE=0, not reserved.  Clear bit 7 in the gpte if the level is
	 * greater than the last level for which bit 7 is the PAGE_SIZE bit.
	 *
	 * The RHS has bit 7 set iff level < (2 + PSE).  If it is clear, bit 7
	 * is not reserved and does not indicate a large page at this level,
	 * so clear PT_PAGE_SIZE_MASK in gpte if that is the case.
	 */
	gpte &= level - (PT32_ROOT_LEVEL + mmu->cpu_role.ext.cr4_pse);
#endif
	/*
	 * PG_LEVEL_4K always terminates.  The RHS has bit 7 set
	 * iff level <= PG_LEVEL_4K, which for our purpose means
	 * level == PG_LEVEL_4K; set PT_PAGE_SIZE_MASK in gpte then.
	 */
	gpte |= level - PG_LEVEL_4K - 1;

	return gpte & PT_PAGE_SIZE_MASK;
}
/*
 * Fetch a guest pte for a guest virtual address, or for an L2's GPA.
 */
static int FNAME(walk_addr_generic)(struct guest_walker *walker,
				    struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
				    gpa_t addr, u64 access)
{
	int ret;
	pt_element_t pte;
	pt_element_t __user *ptep_user;
	gfn_t table_gfn;
	u64 pt_access, pte_access;
	unsigned index, accessed_dirty, pte_pkey;
	u64 nested_access;
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
	walker->level = mmu->cpu_role.base.level;
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

		real_gpa = kvm_translate_gpa(vcpu, mmu, gfn_to_gpa(table_gfn),
					     nested_access, &walker->fault);

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
		if (unlikely(real_gpa == INVALID_GPA))
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
	} while (!FNAME(is_last_gpte)(mmu, walker->level, pte));

	pte_pkey = FNAME(gpte_pkeys)(vcpu, pte);
	accessed_dirty = have_ad ? pte_access & PT_GUEST_ACCESSED_MASK : 0;

	/* Convert to ACC_*_MASK flags for struct guest_walker.  */
	walker->pte_access = FNAME(gpte_access)(pte_access ^ walk_nx_mask);
	errcode = permission_fault(vcpu, mmu, walker->pte_access, pte_pkey, access);
	if (unlikely(errcode))
		goto error;

	gfn = gpte_to_gfn_lvl(pte, walker->level);
	gfn += (addr & PT_LVL_OFFSET_MASK(walker->level)) >> PAGE_SHIFT;

#if PTTYPE == 32
	if (walker->level > PG_LEVEL_4K && is_cpuid_PSE36())
		gfn += pse36_gfn_delta(pte);
#endif

	real_gpa = kvm_translate_gpa(vcpu, mmu, gfn_to_gpa(gfn), access, &walker->fault);
	if (real_gpa == INVALID_GPA)
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
	if (fetch_fault && (is_efer_nx(mmu) || is_cr4_smep(mmu)))
		errcode |= PFERR_FETCH_MASK;

	walker->fault.vector = PF_VECTOR;
	walker->fault.error_code_valid = true;
	walker->fault.error_code = errcode;

#if PTTYPE == PTTYPE_EPT
	/*
	 * Use PFERR_RSVD_MASK in error_code to tell if EPT
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
		vcpu->arch.exit_qualification &= (EPT_VIOLATION_GVA_IS_VALID |
						  EPT_VIOLATION_GVA_TRANSLATED);
		if (write_fault)
			vcpu->arch.exit_qualification |= EPT_VIOLATION_ACC_WRITE;
		if (user_fault)
			vcpu->arch.exit_qualification |= EPT_VIOLATION_ACC_READ;
		if (fetch_fault)
			vcpu->arch.exit_qualification |= EPT_VIOLATION_ACC_INSTR;

		/*
		 * Note, pte_access holds the raw RWX bits from the EPTE, not
		 * ACC_*_MASK flags!
		 */
		vcpu->arch.exit_qualification |= (pte_access & VMX_EPT_RWX_MASK) <<
						 EPT_VIOLATION_RWX_SHIFT;
	}
#endif
	walker->fault.address = addr;
	walker->fault.nested_page_fault = mmu != vcpu->arch.walk_mmu;
	walker->fault.async_page_fault = false;

	trace_kvm_mmu_walker_error(walker->fault.error_code);
	return 0;
}

static int FNAME(walk_addr)(struct guest_walker *walker,
			    struct kvm_vcpu *vcpu, gpa_t addr, u64 access)
{
	return FNAME(walk_addr_generic)(walker, vcpu, vcpu->arch.mmu, addr,
					access);
}

static bool
FNAME(prefetch_gpte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
		     u64 *spte, pt_element_t gpte, bool no_dirty_log)
{
	struct kvm_memory_slot *slot;
	unsigned pte_access;
	gfn_t gfn;
	kvm_pfn_t pfn;

	if (FNAME(prefetch_invalid_gpte)(vcpu, sp, spte, gpte))
		return false;

	pgprintk("%s: gpte %llx spte %p\n", __func__, (u64)gpte, spte);

	gfn = gpte_to_gfn(gpte);
	pte_access = sp->role.access & FNAME(gpte_access)(gpte);
	FNAME(protect_clean_gpte)(vcpu->arch.mmu, &pte_access, gpte);

	slot = gfn_to_memslot_dirty_bitmap(vcpu, gfn,
			no_dirty_log && (pte_access & ACC_WRITE_MASK));
	if (!slot)
		return false;

	pfn = gfn_to_pfn_memslot_atomic(slot, gfn);
	if (is_error_pfn(pfn))
		return false;

	mmu_set_spte(vcpu, slot, spte, pte_access, gfn, pfn, NULL);
	kvm_release_pfn_clean(pfn);
	return true;
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

	/*
	 * If addresses are being invalidated, skip prefetching to avoid
	 * accidentally prefetching those addresses.
	 */
	if (unlikely(vcpu->kvm->mmu_invalidate_in_progress))
		return;

	if (sp->role.direct)
		return __direct_pte_prefetch(vcpu, sp, sptep);

	i = spte_index(sptep) & ~(PTE_PREFETCH_NUM - 1);
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
static int FNAME(fetch)(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault,
			 struct guest_walker *gw)
{
	struct kvm_mmu_page *sp = NULL;
	struct kvm_shadow_walk_iterator it;
	unsigned int direct_access, access;
	int top_level, ret;
	gfn_t base_gfn = fault->gfn;

	WARN_ON_ONCE(gw->gfn != base_gfn);
	direct_access = gw->pte_access;

	top_level = vcpu->arch.mmu->cpu_role.base.level;
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

	if (WARN_ON(!VALID_PAGE(vcpu->arch.mmu->root.hpa)))
		goto out_gpte_changed;

	for_each_shadow_entry(vcpu, fault->addr, it) {
		gfn_t table_gfn;

		clear_sp_write_flooding_count(it.sptep);
		if (it.level == gw->level)
			break;

		table_gfn = gw->table_gfn[it.level - 2];
		access = gw->pt_access[it.level - 2];
		sp = kvm_mmu_get_child_sp(vcpu, it.sptep, table_gfn,
					  false, access);

		if (sp != ERR_PTR(-EEXIST)) {
			/*
			 * We must synchronize the pagetable before linking it
			 * because the guest doesn't need to flush tlb when
			 * the gpte is changed from non-present to present.
			 * Otherwise, the guest may use the wrong mapping.
			 *
			 * For PG_LEVEL_4K, kvm_mmu_get_page() has already
			 * synchronized it transiently via kvm_sync_page().
			 *
			 * For higher level pagetable, we synchronize it via
			 * the slower mmu_sync_children().  If it needs to
			 * break, some progress has been made; return
			 * RET_PF_RETRY and retry on the next #PF.
			 * KVM_REQ_MMU_SYNC is not necessary but it
			 * expedites the process.
			 */
			if (sp->unsync_children &&
			    mmu_sync_children(vcpu, sp, false))
				return RET_PF_RETRY;
		}

		/*
		 * Verify that the gpte in the page we've just write
		 * protected is still there.
		 */
		if (FNAME(gpte_changed)(vcpu, gw, it.level - 1))
			goto out_gpte_changed;

		if (sp != ERR_PTR(-EEXIST))
			link_shadow_page(vcpu, it.sptep, sp);
	}

	kvm_mmu_hugepage_adjust(vcpu, fault);

	trace_kvm_mmu_spte_requested(fault);

	for (; shadow_walk_okay(&it); shadow_walk_next(&it)) {
		/*
		 * We cannot overwrite existing page tables with an NX
		 * large page, as the leaf could be executable.
		 */
		if (fault->nx_huge_page_workaround_enabled)
			disallowed_hugepage_adjust(fault, *it.sptep, it.level);

		base_gfn = gfn_round_for_level(fault->gfn, it.level);
		if (it.level == fault->goal_level)
			break;

		validate_direct_spte(vcpu, it.sptep, direct_access);

		sp = kvm_mmu_get_child_sp(vcpu, it.sptep, base_gfn,
					  true, direct_access);
		if (sp == ERR_PTR(-EEXIST))
			continue;

		link_shadow_page(vcpu, it.sptep, sp);
		if (fault->huge_page_disallowed)
			account_nx_huge_page(vcpu->kvm, sp,
					     fault->req_level >= it.level);
	}

	if (WARN_ON_ONCE(it.level != fault->goal_level))
		return -EFAULT;

	ret = mmu_set_spte(vcpu, fault->slot, it.sptep, gw->pte_access,
			   base_gfn, fault->pfn, fault);
	if (ret == RET_PF_SPURIOUS)
		return ret;

	FNAME(pte_prefetch)(vcpu, gw, it.sptep);
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
	    (!is_cr0_wp(vcpu->arch.mmu) && !user_fault)))
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
static int FNAME(page_fault)(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	struct guest_walker walker;
	int r;
	bool is_self_change_mapping;

	pgprintk("%s: addr %lx err %x\n", __func__, fault->addr, fault->error_code);
	WARN_ON_ONCE(fault->is_tdp);

	/*
	 * Look up the guest pte for the faulting address.
	 * If PFEC.RSVD is set, this is a shadow page fault.
	 * The bit needs to be cleared before walking guest page tables.
	 */
	r = FNAME(walk_addr)(&walker, vcpu, fault->addr,
			     fault->error_code & ~PFERR_RSVD_MASK);

	/*
	 * The page is not mapped by the guest.  Let the guest handle it.
	 */
	if (!r) {
		pgprintk("%s: guest page fault\n", __func__);
		if (!fault->prefetch)
			kvm_inject_emulated_page_fault(vcpu, &walker.fault);

		return RET_PF_RETRY;
	}

	fault->gfn = walker.gfn;
	fault->slot = kvm_vcpu_gfn_to_memslot(vcpu, fault->gfn);

	if (page_fault_handle_page_track(vcpu, fault)) {
		shadow_page_table_clear_flood(vcpu, fault->addr);
		return RET_PF_EMULATE;
	}

	r = mmu_topup_memory_caches(vcpu, true);
	if (r)
		return r;

	vcpu->arch.write_fault_to_shadow_pgtable = false;

	is_self_change_mapping = FNAME(is_self_change_mapping)(vcpu,
	      &walker, fault->user, &vcpu->arch.write_fault_to_shadow_pgtable);

	if (is_self_change_mapping)
		fault->max_level = PG_LEVEL_4K;
	else
		fault->max_level = walker.level;

	r = kvm_faultin_pfn(vcpu, fault, walker.pte_access);
	if (r != RET_PF_CONTINUE)
		return r;

	/*
	 * Do not change pte_access if the pfn is a mmio page, otherwise
	 * we will cache the incorrect access into mmio spte.
	 */
	if (fault->write && !(walker.pte_access & ACC_WRITE_MASK) &&
	    !is_cr0_wp(vcpu->arch.mmu) && !fault->user && fault->slot) {
		walker.pte_access |= ACC_WRITE_MASK;
		walker.pte_access &= ~ACC_USER_MASK;

		/*
		 * If we converted a user page to a kernel page,
		 * so that the kernel can write to it when cr0.wp=0,
		 * then we should prevent the kernel from executing it
		 * if SMEP is enabled.
		 */
		if (is_cr4_smep(vcpu->arch.mmu))
			walker.pte_access &= ~ACC_EXEC_MASK;
	}

	r = RET_PF_RETRY;
	write_lock(&vcpu->kvm->mmu_lock);

	if (is_page_fault_stale(vcpu, fault))
		goto out_unlock;

	r = make_mmu_pages_available(vcpu);
	if (r)
		goto out_unlock;
	r = FNAME(fetch)(vcpu, fault, &walker);

out_unlock:
	write_unlock(&vcpu->kvm->mmu_lock);
	kvm_release_pfn_clean(fault->pfn);
	return r;
}

static gpa_t FNAME(get_level1_sp_gpa)(struct kvm_mmu_page *sp)
{
	int offset = 0;

	WARN_ON(sp->role.level != PG_LEVEL_4K);

	if (PTTYPE == 32)
		offset = sp->role.quadrant << SPTE_LEVEL_BITS;

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

	write_lock(&vcpu->kvm->mmu_lock);
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
			pte_gpa += spte_index(sptep) * sizeof(pt_element_t);

			mmu_page_zap_pte(vcpu->kvm, sp, sptep, NULL);
			if (is_shadow_present_pte(old_spte))
				kvm_flush_remote_tlbs_sptep(vcpu->kvm, sptep);

			if (!rmap_can_add(vcpu))
				break;

			if (kvm_vcpu_read_guest_atomic(vcpu, pte_gpa, &gpte,
						       sizeof(pt_element_t)))
				break;

			FNAME(prefetch_gpte)(vcpu, sp, sptep, gpte, false);
		}

		if (!sp->unsync_children)
			break;
	}
	write_unlock(&vcpu->kvm->mmu_lock);
}

/* Note, @addr is a GPA when gva_to_gpa() translates an L2 GPA to an L1 GPA. */
static gpa_t FNAME(gva_to_gpa)(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			       gpa_t addr, u64 access,
			       struct x86_exception *exception)
{
	struct guest_walker walker;
	gpa_t gpa = INVALID_GPA;
	int r;

#ifndef CONFIG_X86_64
	/* A 64-bit GVA should be impossible on 32-bit KVM. */
	WARN_ON_ONCE((addr >> 32) && mmu == vcpu->arch.walk_mmu);
#endif

	r = FNAME(walk_addr_generic)(&walker, vcpu, mmu, addr, access);

	if (r) {
		gpa = gfn_to_gpa(walker.gfn);
		gpa |= addr & ~PAGE_MASK;
	} else if (exception)
		*exception = walker.fault;

	return gpa;
}

/*
 * Using the information in sp->shadowed_translation (kvm_mmu_page_get_gfn()) is
 * safe because:
 * - The spte has a reference to the struct page, so the pfn for a given gfn
 *   can't change unless all sptes pointing to it are nuked first.
 *
 * Returns
 * < 0: the sp should be zapped
 *   0: the sp is synced and no tlb flushing is required
 * > 0: the sp is synced and tlb flushing is required
 */
static int FNAME(sync_page)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp)
{
	union kvm_mmu_page_role root_role = vcpu->arch.mmu->root_role;
	int i;
	bool host_writable;
	gpa_t first_pte_gpa;
	bool flush = false;

	/*
	 * Ignore various flags when verifying that it's safe to sync a shadow
	 * page using the current MMU context.
	 *
	 *  - level: not part of the overall MMU role and will never match as the MMU's
	 *           level tracks the root level
	 *  - access: updated based on the new guest PTE
	 *  - quadrant: not part of the overall MMU role (similar to level)
	 */
	const union kvm_mmu_page_role sync_role_ign = {
		.level = 0xf,
		.access = 0x7,
		.quadrant = 0x3,
		.passthrough = 0x1,
	};

	/*
	 * Direct pages can never be unsync, and KVM should never attempt to
	 * sync a shadow page for a different MMU context, e.g. if the role
	 * differs then the memslot lookup (SMM vs. non-SMM) will be bogus, the
	 * reserved bits checks will be wrong, etc...
	 */
	if (WARN_ON_ONCE(sp->role.direct ||
			 (sp->role.word ^ root_role.word) & ~sync_role_ign.word))
		return -1;

	first_pte_gpa = FNAME(get_level1_sp_gpa)(sp);

	for (i = 0; i < SPTE_ENT_PER_PAGE; i++) {
		u64 *sptep, spte;
		struct kvm_memory_slot *slot;
		unsigned pte_access;
		pt_element_t gpte;
		gpa_t pte_gpa;
		gfn_t gfn;

		if (!sp->spt[i])
			continue;

		pte_gpa = first_pte_gpa + i * sizeof(pt_element_t);

		if (kvm_vcpu_read_guest_atomic(vcpu, pte_gpa, &gpte,
					       sizeof(pt_element_t)))
			return -1;

		if (FNAME(prefetch_invalid_gpte)(vcpu, sp, &sp->spt[i], gpte)) {
			flush = true;
			continue;
		}

		gfn = gpte_to_gfn(gpte);
		pte_access = sp->role.access;
		pte_access &= FNAME(gpte_access)(gpte);
		FNAME(protect_clean_gpte)(vcpu->arch.mmu, &pte_access, gpte);

		if (sync_mmio_spte(vcpu, &sp->spt[i], gfn, pte_access))
			continue;

		/*
		 * Drop the SPTE if the new protections would result in a RWX=0
		 * SPTE or if the gfn is changing.  The RWX=0 case only affects
		 * EPT with execute-only support, i.e. EPT without an effective
		 * "present" bit, as all other paging modes will create a
		 * read-only SPTE if pte_access is zero.
		 */
		if ((!pte_access && !shadow_present_mask) ||
		    gfn != kvm_mmu_page_get_gfn(sp, i)) {
			drop_spte(vcpu->kvm, &sp->spt[i]);
			flush = true;
			continue;
		}

		/* Update the shadowed access bits in case they changed. */
		kvm_mmu_page_set_access(sp, i, pte_access);

		sptep = &sp->spt[i];
		spte = *sptep;
		host_writable = spte & shadow_host_writable_mask;
		slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
		make_spte(vcpu, sp, slot, pte_access, gfn,
			  spte_to_pfn(spte), spte, true, false,
			  host_writable, &spte);

		flush |= mmu_spte_update(sptep, spte);
	}

	/*
	 * Note, any flush is purely for KVM's correctness, e.g. when dropping
	 * an existing SPTE or clearing W/A/D bits to ensure an mmu_notifier
	 * unmap or dirty logging event doesn't fail to flush.  The guest is
	 * responsible for flushing the TLB to ensure any changes in protection
	 * bits are recognized, i.e. until the guest flushes or page faults on
	 * a relevant address, KVM is architecturally allowed to let vCPUs use
	 * cached translations with the old protection bits.
	 */
	return flush;
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
#undef PT_GUEST_ACCESSED_MASK
#undef PT_GUEST_DIRTY_MASK
#undef PT_GUEST_DIRTY_SHIFT
#undef PT_GUEST_ACCESSED_SHIFT
#undef PT_HAVE_ACCESSED_DIRTY
