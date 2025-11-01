/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MMAN_H__
#define __ASM_MMAN_H__

#include <uapi/asm/mman.h>

#ifndef BUILD_VDSO
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <linux/types.h>

static inline vm_flags_t arch_calc_vm_prot_bits(unsigned long prot,
	unsigned long pkey)
{
	vm_flags_t ret = 0;

	if (system_supports_bti() && (prot & PROT_BTI))
		ret |= VM_ARM64_BTI;

	if (system_supports_mte() && (prot & PROT_MTE))
		ret |= VM_MTE;

#ifdef CONFIG_ARCH_HAS_PKEYS
	if (system_supports_poe()) {
		ret |= pkey & BIT(0) ? VM_PKEY_BIT0 : 0;
		ret |= pkey & BIT(1) ? VM_PKEY_BIT1 : 0;
		ret |= pkey & BIT(2) ? VM_PKEY_BIT2 : 0;
	}
#endif

	return ret;
}
#define arch_calc_vm_prot_bits(prot, pkey) arch_calc_vm_prot_bits(prot, pkey)

static inline vm_flags_t arch_calc_vm_flag_bits(struct file *file,
						unsigned long flags)
{
	/*
	 * Only allow MTE on anonymous mappings as these are guaranteed to be
	 * backed by tags-capable memory. The vm_flags may be overridden by a
	 * filesystem supporting MTE (RAM-based).
	 */
	if (system_supports_mte()) {
		if (flags & (MAP_ANONYMOUS | MAP_HUGETLB))
			return VM_MTE_ALLOWED;
		if (shmem_file(file) || is_file_hugepages(file))
			return VM_MTE_ALLOWED;
	}

	return 0;
}
#define arch_calc_vm_flag_bits(file, flags) arch_calc_vm_flag_bits(file, flags)

static inline bool arch_validate_prot(unsigned long prot,
	unsigned long addr __always_unused)
{
	unsigned long supported = PROT_READ | PROT_WRITE | PROT_EXEC | PROT_SEM;

	if (system_supports_bti())
		supported |= PROT_BTI;

	if (system_supports_mte())
		supported |= PROT_MTE;

	return (prot & ~supported) == 0;
}
#define arch_validate_prot(prot, addr) arch_validate_prot(prot, addr)

static inline bool arch_validate_flags(vm_flags_t vm_flags)
{
	if (system_supports_mte()) {
		/*
		 * only allow VM_MTE if VM_MTE_ALLOWED has been set
		 * previously
		 */
		if ((vm_flags & VM_MTE) && !(vm_flags & VM_MTE_ALLOWED))
			return false;
	}

	if (system_supports_gcs() && (vm_flags & VM_SHADOW_STACK)) {
		/* An executable GCS isn't a good idea. */
		if (vm_flags & VM_EXEC)
			return false;

		/* The memory management core should prevent this */
		VM_WARN_ON(vm_flags & VM_SHARED);
	}

	return true;

}
#define arch_validate_flags(vm_flags) arch_validate_flags(vm_flags)

#endif /* !BUILD_VDSO */

#endif /* ! __ASM_MMAN_H__ */
