/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2014 IBM Corp.
 */

#ifndef _ASM_POWERPC_COPRO_H
#define _ASM_POWERPC_COPRO_H

#include <linux/mm_types.h>

struct copro_slb
{
	u64 esid, vsid;
};

int copro_handle_mm_fault(struct mm_struct *mm, unsigned long ea,
			  unsigned long dsisr, vm_fault_t *flt);

int copro_calculate_slb(struct mm_struct *mm, u64 ea, struct copro_slb *slb);


#ifdef CONFIG_PPC_COPRO_BASE
void copro_flush_all_slbs(struct mm_struct *mm);
#else
static inline void copro_flush_all_slbs(struct mm_struct *mm) {}
#endif
#endif /* _ASM_POWERPC_COPRO_H */
