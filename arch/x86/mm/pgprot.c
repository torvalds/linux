// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/mm.h>
#include <asm/pgtable.h>

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	unsigned long val = pgprot_val(protection_map[vm_flags &
				      (VM_READ|VM_WRITE|VM_EXEC|VM_SHARED)]);

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
	/*
	 * Take the 4 protection key bits out of the vma->vm_flags value and
	 * turn them in to the bits that we can put in to a pte.
	 *
	 * Only override these if Protection Keys are available (which is only
	 * on 64-bit).
	 */
	if (vm_flags & VM_PKEY_BIT0)
		val |= _PAGE_PKEY_BIT0;
	if (vm_flags & VM_PKEY_BIT1)
		val |= _PAGE_PKEY_BIT1;
	if (vm_flags & VM_PKEY_BIT2)
		val |= _PAGE_PKEY_BIT2;
	if (vm_flags & VM_PKEY_BIT3)
		val |= _PAGE_PKEY_BIT3;
#endif

	val = __sme_set(val);
	if (val & _PAGE_PRESENT)
		val &= __supported_pte_mask;
	return __pgprot(val);
}
EXPORT_SYMBOL(vm_get_page_prot);
