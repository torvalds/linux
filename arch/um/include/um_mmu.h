/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __ARCH_UM_MMU_H
#define __ARCH_UM_MMU_H

#include "uml-config.h"
#include "choose-mode.h"
#include "mmu-skas.h"

typedef union mm_context {
	struct mmu_context_skas skas;
} mm_context_t;

#endif
