// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google LLC
 * Author: Will Deacon <will@kernel.org>
 */

#ifndef __ARM64_KVM_PGTABLE_H__
#define __ARM64_KVM_PGTABLE_H__

#include <linux/bits.h>
#include <linux/kvm_host.h>
#include <linux/types.h>

#define KVM_PGTABLE_MAX_LEVELS		4U

/*
 * The largest supported block sizes for KVM (no 52-bit PA support):
 *  - 4K (level 1):	1GB
 *  - 16K (level 2):	32MB
 *  - 64K (level 2):	512MB
 */
#ifdef CONFIG_ARM64_4K_PAGES
#define KVM_PGTABLE_MIN_BLOCK_LEVEL	1U
#else
#define KVM_PGTABLE_MIN_BLOCK_LEVEL	2U
#endif

#define kvm_lpa2_is_enabled()		false

static inline u64 kvm_get_parange(u64 mmfr0)
{
	u64 parange = cpuid_feature_extract_unsigned_field(mmfr0,
				ID_AA64MMFR0_EL1_PARANGE_SHIFT);
	if (parange > ID_AA64MMFR0_EL1_PARANGE_MAX)
		parange = ID_AA64MMFR0_EL1_PARANGE_MAX;

	return parange;
}

typedef u64 kvm_pte_t;

#define KVM_PTE_VALID			BIT(0)

#define KVM_PTE_ADDR_MASK		GENMASK(47, PAGE_SHIFT)
#define KVM_PTE_ADDR_51_48		GENMASK(15, 12)

#define KVM_PHYS_INVALID		(-1ULL)

static inline bool kvm_pte_valid(kvm_pte_t pte)
{
	return pte & KVM_PTE_VALID;
}

static inline u64 kvm_pte_to_phys(kvm_pte_t pte)
{
	u64 pa = pte & KVM_PTE_ADDR_MASK;

	if (PAGE_SHIFT == 16)
		pa |= FIELD_GET(KVM_PTE_ADDR_51_48, pte) << 48;

	return pa;
}

static inline kvm_pte_t kvm_phys_to_pte(u64 pa)
{
	kvm_pte_t pte = pa & KVM_PTE_ADDR_MASK;

	if (PAGE_SHIFT == 16) {
		pa &= GENMASK(51, 48);
		pte |= FIELD_PREP(KVM_PTE_ADDR_51_48, pa >> 48);
	}

	return pte;
}

static inline kvm_pfn_t kvm_pte_to_pfn(kvm_pte_t pte)
{
	return __phys_to_pfn(kvm_pte_to_phys(pte));
}

static inline u64 kvm_granule_shift(u32 level)
{
	/* Assumes KVM_PGTABLE_MAX_LEVELS is 4 */
	return ARM64_HW_PGTABLE_LEVEL_SHIFT(level);
}

static inline u64 kvm_granule_size(u32 level)
{
	return BIT(kvm_granule_shift(level));
}

static inline bool kvm_level_supports_block_mapping(u32 level)
{
	return level >= KVM_PGTABLE_MIN_BLOCK_LEVEL;
}

static inline u32 kvm_supported_block_sizes(void)
{
	u32 level = KVM_PGTABLE_MIN_BLOCK_LEVEL;
	u32 r = 0;

	for (; level < KVM_PGTABLE_MAX_LEVELS; level++)
		r |= BIT(kvm_granule_shift(level));

	return r;
}

static inline bool kvm_is_block_size_supported(u64 size)
{
	bool is_power_of_two = IS_ALIGNED(size, size);

	return is_power_of_two && (size & kvm_supported_block_sizes());
}

/**
 * struct kvm_pgtable_mm_ops - Memory management callbacks.
 * @zalloc_page:		Allocate a single zeroed memory page.
 *				The @arg parameter can be used by the walker
 *				to pass a memcache. The initial refcount of
 *				the page is 1.
 * @zalloc_pages_exact:		Allocate an exact number of zeroed memory pages.
 *				The @size parameter is in bytes, and is rounded
 *				up to the next page boundary. The resulting
 *				allocation is physically contiguous.
 * @free_pages_exact:		Free an exact number of memory pages previously
 *				allocated by zalloc_pages_exact.
 * @free_unlinked_table:	Free an unlinked paging structure by unlinking and
 *				dropping references.
 * @get_page:			Increment the refcount on a page.
 * @put_page:			Decrement the refcount on a page. When the
 *				refcount reaches 0 the page is automatically
 *				freed.
 * @page_count:			Return the refcount of a page.
 * @phys_to_virt:		Convert a physical address into a virtual
 *				address	mapped in the current context.
 * @virt_to_phys:		Convert a virtual address mapped in the current
 *				context into a physical address.
 * @dcache_clean_inval_poc:	Clean and invalidate the data cache to the PoC
 *				for the	specified memory address range.
 * @icache_inval_pou:		Invalidate the instruction cache to the PoU
 *				for the specified memory address range.
 */
struct kvm_pgtable_mm_ops {
	void*		(*zalloc_page)(void *arg);
	void*		(*zalloc_pages_exact)(size_t size);
	void		(*free_pages_exact)(void *addr, size_t size);
	void		(*free_unlinked_table)(void *addr, u32 level);
	void		(*get_page)(void *addr);
	void		(*put_page)(void *addr);
	int		(*page_count)(void *addr);
	void*		(*phys_to_virt)(phys_addr_t phys);
	phys_addr_t	(*virt_to_phys)(void *addr);
	void		(*dcache_clean_inval_poc)(void *addr, size_t size);
	void		(*icache_inval_pou)(void *addr, size_t size);
};

/**
 * enum kvm_pgtable_stage2_flags - Stage-2 page-table flags.
 * @KVM_PGTABLE_S2_NOFWB:	Don't enforce Normal-WB even if the CPUs have
 *				ARM64_HAS_STAGE2_FWB.
 * @KVM_PGTABLE_S2_IDMAP:	Only use identity mappings.
 */
enum kvm_pgtable_stage2_flags {
	KVM_PGTABLE_S2_NOFWB			= BIT(0),
	KVM_PGTABLE_S2_IDMAP			= BIT(1),
};

/**
 * enum kvm_pgtable_prot - Page-table permissions and attributes.
 * @KVM_PGTABLE_PROT_X:		Execute permission.
 * @KVM_PGTABLE_PROT_W:		Write permission.
 * @KVM_PGTABLE_PROT_R:		Read permission.
 * @KVM_PGTABLE_PROT_DEVICE:	Device attributes.
 * @KVM_PGTABLE_PROT_SW0:	Software bit 0.
 * @KVM_PGTABLE_PROT_SW1:	Software bit 1.
 * @KVM_PGTABLE_PROT_SW2:	Software bit 2.
 * @KVM_PGTABLE_PROT_SW3:	Software bit 3.
 */
enum kvm_pgtable_prot {
	KVM_PGTABLE_PROT_X			= BIT(0),
	KVM_PGTABLE_PROT_W			= BIT(1),
	KVM_PGTABLE_PROT_R			= BIT(2),

	KVM_PGTABLE_PROT_DEVICE			= BIT(3),

	KVM_PGTABLE_PROT_SW0			= BIT(55),
	KVM_PGTABLE_PROT_SW1			= BIT(56),
	KVM_PGTABLE_PROT_SW2			= BIT(57),
	KVM_PGTABLE_PROT_SW3			= BIT(58),
};

#define KVM_PGTABLE_PROT_RW	(KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_W)
#define KVM_PGTABLE_PROT_RWX	(KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_X)

#define PKVM_HOST_MEM_PROT	KVM_PGTABLE_PROT_RWX
#define PKVM_HOST_MMIO_PROT	KVM_PGTABLE_PROT_RW

#define PAGE_HYP		KVM_PGTABLE_PROT_RW
#define PAGE_HYP_EXEC		(KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_X)
#define PAGE_HYP_RO		(KVM_PGTABLE_PROT_R)
#define PAGE_HYP_DEVICE		(PAGE_HYP | KVM_PGTABLE_PROT_DEVICE)

typedef bool (*kvm_pgtable_force_pte_cb_t)(u64 addr, u64 end,
					   enum kvm_pgtable_prot prot);

/**
 * enum kvm_pgtable_walk_flags - Flags to control a depth-first page-table walk.
 * @KVM_PGTABLE_WALK_LEAF:		Visit leaf entries, including invalid
 *					entries.
 * @KVM_PGTABLE_WALK_TABLE_PRE:		Visit table entries before their
 *					children.
 * @KVM_PGTABLE_WALK_TABLE_POST:	Visit table entries after their
 *					children.
 * @KVM_PGTABLE_WALK_SHARED:		Indicates the page-tables may be shared
 *					with other software walkers.
 * @KVM_PGTABLE_WALK_HANDLE_FAULT:	Indicates the page-table walk was
 *					invoked from a fault handler.
 * @KVM_PGTABLE_WALK_SKIP_BBM_TLBI:	Visit and update table entries
 *					without Break-before-make's
 *					TLB invalidation.
 * @KVM_PGTABLE_WALK_SKIP_CMO:		Visit and update table entries
 *					without Cache maintenance
 *					operations required.
 */
enum kvm_pgtable_walk_flags {
	KVM_PGTABLE_WALK_LEAF			= BIT(0),
	KVM_PGTABLE_WALK_TABLE_PRE		= BIT(1),
	KVM_PGTABLE_WALK_TABLE_POST		= BIT(2),
	KVM_PGTABLE_WALK_SHARED			= BIT(3),
	KVM_PGTABLE_WALK_HANDLE_FAULT		= BIT(4),
	KVM_PGTABLE_WALK_SKIP_BBM_TLBI		= BIT(5),
	KVM_PGTABLE_WALK_SKIP_CMO		= BIT(6),
};

struct kvm_pgtable_visit_ctx {
	kvm_pte_t				*ptep;
	kvm_pte_t				old;
	void					*arg;
	struct kvm_pgtable_mm_ops		*mm_ops;
	u64					start;
	u64					addr;
	u64					end;
	u32					level;
	enum kvm_pgtable_walk_flags		flags;
};

typedef int (*kvm_pgtable_visitor_fn_t)(const struct kvm_pgtable_visit_ctx *ctx,
					enum kvm_pgtable_walk_flags visit);

static inline bool kvm_pgtable_walk_shared(const struct kvm_pgtable_visit_ctx *ctx)
{
	return ctx->flags & KVM_PGTABLE_WALK_SHARED;
}

/**
 * struct kvm_pgtable_walker - Hook into a page-table walk.
 * @cb:		Callback function to invoke during the walk.
 * @arg:	Argument passed to the callback function.
 * @flags:	Bitwise-OR of flags to identify the entry types on which to
 *		invoke the callback function.
 */
struct kvm_pgtable_walker {
	const kvm_pgtable_visitor_fn_t		cb;
	void * const				arg;
	const enum kvm_pgtable_walk_flags	flags;
};

/*
 * RCU cannot be used in a non-kernel context such as the hyp. As such, page
 * table walkers used in hyp do not call into RCU and instead use other
 * synchronization mechanisms (such as a spinlock).
 */
#if defined(__KVM_NVHE_HYPERVISOR__) || defined(__KVM_VHE_HYPERVISOR__)

typedef kvm_pte_t *kvm_pteref_t;

static inline kvm_pte_t *kvm_dereference_pteref(struct kvm_pgtable_walker *walker,
						kvm_pteref_t pteref)
{
	return pteref;
}

static inline int kvm_pgtable_walk_begin(struct kvm_pgtable_walker *walker)
{
	/*
	 * Due to the lack of RCU (or a similar protection scheme), only
	 * non-shared table walkers are allowed in the hypervisor.
	 */
	if (walker->flags & KVM_PGTABLE_WALK_SHARED)
		return -EPERM;

	return 0;
}

static inline void kvm_pgtable_walk_end(struct kvm_pgtable_walker *walker) {}

static inline bool kvm_pgtable_walk_lock_held(void)
{
	return true;
}

#else

typedef kvm_pte_t __rcu *kvm_pteref_t;

static inline kvm_pte_t *kvm_dereference_pteref(struct kvm_pgtable_walker *walker,
						kvm_pteref_t pteref)
{
	return rcu_dereference_check(pteref, !(walker->flags & KVM_PGTABLE_WALK_SHARED));
}

static inline int kvm_pgtable_walk_begin(struct kvm_pgtable_walker *walker)
{
	if (walker->flags & KVM_PGTABLE_WALK_SHARED)
		rcu_read_lock();

	return 0;
}

static inline void kvm_pgtable_walk_end(struct kvm_pgtable_walker *walker)
{
	if (walker->flags & KVM_PGTABLE_WALK_SHARED)
		rcu_read_unlock();
}

static inline bool kvm_pgtable_walk_lock_held(void)
{
	return rcu_read_lock_held();
}

#endif

/**
 * struct kvm_pgtable - KVM page-table.
 * @ia_bits:		Maximum input address size, in bits.
 * @start_level:	Level at which the page-table walk starts.
 * @pgd:		Pointer to the first top-level entry of the page-table.
 * @mm_ops:		Memory management callbacks.
 * @mmu:		Stage-2 KVM MMU struct. Unused for stage-1 page-tables.
 * @flags:		Stage-2 page-table flags.
 * @force_pte_cb:	Function that returns true if page level mappings must
 *			be used instead of block mappings.
 */
struct kvm_pgtable {
	u32					ia_bits;
	u32					start_level;
	kvm_pteref_t				pgd;
	struct kvm_pgtable_mm_ops		*mm_ops;

	/* Stage-2 only */
	struct kvm_s2_mmu			*mmu;
	enum kvm_pgtable_stage2_flags		flags;
	kvm_pgtable_force_pte_cb_t		force_pte_cb;
};

/**
 * kvm_pgtable_hyp_init() - Initialise a hypervisor stage-1 page-table.
 * @pgt:	Uninitialised page-table structure to initialise.
 * @va_bits:	Maximum virtual address bits.
 * @mm_ops:	Memory management callbacks.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_hyp_init(struct kvm_pgtable *pgt, u32 va_bits,
			 struct kvm_pgtable_mm_ops *mm_ops);

/**
 * kvm_pgtable_hyp_destroy() - Destroy an unused hypervisor stage-1 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_hyp_init().
 *
 * The page-table is assumed to be unreachable by any hardware walkers prior
 * to freeing and therefore no TLB invalidation is performed.
 */
void kvm_pgtable_hyp_destroy(struct kvm_pgtable *pgt);

/**
 * kvm_pgtable_hyp_map() - Install a mapping in a hypervisor stage-1 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_hyp_init().
 * @addr:	Virtual address at which to place the mapping.
 * @size:	Size of the mapping.
 * @phys:	Physical address of the memory to map.
 * @prot:	Permissions and attributes for the mapping.
 *
 * The offset of @addr within a page is ignored, @size is rounded-up to
 * the next page boundary and @phys is rounded-down to the previous page
 * boundary.
 *
 * If device attributes are not explicitly requested in @prot, then the
 * mapping will be normal, cacheable. Attempts to install a new mapping
 * for a virtual address that is already mapped will be rejected with an
 * error and a WARN().
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_hyp_map(struct kvm_pgtable *pgt, u64 addr, u64 size, u64 phys,
			enum kvm_pgtable_prot prot);

/**
 * kvm_pgtable_hyp_unmap() - Remove a mapping from a hypervisor stage-1 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_hyp_init().
 * @addr:	Virtual address from which to remove the mapping.
 * @size:	Size of the mapping.
 *
 * The offset of @addr within a page is ignored, @size is rounded-up to
 * the next page boundary and @phys is rounded-down to the previous page
 * boundary.
 *
 * TLB invalidation is performed for each page-table entry cleared during the
 * unmapping operation and the reference count for the page-table page
 * containing the cleared entry is decremented, with unreferenced pages being
 * freed. The unmapping operation will stop early if it encounters either an
 * invalid page-table entry or a valid block mapping which maps beyond the range
 * being unmapped.
 *
 * Return: Number of bytes unmapped, which may be 0.
 */
u64 kvm_pgtable_hyp_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size);

/**
 * kvm_get_vtcr() - Helper to construct VTCR_EL2
 * @mmfr0:	Sanitized value of SYS_ID_AA64MMFR0_EL1 register.
 * @mmfr1:	Sanitized value of SYS_ID_AA64MMFR1_EL1 register.
 * @phys_shfit:	Value to set in VTCR_EL2.T0SZ.
 *
 * The VTCR value is common across all the physical CPUs on the system.
 * We use system wide sanitised values to fill in different fields,
 * except for Hardware Management of Access Flags. HA Flag is set
 * unconditionally on all CPUs, as it is safe to run with or without
 * the feature and the bit is RES0 on CPUs that don't support it.
 *
 * Return: VTCR_EL2 value
 */
u64 kvm_get_vtcr(u64 mmfr0, u64 mmfr1, u32 phys_shift);

/**
 * kvm_pgtable_stage2_pgd_size() - Helper to compute size of a stage-2 PGD
 * @vtcr:	Content of the VTCR register.
 *
 * Return: the size (in bytes) of the stage-2 PGD
 */
size_t kvm_pgtable_stage2_pgd_size(u64 vtcr);

/**
 * __kvm_pgtable_stage2_init() - Initialise a guest stage-2 page-table.
 * @pgt:	Uninitialised page-table structure to initialise.
 * @mmu:	S2 MMU context for this S2 translation
 * @mm_ops:	Memory management callbacks.
 * @flags:	Stage-2 configuration flags.
 * @force_pte_cb: Function that returns true if page level mappings must
 *		be used instead of block mappings.
 *
 * Return: 0 on success, negative error code on failure.
 */
int __kvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm_s2_mmu *mmu,
			      struct kvm_pgtable_mm_ops *mm_ops,
			      enum kvm_pgtable_stage2_flags flags,
			      kvm_pgtable_force_pte_cb_t force_pte_cb);

#define kvm_pgtable_stage2_init(pgt, mmu, mm_ops) \
	__kvm_pgtable_stage2_init(pgt, mmu, mm_ops, 0, NULL)

/**
 * kvm_pgtable_stage2_destroy() - Destroy an unused guest stage-2 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 *
 * The page-table is assumed to be unreachable by any hardware walkers prior
 * to freeing and therefore no TLB invalidation is performed.
 */
void kvm_pgtable_stage2_destroy(struct kvm_pgtable *pgt);

/**
 * kvm_pgtable_stage2_free_unlinked() - Free an unlinked stage-2 paging structure.
 * @mm_ops:	Memory management callbacks.
 * @pgtable:	Unlinked stage-2 paging structure to be freed.
 * @level:	Level of the stage-2 paging structure to be freed.
 *
 * The page-table is assumed to be unreachable by any hardware walkers prior to
 * freeing and therefore no TLB invalidation is performed.
 */
void kvm_pgtable_stage2_free_unlinked(struct kvm_pgtable_mm_ops *mm_ops, void *pgtable, u32 level);

/**
 * kvm_pgtable_stage2_create_unlinked() - Create an unlinked stage-2 paging structure.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @phys:	Physical address of the memory to map.
 * @level:	Starting level of the stage-2 paging structure to be created.
 * @prot:	Permissions and attributes for the mapping.
 * @mc:		Cache of pre-allocated and zeroed memory from which to allocate
 *		page-table pages.
 * @force_pte:  Force mappings to PAGE_SIZE granularity.
 *
 * Returns an unlinked page-table tree.  This new page-table tree is
 * not reachable (i.e., it is unlinked) from the root pgd and it's
 * therefore unreachableby the hardware page-table walker. No TLB
 * invalidation or CMOs are performed.
 *
 * If device attributes are not explicitly requested in @prot, then the
 * mapping will be normal, cacheable.
 *
 * Return: The fully populated (unlinked) stage-2 paging structure, or
 * an ERR_PTR(error) on failure.
 */
kvm_pte_t *kvm_pgtable_stage2_create_unlinked(struct kvm_pgtable *pgt,
					      u64 phys, u32 level,
					      enum kvm_pgtable_prot prot,
					      void *mc, bool force_pte);

/**
 * kvm_pgtable_stage2_map() - Install a mapping in a guest stage-2 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Intermediate physical address at which to place the mapping.
 * @size:	Size of the mapping.
 * @phys:	Physical address of the memory to map.
 * @prot:	Permissions and attributes for the mapping.
 * @mc:		Cache of pre-allocated and zeroed memory from which to allocate
 *		page-table pages.
 * @flags:	Flags to control the page-table walk (ex. a shared walk)
 *
 * The offset of @addr within a page is ignored, @size is rounded-up to
 * the next page boundary and @phys is rounded-down to the previous page
 * boundary.
 *
 * If device attributes are not explicitly requested in @prot, then the
 * mapping will be normal, cacheable.
 *
 * Note that the update of a valid leaf PTE in this function will be aborted,
 * if it's trying to recreate the exact same mapping or only change the access
 * permissions. Instead, the vCPU will exit one more time from guest if still
 * needed and then go through the path of relaxing permissions.
 *
 * Note that this function will both coalesce existing table entries and split
 * existing block mappings, relying on page-faults to fault back areas outside
 * of the new mapping lazily.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_map(struct kvm_pgtable *pgt, u64 addr, u64 size,
			   u64 phys, enum kvm_pgtable_prot prot,
			   void *mc, enum kvm_pgtable_walk_flags flags);

/**
 * kvm_pgtable_stage2_set_owner() - Unmap and annotate pages in the IPA space to
 *				    track ownership.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Base intermediate physical address to annotate.
 * @size:	Size of the annotated range.
 * @mc:		Cache of pre-allocated and zeroed memory from which to allocate
 *		page-table pages.
 * @owner_id:	Unique identifier for the owner of the page.
 *
 * By default, all page-tables are owned by identifier 0. This function can be
 * used to mark portions of the IPA space as owned by other entities. When a
 * stage 2 is used with identity-mappings, these annotations allow to use the
 * page-table data structure as a simple rmap.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_set_owner(struct kvm_pgtable *pgt, u64 addr, u64 size,
				 void *mc, u8 owner_id);

/**
 * kvm_pgtable_stage2_unmap() - Remove a mapping from a guest stage-2 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Intermediate physical address from which to remove the mapping.
 * @size:	Size of the mapping.
 *
 * The offset of @addr within a page is ignored and @size is rounded-up to
 * the next page boundary.
 *
 * TLB invalidation is performed for each page-table entry cleared during the
 * unmapping operation and the reference count for the page-table page
 * containing the cleared entry is decremented, with unreferenced pages being
 * freed. Unmapping a cacheable page will ensure that it is clean to the PoC if
 * FWB is not supported by the CPU.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_unmap(struct kvm_pgtable *pgt, u64 addr, u64 size);

/**
 * kvm_pgtable_stage2_wrprotect() - Write-protect guest stage-2 address range
 *                                  without TLB invalidation.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Intermediate physical address from which to write-protect,
 * @size:	Size of the range.
 *
 * The offset of @addr within a page is ignored and @size is rounded-up to
 * the next page boundary.
 *
 * Note that it is the caller's responsibility to invalidate the TLB after
 * calling this function to ensure that the updated permissions are visible
 * to the CPUs.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_wrprotect(struct kvm_pgtable *pgt, u64 addr, u64 size);

/**
 * kvm_pgtable_stage2_mkyoung() - Set the access flag in a page-table entry.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Intermediate physical address to identify the page-table entry.
 *
 * The offset of @addr within a page is ignored.
 *
 * If there is a valid, leaf page-table entry used to translate @addr, then
 * set the access flag in that entry.
 *
 * Return: The old page-table entry prior to setting the flag, 0 on failure.
 */
kvm_pte_t kvm_pgtable_stage2_mkyoung(struct kvm_pgtable *pgt, u64 addr);

/**
 * kvm_pgtable_stage2_test_clear_young() - Test and optionally clear the access
 *					   flag in a page-table entry.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Intermediate physical address to identify the page-table entry.
 * @size:	Size of the address range to visit.
 * @mkold:	True if the access flag should be cleared.
 *
 * The offset of @addr within a page is ignored.
 *
 * Tests and conditionally clears the access flag for every valid, leaf
 * page-table entry used to translate the range [@addr, @addr + @size).
 *
 * Note that it is the caller's responsibility to invalidate the TLB after
 * calling this function to ensure that the updated permissions are visible
 * to the CPUs.
 *
 * Return: True if any of the visited PTEs had the access flag set.
 */
bool kvm_pgtable_stage2_test_clear_young(struct kvm_pgtable *pgt, u64 addr,
					 u64 size, bool mkold);

/**
 * kvm_pgtable_stage2_relax_perms() - Relax the permissions enforced by a
 *				      page-table entry.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Intermediate physical address to identify the page-table entry.
 * @prot:	Additional permissions to grant for the mapping.
 *
 * The offset of @addr within a page is ignored.
 *
 * If there is a valid, leaf page-table entry used to translate @addr, then
 * relax the permissions in that entry according to the read, write and
 * execute permissions specified by @prot. No permissions are removed, and
 * TLB invalidation is performed after updating the entry. Software bits cannot
 * be set or cleared using kvm_pgtable_stage2_relax_perms().
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_relax_perms(struct kvm_pgtable *pgt, u64 addr,
				   enum kvm_pgtable_prot prot);

/**
 * kvm_pgtable_stage2_flush_range() - Clean and invalidate data cache to Point
 * 				      of Coherency for guest stage-2 address
 *				      range.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init*().
 * @addr:	Intermediate physical address from which to flush.
 * @size:	Size of the range.
 *
 * The offset of @addr within a page is ignored and @size is rounded-up to
 * the next page boundary.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_flush(struct kvm_pgtable *pgt, u64 addr, u64 size);

/**
 * kvm_pgtable_stage2_split() - Split a range of huge pages into leaf PTEs pointing
 *				to PAGE_SIZE guest pages.
 * @pgt:	 Page-table structure initialised by kvm_pgtable_stage2_init().
 * @addr:	 Intermediate physical address from which to split.
 * @size:	 Size of the range.
 * @mc:		 Cache of pre-allocated and zeroed memory from which to allocate
 *		 page-table pages.
 *
 * The function tries to split any level 1 or 2 entry that overlaps
 * with the input range (given by @addr and @size).
 *
 * Return: 0 on success, negative error code on failure. Note that
 * kvm_pgtable_stage2_split() is best effort: it tries to break as many
 * blocks in the input range as allowed by @mc_capacity.
 */
int kvm_pgtable_stage2_split(struct kvm_pgtable *pgt, u64 addr, u64 size,
			     struct kvm_mmu_memory_cache *mc);

/**
 * kvm_pgtable_walk() - Walk a page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_*_init().
 * @addr:	Input address for the start of the walk.
 * @size:	Size of the range to walk.
 * @walker:	Walker callback description.
 *
 * The offset of @addr within a page is ignored and @size is rounded-up to
 * the next page boundary.
 *
 * The walker will walk the page-table entries corresponding to the input
 * address range specified, visiting entries according to the walker flags.
 * Invalid entries are treated as leaf entries. The visited page table entry is
 * reloaded after invoking the walker callback, allowing the walker to descend
 * into a newly installed table.
 *
 * Returning a negative error code from the walker callback function will
 * terminate the walk immediately with the same error code.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_walk(struct kvm_pgtable *pgt, u64 addr, u64 size,
		     struct kvm_pgtable_walker *walker);

/**
 * kvm_pgtable_get_leaf() - Walk a page-table and retrieve the leaf entry
 *			    with its level.
 * @pgt:	Page-table structure initialised by kvm_pgtable_*_init()
 *		or a similar initialiser.
 * @addr:	Input address for the start of the walk.
 * @ptep:	Pointer to storage for the retrieved PTE.
 * @level:	Pointer to storage for the level of the retrieved PTE.
 *
 * The offset of @addr within a page is ignored.
 *
 * The walker will walk the page-table entries corresponding to the input
 * address specified, retrieving the leaf corresponding to this address.
 * Invalid entries are treated as leaf entries.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_get_leaf(struct kvm_pgtable *pgt, u64 addr,
			 kvm_pte_t *ptep, u32 *level);

/**
 * kvm_pgtable_stage2_pte_prot() - Retrieve the protection attributes of a
 *				   stage-2 Page-Table Entry.
 * @pte:	Page-table entry
 *
 * Return: protection attributes of the page-table entry in the enum
 *	   kvm_pgtable_prot format.
 */
enum kvm_pgtable_prot kvm_pgtable_stage2_pte_prot(kvm_pte_t pte);

/**
 * kvm_pgtable_hyp_pte_prot() - Retrieve the protection attributes of a stage-1
 *				Page-Table Entry.
 * @pte:	Page-table entry
 *
 * Return: protection attributes of the page-table entry in the enum
 *	   kvm_pgtable_prot format.
 */
enum kvm_pgtable_prot kvm_pgtable_hyp_pte_prot(kvm_pte_t pte);

/**
 * kvm_tlb_flush_vmid_range() - Invalidate/flush a range of TLB entries
 *
 * @mmu:	Stage-2 KVM MMU struct
 * @addr:	The base Intermediate physical address from which to invalidate
 * @size:	Size of the range from the base to invalidate
 */
void kvm_tlb_flush_vmid_range(struct kvm_s2_mmu *mmu,
				phys_addr_t addr, size_t size);
#endif	/* __ARM64_KVM_PGTABLE_H__ */
