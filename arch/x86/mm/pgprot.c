// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <asm/mem_encrypt.h>

static pgprot_t protection_map[16] __ro_after_init = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY,
	[VM_WRITE]					= PAGE_COPY,
	[VM_WRITE | VM_READ]				= PAGE_COPY,
	[VM_EXEC]					= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_READ]				= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_WRITE]				= PAGE_COPY_EXEC,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_COPY_EXEC,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED_EXEC
};

void add_encrypt_protection_map(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(protection_map); i++)
		protection_map[i] = pgprot_encrypted(protection_map[i]);
}

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
