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
#include <linux/types.h>
#include <linux/string.h>
#include <asm/page.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/module.h>

#include "vmx.h"
#include "kvm.h"

#define pgprintk(x...) do { } while (0)

#define ASSERT(x)							\
	if (!(x)) {							\
		printk(KERN_WARNING "assertion failed %s:%d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
	}

#define PT64_ENT_PER_PAGE 512
#define PT32_ENT_PER_PAGE 1024

#define PT_WRITABLE_SHIFT 1

#define PT_PRESENT_MASK (1ULL << 0)
#define PT_WRITABLE_MASK (1ULL << PT_WRITABLE_SHIFT)
#define PT_USER_MASK (1ULL << 2)
#define PT_PWT_MASK (1ULL << 3)
#define PT_PCD_MASK (1ULL << 4)
#define PT_ACCESSED_MASK (1ULL << 5)
#define PT_DIRTY_MASK (1ULL << 6)
#define PT_PAGE_SIZE_MASK (1ULL << 7)
#define PT_PAT_MASK (1ULL << 7)
#define PT_GLOBAL_MASK (1ULL << 8)
#define PT64_NX_MASK (1ULL << 63)

#define PT_PAT_SHIFT 7
#define PT_DIR_PAT_SHIFT 12
#define PT_DIR_PAT_MASK (1ULL << PT_DIR_PAT_SHIFT)

#define PT32_DIR_PSE36_SIZE 4
#define PT32_DIR_PSE36_SHIFT 13
#define PT32_DIR_PSE36_MASK (((1ULL << PT32_DIR_PSE36_SIZE) - 1) << PT32_DIR_PSE36_SHIFT)


#define PT32_PTE_COPY_MASK \
	(PT_PRESENT_MASK | PT_PWT_MASK | PT_PCD_MASK | \
	PT_ACCESSED_MASK | PT_DIRTY_MASK | PT_PAT_MASK | \
	PT_GLOBAL_MASK )

#define PT32_NON_PTE_COPY_MASK \
	(PT_PRESENT_MASK | PT_PWT_MASK | PT_PCD_MASK | \
	PT_ACCESSED_MASK | PT_DIRTY_MASK)


#define PT64_PTE_COPY_MASK \
	(PT64_NX_MASK | PT32_PTE_COPY_MASK)

#define PT64_NON_PTE_COPY_MASK \
	(PT64_NX_MASK | PT32_NON_PTE_COPY_MASK)



#define PT_FIRST_AVAIL_BITS_SHIFT 9
#define PT64_SECOND_AVAIL_BITS_SHIFT 52

#define PT_SHADOW_PS_MARK (1ULL << PT_FIRST_AVAIL_BITS_SHIFT)
#define PT_SHADOW_IO_MARK (1ULL << PT_FIRST_AVAIL_BITS_SHIFT)

#define PT_SHADOW_WRITABLE_SHIFT (PT_FIRST_AVAIL_BITS_SHIFT + 1)
#define PT_SHADOW_WRITABLE_MASK (1ULL << PT_SHADOW_WRITABLE_SHIFT)

#define PT_SHADOW_USER_SHIFT (PT_SHADOW_WRITABLE_SHIFT + 1)
#define PT_SHADOW_USER_MASK (1ULL << (PT_SHADOW_USER_SHIFT))

#define PT_SHADOW_BITS_OFFSET (PT_SHADOW_WRITABLE_SHIFT - PT_WRITABLE_SHIFT)

#define VALID_PAGE(x) ((x) != INVALID_PAGE)

#define PT64_LEVEL_BITS 9

#define PT64_LEVEL_SHIFT(level) \
		( PAGE_SHIFT + (level - 1) * PT64_LEVEL_BITS )

#define PT64_LEVEL_MASK(level) \
		(((1ULL << PT64_LEVEL_BITS) - 1) << PT64_LEVEL_SHIFT(level))

#define PT64_INDEX(address, level)\
	(((address) >> PT64_LEVEL_SHIFT(level)) & ((1 << PT64_LEVEL_BITS) - 1))


#define PT32_LEVEL_BITS 10

#define PT32_LEVEL_SHIFT(level) \
		( PAGE_SHIFT + (level - 1) * PT32_LEVEL_BITS )

#define PT32_LEVEL_MASK(level) \
		(((1ULL << PT32_LEVEL_BITS) - 1) << PT32_LEVEL_SHIFT(level))

#define PT32_INDEX(address, level)\
	(((address) >> PT32_LEVEL_SHIFT(level)) & ((1 << PT32_LEVEL_BITS) - 1))


#define PT64_BASE_ADDR_MASK (((1ULL << 52) - 1) & PAGE_MASK)
#define PT64_DIR_BASE_ADDR_MASK \
	(PT64_BASE_ADDR_MASK & ~((1ULL << (PAGE_SHIFT + PT64_LEVEL_BITS)) - 1))

#define PT32_BASE_ADDR_MASK PAGE_MASK
#define PT32_DIR_BASE_ADDR_MASK \
	(PAGE_MASK & ~((1ULL << (PAGE_SHIFT + PT32_LEVEL_BITS)) - 1))


#define PFERR_PRESENT_MASK (1U << 0)
#define PFERR_WRITE_MASK (1U << 1)
#define PFERR_USER_MASK (1U << 2)

#define PT64_ROOT_LEVEL 4
#define PT32_ROOT_LEVEL 2
#define PT32E_ROOT_LEVEL 3

#define PT_DIRECTORY_LEVEL 2
#define PT_PAGE_TABLE_LEVEL 1

static int is_write_protection(struct kvm_vcpu *vcpu)
{
	return vcpu->cr0 & CR0_WP_MASK;
}

static int is_cpuid_PSE36(void)
{
	return 1;
}

static int is_present_pte(unsigned long pte)
{
	return pte & PT_PRESENT_MASK;
}

static int is_writeble_pte(unsigned long pte)
{
	return pte & PT_WRITABLE_MASK;
}

static int is_io_pte(unsigned long pte)
{
	return pte & PT_SHADOW_IO_MARK;
}

static void kvm_mmu_free_page(struct kvm_vcpu *vcpu, hpa_t page_hpa)
{
	struct kvm_mmu_page *page_head = page_header(page_hpa);

	list_del(&page_head->link);
	page_head->page_hpa = page_hpa;
	list_add(&page_head->link, &vcpu->free_pages);
}

static int is_empty_shadow_page(hpa_t page_hpa)
{
	u32 *pos;
	u32 *end;
	for (pos = __va(page_hpa), end = pos + PAGE_SIZE / sizeof(u32);
		      pos != end; pos++)
		if (*pos != 0)
			return 0;
	return 1;
}

static hpa_t kvm_mmu_alloc_page(struct kvm_vcpu *vcpu, u64 *parent_pte)
{
	struct kvm_mmu_page *page;

	if (list_empty(&vcpu->free_pages))
		return INVALID_PAGE;

	page = list_entry(vcpu->free_pages.next, struct kvm_mmu_page, link);
	list_del(&page->link);
	list_add(&page->link, &vcpu->kvm->active_mmu_pages);
	ASSERT(is_empty_shadow_page(page->page_hpa));
	page->slot_bitmap = 0;
	page->global = 1;
	page->parent_pte = parent_pte;
	return page->page_hpa;
}

static void page_header_update_slot(struct kvm *kvm, void *pte, gpa_t gpa)
{
	int slot = memslot_id(kvm, gfn_to_memslot(kvm, gpa >> PAGE_SHIFT));
	struct kvm_mmu_page *page_head = page_header(__pa(pte));

	__set_bit(slot, &page_head->slot_bitmap);
}

hpa_t safe_gpa_to_hpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	hpa_t hpa = gpa_to_hpa(vcpu, gpa);

	return is_error_hpa(hpa) ? bad_page_address | (gpa & ~PAGE_MASK): hpa;
}

hpa_t gpa_to_hpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	struct kvm_memory_slot *slot;
	struct page *page;

	ASSERT((gpa & HPA_ERR_MASK) == 0);
	slot = gfn_to_memslot(vcpu->kvm, gpa >> PAGE_SHIFT);
	if (!slot)
		return gpa | HPA_ERR_MASK;
	page = gfn_to_page(slot, gpa >> PAGE_SHIFT);
	return ((hpa_t)page_to_pfn(page) << PAGE_SHIFT)
		| (gpa & (PAGE_SIZE-1));
}

hpa_t gva_to_hpa(struct kvm_vcpu *vcpu, gva_t gva)
{
	gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, gva);

	if (gpa == UNMAPPED_GVA)
		return UNMAPPED_GVA;
	return gpa_to_hpa(vcpu, gpa);
}


static void release_pt_page_64(struct kvm_vcpu *vcpu, hpa_t page_hpa,
			       int level)
{
	ASSERT(vcpu);
	ASSERT(VALID_PAGE(page_hpa));
	ASSERT(level <= PT64_ROOT_LEVEL && level > 0);

	if (level == 1)
		memset(__va(page_hpa), 0, PAGE_SIZE);
	else {
		u64 *pos;
		u64 *end;

		for (pos = __va(page_hpa), end = pos + PT64_ENT_PER_PAGE;
		     pos != end; pos++) {
			u64 current_ent = *pos;

			*pos = 0;
			if (is_present_pte(current_ent))
				release_pt_page_64(vcpu,
						  current_ent &
						  PT64_BASE_ADDR_MASK,
						  level - 1);
		}
	}
	kvm_mmu_free_page(vcpu, page_hpa);
}

static void nonpaging_new_cr3(struct kvm_vcpu *vcpu)
{
}

static int nonpaging_map(struct kvm_vcpu *vcpu, gva_t v, hpa_t p)
{
	int level = PT32E_ROOT_LEVEL;
	hpa_t table_addr = vcpu->mmu.root_hpa;

	for (; ; level--) {
		u32 index = PT64_INDEX(v, level);
		u64 *table;

		ASSERT(VALID_PAGE(table_addr));
		table = __va(table_addr);

		if (level == 1) {
			mark_page_dirty(vcpu->kvm, v >> PAGE_SHIFT);
			page_header_update_slot(vcpu->kvm, table, v);
			table[index] = p | PT_PRESENT_MASK | PT_WRITABLE_MASK |
								PT_USER_MASK;
			return 0;
		}

		if (table[index] == 0) {
			hpa_t new_table = kvm_mmu_alloc_page(vcpu,
							     &table[index]);

			if (!VALID_PAGE(new_table)) {
				pgprintk("nonpaging_map: ENOMEM\n");
				return -ENOMEM;
			}

			if (level == PT32E_ROOT_LEVEL)
				table[index] = new_table | PT_PRESENT_MASK;
			else
				table[index] = new_table | PT_PRESENT_MASK |
						PT_WRITABLE_MASK | PT_USER_MASK;
		}
		table_addr = table[index] & PT64_BASE_ADDR_MASK;
	}
}

static void nonpaging_flush(struct kvm_vcpu *vcpu)
{
	hpa_t root = vcpu->mmu.root_hpa;

	++kvm_stat.tlb_flush;
	pgprintk("nonpaging_flush\n");
	ASSERT(VALID_PAGE(root));
	release_pt_page_64(vcpu, root, vcpu->mmu.shadow_root_level);
	root = kvm_mmu_alloc_page(vcpu, NULL);
	ASSERT(VALID_PAGE(root));
	vcpu->mmu.root_hpa = root;
	if (is_paging(vcpu))
		root |= (vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK));
	kvm_arch_ops->set_cr3(vcpu, root);
	kvm_arch_ops->tlb_flush(vcpu);
}

static gpa_t nonpaging_gva_to_gpa(struct kvm_vcpu *vcpu, gva_t vaddr)
{
	return vaddr;
}

static int nonpaging_page_fault(struct kvm_vcpu *vcpu, gva_t gva,
			       u32 error_code)
{
	int ret;
	gpa_t addr = gva;

	ASSERT(vcpu);
	ASSERT(VALID_PAGE(vcpu->mmu.root_hpa));

	for (;;) {
	     hpa_t paddr;

	     paddr = gpa_to_hpa(vcpu , addr & PT64_BASE_ADDR_MASK);

	     if (is_error_hpa(paddr))
		     return 1;

	     ret = nonpaging_map(vcpu, addr & PAGE_MASK, paddr);
	     if (ret) {
		     nonpaging_flush(vcpu);
		     continue;
	     }
	     break;
	}
	return ret;
}

static void nonpaging_inval_page(struct kvm_vcpu *vcpu, gva_t addr)
{
}

static void nonpaging_free(struct kvm_vcpu *vcpu)
{
	hpa_t root;

	ASSERT(vcpu);
	root = vcpu->mmu.root_hpa;
	if (VALID_PAGE(root))
		release_pt_page_64(vcpu, root, vcpu->mmu.shadow_root_level);
	vcpu->mmu.root_hpa = INVALID_PAGE;
}

static int nonpaging_init_context(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->mmu;

	context->new_cr3 = nonpaging_new_cr3;
	context->page_fault = nonpaging_page_fault;
	context->inval_page = nonpaging_inval_page;
	context->gva_to_gpa = nonpaging_gva_to_gpa;
	context->free = nonpaging_free;
	context->root_level = PT32E_ROOT_LEVEL;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = kvm_mmu_alloc_page(vcpu, NULL);
	ASSERT(VALID_PAGE(context->root_hpa));
	kvm_arch_ops->set_cr3(vcpu, context->root_hpa);
	return 0;
}


static void kvm_mmu_flush_tlb(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *page, *npage;

	list_for_each_entry_safe(page, npage, &vcpu->kvm->active_mmu_pages,
				 link) {
		if (page->global)
			continue;

		if (!page->parent_pte)
			continue;

		*page->parent_pte = 0;
		release_pt_page_64(vcpu, page->page_hpa, 1);
	}
	++kvm_stat.tlb_flush;
	kvm_arch_ops->tlb_flush(vcpu);
}

static void paging_new_cr3(struct kvm_vcpu *vcpu)
{
	kvm_mmu_flush_tlb(vcpu);
}

static void mark_pagetable_nonglobal(void *shadow_pte)
{
	page_header(__pa(shadow_pte))->global = 0;
}

static inline void set_pte_common(struct kvm_vcpu *vcpu,
			     u64 *shadow_pte,
			     gpa_t gaddr,
			     int dirty,
			     u64 access_bits)
{
	hpa_t paddr;

	*shadow_pte |= access_bits << PT_SHADOW_BITS_OFFSET;
	if (!dirty)
		access_bits &= ~PT_WRITABLE_MASK;

	if (access_bits & PT_WRITABLE_MASK)
		mark_page_dirty(vcpu->kvm, gaddr >> PAGE_SHIFT);

	*shadow_pte |= access_bits;

	paddr = gpa_to_hpa(vcpu, gaddr & PT64_BASE_ADDR_MASK);

	if (!(*shadow_pte & PT_GLOBAL_MASK))
		mark_pagetable_nonglobal(shadow_pte);

	if (is_error_hpa(paddr)) {
		*shadow_pte |= gaddr;
		*shadow_pte |= PT_SHADOW_IO_MARK;
		*shadow_pte &= ~PT_PRESENT_MASK;
	} else {
		*shadow_pte |= paddr;
		page_header_update_slot(vcpu->kvm, shadow_pte, gaddr);
	}
}

static void inject_page_fault(struct kvm_vcpu *vcpu,
			      u64 addr,
			      u32 err_code)
{
	kvm_arch_ops->inject_page_fault(vcpu, addr, err_code);
}

static inline int fix_read_pf(u64 *shadow_ent)
{
	if ((*shadow_ent & PT_SHADOW_USER_MASK) &&
	    !(*shadow_ent & PT_USER_MASK)) {
		/*
		 * If supervisor write protect is disabled, we shadow kernel
		 * pages as user pages so we can trap the write access.
		 */
		*shadow_ent |= PT_USER_MASK;
		*shadow_ent &= ~PT_WRITABLE_MASK;

		return 1;

	}
	return 0;
}

static int may_access(u64 pte, int write, int user)
{

	if (user && !(pte & PT_USER_MASK))
		return 0;
	if (write && !(pte & PT_WRITABLE_MASK))
		return 0;
	return 1;
}

/*
 * Remove a shadow pte.
 */
static void paging_inval_page(struct kvm_vcpu *vcpu, gva_t addr)
{
	hpa_t page_addr = vcpu->mmu.root_hpa;
	int level = vcpu->mmu.shadow_root_level;

	++kvm_stat.invlpg;

	for (; ; level--) {
		u32 index = PT64_INDEX(addr, level);
		u64 *table = __va(page_addr);

		if (level == PT_PAGE_TABLE_LEVEL ) {
			table[index] = 0;
			return;
		}

		if (!is_present_pte(table[index]))
			return;

		page_addr = table[index] & PT64_BASE_ADDR_MASK;

		if (level == PT_DIRECTORY_LEVEL &&
			  (table[index] & PT_SHADOW_PS_MARK)) {
			table[index] = 0;
			release_pt_page_64(vcpu, page_addr, PT_PAGE_TABLE_LEVEL);

			kvm_arch_ops->tlb_flush(vcpu);
			return;
		}
	}
}

static void paging_free(struct kvm_vcpu *vcpu)
{
	nonpaging_free(vcpu);
}

#define PTTYPE 64
#include "paging_tmpl.h"
#undef PTTYPE

#define PTTYPE 32
#include "paging_tmpl.h"
#undef PTTYPE

static int paging64_init_context(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->mmu;

	ASSERT(is_pae(vcpu));
	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging64_page_fault;
	context->inval_page = paging_inval_page;
	context->gva_to_gpa = paging64_gva_to_gpa;
	context->free = paging_free;
	context->root_level = PT64_ROOT_LEVEL;
	context->shadow_root_level = PT64_ROOT_LEVEL;
	context->root_hpa = kvm_mmu_alloc_page(vcpu, NULL);
	ASSERT(VALID_PAGE(context->root_hpa));
	kvm_arch_ops->set_cr3(vcpu, context->root_hpa |
		    (vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK)));
	return 0;
}

static int paging32_init_context(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu *context = &vcpu->mmu;

	context->new_cr3 = paging_new_cr3;
	context->page_fault = paging32_page_fault;
	context->inval_page = paging_inval_page;
	context->gva_to_gpa = paging32_gva_to_gpa;
	context->free = paging_free;
	context->root_level = PT32_ROOT_LEVEL;
	context->shadow_root_level = PT32E_ROOT_LEVEL;
	context->root_hpa = kvm_mmu_alloc_page(vcpu, NULL);
	ASSERT(VALID_PAGE(context->root_hpa));
	kvm_arch_ops->set_cr3(vcpu, context->root_hpa |
		    (vcpu->cr3 & (CR3_PCD_MASK | CR3_WPT_MASK)));
	return 0;
}

static int paging32E_init_context(struct kvm_vcpu *vcpu)
{
	int ret;

	if ((ret = paging64_init_context(vcpu)))
		return ret;

	vcpu->mmu.root_level = PT32E_ROOT_LEVEL;
	vcpu->mmu.shadow_root_level = PT32E_ROOT_LEVEL;
	return 0;
}

static int init_kvm_mmu(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->mmu.root_hpa));

	if (!is_paging(vcpu))
		return nonpaging_init_context(vcpu);
	else if (kvm_arch_ops->is_long_mode(vcpu))
		return paging64_init_context(vcpu);
	else if (is_pae(vcpu))
		return paging32E_init_context(vcpu);
	else
		return paging32_init_context(vcpu);
}

static void destroy_kvm_mmu(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);
	if (VALID_PAGE(vcpu->mmu.root_hpa)) {
		vcpu->mmu.free(vcpu);
		vcpu->mmu.root_hpa = INVALID_PAGE;
	}
}

int kvm_mmu_reset_context(struct kvm_vcpu *vcpu)
{
	destroy_kvm_mmu(vcpu);
	return init_kvm_mmu(vcpu);
}

static void free_mmu_pages(struct kvm_vcpu *vcpu)
{
	while (!list_empty(&vcpu->free_pages)) {
		struct kvm_mmu_page *page;

		page = list_entry(vcpu->free_pages.next,
				  struct kvm_mmu_page, link);
		list_del(&page->link);
		__free_page(pfn_to_page(page->page_hpa >> PAGE_SHIFT));
		page->page_hpa = INVALID_PAGE;
	}
}

static int alloc_mmu_pages(struct kvm_vcpu *vcpu)
{
	int i;

	ASSERT(vcpu);

	for (i = 0; i < KVM_NUM_MMU_PAGES; i++) {
		struct page *page;
		struct kvm_mmu_page *page_header = &vcpu->page_header_buf[i];

		INIT_LIST_HEAD(&page_header->link);
		if ((page = alloc_page(GFP_KVM_MMU)) == NULL)
			goto error_1;
		page->private = (unsigned long)page_header;
		page_header->page_hpa = (hpa_t)page_to_pfn(page) << PAGE_SHIFT;
		memset(__va(page_header->page_hpa), 0, PAGE_SIZE);
		list_add(&page_header->link, &vcpu->free_pages);
	}
	return 0;

error_1:
	free_mmu_pages(vcpu);
	return -ENOMEM;
}

int kvm_mmu_init(struct kvm_vcpu *vcpu)
{
	int r;

	ASSERT(vcpu);
	ASSERT(!VALID_PAGE(vcpu->mmu.root_hpa));
	ASSERT(list_empty(&vcpu->free_pages));

	if ((r = alloc_mmu_pages(vcpu)))
		return r;

	if ((r = init_kvm_mmu(vcpu))) {
		free_mmu_pages(vcpu);
		return r;
	}
	return 0;
}

void kvm_mmu_destroy(struct kvm_vcpu *vcpu)
{
	ASSERT(vcpu);

	destroy_kvm_mmu(vcpu);
	free_mmu_pages(vcpu);
}

void kvm_mmu_slot_remove_write_access(struct kvm *kvm, int slot)
{
	struct kvm_mmu_page *page;

	list_for_each_entry(page, &kvm->active_mmu_pages, link) {
		int i;
		u64 *pt;

		if (!test_bit(slot, &page->slot_bitmap))
			continue;

		pt = __va(page->page_hpa);
		for (i = 0; i < PT64_ENT_PER_PAGE; ++i)
			/* avoid RMW */
			if (pt[i] & PT_WRITABLE_MASK)
				pt[i] &= ~PT_WRITABLE_MASK;

	}
}
