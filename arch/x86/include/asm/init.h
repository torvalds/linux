#ifndef _ASM_X86_INIT_H
#define _ASM_X86_INIT_H

struct x86_mapping_info {
	void *(*alloc_pgt_page)(void *); /* allocate buf for page table */
	void *context;			 /* context for alloc_pgt_page */
	unsigned long pmd_flag;		 /* page flag for PMD entry */
	unsigned long offset;		 /* ident mapping offset */
};

int kernel_ident_mapping_init(struct x86_mapping_info *info, pgd_t *pgd_page,
				unsigned long pstart, unsigned long pend);

#endif /* _ASM_X86_INIT_H */
