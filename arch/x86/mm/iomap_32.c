// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright © 2008 Ingo Molnar
 */

#include <asm/iomap.h>
#include <asm/memtype.h>
#include <linux/export.h>
#include <linux/highmem.h>

static int is_io_mapping_possible(resource_size_t base, unsigned long size)
{
#if !defined(CONFIG_X86_PAE) && defined(CONFIG_PHYS_ADDR_T_64BIT)
	/* There is no way to map greater than 1 << 32 address without PAE */
	if (base + size > 0x100000000ULL)
		return 0;
#endif
	return 1;
}

int iomap_create_wc(resource_size_t base, unsigned long size, pgprot_t *prot)
{
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_WC;
	int ret;

	if (!is_io_mapping_possible(base, size))
		return -EINVAL;

	ret = memtype_reserve_io(base, base + size, &pcm);
	if (ret)
		return ret;

	*prot = __pgprot(__PAGE_KERNEL | cachemode2protval(pcm));
	/* Filter out unsupported __PAGE_KERNEL* bits: */
	pgprot_val(*prot) &= __default_kernel_pte_mask;

	return 0;
}
EXPORT_SYMBOL_GPL(iomap_create_wc);

void iomap_free(resource_size_t base, unsigned long size)
{
	memtype_free_io(base, base + size);
}
EXPORT_SYMBOL_GPL(iomap_free);

void __iomem *__iomap_local_pfn_prot(unsigned long pfn, pgprot_t prot)
{
	/*
	 * For non-PAT systems, translate non-WB request to UC- just in
	 * case the caller set the PWT bit to prot directly without using
	 * pgprot_writecombine(). UC- translates to uncached if the MTRR
	 * is UC or WC. UC- gets the real intention, of the user, which is
	 * "WC if the MTRR is WC, UC if you can't do that."
	 */
	if (!pat_enabled() && pgprot2cachemode(prot) != _PAGE_CACHE_MODE_WB)
		prot = __pgprot(__PAGE_KERNEL |
				cachemode2protval(_PAGE_CACHE_MODE_UC_MINUS));

	/* Filter out unsupported __PAGE_KERNEL* bits: */
	pgprot_val(prot) &= __default_kernel_pte_mask;

	return (void __force __iomem *)__kmap_local_pfn_prot(pfn, prot);
}
EXPORT_SYMBOL_GPL(__iomap_local_pfn_prot);
