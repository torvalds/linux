#ifndef _ASM_X86_PKEYS_H
#define _ASM_X86_PKEYS_H

#define arch_max_pkey() (boot_cpu_has(X86_FEATURE_OSPKE) ? 16 : 1)

extern int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
		unsigned long init_val);

/*
 * Try to dedicate one of the protection keys to be used as an
 * execute-only protection key.
 */
#define PKEY_DEDICATED_EXECUTE_ONLY 15
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

#endif /*_ASM_X86_PKEYS_H */
