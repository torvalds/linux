/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_MMU_H
#define _ASM_MMU_H

#include <asm/vdso.h>

/*
 * Architecture-specific state for a mm_struct.
 * For the Hexagon Virtual Machine, it can be a copy
 * of the pointer to the page table base.
 */
struct mm_context {
	unsigned long long generation;
	unsigned long ptbase;
	struct hexagon_vdso *vdso;
};

typedef struct mm_context mm_context_t;

#endif
