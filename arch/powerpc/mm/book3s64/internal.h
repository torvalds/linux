/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARCH_POWERPC_MM_BOOK3S64_INTERNAL_H
#define ARCH_POWERPC_MM_BOOK3S64_INTERNAL_H

#include <linux/jump_label.h>

extern bool stress_slb_enabled;

DECLARE_STATIC_KEY_FALSE(stress_slb_key);

static inline bool stress_slb(void)
{
	return static_branch_unlikely(&stress_slb_key);
}

void slb_setup_new_exec(void);

void exit_lazy_flush_tlb(struct mm_struct *mm, bool always_flush);

#endif /* ARCH_POWERPC_MM_BOOK3S64_INTERNAL_H */
