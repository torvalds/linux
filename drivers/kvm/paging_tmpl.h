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
	#define PT_DIR_BASE_ADDR_MASK PT64_DIR_BASE_ADDR_MASK
	#define PT_INDEX(addr, level) PT64_INDEX(addr, level)
	#define SHADOW_PT_INDEX(addr, level) PT64_INDEX(addr, level)
	#define PT_LEVEL_MASK(level) PT64_LEVEL_MASK(level)
	#define PT_PTE_COPY_MASK PT64_PTE_COPY_MASK
	#ifdef CONFIG_X86_64
	#define PT_MAX_FULL_LEVELS 4
	#else
	#define PT_MAX_FULL_LEVELS 2
	#endif
#elif PTTYPE == 32
	#define pt_element_t u32
	#define guest_walker guest_walker32
	#define FNAME(name) paging##32_##name
	#define PT_BASE_ADDR_MASK PT32_BASE_ADDR_MASK
	#define PT_DIR_BASE_ADDR_MASK PT32_DIR_BASE_ADDR_MASK
	#define PT_INDEX(addr, level) PT32_INDEX(addr, level)
	#define SHADOW_PT_INDEX(addr, level) PT64_INDEX(addr, level)
	#define PT_LEVEL_MASK(level) PT32_LEVEL_MASK(level)
	#define PT_PTE_COPY_MASK PT32_PTE_COPY_MASK
	#define PT_MAX_FULL_LEVELS 2
#else
	#error Invalid PTTYPE value
#endif

/*
 * The guest_walker structure emulates the behavior of the hardware page
 * table walker.
 */
struct guest_walker {
	int level;
	gfn_t table_gfn[PT_MAX_FULL_LEVELS];
	pt_element_t *table;
	pt_element_t *ptep;
	pt_element_t inherited_ar;
	gfn_t gfn;
	u32 error_code;
};

/*
 * Fetch a guest pte for a guest virtual address
 */
static int FNAME(walk_addr)(struct guest_walker *walker,
			    struct kvm_vcpu *vcpu, gva_t addr,
			    int write_fault, int user_fault, int fetch_fault)
{
	hpa_t hpa;
	struct kvm_memory_slot *slot;
	pt_element_t *ptep;
	pt_element_t root;
	gfn_t table_gfn;

	pgprintk("%s: addr %lx\n", __FUNCTION__, addr);
	walker->level = vcpu->mmu.root_level;
	walker->table = NULL;
	root = vcpu->cr3;
#if PTTYPE == 64
	if (!is_long_mode(vcpu)) {
		walker->ptep = &vcpu->pdptrs[(addr >> 30) & 3];
		root = *walker->ptep;
		if (!(root & PT_PRESENT_MASK))
			goto not_present;
		--walker->level;
	}
#endif
	table_gfn = (root & PT64_BASE_ADDR_MASK) >> PAGE_SHIFT;
	walker->table_gfn[walker->level - 1] = table_gfn;
	pgprintk("%s: table_gfn[%d] %lx\n", __FUNCTION__,
		 walker->level - 1, table_gfn);
	slot = gfn_to_memslot(vcpu->kvm, table_gfn);
	hpa = safe_gpa_to_hpa(vcpu, root & PT64_BASE_ADDR_MASK);
	walker->table = kmap_atomic(pfn_to_page(hpa >> PAGE_SHIFT), KM_USER0);

	ASSERT((!is_long_mode(vcpu) && is_pae(vcpu)) ||
	       (vcpu->cr3 & ~(PAGE_MASK | CR3_FLAGS_MASK)) == 0);

	walker->inherited_ar = PT_USER_MASK | PT_WRITABLE_MASK;

	for (;;) {
		int index = PT_INDEX(addr, walker->level);
		hpa_t paddr;

		ptep = &walker->table[index];
		ASSERT(((unsigned long)walker->table & PAGE_MASK) ==
		       ((unsigned long)ptep & PAGE_MASK));

		if (!is_present_pte(*ptep))
			goto not_present;

		if (write_fault && !is_writeble_pte(*ptep))
			if (user_fault || is_write_protection(vcpu))
				goto access_error;

		if (user_fault && !(*ptep & PT_USER_MASK))
			goto access_error;

#if PTTYPE == 64
		if (fetch_fault && is_nx(vcpu) && (*ptep & PT64_NX_MASK))
			goto access_error;
#endif

		if (!(*ptep & PT_ACCESSED_MASK)) {
			mark_page_dirty(vcpu->kvm, table_gfn);
			*ptep |= PT_ACCESSED_MASK;
		}

		if (walker->level == PT_PAGE_TABLE_LEVEL) {
			walker->gfn = (*ptep & PT_BASE_ADDR_MASK)
				>> PAGE_SHIFT;
			break;
		}

		if (walker->level == PT_DIRECTORY_LEVEL
		    && (*ptep & PT_PAGE_SIZE_MASK)
		    && (PTTYPE == 64 || is_pse(vcpu))) {
			walker->gfn = (*ptep & PT_DIR_BASE_ADDR_MASK)
				>> PAGE_SHIFT;
			walker->gfn += PT_INDEX(addr, PT_PAGE_TABLE_LEVEL);
			break;
		}

		if (walker->level != 3 || is_long_mode(vcpu))
			walker->inherited_ar &= walker->table[index];
		table_gfn = (*ptep & PT_BASE_ADDR_MASK) >> PAGE_SHIFT;
		paddr = safe_gpa_to_hpa(vcpu, *ptep & PT_BASE_ADDR_MASK);
		kunmap_atomic(walker->table, KM_USER0);
		walker->table = kmap_atomic(pfn_to_page(paddr >> PAGE_SHIFT),
					    KM_USER0);
		--walker->level;
		walker->table_gfn[walker->level - 1 ] = table_gfn;
		pgprintk("%s: table_gfn[%d] %lx\n", __FUNCTION__,
			 walker->level - 1, table_gfn);
	}
	walker->ptep = ptep;
	pgprintk("%s: pte %llx\n", __FUNCTION__, (u64)*ptep);
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
	return 0;
}

static void FNAME(release_walker)(struct guest_walker *walker)
{
	if (walker->table)
		kunmap_atomic(walker->table, KM_USER0);
}

static void FNAME(mark_pagetable_dirty)(struct kvm *kvm,
					struct guest_walker *walker)
{
	mark_page_dirty(kvm, walker->table_gfn[walker->level - 1]);
}

static void FNAME(set_pte)(struct kvm_vcpu *vcpu, u64 guest_pte,
			   u64 *shadow_pte, u64 access_bits, gfn_t gfn)
{
	ASSERT(*shadow_pte == 0);
	access_bits &= guest_pte;
	*shadow_pte = (guest_pte & PT_PTE_COPY_MASK);
	set_pte_common(vcpu, shadow_pte, guest_pte & PT_BASE_ADDR_MASK,
		       guest_pte & PT_DIRTY_MASK, access_bits, gfn);
}

static void FNAME(set_pde)(struct kvm_vcpu *vcpu, u64 guest_pde,
			   u64 *shadow_pte, u64 access_bits, gfn_t gfn)
{
	gpa_t gaddr;

	ASSERT(*shadow_pte == 0);
	access_bits &= guest_pde;
	gaddr = (gpa_t)gfn << PAGE_SHIFT;
	if (PTTYPE == 32 && is_cpuid_PSE36())
		gaddr |= (guest_pde & PT32_DIR_PSE36_MASK) <<
			(32 - PT32_DIR_PSE36_SHIFT);
	*shadow_pte = guest_pde & PT_PTE_COPY_MASK;
	set_pte_common(vcpu, shadow_pte, gaddr,
		       guest_pde & PT_DIRTY_MASK, access_bits, gfn);
}

/*
 * Fetch a shadow pte for a specific level in the paging hierarchy.
 */
static u64 *FNAME(fetch)(struct kvm_vcpu *vcpu, gva_t addr,
			      struct guest_walker *walker)
{
	hpa_t shadow_addr;
	int level;
	u64 *prev_shadow_ent = NULL;
	pt_element_t *guest_ent = walker->ptep;

	if (!is_present_pte(*guest_ent))
		return NULL;

	shadow_addr = vcpu->mmu.root_hpa;
	level = vcpu->mmu.shadow_root_level;
	if (level == PT32E_ROOT_LEVEL) {
		shadow_addr = vcpu->mmu.pae_root[(addr >> 30) & 3];
		shadow_addr &= PT64_BASE_ADDR_MASK;
		--level;
	}

	for (; ; level--) {
		u32 index = SHADOW_PT_INDEX(addr, level);
		u64 *shadow_ent = ((u64 *)__va(shadow_addr)) + index;
		struct kvm_mmu_page *shadow_page;
		u64 shadow_pte;
		int metaphysical;
		gfn_t table_gfn;

		if (is_present_pte(*shadow_ent) || is_io_pte(*shadow_ent)) {
			if (level == PT_PAGE_TABLE_LEVEL)
				return shadow_ent;
			shadow_addr = *shadow_ent & PT64_BASE_ADDR_MASK;
			prev_shadow_ent = shadow_ent;
			continue;
		}

		if (level == PT_PAGE_TABLE_LEVEL) {

			if (walker->level == PT_DIRECTORY_LEVEL) {
				if (prev_shadow_ent)
					*prev_shadow_ent |= PT_SHADOW_PS_MARK;
				FNAME(set_pde)(vcpu, *guest_ent, shadow_ent,
					       walker->inherited_ar,
					       walker->gfn);
			} else {
				ASSERT(walker->level == PT_PAGE_TABLE_LEVEL);
				FNAME(set_pte)(vcpu, *guest_ent, shadow_ent,
					       walker->inherited_ar,
					       walker->gfn);
			}
			return shadow_ent;
		}

		if (level - 1 == PT_PAGE_TABLE_LEVEL
		    && walker->level == PT_DIRECTORY_LEVEL) {
			metaphysical = 1;
			table_gfn = (*guest_ent & PT_BASE_ADDR_MASK)
				>> PAGE_SHIFT;
		} else {
			metaphysical = 0;
			table_gfn = walker->table_gfn[level - 2];
		}
		shadow_page = kvm_mmu_get_page(vcpu, table_gfn, addr, level-1,
					       metaphysical, shadow_ent);
		shadow_addr = shadow_page->page_hpa;
		shadow_pte = shadow_addr | PT_PRESENT_MASK | PT_ACCESSED_MASK
			| PT_WRITABLE_MASK | PT_USER_MASK;
		*shadow_ent = shadow_pte;
		prev_shadow_ent = shadow_ent;
	}
}

/*
 * The guest faulted for write.  We need to
 *
 * - check write permissions
 * - update the guest pte dirty bit
 * - update our own dirty page tracking structures
 */
static int FNAME(fix_write_pf)(struct kvm_vcpu *vcpu,
			       u64 *shadow_ent,
			       struct guest_walker *walker,
			       gva_t addr,
			       int user,
			       int *write_pt)
{
	pt_element_t *guest_ent;
	int writable_shadow;
	gfn_t gfn;
	struct kvm_mmu_page *page;

	if (is_writeble_pte(*shadow_ent))
		return !user || (*shadow_ent & PT_USER_MASK);

	writable_shadow = *shadow_ent & PT_SHADOW_WRITABLE_MASK;
	if (user) {
		/*
		 * User mode access.  Fail if it's a kernel page or a read-only
		 * page.
		 */
		if (!(*shadow_ent & PT_SHADOW_USER_MASK) || !writable_shadow)
			return 0;
		ASSERT(*shadow_ent & PT_USER_MASK);
	} else
		/*
		 * Kernel mode access.  Fail if it's a read-only page and
		 * supervisor write protection is enabled.
		 */
		if (!writable_shadow) {
			if (is_write_protection(vcpu))
				return 0;
			*shadow_ent &= ~PT_USER_MASK;
		}

	guest_ent = walker->ptep;

	if (!is_present_pte(*guest_ent)) {
		*shadow_ent = 0;
		return 0;
	}

	gfn = walker->gfn;

	if (user) {
		/*
		 * Usermode page faults won't be for page table updates.
		 */
		while ((page = kvm_mmu_lookup_page(vcpu, gfn)) != NULL) {
			pgprintk("%s: zap %lx %x\n",
				 __FUNCTION__, gfn, page->role.word);
			kvm_mmu_zap_page(vcpu, page);
		}
	} else if (kvm_mmu_lookup_page(vcpu, gfn)) {
		pgprintk("%s: found shadow page for %lx, marking ro\n",
			 __FUNCTION__, gfn);
		mark_page_dirty(vcpu->kvm, gfn);
		FNAME(mark_pagetable_dirty)(vcpu->kvm, walker);
		*guest_ent |= PT_DIRTY_MASK;
		*write_pt = 1;
		return 0;
	}
	mark_page_dirty(vcpu->kvm, gfn);
	*shadow_ent |= PT_WRITABLE_MASK;
	FNAME(mark_pagetable_dirty)(vcpu->kvm, walker);
	*guest_ent |= PT_DIRTY_MASK;
	rmap_add(vcpu, shadow_ent);

	return 1;
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
	u64 *shadow_pte;
	int fixed;
	int write_pt = 0;
	int r;

	pgprintk("%s: addr %lx err %x\n", __FUNCTION__, addr, error_code);
	kvm_mmu_audit(vcpu, "pre page fault");

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	/*
	 * Look up the shadow pte for the faulting address.
	 */
	r = FNAME(walk_addr)(&walker, vcpu, addr, write_fault, user_fault,
			     fetch_fault);

	/*
	 * The page is not mapped by the guest.  Let the guest handle it.
	 */
	if (!r) {
		pgprintk("%s: guest page fault\n", __FUNCTION__);
		inject_page_fault(vcpu, addr, walker.error_code);
		FNAME(release_walker)(&walker);
		return 0;
	}

	shadow_pte = FNAME(fetch)(vcpu, addr, &walker);
	pgprintk("%s: shadow pte %p %llx\n", __FUNCTION__,
		 shadow_pte, *shadow_pte);

	/*
	 * Update the shadow pte.
	 */
	if (write_fault)
		fixed = FNAME(fix_write_pf)(vcpu, shadow_pte, &walker, addr,
					    user_fault, &write_pt);
	else
		fixed = fix_read_pf(shadow_pte);

	pgprintk("%s: updated shadow pte %p %llx\n", __FUNCTION__,
		 shadow_pte, *shadow_pte);

	FNAME(release_walker)(&walker);

	/*
	 * mmio: emulate if accessible, otherwise its a guest fault.
	 */
	if (is_io_pte(*shadow_pte))
		return 1;

	++kvm_stat.pf_fixed;
	kvm_mmu_audit(vcpu, "post page fault (fixed)");

	return write_pt;
}

static gpa_t FNAME(gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t vaddr)
{
	struct guest_walker walker;
	gpa_t gpa = UNMAPPED_GVA;
	int r;

	r = FNAME(walk_addr)(&walker, vcpu, vaddr, 0, 0, 0);

	if (r) {
		gpa = (gpa_t)walker.gfn << PAGE_SHIFT;
		gpa |= vaddr & ~PAGE_MASK;
	}

	FNAME(release_walker)(&walker);
	return gpa;
}

#undef pt_element_t
#undef guest_walker
#undef FNAME
#undef PT_BASE_ADDR_MASK
#undef PT_INDEX
#undef SHADOW_PT_INDEX
#undef PT_LEVEL_MASK
#undef PT_PTE_COPY_MASK
#undef PT_NON_PTE_COPY_MASK
#undef PT_DIR_BASE_ADDR_MASK
#undef PT_MAX_FULL_LEVELS
