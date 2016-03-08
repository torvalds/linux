/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2016
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _ASM_S390_GMAP_H
#define _ASM_S390_GMAP_H

/**
 * struct gmap_struct - guest address space
 * @crst_list: list of all crst tables used in the guest address space
 * @mm: pointer to the parent mm_struct
 * @guest_to_host: radix tree with guest to host address translation
 * @host_to_guest: radix tree with pointer to segment table entries
 * @guest_table_lock: spinlock to protect all entries in the guest page table
 * @ref_count: reference counter for the gmap structure
 * @table: pointer to the page directory
 * @asce: address space control element for gmap page table
 * @pfault_enabled: defines if pfaults are applicable for the guest
 */
struct gmap {
	struct list_head list;
	struct list_head crst_list;
	struct mm_struct *mm;
	struct radix_tree_root guest_to_host;
	struct radix_tree_root host_to_guest;
	spinlock_t guest_table_lock;
	atomic_t ref_count;
	unsigned long *table;
	unsigned long asce;
	unsigned long asce_end;
	void *private;
	bool pfault_enabled;
};

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

struct gmap *gmap_create(struct mm_struct *mm, unsigned long limit);
void gmap_remove(struct gmap *gmap);
struct gmap *gmap_get(struct gmap *gmap);
void gmap_put(struct gmap *gmap);

void gmap_enable(struct gmap *gmap);
void gmap_disable(struct gmap *gmap);
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

void gmap_register_pte_notifier(struct gmap_notifier *);
void gmap_unregister_pte_notifier(struct gmap_notifier *);
void gmap_pte_notify(struct mm_struct *, unsigned long addr, pte_t *);

int gmap_mprotect_notify(struct gmap *, unsigned long start,
			 unsigned long len, int prot);

#endif /* _ASM_S390_GMAP_H */
