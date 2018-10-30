/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_MMAN_H
#define _ASM_X86_MMAN_H

#define MAP_32BIT	0x40		/* only give out 32bit addresses */

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
/*
 * Take the 4 protection key bits out of the vma->vm_flags
 * value and turn them in to the bits that we can put in
 * to a pte.
 *
 * Only override these if Protection Keys are available
 * (which is only on 64-bit).
 */
#define arch_vm_get_page_prot(vm_flags)	__pgprot(	\
		((vm_flags) & VM_PKEY_BIT0 ? _PAGE_PKEY_BIT0 : 0) |	\
		((vm_flags) & VM_PKEY_BIT1 ? _PAGE_PKEY_BIT1 : 0) |	\
		((vm_flags) & VM_PKEY_BIT2 ? _PAGE_PKEY_BIT2 : 0) |	\
		((vm_flags) & VM_PKEY_BIT3 ? _PAGE_PKEY_BIT3 : 0))

#define arch_calc_vm_prot_bits(prot, key) (		\
		((key) & 0x1 ? VM_PKEY_BIT0 : 0) |      \
		((key) & 0x2 ? VM_PKEY_BIT1 : 0) |      \
		((key) & 0x4 ? VM_PKEY_BIT2 : 0) |      \
		((key) & 0x8 ? VM_PKEY_BIT3 : 0))
#endif

#include <asm-generic/mman.h>

#endif /* _ASM_X86_MMAN_H */
