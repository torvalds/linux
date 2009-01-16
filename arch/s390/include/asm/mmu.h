#ifndef __MMU_H
#define __MMU_H

typedef struct {
	struct list_head crst_list;
	struct list_head pgtable_list;
	unsigned long asce_bits;
	unsigned long asce_limit;
	unsigned long vdso_base;
	int noexec;
	int has_pgste;	 /* The mmu context has extended page tables */
	int alloc_pgste; /* cloned contexts will have extended page tables */
} mm_context_t;

#endif
