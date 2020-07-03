/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */
#ifndef _ASM_POWERPC_MMAN_H
#define _ASM_POWERPC_MMAN_H

#include <uapi/asm/mman.h>

#ifdef CONFIG_PPC64

#include <asm/cputable.h>
#include <linux/mm.h>
#include <linux/pkeys.h>
#include <asm/cpu_has_feature.h>

#ifdef CONFIG_PPC_MEM_KEYS
static inline unsigned long arch_calc_vm_prot_bits(unsigned long prot,
		unsigned long pkey)
{
	return pkey_to_vmflag_bits(pkey);
}
#define arch_calc_vm_prot_bits(prot, pkey) arch_calc_vm_prot_bits(prot, pkey)

static inline pgprot_t arch_vm_get_page_prot(unsigned long vm_flags)
{
	return __pgprot(vmflag_to_pte_pkey_bits(vm_flags));
}
#define arch_vm_get_page_prot(vm_flags) arch_vm_get_page_prot(vm_flags)
#endif

#endif /* CONFIG_PPC64 */
#endif	/* _ASM_POWERPC_MMAN_H */
