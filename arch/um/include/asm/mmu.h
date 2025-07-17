/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __ARCH_UM_MMU_H
#define __ARCH_UM_MMU_H

#include "linux/types.h"
#include <mm_id.h>

typedef struct mm_context {
	struct mm_id id;

	struct list_head list;

	/* Address range in need of a TLB sync */
	unsigned long sync_tlb_range_from;
	unsigned long sync_tlb_range_to;
} mm_context_t;

#endif
