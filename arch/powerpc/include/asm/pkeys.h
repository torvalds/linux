/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PowerPC Memory Protection Keys management
 *
 * Copyright 2017, Ram Pai, IBM Corporation.
 */

#ifndef _ASM_POWERPC_KEYS_H
#define _ASM_POWERPC_KEYS_H

#include <linux/jump_label.h>

DECLARE_STATIC_KEY_TRUE(pkey_disabled);
#define ARCH_VM_PKEY_FLAGS 0

static inline bool mm_pkey_is_allocated(struct mm_struct *mm, int pkey)
{
	return false;
}

static inline int mm_pkey_alloc(struct mm_struct *mm)
{
	return -1;
}

static inline int mm_pkey_free(struct mm_struct *mm, int pkey)
{
	return -EINVAL;
}

/*
 * Try to dedicate one of the protection keys to be used as an
 * execute-only protection key.
 */
static inline int execute_only_pkey(struct mm_struct *mm)
{
	return 0;
}

static inline int arch_override_mprotect_pkey(struct vm_area_struct *vma,
					      int prot, int pkey)
{
	return 0;
}

static inline int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
					    unsigned long init_val)
{
	return 0;
}
#endif /*_ASM_POWERPC_KEYS_H */
