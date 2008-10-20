#ifndef _ASM_POWERPC_MMAN_H
#define _ASM_POWERPC_MMAN_H

#include <asm-generic/mman.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define PROT_SAO	0x10		/* Strong Access Ordering */

#define MAP_RENAME      MAP_ANONYMOUS   /* In SunOS terminology */
#define MAP_NORESERVE   0x40            /* don't reserve swap pages */
#define MAP_LOCKED	0x80

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as an executable */

#define MCL_CURRENT     0x2000          /* lock all currently mapped pages */
#define MCL_FUTURE      0x4000          /* lock all additions to address space */

#define MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define MAP_NONBLOCK	0x10000		/* do not block on IO */

#ifdef __KERNEL__
#ifdef CONFIG_PPC64

#include <asm/cputable.h>
#include <linux/mm.h>

/*
 * This file is included by linux/mman.h, so we can't use cacl_vm_prot_bits()
 * here.  How important is the optimization?
 */
static inline unsigned long arch_calc_vm_prot_bits(unsigned long prot)
{
	return (prot & PROT_SAO) ? VM_SAO : 0;
}
#define arch_calc_vm_prot_bits(prot) arch_calc_vm_prot_bits(prot)

static inline pgprot_t arch_vm_get_page_prot(unsigned long vm_flags)
{
	return (vm_flags & VM_SAO) ? __pgprot(_PAGE_SAO) : __pgprot(0);
}
#define arch_vm_get_page_prot(vm_flags) arch_vm_get_page_prot(vm_flags)

static inline int arch_validate_prot(unsigned long prot)
{
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_SEM | PROT_SAO))
		return 0;
	if ((prot & PROT_SAO) && !cpu_has_feature(CPU_FTR_SAO))
		return 0;
	return 1;
}
#define arch_validate_prot(prot) arch_validate_prot(prot)

#endif /* CONFIG_PPC64 */
#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_MMAN_H */
