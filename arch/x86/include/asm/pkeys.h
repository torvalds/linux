#ifndef _ASM_X86_PKEYS_H
#define _ASM_X86_PKEYS_H

#define PKEY_DEDICATED_EXECUTE_ONLY 15
/*
 * Consider the PKEY_DEDICATED_EXECUTE_ONLY key unavailable.
 */
#define arch_max_pkey() (boot_cpu_has(X86_FEATURE_OSPKE) ? \
		PKEY_DEDICATED_EXECUTE_ONLY : 1)

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
		return 0;

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

#endif /*_ASM_X86_PKEYS_H */
