/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __ARCH_UM_MMU_H
#define __ARCH_UM_MMU_H

#include "uml-config.h"
#include "mmu-skas.h"

typedef union mm_context {
	struct mmu_context_skas skas;
} mm_context_t;

#endif
