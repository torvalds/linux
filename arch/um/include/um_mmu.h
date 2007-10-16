/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __ARCH_UM_MMU_H
#define __ARCH_UM_MMU_H

#include "uml-config.h"
#include "mm_id.h"
#include "asm/ldt.h"

typedef struct mm_context {
	struct mm_id id;
	unsigned long last_page_table;
#ifdef CONFIG_3_LEVEL_PGTABLES
	unsigned long last_pmd;
#endif
	struct uml_ldt ldt;
} mm_context_t;

extern void __switch_mm(struct mm_id * mm_idp);

/* Avoid tangled inclusion with asm/ldt.h */
extern long init_new_ldt(struct mm_context *to_mm, struct mm_context *from_mm);
extern void free_ldt(struct mm_context *mm);

#endif
