/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MMAN_H__
#define __ASM_MMAN_H__

#include <linux/compiler.h>
#include <linux/types.h>
#include <uapi/asm/mman.h>

static inline unsigned long arch_calc_vm_prot_bits(unsigned long prot,
	unsigned long pkey __always_unused)
{
	unsigned long ret = 0;

	if (system_supports_bti() && (prot & PROT_BTI))
		ret |= VM_ARM64_BTI;

	if (system_supports_mte() && (prot & PROT_MTE))
		ret |= VM_MTE;

	return ret;
}
#define arch_calc_vm_prot_bits(prot, pkey) arch_calc_vm_prot_bits(prot, pkey)

static inline unsigned long arch_calc_vm_flag_bits(unsigned long flags)
{
	/*
	 * Only allow MTE on anonymous mappings as these are guaranteed to be
	 * backed by tags-capable memory. The vm_flags may be overridden by a
	 * filesystem supporting MTE (RAM-based).
	 */
	if (system_supports_mte() && (flags & MAP_ANONYMOUS))
		return VM_MTE_ALLOWED;

	return 0;
}
#define arch_calc_vm_flag_bits(flags) arch_calc_vm_flag_bits(flags)

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

static inline bool arch_validate_flags(unsigned long vm_flags)
{
	if (!system_supports_mte())
		return true;

	/* only allow VM_MTE if VM_MTE_ALLOWED has been set previously */
	return !(vm_flags & VM_MTE) || (vm_flags & VM_MTE_ALLOWED);
}
#define arch_validate_flags(vm_flags) arch_validate_flags(vm_flags)

#endif /* ! __ASM_MMAN_H__ */
