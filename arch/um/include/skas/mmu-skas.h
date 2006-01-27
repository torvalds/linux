/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_MMU_H
#define __SKAS_MMU_H

#include "linux/config.h"
#include "mm_id.h"
#include "asm/ldt.h"

struct mmu_context_skas {
	struct mm_id id;
	unsigned long last_page_table;
#ifdef CONFIG_3_LEVEL_PGTABLES
	unsigned long last_pmd;
#endif
	uml_ldt_t ldt;
};

extern void switch_mm_skas(struct mm_id * mm_idp);

#endif
