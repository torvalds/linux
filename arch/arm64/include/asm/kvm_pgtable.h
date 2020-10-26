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

typedef u64 kvm_pte_t;

/**
 * struct kvm_pgtable - KVM page-table.
 * @ia_bits:		Maximum input address size, in bits.
 * @start_level:	Level at which the page-table walk starts.
 * @pgd:		Pointer to the first top-level entry of the page-table.
 * @mmu:		Stage-2 KVM MMU struct. Unused for stage-1 page-tables.
 */
struct kvm_pgtable {
	u32					ia_bits;
	u32					start_level;
	kvm_pte_t				*pgd;

	/* Stage-2 only */
	struct kvm_s2_mmu			*mmu;
};

/**
 * enum kvm_pgtable_prot - Page-table permissions and attributes.
 * @KVM_PGTABLE_PROT_X:		Execute permission.
 * @KVM_PGTABLE_PROT_W:		Write permission.
 * @KVM_PGTABLE_PROT_R:		Read permission.
 * @KVM_PGTABLE_PROT_DEVICE:	Device attributes.
 */
enum kvm_pgtable_prot {
	KVM_PGTABLE_PROT_X			= BIT(0),
	KVM_PGTABLE_PROT_W			= BIT(1),
	KVM_PGTABLE_PROT_R			= BIT(2),

	KVM_PGTABLE_PROT_DEVICE			= BIT(3),
};

#define PAGE_HYP		(KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_W)
#define PAGE_HYP_EXEC		(KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_X)
#define PAGE_HYP_RO		(KVM_PGTABLE_PROT_R)
#define PAGE_HYP_DEVICE		(PAGE_HYP | KVM_PGTABLE_PROT_DEVICE)

/**
 * enum kvm_pgtable_walk_flags - Flags to control a depth-first page-table walk.
 * @KVM_PGTABLE_WALK_LEAF:		Visit leaf entries, including invalid
 *					entries.
 * @KVM_PGTABLE_WALK_TABLE_PRE:		Visit table entries before their
 *					children.
 * @KVM_PGTABLE_WALK_TABLE_POST:	Visit table entries after their
 *					children.
 */
enum kvm_pgtable_walk_flags {
	KVM_PGTABLE_WALK_LEAF			= BIT(0),
	KVM_PGTABLE_WALK_TABLE_PRE		= BIT(1),
	KVM_PGTABLE_WALK_TABLE_POST		= BIT(2),
};

typedef int (*kvm_pgtable_visitor_fn_t)(u64 addr, u64 end, u32 level,
					kvm_pte_t *ptep,
					enum kvm_pgtable_walk_flags flag,
					void * const arg);

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

/**
 * kvm_pgtable_hyp_init() - Initialise a hypervisor stage-1 page-table.
 * @pgt:	Uninitialised page-table structure to initialise.
 * @va_bits:	Maximum virtual address bits.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_hyp_init(struct kvm_pgtable *pgt, u32 va_bits);

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
 * kvm_pgtable_stage2_init() - Initialise a guest stage-2 page-table.
 * @pgt:	Uninitialised page-table structure to initialise.
 * @kvm:	KVM structure representing the guest virtual machine.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_init(struct kvm_pgtable *pgt, struct kvm *kvm);

/**
 * kvm_pgtable_stage2_destroy() - Destroy an unused guest stage-2 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
 *
 * The page-table is assumed to be unreachable by any hardware walkers prior
 * to freeing and therefore no TLB invalidation is performed.
 */
void kvm_pgtable_stage2_destroy(struct kvm_pgtable *pgt);

/**
 * kvm_pgtable_stage2_map() - Install a mapping in a guest stage-2 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
 * @addr:	Intermediate physical address at which to place the mapping.
 * @size:	Size of the mapping.
 * @phys:	Physical address of the memory to map.
 * @prot:	Permissions and attributes for the mapping.
 * @mc:		Cache of pre-allocated GFP_PGTABLE_USER memory from which to
 *		allocate page-table pages.
 *
 * The offset of @addr within a page is ignored, @size is rounded-up to
 * the next page boundary and @phys is rounded-down to the previous page
 * boundary.
 *
 * If device attributes are not explicitly requested in @prot, then the
 * mapping will be normal, cacheable.
 *
 * Note that this function will both coalesce existing table entries and split
 * existing block mappings, relying on page-faults to fault back areas outside
 * of the new mapping lazily.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_map(struct kvm_pgtable *pgt, u64 addr, u64 size,
			   u64 phys, enum kvm_pgtable_prot prot,
			   struct kvm_mmu_memory_cache *mc);

/**
 * kvm_pgtable_stage2_unmap() - Remove a mapping from a guest stage-2 page-table.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
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
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
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
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
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
 * kvm_pgtable_stage2_mkold() - Clear the access flag in a page-table entry.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
 * @addr:	Intermediate physical address to identify the page-table entry.
 *
 * The offset of @addr within a page is ignored.
 *
 * If there is a valid, leaf page-table entry used to translate @addr, then
 * clear the access flag in that entry.
 *
 * Note that it is the caller's responsibility to invalidate the TLB after
 * calling this function to ensure that the updated permissions are visible
 * to the CPUs.
 *
 * Return: The old page-table entry prior to clearing the flag, 0 on failure.
 */
kvm_pte_t kvm_pgtable_stage2_mkold(struct kvm_pgtable *pgt, u64 addr);

/**
 * kvm_pgtable_stage2_relax_perms() - Relax the permissions enforced by a
 *				      page-table entry.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
 * @addr:	Intermediate physical address to identify the page-table entry.
 * @prot:	Additional permissions to grant for the mapping.
 *
 * The offset of @addr within a page is ignored.
 *
 * If there is a valid, leaf page-table entry used to translate @addr, then
 * relax the permissions in that entry according to the read, write and
 * execute permissions specified by @prot. No permissions are removed, and
 * TLB invalidation is performed after updating the entry.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_stage2_relax_perms(struct kvm_pgtable *pgt, u64 addr,
				   enum kvm_pgtable_prot prot);

/**
 * kvm_pgtable_stage2_is_young() - Test whether a page-table entry has the
 *				   access flag set.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
 * @addr:	Intermediate physical address to identify the page-table entry.
 *
 * The offset of @addr within a page is ignored.
 *
 * Return: True if the page-table entry has the access flag set, false otherwise.
 */
bool kvm_pgtable_stage2_is_young(struct kvm_pgtable *pgt, u64 addr);

/**
 * kvm_pgtable_stage2_flush_range() - Clean and invalidate data cache to Point
 * 				      of Coherency for guest stage-2 address
 *				      range.
 * @pgt:	Page-table structure initialised by kvm_pgtable_stage2_init().
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
 * Invalid entries are treated as leaf entries. Leaf entries are reloaded
 * after invoking the walker callback, allowing the walker to descend into
 * a newly installed table.
 *
 * Returning a negative error code from the walker callback function will
 * terminate the walk immediately with the same error code.
 *
 * Return: 0 on success, negative error code on failure.
 */
int kvm_pgtable_walk(struct kvm_pgtable *pgt, u64 addr, u64 size,
		     struct kvm_pgtable_walker *walker);

#endif	/* __ARM64_KVM_PGTABLE_H__ */
