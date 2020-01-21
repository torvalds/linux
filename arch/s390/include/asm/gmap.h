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
#define _SEGMENT_ENTRY_GMAP_IN		0x8000	/* invalidation notify bit */
#define _SEGMENT_ENTRY_GMAP_UC		0x4000	/* dirty (migration) */

/**
 * struct gmap_struct - guest address space
 * @list: list head for the mm->context gmap list
 * @crst_list: list of all crst tables used in the guest address space
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
 * @pt_list: list of all page tables used in the shadow guest address space
 * @shadow_lock: spinlock to protect the shadow gmap list
 * @parent: pointer to the parent gmap for shadow guest address spaces
 * @orig_asce: ASCE for which the shadow page table has been created
 * @edat_level: edat level to be used for the shadow translation
 * @removed: flag to indicate if a shadow guest address space has been removed
 * @initialized: flag to indicate if a shadow guest address space can be used
 */
struct gmap {
	struct list_head list;
	struct list_head crst_list;
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
	struct list_head pt_list;
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

void gmap_enable(struct gmap *gmap);
void gmap_disable(struct gmap *gmap);
struct gmap *gmap_get_enabled(void);
int gmap_map_segment(struct gmap *gmap, unsigned long from,
		     unsigned long to, unsigned long len);
int gmap_unmap_segment(struct gmap *gmap, unsigned long to, unsigned long len);
unsigned long __gmap_translate(struct gmap *, unsigned long gaddr);
unsigned long gmap_translate(struct gmap *, unsigned long gaddr);
int __gmap_link(struct gmap *gmap, unsigned long gaddr, unsigned long vmaddr);
int gmap_fault(struct gmap *, unsigned long gaddr, unsigned int fault_flags);
void gmap_discard(struct gmap *, unsigned long from, unsigned long to);
void __gmap_zap(struct gmap *, unsigned long gaddr);
void gmap_unlink(struct mm_struct *, unsigned long *table, unsigned long vmaddr);

int gmap_read_table(struct gmap *gmap, unsigned long gaddr, unsigned long *val);

struct gmap *gmap_shadow(struct gmap *parent, unsigned long asce,
			 int edat_level);
int gmap_shadow_valid(struct gmap *sg, unsigned long asce, int edat_level);
int gmap_shadow_r2t(struct gmap *sg, unsigned long saddr, unsigned long r2t,
		    int fake);
int gmap_shadow_r3t(struct gmap *sg, unsigned long saddr, unsigned long r3t,
		    int fake);
int gmap_shadow_sgt(struct gmap *sg, unsigned long saddr, unsigned long sgt,
		    int fake);
int gmap_shadow_pgt(struct gmap *sg, unsigned long saddr, unsigned long pgt,
		    int fake);
int gmap_shadow_pgt_lookup(struct gmap *sg, unsigned long saddr,
			   unsigned long *pgt, int *dat_protection, int *fake);
int gmap_shadow_page(struct gmap *sg, unsigned long saddr, pte_t pte);

void gmap_register_pte_notifier(struct gmap_notifier *);
void gmap_unregister_pte_notifier(struct gmap_notifier *);
void gmap_pte_notify(struct mm_struct *, unsigned long addr, pte_t *,
		     unsigned long bits);

int gmap_mprotect_notify(struct gmap *, unsigned long start,
			 unsigned long len, int prot);

void gmap_sync_dirty_log_pmd(struct gmap *gmap, unsigned long dirty_bitmap[4],
			     unsigned long gaddr, unsigned long vmaddr);
#endif /* _ASM_S390_GMAP_H */
