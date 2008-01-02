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
	#define PT_DIR_BASE_ADDR_MASK PT32_DIR_BASE_ADDR_MASK
	#define PT_INDEX(addr, level) PT32_INDEX(addr, level)
	#define SHADOW_PT_INDEX(addr, level) PT64_INDEX(addr, level)
	#define PT_LEVEL_MASK(level) PT32_LEVEL_MASK(level)
	#define PT_LEVEL_BITS PT32_LEVEL_BITS
	#define PT_MAX_FULL_LEVELS 2
	#define CMPXCHG cmpxchg
#else
	#error Invalid PTTYPE value
#endif

#define gpte_to_gfn FNAME(gpte_to_gfn)
#define gpte_to_gfn_pde FNAME(gpte_to_gfn_pde)

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

static gfn_t gpte_to_gfn(pt_element_t gpte)
{
	return (gpte & PT_BASE_ADDR_MASK) >> PAGE_SHIFT;
}

static gfn_t gpte_to_gfn_pde(pt_element_t gpte)
{
	return (gpte & PT_DIR_BASE_ADDR_MASK) >> PAGE_SHIFT;
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

	pgprintk("%s: addr %lx\n", __FUNCTION__, addr);
walk:
	walker->level = vcpu->arch.mmu.root_level;
	pte = vcpu->arch.cr3;
#if PTTYPE == 64
	if (!is_long_mode(vcpu)) {
		pte = vcpu->arch.pdptrs[(addr >> 30) & 3];
		if (!is_present_pte(pte))
			goto not_present;
		--walker->level;
	}
#endif
	ASSERT((!is_long_mode(vcpu) && is_pae(vcpu)) ||
	       (vcpu->cr3 & CR3_NONPAE_RESERVED_BITS) == 0);

	pt_access = ACC_ALL;

	for (;;) {
		index = PT_INDEX(addr, walker->level);

		table_gfn = gpte_to_gfn(pte);
		pte_gpa = gfn_to_gpa(table_gfn);
		pte_gpa += index * sizeof(pt_element_t);
		walker->table_gfn[walker->level - 1] = table_gfn;
		walker->pte_gpa[walker->level - 1] = pte_gpa;
		pgprintk("%s: table_gfn[%d] %lx\n", __FUNCTION__,
			 walker->level - 1, table_gfn);

		kvm_read_guest(vcpu->kvm, pte_gpa, &pte, sizeof(pte));

		if (!is_present_pte(pte))
			goto not_present;

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
			mark_page_dirty(vcpu->kvm, table_gfn);
			if (FNAME(cmpxchg_gpte)(vcpu->kvm, table_gfn,
			    index, pte, pte|PT_ACCESSED_MASK))
				goto walk;
			pte |= PT_ACCESSED_MASK;
		}

		pte_access = pt_access & FNAME(gpte_access)(vcpu, pte);

		walker->ptes[walker->level - 1] = pte;

		if (walker->level == PT_PAGE_TABLE_LEVEL) {
			walker->gfn = gpte_to_gfn(pte);
			break;
		}

		if (walker->level == PT_DIRECTORY_LEVEL
		    && (pte & PT_PAGE_SIZE_MASK)
		    && (PTTYPE == 64 || is_pse(vcpu))) {
			walker->gfn = gpte_to_gfn_pde(pte);
			walker->gfn += PT_INDEX(addr, PT_PAGE_TABLE_LEVEL);
			if (PTTYPE == 32 && is_cpuid_PSE36())
				walker->gfn += pse36_gfn_delta(pte);
			break;
		}

		pt_access = pte_access;
		--walker->level;
	}

	if (write_fault && !is_dirty_pte(pte)) {
		bool ret;

		mark_page_dirty(vcpu->kvm, table_gfn);
		ret = FNAME(cmpxchg_gpte)(vcpu->kvm, table_gfn, index, pte,
			    pte|PT_DIRTY_MASK);
		if (ret)
			goto walk;
		pte |= PT_DIRTY_MASK;
		kvm_mmu_pte_write(vcpu, pte_gpa, (u8 *)&pte, sizeof(pte));
		walker->ptes[walker->level - 1] = pte;
	}

	walker->pt_access = pt_access;
	walker->pte_access = pte_access;
	pgprintk("%s: pte %llx pte_access %x pt_access %x\n",
		 __FUNCTION__, (u64)pte, pt_access, pte_access);
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

static void FNAME(update_pte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *page,
			      u64 *spte, const void *pte, int bytes,
			      int offset_in_pte)
{
	pt_element_t gpte;
	unsigned pte_access;
	struct page *npage;

	gpte = *(const pt_element_t *)pte;
	if (~gpte & (PT_PRESENT_MASK | PT_ACCESSED_MASK)) {
		if (!offset_in_pte && !is_present_pte(gpte))
			set_shadow_pte(spte, shadow_notrap_nonpresent_pte);
		return;
	}
	if (bytes < sizeof(pt_element_t))
		return;
	pgprintk("%s: gpte %llx spte %p\n", __FUNCTION__, (u64)gpte, spte);
	pte_access = page->role.access & FNAME(gpte_access)(vcpu, gpte);
	if (gpte_to_gfn(gpte) != vcpu->arch.update_pte.gfn)
		return;
	npage = vcpu->arch.update_pte.page;
	if (!npage)
		return;
	get_page(npage);
	mmu_set_spte(vcpu, spte, page->role.access, pte_access, 0, 0,
		     gpte & PT_DIRTY_MASK, NULL, gpte_to_gfn(gpte), npage);
}

/*
 * Fetch a shadow pte for a specific level in the paging hierarchy.
 */
static u64 *FNAME(fetch)(struct kvm_vcpu *vcpu, gva_t addr,
			 struct guest_walker *walker,
			 int user_fault, int write_fault, int *ptwrite,
			 struct page *page)
{
	hpa_t shadow_addr;
	int level;
	u64 *shadow_ent;
	unsigned access = walker->pt_access;

	if (!is_present_pte(walker->ptes[walker->level - 1]))
		return NULL;

	shadow_addr = vcpu->arch.mmu.root_hpa;
	level = vcpu->arch.mmu.shadow_root_level;
	if (level == PT32E_ROOT_LEVEL) {
		shadow_addr = vcpu->arch.mmu.pae_root[(addr >> 30) & 3];
		shadow_addr &= PT64_BASE_ADDR_MASK;
		--level;
	}

	for (; ; level--) {
		u32 index = SHADOW_PT_INDEX(addr, level);
		struct kvm_mmu_page *shadow_page;
		u64 shadow_pte;
		int metaphysical;
		gfn_t table_gfn;
		bool new_page = 0;

		shadow_ent = ((u64 *)__va(shadow_addr)) + index;
		if (level == PT_PAGE_TABLE_LEVEL)
			break;
		if (is_shadow_present_pte(*shadow_ent)) {
			shadow_addr = *shadow_ent & PT64_BASE_ADDR_MASK;
			continue;
		}

		if (level - 1 == PT_PAGE_TABLE_LEVEL
		    && walker->level == PT_DIRECTORY_LEVEL) {
			metaphysical = 1;
			if (!is_dirty_pte(walker->ptes[level - 1]))
				access &= ~ACC_WRITE_MASK;
			table_gfn = gpte_to_gfn(walker->ptes[level - 1]);
		} else {
			metaphysical = 0;
			table_gfn = walker->table_gfn[level - 2];
		}
		shadow_page = kvm_mmu_get_page(vcpu, table_gfn, addr, level-1,
					       metaphysical, access,
					       shadow_ent, &new_page);
		if (new_page && !metaphysical) {
			int r;
			pt_element_t curr_pte;
			r = kvm_read_guest_atomic(vcpu->kvm,
						  walker->pte_gpa[level - 2],
						  &curr_pte, sizeof(curr_pte));
			if (r || curr_pte != walker->ptes[level - 2]) {
				kvm_release_page_clean(page);
				return NULL;
			}
		}
		shadow_addr = __pa(shadow_page->spt);
		shadow_pte = shadow_addr | PT_PRESENT_MASK | PT_ACCESSED_MASK
			| PT_WRITABLE_MASK | PT_USER_MASK;
		*shadow_ent = shadow_pte;
	}

	mmu_set_spte(vcpu, shadow_ent, access, walker->pte_access & access,
		     user_fault, write_fault,
		     walker->ptes[walker->level-1] & PT_DIRTY_MASK,
		     ptwrite, walker->gfn, page);

	return shadow_ent;
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
	int write_pt = 0;
	int r;
	struct page *page;

	pgprintk("%s: addr %lx err %x\n", __FUNCTION__, addr, error_code);
	kvm_mmu_audit(vcpu, "pre page fault");

	r = mmu_topup_memory_caches(vcpu);
	if (r)
		return r;

	down_read(&current->mm->mmap_sem);
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
		vcpu->arch.last_pt_write_count = 0; /* reset fork detector */
		up_read(&current->mm->mmap_sem);
		return 0;
	}

	page = gfn_to_page(vcpu->kvm, walker.gfn);

	spin_lock(&vcpu->kvm->mmu_lock);
	kvm_mmu_free_some_pages(vcpu);
	shadow_pte = FNAME(fetch)(vcpu, addr, &walker, user_fault, write_fault,
				  &write_pt, page);
	pgprintk("%s: shadow pte %p %llx ptwrite %d\n", __FUNCTION__,
		 shadow_pte, *shadow_pte, write_pt);

	if (!write_pt)
		vcpu->arch.last_pt_write_count = 0; /* reset fork detector */

	/*
	 * mmio: emulate if accessible, otherwise its a guest fault.
	 */
	if (shadow_pte && is_io_pte(*shadow_pte)) {
		spin_unlock(&vcpu->kvm->mmu_lock);
		up_read(&current->mm->mmap_sem);
		return 1;
	}

	++vcpu->stat.pf_fixed;
	kvm_mmu_audit(vcpu, "post page fault (fixed)");
	spin_unlock(&vcpu->kvm->mmu_lock);
	up_read(&current->mm->mmap_sem);

	return write_pt;
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
	int i, offset = 0, r = 0;
	pt_element_t pt;

	if (sp->role.metaphysical
	    || (PTTYPE == 32 && sp->role.level > PT_PAGE_TABLE_LEVEL)) {
		nonpaging_prefetch_page(vcpu, sp);
		return;
	}

	if (PTTYPE == 32)
		offset = sp->role.quadrant << PT64_LEVEL_BITS;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
		gpa_t pte_gpa = gfn_to_gpa(sp->gfn);
		pte_gpa += (i+offset) * sizeof(pt_element_t);

		r = kvm_read_guest_atomic(vcpu->kvm, pte_gpa, &pt,
					  sizeof(pt_element_t));
		if (r || is_present_pte(pt))
			sp->spt[i] = shadow_trap_nonpresent_pte;
		else
			sp->spt[i] = shadow_notrap_nonpresent_pte;
	}
}

#undef pt_element_t
#undef guest_walker
#undef FNAME
#undef PT_BASE_ADDR_MASK
#undef PT_INDEX
#undef SHADOW_PT_INDEX
#undef PT_LEVEL_MASK
#undef PT_DIR_BASE_ADDR_MASK
#undef PT_LEVEL_BITS
#undef PT_MAX_FULL_LEVELS
#undef gpte_to_gfn
#undef gpte_to_gfn_pde
#undef CMPXCHG
