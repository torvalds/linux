/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2016
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _ASM_S390_GMAP_H
#define _ASM_S390_GMAP_H

#include <linux/radix-tree.h>
#include <linux/refcount.h>

/* Generic bits for GMAP notification on DAT table entry changes. */
#define GMAP_NOTIFY_SHADOW	0x2
#define GMAP_NOTIFY_MPROT	0x1

/* Status bits only for huge segment entries */
#define _SEGMENT_ENTRY_GMAP_IN		0x0800	/* invalidation notify bit */
#define _SEGMENT_ENTRY_GMAP_UC		0x0002	/* dirty (migration) */

/**
 * struct gmap_struct - guest address space
 * @list: list head for the mm->context gmap list
 * @mm: pointer to the parent mm_struct
 * @guest_to_host: radix tree with guest to host address translation
 * @host_to_guest: radix tree with pointer to segment table entries
 * @guest_table_lock: spinlock to protect all entries in the guest page table
 * @ref_count: reference counter for the gmap structure
 * @table: pointer to the page directory
 * @asce: address space control element for gmap page table
 * @pfault_enabled: defines if pfaults are applicable for the guest
 * @guest_handle: protected virtual machine handle for the ultravisor
 * @host_to_rmap: radix tree with gmap_rmap lists
 * @children: list of shadow gmap structures
 * @shadow_lock: spinlock to protect the shadow gmap list
 * @parent: pointer to the parent gmap for shadow guest address spaces
 * @orig_asce: ASCE for which the shadow page table has been created
 * @edat_level: edat level to be used for the shadow translation
 * @removed: flag to indicate if a shadow guest address space has been removed
 * @initialized: flag to indicate if a shadow guest address space can be used
 */
struct gmap {
	struct list_head list;
	struct mm_struct *mm;
	struct radix_tree_root guest_to_host;
	struct radix_tree_root host_to_guest;
	spinlock_t guest_table_lock;
	refcount_t ref_count;
	unsigned long *table;
	unsigned long asce;
	unsigned long asce_end;
	void *private;
	bool pfault_enabled;
	/* only set for protected virtual machines */
	unsigned long guest_handle;
	/* Additional data for shadow guest address spaces */
	struct radix_tree_root host_to_rmap;
	struct list_head children;
	spinlock_t shadow_lock;
	struct gmap *parent;
	unsigned long orig_asce;
	int edat_level;
	bool removed;
	bool initialized;
};

/**
 * struct gmap_rmap - reverse mapping for shadow page table entries
 * @next: pointer to next rmap in the list
 * @raddr: virtual rmap address in the shadow guest address space
 */
struct gmap_rmap {
	struct gmap_rmap *next;
	unsigned long raddr;
};

#define gmap_for_each_rmap(pos, head) \
	for (pos = (head); pos; pos = pos->next)

#define gmap_for_each_rmap_safe(pos, n, head) \
	for (pos = (head); n = pos ? pos->next : NULL, pos; pos = n)

/**
 * struct gmap_notifier - notify function block for page invalidation
 * @notifier_call: address of callback function
 */
struct gmap_notifier {
	struct list_head list;
	struct rcu_head rcu;
	void (*notifier_call)(struct gmap *gmap, unsigned long start,
			      unsigned long end);
};

static inline int gmap_is_shadow(struct gmap *gmap)
{
	return !!gmap->parent;
}

struct gmap *gmap_create(struct mm_struct *mm, unsigned long limit);
void gmap_remove(struct gmap *gmap);
struct gmap *gmap_get(struct gmap *gmap);
void gmap_put(struct gmap *gmap);
void gmap_free(struct gmap *gmap);
struct gmap *gmap_alloc(unsigned long limit);

int gmap_map_segment(struct gmap *gmap, unsigned long from,
		     unsigned long to, unsigned long len);
int gmap_unmap_segment(struct gmap *gmap, unsigned long to, unsigned long len);
unsigned long __gmap_translate(struct gmap *, unsigned long gaddr);
int __gmap_link(struct gmap *gmap, unsigned long gaddr, unsigned long vmaddr);
void __gmap_zap(struct gmap *, unsigned long gaddr);
void gmap_unlink(struct mm_struct *, unsigned long *table, unsigned long vmaddr);

int gmap_read_table(struct gmap *gmap, unsigned long gaddr, unsigned long *val);

void gmap_unshadow(struct gmap *sg);
int gmap_shadow_r2t(struct gmap *sg, unsigned long saddr, unsigned long r2t,
		    int fake);
int gmap_shadow_r3t(struct gmap *sg, unsigned long saddr, unsigned long r3t,
		    int fake);
int gmap_shadow_sgt(struct gmap *sg, unsigned long saddr, unsigned long sgt,
		    int fake);
int gmap_shadow_pgt(struct gmap *sg, unsigned long saddr, unsigned long pgt,
		    int fake);
int gmap_shadow_page(struct gmap *sg, unsigned long saddr, pte_t pte);

void gmap_register_pte_notifier(struct gmap_notifier *);
void gmap_unregister_pte_notifier(struct gmap_notifier *);

int gmap_protect_one(struct gmap *gmap, unsigned long gaddr, int prot, unsigned long bits);

void gmap_sync_dirty_log_pmd(struct gmap *gmap, unsigned long dirty_bitmap[4],
			     unsigned long gaddr, unsigned long vmaddr);
int s390_replace_asce(struct gmap *gmap);
void s390_uv_destroy_pfns(unsigned long count, unsigned long *pfns);
int __s390_uv_destroy_range(struct mm_struct *mm, unsigned long start,
			    unsigned long end, bool interruptible);
unsigned long *gmap_table_walk(struct gmap *gmap, unsigned long gaddr, int level);

/**
 * s390_uv_destroy_range - Destroy a range of pages in the given mm.
 * @mm: the mm on which to operate on
 * @start: the start of the range
 * @end: the end of the range
 *
 * This function will call cond_sched, so it should not generate stalls, but
 * it will otherwise only return when it completed.
 */
static inline void s390_uv_destroy_range(struct mm_struct *mm, unsigned long start,
					 unsigned long end)
{
	(void)__s390_uv_destroy_range(mm, start, end, false);
}

/**
 * s390_uv_destroy_range_interruptible - Destroy a range of pages in the
 * given mm, but stop when a fatal signal is received.
 * @mm: the mm on which to operate on
 * @start: the start of the range
 * @end: the end of the range
 *
 * This function will call cond_sched, so it should not generate stalls. If
 * a fatal signal is received, it will return with -EINTR immediately,
 * without finishing destroying the whole range. Upon successful
 * completion, 0 is returned.
 */
static inline int s390_uv_destroy_range_interruptible(struct mm_struct *mm, unsigned long start,
						      unsigned long end)
{
	return __s390_uv_destroy_range(mm, start, end, true);
}
#endif /* _ASM_S390_GMAP_H */
