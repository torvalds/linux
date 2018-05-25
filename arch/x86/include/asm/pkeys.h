/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKEYS_H
#define _ASM_X86_PKEYS_H

#define ARCH_DEFAULT_PKEY	0

#define arch_max_pkey() (boot_cpu_has(X86_FEATURE_OSPKE) ? 16 : 1)

extern int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
		unsigned long init_val);

/*
 * Try to dedicate one of the protection keys to be used as an
 * execute-only protection key.
 */
extern int __execute_only_pkey(struct mm_struct *mm);
static inline int execute_only_pkey(struct mm_struct *mm)
{
	if (!boot_cpu_has(X86_FEATURE_OSPKE))
		return ARCH_DEFAULT_PKEY;

	return __execute_only_pkey(mm);
}

extern int __arch_override_mprotect_pkey(struct vm_area_struct *vma,
		int prot, int pkey);
static inline int arch_override_mprotect_pkey(struct vm_area_struct *vma,
		int prot, int pkey)
{
	if (!boot_cpu_has(X86_FEATURE_OSPKE))
		return 0;

	return __arch_override_mprotect_pkey(vma, prot, pkey);
}

extern int __arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
		unsigned long init_val);

#define ARCH_VM_PKEY_FLAGS (VM_PKEY_BIT0 | VM_PKEY_BIT1 | VM_PKEY_BIT2 | VM_PKEY_BIT3)

#define mm_pkey_allocation_map(mm)	(mm->context.pkey_allocation_map)
#define mm_set_pkey_allocated(mm, pkey) do {		\
	mm_pkey_allocation_map(mm) |= (1U << pkey);	\
} while (0)
#define mm_set_pkey_free(mm, pkey) do {			\
	mm_pkey_allocation_map(mm) &= ~(1U << pkey);	\
} while (0)

static inline
bool mm_pkey_is_allocated(struct mm_struct *mm, int pkey)
{
	/*
	 * "Allocated" pkeys are those that have been returned
	 * from pkey_alloc() or pkey 0 which is allocated
	 * implicitly when the mm is created.
	 */
	if (pkey < 0)
		return false;
	if (pkey >= arch_max_pkey())
		return false;
	/*
	 * The exec-only pkey is set in the allocation map, but
	 * is not available to any of the user interfaces like
	 * mprotect_pkey().
	 */
	if (pkey == mm->context.execute_only_pkey)
		return false;

	return mm_pkey_allocation_map(mm) & (1U << pkey);
}

/*
 * Returns a positive, 4-bit key on success, or -1 on failure.
 */
static inline
int mm_pkey_alloc(struct mm_struct *mm)
{
	/*
	 * Note: this is the one and only place we make sure
	 * that the pkey is valid as far as the hardware is
	 * concerned.  The rest of the kernel trusts that
	 * only good, valid pkeys come out of here.
	 */
	u16 all_pkeys_mask = ((1U << arch_max_pkey()) - 1);
	int ret;

	/*
	 * Are we out of pkeys?  We must handle this specially
	 * because ffz() behavior is undefined if there are no
	 * zeros.
	 */
	if (mm_pkey_allocation_map(mm) == all_pkeys_mask)
		return -1;

	ret = ffz(mm_pkey_allocation_map(mm));

	mm_set_pkey_allocated(mm, ret);

	return ret;
}

static inline
int mm_pkey_free(struct mm_struct *mm, int pkey)
{
	if (!mm_pkey_is_allocated(mm, pkey))
		return -EINVAL;

	mm_set_pkey_free(mm, pkey);

	return 0;
}

extern int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
		unsigned long init_val);
extern int __arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
		unsigned long init_val);
extern void copy_init_pkru_to_fpregs(void);

#endif /*_ASM_X86_PKEYS_H */
