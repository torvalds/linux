#ifndef __MMU_H
#define __MMU_H

typedef struct {
	struct list_head crst_list;
	struct list_head pgtable_list;
	unsigned long asce_bits;
	unsigned long asce_limit;
	int noexec;
	int pgstes;
} mm_context_t;

#endif
