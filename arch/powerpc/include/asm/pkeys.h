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
extern int pkeys_total; /* total pkeys as per device tree */
extern u32 initial_allocation_mask; /* bits set for reserved keys */

/*
 * Define these here temporarily so we're not dependent on patching linux/mm.h.
 * Once it's updated we can drop these.
 */
#ifndef VM_PKEY_BIT0
# define VM_PKEY_SHIFT	VM_HIGH_ARCH_BIT_0
# define VM_PKEY_BIT0	VM_HIGH_ARCH_0
# define VM_PKEY_BIT1	VM_HIGH_ARCH_1
# define VM_PKEY_BIT2	VM_HIGH_ARCH_2
# define VM_PKEY_BIT3	VM_HIGH_ARCH_3
# define VM_PKEY_BIT4	VM_HIGH_ARCH_4
#endif

#define ARCH_VM_PKEY_FLAGS (VM_PKEY_BIT0 | VM_PKEY_BIT1 | VM_PKEY_BIT2 | \
			    VM_PKEY_BIT3 | VM_PKEY_BIT4)

#define arch_max_pkey() pkeys_total

#define pkey_alloc_mask(pkey) (0x1 << pkey)

#define mm_pkey_allocation_map(mm) (mm->context.pkey_allocation_map)

#define __mm_pkey_allocated(mm, pkey) {	\
	mm_pkey_allocation_map(mm) |= pkey_alloc_mask(pkey); \
}

#define __mm_pkey_free(mm, pkey) {	\
	mm_pkey_allocation_map(mm) &= ~pkey_alloc_mask(pkey);	\
}

#define __mm_pkey_is_allocated(mm, pkey)	\
	(mm_pkey_allocation_map(mm) & pkey_alloc_mask(pkey))

#define __mm_pkey_is_reserved(pkey) (initial_allocation_mask & \
				       pkey_alloc_mask(pkey))

static inline bool mm_pkey_is_allocated(struct mm_struct *mm, int pkey)
{
	/* A reserved key is never considered as 'explicitly allocated' */
	return ((pkey < arch_max_pkey()) &&
		!__mm_pkey_is_reserved(pkey) &&
		__mm_pkey_is_allocated(mm, pkey));
}

/*
 * Returns a positive, 5-bit key on success, or -1 on failure.
 * Relies on the mmap_sem to protect against concurrency in mm_pkey_alloc() and
 * mm_pkey_free().
 */
static inline int mm_pkey_alloc(struct mm_struct *mm)
{
	/*
	 * Note: this is the one and only place we make sure that the pkey is
	 * valid as far as the hardware is concerned. The rest of the kernel
	 * trusts that only good, valid pkeys come out of here.
	 */
	u32 all_pkeys_mask = (u32)(~(0x0));
	int ret;

	if (static_branch_likely(&pkey_disabled))
		return -1;

	/*
	 * Are we out of pkeys? We must handle this specially because ffz()
	 * behavior is undefined if there are no zeros.
	 */
	if (mm_pkey_allocation_map(mm) == all_pkeys_mask)
		return -1;

	ret = ffz((u32)mm_pkey_allocation_map(mm));
	__mm_pkey_allocated(mm, ret);
	return ret;
}

static inline int mm_pkey_free(struct mm_struct *mm, int pkey)
{
	if (static_branch_likely(&pkey_disabled))
		return -1;

	if (!mm_pkey_is_allocated(mm, pkey))
		return -EINVAL;

	__mm_pkey_free(mm, pkey);

	return 0;
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

extern void pkey_mm_init(struct mm_struct *mm);
#endif /*_ASM_POWERPC_KEYS_H */
