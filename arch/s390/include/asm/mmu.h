#ifndef __MMU_H
#define __MMU_H

typedef struct {
	atomic_t attach_count;
	unsigned int flush_mm;
	spinlock_t list_lock;
	struct list_head crst_list;
	struct list_head pgtable_list;
	unsigned long asce_bits;
	unsigned long asce_limit;
	unsigned long vdso_base;
	int noexec;
	int has_pgste;	 /* The mmu context has extended page tables */
	int alloc_pgste; /* cloned contexts will have extended page tables */
} mm_context_t;

#define INIT_MM_CONTEXT(name)						      \
	.context.list_lock    = __SPIN_LOCK_UNLOCKED(name.context.list_lock), \
	.context.crst_list    = LIST_HEAD_INIT(name.context.crst_list),	      \
	.context.pgtable_list = LIST_HEAD_INIT(name.context.pgtable_list),

#endif
